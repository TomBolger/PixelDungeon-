@echo off
rem Makes Windows start the PebxelSim runner whenever you log in (after a crash or power cut too).
rem Run once. To undo it: Remove-autostart.cmd
schtasks /Create /F /SC ONLOGON /TN "PebxelSim runner" /TR "\"%~dp0START-sim.cmd\""
if errorlevel 1 (echo Could not add the task. & pause & exit /b 1)
echo Done: the runner now starts when you log in. If it is already running, a second copy just closes itself.
pause
