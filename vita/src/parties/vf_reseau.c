/* Area Play - Telechargement des jaquettes et des fonds. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Telechargement des jaquettes (depot hexflow-covers, comme RetroFlow)
 * ====================================================================== */
#define DL_MAX MAX_TOTAL
static const char *dl_hosts[3] = {
    "https://raw.githubusercontent.com/jimbob4000/hexflow-covers/main/Covers/%s/%s.png",
    "https://cdn.jsdelivr.net/gh/jimbob4000/hexflow-covers@main/Covers/%s/%s.png",
    "https://github.com/jimbob4000/hexflow-covers/raw/main/Covers/%s/%s.png"};
static int dl_host;
typedef struct {
  char id[16];
  char title[72];
  int kind;
  int ok;         /* jaquette : 0 en attente, 1 telecharge, -1 introuvable, -2 erreur reseau */
  int want_cover; /* 1 = telecharger la jaquette */
  int sys;        /* 0 aucun, sinon console des images cherchees par nom (DB_PSP, DB_PS1, DB_SNES...) */
  char file[100]; /* ROM : nom du fichier sans extension, essaye tel quel avant la recherche par nom */
  char alias[64]; /* nom donne par l'utilisateur ("" = pas renomme) : cherche avant le nom d'origine */
} DlJob;
static DlJob dl_jobs[DL_MAX];
static volatile int dl_n, dl_done, dl_state, dl_cancel, dl_err, dl_phase;
static int dl_open, dl_applied, dl_ok_count, dl_miss_count, dl_fail_count;
#define DLQ_MAX (DL_MAX * 2)
static int dl_bgq[DLQ_MAX]; /* images cherchees par nom : indice du jeu + 65536 * type (0 = fond, 1 = jaquette) */
static volatile int dl_q_ok[DLQ_MAX]; /* 0 en attente, 1 recue, -1 introuvable, -2 erreur, -3 inutile ou annulee */
static volatile int dl_bg_n, dl_bg_done;
static int dl_bg_applied, dl_art_ok[2], dl_art_miss[2], dl_art_fail[2], dl_want_covers;
#define QJ(q) (dl_bgq[q] % 65536) /* 65536 > DL_MAX : l'indice du jeu ne deborde jamais sur le type */
#define QA(q) (dl_bgq[q] / 65536)
static int net_ok;
#define DL_BUF_SIZE (1024 * 1024) /* une image telechargee tient dans ce tampon */

/* Image recue utilisable ? Signature PNG, en-tete IHDR coherent (2048 px max), fichier complet (IEND).
 * Important quand la verification du certificat a du etre assouplie : rien d'autre n'est ecrit. */
