/* Area Play - Donnees : jeux, systemes, collections, options. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Donnees : jeux et systemes
 * ====================================================================== */

typedef struct {
  const char *name;       /* titre affiche                              */
  const char *detail;     /* petit texte (identifiant de l'application) */
  unsigned int color;
  const char *cover_path; /* jaquette de demonstration (assets/)        */
  vita2d_texture *cover;  /* jaquette ou icone                          */
  vita2d_texture *bg;     /* fond (pic0.png), charge a la demande       */
  char title[72];
  char id[16];            /* identifiant (ex. PCSB00245)                */
  char icon_path[80];
  char bg_path[80];
  long long last_played;  /* 0 = jamais lance                           */
  int kind;               /* KIND_VITA / KIND_PSP / KIND_HB             */
  int has_bg;             /* 1 = un grand fond (pic0.png) existe        */
  int hidden;             /* 1 = masque par l'utilisateur               */
  int cover_tried;        /* la jaquette a deja ete chargee (ou a echoue) */
  char src_path[160];     /* jeu en fichier : EBOOT.PBP / .iso (PSP), ou ROM */
  unsigned icon_off, icon_size, bg_off, bg_size; /* images dans ce fichier */
  int fav;                /* 1 = favori                                 */
  vita2d_texture *blur;   /* fond flou tire de la jaquette (si pas de grand fond) */
  int cover_pending;      /* 1 = une jaquette (fichier) reste a charger, 2 = en cours (l'icone sert en attendant) */
  unsigned long long coll; /* collections du jeu : bit k = collection k (64 au plus) */
  int bg_queued;          /* 1 = le grand fond est en cours de chargement      */
  int blur_tried;         /* 1 = fond flou deja tente pour l'image actuelle    */
  int icon_idx;           /* icone dans icons.bin (-1 = aucune)                */
  int cover_failed;       /* 1 = fichier de jaquette illisible : l'icone le remplace */
  unsigned seen;          /* derniere image (g_frame) ou le jeu a ete dessine ou demande */
  float appear;           /* fondu d'apparition de l'image (0 = vient d'arriver, 1 = visible) */
  char fold[80];          /* titre et identifiant simplifies (minuscules sans accents) : recherche */
  int rom;                /* ROM lancee par RetroArch : emplacement du systeme + 1 (0 = non) */
} Game;
#define PSP_FILE(g) ((g)->src_path[0] && !(g)->rom) /* jeu PSP en fichier (images lues dans le fichier) */

enum { KIND_VITA = 0, KIND_PSP = 1, KIND_HB = 2 };
#define KIND_CUSTOM0 3    /* kind 3 + k = systeme personnalise (emplacement k) */
#define MAX_CUSTOM_SYS 32 /* systemes ajoutes par l'utilisateur */
static const char *kind_names[3] = {"Jeu PS Vita", "Jeu PSP", "Homebrew"};
static const char *L(const char *fr); /* traduction (fin du fichier) */
static const char *kind_name(int kind);
static const char *kind_label(const Game *g);

/* Donnees de demonstration, utilisees seulement si aucune application
 * n'est trouvee (par exemple dans un emulateur vide). */
static Game demo_vita[] = {
    {"Persona 4 Golden", "RPG", C(184, 111, 57, 255),
     "app0:assets/covers/persona4.jpg", NULL, NULL, "", "", "", "", 0, 0, 0, 0, 0, "", 0, 0, 0, 0},
    {"Gravity Rush", "ACTION", C(67, 127, 171, 255),
     "app0:assets/covers/gravityrush.jpg", NULL, NULL, "", "", "", "", 0, 0, 0, 0, 0, "", 0, 0, 0, 0},
    {"Tearaway", "AVENTURE", C(200, 130, 87, 255),
     "app0:assets/covers/tearaway.jpg", NULL, NULL, "", "", "", "", 0, 0, 0, 0, 0, "", 0, 0, 0, 0},
    {"WipEout 2048", "COURSE", C(69, 102, 184, 255),
     "app0:assets/covers/wipeout2048.jpg", NULL, NULL, "", "", "", "", 0, 0, 0, 0, 0, "", 0, 0, 0, 0}};
