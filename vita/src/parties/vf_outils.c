/* Area Play - Outils : couleurs, animation, texte, formes. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Outils : couleurs et animation
 * ====================================================================== */

/* Multiplie l'alpha d'une couleur par f (0..1). */
static unsigned int fade(unsigned int color, float f) {
  if (f < 0.f) f = 0.f;
  if (f > 1.f) f = 1.f;
  unsigned int a = (unsigned int)(((color >> 24) & 0xFF) * f);
  return (color & 0x00FFFFFF) | (a << 24);
}

/* Lissage exponentiel independant du framerate. */
static float approach(float cur, float target, float speed, float dt) {
  return cur + (target - cur) * (1.0f - expf(-speed * dt));
}

/* ======================================================================
 *  Outils : texte
 * ====================================================================== */

static vita2d_font *pick_font(int style, int size) {
#if !USE_TTF
  (void)style;
  (void)size;
  return NULL;
#else
  int best = 0, bd = 9999;
  for (int i = 0; i < FONT_SIZES; ++i) {
    int d = abs(size - font_sizes[i]);
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  vita2d_font *f = fonts[style == ST_SEMI ? 1 : 0][best];
  if (!f) f = fonts[0][best];
  if (!f) f = fonts[1][best];
  return f;
#endif
}

/* Le texte contient-il des caractères japonais (kana, kanji, pleine chasse) ? La police Inter
 * ne les a pas : ces textes passent par la police systeme de la Vita (latin + japonais). */
static int has_cjk(const char *s) {
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
      unsigned cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      if ((cp >= 0x3000 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFFEF)) return 1;
      p += 2;
    }
  return 0;
}

/* Texte simplifie pour la recherche : minuscules, accents retires (é -> e, ç -> c...),
 * ponctuation remplacee par des espaces ; les autres caracteres (japonais) sont gardes tels quels. */
