/* Area Play - Fond d ecran, options d un jeu, systemes, clavier. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Choix du fond d'ecran (Reglages > Theme > Fond d'ecran)
 * ====================================================================== */

#define WP_MAX 64
static struct {
  char ref[64];   /* "a:aurore.jpg" (voir wall_dirs) */
  char label[48]; /* nom affiche */
} wp_items[WP_MAX];
static int wp_n, wp_open, wp_sel;
static vita2d_texture *wp_tex;
static float wp_timer;

static int cmp_wp(const void *a, const void *b) {
  const char *x = (const char *)a, *y = (const char *)b; /* fonds d'hiver d'abord, puis 'a' < 't' < 'u' */
  if (x[0] != y[0]) return (x[0] == 'x' ? 0 : x[0]) - (y[0] == 'x' ? 0 : y[0]);
  return strcasecmp(x + 2, y + 2);
}

static void wp_free_preview(void) {
  tex_free_later(wp_tex);
  wp_tex = NULL;
}

static void wp_open_list(void) {
  wp_n = 0;
  sceIoMkdir(WALL_DIR, 0777);
  for (int d = 0; d < 4; ++d) {
    SceUID dd = sceIoDopen(wall_dirs[d].dir);
    if (dd < 0) continue;
    SceIoDirent ent;
    for (;;) {
      memset(&ent, 0, sizeof(ent));
      if (sceIoDread(dd, &ent) <= 0 || wp_n >= WP_MAX) break;
      const char *nm = ent.d_name;
      if (SCE_S_ISDIR(ent.d_stat.st_mode) || strlen(nm) > 58) continue;
      if (!(ends_with_ci(nm, ".png") || ends_with_ci(nm, ".jpg") || ends_with_ci(nm, ".jpeg"))) continue;
      snprintf(wp_items[wp_n].ref, sizeof(wp_items[0].ref), "%c:%s", wall_dirs[d].tag, nm);
      ++wp_n;
    }
    sceIoDclose(dd);
  }
  qsort(wp_items, wp_n, sizeof(wp_items[0]), cmp_wp);
  wp_sel = 0;
  const char *cur = custom_get("wall");
  for (int i = 0; i < wp_n; ++i) {
    wall_display_name(wp_items[i].ref + 2, wp_items[i].label, sizeof(wp_items[0].label));
    if (wp_items[i].ref[0] == 'u') { /* fonds de l'utilisateur */
      size_t l = strlen(wp_items[i].label);
      if (l < sizeof(wp_items[0].label) - 9) memcpy(wp_items[i].label + l, " (perso)", 9);
    }
    if (cur && strcmp(cur, wp_items[i].ref) == 0) wp_sel = i + 1;
  }
  wp_free_preview();
  wp_timer = 0.05f;
  wp_open = 1;
}

static void wp_close(void) {
  wp_free_preview();
  wp_open = 0;
}

static void wp_move(int sel) {
  if (sel < 0) sel = 0;
  if (sel > wp_n) sel = wp_n;
  if (sel == wp_sel) return;
  wp_sel = sel;
  wp_free_preview();
  wp_timer = 0.18f;
}

static void wp_apply(void) {
  wall_set(wp_sel == 0 ? NULL : wp_items[wp_sel - 1].ref);
  wp_close();
  snprintf(toast_text, sizeof(toast_text), wp_sel == 0 ? "Fond du thème rétabli" : "Fond d'écran appliqué");
  toast_time = 2.0f;
}

/* Charge l'apercu quand la selection se stabilise. */
static void wp_update(float dt) {
  if (!wp_open || wp_tex || wp_sel == 0) return;
  if (wp_timer > 0.0f) wp_timer -= dt;
  if (wp_timer <= 0.0f) {
    char path[140];
    wall_path_of(wp_items[wp_sel - 1].ref, path, sizeof(path));
    wp_tex = load_image_file(path);
    if (wp_tex) tex_prep(wp_tex);
    else wp_timer = 1e9f; /* illisible : on n'insiste pas */
  }
}

