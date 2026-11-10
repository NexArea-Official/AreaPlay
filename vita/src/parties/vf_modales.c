/* Area Play - Barre de lettres, fenetres, ecran de chargement. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ---- Barre de lettres a droite (liste triee par nom) : glisser le doigt dessus saute a la lettre ---- */
#define LB_X 944
#define LB_Y0 76.0f
#define LB_H 416.0f
#define LB_MAX 40
static int lb_idx[LB_MAX], lb_ch[LB_MAX], lb_n;
static int lb_build(Game **list, int n) { /* un groupe par serie de titres de meme initiale ; une lettre sur deux si trop nombreuses */
  int idx[64], ch[64], gn = 0, last = -1;
  for (int i = 0; i < n && gn < 64; ++i) {
    int c = name_letter(list[i]);
    if (c != last) {
      idx[gn] = i;
      ch[gn] = c;
      ++gn;
      last = c;
    }
  }
  int step = gn > 22 ? 2 : 1;
  lb_n = 0;
  for (int g = 0; g < gn && lb_n < LB_MAX; g += step) {
    lb_idx[lb_n] = idx[g];
    lb_ch[lb_n] = ch[g];
    ++lb_n;
  }
  return lb_n;
}
static int lb_pick(Game **list, int n, float y) { /* indice du jeu sous le doigt, -1 si rien */
  if (!list || n <= 0 || !lb_build(list, n)) return -1;
  float pitch = LB_H / lb_n;
  int k = (int)((y - LB_Y0) / pitch);
  if (k < 0) k = 0;
  if (k >= lb_n) k = lb_n - 1;
  return lb_idx[k];
}
static void draw_letter_bar(Game **list, int n, int sel) {
  if (!lb_build(list, n)) return;
  float pitch = LB_H / lb_n;
  int cur = 0;
  for (int k = 0; k < lb_n; ++k)
    if (lb_idx[k] <= sel) cur = k;
  if (g_lb_a < 0.02f) return;
  float pa = g_pa * g_lb_a;
  round_rect(LB_X - 1, LB_Y0 - 6, 15, LB_H + 10, 7, fade(T.card, pa * 0.6f));
  for (int k = 0; k < lb_n; ++k) {
    char t[2] = {(char)lb_ch[k], 0};
    float cy = LB_Y0 + pitch * (k + 0.5f);
    int on = k == cur;
    draw_text_center(on ? ST_SEMI : ST_REG, LB_X + 6, (int)(cy + 6), FS_SMALL,
                     fade(on ? T.acc_text : T.muted, pa * (on ? 1.0f : 0.8f)), t);
  }
}

static void try_launch(Game *g) {
  char err[96];
  session_begin(g->id);
  if (launch_game(g, err, sizeof(err)) == 0) {
    sceKernelExitProcess(0); /* l'application demandee prend le relais */
  } else {
    session_cancel(g->id);
    snprintf(toast_text, sizeof(toast_text), "%s", err);
    toast_time = 3.0f;
  }
}

/* ---- Fenetre d'infos d'un jeu (touche triangle) ---- */
static int info_open;
static Game *info_game;

static void draw_info(void) {
  if (!info_game) return;
  const Game *g = info_game;
  float w = 640, h = 400, x = (SCREEN_W - w) / 2, y = 62;
  modal_panel(x, y, w, h, 225);
  { /* jaquette descendue (comme Android) : zone 190 x 260 a partir de y + 56 ; une jaquette carree ou large
     * finit a y + 306 (moins de vide dessous), une plus haute remonte (haut a y + 56 au plus tot) */
    float rx, ry, rw, rh;
    thumb_rect(g, x + 120, 0, 190, 260, &rx, &ry, &rw, &rh);
    float top = 250 - rh;
    if (top < 0) top = 0;
    if (top > 60) top = 60;
    draw_thumb(g, x + 120, y + 56 + top, 190, rh, 1.0f);
  }
  char buf[160], val[48];
  float tx = x + 250, tw = w - 250 - 28;
  fit_text(ST_SEMI, FS_SECTION, g->name, (int)tw, buf, sizeof(buf));
  draw_text(ST_SEMI, (int)tx, (int)y + 50, FS_SECTION, T.text, buf);
  draw_text(ST_REG, (int)tx, (int)y + 76, FS_SMALL, T.acc_text, kind_name(g->kind));
  if (coll_text(g, val, sizeof(val)) || g->fav) { /* collections (et favori) sur la fiche */
    char ct[160];
    coll_text(g, ct, sizeof(ct));
    snprintf(buf, sizeof(buf), "%s%s%s", g->fav ? L("Favori") : "", g->fav && ct[0] ? "  ·  " : "", ct);
    int ix = g->fav ? 22 : 0; /* coeur devant « Favori » ; collections seules : pas d'icone */
    if (g->fav) draw_heart_ex(tx + 7, y + 94, 14, T.acc, 0);
    char cut[160];
    fit_text(ST_REG, FS_SMALL, buf, (int)tw - ix, cut, sizeof(cut));
    draw_text(ST_REG, (int)tx + ix, (int)y + 100, FS_SMALL, T.text_soft, cut);
  }
  const Stat *st = stat_find(g->id, 0);
  unsigned launches = st ? st->launches : 0, total = st ? st->total : 0, first = st ? st->first : 0;
  const char *lab[6] = {"Lancements", "Temps de jeu (estimé)", "Durée moyenne", "Dernière partie",
                        "Premier lancement", "Identifiant"};
  for (int i = 0; i < 6; ++i) {
    float ry = y + 134 + i * 36;
    fill_rect(tx, ry + 10, tw, 1, T.card_border);
    draw_text(ST_REG, (int)tx, (int)ry + 2, FS_BODY, T.muted, lab[i]);
    if (i == 0) snprintf(val, sizeof(val), "%u", launches);
    else if (i == 1) fmt_duration(total, val, sizeof(val));
    else if (i == 2) {
      if (launches) fmt_duration(total / launches, val, sizeof(val));
      else snprintf(val, sizeof(val), "-");
    } else if (i == 3) {
      if (g->last_played > 0) ago_text(g->last_played, val, sizeof(val));
      else snprintf(val, sizeof(val), "Jamais lancé");
    } else if (i == 4) fmt_date(first, val, sizeof(val));
    else snprintf(val, sizeof(val), "%s", g->id[0] ? g->id : "-");
    draw_text_right(ST_SEMI, (int)(tx + tw), (int)ry + 2, FS_BODY, T.text, val);
  }
  { /* aides en bas a droite (comme Android) : largeur mesuree d'abord, sans rien afficher */
    int hw = draw_hints(0, -100, FS_SMALL, 0, "X Lancer;O Fermer") - 26;
    draw_hints((int)(x + w - 28) - hw, (int)(y + h - 20), FS_SMALL, T.muted, "X Lancer;O Fermer");
  }
}

