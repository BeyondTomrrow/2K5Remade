@echo off
rem Drop the NFL 2K5 disc image (or the game folder) onto this file: it makes
rem "NFL2K5 for phone" on the Desktop for the Android version (make-phone-folder.ps1).
if "%~1"=="" (
  echo Drag your NFL 2K5 disc image ^(.iso^) or game folder onto this file.
  pause
  exit /b 1
)
set "PS1=%~dp0make-phone-folder.ps1"
if not exist "%PS1%" set "PS1=%~dp0tools\make-phone-folder.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Source "%~1"
pause
