@echo off
call "F:\Program Files\Microsoft Visual Studio‚2\Community\VC\Auxiliary\Buildcvars64.bat" >nul
cd /d E:\NFL2K5-PC	ools
cl /nologo /O2 loopback_rec.c ole32.lib /Fe:loopback_rec.exe
del loopback_rec.obj
