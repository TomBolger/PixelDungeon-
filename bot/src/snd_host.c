// Render the sound engine against a simulated speaker: ./snd_host out.wav <track> <seconds> <speaker Hz> [id@sec ...]
// The speaker has a 12 KB buffer and plays at the given rate (16000 nominal); prints the latency it sees.
#include "pd.h"
#include "ovl.h"
#include "snd.h"
extern const OvlHeader ovl_snd;
static FILE *wf; static uint32_t wn; static double dq, cap = 6000, rate = 16000;
bool speaker_stream_open(SpeakerPcmFormat f, uint8_t v) { return f == SpeakerPcmFormat_16kHz_16bit; }
uint32_t speaker_stream_write(const void *d, uint32_t n) {
  uint32_t room = (uint32_t)(cap - dq) * 2; if (n > room) n = room & ~1u;
  fwrite(d, 1, n, wf); wn += n; dq += n / 2; return n;
}
void speaker_stream_close(void) {}
void speaker_stop(void) {}
static void hdr(void) {
  uint32_t sz = wn; fseek(wf, 0, SEEK_SET);
  uint8_t h[44] = "RIFF....WAVEfmt ";
  uint32_t v = 36 + sz; memcpy(h + 4, &v, 4);
  uint32_t fl = 16; memcpy(h + 16, &fl, 4); uint16_t pcm = 1, ch = 1; memcpy(h + 20, &pcm, 2); memcpy(h + 22, &ch, 2);
  uint32_t sr = 16000, br = 32000; memcpy(h + 24, &sr, 4); memcpy(h + 28, &br, 4); uint16_t ba = 2, bits = 16; memcpy(h + 32, &ba, 2); memcpy(h + 34, &bits, 2);
  memcpy(h + 36, "data", 4); memcpy(h + 40, &sz, 4); fwrite(h, 1, 44, wf);
}
int main(int argc, char **argv) {
  wf = fopen(argv[1], "wb"); fseek(wf, 44, SEEK_SET);
  void (*cmd)(int, int) = ovl_snd.fn[0]; bool (*pump)(uint32_t) = ovl_snd.fn[1]; int (*stats)(int) = ovl_snd.fn[2];
  int track = atoi(argv[2]); double secs = atof(argv[3]); rate = atof(argv[4]);
  if (track >= 0) cmd(SNDC_MUSIC, track);
  int nfx = argc - 5; int fx_id[32]; double fx_t[32];
  for (int i = 0; i < nfx && i < 32; i++) sscanf(argv[5 + i], "%d@%lf", &fx_id[i], &fx_t[i]);
  double lat_sum = 0, lat_max = 0; int n = 0, starve = 0;
  for (uint32_t ms = 1000; ms < 1000 + secs * 1000; ms += 40) {
    for (int i = 0; i < nfx; i++) if (fx_t[i] >= 0 && ms >= 1000 + fx_t[i] * 1000) { cmd(SNDC_SFX, fx_id[i]); fx_t[i] = -1; }
    dq -= 40 * rate / 1000; if (dq < 0) { if (ms > 3000) starve++; dq = 0; }
    pump(ms);
    if (getenv("TRACE") && ms % 2000 == 0) printf("%u ms: queue %.0f ms\n", ms, dq / 16.0);
    if (ms > 3000) { double l = dq / 16.0; lat_sum += l; if (l > lat_max) lat_max = l; n++; }
  }
  hdr(); fclose(wf);
  printf("speaker %.0f Hz: latency avg %.0f ms max %.0f ms, starved %d of %d steps; engine: buffer %d ms, rate %d, corrections %d\n",
         rate, lat_sum / n, lat_max, starve, n, stats(0), stats(2), stats(1));
}
