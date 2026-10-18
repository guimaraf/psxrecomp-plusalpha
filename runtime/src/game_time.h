#pragma once

#include <filesystem>
#include <string>

namespace psx_game_time {
// Windows keeps the portable layout; Linux/macOS use per-user writable storage.
std::filesystem::path save_path(const std::filesystem::path& executable_dir,
                                const std::string& game_id);
// Reading for the launcher never creates or updates the save.
std::string display(const std::filesystem::path& path, const std::string& game_id);
// Starts a session immediately before emulation; stop before returning to lobby.
void start(const std::filesystem::path& path, const std::string& game_id);
void stop();
}