static int png_ok(const unsigned char *b, int len) {
  static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (len < 8 + 25 + 12 || len >= DL_BUF_SIZE || memcmp(b, sig, 8) != 0) return 0; /* plein = tronque */
  if (rd32(b + 8) != 0x0D000000u || memcmp(b + 12, "IHDR", 4) != 0) return 0; /* longueur 13 (gros-boutiste) */
  unsigned w = ((unsigned)b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
  unsigned h = ((unsigned)b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
  if (w == 0 || h == 0 || w > 2048 || h > 2048) return 0;
  return memcmp(b + len - 8, "IEND", 4) == 0;
}
static unsigned char dl_buf[DL_BUF_SIZE];
static void (*http_sink)(const unsigned char *, int);

extern int vf_sslinit(unsigned int) __asm__("sceSslInit");
/* Reseau de base (etat de la connexion, adresse IP, signal) : sans HTTPS, une seule fois. */
static int netctl_ok; /* 0 = pas essaye, 1 = pret, -1 = echec */
static int net_base_start(void) {
  if (netctl_ok) return netctl_ok > 0;
  netctl_ok = -1;
  sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  SceNetInitParam np;
  memset(&np, 0, sizeof(np));
  np.size = 1024 * 1024;
  np.memory = malloc(np.size);
  if (!np.memory) return 0;
  int r = sceNetInit(&np);
  if (r < 0) {
    char l[64];
    snprintf(l, sizeof(l), "reseau : sceNetInit=%08X", (unsigned)r);
    log_line(l);
    free(np.memory);
    return 0;
  }
  sceNetCtlInit();
  netctl_ok = 1;
  return 1;
}

static int net_start(void) {
  log_mem("telechargement");
  if (net_ok) return 1;
  if (!net_base_start()) return 0;
  sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
  int r = vf_sslinit(1024 * 1024); /* HTTPS : le module SSL doit etre initialise (erreur 80435001 sinon) */
  if (r < 0) {
    char l[64];
    snprintf(l, sizeof(l), "telechargement : sceSslInit=%08X", (unsigned)r);
    log_line(l);
  }
  r = sceHttpInit(1024 * 1024);
  if (r < 0) {
    char l[64];
    snprintf(l, sizeof(l), "telechargement : sceHttpInit=%08X", (unsigned)r);
    log_line(l);
    return 0;
  }
  net_ok = 1;
  {
    SceDateTime d;
    char l[64];
    if (sceRtcGetCurrentClockLocalTime(&d) >= 0) {
      snprintf(l, sizeof(l), "telechargement : horloge de la console %04d-%02d-%02d", d.year, d.month, d.day);
      log_line(l);
    }
  }
  return 1;
}

/* Verification du certificat du serveur : la Vita ne connait pas toujours l'autorite de GitHub
 * (erreur 80431075). relax : 0 = verification complete, 1 = sans controle de l'autorite,
 * 2 = sans verification du certificat (les images recues sont de toute facon controlees : signature PNG). */
extern int vf_https_dis_tpl(int, unsigned int) __asm__("sceHttpsDisableOption2");
extern int vf_https_dis_req(int, unsigned int) __asm__("sceHttpsDisableOption");
extern int vf_https_err(int, int *, unsigned int *) __asm__("sceHttpsGetSslError");
extern int vf_https_cb(int, int (*)(unsigned int, void *const *, int, void *), void *) __asm__("sceHttpsSetSslCallback");
static int ssl_accept_all(unsigned int verify, void *const *certs, int n, void *arg) { /* certificat inconnu de la Vita : on continue */
  (void)verify; (void)certs; (void)n; (void)arg;
  return 0;
}
static int ssl_relax;
static char ssl_dbg[200];

/* Telecharge une URL dans buf (cap octets). Retourne le code HTTP (200...), ou un code d'erreur negatif.
 * Plusieurs fils peuvent l'appeler en meme temps, chacun avec son tampon (sans http_sink). */
static int http_get_buf(const char *url, unsigned char *buf, int cap, int *len_out) {
  *len_out = 0;
  int tpl = sceHttpCreateTemplate(VF_DATA_NAME "/2.2", SCE_HTTP_VERSION_1_1, 1);
  if (tpl < 0) return tpl;
  int result = -1;
  unsigned int ssl_flags = ssl_relax == 1 ? 0x10u : (ssl_relax >= 2 ? 0x1Fu : 0u);
  int dis_t = ssl_flags ? vf_https_dis_tpl(tpl, ssl_flags) : 0, dis_r = 0;
  int cbr = ssl_relax >= 1 ? vf_https_cb(tpl, ssl_accept_all, NULL) : 0;
  ssl_dbg[0] = 0;
  int conn = sceHttpCreateConnectionWithURL(tpl, url, 0);
  if (conn >= 0) {
    int req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
    if (req >= 0) {
      if (ssl_flags) dis_r = vf_https_dis_req(req, ssl_flags);
      int r = sceHttpSendRequest(req, NULL, 0);
      if (r < 0) {
        result = r;
        int en = 0;
        unsigned int det = 0;
        int ge = vf_https_err(req, &en, &det);
        snprintf(ssl_dbg, sizeof(ssl_dbg), "send=%08X ssl(get=%08X err=%08X detail=%08X) disable(tpl=%08X req=%08X flags=%02X) cb=%08X",
                 (unsigned)r, (unsigned)ge, (unsigned)en, det, (unsigned)dis_t, (unsigned)dis_r, ssl_flags, (unsigned)cbr);
      } else {
        int status = 0;
        sceHttpGetStatusCode(req, &status);
        result = status;
        if (status == 200) {
          int len = 0, n;
          if (http_sink) { /* flux : liste de fichiers trop grande pour la memoire */
            static unsigned char tmp[4096];
            while ((n = sceHttpReadData(req, tmp, sizeof(tmp))) > 0) {
              http_sink(tmp, n);
              len += n;
            }
          } else {
            while (len < cap && (n = sceHttpReadData(req, buf + len, cap - len)) > 0) len += n;
          }
          *len_out = len;
        }
      }
      sceHttpDeleteRequest(req);
    } else {
      result = req;
    }
    sceHttpDeleteConnection(conn);
  } else {
    result = conn;
  }
  sceHttpDeleteTemplate(tpl);
  return result;
}

static int http_get(const char *url, int *len_out) { return http_get_buf(url, dl_buf, (int)sizeof(dl_buf), len_out); }

/* Essaie la verification complete, puis, si le certificat est refuse, des niveaux moins stricts. */
static int http_get_auto(const char *url, int *len_out) {
  int st = http_get(url, len_out);
  while ((unsigned)st == 0x80431075u && ssl_relax < 2) {
    ++ssl_relax;
    char l[96];
    snprintf(l, sizeof(l), "telechargement : certificat refuse, niveau de verification %d", ssl_relax);
    log_line(l);
    st = http_get(url, len_out);
  }
  return st;
}


/* ---- Images trouvees par le nom du jeu : depot libretro-thumbnails (Named_Snaps = fonds, Named_Boxarts = jaquettes).
 * Systeme d'images : 1 = PSP, 2 = PS1, puis les consoles des ROM (d'apres le coeur RetroArch et l'extension). ---- */
enum {
  DB_PSP = 1, DB_PS1, DB_SNES, DB_NES, DB_GB, DB_GBC, DB_GBA, DB_MD, DB_SMS, DB_GG, DB_MCD, DB_32X, DB_PCE, DB_PCECD,
  DB_NGP, DB_WS, DB_A2600, DB_A7800, DB_LYNX, DB_VB, DB_FBNEO, DB_MAME, DB_ZX, DB_MSX, DB_C64, DB_NEOCD,
  DB_VECTREX, DB_A5200, DB_N64, DB_DC, DB_NDS, DB_3DS, DB_N
};
static const char *bg_db[DB_N] = {
    NULL, "Sony - PlayStation Portable", "Sony - PlayStation", "Nintendo - Super Nintendo Entertainment System",
    "Nintendo - Nintendo Entertainment System", "Nintendo - Game Boy", "Nintendo - Game Boy Color",
    "Nintendo - Game Boy Advance", "Sega - Mega Drive - Genesis", "Sega - Master System - Mark III", "Sega - Game Gear",
    "Sega - Mega-CD - Sega CD", "Sega - 32X", "NEC - PC Engine - TurboGrafx 16", "NEC - PC Engine CD - TurboGrafx-CD",
    "SNK - Neo Geo Pocket Color", "Bandai - WonderSwan Color", "Atari - 2600", "Atari - 7800", "Atari - Lynx",
    "Nintendo - Virtual Boy", "FBNeo - Arcade Games", "MAME", "Sinclair - ZX Spectrum", "Microsoft - MSX",
    "Commodore - 64", "SNK - Neo Geo CD", "GCE - Vectrex", "Atari - 5200", "Nintendo - Nintendo 64",
    "Sega - Dreamcast", "Nintendo - Nintendo DS", "Nintendo - Nintendo 3DS"};
static char bg_folder[DB_N][96]; /* noms ci-dessus, encodes pour une adresse web (espaces : %20) */

static void url_enc(const char *s, char *out, int n) {
  int o = 0;
  for (; *s && o < n - 4; ++s) {
    unsigned char ch = (unsigned char)*s;
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out[o++] = (char)ch;
    else o += snprintf(out + o, n - o, "%%%02X", ch);
  }
  out[o] = 0;
}

/* Console d'une ROM, pour ses images : coeur choisi pour le systeme, precise par l'extension du fichier. */
static int rom_db_of(const Game *g) {
  const char *code = rom_core_of(g->rom - 1);
  if (!code) return 0;
  const char *core = core_name(code);
  const char *p = g->src_path;
#define CORE(x) (strncmp(core, x, strlen(x)) == 0)
#define EXT(x) ends_with_ci(p, x)
  if (CORE("snes9x") || CORE("bsnes") || CORE("mednafen_supafaust") || CORE("supafaust") || CORE("chimerasnes")) return DB_SNES;
  if (CORE("fceumm") || CORE("nestopia") || CORE("quicknes")) return DB_NES;
  if (CORE("mgba") || CORE("gpsp") || CORE("vba") || CORE("meteor")) return EXT(".gbc") ? DB_GBC : EXT(".gb") ? DB_GB : DB_GBA;
  if (CORE("gambatte") || CORE("gearboy") || CORE("tgbdual") || CORE("sameboy")) return EXT(".gbc") ? DB_GBC : DB_GB;
  if (CORE("genesis_plus_gx") || CORE("picodrive"))
    return EXT(".sms") ? DB_SMS : EXT(".gg") ? DB_GG : EXT(".32x") ? DB_32X
           : (EXT(".cue") || EXT(".chd") || EXT(".iso") || EXT(".m3u")) ? DB_MCD : DB_MD;
  if (CORE("pcsx")) return DB_PS1;
  if (CORE("mednafen_pce")) return (EXT(".cue") || EXT(".chd") || EXT(".m3u")) ? DB_PCECD : DB_PCE;
  if (CORE("mednafen_ngp") || CORE("race")) return DB_NGP;
  if (CORE("mednafen_wswan")) return DB_WS;
  if (CORE("stella")) return DB_A2600;
  if (CORE("DEDALOX64") || CORE("mupen64") || CORE("parallel_n64")) return DB_N64;
  if (CORE("FLYCASTDC") || CORE("flycast")) return DB_DC;
  if (CORE("DSVITA000") || CORE("melonds") || CORE("desmume")) return DB_NDS;
  if (CORE("AZAHARPSV") || CORE("citra")) return DB_3DS;
  if (CORE("neocd")) return DB_NEOCD;
  if (CORE("vecx")) return DB_VECTREX;
  if (CORE("atari800")) return DB_A5200;
  if (CORE("prosystem")) return DB_A7800;
  if (CORE("handy") || CORE("mednafen_lynx")) return DB_LYNX;
  if (CORE("mednafen_vb")) return DB_VB;
  if (CORE("fbalpha") || CORE("fbneo")) return DB_FBNEO;
  if (CORE("mame")) return DB_MAME;
  if (CORE("fuse")) return DB_ZX;
  if (CORE("bluemsx") || CORE("fmsx")) return DB_MSX;
  if (CORE("frodo") || CORE("vice")) return DB_C64;
#undef CORE
#undef EXT
  return 0;
}
static char bgj_href[DLQ_MAX][150];
static int bgj_score[DLQ_MAX];
#define NQ 6 /* noms essayes par jeu (voir name_queries) */
static char (*jq_norm)[NQ][72], (*jq_base)[NQ][72]; /* DL_MAX jeux, alloues au premier telechargement */
static unsigned char jq_n[DL_MAX];
static int ls_sys, ls_art, ls_state, ls_vl;
static char ls_val[220];

/* Nom "comparable" : minuscules, lettres et chiffres seulement, sans ce qui est entre () ou [], sans le mot "the". */
static void norm_title(const char *in, char *out, int outsz) {
  int o = 0, depth = 0, wl = 0;
  char word[48];
  for (const char *c = in;; ++c) {
    unsigned char ch = (unsigned char)*c;
    if (ch == '(' || ch == '[') { ++depth; ch = ' '; }
    else if (ch == ')' || ch == ']') { if (depth > 0) --depth; ch = ' '; }
    int alnum = depth == 0 && ch < 128 && isalnum(ch);
    if (alnum) {
      if (wl < (int)sizeof(word) - 1) word[wl++] = (char)tolower(ch);
    } else if (wl > 0 || ch == 0) {
      word[wl] = 0;
      /* chiffres romains = chiffres (Bloody Roar II = Bloody Roar 2) ; pas V ni X, trop ambigus */
      static const char *rom[7][2] = {{"ii", "2"}, {"iii", "3"}, {"iv", "4"}, {"vi", "6"}, {"vii", "7"}, {"viii", "8"}, {"ix", "9"}};
      for (int r = 0; r < 7; ++r)
        if (strcmp(word, rom[r][0]) == 0) {
          snprintf(word, sizeof(word), "%s", rom[r][1]);
          wl = (int)strlen(word);
          break;
        }
      if (wl > 0 && strcmp(word, "the") != 0)
        for (int k = 0; k < wl && o < outsz - 1; ++k) out[o++] = word[k];
      wl = 0;
    }
    if (ch == 0) break;
  }
  out[o] = 0;
}
/* Nom proche (faute de frappe) : memes nombres (Sonic 2 n'est pas Sonic 3), meme premiere lettre, et au plus
 * 1 lettre differente (noms de 6 a 11 lettres) ou 2 (12 et plus). Retourne la distance, ou -1. */
static int name_close(const char *a, const char *b) {
  int la = (int)strlen(a), lb = (int)strlen(b), maxd = lb >= 12 ? 2 : lb >= 6 ? 1 : 0;
  if (!maxd || la - lb > maxd || lb - la > maxd || a[0] != b[0]) return -1;
  char da[16], db[16];
  int na = 0, nb = 0;
  for (const char *p = a; *p && na < 15; ++p)
    if (isdigit((unsigned char)*p)) da[na++] = *p;
  for (const char *p = b; *p && nb < 15; ++p)
    if (isdigit((unsigned char)*p)) db[nb++] = *p;
  if (na != nb || memcmp(da, db, na) != 0) return -1;
  unsigned char row[80], prev[80]; /* distance d'edition (lettres ajoutees, retirees ou changees) */
  if (la >= 80 || lb >= 80) return -1;
  for (int j = 0; j <= lb; ++j) prev[j] = (unsigned char)j;
  for (int i = 1; i <= la; ++i) {
    row[0] = (unsigned char)i;
    int best = row[0];
    for (int j = 1; j <= lb; ++j) {
      int v = prev[j - 1] + (a[i - 1] != b[j - 1]);
      if (prev[j] + 1 < v) v = prev[j] + 1;
      if (row[j - 1] + 1 < v) v = row[j - 1] + 1;
      row[j] = (unsigned char)(v > 250 ? 250 : v);
      if (v < best) best = v;
    }
    if (best > maxd) return -1; /* deja trop loin */
    memcpy(prev, row, (size_t)lb + 1);
  }
  return prev[lb] <= maxd ? prev[lb] : -1;
}

/* Titre sans sous-titre (avant " - "), normalise : "Bloody Roar 2 - Bringer of the New Age" -> "bloodyroar2". */
static void norm_base(const char *in, char *out, int outsz) {
  char t[160];
  snprintf(t, sizeof(t), "%s", in);
  char *sub = strstr(t, " - ");
  if (sub) *sub = 0;
  norm_title(t, out, outsz);
}

/* "Luigis-Mansion-Europe-En-Fr-De-Es-It" -> "Luigis Mansion" : tirets et _ en espaces, regions et langues
 * de fin retirees (noms de fichiers sans parentheses). */
static void name_loose(const char *in, char *out, int outsz) {
  static const char *tail[] = {"europe", "usa", "japan", "france", "world", "asia", "australia", "germany", "spain",
                               "italy", "uk", "korea", "pal", "ntsc", "psx", "en", "fr", "de", "es", "it", "nl", "sv",
                               "no", "da", "pt", "ja", "e", "u", "j"};
  char t[160];
  snprintf(t, sizeof(t), "%s", in);
  for (char *p = t; *p; ++p)
    if (*p == '-' || *p == '_') *p = ' ';
  for (;;) { /* retire le dernier mot tant que c'est une region ou une langue */
    size_t l = strlen(t);
    while (l > 0 && t[l - 1] == ' ') t[--l] = 0;
    char *w = strrchr(t, ' ');
    if (!w) break;
    char word[16];
    int n = 0;
    for (const char *p = w + 1; *p && n < 15; ++p)
      if (!strchr(",.()[]", *p)) word[n++] = (char)tolower((unsigned char)*p);
    word[n] = 0;
    int hit = 0;
    for (size_t k = 0; k < sizeof(tail) / sizeof(tail[0]) && !hit; ++k) hit = strcmp(word, tail[k]) == 0;
    if (!hit) break;
    *w = 0;
  }
  /* espaces multiples en un seul */
  int o = 0;
  for (const char *p = t; *p && o < outsz - 1; ++p)
    if (*p != ' ' || (o > 0 && out[o - 1] != ' ')) out[o++] = *p;
  out[o] = 0;
}

/* Noms a essayer pour un jeu, dans l'ordre : nom donne, nom d'origine, nom du fichier, puis les memes
 * sans tirets ni regions. Normalises, sans doublons. Retourne le nombre (NQ au plus). */
static int name_queries(const char *alias, const char *title, const char *file, char norm[NQ][72], char base[NQ][72]) {
  char src[6][160];
  const char *in[3] = {alias, title, file};
  int m = 0, out = 0;
  for (int k = 0; k < 3; ++k)
    if (in[k] && in[k][0]) snprintf(src[m++], sizeof(src[0]), "%s", in[k]);
  for (int k = 0, n = m; k < n; ++k) name_loose(src[k], src[m++], sizeof(src[0]));
  for (int i = 0; i < m && out < NQ; ++i) {
    char nn[72];
    norm_title(src[i], nn, sizeof(nn));
    if (!nn[0]) continue;
    int dup = 0;
    for (int k = 0; k < out && !dup; ++k) dup = strcmp(norm[k], nn) == 0;
    if (dup) continue;
    memcpy(norm[out], nn, sizeof(nn));
    norm_base(src[i], base[out], 72);
    ++out;
  }
  return out;
}

/* Region d'une jaquette selon la region choisie (Reglages) : 12 = la meilleure. Toutes les regions entre
 * parentheses comptent ("(USA, Europe)" vaut Europe) ; demos, prototypes et compilations passent apres. */
static int region_score(const char *name) {
  static const char *tok[12] = {"Europe", "France", "USA", "Japan", "World", "Asia",
                                "UK", "Germany", "Spain", "Italy", "Australia", "Korea"};
  static const unsigned char val[4][12] = {{6, 5, 2, 1, 4, 1, 5, 3, 3, 3, 3, 1},  /* Europe */
                                           {5, 6, 2, 1, 4, 1, 3, 3, 3, 3, 2, 1},  /* France */
                                           {3, 2, 6, 1, 5, 1, 3, 2, 2, 2, 3, 1},  /* USA */
                                           {2, 2, 2, 6, 5, 4, 2, 2, 2, 2, 2, 3}}; /* Japon */
  int reg = opt_region >= 0 && opt_region < 4 ? opt_region : 0, best = 1;
  for (const char *p = strchr(name, '('); p; p = strchr(p + 1, '(')) {
    const char *e = strchr(p, ')');
    if (!e) break;
    for (int t = 0; t < 12; ++t) {
      size_t tl = strlen(tok[t]);
      for (const char *s = p + 1; s + tl <= e; ++s)
        if (strncmp(s, tok[t], tl) == 0 && !isalpha((unsigned char)s[tl]) && !isalpha((unsigned char)s[-1])) {
          if (val[reg][t] > best) best = val[reg][t];
          break;
        }
    }
  }
  int pen = strstr(name, "(Demo") || strstr(name, "(Beta") || strstr(name, "(Proto") || strstr(name, "(Sample") ||
            strstr(name, "(Kiosk") || strstr(name, "Virtual Console") || strstr(name, "Collection)");
  int s = best * 2 - pen * 3;
  return s < 0 ? 0 : s;
}

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
/* Lien d'une liste du serveur -> nom de l'image (sans ".png"). 0 si ce n'est pas une image. */
static int href_name(const char *val, char *name, int nsz) {
  int vl = (int)strlen(val);
  if (vl < 6 || vl >= 150 || val[0] == '?' || val[0] == '/' || strstr(val, "..")) return 0;
  if (strcasecmp(val + vl - 4, ".png") != 0) return 0;
  int o = 0;
  for (int i = 0; i < vl - 4 && o < nsz - 1; ++i) {
    if (val[i] == '%' && hexv(val[i + 1]) >= 0 && hexv(val[i + 2]) >= 0) {
      name[o++] = (char)(hexv(val[i + 1]) * 16 + hexv(val[i + 2]));
      i += 2;
    } else if (strncmp(val + i, "&amp;", 5) == 0) {
      name[o++] = '&';
      i += 4;
    } else {
      name[o++] = val[i];
    }
  }
  name[o] = 0;
  return 1;
}

/* Note d'une image pour un jeu : >= 100 = meme jeu (nom exact, ou meme titre sans sous-titre),
 * departage par la region choisie ; 1..74 = nom proche (faute de frappe) ; 0 = autre jeu. */
static int match_score(int rs, const char *nn, const char *nb, const char *jn, const char *jb) {
  if (strcmp(nn, jn) == 0) return 100 + rs * 2 + 1;
  if (nb[0] && strcmp(nb, jb) == 0) return 100 + rs * 2 - 2; /* autre titre (sous-titre, chiffre romain) */
  int d = name_close(jn, nn);
  if (d < 0) d = name_close(jb, nb);
  return d < 0 ? 0 : 50 - 10 * d + rs;
}
/* Meilleure note sur les noms essayes : un nom essaye avant l'emporte toujours (200 de plus par rang). */
static int match_best(int rs, const char *nn, const char *nb, int qn, char norm[NQ][72], char base[NQ][72]) {
  int best = 0;
  for (int k = 0; k < qn; ++k) {
    int s = match_score(rs, nn, nb, norm[k], base[k]);
    if (s > 0 && s + (NQ - 1 - k) * 200 > best) best = s + (NQ - 1 - k) * 200;
  }
  return best;
}

static void ls_entry(const char *val) {
  char name[160];
  if (!href_name(val, name, sizeof(name))) return;
  char nn[72], nb[72];
  norm_title(name, nn, sizeof(nn));
  if (!nn[0]) return;
  norm_base(name, nb, sizeof(nb));
  int rs = region_score(name); /* une fois par image de la liste */
  for (int q = 0; q < dl_bg_n; ++q) {
    if (dl_jobs[QJ(q)].sys != ls_sys || QA(q) != ls_art || dl_q_ok[q] != 0) continue;
    int j = QJ(q);
    int score = match_best(rs, nn, nb, jq_n[j], jq_norm[j], jq_base[j]);
    if (score <= 0) continue;
    if (score <= bgj_score[q]) continue;
    bgj_score[q] = score;
    snprintf(bgj_href[q], sizeof(bgj_href[q]), "%s", val);
  }
}
static void (*ls_on)(const char *val) = ls_entry; /* que faire de chaque lien trouve */
static void ls_feed(const unsigned char *b, int n) { /* cherche href="..." au fil de l'eau */
  static const char pat[] = "href=\"";
  for (int i = 0; i < n; ++i) {
    unsigned char c = b[i];
    if (ls_state < 6) {
      if (c == (unsigned char)pat[ls_state]) ++ls_state;
      else ls_state = c == 'h' ? 1 : 0;
      ls_vl = 0;
    } else if (c == '"') {
      ls_val[ls_vl] = 0;
      ls_on(ls_val);
      ls_state = 0;
    } else if (ls_vl < (int)sizeof(ls_val) - 1) {
      ls_val[ls_vl++] = (char)c;
    } else {
      ls_state = 0; /* valeur trop longue : on abandonne */
    }
  }
}

static const char *art_dir[2] = {"Named_Snaps", "Named_Boxarts"};

/* Fin d'une image (0 -> resultat) : compteur de progression. */
static void dl_q_set(int q, int v) {
  dl_q_ok[q] = v;
  __sync_synchronize();
  __sync_fetch_and_add(&dl_bg_done, 1);
}

/* Telecharge une image et l'enregistre (fond ou jaquette du jeu). Retourne le code HTTP (200 = ecrite). */
static int dl_fetch_save(int q, const char *url, unsigned char *buf) {
  DlJob *j = &dl_jobs[QJ(q)];
  int len = 0, st = http_get_buf(url, buf, DL_BUF_SIZE, &len);
  if (st != 200) return st;
  if (!png_ok(buf, len)) return -1;
  char path[96];
  snprintf(path, sizeof(path), QA(q) ? COVER_DIR "/%s.png" : BG_DIR "/%s.png", j->id);
  SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) return fd;
  sceIoWrite(fd, buf, len);
  sceIoClose(fd);
  return 200;
}

/* pass 0 : ROM, nom exact du fichier (sans liste) ; pass 1 : nom trouve dans la liste du serveur. */
static void dl_bg_item(int q, int pass, unsigned char *buf) {
  if (dl_q_ok[q] != 0) return;
  DlJob *j = &dl_jobs[QJ(q)];
  int art = QA(q);
  char url[400], name[320];
  if (pass == 0) { /* seulement si le fichier est deja de la region choisie : sinon la liste du serveur */
    if (!j->file[0] || region_score(j->file) < 12) return;
    url_enc(j->file, name, sizeof(name));
    snprintf(url, sizeof(url), "http://thumbnails.libretro.com/%s/%s/%s.png", bg_folder[j->sys], art_dir[art], name);
    if (dl_fetch_save(q, url, buf) == 200) dl_q_set(q, 1); /* sinon : recherche par nom ensuite */
    return;
  }
  if (!bgj_href[q][0]) {
    char l[200];
    snprintf(l, sizeof(l), "images : aucun nom ne correspond a '%s' (%s)", j->title, jq_norm[QJ(q)][0]);
    log_line(l);
    dl_q_set(q, -1);
    return;
  }
  snprintf(url, sizeof(url), "http://thumbnails.libretro.com/%s/%s/%s", bg_folder[j->sys], art_dir[art], bgj_href[q]);
  int st = dl_fetch_save(q, url, buf);
  if (st != 200) {
    if (!dl_err && st != 404) dl_err = st;
    char l[500];
    snprintf(l, sizeof(l), "images : %s -> code %d (%08X)", url, st, (unsigned)st);
    log_line(l);
  }
  dl_q_set(q, st == 200 ? 1 : st == 404 ? -1 : -2);
}

/* Plusieurs telechargements en parallele : DL_WORKERS fils (dont celui-ci), chacun son tampon. */
#define DL_WORKERS 4
static volatile int dl_wnext, dl_wpass;
static void dl_work_loop(unsigned char *buf) {
  for (;;) {
    int q = __sync_fetch_and_add(&dl_wnext, 1);
    if (q >= dl_bg_n || dl_cancel) break;
    dl_bg_item(q, dl_wpass, buf);
  }
}
static int dl_worker(SceSize args, void *argp) {
  (void)args;
  dl_work_loop(*(unsigned char **)argp);
  return 0;
}
static void dl_parallel(int pass) {
  dl_wnext = 0;
  dl_wpass = pass;
  __sync_synchronize();
  SceUID th[DL_WORKERS];
  unsigned char *bufs[DL_WORKERS];
  int n = 0;
  for (int i = 1; i < DL_WORKERS; ++i) {
    bufs[n] = (unsigned char *)malloc(DL_BUF_SIZE);
    if (!bufs[n]) break;
    th[n] = sceKernelCreateThread("vf_dlw", dl_worker, 0x10000100, 0x10000, 0, 0, NULL);
    if (th[n] < 0 || sceKernelStartThread(th[n], sizeof(bufs[n]), &bufs[n]) < 0) {
      if (th[n] >= 0) sceKernelDeleteThread(th[n]);
      free(bufs[n]);
      break;
    }
    ++n;
  }
  dl_work_loop(dl_buf);
  for (int i = 0; i < n; ++i) {
    sceKernelWaitThreadEnd(th[i], NULL, NULL);
    sceKernelDeleteThread(th[i]);
    free(bufs[i]);
  }
}

static void dl_bg_run(void) {
  if (dl_bg_n <= 0 || dl_cancel) return;
  dl_phase = 2;
  dl_bg_done = 0;
  sceIoMkdir(BG_DIR, 0777);
  for (int sys = 1; sys < DB_N; ++sys) url_enc(bg_db[sys], bg_folder[sys], sizeof(bg_folder[sys]));
  if (!jq_norm) jq_norm = malloc(sizeof(*jq_norm) * DL_MAX);
  if (!jq_base) jq_base = malloc(sizeof(*jq_base) * DL_MAX);
  if (!jq_norm || !jq_base) { /* pas de memoire : rien n'est cherche par nom */
    for (int q = 0; q < dl_bg_n; ++q) dl_q_set(q, -2);
    return;
  }
  for (int q = 0; q < dl_bg_n; ++q) {
    DlJob *j = &dl_jobs[QJ(q)];
    jq_n[QJ(q)] = (unsigned char)name_queries(j->alias, j->title, j->file, jq_norm[QJ(q)], jq_base[QJ(q)]);
    bgj_href[q][0] = 0;
    bgj_score[q] = 0;
    dl_q_ok[q] = 0;
    if (QA(q) == 1 && dl_jobs[QJ(q)].ok == 1) dl_q_set(q, -3); /* jaquette deja recue par son identifiant */
  }
  dl_parallel(0); /* ROM : nom exact, sans telecharger les listes */
  for (int art = 0; art < 2; ++art)
    for (int sys = 1; sys < DB_N; ++sys) {
      int any = 0;
      for (int q = 0; q < dl_bg_n; ++q)
        if (dl_jobs[QJ(q)].sys == sys && QA(q) == art && dl_q_ok[q] == 0) any = 1;
      if (!any || dl_cancel) continue;
      char url[300];
      snprintf(url, sizeof(url), "http://thumbnails.libretro.com/%s/%s/", bg_folder[sys], art_dir[art]);
      ls_sys = sys;
      ls_art = art;
      ls_state = ls_vl = 0;
      int len = 0;
      http_sink = ls_feed;
      int st = http_get_auto(url, &len);
      http_sink = NULL;
      char l[320];
      snprintf(l, sizeof(l), "images : liste %s/%s -> code %d (%08X), %d octets", bg_folder[sys], art_dir[art], st, (unsigned)st, len);
      log_line(l);
      if (st != 200) {
        if (!dl_err) dl_err = st;
        for (int q = 0; q < dl_bg_n; ++q)
          if (dl_jobs[QJ(q)].sys == sys && QA(q) == art && dl_q_ok[q] == 0) dl_q_set(q, -2);
      }
    }
  if (!dl_cancel) dl_parallel(1);
  for (int q = 0; q < dl_bg_n; ++q) /* annule : le reste n'est pas compte */
    if (dl_q_ok[q] == 0) dl_q_set(q, -3);
}
static void draw_splash(const char *msg);
static void dl_run(int sync) {
  for (int i = 0; i < dl_n && !dl_cancel; ++i) {
    DlJob *j = &dl_jobs[i];
    if (!j->want_cover) {
      dl_done = i + 1;
      continue;
    }
    if (sync) {
      char m[64];
      snprintf(m, sizeof(m), L("Téléchargement des jaquettes %d / %d"), i + 1, dl_n);
      draw_splash(m);
    }
    const char *folders[2] = {"PSVita", NULL};
    if (j->kind == KIND_HB) { folders[0] = "HOMEBREW"; folders[1] = "PSVita"; }
    else if (j->kind == KIND_PSP) folders[0] = "PSP";
    int outcome = -1;
    for (int f = 0; f < 2 && folders[f] && outcome != 1; ++f) {
      char url[200];
      int len = 0, st;
      for (;;) { /* si un hote refuse la connexion securisee, on essaie le suivant */
        snprintf(url, sizeof(url), dl_hosts[dl_host], folders[f], j->id);
        st = http_get_auto(url, &len);
        if (((unsigned)st == 0x80431075u || st == 403) && dl_host < 2) {
          if (ssl_dbg[0]) log_line(ssl_dbg);
          ++dl_host;
          continue;
        }
        break;
      }
      if (st == 200 && png_ok(dl_buf, len)) { /* tronquee, mal formee ou demesuree : pas ecrite */
        char path[96];
        snprintf(path, sizeof(path), COVER_DIR "/%s.png", j->id);
        SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (fd >= 0) {
          sceIoWrite(fd, dl_buf, len);
          sceIoClose(fd);
          outcome = 1;
        } else {
          outcome = -2;
        }
      } else if (st == 404) {
        if (outcome != -2) outcome = -1;
      } else {
        outcome = -2;
        if (!dl_err) {
          dl_err = st;
          char l[260];
          snprintf(l, sizeof(l), "telechargement : %s -> code %d (%08X)", url, st, (unsigned)st);
          log_line(l);
          if (ssl_dbg[0]) log_line(ssl_dbg);
        }
      }
    }
    j->ok = outcome;
    dl_done = i + 1;
  }
  dl_bg_run();
  dl_state = 2;
}
static int dl_thread(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  dl_run(0);
  return 0;
}

static int dl_supported(const Game *g) {
  if (!g->id[0]) return 0;
  if (g->kind == KIND_VITA || g->kind == KIND_HB) return 1;
  if (g->kind == KIND_PSP && strncmp(g->id, "PSPEMU", 6) != 0) return 1;
  return 0;
}

/* Images cherchees par nom : jeux PSP et systemes PlayStation 1 (nom du systeme contenant PS1, PSX ou PLAYSTATION). 0 = non. */
static int bg_sys_of(const Game *g) {
  if (!g->title[0]) return 0;
  if (g->rom) return rom_db_of(g);
  if (g->kind == KIND_PSP) return 1;
  int k = g->kind - KIND_CUSTOM0;
  if (k >= 0 && k < MAX_CUSTOM_SYS && SLOT_USED(k)) {
    char u[48];
    int o = 0;
    for (const char *c = slot_name[k]; *c && o < 46; ++c) u[o++] = (*c >= 'a' && *c <= 'z') ? *c - 32 : *c;
    u[o] = 0;
    if (strstr(u, "PSP") || strstr(u, "PORTABLE") || strstr(u, "VITA") || strstr(u, "MOBILE")) return 0;
    if (strstr(u, "PS1") || strstr(u, "PSX") || strstr(u, "PSONE") || strstr(u, "PS ONE")) return 2;
    if (strstr(u, "PLAYSTATION") && !strstr(u, "2") && !strstr(u, "3") && !strstr(u, "4") && !strstr(u, "5")) return 2;
    if (strcmp(slot_tag[k], "PS1") == 0) return 2;
  }
  return 0;
}
static void bg_apply(Game *g) { /* un fond vient d'etre telecharge pour ce jeu */
  snprintf(g->bg_path, sizeof(g->bg_path), BG_DIR "/%s.png", g->id);
  vita2d_wait_rendering_done();
  if (g->bg) vita2d_free_texture(g->bg);
  g->bg = NULL;
  g->has_bg = 1;
}

/* wc : jaquette par identifiant ; bg : fond par nom ; nc : jaquette par nom (si sys != 0). */
static void dl_add_job(const Game *g, int wc, int sys, int bg, int nc) {
  DlJob *j = &dl_jobs[dl_n++];
  memset(j, 0, sizeof(*j));
  snprintf(j->id, sizeof(j->id), "%s", g->id);
  snprintf(j->title, sizeof(j->title), "%s", game_orig_title(g)); /* nom d'origine, meme si renomme */
  snprintf(j->alias, sizeof(j->alias), "%s", strcmp(g->title, j->title) ? g->title : "");
  j->kind = g->kind;
  j->want_cover = wc;
  j->sys = sys;
  if (g->rom) { /* nom libretro : fichier sans extension, caracteres interdits remplaces par _ */
    const char *b = strrchr(g->src_path, '/');
    snprintf(j->file, sizeof(j->file), "%s", b ? b + 1 : g->src_path);
    char *dot = strrchr(j->file, '.');
    if (dot) *dot = 0;
    for (char *p = j->file; *p; ++p)
      if (strchr("&*/:`<>?\\|", *p)) *p = '_';
  }
  if (sys && bg) dl_bgq[dl_bg_n++] = dl_n - 1;
  if (sys && nc) dl_bgq[dl_bg_n++] = dl_n - 1 + 65536;
  if (wc) dl_want_covers = 1;
}
static void dl_start(Game *one) {
  if (dl_state == 1) return;
  dl_n = dl_done = dl_applied = 0;
  dl_bg_n = dl_bg_done = dl_bg_applied = 0;
  dl_ok_count = dl_miss_count = dl_fail_count = 0;
  for (int a = 0; a < 2; ++a) dl_art_ok[a] = dl_art_miss[a] = dl_art_fail[a] = 0;
  dl_want_covers = 0;
  dl_phase = 1;
  dl_host = 0;
  dl_err = 0;
  dl_cancel = 0;
  if (one) {
    int wc = dl_supported(one), sys = bg_sys_of(one);
    int bg = sys && !one->has_bg;
    if (!wc && !sys) {
      snprintf(toast_text, sizeof(toast_text), "Pas de source d'images pour ce type de jeu");
      toast_time = 3.0f;
      return;
    }
    dl_add_job(one, wc, sys, bg, sys != 0);
  } else {
    static const char *exts[3] = {".png", ".jpg", ".jpeg"};
    nameset_load(&cover_set, COVER_DIR);
    for (int i = 0; i < app_n && dl_n < DL_MAX; ++i) {
      const Game *g = &all_apps[i];
      if (g->hidden) continue;
      int have = custom_get(g->id) != NULL;
      char fn[48];
      for (int e = 0; e < 3; ++e) {
        snprintf(fn, sizeof(fn), "%s%s", g->id, exts[e]);
        if (nameset_has(&cover_set, fn)) have = 1;
      }
      int sys = bg_sys_of(g);
      int wc = dl_supported(g) && !have;
      int bg = sys && !g->has_bg;
      int nc = sys && !have;
      if (!wc && !bg && !nc) continue;
      dl_add_job(g, wc, sys, bg, nc);
    }
    if (dl_n == 0) {
      snprintf(toast_text, sizeof(toast_text), "Toutes les images sont déjà là");
      toast_time = 3.0f;
      return;
    }
  }
  if (!net_start()) {
    snprintf(toast_text, sizeof(toast_text), "Réseau indisponible (Wi-Fi activé ?)");
    toast_time = 3.0f;
    dl_n = 0;
    return;
  }
  dl_state = 1;
  dl_open = 1;
  /* Plusieurs essais (priorite / pile) ; en dernier recours on telecharge sans fil separe. */
  static const int prios[3] = {0x10000100, 0x10000100, 0x40};
  static const int stacks[3] = {0x10000, 0x8000, 0x20000};
  int started = 0;
  for (int t = 0; t < 3 && !started; ++t) {
    SceUID th = sceKernelCreateThread("vf_dl", dl_thread, prios[t], stacks[t], 0, 0, NULL);
    int sr = th >= 0 ? sceKernelStartThread(th, 0, NULL) : th;
    char l[96];
    snprintf(l, sizeof(l), "telechargement : thread essai %d create=%08X start=%08X", t, (unsigned)th, (unsigned)sr);
    log_line(l);
    if (th >= 0 && sr >= 0) started = 1;
  }
  if (!started) {
    log_line("telechargement : mode sans fil separe");
    dl_run(1);
  }
}

/* Chaque image : applique les jaquettes recues au fur et a mesure (au plus 2 par image affichee). */
static void dl_poll(void) {
  if (!dl_open) return;
  int budget = 2;
  while (dl_applied < dl_done && budget > 0) {
    DlJob *j = &dl_jobs[dl_applied++];
    if (!j->want_cover) continue;
    if (j->ok == 1) {
      char fn[48];
      ++dl_ok_count;
      snprintf(fn, sizeof(fn), "%s.png", j->id);
      nameset_add(&cover_set, fn);
      custom_set(j->id, NULL); /* l'ancien choix manuel ne masque pas la nouvelle jaquette */
      for (int i = 0; i < app_n; ++i)
        if (strcmp(all_apps[i].id, j->id) == 0) reload_cover(&all_apps[i]);
      --budget;
    } else if (j->ok == -1) ++dl_miss_count;
    else ++dl_fail_count;
  }
  while (dl_bg_applied < dl_bg_n && dl_q_ok[dl_bg_applied] != 0 && budget > 0) {
    int q = dl_bg_applied++;
    DlJob *j = &dl_jobs[QJ(q)];
    int a = QA(q);
    if (dl_q_ok[q] == 1) {
      ++dl_art_ok[a];
      if (a == 0) {
        for (int i = 0; i < app_n; ++i)
          if (strcmp(all_apps[i].id, j->id) == 0) bg_apply(&all_apps[i]);
      } else {
        char fn[48];
        snprintf(fn, sizeof(fn), "%s.png", j->id);
        nameset_add(&cover_set, fn);
        custom_set(j->id, NULL);
        for (int i = 0; i < app_n; ++i)
          if (strcmp(all_apps[i].id, j->id) == 0) reload_cover(&all_apps[i]);
      }
      --budget;
    } else if (dl_q_ok[q] == -1) ++dl_art_miss[a];
    else if (dl_q_ok[q] != -3) ++dl_art_fail[a];
  }
  if (dl_state == 2 && dl_applied >= dl_done && dl_bg_applied >= dl_bg_n) {
    custom_save();
    nameset_load(&bg_set, BG_DIR);
    dl_open = 0;
    dl_state = 0;
    int okc = dl_ok_count + dl_art_ok[1], okb = dl_art_ok[0];
    int miss = dl_miss_count + dl_art_miss[0] + dl_art_miss[1];
    int fail = dl_fail_count + dl_art_fail[0] + dl_art_fail[1];
    if (okc == 0 && okb == 0 && fail > 0 && ((unsigned)dl_err == 0x80431075u || (unsigned)dl_err == 0x80435061u))
      /* le code est affiche : ces erreurs SSL ont plusieurs causes possibles (TLS, memoire, horloge) */
      snprintf(toast_text, sizeof(toast_text), L("Connexion sécurisée refusée (%08X) : iTLS-Enso ? log.txt"), (unsigned)dl_err);
    else if (okc == 0 && okb == 0 && fail > 0)
      snprintf(toast_text, sizeof(toast_text), L("Échec du téléchargement (réseau, code %08X)"), (unsigned)dl_err);
    else
      snprintf(toast_text, sizeof(toast_text), L("Jaquettes : %d, fonds : %d, échecs : %d"), okc, okb, miss + fail);
    toast_time = 5.0f;
  }
}

static void draw_download(void) {
  float w = 520, h = 200, x = (SCREEN_W - w) / 2, y = 170;
  modal_panel(x, y, w, h, 225);
  int bgp = dl_phase == 2;
  draw_text(ST_SEMI, (int)x + 28, (int)y + 46, FS_SECTION, T.text, bgp ? "Recherche des images par nom" : "Téléchargement des jaquettes");
  char t[80];
  int dn = bgp ? dl_bg_n : dl_n;
  int done = bgp ? dl_bg_done : dl_done, total = dn > 0 ? dn : 1;
  snprintf(t, sizeof(t), "%d / %d", done, dn);
  draw_text(ST_REG, (int)x + 28, (int)y + 82, FS_BODY, T.muted, t);
  round_rect(x + 28, y + 100, w - 56, 14, 7, T.card_border);
  float frac = (float)done / (float)total;
  if (frac > 0.0f) round_rect(x + 28, y + 100, (w - 56) * frac, 14, 7, T.acc);
  if (!bgp && done < dn) draw_text(ST_REG, (int)x + 28, (int)y + 142, FS_SMALL, T.muted, dl_jobs[done].id);
  draw_hints((int)x + 28, (int)y + 176, FS_SMALL, T.muted, "O Annuler");
}


/* ======================================================================
 *  Autre version de la jaquette (SELECT > Autre version...) : toutes les jaquettes du serveur pour
 *  ce jeu (regions, editions) ; seule celle choisie est telechargee.
 * ====================================================================== */
#define ALT_MAX 40
static volatile int alt_state; /* 1 recherche, 2 liste prete, 3 telechargement, 4 fini, -1 echec */
static int alt_open, alt_sel, alt_n, alt_sys;
static Game *alt_game;
static char alt_href[ALT_MAX][150], alt_label[ALT_MAX][96];
static int alt_score[ALT_MAX];
static char alt_qn[NQ][72], alt_qb[NQ][72];
static int alt_qc;

static void alt_entry(const char *val) {
  char name[160], nn[72], nb[72];
  if (!href_name(val, name, sizeof(name))) return;
  norm_title(name, nn, sizeof(nn));
  norm_base(name, nb, sizeof(nb));
  int sc = match_best(region_score(name), nn, nb, alt_qc, alt_qn, alt_qb);
  if (sc <= 0) return;
  int pos = alt_n < ALT_MAX ? alt_n++ : -1; /* liste pleine : remplace la moins bonne */
  if (pos < 0) {
    pos = 0;
    for (int i = 1; i < ALT_MAX; ++i)
      if (alt_score[i] < alt_score[pos]) pos = i;
    if (alt_score[pos] >= sc) return;
  }
  alt_score[pos] = sc;
  snprintf(alt_href[pos], sizeof(alt_href[0]), "%s", val);
  snprintf(alt_label[pos], sizeof(alt_label[0]), "%s", name);
}

static int alt_list_thread(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  char url[300];
  snprintf(url, sizeof(url), "http://thumbnails.libretro.com/%s/Named_Boxarts/", bg_folder[alt_sys]);
  ls_state = ls_vl = 0;
  ls_on = alt_entry;
  http_sink = ls_feed;
  int len = 0, st = http_get_auto(url, &len);
  http_sink = NULL;
  ls_on = ls_entry;
  for (int i = 1; i < alt_n; ++i) /* meilleures d'abord (region choisie en tete) */
    for (int k = i; k > 0 && alt_score[k] > alt_score[k - 1]; --k) {
      int ts = alt_score[k];
      char th[150], tl[96];
      memcpy(th, alt_href[k], sizeof(th));
      memcpy(tl, alt_label[k], sizeof(tl));
      alt_score[k] = alt_score[k - 1];
      memcpy(alt_href[k], alt_href[k - 1], sizeof(th));
      memcpy(alt_label[k], alt_label[k - 1], sizeof(tl));
      alt_score[k - 1] = ts;
      memcpy(alt_href[k - 1], th, sizeof(th));
      memcpy(alt_label[k - 1], tl, sizeof(tl));
    }
  __sync_synchronize();
  alt_state = st == 200 ? 2 : -1;
  return 0;
}

static int alt_get_thread(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  char url[400];
  snprintf(url, sizeof(url), "http://thumbnails.libretro.com/%s/Named_Boxarts/%s", bg_folder[alt_sys], alt_href[alt_sel]);
  int len = 0, st = http_get_auto(url, &len), ok = 0;
  if (st == 200 && png_ok(dl_buf, len)) {
    char path[96];
    sceIoMkdir(COVER_DIR, 0777);
    snprintf(path, sizeof(path), COVER_DIR "/%s.png", alt_game->id);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) {
      ok = sceIoWrite(fd, dl_buf, len) == len;
      sceIoClose(fd);
    }
  }
  __sync_synchronize();
  alt_state = ok ? 4 : -1;
  return 0;
}

