/* Area Play - Theme, formes vectorielles, fond d ecran. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Theme (sombre / clair + couleur d'accent)
 * ====================================================================== */

typedef struct {
  unsigned int bg, text, text_soft, muted, line, bar, bar_text, bar_line;
  unsigned int card, card_border, glass, glass_border, sel_fill, switch_off;
  unsigned int acc, acc_light, acc_dark, acc_text, btn_text;
  int backdrop_a;
  int light;
  int has_grad, neon;
  unsigned int grad_top, grad_bot;
} Theme;
static Theme T;

static float lum(unsigned int c) {
  return 0.299f * (c & 0xFF) + 0.587f * ((c >> 8) & 0xFF) +
         0.114f * ((c >> 16) & 0xFF);
}

static unsigned int mix(unsigned int a, unsigned int b, float t) {
  int ar = a & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF;
  int br = b & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF;
  return C((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t),
           (int)(ab + (bb - ab) * t), 255);
}

static unsigned int with_alpha(unsigned int c, int a) {
  if (a < 0) a = 0;
  if (a > 255) a = 255;
  return (c & 0x00FFFFFF) | ((unsigned int)a << 24);
}

/* Themes : sombre, clair, puis des variantes sombres teintees (fond en degrade). */
static const struct {
  const char *name;
  int accent; /* accent propose en choisissant ce theme (-1 = inchange) */
  unsigned int bg, text, text_soft, muted, line, bar, card, card_border, glass, switch_off;
  unsigned int grad_top, grad_bot; /* 0 = fond uni */
} themes[] = {
    {"Sombre", -1, C(14, 16, 24, 255), C(244, 243, 250, 255), C(190, 194, 212, 255), C(150, 154, 176, 255),
     C(34, 36, 50, 255), C(10, 11, 18, 218), C(22, 23, 37, 255), C(48, 49, 70, 255), C(14, 16, 30, 168), C(64, 66, 86, 255), 0, 0},
    {"Clair", -1, C(238, 240, 246, 255), C(24, 27, 40, 255), C(70, 75, 98, 255), C(112, 117, 140, 255),
     C(206, 210, 222, 255), C(255, 255, 255, 225), C(255, 255, 255, 255), C(208, 212, 226, 255), C(255, 255, 255, 170), C(190, 194, 210, 255), 0, 0},
    {"Nexa", 1, C(4, 10, 28, 255), C(240, 246, 255, 255), C(176, 196, 232, 255), C(122, 146, 190, 255),
     C(22, 44, 92, 255), C(4, 12, 34, 218), C(10, 24, 58, 205), C(46, 96, 190, 255), C(8, 20, 52, 168), C(40, 62, 108, 255),
     C(10, 34, 92, 255), C(2, 6, 20, 255)},
    {"Braise", 6, C(20, 10, 10, 255), C(250, 242, 236, 255), C(214, 190, 178, 255), C(166, 140, 128, 255),
     C(60, 30, 26, 255), C(16, 7, 7, 218), C(36, 18, 16, 255), C(84, 44, 36, 255), C(26, 12, 10, 168), C(90, 56, 48, 255),
     C(52, 22, 16, 255), C(14, 6, 6, 255)},
    {"Forêt", 9, C(8, 18, 14, 255), C(238, 248, 242, 255), C(180, 206, 192, 255), C(130, 158, 144, 255),
     C(26, 54, 42, 255), C(6, 14, 11, 218), C(14, 32, 26, 255), C(36, 76, 60, 255), C(10, 24, 18, 168), C(52, 84, 70, 255),
     C(14, 44, 34, 255), C(4, 11, 8, 255)},
    {"Noël", 4, C(18, 42, 66, 255), C(245, 249, 252, 255), C(196, 214, 228, 255), C(140, 166, 186, 255),
     C(44, 72, 98, 255), C(12, 28, 46, 190), C(24, 52, 76, 205), C(76, 112, 140, 255), C(22, 48, 72, 165), C(60, 86, 106, 255),
     C(26, 58, 98, 255), C(16, 44, 52, 255)}};
#define THEME_COUNT LEN(themes)
#define XMAS_THEME 5 /* theme Noel (vf_noel.c) */
#define XMAS_WALL "app0:assets/xmas/hiver1.jpg" /* fond d'hiver du theme Noel, sans fond d'ecran choisi */

