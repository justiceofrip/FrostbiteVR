#pragma once
#include "ChestKnifeCustody.h"
#include "Bc2ChestKnifeInput.h"
#include "Bc2ChestKnifeNative.h"
#include <algorithm>
namespace fvr::bc2 {
// Bound once when a measured draw acquires its physical GunHold. A different
// native lifetime cannot inherit that hold, even at the same knife address.
struct ChestKnifePhysicalHold {
    interaction::HandClaimToken token{};
    std::uint64_t bodyOperation=0,nativeLifetime=0;
};
inline bool ChestKnifePhysicalHoldInputCurrent(const interaction::ChestKnifeCustody& policy,
    const interaction::HandInteraction& hands,const interaction::HandInteractionSample& f,
    const interaction::InputFrame& input,const ChestKnifePhysicalHold& custody)noexcept {
    using namespace interaction;
    const auto phase=policy.Phase();const auto held=hands.Current(InteractionHand::Right);
    const auto& token=custody.token;
    return custody.bodyOperation&&custody.nativeLifetime&&!policy.NeedsNativeCleanup()&&
        !policy.NeedsInitialHeldPose()&&(phase==KnifePhase::Held||phase==KnifePhase::AttackStaged||phase==KnifePhase::Attacking)&&
        policy.Hold()&&*policy.Hold()==token&&held&&held->token==token&&token.id&&
        token.kind==HandClaimKind::GunHold&&token.hand==InteractionHand::Right&&!token.prerequisiteClaim&&
        token.owner==f.owner&&token.item.id&&token.item.generation&&token.contact.id&&token.contact.generation&&
        held->inputSequence&&held->inputSequence<=f.sequence&&f.owner.actor&&f.owner.actorGeneration&&
        f.owner.equipGeneration&&f.owner.space&&f.sequence&&f.focused&&f.tracked[0]&&f.tracked[1]&&!f.released[1]&&
        f.observedNs>0&&f.nowNs>=f.observedNs&&f.deadlineNs>f.nowNs&&f.deadlineNs-f.observedNs<=100000000&&
        input.generation==f.sequence&&input.spaceGeneration==f.owner.space&&input.trackingBasisGeneration&&
        ChestKnifeControlInputCurrent(input)&&GameplayOwnsInput(input.menu,f.nowNs)&&input.hands[1].squeeze>=.65f;
}
// Current native selection supplies semantic physical custody only. The arbiter
// still owns overlap/expiry validation; this proof cannot acquire a claim or
// supply a rendered wrist, pose deadline, initial attachment or jab geometry.
inline std::optional<interaction::HandContactProof> ChestKnifePhysicalHoldContact(
    const interaction::ChestKnifeCustody& policy,const interaction::HandInteraction& hands,
    const interaction::HandInteractionSample& f,const interaction::InputFrame& input,
    const ChestKnifePhysicalHold& custody,const ReloadStateOwner& owner,
    const chestKnifeNative::View& native)noexcept {
    if(!ChestKnifePhysicalHoldInputCurrent(policy,hands,f,input,custody))return {};
    const auto& work=native.currentHoldWork;
    if(!native.owned||!native.healthy||!work.known||work.lifetime!=custody.nativeLifetime||
       work.bodyOperation!=custody.bodyOperation||!work.drawSerial||work.returnReady||work.closing||work.commandPending||
       native.snapshot.selectedOwner!=owner||native.snapshot.sequence!=f.sequence||native.snapshot.observedNs!=f.nowNs||
       owner.weapon!=custody.token.item.id||owner.actorGeneration!=f.owner.actorGeneration||owner.space!=f.owner.space||
       f.owner.actor!=((std::uint64_t(owner.weak)<<32)|owner.soldier)||
       native.anchor.knifeWeapon!=owner.weapon||native.anchor.knifeSlot!=7||!private_knife::KnifeSentinel(native.snapshot))return {};
    for(auto slot:native.snapshot.selected.current)if(slot!=native.anchor.knifeSlot)return {};
    return interaction::HandContactProof{custody.token.contact,f.sequence,f.deadlineNs,true};
}
inline std::optional<interaction::HandContactProof> ChestKnifeRenewedPhysicalHoldContact(
    const interaction::ChestKnifeCustody& policy,const interaction::HandInteraction& hands,
    const interaction::HandInteractionSample& f,const interaction::InputFrame& input,
    const ChestKnifePhysicalHold& custody,const ReloadStateOwner& owner,
    const chestKnifeNative::View& native)noexcept {
    const auto held=hands.Current(interaction::InteractionHand::Right);
    if(!held||held->inputSequence!=f.sequence||held->deadlineNs!=f.deadlineNs||
       !hands.HasRecordedEvidence(f,interaction::InteractionHand::Right))return {};
    return ChestKnifePhysicalHoldContact(policy,hands,f,input,custody,owner,native);
}
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
