# Native C reconstruction

`build/Release/NFL2K5_Rebuild.exe` is a separate native Windows executable. It does not launch xemu, load an Xbox emulator, or execute `default.xbe`.

The first milestone mounts the locally extracted `original/default.xbe` and the 16 `original/vc_53450030` data archives, reports their presence in its own Windows window, and provides the native game-loop and input foundation. Arrow keys or WASD move the demonstration ball. This proves the standalone executable and local-data boundary; it is not yet NFL 2K5 gameplay.

Build it with `powershell -ExecutionPolicy Bypass -File E:\NFL2K5-PC\tools\build.ps1`. The output is `E:\NFL2K5-PC\build\Release\NFL2K5_Rebuild.exe`.

Update: the executable now decodes and displays the original NFL/Players Inc. legal-page artwork directly from archive 0. The output was exported and visually verified. It is a real original UI texture, not yet a working menu. The earlier ball demonstration is only a fallback if loading fails.

Decoder: src/rebuild/legal_texture.c. Entry ID EDD549BD is checked; the archive table determines its location. HITX P8 format, dimensions, payload length, and embedded legalpage label are validated before allocation. It converts Morton-ordered 512x512 indices through the embedded BGRA palette. Export verification: NFL2K5_Rebuild.exe --export-legal writes analysis/legalpage.bmp. No emulator or Xbox executable execution is involved.

Next short burst: inspect the next HITX blocks in the same record and generalize the reader into an original-artwork browser. The translated C remains a reference for original behavior and formats.
