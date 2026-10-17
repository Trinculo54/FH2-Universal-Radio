#pragma once

// Shared by the FH2 mods (FH2 Universal Radio, WheresMyBuddy). Keep the
// copies in each repo identical.
//
// Update notices. Each mod checks, in the background:
//   - that the installed FH2 Mod Loader (d3dcompiler_47.dll) is at least as
//     new as the one it ships with (an older mod's zip may have put an older
//     one back), and
//   - whether its GitHub repo has a newer release (at most every 6 hours;
//     the answer is remembered between launches).
// Anything worth knowing goes in fh2-mods\notices\<mod>.txt, and the notices
// of every installed mod are shown together on the title screen, one per
// line under "PRESS (A) TO START", through Extra Options' mediaoverride
// folder.

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace fh2common {

struct ModInfo {
    std::wstring name;               // "WheresMyBuddy"; also the notice file name
    std::wstring dll;                // "WheresMyBuddy.dll" in fh2-mods
    std::string version;             // this build, "1.1.0"
    std::string repo;                // "Trinculo54/WheresMyBuddy"
    std::string loader_version;      // the FH2 Mod Loader this mod ships with
    std::filesystem::path azimuth_dir;
    bool check_online = true;
    std::function<void(const std::string&)> log;
};

// Starts the checks on a background thread.
void start_update_check(ModInfo info);

// Version of the installed loader; "1.0.0" for the first one, which had no
// version info.
std::string installed_loader_version(const std::filesystem::path& azimuth_dir);

// <0, 0, >0 like strcmp, for "1.2.3" / "v1.2.3" style versions.
int compare_versions(std::string_view a, std::string_view b);

// Rebuilds the title-screen text from all notice files (exposed for tests).
void refresh_title_notices(const std::filesystem::path& azimuth_dir);

} // namespace fh2common
