/* Area Play - Reglages (settings.txt) et dernieres parties. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Sauvegarde : options et "dernieres parties"
 * ====================================================================== */

/* Ancien format (jusqu'a la 4.0) : octets a positions fixes. Lu une seule fois, puis converti. */
static int settings_load_legacy(void) {
  SceUID f = sceIoOpen(SETTINGS_FILE, SCE_O_RDONLY, 0);
  if (f < 0) return 0;
  unsigned char b[24];
  memset(b, 0, sizeof(b));
  int n = sceIoRead(f, b, sizeof(b));
  sceIoClose(f);
  opt_sys_mode = n >= 16 && b[15] < 3 ? b[15] : 0;
  opt_wall_dim = n >= 18 && b[17] < 3 ? b[17] : 1;
  if (n < 5) return 0;
  opt_backdrop = b[0] ? 1 : 0;
  opt_gloss = b[1] ? 1 : 0;
  opt_theme = b[2] < THEME_COUNT ? b[2] : 0;
  opt_accent = b[3] < ACCENT_COUNT ? b[3] : 0;
  opt_view = b[4] < VIEW_COUNT ? b[4] : 0;
  view_default = opt_view;
  if (n >= 6) opt_show_id = b[5] ? 1 : 0;
  for (int i = 0; i < 3; ++i) view_of[i] = n >= 9 ? (b[6 + i] < VIEW_COUNT ? b[6 + i] : 0) : opt_view;
  opt_sort = n >= 10 && b[9] < 3 ? b[9] : 0;
  view_of[3] = n >= 11 && b[10] < VIEW_COUNT ? b[10] : opt_view;
  for (int i = 4; i < 8; ++i) view_of[i] = n >= 15 && b[11 + i - 4] < VIEW_COUNT ? b[11 + i - 4] : opt_view;
  view_of[VIEW_CTX_COLL] = n >= 17 && b[16] < VIEW_COUNT ? b[16] : opt_view;
  return 1;
}

/* Reglages : fichier texte "cle=valeur" (lisible, et l'ajout d'un reglage ne decale plus les autres). */
static struct {
  const char *key;
  int *val, max; /* valeurs acceptees : 0 .. max-1 */
} settings_keys[] = {
    {"fond_dynamique", &opt_backdrop, 2}, {"reflets", &opt_gloss, 2},       {"theme", &opt_theme, THEME_COUNT},
    {"accent", &opt_accent, ACCENT_COUNT}, {"vue", &view_default, VIEW_COUNT}, {"identifiant", &opt_show_id, 2},
    {"tri", &opt_sort, 3},                 {"tuiles_systemes", &opt_sys_mode, 3}, {"voile_fond_ecran", &opt_wall_dim, 3},
    {"anticrenelage", &opt_msaa, 2},       {"compteur_images", &opt_fps, 2},
    {"fond_image_agrandie", &opt_bd_icon, 2}, {"sons", &opt_sound, 4}, {"region_jaquettes", &opt_region, 4},
    {"neige", &opt_snow, 2}, {"fond_demarrage", &opt_splash_bg, 2},               {"noel_propose", &xmas_asked, 10000}, {"noel_theme_avant", &xmas_prev, 10000}};

