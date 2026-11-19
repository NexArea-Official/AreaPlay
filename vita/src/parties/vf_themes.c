/* Area Play - Themes en fichiers (saisons, fetes : Noel, Halloween...). Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Un theme = un dossier avec un fichier theme.txt (voir THEMES.md) :
 *    app0:assets/themes/<id>/        themes livres avec l'appli (Noel)
 *    ux0:data/AreaPlay/themes/<id>/  themes ajoutes / telecharges (meme <id> : remplace celui de l'appli)
 *  Il donne : couleurs, fond d'ecran, animation (neige, feuilles, oiseaux...), decorations (houx...),
 *  icones du menu, neige posee sur les cartes, guirlande, sons clochettes, periode de l'annee.
 *  Theme avec "proposer=1" : propose une fois par an au debut de sa periode, l'ancien theme revient apres.
 *  Les 5 themes de base (Sombre, Clair, Nexa, Braise, Foret) restent dans le code (vf_dessin.c).
 * ====================================================================== */

static int opt_snow = 1; /* animation du theme (Reglages > Theme) ; cle "neige" */
static int xmas_asked;   /* anciennes cles (Noel, 1.0) : reprises une fois dans custom.txt */
static int xmas_prev;

static const char *custom_get(const char *key);
static void custom_set(const char *key, const char *file);
static void custom_save(void);
static void tex_free_later(vita2d_texture *t);
static void wall_set(const char *ref);

enum { FX_NEIGE, FX_TOMBER, FX_CONFETTIS, FX_VOLER, FX_SOLEIL, FX_POUSSER, FX_FEUX, FX_SAUTER, FX_FLOTTER, FX_TYPES };
#define FX_MAXP 120
#define FX_IMG 4
typedef struct {
  int type, count;
  float s0, s1; /* taille (largeur en points) */
  int nimg, ncol;
  char img[FX_IMG][40];
  vita2d_texture *tex[FX_IMG];
  unsigned int col[6];
} ThemeFx;

typedef struct {
  char id[24], dir[72];
  char name[32], name_en[32], desc[72], desc_en[72];
  int propose, accent;
  int easter, p0, p1; /* periode : MMJJ (ou decalage en jours autour de Paques) ; p0 < 0 = aucune */
  unsigned int c[12]; /* fond, texte, texte_doux, discret, ligne, barre, carte, bord_carte, verre, interrupteur, degrade haut/bas */
  unsigned int title; /* etiquettes (sinon couleur d'accent) */
  char wall[40];
  int shade, bells, glow;
  int ngcol;
  unsigned int gcol[6]; /* guirlande : couleurs des ampoules (0 = pas de guirlande) */
  float gsize;          /* taille des ampoules (1 = Noel) */
  char tile[40], spider[40]; /* toile d'araignee dans les coins du bandeau, araignee qui pend */
  char fg[40];                /* image au premier plan, devant les animations (parasol) */
  float fgx, fgy, fgw;
  char frame[40];             /* cadre du bandeau (rubans de Paques) : image decoupee en 9 morceaux */
  int frm[5];                 /* marge autour du bandeau, puis taille des coins : gauche, haut, droite, bas */
  char cover[3][40];          /* posee en bas des jaquettes (herbe et oeufs), une image tiree par jeu */
  int ncover;
  char snow[64];        /* neige posee : prefixe des images (…1.png, …1_petite.png) */
  int snow_src[3][3];
  char deco[4][40], icon[3][40]; /* deco : carte, menu (haut), bouton Lancer, menu (bas) ; icones : Accueil, Collections, Homebrews */
  int nfx;
  ThemeFx fx[2];
} FileTheme;

#define FT_MAX 24
static int fx_for = -1; /* theme pour lequel les particules sont preparees */
static FileTheme ft[FT_MAX];
static int ft_n;
static int ft_loaded = -1; /* theme dont les images sont en memoire */
static vita2d_texture *ft_deco[4][2], *ft_ico[3], *ft_snow[3][2], *ft_tile[2], *ft_spider, *ft_fg, *ft_frame, *ft_cover[3];
static char ft_wall_dir[72]; /* dossier du theme courant (fonds proposes dans Fond d'ecran) */

static int theme_count(void) { return BUILTIN_THEMES + ft_n; }
static FileTheme *ftc(void) { return opt_theme >= BUILTIN_THEMES && opt_theme - BUILTIN_THEMES < ft_n ? &ft[opt_theme - BUILTIN_THEMES] : NULL; }
static int xmas_on(void) { return ftc() != NULL; } /* un theme en fichier est actif */
static const char *theme_name(int i) {
  if (i < BUILTIN_THEMES) return themes[i < 0 ? 0 : i].name;
  const FileTheme *t = &ft[i - BUILTIN_THEMES];
  return lang_code[0] && t->name_en[0] ? t->name_en : t->name;
}
static int theme_accent(int i) { return i < BUILTIN_THEMES ? themes[i].accent : ft[i - BUILTIN_THEMES].accent; }
static const char *theme_id(int i) { return i < BUILTIN_THEMES ? theme_file[i] : ft[i - BUILTIN_THEMES].id; }
static int theme_find(const char *id) {
  if (!id) return -1;
  for (int i = 0; i < theme_count(); ++i)
    if (strcmp(theme_id(i), id) == 0) return i;
  return -1;
}
/* couleur des etiquettes (« Jouer maintenant », systeme d'une carte) */
static unsigned int theme_title(unsigned int def) {
  const FileTheme *t = ftc();
  return t && t->title ? t->title : def;
}
static int theme_glow(void) { return ftc() && ftc()->glow; }
static int theme_has_fx(void) { return ftc() && ftc()->nfx > 0; }

