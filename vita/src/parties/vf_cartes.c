/* Area Play - Zones tactiles, statistiques, cartes et tuile. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Jaquettes, cartes et tuile principale
 * ====================================================================== */

/* Jaquette centree sur cxm, hauteur h, avec ombre. */

/* ---- Zones tactiles : chaque ecran declare ses zones, l'ecran tactile les relit ---- */
enum { H_ITEM = 1, H_RAIL, H_LAUNCH, H_INFO, H_BACK, H_QUIT, H_SEARCH, H_KEY, H_NET };
#define HIT_MAX 192 /* zones tactiles par image (le clavier a lui seul en a 60) */
static int rail_quit; /* 1 = le bouton Quitter a le focus (menu de gauche) */
static struct { float x, y, w, h; int type, idx; } hits[HIT_MAX];
static int hit_n;
static void hit_add(float x, float y, float w, float h, int type, int idx) {
  if (hit_n < HIT_MAX) {
    hits[hit_n].x = x; hits[hit_n].y = y; hits[hit_n].w = w; hits[hit_n].h = h;
    hits[hit_n].type = type; hits[hit_n].idx = idx;
    ++hit_n;
  }
}

/* Petit triangle indiquant qu'il reste du contenu. dir : 0 = gauche, 1 = droite, 2 = haut, 3 = bas. */
static void draw_chev(float cx, float cy, int dir, float alpha) {
  unsigned int col = fade(T.acc_text, alpha);
  const int w = 9, h = 14;
  if (dir < 2) {
    for (int i = 0; i < w; ++i) {
      float hh = dir == 1 ? h * (1.0f - (float)i / w) : h * ((float)(i + 1) / w);
      fill_rect(cx - w / 2 + i, cy - hh / 2, 1, hh, col);
    }
  } else {
    for (int i = 0; i < w; ++i) {
      float ww = dir == 3 ? h * (1.0f - (float)i / w) : h * ((float)(i + 1) / w);
      fill_rect(cx - ww / 2, cy - w / 2 + i, ww, 1, col);
    }
  }
}

