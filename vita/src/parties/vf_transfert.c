/* Area Play - Transfert Wi-Fi. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Transfert Wi-Fi : Area Play sert une page web (http://<adresse de la console>:8080) ou l'on depose des
 *  fichiers depuis un PC ou un telephone. Uniquement des depots, dans des dossiers prevus :
 *    jaquettes (covers/<ID>), fonds d'ecran (wallpapers), icones des systemes (systems/<nom>), jeux retro (dossier de ROM du systeme), jeux PSP (ux0:pspemu/ISO).
 *  Pas de navigation ni de suppression. Le serveur ne tourne que fenetre ouverte (O l'arrete).
 *  Page : app0:assets/transfert/index.html ; elle demande /info (jeux, systemes) puis envoie chaque fichier
 *  par POST /up?k=<type>&...&n=<nom> (corps = le fichier).
 * ====================================================================== */
#include <psp2/io/devctl.h>

#define TF_PORT 8080
#define TF_BUF (256 * 1024)
enum { TK_COVER, TK_WALL, TK_ICON, TK_ROM, TK_ISO, TK_N };
static const char *tf_kind_names[TK_N] = {"Jaquette", "Fond d'écran", "Icône", "Jeu rétro", "Jeu PSP"};

static int tf_open;                /* fenetre ouverte */
static volatile int tf_run;        /* le serveur doit tourner */
static volatile int tf_ls = -1, tf_cs = -1; /* socket d'ecoute, client en cours */
static SceUID tf_th = -1;
static char tf_ip[20];
static char tf_err[96];            /* serveur impossible (pas de Wi-Fi...) */
static unsigned char *tf_buf;      /* reception (alloue a l'ouverture) */

/* Fichier en cours (ecrit par le fil du serveur, lu par l'affichage). */
static volatile int tf_busy;
static char tf_cur[80];
static volatile long long tf_got, tf_total;
static volatile unsigned long long tf_t0;

/* Derniers fichiers recus (le plus recent en premier). */
#define TF_LOG 5
static char tf_log[TF_LOG][96];
static volatile int tf_log_ok[TF_LOG];
static volatile int tf_log_n, tf_files;

/* A faire par le fil principal : jaquette ou icone a recharger. */
#define TF_Q 32
static struct {
  int kind, idx;
  char id[16], ext[6];
} tf_q[TF_Q];
static volatile int tf_qh, tf_qt;
static volatile int tf_need_rescan;

static void tf_log_add(int ok, const char *fmt, const char *a, const char *b) {
  for (int i = TF_LOG - 1; i > 0; --i) {
    memcpy(tf_log[i], tf_log[i - 1], sizeof(tf_log[0]));
    tf_log_ok[i] = tf_log_ok[i - 1];
  }
  snprintf(tf_log[0], sizeof(tf_log[0]), fmt, a, b);
  tf_log_ok[0] = ok;
  if (tf_log_n < TF_LOG) ++tf_log_n;
}

/* ---- Petits outils ---- */
static int tf_send(int s, const void *b, int n) {
  const char *p = (const char *)b;
  while (n > 0) {
    int r = sceNetSend(s, p, (unsigned)n, 0);
    if (r <= 0) return 0;
    p += r;
    n -= r;
  }
  return 1;
}
static void tf_reply(int s, int code, const char *ctype, const char *body, int len) {
  char h[256];
  if (len < 0) len = (int)strlen(body);
  int n = snprintf(h, sizeof(h),
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\n"
                   "Connection: close\r\n\r\n",
                   code, code == 200 ? "OK" : code == 404 ? "Not Found" : "Error", ctype, len);
  if (tf_send(s, h, n)) tf_send(s, body, len);
}
static void tf_reply_json(int s, int ok, const char *err) {
  char b[200];
  snprintf(b, sizeof(b), ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"%s\"}", err ? err : "");
  tf_reply(s, 200, "application/json; charset=utf-8", b, -1);
}

/* Valeur d'un parametre de l'adresse (?k=cover&n=...), decodee (%XX, +). */
static int tf_param(const char *q, const char *key, char *out, int n) {
  int kl = (int)strlen(key);
  out[0] = 0;
  while (q && *q) {
    if (!strncmp(q, key, kl) && q[kl] == '=') {
      q += kl + 1;
      int o = 0;
      while (*q && *q != '&' && o < n - 1) {
        if (*q == '%' && hexv(q[1]) >= 0 && hexv(q[2]) >= 0) {
          out[o++] = (char)(hexv(q[1]) * 16 + hexv(q[2]));
          q += 3;
        } else {
          out[o++] = *q == '+' ? ' ' : *q;
          ++q;
        }
      }
      out[o] = 0;
      return 1;
    }
    q = strchr(q, '&');
    if (q) ++q;
  }
  return 0;
}