static void draw_wp_picker(void) {
  float x = 100, y = 50, w = 760, h = 430;
  modal_panel(x, y, w, h, 215);
  draw_set_icon(17, x + 40, y + 37, T.acc_text);
  draw_text(ST_SEMI, (int)x + 62, (int)y + 45, FS_SECTION, T.text, "Fond d'écran");
  int total = wp_n + 1;
  int first = wp_sel > 7 ? wp_sel - 7 : 0;
  char buf[64];
  for (int k = 0; k < 8 && first + k < total; ++k) {
    int i = first + k;
    float ry = y + 72 + k * 40;
    int sel = i == wp_sel;
    if (sel) round_card(x + 20, ry, 300, 36, 18, T.sel_fill, T.acc, 1.0f);
    const char *name = i == 0 ? "Aucun (fond du thème)" : wp_items[i - 1].label;
    fit_text(sel ? ST_SEMI : ST_REG, FS_BODY, name, 300 - 32, buf, sizeof(buf));
    draw_text(sel ? ST_SEMI : ST_REG, (int)x + 38, (int)ry + 25, FS_BODY, sel ? T.text : T.text_soft, buf);
  }
  if (first > 0) draw_chev(x + 170, y + 66, 2, 1.0f);
  if (first + 8 < total) draw_chev(x + 170, y + 72 + 8 * 40 + 4, 3, 1.0f);

  /* apercu 16:9 a droite, avec une miniature de l'interface par-dessus */
  float px = x + 344, py = y + 76, pw = 392, ph = 223;
  soft_shadow(px + 3, py + 8, pw - 6, ph - 6, 14, 16, C(0, 0, 0, 90));
  if (wp_tex) {
    tex_cover(wp_tex, px, py, pw, ph, 14, C(255, 255, 255, 255));
    round_rect(px + 12, py + 12, 54, ph - 24, 8, with_alpha(T.bar, 150));
    round_rect(px + 78, py + 30, pw - 92, 70, 10, with_alpha(T.glass, 200));
    for (int c = 0; c < 4; ++c) round_rect(px + 78 + c * 76, py + 116, 64, 82, 8, with_alpha(T.card, 210));
  } else {
    round_card(px, py, pw, ph, 14, T.bg, T.card_border, 1.0f);
    draw_text_center(ST_REG, (int)(px + pw / 2), (int)(py + ph / 2 + 6), FS_BODY, T.muted,
                     wp_sel == 0 ? "Fond du thème" : "Chargement...");
  }
  round_outline(px, py, pw, ph, 14, T.card_border);
  draw_text(ST_REG, (int)px, (int)(py + ph + 30), FS_SMALL, T.muted, "Ajoute tes images (PNG / JPG) dans :");
  draw_text(ST_REG, (int)px, (int)(py + ph + 52), FS_SMALL, T.text_soft, WALL_DIR);
  draw_text(ST_REG, (int)px, (int)(py + ph + 74), FS_SMALL, T.muted, "Voile réglable dans Thème > Voile");
  draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted, "X Appliquer;O Annuler;L Page préc.;R Page suiv.");
}

/* ======================================================================
 *  Menu d'options d'un jeu (touche SELECT)
 * ====================================================================== */

static int menu_open, menu_sel;
static Game *menu_game;
enum { MI_PLAY, MI_FAV, MI_COLL, MI_KIND, MI_RENAME, MI_IMAGE, MI_ALTCOVER, MI_BGIMG, MI_DL, MI_HIDE, MI_CLOSE, MI_COUNT };
static int lists_changed; /* les listes ont change : on replace la selection */

static void set_kind(Game *g, int nk) {
  char key[24], val[4];
  snprintf(key, sizeof(key), "kind:%s", g->id);
  snprintf(val, sizeof(val), "%d", nk);
  custom_set(key, val);
  custom_save();
  g->kind = nk;
  build_lists();
  lists_changed = 1;
}

static void set_hidden(Game *g, int hide) {
  char key[24];
  snprintf(key, sizeof(key), "hide:%s", g->id);
  custom_set(key, hide ? "1" : NULL);
  custom_save();
  g->hidden = hide;
  build_lists();
  lists_changed = 1;
}

static void set_fav(Game *g, int on) {
  char key[24];
  snprintf(key, sizeof(key), "fav:%s", g->id);
  custom_set(key, on ? "1" : NULL);
  custom_save();
  g->fav = on;
  build_lists();
  if (opt_sort == 2) lists_changed = 1;
}

/* Categorie suivante / precedente : PS Vita, PSP, Homebrew, puis les systemes ajoutes. */
static int next_kind(int cur, int dir) {
  int kinds[3 + MAX_CUSTOM_SYS], n = 0, idx = 0;
  kinds[n++] = KIND_VITA;
  kinds[n++] = KIND_PSP;
  kinds[n++] = KIND_HB;
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k)
    if (SLOT_USED(k)) kinds[n++] = KIND_CUSTOM0 + k;
  for (int i = 0; i < n; ++i)
    if (kinds[i] == cur) idx = i;
  return kinds[(idx + dir + n) % n];
}

/* ---- Gestion des systemes (touche X sur la tuile "Ajouter un systeme") ---- */