/* ---- Lecture de theme.txt ---- */
static unsigned int ft_color(const char *s) {
  while (*s == ' ') ++s;
  if (*s == '#') ++s;
  unsigned int v = (unsigned int)strtoul(s, NULL, 16);
  int n = 0;
  while (isxdigit((unsigned char)s[n])) ++n;
  if (n == 8) return C((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255);
  return C((v >> 16) & 255, (v >> 8) & 255, v & 255, 255);
}
/* "12-01" -> 1201 ; "paques-7" -> decalage -7 (*easter = 1) */
static int ft_date(const char *s, int *easter) {
  while (*s == ' ') ++s;
  if (strncmp(s, "paques", 6) == 0) {
    *easter = 1;
    return atoi(s + 6);
  }
  return atoi(s) * 100 + (strchr(s, '-') ? atoi(strchr(s, '-') + 1) : 0);
}
static void ft_word(char **p, char *out, int n) { /* mot suivant (separe par espaces) */
  while (**p == ' ') ++*p;
  int k = 0;
  while (**p && **p != ' ') {
    if (k < n - 1) out[k++] = **p;
    ++*p;
  }
  out[k] = 0;
}
static void ft_parse_fx(ThemeFx *f, char *v) {
  static const char *names[FX_TYPES] = {"neige", "tomber", "confettis", "voler", "soleil", "pousser", "feux", "sauter", "flotter"};
  char w[48];
  memset(f, 0, sizeof(*f));
  ft_word(&v, w, sizeof(w));
  f->type = -1;
  for (int i = 0; i < FX_TYPES; ++i)
    if (strcmp(w, names[i]) == 0) f->type = i;
  ft_word(&v, w, sizeof(w));
  f->count = atoi(w);
  if (f->count < 1) f->count = 1;
  if (f->count > FX_MAXP) f->count = FX_MAXP;
  ft_word(&v, w, sizeof(w));
  f->s0 = (float)atof(w);
  f->s1 = strchr(w, '-') ? (float)atof(strchr(w, '-') + 1) : f->s0;
  for (;;) {
    ft_word(&v, w, sizeof(w));
    if (!w[0]) break;
    if (w[0] == '#' && f->ncol < 6) f->col[f->ncol++] = ft_color(w);
    else if (w[0] != '#' && f->nimg < FX_IMG) snprintf(f->img[f->nimg++], sizeof(f->img[0]), "%s", w);
  }
}

static int ft_read(FileTheme *t, const char *dir, const char *id) {
  char p[112];
  static char buf[4096];
  snprintf(p, sizeof(p), "%s/%s/theme.txt", dir, id);
  int n = read_file(p, (unsigned char *)buf, sizeof(buf) - 1);
  if (n <= 0) return 0;
  buf[n] = 0;
  memset(t, 0, sizeof(*t));
  snprintf(t->id, sizeof(t->id), "%s", id);
  snprintf(t->dir, sizeof(t->dir), "%s/%s", dir, id);
  snprintf(t->name, sizeof(t->name), "%s", id);
  t->accent = -1;
  t->p0 = -1;
  static const char *ckeys[12] = {"fond", "texte", "texte_doux", "discret", "ligne", "barre", "carte", "bord_carte", "verre", "interrupteur", "degrade_haut", "degrade_bas"};
  for (int i = 0; i < 12; ++i) t->c[i] = 0;
  for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    while (*line == ' ') ++line;
    char *eq = strchr(line, '=');
    if (line[0] == '#' || !eq) continue;
    *eq = 0;
    char *k = line, *v = eq + 1;
    for (char *e = eq - 1; e >= k && *e == ' '; --e) *e = 0;
    while (*v == ' ') ++v;
    if (!strcmp(k, "nom")) snprintf(t->name, sizeof(t->name), "%s", v);
    else if (!strcmp(k, "nom_en")) snprintf(t->name_en, sizeof(t->name_en), "%s", v);
    else if (!strcmp(k, "description")) snprintf(t->desc, sizeof(t->desc), "%s", v);
    else if (!strcmp(k, "description_en")) snprintf(t->desc_en, sizeof(t->desc_en), "%s", v);
    else if (!strcmp(k, "proposer")) t->propose = atoi(v) != 0;
    else if (!strcmp(k, "accent")) t->accent = atoi(v) < (int)ACCENT_COUNT ? atoi(v) : -1;
    else if (!strcmp(k, "periode") && strchr(v, ',')) {
      t->p0 = ft_date(v, &t->easter);
      t->p1 = ft_date(strchr(v, ',') + 1, &t->easter);
    } else if (!strcmp(k, "titre")) t->title = ft_color(v);
    else if (!strcmp(k, "fond_ecran")) snprintf(t->wall, sizeof(t->wall), "%s", v);
    else if (!strcmp(k, "fond_sombre_bas")) t->shade = atoi(v) != 0;
    else if (!strcmp(k, "sons")) t->bells = strcmp(v, "clochettes") == 0;
    else if (!strcmp(k, "bouton_lumineux")) t->glow = atoi(v) != 0;
    else if (!strcmp(k, "guirlande")) {
      char w[16];
      while (t->ngcol < 6) {
        ft_word(&v, w, sizeof(w));
        if (!w[0]) break;
        t->gcol[t->ngcol++] = ft_color(w);
      }
    } else if (!strcmp(k, "guirlande_taille")) t->gsize = (float)atof(v);
    else if (!strcmp(k, "deco_tuile")) snprintf(t->tile, sizeof(t->tile), "%s", v);
    else if (!strcmp(k, "araignee")) snprintf(t->spider, sizeof(t->spider), "%s", v);
    else if (!strcmp(k, "premier_plan")) {
      ft_word(&v, t->fg, sizeof(t->fg));
      char w[16];
      ft_word(&v, w, sizeof(w));
      t->fgx = (float)atof(w);
      ft_word(&v, w, sizeof(w));
      t->fgy = (float)atof(w);
      ft_word(&v, w, sizeof(w));
      t->fgw = (float)atof(w);
    }
    else if (!strcmp(k, "cadre")) {
      ft_word(&v, t->frame, sizeof(t->frame));
      char w[16];
      for (int i = 0; i < 5; ++i) {
        ft_word(&v, w, sizeof(w));
        t->frm[i] = atoi(w);
      }
    } else if (!strcmp(k, "deco_jaquette"))
      for (t->ncover = 0; t->ncover < 3; ++t->ncover) {
        ft_word(&v, t->cover[t->ncover], sizeof(t->cover[0]));
        if (!t->cover[t->ncover][0]) break;
      }
    else if (!strcmp(k, "neige_posee")) snprintf(t->snow, sizeof(t->snow), "%s", v);
    else if (!strcmp(k, "neige_raccords")) {
      for (int i = 0; i < 3; ++i) {
        char w[32];
        ft_word(&v, w, sizeof(w));
        sscanf(w, "%d,%d,%d", &t->snow_src[i][0], &t->snow_src[i][1], &t->snow_src[i][2]);
      }
    } else if (!strcmp(k, "deco_carte")) snprintf(t->deco[0], sizeof(t->deco[0]), "%s", v);
    else if (!strcmp(k, "deco_menu")) { /* une image, ou deux : haut puis bas du menu */
      ft_word(&v, t->deco[1], sizeof(t->deco[1]));
      ft_word(&v, t->deco[3], sizeof(t->deco[3]));
    }
    else if (!strcmp(k, "deco_bouton")) snprintf(t->deco[2], sizeof(t->deco[2]), "%s", v);
    else if (!strcmp(k, "icones"))
      for (int i = 0; i < 3; ++i) ft_word(&v, t->icon[i], sizeof(t->icon[0]));
    else if (!strcmp(k, "effet") && t->nfx < 2) {
      ft_parse_fx(&t->fx[t->nfx], v);
      if (t->fx[t->nfx].type >= 0) ++t->nfx;
    } else
      for (int i = 0; i < 12; ++i)
        if (!strcmp(k, ckeys[i])) t->c[i] = ft_color(v);
  }
  if (t->snow[0] && !t->snow_src[0][0]) t->snow[0] = 0; /* sans raccords, pas de neige posee */
  if (t->gsize < 0.5f || t->gsize > 3.0f) t->gsize = 1.0f;
  return 1;
}

