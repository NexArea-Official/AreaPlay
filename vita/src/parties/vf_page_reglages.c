/* Area Play - Page Reglages, liste courante, choix d image. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Reglages
 * ====================================================================== */

static int hidden_count(void);

#define SET_GROUPS 5
#define SG_NET 3   /* Reseau : etat, adresse IP, test de connexion */
#define SG_ABOUT 4 /* A propos : une page de texte, sans reglage */
static int set_group = -1, set_sel = 0; /* -1 = menu des reglages, 0..4 = sous-menu */
static const int set_rows[SET_GROUPS][9] = {
    {SR_LANG, SR_VIEW, SR_SYS_MODE, SR_SHOW_ID, SR_HIDDEN, SR_SORT, SR_SOUND, SR_FPS, SR_MSAA},
    {SR_THEME, SR_SNOW, SR_ACCENT, SR_WALL, SR_WALL_DIM, SR_SPLASH_BG, SR_BACKDROP, SR_BD_ICON, SR_GLOSS},
    {SR_SCAN, SR_EMU, SR_DOWNLOAD, SR_REGION, SR_DELETE, -1, -1, -1, -1},
    {SR_NET_STATE, SR_NET_IP, SR_NET_TEST, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1}};
static const int set_rows_n[SET_GROUPS] = {9, 8, 5, 3, 0};
static const char *set_group_names[SET_GROUPS] = {"Affichage", "Thème", "Bibliothèque", "Réseau", "À propos"};
static const char *set_group_desc[SET_GROUPS] = {"Langue, vue des jeux, tuiles, identifiant, jeux masqués, tri, performances",
                                                 "Thème, couleur d'accent, fond d'écran, fond dynamique, reflets",
                                                 "Scanner, émulateurs (RetroArch), jaquettes et fonds, supprimer",
                                                 "État, adresse IP, test de connexion",
                                                 "Version, crédits, licences"};

/* Lignes affichees d'un groupe : « Neige qui tombe » seulement avec le theme Noel. */
static int set_row_visible(int r) { return r != SR_SNOW || xmas_on(); }
static int set_count(int g) {
  int n = 0;
  for (int k = 0; k < set_rows_n[g]; ++k) n += set_row_visible(set_rows[g][k]);
  return n;
}
static int set_row_at(int g, int k) {
  for (int kk = 0; kk < set_rows_n[g]; ++kk)
    if (set_row_visible(set_rows[g][kk]) && k-- == 0) return set_rows[g][kk];
  return -1;
}

/* Reseau (vf_reseau.c) */
static int net_bars = -1; /* barres du Wi-Fi (0 a 4), -1 hors connexion */
static void net_state_text(char *out, int n);
static void net_ip_text(char *out, int n);
static char net_test_msg[64];

/* Petite icone Wi-Fi : nb barres pleines sur 4, centree sur (cx, cy). */
static void wifi_bars(float cx, float cy, int nb, unsigned int c) {
  for (int b = 0; b < 4; ++b) {
    float bh = 5 + b * 3;
    round_rect(cx - 9 + b * 5, cy + 7 - bh, 3, bh, 1.5f, b < nb ? c : fade(c, 0.3f));
  }
}

/* Page A propos : version, mention de transparence, credits (bibliotheques, police, jaquettes). */
static void draw_about(float pa) {
  float x = CONTENT_X + g_ox, y = 98, w = 640, h = 372;
  round_card(x, y, w, h, 14, with_alpha(T.card, 225), T.card_border, pa);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 44, FS_SECTION, fade(T.text, pa), VF_VERSION);
  draw_text(ST_REG, (int)x + 28, (int)y + 70, FS_SMALL, fade(T.muted, pa), "Hub de jeux pour PS Vita");
  /* sur deux lignes au besoin : marge de 28 a droite comme a gauche */
  draw_wrapped(ST_REG, (int)x + 28, (int)y + 104, FS_BODY, (int)w - 56, 2, 24, fade(T.text_soft, pa),
               "Conçu, dessiné et testé par moi sur PS Vita, développé avec l'aide d'une IA.");
  fill_rect(x + 28, y + 146, w - 56, 1, fade(T.line, pa));
  draw_text(ST_SEMI, (int)x + 28, (int)y + 178, FS_BODY, fade(T.text, pa), "Crédits");
  static const char *credits[] = {
      "VitaSDK et vita2d (licence MIT)",
      "libpng, zlib, libjpeg (Independent JPEG Group), bzip2",
      "FreeType : portions © The FreeType Project (freetype.org)",
      "Police Inter de Rasmus Andersson (SIL Open Font License 1.1)",
      "Jaquettes : hexflow-covers (jimbob4000) et libretro-thumbnails",
      "Inspiré de RetroFlow et HexFlow (aucun code repris)"};
  for (int i = 0; i < (int)(sizeof(credits) / sizeof(credits[0])); ++i)
    draw_text(ST_REG, (int)x + 28, (int)y + 208 + i * 28, FS_SMALL, fade(T.text_soft, pa), credits[i]);
}

