#include <psp2/appmgr.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <vita2d.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <zlib.h>

#define C(r, g, b, a) RGBA8(r, g, b, a)
#define LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
/* Nom de l'appli : se change dans CMakeLists.txt (VF_NAME), qui le passe au compilateur. Il sert partout :
 * logo du menu, ecran de demarrage, pied de page, textes, nom de la bulle et dossier ux0:data/<nom>. */
#ifndef VF_APP_NAME
#define VF_APP_NAME "Area Play"
#endif
#ifndef VF_DATA_NAME
#define VF_DATA_NAME "AreaPlay" /* dossier ux0:data/<nom> (sans espace) */
#endif
#define VF_EDITION "VITA EDITION" /* affiche seulement sur l'ecran de chargement */
#define VF_VERSION_NUM "1.0" /* numero : garder en phase avec build.cmd et CMakeLists.txt */
#define VF_VERSION VF_APP_NAME " " VF_VERSION_NUM

/* ======================================================================
 *  REGLAGES : tout ce qu'on change souvent est ici
 * ====================================================================== */

/* --- Polices ---------------------------------------------------------
 * Pour changer de police : copie ton fichier .ttf dans assets/fonts/ et
 * change simplement les deux noms ci-dessous (inutile de renommer le
 * fichier). Prends une police "statique" (pas "Variable"), en .ttf.
 * USE_TTF 0 = utilise la police systeme de la Vita (PGF) a la place.   */
#define USE_TTF 1
#define FONT_REGULAR "app0:assets/fonts/Inter-Regular.ttf"
#define FONT_BOLD "app0:assets/fonts/Inter-SemiBold.ttf"

/* Tailles de police. Chaque taille a sa propre police chargee en memoire
 * (voir fonts[][]) : si tu changes un FS_xxx, tout s'adapte tout seul.  */
#define FS_SMALL 17    /* libelles MAJUSCULES, pied de page, details     */
#define FS_BODY 17     /* texte courant, menu, titres de cartes          */
#define FS_SECTION 22  /* titres de section                              */
#define FS_TITLE 30    /* titre du jeu quand il est trop long            */
#define FS_HERO 42     /* gros titre du jeu                              */
#define SPACING 2      /* espacement des lettres (libelles en MAJ)       */

/* Mise en page */
#define SCREEN_W 960
#define RAIL_W 154
#define CONTENT_X 178
#define CONTENT_R 936
#define BAR_H 50       /* hauteur de la barre de menu du haut            */
#define FOOTER_Y 508
#define TILE_Y 64      /* tuile du jeu en cours                          */
#define TILE_W 758
#define TILE_H 196
#define CARD_W 179
#define CARD_PITCH 193
#define COVER_H 124    /* hauteur des jaquettes du carrousel             */
#define SHELF_Y 308    /* haut des cartes du carrousel                   */
#define MAX_APPS MAX_TOTAL /* une categorie peut contenir toute la bibliotheque */

#define DATA_DIR "ux0:data/" VF_DATA_NAME
#define OLD_DATA_DIR "ux0:data/VitaFlow" /* ancien nom : repris au premier demarrage d'Area Play */
#define SETTINGS_FILE DATA_DIR "/settings.bin"
#define RECENT_FILE DATA_DIR "/recent.bin"
#define CUSTOM_FILE DATA_DIR "/custom.txt"
#define IMG_DIR DATA_DIR "/images"   /* images au choix de l'utilisateur */
#define COVER_DIR DATA_DIR "/covers" /* <IDENTIFIANT>.png / .jpg          */
#define BG_DIR DATA_DIR "/backgrounds" /* fonds telecharges <IDENTIFIANT>.png */
#define SYS_DIR DATA_DIR "/systems"  /* vita.png, psp.png                 */
#define NAMES_FILE DATA_DIR "/names.txt" /* ID|Nom : noms personnalises */
/* Nombre max d'applications (au-dela : signale). Les images ne sont chargees que pour les jeux
 * affiches (tex_evict) : ce nombre ne coute que quelques Ko de memoire par jeu. */
#define MAX_TOTAL 2000
#define CACHE_FILE DATA_DIR "/library.bin"  /* bibliotheque memorisee */
#define PSP_GAME_DIR "ux0:pspemu/PSP/GAME"
#define PSP_ISO_DIR "ux0:pspemu/ISO"
#define LOG_FILE DATA_DIR "/log.txt"  /* icones manquantes, etc. */
#define ICON_CACHE_FILE DATA_DIR "/icons.bin" /* icones memorisees (pixels) */
/* Jeux PSP en ISO / CSO (<partition>:pspemu/ISO) : listes seulement si la bulle "RetroFlow Adrenaline Launcher"
 * et le module AdrBubbleBooter sont installes (facultatif) ; sinon, seuls les jeux qui ont une bulle. */
#define ADR_LAUNCHER_ID "RETROLNCR"
#define ADR_BOOTER "ux0:app/PSPEMUCFW/sce_module/adrbubblebooter.suprx"
#define WALL_DIR DATA_DIR "/wallpapers" /* fonds d'ecran ajoutes par l'utilisateur */
#define SETTINGS_TXT DATA_DIR "/settings.txt" /* reglages "cle=valeur" (remplace settings.bin) */

/* Lignes des reglages (Reglages > Affichage / Theme / Bibliotheque). */
enum {
  SR_BACKDROP, SR_GLOSS, SR_THEME, SR_ACCENT, SR_VIEW, SR_SHOW_ID, SR_HIDDEN, SR_SCAN,
  SR_DELETE, SR_SORT, SR_DOWNLOAD, SR_SYS_MODE, SR_WALL, SR_WALL_DIM, SR_MSAA, SR_FPS, SR_BD_ICON, SR_SOUND, SR_EMU, SR_REGION, SR_LANG,
  SR_SNOW, SR_NET_STATE, SR_NET_IP, SR_NET_TEST, SR_SPLASH_BG, SETTINGS_N
};

enum { ST_REG = 0, ST_SEMI = 1 };
enum { VIEW_TILE = 0, VIEW_GRID = 1, VIEW_LIST = 2, VIEW_CATALOG = 3, VIEW_COUNT = 4 };

/* ======================================================================
 *  Le code est reparti dans src/parties/ (un fichier par theme), inclus ici
 *  dans l'ordre : c'est toujours un seul programme, compile en une fois.
 *  Ne compile pas ces fichiers separement.
 * ====================================================================== */
#include "parties/vf_donnees.c"
#include "parties/vf_dessin.c"
#include "parties/vf_noel.c"
#include "parties/vf_sauvegarde.c"
#include "parties/vf_outils.c"
#include "parties/vf_textures.c"
#include "parties/vf_applications.c"
#include "parties/vf_chargeur.c"
#include "parties/vf_images.c"
#include "parties/vf_collections.c"
#include "parties/vf_bibliotheque.c"
#include "parties/vf_cartes.c"
#include "parties/vf_vues.c"
#include "parties/vf_page_reglages.c"
#include "parties/vf_fenetres.c"
#include "parties/vf_reseau.c"
#include "parties/vf_sons.c"
#include "parties/vf_modales.c"
/* ======================================================================
 *  Programme principal
 * ====================================================================== */

/* Saison de Noel (1er decembre - 6 janvier) : propose une fois le theme Noel ; s'il a ete pris comme ca,
 * l'ancien theme (et son accent) revient tout seul apres la saison. */
static void xmas_check(void) {
  int year = 0;
  if (xmas_season(&year)) {
    if (opt_theme == XMAS_THEME || xmas_asked == year) return;
    xmas_asked = year;
    settings_save();
    conf_kind = 4;
    conf_open = 1;
  } else if (opt_theme == XMAS_THEME && xmas_prev > 0) {
    int p = xmas_prev - 1;
    xmas_prev = 0;
    opt_theme = p / 100 < (int)THEME_COUNT ? p / 100 : 0;
    opt_accent = p % 100 < (int)ACCENT_COUNT ? p % 100 : 0;
    settings_save();
    snprintf(toast_text, sizeof(toast_text), "%s", L("Fin des fêtes : ton thème habituel est revenu"));
    toast_time = 4.0f;
  }
}

static void reset_focus(void) {
  memset(focus, 0, sizeof(focus));
  cat_grid = 0; /* catalogue : chaque liste repart du carrousel */
  cat_t = 0.0f;
}

/* Ouvre la recherche (cadre de la barre du haut) : page des resultats + clavier en bas d'ecran,
 * les resultats se mettent a jour au-dessus pendant la saisie. */
static void search_open(int *page) { /* ecran de recherche : clavier a gauche, resultats a droite */
  if (*page != PG_SEARCH) search_prev_page = *page;
  kb_start_ex(KB_SEARCH, 0, search_q);
  search_run(kb_text);
}

static int pgf_is_latin(unsigned int c) { return c < 0x3000; }
static int pgf_is_other(unsigned int c) { return c >= 0x3000; }

/* Pendant l'ecran VITAFLOW : les images du premier ecran et des premiers jeux de chaque liste,
 * pour que le menu s'affiche complet (pas d'images qui apparaissent une a une). Le reste est
 * prepare ensuite en arriere-plan (cover_pump), avant qu'on ouvre les listes. */
#define PRELOAD_N 8 /* premier ecran seulement : le reste arrive ensuite sans ecran d'attente */
static float g_intro; /* fondu de l'ecran de demarrage vers le menu (1 -> 0) */
static void preload_first_images_run(void);
/* Chargement rapide : l'ecran de demarrage reste le temps que l'animation du logo aille au bout
 * (SPLASH_END, 1,2 s apres le premier affichage ; rien a attendre si elle est deja finie). */
static void preload_first_images(void) {
  preload_first_images_run();
  if (splash_anim_t() < SPLASH_END) {
    vita2d_set_vblank_wait(1);
    splash_prog = 1.0f;
    while (splash_anim_t() < SPLASH_END) {
      draw_splash(NULL);
      sceKernelDelayThread(2000);
    }
    splash_prog = -1.0f;
  }
}
static void preload_first_images_run(void) {
  int total = 0;
  while (total < PRELOAD_N && prefetch_nth(2, total)) ++total; /* combien a charger (pour la barre) */
  g_intro = 1.0f;
  if (!total) return;
  if (!lj_start()) { /* pas de thread : chargement direct, l'animation avance entre deux images */
    vita2d_set_vblank_wait(0);
    for (int done = 0; done < total; ++done) {
      Game *g = prefetch_nth(2, 0);
      if (!g) break;
      splash_prog = (float)done / (float)total;
      draw_splash("Préparation des images...");
      cover_load_now(g);
    }
    splash_prog = -1.0f;
    vita2d_set_vblank_wait(1);
    return;
  }
  /* Les images sont decodees par le thread de chargement ; pendant ce temps l'ecran de demarrage
   * continue de s'animer a 60 images par seconde (avant : une image d'animation par jaquette). */
  Game *batch[PRELOAD_N];
  int submitted = 0;
  unsigned long long t0 = sceKernelGetProcessTimeWide();
  vita2d_set_vblank_wait(1);
  for (;;) {
    lj_poll(NULL, NULL);
    while (submitted < total && lj_busy(LJ_COVER) + lj_busy(LJ_ICON) < LJ_N - 2) {
      Game *g = prefetch_nth(2, 0);
      if (!g) {
        total = submitted; /* plus rien a preparer */
        break;
      }
      cover_submit(g);
      if (g->cover_pending == 1) break; /* file pleine : on reessaie au tour suivant */
      batch[submitted++] = g;
    }
    int done = 0;
    for (int i = 0; i < submitted; ++i)
      if (batch[i]->cover_pending != 2) ++done;
    splash_prog = total ? (float)done / (float)total : 1.0f;
    if (done >= total || sceKernelGetProcessTimeWide() - t0 > 2000000ULL) break; /* 2 s au plus */
    draw_splash("Préparation des images...");
    sceKernelDelayThread(1000); /* laisse la main au thread de chargement */
  }
  for (int i = 0; i < submitted; ++i) batch[i]->appear = 1.0f; /* deja la a l'ouverture : pas de fondu */
  splash_prog = 1.0f;
  draw_splash("Préparation des images...");
  splash_prog = -1.0f;
}

