/* Area Play - Chargeur d images en arriere-plan (thread). Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ---- Chargeur d'images en arriere-plan ----
 * Le decodage des PNG / JPEG (20 a 40 ms chacun) ne se fait jamais dans la boucle d'affichage :
 * un thread traite une file de demandes (fonds en priorite, puis jaquettes) ; le thread principal
 * recupere les images terminees a chaque image et se contente d'echanger les textures. */
/* Jaquettes reduites a 240 x 272 au plus : 240 x 272 x 4 octets tiennent dans un bloc de 256 Ko de memoire
 * video (300 x 340 en prenait deux) : deux fois plus de jaquettes gardees en memoire, meme rendu a l'ecran. */
#define COVER_MAX_W 240
#define COVER_MAX_H 272
enum { LJ_FREE = 0, LJ_QUEUED, LJ_RUNNING, LJ_DONE };
enum { LJ_BG = 1, LJ_COVER = 2, LJ_ICON = 3 };
#define LJ_N 8
typedef struct {
  volatile int state;
  int type, gen;
  int prio; /* 1 = jeu a l'ecran ou proche de la selection, 0 = prechargement */
  Game *g;
  char path[128], alt[96];
  char id[16];   /* LJ_COVER : nom du fichier du cache disque */
  unsigned off;  /* LJ_ICON : position et taille de l'icone dans icons.bin */
  int w, h;
  vita2d_texture *tex, *blur;
} LoadJob;
static LoadJob lj[LJ_N];
static int lj_next_prio = 1; /* priorite des prochaines demandes (0 pendant le prechargement) */
static SceUID lj_thread = -1, lj_sema = -1;
static int lj_gen; /* change quand la bibliotheque est videe : les resultats anciens sont jetes */

/* ---- Cache disque des jaquettes deja decodees et reduites (ux0:data/AreaPlay/cache/<ID>.raw) ----
 * Relire 255 Ko de pixels prets prend quelques ms, contre 20 a 100 ms pour decoder un PNG / JPEG :
 * au retour sur Area Play, les jaquettes reviennent vite. L'en-tete garde la taille, la date et le
 * chemin du fichier d'origine : une jaquette changee ou retelechargee est redecodee. */
