# bot/: the dungeon bot and host tests

| Path | Contents |
|---|---|
| `src/deepbot.c` | The bot: plays whole games through the watch's game code, checks invariants every step, records replays |
| `src/bot.c` | The older random-play crash tester |
| `src/pebble.h`, `src/hoststubs.c`, `src/snd_host.c` | Stand-ins for the Pebble SDK and for the watch's `main.c`, so the game code runs on Linux |
| `tests/` | Small scenario programs (items, shops, saves, targeting, the Java comparison scenarios) |
| `tools/` | Result summaries, parameter tuning, snapshot drawing, the What-If blob converter |
| `runner/` | The Windows/WSL job runner for large batches, and `make_job.sh` |
| `build.sh`, `score.sh`, `goolab.sh`, `compare.sh` | Build, score a batch, Goo lab, C-vs-Java comparison |

Quick start: `./build.sh && ./score.sh 20`. Full guide: [../docs/BOT.md](../docs/BOT.md).