/* Nom affiche du fond d'ecran : nom du fichier sans extension, ou "Thème". */
static void wall_display_name(const char *path, char *out, int n) {
  if (!path || !path[0]) {
    snprintf(out, n, "Thème");
    return;
  }
  const char *b = strrchr(path, '/');
  const char *bn = b ? b + 1 : path;
  if (strncmp(bn, "hiver", 5) == 0 && bn[5] >= '1' && bn[5] <= '9' && bn[6] == '.') { /* fonds d'hiver (theme Noel) */
    snprintf(out, n, L("Hiver (Noël) %d"), bn[5] - '0');
    return;
  }
  snprintf(out, n, "%s", b ? b + 1 : path);
  char *dot = strrchr(out, '.');
  if (dot && dot != out) *dot = 0;
  for (char *q = out; *q; ++q)
    if (*q == '_' || *q == '-') *q = ' ';
  if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}

static void draw_settings(int row) {
  float pa = g_pa;
  draw_text(ST_SEMI, CONTENT_X + g_ox, 80, FS_SECTION, fade(T.text, pa),
            set_group < 0 ? "Réglages" : set_group_names[set_group]);
  if (set_group < 0) {
    for (int g = 0; g < SET_GROUPS; ++g) {
      float y = 100 + g * 64, w = 640, h = 54, x = CONTENT_X + g_ox;
      int sel = g_zone == ZONE_CONTENT && g == set_sel;
      hit_add(x - g_ox, y, w, h, H_ITEM, g);
      if (sel) glow(x, y, w, h, 12, 1.0f, pa);
      round_card(x, y, w, h, 12, sel ? T.sel_fill : with_alpha(T.card, 225), sel ? T.acc : T.card_border, pa);
      if (sel && opt_gloss) gloss(x, y, w, h, 12, 0.45f, 26, pa);
      unsigned int ic = fade(sel ? T.acc_text : T.text_soft, pa);
      if (g == SG_ABOUT) { /* « i » dans un cercle */
        draw_ring(x + 32, y + 27, 10, 1.6f, ic);
        draw_text_center(ST_SEMI, (int)x + 32, (int)y + 33, FS_SMALL, ic, "i");
      } else if (g == SG_NET)
        wifi_bars(x + 32, y + 27, 4, ic);
      else
        draw_set_icon(g == 0 ? 4 : g == 1 ? 2 : 12, x + 32, y + 27, ic);
      draw_text(ST_SEMI, (int)x + 58, (int)y + 25, FS_BODY, fade(T.text, pa), set_group_names[g]);
      draw_text(ST_REG, (int)x + 58, (int)y + 44, FS_SMALL, fade(T.muted, pa), set_group_desc[g]);
      draw_tri(x + w - 34, y + 20, 10, 14, 1, fade(T.text_soft, pa));
    }
    draw_text(ST_REG, CONTENT_X + g_ox, 472, FS_SMALL, fade(T.muted, pa),
              "Images (" DATA_DIR ") : covers, backgrounds, systems, icons   (SELECT : choisir)");
    draw_text(ST_REG, CONTENT_X + g_ox, 494, FS_SMALL, fade(T.muted, pa),
              "Fonds d'écran : " DATA_DIR "/wallpapers   (PNG ou JPG, 960 x 544)");
    return;
  }
  if (set_group == SG_ABOUT) {
    draw_about(pa);
    return;
  }
  static const char *labels[SETTINGS_N] = {
      [SR_BACKDROP] = "Fond dynamique", [SR_GLOSS] = "Cadres glossy", [SR_THEME] = "Thème",
      [SR_ACCENT] = "Couleur d'accent", [SR_VIEW] = "Affichage des jeux", [SR_SHOW_ID] = "Afficher l'identifiant",
      [SR_HIDDEN] = "Jeux masqués", [SR_SCAN] = "Scanner la bibliothèque", [SR_DELETE] = "Supprimer la bibliothèque",
      [SR_SORT] = "Tri des jeux", [SR_DOWNLOAD] = "Télécharger jaquettes et fonds", [SR_SYS_MODE] = "Tuiles des systèmes",
      [SR_WALL] = "Fond d'écran", [SR_WALL_DIM] = "Voile sur le fond d'écran",
      [SR_MSAA] = "Anticrénelage (au redémarrage)", [SR_FPS] = "Compteur d'images (FPS)",
      [SR_BD_ICON] = "Fond des jeux sans image LiveArea", [SR_SOUND] = "Sons de navigation",
      [SR_EMU] = "Émulateurs", [SR_REGION] = "Région des jaquettes", [SR_LANG] = "Langue / Language",
      [SR_SNOW] = "Neige qui tombe", [SR_SPLASH_BG] = "Fond d'écran démarrage", [SR_NET_STATE] = "État", [SR_NET_IP] = "Adresse IP", [SR_NET_TEST] = "Tester la connexion"};
  static const int icon_of[SETTINGS_N] = { /* icone de chaque ligne (fichiers : assets/icons/ICONES.txt) */
      [SR_BACKDROP] = ICO_BACKDROP, [SR_GLOSS] = ICO_GLOSS, [SR_THEME] = ICO_THEME, [SR_ACCENT] = ICO_ACCENT,
      [SR_VIEW] = ICO_VIEW, [SR_SHOW_ID] = ICO_ID, [SR_HIDDEN] = ICO_HIDDEN, [SR_SCAN] = ICO_SCAN,
      [SR_DELETE] = ICO_DELETE, [SR_SORT] = ICO_SORT, [SR_DOWNLOAD] = ICO_DOWNLOAD, [SR_SYS_MODE] = ICO_SYS_TILES,
      [SR_WALL] = ICO_WALL, [SR_WALL_DIM] = ICO_DIM, [SR_MSAA] = ICO_MSAA, [SR_FPS] = ICO_FPS,
      [SR_BD_ICON] = ICO_BD_ICON, [SR_SOUND] = ICO_SOUND, [SR_EMU] = ICO_SYS_TILES, [SR_REGION] = ICO_DOWNLOAD, [SR_LANG] = ICO_ID,
      [SR_SNOW] = ICO_THEME, [SR_SPLASH_BG] = ICO_WALL, [SR_NET_STATE] = -1, [SR_NET_IP] = ICO_ID, [SR_NET_TEST] = ICO_SEARCH};
  for (int kk = 0, nk = set_count(set_group); kk < nk; ++kk) {
    int i = set_row_at(set_group, kk);
    float y = 92 + kk * 36, w = 640, h = 32;
    float x = CONTENT_X + g_ox;
    int sel = g_zone == ZONE_CONTENT && i == row;
    hit_add(x - g_ox, y, w, h, H_ITEM, kk);
    if (sel) glow(x, y, w, h, 11, 1.0f, pa);
    round_card(x, y, w, h, 11, sel ? T.sel_fill : with_alpha(T.card, 225),
               sel ? T.acc : T.card_border, pa);
    if (sel && opt_gloss) gloss(x, y, w, h, 11, 0.45f, 24, pa);
    if (i == SR_NET_STATE) wifi_bars(x + 32, y + 16, 4, fade(sel ? T.acc_text : T.text_soft, pa));
    else draw_set_icon(icon_of[i], x + 32, y + 16,
                       i == SR_ACCENT ? fade(T.acc, pa) : fade(sel ? T.acc_text : T.text_soft, pa));
    draw_text(ST_REG, (int)x + 58, (int)y + 23, FS_BODY, fade(T.text, pa), labels[i]);

    float rx = x + w - 24;
    if (i == SR_NET_STATE || i == SR_NET_IP || i == SR_NET_TEST) { /* valeurs du reseau (texte seul) */
      char v[64];
      if (i == SR_NET_STATE) net_state_text(v, sizeof(v));
      else if (i == SR_NET_IP) net_ip_text(v, sizeof(v));
      else snprintf(v, sizeof(v), "%s", net_test_msg[0] ? net_test_msg : L("Appuie sur X"));
      draw_text_right(ST_SEMI, (int)rx, (int)y + 23, FS_BODY, fade(i == SR_NET_TEST && !net_test_msg[0] ? T.muted : T.text, pa), v);
    } else if (i == SR_BACKDROP || i == SR_GLOSS || i == SR_SHOW_ID || i == SR_MSAA || i == SR_FPS || i == SR_BD_ICON ||
               i == SR_SNOW || i == SR_SPLASH_BG) { /* interrupteurs */
      int on = i == SR_BACKDROP ? opt_backdrop : i == SR_GLOSS ? opt_gloss : i == SR_SHOW_ID ? opt_show_id
               : i == SR_MSAA ? opt_msaa : i == SR_FPS ? opt_fps : i == SR_SNOW ? opt_snow
               : i == SR_SPLASH_BG ? opt_splash_bg : opt_bd_icon;
      const char *txt = i == SR_BACKDROP ? (on ? "Jaquette" : "Uni") : i == SR_BD_ICON ? (on ? "Image agrandie" : "Aucun")
                        : (on ? "Oui" : "Non");
      draw_switch(rx - 46, y + 5, on, pa);
      draw_text_right(ST_REG, (int)(rx - 46 - 14), (int)y + 23, FS_BODY,
                      fade(on ? T.text : T.muted, pa), txt);
    } else if (i == SR_WALL) {
      char wn[48];
      wall_display_name(wall_path, wn, sizeof(wn));
      draw_tri(rx - 10, y + 17, 10, 14, 1, fade(T.text_soft, pa));
      draw_text_right(ST_SEMI, (int)(rx - 10 - 14), (int)y + 23, FS_BODY,
                      fade(wall_path[0] ? T.text : T.muted, pa), wn);
    } else if (i == SR_THEME || i == SR_ACCENT || i == SR_VIEW || i == SR_SORT || i == SR_SYS_MODE || i == SR_WALL_DIM ||
               i == SR_SOUND || i == SR_REGION || i == SR_LANG) {
      const char *txt = i == SR_LANG ? lang_names[lang_index()] : i == SR_REGION ? region_names[opt_region & 3]
                        : i == SR_SOUND ? sound_names[opt_sound & 3]
                        : i == SR_WALL_DIM ? wall_dim_names[opt_wall_dim] : i == SR_SYS_MODE ? sys_mode_names[opt_sys_mode]
                        : i == SR_SORT ? sort_names[opt_sort] : i == SR_THEME ? themes[opt_theme].name
                        : i == SR_ACCENT ? accents[opt_accent].name : view_names[view_default];
      unsigned int ac = fade(T.text_soft, pa);
      draw_tri(rx - 10, y + 17, 10, 14, 1, ac);
      draw_text_right(ST_SEMI, (int)(rx - 10 - 14), (int)y + 23, FS_BODY,
                      fade(T.text, pa), txt);
      int tw = text_w(ST_SEMI, FS_BODY, txt);
      float lx = rx - 10 - 14 - tw - 14 - 10;
      draw_tri(lx, y + 17, 10, 14, -1, ac);
    } else if (i == SR_HIDDEN) {
      int hc = hidden_count();
      char txt[32];
      if (hc == 0) snprintf(txt, sizeof(txt), "Aucun");
      else snprintf(txt, sizeof(txt), L(hc > 1 ? "%d jeux" : "%d jeu"), hc);
      draw_tri(rx - 10, y + 17, 10, 14, 1, fade(T.text_soft, pa));
      draw_text_right(ST_REG, (int)(rx - 10 - 14), (int)y + 23, FS_BODY,
                      fade(hc ? T.text : T.muted, pa), txt);
    } else {
      draw_text_right(ST_REG, (int)rx, (int)y + 23, FS_BODY, fade(T.muted, pa),
                      "Appuie sur X");
    }
  }
  if (set_group == 2) draw_text(ST_REG, CONTENT_X + g_ox, 470, FS_SMALL, fade(T.text_soft, pa), scan_info);
  if (set_group == 0 && opt_msaa != msaa_active)
    draw_text(ST_REG, CONTENT_X + g_ox, 470, FS_SMALL, fade(T.acc_text, pa),
              "L'anticrénelage changera au prochain démarrage de " VF_APP_NAME ".");
}

