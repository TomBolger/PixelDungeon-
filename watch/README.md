# watch/: the Pebble app

| Path | Contents |
|---|---|
| `src/c/` | The app image: core rules (`game.c`), screens and input (`main.c`), drawing (`blit.c`), the overlay loader (`ovl.c`) |
| `src/ovl/<name>/` | Code overlays, each loaded only when needed (level generator, items, regions, windows, sound…) |
| `resources/images/` | Converted Pixel Dungeon art |
| `resources/data/` | Packed tables, sprites, music and sound; the compiled overlays go here at build time |
| `resources/text/` | `en.txt` (every long text) and `mobs.txt` (generated) |
| `tools/` | The build (`build.sh`, `build_ovl.py`) and the art, sound, text and monster converters |
| `package.json`, `wscript` | Pebble project files |

Build with `./tools/build.sh`, which writes `build/watch.pbw`. See [../docs/BUILDING.md](../docs/BUILDING.md) and [../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md).