int main(void) {
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  { /* VitaFlow s'appelle maintenant Area Play : ses reglages, images et caches sont repris tels quels
     * (avant toute lecture de reglages). Echec : dossier neuf, l'erreur est notee dans log.txt. */
    SceIoStat st;
    if (sceIoGetstat(DATA_DIR, &st) < 0 && sceIoGetstat(OLD_DATA_DIR, &st) >= 0) {
      int r = sceIoRename(OLD_DATA_DIR, DATA_DIR);
      if (r < 0) {
        char m[96];
        snprintf(m, sizeof(m), "Reprise de " OLD_DATA_DIR " impossible (0x%08X)", (unsigned)r);
        log_line(m);
      }
    }
  }
  stats_load();
  session_end();
  settings_load(); /* avant vita2d : l'anticrenelage se choisit a l'initialisation */
  lang_init();     /* langue de l'interface (langue.txt), avant le premier texte affiche */
  /* reserve de sommets plus grande : les formes arrondies sont de vrais polygones */
  msaa_active = opt_msaa;
  if (msaa_active) vita2d_init_advanced_with_msaa(4 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
  else vita2d_init_advanced(4 * 1024 * 1024);
  vita2d_set_vblank_wait(1);
  /* Les textures restent en memoire video (CDRAM, choix par defaut de vita2d) : en 4.1 elles etaient
   * en memoire principale, que le module SSL utilise aussi -> jaquettes et HTTPS en echec.
   * Les jaquettes reduites a l'affichage (tex_shrink) suffisent a ne pas saturer la CDRAM. */
  apply_theme();
  sceIoMkdir(DATA_DIR, 0777); /* dossiers a remplir avec VitaShell / FTP */
  sceIoMkdir(IMG_DIR, 0777);
  sceIoMkdir(COVER_DIR, 0777);
  sceIoMkdir(BG_DIR, 0777);
  sceIoMkdir(SYS_DIR, 0777);
  sceIoMkdir(WALL_DIR, 0777);
  sceIoMkdir(DATA_DIR "/covers", 0777);
  sceIoMkdir(DATA_DIR "/backgrounds", 0777);
  icons_dir_init();
  roms_dir_init();
  custom_load();
  load_custom_systems();
  load_collections();
  wall_load_setting();
  xmas_check(); /* theme Noel propose en saison, ancien theme rendu apres */
  apply_theme(); /* le fond d'ecran change l'opacite du fond dynamique */
  nameset_load(&sys_set, SYS_DIR);

  { /* police systeme : latine, puis japonaise pour le reste (titres japonais) */
    vita2d_system_pgf_config pcfg[2] = {{SCE_FONT_LANGUAGE_LATIN, pgf_is_latin}, {SCE_FONT_LANGUAGE_JAPANESE, pgf_is_other}};
    g_pgf = vita2d_load_system_pgf(2, pcfg);
    if (!g_pgf) g_pgf = vita2d_load_default_pgf();
  }
#if USE_TTF
  { /* chaque police est lue une seule fois en memoire et partagee par ses 4 tailles :
     * plus de lecture de la carte memoire quand une lettre s'affiche pour la premiere fois */
    static const char *font_files[2] = {FONT_REGULAR, FONT_BOLD};
    for (int s = 0; s < 2; ++s) {
      unsigned char *fbuf = NULL;
      int fsz = 0;
      SceUID ff = sceIoOpen(font_files[s], SCE_O_RDONLY, 0);
      if (ff >= 0) {
        SceOff sz = sceIoLseek(ff, 0, SCE_SEEK_END);
        sceIoLseek(ff, 0, SCE_SEEK_SET);
        if (sz > 0 && sz < (8 << 20) && (fbuf = (unsigned char *)malloc((size_t)sz)) != NULL) {
          fsz = sceIoRead(ff, fbuf, (int)sz);
          if (fsz != (int)sz) {
            free(fbuf);
            fbuf = NULL;
          }
        }
        sceIoClose(ff);
      }
      for (int i = 0; i < FONT_SIZES; ++i) /* le tampon reste en memoire tant qu'Area Play tourne */
        fonts[s][i] = fbuf ? vita2d_load_font_mem(fbuf, (unsigned)fsz) : vita2d_load_font_file(font_files[s]);
    }
  }
#endif

  /* Premier demarrage : lecture complete. Ensuite : cache (avec ecran d'attente). */
  build_library(0);
  preload_first_images();

  for (int i = 0; i < (int)SYSTEM_COUNT; ++i) load_system_icon(&systems[i]);
  if (scan_error < 0 && app_n == 0) {
    snprintf(toast_text, sizeof(toast_text), L("Accès à ux0:app refusé (0x%08X) : mode unsafe requis"),
             (unsigned)scan_error);
    toast_time = 8.0f;
  } else if (demo_mode) {
    snprintf(toast_text, sizeof(toast_text), "Aucune application trouvée (mode démo)");
    toast_time = 6.0f;
  }

  int page = 0;
  int selected = 0;
  int games_level = 0; /* Jeux / Collections : 0 = choix du systeme (ou de la collection), 1 = ses jeux */
  int cur_system = 0;
  int setting_row = 0;
  unsigned int previous = 0;
  g_zone = ZONE_CONTENT;

  /* Etat d'animation */
  reset_focus();
  focus[0] = 1;
  float page_alpha = 1.0f;
  float page_slide = 0.0f;
  float hero_alpha = 1.0f;
  float rail_y = 94;
  float bd_alpha = 0.0f;   /* opacite du fond du jeu affiche              */
  float prev_base = 0.0f;  /* opacite de l'ancien fond au moment du changement */
  float bg_timer = 0.0f;   /* attente avant de charger le grand fond       */
  Game *shown = NULL;      /* jeu dont le fond est affiche                */
  Game *prev_shown = NULL; /* ancien jeu (fondu enchaine)                  */
  Game *bg_wait = NULL;    /* jeu dont on va charger le grand fond         */

  unsigned long long last_time = sceKernelGetProcessTimeWide();
  unsigned long long prev_frame_us = last_time; /* compteur d'images */

  for (;;) {
    unsigned long long now_us = sceKernelGetProcessTimeWide();
    float dt = (now_us - last_time) / 1000000.0f;
    g_time += dt > 0.1f ? 0.1f : dt;
    last_time = now_us;
    if (dt > 0.1f) dt = 0.1f;

    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned int pressed = pad.buttons & ~previous;
    previous = pad.buttons;

    /* ---- Directions : croix + stick gauche, avec repetition quand on maintient ---- */
    {
      static unsigned int prev_dirs;
      static float hold[4], next_fire[4];
      static const unsigned int dbits[4] = {SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT};
      unsigned int pad_dirs = pad.buttons & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
      unsigned int stick_dirs = 0;
      if (pad.ly < 64) stick_dirs |= SCE_CTRL_UP;
      if (pad.ly > 192) stick_dirs |= SCE_CTRL_DOWN;
      if (pad.lx < 64) stick_dirs |= SCE_CTRL_LEFT;
      if (pad.lx > 192) stick_dirs |= SCE_CTRL_RIGHT;
      unsigned int dirs = pad_dirs | stick_dirs;
      unsigned int dnew = dirs & ~prev_dirs;
      int modal = picker_open || menu_open || hid_open || conf_open || sm_open || kb_open || info_open || dl_open ||
                  wp_open || ct_open || cm_open || em_open || alt_open;
      int rep_ok = modal || g_zone == ZONE_CONTENT; /* pas de repetition dans le menu de gauche */
      pressed = (pressed & ~(SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) | dnew;
      for (int k = 0; k < 4; ++k) {
        if (!(dirs & dbits[k])) {
          hold[k] = 0.0f;
          continue;
        }
        if (dnew & dbits[k]) {
          hold[k] = 0.0f;
          next_fire[k] = 0.35f; /* delai avant la repetition */
          continue;
        }
        hold[k] += dt;
        int horiz = k >= 2;
        if (page == 3 && g_zone == ZONE_CONTENT && !modal && horiz) continue; /* reglages : pas de bascule en boucle */
        if (rep_ok && hold[k] >= next_fire[k]) {
          pressed |= dbits[k];
          next_fire[k] += (pad_dirs & dbits[k]) ? 0.11f : 0.08f; /* stick un peu plus rapide */
        }
      }
      prev_dirs = dirs;
    }
    /* ---- Stick droit (gauche / droite) : curseur du clavier, avec repetition quand on maintient ---- */
    int kb_rstick = 0;
    {
      static int rprev;
      static float rhold, rnext;
      int r = pad.rx < 64 ? -1 : pad.rx > 192 ? 1 : 0;
      if (r && r != rprev) {
        kb_rstick = r;
        rhold = 0.0f;
        rnext = 0.35f; /* delai avant la repetition */
      } else if (r) {
        rhold += dt;
        if (rhold >= rnext) {
          kb_rstick = r;
          rnext += 0.08f;
        }
      }
      rprev = r;
    }

    rail_n = 0; /* menu de gauche : "Favoris" n'apparait que s'il y a au moins un favori */
    rail_ids[rail_n++] = PG_HOME;
    rail_ids[rail_n++] = PG_GAMES;
    rail_ids[rail_n++] = PG_COLL;
    if (fav_n > 0) rail_ids[rail_n++] = PG_FAV;
    rail_ids[rail_n++] = PG_HB;
    rail_ids[rail_n++] = PG_SETTINGS;
    if (page == PG_FAV && fav_n == 0) { /* plus aucun favori : retour au choix du systeme */
      page = PG_GAMES;
      games_level = 0;
      selected = sys_pos(cur_system);
      page_alpha = 0.0f;
      reset_focus();
    }
    if (cur_system >= (int)SYSTEM_COUNT) cur_system = 0;
    if (cur_coll >= coll_n) { /* collection supprimee : retour au choix */
      cur_coll = coll_n > 0 ? coll_n - 1 : 0;
      if (page == PG_COLL && games_level == 1) {
        games_level = 0;
        selected = cur_coll;
        reset_focus();
      }
    }
    int prev_page = page;
    int n = 1;
    Game **list = current_list(page, games_level, cur_system, &n);
    int in_games = (page == PG_HB) || (page == PG_FAV) || (page == PG_SEARCH) ||
                   ((page == PG_GAMES || page == PG_COLL) && games_level == 1);
    sync_view(page, games_level, cur_system);
    int cols = 4;
    int did_enter = 0;
    int do_rescan = 0;
    int want_info = 0;
    int want_quit = 0;
    dl_poll();
    alt_poll();

    /* ---- Ecran tactile : toucher = choisir / valider, glisser = faire defiler ---- */
    {
      static int t_down, t_moved, t_locked, t_strip; /* t_locked : 1 = horizontal, 2 = vertical */
      static float t_sx, t_sy, t_x, t_y, t_time, t_ax, t_ay, t_vx, t_vy, q_timer;
      static unsigned int q_dir;
      static int q_n;
      int other_modal = picker_open || menu_open || hid_open || conf_open || sm_open || kb_open || dl_open ||
                        wp_open || ct_open || cm_open || em_open || alt_open;
      int mode = 1; /* 1 = horizontal, 2 = vertical, 3 = les deux */
      float stepH = 80.0f, stepV = 40.0f;
      if (info_open) mode = 0;
      else if (other_modal) { mode = 3; stepH = 48.0f; stepV = 44.0f; }
      else if (g_zone == ZONE_RAIL) { mode = 2; stepV = 40.0f; }
      else if (page == 3) { mode = 2; stepV = set_group < 0 ? 50.0f : 36.0f; }
      else if (in_games && opt_view == VIEW_LIST) { mode = 2; stepV = 38.0f; }
      else if (in_games && opt_view == VIEW_GRID) { mode = 3; stepV = 90.0f; }
      else if (in_games && opt_view == VIEW_CATALOG && cat_grid) { mode = 3; stepV = 90.0f; }
#define TOUCH_EMIT(d)                                                                   \
  do {                                                                                  \
    unsigned int dd_ = (d);                                                             \
    int skip_ = 0;                                                                      \
    if (!other_modal && !info_open && page != 3 && g_zone == ZONE_CONTENT) {            \
      if (dd_ == SCE_CTRL_LEFT && selected <= 0) skip_ = 1;                             \
      if (dd_ == SCE_CTRL_RIGHT && selected >= n - 1) skip_ = 1;                        \
    }                                                                                   \
    if (!skip_) pressed |= dd_;                                                         \
  } while (0)
      SceTouchData td;
      memset(&td, 0, sizeof(td));
      sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
      float lb_prev = g_lb_a;
      if (pressed) g_touch_ui = 0; /* une touche physique : la barre de lettres disparait */
      if (td.reportNum > 0) {
        g_touch_ui = 1;
        g_touch_idle = 0.0f;
      } else {
        g_touch_idle += dt;
      }
      {
        float la = 4.0f - g_touch_idle;
        g_lb_a = g_touch_ui ? (la > 1.0f ? 1.0f : (la < 0.0f ? 0.0f : la)) : 0.0f;
      }
      if (td.reportNum > 0) {
        float tx = td.report[0].x / 2.0f, ty = td.report[0].y / 2.0f;
        if (!t_down) {
          t_down = 1;
          t_moved = 0;
          t_locked = 0;
          t_sx = t_x = tx;
          t_sy = t_y = ty;
          t_time = 0.0f;
          t_ax = t_ay = t_vx = t_vy = 0.0f;
          q_n = 0;
          t_strip = (lb_prev > 0.5f && tx >= LB_X - 5 && !other_modal && !info_open && in_games && n >= 8 && opt_sort != 1 && list && page != 3);
        } else {
          float ddx = tx - t_x, ddy = ty - t_y, inv = dt > 0.0f ? 1.0f / dt : 0.0f;
          t_vx = t_vx * 0.8f + ddx * inv * 0.2f;
          t_vy = t_vy * 0.8f + ddy * inv * 0.2f;
          t_ax += ddx;
          t_ay += ddy;
        }
        t_x = tx;
        t_y = ty;
        t_time += dt;
        if (t_strip) { /* barre de lettres : le jeu suit le doigt */
          int pk = lb_pick(list, n, t_y);
          if (pk >= 0) {
            selected = pk;
            g_zone = ZONE_CONTENT;
          }
        }
        if (!t_strip && !t_moved && (fabsf(t_x - t_sx) > 14.0f || fabsf(t_y - t_sy) > 14.0f)) {
          t_moved = 1;
          if (mode == 1) t_locked = 1;
          else if (mode == 2) t_locked = 2;
          else t_locked = fabsf(t_x - t_sx) > fabsf(t_y - t_sy) ? 1 : 2;
          t_ax = t_x - t_sx;
          t_ay = t_y - t_sy;
        }
        if (t_moved && mode) { /* le doigt entraine la selection, un cran par "stepH / stepV" */
          if (t_locked == 1) {
            if (t_ax <= -stepH) { t_ax += stepH; TOUCH_EMIT(SCE_CTRL_RIGHT); }
            else if (t_ax >= stepH) { t_ax -= stepH; TOUCH_EMIT(SCE_CTRL_LEFT); }
          } else {
            if (t_ay <= -stepV) { t_ay += stepV; TOUCH_EMIT(SCE_CTRL_DOWN); }
            else if (t_ay >= stepV) { t_ay -= stepV; TOUCH_EMIT(SCE_CTRL_UP); }
          }
        }
      } else if (t_down) {
        t_down = 0;
        int was_strip = t_strip;
        t_strip = 0;
        if (was_strip) {
          /* fin du geste sur la barre de lettres */
        } else if (!t_moved) {
          if (t_time < 0.7f) {
            if (info_open) {
              pressed |= SCE_CTRL_CIRCLE;
            } else if (kb_open) { /* clavier tactile : touches, croix du champ, resultats, retour */
              int key = -1;
              for (int i = hit_n - 1; i >= 0 && key < 0; --i)
                if (hits[i].type == H_KEY && t_sx >= hits[i].x && t_sx <= hits[i].x + hits[i].w &&
                    t_sy >= hits[i].y && t_sy <= hits[i].y + hits[i].h)
                  key = hits[i].idx;
              if (key == KB_CLOSE) pressed |= SCE_CTRL_CIRCLE;
              else if (key == KB_CLEAR) kb_clear();
              else if (key == KB_FIELD) kb_field_tap(t_sx);
              else if (key >= KB_RES0) {
                kb_focus = 1;
                kb_res_sel = key - KB_RES0;
                pressed |= SCE_CTRL_CROSS;
              } else if (key >= 0) {
                kb_focus = 0;
                kb_sel = key;
                pressed |= SCE_CTRL_CROSS;
              }
            } else if (!other_modal) {
              int found = -1;
              for (int i = hit_n - 1; i >= 0 && found < 0; --i)
                if (t_sx >= hits[i].x && t_sx <= hits[i].x + hits[i].w && t_sy >= hits[i].y &&
                    t_sy <= hits[i].y + hits[i].h)
                  found = i;
              if (found >= 0) {
                int ty2 = hits[found].type, ix = hits[found].idx;
                int was_content = g_zone == ZONE_CONTENT;
                if (ty2 == H_SEARCH) search_open(&page);
                if (ty2 == H_NET) { /* barres du Wi-Fi : Reglages > Reseau */
                  page = PG_SETTINGS;
                  set_group = SG_NET;
                  set_sel = 0;
                  g_zone = ZONE_CONTENT;
                }
                if (ty2 == H_QUIT) {
                  conf_kind = 1;
                  conf_open = 1;
                } else if (ty2 == H_RAIL) {
                  rail_quit = 0;
                  if (page == ix && g_zone == ZONE_RAIL) pressed |= SCE_CTRL_CROSS;
                  else {
                    g_zone = ZONE_RAIL;
                    page = ix;
                  }
                } else if (ty2 == H_ITEM) {
                  g_zone = ZONE_CONTENT;
                  if (page == 3) {
                    set_sel = ix;
                    pressed |= SCE_CTRL_CROSS;
                  } else if (was_content && selected == ix) {
                    pressed |= SCE_CTRL_CROSS;
                  } else {
                    selected = ix;
                  }
                } else if (ty2 == H_LAUNCH) {
                  g_zone = ZONE_CONTENT;
                  pressed |= SCE_CTRL_CROSS;
                } else if (ty2 == H_INFO) {
                  g_zone = ZONE_CONTENT;
                  pressed |= SCE_CTRL_TRIANGLE;
                } else if (ty2 == H_BACK) {
                  pressed |= SCE_CTRL_CIRCLE;
                }
              }
            }
          }
        } else if (mode) { /* lance : le defilement continue un peu apres avoir leve le doigt */
          float v = t_locked == 1 ? t_vx : t_vy, step = t_locked == 1 ? stepH : stepV;
          if (fabsf(v) > 250.0f) {
            q_n = (int)(fabsf(v) * 0.2f / step);
            if (q_n > 10) q_n = 10;
            q_dir = t_locked == 1 ? (v < 0.0f ? SCE_CTRL_RIGHT : SCE_CTRL_LEFT)
                                  : (v < 0.0f ? SCE_CTRL_DOWN : SCE_CTRL_UP);
            q_timer = 0.0f;
          }
        }
      }
      if (q_n > 0 && !t_down) {
        q_timer -= dt;
        if (q_timer <= 0.0f) {
          TOUCH_EMIT(q_dir);
          --q_n;
          q_timer = 0.06f;
        }
      }
#undef TOUCH_EMIT
    }

    /* Etat de l'interface avant les touches : s'il change, un son de navigation est joue. */
#define UI_STATE()                                                                                          \
  ((unsigned)page * 7919u + (unsigned)selected * 31u + (unsigned)g_zone * 3u + (unsigned)games_level * 101u + \
   (unsigned)set_sel * 37u + (unsigned)(set_group + 1) * 41u + (unsigned)menu_sel * 43u +                    \
   (unsigned)menu_open * 47u + (unsigned)sm_sel * 53u + (unsigned)sm_open * 59u + (unsigned)ct_sel * 61u +  \
   (unsigned)ct_open * 67u + (unsigned)cm_sel * 71u + (unsigned)cm_open * 73u + (unsigned)hid_sel * 79u +   \
   (unsigned)hid_open * 83u + (unsigned)pick_sel * 89u + (unsigned)picker_open * 97u +                      \
   (unsigned)wp_sel * 103u + (unsigned)wp_open * 107u + (unsigned)kb_sel * 109u + (unsigned)kb_open * 113u + \
   (unsigned)info_open * 127u + (unsigned)conf_open * 131u + (unsigned)rail_quit * 137u +                   \
   (unsigned)cur_system * 139u + (unsigned)cur_coll * 149u + (unsigned)em_sel * 151u + (unsigned)em_open * 157u + \
   (unsigned)alt_sel * 163u + (unsigned)kb_focus * 167u + (unsigned)kb_res_sel * 173u + (unsigned)kb_page * 179u + \
   (unsigned)kb_cur * 181u)
    unsigned ui_before = UI_STATE(), keys_in = pressed;

    /* ---- Fenetres modales : elles prennent toutes les touches ---- */
    if (picker_open) {
      if (pressed & SCE_CTRL_UP) picker_move(pick_sel - 1);
      if (pressed & SCE_CTRL_DOWN) picker_move(pick_sel + 1);
      if (pressed & SCE_CTRL_LTRIGGER) picker_move(pick_sel - 7);
      if (pressed & SCE_CTRL_RTRIGGER) picker_move(pick_sel + 7);
      if (pressed & SCE_CTRL_CROSS) picker_apply();
      else if (pressed & SCE_CTRL_CIRCLE) picker_close();
      pressed &= SCE_CTRL_START;
    } else if (wp_open) {
      if (pressed & SCE_CTRL_UP) wp_move(wp_sel - 1);
      if (pressed & SCE_CTRL_DOWN) wp_move(wp_sel + 1);
      if (pressed & SCE_CTRL_LTRIGGER) wp_move(wp_sel - 8);
      if (pressed & SCE_CTRL_RTRIGGER) wp_move(wp_sel + 8);
      if (pressed & SCE_CTRL_CROSS) wp_apply();
      else if (pressed & SCE_CTRL_CIRCLE) wp_close();
      pressed &= SCE_CTRL_START;
    } else if (menu_open) {
      if ((pressed & SCE_CTRL_UP) && menu_sel > 0) --menu_sel;
      if ((pressed & SCE_CTRL_DOWN) && menu_sel < MI_COUNT - 1) ++menu_sel;
      int mleft = (pressed & SCE_CTRL_LEFT) != 0, mright = (pressed & SCE_CTRL_RIGHT) != 0;
      int mcross = (pressed & SCE_CTRL_CROSS) != 0;
      if (menu_sel == MI_KIND && (mleft || mright || mcross)) {
        /* categorie : gauche / droite / X font defiler PS Vita > PSP > Homebrew */
        set_kind(menu_game, next_kind(menu_game->kind, mleft ? -1 : 1));
      } else if (menu_sel == MI_FAV && (mleft || mright || mcross)) {
        set_fav(menu_game, !menu_game->fav);
      } else if (menu_sel == MI_COLL && (mright || mcross)) {
        menu_open = 0;
        ct_game = menu_game;
        ct_sel = coll_n > 0 ? 1 : 0;
        ct_open = 1;
      } else if (mcross) {
        Game *g = menu_game;
        if (menu_sel == MI_PLAY) {
          menu_open = 0;
          try_launch(g);
        } else if (menu_sel == MI_RENAME) {
          menu_open = 0;
          kb_start_ex(KB_GAME_RENAME, 0, g->title);
          kb_game = g;
        } else if (menu_sel == MI_ALTCOVER) {
          menu_open = 0;
          alt_open_for(g);
        } else if (menu_sel == MI_IMAGE || menu_sel == MI_BGIMG) {
          menu_open = 0;
          pick_bg = menu_sel == MI_BGIMG; /* jaquette ou fond du jeu */
          picker_open_for(g, -1);
        } else if (menu_sel == MI_DL) {
          menu_open = 0;
          dl_start(g);
        } else if (menu_sel == MI_HIDE) {
          set_hidden(menu_game, 1);
          snprintf(toast_text, sizeof(toast_text), "Jeu masqué (Réglages > Jeux masqués)");
          toast_time = 3.0f;
          menu_open = 0;
        } else {
          menu_open = 0;
        }
      }
      if (pressed & SCE_CTRL_CIRCLE) menu_open = 0;
      pressed &= SCE_CTRL_START;
    } else if (dl_open) {
      if (pressed & SCE_CTRL_CIRCLE) dl_cancel = 1;
      pressed &= SCE_CTRL_START;
    } else if (info_open) {
      if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE)) info_open = 0;
      else if (pressed & SCE_CTRL_CROSS) {
        info_open = 0;
        if (info_game) try_launch(info_game);
      }
      pressed &= SCE_CTRL_START;
    } else if (kb_open) {
      /* O : fermer (comme partout) ou revenir au clavier (resultats) ; Carre : effacer une lettre ;
       * Triangle : espace ; SELECT : tout effacer. Recherche : L majuscule, R ou START valider ;
       * renommer / nommer : L / R deplacent le curseur, START valide. Stick droit : curseur partout. */
      int kb_ok = 0, kb_close = 0;
      if (pressed & SCE_CTRL_SELECT) kb_clear();
      if (kb_focus == 1 && kb_mode == KB_SEARCH) { /* liste des resultats */
        if ((pressed & SCE_CTRL_UP) && kb_res_sel > 0) --kb_res_sel;
        if ((pressed & SCE_CTRL_DOWN) && kb_res_sel < search_n - 1) ++kb_res_sel;
        if (pressed & (SCE_CTRL_LEFT | SCE_CTRL_CIRCLE)) kb_focus = 0;
        else if ((pressed & SCE_CTRL_CROSS) && kb_res_sel < search_n) { /* fiche du jeu (X y lance le jeu) */
          info_game = search_ptrs[kb_res_sel];
          info_open = 1;
        }
      } else {
        if (pressed & SCE_CTRL_LEFT) kb_move(-1, 0);
        if ((pressed & SCE_CTRL_RIGHT) && kb_move(1, 0) && kb_mode == KB_SEARCH && search_n > 0) {
          kb_focus = 1; /* sortie a droite du clavier : les resultats */
          if (kb_res_sel >= search_n) kb_res_sel = 0;
        }
        if (pressed & SCE_CTRL_UP) kb_move(0, -1);
        if (pressed & SCE_CTRL_DOWN) kb_move(0, 1);
        if (kb_rstick) kb_cur_move(kb_rstick); /* stick droit : curseur (tous les claviers) */
        if (pressed & SCE_CTRL_SQUARE) kb_back();
        if (pressed & SCE_CTRL_CIRCLE) kb_close = 1;
        if (pressed & SCE_CTRL_TRIANGLE) kb_append(" ");
        if (kb_mode == KB_SEARCH) {
          if (pressed & SCE_CTRL_LTRIGGER) kb_caps = !kb_caps;
          kb_ok = (pressed & (SCE_CTRL_RTRIGGER | SCE_CTRL_START)) != 0;
        } else {
          if (pressed & SCE_CTRL_LTRIGGER) kb_cur_move(-1);
          if (pressed & SCE_CTRL_RTRIGGER) kb_cur_move(1);
          kb_ok = (pressed & SCE_CTRL_START) != 0;
        }
        if ((pressed & SCE_CTRL_CROSS) && kb_press()) kb_ok = 1;
      }
      if (kb_mode == KB_SEARCH && strcmp(kb_text, search_q) != 0) { /* resultats a chaque lettre */
        search_run(kb_text);
        kb_res_sel = 0;
        if (search_n == 0) kb_focus = 0;
      }
      if (kb_ok && kb_mode == KB_SEARCH) { /* OK : on passe aux resultats */
        if (search_n > 0) {
          kb_focus = 1;
          kb_res_sel = 0;
        } else if (kb_text[0]) {
          snprintf(toast_text, sizeof(toast_text), L("Aucun jeu ne correspond à « %s »"), kb_text);
          toast_time = 2.5f;
        }
      } else if (kb_ok && kb_mode == KB_GAME_RENAME) {
        if (kb_game) game_rename(kb_game, kb_text);
        snprintf(toast_text, sizeof(toast_text), kb_text[0] ? "Jeu renommé" : "Nom d'origine rétabli");
        toast_time = 2.0f;
        kb_open = 0;
        lists_changed = 1;
      } else if (kb_ok && kb_mode == KB_SYSTEM) {
        int r = sys_add(kb_text);
        if (r == 0) {
          snprintf(toast_text, sizeof(toast_text), L("Système ajouté : %s"), kb_text);
          kb_open = 0;
          sm_refresh();
        } else if (r == -1) {
          snprintf(toast_text, sizeof(toast_text), "Nombre maximum de systèmes atteint");
        } else {
          snprintf(toast_text, sizeof(toast_text), "Nom vide ou déjà utilisé");
        }
        toast_time = 2.0f;
      } else if (kb_ok && kb_mode == KB_COLL_RENAME) {
        if (coll_rename(kb_arg, kb_text) == 0) {
          snprintf(toast_text, sizeof(toast_text), "Collection renommée");
          kb_open = 0;
        } else {
          snprintf(toast_text, sizeof(toast_text), "Nom vide ou déjà utilisé");
        }
        toast_time = 2.0f;
      } else if (kb_ok) { /* nouvelle collection (depuis Collections ou depuis un jeu) */
        int r = coll_add(kb_text);
        if (r >= 0) {
          kb_open = 0;
          if (kb_mode == KB_COLL_NEW_GAME && ct_game) {
            coll_toggle(ct_game, r);
            for (int i = 0; i < coll_n; ++i)
              if (coll_ids[i] == r) ct_sel = i + 1;
            snprintf(toast_text, sizeof(toast_text), L("Ajouté à « %s »"), coll_name[r]);
          } else {
            for (int i = 0; i < coll_n; ++i) /* la nouvelle tuile est selectionnee */
              if (coll_ids[i] == r && page == PG_COLL && games_level == 0) selected = i;
            snprintf(toast_text, sizeof(toast_text), L("Collection créée : %s"), coll_name[r]);
          }
        } else if (r == -1) {
          snprintf(toast_text, sizeof(toast_text), L("Nombre maximum de collections atteint (%d)"), MAX_COLL);
        } else {
          snprintf(toast_text, sizeof(toast_text), "Nom vide ou déjà utilisé");
        }
        toast_time = 2.2f;
      }
      if (kb_close) {
        kb_open = 0;
        if (kb_mode == KB_SEARCH) { /* recherche quittee : effacee */
          kb_clear();
          search_run("");
        }
      }
      pressed = 0; /* START sert a valider ici */
    } else if (ct_open) {
      int total = coll_n + 1;
      if ((pressed & SCE_CTRL_UP) && ct_sel > 0) --ct_sel;
      if ((pressed & SCE_CTRL_DOWN) && ct_sel < total - 1) ++ct_sel;
      if (pressed & SCE_CTRL_CROSS) {
        if (ct_sel == 0) kb_start_ex(KB_COLL_NEW_GAME, 0, NULL);
        else if (ct_game) {
          int k = coll_ids[ct_sel - 1];
          coll_toggle(ct_game, k);
          snprintf(toast_text, sizeof(toast_text), L((ct_game->coll >> k) & 1 ? "Ajouté à « %s »" : "Retiré de « %s »"),
                   coll_name[k]);
          toast_time = 1.6f;
        }
      }
      if (pressed & SCE_CTRL_CIRCLE) ct_open = 0;
      pressed &= SCE_CTRL_START;
    } else if (cm_open) {
      if ((pressed & SCE_CTRL_UP) && cm_sel > 0) --cm_sel;
      if ((pressed & SCE_CTRL_DOWN) && cm_sel < 2) ++cm_sel;
      if (pressed & SCE_CTRL_CROSS) {
        cm_open = 0;
        if (cm_sel == 0) kb_start_ex(KB_COLL_RENAME, cm_slot, coll_name[cm_slot]);
        else if (cm_sel == 1) {
          conf_kind = 2;
          conf_arg = cm_slot;
          conf_open = 1;
        }
      }
      if (pressed & SCE_CTRL_CIRCLE) cm_open = 0;
      pressed &= SCE_CTRL_START;
    } else if (sm_open) {
      if ((pressed & SCE_CTRL_UP) && sm_sel > 0) --sm_sel;
      if ((pressed & SCE_CTRL_DOWN) && sm_sel < sm_n - 1) ++sm_sel;
      if ((pressed & SCE_CTRL_CROSS) && sm_n > 0) {
        if (sm_type[sm_sel] == 1) {
          kb_start();
        } else {
          snprintf(toast_text, sizeof(toast_text), "Système retiré");
          if (sys_remove(sm_arg[sm_sel]) > 0) do_rescan = 1; /* ses ROM disparaissent de la bibliotheque */
          toast_time = 2.0f;
          sm_refresh();
        }
      }
      if (pressed & SCE_CTRL_CIRCLE) sm_open = 0;
      pressed &= SCE_CTRL_START;
    } else if (alt_open) { /* autre version de la jaquette */
      if (alt_state == 2) {
        if ((pressed & SCE_CTRL_UP) && alt_sel > 0) --alt_sel;
        if ((pressed & SCE_CTRL_DOWN) && alt_sel < alt_n - 1) ++alt_sel;
        if (pressed & SCE_CTRL_CROSS) alt_pick();
      }
      if ((pressed & SCE_CTRL_CIRCLE) && alt_state != 1 && alt_state != 3) alt_open = 0;
      pressed &= SCE_CTRL_START;
    } else if (em_open) {
      if ((pressed & SCE_CTRL_UP) && em_sel > 0) --em_sel;
      if ((pressed & SCE_CTRL_DOWN) && em_sel < em_n * 2 - 1) ++em_sel;
      if (pressed & SCE_CTRL_LEFT) em_change(-1);
      if (pressed & (SCE_CTRL_RIGHT | SCE_CTRL_CROSS)) em_change(+1);
      if (pressed & SCE_CTRL_TRIANGLE) em_all = !em_all; /* tous les coeurs, ou seulement ceux du systeme */
      if (pressed & SCE_CTRL_CIRCLE) {
        em_open = 0;
        if (em_dirty) do_rescan = 1; /* nouveaux coeurs / dossiers : on relit les jeux */
      }
      pressed &= SCE_CTRL_START;
    } else if (hid_open) {
      if ((pressed & SCE_CTRL_UP) && hid_sel > 0) --hid_sel;
      if ((pressed & SCE_CTRL_DOWN) && hid_sel < hid_n - 1) ++hid_sel;
      if ((pressed & SCE_CTRL_CROSS) && hid_n > 0) {
        set_hidden(hid_list[hid_sel], 0);
        hidden_refresh();
        snprintf(toast_text, sizeof(toast_text), "Jeu réaffiché");
        toast_time = 1.8f;
      }
      if ((pressed & SCE_CTRL_SQUARE) && hid_n > 0) {
        unhide_all();
        hidden_refresh();
        snprintf(toast_text, sizeof(toast_text), "Tous les jeux sont réaffichés");
        toast_time = 2.0f;
      }
      if (pressed & SCE_CTRL_CIRCLE) hid_open = 0;
      pressed &= SCE_CTRL_START;
    } else if (conf_open) {
      if ((pressed & SCE_CTRL_CROSS) && conf_kind == 4) { /* passer au theme Noel pour la saison */
        conf_open = 0;
        xmas_prev = opt_theme * 100 + opt_accent + 1;
        opt_theme = XMAS_THEME;
        if (themes[XMAS_THEME].accent >= 0) opt_accent = themes[XMAS_THEME].accent;
        apply_theme();
        settings_save();
      } else if ((pressed & SCE_CTRL_CROSS) && conf_kind == 1) {
        conf_open = 0;
        want_quit = 1; /* quitter (seulement par "Quitter" du menu de gauche : START ne quitte plus) */
      } else if ((pressed & SCE_CTRL_CROSS) && conf_kind == 3) {
        conf_open = 0;
        if (conf_arg >= 0 && conf_arg < MAX_CUSTOM_SYS && SLOT_USED(conf_arg)) {
          if (sys_remove(conf_arg) > 0) do_rescan = 1; /* ses ROM disparaissent de la bibliotheque */
          if (selected >= (int)SYSTEM_COUNT) selected = (int)SYSTEM_COUNT - 1;
          snprintf(toast_text, sizeof(toast_text), "Système retiré");
          toast_time = 2.0f;
        }
      } else if ((pressed & SCE_CTRL_CROSS) && conf_kind == 2) {
        conf_open = 0;
        if (conf_arg >= 0 && conf_arg < MAX_COLL && COLL_USED(conf_arg)) {
          coll_remove(conf_arg);
          snprintf(toast_text, sizeof(toast_text), "Collection supprimée");
          toast_time = 2.0f;
        }
      } else if (pressed & SCE_CTRL_CROSS) {
        delete_library();
        conf_open = 0;
        shown = prev_shown = NULL;
        bg_wait = NULL;
        selected = 0;
        reset_focus();
        snprintf(toast_text, sizeof(toast_text), "Bibliothèque supprimée");
        toast_time = 3.0f;
      } else if (pressed & SCE_CTRL_CIRCLE) {
        conf_open = 0;
      }
      pressed &= SCE_CTRL_START;
    }

    /* Les listes ont change (categorie, masquage) : on replace la selection. */
    if (lists_changed && !menu_open && !hid_open && !picker_open) {
      selected = 0;
      reset_focus();
      lists_changed = 0;
    }

    int up = (pressed & SCE_CTRL_UP) != 0, down = (pressed & SCE_CTRL_DOWN) != 0;
    int left = (pressed & SCE_CTRL_LEFT) != 0, right = (pressed & SCE_CTRL_RIGHT) != 0;
    int lt = (pressed & SCE_CTRL_LTRIGGER) != 0, rt = (pressed & SCE_CTRL_RTRIGGER) != 0;
    int cross = (pressed & SCE_CTRL_CROSS) != 0, circle = (pressed & SCE_CTRL_CIRCLE) != 0;

    /* ---- Deplacement d'une tuile de systeme (Carre sur la page Jeux) : gauche / droite, puis X, O ou Carre ---- */
    if (sys_move_si >= 0) {
      if (page != PG_GAMES || games_level != 0 || g_zone != ZONE_CONTENT || sys_move_si >= (int)SYSTEM_COUNT) {
        sys_move_si = -1;
      } else {
        int p = sys_pos(sys_move_si), q = p + (right || rt ? 1 : 0) - (left || lt ? 1 : 0);
        if (q != p && q >= 0 && q < (int)SYSTEM_COUNT) {
          sys_order[p] = sys_order[q];
          sys_order[q] = sys_move_si;
          selected = q;
        }
        if (cross || circle || (pressed & SCE_CTRL_SQUARE)) {
          sys_move_si = -1;
          sys_order_save();
          snprintf(toast_text, sizeof(toast_text), "Ordre des systèmes enregistré");
          toast_time = 1.8f;
        }
        up = down = left = right = lt = rt = cross = circle = 0;
        pressed &= ~(SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE | SCE_CTRL_SELECT);
      }
    }

    /* ---- Navigation : menu de gauche (ZONE_RAIL) ou page centrale (ZONE_CONTENT) ---- */
    if (g_zone == ZONE_RAIL) {
      /* Menu de gauche : haut/bas changent de page, droite ou X entrent dans la page. */
      if (rail_quit) {
        if (up) rail_quit = 0;
        if (down) { rail_quit = 0; page = rail_ids[0]; }
        if (cross) { conf_kind = 1; conf_open = 1; } /* Quitter : confirmation */
      } else {
        if (up) { /* au-dessus du premier : le cadre de recherche de la barre du haut */
          if (rail_pos(page) == 0) g_zone = ZONE_SEARCH;
          else page = rail_ids[rail_pos(page) - 1];
        }
        if (down) {
          if (rail_pos(page) == rail_n - 1) rail_quit = 1;
          else page = rail_ids[rail_pos(page) + 1];
        }
        if (right || cross) {
          g_zone = ZONE_CONTENT;
          did_enter = 1;
        }
      }
    } else if (g_zone == ZONE_SEARCH) { /* cadre de recherche : X ouvre le clavier */
      if (cross) {
        search_open(&page);
        did_enter = 1;
      }
      if (down || left || circle) g_zone = ZONE_RAIL;
    } else if (page == 3) {
      /* Reglages : menu principal (4 groupes), puis sous-menu ; gauche/droite/X changent la valeur. */
      if (set_group < 0) {
        if (up && set_sel > 0) --set_sel;
        if (down && set_sel < SET_GROUPS - 1) ++set_sel;
        if (right || cross) {
          set_group = set_sel;
          set_sel = 0;
          setting_row = set_row_at(set_group, 0);
        }
        if (circle || left) g_zone = ZONE_RAIL;
      } else if (set_group == SG_ABOUT) { /* A propos : rien a changer, O revient au menu des reglages */
        if (circle) {
          set_sel = set_group;
          set_group = -1;
        }
      } else {
      if (up && set_sel > 0) --set_sel;
      if (set_sel > set_count(set_group) - 1) set_sel = set_count(set_group) - 1; /* ligne Neige retiree (theme change) */
      if (down && set_sel < set_count(set_group) - 1) ++set_sel;
      setting_row = set_row_at(set_group, set_sel);
      int is_action = setting_row == SR_WALL || setting_row == SR_DOWNLOAD || setting_row == SR_DELETE ||
                      setting_row == SR_SCAN || setting_row == SR_HIDDEN || setting_row == SR_EMU ||
                      setting_row == SR_NET_STATE || setting_row == SR_NET_IP || setting_row == SR_NET_TEST;
      if (left && !is_action) change_setting(setting_row, -1);
      if (right || cross) {
        if (setting_row == SR_WALL) {
          wp_open_list();
        } else if (setting_row == SR_DOWNLOAD) {
          dl_start(NULL);
        } else if (setting_row == SR_DELETE) {
          conf_kind = 0;
          conf_open = 1;
        } else if (setting_row == SR_SCAN) {
          do_rescan = 1;
        } else if (setting_row == SR_EMU) {
          em_open_win();
        } else if (setting_row == SR_NET_TEST) {
          net_test_start();
        } else if (setting_row == SR_NET_STATE || setting_row == SR_NET_IP) {
          /* information seulement */
        } else if (setting_row == SR_HIDDEN) {
          hid_sel = 0;
          hidden_refresh();
          hid_open = 1;
        } else {
          change_setting(setting_row, +1);
        }
      }
      if (circle) { /* retour au menu des reglages */
        set_sel = set_group;
        set_group = -1;
      }
      }
    } else {
      if (in_games && opt_sort != 1 && n > 0 && (lt || rt)) { /* L / R : lettre precedente / suivante */
        selected = letter_jump(list, n, selected, rt ? 1 : -1);
        lt = rt = 0;
      }
      int list_mode = in_games && opt_view == VIEW_LIST && n > 0;
      int grid_mode = in_games && opt_view == VIEW_GRID && n > 0;
      int cat_mode = in_games && opt_view == VIEW_CATALOG && n > 0;
      if (list_mode) {
        if (up && selected > 0) --selected;
        if (down && selected < n - 1) ++selected;
        if (left) g_zone = ZONE_RAIL;
        if (lt) selected = selected >= 8 ? selected - 8 : 0;
        if (rt) selected = selected + 8 < n ? selected + 8 : n - 1;
      } else if (grid_mode) {
        if (up && selected >= cols) selected -= cols;
        if (down) {
          if (selected + cols < n) selected += cols;
          else if (selected / cols < (n - 1) / cols) selected = n - 1;
        }
        if (left) {
          if (selected % cols > 0) --selected;
          else g_zone = ZONE_RAIL;
        }
        if (right && selected % cols < cols - 1 && selected + 1 < n) ++selected;
        if (lt && selected > 0) --selected;
        if (rt && selected < n - 1) ++selected;
      } else if (cat_mode && cat_grid) { /* catalogue, grille : Haut sur la 1re ligne ou O revient au carrousel */
        if (up) {
          if (selected >= cols) selected -= cols;
          else cat_grid = 0;
        }
        if (down) {
          if (selected + cols < n) selected += cols;
          else if (selected / cols < (n - 1) / cols) selected = n - 1;
        }
        if (left) {
          if (selected % cols > 0) --selected;
          else g_zone = ZONE_RAIL;
        }
        if (right && selected % cols < cols - 1 && selected + 1 < n) ++selected;
        if (lt && selected > 0) --selected;
        if (rt && selected < n - 1) ++selected;
        if (circle) {
          cat_grid = 0;
          circle = 0; /* O ne quitte pas la liste : il revient seulement au carrousel */
        }
      } else if (cat_mode) { /* catalogue, carrousel : Bas passe a la grille (le jeu choisi est garde) */
        if (left) {
          if (selected > 0) --selected;
          else g_zone = ZONE_RAIL;
        }
        if (right && selected < n - 1) ++selected;
        if (lt && selected > 0) --selected;
        if (rt && selected < n - 1) ++selected;
        if (down) cat_grid = 1;
      } else if ((page == PG_GAMES || page == PG_COLL) && games_level == 0 && opt_sys_mode == 2) { /* tuiles sur 2 lignes */
        int c3 = 3;
        if (up && selected >= c3) selected -= c3;
        if (down) {
          if (selected + c3 < n) selected += c3;
          else if (selected / c3 < (n - 1) / c3) selected = n - 1;
        }
        if (left) {
          if (selected % c3 > 0) --selected;
          else g_zone = ZONE_RAIL;
        }
        if (right && selected % c3 < c3 - 1 && selected + 1 < n) ++selected;
      } else { /* accueil, carrousel, choix du systeme */
        if (left) {
          if (selected > 0) --selected;
          else g_zone = ZONE_RAIL;
        }
        if (right && selected < n - 1) ++selected;
        if (lt && selected > 0) --selected;
        if (rt && selected < n - 1) ++selected;
      }
      if (cross && page == 1 && games_level == 0) {
        if (selected < (int)SYSTEM_COUNT) {
          cur_system = sys_order[selected];
          games_level = 1;
          selected = 0;
          page_alpha = 0.0f;
          page_slide = 24.0f;
          reset_focus();
          did_enter = 1;
        } else {
          sm_sel = 0;
          sm_refresh();
          sm_open = 1;
        }
      }
      if (cross && page == PG_COLL && games_level == 0) {
        if (selected < coll_n) { /* ouvrir la collection */
          cur_coll = selected;
          games_level = 1;
          selected = 0;
          page_alpha = 0.0f;
          page_slide = 24.0f;
          reset_focus();
          did_enter = 1;
        } else if (coll_n >= MAX_COLL) {
          snprintf(toast_text, sizeof(toast_text), L("Nombre maximum de collections atteint (%d)"), MAX_COLL);
          toast_time = 2.2f;
        } else {
          kb_start_ex(KB_COLL_NEW, 0, NULL);
        }
      }
      if ((pressed & SCE_CTRL_SQUARE) && page == PG_GAMES && games_level == 0 && selected < (int)SYSTEM_COUNT) {
        sys_move_si = sys_order[selected]; /* la tuile suit gauche / droite jusqu'a X, O ou Carre */
        snprintf(toast_text, sizeof(toast_text), "Gauche / Droite : déplacer   X : terminer");
        toast_time = 2.5f;
      } else if (pressed & SCE_CTRL_SQUARE) {
        if (in_games) {
          opt_view = (opt_view + 1) % VIEW_COUNT;
          cat_grid = 0;
          cat_t = 0.0f;
          {
            int vc = view_ctx(page, games_level, cur_system);
            if (vc >= 0) {
              view_of[vc] = opt_view;
              view_user[vc] = 1; /* choix de l'utilisateur : on le garde */
            }
          }
          settings_save();
          snprintf(toast_text, sizeof(toast_text), L("Affichage : %s"), L(view_names[opt_view]));
          toast_time = 1.5f;
        }
      }
      if (circle) {
        if ((page == PG_GAMES || page == PG_COLL) && games_level == 1) { /* retour au choix */
          games_level = 0;
          selected = page == PG_COLL ? cur_coll : sys_pos(cur_system);
          page_alpha = 0.0f;
          page_slide = 24.0f;
          reset_focus();
        } else if (page == PG_SEARCH) { /* fin de la recherche : retour a la page d'avant */
          page = search_prev_page;
        } else {
          g_zone = ZONE_RAIL;
        }
      }
    }
    if (pressed & SCE_CTRL_TRIANGLE) want_info = 1;
    /* START sur la tuile d'un systeme ajoute : le retirer (apres confirmation) */
    if ((pressed & SCE_CTRL_START) && page == PG_GAMES && games_level == 0 && g_zone == ZONE_CONTENT &&
        selected < (int)SYSTEM_COUNT && sys_move_si < 0 && !conf_open && !menu_open && !picker_open && !sm_open &&
        !kb_open && !hid_open && !ct_open && !cm_open && !em_open && !alt_open && !info_open && !dl_open && !wp_open) {
      int si = sys_order[selected];
      if (si < 2) {
        snprintf(toast_text, sizeof(toast_text), "PS Vita et PSP ne peuvent pas être retirés");
        toast_time = 2.2f;
      } else {
        conf_kind = 3;
        conf_arg = sys_slot[si];
        conf_open = 1;
      }
    }
    if (want_quit) break;

    { /* sons : X = valider, O = retour (si quelque chose a change), sinon deplacement */
      unsigned ui_after = UI_STATE();
      if (keys_in & SCE_CTRL_CROSS) snd_play(SND_OK);
      else if ((keys_in & SCE_CTRL_CIRCLE) && ui_after != ui_before) snd_play(SND_BACK);
      else if (ui_after != ui_before) snd_play(SND_MOVE);
    }
#undef UI_STATE

    /* ---- Scan de la bibliotheque (Reglages) ---- */
    if (do_rescan) {
      draw_splash("Lecture de la bibliothèque...");
      rescan_library();
      preload_first_images();
      shown = prev_shown = NULL;
      bg_wait = NULL;
      selected = 0;
      reset_focus();
      if (scan_error < 0 && app_n == 0)
        snprintf(toast_text, sizeof(toast_text),
                 "Accès à ux0:app refusé (0x%08X) : mode unsafe requis", (unsigned)scan_error);
      else if (demo_mode)
        snprintf(toast_text, sizeof(toast_text), "Aucune application trouvée (mode démo)");
      else
        snprintf(toast_text, sizeof(toast_text), L(hb_n > 1 ? "%d Vita, %d PSP, %d homebrews trouvés" : "%d Vita, %d PSP, %d homebrew trouvés"),
                 vita_n, psp_n, hb_n);
      toast_time = 3.0f;
    }

    if (page != prev_page) {
      set_group = -1;
      set_sel = 0;
      selected = 0;
      page_alpha = 0.0f;
      page_slide = 24.0f;
      reset_focus();
      if (page == PG_GAMES) {
        games_level = 0;
        selected = sys_pos(cur_system);
      } else if (page == PG_COLL) {
        games_level = 0;
        selected = cur_coll;
      }
    }

    list = current_list(page, games_level, cur_system, &n);
    if (selected >= n) selected = 0;
    in_games = (page == PG_HB) || (page == PG_FAV) || (page == PG_SEARCH) ||
               ((page == PG_GAMES || page == PG_COLL) && games_level == 1);
    sync_view(page, games_level, cur_system);
    int is_chooser = (page == PG_GAMES || page == PG_COLL) && games_level == 0;
    Game *hero = (list && n > 0 && page != PG_SETTINGS) ? list[selected] : NULL;

    /* Changement de jeu : fondu enchaine du fond (sans icone intermediaire). */
    if (hero != shown) {
      /* On garde en dessous le fond le plus visible a l'ecran : en faisant defiler vite, le fond
       * ne retombe plus a zero a chaque jeu traverse (plus de clignotement), le nouveau apparait
       * par-dessus des qu'il est pret. */
      float vis_cur = bd_alpha, vis_prev = prev_shown ? prev_base * (1.0f - bd_alpha) : 0.0f;
      Game *dropped;
      if (vis_cur >= vis_prev || !prev_shown) {
        dropped = prev_shown;
        prev_shown = shown;
        prev_base = vis_cur;
      } else {
        dropped = shown;
        prev_base = vis_prev;
      }
      shown = hero;
      hero_alpha = 0.0f;
      bd_alpha = 0.0f;
      if (dropped && dropped != shown && dropped != prev_shown) release_bg(dropped);
      bg_wait = (shown && shown->has_bg && !shown->bg) ? shown : NULL;
      bg_timer = 0.2f;
    }
    if (bg_wait) { /* le grand fond n'est charge que si la selection se stabilise */
      bg_timer -= dt;
      if (bg_timer <= 0.0f) {
        if (bg_wait != shown) bg_wait = NULL;
        else if (bg_request(bg_wait)) bg_wait = NULL;
      }
    }

    ++g_frame;
    lj_poll(shown, prev_shown);
    tex_evict(); /* images des jeux qui ne sont plus affiches, au-dela de TEX_BUDGET */
    cover_pump(list, n, selected);
    for (int i = 0; i < app_n; ++i) /* fondu d'apparition des images arrivees (0,2 s) */
      if (all_apps[i].appear < 1.0f) {
        all_apps[i].appear += dt * 5.0f;
        if (all_apps[i].appear > 1.0f) all_apps[i].appear = 1.0f;
      }
    int can_pick_sys = is_chooser && page == PG_GAMES && selected < (int)SYSTEM_COUNT;
    int can_pick_coll = is_chooser && page == PG_COLL && selected < coll_n;
    int can_pick_game = page != PG_SETTINGS && hero && hero->id[0] && !is_chooser;

    /* ---- Triangle : infos du jeu, sinon raccourci fond on/off ---- */
    if (want_info) {
      if (can_pick_game && g_zone == ZONE_CONTENT) {
        info_game = hero;
        info_open = 1;
      } else {
        change_setting(0, 1);
        snprintf(toast_text, sizeof(toast_text), "%s", L(opt_backdrop ? "Fond : jaquette" : "Fond : uni"));
        toast_time = 1.5f;
      }
    }

    /* ---- Options d'un jeu / choix d'image d'un systeme (touche SELECT) ---- */
    if ((pressed & SCE_CTRL_SELECT) && !picker_open && !menu_open && !sm_open && page != 3) {
      if (can_pick_sys) {
        pick_bg = 0;
        picker_open_for(NULL, sys_order[selected]);
      } else if (can_pick_coll) {
        cm_slot = coll_ids[selected];
        cm_sel = 0;
        cm_open = 1;
      } else if (can_pick_game) {
        menu_game = hero;
        menu_sel = 0;
        menu_open = 1;
      } else {
        snprintf(toast_text, sizeof(toast_text), "Pas d'options pour cet élément");
        toast_time = 1.8f;
      }
    }

    /* ---- Lancement d'un jeu ---- */
    if (cross && g_zone == ZONE_CONTENT && page != 3 && !did_enter && hero && !picker_open &&
        !menu_open && !sm_open) {
      try_launch(hero);
    }

    /* ---- Mise a jour des animations ---- */
    {
      /* la surbrillance s'allume doucement mais s'eteint vite (pas de double surbrillance) ;
       * coefficients calcules une fois par image, cases deja a 0 ignorees */
      float k_on = 1.0f - expf(-18.0f * dt), k_off = 1.0f - expf(-45.0f * dt);
      for (int i = 0; i < MAX_APPS; ++i) {
        if (g_zone == ZONE_CONTENT && i == selected) focus[i] += (1.0f - focus[i]) * k_on;
        else if (focus[i] > 0.0005f) focus[i] -= focus[i] * k_off;
        else focus[i] = 0.0f;
      }
    }
    page_alpha = approach(page_alpha, 1.0f, 12.0f, dt);
    page_slide = approach(page_slide, 0.0f, 14.0f, dt);
    hero_alpha = approach(hero_alpha, 1.0f, 8.0f, dt);
    if (!shown || backdrop_ready(shown)) bd_alpha = approach(bd_alpha, 1.0f, 7.0f, dt);
    rail_y = approach(rail_y, 94 + rail_pos(page) * 46, 18.0f, dt);
    if (toast_time > 0.0f) toast_time -= dt;

    picker_update(dt);
    wp_update(dt);

    float pa = page_alpha;
    g_pa = pa;
    g_ha = hero_alpha;
    g_ox = (int)page_slide;

    tex_free_flush(); /* textures remplacees a l'image precedente (une seule attente du GPU) */
    /* La reserve de sommets de vita2d est reutilisee a chaque image : si le GPU dessine encore l'image
     * precedente, ses formes sont ecrasees en cours de route (triangles parasites quand l'ecran est
     * charge : fond, fenetre d'options...). On attend donc la fin du dessin precedent. */
    vita2d_wait_rendering_done();
    vita2d_start_drawing();
    vita2d_clear_screen();
    draw_bg_gradient();
    hit_n = 0;

    /* ================= Fond dynamique (optionnel) ================= */
    if (opt_backdrop) {
      if (shown) draw_backdrop(shown, bd_alpha);
      if (prev_shown && bd_alpha < 0.99f)
        draw_backdrop(prev_shown, prev_base * (1.0f - bd_alpha));
      if (shown || prev_shown) draw_backdrop_overlay();
    }

    if (xmas_on() && opt_snow) snow_fall(dt); /* theme Noel : derriere les menus et les cartes */

    /* ================= Barre de menu du haut (verre depoli) ================= */
    grad2(RAIL_W, 0, SCREEN_W - RAIL_W, BAR_H, with_alpha(T.bar, 215), with_alpha(T.bar, 150), 0);
    if (opt_gloss && !T.light) grad2(RAIL_W, 0, SCREEN_W - RAIL_W, 22, C(255, 255, 255, 16), C(255, 255, 255, 0), 0);
    fill_rect(RAIL_W, BAR_H, SCREEN_W - RAIL_W, 1, T.bar_line);

    int hx = spaced(ST_REG, CONTENT_X, 31, FS_SMALL, T.bar_text, "TA COLLECTION",
                    SPACING, 1);
    vita2d_draw_fill_circle(hx + 7, 25, 2, T.muted);
    hx = spaced(ST_REG, hx + 18, 31, FS_SMALL, fade(T.bar_text, pa),
                header_names[page], SPACING, 1);
    if ((page == PG_GAMES || page == PG_COLL) && games_level == 1) {
      char upper[40], crumb[48];
      const char *src = page == PG_GAMES ? systems[cur_system].tag : coll_name[coll_ids[cur_coll]];
      int o = 0;
      for (const char *c = src; *c && o < (int)sizeof(upper) - 1; ++c) /* majuscules (ASCII) */
        upper[o++] = (*c >= 'a' && *c <= 'z') ? *c - 32 : *c;
      upper[o] = 0;
      fit_text(ST_REG, FS_SMALL, upper, 260, crumb, sizeof(crumb));
      vita2d_draw_fill_circle(hx + 7, 25, 2, T.muted);
      spaced(ST_REG, hx + 18, 31, FS_SMALL, fade(T.acc_text, pa), crumb, SPACING, 1);
    }

    SceDateTime now;
    char clock_text[16] = "--:--";
    if (sceRtcGetCurrentClockLocalTime(&now) >= 0)
      snprintf(clock_text, sizeof(clock_text), "%02d:%02d", now.hour, now.minute);

    int battery = scePowerGetBatteryLifePercent();
    if (battery < 0) battery = 0;
    if (battery > 100) battery = 100;

    /* Batterie : pilule arrondie (verte + eclair en charge, orange a 20 % ou moins, rouge a 10 % ou moins) */
    int sx = CONTENT_R;
    int charging = scePowerIsBatteryCharging() > 0;
    unsigned int bcol = charging ? C(90, 200, 120, 255) : battery <= 10 ? C(222, 103, 112, 255)
                      : battery <= 20 ? C(240, 150, 60, 255) : T.acc;
    round_outline_t(sx - 31, 17, 30, 16, 5, 1.5f, T.muted);
    round_rect(sx - 28, 20, 24.0f * battery / 100.0f, 10, 3, bcol);
    round_rect(sx, 22, 3, 6, 1.5f, T.muted);
    if (charging) { /* eclair au milieu de la pile : contour sombre puis trait clair (petits disques le long du trait) */
      static const float bp[4][2] = {{2.5f, -5.5f}, {-2.5f, 0.5f}, {2.5f, -0.5f}, {-2.5f, 5.5f}};
      float cx = sx - 16, cy = 25;
      for (int pass = 0; pass < 2; ++pass)
        for (int s = 0; s < 3; ++s)
          for (int k = 0; k <= 8; ++k) {
            float u = k / 8.0f;
            vita2d_draw_fill_circle(cx + bp[s][0] + u * (bp[s + 1][0] - bp[s][0]), cy + bp[s][1] + u * (bp[s + 1][1] - bp[s][1]),
                                    pass ? 0.9f : 1.8f, pass ? C(255, 255, 255, 255) : C(20, 40, 28, 255));
          }
    }
    sx -= 30 + 8;
    { /* pourcentage, a gauche de la pile. Signe % dessine (celui de la police a une barre trop fine) :
       * deux anneaux et une barre de la meme epaisseur que les chiffres. */
      char batt_text[16];
      const float pw = 11.0f, st = 1.7f;
      unsigned int pc = T.text_soft;
      float px = sx - pw;
      draw_ring(px + 2.6f, 21.6f, 2.4f, st, pc);
      draw_ring(px + pw - 2.6f, 28.4f, 2.4f, st, pc);
      for (int k = 0; k <= 12; ++k) { /* barre : petits disques serres le long du trait */
        float u = k / 12.0f;
        vita2d_draw_fill_circle(px + pw - 1.2f - u * (pw - 2.4f), 19.0f + u * 12.0f, st / 2, pc);
      }
      snprintf(batt_text, sizeof(batt_text), "%d", battery);
      draw_text_right(ST_SEMI, (int)(px - 3), 31, FS_SMALL, pc, batt_text);
      sx = (int)(px - 3) - text_w(ST_SEMI, FS_SMALL, batt_text) - 14;
    }
    /* Wi-Fi : barres du vrai signal (estompees hors connexion) ; un toucher ouvre Reglages > Reseau */
    net_poll(dt);
    for (int b = 0; b < 4; ++b) {
      int bh = 5 + b * 3;
      round_rect(sx - 18 + b * 5, 31 - bh, 3, bh, 1.5f, b < net_bars ? T.text_soft : fade(T.text_soft, 0.3f));
    }
    hit_add(sx - 24, 8, 30, 32, H_NET, 0);
    sx -= 18 + 16;
    draw_text_right(ST_SEMI, sx, 32, FS_BODY, T.bar_text, clock_text);
    { /* cadre de recherche : loupe + texte recherche ; toucher ou Haut depuis "Accueil" puis X */
      float bw = 144, bh = 28, bx2 = sx - text_w(ST_SEMI, FS_BODY, clock_text) - 18 - bw, by2 = 11;
      int sf = g_zone == ZONE_SEARCH;
      if (sf) glow(bx2, by2, bw, bh, bh / 2, 1.0f, 1.0f);
      round_card(bx2, by2, bw, bh, bh / 2, sf ? T.sel_fill : with_alpha(T.bg, 110), sf ? T.acc : T.card_border, 1.0f);
      draw_set_icon(ICO_SEARCH, bx2 + 17, by2 + bh / 2, sf ? T.acc_text : T.muted);
      char sb[40];
      fit_text(ST_REG, FS_SMALL, search_q[0] ? search_q : "Rechercher", (int)bw - 44, sb, sizeof(sb));
      draw_text(ST_REG, (int)bx2 + 32, (int)by2 + 20, FS_SMALL, search_q[0] ? T.text : T.muted, sb);
      hit_add(bx2, by2, bw, bh, H_SEARCH, 0);
    }

    /* ================= Rail de navigation (panneau translucide) ================= */
    /* il descend jusqu'en bas de l'ecran ; le pied de page commence a sa droite */
    { /* avec le clavier (recherche, renommer), le menu s'arrete au pied de page pleine largeur :
       * le fond d'ecran reste visible derriere les aides, comme a droite */
      float rh = kb_open ? FOOTER_Y : 544;
      grad2(0, 0, RAIL_W, rh, with_alpha(T.bar, 175), with_alpha(T.bar, kb_open ? 128 : 120), 0);
      grad2(RAIL_W - 18, 0, 18, rh, with_alpha(T.bar, 0), with_alpha(T.bar, 60), 1);
      fill_rect(RAIL_W - 1, 0, 1, rh, T.line);
    }

    { /* logo : nom de l'appli (VF_APP_NAME), un point entre ses deux mots */
      char la[32], lb[32];
      app_logo(la, lb, sizeof(la));
      int bx = spaced(ST_SEMI, 24, 32, FS_BODY, T.text, la, 3, 1);
      if (lb[0]) {
        vita2d_draw_fill_circle(bx + 1, 25, 2.5f, T.acc);
        spaced(ST_SEMI, bx + 7, 32, FS_BODY, T.text, lb, 3, 1);
      }
    }

    /* Pastille de la page courante : lumineuse quand le menu a le focus, discrete sinon. */
    if (g_zone != ZONE_RAIL) rail_quit = 0;
    int rail_focus = g_zone == ZONE_RAIL && !rail_quit;
    if (rail_focus) glow(8, rail_y - 20, 142, 40, 14, 1.0f, 1.0f);
    int on_quit = g_zone == ZONE_RAIL && rail_quit; /* Quitter a le focus : seule sa pastille s'allume */
    if (page != PG_SEARCH && !on_quit) /* la recherche n'est pas dans le menu : pas de pastille */
      round_rect(8, rail_y - 20, 142, 40, 14, rail_focus ? T.sel_fill : fade(T.sel_fill, 0.55f));
    if (rail_focus) round_outline_t(8, rail_y - 20, 142, 40, 14, 1.5f, T.acc);
    if (!on_quit) round_rect(12, rail_y - 10, 3, 20, 1.5f, T.acc);

    for (int i = 0; i < rail_n; ++i) {
      int pg = rail_ids[i];
      int cy = 94 + i * 46;
      int active = pg == page && !on_quit; /* sur Quitter, la page courante s'eteint aussi */
      hit_add(8, cy - 20, 142, 40, H_RAIL, pg);
      { /* icone du menu : fichier menu_xxx.png s'il existe, sinon dessin du code */
        static const int rail_ico[7] = {ICO_M_HOME, ICO_M_GAMES, ICO_M_HB, ICO_M_SETTINGS, ICO_M_FAV, ICO_M_COLL, ICO_M_SEARCH};
        unsigned int ic = active ? T.acc_text : T.muted;
        int xk = !xmas_on() ? -1 : pg == PG_HOME ? 0 : pg == PG_COLL ? 1 : pg == PG_HB ? 2 : -1;
        if (xk >= 0 && xmas_icon(xk, 29, cy, 20.0f, ic)) { /* theme Noel : sapin, cadeau, flocon */
        } else if (draw_ico_file(rail_ico[pg], 29, cy, 20.0f, ic)) {
        } else if (pg == PG_FAV) {
          draw_heart_ex(29, cy, 19, ic, 1);
        } else {
          draw_set_icon(rail_ico[pg], 29, cy, ic);
        }
      }
      { /* le nom reste dans la pastille (138 de large) : coupe avec "..." s'il est trop long (autre langue) */
        char ml[40];
        fit_text(active ? ST_SEMI : ST_REG, FS_BODY, menu[pg], 150 - 4 - 43, ml, sizeof(ml));
        draw_text(active ? ST_SEMI : ST_REG, 43, cy + 6, FS_BODY, active ? T.text : T.text_soft, ml);
      }
    }
    if (!kb_open) { /* bouton Quitter, tout en bas du menu (sous le pied de page pendant la saisie : cache) */
      float qy = 490; /* le menu descend jusqu'au bas de l'ecran */
      int qf = g_zone == ZONE_RAIL && rail_quit;
      hit_add(8, qy, 142, 40, H_QUIT, 0);
      if (qf) {
        glow(8, qy, 142, 40, 14, 1.0f, 1.0f);
        round_rect(8, qy, 142, 40, 14, T.sel_fill);
        round_outline_t(8, qy, 142, 40, 14, 1.5f, T.acc);
      }
      /* couleurs des autres entrees du menu : icone accent / attenuee, texte clair / doux */
      draw_set_icon(ICO_QUIT, 29, qy + 20, qf ? T.acc_text : T.muted); /* quitter.png, sinon symbole marche/arret */
      draw_text(qf ? ST_SEMI : ST_REG, 43, qy + 26, FS_BODY, qf ? T.text : T.text_soft, "Quitter");
    }
    if (xmas_on()) { /* theme Noel : houx en haut a droite et en bas a gauche du menu */
      holly_at(96, 46, 60, 1, -12, 1.0f);
      holly_at(-10, 416, 58, 0, 18, 1.0f);
    }

    /* ================= Contenu des pages ================= */
    if (page == PG_SETTINGS) {
      draw_settings(setting_row);
    } else if (page == PG_GAMES && games_level == 0) {
      draw_chooser(selected, (int)SYSTEM_COUNT + 1, "Choisis un système", 0);
    } else if (page == PG_COLL && games_level == 0) {
      draw_chooser(selected, coll_n + 1, "Tes collections", 1);
      if (coll_n == 0) {
        draw_text(ST_REG, CONTENT_X + g_ox, 448, FS_BODY, fade(T.text_soft, pa),
                  "Regroupe tes jeux comme tu veux (GTA, Rétro, À finir...).");
        draw_text(ST_REG, CONTENT_X + g_ox, 474, FS_SMALL, fade(T.muted, pa),
                  "Crée une collection ici, puis SELECT sur un jeu > Collections.");
      }
    } else if (page == PG_HOME) {
      if (n == 0)
        draw_empty("Accueil", "Aucune application trouvée",
                   "Installe des jeux ou des homebrews, ils apparaîtront ici.");
      else
        draw_view_tile(list, n, selected, 1, "Récemment joués", "Lancer le jeu",
                       "POUR PARCOURIR");
    } else {
      /* Jeux d'un systeme (page 1) ou homebrews (page 2) : 3 modes d'affichage. */
      static char stitle[64];
      if (page == PG_SEARCH) snprintf(stitle, sizeof(stitle), "« %s »", search_q);
      const char *title = page == PG_GAMES ? systems[cur_system].name
                          : page == PG_COLL ? coll_name[coll_ids[cur_coll]]
                          : page == PG_SEARCH ? stitle
                          : page == PG_FAV ? "Favoris" : "Applications installées";
      const char *button = page == PG_HB ? "Lancer" : "Lancer le jeu";
      static const char *view_hint[VIEW_COUNT] = {
          "AFFICHAGE : CARROUSEL", "AFFICHAGE : GRILLE", "AFFICHAGE : LISTE", "AFFICHAGE : CATALOGUE"};
      if (n == 0) {
        if (page == PG_HB)
          draw_empty(title, "Aucun homebrew installé", "Les applications installées apparaîtront ici.");
        else if (page == PG_FAV)
          draw_empty(title, "Aucun favori", "Ajoute un jeu en favori depuis ses options.");
        else if (page == PG_SEARCH)
          draw_empty("Recherche", search_q[0] ? "Aucun jeu trouvé" : "Tape le nom d'un jeu",
                     "X sur « Recherche » dans le menu de gauche pour chercher.");
        else if (page == PG_COLL) {
          static char e2[96];
          snprintf(e2, sizeof(e2), L("Aucun jeu dans « %s »"), title);
          draw_empty(title, e2, "Ajoute un jeu avec SELECT > Collections.");
        }
        else if (cur_system == 0)
          draw_empty(title, "Aucun jeu PS Vita installé", "Les jeux installés apparaîtront ici.");
        else if (cur_system == 1)
          draw_empty(title, "Aucun jeu PSP trouvé",
                     "Un jeu PSP ou PS1 doit avoir sa bulle sur l'écran d'accueil.");
        else {
          static char e1[80];
          snprintf(e1, sizeof(e1), L("Aucun jeu dans « %s »"), systems[cur_system].name);
          draw_empty(title, e1, "Range un jeu ici avec SELECT > Catégorie.");
        }
      } else if (opt_view == VIEW_GRID) {
        draw_view_grid(list, n, selected, title, view_hint[opt_view]);
      } else if (opt_view == VIEW_LIST) {
        draw_view_list(list, n, selected, button);
      } else if (opt_view == VIEW_CATALOG) {
        draw_view_catalog(list, n, selected, title, button, dt);
      } else {
        draw_view_tile(list, n, selected, 0, title, button, view_hint[opt_view]);
      }
    }

    if (in_games && n >= 8 && opt_sort != 1 && list && page != PG_SETTINGS) draw_letter_bar(list, n, selected);
    else g_lb_a = 0.0f;
    if (menu_open) draw_menu();
    if (sm_open) draw_sysmgr();
    if (em_open) draw_emu();
    if (alt_open) draw_alt();
    if (ct_open) draw_coll_toggle();
    if (cm_open) draw_coll_menu();
    if (kb_open) draw_keyboard();
    if (info_open) draw_info();
    if (dl_open) draw_download();
    if (hid_open) draw_hidden();
    if (conf_open) draw_confirm();
    if (picker_open) draw_picker();
    if (wp_open) draw_wp_picker();

    /* ================= Toast (retour visuel des touches) ================= */
    if (toast_time > 0.0f) {
      float a = toast_time > 0.3f ? 1.0f : toast_time / 0.3f;
      float rise = toast_time > 0.3f ? 0.0f : (0.3f - toast_time) * 20.0f; /* glisse vers le bas en disparaissant */
      char tbuf[96];
      fit_text(ST_REG, FS_BODY, toast_text, 640, tbuf, sizeof(tbuf));
      int w = text_w(ST_REG, FS_BODY, tbuf) + 48;
      int x = (SCREEN_W + RAIL_W - w) / 2;
      float ty = 462 + rise;
      soft_shadow(x + 3, ty + 6, w - 6, 34, 18, 12, fade(C(0, 0, 0, 110), a));
      round_card(x, ty, w, 38, 19, T.sel_fill, T.acc, a);
      draw_text(ST_REG, x + 24, (int)ty + 25, FS_BODY, fade(T.text, a), tbuf);
    }

    /* ================= Pied de page (aides selon le contexte) ================= */
    if (info_open) goto footer_end; /* fenetre Details : elle a ses propres aides, pas de pied de page */
    { /* a droite du menu (pleine largeur avec le clavier : recherche, renommer, nommer) */
      int fx0 = kb_open ? 0 : RAIL_W;
      grad2(fx0, FOOTER_Y, SCREEN_W - fx0, 544 - FOOTER_Y, with_alpha(T.bar, 130), with_alpha(T.bar, 210), 0);
      if (fx0) fill_rect(fx0, FOOTER_Y, SCREEN_W - fx0, 1, T.line); /* clavier : pas de trait */
    }
    unsigned int fcol = T.text_soft;
    int fy = 532, fcy = 526;
    if (!kb_open) draw_text_right(ST_REG, CONTENT_R, fy, FS_SMALL, T.muted, VF_VERSION); /* clavier : pas de version */
    int fx = RAIL_W + 24;
    int content = g_zone == ZONE_CONTENT;
    if (kb_open) { /* clavier (recherche, renommer, nommer) : memes aides ; resultats de la recherche */
      int res = kb_focus && kb_mode == KB_SEARCH;
      int srch = kb_mode == KB_SEARCH; /* renommer / nommer : L/R curseur, START OK */
      int hend = draw_hints(24, fy, FS_BODY, fcol,
                          res ? "X Détails;O Clavier"
                          : srch ? "X Sélectionner;S Effacer;T Espace;O Annuler;L Maj.;E Tout effacer"
                                 : "X Sélectionner;S Effacer;T Espace;O Annuler;C Curseur;E Tout effacer");
      /* R OK (START OK) : seulement s'il reste de la place (pas de numero de version avec le clavier) */
      int okw = (srch ? 36 : text_w(ST_REG, FS_BODY, "START") + 24) + text_w(ST_REG, FS_BODY, "OK");
      if (!res && hend + okw <= CONTENT_R)
        draw_hints(hend, fy, FS_BODY, fcol, srch ? "R OK" : "B OK");
      content = 0;
      goto footer_end;
    }
    const char *x_label = !content || is_chooser ? "Ouvrir" : page == PG_SETTINGS ? "Changer" : "Lancer";
    if (!(content && page == PG_SETTINGS && set_group == SG_ABOUT)) { /* A propos : rien a valider */
      draw_btn(fx + 10, fcy, 'X', fcol);
      draw_text(ST_REG, fx + 26, fy, FS_BODY, fcol, x_label);
      fx += 26 + text_w(ST_REG, FS_BODY, x_label) + 26;
    }
    if (content) {
      const char *c_label = (((page == PG_GAMES || page == PG_COLL) && games_level == 1) ||
                             (page == PG_SETTINGS && set_group >= 0)) ? "Retour" : "Menu";
      draw_btn(fx + 10, fcy, 'O', fcol);
      draw_text(ST_REG, fx + 26, fy, FS_BODY, fcol, c_label);
      hit_add(fx - 8, fcy - 16, 36 + text_w(ST_REG, FS_BODY, c_label), 32, H_BACK, 0);
      fx += 26 + text_w(ST_REG, FS_BODY, c_label) + 26;
      if (in_games || can_pick_sys) {
        const char *sq_label = in_games ? "Affichage" : sys_move_si >= 0 ? "Terminer" : "Déplacer";
        draw_btn(fx + 10, fcy, 'S', fcol);
        draw_text(ST_REG, fx + 26, fy, FS_BODY, fcol, sq_label);
        fx += 26 + text_w(ST_REG, FS_BODY, sq_label) + 26;
      }
      if (can_pick_game && !picker_open && !menu_open) {
        draw_btn(fx + 10, fcy, 'T', fcol);
        draw_text(ST_REG, fx + 26, fy, FS_BODY, fcol, "Détails");
        fx += 26 + text_w(ST_REG, FS_BODY, "Détails") + 26;
      }
      if ((can_pick_sys || can_pick_coll || can_pick_game) && !picker_open && !menu_open) {
        const char *s_label = can_pick_sys ? "Image" : "Options";
        int sw = text_w(ST_REG, FS_BODY, "SELECT");
        round_outline_t(fx, fcy - 14, sw + 14, 26, 13, 1.2f, fcol);
        draw_text(ST_REG, fx + 7, fy, FS_BODY, fcol, "SELECT");
        draw_text(ST_REG, fx + sw + 14 + 10, fy, FS_BODY, fcol, s_label);
        fx += sw + 14 + 10 + text_w(ST_REG, FS_BODY, s_label) + 22;
      }
      if (can_pick_sys && sys_order[selected] >= 2 && !picker_open && !menu_open) { /* systeme ajoute : START retire */
        int sw = text_w(ST_REG, FS_BODY, "START");
        round_outline_t(fx, fcy - 14, sw + 14, 26, 13, 1.2f, fcol);
        draw_text(ST_REG, fx + 7, fy, FS_BODY, fcol, "START");
        draw_text(ST_REG, fx + sw + 14 + 10, fy, FS_BODY, fcol, "Retirer");
      }
    }
  footer_end:

    if (g_intro > 0.0f) { /* l'ecran de demarrage s'efface en fondu et laisse apparaitre le menu */
      float ia = g_intro * g_intro * (3.0f - 2.0f * g_intro); /* courbe douce */
      splash_content("", ia, g_time);
      g_intro -= dt * 3.0f;
    }

    if (opt_fps) { /* images par seconde + temps de calcul d'une image (hors attente de l'ecran) */
      static float fps_avg = 60.0f, cpu_avg;
      float raw = (now_us - prev_frame_us) / 1000000.0f;
      float cpu = (sceKernelGetProcessTimeWide() - now_us) / 1000.0f;
      prev_frame_us = now_us;
      if (raw > 0.0f && raw < 1.0f) fps_avg = fps_avg * 0.92f + (1.0f / raw) * 0.08f;
      cpu_avg = cpu_avg * 0.92f + cpu * 0.08f;
      char ft[48];
      snprintf(ft, sizeof(ft), "%2.0f FPS  %4.1f ms%s", fps_avg, cpu_avg, msaa_active ? "  AA" : "");
      int fw = text_w(ST_SEMI, FS_SMALL, ft) + 20; /* sous la batterie, hors de la barre du haut */
      round_rect(CONTENT_R - fw, BAR_H + 8, fw, 26, 13, C(0, 0, 0, 170));
      draw_text_center(ST_SEMI, CONTENT_R - fw / 2, BAR_H + 27, FS_SMALL,
                       fps_avg >= 55.0f ? C(120, 230, 140, 255) : fps_avg >= 40.0f ? C(245, 205, 60, 255) : C(240, 90, 90, 255), ft);
    }

    vita2d_end_drawing();
    vita2d_swap_buffers();
    if (g_intro <= 0.0f && splash_bg_tex) splash_bg_free(); /* ecran de demarrage parti : image liberee */
  }

  picker_free_preview();
  wp_free_preview();
  wall_free();
  free_app_textures();
  for (int i = 0; i < (int)SYSTEM_COUNT; ++i)
    if (systems[i].icon) vita2d_free_texture(systems[i].icon);
  for (int i = 0; i < ICO_N; ++i)
    if (set_ico[i]) vita2d_free_texture(set_ico[i]);
  for (int i = 0; i < FONT_SIZES; ++i) {
    if (fonts[0][i]) vita2d_free_font(fonts[0][i]);
    if (fonts[1][i]) vita2d_free_font(fonts[1][i]);
  }
  vita2d_fini();
  vita2d_free_pgf(g_pgf);
  sceKernelExitProcess(0);
  return 0;
}