/* Liste des themes : ceux de l'appli, puis ceux de ux0 (meme nom : celui de ux0 remplace). Tri par nom de dossier. */
static int ft_cmp(const void *a, const void *b) { return strcmp(((const FileTheme *)a)->id, ((const FileTheme *)b)->id); }
static void ft_scan(void) {
  static const char *dirs[2] = {"app0:assets/themes", DATA_DIR "/themes"};
  ft_n = 0;
  for (int d = 0; d < 2; ++d) {
    SceUID dd = sceIoDopen(dirs[d]);
    if (dd < 0) continue;
    SceIoDirent ent;
    for (;;) {
      memset(&ent, 0, sizeof(ent));
      if (sceIoDread(dd, &ent) <= 0) break;
      if (!SCE_S_ISDIR(ent.d_stat.st_mode) || ent.d_name[0] == '.' || strlen(ent.d_name) >= sizeof(ft[0].id)) continue;
      int slot = ft_n;
      for (int i = 0; i < ft_n; ++i)
        if (strcmp(ft[i].id, ent.d_name) == 0) slot = i;
      if (slot >= FT_MAX) continue;
      static FileTheme tmp;
      if (!ft_read(&tmp, dirs[d], ent.d_name)) continue;
      ft[slot] = tmp;
      if (slot == ft_n) ++ft_n;
    }
    sceIoDclose(dd);
  }
  qsort(ft, ft_n, sizeof(ft[0]), ft_cmp);
  char l[64];
  snprintf(l, sizeof(l), "themes en fichiers : %d", ft_n);
  log_line(l);
}

/* ---- Images du theme courant (chargees a la demande, liberees au changement de theme) ---- */
static void ft_path(const FileTheme *t, const char *name, const char *suffix, char *out, int n) {
  char base[96];
  snprintf(base, sizeof(base), "%s", name);
  char *dot = strrchr(base, '.');
  char ext[8] = ".png";
  if (dot && strlen(dot) < sizeof(ext)) {
    snprintf(ext, sizeof(ext), "%s", dot);
    *dot = 0;
  }
  if (strchr(base, ':')) snprintf(out, n, "%s%s%s", base, suffix, ext); /* chemin complet (app0:...) */
  else snprintf(out, n, "%s/%s%s%s", t->dir, base, suffix, ext);
}
static vita2d_texture *ft_tex(const FileTheme *t, const char *name, const char *suffix, int quiet) {
  if (!name || !name[0]) return NULL;
  char p[160];
  ft_path(t, name, suffix, p, sizeof(p));
  vita2d_texture *x = load_image_file(p);
  tex_prep(x);
  if (!x && !quiet) {
    char l[200];
    snprintf(l, sizeof(l), "theme : image absente %s", p);
    log_line(l);
  }
  return x;
}
static void ft_unload(void) {
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 2; ++j) {
      tex_free_later(ft_deco[i][j]);
      tex_free_later(ft_snow[i][j]);
      ft_deco[i][j] = ft_snow[i][j] = NULL;
    }
    tex_free_later(ft_ico[i]);
    tex_free_later(ft_cover[i]);
    ft_ico[i] = ft_cover[i] = NULL;
  }
  for (int j = 0; j < 2; ++j) {
    tex_free_later(ft_deco[3][j]);
    tex_free_later(ft_tile[j]);
    ft_deco[3][j] = ft_tile[j] = NULL;
  }
  tex_free_later(ft_spider);
  tex_free_later(ft_fg);
  tex_free_later(ft_frame);
  ft_spider = ft_fg = ft_frame = NULL;
  if (ft_loaded >= 0 && ft_loaded < ft_n)
    for (int f = 0; f < ft[ft_loaded].nfx; ++f)
      for (int i = 0; i < FX_IMG; ++i) {
        tex_free_later(ft[ft_loaded].fx[f].tex[i]);
        ft[ft_loaded].fx[f].tex[i] = NULL;
      }
  ft_loaded = -1;
  fx_for = -1;
}
static FileTheme *ft_use(void) {
  FileTheme *t = ftc();
  int k = t ? (int)(t - ft) : -1;
  if (k == ft_loaded) return t;
  ft_unload();
  ft_wall_dir[0] = 0;
  if (!t) return NULL;
  ft_loaded = k;
  snprintf(ft_wall_dir, sizeof(ft_wall_dir), "%s", t->dir);
  for (int i = 0; i < 3; ++i) {
    ft_deco[i][0] = ft_tex(t, t->deco[i], "", 0);
    ft_deco[i][1] = ft_deco[i][0] ? ft_tex(t, t->deco[i], "_inverse", 1) : NULL; /* version retournee (facultative) */
    if (i == 2) { /* deco du bas du menu (facultative) */
      ft_deco[3][0] = ft_tex(t, t->deco[3], "", 0);
      ft_deco[3][1] = ft_deco[3][0] ? ft_tex(t, t->deco[3], "_inverse", 1) : NULL;
    }
    ft_ico[i] = ft_tex(t, t->icon[i], "", 0);
  }
  ft_tile[0] = ft_tex(t, t->tile, "", 0);
  ft_tile[1] = ft_tile[0] ? ft_tex(t, t->tile, "_inverse", 1) : NULL;
  ft_spider = ft_tex(t, t->spider, "", 0);
  ft_fg = ft_tex(t, t->fg, "", 0);
  ft_frame = ft_tex(t, t->frame, "", 0);
  for (int i = 0; i < t->ncover; ++i) ft_cover[i] = ft_tex(t, t->cover[i], "", 0);
  if (t->snow[0])
    for (int i = 0; i < 3; ++i) {
      char nm[80];
      snprintf(nm, sizeof(nm), "%s%d.png", t->snow, i + 1);
      ft_snow[i][0] = ft_tex(t, nm, "", 0);
      ft_snow[i][1] = ft_tex(t, nm, "_petite", 1);
    }
  for (int f = 0; f < t->nfx; ++f)
    for (int i = 0; i < t->fx[f].nimg; ++i) t->fx[f].tex[i] = ft_tex(t, t->fx[f].img[i], "", 0);
  return t;
}

/* Fond d'ecran par defaut du theme courant (NULL : theme de base). */
static const char *theme_default_wall(void) {
  static char p[160];
  const FileTheme *t = ftc();
  if (!t || !t->wall[0]) return NULL;
  ft_path(t, t->wall, "", p, sizeof(p));
  return p;
}
/* Fond du theme dont le bas est tres clair (neige) : assombri pour les titres. */
static int theme_wall_shade(const char *wp) {
  const FileTheme *t = ftc();
  return t && t->shade && strncmp(wp, t->dir, strlen(t->dir)) == 0;
}
/* Nom d'un fond du theme courant : « Noël 2 » */
static int theme_wall_label(const char *path, char *out, int n) {
  const FileTheme *t = ftc();
  if (!t || strncmp(path, t->dir, strlen(t->dir)) != 0) return 0;
  const char *b = strrchr(path, '/');
  int num = 0;
  for (const char *q = b ? b : path; *q && *q != '.'; ++q)
    if (*q >= '0' && *q <= '9') num = num * 10 + (*q - '0');
  if (num) snprintf(out, n, "%s %d", theme_name(opt_theme), num);
  else snprintf(out, n, "%s", theme_name(opt_theme));
  return 1;
}

/* Couleurs du theme en fichier i (c : celles de Sombre au depart, seules les couleurs donnees changent). */
static void theme_file_colors(int i, unsigned int *c) {
  if (i < 0 || i >= ft_n) return;
  for (int k = 0; k < 12; ++k)
    if (ft[i].c[k]) c[k] = ft[i].c[k];
}