static Game demo_homebrew[] = {
    {"VitaShell", "UTILITAIRE", C(71, 139, 121, 255), NULL, NULL, NULL, "", "", "", "", 0, 0, 0, 0, 0, "", 0, 0, 0, 0},
    {"Adrenaline", "PSP", C(70, 96, 163, 255), NULL, NULL, NULL, "", "", "", "", 0, 0, 0, 0, 0, "", 0, 0, 0, 0},
    {"PKGj", "BIBLIOTHÈQUE", C(159, 101, 61, 255), NULL, NULL, NULL, "", "", "", "", 0, 0, 0, 0, 0, "", 0, 0, 0, 0},
    {VF_APP_NAME, "FRONTEND", C(108, 83, 191, 255), NULL, NULL, NULL, "", "", "", "", 0, 0, 0, 0, 0, "", 0, 0, 0, 0}};

/* Bibliotheque reelle */
static Game all_apps[MAX_TOTAL];
static int app_n;
static Game *vita_ptrs[MAX_APPS];
static Game *psp_ptrs[MAX_APPS];
static Game *hb_ptrs[MAX_APPS];
static Game *home_ptrs[4];
static int vita_n, psp_n, hb_n, home_n;
static int demo_mode;
static int missing_icons;    /* icones d'applications introuvables ou illisibles */
static int apps_dropped;     /* applications ignorees : bibliotheque pleine (MAX_TOTAL) */
static int scan_error;       /* code d'erreur de la lecture de ux0:app (0 = ok) */
static char scan_info[112];  /* resume du dernier scan, affiche dans Reglages */
static char own_id[16];      /* identifiant d'Area Play lui-meme             */
static char toast_text[96];  /* message temporaire en bas de l'ecran          */
static float toast_time;

typedef struct {
  const char *name;
  const char *tag;       /* texte court affiche dans la tuile          */
  const char *files[4];  /* noms de fichier d'icone acceptes (sans .png) */
  unsigned int color;
  Game **games;
  int count;
  vita2d_texture *icon;
} System;

static System systems[2 + MAX_CUSTOM_SYS] = {
    {"PS Vita", "VITA", {"vita", "psvita", "ps_vita", NULL}, C(46, 110, 200, 255),
     vita_ptrs, 0, NULL},
    {"PSP / Adrenaline", "PSP", {"psp", "psp_adrenaline", NULL, NULL},
     C(70, 74, 96, 255), psp_ptrs, 0, NULL}};
static int n_systems = 2;
#define SYSTEM_COUNT ((size_t)n_systems)

/* ---- Systemes personnalises (menu Jeux > Ajouter un systeme) ---- */
static const struct {
  const char *tag, *name, *file;
  unsigned int color;
} sys_presets[] = {
    {"PS1", "PlayStation 1", "ps1", C(120, 120, 130, 255)},
    {"EMU", "Émulateurs", "emu", C(150, 80, 60, 255)},
    {"RETRO", "Rétro", "retro", C(160, 120, 40, 255)},
    {"SCUMM", "ScummVM", "scummvm", C(60, 140, 100, 255)},
    {"PSM", "PlayStation Mobile", "psm", C(80, 110, 170, 255)},
    {"OUTILS", "Outils", "outils", C(90, 90, 110, 255)},
    {"AUTRES", "Autres", "autres", C(110, 90, 150, 255)}};
#define PRESET_COUNT LEN(sys_presets)
static char slot_name[MAX_CUSTOM_SYS][32];  /* nom saisi ("" = emplacement libre) */
static char slot_tag[MAX_CUSTOM_SYS][16];
static char slot_file[MAX_CUSTOM_SYS][32];
static unsigned int slot_col[MAX_CUSTOM_SYS];
#define SLOT_USED(k) (slot_name[k][0] != 0)
static int sys_slot[2 + MAX_CUSTOM_SYS];                /* systeme -> emplacement */
static Game *custom_ptrs[MAX_CUSTOM_SYS][MAX_APPS];
static Game *fav_ptrs[MAX_APPS];
static Game *search_ptrs[MAX_APPS]; /* resultats de la recherche */
static int search_n;
static char search_q[28];           /* texte recherche */
static int fav_n;
static int custom_n[MAX_CUSTOM_SYS];
static vita2d_texture *slot_icon[MAX_CUSTOM_SYS];

