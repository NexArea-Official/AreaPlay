/* Area Play - Theme Noel. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Theme Noel : fond d'hiver, neige qui tombe, neige posee sur la tuile et sur chaque jaquette,
 *  guirlande, branches de houx, icones du menu (sapin, cadeau, flocon), clochettes,
 *  saison (1er decembre - 6 janvier). Meme dessin que la version Android (Xmas.kt).
 *  Images : app0:assets/xmas (fonds) et app0:assets/xmas/deco (neiges, houx, icones).
 * ====================================================================== */

static int opt_snow = 1;  /* neige qui tombe (Reglages > Theme, theme Noel seulement) */
static int xmas_asked;    /* annee (celle de decembre) ou le theme Noel a deja ete propose */
static int xmas_prev;     /* theme * 100 + accent + 1 d'avant Noel ; 0 = theme choisi a la main */

static int xmas_on(void) { return opt_theme == XMAS_THEME; }

static vita2d_texture *xm_snow[3][2]; /* [neige][0 = grande, 1 = petite] */
static vita2d_texture *xm_holly[2];   /* houx, houx retourne */
static vita2d_texture *xm_ico[3];     /* sapin, cadeau, flocon */
/* neiges dessinees : largeur d'origine et colonnes de raccord (bout gauche [0,c1), motif [c1,c2), bout droit) */
static const int xm_snow_src[3][3] = {{1841, 410, 1484}, {1803, 220, 1446}, {1812, 241, 1408}};

static vita2d_texture *xm_tex(const char *name) {
  char p[96];
  snprintf(p, sizeof(p), "app0:assets/xmas/deco/%s.png", name);
  vita2d_texture *t = load_image_file(p);
  tex_prep(t);
  if (!t) {
    char l[120];
    snprintf(l, sizeof(l), "theme Noel : image absente %s", p);
    log_line(l);
  }
  return t;
}

static void xm_load(void) {
  static int done;
  if (done) return;
  done = 1;
  for (int i = 0; i < 3; ++i) {
    char nm[24];
    snprintf(nm, sizeof(nm), "neige%d", i + 1);
    xm_snow[i][0] = xm_tex(nm);
    snprintf(nm, sizeof(nm), "neige%d_petite", i + 1);
    xm_snow[i][1] = xm_tex(nm);
  }
  xm_holly[0] = xm_tex("houx");
  xm_holly[1] = xm_tex("houx_inverse");
  xm_ico[0] = xm_tex("icone_sapin");
  xm_ico[1] = xm_tex("icone_cadeau");
  xm_ico[2] = xm_tex("icone_flocon");
}

static unsigned int xm_hash(const char *s) {
  unsigned int h = 0;
  while (s && *s) h = h * 31u + (unsigned char)*s++;
  return h;
}

/* Du 1er decembre au 6 janvier ; *year = annee de decembre. */
static int xmas_season(int *year) {
  SceDateTime d;
  if (sceRtcGetCurrentClockLocalTime(&d) < 0) return 0;
  if (year) *year = d.month == 1 ? d.year - 1 : d.year;
  return d.month == 12 || (d.month == 1 && d.day <= 6);
}

/* Neige posee sur le bord du haut d'un rectangle (x, y, largeur w) : bout gauche, motif du milieu
 * repete n fois, bout droit, a l'echelle qui tombe juste sur la largeur (rien n'est deforme) ;
 * n est choisi pour une hauteur proche de thick x 2,6. */
/* wref : largeur au repos (carte non agrandie) ; le nombre de motifs est choisi sur elle, pour que la neige
 * ne change pas de forme quand la carte grossit a la selection (elle est seulement agrandie). */
static void snow_cap_ref(float x, float y, float w, float wref, unsigned int seed, float thick, float a) {
  if (w < 6.0f || a <= 0.01f) return;
  if (wref <= 0.0f) wref = w;
  xm_load();
  int i = (int)(seed % 3u);
  float want = thick * 2.6f;
  vita2d_texture *tex = xm_snow[i][want > 34.0f ? 0 : 1]; /* thick fixe : meme image a toutes les tailles */
  if (!tex) tex = xm_snow[i][0];
  if (!tex) return;
  float iw = vita2d_texture_get_width(tex), ih = vita2d_texture_get_height(tex);
  float k = iw / xm_snow_src[i][0];
  int c1 = (int)(xm_snow_src[i][1] * k + 0.5f), c2 = (int)(xm_snow_src[i][2] * k + 0.5f);
  float ends = c1 + (iw - c2), seg = (float)(c2 - c1);
  float wt = w + 5.0f; /* deborde de 2 points a gauche, 3 a droite */
  float wr = wref + 5.0f;
  int n = 0;
  float best = 1e9f;
  for (int t = 0; t <= 12; ++t) {
    float d = fabsf(ih * wr / (ends + t * seg) - want);
    if (d < best) {
      best = d;
      n = t;
    }
  }
  float s = wt / (ends + n * seg);
  float x0 = x - 2.0f;
  float y0 = floorf(y - ih * s * 0.24f + 0.5f); /* le bord passe sous le haut de la couche */
  unsigned int col = fade(C(255, 255, 255, 255), a);
  float src = 0.0f;
  for (int p = 0; p < n + 2; ++p) {
    float sx = p == 0 ? 0.0f : p == n + 1 ? (float)c2 : (float)c1;
    float sw = p == 0 ? (float)c1 : p == n + 1 ? iw - c2 : seg;
    float l = floorf(x0 + src * s + 0.5f), r = floorf(x0 + (src + sw) * s + 0.5f);
    if (r > l) vita2d_draw_texture_tint_part_scale(tex, l, y0, sx, 0, sw, ih, (r - l) / sw, s, col);
    src += sw;
  }
}