/* ======================================================================
 *  Liste de jeux courante
 * ====================================================================== */

/* Retourne la liste affichee et son nombre d'elements.
 * NULL = le choix du systeme (n = systemes + tuile "Ajouter"). */
static Game **current_list(int page, int level, int sys, int *n) {
  if (page == PG_GAMES) {
    if (level == 0) {
      *n = (int)SYSTEM_COUNT + 1;
      return NULL;
    }
    *n = systems[sys].count;
    return systems[sys].games;
  }
  if (page == PG_COLL) { /* choix de la collection (+ tuile "Nouvelle"), puis ses jeux */
    if (level == 0 || cur_coll >= coll_n) {
      *n = coll_n + 1;
      return NULL;
    }
    *n = coll_cnt[coll_ids[cur_coll]];
    return coll_ptrs[coll_ids[cur_coll]];
  }
  if (page == 2) {
    *n = hb_n;
    return hb_ptrs;
  }
  if (page == 4) {
    *n = fav_n;
    return fav_ptrs;
  }
  if (page == PG_SEARCH) {
    *n = search_n;
    return search_ptrs;
  }
  *n = home_n; /* "Recemment joues" */
  return home_ptrs;
}

/* ======================================================================
 *  Choix d'une image (fenetre modale) pour un jeu ou un systeme
 * ====================================================================== */