static vita2d_texture *theme_bg;
static int theme_bg_idx = -1;
static void apply_theme(void) {
  T.light = opt_theme == 1;
  {
    const typeof(themes[0]) *th = &themes[opt_theme];
    T.bg = th->bg;
    T.text = th->text;
    T.text_soft = th->text_soft;
    T.muted = th->muted;
    T.line = th->line;
    T.bar = th->bar;
    T.bar_text = th->text_soft;
    T.bar_line = T.light ? C(0, 0, 0, 30) : C(255, 255, 255, 28);
    T.card = th->card;
    T.card_border = th->card_border;
    T.glass = th->glass;
    T.glass_border = T.light ? C(150, 158, 185, 110) : C(255, 255, 255, 58);
    T.switch_off = th->switch_off;
    T.backdrop_a = T.light ? 105 : 125;
    T.neon = strcmp(th->name, "Nexa") == 0; /* contours lumineux + fond abstrait */
    if (T.neon) T.backdrop_a = 80; /* le fond abstrait reste visible */
    if ((theme_bg_idx == opt_theme && theme_bg) || wall_path[0] || opt_theme == XMAS_THEME) T.backdrop_a = T.backdrop_a * 3 / 4; /* image de fond visible */
    T.has_grad = th->grad_top != 0;
    T.grad_top = th->grad_top;
    T.grad_bot = th->grad_bot;
  }

  /* Couleur d'accent, corrigee si elle manque de contraste sur le fond. */
  unsigned int base = accents[opt_accent].color;
  float l = lum(base);
  unsigned int acc = base;
  if (!T.light && l < 70.0f) {
    acc = mix(base, C(255, 255, 255, 255), 0.45f);
  } else if (T.light && l > 170.0f) {
    float t = (l - 170.0f) / 100.0f;
    if (t > 0.5f) t = 0.5f;
    acc = mix(base, C(0, 0, 0, 255), t);
  }
  T.acc = acc;
  T.acc_light = mix(acc, C(255, 255, 255, 255), 0.28f);
  T.acc_dark = mix(acc, C(0, 0, 0, 255), 0.28f);
  T.acc_text = T.light ? T.acc_dark : T.acc_light;
  T.btn_text = lum(acc) > 160.0f ? C(20, 22, 30, 255) : C(255, 255, 255, 255);
  T.sel_fill = with_alpha(mix(T.card, acc, 0.18f), 232);
  vita2d_set_clear_color(T.bg);
}

/* ======================================================================
 *  Rectangles securises
 * ====================================================================== */

/* Ignore les tailles nulles ou negatives (quads degeneres = risque GPU). */
static void fill_rect(float x, float y, float w, float h, unsigned int color) {
  if (w <= 0.0f || h <= 0.0f || !(color >> 24)) return;
  vita2d_draw_rectangle(x, y, w, h, color);
}

/* ======================================================================
 *  Formes vectorielles : une seule commande GPU par forme
 *  (les sommets sont pris dans la reserve de vita2d, liberee a chaque image)
 * ====================================================================== */

static vita2d_color_vertex *vtx_alloc(int n) {
  return (vita2d_color_vertex *)vita2d_pool_memalign((unsigned)n * sizeof(vita2d_color_vertex),
                                                     sizeof(vita2d_color_vertex));
}
static void vtx_set(vita2d_color_vertex *v, float x, float y, unsigned int c) {
  v->x = x;
  v->y = y;
  v->z = 0.5f;
  v->color = c;
}

/* Nombre de segments par coin (plus le rayon est grand, plus le coin est lisse). */
static int rr_seg(float r) { return r < 3.0f ? 1 : r < 8.0f ? 3 : r < 16.0f ? 5 : 7; }

/* Quart de cercle unite, calcule une fois par nombre de segments. */
static const float *rr_arc(int seg) {
  static float tab[8][8 * 2 + 2];
  static unsigned char done[8];
  if (!done[seg]) {
    for (int i = 0; i <= seg; ++i) {
      float a = 1.5707963f * (float)i / (float)seg;
      tab[seg][i * 2] = cosf(a);
      tab[seg][i * 2 + 1] = sinf(a);
    }
    done[seg] = 1;
  }
  return tab[seg];
}

