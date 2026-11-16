# Handoff: the Pixel Dungeon bot (as of 2026-11-16)

Read this first, then `bot/CLAUDE.md` (the standing rules). This file carries the context of a long earlier session
so a new one can pick up without it.

## Who and what
- Owner: Thomas. He is not a coder: talk in plain language, no jargon, PowerShell (not bash) when he has to run something
  on Windows. Keep reports short. He wants action, not estimates ("you always overestimate how long you take").
- Project: "Pebxel Dungeon", a C port of Pixel Dungeon 1.9.1 (watabou) to Pebble watches. Repo
  `github.com/TomBolger/pixeldungeon-`, working branch **`seed-solve`**.
- Goal of this part: a bot that **wins consistently**. Later targets: the bot on the watch, then the same foundation for
  Shattered Pixel Dungeon. So don't build on anything that couldn't eventually run small.

## Hard rules
- **Never** put the release signing key (`pebxel-release.jks`) or its password in the repo, a commit or a release.
- Commit and push every change: `git push origin seed-solve`. End commit messages with the attribution lines the session gives.
- The bot plays fair: `FAIR=1`, no hidden information (no peeking at the map, unknown items or future dice).
  Rollback/retries are allowed **in training only**, never in the final bot.
- No new "in situation X, do Y" rules (see `bot/CLAUDE.md`): only new options or better numbers.
- Big leaps, not tuning within noise. Test at about half size (about 120 random dungeons, or 48 games of one seed).
  Seed 25 always rides along. Don't rerun deterministic old code just to get the same numbers.
- Deliver watch builds (PBW files) to Thomas as files.

## Code map
- Build: `cd bot && ./build.sh`. Runs on Linux (WSL on Thomas's PC).
- `bot/src/deepbot.c`: the bot's main loop and the old rule bot (about 8 floors on average).
- `bot/src/mcts.inc`: the search. It tries each move at the root, plays the future out with the old bot for 25 turns,
  and scores it with the judge.
  - Move ids: 0–7 step or attack, 8 wait, 9 stairs, 16+ items, 512+ throws, 4096+ zaps, 8192+ walk to a cell.
  - Settings: `MC_CP` (trust in the instinct; 10 is best), `MC_NOBOSS=1` (boss floors go back to the old code),
    `NNPLAY` (instinct only, no search).
- Judge: `judge2`, split into `j2_feats`/`j2_score`. `J2NEW=2` is the refit used now.
- `bot/src/nn.inc`: small neural nets with one hidden layer of 48. Each is a correction on top of the old instinct
  (`POLNN`) or on top of judge2 (`VALNN`).
  - Records: `TN` search decisions, `TV` positions, `TI` imitation.
  - Trainer: `TRAINNN`. A new net is kept only if it beats both the old instinct and the net it started from on
    held-out games.
- `bot/src/train.inc`: the older linear trainer and rollback training (`RB=1`).
- `bot/src/goals.inc`: floor goals (`FGOAL=1`, off by default): leave, rest then leave, or explore, priced by the judge.
  This only reached parity with the old rules.
- `bot/src/suite.inc`: test positions. It saves fight positions and measures each move's true value with many honest
  futures.
- Imitation: action hooks in `watch/src/c` (`ACT_HOOK`), host build only, so nothing changes on the watch.
  `IMREC=1 NOMCTS=1` records the old code's choices.
- Speedups (all verified to give identical games): overlay snapshots, a getenv cache, and a BFS memo. The old
  behaviour is available again with `OVLSLOW`, `GETENVSLOW` and `BFSSLOW`.

## Thomas's PC (where the long runs happen)
- Ryzen 5600X, 12 threads, WSL. Folder `C:\Users\tombo\Documents\PebxelSim`.
- Runner: `bot/runner/pc/runner.sh` (v5). Jobs go in `jobs/`, bundles in `bundles/`, results in `results/`.
  It can resume after a crash or power cut and runs one job at a time.
- Training jobs: `train=1`, `seed=`, `games=`, `gens=`, `vary=0|1|2`.
- Cancel a job with `jobs/JOB.cancel`. `PAUSE-sim.cmd` and `RESUME-sim.cmd` pause and resume. `Install-autostart.cmd`
  starts the runner at logon; whether Thomas has run it is not confirmed.
- Make a bundle: `bash bot/runner/make_job.sh BUNDLE JOB GAMES CHUNK FIRST "name ENV..."`.
- The PC was idle at handoff.

## Results so far (120 random dungeons, fair play)
| Player | Avg deepest floor | Past Goo | Past Tengu | Died floors 1–4 |
|---|---|---|---|---|
| Old rule bot | 8.08 | 68 | 34 | 23 |
| Search + copied instinct, bosses to old code (best: `D=1 HS=0 RT=0 FK=0 CLS=0 FAIR=1 J2NEW=2 NN=1 MC_CP=10 MC_NOBOSS=1`, `POLNN=results/IM-collect/w/cur/polnn.txt`) | 8.02 | 68 | 32 | 21 |
| Same + learned judge net | 7.59 | 60 | 31 | 29 |

What didn't work:
- Linear self-training degraded.
- Neural self-play with rollback degraded.
- Self-play from the search's choices erased the copied instinct; after a fix it was flat to worse.
- Three judge nets predicted outcomes better on paper but played worse. They are weak at telling sibling moves apart
  in one fight.

## Where we are (Thomas, 2026-11-15)
Thomas's read: we built a decent rule bot, found its limits, and have since been "copying our own homework" with fancier
tools. That only reaches novice level. **A new strategy is needed.** He asked for a break and a fresh approach.

The diagnosis: everything is graded by the old bot.
- The search's futures are played by it.
- The judge was fitted to its games.
- The instinct copies it.
- Self-play learned from that search.

So everything shares its ceiling.

Proposed directions (not yet chosen by Thomas):
1. **Death autopsy first (cheap).** Sort every loss by cause: gear too weak for the depth, a bad fight, starving, or a
   misused item. 23 of 120 deaths on floors 1–4 is far worse than a strong player. This decides between 2 and 3.
2. **Learn from real results, one floor at a time.** Start from thousands of saved positions and score each floor by how
   it actually ended, so the judge learns from what happened instead of from the old bot. This needs the most compute;
   cloud time or a CPU upgrade would help here.
3. **A teacher allowed to cheat, in training only.** It sees the map, knows the item identities and can retry. The honest
   bot then learns the choices that can be worked out from what it can see. Risk: the student learns to guess.
4. **An expert as coach for big decisions** (where upgrades go, what to keep, when to descend), from an AI with Pixel
   Dungeon knowledge or from Thomas. It won't help with fights.

Recommendation: 1, then 3 if losses are mostly strategic or 2 if mostly fights. Whatever is chosen should end in a compact
learned player, because the watch can't run heavy search.

**Next step: ask Thomas which direction he wants (or start the autopsy if he says go).**

## More context
- The claude.ai project "Pebxel Dungeon" has dated findings docs, the latest `claude/findings-2026-11-12-search.md`, and
  `claude/shattered-strategy-reference.md` (expert strategy notes).
- `bot/seedlog.md` is a log of results by seed.
