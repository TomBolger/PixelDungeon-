# Pixel Dungeon Face (watchface)

A watchface that plays Pixel Dungeon by itself. Same game code as `watch/` (nothing is forked); the face is a compile switch (`PD_FACE`).

- Build: `face/build.sh` (copies `watch/` to `face/work/`, sets the face's UUID and `PD_FACE`, runs `watch/tools/build.sh`). Output `face/pixel-dungeon-face.pbw`. Needs `PD_ROOT` pointing at a watabou/pixel-dungeon checkout when `third_party/pixel-dungeon` is empty.
- Test builds: `FACE_DEFINES=FACE_WINTEST` (starts holding the Amulet: shows the victory screen), `FACE_NEWGAME` (ignores the save), `FACE_LOGP` (logs every press).
- Code: `watch/src/c/face.inc` (clock, bursts, driving the windows), `face_brain.h` (rules on top of the Assist's target list, shared with the host test), cheats in `game.c` under `PD_FACE`.
- Host test: `cd bot; OUT=facetest MAIN=tests/facetest.c ./build.sh -DPD_FACE; D=1 build/facetest 12`.

Round 2 (bot behaviour, toolbar, memory):
- Toolbar: the "?" button is a third quickslot (`Hero.quick[3]` in the face; face saves have their own magic). Wands and thrown weapons are used from their slot (press it, then aim); only consumables go through the backpack. The bot keeps its best wands/missiles in the slots (`fq_plan` in face.inc), re-assigning through item window > Quickslot > slot.
- Camera: Assist High (targets within the middle zoom's view, as in round 1) with the closest zoom as the home zoom. `face_cam_ok` (face.inc) gates main.c's `cam_to`: close in for a fight; a new far target zooms out to show both, holds ~1.1 s, then closes in for the run; no change within 1.2 s of the last.
- Amulet pickup with a full pack: `FB_DROP` in face_brain.h drops the least useful item first.
- Sound is stubbed out in the face (sfx.c); a new game frees the old floor's pictures first. Each pause logs the lowest free heap and the overlay loading at the time.
- Host: `bot/tests/levtest.c` (traps room + chest + Levitation).