/* Contour d'un rectangle arrondi (sens horaire, depuis le coin haut-gauche). Retourne le nombre de points. */
#define RR_MAXP 32
static int rr_path(float x, float y, float w, float h, float r, int seg, float *px, float *py) {
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r < 0) r = 0;
  const float *a = rr_arc(seg);
  /* centres des coins et signes : haut-gauche, haut-droit, bas-droit, bas-gauche */
  const float ccx[4] = {x + r, x + w - r, x + w - r, x + r}, ccy[4] = {y + r, y + r, y + h - r, y + h - r};
  int n = 0;
  for (int c = 0; c < 4; ++c)
    for (int i = 0; i <= seg; ++i) {
      float co = a[i * 2], si = a[i * 2 + 1], dx, dy;
      switch (c) { /* chaque coin parcourt son quart de cercle dans le sens horaire */
      case 0: dx = -co; dy = -si; break; /* gauche -> haut   */
      case 1: dx = si; dy = -co; break;  /* haut -> droite   */
      case 2: dx = co; dy = si; break;   /* droite -> bas    */
      default: dx = -si; dy = co; break; /* bas -> gauche    */
      }
      px[n] = ccx[c] + dx * r;
      py[n] = ccy[c] + dy * r;
      ++n;
    }
  return n;
}

/* Rectangle arrondi plein. */
static void round_rect(float x, float y, float w, float h, float r, unsigned int color) {
  if (w <= 0.0f || h <= 0.0f || !(color >> 24)) return;
  if (r < 0.75f) {
    fill_rect(x, y, w, h, color);
    return;
  }
  float px[RR_MAXP], py[RR_MAXP];
  int n = rr_path(x, y, w, h, r, rr_seg(r), px, py);
  vita2d_color_vertex *v = vtx_alloc(n + 2);
  if (!v) return;
  vtx_set(&v[0], x + w / 2, y + h / 2, color);
  for (int i = 0; i <= n; ++i) vtx_set(&v[i + 1], px[i % n], py[i % n], color);
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_FAN, v, n + 2);
}

/* Bande entre le contour (x,y,w,h,r) et ce meme contour decale de d
 * (d > 0 : vers l'exterieur, d < 0 : vers l'interieur). Les couleurs se fondent de c_in a c_out. */
static void rr_band(float x, float y, float w, float h, float r, float d, unsigned int c_in,
                    unsigned int c_out) {
  if (w <= 0.0f || h <= 0.0f || (!(c_in >> 24) && !(c_out >> 24))) return;
  float ax[RR_MAXP], ay[RR_MAXP], bx[RR_MAXP], by[RR_MAXP];
  int seg = rr_seg(d > 0 ? r + d : r);
  int n = rr_path(x, y, w, h, r, seg, ax, ay);
  rr_path(x - d, y - d, w + 2 * d, h + 2 * d, r + d, seg, bx, by);
  vita2d_color_vertex *v = vtx_alloc(2 * (n + 1));
  if (!v) return;
  for (int i = 0; i <= n; ++i) {
    vtx_set(&v[2 * i], ax[i % n], ay[i % n], c_in);
    vtx_set(&v[2 * i + 1], bx[i % n], by[i % n], c_out);
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 2 * (n + 1));
}

/* Contour arrondi (sans toucher a l'interieur : ok en translucide). */
static void round_outline_t(float x, float y, float w, float h, float r, float t, unsigned int color) {
  rr_band(x, y, w, h, r, -t, color, color);
}
static void round_outline(float x, float y, float w, float h, float r, unsigned int color) {
  round_outline_t(x, y, w, h, r, 1.0f, color);
}

/* Ombre douce : forme pleine + bord qui s'estompe sur "spread" pixels. */
static void soft_shadow(float x, float y, float w, float h, float r, float spread, unsigned int color) {
  round_rect(x, y, w, h, r, color);
  rr_band(x, y, w, h, r, spread, color, color & 0x00FFFFFFu);
}

/* Degrade a plusieurs etapes reparties regulierement (vertical, ou horizontal si horiz). */
static void grad_rect(float x, float y, float w, float h, const unsigned int *cols, int n, int horiz) {
  if (w <= 0.0f || h <= 0.0f || n < 2) return;
  vita2d_color_vertex *v = vtx_alloc(2 * n);
  if (!v) return;
  for (int i = 0; i < n; ++i) {
    float t = (float)i / (float)(n - 1);
    if (horiz) {
      vtx_set(&v[2 * i], x + w * t, y, cols[i]);
      vtx_set(&v[2 * i + 1], x + w * t, y + h, cols[i]);
    } else {
      vtx_set(&v[2 * i], x, y + h * t, cols[i]);
      vtx_set(&v[2 * i + 1], x + w, y + h * t, cols[i]);
    }
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 2 * n);
}
static void grad2(float x, float y, float w, float h, unsigned int c0, unsigned int c1, int horiz) {
  unsigned int c[2] = {c0, c1};
  grad_rect(x, y, w, h, c, 2, horiz);
}

