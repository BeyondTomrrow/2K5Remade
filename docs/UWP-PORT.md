# UWP / Xbox Dev Mode port

Status: started 2026-10-31. Goal: run the recompiled game as a UWP app, on
Xbox One / Series consoles in Dev Mode (the way RetroArch and xemu UWP builds
run) and as a Store-style Windows app.

Stage 1 (CoreWindow + D3D12 shell, probes, packaging) works. Stage 2: the
game builds as a UWP app, runs from the UWP-linked exe on this PC with D3D12 and
XAudio2, and is packaged with a one-click installer. See "Status" at the end
for what is verified and what is still open.

## Recommendation

Use our own thin UWP layer, plus SDL3 built in a UWP-safe configuration for
SDL_GPU only.

| Option | Verdict |
|---|---|
| SDL3 GDK path | No. Xbox GDK needs ID@Xbox/partner access and a devkit; Dev Mode runs UWP, not GDK titles. Desktop GDK is still Win32. |
| SDL2 WinRT backend | No. The renderer (gx) is built on SDL_GPU, which exists only in SDL3. Going back to SDL2 means rewriting the renderer. |
| Own layer only, no SDL | Possible later, but gx_sdl.c, the sdlgpu files and fb_present are written against SDL3 (about 400 SDL calls). Replacing SDL_GPU means a second renderer backend. |
| **Own CoreWindow layer + SDL3 "UWP minimal"** | **Chosen.** SDL3 keeps doing what the game relies on (SDL_GPU on D3D12, threads, timers, properties, the gamepad API). Everything that touches the OS shell (window, swap-chain target, input devices, audio device, files) is ours. |

Why this way round:

- SDL3 dropped WinRT, but only its window, joystick, audio and filesystem
  backends are Win32-specific. Its core, threads, timers and the SDL_GPU D3D12
  backend use APIs that are in the UWP API set. The local SDL fork gets a
  `SDL_UWP_MINIMAL` CMake option that builds just those, the dummy video
  driver and virtual joysticks. Nothing on the link line comes from
  user32/gdi32/hid/setupapi/winmm.
- The D3D12 backend gets one small patch: with no HWND, it makes its swap
  chain with `CreateSwapChainForCoreWindow` on the CoreWindow that the app
  publishes as the `SDL.window.uwp.corewindow` property.
- Gamepads: `Windows.Gaming.Input` readings are fed into SDL virtual joysticks
  of type GAMEPAD, so the game's SDL gamepad code (menus, Controls screen,
  rumble) works unchanged. Rumble goes back out through `Gamepad.Vibration`.
- The PC sources compile unchanged. Win32 calls outside the UWP API set go to
  a link-time shim (`uwp_win32_shim.c` defines the `__imp_` import slots).
  The shim has no-ops for window chrome and dialogs, the CoreWindow's state
  for window and key queries, and LocalState for shell folders. `main.c` is
  compiled with `main` renamed to `nfl2k5_real_main` and runs on a 16 MB-stack
  thread, the same as Android.

## Xbox Dev Mode constraints (researched, then checked on this PC where possible)

