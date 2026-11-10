/* Area Play - Vues carrousel / grille / liste, choix du systeme. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Les trois modes d'affichage des jeux
 * ====================================================================== */

/* 1) Carrousel : tuile du jeu en cours + une rangee de jaquettes. */
static void draw_view_tile(Game **list, int n, int sel, int is_home,
                           const char *title, const char *button,
                           const char *hint) {
  draw_hero_tile(list[sel], is_home, button);
  draw_text(ST_SEMI, CONTENT_X + g_ox, 294, FS_SECTION, fade(T.text, g_pa), title);
  if (is_home) section_hint(294, "L / R", hint);
  int first = sel > 3 ? sel - 3 : 0;
  for (int k = 0; k < 4 && first + k < n; ++k) {
    int i = first + k;
    draw_card(list[i], CONTENT_X + k * CARD_PITCH + g_ox, SHELF_Y, CARD_W,
              COVER_H, focus[i], !is_home);
    hit_add(CONTENT_X + k * CARD_PITCH, SHELF_Y, CARD_W, COVER_H + 60, H_ITEM, i);
  }
  float my = SHELF_Y + (COVER_H + 60) / 2.0f;
  if (first > 0) draw_chev(CONTENT_X - 12 + g_ox, my, 0, g_pa);
  if (first + 4 < n && g_lb_a < 0.05f) draw_chev(CONTENT_R + 8 + g_ox, my, 1, g_pa);
}

/* 2) Grille : toutes les jaquettes, defilement par rangee. */
static void draw_view_grid(Game **list, int n, int sel, const char *title,
                           const char *hint) {
  draw_text(ST_SEMI, CONTENT_X + g_ox, 98, FS_SECTION, fade(T.text, g_pa), title);
  int cols = 4;
  int rows = (n + cols - 1) / cols;
  int vis = rows <= 1 ? 1 : 2;
  float cov_h = rows <= 1 ? 186.0f : 128.0f;
  float h = cov_h + 60, gap = 8;
  float total = vis * h + (vis - 1) * gap;
  float y0 = 108 + (392 - total) / 2;
  int sel_row = sel / cols;
  int first_row = sel_row >= vis ? sel_row - vis + 1 : 0;
  for (int i = first_row * cols; i < n && i < (first_row + vis) * cols; ++i) {
    draw_card(list[i], CONTENT_X + (i % cols) * CARD_PITCH + g_ox,
              y0 + (i / cols - first_row) * (h + gap), CARD_W, cov_h, focus[i], 1);
    hit_add(CONTENT_X + (i % cols) * CARD_PITCH, y0 + (i / cols - first_row) * (h + gap), CARD_W, h, H_ITEM, i);
  }
  if (first_row > 0) draw_chev(CONTENT_X + 379 + g_ox, 108, 2, g_pa);
  if ((first_row + vis) * cols < n) draw_chev(CONTENT_X + 379 + g_ox, FOOTER_Y - 8, 3, g_pa);
}

/* 4) Catalogue, grille : bandeau reduit du jeu choisi en haut, puis 2 rangees de jaquettes. dy : decalage
 * vertical pendant la transition (la grille arrive du bas). */
static void draw_catalog_grid(Game **list, int n, int sel, float dy) {
  float pa = g_pa, x = CONTENT_X + g_ox, by = TILE_Y + dy, bh = 62;
  const Game *g = list[sel];
  round_rect(x, by, TILE_W, bh, 14, fade(opt_backdrop || wall_path[0] ? T.glass : T.card, pa));
  round_outline(x, by, TILE_W, bh, 14, fade(opt_gloss ? T.glass_border : T.card_border, pa));
  spaced(ST_REG, (int)x + 24, (int)by + 24, FS_SMALL, fade(xmas_on() ? C(232, 185, 96, 255) : T.acc_text, pa), opt_show_id ? g->detail : kind_label(g),
         SPACING + 1, 1);
  char ago[40], right[64];
  if (g->last_played > 0) {
    ago_text(g->last_played, ago, sizeof(ago));
    snprintf(right, sizeof(right), "%s", ago);
  } else snprintf(right, sizeof(right), "%s", L("Jamais lancé"));
  int rw = text_w(ST_REG, FS_SMALL, right);
  draw_text_right(ST_REG, (int)(x + TILE_W - 24), (int)by + 47, FS_SMALL, fade(T.text_soft, pa), right);
  char cut[96];
  fit_text(ST_SEMI, FS_SECTION, g->name, TILE_W - 48 - rw - 24, cut, sizeof(cut));
  draw_text(ST_SEMI, (int)x + 24, (int)by + 50, FS_SECTION, fade(T.text, pa), cut);
  int cols = 4, vis = 2;
  float cov_h = 116.0f, h = cov_h + 60, gap = 8, y0 = 136 + dy;
  int sel_row = sel / cols;
  int first_row = sel_row >= vis ? sel_row - vis + 1 : 0;
  for (int i = first_row * cols; i < n && i < (first_row + vis) * cols; ++i) {
    float cy = y0 + (i / cols - first_row) * (h + gap);
    draw_card(list[i], x + (i % cols) * CARD_PITCH, cy, CARD_W, cov_h, focus[i], 1);
    hit_add(CONTENT_X + (i % cols) * CARD_PITCH, cy, CARD_W, h, H_ITEM, i);
  }
  if (first_row > 0) draw_chev(x + 379, y0 - 6, 2, pa);
  if ((first_row + vis) * cols < n) draw_chev(x + 379, FOOTER_Y - 6, 3, pa);
}

