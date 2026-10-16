#pragma once
#include "Bc2ReloadState.h"
namespace fvr::bc2 {
// Input/menu presentation or transient read loss retires action authority, not a previously
// admitted bounded native stop. Actual actor/equipment loss retires both.
enum class ReloadOwnerLoss {PresentationUnavailable,LifetimeEnded};
inline bool SameReloadCleanupLifetime(const ReloadStateOwner& a,const ReloadStateOwner& b)noexcept {
    return a.player==b.player&&a.soldier==b.soldier&&a.weak==b.weak&&a.weapon==b.weapon&&
        a.actorGeneration==b.actorGeneration&&a.equipGeneration==b.equipGeneration;
}
inline bool ReloadCleanupObservationCurrent(std::uint64_t capturedEpoch,std::uint64_t currentEpoch,
    std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    return capturedEpoch&&capturedEpoch==currentEpoch&&observed>0&&observed<=now&&now<deadline&&
        deadline-observed<=200000000;
}
}