typedef struct {
  char name[64];
} ImgFile;
#define PICK_MAX 2048
static ImgFile pick_files[PICK_MAX];
static int pick_n, picker_open, pick_sel, pick_sys_idx = -1;
static int pick_bg; /* 1 = fond du jeu (cle "bg:<ID>"), 0 = jaquette (cle "<ID>") ou image du systeme */
static Game *pick_game;
static vita2d_texture *pick_tex;
static float pick_timer;
static char pick_title[96];

static int cmp_imgfile(const void *a, const void *b) {
  return strcasecmp(((const ImgFile *)a)->name, ((const ImgFile *)b)->name);
}

static void picker_free_preview(void) {
  tex_free_later(pick_tex);
  pick_tex = NULL;
}

static void picker_key(char *out, int n) {
  if (pick_game) snprintf(out, n, pick_bg ? "bg:%s" : "%s", pick_game->id);
  else snprintf(out, n, "sys:%s", systems[pick_sys_idx].tag);
}

/* Ajoute les images d'un dossier de ux0:data/AreaPlay ("" = images) ; nom affiche "dossier/fichier". */
static void picker_add_dir(const char *sub) {
  char dir[64];
  if (sub[0]) snprintf(dir, sizeof(dir), DATA_DIR "/%s", sub);
  else snprintf(dir, sizeof(dir), IMG_DIR);
  SceUID d = sceIoDopen(dir);
  if (d < 0) return;
  int first = pick_n;
  SceIoDirent ent;
  for (;;) {
    memset(&ent, 0, sizeof(ent));
    if (sceIoDread(d, &ent) <= 0) break;
    if (SCE_S_ISDIR(ent.d_stat.st_mode)) continue;
    const char *nm = ent.d_name;
    if (!(ends_with_ci(nm, ".png") || ends_with_ci(nm, ".jpg") || ends_with_ci(nm, ".jpeg"))) continue;
    if (pick_n >= PICK_MAX) break;
    char full[96];
    if (sub[0]) snprintf(full, sizeof(full), "%s/%s", sub, nm);
    else snprintf(full, sizeof(full), "%s", nm);
    if (strlen(full) >= sizeof(pick_files[0].name)) continue;
    snprintf(pick_files[pick_n++].name, sizeof(pick_files[0].name), "%s", full);
  }
  sceIoDclose(d);
  qsort(pick_files + first, pick_n - first, sizeof(ImgFile), cmp_imgfile); /* trie dans chaque dossier */
}

