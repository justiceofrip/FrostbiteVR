#pragma once
#include "KnifeNativeReader.h"
#include "Bc2KnifeDrawReturnGate.h"
#include "Bc2BodyHolster.h"
#include "Bc2OriginalFiringObserver.h"
#include <ostream>
#include <cmath>
namespace fvr::bc2::chestKnifeNative {
// The default is draw/hold/return. motionEnabled admits deliberate tracked
// jabs; stockTriggerEnabled also admits trigger edges. Normal mode4 enables
// both causes through the original Fire8 consumer. attackBridgeEnabled remains
// a separate default-off qualification request, forbidden in stock mode.
// Requires the existing firing dispatcher installed.
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t imageBase,
    const ReloadStateMemory&,bool motionEnabled=false,bool attackBridgeEnabled=false,bool stockTriggerEnabled=false)noexcept;
void Start()noexcept;
bool TrackedAttackRequested()noexcept;
// First failing branch/field of the unchanged Ready predicate. Existing values
// only; branch 0/1/2 means client0/client1/server, 3 means not inspected/ready.
enum class ReadyReason:unsigned {NotInspected,Ready,Current,Next,TimerFinite,TimerLow,TimerHigh,FlagsA8};
struct ReadyObservation {
    ReadyReason reason=ReadyReason::NotInspected;unsigned branch=3,current=0,next=0,flagsA8=0;float phaseTimer=0;
};
inline ReadyObservation InspectReady(const private_knife::Item& item)noexcept {
    unsigned branch=0;
    for(const auto* s:{&item.client.branches[0],&item.client.branches[1],&item.server}){
        const auto reason=s->currentState!=2?ReadyReason::Current:s->nextState!=2?ReadyReason::Next:
            !std::isfinite(s->phaseTimer)?ReadyReason::TimerFinite:s->phaseTimer<0?ReadyReason::TimerLow:
            s->phaseTimer>1000000?ReadyReason::TimerHigh:(s->flagsA8&8)?ReadyReason::FlagsA8:ReadyReason::Ready;
        if(reason!=ReadyReason::Ready)return {reason,branch,s->currentState,s->nextState,s->flagsA8,s->phaseTimer};
        ++branch;
    }
    return {ReadyReason::Ready};
}
inline void WriteReadyObservation(std::ostream& out,const ReadyObservation& r){
    constexpr const char* names[]{"not_inspected","ready","current","next","timer_finite","timer_low","timer_high","flags_a8_bit8"};
    out<<"{\"reason\":\""<<names[unsigned(r.reason)]<<"\",\"branch\":"<<r.branch;
    if(r.reason==ReadyReason::NotInspected||r.reason==ReadyReason::Ready){
        out<<",\"current\":null,\"next\":null,\"flagsA8\":null,\"phaseTimer\":null}";return;
    }
    out<<",\"current\":"<<r.current<<",\"next\":"<<r.next<<",\"flagsA8\":"<<r.flagsA8<<",\"phaseTimer\":";
    if(std::isfinite(r.phaseTimer))out<<r.phaseTimer;else out<<"null";out<<'}';
}
enum class InspectReason:unsigned {NotAttempted,Available,Busy,Disabled,Anchor,KnifeIdentity,NativeRead,Flags,Unsupported,
    CaptureBusy,RejoinBusy,RejoinChanged,Closing};