| Topic | Finding | Source / test |
|---|---|---|
| Memory | UWP **apps** get 1 GB in the foreground; **games** get 5 GB. Set the package to *Game* in Dev Home / Device Portal (default is App) or it is killed at about 1 GB. The guest uses 64 MB of RAM plus views. The renderer and texture cache are the real budget. | MS docs "System resources for UWP apps and games on Xbox"; Unity forum report |
| CPU | Games: 4 exclusive + 2 shared cores (Xbox One figures). `SetThreadAffinityMask` / priority work in the AppContainer. | docs; probe |
| GPU / D3D | Xbox One games: D3D12 FL 11_0 (D3D11 only FL 10_1). Series X/S: D3D11 and D3D12 at FL 11_0. So **D3D12** is the backend that covers both. The desktop's native-D3D11 path is fine on Series only. Apps (not games) get a share of the GPU of up to 45%. | docs |
| Guest memory map | **Works in an AppContainer.** The 64 MB RAM view at 0x00010000, all 28 mirrors (aliasing verified), the fixed windows at 0x80010000 / 0xFD010000 / 0xFE810000 / 0xFF010000 (`MapViewOfFileEx` / `VirtualAlloc` at fixed addresses), and the 4 GB `VirtualAlloc2FromApp` placeholder plus `MapViewOfFile3FromApp` layout all pass. Every API used is in the UWP allow-list (`SupportedAPIs-x64.xml`), even though some are hidden by the APP-partition headers. **Still to verify on a console.** | `uwp_probe.c`, run via `tools/uwp/probe-appcontainer.ps1` |
| MMIO traps (VEH) | `AddVectoredExceptionHandler` plus a PAGE_READONLY write trap works in the AppContainer, and it is in the allow-list (api-ms-win-core-errorhandling). APU/AC97 traps can stay as they are. | probe |
| Code generation | Not needed: the game is statically recompiled, and the x86 interpreter (mod packs) interprets without JIT. | - |
| Run-time shader compile | `dxcompiler.dll` + `dxil.dll` have **zero** imports outside the UWP API set and load from the package in the AppContainer. Ship them in the package. A precompiled shader cache is still worth adding for first-run stutter (and as a fallback if a console refuses unsigned-by-MS DXIL, not expected). | `check-imports.py` on both DLLs; probe |
| Files | The AppContainer can read its install folder and read/write LocalState. Anything else gets ACCESS_DENIED unless the folder's ACL grants ALL APPLICATION PACKAGES (verified: `E:\2K5Remade\README.md` is denied). On Xbox Dev Mode, USB drives mount as **E:** and are readable by Win32 calls if the NTFS ACL grants ALL APPLICATION PACKAGES (community tools set this). `broadFileSystemAccess` is **not** supported on Xbox. Plan: game files in LocalState (upload with Device Portal's file explorer), or a USB folder named in `LocalState\root.txt`. | probe; Emulation Revival dev-mode utilities; MS docs |
| File size | 2 GB per-file limit reported on Xbox UWP. Our disc files are far smaller. | TechRepublic |
| WebView2 | Not on Xbox. In XAML host mode the HTML scorebug packages run in the XAML `WebView` (EdgeHTML on Xbox) and frames are captured with `CapturePreviewToStreamAsync` (see below). In CoreWindow mode, or for a package EdgeHTML cannot run, the native scorebug is used. | - |
| Threading | CoreApplication owns the main (ASTA) thread, so the game runs on its own thread and the UI thread only dispatches events and polls gamepads. Suspend/resume: Xbox suspends games in the background, so D3D12 must `Trim` on suspend (TODO in stage 2). | - |
| Audio | XAudio2 2.9 and WASAPI (`ActivateAudioInterfaceAsync`) are UWP APIs. SDL's WASAPI uses `IMMDeviceEnumerator` (desktop) and is off in the UWP build. The UWP SDL has an XAudio2 2.9 playback driver instead, so the game's SDL3 audio stream plays through XAudio2 (verified in an AppContainer and in the game). | self-test, game log |
| Store/WACK | Restricted APIs are fine in Dev Mode sideloading. A Store submission would need the restricted capabilities justified, and emulator-like apps are not accepted on Xbox retail. | TweakTown (RetroArch removal) |

## What is Win32-only in our code (import check)

`tools/uwp/check-imports.py` lists a binary's imports that are not in the
App Certification Kit's UWP allow-list. For the current desktop
`build\Release\NFL2K5.exe` it finds **240** imports outside the UWP set:

- user32 (143), gdi32 (38), imm32 (11), hid/setupapi (14), shell32 (10),
  ole32 drag-and-drop (5), pdh (4), dbghelp (5), winmm joystick calls (6),
  version (1), `D3D11CreateDeviceAndSwapChain`, `MFTranscodeGetAudioOutputAvailableTypes`,
  `PulseEvent`.
- Most come from SDL3's desktop backends (window, HID joystick, IME, tray,
  dialogs). With the UWP-minimal SDL they are gone. The rest are game code:
  the crash reporter (dbghelp), window/menu code in fb_present and the
  presentation layer, shell folder lookups, the pdh CPU counter, and the
  native-D3D11 HWND swap chain.