static int sm_open, sm_sel, sm_n;
#define SM_MAX (1 + MAX_CUSTOM_SYS) /* "Nouveau systeme..." + un "Retirer" par systeme */
static int sm_type[SM_MAX], sm_arg[SM_MAX];
static char sm_text[SM_MAX][64];

static void sm_refresh(void) {
  sm_n = 0;
  int free_slot = 0;
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k)
    if (!SLOT_USED(k)) free_slot = 1;
  if (free_slot) {
    sm_type[sm_n] = 1;
    sm_arg[sm_n] = 0;
    snprintf(sm_text[sm_n], sizeof(sm_text[0]), "Nouveau système...");
    ++sm_n;
  }
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k)
    if (SLOT_USED(k) && sm_n < SM_MAX) {
      sm_type[sm_n] = 2;
      sm_arg[sm_n] = k;
      snprintf(sm_text[sm_n], sizeof(sm_text[0]), L("Retirer : %s"), slot_name[k]);
      ++sm_n;
    }
  if (sm_sel >= sm_n) sm_sel = sm_n > 0 ? sm_n - 1 : 0;
}

static int str_ieq(const char *a, const char *b) {
  for (; *a && *b; ++a, ++b) {
    char x = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, y = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
    if (x != y) return 0;
  }
  return *a == *b;
}

/* Cree un systeme portant le nom saisi. 0 = ok, -1 = plus de place, -2 = nom vide ou deja pris. */
static int sys_add(const char *raw) {
  char name[32];
  snprintf(name, sizeof(name), "%s", raw);
  size_t l = strlen(name);
  while (l > 0 && name[l - 1] == ' ') name[--l] = 0;
  const char *nm = name;
  while (*nm == ' ') ++nm;
  if (!*nm) return -2;
  int k = -1;
  for (int i = 0; i < MAX_CUSTOM_SYS; ++i)
    if (!SLOT_USED(i)) {
      k = i;
      break;
    }
  if (k < 0) return -1;
  if (str_ieq(nm, "PS Vita") || str_ieq(nm, "PSP") || str_ieq(nm, "Homebrews") || str_ieq(nm, "Favoris")) return -2;
  for (int i = 0; i < MAX_CUSTOM_SYS; ++i)
    if (SLOT_USED(i) && str_ieq(slot_name[i], nm)) return -2;
  slot_fill(k, nm, -1);
  for (int i = 0; i < MAX_CUSTOM_SYS; ++i)
    if (i != k && SLOT_USED(i) && strcmp(slot_tag[i], slot_tag[k]) == 0) { /* etiquette deja utilisee */
      slot_name[k][0] = 0;
      return -2;
    }
  char key[16], val[48];
  snprintf(key, sizeof(key), "csys%d", k);
  snprintf(val, sizeof(val), "n:%s", slot_name[k]);
  custom_set(key, val);
  custom_save();
  slot_icon[k] = NULL;
  rebuild_systems();
  for (int i = 2; i < n_systems; ++i)
    if (sys_slot[i] == k) load_system_icon(&systems[i]);
  build_lists();
  return 0;
}

/* Retourne le nombre de ROM de ce systeme (il faut alors relire la bibliotheque). */
static int sys_remove(int k) {
  vita2d_wait_rendering_done();
  char key[24];
  int roms = 0;
  for (int i = 2; i < n_systems; ++i)
    if (sys_slot[i] == k) {
      if (systems[i].icon) vita2d_free_texture(systems[i].icon);
      systems[i].icon = NULL;
      snprintf(key, sizeof(key), "sys:%s", systems[i].tag);
      custom_set(key, NULL);
    }
  slot_icon[k] = NULL;
  for (int i = 0; i < app_n; ++i)
    if (all_apps[i].rom == k + 1 || cache_recs[i].kind == KIND_CUSTOM0 + k) ++roms; /* ROM, ou jeu PS1 range la au scan */
  snprintf(key, sizeof(key), "rcore%d", k); /* emulation : coeur et dossier oublies */
  custom_set(key, NULL);
  snprintf(key, sizeof(key), "rdir%d", k);
  custom_set(key, NULL);
  for (int i = 0; i < app_n; ++i)
    if (all_apps[i].kind == KIND_CUSTOM0 + k) { /* les jeux retrouvent leur categorie d'origine */
      snprintf(key, sizeof(key), "kind:%s", all_apps[i].id);
      custom_set(key, NULL);
      all_apps[i].kind = cache_recs[i].kind;
    }
  snprintf(key, sizeof(key), "csys%d", k);
  custom_set(key, NULL);
  custom_save();
  slot_name[k][0] = 0;
  rebuild_systems();
  build_lists();
  lists_changed = 1;
  return roms;
}

