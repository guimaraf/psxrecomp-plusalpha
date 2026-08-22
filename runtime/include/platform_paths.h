#pragma once

#include <filesystem>
#include <string>

namespace PSXRuntime {

struct PlatformPaths {
    std::filesystem::path content_root;
    std::filesystem::path user_root;
    std::filesystem::path config_dir;
    std::filesystem::path saves_dir;
    std::filesystem::path states_dir;
    std::filesystem::path logs_dir;
    std::filesystem::path bios_dir;
    std::filesystem::path disc_dir;
    bool first_run = false;
    bool uwp = false;
};

// Initialize once, before loading game.toml or any mutable runtime file.
// Desktop ports keep their historical executable-relative layout. UWP uses
// SDL_GetPrefPath("PSXRecomp", "SLUS-00548") and seeds writable templates.
bool initialize_platform_paths(const std::filesystem::path& executable_dir,
                               std::string* error_out);

const PlatformPaths& platform_paths();

std::filesystem::path mutable_config_path(const char* filename);

// UWP only: append stdout/stderr to LocalState/logs/debug_log.txt. Desktop is a
// no-op so its console/logging behaviour remains byte-for-byte compatible.
bool redirect_platform_logs(std::string* error_out);

} // namespace PSXRuntime