#define CC_DIR DATA_DIR "/cache"
#define CC_MAGIC 0x31434656u /* "VFC1" */
typedef struct {
  unsigned magic, w, h, size, path_hash, stamp_lo, stamp_hi, pad;
} CcHead;
static int cc_head_for(const char *path, CcHead *h) {
  SceIoStat st;
  if (sceIoGetstat(path, &st) < 0) return 0;
  memset(h, 0, sizeof(*h));
  h->magic = CC_MAGIC;
  h->size = (unsigned)st.st_size;
  unsigned long long t = ((((unsigned long long)st.st_mtime.year * 13 + st.st_mtime.month) * 32 + st.st_mtime.day) * 24 +
                          st.st_mtime.hour) * 3600ULL + st.st_mtime.minute * 60ULL + st.st_mtime.second;
  h->stamp_lo = (unsigned)t;
  h->stamp_hi = (unsigned)(t >> 32);
  unsigned x = 2166136261u;
  for (const char *s = path; *s; ++s) x = (x ^ (unsigned char)*s) * 16777619u;
  h->path_hash = x;
  return 1;
}
static vita2d_texture *cc_load(const char *id, const CcHead *want) {
  char p[64];
  snprintf(p, sizeof(p), CC_DIR "/%s.raw", id);
  SceUID f = sceIoOpen(p, SCE_O_RDONLY, 0);
  if (f < 0) return NULL;
  CcHead h;
  vita2d_texture *t = NULL;
  if (sceIoRead(f, &h, sizeof(h)) == (int)sizeof(h) && h.magic == CC_MAGIC && h.size == want->size &&
      h.path_hash == want->path_hash && h.stamp_lo == want->stamp_lo && h.stamp_hi == want->stamp_hi &&
      h.w > 0 && h.h > 0 && h.w <= COVER_MAX_W && h.h <= COVER_MAX_H && (t = vita2d_create_empty_texture(h.w, h.h))) {
    unsigned char *px = (unsigned char *)vita2d_texture_get_datap(t);
    int stride = (int)vita2d_texture_get_stride(t), row = (int)h.w * 4, ok = 1;
    if (stride == row) ok = sceIoRead(f, px, row * (int)h.h) == row * (int)h.h;
    else
      for (unsigned y = 0; y < h.h && ok; ++y) ok = sceIoRead(f, px + y * stride, row) == row;
    if (!ok) {
      vita2d_free_texture(t);
      t = NULL;
    }
  }
  sceIoClose(f);
  return t;
}
static void cc_save(const char *id, const CcHead *head, const vita2d_texture *t) {
  if (!tex_is_rgba(t)) return;
  char p[64];
  sceIoMkdir(CC_DIR, 0777);
  snprintf(p, sizeof(p), CC_DIR "/%s.raw", id);
  SceUID f = sceIoOpen(p, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  CcHead h = *head;
  h.w = vita2d_texture_get_width(t);
  h.h = vita2d_texture_get_height(t);
  const unsigned char *px = (const unsigned char *)vita2d_texture_get_datap(t);
  int stride = (int)vita2d_texture_get_stride(t), row = (int)h.w * 4, ok = sceIoWrite(f, &h, sizeof(h)) == (int)sizeof(h);
  if (stride == row) ok = ok && sceIoWrite(f, px, row * (int)h.h) == row * (int)h.h;
  else
    for (unsigned y = 0; y < h.h && ok; ++y) ok = sceIoWrite(f, px + y * stride, row) == row;
  sceIoClose(f);
  if (!ok) sceIoRemove(p); /* carte pleine : pas de fichier a moitie ecrit */
}

static void lj_run(LoadJob *j) {
  if (j->type == LJ_BG) {
    j->tex = load_bg_file(j->path);
    if (!j->tex && j->alt[0]) j->tex = load_bg_file(j->alt);
    tex_prep(j->tex);
  } else if (j->type == LJ_ICON) {
    j->tex = iconc_read(j->off, j->w, j->h);
  } else {
    /* le fond flou n'est plus prepare ici : seulement pour le jeu affiche (moitie moins de memoire) */
    CcHead head;
    int cached = j->id[0] && cc_head_for(j->path, &head);
    if (cached && (j->tex = cc_load(j->id, &head)) != NULL) { /* deja decodee : relue telle quelle */
      tex_prep(j->tex);
      return;
    }
    j->tex = load_image_file(j->path);
    if (j->tex) {
      j->tex = tex_shrink(j->tex, COVER_MAX_W, COVER_MAX_H);
      tex_prep(j->tex);
      if (cached) cc_save(j->id, &head, j->tex);
    }
  }
}

static int lj_worker(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  for (;;) {
    sceKernelWaitSema(lj_sema, 1, NULL);
    for (;;) { /* traite tout ce qui attend, fonds d'abord */
      /* ordre : fonds, puis images des jeux a l'ecran, puis prechargement (sinon une jaquette visible
       * attendait derriere celles d'autres systemes et s'affichait en retard) */
      LoadJob *pick = NULL;
      int best = -1;
      for (int i = 0; i < LJ_N; ++i) {
        if (lj[i].state != LJ_QUEUED) continue;
        int rank = lj[i].type == LJ_BG ? 2 : lj[i].prio;
        if (rank > best) {
          best = rank;
          pick = &lj[i];
        }
      }
      if (!pick) break;
      pick->state = LJ_RUNNING;
      __sync_synchronize();
      lj_run(pick);
      __sync_synchronize(); /* resultat visible avant l'etat "termine" (autre coeur) */
      pick->state = LJ_DONE;
    }
  }
  return 0;
}

/* Cree le thread de chargement. Priorites valides : 64 a 191 ; 0x10000100 = priorite par defaut (160).
 * (En 4.1 / 4.2 : 0x10000120 = 192, refuse par la Vita -> aucune jaquette ne se chargeait.)
 * Si aucun essai ne marche, lj_sync = 1 : chargement direct, une image par affichage. */
static int lj_sync;
static int lj_start(void) {
  static int tried;
  if (lj_thread >= 0) return 1;
  if (tried) return 0;
  tried = 1;
  lj_sema = sceKernelCreateSema("vf_load_sema", 0, 0, LJ_N, NULL);
  static const int prios[3] = {0x10000108, 0x10000100, 0xB0}; /* un peu moins prioritaire que l'affichage */
  static const int stacks[3] = {0x20000, 0x10000, 0x10000};
  for (int t = 0; t < 3 && lj_sema >= 0 && lj_thread < 0; ++t) {
    SceUID th = sceKernelCreateThread("vf_load", lj_worker, prios[t], stacks[t], 0, 0, NULL);
    int sr = th >= 0 ? sceKernelStartThread(th, 0, NULL) : th;
    char l[96];
    snprintf(l, sizeof(l), "chargeur : essai %d create=%08X start=%08X", t, (unsigned)th, (unsigned)sr);
    log_line(l);
    if (th >= 0 && sr >= 0) lj_thread = th;
    else if (th >= 0) sceKernelDeleteThread(th);
  }
  if (lj_thread < 0) {
    lj_sync = 1;
    log_line("chargeur : pas de thread, chargement direct des images");
  }
  return lj_thread >= 0;
}

static int lj_busy(int type) {
  int c = 0;
  for (int i = 0; i < LJ_N; ++i)
    if (lj[i].state != LJ_FREE && lj[i].type == type) ++c;
  return c;
}

/* Ajoute une demande. 0 = file pleine (on reessaiera a l'image suivante).
 * Sans thread, l'image est chargee tout de suite et recuperee par lj_poll comme les autres. */
static int lj_submit_ex(int type, Game *g, const char *path, const char *alt, unsigned off, int w, int h) {
  int threaded = lj_start();
  for (int i = 0; i < LJ_N; ++i) {
    LoadJob *j = &lj[i];
    if (j->state != LJ_FREE) continue;
    j->type = type;
    j->prio = lj_next_prio;
    j->gen = lj_gen;
    j->g = g;
    snprintf(j->id, sizeof(j->id), "%s", g && g->id[0] ? g->id : "");
    snprintf(j->path, sizeof(j->path), "%s", path ? path : "");
    snprintf(j->alt, sizeof(j->alt), "%s", alt ? alt : "");
    j->off = off;
    j->w = w;
    j->h = h;
    j->tex = j->blur = NULL;
    if (!threaded) {
      lj_run(j);
      j->state = LJ_DONE;
      return 1;
    }
    __sync_synchronize();
    j->state = LJ_QUEUED;
    sceKernelSignalSema(lj_sema, 1);
    return 1;
  }
  return 0;
}
static int lj_submit(int type, Game *g, const char *path, const char *alt) {
  return lj_submit_ex(type, g, path, alt, 0, 0, 0);
}

/* Demande le chargement du fond de g. Retourne 0 si la file est pleine (on reessaie). */
static int bg_request(Game *g) {
  if (g->bg || !g->has_bg || g->bg_queued) return 1;
  if ((g->src_path[0] && g->bg_size) || !lj_start()) { /* image dans un EBOOT / ISO, ou pas de thread : lecture directe */
    load_bg(g);
    tex_prep(g->bg);
    return 1;
  }
  char alt[96];
  snprintf(alt, sizeof(alt), "ur0:appmeta/%s/pic0.png", g->id);
  if (!lj_submit(LJ_BG, g, g->bg_path, alt)) return 0;
  g->bg_queued = 1;
  return 1;
}

/* A appeler a chaque image : recupere les images terminees. shown / prev : jeux dont le fond est affiche. */
static void lj_poll(const Game *shown, const Game *prev) {
  for (int i = 0; i < LJ_N; ++i) {
    LoadJob *j = &lj[i];
    if (j->state != LJ_DONE) continue;
    __sync_synchronize();
    Game *g = j->g;
    if (j->gen != lj_gen || !g) { /* bibliotheque videe entre-temps : jamais dessinees, liberation directe */
      if (j->tex) vita2d_free_texture(j->tex);
      if (j->blur) vita2d_free_texture(j->blur);
    } else if (j->type == LJ_BG) {
      g->bg_queued = 0;
      if (!j->tex) {
        g->has_bg = 0;
        log_bg_fail(g, g->bg_path);
      } else if ((g == shown || g == prev) && !g->bg) {
        g->bg = j->tex;
      } else {
        vita2d_free_texture(j->tex); /* plus affiche : jamais dessine */
      }
    } else { /* jaquette ou icone */
      g->cover_pending = 0;
      if (j->tex) {
        int had = g->cover != NULL;
        tex_free_later(g->cover);
        tex_free_later(g->blur);
        g->cover = j->tex;
        g->blur = NULL;
        g->blur_tried = 0;
        /* deja dessinee (icone remplacee par la jaquette) : pas de fondu ; sinon apparition en fondu */
        if (!had) g->appear = 0.0f;
        g->seen = g_frame;
      } else {
        char l[200];
        snprintf(l, sizeof(l), "%s illisible : %s", j->type == LJ_ICON ? "icone" : "jaquette",
                 j->type == LJ_ICON ? g->id : j->path);
        log_line(l);
        log_mem("image");
        if (j->type == LJ_COVER) { /* fichier abime : l'icone prendra sa place */
          g->cover_failed = 1;
          if (!g->cover && g->icon_idx >= 0) g->cover_pending = 1;
        }
      }
    }
    j->tex = j->blur = NULL;
    j->state = LJ_FREE;
  }
}

/* La bibliotheque va etre videe : les images en cours de chargement ne doivent pas etre
 * rattachees a la nouvelle application qui occupera la meme place dans all_apps. */
static void bg_forget_pending(void) { ++lj_gen; }

static void release_bg(Game *g) {
  if (!g) return;
  tex_free_later(g->bg); /* le GPU peut encore l'utiliser : liberee avant l'image suivante */
  g->bg = NULL;
  tex_free_later(g->blur); /* fond flou : seulement pour le jeu affiche */
  g->blur = NULL;
  g->blur_tried = 0;
}

/* ---- Images en memoire : seulement pour les jeux affiches ou proches de la selection ----
 * Chaque image occupe de la memoire video (arrondie a 256 Ko par vita2d) : au-dela d'un certain
 * nombre, on libere celles des jeux qui ne sont plus affiches depuis le plus longtemps.
 * Elles seront rechargees (en arriere-plan) si le jeu revient a l'ecran. */
#define TEX_BUDGET 192 /* jeux ayant une image en memoire, au plus (environ 48 Mo : 256 Ko chacune) */
static void tex_evict(void) {
  int res = 0;
  for (int i = 0; i < app_n; ++i)
    if (all_apps[i].cover) ++res;
  g_resident = res;
  if (res <= TEX_BUDGET) return;
  static const unsigned ages[3] = {600, 120, 20}; /* images non dessinees depuis 10 s, 2 s, puis 1/3 s */
  int freed = 0;
  for (int a = 0; a < 3 && res > TEX_BUDGET; ++a)
    for (int i = 0; i < app_n && res > TEX_BUDGET && freed < 16; ++i) {
      Game *g = &all_apps[i];
      if (!g->cover || g_frame - g->seen <= ages[a]) continue;
      tex_free_later(g->cover);
      tex_free_later(g->blur);
      g->cover = NULL;
      g->blur = NULL;
      g->blur_tried = 0;
      g->cover_pending = 1; /* a recharger s'il revient a l'ecran */
      --res;
      ++freed;
    }
  g_resident = res;
}


