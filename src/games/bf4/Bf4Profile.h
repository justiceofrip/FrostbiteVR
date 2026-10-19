#pragma once
// Identifies the executable before anything attaches. BF4 ships bf4.exe (x64) and
// bf4_x86.exe; only x64 is supported. Known builds are enrolled by SHA-256 (computed by
// the launcher); an unknown x64 build is still allowed to *scan* (read-only) so its
// fingerprint and binding report can be sent as feedback, but no native writes happen
// unless bindings validate.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fvr::bf4 {

struct PeSummary {
    std::uint16_t machine = 0;      // 0x8664 x64, 0x014C x86
    std::uint32_t timeDateStamp = 0;
    std::uint32_t sizeOfImage = 0;
    std::uint16_t sectionCount = 0;
    bool is64 = false;
};

// Parses only the headers; the span may be the first ~4 KiB of the file or the mapped image.
std::optional<PeSummary> ParsePeHeaders(std::span<const std::uint8_t> bytes);

enum class BuildVerdict : std::uint8_t {
    Enrolled,       // hash matches a build with recorded evidence
    UnknownX64,     // read-only scan allowed, writes gated on bindings
    WrongArch,      // bf4_x86.exe or other 32-bit image
    NotPe
};

struct KnownBuild {
    std::string_view sha256;   // lowercase hex
    std::string_view label;    // "Steam 2026-xx (EA app)"
    std::string_view evidence; // checkpoint / note
};

struct BuildIdentity {
    BuildVerdict verdict = BuildVerdict::NotPe;
    std::string label;
    PeSummary pe;
};

BuildIdentity IdentifyBuild(std::span<const std::uint8_t> headerBytes, std::string_view sha256Hex);

std::string_view BuildVerdictName(BuildVerdict v) noexcept;

} // namespace fvr::bf4