Everything the kernel needs (CreateFileW, VirtualAlloc at an address,
MapViewOfFileEx, CreateFileMappingW, VEH, threads, events, LoadLibraryW,
XInput via xinputuap) **is** in the allow-list.

Code that is Win32-only and needs abstraction (beyond the shim):

- Window/HWND: `fb_present.c` (SDL window, now the dummy driver),
  `nv2a_gpu_present.inc.c` (native-D3D11 `CreateSwapChainForHwnd`, unused
  under D3D12), F1 overlay and menus that query the client rect.
- Input: XInput (allowed, through xinputuap), SDL gamepads (now virtual
  joysticks), keyboard (CoreWindow `KeyDown`, still to bridge).
- Paths: `GetModuleFileNameA` (works, gives the install folder, which is
  read-only), `NFL2K5_PROJECT_ROOT` fallback (not reachable), saves and
  `nfl2k5_video.ini` beside the exe (must move to LocalState: `NFL2K5_ROOT`
  points there).
- WebView2 presentation host (stubbed), Discord IPC (named pipe, fails
  harmlessly), FFmpeg (POSIX only; Windows uses Media Foundation, which is
  in the UWP set except one transcode helper).
- Crash reporter: dbghelp is not available. Use `RtlCaptureStackBackTrace`
  plus a module/RVA dump, symbolised offline with `symrva.exe`.

## Build, package, run

Toolchain: VS 2022 (MSVC 14.44) with the store CRT libs (`VC\Tools\MSVC\*\lib\x64\store`,
present), Windows SDK 10.0.26100 (C++/WinRT headers, makeappx, signtool,
App Certification Kit: present). **No extra VS workload was needed.** The exe
is linked with `/APPCONTAINER` against `WindowsApp.lib` and the store CRT
(`vcruntime140_app.dll` from the `Microsoft.VCLibs.140.00` framework package).

```
tools\uwp\build-sdl-uwp.ps1             # UWP-minimal SDL3 -> E:\2K5Remade-local\sdl3-uwp
tools\uwp\build-uwp.ps1                 # stage-1 shell -> build-uwp\NFL2K5.exe (+ import check)
tools\uwp\build-uwp.ps1 -Game -Jobs 2   # + game -> build-uwp\game\NFL2K5.exe (first time: ~1 h of generated code)
tools\uwp\package-uwp.ps1 -Game -Sign   # layout + makeappx + signtool -> dist\uwp\NFL2K5_<ver>_x64.msix,
                                        #   + NFL2K5Dev.cer and the installer scripts
tools\uwp\package-uwp.ps1 -Sign -NewCert   # first time only: self-signed cert in E:\2K5Remade-local\uwp-cert
tools\uwp\probe-appcontainer.ps1        # memory-map/VEH/file/DXC probes in an AppContainer
tools\uwp\selftest-appcontainer.ps1     # audio + custom-music self-tests in an AppContainer
tools\uwp\run-game-test.ps1             # the game, unpackaged (--hwnd-test), with screenshots
node tools\uwp\test-webview-shim.js     # the WebView JS shim
tools\uwp\test-css-compat.cpp           # the EdgeHTML CSS rewrite (see the file for the build line)
```

The SDL source for the UWP build is `E:\2K5Remade-local\sdl-src-uwp`: a copy
of the desktop build's SDL (8bf3b7215 + local patches) with its own local git
holding the UWP commits (`SDL_UWP_MINIMAL` CMake option, CoreWindow /
composition / test-HWND swap chains in the D3D12 backend, XAudio2 playback
driver, no helper window). The desktop `sdl-src` is untouched.

When the interpreter sources are mid-edit by someone else, the UWP build can
compile a snapshot instead: `-DNFL2K5_UWP_INTERP_SRC=<file>` (cache variable).

## Sound, custom music, HTML scorebugs

- **Sound.** The APU output layer already writes an SDL3 audio stream; the
  UWP SDL gets an XAudio2 2.9 playback driver (`src/audio/xaudio2/SDL_xaudio2_uwp.cpp`
  in the SDL fork), so the stream plays through XAudio2 with no change to the
  tuned APU/resampler code. The log line `[UWP] SDL audio driver: xaudio2`
  confirms it at run time.
