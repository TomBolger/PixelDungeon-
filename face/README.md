# Pixel Dungeon Face (watchface)

A watchface that plays Pixel Dungeon by itself. Same game code as `watch/` (nothing is forked); the face is a compile switch (`PD_FACE`).

- Build: `face/build.sh` (copies `watch/` to `face/work/`, sets the face's UUID and `PD_FACE`, runs `watch/tools/build.sh`). Output `face/pixel-dungeon-face.pbw`. Needs `PD_ROOT` pointing at a watabou/pixel-dungeon checkout when `third_party/pixel-dungeon` is empty.
- Test builds: `FACE_DEFINES=FACE_WINTEST` (starts holding the Amulet: shows the victory screen), `FACE_NEWGAME` (ignores the save), `FACE_LOGP` (logs every press).
- Code: `watch/src/c/face.inc` (clock, bursts, driving the windows), `face_brain.h` (rules on top of the Assist's target list, shared with the host test), cheats in `game.c` under `PD_FACE`.
- Host test: `cd bot; OUT=facetest MAIN=tests/facetest.c ./build.sh -DPD_FACE; D=1 build/face/facetest 12` (with `-DPD_FACE` the overlays are built with the switch too, in `bot/build/face/`; `VARY=1` gives every game its own dungeon, else the games repeat per class; it prints the share of game time resting, healing drinks, starving, the HP spread).

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
- Back and the other buttons: nothing reaches a watchface (a click handler never fires). Emulator: on Emery a Back press produces no event at all; on Gabbro it closes the face (window `disappear`, the app exits) and the system launches it again, which starts a burst (`face_boot`); Up/Down/Select close it the same way and Back brings it back (a fresh launch, a burst). So a quick Back cannot be made to start play; shake still does. `FACE_BTNTEST` logs these (`EV` codes in face.inc). The timeline's quick view fires the unobstructed-area `did_change`, and an app-focus regain `did_focus`: both start a burst.

## A livelier face (round 5: less resting, fewer pinned fights)
All of this is `PD_FACE` only (the watch app's code and overlays are unchanged); the numbers are constants in `game_int.h`.
- No more forever food: food is found and eaten as in the game. Instead the hero starts with one identified Potion of Healing that is never used up (`game.c` keeps one in the pack after every turn: drunk, thrown, dropped, stolen, it is back; it cannot be sold, `game_pick_ok`). The Assist drinks it in a fight under a quarter health (as before) and in place of a rest when under 40% (`tgt.c`, before the rest rule); `FACE_HEAL_CD` (40 game turns) is the pause between two drinks. Only the healing colour is known; the other potions are not identified.
- Hunger runs `FACE_HUNGER_X` (8) times slower, the stairs cost 1/8 of their hunger too: without the long rests the hero plays about five times faster than the food in the dungeon allows (at x1 he starved most of the game).
- Gear never breaks: durability can fall (and scrolls and enchanting still repair it) but stops at 1 (`item_use`).
- Better gear, a little: weapons and armour get a level more often (45% instead of 40%) and upwards more often (60% instead of 50% of those: +1..+3 from 20% to 27% of items, cursed from 20% to 18%); armour tiers 4:4:5:5:5 instead of even, melee weapon tiers 1: 3, 2-3: 4, 4-5: 5 where each had 4. The Assist still never reads unknown levels.
- Item screens dwell about half a second: `NAV_MS` 480 (was 260), the item-window select wait 500 (was 400), the backpack-open wait stays 650 (`face_tick` drops any wait over 700 ms). `FP_MS` (a plan's time limit) went from 12 to 18 s to match the slower presses.
- Passive regeneration is `FACE_REGEN_X` (3) times faster. This is tunable: if the HP bar sits pinned at full and the fights lose their tension, lower it (or raise `FACE_HEAL_CD`); if it sits near empty, do the opposite.