/* Nom de fichier sur : sans dossier, sans caracteres interdits, sans ".." ; "" si rien ne reste. */
static void tf_clean(const char *in, char *out, int n) {
  const char *b = in;
  for (const char *p = in; *p; ++p)
    if (*p == '/' || *p == '\\') b = p + 1;
  int o = 0;
  for (; *b && o < n - 1; ++b) {
    unsigned char c = (unsigned char)*b;
    if (c < 32 || strchr(":*?\"<>|", c)) c = '_';
    out[o++] = (char)c;
  }
  out[o] = 0;
  while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '.')) out[--o] = 0; /* "jeu." ou "jeu " */
  if (!strcmp(out, ".") || !strcmp(out, "..") || out[0] == '.') out[0] = 0;
}
static void tf_mkdirs(const char *path) { /* cree les dossiers parents de path */
  char p[256];
  snprintf(p, sizeof(p), "%s", path);
  for (char *s = strchr(p, '/'); s; s = strchr(s + 1, '/')) {
    *s = 0;
    sceIoMkdir(p, 0777);
    *s = '/';
  }
}
static long long tf_free_space(const char *path) { /* place libre de la carte de path (-1 = inconnue) */
  char dev[8];
  const char *c = strchr(path, ':');
  if (!c || c - path > 5) return -1;
  snprintf(dev, sizeof(dev), "%.*s:", (int)(c - path), path);
  struct {
    SceOff max_size, free_size;
    SceSize cluster;
    void *unk;
  } info;
  memset(&info, 0, sizeof(info));
  if (sceIoDevctl(dev, 0x3001, NULL, 0, &info, sizeof(info)) < 0) return -1;
  return info.free_size;
}
static int tf_is_image(const char *n) {
  return ends_with_ci(n, ".png") || ends_with_ci(n, ".jpg") || ends_with_ci(n, ".jpeg");
}
static int tf_magic_ok(const char *path) { /* PNG ou JPEG pour de vrai */
  unsigned char m[4] = {0};
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return 0;
  sceIoRead(f, m, 4);
  sceIoClose(f);
  return (m[0] == 0x89 && m[1] == 'P' && m[2] == 'N' && m[3] == 'G') || (m[0] == 0xFF && m[1] == 0xD8);
}
static Game *tf_game(const char *id) {
  for (int i = 0; i < app_n; ++i)
    if (!strcmp(all_apps[i].id, id)) return &all_apps[i];
  return NULL;
}
static void tf_queue(int kind, int idx, const char *id, const char *ext) {
  int t = tf_qt, nt = (t + 1) % TF_Q;
  if (nt == tf_qh) return;
  tf_q[t].kind = kind;
  tf_q[t].idx = idx;
  snprintf(tf_q[t].id, sizeof(tf_q[t].id), "%s", id ? id : "");
  snprintf(tf_q[t].ext, sizeof(tf_q[t].ext), "%s", ext ? ext : "");
  __sync_synchronize();
  tf_qt = nt;
}