- **Custom music.** The Windows path was already Media Foundation (decode
  MP3/AAC/WMA/WAV with the source reader, encode the WMA the game's player
  needs with the sink writer); FFmpeg is only used on Linux/Android and is
  unchanged. The one helper not in the UWP set,
  `MFTranscodeGetAudioOutputAvailableTypes`, is rebuilt from `MFTEnumEx` + the
  WMA encoder MFT (`uwp_mf_shim.cpp`). Song folders: `<root>\Music`,
  `LocalState\Music`, and a folder named in `LocalState\music.txt` (e.g. a
  USB drive), each with one subfolder per playlist.
- **HTML scorebugs.** XAML host mode (`uwp_xaml_app.cpp`): the game draws into
  a SwapChainPanel (composition swap chain) and the presentation package runs
  in a XAML `WebView` (EdgeHTML on Xbox) behind it (`uwp_webview_host.cpp`):
  Android's JS shim (`chrome.webview` polyfill, `__nfl2k5_recv`,
  `__nfl2k5_busy`; page-to-game messages via `window.external.notify`), files
  served from the game folder in `WebResourceRequested` under the same
  `https://presentation.local/` (teams/erateams/portraits) names, frames
  captured with `CapturePreviewToStreamAsync` only while the page changes.
  EdgeHTML 18 has no `document.getAnimations()`, so the shim counts CSS
  animations from their events and keeps a page busy for 1.5 s after a change
  (transitions). The CSS `inset` shorthand (57 uses in the current packages)
  is rewritten to top/right/bottom/left as files are served. A package that
  fails to load, throws before its first frame, or whose capture has no
  transparency falls back to the native scorebug for the session, with the
  reason in the log. Not handled: flex-box `gap` and `aspect-ratio` (EdgeHTML
  lacks them; layouts using them may be off), `getAnimations`-driven code in
  `CBS_2026/replay-wipe.js` and `preview-clock.js` (they check for it and
  skip).
- **Modes.** XAML host mode is the default. CoreWindow mode (no WebView, native
  scorebug) is used when `LocalState\xaml-mode.txt` starts with `off`, and
  is switched on automatically if a XAML start dies within 45 s or the game's
  picture never reaches the panel (the next start then uses CoreWindow mode).
- **Saves and settings** always go to LocalState (`LocalState\NFL2K5-PC`),
  also when `root.txt` points at a read-only game folder.

### Installing

- **Windows (this PC):** `dist\uwp\Install NFL2K5 UWP.cmd` (double-click; it
  lists what it changes, then one UAC prompt): trusts `NFL2K5Dev.cer` in
  Local Machine > Trusted People, installs the `.msix` (and the VCLibs
  runtime if missing and its .appx is present; this PC already has it),
  grants ALL APPLICATION PACKAGES read/execute on `E:\2K5Remade\original`,
  `mods`, `Music`, `assets`, `data`, `font`, `moment-audio` (existing ones
  only; no copies), writes `root.txt` into the app's LocalState and starts the
  app. It records what it changed in `install-state.json`;
  `Uninstall NFL2K5 UWP.cmd` undoes exactly that (package, certificate if it
  added it, ACL entries it added). The .pfx is never shipped. The UAC prompt
  must be answered by the same Windows account that will play (an admin
  account), because the package and LocalState are per user.
- **Xbox (Dev Mode):** Dev Home → Device Portal (browser) → Apps → Add →
  the `.msix` plus the dependency `Microsoft.VCLibs.x64.14.00.appx` (the UWP
  one, from the VS "Universal Windows Platform development" workload's
  `ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64`, or a machine that
  has it). In Dev Home select the app → View details → App type: **Game**
  (5 GB instead of 1 GB). Game files (small installs only; LocalState is
  capped, see the loose-folder install below): copy them into the app's LocalState
  with Device Portal's File explorer, or put them on an NTFS USB drive (it
  mounts as E:) whose folders grant ALL APPLICATION PACKAGES read, and write
  that folder into `LocalState\root.txt`.