static void alt_run(SceKernelThreadEntry fn) { /* fil separe ; sinon tout de suite */
  SceUID th = sceKernelCreateThread("vf_alt", fn, 0x10000100, 0x10000, 0, 0, NULL);
  if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) fn(0, NULL);
}

static void alt_open_for(Game *g) {
  int sys = bg_sys_of(g);
  if (!sys) {
    snprintf(toast_text, sizeof(toast_text), "Jaquette trouvée par l'identifiant du jeu : pas d'autre version");
    toast_time = 3.0f;
    return;
  }
  if (dl_state == 1) {
    snprintf(toast_text, sizeof(toast_text), "Téléchargement en cours : réessaie après");
    toast_time = 2.5f;
    return;
  }
  if (!net_start()) {
    snprintf(toast_text, sizeof(toast_text), "Réseau indisponible (Wi-Fi activé ?)");
    toast_time = 3.0f;
    return;
  }
  for (int s = 1; s < DB_N; ++s) url_enc(bg_db[s], bg_folder[s], sizeof(bg_folder[s]));
  alt_game = g;
  alt_sys = sys;
  alt_n = alt_sel = 0;
  char file[100] = "";
  if (g->rom) { /* nom du fichier sans extension */
    const char *b = strrchr(g->src_path, '/');
    snprintf(file, sizeof(file), "%s", b ? b + 1 : g->src_path);
    char *dot = strrchr(file, '.');
    if (dot) *dot = 0;
  }
  const char *orig = game_orig_title(g);
  alt_qc = name_queries(strcmp(g->title, orig) ? g->title : "", orig, file, alt_qn, alt_qb);
  alt_state = 1;
  alt_open = 1;
  alt_run(alt_list_thread);
}

