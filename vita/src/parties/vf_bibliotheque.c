/* Area Play - Cache, scan, listes, lancement des jeux. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ---- Cache : evite de relire toutes les applications a chaque demarrage ---- */

typedef struct {
  char id[16];
  char title[72];
  char icon_path[80];
  char bg_path[80];
  char src_path[160];
  unsigned color;
  int kind;
  int has_bg;
  unsigned icon_off, icon_size, bg_off, bg_size;
  int rom; /* ROM (RetroArch) : emplacement du systeme + 1 */
} CacheRec;
static CacheRec cache_recs[MAX_TOTAL];

/* Fond d'un jeu : 1) choix de l'utilisateur (cle "bg:<ID>"), 2) fond de l'application,
 * 3) fond telecharge backgrounds/<ID>.png. */
static void game_bg_apply(Game *g, const CacheRec *r) {
  snprintf(g->bg_path, sizeof(g->bg_path), "%s", r->bg_path);
  g->has_bg = r->has_bg;
  g->bg_size = r->bg_size;
  if (!g->has_bg) { /* fond telecharge (PSP / PS1 / ROM) */
    char bn[24];
    snprintf(bn, sizeof(bn), "%s.png", r->id);
    if (nameset_has(&bg_set, bn)) {
      snprintf(g->bg_path, sizeof(g->bg_path), BG_DIR "/%s", bn);
      g->has_bg = 1;
    }
  }
  char key[24];
  snprintf(key, sizeof(key), "bg:%s", r->id);
  const char *c = custom_get(key);
  if (c) {
    choice_path(c, g->bg_path, sizeof(g->bg_path));
    g->has_bg = 1;
    g->bg_size = 0; /* jeu PSP en fichier : l'image choisie remplace celle du fichier */
  }
}

/* Le fond choisi a change : l'ancien est libere, le nouveau sera charge a l'affichage. */
static void game_bg_refresh(Game *g) {
  int i = (int)(g - all_apps);
  if (i < 0 || i >= app_n) return;
  vita2d_wait_rendering_done();
  if (g->bg) vita2d_free_texture(g->bg);
  g->bg = NULL;
  game_bg_apply(g, &cache_recs[i]);
}

/* Ajoute une application a la bibliotheque (noms, categories et masquage
 * personnalises sont appliques ici, donc pris en compte sans nouveau scan). */
static void add_app(const CacheRec *r) {
  if (app_n >= MAX_TOTAL) return;
  cache_recs[app_n] = *r;
  Game *g = &all_apps[app_n++];
  memset(g, 0, sizeof(*g));
  const char *ov = NULL;
  for (int k = 0; k < n_name_ovs; ++k)
    if (strcmp(name_ovs[k].id, r->id) == 0) {
      ov = name_ovs[k].name;
      break;
    }
  snprintf(g->title, sizeof(g->title), "%s", ov ? ov : r->title);
  snprintf(g->id, sizeof(g->id), "%s", r->id);
  g->name = g->title;
  g->detail = g->id;
  { /* titre (et nom d'origine s'il a ete renomme) + identifiant simplifies, pour la recherche */
    char both[180];
    snprintf(both, sizeof(both), "%s %s %s", g->title, ov ? r->title : "", g->id);
    text_fold(both, g->fold, sizeof(g->fold));
  }
  snprintf(g->icon_path, sizeof(g->icon_path), "%s", r->icon_path);
  snprintf(g->bg_path, sizeof(g->bg_path), "%s", r->bg_path);
  snprintf(g->src_path, sizeof(g->src_path), "%s", r->src_path);
  g->icon_off = r->icon_off;
  g->icon_size = r->icon_size;
  g->bg_off = r->bg_off;
  g->bg_size = r->bg_size;
  g->color = r->color;
  g->rom = r->rom;
  if (g->rom) { /* ROM : le nom du fichier remplace l'identifiant dans les details */
    const char *base = strrchr(g->src_path, '/');
    g->detail = base ? base + 1 : g->src_path;
  }
  game_bg_apply(g, r);

  g->last_played = recents_get(r->id);
  char key[24];
  snprintf(key, sizeof(key), "kind:%s", r->id);
  const char *ko = custom_get(key);
  int kind = ko ? atoi(ko) : r->kind;
  if (kind < 0 || kind > 2 + MAX_CUSTOM_SYS) kind = r->kind;
  if (kind >= KIND_CUSTOM0 && !SLOT_USED(kind - KIND_CUSTOM0)) kind = r->kind;
  g->kind = kind;
  snprintf(key, sizeof(key), "fav:%s", r->id);
  g->fav = custom_get(key) != NULL;
  snprintf(key, sizeof(key), "hide:%s", r->id);
  g->hidden = custom_get(key) != NULL;
  g->coll = coll_mask_of(r->id);
  { /* aucune image chargee ici : jaquette (fichier) ou icone (icons.bin) arrivent quand le jeu s'affiche */
    char cp[140];
    int has_file = cover_file_for(g, cp, sizeof(cp));
    if (r->rom && !r->icon_size) {
      g->icon_idx = -1; /* ROM : pas d'icone, seulement une jaquette (fichier ou telechargee) */
    } else if (lazy_covers) {
      g->icon_idx = iconc_find(g->id, app_n - 1);
      if (g->icon_idx < 0 && !has_file) ++missing_icons;
    } else {
      load_icon(g); /* scan : lit l'icone de l'application et la range dans icons.bin */
    }
    g->cover_pending = (has_file || g->icon_idx >= 0) && !g->cover;
  }
  if (splash_on) {
    char m[96];
    if (splash_total > 0) {
      splash_prog = (float)app_n / (float)splash_total;
      snprintf(m, sizeof(m), "%d / %d", app_n, splash_total);
    } else {
      snprintf(m, sizeof(m), "%s %d", splash_msg, app_n);
    }
    draw_splash(m);
  }
}

/* Nom d'origine d'un jeu (titre lu, ou nom du fichier) : sert aux jaquettes cherchees par nom,
 * meme quand l'utilisateur a renomme le jeu. */
static const char *game_orig_title(const Game *g) {
  int i = (int)(g - all_apps);
  return i >= 0 && i < app_n ? cache_recs[i].title : g->title;
}