/* Ligne d'une liste dans une fenetre : carte arrondie, surbrillance si selectionnee. */
static void modal_row(float x, float y, float w, float h, int sel) {
  if (sel) glow(x, y, w, h, h / 2, 1.0f, 1.0f);
  round_card(x, y, w, h, h / 2, sel ? T.sel_fill : with_alpha(T.bg, 120), sel ? T.acc : T.card_border, 1.0f);
}

static void draw_sysmgr(void) {
  float w = 520, h = 410, x = (SCREEN_W - w) / 2, y = 60;
  modal_panel(x, y, w, h, 215);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 44, FS_SECTION, T.text, "Systèmes");
  draw_hints((int)x + 28, (int)y + 72, FS_BODY, T.muted, "X Valider;O Fermer");
  if (sm_n == 0)
    draw_text(ST_REG, (int)x + 28, (int)y + 120, FS_BODY, T.muted, "Rien à ajouter ni à retirer");
  int first = sm_sel > 6 ? sm_sel - 6 : 0;
  for (int k = 0; k < 7 && first + k < sm_n; ++k) {
    int i = first + k;
    float ry = y + 90 + k * 44;
    int sel = i == sm_sel;
    modal_row(x + 20, ry, w - 40, 38, sel);
    draw_text(sel ? ST_SEMI : ST_REG, (int)x + 42, (int)ry + 26, FS_BODY, T.text, sm_text[i]);
  }
}

/* ---- Emulateurs (Reglages > Bibliotheque) : coeur RetroArch et dossier des jeux de chaque systeme ajoute ----
 * Deux lignes par systeme : "Coeur" (fichiers ux0:app/RETROVITA/<coeur>_libretro.self) et "Dossier"
 * (Auto = ux0:roms/<nom du systeme>, un sous-dossier de ux0:roms ou uma0:roms, ou Aucun). */
#define EM_DIRS 64
static int em_open, em_sel, em_dirty, em_ra, em_n, em_core_n, em_dir_n, em_all;
static int em_slots[MAX_CUSTOM_SYS];
static char (*em_cores)[40]; /* coeurs installes : liste agrandie au besoin (aucun coeur oublie) */
static int em_core_cap;

/* Place pour un coeur de plus (NULL seulement si la memoire manque). */
static char *em_core_add(void) {
  if (em_core_n >= em_core_cap) {
    int cap = em_core_cap ? em_core_cap * 2 : 256;
    char (*p)[40] = realloc(em_cores, (size_t)cap * sizeof(em_cores[0]));
    if (!p) return NULL;
    em_cores = p;
    em_core_cap = cap;
  }
  return em_cores[em_core_n++];
}
static char em_dirs[EM_DIRS][96];

static int cmp_core(const void *a, const void *b) { /* par nom de coeur, RetroArch puis Emu4Vita++ */
  const char *x = (const char *)a, *y = (const char *)b;
  int r = core_is_app(x) - core_is_app(y); /* emulateurs autonomes a la fin */
  if (!r) r = strcasecmp(core_name(x), core_name(y));
  return r ? r : core_is_e4v(x) - core_is_e4v(y);
}
static int cmp_dir(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

static void em_list_dirs(const char *root) {
  SceUID d = sceIoDopen(root);
  if (d < 0) return;
  SceIoDirent ent;
  for (;;) {
    memset(&ent, 0, sizeof(ent));
    if (sceIoDread(d, &ent) <= 0) break;
    if (!SCE_S_ISDIR(ent.d_stat.st_mode) || ent.d_name[0] == '.' || em_dir_n >= EM_DIRS) continue;
    if (snprintf(em_dirs[em_dir_n], sizeof(em_dirs[0]), "%s/%s", root, ent.d_name) < (int)sizeof(em_dirs[0])) ++em_dir_n;
  }
  sceIoDclose(d);
}

/* Coeurs installes d'un emulateur : fichiers <pre><coeur><suf> du dossier, ranges avec le prefixe tag. */
static int em_list_cores(const char *dir, const char *pre, const char *suf, const char *tag) {
  int found = 0;
  SceUID d = sceIoDopen(dir);
  if (d < 0) return 0;
  SceIoDirent ent;
  size_t pl = strlen(pre), sl = strlen(suf), tl = strlen(tag);
  for (;;) {
    memset(&ent, 0, sizeof(ent));
    if (sceIoDread(d, &ent) <= 0) break;
    const char *nm = ent.d_name;
    size_t l = strlen(nm);
    if (l <= pl + sl || strncasecmp(nm, pre, pl) != 0 || strcasecmp(nm + l - sl, suf) != 0) continue;
    if (tl + l - pl - sl >= sizeof(em_cores[0])) continue;
    char *c = em_core_add();
    if (!c) break;
    memcpy(c, tag, tl);
    memcpy(c + tl, nm + pl, l - pl - sl);
    c[tl + l - pl - sl] = 0;
    ++found;
  }
  sceIoDclose(d);
  return found;
}

static int em_e4v, em_apps; /* Emu4Vita++ installe, emulateurs autonomes installes */
static void em_refresh(void) {
  em_ra = file_exists("ux0:app/" RA_ID "/eboot.bin");
  em_e4v = file_exists("ux0:app/" E4V_ID "/eboot.bin");
  em_core_n = 0;
  em_list_cores("ux0:app/" E4V_ID, "eboot_", ".self", "e4v:");
  em_list_cores("ux0:app/" RA_ID, "", "_libretro.self", "");
  em_apps = 0;
  for (int i = 0; i < LEN(std_emus); ++i) { /* emulateurs autonomes installes */
    char ep[48];
    snprintf(ep, sizeof(ep), "ux0:app/%s/eboot.bin", std_emus[i].id);
    if (!file_exists(ep)) continue;
    char *c = em_core_add();
    if (!c) break;
    snprintf(c, sizeof(em_cores[0]), "app:%s", std_emus[i].id);
    ++em_apps;
  }
  if (em_core_n) qsort(em_cores, em_core_n, sizeof(em_cores[0]), cmp_core);
  sceIoMkdir(ROMS_ROOT, 0777); /* dossier a remplir : ux0:roms/<systeme> */
  em_dir_n = 0;
  em_list_dirs(ROMS_ROOT);
  em_list_dirs("uma0:roms");
  /* EBOOT PS1 sans bulle, et dossiers conseilles par DSVita et Azahar */
  static const char *pbp_dirs[4] = {"ux0:pspemu/PSP/GAME", "uma0:pspemu/PSP/GAME", "ux0:data/dsvita", "ux0:data/azahar/roms"};
  for (int i = 0; i < 4 && em_dir_n < EM_DIRS; ++i) {
    SceIoStat st;
    if (sceIoGetstat(pbp_dirs[i], &st) >= 0) snprintf(em_dirs[em_dir_n++], sizeof(em_dirs[0]), "%s", pbp_dirs[i]);
  }
  qsort(em_dirs, em_dir_n, sizeof(em_dirs[0]), cmp_dir);
  em_n = 0;
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k)
    if (SLOT_USED(k)) em_slots[em_n++] = k;
  if (em_sel >= em_n * 2) em_sel = 0;
}

