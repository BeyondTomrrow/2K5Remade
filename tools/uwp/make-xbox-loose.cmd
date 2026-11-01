@echo off
rem Build dist\xbox-loose\NFL2K5: the Xbox app plus your game files.
rem Drag your game folder (the one holding "original") onto this file, or run
rem it from the game folder's tools\uwp directory to use that checkout.
if "%~1"=="" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-xbox-loose.ps1"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-xbox-loose.ps1" -GameRoot "%~1"
)
echo.
pause