/* ---- Periode de l'annee (date du jour, ou ux0:data/AreaPlay/date_test.txt "MM-JJ" pour essayer) ---- */
static int doy(int y, int m, int d) {
  static const int cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  if (m < 1) m = 1;
  if (m > 12) m = 12;
  return cum[m - 1] + d + (leap && m > 2);
}
static int easter_doy(int y) { /* Paques (calendrier gregorien) */
  int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451, mo = (h + l - 7 * m + 114) / 31, da = (h + l - 7 * m + 114) % 31 + 1;
  return doy(y, mo, da);
}
static void today(int *y, int *m, int *d) {
  SceDateTime t;
  memset(&t, 0, sizeof(t));
  sceRtcGetCurrentClockLocalTime(&t);
  *y = t.year;
  *m = t.month;
  *d = t.day;
  char b[16];
  if (read_file(DATA_DIR "/date_test.txt", (unsigned char *)b, sizeof(b) - 1) >= 4) {
    b[15] = 0;
    *m = atoi(b);
    *d = strchr(b, '-') ? atoi(strchr(b, '-') + 1) : 1;
  }
}
/* 1 si aujourd'hui est dans la periode du theme ; *key = annee du debut de la periode. */
static int theme_in_period(const FileTheme *t, int *key) {
  if (t->p0 < 0) return 0;
  int y, m, d;
  today(&y, &m, &d);
  int now = doy(y, m, d);
  *key = y;
  if (t->easter) {
    int e = easter_doy(y);
    return now >= e + t->p0 && now <= e + t->p1;
  }
  int s = doy(y, t->p0 / 100, t->p0 % 100), f = doy(y, t->p1 / 100, t->p1 % 100);
  if (s <= f) return now >= s && now <= f;
  if (now >= s) return 1;
  *key = y - 1;
  return now <= f;
}

/* ---- Changement de theme (appele par les Reglages et la proposition de saison) ---- */
static void apply_theme(void);
static void settings_save(void);
static void ft_dir_sync(void) { snprintf(ft_wall_dir, sizeof(ft_wall_dir), "%s", ftc() ? ftc()->dir : ""); }
static void theme_set(int idx, int keep_prev) {
  if (!keep_prev) custom_set("theme_avant", NULL);
  opt_theme = idx >= 0 && idx < theme_count() ? idx : 0;
  ft_dir_sync();
  if (theme_accent(opt_theme) >= 0) opt_accent = theme_accent(opt_theme);
  custom_set("theme_id", theme_id(opt_theme));
  const char *w = custom_get("wall");
  if (w && w[0] == 'x') wall_set(NULL); /* fond de l'ancien theme : on reprend celui du nouveau */
  custom_save();
  apply_theme();
  settings_save();
}

/* Au demarrage : themes trouves, theme choisi (par son nom de dossier), reprise des cles de Noel 1.0. */
static void theme_init(void) {
  ft_scan();
  int k = theme_find(custom_get("theme_id"));
  if (k < 0 && opt_theme >= BUILTIN_THEMES) k = theme_find("noel"); /* 1.0 : theme 5 = Noel */
  if (k < 0) k = opt_theme < BUILTIN_THEMES ? opt_theme : 0;
  opt_theme = k;
  char v[32];
  if (xmas_asked) {
    snprintf(v, sizeof(v), "%d", xmas_asked);
    custom_set("propose_noel", v);
    xmas_asked = 0;
  }
  if (xmas_prev > 0) {
    int p = xmas_prev - 1;
    snprintf(v, sizeof(v), "%s,%d", theme_file[p / 100 < BUILTIN_THEMES ? p / 100 : 0], p % 100);
    custom_set("theme_avant", v);
    xmas_prev = 0;
  }
  custom_set("theme_id", theme_id(opt_theme));
  custom_save();
  ft_dir_sync();
}

/* Debut d'une periode : propose le theme une fois par an. Fin : l'ancien theme revient. */
static int conf_kind, conf_arg, conf_open;
static void xmas_check(void) {
  int key = 0;
  const FileTheme *t = ftc();
  const char *prev = custom_get("theme_avant");
  if (t && t->propose && prev && !theme_in_period(t, &key)) {
    char id[32];
    snprintf(id, sizeof(id), "%s", prev);
    char *comma = strchr(id, ',');
    int acc = comma ? atoi(comma + 1) : -1;
    if (comma) *comma = 0;
    int k = theme_find(id);
    theme_set(k >= 0 ? k : 0, 0);
    if (acc >= 0 && acc < (int)ACCENT_COUNT) opt_accent = acc;
    apply_theme();
    settings_save();
    snprintf(toast_text, sizeof(toast_text), "%s", L("Fin des fêtes : ton thème habituel est revenu"));
    toast_time = 4.0f;
    t = NULL;
  }
  for (int i = 0; i < ft_n; ++i) {
    if (!ft[i].propose || &ft[i] == t || !theme_in_period(&ft[i], &key)) continue;
    char ck[40], v[16];
    snprintf(ck, sizeof(ck), "propose_%s", ft[i].id);
    const char *done = custom_get(ck);
    if (done && atoi(done) == key) continue;
    snprintf(v, sizeof(v), "%d", key);
    custom_set(ck, v);
    custom_save();
    conf_kind = 4;
    conf_arg = i;
    conf_open = 1;
    return;
  }
}
/* Proposition acceptee : l'ancien theme est garde (sauf s'il etait lui-meme un theme de fete). */
static void theme_accept(int i) {
  if (i < 0 || i >= ft_n) return;
  const FileTheme *t = ftc();
  int keep = t && t->propose && custom_get("theme_avant");
  if (!keep) {
    char v[40];
    snprintf(v, sizeof(v), "%s,%d", theme_id(opt_theme), opt_accent);
    custom_set("theme_avant", v);
  }
  theme_set(BUILTIN_THEMES + i, 1);
}

static unsigned int xm_hash(const char *s) {
  unsigned int h = 0;
  while (s && *s) h = h * 31u + (unsigned char)*s++;
  return h;
}

/* Neige posee sur le bord du haut d'un rectangle (x, y, largeur w) : bout gauche, motif du milieu
 * repete n fois, bout droit, a l'echelle qui tombe juste sur la largeur (rien n'est deforme) ;
 * n est choisi pour une hauteur proche de thick x 2,6.
 * wref : largeur au repos (carte non agrandie) ; le nombre de motifs est choisi sur elle, pour que la neige
 * ne change pas de forme quand la carte grossit a la selection (elle est seulement agrandie).
 * Rien si le theme n'a pas de neige posee. */
static void snow_cap_ref(float x, float y, float w, float wref, unsigned int seed, float thick, float a) {
  FileTheme *th = ft_use();
  if (!th || !th->snow[0] || w < 6.0f || a <= 0.01f) return;
  if (wref <= 0.0f) wref = w;
  int i = (int)(seed % 3u);
  float want = thick * 2.6f;
  vita2d_texture *tex = ft_snow[i][want > 34.0f ? 0 : 1]; /* thick fixe : meme image a toutes les tailles */
  if (!tex) tex = ft_snow[i][0];
  if (!tex || th->snow_src[i][0] <= 0) return;
  float iw = vita2d_texture_get_width(tex), ih = vita2d_texture_get_height(tex);
  float k = iw / th->snow_src[i][0];
  int c1 = (int)(th->snow_src[i][1] * k + 0.5f), c2 = (int)(th->snow_src[i][2] * k + 0.5f);
  float ends = c1 + (iw - c2), seg = (float)(c2 - c1);
  if (seg <= 0.0f) return;
  float wt = w + 5.0f; /* deborde de 2 points a gauche, 3 a droite */
  float wr = wref + 5.0f;
  int n = 0;
  float best = 1e9f;
  for (int t = 0; t <= 12; ++t) {
    float d = fabsf(ih * wr / (ends + t * seg) - want);
    if (d < best) {
      best = d;
      n = t;
    }
  }
  float s = wt / (ends + n * seg);
  float x0 = x - 2.0f;
  float y0 = floorf(y - ih * s * 0.24f + 0.5f); /* le bord passe sous le haut de la couche */
  unsigned int col = fade(C(255, 255, 255, 255), a);
  float src = 0.0f;
  for (int p = 0; p < n + 2; ++p) {
    float sx = p == 0 ? 0.0f : p == n + 1 ? (float)c2 : (float)c1;
    float sw = p == 0 ? (float)c1 : p == n + 1 ? iw - c2 : seg;
    float l = floorf(x0 + src * s + 0.5f), r = floorf(x0 + (src + sw) * s + 0.5f);
    if (r > l) vita2d_draw_texture_tint_part_scale(tex, l, y0, sx, 0, sw, ih, (r - l) / sw, s, col);
    src += sw;
  }
}

