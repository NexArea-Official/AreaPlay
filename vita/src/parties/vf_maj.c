/* Area Play - Mise a jour depuis l'appli. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Mise a jour depuis GitHub (depot public NexArea-Official/AreaPlay)
 *  - recherche en arriere-plan, 3 s apres l'ouverture (l'ouverture n'est jamais ralentie) ;
 *    si une version plus recente existe : carte en haut a droite (X installer, O plus tard : elle se replie en
 *    icone a gauche d'"Area Play" en bas), point sur "Reglages", Reglages > Systeme > Mise a jour ;
 *  - option "Installer automatiquement" : telechargement en silence, puis la carte propose d'installer
 *    (sinon l'installation se fait a la prochaine ouverture) ;
 *  - installation : les fichiers changes sont telecharges dans ux0:data/AreaPlay/update/ et verifies
 *    (taille + CRC32), puis update/pret.txt est ecrit et Area Play lance Area Play Updater (ARPL00002, dossier
 *    updater/) et se ferme : la Vita interdit a une appli ouverte d'ecrire dans son propre dossier (8001000D).
 *    L'Updater affiche l'ecran d'installation, copie les fichiers (eboot.bin en dernier) et relance Area Play.
 *    Telechargement coupe ou fichier abime : rien n'est remplace, l'ancienne version reste intacte.
 *  Fichier d'annonce : update/vita/latest.txt du depot, lignes "cle=valeur" :
 *    version=1.2
 *    notes=Premiere nouveaute|Deuxieme nouveaute          (francais, "|" entre les lignes)
 *    notes_en=First change|Second change                  (anglais, facultatif)
 *    file=eboot.bin|1022370|1a2b3c4d                      (chemin dans l'appli | taille | CRC32 hexa)
 *  Les fichiers sont lus dans update/vita/<version>/<chemin>.
 * ====================================================================== */

/* Depot GitHub, en HTTPS par curl + OpenSSL (vf_reseau.c) : marche sans iTLS-Enso. */
#define UPD_URL "https://raw.githubusercontent.com/NexArea-Official/AreaPlay/main/update/vita/"
#define UPD_DIR DATA_DIR "/update"
#define UPD_READY UPD_DIR "/pret.txt" /* lu par Area Play Updater */
#define UPD_TOOL_ID "ARPL00002"      /* Area Play Updater */
#define UPD_HANDOFF 0.55f            /* part de la barre faite par Area Play (telechargement) ; l'Updater continue */
#define UPD_APP_DIR "ux0:app/" VF_TITLE_ID
#define UPD_MAX_FILES 48
#define UPD_MAX_SIZE (12 << 20)

enum { UPD_IDLE, UPD_CHECKING, UPD_UPTODATE, UPD_AVAILABLE, UPD_ERROR, UPD_DOWNLOADING, UPD_INSTALLED, UPD_FAILED };
static volatile int upd_state = UPD_IDLE;
static volatile int upd_done, upd_total, upd_cancel;
static struct {
  char path[64];
  int size;
  unsigned int crc;
} upd_files[UPD_MAX_FILES];
static int upd_nfiles;
static char upd_msg[140];   /* derniere erreur (journal et Reglages) */
static int upd_notified;   /* notification deja affichee */
static int upd_card;       /* carte : 0 cachee, 1 en haut a droite, 2 repliee (icone a gauche d'Area Play, en bas) */
static float upd_card_t, upd_fold; /* apparition de la carte, repli vers l'icone (0 a 1) */
static int upd_silent;     /* installation automatique : pas de redemarrage sans accord */

/* Compare deux numeros "1.2.3" : > 0 si a est plus recent. */
static int upd_ver_cmp(const char *a, const char *b) {
  for (int i = 0; i < 4; ++i) {
    int x = atoi(a), y = atoi(b);
    if (x != y) return x - y;
    a = strchr(a, '.');
    b = strchr(b, '.');
    if (!a && !b) return 0;
    a = a ? a + 1 : "0";
    b = b ? b + 1 : "0";
  }
  return 0;
}

static void upd_split_notes(const char *s) {
  upd_notes_n = 0;
  while (*s && upd_notes_n < 8) {
    int n = 0;
    while (s[n] && s[n] != '|') ++n;
    snprintf(upd_notes[upd_notes_n++], sizeof(upd_notes[0]), "%.*s", n < 159 ? n : 159, s);
    s += n;
    if (*s == '|') ++s;
  }
}

/* Lit latest.txt. 1 si une version et au moins un fichier valide. */
static int upd_parse(char *txt) {
  upd_ver[0] = 0;
  upd_nfiles = 0;
  char notes_fr[1300] = "", notes_en[1300] = "";
  for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    char *eq = strchr(line, '=');
    if (!eq || line[0] == '#') continue;
    *eq = 0;
    const char *v = eq + 1;
    if (strcmp(line, "version") == 0) snprintf(upd_ver, sizeof(upd_ver), "%s", v);
    else if (strcmp(line, "notes") == 0) snprintf(notes_fr, sizeof(notes_fr), "%s", v);
    else if (strcmp(line, "notes_en") == 0) snprintf(notes_en, sizeof(notes_en), "%s", v);
    else if (strcmp(line, "file") == 0 && upd_nfiles < UPD_MAX_FILES) {
      char p[64];
      int sz = 0;
      unsigned int crc = 0;
      if (sscanf(v, "%63[^|]|%d|%x", p, &sz, &crc) == 3 && sz > 0 && sz <= UPD_MAX_SIZE && !strstr(p, "..") && p[0] != '/') {
        snprintf(upd_files[upd_nfiles].path, sizeof(upd_files[0].path), "%s", p);
        upd_files[upd_nfiles].size = sz;
        upd_files[upd_nfiles].crc = crc;
        ++upd_nfiles;
      }
    }
  }
  upd_split_notes(lang_code[0] && notes_en[0] ? notes_en : notes_fr);
  return upd_ver[0] && upd_nfiles > 0;
}

