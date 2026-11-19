/*
 * Area Play Updater (ARPL00002) : installe la mise a jour d'Area Play.
 *
 * Une appli ne peut pas remplacer ses propres fichiers pendant qu'elle tourne (la Vita verrouille son dossier).
 * Area Play telecharge donc la mise a jour et la verifie, ecrit ux0:data/AreaPlay/update/pret.txt, lance
 * cette appli et se ferme. L'Updater :
 *   1. affiche l'ecran de demarrage d'Area Play (meme fond, logo anime, titre, barre) avec les infos de la
 *      mise a jour : couleurs et fond du theme donnes par pret.txt ;
 *   2. reverifie chaque fichier (taille + CRC32), le copie dans ux0:app/ARPL00001 (fichier.tmp puis renommage,
 *      eboot.bin en dernier) ;
 *   3. efface pret.txt et relance Area Play.
 * Erreur : rien d'autre n'est touche, le message s'affiche, puis Area Play (ancienne version) est relancee.
 * Lancee a la main sans mise a jour en attente : "Aucune mise a jour a installer", puis Area Play.
 *
 * pret.txt (ecrit par Area Play, lignes "cle=valeur") :
 *   version=1.2
 *   couleurs=<fond> <texte> <texte discret> <contour> <accent>  (couleurs vita2d, hexadecimal)
 *   degrade=<haut> <bas>                                        (fond sans image)
 *   fond=ux0:data/AreaPlay/wallpapers/x.png                     (facultatif : image par-dessus le degrade)
 *   voile=105                                                   (voile de la couleur de fond sur l'image)
 *   file=eboot.bin|1048964|1922e08b                            (chemin | taille | CRC32), fichier dans update/
 */
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>
#include <zlib.h>
#include <math.h>

#define AP_ID "ARPL00001"
#define AP_APP "ux0:app/" AP_ID
#define AP_DATA "ux0:data/AreaPlay"
#define UPD_DIR AP_DATA "/update"
#define READY UPD_DIR "/pret.txt"
#define RUNNING UPD_DIR "/pret_en_cours.txt" /* renomme au demarrage : un plantage ne relance pas en boucle */
#define MAX_FILES 48

static struct {
  char path[64];
  int size;
  unsigned int crc;
} files[MAX_FILES];
static int nfiles;
static char version[16];
static char wall[160];
static int veil;
static float start; /* "suite=0.55" : Area Play a deja fait cette part de la barre, ecran deja affiche */
/* couleurs (vita2d : 0xAABBGGRR) ; sans pret.txt : theme sombre et image de la LiveArea d'Area Play */
static unsigned int c_bg = 0xFF1E1614, c_text = 0xFFF5F0EE, c_muted = 0xFFA0948E, c_border = 0xFF3A2E2A,
                    c_acc = 0xFFBF536C, c_top = 0xFF2A1E1C, c_bot = 0xFF1E1614;

static vita2d_font *f_reg, *f_semi;
static vita2d_texture *wall_tex;

static void log_line(const char *msg) {
  SceUID f = sceIoOpen(AP_DATA "/log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  if (f < 0) return;
  sceIoWrite(f, "updater : ", 10);
  sceIoWrite(f, msg, strlen(msg));
  sceIoWrite(f, "\r\n", 2);
  sceIoClose(f);
}

static int read_all(const char *path, unsigned char *buf, int max) {
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return f;
  int n = 0;
  while (n < max) {
    int r = sceIoRead(f, buf + n, max - n);
    if (r <= 0) break;
    n += r;
  }
  sceIoClose(f);
  return n;
}

static int write_all(const char *path, const unsigned char *b, int n, int *err) {
  SceUID f = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) {
    *err = f;
    return 0;
  }
  int w = 0;
  while (w < n) {
    int r = sceIoWrite(f, b + w, n - w);
    if (r <= 0) {
      *err = r;
      break;
    }
    w += r;
  }
  sceIoClose(f);
  return w == n;
}

/* Cree les dossiers d'un chemin de fichier (ux0:app/ID/a/b/f -> .../a, .../a/b). */
static void mkdirs(const char *full) {
  char d[200];
  snprintf(d, sizeof(d), "%s", full);
  for (char *p = d + 6; *p; ++p)
    if (*p == '/') {
      *p = 0;
      sceIoMkdir(d, 0777);
      *p = '/';
    }
}