/* Nom saisi sans espaces au debut ni a la fin. 0 si vide. */
static int clean_name(const char *raw, char *out, int n) {
  while (*raw == ' ') ++raw;
  snprintf(out, n, "%s", raw);
  size_t l = strlen(out);
  while (l > 0 && out[l - 1] == ' ') out[--l] = 0;
  return l > 0;
}

static void coll_store_name(int k) {
  char key[16], val[48];
  snprintf(key, sizeof(key), "coll%d", k);
  snprintf(val, sizeof(val), "n:%s", coll_name[k]);
  custom_set(key, COLL_USED(k) ? val : NULL);
}

/* Nom deja pris par une autre collection (except = emplacement a ignorer) ? */
static int coll_name_taken(const char *nm, int except) {
  for (int i = 0; i < MAX_COLL; ++i)
    if (i != except && COLL_USED(i) && str_ieq(coll_name[i], nm)) return 1;
  return 0;
}

/* Cree une collection. Retourne son emplacement, -1 = plus de place, -2 = nom vide ou deja pris. */
static int coll_add(const char *raw) {
  char nm[32];
  if (!clean_name(raw, nm, sizeof(nm)) || coll_name_taken(nm, -1)) return -2;
  int k = -1;
  for (int i = 0; i < MAX_COLL && k < 0; ++i)
    if (!COLL_USED(i)) k = i;
  if (k < 0) return -1;
  snprintf(coll_name[k], sizeof(coll_name[k]), "%s", nm);
  coll_store_name(k);
  custom_save();
  coll_refresh_ids();
  build_lists();
  return k;
}

static int coll_rename(int k, const char *raw) {
  char nm[32];
  if (!clean_name(raw, nm, sizeof(nm)) || coll_name_taken(nm, k)) return -2;
  snprintf(coll_name[k], sizeof(coll_name[k]), "%s", nm);
  coll_store_name(k);
  custom_save();
  return 0;
}

/* Supprime la collection : les jeux restent dans leur systeme, seule l'appartenance est effacee. */
static void coll_remove(int k) {
  unsigned long long bit = COLL_BIT(k);
  for (int i = n_customs - 1; i >= 0; --i) { /* a l'envers : custom_set(NULL) deplace la derniere entree */
    if (strncmp(customs[i].key, "col:", 4) != 0) continue;
    unsigned long long m = strtoull(customs[i].file, NULL, 16);
    if (!(m & bit)) continue;
    char id[24];
    snprintf(id, sizeof(id), "%s", customs[i].key + 4);
    coll_store_mask(id, m & ~bit);
  }
  for (int i = 0; i < app_n; ++i) all_apps[i].coll &= ~bit;
  coll_name[k][0] = 0;
  coll_store_name(k);
  custom_save();
  coll_refresh_ids();
  build_lists();
  lists_changed = 1;
}

static void coll_toggle(Game *g, int k) {
  g->coll ^= COLL_BIT(k);
  coll_store_mask(g->id, g->coll);
  custom_save();
  build_lists();
}


/* ---- Clavier a l'ecran : recherche, nom d'un systeme, d'une collection, d'un jeu ----
 * Deux pages : lettres (AZERTY) ; chiffres, ponctuation et accents (touche "123" / "ABC").
 * Chaque ligne fait 20 demi-touches de large ; une touche speciale peut etre plus large. */