static const char *pick_dir_name(void) { return !pick_game ? "systems" : pick_bg ? "backgrounds" : "covers"; }

static void picker_open_for(Game *g, int sys_idx) {
  pick_game = g;
  pick_sys_idx = sys_idx;
  pick_n = 0;
  sceIoMkdir(DATA_DIR, 0777);
  /* un dossier par usage : jaquettes dans covers, fonds dans backgrounds, systemes dans systems */
  char dir[64];
  snprintf(dir, sizeof(dir), DATA_DIR "/%s", pick_dir_name());
  sceIoMkdir(dir, 0777);
  picker_add_dir(pick_dir_name());
  char key[24];
  picker_key(key, sizeof(key));
  pick_sel = 0;
  const char *cur = custom_get(key);
  if (cur)
    for (int i = 0; i < pick_n; ++i)
      if (strcmp(pick_files[i].name, cur) == 0) {
        pick_sel = i + 1;
        break;
      }
  snprintf(pick_title, sizeof(pick_title), "%s", g ? g->name : systems[sys_idx].name);
  picker_free_preview();
  pick_timer = 0.05f;
  picker_open = 1;
}

static void picker_close(void) {
  picker_free_preview();
  picker_open = 0;
}

static void picker_move(int sel) {
  int total = pick_n + 1;
  if (sel < 0) sel = 0;
  if (sel > total - 1) sel = total - 1;
  if (sel == pick_sel) return;
  pick_sel = sel;
  picker_free_preview();
  pick_timer = 0.15f;
}

