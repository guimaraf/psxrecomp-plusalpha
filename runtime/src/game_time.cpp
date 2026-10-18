#include "game_time.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <vector>

#if defined(_WIN32) && !defined(PSX_UWP)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

#if defined(_WIN32)
namespace psx_game_time {
namespace {
std::string format(uint64_t seconds) {
    char text[64];
    std::snprintf(text, sizeof(text), "%03llu:%02llu",
        static_cast<unsigned long long>(seconds / 3600),
        static_cast<unsigned long long>((seconds / 60) % 60));
    return text;
}

#if defined(_WIN32) && !defined(PSX_UWP)
using Clock = std::chrono::steady_clock;
constexpr char magic[] = "PSXTIME1";

DATA_BLOB entropy_blob(std::string& entropy) {
    return {static_cast<DWORD>(entropy.size()),
            reinterpret_cast<BYTE*>(entropy.data())};
}

bool read_total(const std::filesystem::path& path, const std::string& game_id,
                uint64_t& seconds) {
    seconds = 0;
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return false;
    if (!exists) return true;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = file.tellg();
    if (size <= 0 || size > 4096) return false;
    std::vector<BYTE> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return false;
    std::string entropy = "psxrecomp/gametime/v1/" + game_id;
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()};
    DATA_BLOB extra = entropy_blob(entropy), output{};
    if (!CryptUnprotectData(&input, nullptr, &extra, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
    const bool valid = output.cbData == 16 && std::memcmp(output.pbData, magic, 8) == 0;
    if (valid) {
        for (unsigned i = 0; i < 8; ++i)
            seconds |= static_cast<uint64_t>(output.pbData[8 + i]) << (8 * i);
    }
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return valid;
}

bool write_total(const std::filesystem::path& path, const std::string& game_id,
                 uint64_t seconds) {
    auto temporary = path;
    temporary += ".tmp." + std::to_string(GetCurrentProcessId()) + "." +
                 std::to_string(GetTickCount64());
    std::array<BYTE, 16> payload{};
    std::memcpy(payload.data(), magic, 8);
    for (unsigned i = 0; i < 8; ++i)
        payload[8 + i] = static_cast<BYTE>(seconds >> (8 * i));
    std::string entropy = "psxrecomp/gametime/v1/" + game_id;
    DATA_BLOB input{static_cast<DWORD>(payload.size()), payload.data()};
    DATA_BLOB extra = entropy_blob(entropy), output{};
    const BOOL encrypted = CryptProtectData(&input, nullptr, &extra, nullptr, nullptr,
                                            CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureZeroMemory(payload.data(), payload.size());
    if (!encrypted) return false;
    // Replace only after a complete encrypted write; keep the old save on failure.
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool saved = false;
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        saved = WriteFile(file, output.pbData, output.cbData, &written, nullptr) &&
                written == output.cbData && FlushFileBuffers(file);
        CloseHandle(file);
        if (saved) saved = MoveFileExW(temporary.c_str(), path.c_str(),
                                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!saved) DeleteFileW(temporary.c_str());
    }
    LocalFree(output.pbData);
    return saved;
}

struct Session {
    std::filesystem::path path;
    std::string game_id;
    uint64_t previous = 0;
    Clock::time_point began;
    HANDLE mutex = nullptr;
    bool active = false;
};

Session& session() {
    static Session value;
    return value;
}

void finish() noexcept {
    auto& state = session();
    if (!state.active) return;
    state.active = false;
    try {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - state.began).count();
        const uint64_t delta = elapsed > 0 ? static_cast<uint64_t>(elapsed) : 0;
        const uint64_t total = state.previous +
            (delta > UINT64_MAX - state.previous ? UINT64_MAX - state.previous : delta);
        if (!write_total(state.path, state.game_id, total))
            std::fprintf(stderr, "play time: could not save gametime.save; previous total preserved.\n");
    } catch (...) {
        std::fprintf(stderr, "play time: save failed; previous total preserved.\n");
    }
    ReleaseMutex(state.mutex);
    CloseHandle(state.mutex);
    state.mutex = nullptr;
}
#endif
}

std::filesystem::path save_path(const std::filesystem::path& executable_dir,
                                const std::string& game_id) {
    (void)game_id;
    return executable_dir / "gametime.save";
}

std::string display(const std::filesystem::path& path, const std::string& game_id) {
#if defined(_WIN32) && !defined(PSX_UWP)
    try {
        uint64_t seconds = 0;
        if (read_total(path, game_id, seconds)) return format(seconds);
    } catch (...) {}
    std::fprintf(stderr, "play time: gametime.save is unreadable or invalid; file preserved.\n");
#else
    (void)path;
    (void)game_id;
#endif
    return "Unavailable";
}

void stop() {
#if defined(_WIN32) && !defined(PSX_UWP)
    finish();
#endif
}

void start(const std::filesystem::path& path, const std::string& game_id) {
#if defined(_WIN32) && !defined(PSX_UWP)
    auto& state = session();
    if (state.active) return;
    try {
        // Serialize sessions using this save, including different builds sharing its path.
        auto normalized = std::filesystem::absolute(path).lexically_normal().wstring();
        uint64_t hash = 14695981039346656037ull;
        for (wchar_t c : normalized) {
            if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
            hash = (hash ^ static_cast<uint64_t>(c)) * 1099511628211ull;
        }
        const auto name = L"Local\\PSXGameTime_" + std::to_wstring(hash);
        state.mutex = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!state.mutex) {
            std::fprintf(stderr, "play time: cannot acquire save mutex; recording disabled.\n");
            return;
        }
        const DWORD result = WaitForSingleObject(state.mutex, 0);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            CloseHandle(state.mutex);
            state.mutex = nullptr;
            std::fprintf(stderr, "play time: another session owns this save; recording disabled.\n");
            return;
        }
        state.path = path;
        state.game_id = game_id;
        // Re-read after locking: another session may have updated the launcher total.
        if (!read_total(path, game_id, state.previous)) {
            std::fprintf(stderr, "play time: invalid save; recording disabled and file preserved.\n");
        } else if (!std::filesystem::exists(path) && !write_total(path, game_id, 0)) {
            std::fprintf(stderr, "play time: cannot create encrypted save; recording disabled.\n");
        } else if (std::atexit(finish) == 0) {
            state.began = Clock::now();
            state.active = true;
            return;
        }
    } catch (...) {
        std::fprintf(stderr, "play time: initialization failed; recording disabled.\n");
    }
    if (state.mutex) {
        ReleaseMutex(state.mutex);
        CloseHandle(state.mutex);
        state.mutex = nullptr;
    }
#else
    (void)path;
    (void)game_id;
#endif
}
}
#endif