enum { KA_CHAR = 0, KA_SHIFT, KA_BACK, KA_PAGE, KA_SPACE, KA_OK };
typedef struct {
  const char *lo, *up;
  unsigned char act, span; /* span : largeur en demi-touches (2 = touche normale) */
} KbKey;
#define KB_ROWS 4
#define KB_RMAX 12
static const KbKey kb_pages[2][KB_ROWS][KB_RMAX] = {
    {{{"a", "A", 0, 2}, {"z", "Z", 0, 2}, {"e", "E", 0, 2}, {"r", "R", 0, 2}, {"t", "T", 0, 2}, {"y", "Y", 0, 2},
      {"u", "U", 0, 2}, {"i", "I", 0, 2}, {"o", "O", 0, 2}, {"p", "P", 0, 2}},
     {{"q", "Q", 0, 2}, {"s", "S", 0, 2}, {"d", "D", 0, 2}, {"f", "F", 0, 2}, {"g", "G", 0, 2}, {"h", "H", 0, 2},
      {"j", "J", 0, 2}, {"k", "K", 0, 2}, {"l", "L", 0, 2}, {"m", "M", 0, 2}},
     {{"", "", KA_SHIFT, 3}, {"w", "W", 0, 2}, {"x", "X", 0, 2}, {"c", "C", 0, 2}, {"v", "V", 0, 2}, {"b", "B", 0, 2},
      {"n", "N", 0, 2}, {"'", "'", 0, 2}, {"", "", KA_BACK, 3}},
     {{"123", "123", KA_PAGE, 4}, {"Espace", "Espace", KA_SPACE, 12}, {"OK", "OK", KA_OK, 4}}},
    {{{"1", "1", 0, 2}, {"2", "2", 0, 2}, {"3", "3", 0, 2}, {"4", "4", 0, 2}, {"5", "5", 0, 2}, {"6", "6", 0, 2},
      {"7", "7", 0, 2}, {"8", "8", 0, 2}, {"9", "9", 0, 2}, {"0", "0", 0, 2}},
     {{"-", "-", 0, 2}, {".", ".", 0, 2}, {",", ",", 0, 2}, {":", ":", 0, 2}, {"!", "!", 0, 2}, {"?", "?", 0, 2},
      {"&", "&", 0, 2}, {"(", "(", 0, 2}, {")", ")", 0, 2}, {"/", "/", 0, 2}},
     {{"é", "É", 0, 2}, {"è", "È", 0, 2}, {"ê", "Ê", 0, 2}, {"à", "À", 0, 2}, {"ç", "Ç", 0, 2}, {"ù", "Ù", 0, 2},
      {"ô", "Ô", 0, 2}, {"î", "Î", 0, 2}, {"", "", KA_BACK, 4}},
     {{"ABC", "ABC", KA_PAGE, 4}, {"Espace", "Espace", KA_SPACE, 12}, {"OK", "OK", KA_OK, 4}}}};

static int kb_open, kb_sel, kb_caps = 1, kb_page;
static int kb_focus, kb_res_sel; /* recherche : 0 = clavier, 1 = liste des resultats */
static char kb_text[64];
static int kb_cur; /* curseur : position (en octets) dans kb_text */
#define KB_SEL(r, i) ((r) * 16 + (i))
#define KB_R(s) ((s) / 16)
#define KB_I(s) ((s) % 16)

/* Ce que le texte saisi va devenir. */
enum { KB_SYSTEM = 0, KB_COLL_NEW, KB_COLL_NEW_GAME, KB_COLL_RENAME, KB_SEARCH, KB_GAME_RENAME };
static int kb_mode, kb_arg;
static Game *kb_game; /* KB_GAME_RENAME : jeu renomme */

static int kb_row_n(int r) {
  int n = 0;
  while (n < KB_RMAX && kb_pages[kb_page][r][n].lo) ++n;
  return n;
}
static const KbKey *kb_key(int s) { return &kb_pages[kb_page][KB_R(s)][KB_I(s)]; }

static void kb_start_ex(int mode, int arg, const char *initial) {
  snprintf(kb_text, sizeof(kb_text), "%s", initial ? initial : "");
  kb_cur = (int)strlen(kb_text);
  kb_mode = mode;
  kb_arg = arg;
  kb_sel = 0;
  kb_page = 0;
  kb_focus = 0;
  kb_res_sel = 0;
  kb_caps = kb_text[0] == 0 && mode != KB_SEARCH; /* un nom commence par une majuscule */
  kb_open = 1;
}
static void kb_start(void) { kb_start_ex(KB_SYSTEM, 0, NULL); }
/* Insere au curseur. */
static void kb_append(const char *t) {
  size_t l = strlen(kb_text), a = strlen(t), max = kb_mode == KB_GAME_RENAME ? 60 : 24;
  if (l + a > max) return;
  for (int i = (int)l; i >= kb_cur; --i) kb_text[i + a] = kb_text[i]; /* decale la suite (avec le 0 final) */
  memcpy(kb_text + kb_cur, t, a);
  kb_cur += (int)a;
}
/* Position du caractere avant / apres p (UTF-8). */
static int kb_prev(int p) {
  while (p > 0) {
    --p;
    if (((unsigned char)kb_text[p] & 0xC0) != 0x80) break; /* octet de tete d'un caractere */
  }
  return p;
}
static int kb_next(int p) {
  if (!kb_text[p]) return p;
  ++p;
  while (kb_text[p] && ((unsigned char)kb_text[p] & 0xC0) == 0x80) ++p;
  return p;
}
/* Efface le caractere avant le curseur. */
static void kb_back(void) {
  int p = kb_prev(kb_cur);
  int i = kb_cur - 1;
  do { ++i; kb_text[p + i - kb_cur] = kb_text[i]; } while (kb_text[i]); /* ramene la suite (avec le 0 final) */
  kb_cur = p;
}
static void kb_clear(void) {
  kb_text[0] = 0;
  kb_cur = 0;
}
/* Curseur d'un caractere a gauche (-1) ou a droite (1). */
static void kb_cur_move(int d) { kb_cur = d < 0 ? kb_prev(kb_cur) : kb_next(kb_cur); }