static void em_open_win(void) {
  em_sel = 0;
  em_dirty = 0;
  em_all = 0;
  em_refresh();
  em_open = 1;
}

/* Coeurs adaptes a chaque console : le nom du systeme (simplifie, ex. "snes", "megadrive") est cherche
 * dans "noms", et seuls les coeurs commencant par un des "coeurs" sont proposes. */
static const struct {
  const char *names, *cores;
} em_fams[] = {
    {"snes supernintendo superfamicom sfc supernes", "snes9x bsnes mednafen_supafaust supafaust chimerasnes"},
    {"neogeocd ngcd", "neocd"},
    {"n64 nintendo64 ultra64", "DEDALOX64 mupen64 parallel_n64"},
    {"dreamcast dc segadreamcast", "FLYCASTDC flycast"},
    {"ds nds nintendods", "DSVITA000 melonds desmume"},
    {"3ds nintendo3ds", "AZAHARPSV citra"},
    {"easyrpg rpgmaker rpg", "EASYRPG01"},
    {"vectrex", "vecx"},
    {"amiga", "uae4arm puae"},
    {"pc98 pc9801", "np2kai nekop2"},
    {"nes famicom nintendo nintendoentertainmentsystem", "fceumm nestopia quicknes"},
    {"gb gameboy gbc gameboycolor", "gambatte gearboy tgbdual sameboy mgba vba"},
    {"gba gameboyadvance", "mgba gpsp vba meteor"},
    {"megadrive genesis md segagenesis segamegadrive", "genesis_plus_gx picodrive"},
    {"mastersystem sms segamastersystem gamegear gg", "genesis_plus_gx picodrive smsplus gearsystem"},
    {"megacd segacd mcd", "genesis_plus_gx picodrive"},
    {"32x sega32x", "picodrive"},
    {"ps1 psx psone playstation playstation1", "pcsx"},
    {"pcengine turbografx pce pcecd supergrafx tg16", "mednafen_pce"},
    {"neogeopocket ngp ngpc neogeopocketcolor", "mednafen_ngp race"},
    {"wonderswan ws wsc wonderswancolor", "mednafen_wswan"},
    {"atari2600 2600", "stella"},
    {"atari7800 7800", "prosystem"},
    {"lynx atarilynx", "handy mednafen_lynx"},
    {"virtualboy vb", "mednafen_vb"},
    {"arcade mame fbneo fba fbalpha neogeo cps cps1 cps2 cps3", "fbalpha fbneo mame"},
    {"zxspectrum spectrum zx", "fuse"},
    {"msx msx2", "bluemsx fmsx"},
    {"c64 commodore64 commodore", "frodo vice"},
    {"atari800 atari5200 5200", "atari800"},
    {"gameandwatch gw", "gw"},
    {"dos msdos pc", "dosbox"}};

static int word_in(const char *list, const char *w, int prefix) { /* w (ou un prefixe de w) dans la liste ? */
  size_t wl = strlen(w);
  while (*list) {
    const char *e = strchr(list, ' ');
    size_t l = e ? (size_t)(e - list) : strlen(list);
    if (prefix ? (l <= wl && strncmp(list, w, l) == 0) : (l == wl && strncmp(list, w, l) == 0)) return 1;
    list += l;
    while (*list == ' ') ++list;
  }
  return 0;
}

/* Le coeur convient-il au systeme k ? (systeme inconnu, ou "tous les coeurs" demande : oui) */
static int em_core_fits(int k, const char *core) {
  if (em_all) return 1;
  int known = 0;
  core = core_name(core);
  for (int f = 0; f < LEN(em_fams); ++f)
    if (word_in(em_fams[f].names, slot_file[k], 0)) {
      known = 1;
      if (word_in(em_fams[f].cores, core, 1)) return 1;
    }
  return !known;
}

/* Gauche / droite : valeur precedente / suivante de la ligne selectionnee. */
static void em_change(int dir) {
  if (em_n == 0) return;
  int k = em_slots[em_sel / 2];
  char key[16];
  if (em_sel % 2 == 0) { /* coeur : Aucun, puis les coeurs installes qui conviennent a ce systeme */
    int *list = malloc(((size_t)em_core_n + 1) * sizeof(int)), ln = 0, pos = 0;
    if (!list) return;
    list[ln++] = -1;
    const char *cur = rom_core_of(k);
    for (int i = 0; i < em_core_n; ++i) {
      int is_cur = cur && strcmp(em_cores[i], cur) == 0;
      if (!is_cur && !em_core_fits(k, em_cores[i])) continue;
      if (is_cur) pos = ln;
      list[ln++] = i;
    }
    pos = (pos + dir + ln) % ln;
    snprintf(key, sizeof(key), "rcore%d", k);
    custom_set(key, list[pos] < 0 ? NULL : em_cores[list[pos]]);
    free(list);
  } else { /* dossier : Auto, les sous-dossiers trouves, Aucun */
    snprintf(key, sizeof(key), "rdir%d", k);
    const char *cur = custom_get(key);
    int total = em_dir_n + 2, pos = 0;
    if (cur && strcmp(cur, "-") == 0) pos = total - 1;
    else
      for (int i = 0; cur && i < em_dir_n; ++i)
        if (strcmp(em_dirs[i], cur) == 0) pos = i + 1;
    pos = (pos + dir + total) % total;
    custom_set(key, pos == 0 ? NULL : pos == total - 1 ? "-" : em_dirs[pos - 1]);
  }
  custom_save();
  em_dirty = 1;
}

