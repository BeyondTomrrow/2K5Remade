@echo off
cd /d "E:\NFL2K5-PC\external\xemu-src\build\win64"
start "" "qemu-system-i386w.exe" -config_path "xemu.toml"