/* Noms personnalises : names.txt ("ID|Nom", aussi modifiable a la main). */
static void names_save(void) {
  sceIoMkdir(DATA_DIR, 0777);
  SceUID f = sceIoOpen(NAMES_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  for (int k = 0; k < n_name_ovs; ++k) {
    char l[96];
    int n = snprintf(l, sizeof(l), "%s|%s\r\n", name_ovs[k].id, name_ovs[k].name);
    if (n > 0) sceIoWrite(f, l, n < (int)sizeof(l) ? n : (int)sizeof(l) - 1);
  }
  sceIoClose(f);
}

/* Renomme un jeu (SELECT > Renommer). Nom vide : retour au nom d'origine. */
static void game_rename(Game *g, const char *raw) {
  char nm[64];
  while (*raw == ' ') ++raw;
  snprintf(nm, sizeof(nm), "%s", raw);
  size_t l = strlen(nm);
  while (l > 0 && nm[l - 1] == ' ') nm[--l] = 0;
  int k = 0;
  while (k < n_name_ovs && strcmp(name_ovs[k].id, g->id) != 0) ++k;
  const char *orig = game_orig_title(g);
  if (!nm[0] || strcmp(nm, orig) == 0) { /* nom d'origine : la ligne disparait */
    if (k < n_name_ovs) name_ovs[k] = name_ovs[--n_name_ovs];
  } else {
    if (k == n_name_ovs) {
      if (n_name_ovs >= NAME_OV_MAX) return;
      ++n_name_ovs;
    }
    snprintf(name_ovs[k].id, sizeof(name_ovs[k].id), "%s", g->id);
    snprintf(name_ovs[k].name, sizeof(name_ovs[k].name), "%s", nm);
  }
  names_save();
  snprintf(g->title, sizeof(g->title), "%s", nm[0] ? nm : orig);
  g->name = g->title;
  char both[180];
  snprintf(both, sizeof(both), "%s %s %s", g->title, strcmp(g->title, orig) ? orig : "", g->id);
  text_fold(both, g->fold, sizeof(g->fold));
  build_lists(); /* l'ordre alphabetique change */
}

#define CACHE_MAGIC 0x384C4656u /* "VFL8" (5.4 : jeux PS1 reconnus, bibliotheque relue une fois) */

static void cache_save(void) {
  sceIoMkdir(DATA_DIR, 0777);
  SceUID f = sceIoOpen(CACHE_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  unsigned hdr[2] = {CACHE_MAGIC, ((unsigned)sizeof(CacheRec) << 16) | (unsigned)app_n};
  sceIoWrite(f, hdr, sizeof(hdr));
  if (app_n > 0) sceIoWrite(f, cache_recs, app_n * (int)sizeof(CacheRec));
  sceIoClose(f);
}

/* Retourne 1 si la bibliotheque a ete chargee depuis le cache. */
static int cache_load(void) {
  SceUID f = sceIoOpen(CACHE_FILE, SCE_O_RDONLY, 0);
  if (f < 0) return 0;
  unsigned hdr[2];
  int ok = 0;
  if (sceIoRead(f, hdr, sizeof(hdr)) == (int)sizeof(hdr) && hdr[0] == CACHE_MAGIC &&
      (hdr[1] >> 16) == sizeof(CacheRec)) {
    unsigned cnt = hdr[1] & 0xFFFF;
    splash_total = (int)cnt;
    int want = (int)(cnt * sizeof(CacheRec));
    if (cnt > 0 && cnt <= MAX_TOTAL && sceIoRead(f, cache_recs, want) == want) {
      for (unsigned i = 0; i < cnt; ++i) {
        CacheRec r = cache_recs[i];
        r.id[sizeof(r.id) - 1] = 0;
        r.title[sizeof(r.title) - 1] = 0;
        r.icon_path[sizeof(r.icon_path) - 1] = 0;
        r.bg_path[sizeof(r.bg_path) - 1] = 0;
        r.src_path[sizeof(r.src_path) - 1] = 0;
        add_app(&r);
      }
      ok = 1;
    }
  }
  sceIoClose(f);
  return ok;
}

/* ---- Jeux PSP : ux0:pspemu/PSP/GAME (EBOOT.PBP) et ux0:pspemu/ISO (.iso / .cso) ---- */

static unsigned char iso_buf[65536];

static int file_read_at(SceUID f, unsigned off, void *buf, int len) {
  if (sceIoLseek(f, (SceOff)off, SCE_SEEK_SET) < 0) return -1;
  return sceIoRead(f, buf, len);
}

static unsigned hash_str(const char *s) {
  unsigned h = 5381;
  for (; *s; ++s) h = h * 33 + (unsigned char)*s;
  return h;
}

/* Remplit l'identifiant et le titre d'un jeu PSP a partir de son PARAM.SFO. */
static void psp_fill(CacheRec *r, int sfo_len, const char *path) {
  char title[72] = "", disc[16] = "";
  if (sfo_len > 0) {
    sfo_string(sfo_buf, sfo_len, "TITLE", title, sizeof(title));
    sfo_string(sfo_buf, sfo_len, "DISC_ID", disc, sizeof(disc));
  }
  char id[16];
  int o = 0;
  for (const char *p = disc; *p && o < 12; ++p)
    if (isalnum((unsigned char)*p)) id[o++] = *p;
  id[o] = 0;
  if (o < 4) snprintf(id, sizeof(id), "PSP%08X", hash_str(path));
  snprintf(r->id, sizeof(r->id), "%s", id);
  sanitize_title(title, r->title, sizeof(r->title), r->id);
}

/* Titre de secours : nom du fichier sans extension. */
static void psp_title_from_name(CacheRec *r, const char *name) {
  char t[72];
  snprintf(t, sizeof(t), "%s", name);
  char *dot = strrchr(t, '.');
  if (dot) *dot = 0;
  for (char *q = t; *q; ++q)
    if (*q == '_') *q = ' ';
  sanitize_title(t, r->title, sizeof(r->title), r->id);
}

static void psp_finish(CacheRec *r, const char *path) {
  snprintf(r->src_path, sizeof(r->src_path), "%s", path);
  r->kind = KIND_PSP;
  r->color = accents[hash_str(r->id) % GAME_COLOR_N].color;
  r->has_bg = r->bg_size > 0;
}

static int parse_pbp(const char *path, CacheRec *r) {
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return 0;
  int ok = 0;
  unsigned char h[0x28];
  if (sceIoRead(f, h, sizeof(h)) == (int)sizeof(h) && h[0] == 0 && h[1] == 'P' &&
      h[2] == 'B' && h[3] == 'P') {
    unsigned sfo = rd32(h + 8), icon = rd32(h + 12), icon1 = rd32(h + 16);
    unsigned pic0 = rd32(h + 20), pic1 = rd32(h + 24);
    if (icon > sfo && icon - sfo <= sizeof(sfo_buf)) {
      int len = file_read_at(f, sfo, sfo_buf, (int)(icon - sfo));
      if (len > 0) {
        psp_fill(r, len, path);
        r->icon_off = icon;
        r->icon_size = icon1 > icon ? icon1 - icon : 0;
        r->bg_off = pic0;
        r->bg_size = pic1 > pic0 ? pic1 - pic0 : 0;
        ok = 1;
      }
    }
  }
  sceIoClose(f);
  if (ok) psp_finish(r, path);
  return ok;
}

/* Cherche un fichier dans un dossier ISO9660 (sans tenir compte de la casse ni de ";1"). */
static int iso_find(DiscFile *f, unsigned dir_lba, unsigned dir_len, const char *name,
                    unsigned *lba, unsigned *len) {
  if (dir_len > sizeof(iso_buf)) dir_len = sizeof(iso_buf);
  int got = disc_read(f, dir_lba * 2048u, iso_buf, (int)dir_len);
  if (got <= 0) return 0;
  size_t nl = strlen(name);
  unsigned pos = 0;
  while (pos + 34 < (unsigned)got) {
    unsigned rl = iso_buf[pos];
    if (rl == 0) {
      pos = (pos / 2048 + 1) * 2048;
      continue;
    }
    if (pos + rl > (unsigned)got) break;
    unsigned fl = iso_buf[pos + 32];
    const char *fn = (const char *)iso_buf + pos + 33;
    unsigned cmp = fl;
    for (unsigned k = 0; k < fl; ++k)
      if (fn[k] == ';') {
        cmp = k;
        break;
      }
    if (cmp == nl && strncasecmp(fn, name, nl) == 0) {
      *lba = rd32(iso_buf + pos + 2);
      *len = rd32(iso_buf + pos + 10);
      return 1;
    }
    pos += rl;
  }
  return 0;
}

/* ISO ou CSO : titre, identifiant, et position de ICON0 / PIC0 dans l'image disque. */
static int parse_iso(const char *path, CacheRec *r) {
  DiscFile f;
  if (!disc_open(&f, path)) return 0;
  int ok = 0;
  unsigned char pvd[2048];
  if (disc_read(&f, 16 * 2048u, pvd, 2048) == 2048 && pvd[0] == 1 && memcmp(pvd + 1, "CD001", 5) == 0) {
    unsigned root_lba = rd32(pvd + 158), root_len = rd32(pvd + 166);
    unsigned g_lba, g_len, l, n;
    if (iso_find(&f, root_lba, root_len, "PSP_GAME", &g_lba, &g_len)) {
      if (iso_find(&f, g_lba, g_len, "PARAM.SFO", &l, &n) && n > 0 && n <= sizeof(sfo_buf)) {
        int len = disc_read(&f, l * 2048u, sfo_buf, (int)n);
        if (len > 0) {
          psp_fill(r, len, path);
          ok = 1;
        }
      }
      if (ok) {
        if (iso_find(&f, g_lba, g_len, "ICON0.PNG", &l, &n)) {
          r->icon_off = l * 2048u;
          r->icon_size = n;
        }
        if (iso_find(&f, g_lba, g_len, "PIC0.PNG", &l, &n)) {
          r->bg_off = l * 2048u;
          r->bg_size = n;
        }
      }
    }
  }
  disc_close(&f);
  if (ok) psp_finish(r, path);
  return ok;
}

static int title_key(const char *s, char *out, int n) {
  int o = 0;
  for (; *s && o < n - 1; ++s)
    if (isalnum((unsigned char)*s)) out[o++] = (char)tolower((unsigned char)*s);
  out[o] = 0;
  return o;
}

/* Si une bulle (application) porte le meme titre que ce jeu PSP, c'est la bulle
 * de ce jeu : on la classe en PSP et on ignore le fichier. Retourne 1 si fusionne. */
static int merge_with_bubble(const CacheRec *r) {
  char a[72], b[72];
  if (title_key(r->title, a, sizeof(a)) < 3) return 0;
  for (int i = 0; i < app_n; ++i) {
    if (all_apps[i].src_path[0] || all_apps[i].kind == KIND_VITA) continue;
    title_key(cache_recs[i].title, b, sizeof(b));
    if (strcmp(a, b) != 0) continue;
    char key[24];
    snprintf(key, sizeof(key), "kind:%s", all_apps[i].id);
    if (!custom_get(key)) { /* sauf si l'utilisateur a choisi la categorie lui-meme */
      all_apps[i].kind = KIND_PSP;
      cache_recs[i].kind = KIND_PSP;
    }
    return 1;
  }
  return 0;
}

/* ---- Jeux PSP en ISO / CSO, lances par la bulle "RetroFlow Adrenaline Launcher" (facultatif) ----
 * La bulle (ux0:app/RETROLNCR, installee avec RetroFlow) contient data/boot.bin : le module AdrBubbleBooter
 * d'Adrenaline y lit le jeu a demarrer. Sans cette bulle ou sans le module, les ISO ne sont pas listes. */
static int adr_ready(void) {
  return file_exists("ux0:app/" ADR_LAUNCHER_ID "/eboot.bin") && file_exists(ADR_BOOTER);
}

static int is_disc_name(const char *nm) {
  return ends_with_ci(nm, ".iso") || ends_with_ci(nm, ".cso") || ends_with_ci(nm, ".ciso");
}

/* Dossier ISO : images .iso / .cso (titre et icone lus dans l'image, sinon nom du fichier).
 * depth 0 = dossier ISO lui-meme, 1 = un sous-dossier (rangement par categories). */
static void scan_iso_dir(const char *dir, int depth) {
  SceUID d = sceIoDopen(dir);
  if (d < 0) return;
  SceIoDirent ent;
  for (;;) {
    memset(&ent, 0, sizeof(ent));
    if (sceIoDread(d, &ent) <= 0) break;
    const char *nm = ent.d_name;
    if (nm[0] == '.') continue;
    char path[192];
    if (snprintf(path, sizeof(path), "%s/%s", dir, nm) >= (int)sizeof(((CacheRec *)0)->src_path)) continue;
    if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
      if (depth == 0) scan_iso_dir(path, 1);
      continue;
    }
    if (!is_disc_name(nm)) continue;
    CacheRec r;
    memset(&r, 0, sizeof(r));
    if (!parse_iso(path, &r)) { /* image illisible : on garde le nom du fichier */
      memset(&r, 0, sizeof(r));
      snprintf(r.id, sizeof(r.id), "PSP%08X", hash_str(path));
      psp_title_from_name(&r, nm);
      psp_finish(&r, path);
    }
    if (app_n >= MAX_TOTAL) {
      ++apps_dropped;
      continue;
    }
    if (id_known(r.id) || merge_with_bubble(&r)) continue; /* deja la, ou le jeu a sa propre bulle */
    add_app(&r);
  }
  sceIoDclose(d);
}

/* <partition>:pspemu/ISO sur chaque partition ou Adrenaline peut ranger les jeux. */
static void scan_psp_iso(void) {
  static const char *parts[] = {"ux0:", "ur0:", "uma0:", "imc0:", "xmc0:"};
  for (int p = 0; p < LEN(parts); ++p) {
    char dir[32];
    snprintf(dir, sizeof(dir), "%spspemu/ISO", parts[p]);
    scan_iso_dir(dir, 0);
  }
}

/* Prepare boot.bin de la bulle de lancement pour ce jeu. 0 si pret, -1 sinon (err rempli).
 * boot.bin (320 octets) : "ABB\0", pilote (0x04), fichier execute (0x08), personnalise (0x0C),
 * bouton PS (0x14), marqueur "BBD1" (0x2C), chemin du jeu en minuscules (0x40, 256 octets). */
static int adr_prepare(const Game *g, char *err, int errsz) {
  if (!file_exists("ux0:app/" ADR_LAUNCHER_ID "/eboot.bin")) {
    snprintf(err, errsz, "Bulle « RetroFlow Adrenaline Launcher » absente (voir README)");
    return -1;
  }
  if (!file_exists(ADR_BOOTER)) {
    snprintf(err, errsz, "Module AdrBubbleBooter absent (voir README)");
    return -1;
  }
  unsigned char b[320];
  memset(b, 0, sizeof(b));
  memcpy(b, "ABB", 4);
  b[0x04] = 1; /* pilote INFERNO (lit les ISO et les CSO) */
  b[0x0C] = 1; /* reglages propres a la bulle */
  memcpy(b + 0x2C, "BBD1", 4);
  int o = 0;
  for (const char *c = g->src_path; *c && o < 255; ++c) b[0x40 + o++] = (unsigned char)tolower((unsigned char)*c);
  sceIoMkdir("ux0:app/" ADR_LAUNCHER_ID "/data", 0777);
  SceUID f = sceIoOpen("ux0:app/" ADR_LAUNCHER_ID "/data/boot.bin", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  int w = f >= 0 ? sceIoWrite(f, b, sizeof(b)) : -1;
  if (f >= 0) sceIoClose(f);
  if (w != (int)sizeof(b)) {
    snprintf(err, errsz, L("Écriture de boot.bin impossible (0x%08X)"), (unsigned)(f < 0 ? f : w));
    return -1;
  }
  return 0;
}

/* ---- Systemes emules : ROM lancees par RetroArch (ux0:app/RETROVITA) ----
 * custom.txt : rcore<k> = coeur (ex. snes9x2005), rdir<k> = dossier des jeux ("-" = aucun).
 * Sans dossier choisi : ux0:roms/<nom du systeme simplifie> s'il existe (ex. ux0:roms/snes). */
#define RA_ID "RETROVITA"
#define E4V_ID "EMU4VPLUS" /* Emu4Vita++ : coeurs ux0:app/EMU4VPLUS/eboot_<coeur>.self */
#define ROMS_ROOT "ux0:roms"

/* Emulateurs autonomes (une application par console) : lances par psgm:play?titleid=<ID>&<arg>=<jeu>. */
static const struct {
  const char *id, *name, *arg;
} std_emus[] = {{"DEDALOX64", "DaedalusX64", "param"}, {"FLYCASTDC", "Flycast", "param"},
                {"DSVITA000", "DSVita", "param"},      {"AZAHARPSV", "Azahar", "param"},
                {"EASYRPG01", "EasyRPG", "project-path"}};

/* Coeur choisi : "snes9x2005" = RetroArch, "e4v:snes9x2005" = Emu4Vita++, "app:DEDALOX64" = emulateur autonome. */
static int core_is_e4v(const char *code) { return strncmp(code, "e4v:", 4) == 0; }
static int core_is_app(const char *code) { return strncmp(code, "app:", 4) == 0; }
static const char *core_name(const char *code) { return core_is_e4v(code) || core_is_app(code) ? code + 4 : code; }
static int std_emu_of(const char *code) {
  if (!core_is_app(code)) return -1;
  for (int i = 0; i < LEN(std_emus); ++i)
    if (strcmp(std_emus[i].id, code + 4) == 0) return i;
  return -1;
}
/* Nom affiche : "snes9x2005 (RetroArch)", "DaedalusX64"... */
static void core_label(const char *code, char *out, int n) {
  int e = std_emu_of(code);
  if (e >= 0) snprintf(out, n, "%s", std_emus[e].name);
  else snprintf(out, n, "%s (%s)", core_name(code), core_is_e4v(code) ? "Emu4Vita++" : "RetroArch");
}

/* Cree ux0:roms avec LISEZMOI.txt (une fois) : un sous-dossier par systeme, cree par l'utilisateur. */
static void roms_dir_init(void) {
  sceIoMkdir("ux0:roms", 0777);
  const char *rd = "ux0:roms/LISEZMOI.txt";
  SceUID f = sceIoOpen(rd, SCE_O_RDONLY, 0);
  if (f >= 0) {
    sceIoClose(f);
    return;
  }
  f = sceIoOpen(rd, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  static const char txt[] =
      VF_APP_NAME " - jeux retro (RetroArch, Emu4Vita++, DaedalusX64, Flycast, DSVita, Azahar, EasyRPG)\r\n\r\n"
      "1. Dans " VF_APP_NAME " : Jeux > Ajouter un systeme (ex. SNES).\r\n"
      "2. Cree ici un dossier au nom du systeme, en minuscules, sans espaces ni accents :\r\n"
      "   SNES -> ux0:roms/snes    Mega Drive -> ux0:roms/megadrive    Nintendo 64 -> ux0:roms/nintendo64\r\n"
      "3. Copie tes jeux dedans (un niveau de sous-dossiers est lu). Garde les noms d'origine,\r\n"
      "   ex. \"Super Mario World (USA).sfc\" : les jaquettes sont trouvees par ce nom.\r\n"
      "4. Reglages > Bibliotheque > Emulateurs : choisis le coeur (ou l'emulateur) du systeme.\r\n"
      "   Un autre dossier peut y etre choisi (sous-dossier de ux0:roms ou uma0:roms...).\r\n"
      "5. Reglages > Bibliotheque > Telecharger jaquettes et fonds.\r\n\r\n"
      "EasyRPG : un sous-dossier par jeu (le dossier du jeu RPG Maker).\r\n"
      "Seuls les dossiers des systemes que tu utilises sont utiles : inutile d'en creer d'autres.\r\n";
  sceIoWrite(f, txt, sizeof(txt) - 1);
  sceIoClose(f);
}

static const char *rom_core_of(int k) {
  char key[16];
  snprintf(key, sizeof(key), "rcore%d", k);
  const char *c = custom_get(key);
  return c && c[0] ? c : NULL;
}

/* Dossier des jeux du systeme k. Retourne 0 s'il n'y en a pas ; *is_auto = 1 si choisi d'apres le nom. */
static int rom_dir_of(int k, char *out, int n, int *is_auto) {
  char key[16];
  snprintf(key, sizeof(key), "rdir%d", k);
  const char *d = custom_get(key);
  if (is_auto) *is_auto = 0;
  if (d && strcmp(d, "-") == 0) return 0;
  if (d && d[0]) {
    snprintf(out, n, "%s", d);
    return 1;
  }
  if (!slot_file[k][0]) return 0;
  snprintf(out, n, ROMS_ROOT "/%s", slot_file[k]);
  SceIoStat st;
  if (sceIoGetstat(out, &st) < 0 || !SCE_S_ISDIR(st.st_mode)) return 0;
  if (is_auto) *is_auto = 1;
  return 1;
}

/* Fichiers a ignorer dans un dossier de jeux : sauvegardes, images, textes, correctifs... */
static int rom_ignored(const char *nm) {
  static const char *skip[] = {".srm", ".sav", ".state", ".png", ".jpg", ".jpeg", ".txt", ".xml", ".cfg", ".nfo",
                               ".dat", ".ips", ".bps", ".ups", ".rtc", ".lpl", ".db", ".ini", ".pdf", ".opt"};
  if (nm[0] == '.') return 1;
  for (int i = 0; i < LEN(skip); ++i)
    if (ends_with_ci(nm, skip[i])) return 1;
  for (const char *p = nm; *p; ++p) /* sauvegardes d'etat RetroArch : .state1, .state2... */
    if (strncasecmp(p, ".state", 6) == 0) return 1;
  return 0;
}

/* Titre affiche : nom du fichier sans extension ni mentions entre () ou [] (region, version). */
static void rom_title(const char *nm, char *out, int n) {
  char t[128];
  snprintf(t, sizeof(t), "%s", nm);
  char *dot = strrchr(t, '.');
  if (dot && dot != t) *dot = 0;
  for (char *q = t; *q; ++q)
    if (*q == '_') *q = ' ';
  char *cut = NULL;
  for (char *q = t + 1; *q; ++q)
    if ((*q == '(' || *q == '[') && q[-1] == ' ') {
      cut = q - 1;
      break;
    }
  if (cut) *cut = 0;
  snprintf(out, n, "%s", t);
}

static int pbp_is_ps1(const char *path);
/* depth 0 = dossier du systeme, 1 = un sous-dossier. Un dossier avec des .cue (ou .m3u) :
 * les pistes .bin / .img (ou les .cue de chaque disque) ne sont pas listees en double. */
static void scan_rom_dir(int k, const char *dir, int depth) {
  SceIoDirent ent;
  int has_cue = 0, has_m3u = 0, has_gdi = 0;
  const char *code = rom_core_of(k);
  int easyrpg = code && strcmp(code, "app:EASYRPG01") == 0; /* EasyRPG : chaque sous-dossier est un jeu */
  SceUID d = sceIoDopen(dir);
  if (d < 0) return;
  for (;;) {
    memset(&ent, 0, sizeof(ent));
    if (sceIoDread(d, &ent) <= 0) break;
    if (ends_with_ci(ent.d_name, ".cue")) has_cue = 1;
    if (ends_with_ci(ent.d_name, ".m3u")) has_m3u = 1;
    if (ends_with_ci(ent.d_name, ".gdi")) has_gdi = 1;
  }
  sceIoDclose(d);
  d = sceIoDopen(dir);
  if (d < 0) return;
  for (;;) {
    memset(&ent, 0, sizeof(ent));
    if (sceIoDread(d, &ent) <= 0) break;
    const char *nm = ent.d_name;
    if (rom_ignored(nm)) continue;
    char path[192];
    if (snprintf(path, sizeof(path), "%s/%s", dir, nm) >= (int)sizeof(((CacheRec *)0)->src_path)) continue;
    if (easyrpg) { /* le dossier du jeu est le "fichier" a lancer */
      if (!SCE_S_ISDIR(ent.d_stat.st_mode) || depth > 0 || app_n >= MAX_TOTAL) continue;
      CacheRec r;
      memset(&r, 0, sizeof(r));
      snprintf(r.id, sizeof(r.id), "R%08X", hash_str(path));
      if (id_known(r.id)) continue;
      sanitize_title(nm, r.title, sizeof(r.title), r.id);
      snprintf(r.src_path, sizeof(r.src_path), "%s", path);
      r.kind = KIND_CUSTOM0 + k;
      r.rom = k + 1;
      r.color = slot_col[k];
      add_app(&r);
      continue;
    }
    if (SCE_S_ISDIR(ent.d_stat.st_mode)) {
      if (depth == 0) scan_rom_dir(k, path, 1);
      continue;
    }
    if (has_gdi && (ends_with_ci(nm, ".bin") || ends_with_ci(nm, ".raw"))) continue; /* pistes Dreamcast */
    if (strcasecmp(nm, "EBOOT.PBP") == 0) { /* jeu PS1 en EBOOT (pspemu/PSP/GAME) : sans bulle, par l'emulateur */
      CacheRec r;
      memset(&r, 0, sizeof(r));
      const char *folder = strrchr(dir, '/');
      folder = folder ? folder + 1 : dir;
      if (app_n >= MAX_TOTAL || id_known(folder) || !pbp_is_ps1(path) || !parse_pbp(path, &r) || id_known(r.id))
        continue; /* jeu PSP, homebrew, ou jeu PS1 qui a deja sa bulle */
      r.kind = KIND_CUSTOM0 + k;
      r.rom = k + 1;
      add_app(&r);
      continue;
    }
    if (strstr(dir, "pspemu/PSP/GAME") || strstr(dir, "pspemu/psp/game")) continue; /* la : seulement les EBOOT */
    if (has_cue && (ends_with_ci(nm, ".bin") || ends_with_ci(nm, ".img") || ends_with_ci(nm, ".wav"))) continue;
    if (has_m3u && (ends_with_ci(nm, ".cue") || ends_with_ci(nm, ".chd"))) continue;
    if (app_n >= MAX_TOTAL) {
      ++apps_dropped;
      continue;
    }
    CacheRec r;
    memset(&r, 0, sizeof(r));
    snprintf(r.id, sizeof(r.id), "R%08X", hash_str(path));
    if (id_known(r.id)) continue;
    char t[128];
    rom_title(nm, t, sizeof(t));
    sanitize_title(t, r.title, sizeof(r.title), r.id);
    snprintf(r.src_path, sizeof(r.src_path), "%s", path);
    r.kind = KIND_CUSTOM0 + k;
    r.rom = k + 1;
    r.color = slot_col[k];
    add_app(&r);
  }
  sceIoDclose(d);
}

static void scan_roms(void) {
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k) {
    char dir[160];
    if (SLOT_USED(k) && rom_core_of(k) && rom_dir_of(k, dir, sizeof(dir), NULL)) /* sans coeur : rien a lancer */
      scan_rom_dir(k, dir, 0);
  }
}

/* Adresse de lancement d'une ROM : RetroArch, avec le coeur et le fichier du jeu. 0 = ok. */
static int rom_uri(const Game *g, char *uri, int un, char *err, int errsz) {
  int k = g->rom - 1;
  if (k < 0 || k >= MAX_CUSTOM_SYS || !SLOT_USED(k)) {
    snprintf(err, errsz, "Système retiré : relance le scan");
    return -1;
  }
  const char *code = rom_core_of(k);
  if (!code) {
    snprintf(err, errsz, "Choisis un cœur : Réglages > Bibliothèque");
    return -1;
  }
  SceIoStat gst;
  if (sceIoGetstat(g->src_path, &gst) < 0) { /* fichier, ou dossier (EasyRPG) */
    snprintf(err, errsz, "Jeu introuvable");
    return -1;
  }
  int se = std_emu_of(code);
  if (se >= 0) { /* emulateur autonome */
    char ep[48];
    snprintf(ep, sizeof(ep), "ux0:app/%s/eboot.bin", std_emus[se].id);
    if (!file_exists(ep)) {
      snprintf(err, errsz, L("%s n'est pas installé"), std_emus[se].name);
      return -1;
    }
    snprintf(uri, un, "psgm:play?titleid=%s&%s=%s", std_emus[se].id, std_emus[se].arg, g->src_path);
    return 0;
  }
  int e4v = core_is_e4v(code);
  const char *core = core_name(code);
  if (!file_exists(e4v ? "ux0:app/" E4V_ID "/eboot.bin" : "ux0:app/" RA_ID "/eboot.bin")) {
    snprintf(err, errsz, e4v ? "Emu4Vita++ n'est pas installé" : "RetroArch n'est pas installé");
    return -1;
  }
  char cp[96];
  if (e4v) snprintf(cp, sizeof(cp), "ux0:app/" E4V_ID "/eboot_%s.self", core);
  else snprintf(cp, sizeof(cp), "ux0:app/" RA_ID "/%s_libretro.self", core);
  if (!file_exists(cp)) {
    snprintf(err, errsz, L("Cœur absent : %s"), core);
    return -1;
  }
  if (e4v) { /* Emu4Vita++ (0.71 et plus) : core=<coeur>&rom=<chemin encode> */
    int o = snprintf(uri, un, "psgm:play?titleid=" E4V_ID "&core=%s&rom=", core);
    for (const unsigned char *s = (const unsigned char *)g->src_path; *s && o < un - 4; ++s)
      if (isalnum(*s) || strchr("-_.~:/", *s)) uri[o++] = (char)*s;
      else o += snprintf(uri + o, un - o, "%%%02X", *s);
    uri[o] = 0;
  } else {
    snprintf(uri, un, "psgm:play?titleid=" RA_ID "&param=app0:/%s_libretro.self&param2=%s", core, g->src_path);
  }
  return 0;
}
/* ---- Jeux PS1 installes en bulle ----
 * La bulle d'un jeu PS1 ressemble a celle d'un jeu PSP (meme emulateur de la Vita). On regarde donc
 * le jeu lui-meme : <partition>:pspemu/PSP/GAME/<identifiant de la bulle>/EBOOT.PBP, dont le PARAM.SFO
 * a la categorie "ME" pour un jeu PS1. Bulles d'Adrenaline Bubbles Manager (PSPEMUxxx) : le chemin du
 * jeu est dans data/boot.bin. Les jeux PS1 vont dans le systeme "PS1" (cree au premier jeu trouve). */
static int pbp_is_ps1(const char *path) {
  static unsigned char sb[4096];
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return 0;
  unsigned char h[0x28];
  int ps1 = 0;
  if (sceIoRead(f, h, sizeof(h)) == (int)sizeof(h) && h[1] == 'P' && h[2] == 'B' && h[3] == 'P') {
    unsigned sfo = rd32(h + 8), icon = rd32(h + 12);
    int len = icon > sfo ? (int)(icon - sfo) : 0;
    if (len > (int)sizeof(sb)) len = (int)sizeof(sb);
    if (len > 0 && sceIoLseek(f, (SceOff)sfo, SCE_SEEK_SET) >= 0 && (len = sceIoRead(f, sb, len)) > 0) {
      char cat[8];
      ps1 = sfo_string(sb, len, "CATEGORY", cat, sizeof(cat)) && strcmp(cat, "ME") == 0;
    }
  }
  sceIoClose(f);
  return ps1;
}

static int app_is_ps1(const char *root, const char *id) {
  static const char *serial[] = {"SLUS", "SCUS", "SLES", "SCES", "SCED", "SLPS", "SLPM", "SCPS", "SCPM", "SIPS",
                                 "SLKA", "SCKA", "PAPX", "PCPX", "SLED", "SLUD"};
  for (int i = 0; i < LEN(serial); ++i) /* identifiant d'un disque PS1 */
    if (strncmp(id, serial[i], 4) == 0) return 1;
  static const char *parts[] = {"ux0:", "uma0:", "imc0:", "xmc0:", "ur0:"};
  char p[192];
  for (int i = 0; i < LEN(parts); ++i) {
    snprintf(p, sizeof(p), "%spspemu/PSP/GAME/%s/EBOOT.PBP", parts[i], id);
    if (pbp_is_ps1(p)) return 1;
  }
  unsigned char b[0x140];
  snprintf(p, sizeof(p), "%s/%s/data/boot.bin", root, id);
  int n = read_file(p, b, sizeof(b));
  if (n >= 0x60) {
    int off = memcmp(b, "ABB", 3) == 0 && n >= 0x140 ? 0x40 : 0x20; /* format recent / ancien */
    char gp[160];
    snprintf(gp, sizeof(gp), "%.*s", (int)sizeof(gp) - 1, (const char *)b + off);
    if (ends_with_ci(gp, ".pbp") && pbp_is_ps1(gp)) return 1;
  }
  return 0;
}

/* Categorie des jeux PS1 : un systeme ajoute dont le nom evoque la PS1 (PS1, PSX, PSOne, PlayStation),
 * sinon un systeme "PS1" cree une fois (s'il est retire ensuite, les jeux PS1 restent dans PSP). */
static int ps1_kind(void) {
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k) {
    if (!SLOT_USED(k)) continue;
    char u[48];
    int o = 0;
    for (const char *c = slot_name[k]; *c && o < 46; ++c)
      if (*c != ' ' && *c != '-') u[o++] = (*c >= 'a' && *c <= 'z') ? *c - 32 : *c;
    u[o] = 0;
    if (strcmp(u, "PS1") == 0 || strcmp(u, "PSX") == 0 || strcmp(u, "PSONE") == 0 || strcmp(u, "PLAYSTATION") == 0 ||
        strcmp(u, "PLAYSTATION1") == 0 || strcmp(u, "SONYPLAYSTATION") == 0)
      return KIND_CUSTOM0 + k;
  }
  if (custom_get("ps1made")) return KIND_PSP;
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k) {
    if (SLOT_USED(k)) continue;
    char key[16];
    slot_fill(k, "PS1", -1);
    snprintf(key, sizeof(key), "csys%d", k);
    custom_set(key, "n:PS1");
    custom_set("ps1made", "1");
    custom_save();
    slot_icon[k] = NULL;
    rebuild_systems();
    for (int i = 2; i < n_systems; ++i)
      if (sys_slot[i] == k) load_system_icon(&systems[i]);
    return KIND_CUSTOM0 + k;
  }
  return KIND_PSP;
}
/* ---- Applications installees (ux0:app, uma0:app...) ---- */

