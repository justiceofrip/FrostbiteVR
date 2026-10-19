#include "Bf4Profile.h"

#include <array>
#include <cstring>

namespace fvr::bf4 {

namespace {
template <typename T>
bool Read(std::span<const std::uint8_t> b, std::size_t off, T& out) {
    if (off + sizeof(T) > b.size()) return false;
    std::memcpy(&out, b.data() + off, sizeof(T));
    return true;
}

// Enroll builds here only with evidence (see docs/BF4-RE-PLAYBOOK.md "Enrolling a build").
const std::array<KnownBuild, 0> kKnownBuilds{};
} // namespace

std::optional<PeSummary> ParsePeHeaders(std::span<const std::uint8_t> b) {
    std::uint16_t mz = 0;
    if (!Read(b, 0, mz) || mz != 0x5A4D) return std::nullopt;
    std::uint32_t peOff = 0;
    if (!Read(b, 0x3C, peOff)) return std::nullopt;
    std::uint32_t sig = 0;
    if (!Read(b, peOff, sig) || sig != 0x00004550) return std::nullopt;
    PeSummary s;
    const std::size_t fh = peOff + 4;
    if (!Read(b, fh + 0, s.machine) || !Read(b, fh + 2, s.sectionCount) || !Read(b, fh + 4, s.timeDateStamp))
        return std::nullopt;
    const std::size_t oh = fh + 20;
    std::uint16_t magic = 0;
    if (!Read(b, oh, magic)) return std::nullopt;
    s.is64 = magic == 0x20B;
    if (!Read(b, oh + 56, s.sizeOfImage)) return std::nullopt; // same offset for PE32/PE32+
    return s;
}

BuildIdentity IdentifyBuild(std::span<const std::uint8_t> headerBytes, std::string_view sha256Hex) {
    BuildIdentity id;
    const auto pe = ParsePeHeaders(headerBytes);
    if (!pe) return id;
    id.pe = *pe;
    if (pe->machine != 0x8664 || !pe->is64) {
        id.verdict = BuildVerdict::WrongArch;
        id.label = "32-bit image (use bf4.exe, not bf4_x86.exe)";
        return id;
    }
    for (const auto& k : kKnownBuilds) {
        if (k.sha256 == sha256Hex) {
            id.verdict = BuildVerdict::Enrolled;
            id.label = std::string(k.label);
            return id;
        }
    }
    id.verdict = BuildVerdict::UnknownX64;
    id.label = "unenrolled x64 build";
    return id;
}

std::string_view BuildVerdictName(BuildVerdict v) noexcept {
    switch (v) {
    case BuildVerdict::Enrolled: return "enrolled";
    case BuildVerdict::UnknownX64: return "unknown-x64";
    case BuildVerdict::WrongArch: return "wrong-arch";
    case BuildVerdict::NotPe: return "not-pe";
    }
    return "unknown";
}

} // namespace fvr::bf4