static void draw_emu(void) {
  float w = 720, h = 430, x = (SCREEN_W - w) / 2, y = 56;
  modal_panel(x, y, w, h, 215);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 44, FS_SECTION, T.text, "Émulateurs");
  char info[96];
  if (!em_core_n) snprintf(info, sizeof(info), "Aucun émulateur installé (RetroArch, Emu4Vita++, DaedalusX64...)");
  else snprintf(info, sizeof(info), L(em_core_n > 1 ? "%d cœurs ou émulateurs · jeux dans ux0:roms/<système>"
                                                   : "%d cœur ou émulateur · jeux dans ux0:roms/<système>"), em_core_n);
  draw_text(ST_REG, (int)x + 28, (int)y + 72, FS_BODY, em_core_n ? T.muted : T.acc_text, info);
  if (em_n == 0) {
    draw_text(ST_REG, (int)x + 28, (int)y + 124, FS_BODY, T.text, "Aucun système ajouté.");
    draw_text(ST_REG, (int)x + 28, (int)y + 150, FS_BODY, T.muted, "Crée d'abord un système : Jeux > Ajouter un système.");
  }
  int rows = em_n * 2, first = em_sel > 6 ? em_sel - 6 : 0;
  if (first % 2) --first; /* un systeme reste groupe : coeur puis dossier */
  for (int r = 0; r < 8 && first + r < rows; ++r) {
    int i = first + r, k = em_slots[i / 2], sel = i == em_sel;
    float ry = y + 92 + r * 38, rw = w - 40;
    modal_row(x + 20, ry, rw, 34, sel);
    char label[64], val[128], buf[96];
    if (i % 2 == 0) {
      snprintf(label, sizeof(label), L("%s · Cœur"), slot_name[k]);
      const char *c = rom_core_of(k);
      if (c) core_label(c, val, sizeof(val));
      else snprintf(val, sizeof(val), "Aucun");
    } else {
      snprintf(label, sizeof(label), L("%s · Dossier"), slot_name[k]);
      char key[16], dir[160];
      snprintf(key, sizeof(key), "rdir%d", k);
      const char *cur = custom_get(key);
      int is_auto = 0;
      if (cur && strcmp(cur, "-") == 0) snprintf(val, sizeof(val), "Aucun");
      else if (rom_dir_of(k, dir, sizeof(dir), &is_auto)) snprintf(val, sizeof(val), is_auto ? L("Auto : %s") : "%s", dir);
      else snprintf(val, sizeof(val), L("Auto : créer ux0:roms/%s"), slot_file[k]);
    }
    fit_text(sel ? ST_SEMI : ST_REG, FS_BODY, label, 250, buf, sizeof(buf));
    draw_text(sel ? ST_SEMI : ST_REG, (int)x + 42, (int)ry + 24, FS_BODY, i % 2 ? T.text_soft : T.text, buf);
    float rx = x + 20 + rw - 18;
    fit_text(ST_SEMI, FS_BODY, val, (int)rw - 330, buf, sizeof(buf));
    unsigned int ac = sel ? T.acc_text : T.text_soft;
    draw_tri(rx - 10, ry + 17, 10, 14, 1, ac);
    draw_text_right(ST_SEMI, (int)(rx - 24), (int)ry + 24, FS_BODY, T.text, buf);
    draw_tri(rx - 24 - text_w(ST_SEMI, FS_BODY, buf) - 24, ry + 17, 10, 14, -1, ac);
  }
  draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted,
             em_all ? ". Gauche / Droite : changer;T Cœurs adaptés;O Fermer (relit les jeux)"
                    : ". Gauche / Droite : changer;T Tous les cœurs;O Fermer (relit les jeux)");
}

/* ---- Collections d'un jeu (SELECT > Collections) : cocher / decocher, en creer une ---- */
static int ct_open, ct_sel;
static Game *ct_game;

static void draw_coll_toggle(void) {
  if (!ct_game) return;
  float w = 560, h = 430, x = (SCREEN_W - w) / 2, y = 56;
  modal_panel(x, y, w, h, 215);
  draw_set_icon(19, x + 40, y + 37, T.acc_text);
  draw_text(ST_SEMI, (int)x + 62, (int)y + 45, FS_SECTION, T.text, "Collections");
  char buf[96];
  fit_text(ST_REG, FS_BODY, ct_game->name, (int)w - 56, buf, sizeof(buf));
  draw_text(ST_REG, (int)x + 28, (int)y + 74, FS_BODY, T.muted, buf);
  int total = coll_n + 1, first = ct_sel > 6 ? ct_sel - 6 : 0;
  for (int k = 0; k < 7 && first + k < total; ++k) {
    int i = first + k;
    float ry = y + 92 + k * 42, rw = w - 40;
    int sel = i == ct_sel;
    modal_row(x + 20, ry, rw, 36, sel);
    if (i == 0) { /* creer une collection et y ranger le jeu */
      round_rect(x + 40, ry + 16.5f, 14, 3, 1, T.acc_text);
      round_rect(x + 45.5f, ry + 11, 3, 14, 1, T.acc_text);
      draw_text(sel ? ST_SEMI : ST_REG, (int)x + 66, (int)ry + 25, FS_BODY, T.text, "Nouvelle collection...");
      continue;
    }
    int slot = coll_ids[i - 1], on = (ct_game->coll >> slot) & 1;
    /* case a cocher */
    if (on) {
      round_rect(x + 38, ry + 9, 18, 18, 5, T.acc);
      vita2d_draw_line(x + 42, ry + 18, x + 46, ry + 22, T.btn_text);
      vita2d_draw_line(x + 46, ry + 22, x + 52, ry + 13, T.btn_text);
      vita2d_draw_line(x + 42, ry + 19, x + 46, ry + 23, T.btn_text);
      vita2d_draw_line(x + 46, ry + 23, x + 52, ry + 14, T.btn_text);
    } else {
      round_outline_t(x + 38, ry + 9, 18, 18, 5, 1.5f, T.text_soft);
    }
    fit_text(sel ? ST_SEMI : ST_REG, FS_BODY, coll_name[slot], (int)rw - 140, buf, sizeof(buf));
    draw_text(sel ? ST_SEMI : ST_REG, (int)x + 66, (int)ry + 25, FS_BODY, T.text, buf);
    snprintf(buf, sizeof(buf), "%d", coll_cnt[slot]);
    draw_text_right(ST_REG, (int)(x + 20 + rw - 18), (int)ry + 25, FS_BODY, T.muted, buf);
  }
  if (first > 0) draw_chev(x + w / 2, y + 86, 2, 1.0f);
  if (first + 7 < total) draw_chev(x + w / 2, y + 92 + 7 * 42 + 2, 3, 1.0f);
  draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted, "X Cocher / décocher;O Fermer");
}