static int xm_disc(vita2d_color_vertex *v, float cx, float cy, float r, unsigned int c);
/* Neige du bandeau (x, y, w : bord du cadre) : comme sur Android, de la largeur du bandeau. */
static void snow_cap(float x, float y, float w, unsigned int seed, float thick, float a) {
  snow_cap_ref(x - 2.0f, y, w, w, seed, thick, a);
}

/* Decoration du theme (kind : 0 carte, 1 haut du menu, 2 bouton Lancer, 3 bas du menu) dans le cadre (x, y, w de large) ;
 * flip : version retournee (fichier _inverse) ; rot en degres. */
static void holly_kind(int kind, float x, float y, float w, int flip, float rot, float a) {
  if (!ft_use() || a <= 0.01f) return;
  if (kind == 3 && !ft_deco[3][0]) kind = 1; /* bas du menu : meme image qu'en haut si le theme n'en donne pas d'autre */
  vita2d_texture *t = ft_deco[kind][flip && ft_deco[kind][1] ? 1 : 0];
  if (!t) return;
  float tw = vita2d_texture_get_width(t), sc = w / tw, h = vita2d_texture_get_height(t) * sc;
  vita2d_draw_texture_tint_scale_rotate(t, x + w / 2, y + h / 2, sc, sc, rot * 0.0174533f, fade(C(255, 255, 255, 255), a));
}
static void holly_at(float x, float y, float w, int flip, float rot, float a) { holly_kind(0, x, y, w, flip, rot, a); }

/* Icone du menu : 0 Accueil, 1 Collections, 2 Homebrews (0 si le theme n'en a pas). */
static int xmas_icon(int kind, float cx, float cy, float size, unsigned int c) {
  if (!ft_use()) return 0;
  vita2d_texture *t = kind >= 0 && kind < 3 ? ft_ico[kind] : NULL;
  if (!t) return 0;
  float sc = size / vita2d_texture_get_width(t);
  vita2d_draw_texture_tint_scale(t, cx - size / 2, cy - size / 2, sc, sc, c);
  return 1;
}

/* Premiere icone d'un autre theme (fenetre de proposition) : chargee a part, sans toucher au theme courant. */
static int theme_icon_preview(int i, float cx, float cy, float size, unsigned int c) {
  static vita2d_texture *pv;
  static int pv_for = -1;
  if (i < 0 || i >= ft_n) return 0;
  if (pv_for != i) {
    tex_free_later(pv);
    pv = ft_tex(&ft[i], ft[i].icon[0], "", 1);
    pv_for = i;
  }
  if (!pv) return 0;
  float sc = size / vita2d_texture_get_width(pv);
  vita2d_draw_texture_tint_scale(pv, cx - size / 2, cy - size / 2, sc, sc, c);
  return 1;
}

/* Petit generateur pseudo-aleatoire (meme graine, memes valeurs). */
static unsigned int xm_rs;
static float xm_rnd(void) {
  xm_rs = xm_rs * 1664525u + 1013904223u;
  return (float)(xm_rs >> 8) / 16777216.0f;
}

/* Disque plein ajoute a une liste de triangles (8 cotes). */
static int xm_disc(vita2d_color_vertex *v, float cx, float cy, float r, unsigned int c) {
  static const float co[9] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f, 1};
  static const float si[9] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0};
  for (int k = 0; k < 8; ++k) {
    vtx_set(&v[k * 3], cx, cy, c);
    vtx_set(&v[k * 3 + 1], cx + co[k] * r, cy + si[k] * r, c);
    vtx_set(&v[k * 3 + 2], cx + co[k + 1] * r, cy + si[k + 1] * r, c);
  }
  return 24;
}

/* ======================================================================
 *  Animations du theme (derriere les menus et les cartes)
 * ====================================================================== */
typedef struct {
  float x, y, s, v, ph, r, rv, t, life;
  int img, state;
} Particle;
static Particle fx_p[2][FX_MAXP];

static float fx_size(const ThemeFx *f) { return f->s0 + xm_rnd() * (f->s1 - f->s0); }

static void fx_spawn(const ThemeFx *f, Particle *p, int first) {
  memset(p, 0, sizeof(*p));
  p->img = f->nimg > 0 ? (int)(xm_rnd() * f->nimg) % f->nimg : 0;
  p->ph = xm_rnd() * 6.28f;
  switch (f->type) {
  case FX_NEIGE:
    p->x = xm_rnd() * SCREEN_W;
    p->y = first ? xm_rnd() * 544.0f : -4.0f;
    p->s = 0.8f + xm_rnd() * 2.2f;
    p->v = 0.25f + xm_rnd() * 0.7f;
    break;
  case FX_TOMBER:
  case FX_CONFETTIS:
    p->s = fx_size(f);
    p->x = xm_rnd() * SCREEN_W;
    p->y = first ? xm_rnd() * 544.0f : -p->s - xm_rnd() * 80.0f;
    p->v = (f->type == FX_CONFETTIS ? 0.6f : 0.35f) + xm_rnd() * 0.6f;
    p->r = xm_rnd() * 6.28f;
    p->rv = (xm_rnd() - 0.5f) * (f->type == FX_CONFETTIS ? 0.12f : 0.04f);
    p->img = f->type == FX_CONFETTIS ? (f->ncol ? (int)(xm_rnd() * f->ncol) % f->ncol : 0) : p->img;
    break;
  case FX_VOLER: /* de gauche a droite, un peu plus haut ou plus bas, avec une attente avant de repartir */
    p->s = fx_size(f);
    p->x = first ? xm_rnd() * SCREEN_W : -p->s - xm_rnd() * 500.0f;
    p->y = 70.0f + xm_rnd() * 300.0f;
    p->v = 0.9f + xm_rnd() * 0.9f;
    p->img = 0;
    break;
  case FX_SOLEIL:
    p->s = f->s0;
    p->x = SCREEN_W - 70.0f;
    p->y = 66.0f;
    break;
  case FX_FEUX: /* fusee : monte (0), eclate (1), attend (2) */
    p->x = 240.0f + xm_rnd() * (SCREEN_W - 300.0f);
    p->y = 70.0f + xm_rnd() * 170.0f; /* hauteur de l'explosion */
    p->s = fx_size(f);                /* rayon de l'explosion */
    p->state = first ? 2 : 0;
    p->t = 0.0f;
    p->life = first ? xm_rnd() * 2.5f : 0.3f + xm_rnd() * 2.2f;
    p->img = f->ncol ? (int)(xm_rnd() * f->ncol) % f->ncol : 0;
    p->r = 0.8f + xm_rnd() * 0.4f; /* duree de la montee */
    break;
  case FX_SAUTER: /* lapin : bonds de gauche a droite au bas de l'ecran, puis attend avant de repasser */
    p->s = fx_size(f);
    p->x = first ? 200.0f + xm_rnd() * (SCREEN_W - 300.0f) : -p->s - xm_rnd() * 700.0f;
    p->y = (float)FOOTER_Y - 2.0f;
    p->v = 1.1f + xm_rnd() * 0.6f;
    break;
  case FX_FLOTTER: /* bouee : derive lentement sur la mer et se balance */
    p->s = fx_size(f);
    p->x = 0.0f;
    p->y = 0.0f;
    p->ph = xm_rnd() * 6.28f;
    break;
  case FX_POUSSER: /* fleur qui pousse depuis le bas : pousse, reste, se fane, attend */
    p->s = fx_size(f);
    p->x = 170.0f + xm_rnd() * (SCREEN_W - 190.0f);
    p->y = (float)FOOTER_Y + 4.0f; /* pousse au-dessus du pied de page */
    p->state = first ? (int)(xm_rnd() * 4) : 3;
    p->t = 0.0f;
    p->life = first ? xm_rnd() * 6.0f : 1.0f + xm_rnd() * 5.0f;
    break;
  }
}