- **Xbox, game inside the app (loose-folder install, recommended):** Dev Mode
  caps an app's LocalState at a few GB (the user's Device Portal upload of
  the 6 GB of game files stopped with "out of storage" at about 3.3 GB). The
  app also looks for `original\default.xbe` in its own install folder
  (`uwp_game.cpp`; order: `root.txt`, `LocalState\original`, install folder,
  LocalState; the log line `[UWP] game folder ... (why)` names the choice),
  and every root is read-only to it (saves, settings, caches in
  `LocalState\NFL2K5-PC`). `tools\uwp\make-xbox-loose.cmd` builds
  `dist\xbox-loose\NFL2K5` = `build-uwp\layout` + `original\` (xbes and
  `vc_53450030`, never `disc\`/`CACHE\`) + `mods\presentations`, `teams`,
  `textures`, `presentation.ini` + `assets`, `data`, `font`, `moment-audio`
  (`-IncludeSoftdrink`, `-IncludeMusic` optional), hard-linked on the same
  drive. `tools\uwp\deploy-xbox-loose.cmd` asks for the console IP and the
  Device Portal login, offers (asks first) to uninstall an msix-installed
  NFL2K5, copies the folder to `D:\DevelopmentFiles\LooseApps\NFL2K5`
  (console SMB share from `/ext/smb/developerfolder` with robocopy; fallback
  file by file through `/api/app/packagemanager/upload`, skipping files
  already there with the same size, 3 tries each) and registers it
  (`/api/app/packagemanager/register?folder=<base64>`). Then App type:
  Game in Dev Home. Player steps: `tools\uwp\README-XBOX.txt` (also copied
  next to the loose folder), including the manual SMB way and the
  "Register from Network Share" fallback if the console refuses the size.
  Verified here: the install-folder root (a test package with the game
  inside, installed from `C:\Program Files\WindowsApps`, root.txt set aside:
  intro, title and demo with the SNF scorebug), the folder builder, and the
  deploy script against a local mock of those endpoints (auth, CSRF, the
  uninstall prompt, an upload cut off mid-file, resume, registration).
  Not verified: a real console (endpoint behaviour, size limits of
  DevelopmentFiles/loose installs, the SMB share).
- **Smaller: the data pack (2026-11-01).** `tools\xbpack\xbpack.exe` (built
  by `tools\xbpack\build-xbpack.ps1`, vendored zstd 1.5.6 in
  `external\zstd`) turns `original\vc_53450030` into one
  `original\vc_53450030.xbpak` (format: `external\xboxrecomp\src\kernel\xbpak_format.h`;
  reader: `kernel_pack.c`, hooked into `kernel_file.c` opens/reads/sizes,
  `kernel_overlay.c` for mod packs, `portraits.cpp`). The data repeats at
  MB distances (256 KB blocks: 77% of bank 1; 16 MB: 29%), so compressible
  16 MB regions are one zstd-19 block and the rest (audio, read at random in
  play) raw or 256 KB blocks. Sizes for banks 0..F (5.80 GB; pack G is the
  custom-teams pack the game rewrites at boot and is left out): lossless
  `--drop none` 3.81 GB; `--drop movies` 2.94 GB (the 30 MPEG program
  streams, 988 MB: boot intro, the Crib TV videos, three more, cut to their
  first ~64 KB plus an end code, so the player stops at once); `movies,crib`
  2.93 GB (the Crib's own assets are only 12 MB, so the Crib stays).
  `--keep-for <mod pack>` keeps the small retail runs a pack's overlay copies
  out of movie data (SOFTDRINK v0.7: 5,627 runs, 110 KB; without them it
  crashed at kickoff); `make-xbox-loose.ps1 -Pack` passes every installed
  pack. The tool decompresses the whole pack and compares every kept byte
  with the originals before renaming it into place. Game side: 256 MB block
  cache (`NFL2K5_XBPAK_CACHE_MB`), `NFL2K5_XBPAK_LOG=1` logs each unpacked
  block, `NFL2K5_NO_XBPAK=1` ignores packs. PC numbers (one game: boot,
  Quick Game, 2.5 min of play): 47 16 MB unpacks, 3.1 s in all, 30-110 ms
  each, mostly at scene loads; 256 KB unpacks ~1 ms. Verified on PC: retail
  boot (stub intro), menus, Crib, Quick Game to kickoff and play; SOFTDRINK
  v0.7 to kickoff and play; the installed UWP app (0.2.26.0) with the pack
  in `LocalState\original`. Not verified: a console (Jaguar unpack speed is
  roughly 3x slower, so scene loads take longer), every movie site (the
  three unnamed MPEGs, 92-257 MB, were not reached).
- **Without the installer:** Developer Mode on (Settings → System → For
  developers), then `Add-AppxPackage -Register build-uwp\layout\AppxManifest.xml`.
  Unsigned packages are refused ("an unsigned package cannot include
  Executable activations").

## Status (2026-10-31, installed package)

The user installed the package with `Install NFL2K5 UWP.cmd`; it runs from
`C:\Program Files\WindowsApps` in its AppContainer on this PC.

Works (screenshots in `D:\claude-overflow\uwp-checklist\`):

- Boot, title, menus, a CPU vs CPU Quick Game through to the halftime show,
  XAudio2 sound and menu music, keyboard, XAML host mode with the HTML
  scorebug drawn as a transparent EdgeHTML WebView over the SwapChainPanel.
- Saves: the VIP profile is created, saved and loaded on the next start
  (`LocalState\NFL2K5-PC\saves`). GetFinalPathNameByHandleW is denied in an
  AppContainer for every name type, so the kernel layer keeps each directory
  handle's host path (`kernel_file.c`); before that, save folders listed as
  empty and every save failed with "Cannot create save on Xbox Hard Disk".
- F1 menu text, the music toast and the PlayStation prompt sheet: the GDI
  calls are implemented on Direct2D/DirectWrite (`uwp_gdi_d2d.cpp`). The video
  menu hides the display-mode, desktop-resolution and API rows on UWP.
- EdgeHTML compatibility for the packages, applied as the host serves them:
  `inset`, `color-mix()` (literal pairs mixed exactly, var() mixes take the
  larger share), `conic-gradient()`, canvas `measureText()` ink/font boxes
  (polyfill: the SNF scores, clock and quarter were NaN-sized), `.svg` images
  as data URIs plus an intrinsic size. `NFL2K5_HTMLPRES_PROBE=1` logs element
  layout every 15 s for comparing with WebView2.
- `LocalState\env.txt` sets environment variables; `NFL2K5_PRESS_FILE` drives
  the game without window focus (used by the test scripts).
- The selected mod pack is kept in `LocalState\NFL2K5-PC\mods\active-pack.ini`
  and applies at the next start (a UWP app cannot hand off to another exe).

Xbox readiness (cannot be tested without a console):

- Set the app to **Game** in Dev Home / Device Portal (5 GB instead of 1 GB);
  this is not a manifest setting.
- XAML mode sets `RequiresPointerMode = WhenRequested` and swallows Back, so
  the pad is not turned into a mouse cursor and B does not leave the app.
- Needs the Microsoft.VCLibs.140.00 UWP framework package installed on the
  console; game files in LocalState or on an NTFS USB drive that grants ALL
  APPLICATION PACKAGES read.
- Untested there: fixed-address guest memory map, D3D12 FL11, suspend/resume
  (no Trim on suspend yet), EdgeHTML version on the console.

Open:

- (Fixed) The boot intro froze on its 1st or 4th movie on every renderer: a
  scheduler pulse ran a present on a second thread that spun holding the guest
  lock. With xboxrecomp d7c26a4 the installed app plays all four movies, then
  the title and the attract demo; the UWP skip default is gone.
- Controllers: Windows.Gaming.Input -> SDL virtual gamepads (with rumble) is
  in place but no pad was connected for a test.
- Header prompt icons sometimes show the original Xbox L/R/white/black art
  instead of the modern LT/RT art; not investigated.
- The `.2k5patch` disc-image installer (it starts another exe) is unavailable.