static void scan_root(const char *root, int primary) {
  SceUID d = sceIoDopen(root);
  if (d < 0) {
    if (primary) scan_error = d; /* ex. acces refuse en mode "safe" */
    return;
  }
  SceIoDirent ent;
  for (;;) {
    memset(&ent, 0, sizeof(ent));
    if (sceIoDread(d, &ent) <= 0) break;
    if (!SCE_S_ISDIR(ent.d_stat.st_mode)) continue;
    const char *id = ent.d_name;
    size_t il = strlen(id);
    if (il < 4 || il > 12) continue;
    if (own_id[0] && strcmp(id, own_id) == 0) continue; /* pas Area Play lui-meme */
    if (strcmp(id, ADR_LAUNCHER_ID) == 0) continue;     /* bulle de lancement des ISO, pas un jeu */
    if (strncmp(id, "NPXS", 4) == 0) continue;          /* applications systeme */
    if (id_known(id)) continue;
    if (app_n >= MAX_TOTAL) {
      ++apps_dropped;
      continue;
    }

    char path[112];
    snprintf(path, sizeof(path), "%s/%s/sce_sys/param.sfo", root, id);
    int len = read_file(path, sfo_buf, sizeof(sfo_buf));
    if (len <= 0) continue;
    char title[72], cat[16];
    if (!sfo_string(sfo_buf, len, "TITLE", title, sizeof(title))) title[0] = 0;
    sfo_string(sfo_buf, len, "CATEGORY", cat, sizeof(cat));
    if (cat[0] && strcmp(cat, "gd") != 0) continue; /* applications seulement */

    CacheRec r;
    memset(&r, 0, sizeof(r));
    snprintf(r.id, sizeof(r.id), "%s", id);
    if (strlen(title) <= 1) { /* titre suspect : on garde les octets bruts dans le journal */
      char dbg[200];
      int dn = snprintf(dbg, sizeof(dbg), "titre suspect : %s | texte='%s' | octets=", id, title);
      for (int q = 0; q < 12 && dn < (int)sizeof(dbg) - 4; ++q) dn += snprintf(dbg + dn, sizeof(dbg) - dn, "%02X", sfo_dbg[q]);
      log_line(dbg);
    }
    if (strlen(title) <= 1) { /* repli : param.sfo copie par la LiveArea */
      char am[64];
      snprintf(am, sizeof(am), "ur0:appmeta/%s/param.sfo", id);
      int l2 = read_file(am, sfo_buf, sizeof(sfo_buf));
      char t2[72];
      if (l2 > 0 && sfo_string(sfo_buf, l2, "TITLE", t2, sizeof(t2)) && strlen(t2) > strlen(title))
        snprintf(title, sizeof(title), "%s", t2);
    }
    if (strlen(title) <= 1 && strncmp(id, "PSPEMU", 6) == 0) { /* nom reel : base de la LiveArea */
      char t3[72];
      if (appdb_title(id, t3, sizeof(t3))) snprintf(title, sizeof(title), "%s", t3);
    }
    sanitize_title(title, r.title, sizeof(r.title), id);
    snprintf(r.icon_path, sizeof(r.icon_path), "%s/%s/sce_sys/icon0.png", root, id);
    snprintf(r.bg_path, sizeof(r.bg_path), "%s/%s/sce_sys/pic0.png", root, id);
    if (!image_valid(r.icon_path)) { /* absente ou chiffree : copie de la LiveArea */
      char alt[80];
      snprintf(alt, sizeof(alt), "ur0:appmeta/%s/icon0.png", id);
      if (image_valid(alt)) snprintf(r.icon_path, sizeof(r.icon_path), "%s", alt);
    }
    r.has_bg = 0;
    {
      char lb[80];
      char ld[110];
      snprintf(ld, sizeof(ld), "%s/%s/sce_sys/livearea/contents/", root, id);
      int found = find_livearea_bg(ld, lb, sizeof(lb));
      if (!found) { /* jeux du commerce : copie lisible de la LiveArea */
        snprintf(ld, sizeof(ld), "ur0:appmeta/%s/livearea/contents/", id);
        found = find_livearea_bg(ld, lb, sizeof(lb));
      }
      if (!found) {
        snprintf(ld, sizeof(ld), "ur0:appmeta/%s/sce_sys/livearea/contents/", id);
        found = find_livearea_bg(ld, lb, sizeof(lb));
      }
      if (found) { /* fond de la LiveArea, plus leger que pic0 */
        snprintf(r.bg_path, sizeof(r.bg_path), "%s", lb);
        r.has_bg = 1;
      }
    }
    if (!r.has_bg) r.has_bg = image_valid(r.bg_path);
    if (!r.has_bg) {
      char alt[80];
      snprintf(alt, sizeof(alt), "ur0:appmeta/%s/pic0.png", id);
      if (image_valid(alt)) {
        snprintf(r.bg_path, sizeof(r.bg_path), "%s", alt);
        r.has_bg = 1;
      }
    }
    if (!r.has_bg && strncmp(id, "PSPEMU", 6) != 0) { /* aide au diagnostic : que contient appmeta ? */
      static int dbg_left = 4;
      if (dbg_left > 0) {
        --dbg_left;
        const char *sub[2] = {"", "/livearea/contents"};
        for (int q = 0; q < 2; ++q) {
          char dp[96], line[300];
          snprintf(dp, sizeof(dp), "ur0:appmeta/%s%s", id, sub[q]);
          int ln = snprintf(line, sizeof(line), "fond absent : %s contient :", dp);
          SceUID dd = sceIoDopen(dp);
          if (dd < 0) ln += snprintf(line + ln, sizeof(line) - ln, " (illisible %08X)", (unsigned)dd);
          else {
            SceIoDirent de;
            memset(&de, 0, sizeof(de));
            while (sceIoDread(dd, &de) > 0 && ln < (int)sizeof(line) - 40) {
              ln += snprintf(line + ln, sizeof(line) - ln, " %s", de.d_name);
              memset(&de, 0, sizeof(de));
            }
            sceIoDclose(dd);
          }
          log_line(line);
        }
      }
    }
    r.color = accents[hash_str(id) % GAME_COLOR_N].color;
    r.kind = classify_id(id);
    if (r.kind != KIND_VITA && app_is_ps1(root, id)) r.kind = ps1_kind(); /* jeu PS1 : systeme PS1 */
    add_app(&r);
  }
  sceIoDclose(d);
}