/* ---- Statistiques de jeu (lancements, temps estime) ---- */
#define STATS_FILE DATA_DIR "/stats.bin"
#define SESSION_FILE DATA_DIR "/session.txt"
#define STAT_MAX MAX_TOTAL
typedef struct { char id[16]; unsigned launches, total, first; } Stat;
static Stat stats[STAT_MAX];
static int n_stats;
static void stats_load(void) {
  n_stats = 0;
  SceUID f = sceIoOpen(STATS_FILE, SCE_O_RDONLY, 0);
  if (f < 0) return;
  int n = 0;
  if (sceIoRead(f, &n, sizeof(n)) == (int)sizeof(n) && n > 0 && n <= STAT_MAX)
    n_stats = sceIoRead(f, stats, n * (int)sizeof(Stat)) / (int)sizeof(Stat);
  sceIoClose(f);
}
static void stats_save(void) {
  sceIoMkdir(DATA_DIR, 0777);
  SceUID f = sceIoOpen(STATS_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  sceIoWrite(f, &n_stats, sizeof(n_stats));
  if (n_stats > 0) sceIoWrite(f, stats, n_stats * (int)sizeof(Stat));
  sceIoClose(f);
}
static Stat *stat_find(const char *id, int create) {
  for (int i = 0; i < n_stats; ++i)
    if (strcmp(stats[i].id, id) == 0) return &stats[i];
  if (!create || n_stats >= STAT_MAX) return NULL;
  Stat *st = &stats[n_stats++];
  memset(st, 0, sizeof(*st));
  snprintf(st->id, sizeof(st->id), "%s", id);
  return st;
}
/* Appele avant de lancer un jeu : on note l'heure de depart. */
static void session_begin(const char *id) {
  Stat *st = stat_find(id, 1);
  if (st) {
    ++st->launches;
    if (!st->first) st->first = (unsigned)time(NULL);
    stats_save();
  }
  char line[48];
  int l = snprintf(line, sizeof(line), "%s|%u\n", id, (unsigned)time(NULL));
  SceUID f = sceIoOpen(SESSION_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f >= 0) {
    sceIoWrite(f, line, l);
    sceIoClose(f);
  }
}
/* Lancement refuse : on annule la session commencee. */
static void session_cancel(const char *id) {
  Stat *st = stat_find(id, 0);
  if (st && st->launches > 0) {
    --st->launches;
    stats_save();
  }
  sceIoRemove(SESSION_FILE);
}
/* Au demarrage : si une session etait en cours, on ajoute sa duree (plafonnee a 6 h). */
static void session_end(void) {
  char buf[48];
  memset(buf, 0, sizeof(buf));
  SceUID f = sceIoOpen(SESSION_FILE, SCE_O_RDONLY, 0);
  if (f < 0) return;
  sceIoRead(f, buf, sizeof(buf) - 1);
  sceIoClose(f);
  sceIoRemove(SESSION_FILE);
  char *bar = strchr(buf, '|');
  if (!bar) return;
  *bar = 0;
  long long start = atoll(bar + 1), now = (long long)time(NULL);
  if (start <= 0 || now <= start) return;
  long long d = now - start;
  if (d > 6 * 3600) d = 6 * 3600;
  Stat *st = stat_find(buf, 1);
  if (st) {
    st->total += (unsigned)d;
    stats_save();
  }
}
static void fmt_duration(unsigned sec, char *out, int n) {
  if (sec < 60) snprintf(out, n, "< 1 min");
  else if (sec < 3600) snprintf(out, n, "%u min", sec / 60);
  else snprintf(out, n, "%u h %02u min", sec / 3600, (sec % 3600) / 60);
}
/* Decalage heure locale - UTC (fuseau et heure d'ete reglees sur la console), arrondi au quart d'heure. */
static long long local_offset(void) {
  static int done;
  static long long off;
  if (!done) {
    SceDateTime d;
    done = 1;
    if (sceRtcGetCurrentClockLocalTime(&d) >= 0) {
      struct tm lt;
      memset(&lt, 0, sizeof(lt));
      lt.tm_year = d.year - 1900;
      lt.tm_mon = d.month - 1;
      lt.tm_mday = d.day;
      lt.tm_hour = d.hour;
      lt.tm_min = d.minute;
      lt.tm_sec = d.second;
      long long diff = (long long)mktime(&lt) - (long long)time(NULL); /* mktime sans fuseau = UTC */
      off = (diff >= 0 ? diff + 450 : diff - 450) / 900 * 900;
    }
  }
  return off;
}

static void fmt_date(unsigned t, char *out, int n) {
  if (!t) {
    snprintf(out, n, "-");
    return;
  }
  time_t tt = (time_t)((long long)t + local_offset());
  struct tm *tmv = gmtime(&tt);
  if (!tmv) snprintf(out, n, "-");
  else snprintf(out, n, "%02d/%02d/%04d", tmv->tm_mday, tmv->tm_mon + 1, tmv->tm_year + 1900);
}

/* ---- Coeur (favori) : texture blanche, teintee a l'affichage ---- */
static vita2d_texture *heart_tex, *heart_out_tex;
static vita2d_texture *heart_make(int outline) {
  vita2d_texture *t = vita2d_create_empty_texture(32, 32);
  if (!t) return NULL;
  unsigned int *px = (unsigned int *)vita2d_texture_get_datap(t);
  int stride = vita2d_texture_get_stride(t) / 4;
  for (int j = 0; j < 32; ++j)
    for (int i = 0; i < 32; ++i) {
      int hit = 0, in = 0;
      for (int sj = 0; sj < 4; ++sj)
        for (int si = 0; si < 4; ++si) {
          float x = (i + (si + 0.5f) / 4.0f - 16.0f) / 12.0f;
          float y = (16.0f - (j + (sj + 0.5f) / 4.0f)) / 12.0f + 0.15f;
          float q = x * x + y * y - 1.0f;
          if (q * q * q - x * x * y * y * y <= 0.0f) ++hit;
          float xi = x / 0.74f, yi = (y - 0.2f) / 0.74f + 0.2f; /* coeur reduit : l'interieur du contour */
          float qi = xi * xi + yi * yi - 1.0f;
          if (qi * qi * qi - xi * xi * yi * yi * yi <= 0.0f) ++in;
        }
      int cov = outline ? (hit > in ? hit - in : 0) : hit;
      px[j * stride + i] = ((unsigned int)(cov * 255 / 16) << 24) | 0x00FFFFFFu;
    }
  return t;
}
static int draw_ico_file(int id, float cx, float cy, float box, unsigned int c);
static void draw_heart_ex(float cx, float cy, float size, unsigned int color, int outline) {
  /* favori.png / favori_vide.png s'ils existent, sinon le coeur dessine par le code */
  if (draw_ico_file(outline ? ICO_FAV_EMPTY : ICO_FAV, cx, cy, size, color)) return;
  vita2d_texture **tp = outline ? &heart_out_tex : &heart_tex;
  if (!*tp) *tp = heart_make(outline);
  if (!*tp) return;
  float sc = size / 32.0f;
  vita2d_draw_texture_tint_scale(*tp, cx - size / 2, cy - size / 2, sc, sc, color);
}
static void draw_heart(float cx, float cy, float size, unsigned int color) {
  draw_heart_ex(cx, cy, size, color, 0);
}

/* Jaquette en attente d'image : degrade de la couleur du jeu et initiale du titre. */
static void draw_thumb_placeholder(const Game *g, float cxm, float y, float h, float alpha) {
  float iw = h * 0.81f, ih = h;
  float rad = h * 0.06f;
  if (rad < 5.0f) rad = 5.0f;
  float x = cxm - iw / 2;
  soft_shadow(x + 2, y + 7, iw - 4, ih - 4, rad, 10, fade(C(0, 0, 0, T.light ? 60 : 120), alpha));
  unsigned int c0 = fade(mix(g->color, C(255, 255, 255, 255), 0.18f), alpha * 0.95f);
  unsigned int c1 = fade(mix(g->color, C(0, 0, 0, 255), 0.35f), alpha * 0.95f);
  round_rect(x, y, iw, ih, rad, c1);
  grad2(x, y + rad, iw, ih - 2 * rad, c0, c1, 0);
  round_rect(x, y, iw, rad * 2, rad, c0);
  fill_rect(x, y + rad, iw, 1, c0);
  char ini[2] = {(char)name_letter(g), 0};
  draw_text_center(ST_SEMI, (int)(x + iw / 2), (int)(y + ih / 2 + 14), FS_HERO,
                   fade(C(255, 255, 255, 200), alpha), ini);
}

static void draw_thumb_base(const Game *g, float cxm, float y, float wmax, float h,
                       float alpha) {
  ((Game *)g)->seen = g_frame; /* dessine : son image reste en memoire (ou est demandee) */
  float iw = h * 0.81f, ih = h;
  float rad = h * 0.06f; /* arrondi des jaquettes, proportionnel a leur taille */
  if (rad < 5.0f) rad = 5.0f;
  if (g->cover && g->appear < 0.999f) { /* image qui vient d'arriver : fondu depuis l'attente */
    draw_thumb_placeholder(g, cxm, y, h, alpha * (1.0f - g->appear));
    alpha *= g->appear;
  }
  unsigned int shadow = fade(C(0, 0, 0, T.light ? 60 : 120), alpha);
  if (g->cover) {
    float tw = vita2d_texture_get_width(g->cover);
    float th = vita2d_texture_get_height(g->cover);
    if (tw > th * 1.3f) { /* image paysage (icone PSP 144x80) : cadre carre, image centree */
      float side = h < wmax ? h : wmax;
      float x = cxm - side / 2;
      soft_shadow(x + 2, y + 7, side - 4, side - 4, rad, 10, shadow);
      round_rect(x, y, side, side, rad, fade(mix(g->color, C(0, 0, 0, 255), 0.6f), alpha));
      float ih2 = th * (side / tw);
      tex_round_full(g->cover, x, y + (side - ih2) / 2, side, ih2, 3, fade(C(255, 255, 255, 255), alpha));
      return;
    }
    float sc = h / th;
    iw = tw * sc;
    if (iw > wmax) {
      sc = wmax / tw;
      iw = wmax;
    }
    ih = th * sc;
    float x = cxm - iw / 2, yy = y + (h - ih) / 2;
    soft_shadow(x + 2, yy + 7, iw - 4, ih - 4, rad, 10, shadow);
    tex_round_full(g->cover, x, yy, iw, ih, rad, fade(C(255, 255, 255, 255), alpha));
    return;
  }
  draw_thumb_placeholder(g, cxm, y, h, alpha);
}

static void draw_thumb(const Game *g, float cxm, float y, float wmax, float h, float alpha) {
  draw_thumb_base(g, cxm, y, wmax, h, alpha);
}

/* Rectangle reellement occupe par la jaquette dessinee par draw_thumb (meme calcul). */
static void thumb_rect(const Game *g, float cxm, float y, float wmax, float h, float *rx, float *ry, float *rw, float *rh) {
  float iw = h * 0.81f, ih = h;
  if (g->cover) {
    float tw = vita2d_texture_get_width(g->cover), th = vita2d_texture_get_height(g->cover);
    if (tw > th * 1.3f) { /* icone paysage : cadre carre */
      float side = h < wmax ? h : wmax;
      *rx = cxm - side / 2;
      *ry = y;
      *rw = *rh = side;
      return;
    }
    float sc = h / th;
    iw = tw * sc;
    if (iw > wmax) {
      sc = wmax / tw;
      iw = wmax;
    }
    ih = th * sc;
  }
  *rx = cxm - iw / 2;
  *ry = y + (h - ih) / 2;
  *rw = iw;
  *rh = ih;
}

/* Theme Noel : jaquette avec la neige posee dessus ; holly = houx sur le coin haut gauche (1) ou droit (2). */
/* scale : agrandissement de la carte (1 au repos) ; la neige garde sa forme. */
static void draw_thumb_xmas(const Game *g, float cxm, float y, float wmax, float h, float alpha, int snow, int holly,
                            float scale) {
  draw_thumb(g, cxm, y, wmax, h, alpha);
  if (!xmas_on() || (!snow && !holly)) return;
  float rx, ry, rw, rh;
  thumb_rect(g, cxm, y, wmax, h, &rx, &ry, &rw, &rh);
  if (snow) snow_cap_ref(rx, ry, rw, rw / scale, xm_hash(g->id), 9.0f, alpha);
  if (holly == 1) holly_at(rx - 18, ry - 20, 50, 0, -10, alpha);
  if (holly == 2) holly_at(rx + rw - 32, ry - 20, 50, 1, 10, alpha);
}

/* Carte "poster" : jaquette + titre dessous. Hauteur de carte = cov_h + 60.
 * f: 0 = repos, 1 = selectionnee. */
static void draw_card(const Game *g, float x, float y, float w, float cov_h,
                      float f, int show_detail) {
  float alpha = g_pa;
  float h = cov_h + 60;
  float s = 1.0f + 0.04f * f + 0.014f * sinf(f * 3.14159f);
  float dw = w * s, dh = h * s;
  float cx = x - (dw - w) / 2.0f;
  float cy = y - (dh - h) / 2.0f - 3.0f * f;

  if (f > 0.02f) {
    glow(cx, cy, dw, dh, 16, f, alpha);
    round_card(cx, cy, dw, dh, 16, T.sel_fill, T.acc, alpha * f);
    round_outline_t(cx, cy, dw, dh, 16, 2.0f, fade(T.acc, alpha * f));
    if (opt_gloss) gloss(cx, cy, dw, dh, 16, 0.38f, 26, alpha * f);
  }

  draw_thumb_xmas(g, cx + dw / 2, cy + 10 * s, dw - 24 * s, cov_h * s,
                  alpha * (0.72f + 0.28f * f), 1, 0, s);

  float ty = cy + (10 + cov_h + 22) * s;
  char buf[96];
  fit_text(ST_SEMI, FS_BODY, g->name, (int)dw - 12, buf, sizeof(buf));
  draw_text_center(ST_SEMI, (int)(cx + dw / 2), (int)ty, FS_BODY,
                   fade(T.text, alpha * (0.8f + 0.2f * f)), buf);
  if (show_detail && opt_show_id) {
    fit_text(ST_REG, FS_SMALL, g->detail, (int)dw - 12, buf, sizeof(buf));
    int dwid = spaced(ST_REG, 0, 0, FS_SMALL, 0, buf, 1, 0) - 1;
    spaced(ST_REG, (int)(cx + (dw - dwid) / 2), (int)(ty + 19 * s), FS_SMALL,
           fade(T.muted, alpha), buf, 1, 1);
    if (g->fav) draw_heart(cx + (dw - dwid) / 2 - 12, ty + 14 * s, 13, fade(T.acc, alpha));
  } else if (g->fav) {
    draw_heart(cx + dw / 2, ty + 15 * s, 14, fade(T.acc, alpha));
  }
}

/* "L / R   POUR PARCOURIR" aligne a droite sur la ligne de section. */
static void section_hint(int y, const char *keys, const char *label) {
  int w = spaced(ST_REG, 0, 0, FS_SMALL, 0, label, SPACING, 0) - SPACING;
  int x = CONTENT_R + g_ox - w;
  spaced(ST_REG, x, y, FS_SMALL, fade(T.muted, g_pa), label, SPACING, 1);
  int kw = spaced(ST_REG, 0, 0, FS_SMALL, 0, keys, SPACING, 0) - SPACING;
  spaced(ST_REG, x - 12 - kw, y, FS_SMALL, fade(T.text_soft, g_pa), keys,
         SPACING, 1);
}

/* Bouton principal (Lancer) : degrade de la couleur d'accent + leger halo. */
/* Comme Android : face (hauteur h - 3) posee sur une ombre de la couleur sombre decalee de 3 vers le bas
 * (bas du bouton a y + h + 3) ; le halo suit exactement ce contour, sans vide en dessous. */
static void primary_button(float x, float y, float w, float h, float pa) {
  if (xmas_on()) glow(x, y, w, h + 3, h / 2, 1.0f, pa); /* theme Noel : bouton Lancer lumineux */
  rr_band(x, y, w, h + 3, h / 2, 8.0f, fade(with_alpha(T.acc, 70), pa), with_alpha(T.acc, 0));
  round_rect(x, y + 3, w, h, h / 2, fade(T.acc_dark, pa));
  round_rect(x, y, w, h - 3, h / 2, fade(T.acc, pa));
  if (opt_gloss) gloss(x, y, w, h, h / 2, 0.5f, 40, pa);
}

/* Tuile du jeu en cours : texte a gauche, jaquette a droite. */
/* Bouton "Détails" a cote de "Lancer" (triangle = touche de la manette). */
static void draw_info_button(float x, float y, float w, float pa) {
  round_card(x, y, w, 38, 19, with_alpha(T.card, 210), T.card_border, pa);
  if (opt_gloss && !T.neon) gloss(x, y, w, 38, 19, 0.3f, 34, pa); /* Nexa : contour lumineux seul */
  unsigned int c = fade(T.text, pa);
  draw_btn(x + 22, y + 19, 'T', c);
  draw_text(ST_SEMI, (int)x + 40, (int)y + 25, FS_BODY, c, "Détails");
}

static void draw_hero_tile(const Game *hero, int is_home, const char *button) {
  float pa = g_pa, ha = g_pa * g_ha;
  float x = CONTENT_X + g_ox, y = TILE_Y;
  unsigned int fill = opt_backdrop || wall_path[0] ? T.glass : T.card;
  soft_shadow(x + 4, y + 10, TILE_W - 8, TILE_H - 8, 18, 22, fade(C(0, 0, 0, T.light ? 36 : 90), pa));
  round_rect(x, y, TILE_W, TILE_H, 18, fade(fill, pa));
  if (opt_gloss) gloss(x, y, TILE_W, TILE_H, 18, 0.42f, 30, pa);
  round_outline(x, y, TILE_W, TILE_H, 18,
                fade(opt_gloss ? T.glass_border : T.card_border, pa));
  if (xmas_on()) { /* theme Noel : guirlande autour de la tuile, neige sur le haut */
    garland(x, y, TILE_W, TILE_H, pa);
    snow_cap(x + 10, y, TILE_W - 28, 7, 20.0f, pa);
  }

  int tx0 = (int)x + 28;

  /* Largeur de la jaquette (a droite) : le titre doit rester a gauche. */
  float ch = 164.0f, cy = TILE_Y + 16, cw = 133.0f;
  int wide = hero->cover &&
             vita2d_texture_get_width(hero->cover) > vita2d_texture_get_height(hero->cover) * 1.3f;
  if (hero->cover) {
    cw = vita2d_texture_get_width(hero->cover) * (ch / vita2d_texture_get_height(hero->cover));
    if (wide) cw = ch; /* icone paysage : cadre carre comme les jeux Vita */
  }
  float cx = CONTENT_R - 28 - cw + g_ox;

  const char *label = is_home ? (hero->last_played > 0 ? "REPRENDRE" : "BIBLIOTHÈQUE")
                              : (opt_show_id ? hero->detail : kind_label(hero));
  spaced(ST_REG, tx0, 100, FS_SMALL, fade(xmas_on() ? C(232, 185, 96, 255) : T.acc_text, ha), label, SPACING + 1, 1);

  static const int title_sizes[3] = {FS_HERO, FS_TITLE, FS_SECTION};
  draw_title_fit(ST_SEMI, tx0, 148, fade(T.text, ha), hero->name,
                 (int)(cx - g_ox) - tx0 - 24, title_sizes, 3);

  unsigned int sub = fade(T.text_soft, ha);
  if (hero->last_played > 0) {
    char ago[40];
    ago_text(hero->last_played, ago, sizeof(ago));
    int tx = tx0;
    draw_text(ST_REG, tx, 178, FS_BODY, sub, "Dernière partie");
    tx += text_w(ST_REG, FS_BODY, "Dernière partie") + 12;
    vita2d_draw_fill_circle(tx, 172, 2, sub);
    tx += 12;
    draw_text(ST_REG, tx, 178, FS_BODY, sub, ago);
  } else {
    draw_text(ST_REG, tx0, 178, FS_BODY, sub, "Jamais lancé");
  }

  primary_button(tx0, 196, 172, 38, pa);
  draw_btn(tx0 + 24, 215, 'X', fade(T.btn_text, pa));
  draw_text(ST_SEMI, tx0 + 44, 221, FS_BODY, fade(T.btn_text, pa), button);
  hit_add(tx0 - g_ox, 196, 172, 38, H_LAUNCH, 0);
  draw_info_button(tx0 + 184, 196, 112, pa);
  hit_add(tx0 + 184 - g_ox, 196, 112, 38, H_INFO, 0);

  draw_thumb_xmas(hero, cx + cw / 2, cy, cw, ch, ha, 0, 1, 1.0f);
  if (xmas_on()) holly_at(tx0 - 16, 170, 52, 0, -12, pa); /* sur le bouton Lancer */
}

