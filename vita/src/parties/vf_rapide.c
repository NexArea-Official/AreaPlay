/* Area Play - Menu rapide, retour apres un jeu. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Menu rapide : START maintenu.
 *  Luminosite, volume, veille, retour a la LiveArea, redemarrer, eteindre ; batterie, heure et Wi-Fi en haut.
 * ====================================================================== */

static int qm_open, qm_sel;
static void tf_start(void); /* Transfert Wi-Fi (vf_transfert.c) */
enum { QM_BRIGHT, QM_VOLUME, QM_TRANSFER, QM_SLEEP, QM_LIVEAREA, QM_REBOOT, QM_OFF, QM_N }; /* O ou START ferment */

/* ---- Retour dans Area Play apres un jeu : plugin taiHEN areaplay_return.suprx ----
 * Area Play ecrit son choix dans ux0:data/AreaPlay/return.txt ("1" / "0") ; le plugin le lit. */
#define RETURN_PLUGIN "areaplay_return.suprx"
static int return_plugin_installed(void) {
  static const char *cfg[2] = {"ux0:tai/config.txt", "ur0:tai/config.txt"};
  static char buf[16384];
  for (int i = 0; i < 2; ++i) {
    int n = read_file(cfg[i], (unsigned char *)buf, (int)sizeof(buf) - 1);
    if (n <= 0) continue;
    buf[n] = 0;
    for (char *line = strtok(buf, "\r\n"); line; line = strtok(NULL, "\r\n")) {
      while (*line == ' ' || *line == '\t') ++line;
      if (*line == '#') continue; /* ligne desactivee */
      char *p = strstr(line, RETURN_PLUGIN);
      if (!p) continue;
      char path[160];
      snprintf(path, sizeof(path), "%.*s", (int)(p - line) + (int)strlen(RETURN_PLUGIN), line);
      if (file_exists(path)) return 1;
    }
  }
  return 0;
}
static void flag_save(const char *path, int on) {
  SceUID f = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (f < 0) return;
  sceIoWrite(f, on ? "1" : "0", 1);
  sceIoClose(f);
}
static void return_save(void) {
  flag_save(DATA_DIR "/return.txt", opt_return);
  sceIoRemove(DATA_DIR "/ps.txt"); /* ancien reglage du bouton PS (plugin 1.1) */
}
/* ---- Installation du plugin par Area Play (il est dans le VPK : app0:plugin) ----
 * 1) copie dans ux0:tai ; 2) config.txt lu par taiHEN (ux0:tai s'il existe, sinon ur0:tai) : copie de sauvegarde
 * <config>.areaplay.bak (la premiere fois), puis une seule ligne ajoutee sous *main (section creee si absente) ;
 * le nouveau fichier est ecrit a cote, relu, puis mis a la place (la sauvegarde revient en cas d'echec). */