static void scan_apps(void) {
  own_id[0] = 0;
  int n = read_file("app0:sce_sys/param.sfo", sfo_buf, sizeof(sfo_buf));
  if (n > 0) sfo_string(sfo_buf, n, "TITLE_ID", own_id, sizeof(own_id));
  scan_error = 0;
  scan_root("ux0:app", 1);
  scan_root("uma0:app", 0);
  scan_root("imc0:app", 0);
  scan_root("xmc0:app", 0);
  if (adr_ready()) scan_psp_iso(); /* ISO / CSO : seulement si la bulle de lancement et AdrBubbleBooter sont la */
  scan_roms();
  appdb_free();
}

static void use_demo(void) {
  demo_mode = 1;
  vita_n = psp_n = hb_n = 0;
  for (int i = 0; i < LEN(demo_vita); ++i) {
    if (demo_vita[i].cover_path)
      demo_vita[i].cover = vita2d_load_JPEG_file(demo_vita[i].cover_path);
    tex_prep(demo_vita[i].cover);
    demo_vita[i].appear = 1.0f;
    vita_ptrs[vita_n++] = &demo_vita[i];
  }
  for (int i = 0; i < LEN(demo_homebrew); ++i) {
    demo_homebrew[i].kind = KIND_HB;
    demo_homebrew[i].appear = 1.0f;
    hb_ptrs[hb_n++] = &demo_homebrew[i];
  }
}