/* ---- Liste pour la page : jeux, systemes, dossiers ---- */
static int tf_jlen;
static char tf_jbuf[8192];
static void tf_jflush(int s) {
  if (tf_jlen) tf_send(s, tf_jbuf, tf_jlen);
  tf_jlen = 0;
}
static void tf_jraw(int s, const char *t) {
  int l = (int)strlen(t);
  if (tf_jlen + l > (int)sizeof(tf_jbuf)) tf_jflush(s);
  memcpy(tf_jbuf + tf_jlen, t, l);
  tf_jlen += l;
}
static void tf_jstr(int s, const char *t) { /* chaine JSON entre guillemets */
  char e[8];
  tf_jraw(s, "\"");
  for (; *t; ++t) {
    unsigned char c = (unsigned char)*t;
    if (c == '"' || c == '\\') {
      e[0] = '\\';
      e[1] = (char)c;
      e[2] = 0;
    } else if (c < 32) {
      snprintf(e, sizeof(e), "\\u%04x", c);
    } else {
      e[0] = (char)c;
      e[1] = 0;
    }
    tf_jraw(s, e);
  }
  tf_jraw(s, "\"");
}
static void tf_info(int s) {
  static const char h[] = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\n"
                          "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
  char num[64];
  tf_send(s, h, (int)sizeof(h) - 1);
  tf_jlen = 0;
  tf_jraw(s, "{\"lang\":");
  tf_jstr(s, lang_code[0] ? "en" : "fr");
  tf_jraw(s, ",\"version\":");
  tf_jstr(s, VF_VERSION_NUM);
  snprintf(num, sizeof(num), ",\"adr\":%s,\"games\":[", adr_ready() ? "true" : "false");
  tf_jraw(s, num);
  int first = 1;
  for (int i = 0; i < app_n; ++i) {
    const Game *g = &all_apps[i];
    if (!g->id[0] || !strcmp(g->id, own_id)) continue;
    int k = g->kind - KIND_CUSTOM0;
    const char *sys = g->kind >= 0 && g->kind < 3 ? kind_names[g->kind]
                      : k >= 0 && k < MAX_CUSTOM_SYS && SLOT_USED(k) ? slot_name[k] : "";
    tf_jraw(s, first ? "{\"id\":" : ",{\"id\":");
    first = 0;
    tf_jstr(s, g->id);
    tf_jraw(s, ",\"n\":");
    tf_jstr(s, g->name ? g->name : g->id);
    tf_jraw(s, ",\"s\":");
    tf_jstr(s, L(sys));
    tf_jraw(s, "}");
  }
  tf_jraw(s, "],\"icons\":[");
  for (int i = 0; i < n_systems; ++i) {
    snprintf(num, sizeof(num), "%s{\"i\":%d,\"n\":", i ? "," : "", i);
    tf_jraw(s, num);
    tf_jstr(s, L(systems[i].name));
    tf_jraw(s, "}");
  }
  tf_jraw(s, "],\"roms\":[");
  first = 1;
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k) {
    if (!SLOT_USED(k) || !rom_core_of(k)) continue;
    char dir[160];
    if (!rom_dir_of(k, dir, sizeof(dir), NULL)) snprintf(dir, sizeof(dir), ROMS_ROOT "/%s", slot_file[k]);
    snprintf(num, sizeof(num), "%s{\"k\":%d,\"n\":", first ? "" : ",", k);
    first = 0;
    tf_jraw(s, num);
    tf_jstr(s, slot_name[k]);
    tf_jraw(s, ",\"d\":");
    tf_jstr(s, dir);
    tf_jraw(s, "}");
  }
  tf_jraw(s, "]}");
  tf_jflush(s);
}

/* ---- Reception d'un fichier : <dest>.part puis renomme. Retourne 1 si recu en entier. ---- */
static int tf_receive(int s, const char *dest, long long len, int pre, char *err, int en) {
  char part[300];
  snprintf(part, sizeof(part), "%s.part", dest);
  tf_mkdirs(dest);
  long long fr = tf_free_space(dest);
  if (fr >= 0 && fr < len + (1 << 20)) {
    snprintf(err, en, "%s", L("Pas assez de place sur la carte mémoire"));
    return 0;
  }
  SceUID o = sceIoOpen(part, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (o < 0) {
    snprintf(err, en, L("Écriture impossible (%08X)"), (unsigned)o);
    return 0;
  }
  int t = 20 * 1000 * 1000; /* 20 s sans donnees : abandon */
  sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &t, sizeof(t));
  long long got = 0;
  int ok = 1;
  if (pre > 0) {
    if (pre > len) pre = (int)len;
    ok = sceIoWrite(o, tf_buf, pre) == pre;
    got = pre;
  }
  tf_got = got;
  while (ok && got < len && tf_run) {
    long long want = len - got;
    int r = sceNetRecv(s, tf_buf, want < TF_BUF ? (unsigned)want : TF_BUF, 0);
    if (r <= 0) {
      snprintf(err, en, "%s", L("Transfert interrompu"));
      ok = 0;
      break;
    }
    if (sceIoWrite(o, tf_buf, r) != r) {
      snprintf(err, en, "%s", L("Écriture impossible (carte pleine ?)"));
      ok = 0;
    }
    got += r;
    tf_got = got;
  }
  sceIoClose(o);
  if (ok && got < len) {
    snprintf(err, en, "%s", L("Transfert interrompu"));
    ok = 0;
  }
  if (!ok) {
    sceIoRemove(part);
    return 0;
  }
  sceIoRemove(dest);
  if (sceIoRename(part, dest) < 0) {
    sceIoRemove(part);
    snprintf(err, en, "%s", L("Écriture impossible"));
    return 0;
  }
  return 1;
}

