/* Area Play - Fond dynamique et outils de textures. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Fond dynamique : l'image du jeu selectionne, agrandie et assombrie
 * ====================================================================== */

/* Dessine seulement la zone visible de la texture (pas de clipping).
 * dx, dy : decalage en pixels d'ecran (fond flou : plusieurs copies decalees). */
static void draw_tex_backdrop_ex(const vita2d_texture *tex, float alpha, float dx, float dy) {
  if (alpha <= 0.01f || !tex) return;
  const int x0 = RAIL_W, y0 = 0, aw = SCREEN_W - RAIL_W, ah = FOOTER_Y;
  float tw = vita2d_texture_get_width(tex);
  float th = vita2d_texture_get_height(tex);
  float sc = (float)aw / tw;
  if (th * sc < (float)ah) sc = (float)ah / th;
  float dw = tw * sc, dh = th * sc;
  float sw = aw / sc, sh = ah / sc;
  float sx = (dw - aw) / 2 / sc, sy = (dh - ah) * 0.30f / sc;
  if (sw > tw) sw = tw;
  if (sh > th) sh = th;
  if (sx < 0) sx = 0;
  if (sy < 0) sy = 0;
  if (sx + sw > tw) sx = tw - sw;
  if (sy + sh > th) sy = th - sh;
  { /* mouvement lent : leger zoom avant/arriere et glissement lateral */
    float z = 1.0f + 0.05f * (0.5f + 0.5f * sinf(g_time * 0.16f));
    float nsw = sw / z, nsh = sh / z;
    sx += (sw - nsw) * (0.5f + 0.4f * sinf(g_time * 0.11f));
    sy += (sh - nsh) * 0.5f;
    sw = nsw;
    sh = nsh;
    sc = (float)aw / sw;
  }
  sx += dx / sc;
  sy += dy / sc;
  if (sx < 0) sx = 0;
  if (sy < 0) sy = 0;
  if (sx + sw > tw) sx = tw - sw;
  if (sy + sh > th) sy = th - sh;
  vita2d_draw_texture_tint_part_scale(tex, x0, y0, sx, sy, sw, sh, sc, sc,
                                      fade(C(255, 255, 255, T.backdrop_a), alpha));
}
static void draw_tex_backdrop(const vita2d_texture *tex, float alpha) { draw_tex_backdrop_ex(tex, alpha, 0, 0); }

/* Le fond est "pret" quand sa texture est chargee (ou s'il n'y en a pas besoin). */
static int backdrop_ready(const Game *g) { return !g->has_bg || g->bg != NULL; }

/* ======================================================================
 *  Textures : filtrage, liberation differee, reduction
 * ====================================================================== */

/* Filtrage lineaire (pas de scintillement quand l'image est agrandie, reduite ou deplacee)
 * et bords bloques (sinon le bord oppose de l'image "bave" sur une fine bande). */