/* Adresse du dossier des mises a jour (depot public).
 * Version de test seulement (cmake -DAP_TEST_MAJ=ON) : ux0:data/AreaPlay/maj_test.txt donne une autre adresse
 * (serveur sur le PC). La version publiee ne lit pas ce fichier. */
#ifdef AP_TEST_MAJ
static const char *upd_base(void) {
  static char base[160];
  static int done;
  if (!done) {
    done = 1;
    snprintf(base, sizeof(base), "%s", UPD_URL);
    FILE *f = fopen(DATA_DIR "/maj_test.txt", "r");
    if (f) {
      char l[150];
      if (fgets(l, sizeof(l), f)) {
        int n = (int)strlen(l);
        while (n > 0 && (unsigned char)l[n - 1] <= ' ') l[--n] = 0;
        if (n > 7) snprintf(base, sizeof(base), "%s%s", l, l[n - 1] == '/' ? "" : "/");
      }
      fclose(f);
    }
    char m[200];
    snprintf(m, sizeof(m), "mise a jour : version de test, %s", base);
    log_line(m);
  }
  return base;
}
#else
static const char *upd_base(void) { return UPD_URL; }
#endif

static int upd_get(const char *url, unsigned char *buf, int cap, int *len) { return http_get_buf(url, buf, cap, len); }

/* Recherche (fil a part). */
static int upd_check_thread(SceSize args, void *argp) {
  (void)args;
  int delay = argp ? *(int *)argp : 0;
  if (delay) sceKernelDelayThread(3000000); /* apres l'ouverture : rien n'est ralenti */
  static unsigned char buf[8192];
  char url[200];
  snprintf(url, sizeof(url), "%slatest.txt", upd_base());
  int len = 0, st = net_start() ? upd_get(url, buf, (int)sizeof(buf) - 1, &len) : -1;
  if (st == 200 && len > 0) {
    buf[len] = 0;
    if (upd_parse((char *)buf)) {
      upd_state = upd_ver_cmp(upd_ver, VF_VERSION_NUM) > 0 ? UPD_AVAILABLE : UPD_UPTODATE;
    } else {
      snprintf(upd_msg, sizeof(upd_msg), "latest.txt illisible");
      upd_state = UPD_ERROR;
    }
  } else {
    snprintf(upd_msg, sizeof(upd_msg), "code %d (%08X)", st, (unsigned)st);
    upd_state = UPD_ERROR;
  }
  char l[160];
  snprintf(l, sizeof(l), "mise a jour : version en ligne %s (installee %s), etat %d %s", upd_ver[0] ? upd_ver : "-",
           VF_VERSION_NUM, upd_state, upd_msg);
  log_line(l);
  sceKernelExitDeleteThread(0);
  return 0;
}

/* auto : recherche du demarrage (en silence si rien de nouveau). */
static void upd_check_start(int automatic) {
  if (upd_state == UPD_CHECKING || upd_state == UPD_DOWNLOADING || upd_state == UPD_INSTALLED) return;
  upd_state = UPD_CHECKING;
  upd_msg[0] = 0;
  int delay = automatic; /* copie dans la pile du fil par sceKernelStartThread */
  SceUID th = sceKernelCreateThread("ap_majchk", upd_check_thread, 0x10000100, 0x20000, 0, 0, NULL);
  if (th < 0 || sceKernelStartThread(th, sizeof(delay), &delay) < 0) {
    if (th >= 0) sceKernelDeleteThread(th);
    upd_state = UPD_ERROR;
    snprintf(upd_msg, sizeof(upd_msg), "fil impossible");
  }
}

/* Cree les dossiers d'un chemin de fichier (base/a/b/fichier -> base/a, base/a/b). */
static void upd_mkdirs(const char *full) {
  char d[200];
  snprintf(d, sizeof(d), "%s", full);
  for (char *p = d + 6; *p; ++p) /* apres "ux0:a/" */
    if (*p == '/') {
      *p = 0;
      sceIoMkdir(d, 0777);
      *p = '/';
    }
}

static int upd_err; /* dernier code d'erreur d'ecriture (journal) */
static int upd_write(const char *path, const unsigned char *b, int n) {
  upd_mkdirs(path);
  SceUID f = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) {
    upd_err = f;
    return 0;
  }
  int w = sceIoWrite(f, b, n);
  sceIoClose(f);
  if (w != n) upd_err = w;
  return w == n;
}

/* Fichier lu par Area Play Updater : version, fond et couleurs de l'ecran de demarrage (il le reproduit), fichiers.
 * Meme choix de fond que splash_content : image de la LiveArea, sinon fond d'ecran (+ voile), sinon image du
 * theme, sinon degrade. Les chemins app0: deviennent ux0:app/ARPL00001/ (l'Updater a son propre app0:). */
