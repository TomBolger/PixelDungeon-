/*
 * Shattered PD on the watch - the sound engine, taken as it is from the Pixel Dungeon port (src/ovl/snd/snd.c there).
 * PD's music (transcribed into notes by tools/audio/transcribe.py) is played by a small synth: wavetables
 * measured from the original instruments, decaying notes, three noise drums. PD's sound effects are its
 * own samples, IMA ADPCM. Everything is mixed here into one 8 kHz 16-bit stream for the speaker.
 * Data formats: tools/build_audio.py.
 */
#include <pebble.h>
#include "snd.h"

#define RATE 8000
#define BLOCK 32                                  // samples per envelope / sequencer step (4 ms)
#define NV 12                                     // music voices (notes ring on, so transcriptions overlap a lot)
#define ND 3                                      // drum voices
#define NS 3                                      // sound effects at once
#define LEAD 1600                                 // 8 kHz samples kept queued ahead of the speaker (200 ms; 0.14: was 140, too little on a busy watch)
#define EVBUF 64
#define MHDR (12 + 256 + 8 + 4)                   // music header: magic, counts, 4 wavetables, 4 decays, 4 attacks

enum { V_FREE, V_TONE, V_HAT, V_SNARE, V_KICK };
typedef struct { uint8_t kind, tab, vel, code, att, age; uint16_t left, decay; uint32_t phase, inc; int32_t env, env0, astep; } Voice;
typedef struct { uint32_t pos, end; int32_t pred; int idx; uint8_t buf[64]; uint32_t buf_at; bool on; } Chan;
typedef struct { uint8_t wait, code, len, vel; } Ev;

static bool s_open;
static int16_t s_out[128];                                   // one render, 8 kHz
static Voice s_v[NV + ND];                                    // + two drum voices
static Chan s_c[NS];
static uint32_t s_rng = 0x1234567u;
static Voice *s_glv; static uint8_t s_glp, s_gln, s_gll; static int8_t s_glk;   // the lead's glide (code 133 marks the next note)
static uint16_t s_dmp;                                      // 0.43: decay of 'damped' notes (odd velocity), set by code 134; 0 = off
// music
static ResHandle s_mres; static int s_track = -1;
static int8_t s_tab[4][64]; static uint16_t s_decay[4]; static uint8_t s_attack[4];
static uint16_t s_nev, s_pluck; static uint32_t s_loop, s_ev_i, s_block, s_next_at;
static Ev s_ev[EVBUF]; static uint32_t s_ev_base = 0xFFFFFFFF;
static int s_mgain = 1700, s_sgain = 256;                    // /256: music sits under the effects
// effects
static ResHandle s_sres; static uint16_t s_nsfx;

static uint32_t note_inc(int n) {                            // equal temperament from A4 = 440 Hz, no floats
  static const uint16_t SEMI[12] = { 32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858 };   // 2^(k/12) Q15
  // inc(A4=69) = 440 / 8000 * 2^32 = 236223201
  int d = n - 69, oct = d >= 0 ? d / 12 : -((11 - d) / 12), k = d - oct * 12;
  uint64_t inc = (uint64_t)236223201u * SEMI[k] >> 15;
  return oct >= 0 ? (uint32_t)(inc << oct) : (uint32_t)(inc >> -oct);
}

