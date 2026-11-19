/*
 * areaplay_return.suprx - plugin taiHEN (utilisateur, section *main) d'Area Play Vita Edition.
 *
 * Quand un jeu lance depuis Area Play se ferme, Area Play revient a l'ecran au lieu de la LiveArea du jeu.
 *
 * Fonctionnement (aucune modification du systeme, aucun hook, aucun module noyau) :
 *  - Area Play ecrit son reglage dans ux0:data/AreaPlay/return.txt ("1" = active) ;
 *  - juste avant de lancer un jeu, il ecrit dans ux0:data/AreaPlay/jeu.txt l'identifiant (Title ID) de l'appli
 *    lancee (ou plusieurs, separes par des espaces : la bulle Adrenaline puis Adrenaline) ;
 *  - ce plugin, charge dans la LiveArea (SceShell), regarde toutes les secondes les applis ouvertes :
 *    une fois le jeu vu ouvert puis ferme, il efface jeu.txt et relance Area Play.
 *  Reglage desactive ou jeu.txt absent : le plugin ne fait rien (il attend).
 *  Journal : ux0:data/AreaPlay/return_log.txt (quelques lignes, efface s'il grossit).
 *
 * Securite : L maintenu au demarrage de la console = plugin non charge.
 * Prudence : il attend 20 s apres le demarrage de la console avant de faire quoi que ce soit.
 */
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

#define AP_ID "ARPL00001"
#define AP_DIR "ux0:data/AreaPlay/"
#define AP_URI "psgm:play?titleid=" AP_ID

static int str_len(const char *s) {
  int n = 0;
  while (s[n]) ++n;
  return n;
}
static int str_eq(const char *a, const char *b) {
  while (*a && *a == *b) ++a, ++b;
  return *a == *b;
}

/* Lit un petit fichier texte (n - 1 octets au plus). -1 s'il n'existe pas. */
static int read_small(const char *path, char *b, int n) {
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return -1;
  int r = sceIoRead(f, b, n - 1);
  sceIoClose(f);
  if (r < 0) r = 0;
  b[r] = 0;
  return r;
}

static void log_line(const char *msg) {
  const char *path = AP_DIR "return_log.txt";
  SceIoStat st;
  int flags = SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND;
  if (sceIoGetstat(path, &st) >= 0 && st.st_size > 16384) flags = SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC;
  SceUID f = sceIoOpen(path, flags, 0777);
  if (f < 0) return;
  sceIoWrite(f, msg, str_len(msg));
  sceIoWrite(f, "\r\n", 2);
  sceIoClose(f);
}

/* 1 si une des applis de la liste (Title ID separes par des espaces) est ouverte. */
static int listed_running(const char *list) {
  SceInt32 ids[16];
  int n = sceAppMgrGetRunningAppIdListForShell(ids, 16);
  if (n < 0) return -1;
  for (int i = 0; i < n && i < 16; ++i) {
    SceUID pid = sceAppMgrGetProcessIdByAppIdForShell(ids[i]);
    char name[32];
    for (int k = 0; k < 32; ++k) name[k] = 0;
    if (pid < 0 || sceAppMgrGetNameById(pid, name) < 0) continue;
    /* compare le nom a chaque identifiant de la liste */
    const char *p = list;
    while (*p) {
      while (*p == ' ') ++p;
      char id[16];
      int k = 0;
      while (*p && *p != ' ' && k < 15) id[k++] = *p++;
      id[k] = 0;
      while (*p && *p != ' ') ++p;
      if (k && str_eq(id, name)) return 1;
    }
  }
  return 0;
}

static volatile int running = 1;

static int watch_thread(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  sceKernelDelayThread(20 * 1000 * 1000); /* la console finit de demarrer tranquillement */
  log_line("areaplay_return 1.2 : pret");
  int seen = 0, waited = 0;
  char buf[96];
  while (running) {
    sceKernelDelayThread(1000 * 1000);
    if (read_small(AP_DIR "return.txt", buf, sizeof(buf)) <= 0 || buf[0] != '1') { /* desactive */
      seen = waited = 0;
      sceKernelDelayThread(4 * 1000 * 1000);
      continue;
    }
    if (read_small(AP_DIR "jeu.txt", buf, sizeof(buf)) <= 0) { /* aucun jeu lance depuis Area Play */
      seen = waited = 0;
      continue;
    }
    int r = listed_running(buf);
    if (r < 0) continue;
    if (!seen) {
      if (r) {
        seen = 1;
        log_line("jeu ouvert");
      } else if (++waited > 90) { /* jamais ouvert (lancement refuse) : on oublie */
        sceIoRemove(AP_DIR "jeu.txt");
        waited = 0;
        log_line("jeu jamais vu ouvert : oublie");
      }
    } else if (!r) { /* le jeu vient de se fermer */
      sceIoRemove(AP_DIR "jeu.txt");
      seen = waited = 0;
      sceKernelDelayThread(800 * 1000);
      int e = sceAppMgrLaunchAppByUri(0xFFFFF, AP_URI);
      log_line(e >= 0 ? "jeu ferme : retour dans Area Play" : "jeu ferme : relance d'Area Play refusee");
    }
  }
  return 0;
}

static SceUID thid = -1;

int module_start(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir("ux0:data/AreaPlay", 0777);
  SceCtrlData pad;
  for (int k = 0; k < (int)sizeof(pad); ++k) ((char *)&pad)[k] = 0;
  sceCtrlPeekBufferPositive(0, &pad, 1);
  if (pad.buttons & SCE_CTRL_LTRIGGER) { /* securite : L maintenu au demarrage = plugin pas charge */
    log_line("areaplay_return : L maintenu au demarrage, plugin non charge");
    return SCE_KERNEL_START_NO_RESIDENT;
  }
  log_line("areaplay_return 1.2 : charge par la LiveArea (attente de 20 s)");
  thid = sceKernelCreateThread("areaplay_return", watch_thread, 0x10000100, 0x4000, 0, 0, NULL);
  int st = thid >= 0 ? sceKernelStartThread(thid, 0, NULL) : thid;
  if (st < 0) log_line("areaplay_return : fil de surveillance impossible");
  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  running = 0;
  return SCE_KERNEL_STOP_SUCCESS;
}

int _start(SceSize args, void *argp) __attribute__((weak, alias("module_start")));
