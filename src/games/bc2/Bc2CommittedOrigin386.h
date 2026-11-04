#pragma once
#include <array>
#include <cstdint>

namespace fvr::bc2::origin386 {
using Bits = std::array<std::uint32_t, 3>;
enum class WriteResult { Committed, Restored, Unresolved };

// IEEE binary32 bits, already converted to absolute native coordinates by an
// independently authorized host. The host supplies its reviewed native bound.
// No coordinate conversion, initialPosition addition, or FP arithmetic here.
inline bool BoundedAbsolute(const Bits& xyz, std::uint32_t positiveBound) noexcept {
    if (!positiveBound || positiveBound >= 0x7f800000u) return false;
    for (const auto v : xyz)
        if ((v & 0x7fffffffu) >= 0x7f800000u || (v & 0x7fffffffu) > positiveBound) return false;
    return true;
}

// Synchronous PRE-NATIVE transaction only. The writer may report failure after
// changing a word. No callbacks into gameplay, concurrency, or native execution
// may intervene. The production writer performs three aligned DWORD stores;
// twelve bytes are NOT atomic. A failed commit restores exact prior bits, then
// reads them back. An unproved restoration is never an untouched fallback.
template<class WriteWord, class ReadWord>
WriteResult Commit(const Bits& before, const Bits& desired, WriteWord write, ReadWord read) noexcept {
    for (unsigned i = 0; i != 3; ++i) {
        if (write(i, desired[i])) continue;
        for (unsigned j = 0; j != 3; ++j) (void)write(j, before[j]);
        bool exact = true;
        for (unsigned j = 0; j != 3; ++j) {
            std::uint32_t observed = 0;
            if (!read(j, observed) || observed != before[j]) exact = false;
        }
        return exact ? WriteResult::Restored : WriteResult::Unresolved;
    }
    return WriteResult::Committed;
}
} // namespace fvr::bc2::origin386
