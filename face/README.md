# Pixel Dungeon Face (watchface)

A watchface that plays Pixel Dungeon by itself. Same game code as `watch/` (nothing is forked); the face is a compile switch (`PD_FACE`).

- Build: `face/build.sh` (copies `watch/` to `face/work/`, sets the face's UUID and `PD_FACE`, runs `watch/tools/build.sh`). Output `face/pixel-dungeon-face.pbw`. Needs `PD_ROOT` pointing at a watabou/pixel-dungeon checkout when `third_party/pixel-dungeon` is empty.
- Test builds: `FACE_DEFINES=FACE_WINTEST` (starts holding the Amulet: shows the victory screen), `FACE_NEWGAME` (ignores the save), `FACE_LOGP` (logs every press).
- Code: `watch/src/c/face.inc` (clock, bursts, driving the windows), `face_brain.h` (rules on top of the Assist's target list, shared with the host test), cheats in `game.c` under `PD_FACE`.
- Host test: `cd bot; OUT=facetest MAIN=tests/facetest.c ./build.sh -DPD_FACE; D=1 build/facetest 12`.

## Settings (round 2)
- The face has a Clay settings page (phone side: `face/pkjs/`; `build.sh` fetches the pebble-clay script; the normal app has no JS). The phone sends the settings as one 12-byte array (`CFG`, same order as `FaceCfg` in face.inc) and the weather as three int16 tenths of a degree C (`WX`); the watch keeps both in persist storage.
- The big clock sits between the HUD and the hero (windows get a small clock at the top). Each HUD number (floor, keys, level, health, and the backpack's gold) is a slot: the game's number, or time, date, weather (Open-Meteo, phone location, refreshed every 45 min), steps, heart rate, battery, day, and more. "Clock inside the HUD" shows the time as the level and the date as the health.
- The button tags and menu button on the right edge are not drawn in the face.
- Test builds: `FACE_FAKE` (steps 8412, heart rate 72, weather 21.5 C), `FACE_BAGTEST` (starts with the backpack open), `FACE_LOGS` (log lines and the lowest free heap; the shipped face has no log lines, to save RAM).
