@echo off
setlocal
title Uninstall ESPN NFL 2K5 (UWP test build)
echo.
echo  ESPN NFL 2K5 - UWP test build uninstaller
echo  ==========================================
echo.
echo  This undoes what "Install NFL2K5 UWP.cmd" did (one administrator prompt):
echo.
echo   1. Remove the NFL2K5 UWP app package. Its LocalState folder goes with it,
echo      including saves and settings made in the UWP app.
echo   2. Remove the test certificate "CN=NFL2K5 Dev" from Local Machine ^>
echo      Trusted People (only if the installer added it).
echo   3. Remove the "ALL APPLICATION PACKAGES" read access the installer added
echo      to the game folders (only those listed in install-state.json).
echo.
echo  Your game files in E:\2K5Remade are not touched. A log is written to
echo  uninstall-log.txt next to this file.
echo.
echo  Close this window now to cancel, or
pause
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','\"%~dp0uninstall-nfl2k5-uwp.ps1\"'"
echo.
echo  Done. See uninstall-log.txt for details.
pause
