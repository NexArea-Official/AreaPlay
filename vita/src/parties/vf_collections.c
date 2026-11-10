/* Area Play - Collections et fond d ecran memorise. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ---- Collections : noms dans custom.txt (coll0..coll15 = "n:Nom"), appartenance par jeu ("col:<ID>" = masque hexa) ---- */
static void coll_refresh_ids(void) {
  coll_n = 0;
  for (int k = 0; k < MAX_COLL; ++k)
    if (COLL_USED(k)) coll_ids[coll_n++] = k;
}

static void load_collections(void) {
  for (int k = 0; k < MAX_COLL; ++k) {
    char key[16];
    snprintf(key, sizeof(key), "coll%d", k);
    const char *v = custom_get(key);
    coll_name[k][0] = 0;
    if (v && v[0] == 'n' && v[1] == ':' && v[2]) snprintf(coll_name[k], sizeof(coll_name[k]), "%s", v + 2);
  }
  coll_refresh_ids();
}

static unsigned long long coll_used_mask(void) {
  unsigned long long m = 0;
  for (int i = 0; i < coll_n; ++i) m |= COLL_BIT(coll_ids[i]);
  return m;
}

static unsigned long long coll_mask_of(const char *id) {
  char key[24];
  snprintf(key, sizeof(key), "col:%s", id);
  const char *v = custom_get(key);
  return v ? strtoull(v, NULL, 16) & coll_used_mask() : 0ULL;
}

static void coll_store_mask(const char *id, unsigned long long m) {
  char key[24], val[20];
  snprintf(key, sizeof(key), "col:%s", id);
  snprintf(val, sizeof(val), "%llx", m);
  custom_set(key, m ? val : NULL);
}

/* "GTA, Monde ouvert" : collections d'un jeu. Retourne leur nombre. */
static int coll_text(const Game *g, char *out, int n) {
  int c = 0, len = 0;
  out[0] = 0;
  for (int i = 0; i < coll_n && len < n - 1; ++i) {
    int k = coll_ids[i];
    if (!(g->coll & COLL_BIT(k))) continue;
    len += snprintf(out + len, n - len, "%s%s", c ? ", " : "", coll_name[k]);
    ++c;
  }
  return c;
}

/* ---- Fond d'ecran : reference memorisee dans custom.txt ("wall") ----
 * "a:nom" = app0:assets/wallpapers, "t:nom" = app0:assets/themes, "u:nom" = ux0:data/AreaPlay/wallpapers */
static const struct {
  char tag;
  const char *dir;
} wall_dirs[4] = {{'a', "app0:assets/wallpapers"}, {'t', "app0:assets/themes"}, {'u', WALL_DIR},
                 {'x', "app0:assets/xmas"}}; /* x : fonds d'hiver du theme Noel */

static void wall_path_of(const char *ref, char *out, int n) {
  out[0] = 0;
  if (!ref || !ref[0] || ref[1] != ':' || !ref[2] || strchr(ref + 2, '/') || strstr(ref, "..")) return;
  for (int i = 0; i < 4; ++i)
    if (wall_dirs[i].tag == ref[0]) snprintf(out, n, "%s/%s", wall_dirs[i].dir, ref + 2);
}

/* ref == NULL : retour au fond du theme. */
static void wall_set(const char *ref) {
  custom_set("wall", ref && ref[0] ? ref : NULL);
  custom_save();
  wall_free();
  wall_path_of(ref, wall_path, sizeof(wall_path));
  apply_theme();
}

static void wall_load_setting(void) {
  wall_path_of(custom_get("wall"), wall_path, sizeof(wall_path));
}

static void reload_system_icon(System *sy) {
  vita2d_wait_rendering_done();
  if (sy->icon) vita2d_free_texture(sy->icon);
  sy->icon = NULL;
  load_system_icon(sy);
}

static int id_known(const char *id) {
  for (int i = 0; i < app_n; ++i)
    if (strcmp(all_apps[i].id, id) == 0) return 1;
  return 0;
}

static int file_exists(const char *path) {
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return 0;
  sceIoClose(f);
  return 1;
}

/* Noms personnalises : fichier ux0:data/AreaPlay/names.txt (une ligne "ID|Nom"). */
typedef struct {
  char id[16];
  char name[64];
} NameOv;
#define NAME_OV_MAX MAX_TOTAL
static NameOv name_ovs[NAME_OV_MAX];
static int n_name_ovs;

