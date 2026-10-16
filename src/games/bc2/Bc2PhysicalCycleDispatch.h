#pragma once
#include "Bc2NativeCycleService.h"
#include "Bc2NativeCycleRecovery.h"
#include "fvr/interaction/MechanismAudio.h"
namespace fvr::bc2 {
template<class Lease,class Result> inline std::optional<interaction::MechanismAudioEvent> PhysicalCycleAudio(
    const Lease& lease,const Result& result,const interaction::WeaponCycleProfile& profile,
    const interaction::HandInteractionSample& input)noexcept {
    using namespace interaction;
    if(!result.release&&(!result.target||result.target->travel<=profile.rearTolerance))return {};
    // Only an accepted physical target or completed physical stroke can cue a
    // sound. Native readiness, elapsed animation time and retries cannot.
    return MechanismAudioEvent{{lease.owner.actor,lease.owner.actorGeneration,lease.item.id,lease.item.generation,lease.cycle,lease.shot},
        result.release?MechanismAudioStage::Closure:MechanismAudioStage::RearTravel,input.sequence,input.observedNs,input.deadlineNs,
        true,input.focused,input.tracked[0]&&input.tracked[1]};
}
template<class Api> inline Bc2NativeCycleSubmitResult DispatchCycleControl(const Api& api,const Bc2NativeCycleControl& c)noexcept {
    if(api.controlResult)return api.controlResult(api.context,c);
    return api.control&&api.control(api.context,c)?Bc2NativeCycleSubmitResult::Accepted:Bc2NativeCycleSubmitResult::Rejected;
}
inline Bc2NativeCycleSubmitResult DispatchCycleRecovery(const Bc2PhysicalCycleRecoveryApi& api,void* context,
    const interaction::WeaponCycleIdleDebtRelease& release,const interaction::HandInteractionSample& input)noexcept {
    if(api.submitResult)return api.submitResult(context,release,input);
    return api.submit&&api.submit(context,release,input)?Bc2NativeCycleSubmitResult::Accepted:Bc2NativeCycleSubmitResult::Rejected;
}
// The release/source packet remains immutable across a proven non-dispatch.
// Only processing time advances. New tracking, selection or expired evidence
// retires this attempt; it never authorizes an old request in a new context.
inline bool UndispatchedCycleSourceCurrent(const Bc2NativeCycleControl& original,
    const ReloadStateOwner& owner,interaction::HandInteractionKey item,const interaction::HandInteractionSample& current)noexcept {
    using interaction::weapon_cycle_detail::Window;
    return original.nativeOwner==owner&&original.item==item&&original.input.owner==current.owner&&
        original.input.sequence<=current.sequence&&original.input.observedNs<=current.observedNs&&
        current.focused&&current.tracked[0]&&current.tracked[1]&&
        Window(original.input.observedNs,original.input.deadlineNs,current.nowNs)&&
        Window(current.observedNs,current.deadlineNs,current.nowNs)&&
        (!original.release||Window(original.release->observedNs,original.release->deadlineNs,current.nowNs));
}
}
