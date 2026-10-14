/* Pixel Dungeon for Pebble - the sound engine's commands (src/ovl/snd) */
#pragma once
#include "sfx_ids.auto.h"
enum { SNDC_MUSIC, SNDC_SFX, SNDC_STOP, SNDC_VOL };          // snd_cmd(cmd, arg): music track (-1 none), effect id, stop all, volume 0-2
enum { MUS_THEME, MUS_GAME };