static void picker_apply(void) {
  char key[24];
  picker_key(key, sizeof(key));
  int reset = pick_sel == 0;
  custom_set(key, reset ? NULL : pick_files[pick_sel - 1].name);
  custom_save();
  picker_free_preview();
  if (pick_game && pick_bg) game_bg_refresh(pick_game);
  else if (pick_game) reload_cover(pick_game);
  else reload_system_icon(&systems[pick_sys_idx]);
  picker_open = 0;
  snprintf(toast_text, sizeof(toast_text), reset ? "Image d'origine rétablie" : "Image appliquée");
  toast_time = 2.0f;
}

/* Charge l'apercu quand la selection se stabilise. */
static void picker_update(float dt) {
  if (!picker_open || pick_tex || pick_sel == 0) return;
  if (pick_timer > 0.0f) pick_timer -= dt;
  if (pick_timer <= 0.0f) {
    char path[140];
    choice_path(pick_files[pick_sel - 1].name, path, sizeof(path));
    pick_tex = load_image_file(path);
    tex_prep(pick_tex);
    if (!pick_tex) pick_timer = 1e9f; /* illisible : on n'insiste pas */
  }
}

static void draw_picker(void) {
  float x = 140, y = 56, w = 680, h = 420;
  modal_panel(x, y, w, h, 215);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 44, FS_SECTION, T.text,
            !pick_game ? "Image du système" : pick_bg ? "Fond du jeu" : "Jaquette du jeu");
  char buf[96];
  fit_text(ST_REG, FS_BODY, pick_title, (int)w - 56, buf, sizeof(buf));
  draw_text(ST_REG, (int)x + 28, (int)y + 72, FS_BODY, T.muted, buf);

  int total = pick_n + 1;
  int first = pick_sel > 6 ? pick_sel - 6 : 0;
  for (int k = 0; k < 7 && first + k < total; ++k) {
    int i = first + k;
    float ry = y + 96 + k * 42;
    int sel = i == pick_sel;
    if (sel) round_card(x + 20, ry, 340, 38, 19, T.sel_fill, T.acc, 1.0f);
    const char *name = i == 0 ? "Image d'origine" : pick_files[i - 1].name;
    fit_text(sel ? ST_SEMI : ST_REG, FS_BODY, name, 340 - 28, buf, sizeof(buf));
    draw_text(sel ? ST_SEMI : ST_REG, (int)x + 34, (int)ry + 26, FS_BODY, T.text, buf);
  }
  if (pick_n == 0) {
    draw_text(ST_REG, (int)x + 34, (int)y + 96 + 42 + 28, FS_BODY, T.muted, "Aucune image trouvée.");
    draw_text(ST_REG, (int)x + 34, (int)y + 96 + 42 + 54, FS_SMALL, T.muted, "Copie des PNG ou JPG dans");
    char dn[64];
    snprintf(dn, sizeof(dn), DATA_DIR "/%s", pick_dir_name());
    draw_text(ST_REG, (int)x + 34, (int)y + 96 + 42 + 76, FS_SMALL, T.text_soft, dn);
  }

  float px = x + 384, py = y + 96, pw = 276, ph = 270;
  round_card(px, py, pw, ph, 14, T.bg, T.card_border, 1.0f);
  if (pick_tex) {
    float tw = vita2d_texture_get_width(pick_tex), th = vita2d_texture_get_height(pick_tex);
    float sc = (pw - 20) / tw;
    if (th * sc > ph - 20) sc = (ph - 20) / th;
    vita2d_draw_texture_tint_scale(pick_tex, px + (pw - tw * sc) / 2,
                                   py + (ph - th * sc) / 2, sc, sc,
                                   C(255, 255, 255, 255));
  } else {
    draw_text_center(ST_REG, (int)(px + pw / 2), (int)(py + ph / 2 + 6), FS_BODY, T.muted,
                     pick_sel == 0 ? "Image d'origine" : "Chargement...");
  }
  draw_hints((int)x + 28, (int)(y + h - 22), FS_SMALL, T.muted, "X Appliquer;O Annuler;. Haut / Bas : choisir");
}

