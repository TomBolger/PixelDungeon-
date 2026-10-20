/*
 * Shattered PD on the watch - what drives the sound engine, as in the Pixel Dungeon port (sfx.c there): a timer
 * keeps the speaker fed while anything plays; nothing sounds for the first 2 seconds after launch.
 * The settings are kept on the phone (the game's own settings) and sent here.
 */
#include "snd.h"

#define QUIET_MS 2000
#define SOUND_MUSIC 1
#define SOUND_FX 2
static int s_mask = SOUND_MUSIC | SOUND_FX;
static AppTimer *s_tm, *s_wake;
static int s_track = -1, s_playing = -2;     // what should be playing, what the engine was told
static uint32_t s_quiet_until;

static uint32_t ms_now(void) { time_t s; uint16_t ms; time_ms(&s, &ms); return (uint32_t)s * 1000 + ms; }
static bool quiet(void) { return (int32_t)(ms_now() - s_quiet_until) < 0; }
static void pump(void *d) {
  s_tm = NULL;
  if (snd_pump(ms_now())) s_tm = app_timer_register(40, pump, NULL);
}
static void kick(void) { if (!s_tm) pump(NULL); }
void sound_poll(void) { if (s_tm) { app_timer_cancel(s_tm); pump(NULL); } }
static void wake(void *d) { s_wake = NULL; sound_music(s_track); }

void sound_music(int track) {
  s_track = track;
  if (quiet()) { if (!s_wake) s_wake = app_timer_register(s_quiet_until - ms_now() + 10, wake, NULL); return; }
  int want = (s_mask & 3) == 3 ? track : -1;               // music needs both: music on and sound on
  if (want != s_playing) { snd_cmd(SNDC_MUSIC, want); s_playing = want; }
  if (want >= 0) kick();
}
void sound_effect(int id) {
  if (!(s_mask & SOUND_FX) || id < 0 || quiet()) return;
  snd_cmd(SNDC_SFX, id); kick();
}
void sound_settings(int mask) {
  s_mask = mask & 15;
  if (!(s_mask & SOUND_FX)) {                               // all off: silence, and no timer
    snd_cmd(SNDC_STOP, 0); s_playing = -1;
    if (s_tm) { app_timer_cancel(s_tm); s_tm = NULL; }
    return;
  }
  int v = (s_mask >> 2) & 3; snd_cmd(SNDC_VOL, v > 2 ? 0 : v);
  sound_music(s_track);
}
void sound_init(void) { s_quiet_until = ms_now() + QUIET_MS; }
void sound_deinit(void) { snd_cmd(SNDC_STOP, 0); }
