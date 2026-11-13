@echo off
schtasks /Delete /F /TN "PebxelSim runner"
echo The runner no longer starts by itself.
pause
