#pragma once
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2BoltRuntimeGeometry.h"

namespace fvr::bc2 {
// Request-scoped copies made on the gameplay thread immediately after Tick.
// None of these values grants a claim, renews a clock or dispatches native work.
struct MagazineLiveObservation {
    ReloadStateOwner owner{};interaction::HandInteractionSample input{};
    interaction::HandInteractionKey weapon{};
    const MagazineEquipmentProfile* profile=nullptr;
    MagazineFamilyEvidence family{};MagazineRawContact raw{};
    std::optional<interaction::HandInteractionSample> originalHandEvidence;
    MagazineResourceProbeState state{};
    std::optional<interaction::HandClaim> left,right;
    interaction::AmmoSupplyContact pouch{};
    std::int64_t capturedNs=0;
    bool resourceBackend=false,cancel=false;
};
struct BoltLiveObservation {
    ReloadStateOwner owner{};interaction::HandInteractionSample input{};
    interaction::HandInteractionKey weapon{};
    Bc2BoltControllerContact contact{};
    Bc2PhysicalBoltResult result{};
    // Successful native acknowledgement retained separately from Tick events.
    std::optional<Bc2BoltCompletedEvent> completion;
    std::optional<interaction::HandClaim> left,right;
    std::optional<interaction::HandClaimToken> controllerCustody;
    // Established normal-player wrist-in-weapon geometry, not a contact lease.
    // The player adapter retains no original capture clock for these matrices.
    std::optional<std::array<math::Matrix4,2>> originalGripReferences;
    std::optional<Bc2NativeCycleView> native;
    std::int64_t capturedNs=0,nativeReadNs=0;
    bool cancel=false;
};
namespace live_magazine_bolt {
inline bool Window(const interaction::HandInteractionSample& input,std::int64_t now)noexcept {
    return input.sequence&&input.focused&&input.tracked[0]&&input.tracked[1]&&
        input.observedNs>0&&input.observedNs<=now&&now<input.deadlineNs&&
        input.deadlineNs-input.observedNs<=150000000;
}
inline bool SameInput(const interaction::HandInteractionSample& a,const interaction::HandInteractionSample& b)noexcept {
    // nowNs is processing time, not an input lease. Do not rewrite either copy.
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
template<class Observation> inline bool Joined(const Observation* o,const ReloadStateOwner& owner,
    const interaction::HandInteractionSample& input,std::int64_t now)noexcept {
    return o&&o->owner==owner&&SameInput(o->input,input)&&Window(o->input,now)&&
        o->capturedNs>=o->input.observedNs&&o->capturedNs<=now&&o->capturedNs<o->input.deadlineNs;
}
inline bool ClaimCurrent(const std::optional<interaction::HandClaim>& claim,
    const interaction::HandInteractionSample& input,std::int64_t now)noexcept {
    if(!claim||!Window(input,now))return false;
    const auto& c=*claim;const auto hand=unsigned(c.token.hand);
    return hand<2&&c.token.id&&c.token.owner==input.owner&&c.inputSequence==input.sequence&&
        c.deadlineNs>now&&c.deadlineNs<=input.deadlineNs&&!input.released[hand];
}
inline bool MagazineRawCurrent(const MagazineLiveObservation& o,const RigIdentity& rig,std::uint64_t fingerprint,
    std::int64_t now)noexcept {
    const auto& r=o.raw;
    return Window(o.input,now)&&r.valid&&r.owner==o.owner&&r.rig==rig&&r.rigFingerprint==fingerprint&&
        r.inputEvidence.owner==o.input.owner&&r.inputEvidence.sequence<=o.input.sequence&&
        r.inputEvidence.observedNs<=o.input.observedNs&&Window(r.inputEvidence,now)&&
        o.originalHandEvidence&&SameInput(r.inputEvidence,*o.originalHandEvidence)&&
        interaction::reload_insertion_detail::Rigid(r.rawLeftWristWorldMeters)&&
        interaction::reload_insertion_detail::Rigid(r.weaponWorldMeters);
}
inline bool MagazineResourceCurrent(const MagazineLiveObservation& o,std::int64_t now)noexcept {
    return Window(o.input,now)&&o.resourceBackend&&o.family.binding.profile==o.profile&&
        o.state.resource&&o.state.resource->binding.owner==o.owner&&
        BindMagazineResourceOwners(*o.state.resource,o.family,o.input.owner,o.weapon,now).has_value();
}
inline bool MagazineReplacementCurrent(const MagazineLiveObservation& o,std::int64_t now)noexcept {
    if(!o.state.carriedReplacement||!ClaimCurrent(o.left,o.input,now)||!ClaimCurrent(o.right,o.input,now))return false;
    const auto& c=*o.state.carriedReplacement;
    return c.units&&c.identity.owner==o.input.owner&&c.identity.weapon==o.weapon&&
        c.identity.trackingEpoch==o.input.owner.space&&c.family==interaction::ReloadInsertionFamily::Magazine&&
        c.claim.token==o.left->token&&c.claim.deadlineNs==o.left->deadlineNs&&c.claim.inputSequence==o.left->inputSequence&&
        c.claim.token.kind==interaction::HandClaimKind::AmmoObject&&c.claim.token.item==c.item&&
        o.right->token.kind==interaction::HandClaimKind::GunHold&&o.right->token.item==o.weapon&&
        !o.right->token.prerequisiteClaim&&c.claim.token.prerequisiteClaim==o.right->token.id;
}
inline bool MagazineReceiptJoined(const MagazineLiveObservation& o,std::int64_t now)noexcept {
    if(!MagazineResourceCurrent(o,now))return false;
    const auto& v=*o.state.resource;if(!v.receipt)return false;
    const auto& r=*v.receipt;const auto& c=r.command;
    return v.request&&v.requestState==AmmoResourceRequestState::Completed&&v.terminal&&
        v.terminal->state==AmmoResourceRequestState::Completed&&!v.terminal->request.discard&&
        v.terminal->request.intent.id==v.request&&v.terminal->request.intent.context==c.context&&
        v.terminal->request.intent.operation==c.operation&&v.terminal->request.intent.original==c.original&&
        v.terminal->request.binding.owner==o.owner&&v.terminal->receipt&&*v.terminal->receipt==r&&
        (!c.mutationRequired||v.terminal->nativeDispatched)&&
        v.terminal->resolvedNs>=r.completedNs&&v.terminal->resolvedNs<=now&&
        v.phase==interaction::AmmunitionLedgerPhase::Ready&&c.context==v.snapshot.context&&c.id&&c.sourceSequence&&
        c.requestedNs>0&&r.authorityVerified&&r.copiesVerified&&r.authorityInvocation&&
        r.beganNs>=c.requestedNs&&r.completedNs>=r.beganNs&&r.completedNs<c.deadlineNs&&
        r.completedNs<=now&&v.snapshot.observedNs>=r.completedNs&&r.before==c.before&&r.after==c.after&&
        r.after==v.snapshot.counts;
}
inline bool BoltRawCurrent(const BoltLiveObservation& o,const RigIdentity& rig,std::uint64_t fingerprint,
    std::int64_t now)noexcept {
    const auto& r=o.contact.raw;
    return Window(o.input,now)&&r.valid&&r.nativeOwner==o.owner&&r.rig==rig&&r.rigFingerprint==fingerprint&&
        r.input.owner==o.input.owner&&r.input.sequence<=o.input.sequence&&r.input.observedNs<=o.input.observedNs&&
        Window(r.input,now)&&interaction::reload_insertion_detail::Rigid(r.mechanismWristInWeapon);
}
inline bool BoltCustodyCurrent(const BoltLiveObservation& o,std::int64_t now)noexcept {
    const auto& t=o.result.tracking;
    if(t.nativeOwner!=o.owner||!SameInput(t.input,o.input)||!t.gun||!BoltCustodyFresh(t,now))return false;
    const auto& actual=t.gun->token.hand==interaction::InteractionHand::Left?o.left:o.right;
    return ClaimCurrent(actual,o.input,now)&&actual->token==t.gun->token&&actual->inputSequence==t.gun->inputSequence&&
        actual->deadlineNs==t.gun->deadlineNs;
}
inline bool BoltCurrentContactJoined(const BoltLiveObservation& o,std::int64_t now)noexcept {
    const auto& t=o.result.tracking;
    return BoltCustodyCurrent(o,now)&&t.currentContactGun&&*t.currentContactGun==t.gun->token&&
        t.currentContactInput==o.input.sequence&&t.currentContactObserved==o.input.observedNs&&
        t.currentContactDeadline>now&&t.currentContactDeadline<=o.input.deadlineNs&&
        t.currentContactDeadline<=t.gun->deadlineNs;
}
} // namespace live_magazine_bolt
} // namespace fvr::bc2
