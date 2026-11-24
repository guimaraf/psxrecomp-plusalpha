#pragma once
#include <cstdint>
#include <string>

namespace psx {
// Offline-only policy. The online nameplate formatter keeps its own limit.
inline std::string offline_player_name(const std::string& raw) {
    std::string clean;
    unsigned characters = 0;
    for (size_t at = 0; at < raw.size();) {
        const size_t start = at;
        const auto first = static_cast<unsigned char>(raw[at++]);
        unsigned extra = first < 0x80 ? 0 : first >= 0xC2 && first <= 0xDF ? 1 :
                         first >= 0xE0 && first <= 0xEF ? 2 :
                         first >= 0xF0 && first <= 0xF4 ? 3 : 4;
        if (extra == 4 || at + extra > raw.size()) continue;
        uint32_t cp = extra ? first & ((1u << (6 - extra)) - 1) : first;
        bool valid = true;
        for (unsigned i = 0; i < extra; ++i) {
            const auto next = static_cast<unsigned char>(raw[at]);
            if ((next & 0xC0) != 0x80) { valid = false; break; }
            ++at;
            cp = (cp << 6) | (next & 0x3F);
        }
        if (!valid || (extra == 1 && cp < 0x80) ||
            (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
            cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) continue;
        const bool accent = cp >= 0x300 && cp <= 0x36F;
        if (accent && !characters) continue;
        if ((!accent && characters == 8) || clean.size() + at - start > 60) break;
        if (!accent) ++characters;
        clean.append(raw, start, at - start);
    }
    return clean;
}
}