static void alt_pick(void) {
  if (alt_state != 2 || alt_sel >= alt_n) return;
  alt_state = 3;
  alt_run(alt_get_thread);
}

/* A chaque image : jaquette recue -> remplace l'actuelle. */
static void alt_poll(void) {
  if (!alt_open || alt_state != 4) return;
  char fn[48];
  snprintf(fn, sizeof(fn), "%s.png", alt_game->id);
  nameset_add(&cover_set, fn);
  custom_set(alt_game->id, NULL); /* un ancien choix manuel ne masque pas la nouvelle jaquette */
  custom_save();
  reload_cover(alt_game);
  alt_open = 0;
  snprintf(toast_text, sizeof(toast_text), "Jaquette remplacée");
  toast_time = 2.0f;
}

static void modal_row(float x, float y, float w, float h, int sel);
static void draw_alt(void) {
  float w = 640, h = 420, x = (SCREEN_W - w) / 2, y = 62;
  modal_panel(x, y, w, h, 215);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 42, FS_SECTION, T.text, "Autre version de la jaquette");
  char buf[96];
  fit_text(ST_REG, FS_BODY, alt_game ? alt_game->name : "", (int)w - 56, buf, sizeof(buf));
  draw_text(ST_REG, (int)x + 28, (int)y + 68, FS_BODY, T.muted, buf);
  const char *msg = alt_state == 1 ? "Recherche des versions sur libretro-thumbnails..."
                    : alt_state == 3 ? "Téléchargement..."
                    : alt_state == -1 ? "Échec (réseau ou serveur)."
                    : alt_state == 2 && alt_n == 0 ? "Aucune version trouvée pour ce titre." : NULL;
  if (msg) draw_text(ST_REG, (int)x + 28, (int)y + 120, FS_BODY, T.text_soft, msg);
  if (alt_state == 2 || alt_state == 3) {
    int first = alt_sel > 6 ? alt_sel - 6 : 0;
    for (int k = 0; k < 7 && first + k < alt_n; ++k) {
      int i = first + k, sel = i == alt_sel;
      float ry = y + 92 + k * 42;
      modal_row(x + 20, ry, w - 40, 36, sel);
      fit_text(sel ? ST_SEMI : ST_REG, FS_BODY, alt_label[i], (int)w - 80, buf, sizeof(buf));
      draw_text(sel ? ST_SEMI : ST_REG, (int)x + 40, (int)ry + 25, FS_BODY, T.text, buf);
    }
  }
  draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted, "X Télécharger celle-ci;O Fermer");
}
/* ======================================================================
 *  Reglages > Reseau : etat, adresse IP, test de connexion ; barres du Wi-Fi de la barre du haut
 * ====================================================================== */