/* POST /up : choisit le dossier selon le type, recoit, puis prepare la mise a jour de l'affichage. */
static void tf_upload(int s, const char *q, long long len, int pre) {
  char k[12], name[200], v[64], clean[160], dest[300], err[120] = "";
  int kind = -1, ok = 0, idx = -1;
  tf_param(q, "k", k, sizeof(k));
  tf_param(q, "n", name, sizeof(name));
  static const char *codes[TK_N] = {"cover", "wall", "icon", "rom", "iso"};
  for (int i = 0; i < TK_N; ++i)
    if (!strcmp(k, codes[i])) kind = i;
  tf_clean(name, clean, sizeof(clean));
  Game *g = NULL;
  dest[0] = 0;
  if (kind < 0 || !clean[0] || len <= 0) {
    snprintf(err, sizeof(err), "%s", L("Demande invalide"));
  } else if (kind == TK_COVER || kind == TK_WALL || kind == TK_ICON) {
    if (!tf_is_image(clean) || len > (16 << 20)) snprintf(err, sizeof(err), "%s", L("Image PNG ou JPG attendue (16 Mo au plus)"));
    else if (kind == TK_WALL) snprintf(dest, sizeof(dest), WALL_DIR "/%s", clean);
    else if (kind == TK_COVER) {
      tf_param(q, "id", v, sizeof(v));
      g = tf_game(v);
      if (!g) snprintf(err, sizeof(err), "%s", L("Jeu introuvable"));
      else snprintf(dest, sizeof(dest), COVER_DIR "/%s%s", g->id, ends_with_ci(clean, ".png") ? ".png" : ".jpg");
    } else {
      tf_param(q, "i", v, sizeof(v));
      idx = atoi(v);
      if (!v[0] || idx < 0 || idx >= n_systems || !systems[idx].files[0])
        snprintf(err, sizeof(err), "%s", L("Système introuvable"));
      else
        snprintf(dest, sizeof(dest), SYS_DIR "/%s%s", systems[idx].files[0], ends_with_ci(clean, ".png") ? ".png" : ".jpg");
    }
  } else if (kind == TK_ROM) {
    tf_param(q, "s", v, sizeof(v));
    int sk = atoi(v);
    char dir[160];
    if (!v[0] || sk < 0 || sk >= MAX_CUSTOM_SYS || !SLOT_USED(sk)) snprintf(err, sizeof(err), "%s", L("Système introuvable"));
    else {
      if (!rom_dir_of(sk, dir, sizeof(dir), NULL)) {
        sceIoMkdir(ROMS_ROOT, 0777);
        snprintf(dir, sizeof(dir), ROMS_ROOT "/%s", slot_file[sk]);
      }
      snprintf(dest, sizeof(dest), "%s/%s", dir, clean);
    }
  } else if (kind == TK_ISO) {
    if (!(ends_with_ci(clean, ".iso") || ends_with_ci(clean, ".cso"))) snprintf(err, sizeof(err), "%s", L("Fichier .iso ou .cso attendu"));
    else {
      sceIoMkdir("ux0:pspemu", 0777);
      snprintf(dest, sizeof(dest), PSP_ISO_DIR "/%s", clean);
    }
  }
  if (dest[0]) {
    snprintf(tf_cur, sizeof(tf_cur), "%s", clean);
    tf_total = len;
    tf_got = 0;
    tf_t0 = sceKernelGetProcessTimeWide();
    tf_busy = 1;
    ok = tf_receive(s, dest, len, pre, err, sizeof(err));
    tf_busy = 0;
    if (ok && (kind == TK_COVER || kind == TK_WALL || kind == TK_ICON) && !tf_magic_ok(dest)) {
      sceIoRemove(dest);
      snprintf(err, sizeof(err), "%s", L("Ce fichier n'est pas une image PNG ou JPG"));
      ok = 0;
    }
  }
  if (ok) {
    if (kind == TK_COVER || kind == TK_ICON) {
      const char *x = strrchr(dest, '.');
      tf_queue(kind, idx, g ? g->id : "", x);
    }
    if (kind == TK_ROM || kind == TK_ISO) tf_need_rescan = 1;
    ++tf_files;
    char l[200];
    snprintf(l, sizeof(l), "%s : %s -> %s", k, clean, dest);
    log_line(l);
  }
  tf_log_add(ok, ok ? "%s · %s" : "%s · %s", ok ? L(kind >= 0 ? tf_kind_names[kind] : "") : clean,
               ok ? (g ? g->name : clean) : err);
  tf_reply_json(s, ok, err);
}