static int upd_write_ready(void) {
  static char t[4096];
  static const int dim_a[3] = {40, 105, 165};
  char img[200] = "";
  int veil = 0;
  if (opt_splash_bg) snprintf(img, sizeof(img), "app0:sce_sys/pic0.png");
  else {
    const char *wp = wall_path[0] ? wall_path : theme_default_wall();
    if (wp && wp[0]) {
      snprintf(img, sizeof(img), "%s", wp);
      veil = dim_a[opt_wall_dim % 3];
    } else if (opt_theme < BUILTIN_THEMES) {
      static const char *pat[] = {DATA_DIR "/themes/%s.jpg", DATA_DIR "/themes/%s.png", "app0:assets/themes/%s.jpg",
                                  "app0:assets/themes/%s.png", "app0:assets/themes/%s.jpeg", NULL};
      for (int c = 0; pat[c] && !img[0]; ++c) {
        char p[160];
        SceIoStat st;
        snprintf(p, sizeof(p), pat[c], theme_file[opt_theme]);
        if (sceIoGetstat(p, &st) >= 0) snprintf(img, sizeof(img), "%s", p);
      }
    }
  }
  unsigned int top = T.has_grad ? T.grad_top : mix(T.bg, T.acc, T.light ? 0.06f : 0.09f);
  unsigned int bot = T.has_grad ? T.grad_bot : T.bg;
  int n = snprintf(t, sizeof(t), "version=%s\ncouleurs=%08X %08X %08X %08X %08X\ndegrade=%08X %08X\nvoile=%d\nsuite=%.2f\n",
                   upd_ver, T.bg, T.text, T.muted, T.card_border, T.acc, top, bot, veil, (double)UPD_HANDOFF);
  if (img[0]) n += snprintf(t + n, sizeof(t) - n, "fond=%s%s\n", strncmp(img, "app0:", 5) ? "" : UPD_APP_DIR "/",
                            strncmp(img, "app0:", 5) ? img : img + 5);
  for (int i = 0; i < upd_nfiles && n < (int)sizeof(t) - 100; ++i)
    n += snprintf(t + n, sizeof(t) - n, "file=%s|%d|%08x\n", upd_files[i].path, upd_files[i].size, upd_files[i].crc);
  return upd_write(UPD_READY, (const unsigned char *)t, n);
}

static int upd_install_thread(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  int ok = 1;
  sceIoMkdir(UPD_DIR, 0777);
  /* 1) telechargement et verification de tous les fichiers (rien n'est encore remplace) */
  for (int i = 0; i < upd_nfiles && ok && !upd_cancel; ++i) {
    unsigned char *b = (unsigned char *)malloc((size_t)upd_files[i].size + 1);
    if (!b) {
      ok = 0;
      snprintf(upd_msg, sizeof(upd_msg), "memoire insuffisante");
      break;
    }
    char url[256], dst[200];
    snprintf(url, sizeof(url), "%s%s/%s", upd_base(), upd_ver, upd_files[i].path);
    int len = 0, st = upd_get(url, b, upd_files[i].size + 1, &len);
    unsigned int crc = (unsigned int)crc32(0L, b, (uInt)len);
    if (st != 200 || len != upd_files[i].size || crc != upd_files[i].crc) {
      ok = 0;
      snprintf(upd_msg, sizeof(upd_msg), "%s : code %d, %d octets", upd_files[i].path, st, len);
    } else {
      snprintf(dst, sizeof(dst), UPD_DIR "/%s", upd_files[i].path);
      if (!upd_write(dst, b, len)) {
        ok = 0;
        snprintf(upd_msg, sizeof(upd_msg), "ecriture impossible : %s", dst);
      }
    }
    free(b);
    upd_done = i + 1;
  }
  if (upd_cancel) ok = 0;
  /* 2) le remplacement est fait par Area Play Updater (la Vita verrouille le dossier d'une appli ouverte) */
  if (ok && !upd_write_ready()) {
    ok = 0;
    snprintf(upd_msg, sizeof(upd_msg), "ecriture impossible : " UPD_READY " (%08X)", (unsigned)upd_err);
  }
  char l[200];
  snprintf(l, sizeof(l), "mise a jour %s : %s %s", upd_ver, ok ? "telechargee, prete" : "echec", upd_msg);
  log_line(l);
  upd_state = ok ? UPD_INSTALLED : UPD_FAILED;
  sceKernelExitDeleteThread(0);
  return 0;
}

static void upd_install_start(void) {
  if (upd_state != UPD_AVAILABLE) return;
  upd_cancel = 0;
  upd_done = 0;
  upd_total = upd_nfiles;
  upd_msg[0] = 0;
  upd_state = UPD_DOWNLOADING;
  if (!upd_silent && upd_card != 1) { /* la carte du haut montre la progression */
    upd_card = 1;
    upd_card_t = 0.0f;
  }
  SceUID th = sceKernelCreateThread("ap_majins", upd_install_thread, 0x10000100, 0x20000, 0, 0, NULL);
  if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    if (th >= 0) sceKernelDeleteThread(th);
    snprintf(upd_msg, sizeof(upd_msg), "fil impossible");
    upd_state = UPD_FAILED;
  }
}

