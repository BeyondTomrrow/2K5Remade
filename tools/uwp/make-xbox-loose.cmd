@echo off
rem Build dist\xbox-loose\NFL2K5: the Xbox app plus your game files, the data
rem folder compressed into one pack without the movies (about 3 GB; see
rem README-XBOX.txt). Drag your game folder (the one holding "original") onto
rem this file, or run it from the game folder's tools\uwp directory to use that
rem checkout. For the whole 6 GB folder instead, run make-xbox-loose.ps1
rem without -Pack.
if "%~1"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-xbox-loose.ps1" -Pack
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-xbox-loose.ps1" -Pack -GameRoot "%~1"
)
echo.
pause
