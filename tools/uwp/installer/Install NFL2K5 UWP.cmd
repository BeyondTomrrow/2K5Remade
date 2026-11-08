@echo off
setlocal
title Install ESPN NFL 2K5 (UWP test build)
echo.
echo  ESPN NFL 2K5 - UWP test build installer
echo  ========================================
echo.
echo  This will change your PC as follows (one administrator prompt):
echo.
echo   1. Trust the test certificate NFL2K5Dev.cer (subject "CN=NFL2K5 Dev")
echo      in Local Machine ^> Trusted People, so Windows accepts the package.
echo   2. Install the app package NFL2K5_*_x64.msix from this folder for your
echo      user (plus the Microsoft.VCLibs.140.00 runtime if it is missing and
echo      its .appx is in this folder).
echo   3. Give UWP apps ("ALL APPLICATION PACKAGES") read access to these game
echo      folders, so the app can use your existing files without copying them:
echo         E:\2K5Remade\original  mods  Music  assets  data  font  moment-audio
echo      (only folders that exist; nothing is copied, moved or deleted).
echo   4. Write root.txt into the app's LocalState so it plays from E:\2K5Remade.
echo      Saves and settings stay in the app's own LocalState folder.
echo   5. Start the app.
echo.
echo  "Uninstall NFL2K5 UWP.cmd" undoes all of it. A log is written to
echo  install-log.txt next to this file.
echo.
echo  Close this window now to cancel, or
pause
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','\"%~dp0install-nfl2k5-uwp.ps1\"'"
echo.
echo  Done. See install-log.txt for details.
pause