inline bool InspectionLockMiss(InspectReason reason)noexcept {
    return reason==InspectReason::CaptureBusy||reason==InspectReason::RejoinBusy;
}
enum class BeforePrepareReason:unsigned {NotAttempted,Available,Busy,Closing,Unsupported};
// Read-only work state copied under Inspect's existing gate. Unknown refuses;
// no field changes native command, attack, cleanup or history authority.
struct CurrentHoldWorkObservation {
    std::uint64_t lifetime=0,bodyOperation=0,drawSerial=0;
    bool known=false,drawOnly=false,outstanding=false,commandPending=false,attackPending=false;
    bool returnReady=false,closing=false,attackNative=false,bridgePending=false;
};
inline bool CurrentHoldWorkSettled(const CurrentHoldWorkObservation& v)noexcept {
    return v.known&&v.lifetime&&v.bodyOperation&&v.drawSerial&&!v.outstanding&&!v.commandPending&&
        !v.attackPending&&!v.returnReady&&!v.closing&&!v.attackNative&&!v.bridgePending;
}
inline bool CurrentHoldWorkIdle(const CurrentHoldWorkObservation& v)noexcept {
    // Passive draw-only observation retains its stricter historical contract.
    return v.drawOnly&&CurrentHoldWorkSettled(v);
}
struct View {
    private_knife::Anchor anchor{};private_knife::Snapshot snapshot{};
    bool owned=false,healthy=false,observationComplete=false,motionEnabled=false,attackBridgeEnabled=false,stockTriggerEnabled=false;
    unsigned firstObservationGap=0; // Packed diagnostic: reason*4 + branch, never authority.
    CurrentHoldWorkObservation currentHoldWork{};
};
// Current coherent observation only; it creates no staged native action.
std::optional<View> Inspect(const ReloadStateOwner&,unsigned knifeWeapon,std::uint64_t sequence,std::int64_t nowNs,InspectReason* diagnostic=nullptr)noexcept;
// No-effect draw cleanup after its exact new paired empty receipt.
void AcknowledgeNoEffectRehide(const Bc2BodyHolster&)noexcept;

// Original gather thread only. Call before portable Prepare sees this source.
// False means skip knife processing; other gameplay remains independent.
// Drains an accepted stage or atomically revokes an unconsumed old stage.
// Call as soon as the gather sees actual new hand metadata, before other
// consumers. This revokes only an unconsumed first native boundary.
void SourceAdvanced(const interaction::HandInteractionSample&)noexcept;
// Publish an ineligible source only when the draw-only service is idle.
// A busy lock or any outstanding work requires the full gather.
bool TryIdleHeldGun(const interaction::KnifeSample&)noexcept;
bool BeforePrepare(const interaction::KnifeSample&,interaction::ChestKnifeCustody&,BeforePrepareReason* diagnostic=nullptr)noexcept;
// Proves real shared claims through policy.CanCommit and repeats actual native
// identity/config/count admission. Chest draw body interval must already exist.
bool Stage(const interaction::KnifeCommand&,const interaction::KnifeSample&,
    const interaction::HandInteraction&,interaction::ChestKnifeCustody&,
    const private_knife::Anchor&,const ReloadStateOwner& actualSelectedOwner,
    std::uint64_t bodyOperation,const std::optional<math::Matrix4>& attackGeometry=std::nullopt,
    float worldUnitsPerMeter=1,std::int64_t geometryDeadlineNs=0)noexcept;