/* 4) Catalogue : carrousel (tuile + rangee) ; Bas passe a la grille, Haut (1re ligne) ou O revient.
 * Transition en fondu d'environ 200 ms : le carrousel s'efface, la grille monte un peu en apparaissant. */
static void draw_view_catalog(Game **list, int n, int sel, const char *title, const char *button, float dt) {
  cat_t += (cat_grid ? dt : -dt) / 0.2f;
  if (cat_t < 0.0f) cat_t = 0.0f;
  if (cat_t > 1.0f) cat_t = 1.0f;
  float t = cat_t * cat_t * (3.0f - 2.0f * cat_t), save = g_pa;
  if (t < 0.999f) {
    g_pa = save * (1.0f - t);
    draw_view_tile(list, n, sel, 0, title, button, "");
  }
  if (t > 0.001f) {
    g_pa = save * t;
    draw_catalog_grid(list, n, sel, (1.0f - t) * 40.0f);
  }
  g_pa = save;
}

/* Texte sur plusieurs lignes (coupe aux espaces). Retourne le nombre de lignes dessinees. */
static int draw_wrapped(int style, int x, int y, int size, int maxw, int maxlines, int lineh,
                        unsigned int col, const char *txt) {
  txt = L(txt);
  char line[128];
  int ll = 0, lines = 0;
  const char *p = txt;
  line[0] = 0;
  while (*p && lines < maxlines) {
    char word[64];
    int wl = 0;
    while (*p == ' ') ++p;
    while (*p && *p != ' ' && wl < (int)sizeof(word) - 1) word[wl++] = *p++;
    word[wl] = 0;
    if (!wl) break;
    char trial[200];
    snprintf(trial, sizeof(trial), ll ? "%s %s" : "%s%s", line, word);
    if (ll && text_w(style, size, trial) > maxw) {
      draw_text(style, x, y + lines * lineh, size, col, line);
      ++lines;
      snprintf(line, sizeof(line), "%s", word);
      ll = (int)strlen(line);
    } else {
      snprintf(line, sizeof(line), "%s", trial);
      ll = (int)strlen(line);
    }
  }
  if (ll && lines < maxlines) {
    char cut[128];
    int more = *p != 0 && lines == maxlines - 1;
    fit_text(style, size, line, maxw, cut, sizeof(cut));
    if (more && strlen(cut) < sizeof(cut) - 4) strcat(cut, "...");
    draw_text(style, x, y + lines * lineh, size, col, cut);
    ++lines;
  }
  return lines;
}

/* ---- Icones remplacables ----
 * Chaque icone a un nom (ico_names). Area Play cherche, dans l'ordre :
 *   1) ux0:data/AreaPlay/icons/<nom>.png   (sur la console, sans recompiler)
 *   2) app0:assets/icons/<nom>.png          (dossier assets/icons du projet, avant compilation)
 *   3) les anciens noms (ico_<numero>.png, btn_*.png, home.png...)
 *   4) sinon l'icone est dessinee par le code.
 * Liste complete : assets/icons/ICONES.txt. Icones blanches sur fond transparent : la couleur
 * vient du theme. */
static const char *ico_names[ICO_N] = {
    [ICO_BACKDROP] = "fond_dynamique", [ICO_GLOSS] = "cadres_glossy", [ICO_THEME] = "theme",
    [ICO_ACCENT] = "couleur_accent", [ICO_VIEW] = "affichage", [ICO_ID] = "identifiant",
    [ICO_HIDDEN] = "jeux_masques", [ICO_SCAN] = "scanner", [ICO_DELETE] = "supprimer", [ICO_SORT] = "tri",
    [ICO_DOWNLOAD] = "telecharger", [ICO_LIBRARY] = "bibliotheque", [ICO_PLAY] = "jouer", [ICO_CLOSE] = "fermer",
    [ICO_CLOCK] = "temps_de_jeu", [ICO_KIND] = "categorie", [ICO_WALL] = "fond_ecran", [ICO_DIM] = "voile",
    [ICO_COLL] = "collection", [ICO_BTN_CROSS] = "bouton_croix", [ICO_BTN_CIRCLE] = "bouton_rond",
    [ICO_BTN_SQUARE] = "bouton_carre", [ICO_BTN_TRIANGLE] = "bouton_triangle", [ICO_SYS_TILES] = "tuiles_systemes",
    [ICO_MSAA] = "anticrenelage", [ICO_FPS] = "compteur_images", [ICO_BD_ICON] = "fond_sans_image",
    [ICO_SEARCH] = "recherche", [ICO_SOUND] = "sons", [ICO_QUIT] = "quitter", [ICO_M_HOME] = "menu_accueil",
    [ICO_M_GAMES] = "menu_jeux", [ICO_M_HB] = "menu_homebrews", [ICO_M_SETTINGS] = "menu_reglages",
    [ICO_M_FAV] = "menu_favoris", [ICO_M_COLL] = "menu_collections", [ICO_M_SEARCH] = "menu_recherche",
    [ICO_FAV] = "favori", [ICO_FAV_EMPTY] = "favori_vide"};

