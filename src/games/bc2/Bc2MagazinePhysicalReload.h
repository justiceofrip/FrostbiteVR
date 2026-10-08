#include "Bc2ReloadStartOrigin.h"
#include "Bc2ReloadObservation.h"
#include "Bc2MagazineLeaseObservation.h"
#include "Bc2ReloadKeepAlive.h"
#pragma once
#include "Bc2MagazinePresentation.h"
#include "Bc2ReloadRetirement.h"
#include "Bc2MagazineStart.h"
#include "fvr/interaction/ActionPolicy.h"
#include "fvr/interaction/AmmoSupplyVisual.h"
#include <ostream>
namespace fvr::bc2 {
class Bc2MagazineResourceReload;
enum class MagazineCancelCause:unsigned {
 External=1,ApiOrClock,ReserveUnavailable,OwnerChanged,Mapping,InputOrClaim,
 ExplicitInput,KeepAlive,CounterExhausted,SourceUnavailable,InteractionRejected,InvalidStart,SeatSubmission,NativeObservationRejected,NativeObservationExpired
};
struct MagazinePhysicalApi {
 void* context=nullptr;
 std::optional<Bc2AmmoReserveLease> (*reserve)(void*)noexcept=nullptr;
 std::optional<ReloadHoldIdentity> (*identity)(void*)noexcept=nullptr;
 MagazineCycleStartResult (*start)(void*,const ReloadCycleControl&,const interaction::ManualReloadRequest&,const ReloadMagazineStartupPulse&)noexcept=nullptr;
 MagazineCycleStartResult (*inspectStart)(void*,const ReloadHoldIdentity&,std::uint64_t,const std::optional<ReloadMagazineStartupPulse>&)noexcept=nullptr;
 bool (*keep)(void*,const ReloadCycleControl&)noexcept=nullptr;
 std::optional<ReloadMagazineLease> (*lease)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 std::optional<ReloadMagazineGateAcknowledgement> (*gate)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 bool (*submit)(void*,const ReloadMagazineNativeRequest&)noexcept=nullptr;
 std::optional<ReloadMagazineAckEvidence> (*ack)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 void (*cancel)(void*)noexcept=nullptr;
 std::optional<ReloadCycleRetirement> (*retire)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 std::int64_t (*clock)(void*)noexcept=nullptr;
 ReloadKeepAliveResult (*keepObserved)(void*,const ReloadCycleControl&)noexcept=nullptr;
    ReloadReserveObservation (*reserveObserved)(void*)noexcept=nullptr;
 ReloadMagazineLeaseObservation (*leaseObserved)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 std::optional<AmmoResourceView> (*resourceRead)(void*,const ReloadStateOwner&,std::int64_t)noexcept=nullptr;
 bool (*resourceSubmit)(void*,const AmmoResourceRequest&)noexcept=nullptr;
 std::optional<AmmoResourceOutcome> (*resourceOutcome)(void*,std::uint64_t,const interaction::AmmoResourceContext&)noexcept=nullptr;
};
struct MagazinePhysicalSample {
 bool actionFlagsKnown=false;std::uint64_t actionHeld=0,actionPressed=0;
 MagazineFamilyEvidence family{};
 ReloadStateOwner nativeOwner{};interaction::HandInteractionSample input{};
 interaction::HandInteractionKey weapon{};std::uint64_t trackingEpoch=0,geometrySequence=0;
 math::Matrix4 bodyFromHand{};bool gripPressed=false,ejectPressed=false,cancel=false;
 std::string_view asset{};std::shared_ptr<const SelectedMeshesSnapshot> meshes;
 MagazineRawContact raw{};std::optional<interaction::HandInteractionSample> originalHandEvidence;
};
struct MagazinePhysicalResult {
 MagazineTracking tracking{};interaction::DetachableMagazineResult interaction{};
 bool reloadHeld=false,blocksWeaponActions=false,ownsLeftHand=false;
 // Fresh idle empty-weapon evidence grants ordinary Reload input only. It is
 // not a native hold, physical insertion, or ammunition completion receipt.
 bool ordinaryReloadAllowed=false;
 unsigned acquired=0,submitted=0,completed=0;
 std::optional<interaction::AmmoSupplyVisualSample> bodyAmmo;
};
struct MagazineResourceProbeState {
 MagazinePhysicalResult result{};
 std::optional<AmmoResourceView> resource;
};
// A physically submitted/attached magazine may free the support hand while
// its native ammo transaction still blocks Fire and equipment changes. This
// tests original current-frame evidence; it never advances or acknowledges it.
inline bool MagazineBlocksSupport(const MagazinePhysicalResult& r,std::int64_t now)noexcept {
 if(r.ownsLeftHand)return true;
 if(!r.blocksWeaponActions)return false;
 const auto phase=r.interaction.phase;
 const auto& target=r.tracking.target;
 return !((phase==interaction::DetachableMagazinePhase::AwaitingSeat||phase==interaction::DetachableMagazinePhase::AwaitingOriginalReturn||phase==interaction::DetachableMagazinePhase::Complete)&&
  r.tracking.cycle&&target&&target->role==interaction::MagazinePropRole::Attached&&
  !target->handTarget&&!target->handClaim.id&&MagazineTargetFresh(r.tracking,now));
}
inline void ApplyMagazinePhysicalActions(interaction::ActionOutput& out,const MagazineEquipmentProfile* profile,
 const MagazinePhysicalResult& magazine)noexcept {
 const bool ordinary=magazine.ordinaryReloadAllowed&&!magazine.blocksWeaponActions&&!magazine.reloadHeld;
 if(profile&&profile->Ready()&&!ordinary){out.held&=~interaction::Reload;out.pressed&=~interaction::Reload;}
 if(magazine.reloadHeld)out.held|=interaction::Reload;
 if(magazine.blocksWeaponActions){
  constexpr auto blocked=interaction::Fire|interaction::AlternateFire|interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon;
  out.held&=~blocked;out.pressed&=~blocked;
 }
}
inline void ApplyMagazinePhysicalActions(interaction::ActionOutput& out,std::string_view asset,
 const MagazinePhysicalResult& magazine)noexcept {
 ApplyMagazinePhysicalActions(out,FindMagazineEquipment(asset),magazine);
}
struct MagazinePhysicalProbeState {
 bool active=false,nativeHolding=false,retiring=false,pending=false,ownsHand=false,blocksEquipment=false;
 unsigned acquired=0,started=0,submitted=0,completed=0;
 interaction::DetachableMagazinePhase phase=interaction::DetachableMagazinePhase::Attached;
 interaction::DetachableMagazineReason reason=interaction::DetachableMagazineReason::None;
 std::uint64_t cycle=0;
 std::optional<Bc2AmmoReserveLease> reserve;
 unsigned originalReturns=0;bool originalReturning=false;
 std::optional<interaction::OriginalMagazine> original;
 std::optional<interaction::OriginalMagazineReturnReceipt> originalReceipt;
 unsigned cancelled=0,reconciled=0;
};
// Update shared HandInteraction + renew right GunHold first. Call before support
// grab arbitration so a real magazine contact can claim Mechanism; it never
// steals existing support/sight. Feed raw anatomical wrist from the prior
// renderer sample with ORIGINAL HandInteraction evidence; never guided wrist.
// Wire reloadHeld as the single native Reload pulse, suppress Fire/equipment
// while blocksWeaponActions. Native gate enabling is separately verified.
class Bc2MagazinePhysicalReload {
public:
 static interaction::AmmoSupplyConfig DefaultPouch()noexcept;
 explicit Bc2MagazinePhysicalReload(bool enabled=false,MagazinePhysicalApi api={},
     interaction::AmmoSupplyConfig pouch=DefaultPouch())noexcept;
 ~Bc2MagazinePhysicalReload();
 MagazinePhysicalResult Tick(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
 void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&,MagazineCancelCause=MagazineCancelCause::External)noexcept;
 bool BlocksEquipment()const noexcept;
 bool ResourceBackend()const noexcept{return bool(resource_);}
 MagazineResourceProbeState ResourceProbeState(std::int64_t now)const noexcept;
 void Report(std::ostream&)const;
 void EnableBodyAmmo(bool enabled=true,interaction::SupplyAnchorFrame frame=interaction::SupplyAnchorFrame::HeadYaw)noexcept;
 // Read-only display shared by physical and detached idle routes. Pass the
 // final coherent tracking publication and the actual current right claim.
 std::optional<interaction::AmmoSupplyVisualSample> BodyAmmoDisplay(const MagazineTracking&,
  const std::optional<interaction::HandClaim>& currentGun,std::int64_t now)const noexcept;
 MagazinePhysicalProbeState ProbeState(std::int64_t now)const noexcept {
  return {active_,lease_&&lease_->allThreeHeld&&lease_->deadlineNs>now,retiring_,bool(supply_.Pending()),
   bool(last_.removalClaim)||bool(supply_.Held()),BlocksEquipment(),acquired_,started_,submitted_,completed_,last_.phase,last_.reason,
   owners_.cycle,lastReserve_,originalReturns_,originalReturning_,originalMagazine_,originalReceipt_,cancelled_,reconciled_};
 }
private:
 std::unique_ptr<Bc2MagazineResourceReload> resource_;
 MagazinePhysicalResult TickImpl(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t&)noexcept;
 void RetainDeferredPresentation(MagazinePhysicalResult&,const MagazinePhysicalSample&,interaction::HandInteraction&)noexcept;
 struct EmittedPresentation {MagazineTracking tracking;interaction::DetachableMagazinePhase phase;};
 std::optional<EmittedPresentation> emittedPresentation_;
 bool retainedPresentationThisTick_=false;
 bool Api()const noexcept;
 bool RollbackUnstarted(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
 void ObserveRetirement(const MagazinePhysicalSample&)noexcept;
 std::int64_t Now(std::int64_t fallback)const noexcept;
 void PublishReplacementFrame(MagazineTracking&,const MagazinePhysicalSample&,bool canCapture=false)noexcept;
 const MagazineEquipmentProfile* profile_=&Xm8MagazineEquipment();
 interaction::AmmoSupply supply_;interaction::DetachableMagazine interaction_{Xm8MagazineConfig()};
 interaction::AmmoSupplyConfig pouch_{};bool bodyAmmoEnabled_=false;
 interaction::SupplyAnchorFrame bodyAmmoFrame_=interaction::SupplyAnchorFrame::HeadYaw;
 std::optional<MagazineReplacementFrame> replacementFrame_;
 std::optional<MagazineCarryFrame> removalFrame_;
 ReloadKeepAliveResult Keep(const ReloadCycleControl&,std::int64_t)noexcept;
 std::int64_t acceptedControlDeadline_=0;
 std::int64_t observationGapDeadline_=0;unsigned observationDeferred_=0,observationExpired_=0;
 ReloadStartOrigin startOrigin_{};
 MagazinePhysicalApi api_{};bool enabled_=false,active_=false,retiring_=false,gateApplied_=false,drained_=false,blocksCurrent_=false;
 bool startupUnknown_=false;MagazineCycleStartResult startupResult_=MagazineCycleStartResult::NotStarted;
 unsigned unstarted_=0,startInspections_=0;
 struct StartAttempt {std::uint64_t input=0,request=0,cycle=0;std::int64_t now=0,observed=0,deadline=0;
  unsigned stage=0;bool identityPresent=false,identityMatches=false;int loaded=-1,reserve=-1;};
 std::array<StartAttempt,64> startAttempts_{};unsigned startAttemptCount_=0,startAttemptDropped_=0;
 Bc2MagazineOwnerMap owners_{};
 interaction::DetachableMagazineResult last_{};
 std::optional<interaction::ManualReloadRequest> unseat_;
 std::optional<ReloadMagazineLease> lease_,submittedLease_;
 std::optional<Bc2AmmoReserveLease> lastReserve_,retainedReserve_,originalReserve_;
 std::optional<interaction::OriginalMagazine> originalMagazine_;
 std::optional<interaction::OriginalMagazineReturnReceipt> originalReceipt_;
 bool originalReturning_=false;unsigned originalReturns_=0,originalReturnRejected_=0;
 std::int64_t originalReturnNs_=0;
 MagazineCancelCause cancelCause_=MagazineCancelCause::External;unsigned heldReadDeferrals_=0;
 unsigned reserveReadMisses_=0,transferReadDeferrals_=0,transferReadExpirations_=0,retireAttachedWaits_=0;
 std::optional<ReloadMagazineGateAcknowledgement> gate_;
 std::optional<ReloadMagazineStartupPulse> startupPulse_;
 std::optional<ReloadMagazineAckEvidence> ack_;
 std::optional<ReloadCycleRetirement> retirement_;
 struct Evidence{interaction::AmmoSupplySample supply{};};
 std::array<std::optional<Evidence>,32> history_{};std::size_t historyNext_=0;
 std::uint64_t nextCycle_=0,lastSequence_=0;
 std::int64_t pulseUntil_=0,lastNow_=0,lastInputDeadline_=0;
 unsigned acquired_=0,started_=0,submitted_=0,completed_=0,cancelled_=0,reconciled_=0;
 struct NativeLeaseEvidence {
  bool present=false,verified=false,identityMatches=false,cycleMatches=false,held=false;
  std::uint64_t sequence=0,cycle=0;std::int64_t observedNs=0,deadlineNs=0;
 };
 struct NativeBoundaryEvidence {
  interaction::MagazineNativeObservation supplied{};
  NativeLeaseEvidence prior{},returned{},accepted{};
  std::optional<interaction::HandClaim> currentClaim;
  bool gateApplied=false,sourceChecked=false,sourceAvailable=false,ownerMatches=false,weaponMatches=false;
  unsigned phase=0;std::int64_t reserveObservedNs=0,reserveDeadlineNs=0,controlDeadlineNs=0;
 };
 std::optional<NativeBoundaryEvidence> nativeBoundary_;
 struct Event {unsigned kind=0,phase=0,reason=0;std::uint64_t cycle=0,input=0,geometry=0,item=0,request=0,seat=0;
  std::int64_t observedNs=0,nowNs=0;float progress=0;unsigned cause=0;
  std::uint64_t originalItem=0,originalGeneration=0;unsigned originalRounds=0;int nativeLoaded=-1,nativeReserve=-1;ReloadStartOrigin startOrigin{};std::optional<interaction::MagazineClaimFailure> claimFailure;
  interaction::MagazineNativeFailureCheck nativeFailureCheck=interaction::MagazineNativeFailureCheck::None;
  std::optional<NativeBoundaryEvidence> nativeBoundary;
  std::optional<interaction::MagazineMotionFailure> motionFailure;};
 std::array<Event,128> events_{};unsigned eventCount_=0,eventDropped_=0;
 struct Admission {std::int64_t now=0;std::uint64_t input=0,geometry=0,claim=0;
  unsigned reason=0,claimKind=0;bool released=false,ready=false;};
 std::array<Admission,32> admissions_{};unsigned admissionCount_=0,admissionDropped_=0;
 std::optional<interaction::DetachableMagazineReason> admissionReason_;
 void Record(unsigned,const interaction::HandInteractionSample&,std::uint64_t geometry=0)noexcept;
};
} // namespace fvr::bc2