// Bounded historical diagnostics, never an action/lease or a new native reader.
// Values are copied at successful ordinary Observe before originals reset.
struct ReturnFreeRightProof {
    ReloadStateOwner visibilityOwner{},receiptOwner{};
    interaction::HandInteractionSample visibilityInput{},input{};
    RigIdentity rig{};HolsterSuppressionReceipt suppression{};
    std::uint64_t visibilityRequest=0,receiptRequest=0,receiptInputSequence=0,physicalEquipGeneration=0,drawSerial=0;
    std::int64_t visibilityAuthorizationDeadlineNs=0,authorizationDeadlineNs=0,observedNs=0,deadlineNs=0;
    unsigned verifiedCopyMask=0;
    bool visibilityEnabled=false,visibilityHide=false,hidden=false;
    // Receipt plan scalar identity/clocks only; no palettes or mesh pointer is
    // retained. BodyFreeRightEvidenceCurrent already validated the full plan.
    ReloadStateOwner planOwner{};RigIdentity planRig{};
    std::uint64_t planRequest=0,planInputSequence=0,planPhysicalEquipGeneration=0,planMeshSequence=0;
    std::int64_t planObservedNs=0,planDeadlineNs=0,planInputDeadlineNs=0;
    bool planPresent=false,planHidden=false;
};
struct ReturnCompletion {
    interaction::KnifeCommand command{};interaction::KnifeReceipt receipt{};
    private_knife::Anchor anchor{};private_knife::Snapshot snapshot{};
    ReturnFreeRightProof freeRight{};
    std::uint64_t lifetime=0,bodyOperation=0;unsigned chooserMask=0;
    std::int64_t chooserAcceptedNs=0;
    static constexpr bool actionAuthority=false;
};
inline ReturnCompletion CaptureReturnCompletion(const interaction::KnifeCommand& command,
    const interaction::KnifeReceipt& receipt,const private_knife::Anchor& anchor,const private_knife::Snapshot& snapshot,
    const BodyFreeRightEvidence& empty,std::uint64_t lifetime,std::uint64_t operation,unsigned mask,std::int64_t acceptedNs)noexcept {
    ReturnCompletion out;out.command=command;out.receipt=receipt;out.anchor=anchor;out.snapshot=snapshot;
    out.lifetime=lifetime;out.bodyOperation=operation;out.chooserMask=mask;out.chooserAcceptedNs=acceptedNs;
    auto& f=out.freeRight;const auto& v=empty.visibility;const auto& r=empty.receipt;
    f.visibilityOwner=v.nativeOwner;f.receiptOwner=r.nativeOwner;f.visibilityInput=v.input;f.input=empty.input;
    f.rig=r.rig;f.suppression=empty.suppression;f.visibilityRequest=v.request;f.receiptRequest=r.request;
    f.receiptInputSequence=r.inputSequence;f.physicalEquipGeneration=r.physicalEquipGeneration;f.drawSerial=r.drawSerial;
    f.visibilityAuthorizationDeadlineNs=v.authorizationDeadlineNs;f.authorizationDeadlineNs=empty.authorizationDeadlineNs;
    f.observedNs=r.observedNs;f.deadlineNs=r.deadlineNs;f.verifiedCopyMask=r.verifiedCopyMask;
    f.visibilityEnabled=v.enabled;f.visibilityHide=v.hide;f.hidden=r.hidden;
    if(r.evidence){const auto& p=*r.evidence;f.planPresent=true;f.planOwner=p.nativeOwner;f.planRig=p.rig;
        f.planRequest=p.request;f.planInputSequence=p.inputSequence;f.planPhysicalEquipGeneration=p.physicalEquipGeneration;
        f.planMeshSequence=p.meshSequence;f.planObservedNs=p.observedNs;f.planDeadlineNs=p.deadlineNs;
        f.planInputDeadlineNs=p.inputDeadlineNs;f.planHidden=p.hidden;}
    return out;
}
enum class ReturnPendingReason:unsigned {NotObserved,Chooser,NativeRead,Ammunition,OriginalSelection,
    OriginalReadyOrBad,BodyRehide,FreeRightMissing,FreeRightInvalid,FreeRightOwner,CommandCompletion};
struct ReturnPendingObservation {
    interaction::KnifeCommand command{};ReloadStateOwner originalOwner{};
    std::uint64_t lifetime=0,bodyOperation=0;
    unsigned chooserMask=0;bool returnReady=false;
    ReturnPendingReason lastObserveReason=ReturnPendingReason::NotObserved;
    std::int64_t lastObservedNs=0;
    ReadyObservation readiness{};bool nativeBad=false,nativeBadKnown=false;
};
// Passive try-lock copy of already-owned pending scalars; no memory/native call.
// A null value means unavailable, never completion or absence of native work.
std::optional<ReturnPendingObservation> ReadReturnPendingObservation()noexcept;

// Read-only native outcome. Return is delivered only after the coordinator's
// exact operation has a NEW current paired hide + empty-hand receipt.
std::optional<interaction::KnifeReceipt> Observe(const ReloadStateOwner& actualSelectedOwner,
    const Bc2BodyHolster&,const std::optional<BodyFreeRightEvidence>&,std::int64_t nowNs,
    std::optional<ReturnCompletion>* completed=nullptr)noexcept;
// True is permission to begin new hide of the observed actual original weapon,
// not permission to select it. Still requires explicit accepted Return command.
std::optional<std::uint64_t> ReturnNeedsRehide()noexcept;
// Known actual native actor replacement only; missing input/read is not replacement.
void ObserveLifetime(const ReloadStateOwner&)noexcept;
// Explicit session close, independent of XR input. Settled Held only; accepted
// incomplete operations remain owned. Keep dispatcher/guards alive while polling.
enum class SessionCloseStatus {Pending,Complete,Unresolved};
void RequestSessionClose()noexcept;
SessionCloseStatus PollSessionClose()noexcept;
// Stop the shared firing dispatcher only after close succeeds. False retains callbacks/module.
bool Stop()noexcept;
void Report(std::ostream&);
}
