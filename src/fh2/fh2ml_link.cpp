#include "fh2/fh2ml_link.hpp"

#include <atomic>

namespace fh2 {

namespace {
std::atomic<const FH2ML_Services_v4*> g_services{nullptr};
std::atomic<FH2ML_Mod*> g_mod{nullptr};
} // namespace

void set_services(const FH2ML_Services_v4* services, FH2ML_Mod* mod) noexcept {
    g_mod      = mod;
    g_services = services;
}

const FH2ML_Services_v4* services() noexcept { return g_mod ? g_services.load() : nullptr; }

FH2ML_Mod* mod_handle() noexcept { return g_mod; }

} // namespace fh2
