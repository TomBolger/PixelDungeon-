# PebxelSim on Monolith (Thomas's PC)

The runner (`runner.sh`, in `Documents\PebxelSim` on the PC, started by START-sim.cmd in WSL) plays bot games on all
cores. Claude drives it from the cloud through two branches of this repo:

- **sim-jobs** (read by the runner, no key needed: the repo is public): `bundles/*.tgz` (a bot build), `jobs/*.job`
  (plain settings), and optionally `runner-next.sh` (a new runner, swapped in between jobs). `submit.sh` puts files there.
- **sim-reports** (pushed by the runner with its deploy key): `log.txt` and `jobs/<job>/<run>.txt.gz` when a job ends.
  `wait_report.sh JOB` waits for that push (run it in the background: it exits once the report lands).

Shattered PD bot: `shattered_bundle.sh NAME` packs the Java bot (after `shattered/build-server.sh`) with `simbot`,
then `make_job.sh JOB bundle.tgz GAMES CHUNK FIRST "run SETTING=v ..."` and `submit.sh bundle.tgz out/JOB.job`.

Settings the Shattered bot reads (job run lines): `SIMSEED=n` (repeatable games), `CHEAT=1` (the whole-game smoke test:
the hero can't die, every quest, item kind, the alchemy pot and the win), `WATCH=1` (played with the watch's buttons
through Controls, the Link and the WatchSim model), `MAXDEPTH`, `STARTDEPTH`, `MINES=0`, `ASCEND=0`. Speed (not with
`WATCH=1`): `FAST=0` (the old 1/30 s frames and particles; default fast: 0.2 s frames, no particles, no GL), `BATCH=0` (a
JVM per game; default all of a chunk's games in one JVM, each played as it would be alone), `JIT=c2` (default c1, the
quick compiler: on Monolith 0.8 against 3.4 s a game). A chunk of 20-50 honest games per task amortises the JVM start best. `cover.py report.txt...`
adds up the games' COVER lines: wins, quests, bosses, subclasses, item kinds used, windows, the watch path's problems.
