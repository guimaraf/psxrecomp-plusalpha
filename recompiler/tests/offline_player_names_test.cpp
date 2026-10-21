#include "config_loader.h"
#include "../../runtime/include/offline_player_names.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
static int failures;
template<class Char> static std::string utf8(const Char* text) {
    return reinterpret_cast<const char*>(text);
}
static void check(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); ++failures; }
}
static std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

int main() {
    check(psx::offline_player_name("123456789") == "12345678", "offline limit is eight");
    check(psx::offline_player_name(utf8(u8"áéíóúâêôç")) == utf8(u8"áéíóúâêô"), "accents count as characters, not bytes");
    check(psx::offline_player_name(utf8(u8"a\u0301bcdefghZ")) == utf8(u8"a\u0301bcdefgh"), "combining accent does not consume a letter");
    check(psx::offline_player_name("A\nB") == "AB", "control characters are removed");
    check(psx::offline_player_name(std::string("A\xFF") + "B") == "AB", "invalid UTF-8 is removed");
    check(psx::offline_player_name("").empty(), "empty names preserve fallback semantics");

    const auto folder = fs::temp_directory_path() / ("psx-offline-names-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    const auto path = folder / "settings.toml";
    std::string error;
    check(PSXRecompV4::save_offline_player_names(path, "Fred", "Felipe", error), "Save creates a fresh settings file");
    auto settings = PSXRecompV4::load_user_settings(path);
    check(!settings.parse_error && settings.offline_player_names[0] == "Fred" &&
          settings.offline_player_names[1] == "Felipe", "both names survive reopen");
    settings.netplay_player_name = "OnlineName";
    settings.has_netplay_player_name = true;
    settings.player_nameplates = false;
    settings.has_player_nameplates = true;
    settings.audio_freq = 48000;
    settings.has_audio_freq = true;
    check(PSXRecompV4::save_user_settings(path, settings), "seed other settings");
    const std::string quoted = "A\"B\\C";
    check(PSXRecompV4::save_offline_player_names(path, quoted, utf8(u8"João"), error), "Save replaces existing settings");
    settings = PSXRecompV4::load_user_settings(path);
    check(!settings.parse_error && settings.offline_player_names[0] == quoted &&
          settings.offline_player_names[1] == utf8(u8"João"), "quotes, backslashes and UTF-8 round-trip");
    check(settings.netplay_player_name == "OnlineName", "online identity is preserved");
    check(!settings.player_nameplates && settings.audio_freq == 48000, "other preferences are preserved");
    const auto saved = read(path);
    check(!PSXRecompV4::save_offline_player_names(path, "123456789", "P2", error) && !error.empty(), "ninth character refuses Save");
    check(read(path) == saved, "refused Save cannot change the file");
    check(PSXRecompV4::save_offline_player_names(path, "", "", error), "empty Save clears names");
    settings = PSXRecompV4::load_user_settings(path);
    check(settings.has_offline_player_names[0] && settings.offline_player_names[0].empty() &&
          settings.has_offline_player_names[1] && settings.offline_player_names[1].empty(), "empty names persist");
    {
        std::ofstream file(path, std::ios::trunc);
        file << "[video]\noffline_player_1 = \"1234567890\"\n";
    }
    settings = PSXRecompV4::load_user_settings(path);
    check(settings.offline_player_names[0] == "12345678", "hand-edited files also obey eight characters");
    {
        std::ofstream file(path, std::ios::trunc);
        file << "[video\n";
    }
    const auto broken = read(path);
    check(!PSXRecompV4::save_offline_player_names(path, "P1", "P2", error), "invalid settings refuse Save");
    check(read(path) == broken, "failed parse leaves original file intact");
    const auto blocked = folder / "not-a-file";
    fs::create_directory(blocked);
    check(!PSXRecompV4::save_offline_player_names(blocked, "P1", "P2", error), "I/O failure is reported");
    fs::remove(path);
    fs::remove(blocked);
    check(fs::is_empty(folder), "Save leaves no temporary files behind");
    fs::remove(folder);
    if (failures) return 1;
    std::puts("offline_player_names_test: ok");
    return 0;
}
