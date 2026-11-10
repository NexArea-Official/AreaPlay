/* Area Play - Lecture des applications, icones, app.db. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Bibliotheque : lecture des applications installees
 * ====================================================================== */

static unsigned char sfo_buf[32768];

static int read_file(const char *path, unsigned char *buf, int max) {
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return -1;
  int n = sceIoRead(f, buf, max);
  sceIoClose(f);
  return n;
}

static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned rd32(const unsigned char *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

/* Lit une valeur texte d'un fichier param.sfo (format PSF). */
/* Convertit du texte UTF-16 (little endian) en UTF-8. */
static void utf16le_to_utf8(const unsigned char *in, int inlen, char *out, int outsz) {
  int o = 0;
  for (int i = 0; i + 1 < inlen && o < outsz - 5; i += 2) {
    unsigned u = in[i] | (in[i + 1] << 8);
    if (u == 0) break;
    if (u >= 0xD800 && u <= 0xDBFF && i + 3 < inlen) { /* paire de substitution */
      unsigned lo = in[i + 2] | (in[i + 3] << 8);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    if (u < 0x80) out[o++] = (char)u;
    else if (u < 0x800) {
      out[o++] = (char)(0xC0 | (u >> 6));
      out[o++] = (char)(0x80 | (u & 0x3F));
    } else if (u < 0x10000) {
      out[o++] = (char)(0xE0 | (u >> 12));
      out[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (u & 0x3F));
    } else {
      out[o++] = (char)(0xF0 | (u >> 18));
      out[o++] = (char)(0x80 | ((u >> 12) & 0x3F));
      out[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (u & 0x3F));
    }
  }
  out[o] = 0;
}

static unsigned char sfo_dbg[16]; /* octets bruts du dernier TITLE lu (journal) */

/* Texte "large" simple : unit = 2 ou 4 octets par caractere, be = 1 pour big endian. */
static void wide_to_utf8(const unsigned char *in, int inlen, int unit, int be, char *out, int outsz) {
  int o = 0;
  for (int i = 0; i + unit <= inlen && o < outsz - 5; i += unit) {
    unsigned u = 0;
    for (int k = 0; k < unit; ++k) {
      unsigned b = in[i + (be ? k : unit - 1 - k)];
      u = (u << 8) | b;
    }
    if (u == 0) break;
    if (u < 0x80) out[o++] = (char)u;
    else if (u < 0x800) {
      out[o++] = (char)(0xC0 | (u >> 6));
      out[o++] = (char)(0x80 | (u & 0x3F));
    } else if (u < 0x10000) {
      out[o++] = (char)(0xE0 | (u >> 12));
      out[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (u & 0x3F));
    } else {
      out[o++] = (char)(0xF0 | (u >> 18));
      out[o++] = (char)(0x80 | ((u >> 12) & 0x3F));
      out[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
      out[o++] = (char)(0x80 | (u & 0x3F));
    }
  }
  out[o] = 0;
}

static int sfo_string(const unsigned char *d, int len, const char *key, char *out,
                      int outsz) {
  out[0] = 0;
  if (len < 20 || d[0] != 0 || d[1] != 'P' || d[2] != 'S' || d[3] != 'F') return 0;
  unsigned key_off = rd32(d + 8), data_off = rd32(d + 12), count = rd32(d + 16);
  size_t klen = strlen(key);
  for (unsigned i = 0; i < count && i < 256; ++i) {
    unsigned eo = 20 + i * 16;
    if (eo + 16 > (unsigned)len) break;
    const unsigned char *e = d + eo;
    unsigned koff = key_off + rd16(e);
    unsigned fmt = rd16(e + 2), dlen = rd32(e + 4), doff = data_off + rd32(e + 12);
    if (koff >= (unsigned)len || doff >= (unsigned)len) continue;
    if (fmt != 0x0204 && fmt != 0x0004) continue;
    /* compare la cle, sans depasser le fichier */
    size_t k = 0;
    while (k < klen && koff + k < (unsigned)len && d[koff + k] == (unsigned char)key[k]) ++k;
    if (k != klen || koff + klen >= (unsigned)len || d[koff + klen] != 0) continue;
    unsigned maxlen = rd32(e + 8); /* certains outils ecrivent une longueur trop courte */
    unsigned n = dlen > maxlen ? dlen : maxlen;
    if (doff + n > (unsigned)len) n = len - doff;
    if (n > 254) n = 254;
    unsigned char raw[256];
    memcpy(raw, d + doff, n);
    raw[n] = 0;
    raw[n + 1 > 255 ? 255 : n + 1] = 0;
    if (strcmp(key, "TITLE") == 0) memcpy(sfo_dbg, raw, sizeof(sfo_dbg));
    /* Certains titres sont en UTF-16 (une lettre sur deux est un octet nul). */
    /* On essaie UTF-8, UTF-16 (LE / BE) et UTF-32, et on garde le texte le plus lisible. */
    {
      char cand[4][256];
      snprintf(cand[0], sizeof(cand[0]), "%s", (const char *)raw);
      utf16le_to_utf8(raw, (int)n, cand[1], sizeof(cand[1]));
      wide_to_utf8(raw, (int)n, 4, 0, cand[2], sizeof(cand[2]));
      wide_to_utf8(raw, (int)n, 2, 1, cand[3], sizeof(cand[3]));
      int best = 0, best_score = -1;
      for (int c = 0; c < 4; ++c) {
        int sc = 0;
        for (const unsigned char *q = (const unsigned char *)cand[c]; *q; ++q)
          if (*q >= 0x20 && *q < 0x7F) ++sc;
        if (sc > best_score) {
          best_score = sc;
          best = c;
        }
      }
      snprintf(out, outsz, "%s", cand[best]);
    }
    for (char *q = out; *q; ++q)
      if (*q == '\n' || *q == '\r') *q = ' ';
    return 1;
  }
  return 0;
}

static int file_exists(const char *path);

/* Fichier image lisible ? (les jeux du commerce sont chiffres dans ux0:app :
 * seule la copie de la LiveArea, dans ur0:appmeta, est lisible). */
static int image_valid(const char *path) {
  unsigned char h[8];
  int n = read_file(path, h, 8);
  if (n < 4) return 0;
  return (h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') || (h[0] == 0xFF && h[1] == 0xD8);
}

/* Fond LiveArea d'une application (sce_sys/livearea/contents/...) : plus leger que pic0.png.
 * Le nom du fichier est lu dans template.xml (<livearea-background><image>bg0.png</image>). */
static int find_livearea_bg(const char *dir, char *out, int outsz) {
  static unsigned char xml[8192];
  char path[140];
  snprintf(path, sizeof(path), "%stemplate.xml", dir);
  int n = read_file(path, xml, sizeof(xml) - 1);
  if (n > 0) {
    xml[n] = 0;
    char *b = strstr((char *)xml, "livearea-background");
    if (b) {
      char *im = strstr(b, "<image");
      char *gt = im ? strchr(im, '>') : NULL;
      if (gt) {
        ++gt;
        while (*gt == ' ' || *gt == '\n' || *gt == '\r' || *gt == '\t') ++gt;
        char name[48];
        int o = 0;
        while (gt[o] && gt[o] != '<' && gt[o] != ' ' && gt[o] != '\n' && gt[o] != '\r' && o < (int)sizeof(name) - 1) {
          name[o] = gt[o];
          ++o;
        }
        name[o] = 0;
        if (o > 3 && !strchr(name, '/') && !strstr(name, "..")) {
          snprintf(path, sizeof(path), "%s%s", dir, name);
          if (image_valid(path)) {
            snprintf(out, outsz, "%s", path);
            return 1;
          }
        }
      }
    }
  }
  static const char *names[] = {"bg0.png", "bg.png", "bg1.png", "bg01.png", "bg02.png", NULL};
  for (int i = 0; names[i]; ++i) {
    snprintf(path, sizeof(path), "%s%s", dir, names[i]);
    if (image_valid(path)) {
      snprintf(out, outsz, "%s", path);
      return 1;
    }
  }
  return 0;
}

static int cmp_title(const void *a, const void *b) {
  const Game *x = *(Game *const *)a, *y = *(Game *const *)b;
  if (opt_sort == 2 && x->fav != y->fav) return y->fav - x->fav;
  if (opt_sort == 1 && x->last_played != y->last_played) return x->last_played > y->last_played ? -1 : 1;
  return strcasecmp(x->name, y->name);
}

/* Premiere lettre d'un titre (chiffres et symboles regroupes sous '#'). */
static int name_letter(const Game *g) {
  for (const char *p = g->name; *p; ++p) {
    if (isalpha((unsigned char)*p)) return toupper((unsigned char)*p);
    if (isdigit((unsigned char)*p)) return '#';
  }
  return '#';
}

/* Saut a la lettre suivante (dir > 0) ou au debut de la lettre / lettre precedente. */
static int letter_jump(Game **list, int n, int sel, int dir) {
  if (n <= 0) return 0;
  int c = name_letter(list[sel]);
  if (dir > 0) {
    for (int i = sel + 1; i < n; ++i)
      if (name_letter(list[i]) != c) return i;
    return n - 1;
  }
  int i = sel;
  while (i > 0 && name_letter(list[i - 1]) == c) --i;
  if (i < sel || i == 0) return i;
  int pc = name_letter(list[i - 1]), j = i - 1;
  while (j > 0 && name_letter(list[j - 1]) == pc) --j;
  return j;
}

static int cmp_recent(const void *a, const void *b) {
  const Game *x = *(Game *const *)a, *y = *(Game *const *)b;
  if (x->last_played != y->last_played) return x->last_played > y->last_played ? -1 : 1;
  return strcasecmp(x->name, y->name);
}

/* ---- Jeu PSP en fichier : ISO / EBOOT.PBP lus tels quels, CSO decompresse bloc par bloc ----
 * CSO : en-tete "CISO", taille de bloc a 16, decalage a 21, puis une table de positions
 * (bit 31 = bloc non compresse), blocs compresses en "deflate" brut (zlib). */
typedef struct {
  SceUID fd;
  int cso;
  unsigned bsize, align, nblk;
  int cur;                   /* bloc present dans blk (-1 : aucun) */
  unsigned char *blk, *zbuf;
} DiscFile;

static void disc_close(DiscFile *d) {
  if (d->fd >= 0) sceIoClose(d->fd);
  free(d->blk);
  free(d->zbuf);
  memset(d, 0, sizeof(*d));
  d->fd = -1;
}

static int disc_open(DiscFile *d, const char *path) {
  memset(d, 0, sizeof(*d));
  d->cur = -1;
  d->fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (d->fd < 0) return 0;
  unsigned char h[24];
  if (sceIoRead(d->fd, h, sizeof(h)) == (int)sizeof(h) && memcmp(h, "CISO", 4) == 0) {
    unsigned long long total = rd32(h + 8) | ((unsigned long long)rd32(h + 12) << 32);
    d->bsize = rd32(h + 16);
    d->align = h[21];
    if (d->bsize < 2048 || d->bsize > 65536 || d->align > 8 || h[20] > 1) {
      disc_close(d);
      return 0;
    }
    d->nblk = (unsigned)((total + d->bsize - 1) / d->bsize);
    d->blk = (unsigned char *)malloc(d->bsize);
    d->zbuf = (unsigned char *)malloc(d->bsize + (1u << d->align) + 64);
    d->cso = 1;
    if (!d->blk || !d->zbuf) {
      disc_close(d);
      return 0;
    }
  }
  return 1;
}

static int disc_block(DiscFile *d, unsigned b) {
  if ((int)b == d->cur) return 1;
  unsigned char ix[8];
  if (b >= d->nblk || sceIoLseek(d->fd, 24 + (SceOff)b * 4, SCE_SEEK_SET) < 0 || sceIoRead(d->fd, ix, 8) != 8)
    return 0;
  unsigned e0 = rd32(ix), e1 = rd32(ix + 4);
  SceOff pos = (SceOff)(e0 & 0x7FFFFFFFu) << d->align, end = (SceOff)(e1 & 0x7FFFFFFFu) << d->align;
  if (end <= pos) return 0;
  unsigned raw = (unsigned)(end - pos), zmax = d->bsize + (1u << d->align) + 64;
  if (sceIoLseek(d->fd, pos, SCE_SEEK_SET) < 0) return 0;
  if (e0 & 0x80000000u) { /* bloc stocke tel quel */
    unsigned n = raw < d->bsize ? raw : d->bsize;
    if ((unsigned)sceIoRead(d->fd, d->blk, n) != n) return 0;
  } else {
    if (raw > zmax) raw = zmax;
    int got = sceIoRead(d->fd, d->zbuf, raw);
    if (got <= 0) return 0;
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, -15) != Z_OK) return 0;
    z.next_in = d->zbuf;
    z.avail_in = (unsigned)got;
    z.next_out = d->blk;
    z.avail_out = d->bsize;
    int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    if (r != Z_STREAM_END && z.avail_out != 0) return 0;
  }
  d->cur = (int)b;
  return 1;
}

/* Lit len octets a la position off de l'image disque (ou du fichier). Retourne le nombre lu. */
static int disc_read(DiscFile *d, unsigned off, void *buf, int len) {
  if (!d->cso) {
    if (sceIoLseek(d->fd, (SceOff)off, SCE_SEEK_SET) < 0) return -1;
    return sceIoRead(d->fd, buf, len);
  }
  unsigned char *o = (unsigned char *)buf;
  int done = 0;
  while (done < len) {
    unsigned p = off + (unsigned)done, b = p / d->bsize, in = p % d->bsize;
    if (!disc_block(d, b)) break;
    unsigned n = d->bsize - in;
    if (n > (unsigned)(len - done)) n = (unsigned)(len - done);
    memcpy(o + done, d->blk + in, n);
    done += (int)n;
  }
  return done > 0 ? done : -1;
}

/* Decode un PNG situe dans un fichier a un decalage donne (jeux PSP : EBOOT.PBP, ISO, CSO). */
static vita2d_texture *load_png_range(const char *path, unsigned off, unsigned size) {
  if (size < 16 || size > (4u << 20)) return NULL;
  DiscFile d;
  if (!disc_open(&d, path)) return NULL;
  vita2d_texture *t = NULL;
  unsigned char *buf = (unsigned char *)malloc(size);
  if (buf) {
    if ((unsigned)disc_read(&d, off, buf, (int)size) == size) t = vita2d_load_PNG_buffer(buf);
    free(buf);
  }
  disc_close(&d);
  return t;
}

static int icon_err;      /* code d'echec de la derniere icone (journal) */
static int icon_w, icon_h;

static void log_icon_fail(const Game *g) {
  SceUID f = sceIoOpen(LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  if (f < 0) return;
  char line[300];
  int n = snprintf(line, sizeof(line), "icone manquante : %s | %s | %s | err=%08X %X/%X\n", g->id,
                   g->title, g->src_path[0] ? g->src_path : g->icon_path, (unsigned)icon_err,
                   icon_w, icon_h);
  if (n > 0) sceIoWrite(f, line, n < (int)sizeof(line) ? n : (int)sizeof(line) - 1);
  sceIoClose(f);
}

/* Ecrit une ligne dans le journal. */
static void log_line(const char *msg) {
  SceUID f = sceIoOpen(LOG_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  if (f < 0) return;
  sceIoWrite(f, msg, (int)strlen(msg));
  sceIoWrite(f, "\n", 1);
  sceIoClose(f);
}

/* Memoire libre (principale / video) dans le journal : aide a comprendre un echec de chargement. */
static void log_mem(const char *when) {
  SceKernelFreeMemorySizeInfo mi;
  memset(&mi, 0, sizeof(mi));
  mi.size = sizeof(mi);
  if (sceKernelGetFreeMemorySize(&mi) < 0) return;
  char l[128];
  snprintf(l, sizeof(l), "memoire libre (%s) : principale %d Ko, video %d Ko", when, mi.size_user / 1024,
           mi.size_cdram / 1024);
  log_line(l);
}

/* Charge une icone d'application : d'abord par vita2d, sinon en lisant le fichier
 * nous-memes (meme methode que pour les jeux PSP). */
static vita2d_texture *load_app_png(const char *path) {
  icon_err = 0;
  icon_w = icon_h = 0;
  vita2d_texture *t = vita2d_load_PNG_file(path);
  if (t) return t;
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) {
    icon_err = f;
    return NULL;
  }
  SceOff sz = sceIoLseek(f, 0, SCE_SEEK_END);
  sceIoLseek(f, 0, SCE_SEEK_SET);
  if (sz < 16 || sz > (16 << 20)) {
    icon_err = -2;
    icon_w = (int)sz;
    sceIoClose(f);
    return NULL;
  }
  unsigned char *buf = (unsigned char *)malloc((size_t)sz + 16);
  if (!buf) {
    icon_err = -3;
    sceIoClose(f);
    return NULL;
  }
  if (sceIoRead(f, buf, (int)sz) == (int)sz) {
    if (buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') t = vita2d_load_PNG_buffer(buf);
    else if (buf[0] == 0xFF && buf[1] == 0xD8) t = vita2d_load_JPEG_buffer(buf, (unsigned long)sz);
    else {
      icon_err = -4; /* ni PNG ni JPEG */
      icon_w = (buf[0] << 8) | buf[1];
      icon_h = (buf[2] << 8) | buf[3];
    }
  } else {
    icon_err = -5;
  }
  free(buf);
  sceIoClose(f);
  return t;
}

/* ---- Cache d'icones : pixels bruts dans un seul fichier (icons.bin) ----
 * Au demarrage on ne lit que l'index (identifiant + position des pixels) ; chaque icone n'est
 * chargee qu'au moment ou son jeu s'affiche (voir le chargeur et tex_evict). */

typedef struct {
  char id[16];
  unsigned off;             /* position des pixels dans icons.bin */
  unsigned short w, h;
} IconC;
static IconC iconc[MAX_TOTAL];
static int iconc_n;
static SceUID iconc_wfd = -1; /* ouvert pendant un scan : on y recopie les icones */
static unsigned iconc_wpos;   /* taille deja ecrite pendant le scan */

/* hint : position probable (l'ordre du cache suit celui de la bibliotheque). -1 si absente. */
static int iconc_find(const char *id, int hint) {
  if (hint >= 0 && hint < iconc_n && strcmp(iconc[hint].id, id) == 0) return hint;
  for (int i = 0; i < iconc_n; ++i)
    if (strcmp(iconc[i].id, id) == 0) return i;
  return -1;
}

/* Lit une icone dans icons.bin (appelable depuis le thread de chargement : son propre fichier ouvert). */
static vita2d_texture *iconc_read(unsigned off, int w, int h) {
  if (w <= 0 || h <= 0 || w > 512 || h > 512) return NULL;
  SceUID f = sceIoOpen(ICON_CACHE_FILE, SCE_O_RDONLY, 0);
  if (f < 0) return NULL;
  vita2d_texture *t = NULL;
  int bytes = w * h * 4;
  unsigned char *buf = (unsigned char *)malloc((size_t)bytes);
  if (buf && sceIoLseek(f, (SceOff)off, SCE_SEEK_SET) >= 0 && sceIoRead(f, buf, bytes) == bytes) {
    t = vita2d_create_empty_texture((unsigned)w, (unsigned)h);
    if (t) {
      int stride = (int)vita2d_texture_get_stride(t);
      unsigned char *dst = (unsigned char *)vita2d_texture_get_datap(t);
      for (int y = 0; y < h; ++y) memcpy(dst + (size_t)y * stride, buf + (size_t)y * w * 4, (size_t)w * 4);
      tex_prep(t);
    }
  }
  free(buf);
  sceIoClose(f);
  return t;
}

/* Ecrit l'icone dans icons.bin (pendant un scan) et l'ajoute a l'index. Retourne 1 si c'est fait. */
static int icon_to_cache(const char *id, vita2d_texture *t) {
  if (iconc_wfd < 0 || !t || iconc_n >= MAX_TOTAL) return 0;
  int w = (int)vita2d_texture_get_width(t), h = (int)vita2d_texture_get_height(t);
  int stride = (int)vita2d_texture_get_stride(t);
  const unsigned char *src = (const unsigned char *)vita2d_texture_get_datap(t);
  if (w <= 0 || h <= 0 || w > 512 || h > 512 || !src) return 0;
  unsigned char *buf = (unsigned char *)malloc((size_t)w * h * 4);
  if (!buf) return 0;
  for (int y = 0; y < h; ++y) memcpy(buf + (size_t)y * w * 4, src + (size_t)y * stride, (size_t)w * 4);
  unsigned char hdr[20];
  memset(hdr, 0, sizeof(hdr));
  snprintf((char *)hdr, 16, "%s", id);
  hdr[16] = (unsigned char)(w & 255);
  hdr[17] = (unsigned char)(w >> 8);
  hdr[18] = (unsigned char)(h & 255);
  hdr[19] = (unsigned char)(h >> 8);
  int ok = sceIoWrite(iconc_wfd, hdr, sizeof(hdr)) == (int)sizeof(hdr) && sceIoWrite(iconc_wfd, buf, w * h * 4) == w * h * 4;
  free(buf);
  if (!ok) return 0;
  IconC *e = &iconc[iconc_n++];
  snprintf(e->id, sizeof(e->id), "%s", id);
  e->off = iconc_wpos + 20;
  e->w = (unsigned short)w;
  e->h = (unsigned short)h;
  iconc_wpos += 20 + (unsigned)(w * h * 4);
  return 1;
}

/* Lit seulement l'index de icons.bin (en-tetes), sans charger les pixels. */
static void iconc_index(void) {
  iconc_n = 0;
  SceUID f = sceIoOpen(ICON_CACHE_FILE, SCE_O_RDONLY, 0);
  if (f < 0) return;
  unsigned pos = 0;
  while (iconc_n < MAX_TOTAL) {
    unsigned char hdr[20];
    if (sceIoRead(f, hdr, sizeof(hdr)) != (int)sizeof(hdr)) break;
    int w = hdr[16] | (hdr[17] << 8), h = hdr[18] | (hdr[19] << 8);
    if (w <= 0 || h <= 0 || w > 512 || h > 512) break;
    IconC *e = &iconc[iconc_n++];
    memcpy(e->id, hdr, 16);
    e->id[15] = 0;
    e->off = pos + 20;
    e->w = (unsigned short)w;
    e->h = (unsigned short)h;
    pos += 20 + (unsigned)(w * h * 4);
    if (sceIoLseek(f, (SceOff)pos, SCE_SEEK_SET) < 0) break;
  }
  sceIoClose(f);
}

/* Pendant un scan : lit l'icone de l'application, la range dans icons.bin, puis libere la texture
 * (elle sera rechargee depuis le cache quand le jeu s'affichera). */
static void load_icon(Game *g) {
  g->icon_idx = -1;
  icon_err = 0;
  icon_w = icon_h = 0;
  if (g->src_path[0] && g->icon_size) g->cover = load_png_range(g->src_path, g->icon_off, g->icon_size); /* EBOOT, ISO */
  else {
    g->cover = load_app_png(g->icon_path);
    if (!g->cover) { /* repli : copie de l'icone utilisee par la LiveArea */
      char alt[64];
      int e1 = icon_err;
      snprintf(alt, sizeof(alt), "ur0:appmeta/%s/icon0.png", g->id);
      g->cover = load_app_png(alt);
      if (!g->cover) icon_err = e1;
    }
  }
  if (g->cover && (vita2d_texture_get_width(g->cover) > 512 ||
                   vita2d_texture_get_height(g->cover) > 512)) {
    icon_err = -6; /* trop grande */
    icon_w = (int)vita2d_texture_get_width(g->cover);
    icon_h = (int)vita2d_texture_get_height(g->cover);
    vita2d_free_texture(g->cover);
    g->cover = NULL;
  }
  if (!g->cover) {
    ++missing_icons;
    log_icon_fail(g);
  } else if (icon_to_cache(g->id, g->cover)) {
    g->icon_idx = iconc_n - 1;
    vita2d_free_texture(g->cover); /* jamais dessinee : liberation directe */
    g->cover = NULL;
  } else {
    tex_prep(g->cover); /* cache impossible : on garde la texture */
    g->appear = 1.0f;
  }
}

/* ---- Base des applications de la LiveArea (ur0:shell/db/app.db) : noms des bulles ---- */

static unsigned char *appdb_buf;
static int appdb_len, appdb_tried;

static void appdb_load(void) {
  if (appdb_tried) return;
  appdb_tried = 1;
  SceUID f = sceIoOpen("ur0:shell/db/app.db", SCE_O_RDONLY, 0);
  if (f < 0) {
    log_line("app.db illisible");
    return;
  }
  SceOff sz = sceIoLseek(f, 0, SCE_SEEK_END);
  sceIoLseek(f, 0, SCE_SEEK_SET);
  if (sz < 1024 || sz > (24 << 20)) {
    log_line("app.db : taille inattendue");
    sceIoClose(f);
    return;
  }
  appdb_buf = (unsigned char *)malloc((size_t)sz);
  if (appdb_buf) {
    int total = 0;
    while (total < (int)sz) {
      int want = (int)sz - total;
      if (want > (1 << 20)) want = 1 << 20;
      int r = sceIoRead(f, appdb_buf + total, want);
      if (r <= 0) break;
      total += r;
    }
    appdb_len = total;
  }
  sceIoClose(f);
}

static void appdb_free(void) {
  if (appdb_buf) free(appdb_buf);
  appdb_buf = NULL;
  appdb_len = 0;
  appdb_tried = 0;
}

/* Cherche le nom d'une application dans app.db : le texte qui suit l'identifiant. */
static int appdb_title(const char *id, char *out, int outsz) {
  out[0] = 0;
  appdb_load();
  if (!appdb_buf) return 0;
  int il = (int)strlen(id), shown = 0, found = 0;
  for (int i = 0; i + il < appdb_len;) {
    unsigned char *p = (unsigned char *)memchr(appdb_buf + i, id[0], (size_t)(appdb_len - i - il));
    if (!p) break;
    i = (int)(p - appdb_buf) + 1;
    if (memcmp(p, id, (size_t)il) != 0) continue;
    ++found;
    unsigned char *q = p + il, *end = appdb_buf + appdb_len;
    char run[160];
    int o = 0;
    while (q < end && o < (int)sizeof(run) - 1 && *q >= 0x20 && *q != 0x7F) run[o++] = (char)*q++;
    run[o] = 0;
    if (shown < 3) { /* journal : contexte brut, pour comprendre le format */
      char dbg[400];
      int dn = snprintf(dbg, sizeof(dbg), "app.db %s #%d : avant='", id, found);
      for (int k = -40; k < 0 && dn < 300; ++k) {
        int idx = (int)(p - appdb_buf) + k;
        unsigned char c = idx >= 0 ? appdb_buf[idx] : '.';
        dbg[dn++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
      }
      dn += snprintf(dbg + dn, sizeof(dbg) - dn, "' apres='%.150s'", run);
      log_line(dbg);
      ++shown;
    }
    /* coupe aux chemins et identifiants qui suivent */
    static const char *cuts[] = {"ur0:", "ux0:", "vs0:", "sce_", "PSPEMU", "/", "icon", "pic0", NULL};
    for (int c = 0; cuts[c]; ++c) {
      char *at = strstr(run, cuts[c]);
      if (at) *at = 0;
    }
    int letters = 0;
    for (const char *r = run; *r; ++r)
      if (isalpha((unsigned char)*r)) ++letters;
    if (strlen(run) >= 3 && letters >= 2) {
      snprintf(out, outsz, "%s", run);
      return 1;
    }
  }
  return 0;
}

/* Fond : charge a la demande. Les jeux PSP en fichier sont lus tout de suite ;
 * les autres sont decodes par un thread d'arriere-plan (pas d'a-coup a l'ecran). */
#define BG_MAX_W 4096 /* plus grand : refuse ; au-dela de l'ecran : reduit a 960 x 544 */
#define BG_MAX_H 4096

static void log_bg_fail(const Game *g, const char *path) {
  char line[260];
  snprintf(line, sizeof(line), "fond manquant : %s | %s | err=%08X %X/%X", g->id, path,
           (unsigned)icon_err, icon_w, icon_h);
  log_line(line);
}

static vita2d_texture *load_bg_file(const char *path) {
  vita2d_texture *t = load_app_png(path);
  if (t && (vita2d_texture_get_width(t) > BG_MAX_W || vita2d_texture_get_height(t) > BG_MAX_H)) {
    icon_err = -6;
    icon_w = (int)vita2d_texture_get_width(t);
    icon_h = (int)vita2d_texture_get_height(t);
    vita2d_free_texture(t);
    t = NULL;
  }
  if (t) t = tex_shrink(t, SCREEN_W, 544); /* image choisie tres grande : moins de memoire, meme rendu */
  return t;
}

static void load_bg(Game *g) { /* synchrone */
  if (g->bg || !g->has_bg) return;
  if (g->src_path[0] && g->bg_size) g->bg = load_png_range(g->src_path, g->bg_off, g->bg_size);
  else g->bg = load_bg_file(g->bg_path);
  if (!g->bg && !g->src_path[0]) { /* application : copie de la LiveArea */
    log_bg_fail(g, g->bg_path);
    char alt[64];
    snprintf(alt, sizeof(alt), "ur0:appmeta/%s/pic0.png", g->id);
    g->bg = load_bg_file(alt);
    if (!g->bg) log_bg_fail(g, alt);
  }
  if (!g->bg) g->has_bg = 0; /* illisible : on utilisera l'icone */
}