/* Lit pret.txt. 1 si une version et au moins un fichier. */
static int parse_ready(void) {
  static char txt[8192];
  int n = read_all(RUNNING, (unsigned char *)txt, sizeof(txt) - 1);
  if (n <= 0) return 0;
  txt[n] = 0;
  for (char *line = strtok(txt, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char *v = eq + 1;
    if (!strcmp(line, "version")) snprintf(version, sizeof(version), "%s", v);
    else if (!strcmp(line, "fond")) snprintf(wall, sizeof(wall), "%s", v);
    else if (!strcmp(line, "voile")) veil = atoi(v);
    else if (!strcmp(line, "couleurs")) sscanf(v, "%x %x %x %x %x", &c_bg, &c_text, &c_muted, &c_border, &c_acc);
    else if (!strcmp(line, "degrade")) sscanf(v, "%x %x", &c_top, &c_bot);
    else if (!strcmp(line, "suite")) start = (float)atof(v);
    else if (!strcmp(line, "file") && nfiles < MAX_FILES) {
      char p[64];
      int sz = 0;
      unsigned int crc = 0;
      if (sscanf(v, "%63[^|]|%d|%x", p, &sz, &crc) == 3 && sz > 0 && sz <= (16 << 20) && !strstr(p, "..") && p[0] != '/') {
        snprintf(files[nfiles].path, sizeof(files[0].path), "%s", p);
        files[nfiles].size = sz;
        files[nfiles].crc = crc;
        ++nfiles;
      }
    }
  }
  if (veil < 0) veil = 0;
  if (veil > 255) veil = 255;
  return version[0] && nfiles > 0;
}

static unsigned int with_alpha(unsigned int c, int a) { return (c & 0x00FFFFFF) | ((unsigned int)a << 24); }
static unsigned int fade(unsigned int c, float a) {
  if (a >= 1.0f) return c;
  if (a <= 0.0f) return c & 0x00FFFFFF;
  return with_alpha(c, (int)(((c >> 24) & 0xFF) * a));
}

static void round_rect(float x, float y, float w, float h, float r, unsigned int c) {
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  vita2d_draw_rectangle(x + r, y, w - 2 * r, h, c);
  vita2d_draw_rectangle(x, y + r, r, h - 2 * r, c);
  vita2d_draw_rectangle(x + w - r, y + r, r, h - 2 * r, c);
  vita2d_draw_fill_circle(x + r, y + r, r, c);
  vita2d_draw_fill_circle(x + w - r, y + r, r, c);
  vita2d_draw_fill_circle(x + r, y + h - r, r, c);
  vita2d_draw_fill_circle(x + w - r, y + h - r, r, c);
}

/* Degrade vertical (haut -> bas) sur tout l'ecran. */
static void grad_screen(unsigned int top, unsigned int bot) {
  vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v) return;
  float xs[4] = {0, 960, 0, 960}, ys[4] = {0, 0, 544, 544};
  for (int i = 0; i < 4; ++i) {
    v[i].x = xs[i];
    v[i].y = ys[i];
    v[i].z = 0.5f;
    v[i].color = i < 2 ? top : bot;
  }
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
}

/* Texte avec espacement des lettres (comme le titre de l'ecran de demarrage d'Area Play). */
static int spaced(vita2d_font *f, int x, int y, int size, unsigned int c, const char *s, int sp, int draw) {
  char b[5];
  while (*s) {
    int n = 1;
    unsigned char ch = (unsigned char)*s;
    if (ch >= 0xF0) n = 4;
    else if (ch >= 0xE0) n = 3;
    else if (ch >= 0xC0) n = 2;
    int k = 0;
    while (k < n && s[k]) b[k] = s[k], ++k;
    b[k] = 0;
    s += k;
    if (b[0] == ' ') {
      x += size / 3 + sp;
      continue;
    }
    if (draw) vita2d_font_draw_text(f, x, y, c, size, b);
    x += vita2d_font_text_width(f, size, b) + sp;
  }
  return x;
}
static void text_center(vita2d_font *f, int cx, int y, int size, unsigned int c, const char *t) {
  int w = vita2d_font_text_width(f, size, t);
  vita2d_font_draw_text(f, cx - w / 2, y, c, size, t);
}