/* ---- Options d'une collection (SELECT sur sa tuile) ---- */
static int cm_open, cm_sel, cm_slot;

static void draw_coll_menu(void) {
  if (cm_slot < 0 || cm_slot >= MAX_COLL || !COLL_USED(cm_slot)) return;
  float w = 460, h = 280, x = (SCREEN_W - w) / 2, y = 120;
  modal_panel(x, y, w, h, 215);
  char buf[64];
  fit_text(ST_SEMI, FS_SECTION, coll_name[cm_slot], (int)w - 90, buf, sizeof(buf));
  draw_set_icon(19, x + 40, y + 37, T.acc_text);
  draw_text(ST_SEMI, (int)x + 62, (int)y + 45, FS_SECTION, T.text, buf);
  static const char *items[3] = {"Renommer", "Supprimer la collection", "Fermer"};
  static const int icons[3] = {16, 8, 14};
  for (int i = 0; i < 3; ++i) {
    float ry = y + 72 + i * 50;
    int sel = i == cm_sel;
    modal_row(x + 20, ry, w - 40, 42, sel);
    draw_set_icon(icons[i], x + 48, ry + 21, sel ? T.acc_text : T.text_soft);
    draw_text(sel ? ST_SEMI : ST_REG, (int)x + 72, (int)ry + 28, FS_BODY, T.text, items[i]);
  }
  draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted, "X Valider;O Fermer");
}

static int hidden_count(void) {
  int c = 0;
  for (int i = 0; i < app_n; ++i)
    if (all_apps[i].hidden) ++c;
  return c;
}

static void draw_menu(void) {
  float w = 720, h = 448, x = (SCREEN_W - w) / 2, y = 48;
  modal_panel(x, y, w, h, 215);
  const Game *g = menu_game;
  /* colonne de gauche : jaquette et informations */
  draw_thumb(g, x + 24 + 96, y + 24, 192, 236, 1.0f);
  int nl = draw_wrapped(ST_SEMI, (int)x + 24, (int)y + 284, FS_BODY, 196, 2, 22, T.text, g->name);
  const Stat *st = stat_find(g->id, 0);
  char v[48];
  float iy = y + 284 + nl * 22 + 6;
  draw_set_icon(15, x + 31, iy - 5, T.muted);
  if (st && st->total) fmt_duration(st->total, v, sizeof(v));
  else snprintf(v, sizeof(v), "-");
  draw_text(ST_REG, (int)x + 46, (int)iy, FS_SMALL, T.text_soft, v);
  draw_set_icon(13, x + 31, iy + 18, T.muted);
  if (g->last_played > 0) ago_text(g->last_played, v, sizeof(v));
  else snprintf(v, sizeof(v), "Jamais lancé");
  draw_text(ST_REG, (int)x + 46, (int)iy + 23, FS_SMALL, T.text_soft, v);
  draw_set_icon(16, x + 31, iy + 41, T.muted);
  draw_text(ST_REG, (int)x + 46, (int)iy + 46, FS_SMALL, T.text_soft, kind_name(g->kind));
  {
    char ct[160], cut[96];
    if (coll_text(g, ct, sizeof(ct))) {
      draw_set_icon(19, x + 31, iy + 64, T.muted);
      fit_text(ST_REG, FS_SMALL, ct, 176, cut, sizeof(cut));
      draw_text(ST_REG, (int)x + 46, (int)iy + 69, FS_SMALL, T.text_soft, cut);
    }
  }
  fill_rect(x + 246, y + 24, 1, h - 48, T.card_border);

  /* colonne de droite : les options */
  static const char *items[MI_COUNT] = {"Jouer", "Favori", "Collections", "Catégorie", "Renommer...",
                                        "Choisir la jaquette...", "Autre version de la jaquette...", "Choisir le fond...",
                                        "Télécharger jaquette et fond", "Masquer ce jeu", "Fermer"};
  static const int icons[MI_COUNT] = {13, -1, 19, 16, ICO_ID, 0, ICO_DOWNLOAD, ICO_BACKDROP, 10, 6, 14};
  float ox = x + 268, ow = w - 268 - 24;
  for (int i = 0; i < MI_COUNT; ++i) {
    float ry = y + 18 + i * 37, rh = 32;
    int sel = i == menu_sel;
    if (sel) glow(ox, ry, ow, rh, 12, 1.0f, 1.0f);
    round_card(ox, ry, ow, rh, 12, sel ? T.sel_fill : with_alpha(T.bg, 120), sel ? T.acc : T.card_border, 1.0f);
    if (sel && opt_gloss) gloss(ox, ry, ow, rh, 12, 0.4f, 24, 1.0f);
    unsigned int ic = sel ? T.acc_text : T.text_soft;
    float my = ry + rh / 2;
    if (i == MI_FAV) draw_heart_ex(ox + 26, my, 18, ic, g->fav ? 0 : 1);
    else draw_set_icon(icons[i], ox + 26, my, ic);
    draw_text(sel ? ST_SEMI : ST_REG, (int)ox + 50, (int)my + 7, FS_BODY, T.text, items[i]);
    float rxx = ox + ow - 20;
    if (i == MI_FAV) {
      draw_switch(rxx - 46, my - 12, g->fav, 1.0f);
      draw_text_right(ST_REG, (int)(rxx - 46 - 14), (int)my + 7, FS_BODY, g->fav ? T.text : T.muted, g->fav ? "Oui" : "Non");
    }
    if (i == MI_COLL) { /* nombre de collections du jeu */
      char ct[160], cv[32];
      int nc = coll_text(g, ct, sizeof(ct));
      if (nc == 0) snprintf(cv, sizeof(cv), "Aucune");
      else snprintf(cv, sizeof(cv), L(nc > 1 ? "%d collections" : "%d collection"), nc);
      draw_tri(rxx - 10, my, 10, 14, 1, T.text_soft);
      draw_text_right(ST_REG, (int)(rxx - 24), (int)my + 7, FS_BODY, nc ? T.text : T.muted, cv);
    }
    if (i == MI_KIND) { /* selecteur : gauche / droite changent la categorie */
      const char *kv = kind_name(g->kind);
      int tw = text_w(ST_SEMI, FS_BODY, kv);
      draw_tri(rxx - 10, my, 10, 14, 1, T.text_soft);
      draw_text_right(ST_SEMI, (int)(rxx - 24), (int)my + 7, FS_BODY, T.text, kv);
      draw_tri(rxx - 24 - tw - 14 - 10, my, 10, 14, -1, T.text_soft);
    }
  }
}