/* A l'ouverture : pret.txt d'une session precedente. Version plus recente : on l'installe ; sinon il est efface. */
static void upd_resume(void) {
  char t[600];
  int n = read_file(UPD_READY, (unsigned char *)t, (int)sizeof(t) - 1);
  if (n <= 0) return;
  t[n] = 0;
  char v[16] = "";
  if (strncmp(t, "version=", 8) == 0) sscanf(t + 8, "%15[^\r\n]", v);
  if (!v[0] || upd_ver_cmp(v, VF_VERSION_NUM) <= 0) {
    sceIoRemove(UPD_READY);
    return;
  }
  snprintf(upd_ver, sizeof(upd_ver), "%s", v);
  upd_notified = 2;
  upd_silent = 0;
  upd_state = UPD_INSTALLED;
  upd_card = 1;
  upd_card_t = 0.0f;
  log_line("mise a jour : prete depuis la derniere fois, installation");
}

/* Chaque image (fil principal) : carte une seule fois, redemarrage apres l'installation (sauf installation
 * automatique : on attend l'accord, sinon la nouvelle version demarre au prochain lancement). */
static void upd_poll(float dt) {
  static float relaunch = -1.0f;
  static int first = 1;
  if (first) { /* mise a jour prete d'une session precedente ("Plus tard") : installee a l'ouverture */
    first = 0;
    upd_resume();
  }
  if (upd_state == UPD_INSTALLED && !upd_silent) { /* 1,2 s sur l'ecran de mise a jour, puis l'Updater prend le relais */
    if (relaunch < 0.0f) relaunch = 1.2f;
    relaunch -= dt;
    if (relaunch <= 0.0f) {
      log_line("mise a jour : lancement de Area Play Updater");
      int r1 = sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=" UPD_TOOL_ID);
      sceKernelDelayThread(10000);
      int r2 = sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=" UPD_TOOL_ID);
      if (r1 >= 0 || r2 >= 0) sceKernelExitProcess(0); /* l'Updater installe puis relance Area Play */
      char l[120];
      snprintf(l, sizeof(l), "mise a jour : Area Play Updater ne se lance pas (%08X)", (unsigned)r2);
      log_line(l);
      snprintf(upd_msg, sizeof(upd_msg), "%s", L("Area Play Updater n'est pas installé"));
      upd_state = UPD_FAILED;
      upd_card = 1;
      upd_card_t = 0.0f;
      relaunch = -1.0f;
    }
  }
  if (upd_state == UPD_AVAILABLE && !upd_notified) {
    upd_notified = 1;
    if (opt_upd_install) { /* installation automatique : telechargement en silence */
      upd_silent = 1;
      upd_install_start();
    } else {
      upd_card = 1;
      upd_card_t = 0.0f;
    }
  }
  if (upd_silent && (upd_state == UPD_INSTALLED || upd_state == UPD_FAILED) && upd_notified == 1) {
    upd_notified = 2; /* fini en silence : la carte propose de redemarrer (ou de reessayer) */
    upd_card = 1;
    upd_card_t = 0.0f;
  }
}

/* La carte a quelque chose a montrer (le telechargement silencieux reste discret). */
static int upd_card_has(void) {
  if (upd_state == UPD_DOWNLOADING) return 0;      /* ecran de mise a jour, ou telechargement silencieux */
  if (upd_state == UPD_INSTALLED) return upd_silent; /* prete (installation automatique) : X Installer */
  return upd_state == UPD_AVAILABLE || upd_state == UPD_FAILED;
}

/* X / O quand la carte est affichee. */
static void upd_card_key(unsigned int p) {
  int x = (p & SCE_CTRL_CROSS) != 0, o = (p & SCE_CTRL_CIRCLE) != 0;
  int fold = 0;
  switch (upd_state) {
  case UPD_AVAILABLE:
    if (x) upd_install_start();
    else fold = o;
    break;
  case UPD_DOWNLOADING:
    if (o) upd_cancel = 1;
    break;
  case UPD_INSTALLED:
    if (upd_silent && x) upd_silent = 0; /* installer maintenant (Area Play Updater) */
    else if (upd_silent) fold = o;
    break;
  case UPD_FAILED:
    if (x) {
      upd_state = UPD_AVAILABLE;
      upd_silent = 0;
      upd_install_start();
    } else fold = o;
    break;
  default: break;
  }
  if (fold) { /* "Plus tard" : la carte file en bas a droite, a gauche d'Area Play */
    upd_card = 2;
    upd_fold = 0.0f;
  }
}

/* Carte de mise a jour en haut a droite (sous l'heure et la batterie) ; repliee : petite icone a gauche
 * du nom d'Area Play en bas a droite (un toucher la rouvre). Rien quand une fenetre est ouverte. */