/* ---- Logo anime : le A a facettes d'Area Play (meme dessin et meme animation que son ecran de demarrage) ---- */
static float anim_k(float t, float t0, float dur) {
  float k = (t - t0) / dur;
  if (k <= 0.0f) return 0.0f;
  if (k >= 1.0f) return 1.0f;
  return 1.0f - (1.0f - k) * (1.0f - k);
}
#define LOGO_S 1.8f
#define LX(x) (480.0f + ((x) - 64.0f) * LOGO_S)
#define LY(y) (166.0f + ((y) - 62.0f) * LOGO_S)
static void vset(vita2d_color_vertex *v, float x, float y, unsigned int c) {
  v->x = x;
  v->y = y;
  v->z = 0.5f;
  v->color = c;
}
static void logo_tri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned int c) {
  vita2d_color_vertex *v = vita2d_pool_memalign(3 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
  if (!v) return;
  vset(&v[0], LX(x0), LY(y0), c);
  vset(&v[1], LX(x1), LY(y1), c);
  vset(&v[2], LX(x2), LY(y2), c);
  vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, v, 3);
}
static void logo_edge(float x0, float y0, float x1, float y1, float p, unsigned int c) {
  if (p <= 0.0f) return;
  float ax = LX(x0), ay = LY(y0);
  float bx = ax + (LX(x1) - ax) * p, by = ay + (LY(y1) - ay) * p;
  float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
  const float hw = 1.8f;
  if (l > 0.01f) {
    float nx = -dy / l * hw, ny = dx / l * hw;
    vita2d_color_vertex *v = vita2d_pool_memalign(4 * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
    if (v) {
      vset(&v[0], ax + nx, ay + ny, c);
      vset(&v[1], ax - nx, ay - ny, c);
      vset(&v[2], bx + nx, by + ny, c);
      vset(&v[3], bx - nx, by - ny, c);
      vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4);
    }
  }
  vita2d_draw_fill_circle(ax, ay, hw, c);
  vita2d_draw_fill_circle(bx, by, hw, c);
}
static void splash_logo(float t) {
  float f1 = anim_k(t, 0.75f, 0.25f), f2 = anim_k(t, 0.82f, 0.25f), f3 = anim_k(t, 0.89f, 0.25f);
  if (f1 > 0) logo_tri(64, 20, 64, 64, 30, 104, fade(RGBA8(58, 139, 255, 255), f1));
  if (f2 > 0) logo_tri(64, 20, 98, 104, 64, 64, fade(RGBA8(95, 224, 255, 255), f2));
  if (f3 > 0) {
    unsigned int c = fade(RGBA8(29, 95, 208, 255), f3);
    logo_tri(30, 104, 64, 64, 64, 86, c);
    logo_tri(30, 104, 64, 86, 48, 104, c);
    logo_tri(64, 64, 98, 104, 64, 86, c);
    logo_tri(64, 86, 98, 104, 80, 104, c);
  }
  unsigned int e = RGBA8(232, 251, 255, 255);
  float kb = anim_k(t, 0.0f, 0.18f);
  logo_edge(30, 104, 48, 104, kb, e);
  logo_edge(98, 104, 80, 104, kb, e);
  float kn = anim_k(t, 0.18f, 0.25f);
  logo_edge(48, 104, 64, 86, kn, e);
  logo_edge(80, 104, 64, 86, kn, e);
  float kv = anim_k(t, 0.18f, 0.40f);
  logo_edge(30, 104, 64, 64, kv, e);
  logo_edge(98, 104, 64, 64, kv, e);
  float ko = anim_k(t, 0.18f, 0.55f);
  logo_edge(30, 104, 64, 20, ko, e);
  logo_edge(98, 104, 64, 20, ko, e);
  logo_edge(64, 64, 64, 20, anim_k(t, 0.55f, 0.25f), e);
}

/* L'ecran d'Area Play au demarrage (fond du theme, logo, titre, barre), avec les infos de la mise a jour :
 * msg sous la barre, line en dessous (plus discret). frac < 0 : barre qui glisse (attente). */
static unsigned long long t0;
static float shown;
static int trace = 1; /* premier affichage : chaque etape au journal */
#define STEP(s) \
  do { \
    if (trace) log_line("ecran : " s); \
  } while (0)
