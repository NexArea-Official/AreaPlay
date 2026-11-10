/* Area Play - Images personnalisees, jaquettes, systemes. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Images personnalisees (jaquettes, icones de systemes)
 * ====================================================================== */

/* Choix de l'utilisateur (custom.txt) : plusieurs par jeu (image, favori, masque, categorie, collections). */
#define CUSTOM_MAX (MAX_TOTAL * 6)
typedef struct {
  char key[24];  /* identifiant d'un jeu, ou "sys:VITA" pour un systeme */
  char file[64]; /* nom du fichier dans ux0:data/AreaPlay/images        */
} Custom;
static Custom customs[CUSTOM_MAX];
static int n_customs;
/* Assez grand pour CUSTOM_MAX lignes "cle|valeur" : sinon custom.txt etait tronque sans prevenir. */
static char text_buf[CUSTOM_MAX * (24 + 64 + 2) + 64];

/* Table de hachage cle -> indice : avec des milliers de jeux, une recherche lineaire a chaque
 * consultation ralentirait nettement le demarrage. Valeur stockee : indice + 1 (0 = case vide). */
#define CUSTOM_HASH 32768 /* puissance de 2, plus du double de CUSTOM_MAX */
static int custom_hash[CUSTOM_HASH];
static unsigned custom_key_hash(const char *s) {
  unsigned h = 2166136261u;
  for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
  return h & (CUSTOM_HASH - 1);
}
static void custom_hash_add(int i) {
  unsigned h = custom_key_hash(customs[i].key);
  while (custom_hash[h]) h = (h + 1) & (CUSTOM_HASH - 1);
  custom_hash[h] = i + 1;
}
static void custom_rehash(void) {
  memset(custom_hash, 0, sizeof(custom_hash));
  for (int i = 0; i < n_customs; ++i) custom_hash_add(i);
}
static int custom_find(const char *key) {
  for (unsigned h = custom_key_hash(key); custom_hash[h]; h = (h + 1) & (CUSTOM_HASH - 1))
    if (strcmp(customs[custom_hash[h] - 1].key, key) == 0) return custom_hash[h] - 1;
  return -1;
}

static const char *custom_get(const char *key) {
  int i = custom_find(key);
  return i >= 0 ? customs[i].file : NULL;
}

/* file == NULL : supprime le choix. */
static void custom_set(const char *key, const char *file) {
  int i = custom_find(key);
  if (i >= 0) {
    if (file) snprintf(customs[i].file, sizeof(customs[i].file), "%s", file);
    else { /* la derniere entree prend sa place : les indices changent, on refait la table */
      customs[i] = customs[--n_customs];
      custom_rehash();
    }
    return;
  }
  if (!file || n_customs >= CUSTOM_MAX) return;
  snprintf(customs[n_customs].key, sizeof(customs[n_customs].key), "%s", key);
  snprintf(customs[n_customs].file, sizeof(customs[n_customs].file), "%s", file);
  custom_hash_add(n_customs);
  ++n_customs;
}

