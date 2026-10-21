# PebxelSim on Monolith (Thomas's PC)

The runner (`runner.sh`, in `Documents\PebxelSim` on the PC, started by START-sim.cmd in WSL) plays bot games on all
cores. Claude drives it from the cloud through two branches of this repo:

- **sim-jobs** (read by the runner, no key needed: the repo is public): `bundles/*.tgz` (a bot build), `jobs/*.job`
  (plain settings), and optionally `runner-next.sh` (a new runner, swapped in between jobs). `submit.sh` puts files there.
- **sim-reports** (pushed by the runner with its deploy key): `log.txt` and `jobs/<job>/<run>.txt.gz` when a job ends.
  `wait_report.sh JOB` waits for that push (run it in the background: it exits once the report lands).

Shattered PD bot: `shattered_bundle.sh NAME` packs the Java bot (after `shattered/build-server.sh`) with `simbot`,
then `make_job.sh JOB bundle.tgz GAMES CHUNK FIRST "run SETTING=v ..."` and `submit.sh bundle.tgz out/JOB.job`.