/* ======================================================================
 *  Jeux masques (Reglages) : on peut les reafficher
 * ====================================================================== */

static int hid_open, hid_sel;
static Game *hid_list[MAX_TOTAL];
static int hid_n;

static void hidden_refresh(void) {
  hid_n = 0;
  for (int i = 0; i < app_n; ++i)
    if (all_apps[i].hidden) hid_list[hid_n++] = &all_apps[i];
  if (hid_sel >= hid_n) hid_sel = hid_n > 0 ? hid_n - 1 : 0;
}

static void unhide_all(void) {
  for (int i = 0; i < app_n; ++i) {
    Game *g = &all_apps[i];
    if (!g->hidden) continue;
    char key[24];
    snprintf(key, sizeof(key), "hide:%s", g->id);
    custom_set(key, NULL);
    g->hidden = 0;
  }
  custom_save();
  build_lists();
  lists_changed = 1;
}

static int conf_open, conf_kind; /* conf_kind : 0 = supprimer la bibliotheque, 1 = quitter, 2 = supprimer une collection */
static int conf_arg;             /* collection a supprimer (conf_kind 2) */

/* Efface la liste memorisee : il faut ensuite lancer "Scanner la bibliotheque".
 * Les choix de l'utilisateur (images, categories, jeux masques, noms) sont gardes. */
static void delete_library(void) {
  free_app_textures();
  sceIoRemove(CACHE_FILE);
  sceIoRemove(ICON_CACHE_FILE);
  { /* jaquettes decodees du cache disque */
    SceUID d = sceIoDopen(CC_DIR);
    if (d >= 0) {
      SceIoDirent ent;
      char p[96];
      for (;;) {
        memset(&ent, 0, sizeof(ent));
        if (sceIoDread(d, &ent) <= 0) break;
        if (!ends_with_ci(ent.d_name, ".raw")) continue;
        snprintf(p, sizeof(p), CC_DIR "/%s", ent.d_name);
        sceIoRemove(p);
      }
      sceIoDclose(d);
    }
  }
  app_n = 0;
  demo_mode = 0;
  memset(all_apps, 0, sizeof(all_apps));
  vita_n = psp_n = hb_n = 0;
  build_lists();
  snprintf(scan_info, sizeof(scan_info), "Bibliothèque supprimée : lance « Scanner la bibliothèque ».");
}

static void draw_confirm(void) {
  float w = 440, h = 270, x = (SCREEN_W - w) / 2, y = 135;
  modal_panel(x, y, w, h, 205);
  float cx = x + w / 2;
  /* icone */
  unsigned int ic = T.acc_text;
  if (conf_kind == 4) { /* proposition du theme Noel : un sapin */
    xmas_icon(0, cx, y + 49, 46.0f, ic);
  } else if (conf_kind == 1) { /* quitter : une maison (retour a l'accueil de la Vita) */
    if (!draw_ico_file(ICO_M_HOME, cx, y + 49, 46.0f, ic)) {
      float cy = y + 49;
      for (int i = 0; i < 20; ++i) fill_rect(cx - 2 - i * 1.25f, cy - 22 + i, 4 + i * 2.5f, 1, ic); /* toit */
      fill_rect(cx - 16, cy - 2, 32, 22, ic);                                                 /* murs */
      fill_rect(cx - 5, cy + 7, 10, 13, T.card);                                               /* porte */
    }
  } else if (!draw_ico_file(ICO_DELETE, cx, y + 49, 46.0f, ic)) {
    draw_set_icon(ICO_DELETE, cx, y + 49, ic);
  }
  char l1b[96];
  const char *t1 = conf_kind == 1 ? "Retour à la LiveArea"
                   : conf_kind == 2 ? "Supprimer la collection ?" : "Supprimer la bibliothèque ?";
  const char *l1 = conf_kind == 1 ? "Tu vas quitter " VF_APP_NAME " et revenir" : "La liste mémorisée sera effacée.";
  const char *l2 = conf_kind == 1 ? "au menu principal de la PS Vita." : "Images, catégories et réglages conservés.";
  if (conf_kind == 2) {
    char nm[48];
    fit_text(ST_REG, FS_BODY, conf_arg >= 0 && conf_arg < MAX_COLL ? coll_name[conf_arg] : "", 300, nm, sizeof(nm));
    snprintf(l1b, sizeof(l1b), L("« %s » sera supprimée."), nm);
    l1 = l1b;
    l2 = "Les jeux restent dans leur système.";
  } else if (conf_kind == 4) {
    t1 = "Passer au thème Noël ?";
    l1 = "Neige, houx et guirlande pour les fêtes.";
    l2 = "Ton thème actuel reviendra après le 6 janvier.";
  } else if (conf_kind == 3) { /* retirer un systeme ajoute (START sur sa tuile) */
    char nm[48];
    fit_text(ST_REG, FS_BODY, conf_arg >= 0 && conf_arg < MAX_CUSTOM_SYS ? slot_name[conf_arg] : "", 300, nm, sizeof(nm));
    t1 = "Retirer le système ?";
    snprintf(l1b, sizeof(l1b), L("« %s » sera retiré."), nm);
    l1 = l1b;
    l2 = "Ses jeux retournent dans leur catégorie.";
  }
  draw_text_center(ST_SEMI, (int)cx, (int)y + 112, FS_SECTION, T.text, t1);
  draw_text_center(ST_REG, (int)cx, (int)y + 146, FS_BODY, T.text_soft, l1);
  draw_text_center(ST_REG, (int)cx, (int)y + 170, FS_BODY, T.text_soft, l2);
  float bw = (w - 40 - 14) / 2, by = y + h - 62;
  round_card(x + 20, by, bw, 42, 21, with_alpha(T.bg, 120), T.card_border, 1.0f);
  draw_btn(x + 20 + 26, by + 21, 'O', T.text);
  draw_text_center(ST_SEMI, (int)(x + 20 + bw / 2 + 12), (int)by + 27, FS_BODY, T.text, "Annuler");
  primary_button(x + 20 + bw + 14, by, bw, 42, 1.0f);
  draw_btn(x + 20 + bw + 14 + 26, by + 21, 'X', T.btn_text);
  draw_text_center(ST_SEMI, (int)(x + 20 + bw + 14 + bw / 2 + 12), (int)by + 27, FS_BODY, T.btn_text, "Confirmer");
}

