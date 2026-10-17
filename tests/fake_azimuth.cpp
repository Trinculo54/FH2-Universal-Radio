// Smoke test host: pretends to be azimuth.exe. Loads d3dcompiler_47.dll from
// its own folder (our proxy), resolves D3DCompile through the forwarder, maps
// the FH2 exe like a translation layer would, then idles so the bridge can
// find the image, patch it and serve the dashboard.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <algorithm>

static std::byte* map_image(const wchar_t* path) {
    std::ifstream in{path, std::ios::binary};
    std::vector<char> file((std::istreambuf_iterator<char>(in)), {});
    if (file.size() < 4096) return nullptr;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    auto* base = static_cast<std::byte*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
                                                       MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec)
        if (sec->SizeOfRawData)
            std::memcpy(base + sec->VirtualAddress, file.data() + sec->PointerToRawData,
                        std::min<DWORD>(sec->SizeOfRawData, sec->Misc.VirtualSize));
    return base;
}

int wmain(int argc, wchar_t** argv) {
    HMODULE d3d = LoadLibraryW(L"d3dcompiler_47.dll");
    std::printf("proxy loaded: %p\n", (void*)d3d);
    FARPROC compile = d3d ? GetProcAddress(d3d, "D3DCompile") : nullptr;
    HMODULE owner = nullptr;
    if (compile)
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(compile), &owner);
    wchar_t name[MAX_PATH]{};
    if (owner) GetModuleFileNameW(owner, name, MAX_PATH);
    std::printf("D3DCompile -> %p in %ls (proxy=%s)\n", (void*)compile, name, owner == d3d ? "YES?!" : "no, forwarded");
    Sleep(3000); // Azimuth maps the game a bit after startup
    std::byte* game = argc > 1 ? map_image(argv[1]) : nullptr;
    std::printf("game mapped at %p\n", (void*)game);
    std::fflush(stdout);
    Sleep(argc > 2 ? _wtoi(argv[2]) * 1000 : 20000);
    return 0;
}