/* Une connexion : lit l'entete, repond. */
static void tf_handle(int s) {
  int t = 4 * 1000 * 1000; /* entete : 4 s (le navigateur ouvre parfois une connexion "au cas ou") */
  sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &t, sizeof(t));
  int got = 0, hl = -1;
  char *b = (char *)tf_buf;
  while (got < 8191) {
    int r = sceNetRecv(s, b + got, 8191 - got, 0);
    if (r <= 0) return;
    got += r;
    b[got] = 0;
    char *e = strstr(b, "\r\n\r\n");
    if (e) {
      hl = (int)(e - b) + 4;
      break;
    }
  }
  if (hl < 0) return;
  char method[8] = "", target[512] = "";
  sscanf(b, "%7s %511s", method, target);
  long long clen = 0;
  for (char *p = b; p && p < b + hl; p = strstr(p, "\r\n")) {
    if (*p == '\r') p += 2;
    if (!strncasecmp(p, "Content-Length:", 15)) clen = strtoll(p + 15, NULL, 10);
  }
  int pre = got - hl;
  if (pre > 0) memmove(tf_buf, tf_buf + hl, pre);
  char *q = strchr(target, '?');
  if (q) *q++ = 0;
  if (!strcmp(method, "GET") && (!strcmp(target, "/") || !strcmp(target, "/index.html"))) {
    SceUID f = sceIoOpen("app0:assets/transfert/index.html", SCE_O_RDONLY, 0);
    int n = f >= 0 ? sceIoRead(f, tf_buf, TF_BUF) : 0;
    if (f >= 0) sceIoClose(f);
    if (n > 0) tf_reply(s, 200, "text/html; charset=utf-8", (const char *)tf_buf, n);
    else tf_reply(s, 404, "text/plain", "page manquante", -1);
  } else if (!strcmp(method, "GET") && !strcmp(target, "/info")) {
    tf_info(s);
  } else if (!strcmp(method, "POST") && !strcmp(target, "/up")) {
    tf_upload(s, q ? q : "", clen, pre);
  } else {
    tf_reply(s, 404, "text/plain", "404", -1);
  }
}

static int tf_thread(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  while (tf_run) {
    SceNetSockaddrIn a;
    unsigned int al = sizeof(a);
    int c = sceNetAccept(tf_ls, (SceNetSockaddr *)&a, &al);
    if (c < 0) {
      if (!tf_run) break;
      sceKernelDelayThread(100 * 1000);
      continue;
    }
    tf_cs = c;
    int big = 256 * 1024;
    sceNetSetsockopt(c, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVBUF, &big, sizeof(big));
    tf_handle(c);
    tf_cs = -1;
    sceNetSocketClose(c);
  }
  sceKernelExitDeleteThread(0);
  return 0;
}