static void custom_save(void) {
  int len = 0;
  for (int i = 0; i < n_customs && len < (int)sizeof(text_buf) - 100; ++i)
    len += snprintf(text_buf + len, sizeof(text_buf) - len, "%s|%s\n",
                    customs[i].key, customs[i].file);
  if (len > (int)sizeof(text_buf) - 1) len = (int)sizeof(text_buf) - 1;
  sceIoMkdir(DATA_DIR, 0777);
  SceUID f = sceIoOpen(CUSTOM_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  if (len > 0) sceIoWrite(f, text_buf, len);
  sceIoClose(f);
}

static void custom_load(void) {
  n_customs = 0;
  custom_rehash();
  int n = read_file(CUSTOM_FILE, (unsigned char *)text_buf, sizeof(text_buf) - 1);
  if (n <= 0) return;
  text_buf[n] = 0;
  for (char *line = strtok(text_buf, "\n"); line; line = strtok(NULL, "\n")) {
    char *bar = strchr(line, '|');
    if (!bar) continue;
    *bar = 0;
    char *file = bar + 1;
    size_t fl = strlen(file);
    if (fl && file[fl - 1] == '\r') file[fl - 1] = 0;
    if (line[0] && file[0]) custom_set(line, file);
  }
}

static int ends_with_ci(const char *s, const char *suffix) {
  size_t a = strlen(s), b = strlen(suffix);
  return a >= b && strcasecmp(s + a - b, suffix) == 0;
}

/* Charge une image PNG ou JPEG (taille max 2048 px). NULL si impossible. */
static vita2d_texture *load_image_file(const char *path) {
  vita2d_texture *t = NULL;
  if (ends_with_ci(path, ".png")) t = vita2d_load_PNG_file(path);
  else if (ends_with_ci(path, ".jpg") || ends_with_ci(path, ".jpeg"))
    t = vita2d_load_JPEG_file(path);
  if (t && (vita2d_texture_get_width(t) > 2048 || vita2d_texture_get_height(t) > 2048)) {
    vita2d_free_texture(t);
    t = NULL;
  }
  return t;
}

/* Image choisie par l'utilisateur (SELECT > image) : "nom.png" = dossier images,
 * "systems/nom.png", "covers/..." ou "backgrounds/..." = sous-dossier de ux0:data/AreaPlay. */
static void choice_path(const char *c, char *out, int n) {
  if (strchr(c, '/')) snprintf(out, n, DATA_DIR "/%s", c);
  else snprintf(out, n, IMG_DIR "/%s", c);
}

/* Image d'un systeme : grande image acceptee (jusqu'a 4096 px), reduite a 512 px.
 * Un echec est note dans log.txt (fichier present mais illisible : JPEG renomme en .png, etc.). */
static vita2d_texture *load_sys_image(const char *path) {
  vita2d_texture *t = NULL;
  if (ends_with_ci(path, ".png")) t = vita2d_load_PNG_file(path);
  else if (ends_with_ci(path, ".jpg") || ends_with_ci(path, ".jpeg")) t = vita2d_load_JPEG_file(path);
  if (!t) {
    SceIoStat st;
    if (sceIoGetstat(path, &st) >= 0) {
      char l[200];
      snprintf(l, sizeof(l), "image systeme illisible : %s (%d octets)", path, (int)st.st_size);
      log_line(l);
    }
    return NULL;
  }
  return tex_shrink(t, 512, 512);
}

/* Liste des fichiers d'un dossier, lue une seule fois (evite des dizaines
 * d'ouvertures de fichiers inexistants a chaque demarrage). */
typedef struct {
  char names[800][40];
  int n;
} NameSet;
static NameSet cover_set, sys_set, bg_set;

static void nameset_load(NameSet *ns, const char *dir) {
  ns->n = 0;
  SceUID d = sceIoDopen(dir);
  if (d < 0) return;
  SceIoDirent ent;
  for (;;) {
    memset(&ent, 0, sizeof(ent));
    if (sceIoDread(d, &ent) <= 0) break;
    if (SCE_S_ISDIR(ent.d_stat.st_mode)) continue;
    if (ns->n < 800 && strlen(ent.d_name) < 40)
      snprintf(ns->names[ns->n++], 40, "%s", ent.d_name);
  }
  sceIoDclose(d);
}

static int nameset_has(const NameSet *ns, const char *name) {
  for (int i = 0; i < ns->n; ++i)
    if (strcasecmp(ns->names[i], name) == 0) return 1;
  return 0;
}

/* Ajoute un fichier qu'on vient d'ecrire (evite de relire tout le dossier). */
static void nameset_add(NameSet *ns, const char *name) {
  if (nameset_has(ns, name) || ns->n >= 800 || strlen(name) >= 40) return;
  snprintf(ns->names[ns->n++], 40, "%s", name);
}

/* Fichier de jaquette d'un jeu : 1) choix de l'utilisateur, 2) covers/<ID>.png|jpg.
 * Retourne 0 s'il n'y en a pas (l'icone de l'application sert alors de jaquette). */
static int cover_file_for(const Game *g, char *path, int n) {
  static const char *exts[3] = {".png", ".jpg", ".jpeg"};
  char fn[48];
  const char *c = custom_get(g->id);
  if (c) {
    choice_path(c, path, n);
    return 1;
  }
  for (int e = 0; e < 3; ++e) {
    snprintf(fn, sizeof(fn), "%s%s", g->id, exts[e]);
    if (!nameset_has(&cover_set, fn)) continue;
    snprintf(path, n, COVER_DIR "/%s", fn);
    return 1;
  }
  return 0;
}

/* Demande l'image d'un jeu : sa jaquette (fichier) si elle existe et se lit, sinon son icone (icons.bin). */
static void cover_submit(Game *g) {
  char path[140];
  int ok;
  if (!g->cover_failed && cover_file_for(g, path, sizeof(path))) ok = lj_submit(LJ_COVER, g, path, NULL);
  else if (g->icon_idx >= 0 && g->icon_idx < iconc_n && !g->cover) {
    const IconC *e = &iconc[g->icon_idx];
    ok = lj_submit_ex(LJ_ICON, g, NULL, NULL, e->off, e->w, e->h);
  } else {
    g->cover_pending = 0;
    return;
  }
  if (ok) {
    g->cover_pending = 2;
    g->seen = g_frame; /* pas d'eviction avant d'avoir ete affichee */
  }
}

/* Jeux dans l'ordre ou on les ouvre : accueil, PS Vita, favoris, PSP, homebrews, systemes ajoutes,
 * puis les 4 premiers de chaque collection (mosaiques). startup = seulement les premiers de chaque liste
 * (2 = l'accueil et les premiers PS Vita : ce que montre le premier ecran).
 * Retourne le n-ieme jeu dont l'image reste a charger (n = 0 : le premier), ou NULL. */
static int sys_order[2 + MAX_CUSTOM_SYS]; /* ordre des tuiles (defini plus bas) */
static Game *prefetch_nth(int startup, int n) {
  struct {
    Game **l;
    int c, lim;
  } grp[5 + MAX_CUSTOM_SYS + MAX_COLL];
  int ng = 0;
#define PF_ADD(L, C, LIM) (grp[ng].l = (L), grp[ng].c = (C), grp[ng].lim = (LIM), ++ng)
  PF_ADD(home_ptrs, home_n, 4);
  if (startup == 2) {
    PF_ADD(vita_ptrs, vita_n, 4);
  } else {
    /* les systemes dans l'ordre des tuiles de la page Jeux : le premier ouvert est le premier pret */
    for (int p = 0; p < n_systems; ++p) {
      int si = sys_order[p];
      if (si == 0) PF_ADD(vita_ptrs, vita_n, startup ? 12 : MAX_TOTAL);
      else if (si == 1) PF_ADD(psp_ptrs, psp_n, startup ? 4 : MAX_TOTAL);
      else if (custom_n[sys_slot[si]]) PF_ADD(custom_ptrs[sys_slot[si]], custom_n[sys_slot[si]], startup ? 4 : MAX_TOTAL);
    }
    PF_ADD(fav_ptrs, fav_n, startup ? 6 : MAX_TOTAL);
    for (int i = 0; i < coll_n; ++i) PF_ADD(coll_ptrs[coll_ids[i]], coll_cnt[coll_ids[i]], startup && i >= 3 ? 0 : 4);
    PF_ADD(hb_ptrs, hb_n, startup ? 4 : MAX_TOTAL);
  }
#undef PF_ADD
  for (int q = 0; q < ng; ++q)
    for (int i = 0; i < grp[q].c && i < grp[q].lim; ++i) {
      Game *g = grp[q].l[i];
      if (g->cover_pending == 1 && n-- == 0) return g;
    }
  return NULL;
}

/* A chaque image : confie au chargeur les images des jeux autour de la selection, puis celles des
 * jeux dessines a l'image precedente (mosaiques, accueil...). Les autres ne sont pas chargees.
 * Deux places restent libres dans la file pour les fonds. */
static void cover_load_now(Game *g);
static void cover_pump(Game **list, int n, int sel) {
  /* sans thread (lj_sync), chaque demande est un chargement direct : une seule par image affichee */
#define PUMP_FULL() (lj_sync || lj_busy(LJ_COVER) + lj_busy(LJ_ICON) >= LJ_N - 2)
  { /* jeux dessines a l'image precedente, sans jaquette en fichier : leur icone (icons.bin, deja
     * decodee) est lue tout de suite, ~1 ms, au lieu d'afficher un instant la couleur du jeu */
    int budget = 8;
    char p[140];
    for (int i = 0; i < app_n && budget > 0; ++i) {
      Game *g = &all_apps[i];
      if (g->cover_pending != 1 || g->cover || g->icon_idx < 0 || g_frame - g->seen > 2) continue;
      if (!g->cover_failed && cover_file_for(g, p, sizeof(p))) continue; /* jaquette : en arriere-plan */
      cover_load_now(g);
      --budget;
    }
  }
  if (PUMP_FULL() && !lj_sync) return;
  if (list && n > 0)
    for (int r = 0; r <= 10; ++r)
      for (int sg = 0; sg < 2; ++sg) {
        int i = sg ? sel + r : sel - r;
        if ((sg == 0 && r == 0) || i < 0 || i >= n || list[i]->cover_pending != 1) continue;
        cover_submit(list[i]);
        if (PUMP_FULL()) return;
      }
  for (int i = 0; i < app_n; ++i)
    if (all_apps[i].cover_pending == 1 && g_frame - all_apps[i].seen <= 2) {
      cover_submit(&all_apps[i]);
      if (PUMP_FULL()) return;
    }
  /* Rien d'urgent : on prepare a l'avance les images des listes (dans l'ordre ou on les ouvre),
   * tant qu'il reste de la place, pour qu'elles soient deja la quand on y arrive. */
  /* Au plus 2 demandes de prechargement a la fois, en priorite basse : les places restantes servent
   * aux jeux qui apparaissent a l'ecran, traites avant elles. */
  lj_next_prio = 0;
  for (int k = 0; k < 2 && !lj_sync; ++k) {
    int pre = 0;
    for (int i = 0; i < LJ_N; ++i)
      if (lj[i].state != LJ_FREE && lj[i].type != LJ_BG && lj[i].prio == 0) ++pre;
    if (pre >= 2 || g_resident + lj_busy(LJ_COVER) + lj_busy(LJ_ICON) >= TEX_BUDGET - 24) break;
    Game *g = prefetch_nth(0, 0);
    if (!g) break;
    cover_submit(g);
    if (PUMP_FULL()) break;
  }
  lj_next_prio = 1;
#undef PUMP_FULL
}

/* Charge tout de suite l'image d'un jeu (ecran de demarrage) : affichee sans fondu. */
static void cover_load_now(Game *g) {
  LoadJob j;
  memset(&j, 0, sizeof(j));
  j.g = g;
  if (!g->cover_failed && cover_file_for(g, j.path, sizeof(j.path))) j.type = LJ_COVER;
  else if (g->icon_idx >= 0 && g->icon_idx < iconc_n) {
    j.type = LJ_ICON;
    j.off = iconc[g->icon_idx].off;
    j.w = iconc[g->icon_idx].w;
    j.h = iconc[g->icon_idx].h;
  } else {
    g->cover_pending = 0;
    return;
  }
  lj_run(&j);
  if (j.tex) {
    g->cover = j.tex;
    g->cover_pending = 0;
    g->appear = 1.0f;
    g->seen = g_frame;
  } else if (j.type == LJ_COVER) { /* jaquette illisible : son icone a la place */
    g->cover_failed = 1;
    cover_load_now(g);
  } else {
    g->cover_pending = 0;
  }
}

/* L'image d'un jeu a change (choix manuel, telechargement) : la nouvelle jaquette est chargee
 * en arriere-plan (l'ancienne reste affichee en attendant) ; sans fichier, retour a l'icone. */
static void reload_cover(Game *g) {
  char path[140];
  g->cover_tried = 1;
  g->cover_failed = 0;
  if (cover_file_for(g, path, sizeof(path))) {
    g->cover_pending = 1;
    return;
  }
  tex_free_later(g->cover); /* retour a l'icone : rechargee depuis icons.bin */
  tex_free_later(g->blur);
  g->cover = NULL;
  g->blur = NULL;
  g->blur_tried = 0;
  g->cover_pending = g->icon_idx >= 0;
}

/* Icone d'un systeme : 1) choix de l'utilisateur, 2) systems/<nom>.png,
 * 3) assets/systems/<nom>.png (dans le VPK). */
static void load_system_icon(System *sy) {
  static const char *exts[3] = {".png", ".jpg", ".jpeg"};
  char key[24], path[140], fn[48];
  snprintf(key, sizeof(key), "sys:%s", sy->tag);
  const char *c = custom_get(key);
  if (c) {
    choice_path(c, path, sizeof(path));
    sy->icon = load_sys_image(path);
    if (sy->icon) return;
  }
  nameset_load(&sys_set, SYS_DIR); /* relu a chaque fois : une image ajoutee depuis le demarrage est vue */
  for (int i = 0; i < 4 && sy->files[i]; ++i) {
    for (int e = 0; e < 3; ++e) {
      snprintf(fn, sizeof(fn), "%s%s", sy->files[i], exts[e]);
      if (!nameset_has(&sys_set, fn)) continue;
      snprintf(path, sizeof(path), SYS_DIR "/%s", fn);
      sy->icon = load_sys_image(path);
      if (sy->icon) return;
    }
  }
  for (int i = 0; i < 4 && sy->files[i]; ++i) { /* icones fournies dans le VPK */
    for (int e = 0; e < 3; ++e) {
      snprintf(path, sizeof(path), "app0:assets/systems/%s%s", sy->files[i], exts[e]);
      sy->icon = load_image_file(path);
      if (sy->icon) return;
    }
  }
}

/* Ordre des tuiles sur la page Jeux : position a l'ecran -> indice dans systems[].
 * custom.txt "sysorder" : un caractere par systeme ('0' PS Vita, '1' PSP, 'A'+k / 'a'+k-26 systeme ajoute k). */
static int sys_order[2 + MAX_CUSTOM_SYS];
static int sys_move_si = -1; /* systeme dont la tuile est en cours de deplacement (-1 : aucun) */
static char sys_code(int si) {
  if (si < 2) return (char)('0' + si);
  int k = sys_slot[si];
  return (char)(k < 26 ? 'A' + k : 'a' + k - 26);
}
static void sys_order_load(void) {
  int used[2 + MAX_CUSTOM_SYS] = {0}, m = 0;
  const char *v = custom_get("sysorder");
  for (; v && *v; ++v)
    for (int si = 0; si < n_systems; ++si)
      if (!used[si] && sys_code(si) == *v) {
        used[si] = 1;
        sys_order[m++] = si;
        break;
      }
  for (int si = 0; si < n_systems; ++si) /* systemes absents de la liste (nouveaux) : a la fin */
    if (!used[si]) sys_order[m++] = si;
}
static void sys_order_save(void) {
  char buf[2 + MAX_CUSTOM_SYS + 1];
  int o = 0;
  for (int p = 0; p < n_systems; ++p) buf[o++] = sys_code(sys_order[p]);
  buf[o] = 0;
  custom_set("sysorder", buf);
  custom_save();
}
static int sys_pos(int si) { /* position a l'ecran du systeme si */
  for (int p = 0; p < n_systems; ++p)
    if (sys_order[p] == si) return p;
  return 0;
}

/* Reconstruit la liste des systemes (PS Vita, PSP, puis les systemes ajoutes). */
static void rebuild_systems(void) {
  for (int i = 2; i < n_systems; ++i) slot_icon[sys_slot[i]] = systems[i].icon;
  n_systems = 2;
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k) {
    if (!SLOT_USED(k)) continue;
    System *sy = &systems[n_systems];
    memset(sy, 0, sizeof(*sy));
    sy->name = slot_name[k];
    sy->tag = slot_tag[k];
    sy->files[0] = slot_file[k];
    sy->color = slot_col[k];
    sy->games = custom_ptrs[k];
    sy->icon = slot_icon[k];
    sys_slot[n_systems] = k;
    ++n_systems;
  }
  sys_order_load();
}

