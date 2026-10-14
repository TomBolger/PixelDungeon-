# The dungeon bot

`bot/src/deepbot.c` is a program that plays Pixel Dungeon from start to finish, using the **watch's own game code** compiled for Linux. It has three jobs:

1. **Testing.** Every step, it checks dozens of invariants about the game state. Thousands of bot games find crashes, rule mistakes and save bugs long before a person would.
2. **Strategy research.** It is an attempt to play the game well: exploring, fighting, item use, food and upgrade decisions, and bosses. Its targeting logic is shared with the watch's Assist suggestions.
3. **Content.** It can record every step of its games (`REC=`) and writes checkpoints for the What-If test build.

The bot plays fairly. It only knows what the hero could know (unidentified items stay unknown), and under normal settings there is no head start and no second life. It has not won a normal-rules game yet: it reaches floor 5–6 on average, and its most common killers are Goo and Tengu.

## Build and run

```sh
bot/build.sh                    # -> bot/build/deepbot (about 20 seconds)
cd bot
./score.sh 40 4                 # 40 games in 4 parallel shards, with a summary
```

`build.sh` compiles `bot/src/deepbot.c` together with `watch/src/c/*.c`. Each overlay in `watch/src/ovl/` becomes `bot/build/libovl_<name>.so`. To build something else against the same game code:

```sh
OUT=itemtest MAIN=tests/itemtest.c ./build.sh      # -> build/itemtest
```

Run a single game:

```sh
START=7 D=1 HS=0 RT=0 FK=0 build/deepbot 8     # plays game 7 only (games START .. N-1)
```

Every game is reproducible: game *g* always gets the same dungeon, and with the same settings the same moves.

## Output

`D=1` prints one line per game:

```
game 13 cls 1: died depth 13 (max 13) lvl 12 turns 3944 steps 1876 HP 0/75 STR 14 arm 27+4 wpn 14+1 quests 1220: Killed by a cave spinner on level 13
```

After the games, it prints totals: deaths by depth and cause, quests, look-ahead use, and **violations**. Any `VIOL[...]` line is a bug in the game code (or the bot) and should be investigated. It names the game and step, so you can replay it with `START=g` and get a verbose trace with `STOP=g:step`.

## Settings

Settings are environment variables; there are about 300 of them. Most are experiment switches, each marked in the source with the version and the reason it was added. The ones you'll use most:

| Setting | Meaning |
|---|---|
| `START=g`, then `N` as the argument | Play games `g` to `N-1` |
| `CLS=c` | Force a class: 0 Warrior, 1 Mage, 2 Rogue, 3 Huntress (default: game number mod 4) |
| `HS=n` | Head start: extra HP, strength and gear (default 3). **Use `HS=0` for normal rules.** |
| `RT=n`, `FK=n` | A save/load round trip every ~n steps, and a forked replay check every ~n steps (testing). Set both to 0 for speed. |
| `D=1` / `V=1` | One line per game / everything |
| `STOP=g:s`, `STOPD=d` | Verbose from step *s* of game *g* / from depth *d* |
| `SD=n` | Start on depth *n* with matching gear (boss labs; see `goolab.sh`) |
| `STEPS=n` | Decision limit per game |
| `NOLA=1` | Turn off look-ahead (rules only) |
| `LAMULT`, `LAK` | Look-ahead effort |
| `ASSIST=0/1/2` | The watch's Assist level, for testing the suggester |
| `REC=file.jsonl` | Record every step of every game as one JSON line (map, hero, creatures, items, decision, log) |
| `CAMPDIR=dir` | Write a checkpoint at each new floor (`dir/gGGGG_dDD.bin`) |
| `CAMPLOAD=file` | Continue a game from a checkpoint |
| `PHOTODIR=dir` (`PHOTOSTAGE=1`) | Save the game at moments worth a screenshot (each region, special rooms, quest givers, boss fights, a floor-25 backpack) for the What-If build; `PHOTOSTAGE` swaps in a level and gear typical of the depth |
| `FOODLOG=1`, `FIGHTLOG=1`, `GEARLOG=1` | Extra logs about food, fights and gear |

### Experiments

These switches test strategy ideas against each other:

- `AVOID`, `AVOIDFAV`: skip fights the hero isn't clearly favoured in.
- `THRIFT`: hunt for experience before Goo when under-levelled.
- `GOO2`: kite Goo using the water.
- `EXPGAIN`, `EXPCOMMIT`, `EVLEAVE`: when to stop exploring a floor.
- `UPGCF`: play an upgrade scroll into armour and into the weapon, and compare.
- `CFV` and `BLAME`: replay the moments before a death with different settings to find what would have saved the hero.

Compare two settings on the same games. Same game numbers mean the same dungeons, so differences come from the decisions:

```sh
./score.sh 200 4                    # baseline
AVOID=1 ./score.sh 200 4            # the same 200 dungeons with AVOID on
```

## Scripts

| Script | What it does |
|---|---|
| `bot/score.sh [N] [shards]` | N normal-rules games: mean deepest floor, wins, causes of death, results by class |
| `bot/goolab.sh [N]` | N heroes start on floor 5 with typical gear: how often they beat Goo |
| `bot/compare.sh [runs]` | Fixed scenarios (melee against a rat or crab, fire, gas) in the C port and the original Java game, with z-scores |
| `bot/tools/tools_tune.py` | Coordinate search over the bot's judgment parameters (`PRM`) |
| `bot/tools/whatif_blob.py` | Turn a checkpoint into a blob for `scripts/whatif.sh` |
| `bot/tools/table.py`, `paired.py`, `leave.py`, `walk.py`, `blame.py`, `upg.py`, `cfsum.py`, `goolab.py` | Summaries of large runs (paired comparisons, floor-leaving times, walking efficiency, death replays, upgrade choices) |
| `bot/tools/snap.py` | Draw a bot snapshot (`SNAPG` / `SNAPDEATH` JSON) as the watch shows it |

## Host tests (`bot/tests`)

Small programs that set up one situation with the real game code and print what happens:

- **Items, bags and shops:** `alchtest`, `bagtest`, `itemtest`, `shoptest`, `smithtest`, `tometest`.
- **Classes and NPCs:** `classtest`, `imptest`.
- **Save data and gas:** `bonestest`, `gastest`, `rectest`.
- **Targeting and rankings:** `tgtest`, `ranktest`, `jtest`.
- **Java comparison:** `scen` runs the scenarios that `compare.sh` checks against the Java original.

Build one with `OUT=name MAIN=tests/name.c ./build.sh` and run `build/name`. These tests print what happens and do not assert pass or fail. Compare their output before and after a change.

`bot/src/bot.c` is the older random-play tester: it uses every kind of item at random, which is good at finding crashes.

## Running thousands of games on a Windows PC (`bot/runner`)

`runner.sh` runs inside WSL on a Windows PC and uses every core. It watches a shared folder, so jobs can be dropped in from anywhere.

1. Make a folder such as `Documents\PebxelSim` and copy `runner.sh`, `PAUSE-sim.cmd` and `RESUME-sim.cmd` into it.
2. Start it once in WSL: `bash /mnt/c/Users/<you>/Documents/PebxelSim/runner.sh`. Leave the window open, or minimise it.
3. Make a job on the build machine:

   ```sh
   bot/runner/make_job.sh b001 avoid-test 192 1 0 \
       "base D=1 HS=0 RT=0 FK=0" \
       "avoid AVOID=1 D=1 HS=0 RT=0 FK=0"
   ```

   This writes `bot/build/runner/b001.tgz` (the bot, its overlays and game data) and `avoid-test.job`.
4. Copy the `.tgz` into the runner's `bundles\` folder and the `.job` into `jobs\`. The runner picks the job up, plays games 0–191 with each setting line, and writes `results\avoid-test\base.txt` and `avoid.txt`. Its progress is in `status.txt`.

Rules for job files:

- Job files are plain settings, never shell commands, and the runner rejects anything else.
- Setting values may not contain `;` or `=`, so write `CFV` specs with `/` and `-` instead.
- Bundles are cached by name, so give every new build a new bundle name.
- `PAUSE-sim.cmd` freezes all games (your PC is free again), and `RESUME-sim.cmd` continues them.
- To cancel a job, create `jobs\<job>.cancel`. To stop the runner, create a file named `STOP`.

## Known limits

- The overlay libraries are opened with `dlopen` on every load. That makes long runs under AddressSanitizer leak, so run them in chunks of 40 games or fewer per process.
- The bot needs Linux (`fork` for look-ahead, `/proc/self/exe` to find its overlays). On Windows, use WSL.