#define RETURN_SRC "app0:plugin/" RETURN_PLUGIN
#define RETURN_DST "ux0:tai/" RETURN_PLUGIN
static int return_bundled(void) { return file_exists(RETURN_SRC); }
static int return_install(char *err, int en) {
  static char buf[65536], out[65536 + 256];
  char l[200];
  sceIoMkdir("ux0:tai", 0777);
  if (!tool_copy(RETURN_SRC, RETURN_DST)) {
    snprintf(err, en, "%s", L("Copie du plugin impossible"));
    return 0;
  }
  if (return_plugin_installed()) return 1; /* la ligne y est deja */
  const char *cfg = file_exists("ux0:tai/config.txt") ? "ux0:tai/config.txt" : "ur0:tai/config.txt";
  int n = read_file(cfg, (unsigned char *)buf, (int)sizeof(buf) - 1);
  if (n <= 0 || n >= (int)sizeof(buf) - 1) {
    snprintf(err, en, "%s", L("config.txt de taiHEN introuvable"));
    return 0;
  }
  buf[n] = 0;
  char bak[64], tmp[64];
  snprintf(bak, sizeof(bak), "%s.areaplay.bak", cfg);
  snprintf(tmp, sizeof(tmp), "%s.areaplay.tmp", cfg);
  if (!file_exists(bak) && !tool_copy(cfg, bak)) {
    snprintf(err, en, "%s", L("Sauvegarde de config.txt impossible"));
    return 0;
  }
  const char *nl = strstr(buf, "\r\n") ? "\r\n" : "\n";
  int o = 0, done = 0;
  for (const char *p = buf; *p;) { /* recopie ligne par ligne, la ligne du plugin apres "*main" */
    const char *e = p;
    while (*e && *e != '\n') ++e;
    int ln = (int)(e - p) + (*e ? 1 : 0);
    memcpy(out + o, p, ln);
    o += ln;
    const char *q = p;
    while (q < e && (*q == ' ' || *q == '\t')) ++q;
    if (!done && !strncmp(q, "*main", 5) && (q[5] == '\r' || q[5] == '\n' || q[5] == ' ' || q[5] == 0)) {
      if (!*e) o += snprintf(out + o, sizeof(out) - o, "%s", nl); /* *main sur la derniere ligne */
      o += snprintf(out + o, sizeof(out) - o, "%s%s", RETURN_DST, nl);
      done = 1;
    }
    p = e + (*e ? 1 : 0);
  }
  if (!done) { /* pas de section *main : ajoutee a la fin */
    if (o > 0 && out[o - 1] != '\n') o += snprintf(out + o, sizeof(out) - o, "%s", nl);
    o += snprintf(out + o, sizeof(out) - o, "*main%s%s%s", nl, RETURN_DST, nl);
  }
  SceUID f = sceIoOpen(tmp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  int ok = f >= 0 && sceIoWrite(f, out, o) == o;
  if (f >= 0) sceIoClose(f);
  ok = ok && read_file(tmp, (unsigned char *)buf, (int)sizeof(buf)) == o && !memcmp(buf, out, o);
  if (ok) {
    sceIoRemove(cfg);
    ok = sceIoRename(tmp, cfg) >= 0;
    if (!ok) tool_copy(bak, cfg); /* config.txt d'origine remis */
  }
  sceIoRemove(tmp);
  snprintf(l, sizeof(l), "plugin de retour : %s, ligne ajoutee dans %s %s", RETURN_DST, cfg, ok ? "ok" : "ECHEC");
  log_line(l);
  if (!ok) snprintf(err, en, "%s", L("Écriture de config.txt impossible"));
  return ok;
}
/* Interrupteur "Retour dans Area Play apres un jeu" : a l'activation sans plugin, proposition d'installation. */
static void return_toggle(void) {
  if (!opt_return && !return_plugin_installed() && return_bundled()) {
    conf_kind = 6;
    conf_open = 1;
    return;
  }
  opt_return = !opt_return;
  return_check();
}
/* Confirme : installe, active, redemarre la console (le plugin n'est charge qu'au demarrage). */
static void return_install_confirmed(void) {
  char err[96] = "";
  if (!return_install(err, sizeof(err))) {
    snprintf(toast_text, sizeof(toast_text), "%s", err);
    toast_time = 5.0f;
    return;
  }
  opt_return = 1;
  settings_save();
  return_save();
  scePowerRequestColdReset();
}

/* Option activee sans le plugin : message (a l'activation et au demarrage). */
static void return_check(void) {
  return_save();
  sceIoRemove(DATA_DIR "/retour.txt"); /* anciens noms (versions de test) */
  sceIoRemove(DATA_DIR "/retour_log.txt");
  { /* plugin deja copie dans ux0:tai mais plus ancien que celui du VPK : remplace (actif au prochain demarrage) */
    int sa, sb;
    unsigned ca = tool_crc(RETURN_SRC, &sa), cb = tool_crc(RETURN_DST, &sb);
    if (sa > 0 && sb > 0 && (sa != sb || ca != cb) && tool_copy(RETURN_SRC, RETURN_DST))
      log_line("plugin de retour : mis a jour dans " RETURN_DST);
  }
  if (opt_return && !return_plugin_installed()) {
    snprintf(toast_text, sizeof(toast_text), "%s", L("Plugin " RETURN_PLUGIN " requis (voir README)"));
    toast_time = 6.0f;
  }
}

/* ---- Luminosite (registre /CONFIG/DISPLAY/brightness, 21..65536) et volume (0..30) ---- */
static int qm_bright_get(int *maxv) {
  int m = 65536, v = 0;
  if (sceAVConfigGetDisplayMaxBrightness(&m) < 0 || m <= 0) m = 65536;
  if (sceRegMgrGetKeyInt("/CONFIG/DISPLAY", "brightness", &v) < 0) v = m / 2;
  *maxv = m;
  return v;
}
static void qm_bright_step(int dir) {
  int m, v = qm_bright_get(&m);
  int step = m / 10;
  v += dir * step;
  if (v < 21 + m / 20) v = 21 + m / 20; /* jamais un ecran noir */
  if (v > m) v = m;
  sceAVConfigSetDisplayBrightness(v);
  sceRegMgrSetKeyInt("/CONFIG/DISPLAY", "brightness", v); /* garde le reglage (comme le menu de la console) */
}
static int qm_volume_get(void) {
  int v = 0;
  if (sceAVConfigGetSystemVol(&v) < 0) v = -1;
  return v;
}
static void qm_volume_step(int dir) {
  int v = qm_volume_get();
  if (v < 0) return;
  v += dir * 2;
  if (v < 0) v = 0;
  if (v > 30) v = 30;
  sceAVConfigSetSystemVol(v);
}

static void qm_toggle(void) {
  qm_open = !qm_open;
  qm_sel = 0;
}

/* Une touche dans le menu rapide. Retourne 1 si l'appli doit quitter (Retour a la LiveArea). */
static int qm_input(unsigned int pressed) {
  if (pressed & SCE_CTRL_UP) qm_sel = (qm_sel + QM_N - 1) % QM_N;
  if (pressed & SCE_CTRL_DOWN) qm_sel = (qm_sel + 1) % QM_N;
  int dir = (pressed & SCE_CTRL_RIGHT) ? 1 : (pressed & SCE_CTRL_LEFT) ? -1 : 0;
  if (dir && qm_sel == QM_BRIGHT) qm_bright_step(dir);
  if (dir && qm_sel == QM_VOLUME) qm_volume_step(dir);
  if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) qm_open = 0;
  if (!(pressed & SCE_CTRL_CROSS)) return 0;
  switch (qm_sel) {
  case QM_BRIGHT: qm_bright_step(1); break;
  case QM_VOLUME: qm_volume_step(1); break;
  case QM_TRANSFER: qm_open = 0; tf_start(); break;
  case QM_SLEEP: qm_open = 0; scePowerRequestSuspend(); break;
  case QM_LIVEAREA: qm_open = 0; return 1;
  case QM_REBOOT: scePowerRequestColdReset(); break;
  case QM_OFF: scePowerRequestStandby(); break;
  default: qm_open = 0; break;
  }
  return 0;
}