static void upd_card_draw(float dt, int hidden) {
  if (!upd_card || !upd_card_has()) return;
  /* rectangle de la carte */
  char title[80];
  const char *hints = NULL;
  int bar = 0;
  switch (upd_state) {
  case UPD_AVAILABLE:
    snprintf(title, sizeof(title), L("Mise à jour %s disponible"), upd_ver);
    hints = "X Installer;O Plus tard";
    break;
  case UPD_DOWNLOADING:
    snprintf(title, sizeof(title), L("Mise à jour %s"), upd_ver);
    hints = "O Annuler";
    bar = 1;
    break;
  case UPD_INSTALLED:
    snprintf(title, sizeof(title), L(upd_silent ? "Mise à jour %s prête" : "Mise à jour %s"), upd_ver);
    hints = upd_silent ? "X Installer;O Plus tard" : NULL;
    break;
  default:
    snprintf(title, sizeof(title), "%s", L(upd_cancel ? "Mise à jour annulée" : "Échec de la mise à jour"));
    hints = "X Réessayer;O Plus tard";
    break;
  }
  float w = text_w(ST_SEMI, FS_BODY, title) + 48 + 22;
  if (w < 290) w = 290;
  float h = 70, x = CONTENT_R - w, y = BAR_H + 6 + (opt_fps ? 34 : 0);
  /* icone repliee : a gauche de "Area Play x.y" du pied de page */
  int vw = text_w(ST_REG, FS_SMALL, VF_VERSION);
  float ix = CONTENT_R - vw - 16, iy = 526;
  if (hidden) return;

  if (upd_card == 1) {
    if (upd_card_t < 1.0f) upd_card_t += dt * 4.0f;
    if (upd_card_t > 1.0f) upd_card_t = 1.0f;
    float e = upd_card_t * upd_card_t * (3.0f - 2.0f * upd_card_t), a = e;
    x += (1.0f - e) * 40.0f;
    soft_shadow(x + 3, y + 6, w - 6, h - 4, 16, 12, fade(C(0, 0, 0, 110), a));
    round_card(x, y, w, h, 16, T.sel_fill, T.acc, a);
    draw_set_icon(ICO_DOWNLOAD, x + 26, y + 24, fade(T.acc_text, a));
    draw_text(ST_SEMI, (int)x + 48, (int)y + 30, FS_BODY, fade(T.text, a), title);
    float hx = x + 48;
    if (bar) { /* progression : fichiers recus / total */
      int total = upd_total > 0 ? upd_total : 1;
      float bw = 110, fr = (float)upd_done / (float)total;
      round_rect(hx, y + 45, bw, 8, 4, fade(T.card_border, a));
      if (fr > 0.04f) round_rect(hx, y + 45, bw * fr, 8, 4, fade(T.acc, a));
      hx += bw + 16;
    }
    if (hints) draw_hints((int)hx, (int)y + 56, FS_SMALL, fade(T.text_soft, a), hints);
    else draw_text(ST_REG, (int)hx, (int)y + 54, FS_SMALL, fade(T.muted, a), L("Installation..."));
    hit_add(x, y, w, h, H_UPD, 0);
    return;
  }
  /* repliee : la carte rapetisse en filant vers l'icone, puis l'icone reste (legere pulsation) */
  if (upd_fold < 1.0f) {
    upd_fold += dt * 2.5f;
    if (upd_fold > 1.0f) upd_fold = 1.0f;
    float f = upd_fold * upd_fold * (3.0f - 2.0f * upd_fold);
    float cw = w + (24 - w) * f, ch = h + (24 - h) * f;
    float cx = (x + w / 2) + (ix - (x + w / 2)) * f, cy = (y + h / 2) + (iy - (y + h / 2)) * f;
    round_card(cx - cw / 2, cy - ch / 2, cw, ch, 12, T.sel_fill, T.acc, 1.0f - f * 0.6f);
  }
  float pulse = 0.75f + 0.25f * sinf(g_time * 3.0f);
  draw_set_icon(ICO_DOWNLOAD, ix, iy, fade(T.acc, upd_fold * pulse));
  hit_add(ix - 16, FOOTER_Y, CONTENT_R - ix + 16, 544 - FOOTER_Y, H_UPD, 1);
}

/* ---- Ecran de mise a jour : l'ecran de demarrage d'Area Play (fond, logo, titre, barre) avec les etapes qui
 * defilent sous la barre. Pendant le telechargement puis le passage a Area Play Updater, qui reprend le meme
 * ecran la ou il s'arrete (barre a UPD_HANDOFF) : on ne voit pas le changement d'appli. ---- */
static int upd_screen_on(void) {
  return !upd_silent && (upd_state == UPD_DOWNLOADING || upd_state == UPD_INSTALLED);
}

/* Message qui defile : le precedent monte et s'efface, le nouveau arrive par le bas. */
static void upd_msg_draw(const char *msg, float dt, float a) {
  static char cur[96], prev[96];
  static float k = 1.0f;
  if (strcmp(cur, msg) != 0) {
    snprintf(prev, sizeof(prev), "%s", cur);
    snprintf(cur, sizeof(cur), "%s", msg);
    k = 0.0f;
  }
  if (k < 1.0f) k += dt * 3.0f;
  if (k > 1.0f) k = 1.0f;
  float e = k * k * (3.0f - 2.0f * k);
  if (prev[0] && e < 1.0f)
    draw_text_center(ST_REG, SCREEN_W / 2, (int)(392 - 18 * e), FS_BODY, fade(T.text, a * (1.0f - e)), prev);
  draw_text_center(ST_REG, SCREEN_W / 2, (int)(392 + 18 * (1.0f - e)), FS_BODY, fade(T.text, a * e), cur);
}

static void upd_screen_draw(float dt) {
  static float a;
  static int was_on;
  if (!upd_screen_on()) {
    if (was_on) splash_prog = -1.0f; /* annule : la barre de l'ecran de demarrage repart de zero */
    was_on = 0;
    a = 0.0f;
    return;
  }
  was_on = 1;
  if (a < 1.0f) a += dt * 5.0f;
  if (a > 1.0f) a = 1.0f;
  char m[96];
  if (upd_state == UPD_DOWNLOADING) {
    int total = upd_total > 0 ? upd_total : 1;
    splash_prog = 0.05f + (UPD_HANDOFF - 0.15f) * upd_done / total;
    snprintf(m, sizeof(m), L("Téléchargement de la mise à jour %s"), upd_ver);
  } else {
    splash_prog = UPD_HANDOFF;
    snprintf(m, sizeof(m), "%s", L("Préparation de l'installation..."));
  }
  fill_rect(0, 0, SCREEN_W, 544, fade(C(0, 0, 0, 255), a)); /* rien de l'interface ne transparait */
  splash_content("", a, g_time);
  upd_msg_draw(m, dt, a);
  if (upd_state == UPD_DOWNLOADING)
    draw_hints(SCREEN_W / 2 - 40, 500, FS_SMALL, fade(T.muted, a), "O Annuler");
}