/* Cree ux0:data/AreaPlay/icons et y ecrit la liste des noms (NOMS_ICONES.txt) si elle n'y est pas :
 * une icone deposee la remplace celle d'Area Play ; absente ou illisible, celle d'Area Play reste. */
static void icons_dir_init(void) {
  sceIoMkdir(DATA_DIR "/icons", 0777);
  const char *list = DATA_DIR "/icons/NOMS_ICONES.txt";
  SceUID f = sceIoOpen(list, SCE_O_RDONLY, 0);
  if (f >= 0) {
    sceIoClose(f);
    return;
  }
  f = sceIoOpen(list, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  static const char head[] =
      "Icones de " VF_APP_NAME " : depose ici <nom>.png (PNG blanc sur fond transparent, 64 x 64 conseille).\r\n"
      "La couleur vient du theme. Fichier absent ou illisible : l'icone d'origine est utilisee.\r\n"
      "Relancer " VF_APP_NAME " apres un changement.\r\n\r\n";
  sceIoWrite(f, head, sizeof(head) - 1);
  for (int i = 0; i < ICO_N; ++i) {
    if (!ico_names[i]) continue;
    char l[64];
    int n = snprintf(l, sizeof(l), "%s.png\r\n", ico_names[i]);
    sceIoWrite(f, l, n);
  }
  sceIoClose(f);
}

/* Nom des fichiers des versions precedentes (toujours acceptes). */
static const char *ico_legacy(int id, char *buf, int n) {
  static const char *bn[4] = {"btn_cross", "btn_circle", "btn_square", "btn_triangle"};
  static const char *menu_files[4] = {"home", "games", "homebrew", "settings"};
  if (id >= ICO_BTN_CROSS && id <= ICO_BTN_TRIANGLE) return bn[id - ICO_BTN_CROSS];
  if (id >= ICO_M_HOME && id <= ICO_M_SETTINGS) return menu_files[id - ICO_M_HOME];
  if (id < 20) {
    snprintf(buf, n, "ico_%d", id);
    return buf;
  }
  return NULL;
}

static vita2d_texture *set_ico[ICO_N];
static unsigned char set_ico_tried[ICO_N];
static vita2d_texture *ico_get(int id) {
  if (id < 0 || id >= ICO_N) return NULL;
  if (!set_ico_tried[id]) {
    set_ico_tried[id] = 1;
    char path[112], lb[16];
    const char *legacy = ico_legacy(id, lb, sizeof(lb));
    const char *names[2] = {ico_names[id], legacy};
    for (int k = 0; k < 2 && !set_ico[id]; ++k) {
      if (!names[k] || !names[k][0]) continue;
      snprintf(path, sizeof(path), DATA_DIR "/icons/%s.png", names[k]);
      set_ico[id] = load_image_file(path);
      if (!set_ico[id]) {
        snprintf(path, sizeof(path), "app0:assets/icons/%s.png", names[k]);
        set_ico[id] = load_image_file(path);
      }
      /* anciennes icones du menu (noires) : passees en blanc pour que la couleur du theme s'applique */
      if (set_ico[id] && k == 1 && id >= ICO_M_HOME && id <= ICO_M_SETTINGS && tex_is_rgba(set_ico[id])) {
        unsigned int *px = (unsigned int *)vita2d_texture_get_datap(set_ico[id]);
        int sw = vita2d_texture_get_stride(set_ico[id]) / 4, th = vita2d_texture_get_height(set_ico[id]);
        for (int q = 0; q < sw * th; ++q) px[q] |= 0x00FFFFFFu;
      }
    }
    tex_prep(set_ico[id]);
  }
  return set_ico[id];
}

/* Dessine une icone fichier dans un carre de "box" pixels (centree, proportions gardees). 0 si absente. */
static int draw_ico_file(int id, float cx, float cy, float box, unsigned int c) {
  vita2d_texture *ct = ico_get(id);
  if (!ct) return 0;
  float tw = vita2d_texture_get_width(ct), th = vita2d_texture_get_height(ct);
  float sc = box / (tw > th ? tw : th);
  vita2d_draw_texture_tint_scale(ct, cx - tw * sc / 2, cy - th * sc / 2, sc, sc, c);
  return 1;
}

/* Petites icones des menus et reglages (ICO_xxx). Fichier s'il existe, sinon dessin du code. */
static void draw_set_icon(int id, float cx, float cy, unsigned int c) {
  if (draw_ico_file(id, cx, cy, 20.0f, c)) return;
  /* pas de fichier : certaines icones reprennent le dessin d'une autre */
  if (id == ICO_SYS_TILES) id = ICO_VIEW;
  else if (id == ICO_MSAA) id = ICO_GLOSS;
  else if (id == ICO_FPS) id = ICO_CLOCK;
  else if (id == ICO_BD_ICON) id = ICO_BACKDROP;
  else if (id == ICO_M_COLL) id = ICO_COLL;
  switch (id) {
  case ICO_SEARCH:
  case ICO_M_SEARCH: /* loupe */
    draw_ring(cx - 2, cy - 2, 6.5f, 1.8f, c);
    vita2d_draw_line(cx + 2.5f, cy + 2.5f, cx + 7, cy + 7, c);
    vita2d_draw_line(cx + 3.5f, cy + 2.5f, cx + 8, cy + 7, c);
    vita2d_draw_line(cx + 2.5f, cy + 3.5f, cx + 7, cy + 8, c);
    break;
  case ICO_SOUND: /* haut-parleur */
    fill_rect(cx - 8, cy - 3, 4, 6, c);
    for (int i = 0; i < 5; ++i) fill_rect(cx - 4 + i, cy - 3 - i, 1, 6 + 2 * i, c);
    draw_arc(cx + 2, cy, 5.0f, 1.5f, -0.9f, 0.9f, c);
    draw_arc(cx + 2, cy, 9.0f, 1.5f, -0.9f, 0.9f, c);
    break;
  case ICO_QUIT: /* marche / arret */
    draw_arc(cx, cy + 1, 7.5f, 1.8f, -1.5708f + 0.7f, -1.5708f + 6.2832f - 0.7f, c);
    round_rect(cx - 1.1f, cy - 10, 2.2f, 10, 1.1f, c);
    break;
  case 0: /* fond : cadre + montagne + soleil */
    draw_frame(cx - 8, cy - 7, 16, 14, 1.5f, c);
    vita2d_draw_line(cx - 6, cy + 5, cx - 1, cy - 1, c);
    vita2d_draw_line(cx - 1, cy - 1, cx + 2, cy + 3, c);
    vita2d_draw_line(cx + 2, cy + 3, cx + 4, cy + 1, c);
    vita2d_draw_fill_circle(cx + 4, cy - 3, 1.6f, c);
    break;
  case 1: /* cadres glossy */
    round_outline(cx - 8, cy - 6, 16, 12, 4, c);
    fill_rect(cx - 5, cy - 3, 7, 1.5f, c);
    break;
  case 2: /* theme : disque a moitie plein */
    draw_ring(cx, cy, 7.5f, 1.5f, c);
    for (int dy = -6; dy <= 6; ++dy) {
      float w = sqrtf(42.0f - (float)(dy * dy));
      fill_rect(cx, cy + dy, w, 1, c);
    }
    break;
  case 3: /* accent : pastille (l'interieur prend la couleur d'accent courante) */
    draw_ring(cx, cy, 8.5f, 1.4f, T.text_soft);
    vita2d_draw_fill_circle(cx, cy, 6.5f, T.acc);
    break;
  case 4: /* affichage : 4 carres */
    fill_rect(cx - 7, cy - 7, 6, 6, c);
    fill_rect(cx + 1, cy - 7, 6, 6, c);
    fill_rect(cx - 7, cy + 1, 6, 6, c);
    fill_rect(cx + 1, cy + 1, 6, 6, c);
    break;
  case 5: /* identifiant */
    draw_text_center(ST_SEMI, (int)cx, (int)cy + 5, FS_SMALL, c, "ID");
    break;
  case 6: /* jeux masques : oeil barre */
    vita2d_draw_line(cx - 8, cy, cx, cy - 5, c);
    vita2d_draw_line(cx, cy - 5, cx + 8, cy, c);
    vita2d_draw_line(cx + 8, cy, cx, cy + 5, c);
    vita2d_draw_line(cx, cy + 5, cx - 8, cy, c);
    vita2d_draw_fill_circle(cx, cy, 2.2f, c);
    vita2d_draw_line(cx - 7, cy + 7, cx + 7, cy - 7, c);
    break;
  case 7: /* scanner : loupe */
    draw_ring(cx - 2, cy - 2, 5.5f, 1.5f, c);
    vita2d_draw_line(cx + 2, cy + 2, cx + 7, cy + 7, c);
    vita2d_draw_line(cx + 3, cy + 2, cx + 8, cy + 7, c);
    break;
  case 8: /* supprimer : corbeille */
    fill_rect(cx - 7, cy - 6, 14, 2, c);
    fill_rect(cx - 2, cy - 8, 4, 2, c);
    draw_frame(cx - 5, cy - 4, 10, 11, 1.5f, c);
    break;
  case 9: /* tri : trois barres */
    fill_rect(cx - 7, cy - 6, 14, 2, c);
    fill_rect(cx - 7, cy - 1, 10, 2, c);
    fill_rect(cx - 7, cy + 4, 6, 2, c);
    break;
  case 10: /* telecharger */
    fill_rect(cx - 1, cy - 8, 2, 10, c);
    vita2d_draw_line(cx - 5, cy - 1, cx, cy + 4, c);
    vita2d_draw_line(cx, cy + 4, cx + 5, cy - 1, c);
    fill_rect(cx - 7, cy + 7, 14, 2, c);
    break;
  case 12: /* bibliotheque : livres */
    fill_rect(cx - 8, cy - 5, 4, 12, c);
    fill_rect(cx - 3, cy - 8, 4, 15, c);
    fill_rect(cx + 2, cy - 3, 4, 10, c);
    break;
  case 13: /* jouer : triangle */
    draw_tri(cx - 4, cy, 10, 13, 1, c);
    break;
  case 14: /* fermer : croix */
    vita2d_draw_line(cx - 6, cy - 6, cx + 6, cy + 6, c);
    vita2d_draw_line(cx - 6, cy + 6, cx + 6, cy - 6, c);
    vita2d_draw_line(cx - 5, cy - 6, cx + 7, cy + 6, c);
    vita2d_draw_line(cx - 5, cy + 6, cx + 7, cy - 6, c);
    break;
  case 15: /* horloge */
    draw_ring(cx, cy, 7.5f, 1.4f, c);
    vita2d_draw_line(cx, cy, cx, cy - 5, c);
    vita2d_draw_line(cx, cy, cx + 4, cy + 2, c);
    break;
  case 16: /* etiquette (categorie) */
    draw_frame(cx - 7, cy - 5, 14, 10, 1.5f, c);
    fill_rect(cx - 4, cy - 1, 8, 1.5f, c);
    break;
  case 17: /* fond d'ecran : ecran + paysage */
    round_outline_t(cx - 9, cy - 7, 18, 14, 3, 1.5f, c);
    vita2d_draw_line(cx - 6, cy + 4, cx - 2, cy - 1, c);
    vita2d_draw_line(cx - 2, cy - 1, cx + 1, cy + 2, c);
    vita2d_draw_line(cx + 1, cy + 2, cx + 3, cy, c);
    vita2d_draw_line(cx + 3, cy, cx + 6, cy + 4, c);
    vita2d_draw_fill_circle(cx + 4, cy - 3, 1.5f, c);
    break;
  case 18: /* voile : carre a moitie rempli */
    round_outline_t(cx - 8, cy - 8, 16, 16, 3, 1.5f, c);
    round_rect(cx - 8, cy - 8, 8, 16, 2, c);
    break;
  case 19: /* collection : cartes empilees */
    round_outline_t(cx - 4, cy - 8, 12, 14, 3, 1.5f, with_alpha(c, ((c >> 24) & 0xFF) / 2));
    round_rect(cx - 8, cy - 4, 12, 14, 3, c);
    break;
  default:
    break;
  }
}

/* Texte qui defile lentement quand il est trop long pour la place (sinon affiche normalement). */
static float marquee_t0;
static void draw_marquee(int style, int x, int y, int size, int maxw, unsigned int col, const char *txt, int active) {
  int w = text_w(style, size, txt);
  if (!active || w <= maxw) {
    char b[96];
    fit_text(style, size, txt, maxw, b, sizeof(b));
    draw_text(style, x, y, size, col, b);
    return;
  }
  float over = (float)(w - maxw), speed = 38.0f, run = over / speed;
  float t = fmodf(g_time - marquee_t0, 2.0f * run + 3.0f), off;
  if (t < 1.0f) off = 0.0f;
  else if (t < 1.0f + run) off = (t - 1.0f) * speed;
  else if (t < 2.0f + run) off = over;
  else off = over - (t - 2.0f - run) * speed;
  vita2d_set_clip_rectangle(x - 2, y - size - 4, x + maxw, y + 10);
  vita2d_enable_clipping();
  draw_text(style, x - (int)off, y, size, col, txt);
  vita2d_disable_clipping();
}

/* 3) Liste : titres a gauche, apercu de la jaquette a droite. */
static void draw_view_list(Game **list, int n, int sel, const char *button) {
  float pa = g_pa, ha = g_pa * g_ha;
  int first = sel > 7 ? sel - 7 : 0;
  float lx = CONTENT_X + g_ox, lw = 320;
  static int last_sel = -1;
  if (sel != last_sel) {
    last_sel = sel;
    marquee_t0 = g_time;
  }
  for (int k = 0; k < 8 && first + k < n; ++k) {
    int i = first + k;
    float y = 70 + k * 50;
    float f = focus[i];
    if (f > 0.02f) {
      round_card(lx, y, lw, 44, 12, T.sel_fill, T.acc, pa * f);
      if (opt_gloss) gloss(lx, y, lw, 44, 12, 0.5f, 22, pa * f);
    }
    hit_add(lx - g_ox, y, lw, 44, H_ITEM, i);
    int dw = opt_show_id ? text_w(ST_REG, FS_BODY, list[i]->detail) : 0;
    draw_marquee(f > 0.5f ? ST_SEMI : ST_REG, (int)lx + 22, (int)y + 29, FS_BODY,
                 (int)lw - 22 - 18 - dw - (opt_show_id ? 14 : 0) - (list[i]->fav ? 20 : 0), fade(T.text, pa), list[i]->name, i == sel);
    if (list[i]->fav) draw_heart(lx + lw - 22, y + 22, 18, fade(T.acc, pa));
    if (opt_show_id)
      draw_text_right(ST_REG, (int)(lx + lw) - (list[i]->fav ? 38 : 18), (int)y + 29, FS_BODY,
                      fade(T.muted, pa), list[i]->detail);
  }

  if (first > 0) draw_chev(lx + lw / 2, 60, 2, pa);
  if (first + 8 < n) draw_chev(lx + lw / 2, 70 + 8 * 50 + 2, 3, pa);

  /* Apercu a droite : jaquette, titre (3 lignes reservees), categorie, temps, lancements, favori */
  const Game *hero = list[sel];
  float px = CONTENT_X + 340 + g_ox, py = 70, pw = CONTENT_R - (CONTENT_X + 340), ph = 408;
  soft_shadow(px + 4, py + 10, pw - 8, ph - 8, 18, 22, fade(C(0, 0, 0, T.light ? 36 : 90), pa));
  round_rect(px, py, pw, ph, 18, fade(opt_backdrop || wall_path[0] ? T.glass : T.card, pa));
  if (opt_gloss) gloss(px, py, pw, ph, 18, 0.30f, 26, pa);
  round_outline(px, py, pw, ph, 18, fade(opt_gloss ? T.glass_border : T.card_border, pa));
  if (xmas_on()) { /* theme Noel : guirlande et neige sur l'apercu */
    garland(px, py, pw, ph, pa);
    snow_cap(px + 10, py, pw - 28, 11, 16.0f, pa);
  }
  draw_thumb_xmas(hero, px + 16 + 106, py + 40, 212, 262, ha, 1, 2, 1.0f);
  float cx2 = px + 244, cw2 = pw - 244 - 16;
  draw_wrapped(ST_SEMI, (int)cx2, (int)py + 38, FS_BODY, (int)cw2, 3, 22, fade(T.text, ha), hero->name);
  float iy = py + 38 + 3 * 22 + 8; /* les lignes suivantes ne bougent jamais */
  {
    const Stat *st = stat_find(hero->id, 0);
    char v[40];
    draw_text(ST_REG, (int)cx2, (int)iy, FS_SMALL, fade(T.acc_text, ha), opt_show_id ? hero->detail : kind_label_ex(hero, 0));
    float sy2 = iy + 30;
    draw_set_icon(15, cx2 + 8, sy2 - 5, fade(T.muted, ha));
    if (st && st->total) fmt_duration(st->total, v, sizeof(v));
    else snprintf(v, sizeof(v), "-");
    draw_text(ST_REG, (int)cx2 + 24, (int)sy2, FS_SMALL, fade(T.text_soft, ha), v);
    draw_set_icon(13, cx2 + 8, sy2 + 26, fade(T.muted, ha));
    snprintf(v, sizeof(v), L(st && st->launches > 1 ? "%u lancements" : "%u lancement"), st ? st->launches : 0u);
    draw_text(ST_REG, (int)cx2 + 24, (int)sy2 + 31, FS_SMALL, fade(T.text_soft, ha), v);
    float ly = sy2 + 62;
    if (hero->fav) {
      draw_heart(cx2 + 8, ly - 5, 16, fade(T.acc, ha));
      draw_text(ST_REG, (int)cx2 + 24, (int)ly, FS_SMALL, fade(T.text_soft, ha), "Favori");
      ly += 31;
    }
    char ct[160];
    if (coll_text(hero, ct, sizeof(ct))) { /* collections du jeu, sur 2 lignes au plus */
      draw_set_icon(19, cx2 + 8, ly - 5, fade(T.muted, ha));
      draw_wrapped(ST_REG, (int)cx2 + 24, (int)ly, FS_SMALL, (int)cw2 - 24, 2, 20, fade(T.text_soft, ha), ct);
    }
  }
  float bx = px + 16, by = py + ph - 52;
  float bw2 = (pw - 32 - 8) / 2.0f; /* deux boutons de meme largeur */
  primary_button(bx, by, bw2, 38, pa);
  draw_btn(bx + 22, by + 19, 'X', fade(T.btn_text, pa));
  draw_text(ST_SEMI, (int)bx + 42, (int)by + 25, FS_BODY, fade(T.btn_text, pa), button);
  hit_add(bx - g_ox, by, bw2, 38, H_LAUNCH, 0);
  draw_info_button(bx + bw2 + 8, by, bw2, pa);
  hit_add(bx + bw2 + 8 - g_ox, by, bw2, 38, H_INFO, 0);
  if (xmas_on()) holly_at(bx - 16, by - 26, 52, 0, -12, pa); /* sur le bouton Lancer */
}

/* Liste vide : message centre. */
static void draw_empty(const char *title, const char *line1, const char *line2) {
  draw_text(ST_SEMI, CONTENT_X + g_ox, 98, FS_SECTION, fade(T.text, g_pa), title);
  int cx = CONTENT_X + TILE_W / 2 + g_ox;
  draw_text_center(ST_SEMI, cx, 270, FS_SECTION, fade(T.text_soft, g_pa), line1);
  draw_text_center(ST_REG, cx, 304, FS_BODY, fade(T.muted, g_pa), line2);
}

/* ======================================================================
 *  Choix du systeme (menu Jeux, premier niveau)
 * ====================================================================== */

static void draw_system_card(int idx, float x, float y, float w, float h, float f) {
  float alpha = g_pa;
  float s = 1.0f + 0.04f * f + 0.014f * sinf(f * 3.14159f);
  float dw = w * s, dh = h * s;
  float cx = x - (dw - w) / 2.0f;
  float cy = y - (dh - h) / 2.0f - 3.0f * f;
  int is_add = idx >= (int)SYSTEM_COUNT;

  if (f > 0.02f) glow(cx, cy, dw, dh, 16, f, alpha);
  round_card(cx, cy, dw, dh, 16, f > 0.02f ? T.sel_fill : with_alpha(T.card, 225),
             f > 0.02f ? T.acc : T.card_border, alpha * (0.85f + 0.15f * f));
  if (f > 0.02f) round_outline_t(cx, cy, dw, dh, 16, 2.0f, fade(T.acc, alpha * f));
  if (opt_gloss) gloss(cx, cy, dw, dh, 16, 0.38f, f > 0.02f ? 26 : 16, alpha);
  if (xmas_on()) snow_cap_ref(cx, cy, dw - 1, w - 1, (unsigned)(idx * 31 + 3), 10.0f, alpha); /* devant le contour */

  float icx = cx + dw / 2, icy = cy + 12 * s;
  float side = dh * 0.64f; /* zone de l'icone : grande */
  float ty = cy + dh - 54 * s, ty2 = cy + dh - 24 * s;
  if (is_add) {
    float m = icy + side / 2;
    fill_rect(icx - 26, m - 3, 52, 6, fade(T.muted, alpha));
    fill_rect(icx - 3, m - 26, 6, 52, fade(T.muted, alpha));
    draw_text_center(ST_SEMI, (int)icx, (int)ty, FS_SECTION, fade(T.text, alpha), "Ajouter");
    draw_text_center(ST_REG, (int)icx, (int)ty2, FS_BODY, fade(T.muted, alpha), "un système");
    return;
  }
  const System *sy = &systems[idx];
  if (sy->icon) {
    float th = vita2d_texture_get_height(sy->icon), tw0 = vita2d_texture_get_width(sy->icon);
    float sc = side / th;
    if (tw0 * sc > dw - 24) sc = (dw - 24) / tw0;
    float tw = tw0 * sc, thh = th * sc;
    vita2d_draw_texture_tint_scale(sy->icon, icx - tw / 2, icy + (side - thh) / 2, sc, sc,
                                   fade(C(255, 255, 255, 255), alpha));
  } else {
    float sq = side * 0.8f, sx = icx - sq / 2, sy0 = icy + (side - sq) / 2;
    round_rect(sx + 3, sy0 + 5, sq, sq, 20, fade(C(0, 0, 0, 90), alpha));
    round_rect(sx, sy0, sq, sq, 20, fade(sy->color, alpha));
    if (opt_gloss) gloss(sx, sy0, sq, sq, 20, 0.45f, 40, alpha);
    draw_text_center(ST_SEMI, (int)icx, (int)(sy0 + sq / 2 + 8), FS_SECTION,
                     fade(C(255, 255, 255, 255), alpha), sy->tag);
  }
  if (idx == sys_move_si) { /* tuile en deplacement : fleches de part et d'autre */
    unsigned int ac = fade(T.acc_text, alpha);
    round_outline_t(cx - 4, cy - 4, dw + 8, dh + 8, 19, 2.5f, fade(T.acc, alpha));
    draw_tri(cx + 14, cy + dh / 2, 12, 18, -1, ac);
    draw_tri(cx + dw - 14, cy + dh / 2, 12, 18, 1, ac);
  }
  char count[32], nm[64];
  if (sy->count == 0) snprintf(count, sizeof(count), "Aucun jeu");
  else snprintf(count, sizeof(count), L(sy->count > 1 ? "%d jeux" : "%d jeu"), sy->count);
  int nsz = text_w(ST_SEMI, FS_SECTION, sy->name) <= (int)dw - 20 ? FS_SECTION : FS_BODY;
  fit_text(ST_SEMI, nsz, sy->name, (int)dw - 20, nm, sizeof(nm));
  draw_text_center(ST_SEMI, (int)icx, (int)ty, nsz, fade(T.text, alpha), nm);
  draw_text_center(ST_REG, (int)icx, (int)ty2, FS_BODY, fade(T.muted, alpha), count);
}

static int cur_coll; /* collection ouverte (indice dans coll_ids) */

/* Tuile d'une collection : mosaique des jaquettes de ses jeux, nom et nombre de jeux. */
static void draw_coll_card(int idx, float x, float y, float w, float h, float f) {
  float alpha = g_pa;
  float s = 1.0f + 0.04f * f + 0.014f * sinf(f * 3.14159f);
  float dw = w * s, dh = h * s;
  float cx = x - (dw - w) / 2.0f;
  float cy = y - (dh - h) / 2.0f - 3.0f * f;
  int is_add = idx >= coll_n;

  if (f > 0.02f) glow(cx, cy, dw, dh, 16, f, alpha);
  round_card(cx, cy, dw, dh, 16, f > 0.02f ? T.sel_fill : with_alpha(T.card, 225),
             f > 0.02f ? T.acc : T.card_border, alpha * (0.85f + 0.15f * f));
  if (f > 0.02f) round_outline_t(cx, cy, dw, dh, 16, 2.0f, fade(T.acc, alpha * f));
  if (opt_gloss) gloss(cx, cy, dw, dh, 16, 0.38f, f > 0.02f ? 26 : 16, alpha);
  if (xmas_on()) snow_cap_ref(cx, cy, dw - 1, w - 1, (unsigned)(idx * 37 + 5), 10.0f, alpha); /* devant le contour */

  float pad = 12 * s, ix = cx + pad, iy = cy + pad, iw = dw - 2 * pad, ih = dh * 0.64f - pad;
  float ty = cy + dh - 50 * s, ty2 = cy + dh - 22 * s, icx = cx + dw / 2;
  if (is_add) {
    float m = iy + ih / 2;
    round_outline_t(icx - 34, m - 34, 68, 68, 18, 2.0f, fade(T.muted, alpha));
    round_rect(icx - 18, m - 2.5f, 36, 5, 2, fade(T.muted, alpha));
    round_rect(icx - 2.5f, m - 18, 5, 36, 2, fade(T.muted, alpha));
    draw_text_center(ST_SEMI, (int)icx, (int)ty, FS_SECTION, fade(T.text, alpha), "Nouvelle");
    draw_text_center(ST_REG, (int)icx, (int)ty2, FS_BODY, fade(T.muted, alpha), "collection");
    return;
  }
  int k = coll_ids[idx], cnt = coll_cnt[k];
  unsigned int white = fade(C(255, 255, 255, 255), alpha);
  for (int q = 0; q < cnt && q < 4; ++q) coll_ptrs[k][q]->seen = g_frame; /* images de la mosaique */
  if (cnt == 0) { /* collection vide : cartes empilees */
    float m = iy + ih / 2;
    round_rect(icx - 22, m - 38, 56, 70, 10, fade(with_alpha(T.acc, 90), alpha));
    round_rect(icx - 34, m - 30, 56, 70, 10, fade(T.acc, alpha));
    if (opt_gloss) gloss(icx - 34, m - 30, 56, 70, 10, 0.45f, 50, alpha);
  } else if (cnt == 1 || iw < 120) {
    Game *g = coll_ptrs[k][0];
    round_rect(ix, iy, iw, ih, 10, fade(g->color, alpha));
    if (g->cover) tex_cover(g->cover, ix, iy, iw, ih, 10, fade(white, g->appear));
  } else { /* mosaique 2 x 2 */
    float gap = 4, cw = (iw - gap) / 2, chh = (ih - gap) / 2;
    for (int q = 0; q < 4; ++q) {
      float qx = ix + (q % 2) * (cw + gap), qy = iy + (q / 2) * (chh + gap);
      if (q < cnt) { /* couleur du jeu, puis son image en fondu quand elle est chargee */
        Game *mg = coll_ptrs[k][q];
        round_rect(qx, qy, cw, chh, 8, fade(mg->color, alpha));
        if (mg->cover) tex_cover(mg->cover, qx, qy, cw, chh, 8, fade(white, mg->appear));
      } else {
        round_rect(qx, qy, cw, chh, 8, fade(with_alpha(T.card_border, 140), alpha));
      }
    }
  }
  char count[32], nm[64];
  if (cnt == 0) snprintf(count, sizeof(count), "Vide");
  else snprintf(count, sizeof(count), L(cnt > 1 ? "%d jeux" : "%d jeu"), cnt);
  int nsz = text_w(ST_SEMI, FS_SECTION, coll_name[k]) <= (int)dw - 20 ? FS_SECTION : FS_BODY;
  fit_text(ST_SEMI, nsz, coll_name[k], (int)dw - 20, nm, sizeof(nm));
  draw_text_center(ST_SEMI, (int)icx, (int)ty, nsz, fade(T.text, alpha), nm);
  draw_text_center(ST_REG, (int)icx, (int)ty2, FS_BODY, fade(T.muted, alpha), count);
}


/* Choix d'un systeme (menu Jeux) ou d'une collection : tuiles en 1 ou 2 rangees. */
static void draw_chooser(int sel, int total, const char *title, int collections) {
  draw_text(ST_SEMI, CONTENT_X + g_ox, 98, FS_SECTION, fade(T.text, g_pa), title);
  int cols = opt_sys_mode == 1 ? 4 : 3, rows = opt_sys_mode == 2 ? 2 : 1;
  float tw = opt_sys_mode == 1 ? 180.0f : 240.0f, pitch = opt_sys_mode == 1 ? 193.0f : 254.0f;
  float th = rows == 2 ? 176.0f : 236.0f, y0 = rows == 2 ? 124.0f : 158.0f, rp = 192.0f;
  int first = 0, first_row = 0;
  if (rows == 1) first = sel > cols - 1 ? sel - (cols - 1) : 0;
  else first_row = sel / cols >= rows ? sel / cols - rows + 1 : 0;
  for (int k = 0; k < cols * rows; ++k) {
    int i = rows == 1 ? first + k : first_row * cols + k;
    if (i >= total) break;
    float tx = CONTENT_X + (k % cols) * pitch, ty = y0 + (k / cols) * rp;
    if (collections) draw_coll_card(i, tx + g_ox, ty, tw, th, focus[i]);
    else draw_system_card(i < (int)SYSTEM_COUNT ? sys_order[i] : i, tx + g_ox, ty, tw, th, focus[i]);
    hit_add(tx, ty, tw, th, H_ITEM, i);
  }
  if (rows == 1) {
    if (first > 0) draw_chev(CONTENT_X - 12 + g_ox, 276, 0, g_pa);
    if (first + cols < total) draw_chev(CONTENT_R + 8 + g_ox, 276, 1, g_pa);
  } else {
    if (first_row > 0) draw_chev(CONTENT_X + 379 + g_ox, 108, 2, g_pa);
    if ((first_row + rows) * cols < total) draw_chev(CONTENT_X + 379 + g_ox, FOOTER_Y - 8, 3, g_pa);
  }
}

