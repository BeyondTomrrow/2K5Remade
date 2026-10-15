@echo off
cd /d "%~dp0"
where pyw >nul 2>nul
if not errorlevel 1 (
  start "" pyw -3 "%~dp0ncaa_app.py" gui
  exit /b
)
python "%~dp0ncaa_app.py" gui
if errorlevel 1 pause