static void tex_prep(vita2d_texture *t) {
  if (!t) return;
  vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
  sceGxmTextureSetUAddrMode(&t->gxm_tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
  sceGxmTextureSetVAddrMode(&t->gxm_tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
}

/* Une texture deja dessinee ne peut etre liberee qu'une fois le GPU termine :
 * on les regroupe et on attend le GPU une seule fois par image (au lieu d'une fois par texture). */
#define FREE_Q 96
static vita2d_texture *free_q[FREE_Q];
static int free_qn;
static void tex_free_flush(void) {
  if (!free_qn) return;
  vita2d_wait_rendering_done();
  for (int i = 0; i < free_qn; ++i) vita2d_free_texture(free_q[i]);
  free_qn = 0;
}
static void tex_free_later(vita2d_texture *t) {
  if (!t) return;
  if (free_qn >= FREE_Q) tex_free_flush();
  free_q[free_qn++] = t;
}

/* Seules les textures 32 bits (RGBA) sont lues pixel par pixel. */
static int tex_is_rgba(const vita2d_texture *t) {
  return t && vita2d_texture_get_format(t) == SCE_GXM_TEXTURE_FORMAT_A8B8G8R8 && vita2d_texture_get_datap(t);
}

/* Reduction par moyenne de blocs (chaque pixel source est lu une seule fois).
 * opaque = 1 : resultat sans transparence (fond flou). */
static void box_resample(const unsigned int *src, int sstride, int w, int h, unsigned int *dst, int dstride,
                         int dw, int dh, int opaque) {
  for (int by = 0; by < dh; ++by) {
    int y0 = by * h / dh, y1 = (by + 1) * h / dh;
    if (y1 <= y0) y1 = y0 + 1;
    for (int bx = 0; bx < dw; ++bx) {
      int x0 = bx * w / dw, x1 = (bx + 1) * w / dw;
      if (x1 <= x0) x1 = x0 + 1;
      unsigned int r = 0, g = 0, b = 0, a = 0, n = 0;
      for (int y = y0; y < y1 && y < h; ++y) {
        const unsigned int *row = src + y * sstride;
        for (int x = x0; x < x1 && x < w; ++x) {
          unsigned int c = row[x], al = c >> 24;
          r += (c & 0xFF) * al;
          g += ((c >> 8) & 0xFF) * al;
          b += ((c >> 16) & 0xFF) * al;
          a += al;
          ++n;
        }
      }
      unsigned int rr = a ? r / a : 0, gg = a ? g / a : 0, bb = a ? b / a : 0;
      unsigned int aa = opaque ? 255u : (n ? a / n : 0);
      dst[by * dstride + bx] = (aa << 24) | (bb << 16) | (gg << 8) | rr;
    }
  }
}

/* Jaquette trop grande pour l'ecran : on la reduit (memoire video divisee jusqu'a 4,
 * affichage plus net). La texture d'origine, jamais dessinee, est liberee. */
static vita2d_texture *tex_shrink(vita2d_texture *t, int maxw, int maxh) {
  if (!tex_is_rgba(t)) return t;
  int w = vita2d_texture_get_width(t), h = vita2d_texture_get_height(t);
  float s = (float)maxw / w;
  if ((float)maxh / h < s) s = (float)maxh / h;
  if (s >= 0.95f) return t;
  int nw = (int)(w * s + 0.5f), nh = (int)(h * s + 0.5f);
  if (nw < 1 || nh < 1) return t;
  vita2d_texture *n = vita2d_create_empty_texture(nw, nh);
  if (!n) return t;
  box_resample((const unsigned int *)vita2d_texture_get_datap(t), vita2d_texture_get_stride(t) / 4, w, h,
               (unsigned int *)vita2d_texture_get_datap(n), vita2d_texture_get_stride(n) / 4, nw, nh, 0);
  vita2d_free_texture(t);
  return n;
}

static void draw_backdrop(const Game *g, float alpha) {
  if (alpha <= 0.01f) return;
  ((Game *)g)->seen = g_frame;
  if (g->has_bg) { /* vrai fond (LiveArea, telecharge ou choisi) : tout l'ecran, sans zoom ni mouvement */
    if (g->bg) tex_cover(g->bg, 0, 0, SCREEN_W, 544, 0, fade(C(255, 255, 255, T.backdrop_a), alpha));
    return;
  }
  /* Pas de grand fond (LiveArea) : selon le reglage, rien, ou lavis de la couleur du jeu puis
   * son image (jaquette ou icone) agrandie, en un seul dessin. (4.4 : un flou en 9 dessins plein
   * ecran faisait tomber l'affichage a 30-40 images par seconde : retire.) */
  if (!opt_bd_icon) return;
  fill_rect(RAIL_W, 0, SCREEN_W - RAIL_W, FOOTER_Y, fade(with_alpha(g->color, 70), alpha));
  if (g->cover) draw_tex_backdrop(g->cover, alpha * 0.9f * g->appear);
}

/* Degrades qui fondent l'image dans le fond : lisibilite du texte. */
static void draw_backdrop_overlay(void) {
  unsigned int h[8], v[5];
  for (int i = 0; i < 8; ++i) { /* de gauche a droite : 190 -> 0 (courbe adoucie) */
    float t = (float)i / 7.0f;
    h[i] = with_alpha(T.bg, (int)(powf(1.0f - t, 1.4f) * 190.0f));
  }
  grad_rect(RAIL_W, 0, 700, FOOTER_Y, h, 8, 1);
  for (int i = 0; i < 5; ++i) /* de y = 250 a 350 : 0 -> 228 */
    v[i] = with_alpha(T.bg, (int)(powf((float)i / 4.0f, 1.2f) * 228.0f));
  grad_rect(RAIL_W, 250, SCREEN_W - RAIL_W, 100, v, 5, 0);
  fill_rect(RAIL_W, 350, SCREEN_W - RAIL_W, 544 - 350, with_alpha(T.bg, 228)); /* jusqu'en bas : pied de page raccord */
}