/* ---- QR code de l'adresse (versions 1 a 4, correction L, masque 0 : assez pour "http://xxx.xxx.xxx.xxx:8080") ---- */
static unsigned char tf_qr[33][33], tf_qf[33][33]; /* modules noirs, modules de fonction */
static int tf_qn;                                  /* cote (0 = pas de QR) */
static unsigned char gf_mul(unsigned char a, unsigned char b) {
  unsigned r = 0;
  for (int i = 7; i >= 0; --i) {
    r = (r << 1) ^ ((r >> 7) * 0x11D);
    r ^= ((b >> i) & 1) * a;
  }
  return (unsigned char)r;
}
static void qr_fn(int x, int y, int dark) {
  tf_qr[y][x] = (unsigned char)dark;
  tf_qf[y][x] = 1;
}
static void tf_qr_make(const char *txt) {
  static const int data_cw[5] = {0, 19, 34, 55, 80}, ec_cw[5] = {0, 7, 10, 15, 20};
  int len = (int)strlen(txt), ver = 1;
  while (ver <= 4 && 4 + 8 + len * 8 > data_cw[ver] * 8) ++ver;
  tf_qn = 0;
  if (ver > 4) return;
  int n = 17 + 4 * ver, dn = data_cw[ver], en = ec_cw[ver];
  unsigned char cw[100];
  memset(cw, 0, sizeof(cw));
  /* donnees : mode octet (0100), longueur sur 8 bits, octets, fin, remplissage */
  int bit = 0;
#define QPUT(v, nb)                                                    \
  for (int b_ = (nb)-1; b_ >= 0; --b_, ++bit)                          \
    if (((v) >> b_) & 1) cw[bit >> 3] |= (unsigned char)(0x80 >> (bit & 7));
  QPUT(4, 4);
  QPUT(len, 8);
  for (int i = 0; i < len; ++i) QPUT((unsigned char)txt[i], 8);
  int term = dn * 8 - bit < 4 ? dn * 8 - bit : 4;
  bit += term;
  bit = (bit + 7) & ~7;
#undef QPUT
  for (int i = bit / 8, p = 0; i < dn; ++i, p ^= 1) cw[i] = p ? 0x11 : 0xEC;
  /* correction d'erreurs (Reed-Solomon) */
  unsigned char gen[32] = {0}, rem[32] = {0};
  gen[en - 1] = 1;
  for (int i = 0, root = 1; i < en; ++i) {
    for (int j = 0; j < en; ++j) {
      gen[j] = gf_mul(gen[j], (unsigned char)root);
      if (j + 1 < en) gen[j] ^= gen[j + 1];
    }
    root = gf_mul((unsigned char)root, 2);
  }
  for (int i = 0; i < dn; ++i) {
    unsigned char f = cw[i] ^ rem[0];
    memmove(rem, rem + 1, en - 1);
    rem[en - 1] = 0;
    for (int j = 0; j < en; ++j) rem[j] ^= gf_mul(gen[j], f);
  }
  memcpy(cw + dn, rem, en);
  /* motifs fixes */
  memset(tf_qr, 0, sizeof(tf_qr));
  memset(tf_qf, 0, sizeof(tf_qf));
  for (int i = 0; i < n; ++i) {
    qr_fn(6, i, i % 2 == 0);
    qr_fn(i, 6, i % 2 == 0);
  }
  static const int fx[3] = {3, -1, 3}, fy[3] = {3, 3, -1};
  for (int f = 0; f < 3; ++f) {
    int cx = fx[f] < 0 ? n - 4 : 3, cy = fy[f] < 0 ? n - 4 : 3;
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        int x = cx + dx, y = cy + dy, d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
        if (x >= 0 && x < n && y >= 0 && y < n) qr_fn(x, y, d != 2 && d != 4);
      }
  }
  if (ver >= 2) {
    int a = n - 7;
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) {
        int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
        qr_fn(a + dx, a + dy, d != 1);
      }
  }
  /* format : correction L (01), masque 0 */
  int data = 1 << 3 | 0, r = data;
  for (int i = 0; i < 10; ++i) r = (r << 1) ^ ((r >> 9) * 0x537);
  int bits = (data << 10 | r) ^ 0x5412;
  for (int i = 0; i <= 5; ++i) qr_fn(8, i, (bits >> i) & 1);
  qr_fn(8, 7, (bits >> 6) & 1);
  qr_fn(8, 8, (bits >> 7) & 1);
  qr_fn(7, 8, (bits >> 8) & 1);
  for (int i = 9; i < 15; ++i) qr_fn(14 - i, 8, (bits >> i) & 1);
  for (int i = 0; i < 8; ++i) qr_fn(n - 1 - i, 8, (bits >> i) & 1);
  for (int i = 8; i < 15; ++i) qr_fn(8, n - 15 + i, (bits >> i) & 1);
  qr_fn(8, n - 8, 1);
  /* donnees en zigzag, puis masque 0 ((x + y) pair = inverse) */
  int total = (dn + en) * 8, i = 0;
  for (int right = n - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < n; ++vert)
      for (int j = 0; j < 2; ++j) {
        int x = right - j, up = ((right + 1) & 2) == 0, y = up ? n - 1 - vert : vert;
        if (tf_qf[y][x]) continue;
        int dark = i < total ? (cw[i >> 3] >> (7 - (i & 7))) & 1 : 0;
        ++i;
        tf_qr[y][x] = (unsigned char)(dark ^ ((x + y) % 2 == 0));
      }
  }
  tf_qn = n;
}

/* ---- Ouverture / fermeture ---- */
static void tf_stop(void) {
  if (tf_th >= 0) {
    tf_run = 0;
    if (tf_cs >= 0) sceNetSocketAbort(tf_cs, 0);
    if (tf_ls >= 0) sceNetSocketAbort(tf_ls, 0);
    SceUInt tmo = 3 * 1000 * 1000;
    sceKernelWaitThreadEnd(tf_th, NULL, &tmo);
    tf_th = -1;
  }
  if (tf_ls >= 0) sceNetSocketClose(tf_ls);
  tf_ls = -1;
}
static void tf_start(void) {
  tf_open = 1;
  tf_err[0] = 0;
  tf_qn = 0;
  tf_log_n = tf_files = 0;
  tf_busy = 0;
  tf_qh = tf_qt = 0;
  tf_need_rescan = 0;
  net_ip_text(tf_ip, sizeof(tf_ip));
  if (!strcmp(tf_ip, "-")) {
    snprintf(tf_err, sizeof(tf_err), "%s", L("Pas de Wi-Fi : connecte la console à ton réseau."));
    return;
  }
  if (!tf_buf) tf_buf = malloc(TF_BUF + 16);
  int s = tf_buf ? sceNetSocket("ap_transfert", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0) : -1;
  if (s < 0) {
    snprintf(tf_err, sizeof(tf_err), L("Serveur impossible (%08X)"), (unsigned)s);
    return;
  }
  int one = 1;
  sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &one, sizeof(one));
  SceNetSockaddrIn a;
  memset(&a, 0, sizeof(a));
  a.sin_len = sizeof(a);
  a.sin_family = SCE_NET_AF_INET;
  a.sin_port = sceNetHtons(TF_PORT);
  a.sin_addr.s_addr = SCE_NET_INADDR_ANY;
  int r = sceNetBind(s, (SceNetSockaddr *)&a, sizeof(a));
  if (r >= 0) r = sceNetListen(s, 4);
  if (r < 0) {
    sceNetSocketClose(s);
    snprintf(tf_err, sizeof(tf_err), L("Serveur impossible (%08X)"), (unsigned)r);
    return;
  }
  tf_ls = s;
  tf_run = 1;
  tf_th = sceKernelCreateThread("ap_transfert", tf_thread, 0x10000100, 0x10000, 0, 0, NULL);
  if (tf_th < 0 || sceKernelStartThread(tf_th, 0, NULL) < 0) {
    tf_th = -1;
    tf_stop();
    snprintf(tf_err, sizeof(tf_err), "%s", L("Serveur impossible"));
    return;
  }
  char url[48];
  snprintf(url, sizeof(url), "http://%s:%d", tf_ip, TF_PORT);
  tf_qr_make(url);
  log_line("transfert wi-fi : serveur ouvert");
}