/* Toucher la carte (= X) ou l'icone repliee (la carte revient). */
static void upd_card_touch(unsigned int *pressed) {
  if (upd_card == 2) {
    upd_card = 1;
    upd_card_t = 0.0f;
  } else if (upd_card == 1) {
    *pressed |= SCE_CTRL_CROSS;
  }
}

/* Texte de la ligne "Mise a jour" des reglages. */
static void upd_status_text(char *out, int n) {
  switch (upd_state) {
  case UPD_CHECKING: snprintf(out, n, "%s", L("Recherche...")); break;
  case UPD_UPTODATE: snprintf(out, n, L("À jour (%s)"), VF_VERSION_NUM); break;
  case UPD_AVAILABLE: snprintf(out, n, L("%s disponible : X pour installer"), upd_ver); break;
  case UPD_ERROR: snprintf(out, n, "%s", L("Pas de réponse : X pour réessayer")); break;
  case UPD_DOWNLOADING: snprintf(out, n, L("Téléchargement %d / %d"), upd_done, upd_total); break;
  case UPD_INSTALLED:
    snprintf(out, n, "%s", L(upd_silent ? "Prête : X pour installer" : "Installation..."));
    break;
  case UPD_FAILED: snprintf(out, n, "%s", L("Échec : X pour réessayer")); break;
  default: snprintf(out, n, L("Version %s : X pour chercher"), VF_VERSION_NUM); break;
  }
}

/* ======================================================================
 *  Nouveautes (Reglages > Systeme) : la mise a jour disponible (notes de latest.txt), puis l'historique
 *  livre avec l'appli (app0:assets/nouveautes.txt : "= 1.1", puis des lignes "fr: ..." et "en: ...").
 *  Haut / Bas, L / R et le glissement font defiler ; X, O ou un toucher ferment.
 * ====================================================================== */
#define NV_MAX 200
#define NV_W 640
#define NV_ROWS 12
typedef struct {
  char t[100];
  unsigned char kind; /* 0 titre de version, 1 debut de puce, 2 suite de puce, 3 espace */
} NvLine;
static NvLine nv_lines[NV_MAX];
static int nv_n, nv_open, nv_top;

static void nv_add(int kind, const char *s) {
  if (nv_n >= NV_MAX) return;
  snprintf(nv_lines[nv_n].t, sizeof(nv_lines[0].t), "%s", s);
  nv_lines[nv_n++].kind = (unsigned char)kind;
}
/* Une nouveaute, coupee aux espaces sur la largeur de la fenetre. */
static void nv_bullet(const char *s) {
  char line[100] = "", trial[200];
  int first = 1;
  while (*s) {
    while (*s == ' ') ++s;
    const char *e = s;
    while (*e && *e != ' ') ++e;
    if (e == s) break;
    snprintf(trial, sizeof(trial), line[0] ? "%s %.*s" : "%s%.*s", line, (int)(e - s), s);
    if (line[0] && text_w(ST_REG, FS_BODY, trial) > NV_W - 110) {
      nv_add(first ? 1 : 2, line);
      first = 0;
      snprintf(line, sizeof(line), "%.*s", (int)(e - s), s);
    } else {
      snprintf(line, sizeof(line), "%s", trial);
    }
    s = e;
  }
  if (line[0]) nv_add(first ? 1 : 2, line);
}

static void nv_open_win(void) {
  static char buf[16384];
  char h[100];
  nv_n = nv_top = 0;
  nv_open = 1;
  if (upd_ver[0] && upd_ver_cmp(upd_ver, VF_VERSION_NUM) > 0 && upd_notes_n > 0) { /* en ligne, pas encore installee */
    snprintf(h, sizeof(h), L("Version %s disponible"), upd_ver);
    nv_add(0, h);
    for (int i = 0; i < upd_notes_n; ++i) nv_bullet(upd_notes[i]);
  }
  int n = read_file("app0:assets/nouveautes.txt", (unsigned char *)buf, (int)sizeof(buf) - 1);
  buf[n > 0 ? n : 0] = 0;
  const char *want = lang_code[0] && strstr(buf, "\nen:") ? "en:" : "fr:"; /* anglais pour toute autre langue */
  for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    while (*line == ' ') ++line;
    if (*line == '=') {
      char *v = line + 1;
      while (*v == ' ') ++v;
      if (nv_n) nv_add(3, "");
      snprintf(h, sizeof(h), L(strcmp(v, VF_VERSION_NUM) ? "Version %s" : "Version %s (installée)"), v);
      nv_add(0, h);
    } else if (!strncmp(line, want, 3)) {
      nv_bullet(line + 3);
    }
  }
  if (!nv_n) nv_add(1, L("Aucune information"));
}