static void text_fold(const char *in, char *out, int n) {
  static const char l1[65] = /* U+00C0 a U+00FF (64 lettres) */
      "aaaaaaaceeeeiiii" "dnooooo ouuuuyts" "aaaaaaaceeeeiiii" "dnooooo ouuuuyty";
  int o = 0;
  for (const unsigned char *p = (const unsigned char *)in; *p && o < n - 4;) {
    unsigned char c = *p;
    if (c < 0x80) {
      out[o++] = isalnum(c) ? (char)tolower(c) : ' ';
      ++p;
    } else if ((c & 0xE0) == 0xC0 && p[1]) {
      unsigned cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
      if (cp >= 0xC0 && cp <= 0xFF) out[o++] = l1[cp - 0xC0];
      else if (cp == 0x152 || cp == 0x153) out[o++] = 'o'; /* oe */
      else out[o++] = ' ';
      p += 2;
    } else { /* sequence plus longue : recopiee */
      int len = (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
      for (int k = 0; k < len && p[k] && o < n - 1; ++k) out[o++] = (char)p[k];
      p += len;
    }
  }
  out[o] = 0;
}

static int text_w(int style, int size, const char *s) {
  s = L(s); /* texte traduit si une autre langue est choisie (voir L) */
  vita2d_font *f = has_cjk(s) ? NULL : pick_font(style, size);
  if (f) return vita2d_font_text_width(f, size, s);
  /* Police systeme (PGF) : le "gras" est simule en dessinant deux fois. */
  return vita2d_pgf_text_width(g_pgf, size / 20.0f, s) + (style == ST_SEMI ? 1 : 0);
}

static void draw_text(int style, int x, int y, int size, unsigned int color,
                      const char *s) {
  s = L(s);
  vita2d_font *f = has_cjk(s) ? NULL : pick_font(style, size);
  if (f) {
    vita2d_font_draw_text(f, x, y, color, size, s);
  } else {
    vita2d_pgf_draw_text(g_pgf, x, y, color, size / 20.0f, s);
    if (style == ST_SEMI) vita2d_pgf_draw_text(g_pgf, x + 1, y, color, size / 20.0f, s);
  }
}

static void draw_text_right(int style, int right, int y, int size,
                            unsigned int color, const char *s) {
  draw_text(style, right - text_w(style, size, s), y, size, color, s);
}

static void draw_text_center(int style, int cx, int y, int size,
                             unsigned int color, const char *s) {
  draw_text(style, cx - text_w(style, size, s) / 2, y, size, color, s);
}

/* Copie s dans out ; si c'est trop large, coupe et ajoute "...". */
static void fit_text(int style, int size, const char *s, int maxw, char *out,
                     int outsz) {
  s = L(s);
  snprintf(out, outsz, "%s", s);
  if (text_w(style, size, out) <= maxw) return;
  int len = (int)strlen(out);
  char tmp[96];
  while (len > 1) {
    --len;
    while (len > 0 && (out[len] & 0xC0) == 0x80) --len; /* debut d'un caractere UTF-8 */
    if (len >= (int)sizeof(tmp) - 4) continue;
    memcpy(tmp, out, len);
    memcpy(tmp + len, "...", 4);
    if (text_w(style, size, tmp) <= maxw) {
      snprintf(out, outsz, "%s", tmp);
      return;
    }
  }
  snprintf(out, outsz, "...");
}

/* Titre : essaie chaque taille (de la plus grande a la plus petite),
 * puis coupe avec "..." si ca ne rentre toujours pas. */
static void draw_title_fit(int style, int x, int y, unsigned int color,
                           const char *s, int maxw, const int *sizes, int ns) {
  for (int i = 0; i < ns; ++i) {
    if (text_w(style, sizes[i], s) <= maxw) {
      draw_text(style, x, y, sizes[i], color, s);
      return;
    }
  }
  char buf[96];
  fit_text(style, sizes[ns - 1], s, maxw, buf, sizeof(buf));
  draw_text(style, x, y, sizes[ns - 1], color, buf);
}

/* Texte avec espacement des lettres. do_draw=0 : mesure seulement.
 * Retourne la position x de fin. Gere l'UTF-8 (accents). */
static int spaced(int style, int x, int y, int size, unsigned int color,
                  const char *s, int spacing, int do_draw) {
  s = L(s);
  char buf[5];
  while (*s) {
    int n = 1;
    unsigned char c = (unsigned char)*s;
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    int k = 0;
    while (k < n && s[k]) {
      buf[k] = s[k];
      ++k;
    }
    buf[k] = 0;
    s += k;
    if (buf[0] == ' ') {
      x += size / 3 + spacing;
      continue;
    }
    if (do_draw) draw_text(style, x, y, size, color, buf);
    x += text_w(style, size, buf) + spacing;
  }
  return x;
}

/* ======================================================================
 *  Outils : formes
 * ====================================================================== */

static void draw_frame(float x, float y, float w, float h, float t,
                       unsigned int color) {
  fill_rect(x, y, w, t, color);
  fill_rect(x, y + h - t, w, t, color);
  fill_rect(x, y + t, t, h - 2 * t, color);
  fill_rect(x + w - t, y + t, t, h - 2 * t, color);
}

/* Reflet "glossy" : degrade blanc translucide en haut d'une forme arrondie
 * (une seule bande de triangles qui suit l'arrondi des coins). */
static void gloss(float x, float y, float w, float h, float r, float frac,
                  int max_a, float alpha) {
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  float gh = h * frac;
  if (gh < 2.0f || max_a <= 0 || alpha <= 0.01f) return;
  float ys[12];
  int ny = 0;
  for (int i = 0; i <= 5 && ny < 8; ++i) { /* lignes dans l'arrondi des coins */
    float yy = r * (float)i / 5.0f;
    if (yy < gh) ys[ny++] = yy;
  }
  for (int k = 1; k <= 4; ++k) { /* puis le degrade jusqu'en bas du reflet */
    float yy = gh * (float)k / 4.0f;
    if (ny == 0 || yy > ys[ny - 1] + 0.5f) ys[ny++] = yy;
  }
  vita2d_color_vertex *v = vtx_alloc(2 * ny);
  if (!v) return;
  for (int i = 0; i < ny; ++i) {
    float yy = ys[i], inset = 0.0f;
    if (yy < r) {
      float dy = r - yy;
      inset = r - sqrtf(r * r - dy * dy);
    }
    float t = 1.0f - yy / gh;
    float in = yy < 3.0f ? yy / 3.0f : 1.0f; /* fondu sur les 3 premiers pixels : pas de barre claire collee au contour */
    unsigned int c = fade(C(255, 255, 255, (int)(max_a * t * t * in)), alpha);
    vtx_set(&v[2 * i], x + 1 + inset, y + 1 + yy, c);
    vtx_set(&v[2 * i + 1], x + w - 1 - inset, y + 1 + yy, c);
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 2 * ny);
  /* (plus de ligne blanche d'un coin a l'autre sous le contour : elle doublait le haut des cartes) */
}

/* Carte arrondie : fond + contour. */
static void round_card(float x, float y, float w, float h, float r,
                       unsigned int fill, unsigned int border, float alpha) {
  if (T.neon && r >= 10.0f && alpha > 0.05f) { /* contour lumineux (theme Nexa) */
    rr_band(x, y, w, h, r, 5.0f, fade(with_alpha(T.acc, 110), alpha), with_alpha(T.acc, 0));
    round_rect(x, y, w, h, r, fade(fill, alpha));
    round_outline_t(x, y, w, h, r, 1.5f, fade(T.acc_light, alpha));
    return;
  }
  round_rect(x, y, w, h, r, fade(fill, alpha));
  round_outline(x, y, w, h, r, fade(border, alpha));
}

/* Halo lumineux de la couleur d'accent autour d'une carte selectionnee. */
static void glow(float x, float y, float w, float h, float r, float f, float alpha) {
  rr_band(x, y, w, h, r, 9.0f, fade(with_alpha(T.acc, (int)(80 * f)), alpha), with_alpha(T.acc, 0));
}

/* Panneau des fenetres : voile sur l'ecran, ombre portee, carte et reflet. */
static void modal_panel(float x, float y, float w, float h, int dim) {
  fill_rect(0, 0, SCREEN_W, 544, with_alpha(T.bg, dim));
  soft_shadow(x, y + 10, w, h, 20, 26, C(0, 0, 0, T.light ? 50 : 120));
  round_card(x, y, w, h, 20, T.card, T.card_border, 1.0f);
  if (opt_gloss) gloss(x, y, w, h, 20, 0.2f, 20, 1.0f);
}

/* Triangle : dir > 0 pointe a droite, dir < 0 a gauche. */
static void draw_tri(float x, float cy, float w, float h, int dir,
                     unsigned int color) {
  for (int i = 0; i < (int)w; ++i) {
    float hh = dir > 0 ? h * (1.0f - (float)i / w) : h * ((float)(i + 1) / w);
    fill_rect(x + i, cy - hh / 2, 1, hh, color);
  }
}

static void glyph_cross(float x, float cy, unsigned int c) {
  vita2d_draw_line(x, cy - 4, x + 8, cy + 4, c);
  vita2d_draw_line(x + 1, cy - 4, x + 9, cy + 4, c);
  vita2d_draw_line(x + 8, cy - 4, x, cy + 4, c);
  vita2d_draw_line(x + 9, cy - 4, x + 1, cy + 4, c);
}

/* Bouton de manette dans un rond : b = 'X', 'O', 'S' (carre), 'T' (triangle). */
static void draw_btn(float cx, float cy, char b, unsigned int col) {
  {
    int bi = b == 'X' ? 0 : b == 'O' ? 1 : b == 'S' ? 2 : b == 'T' ? 3 : -1;
    vita2d_texture *bt = bi >= 0 ? ico_get(20 + bi) : NULL;
    if (bt) {
      float sc = 22.0f / vita2d_texture_get_height(bt);
      vita2d_draw_texture_tint_scale(bt, cx - vita2d_texture_get_width(bt) * sc / 2, cy - 11, sc, sc, col);
      return;
    }
  }
  vita2d_draw_fill_circle(cx, cy, 10.0f, with_alpha(col, 36));
  draw_ring(cx, cy, 10.0f, 1.3f, col);
  switch (b) {
  case 'X':
    vita2d_draw_line(cx - 3.5f, cy - 3.5f, cx + 3.5f, cy + 3.5f, col);
    vita2d_draw_line(cx - 3.5f, cy + 3.5f, cx + 3.5f, cy - 3.5f, col);
    vita2d_draw_line(cx - 2.5f, cy - 3.5f, cx + 4.5f, cy + 3.5f, col);
    vita2d_draw_line(cx - 2.5f, cy + 3.5f, cx + 4.5f, cy - 3.5f, col);
    break;
  case 'O': draw_ring(cx, cy, 4.2f, 1.3f, col); break;
  case 'S': draw_frame(cx - 3.8f, cy - 3.8f, 7.6f, 7.6f, 1.3f, col); break;
  case 'T':
    vita2d_draw_line(cx - 4.5f, cy + 3.5f, cx, cy - 4.5f, col);
    vita2d_draw_line(cx, cy - 4.5f, cx + 4.5f, cy + 3.5f, col);
    vita2d_draw_line(cx - 4.5f, cy + 3.5f, cx + 4.5f, cy + 3.5f, col);
    break;
  default: break;
  }
}
/* Aides de touches : "X Valider;O Annuler;T Details". Code : X O S T (boutons ronds), L R (gachettes), . (rien). */
static int draw_hints(int x, int y, int size, unsigned int col, const char *spec) {
  spec = L(spec);
  char item[80];
  const char *p = spec;
  while (*p) {
    int n = 0;
    while (*p && *p != ';' && n < (int)sizeof(item) - 1) item[n++] = *p++;
    item[n] = 0;
    if (*p == ';') ++p;
    if (n < 3) continue;
    char b = item[0];
    const char *label = item + 2;
    if (b == 'L' || b == 'R' || b == 'C') { /* C : gachettes L/R ensemble */
      const char *bt = b == 'L' ? "L" : b == 'R' ? "R" : "L/R";
      int bw = b == 'C' ? text_w(ST_SEMI, FS_SMALL, bt) + 14 : 28;
      round_outline(x, y - 17, bw, 20, 6, col);
      draw_text_center(ST_SEMI, x + bw / 2, y - 2, FS_SMALL, col, bt);
      x += bw + 8;
    } else if (b == 'E' || b == 'B') { /* touches SELECT (E) et START (B) : pastille avec leur nom */
      const char *bt = b == 'E' ? "SELECT" : "START";
      int sw = text_w(ST_REG, size, bt);
      round_outline_t(x, y - 20, sw + 14, 26, 13, 1.2f, col);
      draw_text(ST_REG, x + 7, y, size, col, bt);
      x += sw + 14 + 10;
    } else if (b != '.') {
      draw_btn(x + 10, y - 6, b, col);
      x += 26;
    }
    draw_text(ST_REG, x, y, size, col, label);
    x += text_w(ST_REG, size, label) + 26;
  }
  return x;
}

/* Interrupteur on/off. */
static void draw_switch(float x, float y, int on, float alpha) {
  round_rect(x, y, 46, 24, 12, fade(on ? T.acc : T.switch_off, alpha));
  vita2d_draw_fill_circle(on ? x + 34 : x + 12, y + 12, 9,
                          fade(C(255, 255, 255, 255), alpha));
}