static void names_load(void) {
  n_name_ovs = 0;
  int n = read_file(NAMES_FILE, (unsigned char *)text_buf, sizeof(text_buf) - 1);
  if (n <= 0) return;
  text_buf[n] = 0;
  char *p = text_buf;
  if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
    p += 3; /* BOM UTF-8 des editeurs Windows */
  for (char *line = strtok(p, "\n"); line && n_name_ovs < NAME_OV_MAX; line = strtok(NULL, "\n")) {
    char *bar = strchr(line, '|');
    if (!bar) continue;
    *bar = 0;
    char *nm = bar + 1;
    size_t nl = strlen(nm);
    while (nl && (nm[nl - 1] == '\r' || nm[nl - 1] == ' ')) nm[--nl] = 0;
    size_t il = strlen(line);
    while (il && (line[il - 1] == ' ' || line[il - 1] == '\t')) line[--il] = 0;
    if (!line[0] || !nm[0]) continue;
    snprintf(name_ovs[n_name_ovs].id, sizeof(name_ovs[0].id), "%s", line);
    snprintf(name_ovs[n_name_ovs].name, sizeof(name_ovs[0].name), "%s", nm);
    ++n_name_ovs;
  }
}

/* Identifiants de jeux PSP : ULUS10041, UCES01234, NPUZ00001... */
static int is_psp_id(const char *id) {
  if (strlen(id) != 9) return 0;
  int prefix = (id[0] == 'U' && (id[1] == 'C' || id[1] == 'L')) || (id[0] == 'N' && id[1] == 'P');
  if (!prefix || !isupper((unsigned char)id[2]) || !isupper((unsigned char)id[3])) return 0;
  for (int i = 4; i < 9; ++i)
    if (!isdigit((unsigned char)id[i])) return 0;
  return 1;
}

static int classify_id(const char *id) {
  if (strncmp(id, "PCS", 3) == 0) return KIND_VITA;
  /* Bulles PSP creees pour Adrenaline : PSPEMU010, PSPEMU011... (pas PSPEMUCFW = Adrenaline) */
  if (strncmp(id, "PSPEMU", 6) == 0 && strcmp(id, "PSPEMUCFW") != 0) return KIND_PSP;
  if (is_psp_id(id)) return KIND_PSP;
  return KIND_HB;
}

/* Garde les caracteres que la police sait afficher (latin + ponctuation).
 * Si rien de lisible ne reste (titre japonais, par exemple), on utilise l'ID. */
static void sanitize_title(const char *in, char *out, int outsz, const char *id) {
  int o = 0, alnum = 0;
  for (const unsigned char *p = (const unsigned char *)in; *p && o < outsz - 5;) {
    unsigned cp = 0;
    int len = 1;
    if (*p < 0x80) {
      cp = *p;
    } else if ((*p & 0xE0) == 0xC0 && p[1]) {
      cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
      len = 2;
    } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
      cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      len = 3;
    } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
      len = 4;
    }
    /* japonais : kana, kanji, ponctuation et caracteres pleine chasse (police systeme de la Vita) */
    int cjk = len == 3 && ((cp >= 0x3000 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFFEF));
    int keep = (cp >= 0x20 && cp < 0x7F) || (len == 2 && cp >= 0xA0 && cp <= 0x24F) ||
               (len == 3 && cp >= 0x2010 && cp <= 0x2122) || cjk;
    if (keep) {
      if (len == 1 && p[0] == ' ' && (o == 0 || out[o - 1] == ' ')) {
        /* pas d'espace en double ni au debut */
      } else {
        for (int k = 0; k < len; ++k) out[o++] = (char)p[k];
        if (len == 1 && isalnum(p[0])) ++alnum;
        if (len == 2 || (cjk && cp >= 0x3040)) ++alnum;
      }
    }
    p += len;
  }
  while (o > 0 && out[o - 1] == ' ') --o;
  out[o] = 0;
  if (alnum == 0) snprintf(out, outsz, "%s", id);
}

static void draw_splash(const char *msg);
static int splash_on, splash_total, lazy_covers;
static float splash_prog;
static const char *splash_msg = "";

