#include "PatternScan.h"

#include <cstring>

namespace fvr::frostbite {

namespace {
int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

std::optional<BytePattern> ParsePattern(std::string_view text) {
    BytePattern p;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == ' ' || c == '\t') { ++i; continue; }
        if (c == '?') {
            p.bytes.push_back(0);
            p.mask.push_back(0);
            ++i;
            if (i < text.size() && text[i] == '?') ++i;
            continue;
        }
        if (i + 1 >= text.size()) return std::nullopt;
        const int hi = HexValue(text[i]);
        const int lo = HexValue(text[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        p.bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
        p.mask.push_back(1);
        i += 2;
    }
    if (p.empty()) return std::nullopt;
    // A pattern that is all wildcards would match everywhere.
    bool anyFixed = false;
    for (auto m : p.mask) anyFixed = anyFixed || m;
    if (!anyFixed) return std::nullopt;
    return p;
}

std::string_view ScanStatusName(ScanStatus status) noexcept {
    switch (status) {
    case ScanStatus::Ok: return "ok";
    case ScanStatus::NotFound: return "not-found";
    case ScanStatus::Ambiguous: return "ambiguous";
    case ScanStatus::InvalidPattern: return "invalid-pattern";
    case ScanStatus::Unenrolled: return "unenrolled";
    case ScanStatus::ValidationFailed: return "validation-failed";
    }
    return "unknown";
}

ScanResult FindUnique(std::span<const std::uint8_t> region, const BytePattern& pattern) {
    ScanResult result;
    if (pattern.empty()) {
        result.status = ScanStatus::InvalidPattern;
        return result;
    }
    const std::size_t n = pattern.size();
    if (region.size() < n) return result;

    // Anchor on the first fixed byte and let memchr do the heavy lifting.
    std::size_t anchor = 0;
    while (!pattern.mask[anchor]) ++anchor;
    const std::uint8_t anchorByte = pattern.bytes[anchor];

    const std::uint8_t* base = region.data();
    const std::size_t last = region.size() - n;
    std::size_t pos = 0;
    while (pos <= last) {
        const void* hit = std::memchr(base + pos + anchor, anchorByte, last - pos + 1);
        if (!hit) break;
        const std::size_t start = static_cast<std::size_t>(static_cast<const std::uint8_t*>(hit) - base) - anchor;
        bool match = true;
        for (std::size_t k = 0; k < n; ++k) {
            if (pattern.mask[k] && base[start + k] != pattern.bytes[k]) { match = false; break; }
        }
        if (match) {
            if (result.matchCount == 0) result.offset = start;
            if (++result.matchCount >= 2) break;
        }
        pos = start + 1;
    }
    result.status = result.matchCount == 1 ? ScanStatus::Ok
                  : result.matchCount == 0 ? ScanStatus::NotFound
                                           : ScanStatus::Ambiguous;
    return result;
}

std::optional<std::uint64_t> ResolveRipRelative(std::span<const std::uint8_t> region,
                                                std::uint64_t regionBase,
                                                std::size_t instructionOffset,
                                                std::size_t dispOffset,
                                                std::size_t instructionLength) {
    if (dispOffset + 4 > instructionLength) return std::nullopt;
    if (instructionOffset + instructionLength > region.size()) return std::nullopt;
    std::int32_t disp = 0;
    std::memcpy(&disp, region.data() + instructionOffset + dispOffset, sizeof(disp));
    const std::uint64_t next = regionBase + instructionOffset + instructionLength;
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(next) + disp);
}

} // namespace fvr::frostbite