/* Position horizontale (en demi-touches) du centre d'une touche : lignes plus courtes centrees. */
static int kb_center2(int page, int r, int i) {
  int tot = 0, before = 0;
  for (int k = 0; k < KB_RMAX && kb_pages[page][r][k].lo; ++k) {
    if (k < i) before += kb_pages[page][r][k].span;
    tot += kb_pages[page][r][k].span;
  }
  return (20 - tot) + 2 * before + kb_pages[page][r][i].span; /* x2 : centre sans fraction */
}

/* Fleches dans le clavier. Retourne 1 si on sort par la droite (vers les resultats). */
static int kb_move(int dx, int dy) {
  int r = KB_R(kb_sel), i = KB_I(kb_sel), n = kb_row_n(r);
  if (dx) {
    i += dx;
    if (i >= n) return 1;
    if (i < 0) i = 0;
  }
  if (dy) {
    int nr = r + dy;
    if (nr < 0 || nr >= KB_ROWS) return 0;
    int cx = kb_center2(kb_page, r, i), best = 0, bd = 1000;
    for (int k = 0; k < kb_row_n(nr); ++k) {
      int d = abs(kb_center2(kb_page, nr, k) - cx);
      if (d < bd) bd = d, best = k;
    }
    r = nr;
    i = best;
  }
  kb_sel = KB_SEL(r, i);
  return 0;
}

/* Touche choisie. Retourne 1 quand l'utilisateur valide ("OK"). */
static int kb_press(void) {
  const KbKey *k = kb_key(kb_sel);
  switch (k->act) {
    case KA_SHIFT: kb_caps = !kb_caps; return 0;
    case KA_BACK: kb_back(); return 0;
    case KA_SPACE: kb_append(" "); return 0;
    case KA_OK: return 1;
    case KA_PAGE: { /* meme ligne, touche la plus proche sur l'autre page */
      int r = KB_R(kb_sel), cx = kb_center2(kb_page, r, KB_I(kb_sel));
      kb_page = !kb_page;
      int best = 0, bd = 1000;
      for (int q = 0; q < kb_row_n(r); ++q) {
        int d = abs(kb_center2(kb_page, r, q) - cx);
        if (d < bd) bd = d, best = q;
      }
      kb_sel = KB_SEL(r, best);
      return 0;
    }
    default:
      kb_append(kb_caps ? k->up : k->lo);
      if (kb_caps && kb_page == 0) kb_caps = 0; /* majuscule pour une lettre, puis minuscules */
      return 0;
  }
}

/* Touches dessinees dans (x, y, w), lignes de hauteur kh. Zones tactiles : H_KEY, idx = KB_SEL. */
static void kb_draw_keys(float x, float y, float w, float kh) {
  float gap = 6, unit = (w - 9 * gap) / 10.0f; /* largeur d'une touche normale */
  for (int r = 0; r < KB_ROWS; ++r) {
    int n = kb_row_n(r), tot = 0;
    for (int i = 0; i < n; ++i) tot += kb_pages[kb_page][r][i].span;
    float kx = x + (20 - tot) * (unit + gap) / 4.0f, ky = y + r * (kh + 8);
    for (int i = 0; i < n; ++i) {
      const KbKey *k = &kb_pages[kb_page][r][i];
      float kw = unit * k->span / 2.0f + gap * (k->span / 2.0f - 1.0f);
      int sel = kb_focus == 0 && kb_sel == KB_SEL(r, i), ok = k->act == KA_OK;
      if (ok) primary_button(kx, ky, kw, kh, 1.0f);
      else
        round_card(kx, ky, kw, kh, 10, sel ? T.sel_fill : k->act ? with_alpha(T.card_border, 160) : with_alpha(T.bg, 150),
                   sel ? T.acc : T.card_border, 1.0f);
      if (sel) round_outline_t(kx - 2, ky - 2, kw + 4, kh + 4, 12, 2.0f, ok ? T.text : T.acc);
      float cx = kx + kw / 2, cy = ky + kh / 2;
      unsigned int tc = ok ? T.btn_text : T.text;
      if (k->act == KA_SHIFT) { /* fleche vers le haut ; pleine si majuscules */
        unsigned int c = kb_caps ? T.acc_text : T.text;
        for (int q = 0; q < 10; ++q) fill_rect(cx - q, cy - 10 + q, 2 * q + 1, 1, c);
        fill_rect(cx - 4, cy, 9, 9, c);
      } else if (k->act == KA_BACK) { /* fleche vers la gauche + barre */
        draw_tri(cx - 14, cy, 12, 18, -1, T.text);
        fill_rect(cx - 2, cy - 3, 16, 6, T.text);
      } else {
        const char *t = kb_caps ? k->up : k->lo;
        int fs = k->act ? FS_BODY : FS_SECTION;
        draw_text_center(sel || ok ? ST_SEMI : ST_REG, (int)cx, (int)(cy + fs * 0.36f), fs, tc, t);
      }
      hit_add(kx, ky, kw, kh, H_KEY, KB_SEL(r, i));
      kx += kw + gap;
    }
  }
}