static void draw_hidden(void) {
  float x = 140, y = 56, w = 680, h = 420;
  modal_panel(x, y, w, h, 215);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 44, FS_SECTION, T.text, "Jeux masqués");
  char buf[96];
  if (hid_n == 0) {
    draw_text(ST_REG, (int)x + 28, (int)y + 130, FS_BODY, T.muted, "Aucun jeu masqué.");
    draw_text(ST_REG, (int)x + 28, (int)y + 158, FS_SMALL, T.muted,
              "Masque un jeu avec SELECT > Masquer ce jeu.");
  } else {
    snprintf(buf, sizeof(buf), L(hid_n > 1 ? "%d jeux masqués" : "%d jeu masqué"), hid_n);
    draw_text(ST_REG, (int)x + 28, (int)y + 72, FS_BODY, T.muted, buf);
  }
  int first = hid_sel > 6 ? hid_sel - 6 : 0;
  for (int k = 0; k < 7 && first + k < hid_n; ++k) {
    int i = first + k;
    float ry = y + 92 + k * 42;
    int sel = i == hid_sel;
    modal_row(x + 20, ry, w - 40, 38, sel);
    int kw = text_w(ST_REG, FS_BODY, kind_name(hid_list[i]->kind));
    fit_text(sel ? ST_SEMI : ST_REG, FS_BODY, hid_list[i]->name, (int)w - 40 - 40 - kw - 20,
             buf, sizeof(buf));
    draw_text(sel ? ST_SEMI : ST_REG, (int)x + 40, (int)ry + 26, FS_BODY, T.text, buf);
    draw_text_right(ST_REG, (int)(x + w - 40), (int)ry + 26, FS_BODY, T.muted,
                    kind_name(hid_list[i]->kind));
  }
  draw_hints((int)x + 28, (int)(y + h - 22), FS_SMALL, T.muted, "X Réafficher;S Tout réafficher;O Fermer");
}

/* ---- Logo anime de l'ecran de demarrage : le A a facettes d'Area Play ----
 * Coordonnees du dessin d'origine (boite 30..98 x 20..104, centre 64,62), mises a l'echelle a l'ecran.
 * Les aretes s'allument du bas vers la pointe, puis les facettes se remplissent, puis le titre arrive. */
#define SPLASH_END 1.2f /* secondes : animation terminee (l'ecran attend au plus jusque-la) */
static unsigned long long splash_t0; /* debut de l'animation (premier affichage) */
static float splash_anim_t(void) {
  unsigned long long now = sceKernelGetProcessTimeWide();
  if (!splash_t0) splash_t0 = now;
  return (float)(now - splash_t0) / 1000000.0f;
}
static float anim_k(float t, float t0, float dur) { /* 0 -> 1 entre t0 et t0 + dur, depart doux */
  float k = (t - t0) / dur;
  if (k <= 0.0f) return 0.0f;
  if (k >= 1.0f) return 1.0f;
  return 1.0f - (1.0f - k) * (1.0f - k);
}
#define LOGO_S 1.8f
#define LX(x) (SCREEN_W / 2.0f + ((x) - 64.0f) * LOGO_S)
#define LY(y) (166.0f + ((y) - 62.0f) * LOGO_S)
static void logo_tri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int c) {
  vita2d_color_vertex *v = vtx_alloc(3);
  if (!v) return;
  vtx_set(&v[0], LX(x0), LY(y0), c);
  vtx_set(&v[1], LX(x1), LY(y1), c);
  vtx_set(&v[2], LX(x2), LY(y2), c);
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, 3);
}
/* Arete tracee de (x0,y0) vers (x1,y1) sur la fraction p : trait epais a bouts ronds. */
static void logo_edge(float x0, float y0, float x1, float y1, float p, unsigned int c) {
  if (p <= 0.0f) return;
  float ax = LX(x0), ay = LY(y0);
  float bx = ax + (LX(x1) - ax) * p, by = ay + (LY(y1) - ay) * p;
  float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
  const float hw = 1.8f; /* demi-epaisseur */
  if (l > 0.01f) {
    float nx = -dy / l * hw, ny = dx / l * hw;
    vita2d_color_vertex *v = vtx_alloc(4);
    if (v) {
      vtx_set(&v[0], ax + nx, ay + ny, c);
      vtx_set(&v[1], ax - nx, ay - ny, c);
      vtx_set(&v[2], bx + nx, by + ny, c);
      vtx_set(&v[3], bx - nx, by - ny, c);
      vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
    }
  }
  vita2d_draw_fill_circle(ax, ay, hw, c);
  vita2d_draw_fill_circle(bx, by, hw, c);
}
static void splash_logo(float t, float a) {
  /* facettes (sous les aretes) */
  float f1 = anim_k(t, 0.75f, 0.25f), f2 = anim_k(t, 0.82f, 0.25f), f3 = anim_k(t, 0.89f, 0.25f);
  if (f1 > 0) logo_tri(64, 20, 64, 64, 30, 104, fade(C(58, 139, 255, 255), f1 * a));
  if (f2 > 0) logo_tri(64, 20, 98, 104, 64, 64, fade(C(95, 224, 255, 255), f2 * a));
  if (f3 > 0) {
    unsigned int c = fade(C(29, 95, 208, 255), f3 * a);
    logo_tri(30, 104, 64, 64, 64, 86, c);
    logo_tri(30, 104, 64, 86, 48, 104, c);
    logo_tri(64, 64, 98, 104, 64, 86, c);
    logo_tri(64, 86, 98, 104, 80, 104, c);
  }
  /* aretes lumineuses : la base, puis tout monte vers la pointe, l'arete du milieu en dernier */
  unsigned int e = fade(C(232, 251, 255, 255), a);
  float kb = anim_k(t, 0.0f, 0.18f);
  logo_edge(30, 104, 48, 104, kb, e);
  logo_edge(98, 104, 80, 104, kb, e);
  float kn = anim_k(t, 0.18f, 0.25f);
  logo_edge(48, 104, 64, 86, kn, e);
  logo_edge(80, 104, 64, 86, kn, e);
  float kv = anim_k(t, 0.18f, 0.40f);
  logo_edge(30, 104, 64, 64, kv, e);
  logo_edge(98, 104, 64, 64, kv, e);
  float ko = anim_k(t, 0.18f, 0.55f);
  logo_edge(30, 104, 64, 20, ko, e);
  logo_edge(98, 104, 64, 20, ko, e);
  logo_edge(64, 64, 64, 20, anim_k(t, 0.55f, 0.25f), e);
}