/* Arc de cercle d'epaisseur t (angles en radians, 0 = droite, sens horaire a l'ecran). */
static void draw_arc(float cx, float cy, float r, float t, float a0, float a1, unsigned int color) {
  int n = (int)((a1 - a0) * r / 2.5f);
  if (n < 8) n = 8;
  if (n > 72) n = 72;
  vita2d_color_vertex *v = vtx_alloc(2 * (n + 1));
  if (!v) return;
  for (int i = 0; i <= n; ++i) {
    float a = a0 + (a1 - a0) * (float)i / (float)n, co = cosf(a), si = sinf(a);
    vtx_set(&v[2 * i], cx + co * r, cy + si * r, color);
    vtx_set(&v[2 * i + 1], cx + co * (r - t), cy + si * (r - t), color);
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 2 * (n + 1));
}
static void draw_ring(float cx, float cy, float r, float t, unsigned int color) {
  draw_arc(cx, cy, r, t, 0.0f, 6.2831853f, color);
}

/* Texture dans un rectangle arrondi. (u0,v0)-(u1,v1) : partie de l'image utilisee (0..1). */
static void tex_round(const vita2d_texture *tex, float x, float y, float w, float h, float r,
                      float u0, float v0, float u1, float v1, unsigned int tint) {
  if (!tex || w <= 0.0f || h <= 0.0f || !(tint >> 24)) return;
  float px[RR_MAXP], py[RR_MAXP];
  int n = rr_path(x, y, w, h, r, rr_seg(r), px, py);
  vita2d_texture_vertex *v = (vita2d_texture_vertex *)vita2d_pool_memalign(
      (unsigned)(n + 2) * sizeof(vita2d_texture_vertex), sizeof(vita2d_texture_vertex));
  if (!v) return;
  for (int i = -1; i <= n; ++i) {
    float vx = i < 0 ? x + w / 2 : px[i % n], vy = i < 0 ? y + h / 2 : py[i % n];
    vita2d_texture_vertex *q = &v[i + 1];
    q->x = vx;
    q->y = vy;
    q->z = 0.5f;
    q->u = u0 + (u1 - u0) * (vx - x) / w;
    q->v = v0 + (v1 - v0) * (vy - y) / h;
  }
  vita2d_draw_array_textured(tex, SCE_GXM_PRIMITIVE_TRIANGLE_FAN, v, n + 2, tint);
}