static const char *kind_name(int kind) {
  if (kind >= 0 && kind < 3) return kind_names[kind];
  int k = kind - KIND_CUSTOM0;
  if (k >= 0 && k < MAX_CUSTOM_SYS && SLOT_USED(k)) return slot_name[k];
  return "Jeu";
}
static const char *kind_label_ex(const Game *g, int with_fav);
static const char *kind_label(const Game *g) { return kind_label_ex(g, 1); }
static const char *kind_label_ex(const Game *g, int with_fav) {
  static char buf[48];
  if (g->kind == KIND_VITA) snprintf(buf, sizeof(buf), "%s", L("JEU PS VITA"));
  else if (g->kind == KIND_PSP) snprintf(buf, sizeof(buf), "%s", L("JEU PSP"));
  else if (g->kind == KIND_HB) snprintf(buf, sizeof(buf), "%s", L("HOMEBREW"));
  else snprintf(buf, sizeof(buf), "%s", kind_name(g->kind));
  if (g->fav && with_fav) {
    size_t l = strlen(buf);
    snprintf(buf + l, sizeof(buf) - l, "  ·  %s", L("FAVORI"));
  }
  return buf;
}

/* ---- Collections : regroupements libres (un jeu garde son systeme et peut etre dans plusieurs collections) ---- */
#define MAX_COLL 64 /* une collection = un bit de Game.coll */
#define COLL_BIT(k) (1ULL << (k))
static char coll_name[MAX_COLL][32]; /* "" = emplacement libre */
#define COLL_USED(k) (coll_name[k][0] != 0)
static int coll_ids[MAX_COLL], coll_n;  /* collections affichees (ordre de creation) -> emplacement */
static Game *coll_ptrs[MAX_COLL][MAX_APPS];
static int coll_cnt[MAX_COLL];

/* Pages du menu de gauche */
enum { PG_HOME = 0, PG_GAMES = 1, PG_HB = 2, PG_SETTINGS = 3, PG_FAV = 4, PG_COLL = 5, PG_SEARCH = 6 };

/* Icones (fichiers remplacables, voir ico_get et assets/icons/ICONES.txt). Les numeros 0 a 23
 * sont ceux des versions precedentes (fichiers ico_<numero>.png toujours acceptes). */
enum {
  ICO_BACKDROP = 0, ICO_GLOSS, ICO_THEME, ICO_ACCENT, ICO_VIEW, ICO_ID, ICO_HIDDEN, ICO_SCAN, ICO_DELETE,
  ICO_SORT, ICO_DOWNLOAD, ICO_UNUSED11, ICO_LIBRARY, ICO_PLAY, ICO_CLOSE, ICO_CLOCK, ICO_KIND, ICO_WALL,
  ICO_DIM, ICO_COLL, ICO_BTN_CROSS, ICO_BTN_CIRCLE, ICO_BTN_SQUARE, ICO_BTN_TRIANGLE, ICO_SYS_TILES,
  ICO_MSAA, ICO_FPS, ICO_BD_ICON, ICO_SEARCH, ICO_SOUND, ICO_QUIT, ICO_M_HOME, ICO_M_GAMES, ICO_M_HB,
  ICO_M_SETTINGS, ICO_M_FAV, ICO_M_COLL, ICO_M_SEARCH, ICO_FAV, ICO_FAV_EMPTY, ICO_N
};
static int rail_ids[7], rail_n;
static int rail_pos(int pg) {
  for (int i = 0; i < rail_n; ++i)
    if (rail_ids[i] == pg) return i;
  return 0;
}
static const char *menu[] = {"Accueil", "Jeux", "Homebrews", /* pastille de 142 de large : le pluriel tient */ "Réglages", "Favoris", "Collections", "Recherche"};
static const char *header_names[] = {"APERÇU", "JEUX", "HOMEBREWS", "RÉGLAGES", "FAVORIS", "COLLECTIONS", "RECHERCHE"};
static const char *view_names[VIEW_COUNT] = {"Carrousel", "Grille", "Liste", "Catalogue"};