static const int16_t STEPS[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
  73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
  963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
  7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t IDX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

// ---------------- music ----------------
static bool ev_at(uint32_t i, Ev *e) {
  if (i < s_ev_base || i >= s_ev_base + EVBUF) {
    s_ev_base = i;
    int n = s_nev - i < EVBUF ? s_nev - i : EVBUF;
    if (n <= 0) return false;
    resource_load_byte_range(s_mres, MHDR + 4 * i, (uint8_t *)s_ev, 4 * n);
  }
  if (i >= s_nev) return false;
  *e = s_ev[i - s_ev_base];
  return true;
}
static void music_start(int track) {
  s_track = track;
  for (int i = 0; i < NV + ND; i++) s_v[i].kind = V_FREE;
  if (track < 0) return;
  s_mres = resource_get_handle(track == 0 ? RESOURCE_ID_MUSIC_THEME : RESOURCE_ID_MUSIC_GAME);
  uint8_t h[MHDR];
  if (!s_mres || resource_load_byte_range(s_mres, 0, h, sizeof h) != sizeof h || h[0] != 'P' || h[1] != 'D' || h[2] != 'M' || h[3] != '1') { s_track = -1; return; }   // (no memcmp: it would join the app)
  memcpy(&s_nev, h + 4, 2); memcpy(&s_pluck, h + 6, 2); memcpy(&s_loop, h + 8, 4);
  memcpy(s_tab, h + 12, 256); memcpy(s_decay, h + 12 + 256, 8); memcpy(s_attack, h + 12 + 256 + 8, 4);
  s_ev_base = 0xFFFFFFFF; s_ev_i = 0; s_block = 0; s_dmp = 0;
  Ev e; s_next_at = ev_at(0, &e) ? e.wait : 0;
}
static Voice *grab(int lo, int hi) {                          // a free voice, else the quietest
  Voice *best = &s_v[lo];
  for (int i = lo; i < hi; i++) { if (s_v[i].kind == V_FREE) return &s_v[i]; if (s_v[i].env < best->env) best = &s_v[i]; }
  return best;
}
static void note_on(const Ev *e) {
  if (e->code == 0) return;
  if (e->code == 133) { s_glp = 1; s_glk = e->vel ? (int8_t)(e->vel << 4) >> 4 : 1; s_gll = e->vel >> 4 ? (e->vel >> 4) * 8 : 20; return; }
      // the next lead note glides in from k (low 4 bits, signed; 0: 1) semitones above (below if < 0) over n*8 blocks (high 4 bits; 0: 80 ms)
  if (e->code == 134) { s_dmp = 65535 - e->vel * 16; return; }   // damped notes from now on (the game's piano chords fade out)
  if (e->code >= 128 && e->code < 160) {                                    // drums: hat, snare, kick
    Voice *v = grab(NV, NV + ND);
    int k = e->code - 128;
    v->kind = k >= 3 ? V_HAT : V_HAT + k; v->vel = e->vel; v->env0 = 0; v->env = e->vel * 150; v->att = 0; v->code = 0;
    v->decay = k == 0 ? 54600 : k == 3 ? 63814 : k == 4 ? 65013 : 62340;   // hat 22 ms, snare/kick 80 ms, open hat 150 ms, cymbal 0.5 s
    v->left = 0xFFFF; v->phase = 0; v->inc = note_inc(40);   // kick: ~80 Hz, nearly steady (as in PD's theme)
    return;
  }
  // the same pitch still ringing: play it again on that voice, in phase (two copies would beat: a warble)
  Voice *v = NULL;
  for (int i = 0; i < NV; i++) if (s_v[i].kind == V_TONE && s_v[i].code == e->code) { v = &s_v[i]; break; }
  int pn = e->code >= 200 ? e->code - 160 : e->code >= 160 ? e->code - 120 : e->code;   // 160+: the lead (pitch + 120), 200+: with vibrato (+160)
  int t = e->code >= 160 || pn >= 84 ? 3 : pn < 52 ? 0 : pn < 67 ? 1 : 2;
  if (!v) { v = grab(0, NV); v->phase = 0; v->env = 0; }
  if (s_glp) { s_glv = v; s_gln = s_gll; s_glp = 0; }
  v->kind = V_TONE; v->tab = t; v->vel = e->vel; v->age = 0; v->decay = s_dmp && e->vel & 1 ? s_dmp : s_decay[t]; v->code = e->code;
  v->inc = note_inc(pn);
  int n = s_attack[t] ? s_attack[t] : 1;                     // rise to full over the instrument's attack time
  v->env0 = v->env; v->astep = (e->vel * (t == 3 || (s_dmp && pn < 40) ? 400 : 256) - v->env) / n; v->env += v->astep; v->att = n - 1;   // the lead stands out (1.56x), and the game's bass (0.43: a tiny speaker)
  v->left = e->len * 4;                                      // 16 ms units -> 4 ms blocks
  if (s_pluck) {                                             // plucked instruments: a short click of noise on the attack
    Voice *d = grab(NV, NV + ND);
    d->kind = V_HAT; d->env0 = 0; d->env = e->vel * s_pluck; d->decay = 29440; d->left = 0xFFFF; d->att = 0; d->code = 0;
  }
}
static void music_block(void) {                              // the sequencer, once per block
  if (s_track < 0) return;
  while (s_block >= s_next_at) {
    Ev e;
    if (!ev_at(s_ev_i, &e)) {                                // end of the piece: wait out its length, then loop
      if (s_block < s_loop) return;
      s_block -= s_loop; s_ev_i = 0;
      s_next_at = ev_at(0, &e) ? e.wait : 0;
      continue;
    }
    note_on(&e);
    s_ev_i++;
    Ev n; if (ev_at(s_ev_i, &n)) s_next_at += n.wait; else s_next_at = s_loop;
  }
  s_block++;
}

// ---------------- effects ----------------
static void sfx_start(int id) {
  if (!s_sres) s_sres = resource_get_handle(RESOURCE_ID_SFX);
  uint8_t h[12];
  if (!s_sres || id < 0) return;
  if (!s_nsfx) { uint8_t hd[8]; resource_load_byte_range(s_sres, 0, hd, 8); memcpy(&s_nsfx, hd + 4, 2); }
  if (id >= s_nsfx) return;
  resource_load_byte_range(s_sres, 8 + 12 * id, h, 12);
  Chan *c = &s_c[0];
  for (int i = 0; i < NS; i++) { if (!s_c[i].on) { c = &s_c[i]; break; } if (s_c[i].pos > c->pos) c = &s_c[i]; }   // else the oldest
  uint32_t off, n; int16_t first;
  memcpy(&off, h, 4); memcpy(&n, h + 4, 4); memcpy(&first, h + 8, 2);
  c->pos = off * 2; c->end = off * 2 + n; c->pred = first; c->idx = h[10]; c->buf_at = 0xFFFFFFFF; c->on = true;
}
static int sfx_sample(Chan *c) {                             // next decoded sample
  uint32_t byte = c->pos >> 1;
  if (byte < c->buf_at || byte >= c->buf_at + sizeof c->buf) { c->buf_at = byte; resource_load_byte_range(s_sres, byte, c->buf, sizeof c->buf); }
  int code = (c->buf[byte - c->buf_at] >> ((c->pos & 1) * 4)) & 15;
  int step = STEPS[c->idx], dq = step >> 3;
  if (code & 4) dq += step;
  if (code & 2) dq += step >> 1;
  if (code & 1) dq += step >> 2;
  c->pred += code & 8 ? -dq : dq;
  if (c->pred > 32767) c->pred = 32767; else if (c->pred < -32768) c->pred = -32768;
  c->idx += IDX[code & 7]; if (c->idx < 0) c->idx = 0; else if (c->idx > 88) c->idx = 88;
  if (++c->pos >= c->end) c->on = false;
  return c->pred;
}

// ---------------- mixing ----------------
static void render(int16_t *out, int n) {                    // n: a multiple of BLOCK
  for (int b = 0; b < n; b += BLOCK) {
    music_block();
    int32_t acc[BLOCK]; memset(acc, 0, sizeof acc);
    for (int i = 0; i < NV + ND; i++) {
      Voice *v = &s_v[i];
      if (v->kind == V_FREE) continue;
      int32_t e = v->env0, step = (v->env - v->env0) / BLOCK;
      const int8_t *tab = s_tab[v->kind == V_KICK ? 1 : v->tab];
      for (int j = 0; j < BLOCK; j++, e += step) {
        int s;
        if (v->kind == V_TONE || v->kind == V_KICK) {            // linear interpolation between table steps: a clean tone
          int i = v->phase >> 26, f = (v->phase >> 18) & 255;
          s = tab[i] + ((tab[(i + 1) & 63] - tab[i]) * f >> 8); v->phase += v->inc;
        }
        else { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; int nz = (int8_t)(s_rng >> 24);
               s = v->kind == V_HAT ? (nz - (int)(int8_t)v->phase) / 2 : nz; v->phase = (uint8_t)nz; }
        acc[j] += s * (e >> 4) >> 8;
      }
      // next block's envelope: decay while held, then release (about 50 ms)
      v->env0 = v->env;
      if (v->left) v->left--;
      if (v->att) { v->att--; v->env += v->astep; }
      else if (v->left) { v->env = (int32_t)((int64_t)v->env * v->decay >> 16);
        if (v->kind == V_TONE && v->tab == 3 && v->env < v->vel * 240) v->env = v->vel * 240; }   // the lead: a quick decay to a sustain level
      else v->env = v->env * 3 / 4;
      if (v->kind == V_KICK) v->inc = v->inc - v->inc / 128;
      if (v->kind == V_TONE && v->code >= 160) {             // the lead's pitch: glides, and vibrato (0.43: like PD's guitar,
        uint32_t b = note_inc(v->code >= 200 ? v->code - 160 : v->code - 120); int32_t d = 0;   // +-0.6 semitone, 4.1 Hz, after 0.12 s)
        v->age = v->age == 255 ? 134 : v->age + 1;            // (wraps by whole vibrato periods)
        if (v->code >= 200) { int q = (v->age + 46) % 61, tr = q < 30 ? q - 15 : 45 - q, dp = v->age < 30 ? 0 : v->age > 75 ? 45 : v->age - 30;
          d = (int32_t)(b >> 9) * tr * dp / 37; }
        if (v == s_glv && s_gln) { d += (int32_t)(b >> 4) * s_glk * s_gln / s_gll; s_gln--; }   // glide: k semitones, falling to 0
        v->inc = b + d;
      }
      if (v->env < 200 && v->env0 < 200 && !v->att) v->kind = V_FREE;
    }
    for (int j = 0; j < BLOCK; j++) {
      int32_t m = acc[j] * s_mgain >> 8, fx = 0;
      for (int c = 0; c < NS; c++) if (s_c[c].on) fx += sfx_sample(&s_c[c]);
      int32_t x = m + (fx * s_sgain >> 8);
      int32_t a = x < 0 ? -x : x;                              // 0.40: a soft knee above ~0.7 (1:3), so the music can be louder
      if (a > 22000) { a = 22000 + (a - 22000) / 3; if (a > 32767) a = 32767; x = x < 0 ? -a : a; }
      out[b + j] = x;
    }
  }
}
static bool busy(void) {
  if (s_track >= 0) return true;
  for (int c = 0; c < NS; c++) if (s_c[c].on) return true;
  for (int i = 0; i < NV + ND; i++) if (s_v[i].kind != V_FREE) return true;
  return false;
}

// The speaker plays at 16 kHz (each 8 kHz sample is doubled by interpolation): its buffer then holds only
// ~0.37 s, so even when our idea of how much is queued drifts from the truth, sound can never lag further.
// We keep about LEAD queued; when the speaker refuses data its buffer is full, which tells us exactly how
// much is queued, and the estimate is reset to that (the drift on a real watch made sound lag a second).
#define ORATE 16000
#define LEAD_O (LEAD * 2)
static int32_t s_q, s_cap, s_frac;                                   // output samples queued (estimate), buffer size (measured once)
static uint32_t s_last_ms, s_idle_ms, s_sync_ms; static uint16_t s_resyncs;
static int32_t s_rate = ORATE + ORATE / 50;                 // the speaker's real rate as learned; a little high at first,
                                                             // so we overfill slightly and get told the truth (a full buffer)
static int16_t s_prev;
static int16_t s_o16[256]; static int s_o16_n, s_o16_at;     // 16 kHz samples not yet accepted
static bool speaker_put(void) {                              // false: the speaker's buffer is full
  if (s_o16_at >= s_o16_n) {
    render(s_out, 128);
    for (int k = 0; k < 128; k++) { s_o16[2 * k] = (s_prev + s_out[k]) / 2; s_o16[2 * k + 1] = s_out[k]; s_prev = s_out[k]; }
    s_o16_n = 256; s_o16_at = 0;
  }
  int want = s_o16_n - s_o16_at;
  int w = speaker_stream_write(s_o16 + s_o16_at, want * 2) / 2;
  s_o16_at += w; s_q += w;
  return w == want;
}
// Returns false once there has been nothing to play for 3 s (the stream is then closed and the app stops calling).
bool snd_pump(uint32_t now) {
  bool b = busy();
  if (!s_open) {
    if (!b) return false;
    if (!speaker_stream_open(SpeakerPcmFormat_16kHz_16bit, 100)) return false;
    s_open = true; s_last_ms = now; s_q = 0; s_o16_n = s_o16_at = 0; s_idle_ms = 0;
    if (!s_cap) {                                            // first time: measure the buffer with silence
      memset(s_o16, 0, sizeof s_o16);
      for (int g = 0; g < 400; g++) { int w = speaker_stream_write(s_o16, sizeof s_o16) / 2; s_cap += w; if (w < 256) break; }
      s_q = s_cap; s_sync_ms = now;
      if (s_cap < 512) s_cap = 512;                          // (0.15: a small speaker buffer is no longer inflated: that starved it)
    }
  }
  int32_t el = (int32_t)(now - s_last_ms); s_last_ms = now;
  s_frac += el * s_rate; s_q -= s_frac / 1000; s_frac %= 1000; if (s_q < 0) s_q = 0;
  if (b) s_idle_ms = 0; else if ((s_idle_ms += el) > 3000) { speaker_stream_close(); s_open = false; return false; }
  int32_t lead = s_cap * 7 / 8 < LEAD_O ? s_cap * 7 / 8 : LEAD_O;   // as much as fits, up to 200 ms
  for (int guard = 0; guard < 32 && s_q < lead; guard++)
    if (!speaker_put()) {                                    // full: exactly s_cap queued
      int32_t err = s_cap - s_q, t = (int32_t)(now - s_sync_ms);   // samples we thought were played but weren't
      if (s_sync_ms && t > 2000) {                           // learn the rate (kept 0.05% high so we never starve it)
        int32_t r = s_rate - err * 1000 / t + ORATE * 5 / 10000;
        if (r < ORATE * 95 / 100) r = ORATE * 95 / 100; else if (r > ORATE * 105 / 100) r = ORATE * 105 / 100;
        s_rate = r;
      }
      if (err > s_cap / 4) s_resyncs++;
      s_sync_ms = now; s_q = s_cap;
      break;
    }
  return true;
}
int snd_stats(int k) { return k == 0 ? s_cap / (ORATE / 1000) : k == 1 ? s_resyncs : s_rate; }   // buffer ms, corrections, rate

void snd_cmd(int cmd, int arg) {
  switch (cmd) {
    case SNDC_MUSIC: if (arg != s_track) music_start(arg); break;
    case SNDC_SFX: sfx_start(arg); break;
    case SNDC_VOL: s_mgain = 1700 >> arg; s_sgain = 256 >> arg; break;   // 0.40: loud, medium (-6 dB), quiet (-12 dB)
    case SNDC_STOP: if (s_open) { speaker_stop(); speaker_stream_close(); s_open = false; } s_track = -1;
      for (int i = 0; i < NV + ND; i++) s_v[i].kind = V_FREE;
      for (int c = 0; c < NS; c++) s_c[c].on = false;
      s_o16_n = s_o16_at = 0; break;
  }
}

