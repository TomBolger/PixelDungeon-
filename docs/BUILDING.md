# Building

There are three things you can build:

| What | Needs | Command |
|---|---|---|
| The watch app | Pebble SDK, Python 3 | `watch/tools/build.sh` |
| The bot and host tests | gcc, Python 3 | `bot/build.sh` |
| The Java reference game | Java 21 | `tools/javapd/build.sh` |

Everything builds on Linux. On Windows, use **WSL** (Ubuntu) and run the commands there. On macOS, the watch app builds with the SDK, but the bot uses Linux-only calls (`/proc`, `fork`) and needs Linux or WSL.

## 1. Get the code

```sh
git clone --recursive https://github.com/TomBolger/PixelDungeon-.git
cd PixelDungeon-
```

If you cloned without `--recursive`, run `git submodule update --init`. The submodules hold Watabou's original source (`third_party/`). The build reads monster stats and descriptions from it, and the art and sound tools read its assets.

## 2. Install the tools

**Pebble SDK.** Follow the official instructions at <https://developer.repebble.com/sdk>. In short, install the `pebble` command line tool, then run `pebble sdk install latest`. The project is tested with Pebble Tool 5.0 and SDK 4.33.

**Python packages:**

```sh
pip install pillow numpy          # art and sound packing
```

You only need `librosa` (and the packages listed at the top of `watch/tools/audio/transcribe.py`) to redo the music transcription, which is rarely necessary.

**Bot:** `gcc` and `python3`. On Ubuntu or WSL: `sudo apt install build-essential python3`.

## 3. Build the watch app

```sh
cd watch
./tools/build.sh
```

The output is `watch/build/watch.pbw`, holding the Emery and Gabbro versions, plus logs in `watch/build/logs/`. A full build takes about a minute.

`build.sh` runs these steps in order:

1. Regenerates the text and monster tables (`resources/data/text.bin`, `mobs.bin`, `item_defs.bin`, `src/c/text_ids.h`, `src/c/mobs.auto.h`).
2. Runs `pebble build` once to get the app's final memory layout.
3. Compiles each code overlay in `src/ovl/` against that layout (`tools/build_ovl.py`).
4. Runs `pebble build` again so the package carries the fresh overlays.
5. Checks that the app image did not change between the two passes. If it did, the overlays would not match it, and the build stops.

It ends with `build OK` and prints the free heap for each platform. If a step fails, look at the matching log in `watch/build/logs/`.

### Size limits to watch

- **App image:** at most 64 KB. `pebble build` prints the size, and it is currently about 63.8 KB on Emery. New code should normally go into an overlay (see [ARCHITECTURE.md](ARCHITECTURE.md)).
- **Overlay images:** each must fit the 8 KB overlay arena. `build_ovl.py` prints each overlay's size and fails if one is too big.
- **Resources:** at most 1 MB per platform. The current pack is about 560 KB.

## 4. Install and run

**On a watch.** Open the Pebble phone app, enable the developer connection, and run:

```sh
pebble install --phone <phone-ip> build/watch.pbw
```

You can also send the `.pbw` file to your phone and open it with the Pebble app.

**In the emulator.**

```sh
pebble install --emulator emery build/watch.pbw
pebble logs --emulator emery          # the app's log output
```

On a headless Linux server, `scripts/emu_install.sh <pbw> [wipe]` starts a virtual display and installs the app, retrying if needed. `scripts/emu_shot.sh emery <name> [buttons…]` presses buttons and saves a screenshot.

## 5. Releases

```sh
scripts/package.sh 0.56
```

This writes two files to `dist/`:

- `pixel-dungeon-0.56.pbw`: the game. Update the version string in `watch/src/ovl/ui/ui.c` and `watch/package.json` first.
- `pixel-dungeon-0.56-testkit.pbw`: **PD Test Kit**, a separate app with its own save, built with `TEST_BUILD`. It starts at level 30 with every item laid out on the first two floors, for testing.

The script edits `package.json`, `pd.h` and `ui.c` for the test kit and always puts them back, even if the build fails.

`scripts/whatif.sh <label> <blob>` builds **PD What-If**: a test kit that starts from a frozen moment of a bot game instead of a new game. The bot writes these checkpoints (see [BOT.md](BOT.md)).

## 6. Regenerating art, sound and text

The converted assets are committed, so a normal build doesn't need these steps. Run them only when changing the art, sound or text.

| Change | Tool |
|---|---|
| Game texts | Edit `watch/resources/text/en.txt`; the next build picks it up. |
| Monster stats or names | `watch/tools/mobs.py` (reads the Java source in `third_party/`). |
| Monster sprites | `python3 tools/mobs.py art` |
| Item, tile and UI art | `tools/convert_art.py <pd-assets> <out>`, `tools/convert_regions.py`, then `tools/split_sheets.py` |
| Music and sound effects | `tools/build_audio.py` (music notes come from `tools/audio/*.json`) |
| A new raw resource | `tools/add_res.py NAME FILE` |

Set `PD_ROOT=/path/to/pixel-dungeon` to point the tools at a different copy of the original source.

## 7. Bot and Java reference

See [BOT.md](BOT.md). In short:

```sh
bot/build.sh && bot/score.sh 20       # build the bot, play 20 games
tools/javapd/build.sh                 # build the original Java game, headless
bot/compare.sh 2000                   # C port vs Java original, side by side
```