/* Couleurs d'accent proposees dans Reglages */
static const struct {
  const char *name;
  unsigned int color;
} accents[] = {{"Violet", C(157, 137, 247, 255)},
               {"Bleu clair", C(95, 190, 255, 255)},
               {"Bleu foncé", C(47, 86, 200, 255)},
               {"Gris", C(150, 155, 170, 255)},
               {"Rouge", C(226, 72, 80, 255)},
               {"Jaune", C(245, 205, 60, 255)},
               {"Orange", C(245, 140, 50, 255)},
               {"Blanc", C(240, 240, 245, 255)},
               {"Noir", C(30, 32, 40, 255)},
               {"Vert clair", C(120, 220, 130, 255)},
               {"Vert foncé", C(40, 130, 70, 255)},
               {"Kaki", C(155, 145, 85, 255)},
               /* ajoutees en 5.3 (apres les 12 premieres : la couleur des jeux n'en depend pas) */
               {"Rose", C(240, 120, 180, 255)},
               {"Fuchsia", C(215, 70, 200, 255)},
               {"Lavande", C(190, 170, 240, 255)},
               {"Indigo", C(90, 80, 200, 255)},
               {"Turquoise", C(60, 200, 190, 255)},
               {"Cyan", C(70, 220, 240, 255)},
               {"Menthe", C(150, 235, 190, 255)},
               {"Citron vert", C(180, 225, 60, 255)},
               {"Or", C(215, 175, 75, 255)},
               {"Corail", C(250, 125, 105, 255)},
               {"Bordeaux", C(150, 40, 70, 255)},
               {"Cuivre", C(200, 115, 70, 255)},
               {"Sable", C(220, 195, 150, 255)},
               {"Ardoise", C(100, 120, 145, 255)}};
#define ACCENT_COUNT LEN(accents)
static int opt_region; /* jaquettes cherchees par nom : 0 Europe, 1 France, 2 USA, 3 Japon */
static const char *region_names[4] = {"Europe", "France", "USA", "Japon"};
#define GAME_COLOR_N 12 /* couleurs des jeux : les 12 premieres (inchangees quand on ajoute des accents) */

static vita2d_pgf *g_pgf;

/* Une police chargee PAR taille et PAR style : le cache de lettres de vita2d
 * abime les glyphes quand une meme police sert plusieurs tailles. */
#define FONT_SIZES 4
static const int font_sizes[FONT_SIZES] = {FS_BODY, FS_SECTION, FS_TITLE, FS_HERO};
static vita2d_font *fonts[2][FONT_SIZES]; /* [0] = regulier, [1] = gras */

/* Options (modifiables dans Reglages, sauvegardees dans ux0:data/AreaPlay) */
static int opt_backdrop = 1; /* 1 = jaquette en fond, 0 = fond uni        */
static int opt_gloss = 1;    /* 1 = cadres glossy                         */
static int opt_theme = 0;    /* index dans themes[] (0 sombre, 1 clair, ...) */
static int opt_accent = 0;   /* index dans accents[]                      */
static int opt_sort = 0;           /* 0 = A-Z, 1 = recents, 2 = favoris d'abord */
static const char *sort_names[3] = {"A-Z", "Récents", "Favoris d'abord"};
static int opt_sys_mode;  /* tuiles du choix de systeme : 0 = 3 par ligne, 1 = 4 par ligne, 2 = 2 lignes */
static const char *sys_mode_names[3] = {"3 par ligne", "4 par ligne", "2 lignes"};
static int opt_view = VIEW_TILE;  /* vue du menu affiche en ce moment */
static int cat_grid;               /* vue Catalogue : 0 = carrousel, 1 = grille */
static float cat_t;                /* vue Catalogue : avancement de la transition vers la grille (0 a 1) */
/* contextes de vue : 0 PS Vita, 1 PSP, 2 Homebrews, 3 Favoris, 4.. systemes ajoutes, puis collections */
#define VIEW_CTX_COLL (4 + MAX_CUSTOM_SYS)
#define VIEW_CTX_SEARCH (VIEW_CTX_COLL + 1)
#define VIEW_CTX_N (VIEW_CTX_SEARCH + 1)
static int view_default;            /* vue proposee par defaut (Reglages) */
static int view_user[VIEW_CTX_N];   /* 1 = vue choisie par l'utilisateur pour ce menu (carre) */
static int view_of[VIEW_CTX_N];    /* vue de chaque menu : PS Vita, PSP, Homebrews, Favoris, systemes personnalises, collections */