static int in_home(Game *g) {
  for (int i = 0; i < home_n; ++i)
    if (home_ptrs[i] == g) return 1;
  return 0;
}

/* Accueil : les jeux lances le plus recemment, completes par les autres. */
static void build_home(void) {
  static Game *all[MAX_APPS * 3];
  int n = 0;
  for (int i = 0; i < vita_n; ++i) all[n++] = vita_ptrs[i];
  for (int i = 0; i < psp_n; ++i) all[n++] = psp_ptrs[i];
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k) /* homebrews exclus : l'accueil ne montre que des jeux */
    for (int i = 0; i < custom_n[k] && n < MAX_APPS * 3; ++i) all[n++] = custom_ptrs[k][i];
  qsort(all, n, sizeof(Game *), cmp_recent);
  home_n = 0;
  for (int i = 0; i < n && home_n < 4 && all[i]->last_played > 0; ++i)
    home_ptrs[home_n++] = all[i];
  for (int i = 0; i < vita_n && home_n < 4; ++i)
    if (!in_home(vita_ptrs[i])) home_ptrs[home_n++] = vita_ptrs[i];
  for (int i = 0; i < psp_n && home_n < 4; ++i)
    if (!in_home(psp_ptrs[i])) home_ptrs[home_n++] = psp_ptrs[i];

  for (int k = 0; k < MAX_CUSTOM_SYS; ++k)
    for (int i = 0; i < custom_n[k] && home_n < 4; ++i)
      if (!in_home(custom_ptrs[k][i])) home_ptrs[home_n++] = custom_ptrs[k][i];
}