static void draw_screen(const char *msg, float frac, const char *line) {
  unsigned long long now = sceKernelGetProcessTimeWide();
  if (!t0) { /* le logo se redessine (animation de l'ecran de demarrage) ; la barre reprend ou Area Play l'a laissee */
    t0 = now;
    shown = start;
  }
  float t = (now - t0) / 1000000.0f;
  if (frac >= 0.0f) frac = start + (1.0f - start) * frac; /* la barre continue la ou Area Play l'a laissee */
  if (frac >= 0.0f) shown += (frac - shown) * 0.18f;
  vita2d_start_drawing();
  vita2d_set_clear_color(c_bg);
  vita2d_clear_screen();
  STEP("debut");
  grad_screen(c_top, c_bot);
  STEP("degrade");
  if (wall_tex) {
    vita2d_draw_texture_scale(wall_tex, 0, 0, 960.0f / vita2d_texture_get_width(wall_tex),
                              544.0f / vita2d_texture_get_height(wall_tex));
    if (veil > 0) vita2d_draw_rectangle(0, 0, 960, 544, with_alpha(c_bg, veil));
  }
  STEP("fond");
  splash_logo(t);
  STEP("logo");
  float ta = anim_k(t, 0.80f, 0.30f), te = anim_k(t, 0.90f, 0.30f);
  if (ta > 0) {
    int bx = spaced(f_semi, 0, 0, 42, 0, "AREA PLAY", 8, 0) - 8;
    spaced(f_semi, 480 - bx / 2, 296, 42, fade(c_text, ta), "AREA PLAY", 8, 1);
  }
  if (te > 0) {
    int ew = spaced(f_reg, 0, 0, 17, 0, "VITA EDITION", 4, 0) - 4;
    spaced(f_reg, 480 - ew / 2, 324, 17, fade(c_muted, te), "VITA EDITION", 4, 1);
  }
  float bw = 340, bx = 480 - bw / 2, by = 352;
  STEP("titre");
  round_rect(bx, by, bw, 8, 4, c_border);
  if (frac >= 0.0f) {
    float fw = bw * (shown > 1.0f ? 1.0f : shown);
    if (fw > 8) round_rect(bx, by, fw, 8, 4, c_acc);
  } else {
    float ph = fmodf(t * 0.9f, 1.0f), seg = 110;
    round_rect(bx + (bw - seg) * (0.5f - 0.5f * cosf(ph * 6.2831853f)), by, seg, 8, 4, c_acc);
  }
  { /* message qui defile : le precedent monte et s'efface, le nouveau arrive par le bas */
    static char cur[128], prev[128];
    static float k = 1.0f;
    if (msg && strcmp(cur, msg) != 0) {
      snprintf(prev, sizeof(prev), "%s", start > 0.0f && !cur[0] ? "Préparation de l'installation..." : cur);
      snprintf(cur, sizeof(cur), "%s", msg);
      k = 0.0f;
    }
    if (k < 1.0f) k += 3.0f / 60.0f;
    if (k > 1.0f) k = 1.0f;
    float e = k * k * (3.0f - 2.0f * k);
    if (prev[0] && e < 1.0f) text_center(f_reg, 480, (int)(392 - 18 * e), 17, fade(c_text, te * (1.0f - e)), prev);
    if (cur[0]) text_center(f_reg, 480, (int)(392 + 18 * (1.0f - e)), 17, fade(c_text, te * e), cur);
  }
  if (line && line[0]) text_center(f_reg, 480, 418, 15, fade(c_muted, te), line);
  STEP("textes");
  vita2d_end_drawing();
  STEP("fin de l'image");
  vita2d_swap_buffers();
  STEP("image affichee");
  trace = 0;
}

/* Montre l'ecran pendant n images (60 = 1 s). */
static void hold(const char *msg, float frac, const char *line, int frames) {
  for (int i = 0; i < frames; ++i) draw_screen(msg, frac, line);
}

static void relaunch(void) {
  sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=" AP_ID);
  sceKernelDelayThread(10000);
  sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=" AP_ID);
}

static char title_msg[64]; /* "Installation de la mise a jour 1.2" */

