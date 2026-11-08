@echo off
rem Send the Xbox loose folder (make-xbox-loose.cmd) to an Xbox in Dev Mode.
rem It asks for the console's IP and your Device Portal login.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy-xbox-loose.ps1" %*
echo.
pause
