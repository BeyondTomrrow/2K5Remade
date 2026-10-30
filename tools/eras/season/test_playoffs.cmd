@echo off
rem Offline seeding check (M14): builds tools\eras\season\test_playoffs.c with src\nfl2k5_eras_playoffs.c and runs it
rem on a league folder (default logs\season\home-real: the real 1980 results).
setlocal
set ROOT=%~dp0..\..\..
call "F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%ROOT%"
if not exist build\temp\season mkdir build\temp\season
cl /nologo /W3 /O1 /I src /Fo:build\temp\season\ /Fe:build\temp\season\test_playoffs.exe tools\eras\season\test_playoffs.c src\nfl2k5_eras_playoffs.c
if errorlevel 1 exit /b 1
build\temp\season\test_playoffs.exe %*
popd
