# Pixel Dungeon Face (watchface)

A watchface that plays Pixel Dungeon by itself. Same game code as `watch/` (nothing is forked); the face is a compile switch (`PD_FACE`).

- Build: `face/build.sh` (copies `watch/` to `face/work/`, sets the face's UUID and `PD_FACE`, runs `watch/tools/build.sh`). Output `face/pixel-dungeon-face.pbw`. Needs `PD_ROOT` pointing at a watabou/pixel-dungeon checkout when `third_party/pixel-dungeon` is empty.
- Test builds: `FACE_DEFINES=FACE_WINTEST` (starts holding the Amulet: shows the victory screen), `FACE_NEWGAME` (ignores the save), `FACE_LOGP` (logs every press).
- Code: `watch/src/c/face.inc` (clock, bursts, driving the windows), `face_brain.h` (rules on top of the Assist's target list, shared with the host test), cheats in `game.c` under `PD_FACE`.
- Host test: `cd bot; OUT=facetest MAIN=tests/facetest.c ./build.sh -DPD_FACE; D=1 build/facetest 12`.

## Settings (round 2)
- The face has a Clay settings page (phone side: `face/pkjs/`; `build.sh` fetches the pebble-clay script; the normal app has no JS). The phone sends the settings as one 13-byte array (`CFG`, same order as `FaceCfg` in face.inc; byte 4 is a reserved 0, once "clock inside the HUD"; an old 12-byte save still loads, its old HUD clock becoming the time and date slots) and the weather as three int16 tenths of a degree C (`WX`); the watch keeps both in persist storage.
- The big clock sits between the HUD and the hero (windows get a small clock at the top). Each HUD number (floor, keys, level, health, and the backpack's gold) is a slot: the game's number, or time, date, weather (Open-Meteo, phone location, refreshed every 45 min), steps, heart rate, battery, day, and more. The level frame widens to the left by itself whenever the level slot shows anything but the game's number. "Date order" (settings, with the slots) is used by every Date slot. "Vibrate for game events" (default off) turns on the buzz when the hero is hurt or dies (the normal app always buzzes).
- The button tags and menu button on the right edge are not drawn in the face.
- Test builds: `FACE_FAKE` (steps 8412, heart rate 72, weather 21.5 C), `FACE_BAGTEST` (starts with the backpack open), `FACE_LOGS` (log lines and the lowest free heap; the shipped face has no log lines, to save RAM).

Round 2 (bot behaviour, toolbar, memory):
- Toolbar: the "?" button is a third quickslot (`Hero.quick[3]` in the face; face saves have their own magic). Wands and thrown weapons are used from their slot (press it, then aim); only consumables go through the backpack. The bot keeps its best wands/missiles in the slots (`fq_plan` in face.inc), re-assigning through item window > Quickslot > slot.
- Camera: Assist High (targets within the middle zoom's view, as in round 1) with the closest zoom as the home zoom. `face_cam_ok` (face.inc) gates main.c's `cam_to`: close in for a fight; a new far target zooms out to show both, holds ~1.1 s, then closes in for the run; no change within 1.2 s of the last.
- Amulet pickup with a full pack: `FB_DROP` in face_brain.h drops the least useful item first.
- Sound is stubbed out in the face (sfx.c); a new game frees the old floor's pictures first. Each pause logs the lowest free heap and the overlay loading at the time.
- Test builds (round 3): `FACE_NEWGAME FACE_QPTEST` (javelins, tomahawks etc. in the pack: watch the quickslot plan), `FACE_CAMLOG` (with FACE_HEAPLOG: a few log lines).
- Host: `bot/tests/levtest.c` (traps room + chest + Levitation).

## Pacing and wake-ups (owner round 4)
- Measured with `FACE_ACTLOG` (one log line per game turn; `FACE_OLDPACE` restores the old timings to compare). Causes found: the far-target camera glance held 1.1 s plus a 450 ms wait after it (about 2 s of no play each, every few seconds); an idle tick waited up to a second past the bot's next decision (`face_wake` in face.inc now cuts the wait to it); STEP_MS 140 and NAV_MS 330 (window presses) are now 100 and 200.
- The glance is now 650 ms (the swoop out and a moment to read), at most every 3 s; the camera closes in while the walk starts (main.c `act` centres it), no wait.
- The emulator's clock steps back about a second at times, which stalls a wait set from a late read: `face_tick` drops any wait over 700 ms (no real one is longer).
- Back and the other buttons: nothing reaches a watchface (no click handler fires). The system closes the face (window `disappear`, then the app exits) and launches it again when it comes back, which starts a burst (`face_boot`). `FACE_BTNTEST` logs these (`EV` codes in face.inc). The timeline's quick view fires the unobstructed-area `did_change`, and an app-focus regain `did_focus`: both start a burst.