/* Reglage "Fond d'ecran demarrage" : l'image du LiveArea (pic0.png, 960 x 544) derriere le logo.
 * Lue au premier affichage, liberee quand l'ecran de demarrage a disparu (splash_bg_free). */
static vita2d_texture *splash_bg_tex;
static int splash_bg_tried;
static int splash_bg_draw(float a) {
  if (!opt_splash_bg) return 0;
  if (!splash_bg_tex && !splash_bg_tried) {
    splash_bg_tried = 1;
    splash_bg_tex = vita2d_load_PNG_file("app0:sce_sys/pic0.png");
    if (!splash_bg_tex) log_line("Fond de demarrage : app0:sce_sys/pic0.png illisible");
  }
  if (!splash_bg_tex) return 0;
  float sx = SCREEN_W / (float)vita2d_texture_get_width(splash_bg_tex);
  float sy = 544.0f / (float)vita2d_texture_get_height(splash_bg_tex);
  vita2d_draw_texture_tint_scale(splash_bg_tex, 0, 0, sx, sy, fade(C(255, 255, 255, 255), a));
  return 1;
}
static void splash_bg_free(void) {
  if (!splash_bg_tex) return;
  vita2d_wait_rendering_done();
  vita2d_free_texture(splash_bg_tex);
  splash_bg_tex = NULL;
  splash_bg_tried = 0; /* rechargee si l'ecran revient (nouvelle lecture de la bibliotheque) */
}

/* Contenu de l'ecran de demarrage (fond, logo anime, titre, barre). a : opacite (fondu vers le menu). */
static void splash_content(const char *msg, float a, float t) {
  static float shown_prog; /* la barre glisse vers la progression reelle au lieu de sauter */
  if (splash_prog < 0.0f) shown_prog = 0.0f;
  else shown_prog += (splash_prog - shown_prog) * 0.18f;
  if (!splash_bg_draw(a)) draw_bg_layer(a);
  float cx = SCREEN_W / 2.0f;
  float at = splash_anim_t();
  splash_logo(at, a);
  /* titre et edition : apres le trace des aretes */
  float ta = anim_k(at, 0.80f, 0.30f), te = anim_k(at, 0.90f, 0.30f);
  char la[32], lb[32], title[64];
  app_logo(la, lb, sizeof(la));
  snprintf(title, sizeof(title), lb[0] ? "%s %s" : "%s%s", la, lb);
  if (ta > 0) {
    int bx = spaced(ST_SEMI, 0, 0, FS_HERO, 0, title, 8, 0) - 8;
    spaced(ST_SEMI, (int)(cx - bx / 2), 296, FS_HERO, fade(T.text, ta * a), title, 8, 1);
  }
  if (te > 0) {
    int ew = spaced(ST_REG, 0, 0, FS_SMALL, 0, VF_EDITION, 4, 0) - 4;
    spaced(ST_REG, (int)(cx - ew / 2), 324, FS_SMALL, fade(T.muted, te * a), VF_EDITION, 4, 1);
  }
  if (msg[0]) draw_text_center(ST_REG, (int)cx, 392, FS_BODY, fade(T.muted, a), msg);

  /* barre (visible des le debut) : progression reelle, sinon bande qui glisse */
  float bw = 340, bxp = cx - bw / 2, by = 352;
  round_rect(bxp, by, bw, 8, 4, fade(T.card_border, a));
  if (splash_prog >= 0.0f) {
    float fw = bw * (shown_prog > 1.0f ? 1.0f : shown_prog);
    if (fw > 8) round_rect(bxp, by, fw, 8, 4, fade(T.acc, a));
  } else {
    float ph = fmodf(t * 0.9f, 1.0f), seg = 110;
    float x0 = bxp + (bw - seg) * (0.5f - 0.5f * cosf(ph * 6.2831853f));
    round_rect(x0, by, seg, 8, 4, fade(T.acc, a));
  }
}

static void draw_splash(const char *msg) { /* msg NULL : garde le dernier message */
  static unsigned long long last;
  static char last_msg[96];
  if (msg) snprintf(last_msg, sizeof(last_msg), "%s", msg);
  msg = last_msg;
  unsigned long long now = sceKernelGetProcessTimeWide();
  if (last && now - last < 15000ULL) return; /* 60 images par seconde au plus */
  last = now;
  vita2d_start_drawing();
  vita2d_clear_screen();
  splash_content(msg, 1.0f, (float)(now % 100000000ULL) / 1000000.0f);
  vita2d_end_drawing();
  vita2d_swap_buffers();
}

