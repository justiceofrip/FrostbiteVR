#pragma once
#include "ChestKnifeCustody.h"
#include <algorithm>
namespace fvr::bc2 {
// A stock button attack needs the existing knife custody, not tracked blade
// geometry. Bridge one missing render publication with only that exact live
// GunHold's remaining lease. This read cannot acquire a hold, extend its
// deadline, create a render receipt or authorize a tracked physical stroke.
inline std::optional<interaction::HandContactProof> ChestKnifeRetainedStockContact(
    const interaction::ChestKnifeCustody& policy,const interaction::HandInteraction& hands,
    const interaction::KnifeSample& sample,bool stockMode,bool nativeHoldCurrent)noexcept {
    using namespace interaction;
    const auto phase=policy.Phase();const auto& f=sample.source;
    if(!stockMode||!nativeHoldCurrent||policy.NeedsNativeCleanup()||
       (phase!=KnifePhase::Held&&phase!=KnifePhase::AttackStaged&&phase!=KnifePhase::Attacking)||
       !f.owner.actor||!f.owner.actorGeneration||!f.owner.equipGeneration||!f.owner.space||!f.sequence||
       !f.focused||!f.tracked[0]||!f.tracked[1]||f.released[1]||f.observedNs<=0||f.nowNs<f.observedNs||
       f.deadlineNs<=f.nowNs||f.deadlineNs-f.observedNs>100000000||
       !GameplayOwnsInput(sample.menu,f.nowNs)||!sample.onFoot||!sample.fireSuppressed||
       !(sample.controlsNeutral||sample.holdInputCurrent)||!sample.knifeSelected||!sample.gripHeld[1]||
       !sample.knife.id||!sample.knife.generation||!policy.Hold()||
       !hands.HasRecordedEvidence(f,InteractionHand::Right))return {};
    const auto held=hands.Current(InteractionHand::Right);const auto& token=*policy.Hold();
    if(!held||held->token!=token||!token.id||token.kind!=HandClaimKind::GunHold||
       token.hand!=InteractionHand::Right||token.owner!=f.owner||token.item!=sample.knife||
       token.prerequisiteClaim||!token.contact.id||!token.contact.generation||
       !held->inputSequence||held->inputSequence>f.sequence||held->deadlineNs<=f.nowNs)return {};
    return HandContactProof{token.contact,f.sequence,(std::min)(held->deadlineNs,f.deadlineNs),true};
}
}
