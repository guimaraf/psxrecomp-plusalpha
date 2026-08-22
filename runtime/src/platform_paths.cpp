#include "platform_paths.h"

#include <SDL.h>

#include <cstdio>
#include <system_error>

namespace fs = std::filesystem;

namespace PSXRuntime {
namespace {

PlatformPaths g_paths;
bool g_initialized = false;

#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
constexpr bool kIsUwp = true;
#else
constexpr bool kIsUwp = false;
#endif

bool copy_template_if_missing(const fs::path& source,
                              const fs::path& destination,
                              std::string* error_out) {
    std::error_code ec;
    if (fs::exists(destination, ec)) return true;
    ec.clear();
    if (!fs::exists(source, ec)) {
        if (error_out) *error_out = "template ausente: " + source.string();
        return false;
    }
    ec.clear();
    fs::copy_file(source, destination, fs::copy_options::none, ec);
    if (ec) {
        if (error_out) {
            *error_out = "falha ao copiar " + source.string() + " para " +
                         destination.string() + ": " + ec.message();
        }
        return false;
    }
    return true;
}

bool create_directory_checked(const fs::path& path, std::string* error_out) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        if (error_out) {
            *error_out = "falha ao criar " + path.string() + ": " + ec.message();
        }
        return false;
    }
    return true;
}

} // namespace

bool initialize_platform_paths(const fs::path& executable_dir,
                               std::string* error_out) {
    if (g_initialized) return true;
    if (error_out) error_out->clear();

    g_paths = {};
    g_paths.uwp = kIsUwp;
    g_paths.content_root = executable_dir;

    if (!kIsUwp) {
        // Preserve the established Windows/Linux portable layout. The launcher
        // remains the authority for choosing BIOS, disc and memory-card paths.
        g_paths.user_root = executable_dir;
        g_paths.config_dir = executable_dir;
        g_paths.saves_dir = executable_dir;
        g_paths.states_dir = executable_dir;
        g_paths.logs_dir = executable_dir;
        g_paths.bios_dir = executable_dir / "bios";
        g_paths.disc_dir = executable_dir / "disc";
        g_initialized = true;
        return true;
    }

    if (char* base_path = SDL_GetBasePath()) {
        g_paths.content_root = fs::path(base_path);
        SDL_free(base_path);
    }

    char* pref_path = SDL_GetPrefPath("PSXRecomp", "SLUS-00548");
    if (!pref_path) {
        if (error_out) {
            *error_out = std::string("SDL_GetPrefPath falhou: ") + SDL_GetError();
        }
        return false;
    }
    g_paths.user_root = fs::path(pref_path);
    SDL_free(pref_path);

    g_paths.config_dir = g_paths.user_root / "config";
    g_paths.saves_dir = g_paths.user_root / "saves";
    g_paths.states_dir = g_paths.saves_dir / "states";
    g_paths.logs_dir = g_paths.user_root / "logs";
    g_paths.bios_dir = g_paths.user_root / "bios";
    g_paths.disc_dir = g_paths.user_root / "disc";

    // game.toml is the first-run sentinel. SDL_GetPrefPath creates its own base
    // directory, so testing user_root itself cannot distinguish a first launch.
    std::error_code exists_ec;
    g_paths.first_run = !fs::exists(g_paths.config_dir / "game.toml", exists_ec);

    for (const fs::path* dir : {&g_paths.config_dir, &g_paths.saves_dir,
                                &g_paths.states_dir, &g_paths.logs_dir,
                                &g_paths.bios_dir, &g_paths.disc_dir}) {
        if (!create_directory_checked(*dir, error_out)) return false;
    }

    for (const char* filename : {"game.toml", "settings.toml", "input.ini",
                                 "keybinds.ini"}) {
        if (!copy_template_if_missing(g_paths.content_root / filename,
                                      g_paths.config_dir / filename,
                                      error_out)) {
            return false;
        }
    }

    g_initialized = true;
    return true;
}

const PlatformPaths& platform_paths() {
    return g_paths;
}

fs::path mutable_config_path(const char* filename) {
    return g_paths.config_dir / (filename ? filename : "");
}

bool redirect_platform_logs(std::string* error_out) {
    if (!g_paths.uwp) return true;
    if (error_out) error_out->clear();

    const fs::path log_path = g_paths.logs_dir / "debug_log.txt";
    FILE* out = std::freopen(log_path.string().c_str(), "a", stdout);
    FILE* err = std::freopen(log_path.string().c_str(), "a", stderr);
    if (!out || !err) {
        if (error_out) *error_out = "não foi possível abrir " + log_path.string();
        return false;
    }
    // UCRT requires a non-zero size for buffered modes. Keep the UWP log
    // streams unbuffered so startup diagnostics survive an early exit/crash.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::fprintf(stderr, "\n=== PSXRecomp UWP session ===\n");
    return true;
}

} // namespace PSXRuntime