static void snow_cap(float x, float y, float w, unsigned int seed, float thick, float a) {
  snow_cap_ref(x, y, w, w, seed, thick, a);
}

/* Branche de houx (2 feuilles, 3 baies) dans le cadre (x, y, w de large) ; rot en degres. */
static void holly_at(float x, float y, float w, int flip, float rot, float a) {
  xm_load();
  vita2d_texture *t = xm_holly[flip ? 1 : 0];
  if (!t || a <= 0.01f) return;
  float h = w * 56.0f / 74.0f, sc = w / vita2d_texture_get_width(t);
  vita2d_draw_texture_tint_scale_rotate(t, x + w / 2, y + h / 2, sc, sc, rot * 0.0174533f, fade(C(255, 255, 255, 255), a));
}

/* Icone du menu en theme Noel : 0 sapin (Accueil), 1 cadeau (Collections), 2 flocon (Homebrews). */
static int xmas_icon(int kind, float cx, float cy, float size, unsigned int c) {
  xm_load();
  vita2d_texture *t = kind >= 0 && kind < 3 ? xm_ico[kind] : NULL;
  if (!t) return 0;
  float sc = size / vita2d_texture_get_width(t);
  vita2d_draw_texture_tint_scale(t, cx - size / 2, cy - size / 2, sc, sc, c);
  return 1;
}

/* Petit generateur pseudo-aleatoire (meme graine, memes valeurs). */
static unsigned int xm_rs;
static float xm_rnd(void) {
  xm_rs = xm_rs * 1664525u + 1013904223u;
  return (float)(xm_rs >> 8) / 16777216.0f;
}

/* Disque plein ajoute a une liste de triangles (8 cotes). */
static int xm_disc(vita2d_color_vertex *v, float cx, float cy, float r, unsigned int c) {
  static const float co[9] = {1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0, 0.7071f, 1};
  static const float si[9] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f, 0};
  for (int k = 0; k < 8; ++k) {
    vtx_set(&v[k * 3], cx, cy, c);
    vtx_set(&v[k * 3 + 1], cx + co[k] * r, cy + si[k] * r, c);
    vtx_set(&v[k * 3 + 2], cx + co[k + 1] * r, cy + si[k + 1] * r, c);
  }
  return 24;
}

/* Neige qui tombe doucement sur tout l'ecran (derriere les menus et les cartes). dt en secondes. */
#define XM_FLAKES 90
static void snow_fall(float dt) {
  static float f[XM_FLAKES][5]; /* x, y (0..1), rayon, vitesse, derive */
  static int init;
  if (!init) {
    init = 1;
    xm_rs = 42;
    for (int i = 0; i < XM_FLAKES; ++i) {
      f[i][0] = xm_rnd();
      f[i][1] = xm_rnd();
      f[i][2] = 0.8f + xm_rnd() * 2.2f;
      f[i][3] = 0.25f + xm_rnd() * 0.7f;
      f[i][4] = xm_rnd() * 6.0f;
    }
  }
  float fr = dt * 60.0f; /* en images de 60 Hz */
  if (fr > 4.0f) fr = 4.0f;
  vita2d_color_vertex *v = vtx_alloc(XM_FLAKES * 24);
  if (!v) return;
  unsigned int c = C(255, 255, 255, 217);
  int n = 0;
  for (int i = 0; i < XM_FLAKES; ++i) {
    f[i][1] += f[i][3] * fr / 544.0f;
    f[i][4] += 0.01f * fr;
    f[i][0] += sinf(f[i][4]) * 0.3f * fr / 960.0f;
    if (f[i][1] > 1.01f) {
      f[i][1] = -0.01f;
      f[i][0] = fmodf(f[i][0] + 0.37f, 1.0f);
    }
    if (f[i][0] < 0.0f) f[i][0] += 1.0f;
    if (f[i][0] > 1.0f) f[i][0] -= 1.0f;
    n += xm_disc(v + n, f[i][0] * SCREEN_W, f[i][1] * 544.0f, f[i][2], c);
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, n);
}