/* Fil principal : jaquettes et icones recues a recharger. */
static void tf_poll(void) {
  while (tf_qh != tf_qt) {
    int h = tf_qh;
    if (tf_q[h].kind == TK_COVER) {
      Game *g = tf_game(tf_q[h].id);
      if (g) {
        static const char *exts[3] = {".png", ".jpg", ".jpeg"};
        char fn[48];
        for (int e = 0; e < 3; ++e) { /* l'autre format ne doit pas passer devant */
          if (!strcmp(exts[e], tf_q[h].ext)) continue;
          snprintf(fn, sizeof(fn), COVER_DIR "/%s%s", g->id, exts[e]);
          sceIoRemove(fn);
          snprintf(fn, sizeof(fn), "%s%s", g->id, exts[e]);
          for (int i = 0; i < cover_set.n; ++i)
            if (!strcasecmp(cover_set.names[i], fn)) {
              memmove(cover_set.names[i], cover_set.names[--cover_set.n], sizeof(cover_set.names[0]));
              break;
            }
        }
        snprintf(fn, sizeof(fn), "%s%s", g->id, tf_q[h].ext);
        nameset_add(&cover_set, fn);
        custom_set(g->id, NULL); /* un ancien choix manuel ne masque pas la nouvelle jaquette */
        custom_save();
        g->cover_failed = 0;
        reload_cover(g);
      }
    } else if (tf_q[h].kind == TK_ICON && tf_q[h].idx >= 0 && tf_q[h].idx < n_systems) {
      System *sy = &systems[tf_q[h].idx];
      static const char *exts[3] = {".png", ".jpg", ".jpeg"};
      char fn[80], key[24];
      for (int e = 0; e < 3; ++e)
        if (strcmp(exts[e], tf_q[h].ext)) {
          snprintf(fn, sizeof(fn), SYS_DIR "/%s%s", sy->files[0], exts[e]);
          sceIoRemove(fn);
        }
      snprintf(key, sizeof(key), "sys:%s", sy->tag);
      custom_set(key, NULL);
      custom_save();
      reload_system_icon(sy);
    }
    tf_qh = (h + 1) % TF_Q;
  }
}

static void tf_close(void) {
  tf_stop();
  tf_poll();
  tf_open = 0;
  if (tf_need_rescan) rescan_library();
  if (tf_files) {
    snprintf(toast_text, sizeof(toast_text), L(tf_files > 1 ? "%d fichiers reçus" : "%d fichier reçu"), tf_files);
    toast_time = 3.0f;
  }
  log_line("transfert wi-fi : serveur ferme");
}

static void tf_input(unsigned int pressed) {
  if (pressed & SCE_CTRL_CIRCLE) tf_close();
}

static void tf_size(long long b, char *out, int n) {
  if (b >= 1024LL * 1024 * 1024) snprintf(out, n, L("%.2f Go"), b / (1024.0 * 1024.0 * 1024.0));
  else if (b >= 1024 * 1024) snprintf(out, n, L("%.1f Mo"), b / (1024.0 * 1024.0));
  else snprintf(out, n, L("%d Ko"), (int)((b + 1023) / 1024));
}

