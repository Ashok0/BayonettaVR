#include <windows.h>
#include <cstdio>

int main() {
    HMODULE dll = LoadLibraryW(L"build\\Win32\\Release\\d3d9.dll");
    if (!dll) { std::printf("LoadLibrary failed: %lu\n", GetLastError()); return 1; }
    const char* names[] = {"Direct3DCreate9Ex", "Direct3DCreate9", "D3DPERF_BeginEvent", "D3DPERF_EndEvent"};
    for (const char* name : names) {
        if (!GetProcAddress(dll, name)) { std::printf("missing export: %s\n", name); return 2; }
    }
    FreeLibrary(dll);
    std::puts("proxy load/export test passed");
    return 0;
}