/* Remplit un emplacement a partir d'un nom saisi : etiquette en majuscules, nom de fichier simple. */
static void slot_fill(int k, const char *name, int legacy) {
  static const unsigned int pal[6] = {C(46, 110, 200, 255), C(150, 80, 60, 255), C(60, 140, 100, 255),
                                      C(160, 120, 40, 255), C(110, 90, 150, 255), C(90, 120, 130, 255)};
  snprintf(slot_name[k], sizeof(slot_name[k]), "%s", name);
  unsigned int h = 0;
  for (const char *c = name; *c; ++c) h = h * 31 + (unsigned char)*c;
  slot_col[k] = pal[h % 6];
  if (legacy >= 0 && legacy < (int)PRESET_COUNT) {
    snprintf(slot_tag[k], sizeof(slot_tag[k]), "%s", sys_presets[legacy].tag);
    snprintf(slot_file[k], sizeof(slot_file[k]), "%s", sys_presets[legacy].file);
    slot_col[k] = sys_presets[legacy].color;
    return;
  }
  int o = 0;
  for (const char *c = name; *c && o < 12; ++c) slot_tag[k][o++] = (*c >= 'a' && *c <= 'z') ? *c - 32 : *c;
  /* si on a coupe un caractere accentue en deux, on le retire en entier */
  while (o > 0 && name[o] && ((unsigned char)name[o] & 0xC0) == 0x80) --o;
  slot_tag[k][o] = 0;
  o = 0;
  for (const char *c = name; *c && o < 24; ++c) {
    if (*c >= 'A' && *c <= 'Z') slot_file[k][o++] = *c + 32;
    else if ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')) slot_file[k][o++] = *c;
  }
  slot_file[k][o] = 0;
  if (!o) snprintf(slot_file[k], sizeof(slot_file[k]), "sys%d", k);
}

/* Lit les systemes ajoutes (custom.txt : csys0..csys3 = "n:Nom", ou numero d'un ancien modele). */
static void load_custom_systems(void) {
  for (int k = 0; k < MAX_CUSTOM_SYS; ++k) {
    char key[16];
    snprintf(key, sizeof(key), "csys%d", k);
    const char *v = custom_get(key);
    slot_name[k][0] = 0;
    if (!v) continue;
    if (v[0] == 'n' && v[1] == ':' && v[2]) slot_fill(k, v + 2, -1);
    else if (v[0] >= '0' && v[0] <= '9') {
      int pi = atoi(v);
      if (pi >= 0 && pi < (int)PRESET_COUNT) slot_fill(k, sys_presets[pi].name, pi);
    }
  }
  rebuild_systems();
}

