/* Area Play - Sons de navigation. Partie de main.c (incluse par lui, ne pas compiler seule). */
/* ======================================================================
 *  Sons de navigation
 *  Trois sons : deplacement, valider, retour. Par defaut ils sont fabriques par le code (petits
 *  "tic" doux) ; chacun peut etre remplace par un fichier WAV (16 bits, mono ou stereo,
 *  n'importe quelle frequence) :
 *    ux0:data/AreaPlay/sons/<nom>.wav   (sur la console)   puis
 *    app0:assets/sons/<nom>.wav          (dossier assets/sons du projet, avant compilation)
 *  noms : deplacement.wav, valider.wav, retour.wav
 *  Un thread audio joue le dernier son demande (un nouveau son coupe le precedent).
 * ====================================================================== */

#define SND_RATE 48000
#define SND_GRAIN 512 /* echantillons par envoi (~10 ms) */
#define SND_MAX_LEN (SND_RATE * 2) /* 2 s au plus par son */
enum { SND_MOVE = 0, SND_OK, SND_BACK, SND_N };
static const char *snd_names[SND_N] = {"deplacement", "valider", "retour"};
static short *snd_pcm[2 * SND_N]; /* SND_N sons normaux, puis les memes en clochettes (theme Noel) */
static int snd_len[2 * SND_N];
static volatile int snd_req = -1; /* son demande par le thread principal (-1 = rien) */
static int snd_port = -1;
static SceUID snd_thread = -1;
static int snd_ready;

/* Son fabrique : suite de notes (frequence, duree en ms), enveloppe douce, sinus + harmonique. */
static void snd_synth(int id, const float *freqs, const int *ms, int notes, float amp) {
  int total = 0;
  for (int i = 0; i < notes; ++i) total += SND_RATE * ms[i] / 1000;
  short *p = (short *)malloc((size_t)total * sizeof(short));
  if (!p) return;
  int o = 0;
  for (int i = 0; i < notes; ++i) {
    int n = SND_RATE * ms[i] / 1000;
    float ph = 0.0f, dph = 6.2831853f * freqs[i] / SND_RATE;
    for (int k = 0; k < n; ++k, ++o) {
      float t = (float)k / n;
      float env = (k < 96 ? k / 96.0f : 1.0f) * expf(-4.5f * t); /* attaque sans clic, decroissance rapide */
      float s = sinf(ph) + 0.25f * sinf(2.0f * ph);
      ph += dph;
      p[o] = (short)(s * env * amp * 26000.0f);
    }
  }
  snd_pcm[id] = p;
  snd_len[id] = total;
}

/* Theme Noel : clochette, comme VitaFlow Android (Sounds.kt bell) : partiels non harmoniques d'une cloche,
 * qui s'eteignent plus lentement. */
static void snd_bell(int id, const float *freqs, const int *ms, int notes, float amp) {
  int total = 0;
  for (int i = 0; i < notes; ++i) total += SND_RATE * ms[i] / 1000;
  short *p = (short *)malloc((size_t)total * sizeof(short));
  if (!p) return;
  int o = 0;
  for (int i = 0; i < notes; ++i) {
    int n = SND_RATE * ms[i] / 1000;
    float w = 6.2831853f * freqs[i] / SND_RATE;
    for (int k = 0; k < n; ++k, ++o) {
      float t = (float)k / n;
      float env = (k < 48 ? k / 48.0f : 1.0f) * expf(-5.0f * t);
      float s = sinf(w * k) + 0.45f * sinf(2.0f * w * k) * expf(-3.0f * t) + 0.3f * sinf(2.76f * w * k) * expf(-6.0f * t) +
                0.15f * sinf(5.4f * w * k) * expf(-9.0f * t);
      float v = s * env * amp * 17000.0f;
      p[o] = (short)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
    }
  }
  snd_pcm[id] = p;
  snd_len[id] = total;
}

/* Lit un fichier WAV (PCM 16 bits) et le convertit en mono 48 kHz. 1 si reussi. */
static int snd_load_wav(int id, const char *path) {
  SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0);
  if (f < 0) return 0;
  SceOff sz = sceIoLseek(f, 0, SCE_SEEK_END);
  sceIoLseek(f, 0, SCE_SEEK_SET);
  if (sz < 44 || sz > (8 << 20)) {
    sceIoClose(f);
    return 0;
  }
  unsigned char *b = (unsigned char *)malloc((size_t)sz);
  int ok = 0;
  if (b && sceIoRead(f, b, (int)sz) == (int)sz && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0) {
    int ch = 0, rate = 0, bits = 0, fmt = 0;
    const unsigned char *data = NULL;
    unsigned dlen = 0;
    for (unsigned pos = 12; pos + 8 <= (unsigned)sz;) { /* parcours des blocs "fmt " et "data" */
      unsigned clen = rd32(b + pos + 4);
      if (memcmp(b + pos, "fmt ", 4) == 0 && clen >= 16 && pos + 8 + 16 <= (unsigned)sz) {
        fmt = rd16(b + pos + 8);
        ch = rd16(b + pos + 10);
        rate = (int)rd32(b + pos + 12);
        bits = rd16(b + pos + 22);
      } else if (memcmp(b + pos, "data", 4) == 0) {
        data = b + pos + 8;
        dlen = clen;
        if (pos + 8 + dlen > (unsigned)sz) dlen = (unsigned)sz - pos - 8;
        break;
      }
      pos += 8 + clen + (clen & 1);
    }
    if (data && fmt == 1 && bits == 16 && (ch == 1 || ch == 2) && rate >= 8000 && rate <= 96000) {
      int frames = (int)(dlen / (unsigned)(2 * ch));
      int out_n = (int)((long long)frames * SND_RATE / rate);
      if (out_n > SND_MAX_LEN) out_n = SND_MAX_LEN;
      short *p = out_n > 0 ? (short *)malloc((size_t)out_n * sizeof(short)) : NULL;
      if (p) {
        for (int i = 0; i < out_n; ++i) { /* reechantillonnage simple + melange des canaux */
          int src = (int)((long long)i * rate / SND_RATE);
          if (src >= frames) src = frames - 1;
          const unsigned char *q = data + (size_t)src * 2 * ch;
          int s = (short)rd16(q);
          if (ch == 2) s = (s + (short)rd16(q + 2)) / 2;
          p[i] = (short)s;
        }
        free(snd_pcm[id]);
        snd_pcm[id] = p;
        snd_len[id] = out_n;
        ok = 1;
      }
    }
  }
  free(b);
  sceIoClose(f);
  if (!ok) {
    char l[160];
    snprintf(l, sizeof(l), "son illisible (WAV PCM 16 bits attendu) : %s", path);
    log_line(l);
  }
  return ok;
}

