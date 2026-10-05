#include <windows.h>
#include <d3d12.h>
#include <cstdio>
int main() {
    std::puts("NFL2K5 development toolchain check: native x64 build OK.");
    std::puts("This is a build check, not a recompiled game. DX12/Vulkan renderers are pending.");
    return sizeof(void*) == 8 ? 0 : 1;
}