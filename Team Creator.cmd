@echo off
rem NFL 2K5 Team Creator: make new teams (tools\team-creator)
cd /d "%~dp0"
python tools\team-creator\creator.py
if errorlevel 1 pause