static void nv_input(unsigned int pressed) {
  int max = nv_n - NV_ROWS;
  if (pressed & SCE_CTRL_UP) --nv_top;
  if (pressed & SCE_CTRL_DOWN) ++nv_top;
  if (pressed & SCE_CTRL_LTRIGGER) nv_top -= NV_ROWS - 1;
  if (pressed & SCE_CTRL_RTRIGGER) nv_top += NV_ROWS - 1;
  if (nv_top > max) nv_top = max;
  if (nv_top < 0) nv_top = 0;
  if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS)) nv_open = 0;
}

static void nv_draw(void) {
  float w = NV_W, h = 452, x = (SCREEN_W - w) / 2, y = 40;
  modal_panel(x, y, w, h, 170);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 44, FS_SECTION, T.text, "Nouveautés");
  for (int i = 0; i < NV_ROWS && nv_top + i < nv_n; ++i) {
    const NvLine *l = &nv_lines[nv_top + i];
    int ly = (int)y + 90 + i * 27;
    if (l->kind == 0) {
      draw_text(ST_SEMI, (int)x + 28, ly, FS_BODY, T.acc, l->t);
    } else if (l->kind != 3) {
      if (l->kind == 1) fill_rect(x + 40, ly - 9, 5, 5, T.text_soft); /* puce */
      draw_text(ST_REG, (int)x + 56, ly, FS_BODY, T.text_soft, l->t);
    }
  }
  if (nv_n > NV_ROWS) { /* barre de defilement */
    float th = 12 * 27 - 4, ty = y + 72, bh = th * NV_ROWS / nv_n;
    round_rect(x + w - 18, ty, 4, th, 2, T.card_border);
    round_rect(x + w - 18, ty + (th - bh) * nv_top / (nv_n - NV_ROWS), 4, bh, 2, T.acc);
  }
  draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted,
             nv_n > NV_ROWS ? ". Haut / Bas : défiler;O Fermer" : "O Fermer");
}

/* ======================================================================
 *  Area Play Updater livre avec Area Play (dossier app0:updater du VPK).
 *  Au demarrage, dans un fil : absent -> installe (fichiers copies dans ux0:temp, puis inscrits par l'installateur
 *  du systeme, comme un VPK installe par un explorateur) ; present mais ancien -> son eboot.bin est remplace.
 * ====================================================================== */
#include <psp2/promoterutil.h>
#define TOOL_SRC "app0:updater"
#define TOOL_APP "ux0:app/" UPD_TOOL_ID
#define TOOL_TMP "ux0:temp/areaplay_updater"

/* Copie un fichier (dossiers parents crees). 1 si ok. */
static int tool_copy(const char *src, const char *dst) {
  static unsigned char b[64 * 1024];
  char d[200];
  snprintf(d, sizeof(d), "%s", dst);
  for (char *s = strchr(d, '/'); s; s = strchr(s + 1, '/')) {
    *s = 0;
    sceIoMkdir(d, 0777);
    *s = '/';
  }
  SceUID i = sceIoOpen(src, SCE_O_RDONLY, 0);
  if (i < 0) return 0;
  SceUID o = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  int ok = o >= 0;
  while (ok) {
    int n = sceIoRead(i, b, sizeof(b));
    if (n <= 0) break;
    ok = sceIoWrite(o, b, n) == n;
  }
  sceIoClose(i);
  if (o >= 0) sceIoClose(o);
  return ok;
}
static unsigned tool_crc(const char *path, int *size) { /* crc32 d'un fichier (taille dans *size, -1 = absent) */
  static unsigned char b[64 * 1024];
  unsigned c = 0;
  *size = -1;
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return 0;
  *size = 0;
  for (;;) {
    int n = sceIoRead(f, b, sizeof(b));
    if (n <= 0) break;
    c = (unsigned)crc32(c, b, (uInt)n);
    *size += n;
  }
  sceIoClose(f);
  return c;
}

/* Fichiers de l'Updater : chemin dans app0:updater (ou ailleurs) -> chemin dans son dossier. */
static const struct {
  const char *src, *dst;
} tool_files[] = {
    {TOOL_SRC "/eboot.bin", "eboot.bin"},
    {"app0:assets/fonts/Inter-Regular.ttf", "assets/fonts/Inter-Regular.ttf"}, /* les polices d'Area Play */
    {"app0:assets/fonts/Inter-SemiBold.ttf", "assets/fonts/Inter-SemiBold.ttf"},
    {TOOL_SRC "/sce_sys/param.sfo", "sce_sys/param.sfo"},
    {TOOL_SRC "/sce_sys/icon0.png", "sce_sys/icon0.png"},
    {TOOL_SRC "/sce_sys/pic0.png", "sce_sys/pic0.png"},
    {TOOL_SRC "/sce_sys/livearea/contents/template.xml", "sce_sys/livearea/contents/template.xml"},
    {TOOL_SRC "/sce_sys/livearea/contents/bg0.png", "sce_sys/livearea/contents/bg0.png"},
    {TOOL_SRC "/sce_sys/livearea/contents/startup.png", "sce_sys/livearea/contents/startup.png"},
    {TOOL_SRC "/sce_sys/package/head.bin", "sce_sys/package/head.bin"}, /* demande par l'installateur du systeme */
};

static void tool_rmtree(const char *dir) {
  SceUID d = sceIoDopen(dir);
  if (d < 0) return;
  SceIoDirent e;
  char p[256];
  for (;;) {
    memset(&e, 0, sizeof(e));
    if (sceIoDread(d, &e) <= 0) break;
    if (e.d_name[0] == '.') continue;
    snprintf(p, sizeof(p), "%s/%s", dir, e.d_name);
    if (SCE_S_ISDIR(e.d_stat.st_mode)) tool_rmtree(p);
    else sceIoRemove(p);
  }
  sceIoDclose(d);
  sceIoRmdir(dir);
}

