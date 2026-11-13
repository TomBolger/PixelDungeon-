PebxelSim - Claude's simulation runner

runner.sh watches the jobs folder. When Claude drops a job there, the runner
unpacks the matching bot bundle (bundles folder) inside WSL and plays
thousands of games across all CPU cores at low priority. Results go in the
results folder. Job files are plain settings, never commands.

Pause it (to use your PC):   double-click PAUSE-sim.cmd  (everything freezes within 15 seconds)
Carry on:                    double-click RESUME-sim.cmd (it picks up exactly where it stopped)
Start it after a restart:    double-click START-sim.cmd  (leave the window open; minimising is fine)
Start it automatically:      double-click Install-autostart.cmd once (Remove-autostart.cmd undoes it)
Stop it: close its window, press Ctrl+C in it, or create a file named STOP here.

After a crash or power cut nothing finished is lost: a job carries on from the games
already done, and a training run from its last finished round.
(For the PC to come back on by itself after a power cut, the BIOS needs "restore on AC
power loss" switched on, and Windows needs to log in by itself; neither is required.)