static void tf_draw(void) {
  sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT); /* ecran allume, pas de veille : le Wi-Fi reste actif */
  tf_poll();
  float w = 780, h = 448, x = (SCREEN_W - w) / 2, y = 40;
  modal_panel(x, y, w, h, 200);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 44, FS_SECTION, T.text, "Transfert Wi-Fi");
  if (tf_err[0]) {
    draw_wrapped(ST_REG, (int)x + 28, (int)y + 100, FS_BODY, (int)w - 56, 3, 26, T.text_soft, tf_err);
    draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted, "O Fermer");
    return;
  }
  float lw = w - 56 - 230; /* colonne de gauche ; QR a droite */
  char url[48], line[160];
  snprintf(url, sizeof(url), "http://%s:%d", tf_ip, TF_PORT);
  draw_text(ST_REG, (int)x + 28, (int)y + 84, FS_BODY, T.muted, "Sur ton PC ou ton téléphone, ouvre :");
  draw_text(ST_SEMI, (int)x + 28, (int)y + 122, FS_SECTION, T.acc, url);
  draw_wrapped(ST_REG, (int)x + 28, (int)y + 154, FS_SMALL, (int)lw, 2, 20, T.muted,
               "ou scanne le QR code. Même réseau Wi-Fi que la console.");
  /* fichier en cours */
  float cy = y + 214;
  if (tf_busy) {
    long long got = tf_got, tot = tf_total;
    char a[24], b[24], sp[24];
    fit_text(ST_SEMI, FS_BODY, tf_cur, (int)lw, line, sizeof(line));
    draw_text(ST_SEMI, (int)x + 28, (int)cy, FS_BODY, T.text, line);
    float k = tot > 0 ? (float)((double)got / (double)tot) : 0.0f;
    round_rect(x + 28, cy + 14, lw, 8, 4, T.card_border);
    if (k > 0.01f) round_rect(x + 28, cy + 14, lw * (k > 1 ? 1 : k), 8, 4, T.acc);
    double secs = (sceKernelGetProcessTimeWide() - tf_t0) / 1000000.0;
    double bps = secs > 0.5 ? got / secs : 0;
    tf_size(got, a, sizeof(a));
    tf_size(tot, b, sizeof(b));
    tf_size((long long)bps, sp, sizeof(sp));
    if (bps > 1024 && tot > got) {
      int left = (int)((tot - got) / bps);
      if (left >= 60) snprintf(line, sizeof(line), L("%s / %s · %s/s · encore %d min"), a, b, sp, (left + 59) / 60);
      else snprintf(line, sizeof(line), L("%s / %s · %s/s · encore %d s"), a, b, sp, left);
    } else {
      snprintf(line, sizeof(line), "%s / %s", a, b);
    }
    draw_text(ST_REG, (int)x + 28, (int)cy + 44, FS_SMALL, T.text_soft, line);
  } else {
    draw_text(ST_REG, (int)x + 28, (int)cy, FS_BODY, T.text_soft, "En attente de fichiers...");
  }
  /* derniers fichiers */
  if (tf_log_n > 0) {
    float ly = y + 290;
    snprintf(line, sizeof(line), L(tf_files > 1 ? "Reçus : %d fichiers" : "Reçus : %d fichier"), tf_files);
    draw_text(ST_SEMI, (int)x + 28, (int)ly, FS_SMALL, T.muted, line);
    for (int i = 0; i < tf_log_n; ++i) {
      int ok = tf_log_ok[i];
      float yy = ly + 26 + i * 24;
      fill_rect(x + 30, yy - 9, 6, 6, ok ? T.acc : C(230, 90, 80, 255));
      fit_text(ST_REG, FS_SMALL, tf_log[i], (int)lw - 20, line, sizeof(line));
      draw_text(ST_REG, (int)x + 46, (int)yy, FS_SMALL, ok ? T.text_soft : C(240, 130, 120, 255), line);
    }
  }
  /* QR code (fond blanc, marge de 4 modules) */
  if (tf_qn) {
    float qs = 200, qx = x + w - 28 - qs, qy = y + 70;
    int px = (int)(qs / (tf_qn + 8));
    float side = px * (tf_qn + 8), ox = qx + (qs - side) / 2, oy = qy + (qs - side) / 2;
    round_rect(ox, oy, side, side, 8, C(255, 255, 255, 255));
    for (int yy = 0; yy < tf_qn; ++yy)
      for (int xx = 0; xx < tf_qn; ++xx)
        if (tf_qr[yy][xx]) fill_rect(ox + (xx + 4) * px, oy + (yy + 4) * px, px, px, C(0, 0, 0, 255));
  }
  draw_wrapped(ST_REG, (int)(x + w - 28 - 200), (int)y + 300, FS_SMALL, 200, 3, 20, T.muted,
               "Jaquettes, fonds d'écran, icônes, jeux rétro et jeux PSP.");
  draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted, "O Arrêter le transfert");
}