static volatile int tool_state; /* 0 rien a faire, 1 installe, 2 mis a jour, -1 echec */
static int tool_thread(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  char l[160], dst[200];
  int sa, sb;
  unsigned ca = tool_crc(TOOL_SRC "/eboot.bin", &sa), cb = tool_crc(TOOL_APP "/eboot.bin", &sb);
  snprintf(l, sizeof(l), "updater : verification (livre %d octets %08X, installe %d octets %08X)", sa, ca, sb, cb);
  log_line(l);
  if (sa <= 0) { /* VPK sans l'Updater (compilation de test) */
    return 0;
  }
  if (sb > 0) { /* deja installe : mise a jour de ses fichiers si besoin */
    if (sa != sb || ca != cb) {
      int ok = tool_copy(TOOL_SRC "/eboot.bin", TOOL_APP "/eboot.bin.tmp");
      if (ok) {
        sceIoRemove(TOOL_APP "/eboot.bin");
        ok = sceIoRename(TOOL_APP "/eboot.bin.tmp", TOOL_APP "/eboot.bin") >= 0;
      }
      snprintf(l, sizeof(l), "updater : mis a jour %s", ok ? "ok" : "ECHEC");
      log_line(l);
      tool_state = ok ? 2 : -1;
    }
      return 0;
  }
  /* absent : dossier temporaire puis installateur du systeme */
  tool_rmtree(TOOL_TMP);
  sceIoMkdir("ux0:temp", 0777);
  int ok = 1;
  for (int i = 0; i < (int)(sizeof(tool_files) / sizeof(tool_files[0])) && ok; ++i) {
    snprintf(dst, sizeof(dst), TOOL_TMP "/%s", tool_files[i].dst);
    ok = tool_copy(tool_files[i].src, dst);
    if (!ok) {
      snprintf(l, sizeof(l), "updater : copie impossible de %s", tool_files[i].src);
      log_line(l);
    }
  }
  int r = -1, res = -1, st = 1;
#define TOOL_LOG(...)                         do {                                          snprintf(l, sizeof(l), __VA_ARGS__);        log_line(l);                              } while (0)
  TOOL_LOG("updater : fichiers copies dans " TOOL_TMP " %s", ok ? "ok" : "ECHEC");
  if (ok) {
    { /* l'installateur s'appuie sur la bibliotheque d'interface du systeme (PAF) : chargee avant, avec son tas */
      static uint32_t opt[0x100];
      uint32_t paf_args[5] = {0x400000, 0xEA60, 0x40000, 0, 0};
      memset(opt, 0, sizeof(opt));
      opt[1] = (uint32_t)&opt[0];
      r = sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(paf_args), paf_args,
                                                (SceSysmoduleOpt *)opt);
      TOOL_LOG("updater : module PAF %08X", (unsigned)r);
    }
    r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    TOOL_LOG("updater : module d'installation %08X", (unsigned)r);
    int inited = 0;
    if (r >= 0) {
      r = scePromoterUtilityInit();
      inited = r >= 0;
      TOOL_LOG("updater : init %08X", (unsigned)r);
    }
    if (r >= 0) {
      r = scePromoterUtilityPromotePkgWithRif(TOOL_TMP, 1);
      TOOL_LOG("updater : inscription demandee %08X", (unsigned)r);
    }
    for (int k = 0; r >= 0 && k < 300; ++k) { /* 30 s au plus */
      int g = scePromoterUtilityGetState(&st);
      if (k % 10 == 0) TOOL_LOG("updater : etat %d (%08X)", st, (unsigned)g);
      if (g < 0 || st == 0) break;
      sceKernelDelayThread(100 * 1000);
    }
    if (r >= 0) {
      r = scePromoterUtilityGetResult(&res);
      TOOL_LOG("updater : resultat %08X (%08X)", (unsigned)res, (unsigned)r);
      if (r >= 0) r = res;
    }
#undef TOOL_LOG
    if (inited) scePromoterUtilityExit(); /* sans module charge, ses fonctions bloquent : pas d'appel */
  }
  tool_rmtree(TOOL_TMP);
  ok = ok && r >= 0 && file_exists(TOOL_APP "/eboot.bin");
  snprintf(l, sizeof(l), "updater : installation %s (%08X)", ok ? "ok" : "ECHEC", (unsigned)r);
  log_line(l);
  tool_state = ok ? 1 : -1;
  return 0;
}
static void tool_start(void) {
  SceUID th = sceKernelCreateThread("ap_updater", tool_thread, 0x10000100, 0x10000, 0, 0, NULL);
  int r = th >= 0 ? sceKernelStartThread(th, 0, NULL) : th;
  if (r < 0) { /* pas de fil : tout de suite */
    char l[80];
    snprintf(l, sizeof(l), "updater : fil impossible (%08X), verification directe", (unsigned)r);
    log_line(l);
    tool_thread(0, NULL);
  }
}
/* Fil principal : message une fois l'Updater installe. */
static void tool_poll(void) {
  if (tool_state == 1) {
    snprintf(toast_text, sizeof(toast_text), "%s", L("Area Play Updater installé"));
    toast_time = 3.0f;
  }
  if (tool_state != 0) tool_state = 0;
}