static void settings_save(void) {
  char buf[2048];
  int len = snprintf(buf, sizeof(buf), "# " VF_APP_NAME " : reglages (cle=valeur)\n");
  for (int i = 0; i < LEN(settings_keys); ++i)
    len += snprintf(buf + len, sizeof(buf) - len, "%s=%d\n", settings_keys[i].key, *settings_keys[i].val);
  for (int i = 0; i < VIEW_CTX_COLL; ++i) /* vue de chaque menu (systemes) */
    len += snprintf(buf + len, sizeof(buf) - len, "vue_menu%d=%d\n", i, view_of[i]);
  len += snprintf(buf + len, sizeof(buf) - len, "vue_collections=%d\n", view_of[VIEW_CTX_COLL]);
  len += snprintf(buf + len, sizeof(buf) - len, "vue_recherche=%d\n", view_of[VIEW_CTX_SEARCH]);
  sceIoMkdir(DATA_DIR, 0777);
  SceUID f = sceIoOpen(SETTINGS_TXT, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  sceIoWrite(f, buf, len);
  sceIoClose(f);
}

static void settings_load(void) {
  char buf[2048];
  int n = read_file(SETTINGS_TXT, (unsigned char *)buf, sizeof(buf) - 1);
  if (n <= 0) { /* premier lancement de cette version : conversion de settings.bin */
    if (settings_load_legacy()) {
      for (int i = 0; i < VIEW_CTX_N; ++i) view_user[i] = view_of[i] != view_default;
      settings_save();
      sceIoRemove(SETTINGS_FILE);
    }
    return;
  }
  buf[n] = 0;
  for (int i = 0; i < VIEW_CTX_N; ++i) view_of[i] = -1;
  for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    char *eq = strchr(line, '=');
    if (!eq || line[0] == '#') continue;
    *eq = 0;
    int v = atoi(eq + 1);
    for (int i = 0; i < LEN(settings_keys); ++i)
      if (strcmp(line, settings_keys[i].key) == 0 && v >= 0 && v < settings_keys[i].max) *settings_keys[i].val = v;
    if (strncmp(line, "vue_menu", 8) == 0) {
      int k = atoi(line + 8);
      if (k >= 0 && k < VIEW_CTX_COLL && v >= 0 && v < VIEW_COUNT) view_of[k] = v;
    }
    if (strcmp(line, "vue_collections") == 0 && v >= 0 && v < VIEW_COUNT) view_of[VIEW_CTX_COLL] = v;
    if (strcmp(line, "vue_recherche") == 0 && v >= 0 && v < VIEW_COUNT) view_of[VIEW_CTX_SEARCH] = v;
  }
  opt_view = view_default;
  for (int i = 0; i < VIEW_CTX_N; ++i) {
    if (view_of[i] < 0) view_of[i] = view_default;
    view_user[i] = view_of[i] != view_default; /* une vue differente du defaut = choisie */
  }
}

/* dir = +1 / -1 : sens de changement pour les choix a plusieurs valeurs. */
static void build_lists(void);
static void change_setting(int row, int dir) {
  switch (row) {
  case SR_BACKDROP: opt_backdrop = !opt_backdrop; break;
  case SR_GLOSS: opt_gloss = !opt_gloss; break;
  case SR_SHOW_ID: opt_show_id = !opt_show_id; break;
  case SR_FPS: opt_fps = !opt_fps; break;
  case SR_BD_ICON: opt_bd_icon = !opt_bd_icon; break;
  case SR_SOUND: opt_sound = (opt_sound + dir + 4) % 4; break;
  case SR_REGION: opt_region = (opt_region + dir + 4) % 4; break;
  case SR_LANG: lang_set((lang_index() + dir + lang_count) % lang_count); break;
  case SR_MSAA: opt_msaa = !opt_msaa; break; /* pris en compte au prochain demarrage */
  case SR_SNOW: opt_snow = !opt_snow; break;
  case SR_SPLASH_BG: opt_splash_bg = !opt_splash_bg; break;
  case SR_THEME:
    opt_theme = (opt_theme + dir + (int)THEME_COUNT) % (int)THEME_COUNT;
    xmas_prev = 0; /* choisi a la main : il ne change plus tout seul apres Noel */
    if (themes[opt_theme].accent >= 0) opt_accent = themes[opt_theme].accent;
    break;
  case SR_ACCENT: opt_accent = (opt_accent + dir + (int)ACCENT_COUNT) % (int)ACCENT_COUNT; break;
  case SR_VIEW: /* meme vue pour tous les menus */
    view_default = (view_default + dir + VIEW_COUNT) % VIEW_COUNT;
    for (int i = 0; i < VIEW_CTX_N; ++i)
      if (!view_user[i]) view_of[i] = view_default; /* les menus ou tu as choisi une vue la gardent */
    last_view_ctx = -2; /* forcer la relecture de la vue du menu courant */
    break;
  case SR_SYS_MODE: opt_sys_mode = (opt_sys_mode + dir + 3) % 3; break;
  case SR_WALL_DIM: opt_wall_dim = (opt_wall_dim + dir + 3) % 3; break;
  case SR_SORT:
    opt_sort = (opt_sort + dir + 3) % 3;
    build_lists();
    break;
  default: return;
  }
  apply_theme();
  settings_save();
}

