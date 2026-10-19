#pragma once
// Signature scanning for Frostbite executables. Platform-neutral: operates on a byte
// span plus the address it was mapped at, so the same code runs over the live bf4.exe
// image, a file on disk, or the emulated test image.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fvr::frostbite {

struct BytePattern {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> mask; // 1 = byte must match, 0 = wildcard
    std::size_t size() const noexcept { return bytes.size(); }
    bool empty() const noexcept { return bytes.empty(); }
};

// IDA-style text: "48 8B 05 ?? ?? ?? ?? 48 85 C0". '?' and '??' are wildcards.
std::optional<BytePattern> ParsePattern(std::string_view text);

enum class ScanStatus : std::uint8_t {
    Ok,
    NotFound,
    Ambiguous,      // more than one match: never guess
    InvalidPattern,
    Unenrolled,     // no pattern recorded yet (RE pending) -> feature stays disabled
    ValidationFailed
};

std::string_view ScanStatusName(ScanStatus status) noexcept;

struct ScanResult {
    ScanStatus status = ScanStatus::NotFound;
    std::size_t offset = 0;     // first match, relative to region start
    std::size_t matchCount = 0; // capped at the scan limit
};

// Requires exactly one match. Stops after 2 so ambiguity is cheap to detect.
ScanResult FindUnique(std::span<const std::uint8_t> region, const BytePattern& pattern);

// x64 RIP-relative operand: target = address(instr) + instrLength + int32 at dispOffset.
std::optional<std::uint64_t> ResolveRipRelative(std::span<const std::uint8_t> region,
                                                std::uint64_t regionBase,
                                                std::size_t instructionOffset,
                                                std::size_t dispOffset,
                                                std::size_t instructionLength);

} // namespace fvr::frostbite