/* Guirlande autour de la tuile (x, y, w, h) : le fil pend en festons le long du bord gauche, du bas et
 * du bord droit (le haut est sous la neige), ampoules colorees qui scintillent. */
#define XM_PTS 64
static void garland(float x, float y, float w, float h, float a) {
  if (a <= 0.01f) return;
  float px[XM_PTS], py[XM_PTS], nx[XM_PTS], ny[XM_PTS];
  int np = 0;
#define XM_ADD(X, Y, NX, NY)                                                                                       \
  if (np < XM_PTS) {                                                                                               \
    px[np] = (X);                                                                                                  \
    py[np] = (Y);                                                                                                  \
    nx[np] = (NX);                                                                                                 \
    ny[np] = (NY);                                                                                                 \
    ++np;                                                                                                          \
  }
  const float st = 40.0f;
  for (float yy = 16.0f; yy < h - 20.0f; yy += st) XM_ADD(0, yy, -1, 0)
  XM_ADD(5, h - 5, -0.7f, 0.7f)
  for (float xx = 22.0f; xx < w - 22.0f; xx += st) XM_ADD(xx, h, 0, 1)
  XM_ADD(w - 5, h - 5, 0.7f, 0.7f)
  for (float yy = h - 20.0f; yy > 16.0f; yy -= st) XM_ADD(w, yy, 1, 0)
  XM_ADD(w, 16, 1, 0)
#undef XM_ADD
  if (np < 2) return;
  /* fil : chaque feston est une courbe (8 morceaux) qui pend vers l'exterieur */
  const int SUB = 8;
  vita2d_color_vertex *v = vtx_alloc((np - 1) * SUB * 6);
  if (!v) return;
  unsigned int wc = fade(C(48, 66, 74, 255), a);
  int n = 0;
  for (int q = 1; q < np; ++q) {
    float mx = (nx[q - 1] + nx[q]) / 2, my = (ny[q - 1] + ny[q]) / 2;
    float cx = (px[q - 1] + px[q]) / 2 + mx * 7.0f, cy = (py[q - 1] + py[q]) / 2 + my * 7.0f;
    float lx = px[q - 1], ly = py[q - 1];
    for (int s = 1; s <= SUB; ++s) {
      float t = (float)s / SUB, u = 1 - t;
      float ex = u * u * px[q - 1] + 2 * u * t * cx + t * t * px[q], ey = u * u * py[q - 1] + 2 * u * t * cy + t * t * py[q];
      float dx = ex - lx, dy = ey - ly, len = sqrtf(dx * dx + dy * dy);
      if (len > 0.001f) {
        float ox = -dy / len * 0.6f, oy = dx / len * 0.6f;
        vtx_set(&v[n++], x + lx + ox, y + ly + oy, wc);
        vtx_set(&v[n++], x + lx - ox, y + ly - oy, wc);
        vtx_set(&v[n++], x + ex + ox, y + ey + oy, wc);
        vtx_set(&v[n++], x + ex + ox, y + ey + oy, wc);
        vtx_set(&v[n++], x + lx - ox, y + ly - oy, wc);
        vtx_set(&v[n++], x + ex - ox, y + ey - oy, wc);
      }
      lx = ex;
      ly = ey;
    }
  }
  if (n) vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, n);
  /* ampoules : au creux de chaque feston, nouveau tirage toutes les 0,6 s */
  static const unsigned int cols[4] = {C(217, 55, 75, 255), C(232, 185, 96, 255), C(63, 174, 106, 255), C(143, 208, 255, 255)};
  vita2d_color_vertex *b = vtx_alloc((np - 1) * 48);
  if (!b) return;
  xm_rs = (unsigned int)(g_time / 0.6f) * 2654435761u + 12345u;
  n = 0;
  for (int q = 1; q < np; ++q) {
    float mx = (nx[q - 1] + nx[q]) / 2, my = (ny[q - 1] + ny[q]) / 2;
    float bx = x + (px[q - 1] + px[q]) / 2 + mx * 3.5f, by = y + (py[q - 1] + py[q]) / 2 + my * 3.5f;
    unsigned int c = cols[(q - 1) % 4];
    int on = xm_rnd() >= 0.25f;
    if (on) n += xm_disc(b + n, bx, by, 5.5f, fade(c, a * 0.35f));
    n += xm_disc(b + n, bx, by, 2.8f, fade(c, a * (on ? 1.0f : 0.35f)));
  }
  if (n) vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, b, n);
}