/* Vue de chaque menu : 0 = PS Vita, 1 = PSP, 2 = Homebrews (-1 ailleurs). */
static int view_all_same(void) {
  for (int i = 1; i < VIEW_CTX_N; ++i)
    if (view_of[i] != view_of[0]) return 0;
  return 1;
}
static int view_ctx(int page, int level, int sys) {
  if (page == PG_HB) return 2;
  if (page == PG_FAV) return 3;
  if (page == PG_COLL) return level == 1 ? VIEW_CTX_COLL : -1;
  if (page == PG_SEARCH) return VIEW_CTX_SEARCH;
  if (page == 1 && level == 1) return sys >= 0 && sys < 2 ? sys : (sys >= 2 && sys < 2 + MAX_CUSTOM_SYS ? 2 + sys : 0);
  return -1;
}

/* Quand on change de menu, on reprend la vue memorisee de ce menu. */
static void sync_view(int page, int level, int sys) {
  int vc = view_ctx(page, level, sys);
  if (vc != last_view_ctx) {
    last_view_ctx = vc;
    if (vc >= 0) opt_view = view_of[vc];
  }
}

#define RECENT_MAX 16
typedef struct {
  char id[16];
  long long t;
} Recent;
static Recent recents[RECENT_MAX];
static int n_recents;

static void recents_load(void) {
  SceUID f = sceIoOpen(RECENT_FILE, SCE_O_RDONLY, 0);
  if (f < 0) return;
  int cnt = 0;
  if (sceIoRead(f, &cnt, sizeof(cnt)) == (int)sizeof(cnt) && cnt >= 0 &&
      cnt <= RECENT_MAX) {
    int want = cnt * (int)sizeof(Recent);
    if (want == 0 || sceIoRead(f, recents, want) == want) n_recents = cnt;
  }
  sceIoClose(f);
}

static void recents_save(void) {
  sceIoMkdir(DATA_DIR, 0777);
  SceUID f = sceIoOpen(RECENT_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  sceIoWrite(f, &n_recents, sizeof(n_recents));
  if (n_recents > 0) sceIoWrite(f, recents, n_recents * (int)sizeof(Recent));
  sceIoClose(f);
}

static long long recents_get(const char *id) {
  for (int i = 0; i < n_recents; ++i)
    if (strcmp(recents[i].id, id) == 0) return recents[i].t;
  return 0;
}

/* t == 0 : supprime l'entree. */
static void recents_set(const char *id, long long t) {
  for (int i = 0; i < n_recents; ++i) {
    if (strcmp(recents[i].id, id) != 0) continue;
    if (t == 0) {
      recents[i] = recents[n_recents - 1];
      --n_recents;
    } else {
      recents[i].t = t;
    }
    return;
  }
  if (t == 0) return;
  int slot = n_recents;
  if (n_recents >= RECENT_MAX) { /* plein : on remplace la plus ancienne */
    slot = 0;
    for (int i = 1; i < n_recents; ++i)
      if (recents[i].t < recents[slot].t) slot = i;
  } else {
    ++n_recents;
  }
  snprintf(recents[slot].id, sizeof(recents[slot].id), "%s", id);
  recents[slot].t = t;
}

static void ago_text(long long t, char *out, int n) {
  long long d = (long long)time(NULL) - t;
  if (d < 0) d = 0;
  if (d < 90) snprintf(out, n, "à l'instant");
  else if (d < 3600) snprintf(out, n, L("il y a %d min"), (int)(d / 60));
  else if (d < 86400) snprintf(out, n, L("il y a %d h"), (int)(d / 3600));
  else if (d < 86400LL * 60) snprintf(out, n, L(d / 86400 > 1 ? "il y a %d jours" : "il y a %d jour"), (int)(d / 86400));
  else snprintf(out, n, L("il y a %d mois"), (int)(d / (86400LL * 30)));
}

