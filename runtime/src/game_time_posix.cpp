#include "game_time.h"

#if !defined(_WIN32)
#include <sodium.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#else
#include <libsecret/secret.h>
#endif

namespace psx_game_time {
namespace {
using Clock = std::chrono::steady_clock;
using Key = std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>;
constexpr char envelope[] = "PSXTIME2";
constexpr char payload_magic[] = "PSXTIME1";
constexpr size_t nonce_size = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr size_t file_size = 8 + nonce_size + 16 + crypto_aead_xchacha20poly1305_ietf_ABYTES;
static_assert(file_size == 64, "Unexpected play-time envelope size");

struct Session {
    std::filesystem::path path;
    std::string game_id;
    Key key{};
    uint64_t previous = 0;
    Clock::time_point began;
    int lock = -1;
    bool active = false;
};
Session& session() { static Session value; return value; }

std::string safe_id(const std::string& value) {
    std::string result;
    for (unsigned char c : value)
        result += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_') ? char(c) : '_';
    return result.empty() ? "game" : result;
}

// No key is embedded in the program or stored beside gametime.save.
// Missing credentials may be created only when starting a game with no save.
#if defined(__APPLE__)
struct KeychainInteraction {
    Boolean previous = false;
    bool configured = false;
    KeychainInteraction() {
        configured = SecKeychainGetUserInteractionAllowed(&previous) == errSecSuccess &&
                     SecKeychainSetUserInteractionAllowed(false) == errSecSuccess;
    }
    ~KeychainInteraction() {
        if (configured) SecKeychainSetUserInteractionAllowed(previous);
    }
};
bool obtain_key(const std::string& game_id, bool create, Key& key) {
    // Legacy macOS keychains also need this process policy; restore it after the call.
    KeychainInteraction interaction;
    if (!interaction.configured) return false;
    CFStringRef account = CFStringCreateWithCString(nullptr, game_id.c_str(), kCFStringEncodingUTF8);
    if (!account) return false;
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(nullptr, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!query) { CFRelease(account); return false; }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, CFSTR("org.psxrecomp.GameTime.v2"));
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    // A locked/denied keychain disables recording rather than blocking startup on a prompt.
    CFDictionarySetValue(query, kSecUseAuthenticationUI, kSecUseAuthenticationUIFail);
    CFTypeRef data = nullptr;
    OSStatus status = SecItemCopyMatching(query, &data);
    bool ok = false;
    if (status == errSecSuccess) {
        ok = data && CFGetTypeID(data) == CFDataGetTypeID() &&
             CFDataGetLength(static_cast<CFDataRef>(data)) == static_cast<CFIndex>(key.size());
        if (ok) std::memcpy(key.data(), CFDataGetBytePtr(static_cast<CFDataRef>(data)), key.size());
    } else if (status == errSecItemNotFound && create) {
        crypto_aead_xchacha20poly1305_ietf_keygen(key.data());
        CFDataRef secret = CFDataCreate(nullptr, key.data(), key.size());
        if (secret) {
            CFDictionaryRemoveValue(query, kSecReturnData);
            CFDictionarySetValue(query, kSecValueData, secret);
            CFDictionarySetValue(query, kSecAttrLabel, CFSTR("PSXRecomp encrypted play time"));
            CFDictionarySetValue(query, kSecAttrSynchronizable, kCFBooleanFalse);
            status = SecItemAdd(query, nullptr);
            ok = status == errSecSuccess;
            CFRelease(secret);
        }
    }
    if (!ok) std::fprintf(stderr, "play time: Keychain credential unavailable (status %d).\n", int(status));
    if (data) CFRelease(data);
    CFRelease(query);
    CFRelease(account);
    return ok;
}
#else
const SecretSchema schema = {
    "org.psxrecomp.GameTime", SECRET_SCHEMA_NONE,
    {{"game", SECRET_SCHEMA_ATTRIBUTE_STRING}, {"version", SECRET_SCHEMA_ATTRIBUTE_STRING},
     {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}
};

// Run async keyring calls on a private context with a cancellation deadline.
// This is used only before the launcher/emulation, never in the frame loop.
struct SecretRequest {
    GMainLoop* loop = nullptr;
    gchar* password = nullptr;
    GError* error = nullptr;
    bool stored = false;
};
void lookup_done(GObject*, GAsyncResult* result, gpointer context) {
    auto& request = *static_cast<SecretRequest*>(context);
    request.password = secret_password_lookup_finish(result, &request.error);
    g_main_loop_quit(request.loop);
}
void store_done(GObject*, GAsyncResult* result, gpointer context) {
    auto& request = *static_cast<SecretRequest*>(context);
    request.stored = secret_password_store_finish(result, &request.error);
    g_main_loop_quit(request.loop);
}
bool secret_request(const std::string& game_id, const char* store, SecretRequest& request) {
    GMainContext* context = g_main_context_new();
    g_main_context_push_thread_default(context);
    request.loop = g_main_loop_new(context, FALSE);
    GCancellable* cancel = g_cancellable_new();
    GSource* deadline = g_timeout_source_new(5000);
    g_source_set_callback(deadline, [](gpointer value) -> gboolean {
        g_cancellable_cancel(static_cast<GCancellable*>(value));
        return G_SOURCE_REMOVE;
    }, cancel, nullptr);
    g_source_attach(deadline, context);
    if (store)
        secret_password_store(&schema, SECRET_COLLECTION_DEFAULT,
            "PSXRecomp encrypted play time", store, cancel, store_done, &request,
            "game", game_id.c_str(), "version", "2", nullptr);
    else
        secret_password_lookup(&schema, cancel, lookup_done, &request,
            "game", game_id.c_str(), "version", "2", nullptr);
    g_main_loop_run(request.loop);
    g_source_destroy(deadline);
    g_source_unref(deadline);
    g_object_unref(cancel);
    g_main_loop_unref(request.loop);
    request.loop = nullptr;
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    if (request.error) {
        std::fprintf(stderr, "play time: Secret Service unavailable: %s\n", request.error->message);
        g_error_free(request.error);
        request.error = nullptr;
        return false;
    }
    return true;
}
bool obtain_key(const std::string& game_id, bool create, Key& key) {
    SecretRequest request;
    if (!secret_request(game_id, nullptr, request)) {
        if (request.password) secret_password_free(request.password);
        return false;
    }
    if (request.password) {
        size_t decoded = 0;
        const bool ok = std::strlen(request.password) == key.size() * 2 &&
            sodium_hex2bin(key.data(), key.size(), request.password, key.size() * 2,
                           nullptr, &decoded, nullptr) == 0 && decoded == key.size();
        secret_password_free(request.password);
        request.password = nullptr;
        return ok;
    }
    if (!create) return false;
    crypto_aead_xchacha20poly1305_ietf_keygen(key.data());
    std::array<char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES * 2 + 1> hex{};
    sodium_bin2hex(hex.data(), hex.size(), key.data(), key.size());
    const bool ok = secret_request(game_id, hex.data(), request) && request.stored;
    sodium_memzero(hex.data(), hex.size());
    return ok;
}
#endif

bool absent(const std::filesystem::path& path, bool& missing) {
    struct stat info{};
    if (lstat(path.c_str(), &info) == 0) {
        missing = false;
        return S_ISREG(info.st_mode) && info.st_uid == getuid();
    }
    missing = errno == ENOENT;
    return missing;
}

std::string associated_data(const std::string& game_id) {
    return std::string(envelope, 8) + "/psxrecomp/gametime/v2/" + game_id;
}
bool read_total(const std::filesystem::path& path, const std::string& game_id,
                const Key& key, uint64_t& seconds) {
    seconds = 0;
    int file = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (file < 0) return false;
    struct stat info{};
    bool ok = fstat(file, &info) == 0 && S_ISREG(info.st_mode) &&
              info.st_uid == getuid() && info.st_size == static_cast<off_t>(file_size);
    std::array<unsigned char, file_size> bytes{};
    size_t offset = 0;
    while (ok && offset < bytes.size()) {
        const ssize_t count = read(file, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = false; break; }
        offset += size_t(count);
    }
    close(file);
    if (!ok || std::memcmp(bytes.data(), envelope, 8) != 0) return false;
    const std::string aad = associated_data(game_id);
    std::array<unsigned char, 16> payload{};
    unsigned long long length = 0;
    ok = crypto_aead_xchacha20poly1305_ietf_decrypt(payload.data(), &length, nullptr,
        bytes.data() + 8 + nonce_size, bytes.size() - 8 - nonce_size,
        reinterpret_cast<const unsigned char*>(aad.data()), aad.size(),
        bytes.data() + 8, key.data()) == 0 && length == payload.size() &&
        std::memcmp(payload.data(), payload_magic, 8) == 0;
    if (ok)
        for (unsigned i = 0; i < 8; ++i) seconds |= uint64_t(payload[8 + i]) << (8 * i);
    sodium_memzero(payload.data(), payload.size());
    return ok;
}

bool write_total(const Session& state, uint64_t seconds) {
    std::array<unsigned char, file_size> bytes{};
    std::array<unsigned char, 16> payload{};
    std::memcpy(bytes.data(), envelope, 8);
    std::memcpy(payload.data(), payload_magic, 8);
    for (unsigned i = 0; i < 8; ++i) payload[8 + i] = static_cast<unsigned char>(seconds >> (8 * i));
    randombytes_buf(bytes.data() + 8, nonce_size);
    const std::string aad = associated_data(state.game_id);
    unsigned long long encrypted_size = 0;
    const bool encrypted = crypto_aead_xchacha20poly1305_ietf_encrypt(
        bytes.data() + 8 + nonce_size, &encrypted_size, payload.data(), payload.size(),
        reinterpret_cast<const unsigned char*>(aad.data()), aad.size(), nullptr,
        bytes.data() + 8, state.key.data()) == 0;
    sodium_memzero(payload.data(), payload.size());
    if (!encrypted || encrypted_size != bytes.size() - 8 - nonce_size) return false;
    std::string temporary = state.path.string() + ".tmp.XXXXXX";
    int file = mkstemp(temporary.data()); // unique, owner-only temporary file in the same directory
    if (file < 0) return false;
    bool ok = fchmod(file, 0600) == 0;
    size_t offset = 0;
    while (ok && offset < bytes.size()) {
        const ssize_t count = write(file, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ok = false; break; }
        offset += size_t(count);
    }
    if (ok) ok = fsync(file) == 0;
    if (close(file) != 0) ok = false;
    if (ok) ok = rename(temporary.c_str(), state.path.c_str()) == 0;
    if (!ok) unlink(temporary.c_str());
    else {
        int directory = open(state.path.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
        if (directory >= 0) { fsync(directory); close(directory); }
    }
    return ok;
}

void release(Session& state) {
    sodium_memzero(state.key.data(), state.key.size());
    if (state.lock >= 0) { flock(state.lock, LOCK_UN); close(state.lock); state.lock = -1; }
}
void finish() noexcept {
    auto& state = session();
    if (!state.active) return;
    state.active = false;
    try {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - state.began).count();
        const uint64_t delta = elapsed > 0 ? uint64_t(elapsed) : 0;
        const uint64_t total = state.previous +
            (delta > UINT64_MAX - state.previous ? UINT64_MAX - state.previous : delta);
        if (!write_total(state, total))
            std::fprintf(stderr, "play time: encrypted save failed; previous total preserved.\n");
    } catch (...) { std::fprintf(stderr, "play time: save failed; previous total preserved.\n"); }
    release(state);
}
}

std::filesystem::path save_path(const std::filesystem::path& executable_dir,
                                const std::string& game_id) {
    (void)executable_dir;
    const char* home = std::getenv("HOME");
    std::filesystem::path base;
#if defined(__APPLE__)
    if (home && std::filesystem::path(home).is_absolute())
        base = std::filesystem::path(home) / "Library/Application Support";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && std::filesystem::path(xdg).is_absolute()) base = xdg;
    else if (home && std::filesystem::path(home).is_absolute())
        base = std::filesystem::path(home) / ".local/share";
#endif
    return base.empty() ? std::filesystem::path{} :
        base / "PSXRecomp" / safe_id(game_id) / "gametime.save";
}

