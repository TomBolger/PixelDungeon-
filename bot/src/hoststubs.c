// host-only stand-ins for main.c functions the overlays call
#include "pd.h"
#include <stdlib.h>
bool huntress_unlocked(void) { return persist_exists(KEY_HUNTRESS); }
int g_ovl_need, g_ovl_big;
void snd_poll(void) {}
static char s_tb[720]; char *s_text_buf = s_tb;
uint32_t g_host_time = 12345; uint16_t g_host_ms;
uint8_t g_assist = 1;   // 0.51: the watch's Assist setting (ASSIST=0/1/2 in the bot's env, read by deepbot)
__attribute__((constructor)) static void assist_env(void) { const char *a = getenv("ASSIST"); if (a) g_assist = (uint8_t)atoi(a); }

