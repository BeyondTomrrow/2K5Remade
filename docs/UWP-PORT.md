# UWP / Xbox Dev Mode port

Status: started 2026-10-31. Goal: run the recompiled game as a UWP app, on
Xbox One / Series consoles in Dev Mode (the way RetroArch and xemu UWP builds
run) and as a Store-style Windows app.

Stage 1 (CoreWindow + D3D12 shell, probes, packaging) works. Stage 2 (the
game) builds and links; see "Status" at the end for what has run and what is
still open.

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
| WebView2 | Not on Xbox. The HTML scorebug packages fall back to the native HUD (`uwp_webview2_stub.cpp`). A later option is to host packages in the XAML `WebView` (EdgeHTML on Xbox), capturing frames with `CapturePreviewToStreamAsync`. | - |
| Threading | CoreApplication owns the main (ASTA) thread, so the game runs on its own thread and the UI thread only dispatches events and polls gamepads. Suspend/resume: Xbox suspends games in the background, so D3D12 must `Trim` on suspend (TODO in stage 2). | - |
| Audio | XAudio2 2.9 and WASAPI (`ActivateAudioInterfaceAsync`) are UWP APIs. SDL's WASAPI uses `IMMDeviceEnumerator` (desktop) and is off in the UWP build. The game's SDL3 audio stream output needs a UWP sink: the planned way is XAudio2 directly in the apu output layer. Until then audio goes to SDL's dummy driver (silent). | - |
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
tools\uwp\build-uwp.ps1                 # shell -> build-uwp\NFL2K5.exe (+ import check)
tools\uwp\build-sdl-uwp.ps1             # UWP-minimal SDL3 -> E:\2K5Remade-local\sdl3-uwp
tools\uwp\build-uwp.ps1 -Game           # + game -> build-uwp\game\NFL2K5.exe
tools\uwp\package-uwp.ps1 -Sign         # layout + makeappx + signtool -> dist\uwp\NFL2K5_<ver>_x64.msix
tools\uwp\package-uwp.ps1 -Sign -NewCert   # first time: self-signed cert in E:\2K5Remade-local\uwp-cert
tools\uwp\probe-appcontainer.ps1        # run the probes in an AppContainer (no install needed)
```

The SDL source for the UWP build is `E:\2K5Remade-local\sdl-src-uwp`: a copy
of the desktop build's SDL (8bf3b7215 + local patches) with its own local git
holding the UWP commits. The desktop `sdl-src` is untouched.

### Installing

- **Xbox (Dev Mode):** Dev Home → Device Portal (browser) → Apps → Add →
  the `.msix`. Add the dependency `Microsoft.VCLibs.x64.14.00.appx` (the UWP
  one, from the VS "Universal Windows Platform development" workload's
  `ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\x64`, or from a machine
  that has it). Then in Dev Home select the app → View details → App type:
  **Game**. Copy the game files into the app's LocalState with Device
  Portal's File explorer, or point `LocalState\root.txt` at a USB folder.
- **Windows:** the package is signed with a self-signed certificate. To
  install it you need either **Developer Mode** (Settings → System → For
  developers; then `Add-AppxPackage -Register build-uwp\layout\AppxManifest.xml`),
  or the `.cer` trusted in *Local Machine → Trusted People* (admin). Both
  are system settings, so they were not changed here. Unsigned packages with
  the OID publisher are refused ("an unsigned package cannot include
  Executable activations").

## Status

See the report at the end of the 2026-10-31 session, kept in sync below.

- Stage 1: `build-uwp\NFL2K5.exe` builds with no imports outside the UWP API set.
  `dist\uwp\NFL2K5_0.1.0.0_x64.msix` packs and signs. All 15 probes pass in an AppContainer
  on this PC. The CoreWindow/D3D12 screen itself has not been seen yet:
  installing needs Developer Mode on this PC (see above).
- Stage 2: see the commit log for the current state.