/* Image entiere, arrondie, etiree sur (x,y,w,h). */
static void tex_round_full(const vita2d_texture *tex, float x, float y, float w, float h, float r,
                           unsigned int tint) {
  tex_round(tex, x, y, w, h, r, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

/* Image qui remplit (x,y,w,h) sans deformation : les bords qui depassent sont coupes. */
static void tex_cover(const vita2d_texture *tex, float x, float y, float w, float h, float r,
                      unsigned int tint) {
  if (!tex) return;
  float tw = vita2d_texture_get_width(tex), th = vita2d_texture_get_height(tex);
  if (tw <= 0 || th <= 0) return;
  float ta = tw / th, da = w / h, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  if (ta > da) { /* image plus large : on coupe a gauche et a droite */
    float k = da / ta;
    u0 = (1 - k) / 2;
    u1 = u0 + k;
  } else {
    float k = ta / da;
    v0 = (1 - k) / 2;
    v1 = v0 + k;
  }
  tex_round(tex, x, y, w, h, r, u0, v0, u1, v1, tint);
}

/* Fond en degrade vertical (themes teintes). */
static vita2d_texture *load_image_file(const char *path);
static void tex_prep(vita2d_texture *t);
static unsigned int fade(unsigned int color, float f);
static int read_file(const char *path, unsigned char *buf, int max);
static void log_line(const char *msg);
static vita2d_texture *ico_get(int id);
static const char *theme_file[THEME_COUNT] = {"sombre", "clair", "nexa", "braise", "foret", "noel"}; /* nom des images de fond */
static void wall_free(void) {
  if (wall_tex) {
    vita2d_wait_rendering_done();
    vita2d_free_texture(wall_tex);
    wall_tex = NULL;
  }
  wall_loaded = 0;
}

/* Fond d'ecran choisi : image etiree sur tout l'ecran (sans deformation), puis voile du theme. */
static int draw_wallpaper(float a) {
  /* theme Noel sans fond d'ecran choisi : le fond d'hiver */
  const char *wp = wall_path[0] ? wall_path : opt_theme == XMAS_THEME ? XMAS_WALL : NULL;
  if (!wp) return 0;
  static char loaded_path[sizeof(wall_path)];
  if (wall_loaded && strcmp(loaded_path, wp) != 0) wall_free(); /* passage du fond d'hiver au fond choisi */
  if (!wall_loaded) {
    wall_loaded = 1;
    snprintf(loaded_path, sizeof(loaded_path), "%s", wp);
    wall_tex = load_image_file(wp);
    if (wall_tex) tex_prep(wall_tex);
    else {
      char ml[160];
      snprintf(ml, sizeof(ml), "fond d'ecran illisible : %s", wp);
      log_line(ml);
    }
  }
  if (!wall_tex) return 0;
  tex_cover(wall_tex, 0, 0, SCREEN_W, 544, 0, fade(C(255, 255, 255, 255), a));
  static const int dim_a[3] = {40, 105, 165};
  fill_rect(0, 0, SCREEN_W, 544, fade(with_alpha(T.bg, dim_a[opt_wall_dim]), a));
  if (strncmp(wp, "app0:assets/xmas/", 17) == 0) { /* fond d'hiver : la neige du bas est tres claire, assombrie pour les titres */
    grad2(0, 544 * 0.45f, SCREEN_W, 544 * 0.30f, with_alpha(T.bg, 0), fade(with_alpha(T.bg, 140), a), 0);
    grad2(0, 544 * 0.75f, SCREEN_W, 544 * 0.25f + 1, fade(with_alpha(T.bg, 140), a), fade(with_alpha(T.bg, 179), a), 0);
  }
  return 1;
}

/* Fond de l'ecran (fond d'ecran, image du theme ou degrade). a < 1 : par-dessus ce qui est deja
 * dessine (fondu entre l'ecran de demarrage et le menu). */
static void draw_bg_layer(float a) {
  if (draw_wallpaper(a)) return;
  if (theme_bg_idx != opt_theme) { /* image de fond du theme : <nom>.jpg ou .png */
    if (theme_bg) {
      vita2d_wait_rendering_done();
      vita2d_free_texture(theme_bg);
      theme_bg = NULL;
    }
    theme_bg_idx = opt_theme;
    static const char *pat[] = {DATA_DIR "/themes/%s.jpg", DATA_DIR "/themes/%s.png",
                                "app0:assets/themes/%s.jpg", "app0:assets/themes/%s.png",
                                "app0:assets/themes/%s.jpeg", NULL};
    for (int c = 0; pat[c] && !theme_bg; ++c) {
      char pth[120];
      snprintf(pth, sizeof(pth), pat[c], theme_file[opt_theme]);
      theme_bg = load_image_file(pth);
    }
    tex_prep(theme_bg);
    char ml[96];
    snprintf(ml, sizeof(ml), "theme %s : image de fond %s", theme_file[opt_theme], theme_bg ? "chargee" : "absente");
    log_line(ml);
  }
  if (theme_bg) {
    float sx = (float)SCREEN_W / vita2d_texture_get_width(theme_bg), sy = 544.0f / vita2d_texture_get_height(theme_bg);
    vita2d_draw_texture_tint_scale(theme_bg, 0, 0, sx, sy, fade(C(255, 255, 255, 255), a));
    return;
  }
  if (T.has_grad) {
    grad2(0, 0, SCREEN_W, 544, fade(T.grad_top, a), fade(T.grad_bot, a), 0);
    return;
  }
  /* themes unis : leger halo de la couleur d'accent en haut a droite, plus moderne qu'un aplat */
  grad2(0, 0, SCREEN_W, 544, fade(mix(T.bg, T.acc, T.light ? 0.06f : 0.09f), a), fade(T.bg, a), 0);
}
static void draw_bg_gradient(void) { draw_bg_layer(1.0f); }