/* Fond d'ecran : image choisie dans Reglages > Theme (chemin complet, "" = fond du theme). */
static char wall_path[112];
static vita2d_texture *wall_tex;
static int wall_loaded;           /* 1 = wall_path a deja ete charge (ou a echoue) */
static int opt_wall_dim = 1;      /* voile sur le fond d'ecran : 0 leger, 1 moyen, 2 fort */
static int opt_msaa = 1;          /* anticrenelage 4x (applique au demarrage) */
static int msaa_active;           /* valeur reellement utilisee depuis le demarrage */
static int opt_splash_bg;         /* 1 = ecran de demarrage sur l'image du LiveArea (app0:sce_sys/pic0.png) */
static int opt_fps;               /* 1 = compteur d'images par seconde en haut de l'ecran */
static int opt_bd_icon = 1;       /* jeux sans fond LiveArea : 1 = leur image agrandie, 0 = aucun fond */
static int opt_sound = 2;         /* sons de navigation : 0 non, 1 faibles, 2 moyens, 3 forts */
static const char *sound_names[4] = {"Non", "Faibles", "Moyens", "Forts"};
static const char *wall_dim_names[3] = {"Léger", "Moyen", "Fort"};
static int last_view_ctx = -1;
static int opt_show_id = 1;  /* 1 = afficher l'identifiant sous les titres  */

/* Zone qui a le focus : le menu de gauche ou la page centrale. */
enum { ZONE_RAIL = 0, ZONE_CONTENT = 1, ZONE_SEARCH = 2 }; /* ZONE_SEARCH : cadre de recherche (barre du haut) */
static int search_prev_page; /* page affichee avant la recherche (O y ramene) */
static int g_zone = ZONE_CONTENT;

/* Etat d'animation partage par les fonctions de dessin */
static float focus[MAX_APPS];
static float g_pa = 1.0f; /* fondu de la page        */
static float g_ha = 1.0f; /* fondu du jeu affiche    */
static float g_time;      /* temps ecoule (animations continues) */
static int g_resident;    /* jeux ayant une image en memoire (compte par tex_evict) */
static unsigned g_frame = 1000; /* numero de l'image affichee (images en memoire : voir tex_evict) ;
                                   commence loin de 0 : un jeu jamais dessine (seen = 0) n'est pas "recent" */
static int g_touch_ui;            /* 1 = derniere action au doigt (la barre de lettres s'affiche) */
static float g_touch_idle = 99.0f;/* secondes depuis le dernier contact */
static float g_lb_a;              /* opacite de la barre de lettres (0 = cachee) */
static int g_ox = 0;      /* decalage horizontal     */


/* ======================================================================
 *  Langue de l'interface
 *  Les textes sont ecrits en francais dans le code. L(texte) renvoie sa traduction si une autre langue
 *  est choisie. Une langue = un fichier <code>.txt (app0:assets/lang/ dans le VPK, ou
 *  ux0:data/AreaPlay/lang/ pour en ajouter sans recompiler) : "@name Nom de la langue", puis des paires
 *  de lignes "texte francais" / "traduction" ; lignes vides et "#..." ignorees. Texte absent : francais.
 * ====================================================================== */