/* Installe les fichiers. 1 si tout est en place ; sinon msg explique. */
static int install(char *msg, int n) {
  /* 1) verification de tous les fichiers recus (rien n'est encore remplace) */
  for (int i = 0; i < nfiles; ++i) {
    char src[200];
    snprintf(src, sizeof(src), UPD_DIR "/%s", files[i].path);
    unsigned char *b = malloc(files[i].size + 1);
    int len = b ? read_all(src, b, files[i].size + 1) : -1;
    unsigned int crc = len > 0 ? (unsigned int)crc32(0L, b, (uInt)len) : 0;
    free(b);
    draw_screen(title_msg, 0.15f * (i + 1) / nfiles, "Vérification des fichiers...");
    if (len != files[i].size || crc != files[i].crc) {
      snprintf(msg, n, "fichier abîmé : %s (%d octets)", files[i].path, len);
      return 0;
    }
  }
  /* 2) copie : fichier.tmp puis renommage ; eboot.bin en dernier */
  int done = 0;
  for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < nfiles; ++i) {
      if ((strcmp(files[i].path, "eboot.bin") == 0) != pass) continue;
      char src[200], dst[200], tmp[210];
      snprintf(src, sizeof(src), UPD_DIR "/%s", files[i].path);
      snprintf(dst, sizeof(dst), AP_APP "/%s", files[i].path);
      snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
      draw_screen(title_msg, 0.15f + 0.85f * done / nfiles, "Ne pas éteindre la console pendant l'installation.");
      unsigned char *b = malloc(files[i].size);
      int len = b ? read_all(src, b, files[i].size) : -1, err = 0;
      mkdirs(dst);
      if (len != files[i].size || !write_all(tmp, b, len, &err)) {
        free(b);
        sceIoRemove(tmp);
        snprintf(msg, n, "copie impossible : %s (%08X)", files[i].path, (unsigned)err);
        return 0;
      }
      free(b);
      sceIoRemove(dst);
      int rn = sceIoRename(tmp, dst);
      if (rn < 0) {
        snprintf(msg, n, "renommage impossible : %s (%08X)", files[i].path, (unsigned)rn);
        return 0;
      }
      ++done;
    }
  return 1;
}

/* Efface les fichiers recus et pret.txt (dossier update vide pour la prochaine fois). */
static void cleanup(void) {
  for (int i = 0; i < nfiles; ++i) {
    char src[200];
    snprintf(src, sizeof(src), UPD_DIR "/%s", files[i].path);
    sceIoRemove(src);
  }
  sceIoRemove(RUNNING);
}

int main(void) {
  log_line("demarrage");
  vita2d_init_advanced(4 * 1024 * 1024); /* comme Area Play */
  vita2d_set_vblank_wait(1);
  f_reg = vita2d_load_font_file("app0:assets/fonts/Inter-Regular.ttf");
  f_semi = vita2d_load_font_file("app0:assets/fonts/Inter-SemiBold.ttf");
  if (!f_semi) f_semi = f_reg;

  sceIoRemove(RUNNING);
  sceIoRename(READY, RUNNING);
  int ready = parse_ready();
  {
    char l[220];
    snprintf(l, sizeof(l), "polices %s %s, fond %s", f_reg ? "ok" : "ABSENTE", f_semi ? "ok" : "ABSENTE",
             wall[0] ? wall : "(aucun)");
    log_line(l);
  }
  if (!wall[0]) snprintf(wall, sizeof(wall), "%s", AP_APP "/sce_sys/pic0.png");
  {
    const char *dot = strrchr(wall, '.');
    if (dot && (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg"))) wall_tex = vita2d_load_JPEG_file(wall);
    else wall_tex = vita2d_load_PNG_file(wall);
    char l[220];
    if (wall_tex)
      snprintf(l, sizeof(l), "fond charge : %d x %d", vita2d_texture_get_width(wall_tex), vita2d_texture_get_height(wall_tex));
    else
      snprintf(l, sizeof(l), "fond illisible : %s", wall);
    log_line(l);
  }

  if (!ready) {
    log_line("aucune mise a jour en attente");
    hold("Aucune mise à jour à installer", -1.0f, "", 120);
  } else {
    char l[160], msg[128] = "";
    snprintf(title_msg, sizeof(title_msg), "Installation de la mise à jour %s", version);
    snprintf(l, sizeof(l), "installation %s : %d fichier(s)", version, nfiles);
    log_line(l);
    hold(title_msg, 0.0f, "Ne pas éteindre la console pendant l'installation.", 66); /* le logo se dessine */
    int ok = install(msg, sizeof(msg));
    if (ok) {
      cleanup();
      snprintf(l, sizeof(l), "%s installee", version);
      log_line(l);
      hold(title_msg, 1.0f, "Ne pas éteindre la console pendant l'installation.", 40);
      snprintf(title_msg, sizeof(title_msg), "Mise à jour %s installée", version);
      hold(title_msg, 1.0f, "", 75);
      hold("Ouverture d'Area Play...", 1.0f, "", 45);
    } else {
      sceIoRemove(RUNNING); /* Area Play proposera de nouveau la mise a jour */
      snprintf(l, sizeof(l), "echec : %s", msg);
      log_line(l);
      hold("La mise à jour a échoué", -1.0f, msg, 150);
      hold("La mise à jour a échoué", -1.0f, "La version actuelle reste installée.", 120);
    }
  }
  relaunch();
  sceKernelExitProcess(0); /* tout de suite, comme Area Play : sinon la Vita demande quelle appli fermer */
  return 0;
}
