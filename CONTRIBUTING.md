# Contributing

Bug reports, fixes and bot strategy ideas are all welcome. Please open an issue first for anything large.

## Ground rules

- **Match the original game.** Changes to rules, numbers, generation or texts should match Pixel Dungeon 1.9.1 (`third_party/pixel-dungeon`), unless they deliberately make the tiny screen fairer. Name the Java class or method you followed in a comment.
- **Respect the budget.** The app image is almost at its 64 KB limit. New code normally goes into an overlay (`watch/src/ovl/`), and long text goes into `watch/resources/text/en.txt`. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
- **Don't break saves.** If the save layout changes, raise `SAVE_MAGIC` in `watch/src/ovl/floor/floor.c` and convert older saves on load. Players' runs must survive updates.
- **Don't edit generated files.** That includes files ending in `.auto.h`, `text_ids.h`, `text.bin`, `mobs.bin`, `item_defs.bin`, and the overlay and keep-table files listed in `.gitignore`. Change their sources and rebuild.

## Code style

The C code is compact on purpose: short lines of related statements, comments at the end of lines, and helpers kept `static`.

- Write comments for the reader: say *why*, and which PD source it follows.
- Tag notable changes with the version they arrived in (for example `// 0.54:`), so the history can be traced from the code.
- Anything an overlay calls in the app must be a real, non-static symbol declared in `game_int.h` or `items_int.h`. `tools/build_ovl.py` adds it to the keep table automatically.
- An overlay's `static` variables reset on every load. State that must last goes in `game.c` or the save.

## Before you send a change

1. **Build the watch app.** `cd watch && ./tools/build.sh` must end with `build OK`. Check that the app image stays under 64 KB and the free heap does not shrink without a reason.
2. **Run the bot.** `bot/build.sh && cd bot && RT=400 FK=3000 ./score.sh 40 4`. The round-trip and fork checks are on in this run, and there should be **no `VIOL[...]` lines**. For rule or strategy changes, compare the summary with the same games before your change.
3. **Compare with the original.** For combat or effect changes, run `bot/compare.sh 2000` and check that no |z| is above 3.
4. **Test on a watch or the emulator.** UI changes need a look on the screen. `scripts/emu_install.sh` and `scripts/emu_shot.sh` help on a headless machine.

## Commits and releases

- Keep commits focused, and describe the player-visible effect in the message.
- To make a release, run `scripts/package.sh <version>`. Set the version in `watch/package.json` and `watch/src/ovl/ui/ui.c` first.

## License

By contributing, you agree that your work is released under the GNU GPL v3, the same license as Pixel Dungeon and this port.