/* Repartit les applications entre PS Vita / PSP / Homebrews (masquees exclues). */
/* Recherche : jeux (non masques) dont le titre ou l'identifiant contient tous les mots tapes,
 * sans tenir compte des majuscules ni des accents. Resultats tries comme les autres listes. */
static void search_run(const char *q) {
  char fq[64], *words[8], qq[sizeof(search_q)];
  int nw = 0;
  snprintf(qq, sizeof(qq), "%s", q); /* q peut etre search_q lui-meme */
  memcpy(search_q, qq, sizeof(search_q));
  text_fold(qq, fq, sizeof(fq));
  for (char *w = strtok(fq, " "); w && nw < 8; w = strtok(NULL, " ")) words[nw++] = w;
  search_n = 0;
  if (nw == 0 || demo_mode) return;
  for (int i = 0; i < app_n && search_n < MAX_APPS; ++i) {
    Game *g = &all_apps[i];
    if (g->hidden) continue;
    int ok = 1;
    for (int k = 0; k < nw && ok; ++k)
      if (!strstr(g->fold, words[k])) ok = 0;
    if (ok) search_ptrs[search_n++] = g;
  }
  qsort(search_ptrs, search_n, sizeof(Game *), cmp_title);
}

static void build_lists(void) {
  for (int k = 0; k < MAX_COLL; ++k) coll_cnt[k] = 0;
  if (!demo_mode) {
    vita_n = psp_n = hb_n = 0;
    for (int k = 0; k < MAX_CUSTOM_SYS; ++k) custom_n[k] = 0;
    for (int i = 0; i < app_n; ++i) {
      Game *g = &all_apps[i];
      if (g->hidden) continue;
      if (g->kind == KIND_VITA && vita_n < MAX_APPS) vita_ptrs[vita_n++] = g;
      else if (g->kind == KIND_PSP && psp_n < MAX_APPS) psp_ptrs[psp_n++] = g;
      else if (g->kind == KIND_HB && hb_n < MAX_APPS) hb_ptrs[hb_n++] = g;
      else if (g->kind >= KIND_CUSTOM0 && g->kind < KIND_CUSTOM0 + MAX_CUSTOM_SYS) {
        int k = g->kind - KIND_CUSTOM0;
        if (SLOT_USED(k) && custom_n[k] < MAX_APPS) custom_ptrs[k][custom_n[k]++] = g;
      }
    }
    qsort(vita_ptrs, vita_n, sizeof(Game *), cmp_title);
    qsort(psp_ptrs, psp_n, sizeof(Game *), cmp_title);
    qsort(hb_ptrs, hb_n, sizeof(Game *), cmp_title);
    for (int k = 0; k < MAX_CUSTOM_SYS; ++k) qsort(custom_ptrs[k], custom_n[k], sizeof(Game *), cmp_title);
    fav_n = 0;
    for (int i = 0; i < app_n && fav_n < MAX_APPS; ++i)
      if (all_apps[i].fav && !all_apps[i].hidden) fav_ptrs[fav_n++] = &all_apps[i];
    qsort(fav_ptrs, fav_n, sizeof(Game *), cmp_title);
    /* Collections : un jeu reste dans son systeme et apparait aussi dans chacune de ses collections. */
    for (int i = 0; i < app_n; ++i) {
      Game *g = &all_apps[i];
      if (g->hidden || !g->coll) continue;
      for (int k = 0; k < MAX_COLL; ++k)
        if ((g->coll & COLL_BIT(k)) && COLL_USED(k) && coll_cnt[k] < MAX_APPS) coll_ptrs[k][coll_cnt[k]++] = g;
    }
    for (int k = 0; k < MAX_COLL; ++k) qsort(coll_ptrs[k], coll_cnt[k], sizeof(Game *), cmp_title);
  }
  systems[0].count = vita_n;
  systems[1].count = psp_n;
  for (int i = 2; i < n_systems; ++i) systems[i].count = custom_n[sys_slot[i]];
  build_home();
  search_run(search_q); /* resultats a jour (jeu masque, renomme, tri...) */
}

