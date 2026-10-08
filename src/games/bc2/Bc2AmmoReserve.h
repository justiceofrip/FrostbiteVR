#pragma once
#include "Bc2ReloadHold.h"
namespace fvr::bc2 {
// Read-only native reserve evidence available BEFORE starting a reload. The
// publisher owns a monotonic reserve sequence independent of round ack sequence.
// No reload cycle, synthetic held state, or supply policy dependency belongs here.
struct Bc2AmmoReserveLease {
    ReloadHoldIdentity identity{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    std::int32_t loaded=0,reserve=0,capacity=0;
    bool verified=false;
    // Only a quiet, coherent full observation of all three input-processing
    // states can set this. Reserve availability alone is not firing readiness.
    bool reloadInputReady=false;
    // Exact all-three current/next idle2 and timer0 from the same doubled
    // native read. Independent of magazine fullness or reserve availability.
    bool allThreeIdle=false;
    // Exact all-three applied/restored idle Step inhibit receipts, bounded by
    // their ORIGINAL owner leases. Does not imply hold or transfer authority.
    bool emptyReloadControlled=false;
};
} // namespace fvr::bc2