/* Signal du Wi-Fi relu toutes les 5 s (net_bars : 0 a 4, -1 hors connexion). */
static void net_poll(float dt) {
  static float t;
  t -= dt;
  if (t > 0.0f) return;
  t = 5.0f;
  int st = 0;
  if (!net_base_start() || sceNetCtlInetGetState(&st) < 0 || st != SCE_NETCTL_STATE_CONNECTED) {
    net_bars = -1;
    return;
  }
  SceNetCtlInfo info;
  memset(&info, 0, sizeof(info));
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &info) < 0) {
    net_bars = 4;
    return;
  }
  int p = (int)info.rssi_percentage;
  net_bars = p >= 75 ? 4 : p >= 50 ? 3 : p >= 25 ? 2 : p > 0 ? 1 : 0;
}

/* "Wi-Fi (nom du reseau)", "Connexion en cours..." ou "Aucune connexion". */
static void net_state_text(char *out, int n) {
  int st = 0;
  if (!net_base_start() || sceNetCtlInetGetState(&st) < 0 || st == SCE_NETCTL_STATE_DISCONNECTED) {
    snprintf(out, n, "%s", L("Aucune connexion"));
    return;
  }
  if (st != SCE_NETCTL_STATE_CONNECTED) {
    snprintf(out, n, "%s", L("Connexion en cours..."));
    return;
  }
  SceNetCtlInfo info;
  memset(&info, 0, sizeof(info));
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_SSID, &info) >= 0 && info.ssid[0]) {
    char ss[40];
    fit_text(ST_SEMI, FS_BODY, info.ssid, 260, ss, sizeof(ss));
    snprintf(out, n, "Wi-Fi (%s)", ss);
  } else {
    snprintf(out, n, "Wi-Fi");
  }
}