/* Image centree en (cx, cy), largeur w, rotation r (radians). */
static void fx_sprite(vita2d_texture *t, float cx, float cy, float w, float r, unsigned int col) {
  if (!t) return;
  float sc = w / vita2d_texture_get_width(t);
  vita2d_draw_texture_tint_scale_rotate(t, cx, cy, sc, sc, r, col);
}

static void fx_run(ThemeFx *f, Particle *ps, float fr, float dt) {
  int n = f->count;
  if (f->type == FX_NEIGE || f->type == FX_CONFETTIS) { /* formes dessinees : un seul envoi au GPU */
    vita2d_color_vertex *v = vtx_alloc(n * (f->type == FX_NEIGE ? 24 : 6));
    if (!v) return;
    int k = 0;
    for (int i = 0; i < n; ++i) {
      Particle *p = &ps[i];
      p->y += p->v * fr;
      p->ph += 0.01f * fr;
      p->x += sinf(p->ph) * 0.3f * fr;
      if (p->x < 0) p->x += SCREEN_W;
      if (p->x > SCREEN_W) p->x -= SCREEN_W;
      if (f->type == FX_NEIGE) {
        if (p->y > 548.0f) fx_spawn(f, p, 0);
        k += xm_disc(v + k, p->x, p->y, p->s, C(255, 255, 255, 217));
      } else {
        if (p->y > 544.0f + p->s) fx_spawn(f, p, 0);
        p->r += p->rv * fr;
        p->ph += 0.05f * fr;
        float w = p->s * 0.5f * fabsf(cosf(p->ph * 2.0f)) + 0.6f, h = p->s * 0.32f; /* papier qui tourne sur lui-meme */
        float c = cosf(p->r), s = sinf(p->r);
        unsigned int col = f->ncol ? f->col[p->img % f->ncol] : C(255, 255, 255, 255);
        float ax = -w * c + h * s, ay = -w * s - h * c, bx = w * c + h * s, by = w * s - h * c;
        vtx_set(&v[k++], p->x + ax, p->y + ay, col);
        vtx_set(&v[k++], p->x + bx, p->y + by, col);
        vtx_set(&v[k++], p->x - bx, p->y - by, col);
        vtx_set(&v[k++], p->x + bx, p->y + by, col);
        vtx_set(&v[k++], p->x - ax, p->y - ay, col);
        vtx_set(&v[k++], p->x - bx, p->y - by, col);
      }
    }
    if (k) vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, k);
    return;
  }
  if (f->type == FX_FEUX) { /* feux d'artifice : formes dessinees, un seul envoi au GPU */
    vita2d_color_vertex *v = vtx_alloc(n * (36 * 2 + 3) * 24);
    if (!v) return;
    int k = 0;
    for (int i = 0; i < n; ++i) {
      Particle *p = &ps[i];
      p->t += dt;
      unsigned int col = f->ncol ? f->col[p->img % f->ncol] : C(255, 220, 120, 255);
      if (p->state == 2) { /* attente */
        if (p->t >= p->life) fx_spawn(f, p, 0);
        continue;
      }
      if (p->state == 0) { /* montee : point brillant et sa trainee */
        float u = p->t / p->r;
        if (u >= 1.0f) {
          p->state = 1;
          p->t = 0.0f;
          continue;
        }
        float e = 1.0f - (1.0f - u) * (1.0f - u), by = (float)FOOTER_Y + 10.0f;
        float yy = by + (p->y - by) * e;
        for (int q = 0; q < 3; ++q) k += xm_disc(v + k, p->x, yy + q * 7.0f, 2.2f - q * 0.6f, fade(C(255, 240, 200, 255), 1.0f - q * 0.3f));
        continue;
      }
      /* explosion : 36 etincelles qui s'ecartent, retombent et s'eteignent (1,8 s) */
      float u = p->t / 1.8f;
      if (u >= 1.0f) {
        p->state = 2;
        p->t = 0.0f;
        p->life = 0.2f + xm_rnd() * 1.6f;
        continue;
      }
      float sp = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u), fall = u * u * 40.0f, al = u < 0.7f ? 1.0f : (1.0f - u) / 0.3f;
      if (u < 0.12f) k += xm_disc(v + k, p->x, p->y, p->s * 0.5f * (u / 0.12f), fade(col, 0.25f * (1.0f - u / 0.12f))); /* flash */
      for (int q = 0; q < 36; ++q) {
        float ang = q * 0.17453f + (q & 1) * 0.05f, rr = p->s * (q % 3 == 0 ? 1.0f : 0.82f) * sp;
        float ex = p->x + cosf(ang) * rr, ey = p->y + sinf(ang) * rr + fall;
        k += xm_disc(v + k, ex, ey, 1.9f, fade(col, al));
        k += xm_disc(v + k, p->x + cosf(ang) * rr * 0.86f, p->y + sinf(ang) * rr * 0.86f + fall * 0.8f, 1.2f, fade(col, al * 0.45f)); /* trainee */
      }
    }
    if (k) vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, k);
    return;
  }
  unsigned int white = C(255, 255, 255, 235);
  for (int i = 0; i < n; ++i) {
    Particle *p = &ps[i];
    switch (f->type) {
    case FX_TOMBER: /* feuilles, oeufs : tombent en se balancant */
      p->y += p->v * fr;
      p->ph += 0.02f * fr;
      p->x += sinf(p->ph) * 0.6f * fr;
      p->r += p->rv * fr + sinf(p->ph) * 0.004f * fr;
      if (p->y > 544.0f + p->s) fx_spawn(f, p, 0);
      fx_sprite(f->tex[p->img], p->x, p->y, p->s, p->r, white);
      break;
    case FX_VOLER: { /* oiseaux, chauves-souris : ailes battantes (images 1 et 2) */
      p->x += p->v * fr;
      p->ph += 0.03f * fr;
      p->t += dt;
      if (p->x > SCREEN_W + p->s) fx_spawn(f, p, 0);
      if (p->x < -p->s) break; /* attend son tour, hors de l'ecran */
      int frame = f->nimg > 1 ? (int)(p->t / 0.14f) % f->nimg : 0;
      fx_sprite(f->tex[frame], p->x, p->y + sinf(p->ph) * 18.0f, p->s, sinf(p->ph) * 0.12f, white);
      break;
    }
    case FX_SOLEIL: { /* soleil en haut a droite : rayons qui tournent lentement, halo qui respire */
      p->r += 0.0025f * fr;
      p->ph += 0.02f * fr;
      float pulse = 1.0f + sinf(p->ph) * 0.04f;
      if (f->tex[1]) fx_sprite(f->tex[1], p->x, p->y, p->s * 1.25f * pulse, 0, C(255, 255, 255, 150));
      fx_sprite(f->tex[0], p->x, p->y, p->s * pulse, p->r, C(255, 255, 255, 230));
      break;
    }
    case FX_SAUTER: { /* bonds : 0,7 s en l'air, 0,25 s au sol */
      p->t += dt;
      float cyc = fmodf(p->t, 0.95f);
      float hop = cyc < 0.7f ? sinf(cyc / 0.7f * 3.14159f) : 0.0f;
      if (cyc < 0.7f) p->x += p->v * fr;
      if (p->x > SCREEN_W + p->s) fx_spawn(f, p, 0);
      if (p->x < -p->s) {
        p->x += p->v * fr; /* attend son tour, hors de l'ecran */
        break;
      }
      vita2d_texture *t = f->tex[hop > 0.05f && f->nimg > 1 ? 1 : 0];
      if (!t) break;
      float h = p->s * vita2d_texture_get_height(t) / vita2d_texture_get_width(t);
      fx_sprite(t, p->x, p->y - h / 2 - hop * p->s * 0.9f, p->s, (hop > 0.05f ? (cyc < 0.35f ? -0.18f : 0.12f) : 0.0f), white);
      break;
    }
    case FX_FLOTTER: { /* sur la mer du fond (y 330..390 de l'ecran), va et vient tres lentement */
      p->ph += 0.004f * fr;
      p->r += 0.03f * fr;
      vita2d_texture *t = f->tex[0];
      if (!t) break;
      float cx = 560.0f + sinf(p->ph) * 260.0f, cy = 352.0f + sinf(p->r) * 4.0f;
      fx_sprite(t, cx, cy, p->s, sinf(p->r * 0.8f) * 0.07f, white);
      break;
    }
    case FX_POUSSER: { /* fleurs : 0 pousse, 1 en fleur, 2 se fane, 3 attend */
      p->t += dt;
      p->ph += 0.025f * fr;
      float g = 1.0f;
      if (p->state == 0) g = p->t / 2.2f;
      else if (p->state == 2) g = 1.0f - p->t / 1.6f;
      else if (p->state == 3) g = 0.0f;
      if ((p->state == 0 && p->t >= 2.2f) || (p->state == 2 && p->t >= 1.6f) || (p->state == 1 && p->t >= p->life)) {
        p->state = (p->state + 1) % 4;
        p->t = 0.0f;
        if (p->state == 1) p->life = 6.0f + xm_rnd() * 6.0f;
      } else if (p->state == 3 && p->t >= p->life) {
        fx_spawn(f, p, 0);
        p->state = 0;
      }
      if (g <= 0.01f) break;
      if (g > 1.0f) g = 1.0f;
      g = 1.0f - (1.0f - g) * (1.0f - g); /* pousse vite puis ralentit */
      vita2d_texture *t = f->tex[p->img];
      if (!t) break;
      float w = p->s * g, h = w * vita2d_texture_get_height(t) / vita2d_texture_get_width(t);
      float r = sinf(p->ph) * 0.08f; /* se balance autour de sa tige (pied fixe) */
      fx_sprite(t, p->x + sinf(r) * h / 2, p->y - cosf(r) * h / 2, w, r, C(255, 255, 255, (int)(240 * (p->state == 2 ? g : 1.0f))));
      break;
    }
    }
  }
}