std::string display(const std::filesystem::path& path, const std::string& game_id) {
    if (path.empty() || sodium_init() < 0) return "Unavailable";
    Key key{};
    std::string result = "Unavailable";
    try {
        bool missing = false;
        if (absent(path, missing)) {
            if (missing) return "000:00";
            uint64_t seconds = 0;
            if (obtain_key(game_id, false, key) && read_total(path, game_id, key, seconds)) {
                char text[64];
                std::snprintf(text, sizeof(text), "%03llu:%02llu",
                    static_cast<unsigned long long>(seconds / 3600),
                    static_cast<unsigned long long>((seconds / 60) % 60));
                result = text;
            }
        }
    } catch (...) {}
    sodium_memzero(key.data(), key.size());
    if (result == "Unavailable")
        std::fprintf(stderr, "play time: save or key unavailable; existing data preserved.\n");
    return result;
}

void start(const std::filesystem::path& path, const std::string& game_id) {
    auto& state = session();
    if (state.active || path.empty() || sodium_init() < 0) return;
    try {
        std::filesystem::create_directories(path.parent_path());
        struct stat directory{};
        if (lstat(path.parent_path().c_str(), &directory) != 0 ||
            !S_ISDIR(directory.st_mode) || directory.st_uid != getuid() ||
            chmod(path.parent_path().c_str(), 0700) != 0)
            throw std::runtime_error("save directory is not private and owned by this user");
        auto lock_path = path;
        lock_path += ".lock";
        state.lock = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat lock_info{};
        if (state.lock < 0 || fstat(state.lock, &lock_info) != 0 ||
            !S_ISREG(lock_info.st_mode) || lock_info.st_uid != getuid() ||
            flock(state.lock, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("another session owns the save or locking is unavailable");
        state.path = path;
        state.game_id = game_id;
        bool missing = false;
        if (!absent(path, missing) || !obtain_key(game_id, missing, state.key))
            throw std::runtime_error("save or credential unavailable; refusing to replace it");
        state.previous = 0;
        if (missing ? !write_total(state, 0) : !read_total(path, game_id, state.key, state.previous))
            throw std::runtime_error("encrypted save could not be created or authenticated");
        if (std::atexit(finish) != 0) throw std::runtime_error("exit handler registration failed");
        state.began = Clock::now();
        state.active = true;
        return;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "play time: recording disabled: %s\n", error.what());
    } catch (...) { std::fprintf(stderr, "play time: recording initialization failed.\n"); }
    release(state);
}
}
#endif