#define LANG_HASH 4096 /* puissance de 2 */
#define LANG_MAX 12
static char *lang_buf;
static const char *lang_key[LANG_HASH], *lang_val[LANG_HASH];
static int lang_n;
static char lang_code[16]; /* "" = francais */
static char lang_codes[LANG_MAX][16], lang_names[LANG_MAX][32];
static int lang_count; /* langues trouvees (0 = francais, toujours la) */

static unsigned lang_hash(const char *s) {
  unsigned h = 2166136261u;
  for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
  return h & (LANG_HASH - 1);
}

static const char *L(const char *fr) {
  if (!lang_n || !fr || !fr[0]) return fr;
  for (unsigned h = lang_hash(fr); lang_key[h]; h = (h + 1) & (LANG_HASH - 1))
    if (strcmp(lang_key[h], fr) == 0) return lang_val[h];
  return fr;
}

/* Lit un fichier de langue entier (alloue, termine par 0). */
static char *lang_read(const char *code, int *len) {
  static const char *dirs[2] = {DATA_DIR "/lang", "app0:assets/lang"};
  for (int d = 0; d < 2; ++d) {
    char p[96];
    snprintf(p, sizeof(p), "%s/%s.txt", dirs[d], code);
    SceUID f = sceIoOpen(p, SCE_O_RDONLY, 0);
    if (f < 0) continue;
    SceOff sz = sceIoLseek(f, 0, SCE_SEEK_END);
    sceIoLseek(f, 0, SCE_SEEK_SET);
    char *b = sz > 0 && sz < (1 << 20) ? (char *)malloc((size_t)sz + 1) : NULL;
    if (b && sceIoRead(f, b, (int)sz) == (int)sz) {
      b[sz] = 0;
      *len = (int)sz;
      sceIoClose(f);
      return b;
    }
    free(b);
    sceIoClose(f);
  }
  return NULL;
}

/* Nom de l'appli en majuscules pour le logo, coupe a l'espace ou la ou une majuscule suit une minuscule
 * ("Area Play" -> "AREA" + "PLAY", le logo met un point entre les deux). Sans coupure, b reste vide. */
static void app_logo(char *a, char *b, int n) {
  const char *s = VF_APP_NAME;
  int cut = 0, la = 0, lb = 0;
  for (int i = 1; s[i] && !cut; ++i)
    if (s[i] == ' ' || (s[i] >= 'A' && s[i] <= 'Z' && s[i - 1] >= 'a' && s[i - 1] <= 'z')) cut = i;
  for (int i = 0; s[i]; ++i) {
    char c = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    if (c == ' ') continue;
    if (cut && i >= cut) { if (lb < n - 1) b[lb++] = c; }
    else if (la < n - 1) a[la++] = c;
  }
  a[la] = 0;
  b[lb] = 0;
}

/* Les fichiers de langue ecrivent "VitaFlow" : remplace par le nom de l'appli (VF_APP_NAME) s'il a change,
 * pour que les textes du code (ecrits avec VF_APP_NAME) retrouvent leur traduction. */
static char *lang_rename(char *b, int *len) {
  static const char old[] = "VitaFlow";
  const size_t ol = sizeof(old) - 1, nl = strlen(VF_APP_NAME);
  if (strcmp(VF_APP_NAME, old) == 0) return b;
  int n = 0;
  for (const char *s = strstr(b, old); s; s = strstr(s + ol, old)) ++n;
  char *r = n ? (char *)malloc((size_t)*len + n * nl + 1) : NULL;
  if (!r) return b;
  char *o = r;
  const char *s = b, *f;
  while ((f = strstr(s, old))) {
    memcpy(o, s, (size_t)(f - s));
    o += f - s;
    memcpy(o, VF_APP_NAME, nl);
    o += nl;
    s = f + ol;
  }
  strcpy(o, s);
  *len = (int)(o - r) + (int)strlen(s);
  free(b);
  return r;
}