/* dt en secondes ; toutes les animations du theme courant. */
static void snow_fall(float dt) {
  FileTheme *t = ft_use();
  if (!t) return;
  if (opt_snow && t->nfx) {
  if (fx_for != ft_loaded) {
    fx_for = ft_loaded;
    xm_rs = 42;
    for (int f = 0; f < t->nfx; ++f)
      for (int i = 0; i < t->fx[f].count; ++i) fx_spawn(&t->fx[f], &fx_p[f][i], 1);
  }
  float fr = dt * 60.0f; /* en images de 60 Hz */
  if (fr > 4.0f) fr = 4.0f;
  for (int f = 0; f < t->nfx; ++f) fx_run(&t->fx[f], fx_p[f], fr, dt);
  }
  const char *dw = theme_default_wall(); /* premier plan decoupe dans le fond du theme : seulement sur ce fond */
  if (ft_fg && t->fgw > 0.0f && (!wall_path[0] || (dw && strcmp(wall_path, dw) == 0))) { /* parasol, hautes herbes : devant les animations */
    float sc = t->fgw / vita2d_texture_get_width(ft_fg);
    vita2d_draw_texture_scale(ft_fg, t->fgx, t->fgy, sc, sc);
  }
}

/* Guirlande autour de la tuile (x, y, w, h) : le fil pend en festons le long du bord gauche, du bas et
 * du bord droit (le haut est sous la neige), ampoules colorees qui scintillent. Rien si le theme n'en a pas. */
