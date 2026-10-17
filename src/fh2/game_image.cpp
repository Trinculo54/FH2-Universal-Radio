#include "fh2/game_image.hpp"
#include "fh2/offsets.hpp"
#include "fh6/safe_mem.hpp"

#include <windows.h>
#include <cstring>

namespace fh2 {

namespace {

constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

bool header_matches(const std::byte* base) noexcept {
    IMAGE_DOS_HEADER dos{};
    if (!fh6::safe_read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (dos.e_lfanew < 64 || dos.e_lfanew > 4096) return false;
    IMAGE_NT_HEADERS64 nt{};
    if (!fh6::safe_read(base + dos.e_lfanew, nt)) return false;
    return nt.Signature == IMAGE_NT_SIGNATURE &&
           nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           nt.FileHeader.TimeDateStamp == image::kTimestamp &&
           nt.OptionalHeader.SizeOfImage == image::kSizeOfImage;
}

} // namespace

std::optional<GameImage> find_game_image() noexcept {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    auto addr       = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const auto last = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < last && VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const auto region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const auto next   = region + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.BaseAddress == mbi.AllocationBase &&
            (mbi.Protect & kReadable) && !(mbi.Protect & PAGE_GUARD) && mbi.RegionSize >= 0x1000) {
            auto* base = static_cast<std::byte*>(mbi.BaseAddress);
            if (header_matches(base)) return GameImage{base, image::kSizeOfImage};
        }
        if (next <= addr) break;
        addr = next;
    }
    return std::nullopt;
}

uint32_t find_export(const GameImage& img, std::string_view name) noexcept {
    uint32_t result = 0;
    fh6::seh_call([&] {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(img.base);
        const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(img.base + dos->e_lfanew);
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress || dir.VirtualAddress >= img.size) return;
        const auto* ed    = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(img.base + dir.VirtualAddress);
        const auto* names = reinterpret_cast<const uint32_t*>(img.base + ed->AddressOfNames);
        const auto* ords  = reinterpret_cast<const uint16_t*>(img.base + ed->AddressOfNameOrdinals);
        const auto* funcs = reinterpret_cast<const uint32_t*>(img.base + ed->AddressOfFunctions);
        for (DWORD i = 0; i < ed->NumberOfNames; ++i) {
            const char* n = reinterpret_cast<const char*>(img.base + names[i]);
            if (name.size() == std::strlen(n) && std::memcmp(n, name.data(), name.size()) == 0) {
                if (ords[i] < ed->NumberOfFunctions) result = funcs[ords[i]];
                return;
            }
        }
    });
    return result;
}

std::byte* alloc_near(const std::byte* anchor, std::size_t size) noexcept {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uintptr_t gran   = si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
    const uintptr_t origin = reinterpret_cast<uintptr_t>(anchor) & ~(gran - 1);
    constexpr uintptr_t kRange = 0x70000000; // ~1.75 GB either side

    // Walk outwards: just below the image first (the slot above is usually
    // the game's own heap reservations), then above.
    for (uintptr_t d = gran; d < kRange; d += gran) {
        if (origin > d) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(origin - d), size,
                                       MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                return static_cast<std::byte*>(p);
        }
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(origin + image::kSizeOfImage + d), size,
                                   MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
            return static_cast<std::byte*>(p);
    }
    return nullptr;
}

const std::byte* call_target(const std::byte* site) noexcept {
    uint8_t op = 0;
    int32_t rel = 0;
    if (!fh6::safe_read(site, op) || op != 0xE8) return nullptr;
    if (!fh6::safe_read(site + 1, rel)) return nullptr;
    return site + 5 + rel;
}

bool patch_call(std::byte* site, const void* expected_target, const void* new_target) noexcept {
    if (call_target(site) != expected_target) return false;

    const auto delta = reinterpret_cast<intptr_t>(new_target) - reinterpret_cast<intptr_t>(site + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) return false;
    const auto rel = static_cast<int32_t>(delta);

    DWORD old = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return false;

    bool ok             = false;
    const auto addr     = reinterpret_cast<uintptr_t>(site);
    const auto aligned  = addr & ~uintptr_t{7};
    const auto offset   = addr - aligned;
    if (offset <= 3) {
        // Whole instruction sits inside one qword: swap it in a single
        // locked write so a thread executing it never sees a torn rel32.
        auto* q = reinterpret_cast<volatile LONG64*>(aligned);
        for (int tries = 0; tries < 16 && !ok; ++tries) {
            LONG64 cur = *q;
            LONG64 nxt = cur;
            std::memcpy(reinterpret_cast<std::byte*>(&nxt) + offset + 1, &rel, 4);
            ok = InterlockedCompareExchange64(q, nxt, cur) == cur;
        }
    } else {
        std::memcpy(site + 1, &rel, 4);
        ok = true;
    }

    DWORD ignored = 0;
    VirtualProtect(site, 5, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return ok;
}

} // namespace fh2