/* Adresse IP de la console, "-" sans connexion. */
static void net_ip_text(char *out, int n) {
  int st = 0;
  SceNetCtlInfo info;
  memset(&info, 0, sizeof(info));
  if (!net_base_start() || sceNetCtlInetGetState(&st) < 0 || st != SCE_NETCTL_STATE_CONNECTED ||
      sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0 || !info.ip_address[0]) {
    snprintf(out, n, "-");
    return;
  }
  snprintf(out, n, "%s", info.ip_address);
}

/* Test : joint le serveur des jaquettes et mesure le temps de reponse (dans un fil a part). */
static volatile int net_testing;
static int net_test_thread(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  static unsigned char tb[2048];
  int len = 0;
  unsigned long long t0 = sceKernelGetProcessTimeWide();
  int code = net_start() ? http_get_buf("https://thumbnails.libretro.com/", tb, (int)sizeof(tb), &len) : -1;
  unsigned ms = (unsigned)((sceKernelGetProcessTimeWide() - t0) / 1000ULL);
  if (code >= 200 && code < 400) snprintf(net_test_msg, sizeof(net_test_msg), L("OK (%u ms)"), ms);
  else if (code > 0) snprintf(net_test_msg, sizeof(net_test_msg), L("Échec (code %d)"), code);
  else snprintf(net_test_msg, sizeof(net_test_msg), "%s", L("Échec : pas de réponse"));
  net_testing = 0;
  sceKernelExitDeleteThread(0);
  return 0;
}
static void net_test_start(void) {
  if (net_testing) return;
  net_testing = 1;
  snprintf(net_test_msg, sizeof(net_test_msg), "%s", L("Test en cours..."));
  SceUID th = sceKernelCreateThread("vf_nettest", net_test_thread, 0x10000100, 0x8000, 0, 0, NULL);
  if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    if (th >= 0) sceKernelDeleteThread(th);
    snprintf(net_test_msg, sizeof(net_test_msg), "%s", L("Échec : pas de réponse"));
    net_testing = 0;
  }
}
