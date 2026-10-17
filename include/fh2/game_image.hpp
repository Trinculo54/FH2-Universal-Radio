#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fh2 {

// The FH2 executable as mapped into this process by the translation layer.
// Azimuth maps the Durango PE itself (it never shows up in the loader's
// module list), so we find it by walking the address space for an
// allocation whose PE header matches the known build.
struct GameImage {
    std::byte* base = nullptr;
    std::size_t size = 0;

    bool valid() const noexcept { return base != nullptr; }
    std::byte* at(uint32_t rva) const noexcept { return base + rva; }
};

// One pass over the address space. Cheap enough to poll every few hundred ms.
std::optional<GameImage> find_game_image() noexcept;

// RVA of a named export, or 0. Walks the mapped export directory.
uint32_t find_export(const GameImage& img, std::string_view name) noexcept;

// Patch helpers ------------------------------------------------------------

// Executable memory within +/-2 GB of `anchor`, so rel32 calls can reach it.
std::byte* alloc_near(const std::byte* anchor, std::size_t size) noexcept;

// If the 5 bytes at `site` are `E8 rel32` calling `expected_target`, retarget
// the call at `new_target` (one 8-byte CAS when the instruction doesn't cross
// a qword boundary). Returns false and leaves the code alone otherwise.
bool patch_call(std::byte* site, const void* expected_target, const void* new_target) noexcept;

// Current target of the `E8 rel32` at `site`, or nullptr if it isn't one.
const std::byte* call_target(const std::byte* site) noexcept;

} // namespace fh2