#define XM_PTS 64
static void web_corners(const FileTheme *th, float x, float y, float w, float a);
static void garland(float x, float y, float w, float h, float a) {
  const FileTheme *th = ft_use();
  if (!th || a <= 0.01f) return;
  if (ft_tile[0]) web_corners(th, x, y, w, a);
  if (!th->ngcol) return;
  int top = !th->snow[0]; /* sans neige posee, la guirlande fait aussi le haut */
  float px[XM_PTS], py[XM_PTS], nx[XM_PTS], ny[XM_PTS];
  int np = 0;
#define XM_ADD(X, Y, NX, NY)                                                                                       \
  if (np < XM_PTS) {                                                                                               \
    px[np] = (X);                                                                                                  \
    py[np] = (Y);                                                                                                  \
    nx[np] = (NX);                                                                                                 \
    ny[np] = (NY);                                                                                                 \
    ++np;                                                                                                          \
  }
  const float st = 40.0f;
  for (float yy = 16.0f; yy < h - 20.0f; yy += st) XM_ADD(0, yy, -1, 0)
  XM_ADD(5, h - 5, -0.7f, 0.7f)
  for (float xx = 22.0f; xx < w - 22.0f; xx += st) XM_ADD(xx, h, 0, 1)
  XM_ADD(w - 5, h - 5, 0.7f, 0.7f)
  for (float yy = h - 20.0f; yy > 16.0f; yy -= st) XM_ADD(w, yy, 1, 0)
  XM_ADD(w, 16, 1, 0)
  if (top) {
    XM_ADD(w - 5, 5, 0.7f, -0.7f)
    for (float xx = w - 22.0f; xx > 22.0f; xx -= st) XM_ADD(xx, 0, 0, -1)
    XM_ADD(5, 5, -0.7f, -0.7f)
    XM_ADD(0, 16, -1, 0)
  }
#undef XM_ADD
  if (np < 2) return;
  /* fil : chaque feston est une courbe (8 morceaux) qui pend vers l'exterieur */
  const int SUB = 8;
  vita2d_color_vertex *v = vtx_alloc((np - 1) * SUB * 6);
  if (!v) return;
  unsigned int wc = fade(C(48, 66, 74, 255), a);
  int n = 0;
  for (int q = 1; q < np; ++q) {
    float mx = (nx[q - 1] + nx[q]) / 2, my = (ny[q - 1] + ny[q]) / 2;
    float cx = (px[q - 1] + px[q]) / 2 + mx * 7.0f, cy = (py[q - 1] + py[q]) / 2 + my * 7.0f;
    float lx = px[q - 1], ly = py[q - 1];
    for (int s = 1; s <= SUB; ++s) {
      float t = (float)s / SUB, u = 1 - t;
      float ex = u * u * px[q - 1] + 2 * u * t * cx + t * t * px[q], ey = u * u * py[q - 1] + 2 * u * t * cy + t * t * py[q];
      float dx = ex - lx, dy = ey - ly, len = sqrtf(dx * dx + dy * dy);
      if (len > 0.001f) {
        float ox = -dy / len * 0.6f, oy = dx / len * 0.6f;
        vtx_set(&v[n++], x + lx + ox, y + ly + oy, wc);
        vtx_set(&v[n++], x + lx - ox, y + ly - oy, wc);
        vtx_set(&v[n++], x + ex + ox, y + ey + oy, wc);
        vtx_set(&v[n++], x + ex + ox, y + ey + oy, wc);
        vtx_set(&v[n++], x + lx - ox, y + ly - oy, wc);
        vtx_set(&v[n++], x + ex - ox, y + ey - oy, wc);
      }
      lx = ex;
      ly = ey;
    }
  }
  if (n) vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, n);
  /* ampoules : au creux de chaque feston, nouveau tirage toutes les 0,6 s */
  vita2d_color_vertex *b = vtx_alloc((np - 1) * 72);
  if (!b) return;
  xm_rs = (unsigned int)(g_time / 0.6f) * 2654435761u + 12345u;
  n = 0;
  for (int q = 1; q < np; ++q) {
    float mx = (nx[q - 1] + nx[q]) / 2, my = (ny[q - 1] + ny[q]) / 2;
    float bx = x + (px[q - 1] + px[q]) / 2 + mx * 3.5f, by = y + (py[q - 1] + py[q]) / 2 + my * 3.5f;
    unsigned int c = th->gcol[(q - 1) % th->ngcol];
    int on = xm_rnd() >= 0.25f;
    float gs = th->gsize;
    if (on) n += xm_disc(b + n, bx, by, 5.5f * gs, fade(c, a * (gs > 1.0f ? 0.5f : 0.35f)));
    n += xm_disc(b + n, bx, by, 2.8f * gs, fade(c, a * (on ? 1.0f : 0.35f)));
    if (on && gs > 1.0f) n += xm_disc(b + n, bx, by, 1.3f * gs, fade(C(255, 255, 255, 255), a * 0.8f)); /* reflet blanc */
  }
  if (n) vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, b, n);
}

/* Toiles d'araignee dans les coins du haut du bandeau (x, y, w) ; une araignee monte et descend au bout de son fil. */
static void web_corners(const FileTheme *th, float x, float y, float w, float a) {
  (void)th;
  vita2d_texture *l = ft_tile[0], *r = ft_tile[1] ? ft_tile[1] : ft_tile[0];
  float sz = 92.0f, sc = sz / vita2d_texture_get_width(l);
  unsigned int col = fade(C(255, 255, 255, 230), a);
  vita2d_draw_texture_tint_scale(l, x - 4, y - 4, sc, sc, col);
  vita2d_draw_texture_tint_scale(r, x + w - sz + 4, y - 4, sc, sc, col);
}
/* Image posee en bas d'une jaquette (rx, ry, rw, rh) : herbe et oeufs de Paques ; une des images, choisie par jeu. */
static void theme_cover_deco(float rx, float ry, float rw, float rh, unsigned int seed, float a) {
  const FileTheme *th = ft_use();
  if (!th || !th->ncover || a <= 0.01f) return;
  vita2d_texture *t = ft_cover[seed % (unsigned)th->ncover];
  if (!t) return;
  float w = rw + 8.0f, sc = w / vita2d_texture_get_width(t), h = vita2d_texture_get_height(t) * sc;
  vita2d_draw_texture_tint_scale(t, rx - 4.0f, ry + rh - h * 0.86f, sc, sc, fade(C(255, 255, 255, 255), a));
}

/* Cadre du bandeau (x, y, w, h) : image en 9 morceaux. Les coins (rubans, noeud) gardent leur taille, les bords
 * droits sont etires entre eux : ils se raccordent exactement, quelle que soit la taille du bandeau. */
static void ft_part(vita2d_texture *t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
                    unsigned int c) {
  if (sw > 0 && sh > 0 && dw > 0 && dh > 0) vita2d_draw_texture_tint_part_scale(t, dx, dy, sx, sy, sw, sh, dw / sw, dh / sh, c);
}
static void theme_frame(float x, float y, float w, float h, float a) {
  const FileTheme *th = ft_use();
  if (!th || !ft_frame || a <= 0.01f) return;
  float iw = vita2d_texture_get_width(ft_frame), ih = vita2d_texture_get_height(ft_frame);
  float m = th->frm[0], l = th->frm[1], t = th->frm[2], r = th->frm[3], b = th->frm[4];
  float X = floorf(x - m + 0.5f), Y = floorf(y - m + 0.5f), W = floorf(w + 2 * m + 0.5f), H = floorf(h + 2 * m + 0.5f);
  if (l + r > W) l = r = W / 2; /* bandeau trop petit : les coins se partagent la place */
  if (t + b > H) t = b = H / 2;
  float sx[3] = {0, l, iw - r}, sw[3] = {l, iw - l - r, r}, dx[3] = {X, X + l, X + W - r}, dw[3] = {l, W - l - r, r};
  float sy[3] = {0, t, ih - b}, sh[3] = {t, ih - t - b, b}, dy[3] = {Y, Y + t, Y + H - b}, dh[3] = {t, H - t - b, b};
  unsigned int c = fade(C(255, 255, 255, 255), a);
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i)
      if (i != 1 || j != 1) ft_part(ft_frame, sx[i], sy[j], sw[i], sh[j], dx[i], dy[j], dw[i], dh[j], c);
}

/* Araignee au bout de son fil, devant le contenu du bandeau (appelee a la fin de son dessin). */
static void theme_spider(float x, float y, float w, float a) {
  if (!ft_use() || !ft_spider || !ft_tile[0] || a <= 0.01f) return;
  float sx = x + w - 54.0f, len = 28.0f + 20.0f * sinf(g_time * 0.7f); /* descend et remonte doucement */
  fill_rect(sx - 0.5f, y + 18.0f, 1.0f, len, fade(C(230, 230, 240, 160), a));
  float ss = 26.0f / vita2d_texture_get_width(ft_spider);
  vita2d_draw_texture_tint_scale_rotate(ft_spider, sx, y + 18.0f + len + 8.0f, ss, ss, sinf(g_time * 1.3f) * 0.12f, fade(C(255, 255, 255, 255), a));
}