static void draw_quick(void) {
  float w = 460, h = 446, x = (SCREEN_W - w) / 2, y = 40; /* marge sous la derniere ligne avant les aides */
  modal_panel(x, y, w, h, 170);
  draw_text(ST_SEMI, (int)x + 28, (int)y + 44, FS_SECTION, T.text, "Menu rapide");
  { /* heure, batterie, Wi-Fi */
    SceDateTime now;
    char info[96], clk[8] = "--:--";
    if (sceRtcGetCurrentClockLocalTime(&now) >= 0) snprintf(clk, sizeof(clk), "%02d:%02d", now.hour, now.minute);
    int bat = scePowerGetBatteryLifePercent(), mins = scePowerGetBatteryLifeTime();
    if (mins > 0 && !scePowerIsBatteryCharging())
      snprintf(info, sizeof(info), L("%s   ·   %d %% (%dh%02d)"), clk, bat, mins / 60, mins % 60);
    else
      snprintf(info, sizeof(info), "%s   ·   %d %%", clk, bat);
    draw_text_right(ST_REG, (int)(x + w - 28), (int)y + 44, FS_SMALL, T.muted, info);
    char ns[64];
    net_state_text(ns, sizeof(ns));
    wifi_bars(x + 37, y + 70, net_bars < 0 ? 0 : net_bars, T.muted);
    draw_text(ST_REG, (int)x + 56, (int)y + 76, FS_SMALL, T.muted, ns);
  }
  static const char *labels[QM_N] = {"Luminosité", "Volume", "Transfert Wi-Fi", "Mettre en veille", "Retour à la LiveArea",
                                     "Redémarrer la console", "Éteindre la console"};
  for (int i = 0; i < QM_N; ++i) {
    float ry = y + 96 + i * 42;
    int sel = i == qm_sel;
    modal_row(x + 20, ry, w - 40, 36, sel);
    draw_text(sel ? ST_SEMI : ST_REG, (int)x + 42, (int)ry + 24, FS_BODY, T.text, labels[i]);
    if (i == QM_BRIGHT || i == QM_VOLUME) {
      float pct;
      if (i == QM_BRIGHT) {
        int m, v = qm_bright_get(&m);
        pct = (float)v / (float)m;
      } else {
        int v = qm_volume_get();
        pct = v < 0 ? 0.0f : v / 30.0f;
      }
      float bw = 150, bx = x + w - 40 - bw, by = ry + 14;
      round_rect(bx, by, bw, 8, 4, T.card_border);
      if (pct > 0.03f) round_rect(bx, by, bw * (pct > 1 ? 1 : pct), 8, 4, T.acc);
      if (sel) {
        draw_tri(bx - 22, by + 4, 8, 12, -1, T.text_soft);
        draw_tri(bx + bw + 10, by + 4, 8, 12, 1, T.text_soft);
      }
    }
  }
  draw_hints((int)x + 28, (int)(y + h - 20), FS_SMALL, T.muted, "X Valider;. Gauche / Droite : régler;O Fermer");
}