#define KB_CLEAR 900  /* zones tactiles particulieres du clavier */
#define KB_CLOSE 901
#define KB_FIELD 902 /* texte du champ : place le curseur */
#define KB_RES0 1000 /* + n : resultat n de la recherche */
static float kbf_x; /* champ : ou commence le texte affiche, et a partir de quel octet (toucher) */
static int kbf_start;
/* Toucher du champ en sx : curseur devant le caractere le plus proche. */
static void kb_field_tap(float sx) {
  char buf[96];
  int c = kb_cur, best = c;
  float bd = 1e9f, px = sx - kbf_x;
  snprintf(buf, sizeof(buf), "%s%.*s", kbf_start ? "..." : "", c - kbf_start, kb_text + kbf_start);
  int pw = buf[0] ? text_w(ST_SEMI, FS_BODY, buf) : 0;
  for (int i = kbf_start;; i = kb_next(i)) {
    if (i <= c) snprintf(buf, sizeof(buf), "%s%.*s", kbf_start ? "..." : "", i - kbf_start, kb_text + kbf_start);
    else snprintf(buf, sizeof(buf), "%.*s", i - c, kb_text + c);
    float xi = (float)(buf[0] ? text_w(ST_SEMI, FS_BODY, buf) : 0) + (i > c ? pw + 4 : 0);
    float d = xi > px ? xi - px : px - xi;
    if (d < bd) { bd = d; best = i; }
    if (!kb_text[i]) break;
  }
  kb_cur = best;
}

#define KB_RES_VIS 6 /* resultats visibles a la fois */
/* Champ de saisie arrondi : loupe (recherche), texte ou indication, croix pour tout effacer.
 * Texte avant le curseur (debut coupe "..." s'il est trop long), curseur, puis la suite. */
static void kb_draw_field(float x, float y, float w, float h, const char *hint) {
  round_card(x, y, w, h, h / 2, with_alpha(T.bg, 200), kb_focus == 0 ? T.acc : T.card_border, 1.0f);
  float tx = x + 20;
  if (kb_mode == KB_SEARCH) {
    draw_set_icon(ICO_SEARCH, x + 24, y + h / 2, T.text_soft);
    tx = x + 46;
  }
  char buf[96];
  if (kb_text[0]) {
    int avail = (int)(w - (tx - x) - 46), c = kb_cur, start = 0;
    for (;;) {
      snprintf(buf, sizeof(buf), "%s%.*s", start ? "..." : "", c - start, kb_text + start);
      if (start >= c || text_w(ST_SEMI, FS_BODY, buf) <= avail * 3 / 4) break;
      start = kb_next(start);
    }
    int pw = buf[0] ? text_w(ST_SEMI, FS_BODY, buf) : 0;
    draw_text(ST_SEMI, (int)tx, (int)(y + h / 2 + 6), FS_BODY, T.text, buf);
    fill_rect(tx + pw + 1, y + h / 2 - 11, 2, 22, T.acc);
    if (kb_text[c]) {
      char cut[96];
      fit_text(ST_SEMI, FS_BODY, kb_text + c, avail - pw - 4, cut, sizeof(cut));
      draw_text(ST_SEMI, (int)tx + pw + 4, (int)(y + h / 2 + 6), FS_BODY, T.text, cut);
    }
    kbf_x = tx;
    kbf_start = start;
    hit_add(tx - 10, y, avail + 10, h, H_KEY, KB_FIELD);
    float cx = x + w - 22, cy = y + h / 2; /* croix : tout effacer */
    vita2d_draw_fill_circle(cx, cy, 11, with_alpha(T.card_border, 200));
    vita2d_draw_line(cx - 4, cy - 4, cx + 4, cy + 4, T.text);
    vita2d_draw_line(cx + 4, cy - 4, cx - 4, cy + 4, T.text);
    hit_add(cx - 16, cy - 16, 32, 32, H_KEY, KB_CLEAR);
  } else {
    draw_text(ST_REG, (int)tx, (int)(y + h / 2 + 6), FS_BODY, T.muted, hint);
  }
}


