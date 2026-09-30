A native Windows port and static recompiler for ESPN NFL 2K5 (Original Xbox). This project translates original Xbox executable (.xbe) machine code into native C/C++ while providing High-Level Emulation (HLE) of the Xbox kernel and low-level translation of its bespoke hardware sub-systems.

By intercepting the game's original logic, this project allows for arbitrary resolutions, modernized rendering, and a completely custom, data-driven broadcast presentation engine.



Modern RHI (nfl2k5-rhi): A C++23 static library providing DirectX 12 and Vulkan rendering backends. It intercepts raw NV2A pushbuffer words, translates legacy fixed-function hardware states and custom vertex microcode, and issues modern command list This will be a seperate project that is part of the same repo.
However this allows for accurate rendering to the original and even allows for new rendering tech (DLSS, FSR, XeSS) should also run extremely well on different hardware setups 

Prerequisites 

    CMake 3.20+

    Visual Studio / MSVC (or a C++23 compatible compiler)

    Windows 10 SDK

    Vulkan SDK (MODERN RHI ONLY) 

You must provide your own legally dumped copy of the retail ESPN NFL 2K5 Xbox disc. Extract the contents and place them in the original/disc directory, with the retail default.xbe placed in the original/ root.

Due to the extreme timing sensitivity of threading handoffs between host Windows threads and the Xbox guest scheduler, standard debugging tools or stdout logging often perturb execution and hide race conditions.

The project utilizes a vast array of lock-free telemetry variables (e.g., volatile LONG) and single-step hardware execution watches (RECOMP_HW_WATCH=1) to intercept memory reads, measure pushbuffer limits, and isolate deadlocks without stalling the guest CPU.

