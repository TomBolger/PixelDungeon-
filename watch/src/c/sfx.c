/*
 * Pixel Dungeon for Pebble - sound: the settings, the engine overlay (src/ovl/snd) and the timer that keeps
 * the speaker fed. Nothing sounds for the first 2 seconds after launch, so there is time to mute.
 */
#include "pd.h"
#include "ovl.h"

#define KEY_SOUND 202                       // bit 0 music, bit 1 sound on, bits 2-3 volume (absent: on, loud)
#define QUIET_MS 2000
uint8_t g_sound = SOUND_MUSIC | SOUND_FX;
static const OvlHeader *s_snd;
static AppTimer *s_tm, *s_wake;
static int s_track = -1;                    // what should be playing (PD's Music.play)
static uint32_t s_quiet_until;

static uint32_t ms_now(void) { time_t s; uint16_t ms; time_ms(&s, &ms); return (uint32_t)s * 1000 + ms; }
static bool quiet(void) { return (int32_t)(ms_now() - s_quiet_until) < 0; }
static void load(void) {                    // the engine starts at full volume: set ours
  s_snd = ovl_keep(RESOURCE_ID_OVL_SND);
  if (s_snd) ((void (*)(int, int))s_snd->fn[0])(SNDC_VOL, SOUND_VOL(g_sound));
}
static void cmd(int c, int a) {
  if (!s_snd) load();
  if (s_snd) ((void (*)(int, int))s_snd->fn[0])(c, a);
}
static void pump(void *d) {
  s_tm = NULL;
  if (s_snd && ((bool (*)(uint32_t))s_snd->fn[1])(ms_now())) s_tm = app_timer_register(40, pump, NULL);
}
static void kick(void) { if (!s_tm) pump(NULL); }
void snd_poll(void) { if (s_tm) { app_timer_cancel(s_tm); pump(NULL); } }   // each frame too: a slow frame can't starve the speaker
static void wake(void *d) { s_wake = NULL; snd_music(s_track); }

void snd_music(int track) {
  s_track = track;
  if (quiet()) { if (!s_wake) s_wake = app_timer_register(s_quiet_until - ms_now() + 10, wake, NULL); return; }
  if ((g_sound & 3) == 3) { cmd(SNDC_MUSIC, track); kick(); }
  else if (s_snd) cmd(SNDC_MUSIC, -1);
}
void ui_sound(int id) {
  if (!(g_sound & SOUND_FX) || id < 0 || quiet()) return;
  cmd(SNDC_SFX, id); kick();
}
void snd_set(uint8_t mask) {
  g_sound = mask;
  persist_write_int(KEY_SOUND, mask);
  if (!(mask & SOUND_FX)) {                                 // all off: no engine, no timer, no CPU
    if (s_snd) { cmd(SNDC_STOP, 0); ovl_free(s_snd); s_snd = NULL; }
    if (s_tm) { app_timer_cancel(s_tm); s_tm = NULL; }
    return;
  }
  if (s_snd) cmd(SNDC_VOL, SOUND_VOL(mask));
  snd_music(s_track);
}
void snd_cycle(void) {
  uint8_t m = g_sound, v = SOUND_VOL(m);
  if (!(m & SOUND_FX)) m = (m & SOUND_MUSIC) | SOUND_FX;      // off -> loud
  else if (v < 2) m = (m & 3) | (v + 1) << 2;
  else m &= ~SOUND_FX;
  snd_set(m);
}
void snd_init(void) {
  if (persist_exists(KEY_SOUND)) { g_sound = persist_read_int(KEY_SOUND) & 15; if (SOUND_VOL(g_sound) > 2) g_sound &= 3; }
  s_quiet_until = ms_now() + QUIET_MS;
  if (g_sound & SOUND_FX) load();                       // loaded now, low in the heap: it stays, and must not split it later
}
void snd_hold(bool off) {                                 // the engine out of the heap while a floor is built (it needs one big block)
  if (off) { if (s_snd) { cmd(SNDC_STOP, 0); ovl_free(s_snd); s_snd = NULL; } if (s_tm) { app_timer_cancel(s_tm); s_tm = NULL; } }
  else if ((g_sound & SOUND_FX) && !s_snd) load();
}
void snd_deinit(void) { if (s_snd) cmd(SNDC_STOP, 0); }