/* Recherche : ecran entier. En-tete, champ, clavier a gauche ; resultats a droite (a chaque lettre). */
static void draw_search_screen(void) {
  /* toute la largeur sous la barre du haut (le menu de gauche s'efface : seule page sans lui) */
  fill_rect(0, BAR_H + 1, SCREEN_W, 544 - BAR_H - 1, T.bg); /* jusqu'en bas : pied de page identique sur toute la largeur */
  draw_bg_layer(0.25f);
  /* gauche : champ et clavier */
  float lx = 24, lw = 548;
  kb_draw_field(lx, 64, lw, 46, "Rechercher un jeu...");
  kb_draw_keys(lx, 124, lw, 64);
  /* droite : resultats */
  float rx = lx + lw + 16, rw = CONTENT_R - rx, ry = 64, rh = FOOTER_Y - 10 - ry;
  round_card(rx, ry, rw, rh, 16, with_alpha(T.card, 225), T.card_border, 1.0f);
  draw_text(ST_SEMI, (int)rx + 18, (int)ry + 32, FS_BODY, T.text, "Résultats");
  char cnt[24];
  if (!kb_text[0]) cnt[0] = 0;
  else if (search_n == 0) snprintf(cnt, sizeof(cnt), "Aucun");
  else snprintf(cnt, sizeof(cnt), "%d", search_n);
  draw_text_right(ST_REG, (int)(rx + rw - 18), (int)ry + 32, FS_BODY, search_n ? T.acc_text : T.muted, cnt);
  if (!kb_text[0]) {
    draw_text(ST_REG, (int)rx + 18, (int)ry + 76, FS_SMALL, T.muted, "Tape le nom d'un jeu :");
    draw_text(ST_REG, (int)rx + 18, (int)ry + 98, FS_SMALL, T.muted, "les résultats s'affichent ici.");
  }
  int first = kb_res_sel > KB_RES_VIS - 1 ? kb_res_sel - (KB_RES_VIS - 1) : 0;
  for (int k = 0; k < KB_RES_VIS && kb_text[0] && first + k < search_n; ++k) {
    int i = first + k, sel = kb_focus == 1 && i == kb_res_sel;
    Game *g = search_ptrs[i];
    float iy = ry + 46 + k * 64; /* jaquettes plus grandes : 58 px de haut */
    if (sel) {
      glow(rx + 8, iy, rw - 16, 60, 12, 1.0f, 1.0f);
      round_card(rx + 8, iy, rw - 16, 60, 12, T.sel_fill, T.acc, 1.0f);
    }
    draw_thumb(g, rx + 44, iy + 3, 54, 54, 1.0f);
    char nm[96];
    fit_text(sel ? ST_SEMI : ST_REG, FS_BODY, g->name, (int)rw - 110, nm, sizeof(nm));
    draw_text(sel ? ST_SEMI : ST_REG, (int)rx + 82, (int)iy + 27, FS_BODY, T.text, nm);
    draw_text(ST_REG, (int)rx + 82, (int)iy + 47, FS_SMALL, T.muted, kind_label(g));
    hit_add(rx + 8, iy, rw - 16, 60, H_KEY, KB_RES0 + i);
  }
  if (first > 0) draw_chev(rx + rw / 2, ry + 44, 2, 1.0f);
  if (first + KB_RES_VIS < search_n && kb_text[0]) draw_chev(rx + rw / 2, ry + rh - 8, 3, 1.0f);
}

/* Autres saisies (nom d'un systeme, d'une collection, d'un jeu) : meme clavier dans une fenetre. */
static void draw_keyboard(void) {
  if (kb_mode == KB_SEARCH) {
    draw_search_screen();
    return;
  }
  fill_rect(0, 0, SCREEN_W, 544, with_alpha(T.bg, 170));
  float w = 640, h = 420, x = (SCREEN_W - w) / 2, y = 62;
  modal_panel(x, y, w, h, 215);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 42, FS_SECTION, T.text,
            kb_mode == KB_SYSTEM ? "Nom du système" : kb_mode == KB_COLL_RENAME ? "Renommer la collection"
            : kb_mode == KB_GAME_RENAME ? "Renommer le jeu" : "Nom de la collection");
  kb_draw_field(x + 28, y + 62, w - 56, 46,
                kb_mode == KB_GAME_RENAME ? "Vide = nom d'origine" : "Tape un nom...");
  kb_draw_keys(x + 28, y + 124, w - 56, 56);
  /* aides des touches : dans le pied de page, comme pour la recherche */
}
