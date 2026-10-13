#pragma once
#include "Bc2ReloadFlowRuntime.h"
namespace fvr::bc2 {
// Display availability only. A busy/cohort read may retain the last complete
// reserve through its ORIGINAL lease. Nothing here supplies a reload consumer,
// counts HUD, reservation, native readiness or ammunition acknowledgement.
class StowedAmmoReserveCache {
public:
    void Clear()noexcept {last_.reset();}
    std::optional<Bc2AmmoReserveLease> Observe(const ReloadReserveObservation& observation,
        const ReloadStateOwner& owner,std::int64_t now)noexcept {
        if(observation.result==ReloadObservationResult::Available)last_=observation.lease;
        else if(observation.result!=ReloadObservationResult::Deferred&&observation.result!=ReloadObservationResult::CohortGap)Clear();
        if(last_&&(!last_->verified||!last_->sequence||last_->identity.owner!=owner||
            last_->observedNs<=0||last_->observedNs>now||last_->deadlineNs<=now||
            last_->deadlineNs-last_->observedNs>200000000))Clear();
        return last_;
    }
private:
    std::optional<Bc2AmmoReserveLease> last_;
};
}
