@echo off
rem Opens Presentation Studio (HTML scorebug editor) in your browser.
rem Keep this window open while you edit; close it when you are done.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\presentation-studio\serve.ps1"
pause