/* force_scan = 0 : on utilise le cache s'il existe (demarrage normal).
 * force_scan = 1 : on relit tout (option "Scanner la bibliotheque"). */
static void build_library(int force_scan) {
  recents_load();
  names_load();
  nameset_load(&cover_set, COVER_DIR);
  nameset_load(&bg_set, BG_DIR);
  app_n = 0;
  scan_error = 0;
  missing_icons = 0;
  apps_dropped = 0;
  int from_cache = 0;
  int cache_exists = file_exists(CACHE_FILE) && file_exists(ICON_CACHE_FILE);
  splash_msg = (!force_scan && cache_exists) ? "Chargement..." : "Lecture de la bibliothèque...";
  splash_on = 1;
  splash_total = 0;
  splash_prog = -1.0f;
  vita2d_set_vblank_wait(0); /* pas d'attente d'ecran pendant le chargement */
  draw_splash(splash_msg);
  if (!force_scan && cache_exists) {
    /* Demarrage normal : aucune lecture des applications ni de leurs images,
     * seulement les fichiers memorises par le dernier scan. */
    iconc_index(); /* index seulement : les pixels sont lus a l'affichage */
    lazy_covers = 1;
    from_cache = cache_load();
    lazy_covers = 0;
  }
  if (!from_cache) {
    app_n = 0;
    memset(all_apps, 0, sizeof(all_apps));
    missing_icons = 0;
    sceIoMkdir(DATA_DIR, 0777);
    SceUID lf = sceIoOpen(LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (lf >= 0) sceIoClose(lf); /* le journal est recree a chaque scan */
    iconc_n = 0;
    iconc_wpos = 0;
    iconc_wfd = sceIoOpen(ICON_CACHE_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    scan_apps();
    if (iconc_wfd >= 0) sceIoClose(iconc_wfd);
    iconc_wfd = -1;
    if (app_n > 0) cache_save();
  }
  splash_on = 0;
  vita2d_set_vblank_wait(1);
  if (app_n == 0) use_demo();
  build_lists();
  log_mem("bibliotheque chargee");

  char miss[96] = "";
  int ml = 0;
  if (missing_icons > 0)
    ml = snprintf(miss, sizeof(miss), L(missing_icons > 1 ? "%d icônes manquantes." : "%d icône manquante."), missing_icons);
  if (apps_dropped > 0) { /* bibliotheque pleine : on le dit au lieu d'ignorer en silence */
    snprintf(miss + ml, sizeof(miss) - ml, L(apps_dropped > 1 ? " %d app. ignorées (max %d)." : " %d app. ignorée (max %d)."),
             apps_dropped, MAX_TOTAL);
    snprintf(toast_text, sizeof(toast_text),
             L(apps_dropped > 1 ? "Bibliothèque pleine : %d applications ignorées" : "Bibliothèque pleine : %d application ignorée"),
             apps_dropped);
    toast_time = 5.0f;
  }
  if (scan_error < 0 && app_n == 0)
    snprintf(scan_info, sizeof(scan_info),
             "Lecture de ux0:app refusée (0x%08X) : mode unsafe requis.", (unsigned)scan_error);
  else if (demo_mode)
    snprintf(scan_info, sizeof(scan_info),
             "Aucune application trouvée : affichage de jeux de démonstration.");
  else
    snprintf(scan_info, sizeof(scan_info), L(hb_n > 1 ? "%s : %d Vita, %d PSP, %d homebrews.%s" : "%s : %d Vita, %d PSP, %d homebrew.%s"),
             from_cache ? "Cache" : "Scan", vita_n, psp_n, hb_n, miss);
}

static void free_app_textures(void) {
  tex_free_flush();
  vita2d_wait_rendering_done();
  for (int i = 0; i < app_n; ++i) {
    Game *g = &all_apps[i];
    if (g->cover) vita2d_free_texture(g->cover);
    if (g->bg) vita2d_free_texture(g->bg);
    if (g->blur) vita2d_free_texture(g->blur);
    g->cover = NULL;
    g->bg = NULL;
    g->blur = NULL;
  }
  bg_forget_pending();
  for (int i = 0; i < LEN(demo_vita); ++i) {
    if (demo_vita[i].cover) vita2d_free_texture(demo_vita[i].cover);
    if (demo_vita[i].blur) vita2d_free_texture(demo_vita[i].blur);
    demo_vita[i].cover = NULL;
    demo_vita[i].blur = NULL;
  }
}

/* Relit les applications installees (option "Scanner la bibliotheque"). */
static void rescan_library(void) {
  free_app_textures();
  app_n = 0;
  demo_mode = 0;
  memset(all_apps, 0, sizeof(all_apps));
  build_library(1);
}

/* Lance une application. Retourne 0 si la demande a ete acceptee. */
static int launch_game(Game *g, char *err, int errsz) {
  if (!g->id[0]) {
    snprintf(err, errsz, "Jeu de démonstration : rien à lancer");
    return -1;
  }
  char uri[320];
  if (g->rom) { /* ROM : RetroArch demarre directement le jeu avec le coeur du systeme */
    if (rom_uri(g, uri, sizeof(uri), err, errsz) < 0) return -1;
  } else if (PSP_FILE(g)) { /* jeu PSP en ISO / CSO : la bulle de lancement demarre le jeu dans Adrenaline */
    if (adr_prepare(g, err, errsz) < 0) return -1;
    snprintf(uri, sizeof(uri), "psgm:play?titleid=%s", ADR_LAUNCHER_ID);
  } else {
    snprintf(uri, sizeof(uri), "psgm:play?titleid=%s", g->id);
  }
  long long before = g->last_played;
  long long now = (long long)time(NULL);
  g->last_played = now;
  recents_set(g->id, now);
  recents_save();
  sceKernelDelayThread(10000);
  int r1 = sceAppMgrLaunchAppByUri(0xFFFFF, uri);
  sceKernelDelayThread(10000);
  int r2 = sceAppMgrLaunchAppByUri(0xFFFFF, uri);
  if (r1 < 0 && r2 < 0) { /* refuse : on annule la "derniere partie" */
    g->last_played = before;
    recents_set(g->id, before);
    recents_save();
    snprintf(err, errsz, L("Lancement refusé (0x%08X)"), (unsigned)r2);
    return -1;
  }
  return 0;
}

