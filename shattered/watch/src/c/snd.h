/* Shattered PD on the watch - sound: Pixel Dungeon's engine (snd.c) and what drives it (sound.c). */
#pragma once
#include <pebble.h>
enum { SNDC_MUSIC, SNDC_SFX, SNDC_STOP, SNDC_VOL };          // snd_cmd(cmd, arg): music track (-1 none), effect id, stop all, volume 0-2
enum { MUS_THEME, MUS_GAME };
void snd_cmd(int cmd, int arg);
bool snd_pump(uint32_t now_ms);                              // feeds the speaker; false once idle 3 s (stream closed)
int snd_stats(int k);

// The phone decides what plays (C_SOUND): the settings (bit 0 music, bit 1 sound, bits 2-3 volume 0-2),
// the music track (-1 none), an effect (Pixel Dungeon's numbering, tools/build_audio.py there).
void sound_settings(int mask);
void sound_music(int track);
void sound_effect(int id);
void sound_poll(void);                                       // once a frame: a slow frame can't starve the speaker
void sound_init(void);
void sound_deinit(void);
