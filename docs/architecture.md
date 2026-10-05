# Architecture and milestones

Original XBE -> analysis/function discovery -> generated C -> Xbox compatibility layer -> native NFL2K5.exe.

Keep generated code in src/recomp/gen and manual Xbox overrides in src/xbox. Preserve upstream in external/xboxrecomp; record its revision before changes.

The eventual renderer must expose an API-neutral interface for resources, pipelines, draws, synchronization, presentation, and resize. Implement separate DirectX 12 and Vulkan backends in src/renderer. Xbox D3D8/NV2A translation should feed this interface; guest/game code must not depend directly on either host API. Shader translation and resource semantics require explicit work and validation.

Upstream currently provides a D3D11 compatibility path. Compiling it validates dependencies only; it does not implement the requested modern renderer. A temporary upstream path may help CPU/runtime bring-up, but the long-term architecture is DX12 plus Vulkan.

Reserve src/upscaling/streamline for later integration. Do not add Streamline/DLSS until the renderer provides suitable color, depth, motion vectors, exposure, jitter, and resource lifecycle integration. Backend-specific support must be verified then.

Milestones: (1) runtime libraries and native toolchain check; (2) inspect the user XBE and record hash, title, imports, entry point, and unsupported code; (3) generate C and review dispatch/ABI/runtime initialization; (4) build an actual NFL2K5.exe, debug first guest execution; (5) graphics/audio/input/filesystem bring-up; (6) DX12 and Vulkan; (7) optional upscaling. None of the later milestones is implied by a successful library build.

Vulkan SDK and Ghidra/JDK are deferred until their corresponding renderer/deeper-analysis milestones. Git, Python, Capstone, MSVC, CMake, Ninja and Windows SDK cover the initial pipeline. No proprietary Xbox SDK is required or downloaded.