/* Choisit la langue (code "" = francais). */
static void lang_load(const char *code) {
  free(lang_buf);
  lang_buf = NULL;
  memset(lang_key, 0, sizeof(lang_key));
  lang_n = 0;
  snprintf(lang_code, sizeof(lang_code), "%s", code ? code : "");
  if (!lang_code[0]) return;
  int len = 0;
  lang_buf = lang_read(lang_code, &len);
  if (!lang_buf) {
    lang_code[0] = 0;
    return;
  }
  lang_buf = lang_rename(lang_buf, &len);
  char *p = lang_buf, *key = NULL;
  if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3; /* BOM */
  while (*p) {
    char *e = strchr(p, '\n');
    if (e) *e = 0;
    size_t l = strlen(p);
    if (l && p[l - 1] == '\r') p[--l] = 0;
    if (l && p[0] != '#' && p[0] != '@') {
      if (!key) key = p;
      else { /* paire complete */
        if (lang_n < LANG_HASH / 2) {
          unsigned h = lang_hash(key);
          while (lang_key[h]) h = (h + 1) & (LANG_HASH - 1);
          lang_key[h] = key;
          lang_val[h] = p;
          ++lang_n;
        }
        key = NULL;
      }
    }
    if (!e) break;
    p = e + 1;
  }
}

/* Langues disponibles : francais + chaque fichier .txt des dossiers de langues ("@name" = nom affiche). */
static void lang_scan(void) {
  lang_count = 1;
  snprintf(lang_codes[0], sizeof(lang_codes[0]), "%s", "");
  snprintf(lang_names[0], sizeof(lang_names[0]), "%s", "Français");
  static const char *dirs[2] = {"app0:assets/lang", DATA_DIR "/lang"};
  for (int d = 0; d < 2; ++d) {
    SceUID dd = sceIoDopen(dirs[d]);
    if (dd < 0) continue;
    SceIoDirent ent;
    for (;;) {
      memset(&ent, 0, sizeof(ent));
      if (sceIoDread(dd, &ent) <= 0 || lang_count >= LANG_MAX) break;
      size_t l = strlen(ent.d_name);
      if (l < 5 || l > 15 || strcasecmp(ent.d_name + l - 4, ".txt") != 0) continue;
      char code[16];
      snprintf(code, sizeof(code), "%.*s", (int)(l - 4), ent.d_name);
      int dup = 0;
      for (int i = 0; i < lang_count; ++i)
        if (strcmp(lang_codes[i], code) == 0) dup = 1;
      if (dup) continue;
      int len = 0;
      char *b = lang_read(code, &len), *nm = b ? strstr(b, "@name ") : NULL;
      char name[32];
      snprintf(name, sizeof(name), "%s", code);
      if (nm) {
        nm += 6;
        int o = 0;
        while (nm[o] && nm[o] != '\r' && nm[o] != '\n' && o < (int)sizeof(name) - 1) name[o] = nm[o], ++o;
        name[o] = 0;
      }
      free(b);
      snprintf(lang_codes[lang_count], sizeof(lang_codes[0]), "%s", code);
      snprintf(lang_names[lang_count], sizeof(lang_names[0]), "%s", name);
      ++lang_count;
    }
    sceIoDclose(dd);
  }
}

#define LANG_FILE DATA_DIR "/langue.txt" /* code de la langue choisie */
static void lang_init(void) {
  lang_scan();
  char code[16] = "";
  SceUID f = sceIoOpen(LANG_FILE, SCE_O_RDONLY, 0);
  if (f >= 0) {
    int n = sceIoRead(f, code, sizeof(code) - 1);
    sceIoClose(f);
    code[n > 0 ? n : 0] = 0;
    for (char *q = code; *q; ++q)
      if (*q == '\r' || *q == '\n' || *q == ' ') *q = 0;
  }
  lang_load(code);
}
static int lang_index(void) {
  for (int i = 0; i < lang_count; ++i)
    if (strcmp(lang_codes[i], lang_code) == 0) return i;
  return 0;
}
static void lang_set(int i) { /* Reglages > Affichage > Langue : immediat, garde dans langue.txt */
  if (i < 0 || i >= lang_count) return;
  lang_load(lang_codes[i]);
  SceUID f = sceIoOpen(LANG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f >= 0) {
    sceIoWrite(f, lang_code, strlen(lang_code));
    sceIoClose(f);
  }
}