static int snd_worker(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  /* deux tampons en alternance : le materiel lit encore le precedent pendant qu'on remplit le suivant
   * (un seul tampon reecrit pendant la lecture = petit gresillement sur la console, pas sur l'emulateur) */
  static short bufs[2][SND_GRAIN];
  int cur = -1, pos = 0, bi = 0;
  for (;;) {
    int r = snd_req;
    if (r >= 0) { /* nouveau son : il remplace celui en cours */
      snd_req = -1;
      cur = r;
      pos = 0;
    }
    static const float vols[4] = {0.0f, 0.3f, 0.6f, 1.0f};
    float v = vols[opt_sound & 3];
    short *buf = bufs[bi];
    bi ^= 1;
    for (int i = 0; i < SND_GRAIN; ++i) {
      int s = 0;
      if (cur >= 0 && pos < snd_len[cur]) s = (int)(snd_pcm[cur][pos++] * v);
      buf[i] = (short)s;
    }
    if (cur >= 0 && pos >= snd_len[cur]) cur = -1;
    sceAudioOutOutput(snd_port, buf); /* bloque ~10 ms : rythme du thread */
  }
  return 0;
}

/* Prepare les sons et le thread audio (une seule fois, au premier son joue). */
static int snd_start(void) {
  if (snd_ready) return snd_ready > 0;
  snd_ready = -1;
  { /* sons fabriques */
    static const float mv_f[1] = {1760.0f}, ok_f[2] = {988.0f, 1480.0f}, bk_f[2] = {1318.0f, 880.0f};
    static const int mv_ms[1] = {28}, ok_ms[2] = {45, 70}, bk_ms[2] = {45, 70};
    snd_synth(SND_MOVE, mv_f, mv_ms, 1, 0.22f);
    snd_synth(SND_OK, ok_f, ok_ms, 2, 0.30f);
    snd_synth(SND_BACK, bk_f, bk_ms, 2, 0.26f);
    /* theme Noel : clochettes (memes sons qu'Android) */
    static const float bm_f[1] = {2093.0f}, bo_f[2] = {1568.0f, 2093.0f}, bb_f[2] = {2093.0f, 1568.0f};
    static const int bm_ms[1] = {90}, bo_ms[2] = {80, 220}, bb_ms[2] = {80, 200};
    snd_bell(SND_N + SND_MOVE, bm_f, bm_ms, 1, 0.20f);
    snd_bell(SND_N + SND_OK, bo_f, bo_ms, 2, 0.28f);
    snd_bell(SND_N + SND_BACK, bb_f, bb_ms, 2, 0.24f);
  }
  for (int i = 0; i < SND_N; ++i) { /* fichiers WAV de l'utilisateur, s'il y en a */
    char path[96];
    snprintf(path, sizeof(path), DATA_DIR "/sons/%s.wav", snd_names[i]);
    if (!file_exists(path)) snprintf(path, sizeof(path), "app0:assets/sons/%s.wav", snd_names[i]);
    if (file_exists(path)) snd_load_wav(i, path);
  }
  snd_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, SND_GRAIN, SND_RATE, SCE_AUDIO_OUT_MODE_MONO);
  if (snd_port < 0) {
    char l[64];
    snprintf(l, sizeof(l), "sons : port audio refuse (%08X)", (unsigned)snd_port);
    log_line(l);
    return 0;
  }
  int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
  sceAudioOutSetVolume(snd_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
  static const int prios[2] = {0x100000F0, 0x10000100}; /* un peu plus prioritaire que l'affichage */
  for (int t = 0; t < 2 && snd_thread < 0; ++t) {
    SceUID th = sceKernelCreateThread("vf_snd", snd_worker, prios[t], 0x4000, 0, 0, NULL);
    if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0) snd_thread = th;
    else if (th >= 0) sceKernelDeleteThread(th);
  }
  if (snd_thread < 0) {
    log_line("sons : thread audio impossible");
    return 0;
  }
  snd_ready = 1;
  return 1;
}

static void snd_play(int id) {
  if (!opt_sound || id < 0 || id >= SND_N) return;
  if (!snd_start()) return;
  if (ftc() && ftc()->bells && snd_pcm[SND_N + id]) id += SND_N; /* theme Noel : clochettes */
  if (!snd_pcm[id]) return;
  snd_req = id;
}
