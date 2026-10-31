#include "Bc2BodyInteractionInput.h"
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
#include "Bc2ContextualToolAdapter.h"
#endif
#include "Bc2NativeCycleSelectionTracker.h"
#include "Bc2ShotgunEquipment.h"
#include "Bc2BoltEquipment.h"
#include "Bc2PhysicalFixedContainer.h"
#include "Bc2NativeInputEpochs.h"
#include "Bc2HeldFireCadence.h"
#include "Bc2SceneMilestones.h"
#include "Bc2ViewBasisEvidence.h"
#include <Windows.h>
#include <intrin.h>
#include <MinHook.h>
#include "Bc2Gameplay.h"
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
#include "Bc2ChestKnifeCoordinator.h"
#include "Bc2SelectedKnifeNative.h"
#endif
#include "Bc2M95StockShotProbe.h"
#include "Bc2NativeCycleRuntime.h"
#include "Bc2MechanismAudio.h"
#include "Bc2ShellReloadAudio.h"
#include "Bc2PumpControllerProbe.h"
#include "Bc2BoltCustodyBridge.h"
#include "Bc2BoltPlayerCustody.h"
#include "Bc2BoltRuntimeGeometry.h"
#include "Bc2PumpActions.h"
#include "Bc2PumpCalibration225.h"
#include "fvr/interaction/ReloadGrip.h"
#include "fvr/interaction/MechanismAimContinuation.h"
#include "Bc2EmptyFireProbe.h"
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
#include "Bc2ArmingEmptyProbe.h"
#endif
#include "Bc2InputBinding.h"
#include "Bc2EquipmentIdentity.h"
#include "Bc2VehicleRoutes.h"
#include "Bc2BoatProfile.h"
#include "Bc2CampaignVehicleProfile.h"
#include "Bc2VehicleCameraPolicy.h"
#include "Bc2VehicleShotObserver.h"
#include "Bc2BoatReticle.h"
#ifdef FVR_BC2_VEHICLE_MOUNTED_PROBE
#include "Bc2VehicleMountedObserver.h"
#endif
#include "Bc2AttachedSoldierProfile.h"
#include "Bc2BoatAim.h"
#include "Bc2VehicleCommit.h"
#include "Bc2ContextInteractEvidence.h"
#include "fvr/interaction/VehicleTracking.h"
#include "Bc2Rig.h"
#include "Bc2PlayerPair.h"
#include "Bc2RigPublication.h"
#include "Bc2WeaponProfiles.h"
#include "Bc2WeaponMode.h"
#include "Bc2BodyInventory.h"
#include "Bc2StowedAmmoReserve.h"
#include "Bc2BodyHolster.h"
#include "Bc2BodyHolsterLifecycle.h"
#include "Bc2ReloadState.h"
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
#include "Bc2OrdinaryGrenadeProbe.h"
#include "Bc2PlayerGrenade.h"
#endif
#include "Bc2SelectedMeshesObservation.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2ReloadRequestProbe.h"
#include "Bc2MagazineReloadProbe.h"
#include "Bc2MagazineReloadSession.h"
#include "Bc2PhysicalReload.h"
#include "Bc2PhysicalFeed.h"
#include "Bc2PhysicalPistolSlide.h"
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2MagazineResourceProbe.h"
#include "Bc2InventoryReloadProbe.h"
#include "Bc2MagazineDetached.h"
#include "Bc2MagazineDetachedProbe.h"
#include "Bc2PhysicalReloadProbe.h"
#include "Bc2ResourceInventoryProbe.h"
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
#include "Bc2PhysicalReloadLifecycleProbe.h"
static_assert(FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO>=1&&FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO<=4);
#endif
#include "fvr/interaction/SightFlip.h"
#include "fvr/interaction/SightFlipPackets.h"
#include "fvr/interaction/SightGrasp.h"
#include "fvr/interaction/SightVisualHandoff.h"
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/SightOwnershipRecovery.h"
#include "fvr/interaction/SupportOwnershipRecovery.h"
#include "fvr/interaction/ArmIk.h"
#include "fvr/interaction/AimFrame.h"
#include "fvr/interaction/TrackedBodyAnchor.h"
#include "fvr/interaction/RoomscaleFollow.h"
#include "fvr/interaction/ComfortCamera.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cmath>
#include <vector>
#include <mutex>
#include "fvr/interaction/FiringPoseHistory.h"
#include "Bc2ProjectileLaunch.h"
#include "Bc2ServerFireObservation.h"
#include "Bc2FireRouteTelemetry.h"
namespace fvr::bc2::gameplay {
namespace {
using UpdateFn=void(__thiscall*)(void*,float);using GatherFn=void(__thiscall*)(void*,void*);
UpdateFn updateOriginal=nullptr;GatherFn gatherOriginal=nullptr;
using FireOriginFn=std::uintptr_t(__thiscall*)(void*,void*);
FireOriginFn fireOriginOriginal=nullptr;FireOriginCandidates fireProfile{};
struct FireRecord {unsigned caller=0,effects=0,player=0,serverSoldier=0,clientSoldier=0,weapon=0,data=0,thread=0;ULONGLONG ms=0;std::array<float,16> matrix{},clientEffects{},serverEffects{};unsigned playerId=0,weaponKind=0,shotToken=0;bool tokenValid=false;bool clientPath=false,originWritten=false;unsigned phase=0;std::array<float,16> after{};std::optional<rigPublication::WeaponShotFrame> shot;std::optional<math::Matrix4> mappedShot;std::optional<interaction::FiringPoseSample> eventPose;
    WeaponEquipmentIdentity equipment{};bool nativeMuzzleSupported=false,observedCandidateUnexpired=false;
    std::int64_t observedQpc=0,observedCurrentDeadline=0;std::uint64_t observedCurrentGeneration=0;
    std::optional<math::Matrix4> observedMuzzleCandidate;
    std::optional<NativeMuzzleConfig> nativeConfig;bool nativeConfigUnchanged=false;
    std::uint64_t nativeInvocation=0;
};
std::array<FireRecord,32> fireRecords{};unsigned fireRecordCount=0;
std::atomic_flag fireRecordGate=ATOMIC_FLAG_INIT;
std::atomic<unsigned> fireOriginCalls=0,fireOriginMatches=0,fireOriginRejected=0;
using ClientShootFn=std::uintptr_t(__thiscall*)(void*,void*,float,unsigned,void*);
using MatrixCopyFn=std::uintptr_t(__thiscall*)(void*,const void*);
ClientShootFn clientShootOriginal=nullptr,serverShootOriginal=nullptr;MatrixCopyFn matrixCopyOriginal=nullptr;
using ComposeFn=std::uintptr_t(__thiscall*)(void*,void*,const void*);ComposeFn composeOriginal=nullptr;
std::mutex shotPoseMutex;interaction::FiringPoseHistory shotHistory;
std::optional<MuzzleEventSource> muzzleHistorySource;
std::atomic<unsigned> clientOriginWrites=0,serverOriginWrites=0,originWriteFailures=0,shotSourceChanges=0,originFallbacks=0,originWritesAfterLogFull=0;
std::atomic<unsigned> clientShotCalls=0,clientShotCopies=0,clientShotRejected=0;
AimCandidates aimingProfile{};InputBindingCandidates profile{};std::uintptr_t image=0;unsigned imageSize=0;
InputReader readInput=nullptr;std::array<void*,14> hooks{};
using ProjectileContextFn=std::uintptr_t(__thiscall*)(void*,void*);
ProjectileContextFn projectileContextOriginal=nullptr;ProjectileLaunchBinding projectileLaunchBinding{};
std::atomic<std::uint64_t> shotInvocationSerial=0;
FeedbackWriter writeFeedback=nullptr;unsigned feedbackSent=0,feedbackDropped=0;
std::atomic<bool> enabled=false;std::atomic<unsigned> active=0,inputThread=0;
bool fireRouteTelemetryEnabled=false;
FireRouteTelemetry fireRouteTelemetry;
thread_local FireRouteRecord* interactionTrace=nullptr; // Callback-local observation only.
std::atomic<unsigned> updates=0,localUpdates=0,gathers=0,onFoot=0,vehicle=0,stale=0,applied=0,committed=0,nativeInactive=0,mismatches=0,turnUnavailable=0,turns=0,moving=0,buttons=0;
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
std::unique_ptr<Bc2ChestKnifeCoordinator> chestKnife;
#endif
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
std::unique_ptr<Bc2ContextualToolAdapter> contextualTool;
#endif
bool motionAim=false,bodyFollow=false,observePoses=false,handPoses=false,deathProbe=false,equipProbe=false,equipHeld=true,muzzleFire=false,twoHandGrip=false;
bool weaponModeBindingVerified=false,weaponModeHeld=true,sightFlip=false;
std::optional<interaction::SightFlip> sightPolicy;
interaction::SightFlipPhase sightPhase=interaction::SightFlipPhase::Idle;
std::uint64_t sightAcknowledged=0;
std::optional<interaction::SightGraspBinding> sightGrasp;
std::optional<interaction::SightVisualHandoff> sightVisual;
std::uint64_t sightGraspGeneration=0;
const SightAdapterProfile* sightAdapterAtGrab=nullptr;
math::Matrix4 sightFrontAtGrab{};math::Vec3 sightPalmAtGrab{};interaction::SightMode sightGraspMode=interaction::SightMode::Primary;
std::uint64_t sightGraspToken=0;
std::atomic<unsigned> sightRequests=0,sightCommits=0,sightCancellations=0;
std::optional<ReloadStateBinding> reloadStateBinding;
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
std::optional<ReloadServerBinding> ordinaryGrenadeServer;
bool ordinaryGrenadeBindings=false;
std::unique_ptr<private_grenade::Probe> ordinaryGrenade;
std::unique_ptr<player_grenade::Controller> playerGrenade;
#ifdef FVR_BC2_TWO_GRENADE_DEATH_PROBE
bool twoGrenadeDeathEnabled=false;
TwoGrenadeLifetimeGuard twoGrenadeDeathLifetime;
std::atomic_flag twoGrenadeDeathGate=ATOMIC_FLAG_INIT;
TwoGrenadeNativeWitness twoGrenadeDeathWitness{};
bool TwoGrenadeDeathAllowsPrepare(const std::optional<private_grenade::Native>& native)noexcept {
    if(!twoGrenadeDeathEnabled)return true;
    if(!native||!playerGrenade)return false;
    const auto& owner=native->owner;
    return twoGrenadeDeathLifetime.Allows(owner.player,owner.soldier,owner.weak,owner.actorGeneration,owner.space,playerGrenade->NativeEdges());
}
void PublishTwoGrenadeDeathReadiness(const std::optional<private_grenade::Native>& native,const private_grenade::Facts& facts)noexcept {
    if(!twoGrenadeDeathEnabled||!playerGrenade)return;
    const auto value=native&&!twoGrenadeDeathLifetime.Lost()?playerGrenade->SecondDeathReadiness(*native,facts):std::nullopt;
    if(twoGrenadeDeathGate.test_and_set(std::memory_order_acquire))return;
    twoGrenadeDeathWitness=value?*value:TwoGrenadeNativeWitness{};
    twoGrenadeDeathGate.clear(std::memory_order_release);
}
#endif
#endif
SelectedMeshesObservation selectedMeshes;
std::atomic<std::shared_ptr<const AmmoCounterOwner>> ammoCounterOwner;
std::uint64_t mechanismAudioActor=0,mechanismAudioGeneration=0;
std::uint64_t shellAudioActor=0,shellAudioGeneration=0;
void PublishShellReloadAudio(const ReloadStateOwner& native,const interaction::HandInteractionSample& input,
    interaction::HandInteractionKey item,const PhysicalReloadProbeState& state,
    const std::optional<interaction::ShellReloadAudioEvent>& event,bool manual,const ShotgunAudioProfile* profile) {
    if(shellAudioActor&&(shellAudioActor!=input.owner.actor||shellAudioGeneration!=input.owner.actorGeneration))
        shellReloadAudio::RetireActor(shellAudioActor,shellAudioGeneration);
    shellAudioActor=input.owner.actor;shellAudioGeneration=input.owner.actorGeneration;
    shellReloadAudio::Source source;
    source.soldier=native.soldier;source.weak=native.weak;source.weapon=native.weapon;
    source.identity={input.owner.actor,input.owner.actorGeneration,item.id,item.generation,
        state.identity.owner==native?state.cycle:0};
    if(event)source.identity=event->identity;
    source.observedNs=input.observedNs;source.deadlineNs=input.deadlineNs;
    source.manual=manual;source.spas=manual;source.profile=profile;source.focused=input.focused;source.tracked=input.tracked[0]&&input.tracked[1];
    shellReloadAudio::Publish(source);
    if(event)shellReloadAudio::Emit(*event);
}
void PublishMechanismAudio(const ReloadStateOwner& native,const interaction::HandInteractionSample& input,
    interaction::HandInteractionKey item,mechanismAudio::Cue cue,
    const std::optional<interaction::MechanismAudioEvent>& event,bool manual,const ShotgunAudioProfile* profile) {
    if(mechanismAudioActor&&(mechanismAudioActor!=input.owner.actor||mechanismAudioGeneration!=input.owner.actorGeneration))
        mechanismAudio::RetireActor(mechanismAudioActor,mechanismAudioGeneration);
    mechanismAudioActor=input.owner.actor;mechanismAudioGeneration=input.owner.actorGeneration;
    mechanismAudio::Source source;
    source.soldier=native.soldier;source.weak=native.weak;source.weapon=native.weapon;
    source.identity={input.owner.actor,input.owner.actorGeneration,item.id,item.generation,0,0};
    if(const auto view=reloadFlowRuntime::ReadNativeCycleView(input.nowNs);view&&view->native.owner==native){
        source.identity.cycle=view->cycle;source.identity.shot=view->shot;
    }
    if(event)source.identity=event->identity;
    source.cue=cue;source.profile=profile;source.observedNs=input.observedNs;source.deadlineNs=input.deadlineNs;
    source.manual=manual;source.focused=input.focused;source.tracked=input.tracked[0]&&input.tracked[1];
    mechanismAudio::Publish(source);
    if(event)mechanismAudio::Emit(*event);
}
void CancelPhysicalReload(PhysicalReloadResult* unpublished=nullptr,unsigned sourceFlags=0,bool preserveMagazine=false)noexcept;
void CancelWeaponVisibilityProbe()noexcept;
std::optional<Bc2EmptyFireProbe> emptyFireFixture;
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
std::optional<Bc2ArmingEmptyProbe> armingEmptyFixture;
#endif
void ClearReloadOwner(ReloadOwnerLoss loss=ReloadOwnerLoss::LifetimeEnded)noexcept {
    ammoCounterOwner.store({},std::memory_order_release);
    mechanismAudio::Publish({});
    shellReloadAudio::Publish({});
    if(loss==ReloadOwnerLoss::LifetimeEnded&&shellAudioActor){
        shellReloadAudio::RetireActor(shellAudioActor,shellAudioGeneration);
        shellAudioActor=shellAudioGeneration=0;
    }
    if(loss==ReloadOwnerLoss::LifetimeEnded&&mechanismAudioActor){
        mechanismAudio::RetireActor(mechanismAudioActor,mechanismAudioGeneration);
        mechanismAudioActor=mechanismAudioGeneration=0;
    }
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
if(armingEmptyFixture)armingEmptyFixture->Cancel();
#endif
if(emptyFireFixture)emptyFireFixture->Cancel();CancelWeaponVisibilityProbe();CancelPhysicalReload(nullptr,0,loss==ReloadOwnerLoss::PresentationUnavailable);selectedMeshes.Clear();reloadFlowRuntime::ClearOwner(loss);}
std::array<ReloadStateSnapshot,128> reloadSnapshots{};
unsigned reloadSnapshotCount=0;std::array<unsigned,7> reloadStatuses{};
std::array<unsigned,4> reloadPublicationStatuses{};std::array<unsigned,10> reloadPublicationReasons{};
ULONGLONG reloadObservedAt=0,reloadObservedStart=0;
std::int64_t reloadObservedSourceNs=0;
bool reloadRequestMode=false;std::optional<Bc2ReloadRequestProbe> reloadRequestFixture;std::optional<Bc2MagazineReloadProbe> magazineReloadFixture;
bool physicalReloadMode=false;std::optional<Bc2PhysicalReload> physicalReload;
std::optional<Bc2PhysicalPump> physicalPump;
std::optional<Bc2PhysicalBolt> physicalBolt;
std::optional<Bc2PhysicalFixedContainer> physicalFixedContainer;
bool fixedContainerGripHeld=false;
std::optional<Bc2BoltControllerProbe> physicalBoltFixture;
std::shared_ptr<const Bc2BoltCalibration> physicalBoltCalibration;
interaction::BoltCustodyPhase physicalBoltCustody=interaction::BoltCustodyPhase::Idle;
bool physicalBoltContactStarted=false;
bool ordinaryManualCycles=false;
std::optional<Bc2BoltPlayerCustody> ordinaryBoltPlayer;
std::optional<Bc2BoltControllerProbe> ordinaryBoltInputFixture;
ReloadStateOwner ordinaryAdapterOwner{};
const ShotgunReloadEquipmentProfile* ordinaryShotgunEquipment=nullptr;
Bc2NativeCycleSelectionTracker ordinaryCycleSelection;
std::uint64_t ordinarySelectionReadGaps=0,ordinarySelectionRetainedGaps=0,ordinarySelectionCustodyGaps=0;
std::uint64_t ordinarySelectionFirstCustodyGap=0;
std::array<std::uint64_t,6> ordinaryBoltPreparationSkips{},ordinaryBoltPreparationFirstOwnedSkip{};
void RecordOrdinaryBoltPreparationSkip(unsigned reason,const interaction::InputFrame& input)noexcept {
    ++ordinaryBoltPreparationSkips[reason];
    if(ordinaryBoltPlayer&&ordinaryBoltPlayer->OwnsGunCustody()&&!ordinaryBoltPreparationFirstOwnedSkip[reason])
        ordinaryBoltPreparationFirstOwnedSkip[reason]=input.generation;
}
std::optional<Bc2PhysicalBoltSample> ordinaryBoltSample;
Bc2PhysicalBoltResult ordinaryBoltResult;
Bc2BoltControllerContact ordinaryBoltContact;
bool ordinaryBoltEarlyTick=false,ordinarySelectionReady=false,ordinaryTrackingRecovery=false;
Bc2PhysicalBoltApi OrdinaryBoltApi()noexcept {
    return {nullptr,
        [](void*,const Bc2NativeCycleControl& c)noexcept{return reloadFlowRuntime::PublishNativeCycleControl(c);},
        [](void*,std::int64_t now)noexcept{return reloadFlowRuntime::ReadNativeCycleView(now);},
        [](void*,const interaction::WeaponCycleReady& r)noexcept{return reloadFlowRuntime::AcknowledgeNativeCycleReady(r);},
        [](void*)noexcept{reloadFlowRuntime::CancelNativeCycle();},
        {[](void*,std::int64_t now)noexcept{return reloadFlowRuntime::ReadNativeCycleRecoveryView(now);},
         [](void*,const interaction::WeaponCycleIdleDebtRelease& r,const interaction::HandInteractionSample& input)noexcept{
             return reloadFlowRuntime::SubmitNativeCycleRecovery(r,input);},
         [](void*,const interaction::WeaponCycleIdleDebtReady& r)noexcept{return reloadFlowRuntime::AcknowledgeNativeCycleRecovery(r);},
         [](void*,const interaction::WeaponCycleIdleDebtRelease& r,const interaction::HandInteractionSample& input)noexcept{
             return reloadFlowRuntime::SubmitNativeCycleRecoveryResult(r,input);}},
        [](void*,const Bc2NativeCycleControl& c)noexcept{return reloadFlowRuntime::PublishNativeCycleControlResult(c);},
        [](void*,const Bc2NativeCycleControl& c,const interaction::WeaponCycleReloadPause& pause)noexcept{
            return reloadFlowRuntime::PrepareNativeCycleReload(c,pause);},
        [](void*,const Bc2NativeCycleControl& c,const interaction::WeaponCycleIdleDebtLease& lease)noexcept{
            return reloadFlowRuntime::ResumeNativeCycleReload(c,lease);}};
}
Bc2PhysicalPumpApi OrdinaryPumpApi()noexcept {
    return {nullptr,
        [](void*,const Bc2NativeCycleControl& c)noexcept{return reloadFlowRuntime::PublishNativeCycleControl(c);},
        [](void*,std::int64_t now)noexcept{return reloadFlowRuntime::ReadNativeCycleView(now);},
        [](void*,const interaction::WeaponCycleReady& r)noexcept{return reloadFlowRuntime::AcknowledgeNativeCycleReady(r);},
        [](void*)noexcept{reloadFlowRuntime::CancelNativeCycle();},
        {[](void*,std::int64_t now)noexcept{return reloadFlowRuntime::ReadNativeCycleRecoveryView(now);},
         [](void*,const interaction::WeaponCycleIdleDebtRelease& r,const interaction::HandInteractionSample& input)noexcept{
             return reloadFlowRuntime::SubmitNativeCycleRecovery(r,input);},
         [](void*,const interaction::WeaponCycleIdleDebtReady& r)noexcept{return reloadFlowRuntime::AcknowledgeNativeCycleRecovery(r);},
         [](void*,const interaction::WeaponCycleIdleDebtRelease& r,const interaction::HandInteractionSample& input)noexcept{
             return reloadFlowRuntime::SubmitNativeCycleRecoveryResult(r,input);}},
        [](void*,const Bc2NativeCycleControl& c)noexcept{return reloadFlowRuntime::PublishNativeCycleControlResult(c);}};
}
std::optional<Bc2PumpControllerProbe> physicalPumpFixture;
std::optional<Bc2M95StockShotProbe> m95StockShotFixture;
PhysicalReloadApi physicalReloadApi{};
std::optional<Bc2PhysicalFeed> physicalFeed;
std::optional<Bc2PhysicalPistolSlide> physicalPistolSlide;
std::optional<Bc2MagazinePhysicalReload> magazinePhysical;MagazinePhysicalApi magazinePhysicalApi{};
std::optional<Bc2MagazinePhysicalProbe> magazinePhysicalFixture;
std::optional<Bc2MagazineResourceProbe> magazineResourceFixture;
std::optional<Bc2InventoryReloadProbe> inventoryReloadFixture;
std::optional<Bc2ResourceInventoryProbe> resourceInventoryFixture;
#ifdef FVR_BC2_SHELL_FOCUS_LOSS_PROBE
std::atomic<std::shared_ptr<const ShellFocusWitness>> shellFocusReady;
#endif
#ifdef FVR_BC2_PUMP_RECEIVER_GAP_PROBE
std::atomic<std::shared_ptr<const PumpReceiverGapWitness>> receiverGapReady;
#endif
bool resourcePumpCombination=false;
std::optional<Bc2MagazineDetached> magazineDetached;
std::optional<Bc2MagazineDetachedProbe> magazineDetachedFixture;
std::atomic<unsigned> magazineRecoverySuppressionCommits=0,magazineRecoverySuppressionFailures=0;
std::optional<Bc2BodyInventory> bodyInventory;
std::optional<Bc2BodyHolster> bodyHolster;
StowedAmmoReserveCache stowedAmmoReserveCache;
std::atomic<std::shared_ptr<const BodyHolsterProbeSample>> bodyHolsterSample;
std::optional<Bc2BodyHolsterProbe> bodyHolsterFixture;
std::atomic<std::shared_ptr<const BodyHolsterFixtureSample>> bodyHolsterFixtureSample;
std::optional<WeaponModeCommand> holsterQueuedDraw;
std::uint64_t holsterNativeTick=0;
std::atomic<unsigned> holsterSuppressionCommits=0,holsterSuppressionFailures=0,holsterFreePublications=0;
interaction::BodyAnchorConfig bodyAnchors{};
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
std::optional<Bc2PhysicalReloadLifecycleProbe> physicalReloadFixture;
#else
std::optional<Bc2PhysicalReloadProbe> physicalReloadFixture;
#endif
std::optional<Bc2WeaponVisibilityProbe> weaponVisibilityProbe;
std::atomic<std::shared_ptr<const WeaponVisibilityProbeSample>> weaponVisibilitySample;
std::atomic<unsigned> weaponVisibilityActionCommits=0,weaponVisibilityActionFailures=0;

struct SightRecord {
    ULONGLONG ms=0;std::uint64_t generation=0,contactGeneration=0,coherentGeneration=0,request=0;
    unsigned weapon=0,phase=0,cancel=0,packetReason=0;
    float distance=0,angle=0,squeeze=0,pairedSqueeze=0,supportDistance=0;
    bool contact=false,grabbed=false,committed=false,preferred=false,tracked=false,familyValid=false,paired=false,advanced=false;
};
std::array<SightRecord,192> sightRecords{};unsigned sightRecordCount=0,sightRecordNext=0;ULONGLONG sightRecordTime=0;
std::array<SightRecord,128> sightEvents{};unsigned sightEventCount=0,sightEventNext=0;
std::uint64_t sightRecordsTotal=0,sightEventsTotal=0;
interaction::SightFlipPackets sightPackets;
// Gesture summaries survive the periodic pose ring rolling over. Keep bounded
// native-relative geometry and extrema, not an unbounded tracking history.
struct SightGestureRecord {
    std::uint64_t id=0,startGeneration=0,endGeneration=0,request=0;
    ULONGLONG startMs=0,endMs=0;
    unsigned weapon=0,startMode=0,reason=0,freshSamples=0;
    math::Vec3 pivot{},axis{},nativePivot{},startHand{},endHand{};
    float minAngle=0,maxAngle=0,peakProgress=0,maxContactDistance=0;
    std::int64_t longestDetentNs=0;
    bool committed=false,finished=false;
};
std::array<SightGestureRecord,64> sightGestures{};
unsigned sightGestureCount=0,sightGestureNext=0;std::uint64_t sightGesturesTotal=0;
std::optional<unsigned> activeSightGesture;
void FinishSightGesture(const interaction::SightFlipResult& result){
    if(!activeSightGesture)return;
    auto& r=sightGestures[*activeSightGesture];r.endMs=GetTickCount64();
    r.reason=unsigned(result.reason);r.finished=true;activeSightGesture.reset();
}
void RecordSightGesture(const interaction::SightFlipSample& sample,const interaction::SightFlipResult& result,
    const rigPublication::WeaponSightContact& contact,unsigned weapon,bool fresh){
    if(result.grabbed&&sightPolicy){
        activeSightGesture=sightGestureNext;sightGestureNext=(sightGestureNext+1)%sightGestures.size();
        sightGestureCount=std::min(unsigned(sightGestures.size()),sightGestureCount+1);
        auto& r=sightGestures[*activeSightGesture];r={};r.id=++sightGesturesTotal;
        r.startMs=GetTickCount64();r.startGeneration=sample.sequence;r.weapon=weapon;r.startMode=unsigned(sample.nativeMode);
        r.pivot=sightPolicy->Config().pivotMeters;r.axis=sightPolicy->Config().axis;
        r.nativePivot=contact.pivotMeters;r.startHand=sample.handLocalMeters;r.endHand=r.startHand;
    }
    if(!activeSightGesture)return;
    auto& r=sightGestures[*activeSightGesture];r.endMs=GetTickCount64();r.endGeneration=sample.sequence;
    r.minAngle=std::min(r.minAngle,result.signedRadians);r.maxAngle=std::max(r.maxAngle,result.signedRadians);
    r.peakProgress=std::max(r.peakProgress,r.startMode==unsigned(interaction::SightMode::Primary)?result.signedRadians:-result.signedRadians);
    if(!r.request)r.longestDetentNs=std::max(r.longestDetentNs,result.detentDwellNs); // Stop at dispatch, not post-ack hold.
    if(fresh&&sample.contactValid&&std::isfinite(sample.handLocalMeters.x)&&std::isfinite(sample.handLocalMeters.y)&&std::isfinite(sample.handLocalMeters.z)){
        ++r.freshSamples;r.endHand=sample.handLocalMeters;
        if(std::isfinite(sample.contactDistanceMeters))r.maxContactDistance=std::max(r.maxContactDistance,sample.contactDistanceMeters);
    }
    if(result.request)r.request=result.request->id;
    if(result.committedMode)r.committed=true;
    if(result.released||result.cancelled)FinishSightGesture(result);
}
void RecordSight(const SightRecord& record,bool event){
    if(event||record.ms-sightRecordTime>=100){
        sightRecords[sightRecordNext]=record;sightRecordNext=(sightRecordNext+1)%sightRecords.size();
        sightRecordCount=std::min(unsigned(sightRecords.size()),sightRecordCount+1);sightRecordTime=record.ms;++sightRecordsTotal;
    }
    if(event){
        sightEvents[sightEventNext]=record;sightEventNext=(sightEventNext+1)%sightEvents.size();
        sightEventCount=std::min(unsigned(sightEvents.size()),sightEventCount+1);++sightEventsTotal;
    }
}
void ReleaseSightOwnership()noexcept;
void CancelPendingSight(std::uint64_t)noexcept;
void CancelSight(){sightAdapterAtGrab=nullptr;ReleaseSightOwnership();sightGrasp.reset();sightVisual.reset();sightPackets.Reset();if(sightPolicy){const auto result=sightPolicy->Update({});if(result.cancelled)++sightCancellations;CancelPendingSight(result.cancelledRequest);FinishSightGesture(result);
    if(interactionTrace){auto& r=interactionTrace->interactions.sight;r.observed=r.reset=true;r.phase=unsigned(interaction::SightFlipPhase::Idle);
        r.policyPhase=unsigned(result.phase);r.reason=unsigned(result.reason);r.cancelled=result.cancelled;r.released=result.released;}}
    sightPhase=interaction::SightFlipPhase::Idle;sightAcknowledged=0;}

struct ModeRecord {ULONGLONG ms=0,ackMs=0;unsigned actor=0,weak=0,from=0,target=0,action=0;std::uint64_t owner=0,space=0;bool cancelled=false;std::uint64_t gesture=0;};
std::array<ModeRecord,32> modeRecords{};unsigned modeRecordCount=0,modeRecordNext=0;
std::optional<unsigned> pendingMode;
std::atomic<unsigned> modeRequests=0,modeRejections=0,modeAcknowledgements=0;
interaction::SupportGrip supportGrip;
interaction::MechanismAimContinuation pumpAimContinuation;
std::atomic<unsigned> pumpAimCaptures=0,pumpAimApplications=0,pumpAimRejected=0;
std::atomic<unsigned> supportSamples=0,supportGrabs=0,supportReleases=0,supportPreservationFailures=0,supportFireSamples=0,supportAimSamples=0;
float supportAimResidual=0;
struct SupportRecord {ULONGLONG ms=0;std::uint64_t generation=0;float distance=0,squeeze=0,angle=0;bool contact=false,holding=false,engaged=false,released=false;math::Quaternion rawAim{},aim{};unsigned weaponKind=0;unsigned reason=0,actions=0;std::uint64_t token=0;};
std::array<SupportRecord,192> supportRecords{};unsigned supportRecordCount=0,supportRecordNext=0;ULONGLONG supportRecordTime=0;
// Keep the earliest transition evidence separately from rolling pose samples.
// A long idle/headset-off tail must not overwrite the first unexpected drop.
struct SupportEvent {
    SupportRecord sample{};ULONGLONG eventMs=0;std::uint64_t previousToken=0,rigEpoch=0,space=0;
    unsigned actor=0,weak=0,weapon=0,cancelFlags=0;
    bool focused=false,headTracked=false,leftGripTracked=false,rightGripTracked=false,rightAimTracked=false;
    math::Vec3 leftGrip{},rightGrip{};const char* resetReason=nullptr;
};
std::array<SupportEvent,512> supportEvents{};unsigned supportEventCount=0,supportEventTotal=0,supportEventDropped=0,supportForcedResets=0;
SupportEvent supportAttemptRecord{};bool supportAttemptActive=false;
std::array<unsigned,9> supportReleaseCounts{};
SupportEvent lastSupportSample{};
void RetainSupportEvent(const SupportEvent& event)noexcept {
    ++supportEventTotal;
    if(supportEventCount<supportEvents.size())supportEvents[supportEventCount++]=event;
    else ++supportEventDropped;
}
void ResetSupportGrip(const char* reason)noexcept {
    if(interactionTrace){auto& r=interactionTrace->interactions.support;r.observed=true;
        r.previousToken=lastSupportSample.sample.holding?lastSupportSample.sample.token:0;
        r.holding=false;r.token=0;r.reset={};const std::string_view label(reason);
        std::copy_n(label.begin(),std::min(label.size(),r.reset.size()-1),r.reset.begin());}
    if(lastSupportSample.sample.holding){
        auto event=lastSupportSample;event.eventMs=GetTickCount64();event.previousToken=event.sample.token;
        event.resetReason=reason;RetainSupportEvent(event);++supportForcedResets;
    }
    supportGrip.Reset();pumpAimContinuation.Reset();lastSupportSample.sample.holding=false;lastSupportSample.sample.token=0;
}

std::atomic<unsigned> equipCommands=0,vehicleInteractions=0,nextWeaponCommands=0,previousWeaponCommands=0;
std::atomic<unsigned> grenadeSamples=0,leftOnlyActions=0,rightOnlyActions=0,leftLossFire=0,untrackedFire=0,recoveryFireSuppressed=0;
BodyPositionCandidates bodyProfile{};interaction::RoomscaleFollow roomscale;
struct FollowRecord {ULONGLONG ms=0;math::Vec3 head{},body{},consumed{};float forward=0,strafe=0;};
std::array<FollowRecord,96> followRecords{};unsigned followRecordCount=0;ULONGLONG followRecordTime=0;
math::Vec3 consumedOffset{};std::atomic<unsigned> followSamples=0,bodyReadFailures=0;
FirstPersonPoseCandidates poseProfile{};RigCandidates rigProfile{};
using AnimationUpdateFn=std::uintptr_t(__thiscall*)(void*,float,float,bool);
using EvaluateFn=std::uintptr_t(__thiscall*)(void*,float,bool);
using PostEvaluateFn=std::uintptr_t(__thiscall*)(void*,float);
AnimationUpdateFn animationOriginal=nullptr;EvaluateFn evaluateOriginal=nullptr;PostEvaluateFn postOriginal=nullptr;
using WeaponWorldFn=std::uintptr_t(__thiscall*)(void*,void*);WeaponWorldFn weaponWorldOriginal=nullptr;
struct PhaseRecord {unsigned sequence=0,phase=0,thread=0,animation=0,world=0,skin=0,overrideSkin=0;bool overrideActive=false;std::uint64_t worldHash=0,skinHash=0,overrideHash=0;};
std::array<PhaseRecord,480> phaseRecords{};unsigned phaseRecordCount=0;
std::atomic_flag phaseRecordGate=ATOMIC_FLAG_INIT;
std::atomic<unsigned> animationCalls=0,animationInFlight=0,animationOverlap=0,phaseFailures=0;
std::atomic<ULONGLONG> phaseLastTime=0;
struct AnimationScope {unsigned animation=0,sequence=0;bool sample=false;};thread_local AnimationScope* animationScope=nullptr;

std::optional<RigSnapshot> observedRig;unsigned rigAttempts=0;
unsigned ikPreviewSolved=0,ikPreviewFailed=0,ikPreviewEdits=0,ikPreviewClamps=0;
float ikPreviewLengthError=0,ikPreviewTargetError=0;
void PreviewArmIk(const RigSnapshot& rig){
    const auto point=[](const math::Matrix4& m){return math::Vec3{m.values[3][0],m.values[3][1],m.values[3][2]};};
    const auto diff=[](math::Vec3 a,math::Vec3 b){return math::Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
    const auto distance=[&](const math::Matrix4& a,const math::Matrix4& b){auto d=diff(point(a),point(b));return std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);};
    const auto& native=rig.evaluatedWorld;
    for(unsigned sample=0;sample<12;++sample){
        auto left=native[rig.left.wrist],right=native[rig.right.wrist];
        // A bounded copied-pose exercise, not controller calibration or a write.
        const float angle=(float(sample)-5.5f)*.04f;
        math::Matrix4 roll{};for(unsigned n=0;n<4;++n)roll.values[n][n]=1;
        roll.values[0][0]=roll.values[1][1]=std::cos(angle);roll.values[0][1]=std::sin(angle);roll.values[1][0]=-std::sin(angle);
        left=interaction::Multiply(roll,left);right=interaction::Multiply(roll,right);
        left.values[3][0]+=.08f*std::sin(float(sample));right.values[3][2]+=.08f*std::cos(float(sample));
        const auto solved=interaction::SolveTrackedArms(rig.parents,native,rig.left,rig.right,
            {left,diff(point(native[rig.left.elbow]),point(native[rig.left.shoulder]))},
            {right,diff(point(native[rig.right.elbow]),point(native[rig.right.shoulder]))});
        if(!solved){++ikPreviewFailed;continue;}const auto plan=BuildRigPosePlan(rig,solved->writes);
        if(!plan){++ikPreviewFailed;continue;}++ikPreviewSolved;ikPreviewEdits+=unsigned(plan->edits.size());
        auto posed=native;for(const auto& write:solved->writes)posed[write.index]=write.transform;
        for(auto arm:{rig.left,rig.right})for(auto pair:{std::pair{arm.shoulder,arm.elbow},std::pair{arm.elbow,arm.wrist}})
            ikPreviewLengthError=std::max(ikPreviewLengthError,std::abs(distance(native[pair.first],native[pair.second])-distance(posed[pair.first],posed[pair.second])));
        for(unsigned side=0;side<2;++side){ikPreviewClamps+=solved->reachClamped[side]?1u:0u;ikPreviewTargetError=std::max(ikPreviewTargetError,solved->targetError[side]);}
    }
}

using PoseFn=std::uintptr_t(__thiscall*)(void*,void*);
PoseFn worldPoseOriginal=nullptr,rootPoseOriginal=nullptr;
struct PoseRecord {unsigned kind=0,thread=0,caller=0,owner=0,object=0;ULONGLONG ms=0;std::array<float,16> matrix{};};
std::array<PoseRecord,64> poseRecords{};unsigned poseRecordCount=0;
std::atomic_flag poseRecordGate=ATOMIC_FLAG_INIT;std::array<ULONGLONG,2> poseRecordTime{};
std::atomic<unsigned> poseCalls=0,poseRejected=0;

struct Anchor {std::atomic<unsigned> sequence=0,soldier=0,weak=0;std::atomic<std::uint64_t> space=0;std::atomic<float> yaw=0,consumedX=0,consumedZ=0,units=1;};Anchor anchor;
interaction::TrackedBodyAnchor bodyAnchor;float baseYaw=0,lastHandYaw=0,lastHeadYaw=0;
std::atomic<unsigned> aimSamples=0,baseViews=0;
// Request samples and snapshot outcomes, not native charge/throw acknowledgements.
std::atomic<unsigned> expackReloadRequests=0,expackReloadZoomRoutes=0,expackReloadUnresolved=0;
std::array<std::atomic<unsigned>,unsigned(ReloadStateStatus::ChangedDuringRead)+1> expackReloadReads{};
struct BaseRecord {float nativeYaw=0,nativePitch=0,sourceYaw=0,viewYaw=0,bodyYaw=0;bool sharedEye=false,inputMaskRead=false;math::Vec3 sourcePosition{},eyePosition{};unsigned weapon=0,player=0,soldier=0,weak=0,controlled=0,inputMask=0;ULONGLONG ms=0;std::uint64_t space=0;};
ViewBasisEvidence<BaseRecord> baseRecords;std::mutex baseRecordMutex;ULONGLONG baseRecordTime=0;
void PublishAnchor(unsigned soldier,unsigned weak,std::uint64_t space,float yaw,float units)noexcept {
    anchor.sequence.fetch_add(1,std::memory_order_acq_rel);
    anchor.soldier.store(soldier);anchor.weak.store(weak);anchor.space.store(space);anchor.yaw.store(yaw);anchor.consumedX.store(consumedOffset.x);anchor.consumedZ.store(consumedOffset.z);anchor.units.store(units);
    anchor.sequence.fetch_add(1,std::memory_order_release);
}
interaction::ControllerActions actions;
interaction::VehicleControllerActions vehicleActions;
ContextInteractEvidence contextInteractEvidence;
std::atomic<unsigned> vehicleRouteRejects=0,vehicleUnsupported=0,vehicleDriveCommits=0,vehicleCameraViews=0;
std::atomic<unsigned> vehicleCameraWaits=0,vehicleCameraUnsupportedFrames=0;
std::atomic<unsigned> vehicleSeatEntity=0,vehicleSeatEntry=0;
std::atomic<std::uint64_t> vehicleRouteFingerprint=0;
std::atomic<float> vehicleThrottle=0,vehicleSteer=0;
std::atomic<unsigned> vehicleThrottleNonzero=0,vehicleSteerNonzero=0,vehicleReadbackFailures=0;
std::atomic<float> vehicleThrottleMaximum=0,vehicleSteerMaximum=0;
struct VehicleGatherRecord {
    unsigned entity=0,entry=0,soldier=0,weak=0,mask=0,permissions=0;
    unsigned flagsWritten=0,flagsObserved=0;bool exitHeld=false,flagsVerified=false;
    std::uint64_t actorGeneration=0,seatGeneration=0,space=0,inputSequence=0;
    ULONGLONG ms=0;std::array<float,VehicleAxisCount> before{},written{},observed{};
    std::array<unsigned,VehicleAxisCount> actions{};unsigned role=0,consumerDriver=0,consumerCamera=0,consumerWeapons=0;bool verified=false;
};
std::array<VehicleGatherRecord,16> vehicleGatherRecords{};std::atomic<unsigned> vehicleGatherCount=0;
VehicleGatherRecord previousVehicleGather{};bool havePreviousVehicleGather=false;ULONGLONG vehicleGatherRecordedAt=0;
std::mutex vehicleViewMutex;
using VehicleViewState=interaction::VehicleCameraSample;
VehicleViewState vehicleView{};interaction::VehicleCameraAnchor vehicleCameraAnchor;
VehicleCameraMode vehicleCameraMode=VehicleCameraMode::Unavailable;
bool boatHeadAim=false,boatHeadFire=false,boatAimReady=false;
std::uint64_t boatAimSpace=0;
BoatAimBinding boatAimBinding{};VehicleRouteSnapshot boatAimSeat{};
interaction::VehicleHeadAim boatHeadPolicy;math::Pose boatAimReference{};
std::atomic<unsigned> boatAimSamples=0,boatAimRejects=0,boatAimCommits=0,boatAimFireCommits=0,boatAimCameraViews=0;
struct BoatAimRecord {ULONGLONG ms=0;std::uint64_t sequence=0;float yaw=0,pitch=0,yawError=0,pitchError=0,jointYaw=0,jointPitch=0,fire=0;};
std::array<BoatAimRecord,64> boatAimRecords{};unsigned boatAimRecordCount=0;ULONGLONG boatAimRecordedAt=0;
struct VehicleCameraRecord {
    interaction::VehicleTrackingOwner owner{};unsigned frame=0,appliedMask=0;ULONGLONG ms=0;
    runtime::TrackingFrame tracking{};math::Matrix4 native{},adjusted{};
    std::array<math::Matrix4,2> planned{},applied{};
};
std::array<VehicleCameraRecord,16> vehicleCameraRecords{};unsigned vehicleCameraRecordCount=0;
bool CameraEvidenceMatrix(const RenderViewCopy& copy,math::Matrix4& matrix)noexcept {
    engine::FrostbiteCameraInput source{};std::memcpy(&source.transform,copy.bytes.data()+0x50,64);
    std::memcpy(&source.nearPlane,copy.bytes.data()+0x1c,4);std::memcpy(&source.farPlane,copy.bytes.data()+0x20,4);source.worldUnitsPerMeter=1;
    const auto result=engine::CanonicalCamera(source);if(!result)return false;matrix=result->camera;return true;
}
void PublishVehicleView(const VehicleViewState& next,VehicleCameraMode mode=VehicleCameraMode::Unavailable)noexcept {
    std::lock_guard lock(vehicleViewMutex);
    if(PublishVehicleCameraMode(vehicleView,vehicleCameraMode,next,mode))vehicleCameraAnchor.Reset();
}
std::optional<VehicleSeatProfile> cachedVehicleProfile;
CampaignVehicleBinding campaignVehicleBinding{};
VehicleShotBinding vehicleShotBinding{};VehicleShotObserver vehicleShotObserver;
VehicleReticleSourceBinding boatReticleBinding{};BoatReticleRuntime boatReticle;
#ifdef FVR_BC2_VEHICLE_MOUNTED_PROBE
VehicleMountedObserver vehicleMountedObserver;
bool vehicleMountedRegistered=false; // Install/Stop lane only.
#endif
std::uint64_t vehicleShotSpace=0; // Only the verified input thread reads/writes this.
std::optional<CampaignVehicleSnapshot> cachedCampaignVehicle;
ULONGLONG campaignVehicleRetryAt=0;
VehicleSeatIdentity cachedVehicleIdentity{};std::uint64_t cachedVehicleFingerprint=0;
unsigned cachedVehicleData=0,cachedEntryData=0;
GameplayInputLease gameplayInputLease;
auto& cachedInput=gameplayInputLease.input;auto& inputDeadline=gameplayInputLease.deadlineQpc;
bool Read(std::uintptr_t at,void* out,std::size_t n)noexcept {SIZE_T actual=0;return at>=0x10000&&at<=UINT32_MAX-n&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,n,&actual)&&actual==n;}
unsigned U32(std::uintptr_t at)noexcept {unsigned out=0;Read(at,&out,4);return out;}
bool InImage(unsigned at,unsigned n)noexcept{return at>=image&&std::uint64_t(at)+n<=image+imageSize;}
enum class OwnerRole : unsigned {OnFoot,Vehicle,AttachedSoldier};
struct Owner {unsigned manager=0,player=0,weak=0,soldier=0,controlled=0,entry=0,router=0,cache=0,weapon=0,aim=0;WeaponEquipmentIdentity equipment{};bool foot=false;OwnerRole role=OwnerRole::Vehicle;
    // The read-only role must not change the existing control/publication identity.
    bool operator==(const Owner& b)const noexcept{return manager==b.manager&&player==b.player&&weak==b.weak&&soldier==b.soldier&&controlled==b.controlled&&entry==b.entry&&router==b.router&&cache==b.cache&&weapon==b.weapon&&aim==b.aim&&equipment==b.equipment&&foot==b.foot;}};
bool Type(unsigned object,const char* expected)noexcept {
    // Inspect the virtual reflection getter without invoking native code.
    const auto vt=U32(object);if(!InImage(vt,12))return false;const auto getter=U32(vt+8);
    std::array<unsigned char,6> code{};if(!InImage(getter,6)||!Read(getter,code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return false;
    unsigned info=0;std::memcpy(&info,code.data()+1,4);if(!InImage(info,8))return false;
    const auto metadata=U32(info+4),name=U32(metadata);char text[64]{};const auto n=std::strlen(expected)+1;
    return n<=sizeof(text)&&InImage(metadata,4)&&InImage(name,unsigned(n))&&Read(name,text,n)&&!std::memcmp(text,expected,n);
}
bool Resolve(Owner& o)noexcept {
    o.manager=U32(image+profile.gameplay.contextObject+8);if(U32(o.manager)!=image+profile.gameplay.managerVtable)return false;
    o.player=U32(o.manager+profile.gameplay.localPlayerOffset);if(!o.player)return false;
    const auto vt=U32(o.player);if(!InImage(vt,0x3c)||U32(vt+0x38)!=image+profile.controlledGetter)return false;
    unsigned char flags=0;if(!Read(o.player+0xccd,&flags,1)||!(flags&8))return false;
    o.weak=U32(o.player+profile.gameplay.soldierWeakOffset);const auto target=U32(o.weak);if(target<4)return false;o.soldier=target-4;
    if(!Type(o.soldier,"ClientSoldierEntity")||U32(o.soldier+0x220)!=o.player)return false;
    const auto controlled=U32(o.player+0xc68),attached=U32(o.player+0xc60);
    const bool useAttached=controlled==o.soldier&&attached;
    o.controlled=useAttached?attached:controlled;const auto slot=U32(o.player+(useAttached?0xc64:0xc6c));
    const auto begin=U32(o.controlled+0x7c),end=U32(o.controlled+0x80);
    if(!begin||end<begin||end-begin>256||(end-begin)%4||slot>=(end-begin)/4)return false;
    o.entry=U32(begin+slot*4);o.router=U32(o.entry+0x198);o.cache=U32(o.player+profile.gameplay.inputCacheOffset);
    if(U32(o.router)!=image+profile.gameplay.inputRouterVtable||U32(o.cache)!=image+profile.cacheVtable)return false;
    o.foot=o.controlled==o.soldier;
    o.role=o.foot?OwnerRole::OnFoot:(useAttached&&Type(o.entry,"ClientSoldierEntryComponent")?OwnerRole::AttachedSoldier:OwnerRole::Vehicle);
    if(o.foot){
        unsigned char soldierFlags=0;Read(o.soldier+0x114,&soldierFlags,1);
        const auto inventory=U32(o.soldier+((soldierFlags&1)?0x24c:0x248)),index=U32(inventory+0x14c);
        const auto first=U32(o.soldier+0x260),last=U32(o.soldier+0x264);
        if(first&&last>=first&&last-first<=256&&!((last-first)%4)&&index<(last-first)/4){
            const auto weapon=U32(first+4*index),data=U32(weapon+4),aim=U32(weapon+0x2c);
            if(Type(data,"SoldierWeaponData")&&aim&&U32(aim)==U32(data+0x9c)&&Type(U32(aim),"SoldierAimingSimulationData")){
                const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
                    [](void*,unsigned at,const char* name){return Type(at,name);}};
                const auto equipment=ReadWeaponEquipmentIdentity(memory,weapon);
                if(!equipment||equipment->data!=data)return false;
                o.weapon=weapon;o.aim=aim;o.equipment=*equipment;
            }
        }
    }
    return U32(o.manager+profile.gameplay.localPlayerOffset)==o.player&&U32(o.weak)==target;
}
bool BodyPosition(const Owner& owner,math::Vec3& out)noexcept {
    unsigned char flags=0;if(!Read(owner.soldier+0x114,&flags,1)||!(flags&1))return false;
    const auto authority=U32(owner.soldier+0x230),character=U32(authority+4);
    if(!character||character!=U32(owner.soldier+0x238)||!Type(character,"CharacterEntity")||U32(U32(character)+0x8c)!=image+bodyProfile.getter)return false;
    const auto proxy=U32(character+0xbc),body=U32(character+0x40);
    const auto position=proxy?proxy+0x110:U32(U32(body+0x30)+0x18)+0x30;
    if(!Read(position,&out,12)||!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z))return false;
    out.z=-out.z;return U32(authority+4)==character&&U32(owner.soldier+0x238)==character;
}
bool Writable(unsigned at,unsigned n)noexcept {
    MEMORY_BASIC_INFORMATION mbi{};if(!VirtualQuery(reinterpret_cast<void*>(at),&mbi,sizeof(mbi))||mbi.State!=MEM_COMMIT||(mbi.Protect&PAGE_GUARD))return false;
    const auto p=mbi.Protect&0xff;return (p==PAGE_READWRITE||p==PAGE_EXECUTE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_WRITECOPY)&&
        std::uintptr_t(mbi.BaseAddress)<=at&&std::uint64_t(at)+n<=std::uintptr_t(mbi.BaseAddress)+mbi.RegionSize;
}
struct Callback {Callback(){++active;}~Callback(){--active;}};
struct Scope {Owner owner{};InputOverride patch;bool used=false;std::uint64_t nativeTick=0;};thread_local Scope* scope=nullptr;
Owner lastOwner{};NativeInputEpochs nativeEpochs{};
auto& epoch=nativeEpochs.equipment;auto& inputEpoch=nativeEpochs.input;
std::uint64_t rigEpoch=0,renderEquipmentEpoch=0,nativeInputPauses=0;ULONGLONG lastTick=0;
#ifdef FVR_BC2_ORDINARY_RESOURCE_PAUSE_PROBE
struct NativeInputPauseProbe {bool ran=false,sameOwner=false;ULONGLONG begin=0,end=0;
    NativeInputEpochs before{},after{};} nativeInputPauseProbe;
#endif
// Ownership consumes current safety and separately identified original contact
// evidence. It never retimestamps the renderer's N-1 geometry as raw packet N.
interaction::HandClaimDropJournal<64> handDropJournal;
interaction::HandInteraction handOwnership{{},handDropJournal.Observer()};
interaction::HandInteractionSample handSample{};
interaction::HandInteractionKey handItem{};
struct HandEvidence {interaction::HandInteractionSample sample{};unsigned weapon=0;};
std::array<HandEvidence,32> handHistory{};unsigned handHistoryNext=0;
std::uint64_t handEquip=0,handIntent=0,handPhysical=0,handActor=0,handRig=0;
WeaponEquipmentIdentity handNativeEquipment{};
unsigned handInventory=0,handWeapon=0;std::int64_t handFrequency=0;
struct HandOwnershipRecord {
    std::uint64_t serial=0,rawGeneration=0;std::int64_t nowNs=0;
    interaction::HandInteractionOwner owner{};interaction::HandInteractionKey item{};
    unsigned weapon=0,sightPhase=0;
    std::optional<interaction::HandClaim> left{},right{};
    bool supportHolding=false,sightCommit=false;std::uint64_t supportToken=0,sightRequest=0,sightAck=0;
    const char* reason="transition";
};
std::array<HandOwnershipRecord,512> handEvents{};unsigned handEventCount=0;
std::uint64_t handChecks=0,handEventsTotal=0,handEventsDropped=0,handOverlap=0,handMissingSupport=0,handMissingSight=0,handLeaseFailures=0,handClaimRejected=0;
HandOwnershipRecord handPrevious{};
std::uint64_t gatherSightRequest=0,gatherSightAck=0;bool gatherSightCommit=false,handDirty=false;
const char* handReason="transition";const char* handLastReset=nullptr;
bool sightLeaseContinuation=false;
std::optional<interaction::HandClaim> handPreviousLeft;
interaction::HandInteractionResult handLifecycle{};
std::int64_t HandNanos(std::int64_t ticks)noexcept {
    if(ticks<=0||handFrequency<=0)return 0;
    const auto seconds=ticks/handFrequency;
    if(seconds>INT64_MAX/1000000000)return 0;
    return seconds*1000000000+std::int64_t((static_cast<long double>(ticks%handFrequency)*1000000000)/handFrequency);
}
std::int64_t HandTicks(std::int64_t nanos)noexcept {
    if(nanos<=0||handFrequency<=0)return 0;
    const auto seconds=nanos/1000000000;
    if(seconds>INT64_MAX/handFrequency)return 0;
    const auto whole=seconds*handFrequency;
    const auto part=std::int64_t((static_cast<long double>(nanos%1000000000)*handFrequency)/1000000000);
    return whole>INT64_MAX-part?0:whole+part;
}
void HandChanged(const char* reason)noexcept {handDirty=true;handReason=reason;}
bool RefreshHandProcessingTime()noexcept {
    // Consumers may have advanced the arbiter during this same Gather. Queued
    // ownership work uses its own processing time without renewing raw input,
    // renderer contact, sequence, or the original deadline.
    LARGE_INTEGER clock{};if(!QueryPerformanceCounter(&clock))return false;
    const auto now=HandNanos(clock.QuadPart);if(now<=0)return false;
    handSample.nowNs=now;return true;
}
void CancelPendingSight(std::uint64_t request)noexcept {
    if(request&&pendingMode&&modeRecords[*pendingMode].gesture==request){modeRecords[*pendingMode].cancelled=true;pendingMode.reset();}
}
void ReleaseLeftOwnership(interaction::HandClaimKind kind)noexcept {
    if(!RefreshHandProcessingTime())return;
    const auto claim=handOwnership.Current(interaction::InteractionHand::Left);
    if(claim&&claim->token.kind==kind){handOwnership.Release(handSample,claim->token);HandChanged("release");}
}
void ReleaseSightOwnership()noexcept {ReleaseLeftOwnership(interaction::HandClaimKind::Sight);}
void CancelWeaponVisibilityProbe()noexcept {
    if(!weaponVisibilityProbe)return;
    if(weaponVisibilityProbe->RequestId()){LARGE_INTEGER now{};QueryPerformanceCounter(&now);weaponVisibilityProbe->Cancel(8,HandNanos(now.QuadPart));}
    rigPublication::PublishWeaponVisibility({});weaponVisibilitySample.store({},std::memory_order_release);
}
void SuppressMagazineRecovery()noexcept;
void CancelPhysicalReload(PhysicalReloadResult* unpublished,unsigned sourceFlags,bool preserveMagazine)noexcept {
    if(physicalFixedContainer){auto safety=handSample;LARGE_INTEGER now{};
        if(QueryPerformanceCounter(&now))safety.nowNs=HandNanos(now.QuadPart);
        physicalFixedContainer->Cancel(safety,handOwnership);fixedContainerGripHeld=false;}
    if(physicalFeed){auto safety=handSample;LARGE_INTEGER now{};
        if(QueryPerformanceCounter(&now))safety.nowNs=HandNanos(now.QuadPart);
        physicalFeed->Suspend(safety,handOwnership);
        if(interactionTrace){auto& r=interactionTrace->interactions.feed;r={};r.observed=r.suspended=true;}}
    if(physicalPistolSlide){auto safety=handSample;LARGE_INTEGER now{};
        if(QueryPerformanceCounter(&now))safety.nowNs=HandNanos(now.QuadPart);
        physicalPistolSlide->Suspend(safety,handOwnership);}
    if(physicalPump&&(!ordinaryManualCycles||ordinaryShotgunEquipment)){auto current=handSample;LARGE_INTEGER clock{};
        if(QueryPerformanceCounter(&clock))current.nowNs=HandNanos(clock.QuadPart);physicalPump->Cancel(current,handOwnership);}
    if(physicalBolt&&(!ordinaryManualCycles||(!ordinaryShotgunEquipment&&physicalBoltCalibration&&
       handNativeEquipment.Asset()==physicalBoltCalibration->asset))){auto current=handSample;LARGE_INTEGER clock{};
        if(QueryPerformanceCounter(&clock))current.nowNs=HandNanos(clock.QuadPart);physicalBolt->Cancel(current,handOwnership);}
    // A fixture may finish AFTER Tick produced output in this same gather.
    // Revoke that unpublished capability/pulse before native cancellation;
    // otherwise the later PublishTracking would revive the old shell control.
    if(unpublished)*unpublished={};
    if(magazineDetached){magazineDetached->Invalidate();SuppressMagazineRecovery();}
    if(magazinePhysical){auto safety=handSample;LARGE_INTEGER now{};
        if(QueryPerformanceCounter(&now))safety.nowNs=HandNanos(now.QuadPart);
        if(preserveMagazine)magazinePhysical->Suspend(safety,handOwnership);
        else magazinePhysical->Cancel(safety,handOwnership);}
    if(physicalReload){auto safety=handSample;LARGE_INTEGER now{};if(QueryPerformanceCounter(&now))safety.nowNs=HandNanos(now.QuadPart);
        physicalReload->Cancel(safety,handOwnership,sourceFlags?PhysicalReloadCancelReason::RequestedInput:PhysicalReloadCancelReason::External,sourceFlags);
        if(interactionTrace){auto& r=interactionTrace->interactions.shell;r={};r.observed=r.externalCancel=true;r.bridgePhase=~0u;
            r.cancelReason=unsigned(sourceFlags?PhysicalReloadCancelReason::RequestedInput:PhysicalReloadCancelReason::External);r.sourceFlags=sourceFlags;}}
}
bool HolsterCacheCurrent(void*,const HolsterSuppressionRequest& r)noexcept {
    if(bodyHolster&&bodyHolster->DiagnosticDeadline()){
        LARGE_INTEGER clock{};if(!QueryPerformanceCounter(&clock))return false;const auto now=HandNanos(clock.QuadPart);
        if(now<bodyHolster->DiagnosticStart()||now>=bodyHolster->DiagnosticDeadline())return false;
    }
    Owner check{};return enabled.load(std::memory_order_acquire)&&scope&&scope->used&&scope->nativeTick==r.nativeTick&&
        Resolve(check)&&check==scope->owner&&check.foot&&check.cache==r.cache&&Writable(check.cache,InputBytes)&&
        r.owner==ReloadStateOwner{check.player,check.soldier,check.weak,check.weapon,rigEpoch,epoch,r.input.owner.space}&&
        r.input.owner.actor==((std::uint64_t(check.weak)<<32)|check.soldier)&&r.input.owner.actorGeneration==rigEpoch;
}
void SuppressMagazineRecovery()noexcept {
    if(!magazineDetached||!magazineDetached->BlocksActions()||!scope||!scope->used||!scope->owner.foot)return;
    const ReloadStateOwner owner{scope->owner.player,scope->owner.soldier,scope->owner.weak,scope->owner.weapon,rigEpoch,epoch,handSample.owner.space};
    auto safety=handSample;LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now))return;safety.nowNs=HandNanos(now.QuadPart);
    if(const auto request=magazineDetached->RecoveryDemand(owner,safety,scope->nativeTick,scope->owner.cache)){HolsterInputOverride patch;
        if(patch.Apply({reinterpret_cast<std::byte*>(scope->owner.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent})&&patch.Commit())++magazineRecoverySuppressionCommits;
        else ++magazineRecoverySuppressionFailures;
    }
}
BodyHolsterLifecycleLog bodyHolsterLifecycle;
void InvalidateBodyHolsterState(BodyHolsterLifecycleReason reason,bool preserve=false)noexcept {
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
    if(contextualTool){auto source=handSample;LARGE_INTEGER stamp{};
        if(QueryPerformanceCounter(&stamp))source.nowNs=HandNanos(stamp.QuadPart);
        contextualTool->Missing(source,handOwnership);}
#endif
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    if(chestKnife){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);chestKnife->Missing(HandNanos(stamp.QuadPart),handOwnership);}
#endif

    stowedAmmoReserveCache.Clear();
    if(!bodyHolster)return;auto source=handSample;LARGE_INTEGER now{};
    if(QueryPerformanceCounter(&now))source.nowNs=HandNanos(now.QuadPart);
    bodyHolsterLifecycle.Invalidate(*bodyHolster,reason,preserve,source,epoch);
    rigPublication::PublishOrdinaryEquipment({});
}
void InvalidateBodyHolster(bool trackingGap=false,BodyHolsterLifecycleReason reason=BodyHolsterLifecycleReason::External)noexcept {
    bodyHolsterSample.store({},std::memory_order_release);
    if(!bodyHolster)return;InvalidateBodyHolsterState(reason,trackingGap);holsterQueuedDraw.reset();rigPublication::PublishWeaponVisibility({});
    // Keep a current on-foot gather suppressed during tracking-loss recovery.
    // The old XR packet is NOT renewed and cannot grant free-hand pose authority.
    if(bodyHolster->BlocksActions()&&scope&&scope->used&&scope->owner.foot){
        BodyHolsterSample s;s.nativeOwner={scope->owner.player,scope->owner.soldier,scope->owner.weak,scope->owner.weapon,rigEpoch,epoch,handSample.owner.space};
        s.hand=handSample;LARGE_INTEGER now{};QueryPerformanceCounter(&now);s.hand.nowNs=HandNanos(now.QuadPart);
        s.nativeTick=scope->nativeTick;s.cache=scope->owner.cache;
        if(const auto request=bodyHolster->Demand(s)){HolsterInputOverride patch;
            if(patch.Apply({reinterpret_cast<std::byte*>(s.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent})&&patch.Commit())++holsterSuppressionCommits;
            else ++holsterSuppressionFailures;}
    }
}
void ResetHandOwnership(const char* reason,bool preserveMagazine=false)noexcept {
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    (void)selectedKnife::Missing();
#endif
    // Invalidating pose leases must not discard an original magazine merely
    // because tracking vanished. This can follow ClearReloadOwner in one tick.
    CancelPhysicalReload(nullptr,0,preserveMagazine||!std::strcmp(reason,"input_unavailable"));
    if(bodyInventory&&std::strcmp(reason,"identity")){
        const auto cause=!std::strcmp(reason,"input_unavailable")?BodyHolsterLifecycleReason::InputUnavailable:
            !std::strcmp(reason,"owner_unavailable")?BodyHolsterLifecycleReason::OwnerUnavailable:
            !std::strcmp(reason,"not_on_foot")?BodyHolsterLifecycleReason::NotOnFoot:
            !std::strcmp(reason,"session_stop")?BodyHolsterLifecycleReason::SessionStop:BodyHolsterLifecycleReason::External;
        InvalidateBodyHolster(cause==BodyHolsterLifecycleReason::InputUnavailable,cause);
        if(cause==BodyHolsterLifecycleReason::InputUnavailable)bodyInventory->SuspendInteraction();
        else bodyInventory->Cancel();}
    const bool occupied=handOwnership.Current(interaction::InteractionHand::Left).has_value()||
        handOwnership.Current(interaction::InteractionHand::Right).has_value();
    handOwnership.Reset();handHistory={};handHistoryNext=0;
    if(occupied||!handLastReset||std::strcmp(handLastReset,reason))HandChanged(reason);
    handLastReset=reason;
}
void RecordHandOwnership()noexcept {
    if(!twoHandGrip)return;
    LARGE_INTEGER eventClock{};QueryPerformanceCounter(&eventClock);
    HandOwnershipRecord row;row.serial=++handChecks;row.rawGeneration=handSample.sequence;row.nowNs=HandNanos(eventClock.QuadPart);
    row.owner=handSample.owner;row.item=handItem;row.weapon=handWeapon;
    row.left=handOwnership.Current(interaction::InteractionHand::Left);row.right=handOwnership.Current(interaction::InteractionHand::Right);
    row.supportHolding=lastSupportSample.sample.holding;row.supportToken=lastSupportSample.sample.token;
    row.sightPhase=unsigned(sightPhase);row.sightRequest=gatherSightRequest;row.sightAck=gatherSightAck;row.sightCommit=gatherSightCommit;row.reason=handReason;
    const bool supportClaim=row.left&&row.left->token.kind==interaction::HandClaimKind::WeaponSupport;
    const bool sightClaim=row.left&&row.left->token.kind==interaction::HandClaimKind::Sight;
    const bool sightActive=sightPhase!=interaction::SightFlipPhase::Idle;
    if(row.supportHolding&&sightActive)++handOverlap;
    if(row.supportHolding!=supportClaim)++handMissingSupport;
    if(sightActive!=sightClaim)++handMissingSight;
    for(const auto* claim:{&row.left,&row.right})if(*claim&&(*claim)->deadlineNs<=row.nowNs)++handLeaseFailures;
    const auto id=[](const std::optional<interaction::HandClaim>& claim){return claim?claim->token.id:0;};
    const bool changed=id(row.left)!=id(handPrevious.left)||id(row.right)!=id(handPrevious.right)||
        row.supportHolding!=handPrevious.supportHolding||row.supportToken!=handPrevious.supportToken||row.sightPhase!=handPrevious.sightPhase;
    if(changed||handDirty||gatherSightRequest||gatherSightAck||gatherSightCommit){
        ++handEventsTotal;if(handEventCount<handEvents.size())handEvents[handEventCount++]=row;else ++handEventsDropped;
    }
    handPrevious=row;handDirty=false;handReason="transition";
}
const HandEvidence* FindHandEvidence(std::uint64_t generation,unsigned weapon)noexcept {
    for(const auto& value:handHistory)if(value.sample.sequence==generation&&value.sample.owner==handSample.owner&&value.weapon==weapon)return &value;
    return nullptr;
}
bool OwnLeft(interaction::HandClaimKind kind,bool acquire,std::uint64_t generation,std::int64_t deadline,unsigned weapon,
    const interaction::SupportGripResult* supportContinuation=nullptr,bool cancelling=false)noexcept {
    if(!RefreshHandProcessingTime())return false;
    const auto source=FindHandEvidence(generation,weapon);
    const auto gun=handOwnership.Current(interaction::InteractionHand::Right);
    if(!source||!gun){if(acquire){++handClaimRejected;HandChanged("claim_rejected");}return false;}
    const interaction::HandContactProof proof{
        {kind==interaction::HandClaimKind::Sight?3ull:2ull,kind==interaction::HandClaimKind::Sight?handEquip:std::uint64_t(weapon)},
        generation,std::min(source->sample.deadlineNs,HandNanos(deadline)),true};
    const auto left=handOwnership.Current(interaction::InteractionHand::Left);
    // Ammo pickup and original pending-item reservations never transfer to a support/sight claim.
    if(left&&left->token.kind==interaction::HandClaimKind::AmmoObject)return false;
    const bool continuing=!acquire&&supportContinuation&&kind==interaction::HandClaimKind::WeaponSupport&&
        interaction::SupportLeaseContinuationEligible(handPreviousLeft,handLifecycle,handSample,handItem,
            lastSupportSample.sample.token,*supportContinuation,left,gun,source->sample,proof,cancelling);
    acquire=acquire||continuing;
    interaction::HandInteractionResult result;
    if(left&&left->token.kind==kind)
        result=handOwnership.RenewFrom(handSample,source->sample,left->token,proof);
    else if(acquire){
        interaction::HandClaimRequest request{handSample.owner,interaction::InteractionHand::Left,kind,handItem,proof,++handIntent,gun->token.id};
        result=left?handOwnership.TransferFrom(handSample,source->sample,left->token,request):handOwnership.AcquireFrom(handSample,source->sample,request);
    }else return false;
    if(!result.accepted){++handClaimRejected;HandChanged("claim_rejected");}
    if(result.accepted&&(!left||left->token.kind!=kind))HandChanged(continuing?"support_lease_reservation":"acquire");
    return result.accepted;
}
bool ReserveAcknowledgedSight()noexcept {
    if(!RefreshHandProcessingTime())return false;
    const auto gun=handOwnership.Current(interaction::InteractionHand::Right);
    if(!gun||handOwnership.Current(interaction::InteractionHand::Left))return false;
    // An exact native acknowledgement continues an already dispatched gesture.
    // This is a new current-input reservation, never resurrected old geometry.
    const interaction::HandContactProof proof{{3,handEquip},handSample.sequence,handSample.deadlineNs,true};
    const interaction::HandClaimRequest request{handSample.owner,interaction::InteractionHand::Left,
        interaction::HandClaimKind::Sight,handItem,proof,++handIntent,gun->token.id};
    const bool accepted=handOwnership.Acquire(handSample,request).accepted;
    if(accepted)HandChanged("native_ack_reservation");
    else{++handClaimRejected;HandChanged("claim_rejected");}
    return accepted;
}
void PrepareTrackedBodyAnchor(const Owner& owner,const interaction::InputFrame& input,interaction::ActionOutput& out,float nativeYaw)noexcept {
        const auto prepared=bodyAnchor.Update((std::uint64_t(owner.weak)<<32)|owner.soldier,
            input,nativeYaw,lastHeadYaw,out.turnDegrees);
        if(!prepared)return;
        baseYaw=prepared->yaw;if(prepared->referenceChanged)lastHandYaw=lastHeadYaw=0;
        if(out.turnDegrees)++turns;
        if(bodyFollow&&out.active){
            math::Vec3 body{};const auto localHead=math::MakeRelativePose(input.referenceHead,input.head);
            if(localHead&&BodyPosition(owner,body)){
                const float units=input.worldUnitsPerMeter;body.x/=units;body.y/=units;body.z/=units;
                const auto state=roomscale.Update({(std::uint64_t(owner.weak)<<32)|owner.soldier,input.spaceGeneration,GetTickCount64(),localHead->position,body,baseYaw-3.141592653589793f,std::hypot(out.forward,out.strafe)>.001f});
                consumedOffset=state.consumedLocalMeters;
                // BC2 adapter calibration: about 6 metres/second at full native
                // on-foot input. The feedback loop uses actual collision motion.
                if(state.valid&&state.driving){out.forward=RoomscaleNativeAxis(state.forwardMetersPerSecond);out.strafe=RoomscaleNativeAxis(state.strafeMetersPerSecond);++followSamples;}
                const auto now=GetTickCount64();if(followRecordCount<followRecords.size()&&now-followRecordTime>=100){
                    followRecords[followRecordCount++]={now,localHead->position,body,consumedOffset,out.forward,out.strafe};followRecordTime=now;
                }
            }else{roomscale.Suspend();++bodyReadFailures;}
        }
 }
void SelectOrdinaryCycle(const Owner& owner,const interaction::InputFrame& input)noexcept {
    ordinarySelectionReady=false;ordinaryTrackingRecovery=false;
    const auto* retainedShotgun=ordinaryShotgunEquipment;ordinaryShotgunEquipment=nullptr;
    if(!ordinaryManualCycles)return;
    const auto asset=owner.equipment.Asset();
    const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
    const Bc2BoltCalibration* boltEquipment=nullptr;
    const ShotgunReloadEquipmentProfile* shotgunEquipment=nullptr;
    LARGE_INTEGER stamp{};if(!QueryPerformanceCounter(&stamp))return;
    const auto now=HandNanos(stamp.QuadPart);
    const auto frame=rigPublication::ReadBodyWeaponFrame(owner.soldier,owner.weak,owner.weapon);
    const auto selected=selectedMeshes.Read(current,now);
    if(frame&&frame->aimEvidence&&selected&&frame->ownerGeneration==rigEpoch&&
       frame->space==input.spaceGeneration&&frame->deadline>stamp.QuadPart){
        const auto& proof=*frame->aimEvidence;
        const auto end=std::find(proof.configurationPath.begin(),proof.configurationPath.end(),'\0');
        if(end!=proof.configurationPath.end()&&proof.configurationPathVerified&&proof.selectedRigVerified&&
           proof.equipment==owner.equipment&&proof.configurationSequence&&proof.configurationDeadlineNs>now){
            const std::string_view config{proof.configurationPath.data(),std::size_t(end-proof.configurationPath.begin())};
            const auto* equipment=FindShotgunEquipment(config,asset,proof.meshPath,proof.rigFingerprint);
            if(equipment&&equipment->pump&&ShotgunEquipmentSelected(*selected,current,equipment,now))shotgunEquipment=equipment;
            const auto* bolt=FindBoltEquipment(config,asset,proof.meshPath,proof.rigFingerprint);
            if(bolt&&BoltEquipmentSelected(*selected,current,*bolt,now))boltEquipment=bolt;
        }
    }
    // Preserve the selected mechanism on a publication gap. A new owner still
    // requires its exact Config/mesh/rig join before any physical adapter changes.
    if(current==ordinaryAdapterOwner){
        if(!shotgunEquipment&&!boltEquipment){
            shotgunEquipment=retainedShotgun;
            if(!shotgunEquipment&&physicalBoltCalibration&&physicalBoltCalibration->asset==asset)
                boltEquipment=physicalBoltCalibration.get();
        }
    }
    if(!shotgunEquipment&&!boltEquipment)return;
    ordinaryShotgunEquipment=shotgunEquipment;
    const auto mode=shotgunEquipment?Bc2NativeCycleMode::Pump:Bc2NativeCycleMode::Bolt;
    const auto view=reloadFlowRuntime::ReadNativeCycleView(HandNanos(stamp.QuadPart));
    ordinarySelectionReady=ordinaryCycleSelection.Observe(mode,current,view);
    ordinaryTrackingRecovery=physicalPump&&physicalPump->BlocksFire()&&
        ordinaryCycleSelection.RoutesTrackingRecovery(mode,current);
    if(!view){
        ++ordinarySelectionReadGaps;
        if(ordinarySelectionReady){
            ++ordinarySelectionRetainedGaps;
            if(ordinaryBoltPlayer&&ordinaryBoltPlayer->OwnsGunCustody()){
                ++ordinarySelectionCustodyGaps;
                if(!ordinarySelectionFirstCustodyGap)ordinarySelectionFirstCustodyGap=input.generation;
            }
        }
    }
    if(ordinarySelectionReady)return;
    // The native selector also refuses debt/Ready/open calls. Physical custody
    // must have completed its own return before that native handoff is asked.
    const bool replacementActor=NativeCycleClientActorReplaced(ordinaryAdapterOwner,current);
    if(!replacementActor&&((ordinaryBoltPlayer&&(ordinaryBoltPlayer->BlocksFire()||ordinaryBoltPlayer->OwnsGunCustody()))||
       (physicalPump&&physicalPump->BlocksFire())))return;
    // A resolved replacement actor may request retirement despite old physical
    // debt. Native selection still requires fresh all-branch ownership plus an
    // excluded/drained callback boundary. No physical state changes on rejection.
    if(!reloadFlowRuntime::SelectNativeCycleMode(mode,current))return;
    ordinaryCycleSelection.Selected(mode,current);
    ordinarySelectionReady=true;
    if(ordinaryAdapterOwner!=current){
        // Selection succeeded only after exact old native/physical retirement.
        // New native item lifetime gets new physical references, never old grips.
        ordinaryShotgunEquipment=shotgunEquipment;
        if(boltEquipment){
            physicalBoltCalibration=std::make_shared<const Bc2BoltCalibration>(*boltEquipment);
            physicalBolt.emplace(physicalBoltCalibration,OrdinaryBoltApi());
            ordinaryBoltPlayer.emplace(physicalBoltCalibration);
        }
        if(shotgunEquipment)
            physicalPump.emplace(std::make_shared<const Bc2PumpCalibration>(*shotgunEquipment->pump),OrdinaryPumpApi());
        ordinaryAdapterOwner=current;physicalBoltCustody=interaction::BoltCustodyPhase::Idle;
    }
}
void PrepareOrdinaryBoltBeforeHands(const Owner& owner,const interaction::InputFrame& input,bool cancelling)noexcept {
    ordinaryBoltSample.reset();ordinaryBoltResult={};ordinaryBoltContact={};ordinaryBoltEarlyTick=false;
    if(!ordinaryManualCycles||!ordinaryBoltPlayer||!physicalBolt||ordinaryShotgunEquipment||
       !physicalBoltCalibration||owner.equipment.Asset()!=physicalBoltCalibration->asset)return;
    const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
    ordinaryBoltResult.tracking={true,current,handSample,physicalBoltCalibration};ordinaryBoltResult.blocksFire=true;
    ordinaryBoltContact=rigPublication::ReadBoltContact(current);
    if(!ordinarySelectionReady){RecordOrdinaryBoltPreparationSkip(1,input);return;}
    math::Vec3 actor{};if(!BodyPosition(owner,actor)){RecordOrdinaryBoltPreparationSkip(2,input);return;}
    rigPublication::Tracking tracked{input,owner.soldier,owner.weak,owner.weapon,rigEpoch,inputDeadline,baseYaw,consumedOffset,actor};
    auto body=rigPublication::BuildCurrentEyeBase(tracked);if(!body){RecordOrdinaryBoltPreparationSkip(3,input);return;}
    for(unsigned n=0;n<3;++n)body->values[3][n]/=input.worldUnitsPerMeter;
    const auto ammo=reloadFlowRuntime::ReadM95StockShotReserve();
    if(!RefreshHandProcessingTime()){RecordOrdinaryBoltPreparationSkip(4,input);return;}
    Bc2BoltPlayerInput player;player.owner=current;player.item=handItem;player.asset=physicalBoltCalibration->asset;
    player.mesh=physicalBoltCalibration->mesh;player.controls=&input;player.safety=handSample;
    player.body={current,handSample,input.referenceHead,*body,input.worldUnitsPerMeter};player.contact=ordinaryBoltContact;
    player.native=reloadFlowRuntime::ReadNativeCycleView(handSample.nowNs);player.ammunition=ammo;
    player.recovery=reloadFlowRuntime::ReadNativeCycleRecoveryView(handSample.nowNs);
    ordinaryBoltSample=ordinaryBoltPlayer->Prepare(player,handOwnership,handIntent);
    if(!ordinaryBoltSample){RecordOrdinaryBoltPreparationSkip(5,input);return;}
    ordinaryBoltSample->cancel=cancelling||ordinaryBoltSample->cancel;
    if(ordinaryBoltSample->reloadIntent||(ordinaryBoltSample->custodyTransfer&&ordinaryBoltSample->custodyTransfer->releaseDepartingGun)){
        // This explicit atomic transaction consumes the real release edge before
        // the generic arbiter can destroy its departing Gun+Support pair.
        ordinaryBoltResult=physicalBolt->Tick(*ordinaryBoltSample,handOwnership,handIntent);
        ordinaryBoltPlayer->Observe(ordinaryBoltResult);ordinaryBoltEarlyTick=true;
        physicalBoltCustody=ordinaryBoltResult.tracking.custody;
        if(ordinaryBoltResult.custodyChange&&ordinaryBoltResult.custodyChange->transaction.accepted)HandChanged("bolt_player_enter");
    }
}
void ObserveHandOwnership(const Owner& owner,const interaction::InputFrame& input,std::int64_t nowQpc,bool aimValid,bool cancelling,const std::optional<WeaponModeCommand>& family)noexcept {
    const bool boltHands=physicalBolt&&(!ordinaryManualCycles||(!ordinaryShotgunEquipment&&physicalBoltCalibration&&
        owner.equipment.Asset()==physicalBoltCalibration->asset));
    sightLeaseContinuation=false;handPreviousLeft.reset();handLifecycle={};
    handLastReset=nullptr;
    const auto actor=(std::uint64_t(owner.weak)<<32)|owner.soldier;
    const auto physical=family?std::uint64_t(family->persistent):std::uint64_t(owner.weapon);
    const auto inventory=family?family->inventory:0;
    if(handActor!=actor||handRig!=rigEpoch||handPhysical!=physical||handInventory!=inventory||
       (!family&&handNativeEquipment!=owner.equipment)){
        handActor=actor;handRig=rigEpoch;handPhysical=physical;handInventory=inventory;++handEquip;
        ResetHandOwnership("identity");
    }
    handNativeEquipment=owner.equipment;handWeapon=owner.weapon;handItem={physical,handEquip};
    const interaction::HandInteractionOwner identity{actor,rigEpoch,handEquip,input.spaceGeneration};
    LARGE_INTEGER handClock{};if(QueryPerformanceCounter(&handClock))nowQpc=handClock.QuadPart;
    const auto nowNs=HandNanos(nowQpc);
    bool knifeHands=false;
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    knifeHands=chestKnife&&bodyHolster&&(bodyHolster->TemporaryToolActive()||bodyHolster->Phase()==BodyHolsterPhase::Empty);
#endif

#ifdef FVR_BC2_CONTEXTUAL_TOOLS
    knifeHands=knifeHands||(contextualTool&&bodyHolster&&
        (bodyHolster->TemporaryToolActive()||bodyHolster->Phase()==BodyHolsterPhase::Empty));
#endif
    if(handSample.owner!=identity||handSample.sequence!=input.generation){
        handSample={identity,input.generation,(physicalReloadMode||physicalPump||physicalBolt||bodyInventory||weaponVisibilityProbe)?HandNanos(inputDeadline-handFrequency/10):nowNs,HandNanos(inputDeadline),nowNs,
            input.focused&&input.headValid,{input.hands[0].gripTracked&&bool(input.hands[0].active&interaction::Squeeze),
            input.hands[1].gripTracked&&input.hands[1].aimTracked&&aimValid}};
        handSample.released[0]=input.hands[0].squeeze<=.35f;
        if(boltHands||knifeHands)handSample.released[1]=input.hands[1].squeeze<=.35f;
        handHistory[handHistoryNext]={handSample,owner.weapon};handHistoryNext=(handHistoryNext+1)%handHistory.size();
    }else{
        handSample.nowNs=nowNs;handSample.focused=handSample.focused&&input.focused&&input.headValid;
        handSample.tracked[0]=handSample.tracked[0]&&input.hands[0].gripTracked&&(!(physicalReloadMode||physicalPump||physicalBolt)||bool(input.hands[0].active&interaction::Squeeze));
        handSample.tracked[1]=handSample.tracked[1]&&input.hands[1].gripTracked&&input.hands[1].aimTracked&&aimValid;
        handSample.released[0]=handSample.released[0]||input.hands[0].squeeze<=.35f;
        if(boltHands||knifeHands)handSample.released[1]=handSample.released[1]||input.hands[1].squeeze<=.35f;
    }
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    if(chestKnife)chestKnifeNative::SourceAdvanced(handSample);
    selectedKnife::SourceAdvanced(handSample,inputEpoch);
#endif
    PrepareOrdinaryBoltBeforeHands(owner,input,cancelling);
    handPreviousLeft=handOwnership.Current(interaction::InteractionHand::Left);
    const bool nativeGunHeld=!knifeHands&&handSample.focused&&handSample.tracked[1]&&(!boltHands||
        (physicalBoltCustody!=interaction::BoltCustodyPhase::Manipulating&&!handSample.released[1]));
    const interaction::HandContactProof grip{{1,handEquip},handSample.sequence,handSample.deadlineNs,true};
    const auto previousRight=handOwnership.Current(interaction::InteractionHand::Right);
    // Renew only the exact currently equipped root hold while incorporating a
    // newer overlapping input. Expiring first would discard continuous custody
    // before the same call supplies this original fresh semantic contact.
    // The custody adapter may own either hand. Its current mapped proof must
    // reach the same atomic root continuation before the ordinary expiry pass.
    const auto custodyLifecycle=ordinaryBoltSample&&physicalBolt&&!ordinaryBoltEarlyTick?
        physicalBolt->UpdateGunHold(*ordinaryBoltSample,handOwnership):std::nullopt;
    handLifecycle=custodyLifecycle?*custodyLifecycle:
        nativeGunHeld&&previousRight&&previousRight->token.kind==interaction::HandClaimKind::GunHold&&
        previousRight->token.item==handItem?
        handOwnership.UpdateAndRenewGunHold(handSample,previousRight->token,grip).lifecycle:handOwnership.Update(handSample);
    if(!handLifecycle.inputValid)return;
    sightLeaseContinuation=interaction::SightLeaseContinuationEligible(
        handPreviousLeft,handLifecycle,handSample,handItem,cancelling,input.hands[0].squeeze);
    // BC2 still owns an equipped gun; right grip is not a physical draw gesture.
    // This claim only supplies ownership for the dependent off-hand interaction.
    if(nativeGunHeld){
        const auto right=handOwnership.Current(interaction::InteractionHand::Right);
        if(right)handOwnership.Renew(handSample,right->token,grip);
        else if(!bodyHolster||!bodyHolster->BlocksActions()){
            const interaction::HandClaimRequest request{handSample.owner,interaction::InteractionHand::Right,interaction::HandClaimKind::GunHold,handItem,grip,++handIntent,0};
            if(handOwnership.Acquire(handSample,request).accepted)HandChanged("gun_hold");
        }
    }
}

#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
private_grenade::Facts GrenadeFacts(const Owner& owner,const interaction::InputFrame& input,
    const interaction::ActionOutput& out,bool visible)noexcept {
    private_grenade::Facts f;f.source.input=input;f.source.semantic=out;
    LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);f.source.nowNs=HandNanos(stamp.QuadPart);
    f.source.observedNs=HandNanos(inputDeadline-handFrequency/10);f.source.deadlineNs=HandNanos(inputDeadline);
    f.hand=handSample;f.hand.nowNs=f.source.nowNs;f.handItem=handItem;
    f.left=handOwnership.Current(interaction::InteractionHand::Left);f.right=handOwnership.Current(interaction::InteractionHand::Right);
    f.display=bodyInventory?bodyInventory->Display(f.source.nowNs):std::nullopt;
    f.holsterPhase=bodyHolster?unsigned(bodyHolster->Phase()):0;f.visible=visible;
    float fire=-1;unsigned bits=0;const bool cache=Read(owner.cache+8+4*8,&fire,4)&&Read(owner.cache+0x9c,&bits,4);
    f.cacheQuiet=cache&&fire==0;
    auto& a=f.source.admission;Owner current{};
    a.onFoot=owner.foot;a.nativePlaying=(U32(owner.cache+0x98)&0x103u)==0x103u;
    a.bindingVerified=ordinaryGrenadeBindings&&weaponModeBindingVerified;
    a.ownerCurrent=Resolve(current)&&current==owner;
    a.nativeFireNeutral=f.cacheQuiet;a.nativeGrenadeNeutral=cache&&!(bits&(1u<<6));
    // Set only by the finite explicit trace/receiver contract, never by focused.
    a.menuOwnershipKnown=bool(ordinaryGrenade);a.menuClear=true;
    return f;
}
std::optional<private_grenade::Native> GrenadeNative(const Owner& owner,const interaction::InputFrame& input,std::int64_t now)noexcept {
    if((!ordinaryGrenade&&!playerGrenade)||(ordinaryGrenade&&!ordinaryGrenade->NeedsNative())||!ordinaryGrenadeBindings||!reloadStateBinding||!ordinaryGrenadeServer||!owner.foot||!input.spaceGeneration)return {};
    // Establish the actual actor lifetime before a saved target can make its
    // read fail after a scene/actor replacement. Equip changes are stock flow.
    if(playerGrenade)playerGrenade->Lifetime({owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration});
    if(playerGrenade&&!playerGrenade->NeedsNative())return {};
    const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
    return private_grenade::ReadNative(memory,*reloadStateBinding,*ordinaryGrenadeServer,unsigned(image),
        {owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration},input.generation,now,
        playerGrenade?playerGrenade->Original():ordinaryGrenade->Original(),playerGrenade?playerGrenade->Target():ordinaryGrenade->Target());
}
void GrenadeObserveCurrent(const Owner& owner,const interaction::InputFrame& input,const interaction::ActionOutput& out,bool visible,bool custody=true)noexcept {
    if(playerGrenade?!playerGrenade->Started():(!ordinaryGrenade||!ordinaryGrenade->Started()||!ordinaryGrenade->NeedsNative()))return;
    auto f=GrenadeFacts(owner,input,out,visible);f.custodyObserved=custody;const auto native=GrenadeNative(owner,input,f.source.nowNs);
    LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);f.source.nowNs=HandNanos(stamp.QuadPart);
    if(playerGrenade)playerGrenade->Observe(native,f);else ordinaryGrenade->Observe(native,f);
}
#endif

#include "Bc2AttachedSoldierObservation.inc"
// Observe native output on the original calling thread. No game/animation
// memory is modified; forwarding EAX also preserves callers that inspect it.
void ObservePose(unsigned kind,unsigned object,unsigned matrix,unsigned caller)noexcept {
    if(!observePoses||!enabled.load(std::memory_order_acquire))return;
    ++poseCalls;if(poseRecordGate.test_and_set(std::memory_order_acquire))return;
    struct Gate {~Gate(){poseRecordGate.clear(std::memory_order_release);}} gate;
    const auto now=GetTickCount64();if(poseRecordCount>=poseRecords.size()||now-poseRecordTime[kind]<100)return;
    Owner owner{};if(!Resolve(owner)||!owner.foot)return;
    if(kind==0){if(object!=owner.soldier)return;}
    else if(U32(object+4)!=owner.soldier||(U32(owner.soldier+0x3b8)!=object&&U32(owner.soldier+0x3d0)!=object))return;
    if(kind==1&&!observedRig&&rigAttempts<3){++rigAttempts;
        const RigMemory memory{nullptr,[](void*,std::uint32_t at,void* out,std::size_t n){return Read(at,out,n);}};
        observedRig=ReadFirstPersonRig(memory,owner.soldier,owner.weak);if(observedRig)PreviewArmIk(*observedRig);
    }
    PoseRecord record{kind,GetCurrentThreadId(),caller,owner.soldier,object,now,{}};
    if(!Read(matrix,record.matrix.data(),64)){++poseRejected;return;}
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col)if(!std::isfinite(record.matrix[row*4+col])){++poseRejected;return;}
    poseRecords[poseRecordCount++]=record;poseRecordTime[kind]=now;
}
std::uintptr_t __fastcall WorldPoseHook(void* self,void*,void* matrix){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=worldPoseOriginal(self,matrix);
    if(handPoses&&enabled.load(std::memory_order_acquire)){
        Owner owner{};math::Matrix4 camera{};
        const bool resolved=Resolve(owner);
        if(resolved)ObserveAttached(owner,AttachedPhase::WorldPose,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(matrix),caller);
        if(resolved&&owner.foot&&owner.soldier==reinterpret_cast<unsigned>(self)&&Read(reinterpret_cast<unsigned>(matrix),&camera,64)){
            for(unsigned row=0;row<4;++row)camera.values[row][3]=row==3?1.f:0.f;
            for(unsigned n=0;n<4;++n){camera.values[2][n]=-camera.values[2][n];camera.values[n][2]=-camera.values[n][2];}
            rigPublication::PublishNativeEye(owner.soldier,owner.weak,camera);
        }
    }
    ObservePose(0,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(matrix),caller);return result;
}
std::uintptr_t __fastcall RootPoseHook(void* self,void*,void* matrix){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=rootPoseOriginal(self,matrix);
    ObservePose(1,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(self)+16,caller);return result;
}
// Diagnostic only. Original animation runs once on its native thread. Cross-thread
// overlap is measured, never hidden by serializing or replaying native jobs.
void ObservePhase(unsigned phase)noexcept {
    if(!animationScope||!animationScope->sample||phaseRecordGate.test_and_set(std::memory_order_acquire))return;
    struct Release {~Release(){phaseRecordGate.clear(std::memory_order_release);}} release;
    if(phaseRecordCount>=phaseRecords.size())return;
    const auto anim=animationScope->animation,pose=U32(anim+0x54),sk=U32(anim+0x50),header=U32(pose+0x18),count=U32(sk+0x10);
    if(count<6||count>1024||U32(header)!=count||U32(pose+0x28)!=sk){++phaseFailures;return;}
    PhaseRecord r{};r.sequence=animationScope->sequence;r.phase=phase;r.thread=GetCurrentThreadId();r.animation=anim;
    r.world=U32(header+4);r.skin=U32(header+8);r.overrideSkin=U32(anim+0x31c);unsigned char flag=0;Read(anim+0x99c,&flag,1);r.overrideActive=flag!=0;
    const auto hash=[&](unsigned at)->std::uint64_t {
        std::array<std::byte,64*1024> buffer{};if(!Read(at,buffer.data(),count*64)){++phaseFailures;return 0;}
        std::uint64_t result=14695981039346656037ull;
        // Skip undefined SIMD padding; compare only the twelve affine scalars.
        for(unsigned i=0;i<count*64;++i)if((i%16)<12){result^=std::to_integer<unsigned char>(buffer[i]);result*=1099511628211ull;}return result;
    };
    r.worldHash=hash(r.world);r.skinHash=hash(r.skin);if(r.overrideSkin)r.overrideHash=hash(r.overrideSkin);
    phaseRecords[phaseRecordCount++]=r;
}
std::uintptr_t __fastcall AnimationHook(void* self,void*,float dt,float alpha,bool full){
    Callback callback;Owner owner{};
    const bool resolved=observePoses&&enabled.load(std::memory_order_acquire)&&!animationScope&&Resolve(owner);
    if(resolved)ObserveAttached(owner,AttachedPhase::Animation,reinterpret_cast<unsigned>(self));
    if(!resolved||!owner.foot||reinterpret_cast<unsigned>(self)!=owner.soldier+0x214||U32(U32(owner.soldier)+0x160)!=image+rigProfile.animationGetter)
        return animationOriginal(self,dt,alpha,full);
    unsigned char flags=0;Read(owner.soldier+0x475,&flags,1);if(!(flags&1))return animationOriginal(self,dt,alpha,full);
    AnimationScope local{U32(owner.soldier+0x3b4),++animationCalls,false};
    const auto now=GetTickCount64();auto last=phaseLastTime.load();
    local.sample=now-last>=100&&phaseLastTime.compare_exchange_strong(last,now);
    if(animationInFlight.fetch_add(1)!=0)++animationOverlap;
    struct Release {~Release(){animationScope=nullptr;--animationInFlight;}} release;animationScope=&local;
    ObservePhase(0);const auto result=animationOriginal(self,dt,alpha,full);ObservePhase(4);return result;
}
std::uintptr_t __fastcall EvaluateHook(void* self,void*,float dt,bool full){
    Callback callback;const auto result=evaluateOriginal(self,dt,full);
    if(animationScope&&animationScope->animation==reinterpret_cast<unsigned>(self))ObservePhase(1);return result;
}
std::uintptr_t __fastcall PostEvaluateHook(void* self,void*,float alpha){
    Callback callback;const auto result=postOriginal(self,alpha);
    if(animationScope&&animationScope->animation==reinterpret_cast<unsigned>(self))ObservePhase(2);return result;
}
std::uintptr_t __fastcall WeaponWorldHook(void* self,void*,void* matrix){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=weaponWorldOriginal(self,matrix);
    if(handPoses&&animationScope&&animationScope->animation==reinterpret_cast<unsigned>(self)&&caller==image+rigProfile.animationUpdate+0x85)rigPublication::RetargetWeapon(reinterpret_cast<unsigned>(self),matrix);
    if(animationScope&&animationScope->animation==reinterpret_cast<unsigned>(self))ObservePhase(3);return result;
}
// Observe only caller-owned results produced during the server shot path.
// Matching data/actor links identify candidates, not permission to alter fire.
std::uintptr_t __fastcall FireOriginHook(void* self,void*,void* matrix){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    const auto result=fireOriginOriginal(self,matrix);
    if(!enabled.load(std::memory_order_acquire)||(caller!=image+fireProfile.callerA&&caller!=image+fireProfile.callerB))return result;
    ++fireOriginCalls;if(fireRecordGate.test_and_set(std::memory_order_acquire))return result;
    struct Release {~Release(){fireRecordGate.clear(std::memory_order_release);}} release;
    Owner owner{};const auto effects=reinterpret_cast<unsigned>(self);
    if(!Resolve(owner))return result;
    ObserveAttached(owner,AttachedPhase::FireOrigin,effects,reinterpret_cast<unsigned>(matrix),caller);
    if(!owner.foot||!owner.weapon||!Type(effects,"ServerWeaponFiringEffects"))return result;
    const auto client=U32(owner.weapon+0x38),data=U32(effects+0x10),player=U32(effects+0x128),vt=U32(player);
    if(!Type(client,"ClientWeaponFiringEffects")||!Type(data,"WeaponFiringData")||data!=U32(client+0x10)||!InImage(vt,0x34)||U32(vt+0x30)!=image+fireProfile.serverControlledGetter)return result;
    const auto soldier=U32(player+0xc3c),callbackObject=U32(effects+0xd4);
    if(!Type(soldier,"ServerSoldierEntity")||U32(soldier+0xc)!=U32(owner.soldier+0xc)||U32(callbackObject+4)!=data)return result;
    const PlayerPairMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    const auto id=MatchServerPlayer(memory,unsigned(image)+fireProfile.serverContext,unsigned(image)+fireProfile.serverManagerVtable,owner.player,player);
    if(!id||U32(player+0xc6c)!=soldier)return result;
    ++fireOriginMatches;if(fireRecordCount>=fireRecords.size())return result;
    FireRecord row{caller,effects,player,soldier,owner.soldier,owner.weapon,data,GetCurrentThreadId(),GetTickCount64()};
    if(result!=reinterpret_cast<unsigned>(matrix)||!Read(result,row.matrix.data(),64)||!Read(client+0x30,row.clientEffects.data(),64)||!Read(effects+0x30,row.serverEffects.data(),64)){++fireOriginRejected;return result;}
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)if(!std::isfinite(row.matrix[r*4+c])||!std::isfinite(row.clientEffects[r*4+c])||!std::isfinite(row.serverEffects[r*4+c])){++fireOriginRejected;return result;}
    row.playerId=*id;row.equipment=owner.equipment;row.shot=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
    if(row.shot){const auto canonical=[](const std::array<float,16>& values){math::Matrix4 m{};std::memcpy(&m,values.data(),64);
        for(unsigned r=0;r<4;++r)m.values[r][3]=r==3?1.f:0.f;
        for(unsigned n=0;n<4;++n){m.values[2][n]=-m.values[2][n];m.values[n][2]=-m.values[n][2];}return m;};
        row.mappedShot=interaction::PlaceFireAtMuzzle(canonical(row.matrix),row.shot->trackedFlash,2*row.shot->unitsPerMetre);
    }
    row.after=row.matrix;fireRecords[fireRecordCount++]=row;return result;
}
struct ShotScope {Owner owner;unsigned effects=0,serverPlayer=0,serverSoldier=0;bool supported=false;std::optional<rigPublication::WeaponShotFrame> shot;unsigned shotToken=0;bool tokenValid=false;
    unsigned configuration=0;std::optional<NativeMuzzleConfig> nativeConfig;
    std::uint64_t invocation=0,observedHelperCallbacks=0;unsigned launchOrdinal=0;
    std::optional<std::array<float,16>> composedOutput;bool compositionWritten=false;
    std::optional<interaction::FiringPoseSample> eventPose;
};
thread_local ShotScope* clientShotScope=nullptr;thread_local ShotScope* serverShotScope=nullptr;
struct ProjectileLaunchRecord {
    std::uint64_t invocation=0,candidateGeneration=0,ownerGeneration=0,space=0;
    unsigned ordinal=0,effects=0,configuration=0,player=0,soldier=0,thread=0,caller=0,token=0;
    WeaponEquipmentIdentity equipment{};
    std::int64_t beforeQpc=0,afterQpc=0;
    bool helperResultMatches=false,ownerAndConfigCurrent=false,accepted=false,serverShootReturned=false;
    bool compositionWritten=false;std::optional<std::array<float,16>> composedOutput;
    std::optional<interaction::FiringPoseSample> eventPose;
    std::optional<ProjectileLaunchArguments> before,after;
};
std::array<ProjectileLaunchRecord,64> projectileLaunchRecords{};
std::atomic<unsigned> projectileLaunchCount=0,projectileLaunchCalls=0,projectileLaunchRejected=0,
    projectileLaunchDropped=0,projectileLaunchBusy=0,projectileLaunchReturnUnrecorded=0;
std::atomic_flag projectileLaunchGate=ATOMIC_FLAG_INIT;
bool projectileLaunchHookRemoved=false;
ServerFireObservation serverFireObservation;
std::atomic_flag serverFireObservationGate=ATOMIC_FLAG_INIT;
std::atomic<std::uint64_t> serverFireScopeStarts=0,serverFireScopeReturns=0,serverFireObservationBusy=0;
void ObserveServerFireReturned(const ShotScope& scope,std::int64_t begin,std::int64_t end,std::uint64_t returnedOrdinal)noexcept {
    // The scope already owns these copies. Do not add native reads after the
    // detailed launch prefix fills or infer input/shot success from this event.
    ServerFireObservationSample row;auto& k=row.key;
    k.player=scope.owner.player;k.soldier=scope.owner.soldier;k.weak=scope.owner.weak;
    k.serverPlayer=scope.serverPlayer;k.serverSoldier=scope.serverSoldier;k.effects=scope.effects;k.configuration=scope.configuration;
    k.equipment=scope.owner.equipment;k.candidatePresent=scope.shot.has_value();
    row.invocation=scope.invocation;row.returnedOrdinal=returnedOrdinal;row.helperCallbacks=scope.observedHelperCallbacks;
    row.beginQpc=begin;row.endQpc=end;row.beginNs=HandNanos(begin);row.token=scope.shotToken;row.tokenValid=scope.tokenValid;
    row.muzzleSupported=scope.supported;row.compositionWritten=scope.compositionWritten;row.eventPresent=scope.eventPose.has_value();
    if(scope.eventPose){row.eventGeneration=scope.eventPose->generation;row.eventDeadlineQpc=scope.eventPose->deadline;}
    if(scope.shot){const auto& shot=*scope.shot;k.candidateOwnerGeneration=shot.ownerGeneration;k.space=shot.space;
        row.candidateGeneration=shot.generation;row.candidateDeadlineQpc=shot.deadline;row.controllerPresent=shot.controllerMapping.valid;
        if(row.controllerPresent)row.controller=shot.controllerMapping.inputEvidence;}
    if(serverFireObservationGate.test_and_set(std::memory_order_acquire)){++serverFireObservationBusy;return;}
    serverFireObservation.Observe(row);serverFireObservationGate.clear(std::memory_order_release);
}
void MarkServerShootReturned(std::uint64_t invocation)noexcept {
    if(!invocation)return;
    if(projectileLaunchGate.test_and_set(std::memory_order_acquire)){++projectileLaunchReturnUnrecorded;return;}
    for(unsigned n=0;n<projectileLaunchCount.load();++n)if(projectileLaunchRecords[n].invocation==invocation)
        projectileLaunchRecords[n].serverShootReturned=true;
    projectileLaunchGate.clear(std::memory_order_release);
}
std::string_view ReadWeaponAsset(unsigned weapon,std::array<char,64>& name)noexcept {
    if(!weapon||!Read(U32(U32(weapon+4)+0xc),name.data(),name.size())){name={};return {};}
    const auto end=static_cast<const char*>(std::memchr(name.data(),0,name.size()));
    if(!end){name={};return {};}
    for(auto at=name.data();at!=end;++at)if(static_cast<unsigned char>(*at)<32||static_cast<unsigned char>(*at)>126){name={};return {};}
    return {name.data(),std::size_t(end-name.data())};
}
const Bc2WeaponProfile* CurrentWeaponProfile(unsigned weapon)noexcept {
    std::array<char,64> name{};return FindWeaponProfile(ReadWeaponAsset(weapon,name));
}
unsigned MuzzleWeaponKind(unsigned weapon)noexcept {
    const auto item=CurrentWeaponProfile(weapon);return item?item->telemetryKind:0;
}
bool SupportedMuzzleShot(const Owner& owner,unsigned effects,unsigned config,
    const std::optional<rigPublication::WeaponShotFrame>& shot,std::optional<NativeMuzzleConfig>& observed)noexcept {
    observed=ReadNativeMuzzleConfig({nullptr,[](void*,unsigned at,void* out,std::size_t size){return Read(at,out,size);}},effects,config);
    if(!observed||!Type(observed->firingData,"WeaponFiringData")||!OrdinaryMuzzleConfig(*observed))return false;
    const auto item=CurrentWeaponProfile(owner.weapon);
    if(interaction::WeaponFeatureEnabled(item?&item->core:nullptr,interaction::WeaponFeature::TranslatedMuzzle))return true;
    return shot&&shot->muzzleBinding&&
        (shot->muzzleBinding->support?shot->muzzleBinding->support->equipment:shot->muzzleBinding->authored.equipment)==owner.equipment&&
        Type(observed->projectileData,"BulletEntityData")&&MatchesMuzzleEnrollment(*observed,*shot->muzzleBinding);
}
std::uintptr_t __fastcall ProjectileContextHook(void* self,void*,void* output){
    Callback callback;const auto original=[&]{return projectileContextOriginal(self,output);};
    const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());auto* current=serverShotScope;
    if(!enabled.load(std::memory_order_acquire)||!projectileLaunchBinding.verified||!current||
       caller!=projectileLaunchBinding.helperReturn||reinterpret_cast<unsigned>(self)!=current->effects)return original();
    ++projectileLaunchCalls;
    ++current->observedHelperCallbacks;
    if(projectileLaunchCount.load(std::memory_order_relaxed)>=projectileLaunchRecords.size()||current->launchOrdinal==UINT32_MAX){++projectileLaunchDropped;return original();}
    const auto returnSlot=reinterpret_cast<unsigned>(_AddressOfReturnAddress());
    const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto low=reinterpret_cast<unsigned>(tib->StackLimit),high=reinterpret_cast<unsigned>(tib->StackBase);
    const MuzzleReadMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t size){return Read(at,out,size);}};
    ProjectileLaunchRecord row;row.invocation=current->invocation;row.ordinal=++current->launchOrdinal;
    row.effects=current->effects;row.configuration=current->configuration;row.player=current->serverPlayer;
    row.soldier=current->serverSoldier;row.equipment=current->owner.equipment;row.thread=GetCurrentThreadId();row.caller=caller;row.token=current->shotToken;
    row.composedOutput=current->composedOutput;row.compositionWritten=current->compositionWritten;row.eventPose=current->eventPose;
    if(current->shot){row.candidateGeneration=current->shot->generation;row.ownerGeneration=current->shot->ownerGeneration;row.space=current->shot->space;}
    return ObserveProjectileContextHelper(original,[&]()noexcept{
        LARGE_INTEGER q{};QueryPerformanceCounter(&q);row.beforeQpc=q.QuadPart;
        row.before=ReadProjectileLaunchArguments(memory,projectileLaunchBinding,returnSlot,low,high);
    },[&](std::uintptr_t result)noexcept{
        // Copy the arguments immediately after the real helper, before other
        // observation reads. No ownership-bearing helper output is copied.
        row.after=ReadProjectileLaunchArguments(memory,projectileLaunchBinding,returnSlot,low,high);
        LARGE_INTEGER q{};QueryPerformanceCounter(&q);row.afterQpc=q.QuadPart;
        row.helperResultMatches=result==reinterpret_cast<unsigned>(output)&&row.after&&row.after->contextOutput==result;
        Owner now{};const auto config=ReadNativeMuzzleConfig(memory,current->effects,current->configuration);
        row.ownerAndConfigCurrent=serverShotScope==current&&Resolve(now)&&now==current->owner&&
            current->nativeConfig&&config&&*current->nativeConfig==*config;
        row.accepted=row.before&&row.after&&row.helperResultMatches&&row.ownerAndConfigCurrent&&current->tokenValid&&
            row.before->token==current->shotToken&&row.after->token==current->shotToken;
        if(!row.accepted)++projectileLaunchRejected;
        if(projectileLaunchGate.test_and_set(std::memory_order_acquire)){++projectileLaunchBusy;return;}
        const auto index=projectileLaunchCount.load(std::memory_order_relaxed);
        if(index<projectileLaunchRecords.size()){projectileLaunchRecords[index]=row;projectileLaunchCount.store(index+1,std::memory_order_release);}
        else ++projectileLaunchDropped;
        projectileLaunchGate.clear(std::memory_order_release);
    });
}
std::uintptr_t __fastcall ServerShootHook(void* self,void*,void* config,float dt,unsigned primary,void* context){
    Callback callback;Owner owner{};const auto effects=reinterpret_cast<unsigned>(self);
    const auto passthrough=[&]{return serverShootOriginal(self,config,dt,primary,context);};
    if(!enabled.load(std::memory_order_acquire)||!Resolve(owner))return passthrough();
    ObserveAttached(owner,AttachedPhase::ServerFire,effects,reinterpret_cast<unsigned>(config),reinterpret_cast<unsigned>(_ReturnAddress()));
    if(!owner.foot||!owner.weapon||!Type(effects,"ServerWeaponFiringEffects"))return passthrough();
    const auto client=U32(owner.weapon+0x38),data=U32(effects+0x10),player=U32(effects+0x128),vt=U32(player);
    if(!Type(client,"ClientWeaponFiringEffects")||!Type(data,"WeaponFiringData")||data!=U32(client+0x10)||!InImage(vt,0x34)||U32(vt+0x30)!=image+fireProfile.serverControlledGetter)return passthrough();
    const auto soldier=U32(player+0xc3c),callbackObject=U32(effects+0xd4);
    if(!Type(soldier,"ServerSoldierEntity")||U32(soldier+0xc)!=U32(owner.soldier+0xc)||U32(callbackObject+4)!=data||U32(player+0xc6c)!=soldier)return passthrough();
    const PlayerPairMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    if(!MatchServerPlayer(memory,unsigned(image)+fireProfile.serverContext,unsigned(image)+fireProfile.serverManagerVtable,owner.player,player))return passthrough();
    ShotScope local{owner,effects,player,soldier};local.configuration=reinterpret_cast<unsigned>(config);
    local.invocation=shotInvocationSerial.fetch_add(1,std::memory_order_relaxed)+1;
    local.shot=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
    local.supported=SupportedMuzzleShot(owner,effects,local.configuration,local.shot,local.nativeConfig);
    local.tokenValid=Read(reinterpret_cast<unsigned>(context)+0x1c,&local.shotToken,4);
    auto* previous=serverShotScope;serverShotScope=&local;struct Restore {ShotScope* previous;~Restore(){serverShotScope=previous;}} restore{previous};
    ++serverFireScopeStarts;
    LARGE_INTEGER begin{},end{};if(!QueryPerformanceCounter(&begin))begin.QuadPart=0;
    const auto result=passthrough();if(!QueryPerformanceCounter(&end))end.QuadPart=0;
    const auto returnedOrdinal=serverFireScopeReturns.fetch_add(1,std::memory_order_relaxed)+1;
    MarkServerShootReturned(local.invocation);ObserveServerFireReturned(local,begin.QuadPart,end.QuadPart,returnedOrdinal);return result;
}
std::uintptr_t __fastcall ClientShootHook(void* self,void*,void* config,float dt,unsigned primary,void* context){
    Callback callback;Owner owner{};const auto effects=reinterpret_cast<unsigned>(self);
    const bool resolved=enabled.load(std::memory_order_acquire)&&Resolve(owner);
    if(resolved)ObserveAttached(owner,AttachedPhase::ClientFire,effects,reinterpret_cast<unsigned>(config),reinterpret_cast<unsigned>(_ReturnAddress()));
    // Observe matched mounted-gun callbacks without entering the infantry
    // transform/shot-authority path. The original is called exactly once.
    if(resolved&&!owner.foot&&boatReticleBinding.verified){
        const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
        const auto sample=[&]{LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
            Owner current{};if(!Resolve(current)||current!=owner||current.foot)return std::optional<BoatReticleShotWitness>{};
            return boatReticle.BeginShot(memory,boatReticleBinding,boatAimBinding,(std::uint64_t(owner.weak)<<32)|owner.soldier,owner.controlled,owner.entry,
                effects,reinterpret_cast<unsigned>(config),caller,HandNanos(stamp.QuadPart));};
        if(const auto before=sample()){
            const auto result=clientShootOriginal(self,config,dt,primary,context);
            if(const auto after=sample())boatReticle.EndShot(*before,*after);
            return result;
        }
    }
    if(resolved&&!owner.foot&&vehicleShotBinding.verified){
        const auto source=vehicleShotObserver.Source();
        if(source){
            const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
            const VehicleRouteBinding binding{unsigned(image+profile.gameplay.managerVtable),unsigned(image+profile.gameplay.inputRouterVtable),unsigned(image+profile.cacheVtable),true};
            const VehicleShotCall call{effects,reinterpret_cast<unsigned>(config),reinterpret_cast<unsigned>(_ReturnAddress()),reinterpret_cast<unsigned>(context)};
            const auto sample=[&]{
                LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const auto now=HandNanos(stamp.QuadPart);
                VehicleShotSample rejected;rejected.reason=VehicleShotReason::Owner;rejected.nowNs=now;
                if(!vehicleShotObserver.Current(*source)){rejected.reason=VehicleShotReason::Changed;return rejected;}
                Owner current{};const auto& expected=source->owner;
                if(!Resolve(current)||current.foot||current.manager!=expected.manager||current.player!=expected.player||
                   current.soldier!=expected.soldier||current.weak!=expected.weak)return rejected;
                const auto routes=ReadVehicleRoutes(memory,binding,expected);
                if(routes.status!=VehicleRouteStatus::Okay)return rejected;
                auto observed=ReadVehicleShot(memory,vehicleShotBinding,*source,routes.snapshot,call,now);
                QueryPerformanceCounter(&stamp);observed.nowNs=HandNanos(stamp.QuadPart);
                if(!vehicleShotObserver.Current(*source))observed.reason=VehicleShotReason::Changed;
                if(observed.reason==VehicleShotReason::Okay&&(observed.nowNs<source->observedNs||observed.nowNs>=source->deadlineNs))
                    observed.reason=VehicleShotReason::Stale;
                return observed;
            };
            const auto before=sample();const auto result=clientShootOriginal(self,config,dt,primary,context);
            const auto after=sample();vehicleShotObserver.Record(*source,call,before,after,VehicleShotQueueGrowth(memory,before,after));
            return result;
        }
    }
    if(!resolved||!owner.foot||!owner.weapon||
       U32(owner.weapon+0x38)!=effects||!Type(effects,"ClientWeaponFiringEffects")||U32(U32(effects)+0x4c)!=image+fireProfile.clientShoot)
        return clientShootOriginal(self,config,dt,primary,context);
    const PlayerPairMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    const auto paired=FindServerPlayer(memory,unsigned(image)+fireProfile.serverContext,unsigned(image)+fireProfile.serverManagerVtable,owner.player);
    const auto player=paired.value_or(0),soldier=player?U32(player+0xc3c):0,vt=player?U32(player):0;
    const bool campaign=player&&InImage(vt,0x34)&&U32(vt+0x30)==image+fireProfile.serverControlledGetter&&
        Type(soldier,"ServerSoldierEntity")&&U32(soldier+0xc)==U32(owner.soldier+0xc)&&U32(player+0xc6c)==soldier;
    ++clientShotCalls;ShotScope local{owner,effects,player,soldier};local.configuration=reinterpret_cast<unsigned>(config);
    local.invocation=shotInvocationSerial.fetch_add(1,std::memory_order_relaxed)+1;
    local.shot=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
    local.supported=campaign&&SupportedMuzzleShot(owner,effects,local.configuration,local.shot,local.nativeConfig);
    local.tokenValid=Read(reinterpret_cast<unsigned>(context)+0x1c,&local.shotToken,4);auto* previous=clientShotScope;clientShotScope=&local;
    struct Restore {ShotScope* previous;~Restore(){clientShotScope=previous;}} restore{previous};
    return clientShootOriginal(self,config,dt,primary,context);
}
std::uintptr_t __fastcall MatrixCopyHook(void* self,void*,const void* source){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    const auto result=matrixCopyOriginal(self,source);
    if(!enabled.load(std::memory_order_acquire)||!clientShotScope||(caller!=image+fireProfile.clientCopyA&&caller!=image+fireProfile.clientCopyB))return result;
    ++clientShotCopies;if(fireRecordGate.test_and_set(std::memory_order_acquire))return result;
    struct Release {~Release(){fireRecordGate.clear(std::memory_order_release);}} release;
    if(fireRecordCount>=fireRecords.size())return result;Owner now{};const auto& shotScope=*clientShotScope;
    if(!Resolve(now)||now!=shotScope.owner||U32(now.weapon+0x38)!=shotScope.effects){++clientShotRejected;return result;}
    FireRecord row{};row.phase=1;row.clientPath=true;row.caller=caller;row.effects=shotScope.effects;row.clientSoldier=now.soldier;row.weapon=now.weapon;row.playerId=U32(now.player+0x154);row.data=U32(shotScope.effects+0x10);row.thread=GetCurrentThreadId();row.ms=GetTickCount64();
    if(result!=reinterpret_cast<unsigned>(self)||!Read(result,row.matrix.data(),64)||!Read(shotScope.effects+0x30,row.clientEffects.data(),64)){++clientShotRejected;return result;}
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)if(!std::isfinite(row.matrix[r*4+c])||!std::isfinite(row.clientEffects[r*4+c])){++clientShotRejected;return result;}
    row.equipment=now.equipment;row.shot=rigPublication::ReadWeaponShotFrame(now.soldier,now.weak,now.weapon);
    if(row.shot){const auto canonical=[](const std::array<float,16>& values){math::Matrix4 m{};std::memcpy(&m,values.data(),64);
        for(unsigned r=0;r<4;++r)m.values[r][3]=r==3?1.f:0.f;
        for(unsigned n=0;n<4;++n){m.values[2][n]=-m.values[2][n];m.values[n][2]=-m.values[n][2];}return m;};
        row.mappedShot=interaction::PlaceFireAtMuzzle(canonical(row.matrix),row.shot->trackedFlash,2*row.shot->unitsPerMetre);
    }
    row.after=row.matrix;fireRecords[fireRecordCount++]=row;return result;
}
std::uintptr_t __fastcall ComposeHook(void* self,void*,void* output,const void* rhs){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=composeOriginal(self,output,rhs);
    if(!enabled.load(std::memory_order_acquire))return result;
    const bool client=caller==image+fireProfile.clientCompose;auto* current=client?clientShotScope:(caller==image+fireProfile.serverCompose?serverShotScope:nullptr);
    if(!current)return result;
    const bool recording=!fireRecordGate.test_and_set(std::memory_order_acquire);
    struct Release {bool held;~Release(){if(held)fireRecordGate.clear(std::memory_order_release);}} release{recording};
    Owner owner{};
    if(!Resolve(owner)||owner!=current->owner)return result;
    FireRecord row{};row.phase=2;row.clientPath=client;row.caller=caller;row.effects=current->effects;row.data=U32(current->effects+0x10);row.player=current->serverPlayer;row.serverSoldier=current->serverSoldier;row.clientSoldier=owner.soldier;row.weapon=owner.weapon;row.playerId=U32(owner.player+0x154);row.thread=GetCurrentThreadId();row.ms=GetTickCount64();row.shot=current->shot;row.weaponKind=MuzzleWeaponKind(owner.weapon);row.shotToken=current->shotToken;row.tokenValid=current->tokenValid;
    const auto at=reinterpret_cast<unsigned>(output);if(result!=at||!Read(at,row.matrix.data(),64)){++originWriteFailures;return result;}
    const auto canonical=[](const std::array<float,16>& values){math::Matrix4 m{};std::memcpy(&m,values.data(),64);for(unsigned r=0;r<4;++r)m.values[r][3]=r==3?1.f:0.f;
        for(unsigned n=0;n<4;++n){m.values[2][n]=-m.values[2][n];m.values[n][2]=-m.values[n][2];}return m;};
    row.after=row.matrix;row.equipment=owner.equipment;row.nativeMuzzleSupported=current->supported;row.nativeInvocation=current->invocation;
    LARGE_INTEGER q{};QueryPerformanceCounter(&q);
    const auto live=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
    row.observedQpc=q.QuadPart;
    if(live){row.observedCurrentGeneration=live->generation;row.observedCurrentDeadline=live->deadline;}
    row.observedCandidateUnexpired=row.shot&&live&&live->space==row.shot->space&&
        live->ownerGeneration==row.shot->ownerGeneration&&live->generation>=row.shot->generation&&q.QuadPart<row.shot->deadline;
    // Observation is separate from mappedShot, which participates in the
    // unchanged native-write gate below. Unsupported guns remain passthrough.
    if(row.observedCandidateUnexpired)row.observedMuzzleCandidate=interaction::PlaceFireAtMuzzle(
        canonical(row.matrix),row.shot->trackedFlash,2*row.shot->unitsPerMetre);
    const bool admittedNow=SupportedMuzzleShot(owner,current->effects,current->configuration,live,row.nativeConfig);
    row.nativeConfigUnchanged=current->nativeConfig&&row.nativeConfig&&*current->nativeConfig==*row.nativeConfig;
    const bool sameEnrollment=row.shot&&live&&bool(row.shot->muzzleBinding)==bool(live->muzzleBinding)&&
        (!row.shot->muzzleBinding||MuzzleBindingContinues(*row.shot->muzzleBinding,*live->muzzleBinding,HandNanos(q.QuadPart)));
    row.nativeMuzzleSupported=current->supported&&admittedNow&&row.nativeConfigUnchanged&&sameEnrollment;
    if(row.nativeMuzzleSupported&&row.tokenValid&&row.shot&&live&&live->space==row.shot->space&&live->ownerGeneration==row.shot->ownerGeneration){
        std::lock_guard lock(shotPoseMutex);
        const auto& binding=row.shot->muzzleBinding;
        const MuzzleEventSource source{owner.equipment,binding?binding->profile:nullptr,row.nativeConfig->firingData,
            row.nativeConfig->config,binding?(binding->support?binding->support->owner.equipmentGeneration:binding->authored.owner.equipmentGeneration):0};
        const auto eventDeadline=binding?std::min(row.shot->deadline,HandTicks(MuzzleBindingDeadlineNs(*binding))):row.shot->deadline;
        row.eventPose=ResolveMuzzleEvent(shotHistory,muzzleHistorySource,source,
            {(std::uint64_t(owner.weak)<<32)|owner.soldier,row.shot->ownerGeneration,owner.weapon,row.shot->space,row.shotToken},
            {row.shot->trackedFlash,row.shot->generation,eventDeadline},q.QuadPart);
        if(row.eventPose)row.mappedShot=interaction::PlaceFireAtMuzzle(canonical(row.matrix),row.eventPose->muzzle,2*row.shot->unitsPerMetre);
    }
    QueryPerformanceCounter(&q);
    if(muzzleFire&&row.mappedShot&&q.QuadPart<row.eventPose->deadline){
        const auto left=reinterpret_cast<unsigned>(self),right=reinterpret_cast<unsigned>(rhs);
        const auto separate=[&](unsigned p){return std::uint64_t(at)+64<=p||std::uint64_t(p)+64<=at;};
        std::array<std::byte,64> before{},leftBefore{},rightBefore{},after{},leftAfter{},rightAfter{};
        if(separate(left)&&separate(right)&&Writable(at,64)&&Read(at,before.data(),64)&&!std::memcmp(before.data(),row.matrix.data(),64)&&Read(left,leftBefore.data(),64)&&Read(right,rightBefore.data(),64)){
            const auto& m=*row.mappedShot;const std::array<float,3> position{m.values[3][0],m.values[3][1],-m.values[3][2]};
            // Caller-owned composition result only: preserve all orientation and
            // padding bytes, and leave both native input matrices untouched.
            std::memcpy(reinterpret_cast<void*>(at+48),position.data(),12);
            const bool verified=Read(at,after.data(),64)&&!std::memcmp(before.data(),after.data(),48)&&!std::memcmp(before.data()+60,after.data()+60,4)&&!std::memcmp(after.data()+48,position.data(),12);
            if(!verified)++originWriteFailures;
            if(!Read(left,leftAfter.data(),64)||leftAfter!=leftBefore||!Read(right,rightAfter.data(),64)||rightAfter!=rightBefore)++shotSourceChanges;
            if(verified){row.originWritten=true;std::memcpy(row.after.data(),after.data(),64);if(client)++clientOriginWrites;else ++serverOriginWrites;}
        }else ++originWriteFailures;
    }
    if(muzzleFire&&current->supported&&!row.originWritten)++originFallbacks;
    if(row.originWritten&&recording&&fireRecordCount>=fireRecords.size())++originWritesAfterLogFull;
    current->composedOutput=row.after;current->compositionWritten=row.originWritten;current->eventPose=row.eventPose;
    if(recording&&fireRecordCount<fireRecords.size())fireRecords[fireRecordCount++]=row;return result;
}
void __fastcall GatherHook(void* self,void*,void* cache){
    Callback callback;gatherOriginal(self,cache);
    if(!enabled.load(std::memory_order_acquire)||!scope||scope->used)return;
    if(reinterpret_cast<unsigned>(self)!=scope->owner.router||reinterpret_cast<unsigned>(cache)!=scope->owner.cache){++mismatches;return;}
    scope->used=true;++gathers;if((bodyHolster||magazineDetached||physicalFeed||physicalPistolSlide)&&holsterNativeTick<UINT64_MAX)scope->nativeTick=++holsterNativeTick;
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    struct SelectedKnifeGatherOutcome {
        selectedKnife::Request request{};bool observed=false,published=false;
        ~SelectedKnifeGatherOutcome(){
            if(request.fire&&!published)selectedKnife::Published(request,false);
            if(!observed)selectedKnife::Missing();
        }
    } selectedKnifeOutcome;
#endif
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
    struct GrenadeGatherOutcome {bool observed=false;~GrenadeGatherOutcome(){if((ordinaryGrenade||playerGrenade)&&!observed){LARGE_INTEGER q{};QueryPerformanceCounter(&q);if(playerGrenade)playerGrenade->Missing(HandNanos(q.QuadPart));else ordinaryGrenade->Missing(HandNanos(q.QuadPart));}}} grenadeOutcome;
#endif
    std::optional<rigPublication::Tracking> holsterTracking;
    bool detachedUsed=false;std::optional<Bc2AmmoReserveLease> detachedReserve;
    interaction::InputFrame input{};
    std::int64_t deadline=0;const auto result=readInput?readInput(input,deadline):ipc::ChannelResult::Closed;
    gameplayInputLease.Observe(result,input,deadline);
    LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);
    const bool fresh=gameplayInputLease.Fresh(clock.QuadPart);
    input=fresh?cachedInput:interaction::InputFrame{};if(!fresh)++stale;
    // Retire old observation labels even when journal publication is busy.
    if(!fresh||!input.focused||vehicleShotSpace!=input.spaceGeneration){vehicleShotObserver.Invalidate();boatReticle.Invalidate();}
    vehicleShotSpace=input.spaceGeneration;
    const auto& owner=scope->owner;
    struct FireRouteGatherScope {
        FireRouteRecord record{};bool observing=false;
        FireRouteRecord* previous=interactionTrace;
        ~FireRouteGatherScope(){interactionTrace=previous;if(observing){LARGE_INTEGER now{};
            if(QueryPerformanceCounter(&now))record.sampledNs=HandNanos(now.QuadPart);
            fireRouteTelemetry.Observe(record);}}
    } fireTrace;
    fireTrace.observing=fireRouteTelemetryEnabled;
    interactionTrace=fireTrace.observing?&fireTrace.record:nullptr;
    if(fireTrace.observing){auto& r=fireTrace.record;
        r.owner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,cachedInput.spaceGeneration};
        r.equipment=owner.equipment;r.input=cachedInput.generation;r.inputEpoch=inputEpoch;r.cache=owner.cache;
        r.gatherNs=r.sampledNs=HandNanos(clock.QuadPart);r.inputPresent=cachedInput.generation!=0;r.inputFresh=fresh;r.onFoot=owner.foot;
        r.rightAimTracked=input.hands[1].aimTracked;
        r.inputObservedNs=inputDeadline>0?HandNanos(inputDeadline-handFrequency/10):0;r.inputDeadlineNs=HandNanos(inputDeadline);
        r.requested=r.inputPresent&&cachedInput.hands[1].trigger>=.75f;
        if(!fresh)r.reasons|=FireRouteStaleInput;
        if(!owner.foot)r.reasons|=FireRouteNotOnFoot;
        if(fresh&&!interaction::ValidInput(input))r.reasons|=FireRouteInvalidInput;
        if(fresh&&!input.focused)r.reasons|=FireRouteFocusLost;
        if(fresh&&!input.headValid)r.reasons|=FireRouteHeadMissing;
    }
    std::optional<FeedEquipmentGate> retainedFeedGate;
    std::optional<WeaponEquipmentIdentity> feedEquipment;
    std::optional<AmmoResourceBinding> feedResourceBinding;
    if((physicalFeed||physicalPistolSlide)&&owner.foot){
        const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
            [](void*,unsigned at,const char* type){return Type(at,type);}};
        const auto equipment=ReadWeaponEquipmentIdentity(memory,owner.weapon);
        const ReloadStateOwner selected{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,handSample.owner.space};
        if(equipment&&*equipment==owner.equipment){
            feedEquipment=equipment;
            feedResourceBinding=bodyInventory?bodyInventory->ResourceBinding(selected,HandNanos(clock.QuadPart)):std::nullopt;
            if(physicalFeed)retainedFeedGate=physicalFeed->RetainedGate(selected,*feedEquipment,feedResourceBinding);
            if(retainedFeedGate){
                auto safety=handSample;safety.nowNs=HandNanos(clock.QuadPart);
                const HolsterSuppressionRequest demand{selected,safety,retainedFeedGate->lifetime,scope->nativeTick,owner.cache};
                HolsterInputOverride suppression;
                // This established cache adapter accepts expired input only
                // for suppression. Ordinary current mapping below restores
                // permitted actions; every early return keeps reload/debt off.
                if(suppression.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},demand,{nullptr,HolsterCacheCurrent}))
                    (void)suppression.Commit();
            }
            if(physicalPistolSlide&&feedResourceBinding&&
               feedResourceBinding->owner==selected&&feedResourceBinding->data==equipment->data&&
               feedResourceBinding->persistence==equipment->persistence){
                const auto now=HandNanos(clock.QuadPart);
                const auto debt=reloadFlowRuntime::ReadPistolAfterFeedState(feedResourceBinding->context.resource,now);
                const auto* profile=debt?ResolveBc2PistolSlideRegistration(debt->profileKey):nullptr;
                if(profile&&equipment->Asset()==profile->configuration.assetName&&debt->blocksFire&&
                   debt->request.intent.id&&AmmoResourceBindingFresh(*feedResourceBinding,selected,now)){
                    auto safety=handSample;safety.nowNs=now;
                    const HolsterSuppressionRequest demand{selected,safety,debt->request.intent.id,scope->nativeTick,owner.cache};
                    HolsterInputOverride suppression;
                    if(suppression.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},demand,{nullptr,HolsterCacheCurrent}))
                        (void)suppression.Commit();
                }
            }
        }
    }
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    selectedKnife::ObserveOwner({owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration});
#endif
    ObserveAttached(owner,AttachedPhase::Gather,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(cache));
    ContextInteractRecord interact;interact.input=input.generation;interact.space=input.spaceGeneration;
    interact.actorGeneration=rigEpoch;interact.equipGeneration=epoch;interact.observedNs=HandNanos(inputDeadline-handFrequency/10);
    interact.deadlineNs=HandNanos(inputDeadline);interact.nowNs=HandNanos(clock.QuadPart);
    interact.player=owner.player;interact.soldier=owner.soldier;interact.weak=owner.weak;interact.controlled=owner.controlled;
    interact.entry=owner.entry;interact.cache=owner.cache;interact.onFoot=owner.foot;interact.action=owner.foot?27u:16u;interact.actionMask=1u<<interact.action;
    interact.sourceFresh=fresh;interact.controllerHeld=(input.hands[0].held&interaction::Primary)!=0;
    struct InteractReportScope {
        ContextInteractRecord& record;const Owner& owner;bool readback;
        ~InteractReportScope(){
            if(record.committed&&readback){Owner current{};record.ownerCurrent=Resolve(current)&&current==owner;
                unsigned word=0;record.readback=record.ownerCurrent&&Read(owner.cache+0x98,&word,4);
                record.nativeHeld=record.readback&&record.actionMask&&(word&record.actionMask)==record.actionMask;}
            LARGE_INTEGER now{};if(QueryPerformanceCounter(&now))record.nowNs=HandNanos(now.QuadPart);
            contextInteractEvidence.Observe(record);
        }
    } interactScope{interact,owner,contextInteractEvidence.NeedsReadback(interact.controllerHeld)};
    gatherSightRequest=gatherSightAck=0;gatherSightCommit=false;
    struct OwnershipReportScope {~OwnershipReportScope(){RecordHandOwnership();}} ownershipReportScope;
    if(pendingMode){
        auto& request=modeRecords[*pendingMode];
        if(request.actor!=owner.soldier||request.weak!=owner.weak||request.owner!=rigEpoch||request.space!=input.spaceGeneration||!fresh||!input.focused||!input.headValid||!input.hands[0].gripTracked||(owner.weapon!=request.from&&owner.weapon!=request.target)||GetTickCount64()-request.ms>2000){
            request.cancelled=true;pendingMode.reset();
        }else if(owner.weapon==request.target){request.ackMs=GetTickCount64();if(request.gesture)sightAcknowledged=request.gesture;++modeAcknowledgements;pendingMode.reset();}
    }

    if(!input.hands[1].gripTracked||!input.hands[1].aimTracked){std::lock_guard lock(shotPoseMutex);shotHistory.Reset();}
    // FrameChannel owns freshness against QPC; XR times are a separate clock.
    // Duplicate samples preserve holds, never duplicate action/snap edges.
    // Native gather encodes allowed float actions in low bits of +98.
    // Require movement and fire eligibility; menus/cutscenes must keep control.
    const auto allowed=U32(owner.cache+0x98);
    const bool vehicleInteraction=!owner.foot&&Type(owner.controlled,"ClientVehicleEntity")&&(allowed&0x11u)==0x11u;
    bool nativePlaying=owner.foot?(allowed&0x103u)==0x103u:vehicleInteraction;
    if(!nativePlaying)fireTrace.record.reasons|=FireRouteNativeInactive;
    interact.playing=nativePlaying;
    if(owner.foot&&!nativePlaying)++nativeInactive;
    if(!owner.foot){
        // Vehicle controls have their own neutral recovery, continuous axes and
        // ownership. Infantry snap/cycling/aim/roomscale never execute here.
        actions={};ClearReloadOwner();
        CancelSight();weaponModeHeld=true;ResetSupportGrip("not_on_foot");ResetHandOwnership("not_on_foot");rigPublication::PublishTracking({});roomscale.Suspend();
        const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        const VehicleRouteBinding binding{unsigned(image+profile.gameplay.managerVtable),unsigned(image+profile.gameplay.inputRouterVtable),unsigned(image+profile.cacheVtable),true};
        const auto routes=ReadVehicleRoutes(memory,binding,{owner.manager,owner.player,owner.soldier,owner.weak,rigEpoch,epoch});
        if(routes.status!=VehicleRouteStatus::Okay){++vehicleRouteRejects;vehicleActions.Reset();PublishVehicleView({});return;}
        const auto& seat=routes.snapshot;const auto data=U32(owner.controlled+12),entryData=U32(owner.entry+12);
        // Revalidate live consumers on every gather; static route equality alone
        // must not preserve driving/fire authority after native reassociation.
        if(cachedCampaignVehicle&&!RevalidateCampaignVehicleProfile(memory,*cachedCampaignVehicle,seat)){
            cachedCampaignVehicle.reset();cachedVehicleProfile.reset();campaignVehicleRetryAt=0;
        }
        if((!cachedVehicleProfile&&GetTickCount64()>=campaignVehicleRetryAt)||seat.identity!=cachedVehicleIdentity||seat.fingerprint!=cachedVehicleFingerprint||data!=cachedVehicleData||entryData!=cachedEntryData){
            cachedCampaignVehicle.reset();cachedVehicleProfile=ReadPblDriverProfile(memory,seat);
            if(!cachedVehicleProfile){
                cachedCampaignVehicle=ReadCampaignVehicleProfile(memory,campaignVehicleBinding,seat);
                if(cachedCampaignVehicle)cachedVehicleProfile=cachedCampaignVehicle->profile;
            }
            cachedVehicleIdentity=seat.identity;cachedVehicleFingerprint=seat.fingerprint;cachedVehicleData=data;cachedEntryData=entryData;
            // Only failed admission is rate-limited. This grants no input lease.
            campaignVehicleRetryAt=GetTickCount64()+250;
        }
        if(cachedCampaignVehicle)nativePlaying=CampaignVehiclePlaying(cachedCampaignVehicle->profile,allowed);
        interact.playing=nativePlaying;if(!nativePlaying)++nativeInactive;
        vehicleSeatEntity.store(owner.controlled);vehicleSeatEntry.store(owner.entry);vehicleRouteFingerprint.store(seat.fingerprint);
        const interaction::VehicleTrackingOwner seatOwner{seat.identity.actor,rigEpoch,owner.controlled,owner.entry};
        const auto controls=vehicleActions.Update(input,{seatOwner,input.spaceGeneration},nativePlaying,fresh);
        interact.semanticHeld=controls.axes.exit;
        const bool viewValid=cachedVehicleProfile&&fresh&&nativePlaying&&interaction::ValidInput(input)&&input.focused&&input.headValid;
        interaction::VehicleAimOutput headAim;std::optional<BoatAimSnapshot> nativeBoatAim;
        if(boatHeadAim&&!cachedCampaignVehicle&&cachedVehicleProfile&&viewValid)nativeBoatAim=ReadPblDriverAim(memory,boatAimBinding,seat);
        {
            std::lock_guard lock(vehicleViewMutex);boatAimReady=false;boatAimSpace=0;
            if(nativeBoatAim){
                ++boatAimSamples;const interaction::VehicleAimObservation observation{seatOwner,input.spaceGeneration,nativeBoatAim->hull,nativeBoatAim->cameraWorld,nativeBoatAim->neutralCameraLocal,
                    HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),true};
                headAim=boatHeadPolicy.Update(observation,input,HandNanos(clock.QuadPart),PblDriverAimLimits());
                if(headAim.ready){boatAimReady=true;boatAimSpace=input.spaceGeneration;boatAimSeat=seat;boatAimReference=boatHeadPolicy.Reference();}
            }else if(boatHeadAim&&!cachedCampaignVehicle&&cachedVehicleProfile&&viewValid)++boatAimRejects;
        }
        PublishVehicleView({seatOwner,input.spaceGeneration,bool(cachedVehicleProfile),viewValid,
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline)},
            !cachedVehicleProfile?VehicleCameraMode::Unavailable:
            boatHeadAim&&!cachedCampaignVehicle?VehicleCameraMode::PblHeadAim:VehicleCameraMode::NativeSeat);
        // The native component/config checks must not extend the original XR lease.
        if(cachedCampaignVehicle&&!RevalidateCampaignVehicleProfile(memory,*cachedCampaignVehicle,seat))return;
        QueryPerformanceCounter(&clock);
        Owner check{};if(!fresh||clock.QuadPart>=inputDeadline||!controls.active||!Resolve(check)||check!=owner||!Writable(owner.cache,InputBytes))return;
        if(!cachedVehicleProfile){
            ++vehicleUnsupported;
            // Preserve the previously supported exit, only when this actual
            // native router independently maps ChangeVehicle16 to concept36.
            const auto exit=std::find(seat.routes.begin(),seat.routes.begin()+seat.count,VehicleRoute{16,36});
            interaction::ActionOutput out{};out.active=true;out.owner=owner.soldier;if(controls.axes.exit)out.held|=interaction::Use;
            if(vehicleInteraction&&exit!=seat.routes.begin()+seat.count&&scope->patch.ApplyVehicleExit({reinterpret_cast<std::byte*>(owner.cache),InputBytes},out)){scope->patch.Commit();interact.committed=true;if(controls.axes.exit)++vehicleInteractions;}
            return;
        }
        auto command=CampaignVehicleCommand(seat.identity,controls);
        auto inputProfile=*cachedVehicleProfile;
        if(headAim.ready&&nativeBoatAim){
            inputProfile.role=VehicleSeatRole::DriverGunner;
            inputProfile.axes[unsigned(VehicleAxis::LookYaw)]=static_cast<EntryAction>(6); // Verified EiaRoll, not hull-steering EiaYaw.
            inputProfile.axes[unsigned(VehicleAxis::LookPitch)]=EntryAction::Pitch;
            inputProfile.axes[unsigned(VehicleAxis::Fire)]=EntryAction::Fire;
            command.axes[unsigned(VehicleAxis::LookYaw)]=headAim.yaw;command.axes[unsigned(VehicleAxis::LookPitch)]=headAim.pitch;
            command.axes[unsigned(VehicleAxis::Fire)]=boatHeadFire&&controls.armedRight?controls.axes.fire:0;
        }
        const auto plan=BuildVehicleInputPlan(inputProfile,{seat.identity,seat.fingerprint,true,nativePlaying},command,{reinterpret_cast<const std::byte*>(owner.cache),InputBytes});
        VehicleInputOverride patch;
        if(!patch.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},plan)){++vehicleRouteRejects;return;}
        patch.Commit();interact.committed=true;
        if(headAim.ready&&nativeBoatAim){
            ++boatAimCommits;if(command.axes[unsigned(VehicleAxis::Fire)]>0&&!(plan.deniedAxes&(1u<<unsigned(VehicleAxis::Fire))))++boatAimFireCommits;
            std::lock_guard lock(vehicleViewMutex);const auto now=GetTickCount64();
            if(boatAimRecordCount<boatAimRecords.size()&&(!boatAimRecordCount||now-boatAimRecordedAt>=100)){
                boatAimRecords[boatAimRecordCount++]={now,input.generation,headAim.yaw,headAim.pitch,headAim.yawError,headAim.pitchError,nativeBoatAim->jointYaw,nativeBoatAim->jointPitch,command.axes[unsigned(VehicleAxis::Fire)]};boatAimRecordedAt=now;
            }
        }
        ++vehicleDriveCommits;vehicleThrottle.store(controls.axes.throttle);vehicleSteer.store(controls.axes.steer);if(controls.axes.exit)++vehicleInteractions;
        // Capture actual native gather-buffer words after scoped commit, not
        // just the requested axes or final neutral sample in the stop report.
        VehicleGatherRecord record;record.entity=owner.controlled;record.entry=owner.entry;record.soldier=owner.soldier;record.weak=owner.weak;
        record.actorGeneration=rigEpoch;record.seatGeneration=epoch;record.space=input.spaceGeneration;record.inputSequence=input.generation;record.ms=GetTickCount64();record.permissions=allowed;
        record.exitHeld=command.exitHeld;record.flagsWritten=allowed;
        for(unsigned n=0;n<plan.count;++n)if(plan.edits[n].offset==0x98)record.flagsWritten=plan.edits[n].after;
        record.flagsVerified=Read(owner.cache+0x98,&record.flagsObserved,4)&&record.flagsObserved==record.flagsWritten;
        bool readback=true;
        record.role=unsigned(inputProfile.role);
        if(cachedCampaignVehicle){record.consumerDriver=cachedCampaignVehicle->driver;record.consumerCamera=cachedCampaignVehicle->camera;record.consumerWeapons=cachedCampaignVehicle->weapons;}
        for(unsigned n=0;n<plan.count;++n){const auto& edit=plan.edits[n];if(edit.offset==0x98)continue;
            unsigned axis=VehicleAxisCount;
            for(unsigned a=0;a<VehicleAxisCount;++a)if(inputProfile.axes[a]&&edit.offset==8+4*unsigned(*inputProfile.axes[a]))axis=a;
            if(axis==VehicleAxisCount){readback=false;continue;}
            record.actions[axis]=unsigned(*inputProfile.axes[axis]);record.mask|=1u<<axis;
            std::memcpy(&record.before[axis],&edit.before,4);std::memcpy(&record.written[axis],&edit.after,4);
            const bool read=Read(owner.cache+unsigned(edit.offset),&record.observed[axis],4);
            readback&=read&&!std::memcmp(&record.observed[axis],&edit.after,4);
        }
        record.verified=record.mask&&readback;
        if(!record.verified)++vehicleReadbackFailures;
        else {
            if((record.mask&1)&&std::abs(record.observed[0])>0){++vehicleThrottleNonzero;vehicleThrottleMaximum.store(std::max(vehicleThrottleMaximum.load(),std::abs(record.observed[0])));}
            if((record.mask&2)&&std::abs(record.observed[1])>0){++vehicleSteerNonzero;vehicleSteerMaximum.store(std::max(vehicleSteerMaximum.load(),std::abs(record.observed[1])));}
        }
        const bool changed=!havePreviousVehicleGather||record.entity!=previousVehicleGather.entity||record.entry!=previousVehicleGather.entry||record.actorGeneration!=previousVehicleGather.actorGeneration||record.seatGeneration!=previousVehicleGather.seatGeneration||record.space!=previousVehicleGather.space||record.mask!=previousVehicleGather.mask||record.written!=previousVehicleGather.written||record.verified!=previousVehicleGather.verified||record.exitHeld!=previousVehicleGather.exitHeld||record.flagsWritten!=previousVehicleGather.flagsWritten||record.flagsObserved!=previousVehicleGather.flagsObserved||record.flagsVerified!=previousVehicleGather.flagsVerified;
        const bool held=std::any_of(record.written.begin(),record.written.end(),[](float v){return v!=0;})&&record.ms-vehicleGatherRecordedAt>=100;
        const auto count=vehicleGatherCount.load();
        if(count<vehicleGatherRecords.size()&&(changed||held)){vehicleGatherRecords[count]=record;vehicleGatherRecordedAt=record.ms;vehicleGatherCount.store(count+1,std::memory_order_release);}
        if(vehicleShotBinding.verified&&cachedCampaignVehicle&&record.verified&&record.flagsVerified)
            vehicleShotObserver.Publish({*cachedCampaignVehicle,{owner.manager,owner.player,owner.soldier,owner.weak,rigEpoch,epoch},
                input.generation,input.spaceGeneration,HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),record.observed[4]});
        if(boatReticleBinding.verified&&nativeBoatAim&&record.verified&&record.flagsVerified){
            VehicleReticleProducerContext reticleContext;
            reticleContext.input={seatOwner,input.spaceGeneration,true,viewValid,HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline)};
            reticleContext.inputSequence=input.generation;reticleContext.scene=rigEpoch;reticleContext.weaponGeneration=epoch;
            reticleContext.focused=input.focused;reticleContext.playing=nativePlaying;
            LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
            boatReticle.Publish(memory,boatReticleBinding,*nativeBoatAim,seat,reticleContext,HandNanos(stamp.QuadPart));
        }else boatReticle.Invalidate();
        previousVehicleGather=record;havePreviousVehicleGather=true;
        return;
    }
    vehicleActions.Reset();PublishVehicleView({});boatReticle.Invalidate();
    {std::lock_guard lock(vehicleViewMutex);boatAimReady=false;boatHeadPolicy.Reset();}
    auto actionInput=input;if(reloadRequestMode)actionInput.hands[0].trigger=0;
    if(inventoryReloadFixture)inventoryReloadFixture->ActionInput(actionInput);
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
    if(armingEmptyFixture){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto reserve=reloadFlowRuntime::ReadReserve();const auto family=reloadFlowRuntime::ReadRequestProbeSnapshot();bool neutral=true;
        for(const auto& h:input.hands)neutral=neutral&&!h.held&&h.trigger==0&&h.squeeze==0&&h.stickX==0&&h.stickY==0;
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
        const auto trigger=armingEmptyFixture->Fire(current,physicalReload.has_value()&&family&&family->identity.owner==current&&family->family==unsigned(ReloadNativeFamily::SpasTube),
            fresh&&nativePlaying&&interaction::ValidInput(input)&&input.focused&&input.headValid&&neutral,reserve,HandNanos(stamp.QuadPart));
        for(auto& h:actionInput.hands){h.held=0;h.squeeze=h.stickX=h.stickY=0;h.trigger=0;}actionInput.hands[1].trigger=trigger;
    }
#endif
    if(emptyFireFixture){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto profileId=reloadFlowRuntime::ReadMagazineRequestProfile(current);
        const auto fixtureProfile=profileId?FindMagazineEquipment(*profileId):nullptr;
        const bool supported=(fixtureProfile&&fixtureProfile->Ready())||owner.equipment.Asset()==SpasReloadAsset;
        bool neutral=true;for(const auto& h:input.hands)neutral=neutral&&!h.held&&h.trigger==0&&h.squeeze==0&&h.stickX==0&&h.stickY==0;
        const bool safe=fresh&&nativePlaying&&interaction::ValidInput(input)&&input.focused&&input.headValid&&neutral;
        const auto reserve=reloadFlowRuntime::ReadReserve();
        // Reserve stamps its actual read. Validate after that read completes,
        // rather than treating every fresh observation as future evidence.
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
        const float trigger=emptyFireFixture->Tick(current,supported,safe,reserve,HandNanos(stamp.QuadPart));
        for(auto& h:actionInput.hands){h.held=0;h.squeeze=h.stickX=h.stickY=0;h.trigger=0;}
        actionInput.hands[1].trigger=trigger;
    }
    if(m95StockShotFixture){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto reserve=reloadFlowRuntime::ReadM95StockShotReserve();
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
        const auto trigger=m95StockShotFixture->Tick(input,current,fresh&&nativePlaying,
            owner.equipment.Asset()=="M95_sp",reserve,HandNanos(stamp.QuadPart));
        for(auto& h:actionInput.hands){h.held=0;h.squeeze=h.stickX=h.stickY=h.trigger=0;}
        actionInput.hands[1].trigger=trigger;
        rigPublication::PublishM95ShotCapturePhase(1+unsigned(m95StockShotFixture->Current()));
    }
    // Firing fixtures prepare their trigger below, after reading original rig
    // evidence. Mapping this generation first would cache neutral Fire for the
    // whole generation, even if Update were called again after Prepare.
    const bool holsterFireFixture=bodyHolsterFixture&&bodyHolsterFixture->FireRequested();
    const bool pumpFireFixture=physicalPumpFixture&&physicalPump;
    const bool deferActions=(holsterFireFixture||pumpFireFixture||physicalBoltFixture.has_value()||ordinaryBoltInputFixture.has_value()||resourceInventoryFixture.has_value())&&fresh&&nativePlaying&&
        interaction::ValidInput(input)&&input.focused&&input.headValid;
    auto out=deferActions?interaction::ActionOutput{}:actions.Update(actionInput,{owner.soldier,inputEpoch,nativePlaying,true,rigEpoch},input.predictedNs,HandNanos(clock.QuadPart));
    fireTrace.record.mapped=(out.held&interaction::Fire)!=0;
    if(m95StockShotFixture&&actionInput.hands[1].trigger<.75f){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}
    if(!fresh||!nativePlaying||!interaction::ValidInput(input)||!input.focused||!input.headValid){ClearReloadOwner(ReloadOwnerLoss::PresentationUnavailable);CancelSight();weaponModeHeld=true;ResetSupportGrip(!fresh?"stale_input":!nativePlaying?"native_inactive":!interaction::ValidInput(input)?"invalid_input":!input.focused?"focus_loss":"head_untracked");ResetHandOwnership("input_unavailable"); {std::lock_guard lock(shotPoseMutex);shotHistory.Reset();} rigPublication::PublishTracking({});roomscale.Suspend();
#ifdef FVR_BC2_SHELL_FOCUS_LOSS_PROBE
        if(fresh&&!input.focused&&resourceInventoryFixture){
            const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
            resourceInventoryFixture->ObserveShellFocusLoss(input,current,HandNanos(inputDeadline-handFrequency/10),
                HandNanos(inputDeadline),HandNanos(clock.QuadPart));
        }
#endif
#ifdef FVR_BC2_PUMP_RECEIVER_GAP_PROBE
        if(!fresh&&resourceInventoryFixture){const auto now=HandNanos(clock.QuadPart);
            resourceInventoryFixture->ObserveMissingReceiver(reloadFlowRuntime::ReadNativeCycleView(now),
                !handOwnership.Current(interaction::InteractionHand::Left)&&!handOwnership.Current(interaction::InteractionHand::Right),now);}
#endif
        return; }
    if(!out.active||out.owner!=owner.soldier){out={};out.owner=owner.soldier;}
    Owner check{};const bool ownerResolved=Resolve(check);
    if(!ownerResolved||check!=owner||!Writable(owner.cache,InputBytes)){
        fireTrace.record.reasons|=FireRouteOwnerUnavailable;
        ClearReloadOwner(ownerResolved&&check!=owner?ReloadOwnerLoss::LifetimeEnded:ReloadOwnerLoss::PresentationUnavailable);
        CancelSight();ResetSupportGrip("owner_unavailable");ResetHandOwnership("owner_unavailable",!ownerResolved||check==owner);rigPublication::PublishTracking({});return;}
    const auto aimer=U32(owner.aim+8);
    float nativeYaw=0;const bool aimValid=owner.aim&&U32(U32(owner.aim)+0x28)==0&&Type(U32(aimer),"HaloAimerData")&&
        Read(aimer+12,&nativeYaw,4)&&std::isfinite(nativeYaw)&&std::abs(nativeYaw)<100&&Writable(aimer+12,8)&&Writable(owner.aim+12,8);
    if(handPoses&&!aimValid)rigPublication::PublishTracking({});

    // On-demand bounded observer. No native calls/writes and no reload policy
    // authority: both native branches remain visible with their actual states.
    const auto reloadSourceNowNs=HandNanos(clock.QuadPart);
    if(reloadStateBinding&&(reloadRequestMode||!reloadObservedStart||GetTickCount64()-reloadObservedStart<20000)&&
        SelectedMeshesObservation::SourceObservationDue(reloadObservedSourceNs,reloadSourceNowNs)){
        reloadObservedAt=GetTickCount64();if(!reloadObservedStart)reloadObservedStart=reloadObservedAt;
        reloadObservedSourceNs=reloadSourceNowNs;
        const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        const auto observed=ReadReloadState(memory,*reloadStateBinding,unsigned(image),
            {owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration},input.generation,reloadSourceNowNs);
        ++reloadStatuses[std::size_t(observed.status)];
        if(observed.snapshot){
            if(fireTrace.observing)ObserveFireRouteConfiguration(fireTrace.record,*observed.snapshot);
            const auto publication=reloadFlowRuntime::PublishOwnerObserved(*observed.snapshot,observed.snapshot->observedNs+200000000);
            ++reloadPublicationStatuses[unsigned(publication.status)];++reloadPublicationReasons[unsigned(publication.reason)];
            // Deferred serialization supplies no new native lease. Do not turn it
            // into ClearOwner; original deadlines and native structural reads remain gates.
            LARGE_INTEGER selectedNow{};if(QueryPerformanceCounter(&selectedNow))
                selectedMeshes.Observe(*observed.snapshot,HandNanos(selectedNow.QuadPart));
            if(reloadSnapshotCount<reloadSnapshots.size())reloadSnapshots[reloadSnapshotCount++]=*observed.snapshot;
        }else ClearReloadOwner(observed.status==ReloadStateStatus::ReadFailure||observed.status==ReloadStateStatus::ChangedDuringRead?
            ReloadOwnerLoss::PresentationUnavailable:ReloadOwnerLoss::LifetimeEnded);
    }
    const auto weaponAsset=owner.equipment.asset;const auto asset=owner.equipment.Asset();
    const auto itemProfileCandidate=FindWeaponProfile(asset);
    // 40mmgl is a generic name reused by other rifles. The measured profile is
    // only the exact shared scoped-XM8 mesh/persistence/mode-map family.
    static unsigned familySoldier=0,familyWeak=0,familyWeapon=0,familyData=0;static WeaponEquipmentIdentity familyEquipment{};static std::uint64_t familyEpoch=0;
    static std::optional<WeaponModeCommand> familyBinding;static ULONGLONG familyChecked=0;
    const auto nowFamily=GetTickCount64();const auto data=U32(owner.weapon+4);
    if(familySoldier!=owner.soldier||familyWeak!=owner.weak||familyWeapon!=owner.weapon||familyData!=data||familyEquipment!=owner.equipment||familyEpoch!=rigEpoch||nowFamily-familyChecked>=500){
        familySoldier=owner.soldier;familyWeak=owner.weak;familyWeapon=owner.weapon;familyData=data;familyEpoch=rigEpoch;familyChecked=nowFamily;familyEquipment=owner.equipment;
        const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},[](void*,unsigned at,const char* expected){return Type(at,expected);}};
        familyBinding=ResolveWeaponMode(memory,owner.soldier,owner.weapon);
    }
    const auto itemProfile=(asset!="40mmgl"||familyBinding)?itemProfileCandidate:nullptr;
    const auto coreProfile=itemProfile?&itemProfile->core:nullptr;
    if(magazineDetachedFixture&&magazineDetached){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto contact=rigPublication::ReadMagazineContact(current);
        const auto reserve=reloadFlowRuntime::ReadReserve();
        // These reads stamp evidence after Gather began. Compare against a
        // processing clock sampled after them, retaining the input deadline.
        LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
        magazineDetachedFixture->Prepare(input,current,asset,contact,reserve,
            magazineDetached->Phase(),HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
    if(magazinePhysicalFixture&&magazinePhysical){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto now=HandNanos(clock.QuadPart);
        magazinePhysicalFixture->Prepare(input,current,asset,rigPublication::ReadMagazineContact(current),magazinePhysical->ProbeState(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
    if(ordinaryBoltInputFixture&&ordinaryManualCycles&&ordinaryBoltPlayer){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto raw=rigPublication::ReadBoltContact(current);const auto reserve=reloadFlowRuntime::ReadM95StockShotReserve();
        LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
        const auto native=reloadFlowRuntime::ReadNativeCycleView(now);
        ordinaryBoltInputFixture->ObserveBody(ReadBodyHolsterProbe(now));
        ordinaryBoltInputFixture->ObservePlayer(ordinaryBoltPlayer->ReferencesReady(),native&&
            native->selectedMode==Bc2NativeCycleMode::M95&&native->selectedOwner==current);
        ordinaryBoltInputFixture->Prepare(input,current,asset,raw,native,reserve,
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
        actionInput=input;if(reloadRequestMode)actionInput.hands[0].trigger=0;
        out=actions.Update(actionInput,{owner.soldier,inputEpoch,nativePlaying&&!ordinaryBoltInputFixture->Failed(),true,rigEpoch},input.predictedNs,HandNanos(clock.QuadPart));
        if(ordinaryBoltInputFixture->NeedsNeutralFire(now)){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}
        if(!out.active||out.owner!=owner.soldier){out={};out.owner=owner.soldier;}
    }
    if(physicalBoltFixture&&physicalBolt){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto raw=rigPublication::ReadBoltContact(current);const auto reserve=reloadFlowRuntime::ReadM95StockShotReserve();
        LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
        physicalBoltFixture->Prepare(input,current,asset,raw,reloadFlowRuntime::ReadNativeCycleView(now),reserve,
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
        actionInput=input;if(reloadRequestMode)actionInput.hands[0].trigger=0;
        out=actions.Update(actionInput,{owner.soldier,inputEpoch,nativePlaying&&!physicalBoltFixture->Failed(),true,rigEpoch},input.predictedNs,HandNanos(clock.QuadPart));
        if(!out.active||out.owner!=owner.soldier){out={};out.owner=owner.soldier;}
    }
    if(physicalPumpFixture&&physicalPump){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto raw=rigPublication::ReadPumpContact(current);
        LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
        physicalPumpFixture->Prepare(input,current,asset,raw,reloadFlowRuntime::ReadNativeCycleView(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
        actionInput=input;if(reloadRequestMode)actionInput.hands[0].trigger=0;
        out=actions.Update(actionInput,{owner.soldier,inputEpoch,nativePlaying&&!physicalPumpFixture->Failed(),true,rigEpoch},input.predictedNs,HandNanos(clock.QuadPart));
        if(!out.active||out.owner!=owner.soldier){out={};out.owner=owner.soldier;}
    }
    if(magazineResourceFixture&&magazinePhysical){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto contact=rigPublication::ReadMagazineContact(current);
        LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
        magazineResourceFixture->Prepare(input,current,asset,contact,magazinePhysical->ResourceProbeState(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
    if(resourceInventoryFixture&&magazinePhysical&&physicalReload&&bodyInventory){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        ResourceInventoryObservation observed;observed.reserve=reloadFlowRuntime::ReadReserve();
        LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
        observed.body=ReadBodyHolsterProbe(now);observed.display=bodyInventory->Display(now);
        observed.magazineRaw=rigPublication::ReadMagazineContact(current);observed.magazine=magazinePhysical->ResourceProbeState(now);
        // A cancelled/empty startup consumer has no cached view. Read the same
        // native publication without synthesizing counts or changing its state.
        if(!observed.magazine.resource)observed.magazine.resource=reloadFlowRuntime::ReadAmmoResourceView(current,now);
        observed.shellRaw=rigPublication::ReadReloadContact(current);observed.shell=physicalReload->ProbeState(now);
        observed.shellBlocks=physicalReload->BlocksEquipment();
        if(const auto frame=rigPublication::ReadWeaponSupportFrame(owner.soldier,owner.weak,owner.weapon)){
            observed.supportRaw=frame->controllerMapping;observed.supportTarget=frame->targetWristWorldMeters;
            observed.supportVerified=frame->support.valid&&(frame->authoredSupportReference||
                interaction::WeaponFeatureEnabled(coreProfile,interaction::WeaponFeature::SupportGrip));
        }
        if(resourcePumpCombination){observed.pumpRaw=rigPublication::ReadPumpContact(current);observed.pumpNative=reloadFlowRuntime::ReadNativeCycleView(now);}
        resourceInventoryFixture->Prepare(input,current,asset,observed,HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
        if(resourceInventoryFixture->Recording())reloadFlowRuntime::BeginInventoryReloadRecords();
        // First and only mapping for this generation, after fixture Prepare.
        actionInput=input;if(reloadRequestMode)actionInput.hands[0].trigger=0;
        out=actions.Update(actionInput,{owner.soldier,inputEpoch,nativePlaying&&!resourceInventoryFixture->CancelConsumer(),true,rigEpoch},input.predictedNs,HandNanos(clock.QuadPart));
        if(!out.active||out.owner!=owner.soldier){out={};out.owner=owner.soldier;}
    }
    if(inventoryReloadFixture&&magazinePhysical&&bodyInventory){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
        inventoryReloadFixture->Prepare(input,current,asset,ReadBodyHolsterProbe(now),bodyInventory->Display(now),
            rigPublication::ReadMagazineContact(current),magazinePhysical->ProbeState(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
        if(inventoryReloadFixture->State()==Bc2InventoryReloadProbe::Phase::Reload)
            reloadFlowRuntime::BeginInventoryReloadRecords();
    }
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
    if(armingEmptyFixture&&physicalReload){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto now=HandNanos(clock.QuadPart);
        armingEmptyFixture->Prepare(input,current,asset,rigPublication::ReadReloadContact(current),physicalReload->ProbeState(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
#endif
    if(physicalReloadFixture&&physicalReload){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto now=HandNanos(clock.QuadPart);
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
        {const auto state=physicalReload->ProbeState(now);
         physicalReloadFixture->Entry(reloadFlowRuntime::ReadPreholdEntry(state.identity,state.cycle,now));}
#endif
        physicalReloadFixture->Prepare(input,current,asset,rigPublication::ReadReloadContact(current),physicalReload->ProbeState(now),
            HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),now);
    }
    if(bodyHolsterFixture){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto diagnosticAmmo=bodyHolsterFixture->FireRequested()?reloadFlowRuntime::ReadDiagnosticFireReserve():std::optional<Bc2AmmoReserveLease>{};
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);const auto ns=HandNanos(now.QuadPart);
        bodyHolsterFixture->Prepare(input,current,asset,ReadBodyHolsterProbe(ns),HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),ns,diagnosticAmmo);
        if(bodyHolsterFixture->Failed())InvalidateBodyHolster(false,BodyHolsterLifecycleReason::DiagnosticFailed);
        if(holsterFireFixture){actionInput=input;if(reloadRequestMode)actionInput.hands[0].trigger=0;
            out=actions.Update(actionInput,{owner.soldier,inputEpoch,nativePlaying&&!bodyHolsterFixture->Failed(),true,rigEpoch},input.predictedNs,HandNanos(clock.QuadPart));
            if(!out.active||out.owner!=owner.soldier){out={};out.owner=owner.soldier;}
            if(bodyHolsterFixture->NeedsNeutralFire()){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}}
    }
    const bool ordinaryBodyPrepared=ordinaryManualCycles&&motionAim&&aimValid;
    if(ordinaryBodyPrepared)PrepareTrackedBodyAnchor(owner,input,out,nativeYaw);
    SelectOrdinaryCycle(owner,input);
    if(twoHandGrip){
        ObserveHandOwnership(owner,input,clock.QuadPart,aimValid,
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon))!=0,familyBinding);
        const auto left=handOwnership.Current(interaction::InteractionHand::Left);
        if(sightPhase!=interaction::SightFlipPhase::Idle&&(!left||left->token.kind!=interaction::HandClaimKind::Sight)&&
            !(sightLeaseContinuation&&sightPhase==interaction::SightFlipPhase::AwaitingAcknowledgement&&sightAcknowledged))CancelSight();
    }
    if(reloadRequestFixture){
        // FrameChannel publishes deadline = source QPC + frequency/10. Preserve
        // that exact source epoch across duplicate/Busy input reads.
        reloadRequestFixture->Tick(input,handSample.owner,HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),HandNanos(clock.QuadPart));
    }
    if(magazineReloadFixture)magazineReloadFixture->Tick(input,handSample.owner,HandNanos(inputDeadline-handFrequency/10),HandNanos(inputDeadline),HandNanos(clock.QuadPart));
    if(weaponVisibilityProbe){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        // Count/mesh observation above may have completed after Gather began.
        // Refresh processing time only; original IPC evidence is immutable.
        auto processing=handSample;LARGE_INTEGER visibilityNow{};
        if(QueryPerformanceCounter(&visibilityNow))processing.nowNs=HandNanos(visibilityNow.QuadPart);
        auto sample=weaponVisibilityProbe->Tick(current,processing,selectedMeshes.Read(current,processing.nowNs),asset,
            rigPublication::ReadWeaponVisibilityReceipt(current,weaponVisibilityProbe->RequestId(),processing.nowNs));
        rigPublication::PublishWeaponVisibility(sample.intent);
        try{weaponVisibilitySample.store(std::make_shared<const WeaponVisibilityProbeSample>(std::move(sample)),std::memory_order_release);}
        catch(...){CancelWeaponVisibilityProbe();}
    }
    PhysicalReloadResult physicalResult;MagazinePhysicalResult magazineResult;Bc2PhysicalPumpResult pumpResult;Bc2PhysicalBoltResult boltResult;
    Bc2PhysicalFeedResult feedResult;
    // Deferred diagnostic mappings are now complete; this precedes all ordinary
    // magazine/feed/pump/sight filters and never changes the ActionOutput.
    fireTrace.record.mapped=(out.held&interaction::Fire)!=0;
    Bc2PhysicalPistolSlideResult pistolSlideResult;
    Bc2PhysicalFixedContainerResult fixedResult;
    Bc2BoltControllerContact boltContact;
    const bool pumpSelected=physicalPump&&(!ordinaryManualCycles||
        (ordinaryShotgunEquipment&&(ordinarySelectionReady||ordinaryTrackingRecovery)));
    const auto currentPump=pumpSelected?reloadFlowRuntime::ReadNativeCycleView(handSample.nowNs):std::nullopt;
    const auto pumpGates=EvaluatePumpActionGates(pumpSelected,physicalPump&&physicalPump->BlocksFire(),
        physicalPump&&physicalPump->HasCycleDebt(),currentPump?std::optional(currentPump->blocksFire):std::nullopt);
    const bool pumpDebt=pumpGates.blockActions;
    ApplyPumpActionGate(out,pumpDebt);
    std::optional<MagazineFamilyEvidence> physicalReplacementFamily;
    const ReloadStateOwner magazineOwner{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
    const auto magazineProfileId=magazinePhysical?reloadFlowRuntime::ReadMagazineRequestProfile(magazineOwner):std::nullopt;
    const auto magazineProfile=magazineProfileId?FindMagazineEquipment(*magazineProfileId):nullptr;
    if(physicalFixedContainer){
        Bc2PhysicalFixedContainerSample sample;sample.nativeOwner=magazineOwner;sample.input=handSample;sample.weapon=handItem;
        sample.geometrySequence=input.generation;sample.trackingEpoch=input.spaceGeneration;
        sample.grip=input.hands[0].squeeze>=.7f;sample.gripPressed=sample.grip&&!fixedContainerGripHeld;
        if(input.hands[0].squeeze<=.35f)fixedContainerGripHeld=false;else if(sample.grip)fixedContainerGripHeld=true;
        for(const auto& event:handLifecycle.released)if(event&&handPreviousLeft&&event->token==handPreviousLeft->token)
            sample.releaseEvidence=interaction::FixedContainerReleaseEvidence{*handPreviousLeft,*event,handSample};
        sample.cancel=!aimValid||!out.active||pumpDebt||sightPhase!=interaction::SightFlipPhase::Idle||
            (physicalReload&&physicalReload->BlocksEquipment())||(magazinePhysical&&magazinePhysical->BlocksEquipment())||
            !bodyInventory||!bodyHolster||bodyInventory->Pending()||bodyHolster->BlocksActions()||
            bodyHolster->TemporaryToolActive()||bool(pendingMode)||
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon));
        sample.finishLoading=(out.pressed&interaction::Reload)!=0;
        const auto frame=rigPublication::ReadBodyWeaponFrame(owner.soldier,owner.weak,owner.weapon);
        if(frame&&frame->aimEvidence){const auto& proof=*frame->aimEvidence;
            const auto end=std::find(proof.configurationPath.begin(),proof.configurationPath.end(),'\0');
            if(end!=proof.configurationPath.end()&&proof.configurationPathVerified&&proof.selectedRigVerified&&
               proof.equipment==owner.equipment&&proof.configurationSequence&&proof.configurationDeadlineNs>handSample.nowNs&&
               frame->ownerGeneration==rigEpoch&&frame->space==input.spaceGeneration&&frame->deadline>clock.QuadPart){
                sample.configurationPath={proof.configurationPath.data(),std::size_t(end-proof.configurationPath.begin())};
                sample.mesh=proof.meshPath;sample.rigFingerprint=proof.rigFingerprint;
                sample.equipment=FindFixedContainerEquipment(sample.configurationPath,sample.mesh,sample.rigFingerprint);
                sample.raw=frame->fixedContainer;
            }
        }
        if(const auto pouch=interaction::BodyAnchorHandPose(input,interaction::InteractionHand::Left))sample.bodyFromHand=*pouch;
        else sample.cancel=true;
        fixedResult=physicalFixedContainer->Tick(sample,handOwnership,handIntent);
        if(sample.equipment){out.held&=~interaction::Reload;out.pressed&=~interaction::Reload;}
        if(fixedResult.blocksFire){constexpr auto mask=interaction::Fire|interaction::AlternateFire;
            out.held&=~mask;out.pressed&=~mask;}
    }
    if(physicalFeed){
        Bc2PhysicalFeedSample sample;sample.owner=magazineOwner;sample.input=handSample;sample.weapon=handItem;
        feedResourceBinding=bodyInventory?bodyInventory->ResourceBinding(magazineOwner,handSample.nowNs):std::nullopt;
        sample.equipment=feedEquipment;sample.binding=feedResourceBinding;
        sample.bodyFromHand=interaction::BodyAnchorHandPose(input,interaction::InteractionHand::Left);
        sample.asset=asset;sample.grip=input.hands[0].squeeze>=.7f;sample.reloadPressed=(out.pressed&interaction::Reload)!=0;
        sample.selected=selectedMeshes.Read(magazineOwner,handSample.nowNs);
        sample.cancel=!sample.bodyFromHand||!aimValid||!out.active||pumpDebt||sightPhase!=interaction::SightFlipPhase::Idle||
            (physicalReload&&physicalReload->BlocksEquipment())||(magazinePhysical&&magazinePhysical->BlocksEquipment())||
            (magazineDetached&&magazineDetached->BlocksActions())||!bodyInventory||!bodyHolster||
            bodyInventory->Pending()||bodyInventory->AutomaticStowPending()||bodyHolster->Phase()!=BodyHolsterPhase::Held||
            bodyHolster->TemporaryToolActive()||bool(pendingMode)||
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon));
        const auto frame=rigPublication::ReadBodyWeaponFrame(owner.soldier,owner.weak,owner.weapon);
        if(frame&&frame->aimEvidence){const auto& proof=*frame->aimEvidence;
            sample.raw=frame->mechanismControllerMapping;sample.frameSequence=frame->generation;sample.rigFingerprint=proof.rigFingerprint;
            const auto end=std::find(proof.configurationPath.begin(),proof.configurationPath.end(),'\0');
            if(end!=proof.configurationPath.end())sample.configurationPath={proof.configurationPath.data(),std::size_t(end-proof.configurationPath.begin())};
            sample.meshPath=proof.meshPath;
            sample.selectedRigVerified=proof.configurationPathVerified&&proof.selectedRigVerified&&proof.equipment==owner.equipment&&
                proof.configurationSequence&&proof.configurationDeadlineNs>handSample.nowNs&&frame->ownerGeneration==rigEpoch&&
                frame->space==input.spaceGeneration&&frame->deadline>clock.QuadPart;
        }
        feedResult=physicalFeed->Tick(sample,handOwnership,handIntent);
        if(fireTrace.observing){const auto& d=feedResult.diagnostic;auto& r=fireTrace.record.interactions.feed;
            r.observed=true;r.exit=unsigned(d.exit);r.flags=d.flags;r.status=d.status;r.rawChecked=d.rawChecked;r.rawPassed=d.rawPassed;
            r.admissionChecked=d.admissionChecked;r.admissionPassed=d.admissionPassed;r.reloadPhase=d.reloadPhase;r.operation=d.operation;r.cancel=sample.cancel;
            r.selectionFailure=d.selectionFailure;r.policyEvaluated=d.policyEvaluated;
            r.idleChecked=d.idleChecked;r.idlePassed=d.idlePassed;
            fireTrace.record.feedCoverDistance=d.coverDistanceMeters;fireTrace.record.feedCoverAngle=d.coverAngleRadians;
            fireTrace.record.feedNeutralAgeNs=d.neutralAgeNs;
            fireTrace.record.feedSource=sample.raw.inputEvidence.sequence;fireTrace.record.feedSourceDeadlineNs=sample.raw.inputEvidence.deadlineNs;}
        if(feedResult.blocksFire){constexpr auto mask=interaction::Fire|interaction::AlternateFire|interaction::Reload;
            out.held&=~mask;out.pressed&=~mask;}
    }
    // An old actor's pending magazine can itself block family selection below.
    // Retirement is separate: exact native replacement + callback drain only.
    if(magazinePhysical||physicalFeed||physicalPistolSlide||physicalFixedContainer){const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        reloadFlowRuntime::RetireAmmoForReplacementActor(current);}
    if(magazinePhysical&&!pumpDebt&&!(physicalReload&&physicalReload->BlocksEquipment())&&!magazinePhysical->BlocksEquipment()&&!(magazineDetached&&magazineDetached->BlocksActions())){
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        if(magazineProfile&&magazineProfile->Ready())reloadFlowRuntime::SelectMagazineRequestProfile(magazineProfile->nativeId,current);
        else if(ordinaryShotgunEquipment)reloadFlowRuntime::SelectRequestFamily(ReloadNativeFamily::SpasTube,current);
    }
    BodyDrawResult bodyDraw;std::optional<BodyDrawSample> holsterBodySample;
    if(bodyInventory){
        BodyDrawSample sample;sample.owner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        sample.input=input;sample.hand=handSample;sample.gun=handItem;sample.verifiedFamily=familyBinding;
        // Interaction eligibility cannot retire physical inventory identity.
        // The body reader independently proves current native metadata while
        // these actions suspend gestures, presentation and pending requests.
        SetBodyInteractionInput(sample,aimValid,out.active,sightPhase==interaction::SightFlipPhase::Idle,out.pressed|out.held);
        sample.reloadBusy=fixedResult.blocksFire||feedResult.blocksEquipment||pumpDebt||(physicalReload&&physicalReload->BlocksEquipment())||(magazinePhysical&&magazinePhysical->BlocksEquipment())||(magazineDetached&&magazineDetached->BlocksActions());
        const auto pose=bodyHolster?rigPublication::ReadBodyWeaponFrame(owner.soldier,owner.weak,owner.weapon):rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
        if(pose&&pose->ownerGeneration==rigEpoch&&pose->space==input.spaceGeneration){
            if(const auto original=FindHandEvidence(pose->generation,owner.weapon))
                sample.visible=BodyVisibleRig{sample.owner,pose->generation,original->sample.observedNs,
                    std::min(original->sample.deadlineNs,HandNanos(pose->deadline))};
        }
        const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},
            [](void*,unsigned at,const char* expected){return Type(at,expected);}};
        if(bodyHolster){holsterBodySample=sample;bodyDraw.pending=bodyHolster->BlocksActions()||bodyInventory->Pending();bodyDraw.blockFire=bodyDraw.pending;
            if(bodyDraw.pending)ResetSupportGrip("body_holster");}
        else {bodyDraw=bodyInventory->Tick(memory,sample,handOwnership,handIntent);
            reloadFlowRuntime::PublishAmmoResourceBinding(bodyInventory->ResourceBinding(sample.owner,sample.hand.nowNs));}
        if(bodyDraw.blockFire){out.held&=~(interaction::Fire|interaction::AlternateFire|interaction::Reload);out.pressed&=~(interaction::Fire|interaction::AlternateFire|interaction::Reload);}
    }
    if(magazinePhysical){
        MagazinePhysicalSample sample;sample.entryProfileObserved=true;sample.entryProfile=magazineProfile;
        sample.nativeOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        sample.input=handSample;sample.weapon=handItem;sample.geometrySequence=input.generation;sample.trackingEpoch=input.spaceGeneration;
        sample.actionFlagsKnown=true;sample.actionHeld=out.held;sample.actionPressed=out.pressed;
        const WeaponModeMemory familyMemory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},
            [](void*,unsigned at,const char* expected){return Type(at,expected);}};
        if(magazineProfile&&magazineProfile->Ready())
            sample.family=ResolveMagazineFamily(familyMemory,sample.nativeOwner,handSample,handItem,*magazineProfile).value_or(MagazineFamilyEvidence{});
        // Exact current native-to-physical mapping remains available even when
        // an old SPAS cycle temporarily blocks native reload-family selection.
        if(sample.family.verified)physicalReplacementFamily=sample.family;
        sample.gripPressed=interaction::ReloadGripActive(input.hands[0].squeeze,handSample,handOwnership.Current(interaction::InteractionHand::Left));
        sample.ejectPressed=(out.pressed&interaction::Reload)!=0;
        sample.cancel=pumpDebt||bodyDraw.pending||!aimValid||!out.active||(magazinePhysicalFixture&&magazinePhysicalFixture->CancelConsumer())||
            (magazineResourceFixture&&magazineResourceFixture->CancelConsumer())||
            (inventoryReloadFixture&&inventoryReloadFixture->CancelConsumer())||
            (resourceInventoryFixture&&resourceInventoryFixture->CancelConsumer())||
            (physicalReload&&physicalReload->BlocksEquipment());
        sample.asset=asset;sample.meshes=selectedMeshes.Read(sample.nativeOwner,handSample.nowNs);
        sample.raw=rigPublication::ReadMagazineContact(sample.nativeOwner);
        if(const auto original=FindHandEvidence(sample.raw.inputEvidence.sequence,owner.weapon))sample.originalHandEvidence=original->sample;
        const auto pouch=bodyInventory?interaction::BodyAnchorHandPose(input,interaction::InteractionHand::Left):PhysicalReloadPouchPose(input);
        if(pouch)sample.bodyFromHand=*pouch;else sample.cancel=true;
        if(magazineDetached&&!magazinePhysical->ResourceBackend()){detachedReserve=reloadFlowRuntime::ReadReserve();
            detachedUsed=magazineDetached->Routes(detachedReserve,magazinePhysical->BlocksEquipment());}
        if(detachedUsed){if(magazineDetachedFixture&&magazineDetachedFixture->CancelConsumer())sample.cancel=true;
            // Only processing time advances after the fresh native count read;
            // never renew the controller packet or renderer contact lifetime.
            LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);sample.input.nowNs=HandNanos(processed.QuadPart);
            magazineResult=magazineDetached->Prepare(sample,detachedReserve,scope->nativeTick,owner.cache,handOwnership);}
        else magazineResult=magazinePhysical->Tick(sample,handOwnership,handIntent);
        if(magazinePhysicalFixture){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const auto now=HandNanos(stamp.QuadPart);
            magazinePhysicalFixture->Observe(magazinePhysical->ProbeState(now),rigPublication::ReadMagazinePackCounters(),now);
            if(magazinePhysicalFixture->CancelConsumer()){auto safety=handSample;safety.nowNs=now;
                magazinePhysical->Cancel(safety,handOwnership);magazineResult.tracking.target.reset();magazineResult.reloadHeld=false;
                magazineResult.blocksWeaponActions=magazinePhysical->BlocksEquipment();}}
        if(magazineResourceFixture){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const auto now=HandNanos(stamp.QuadPart);
            magazineResourceFixture->Observe(magazinePhysical->ResourceProbeState(now),rigPublication::ReadMagazinePackCounters(),now);
            if(magazineResourceFixture->CancelConsumer()){auto safety=handSample;safety.nowNs=now;
                magazinePhysical->Cancel(safety,handOwnership);magazineResult.tracking.target.reset();magazineResult.reloadHeld=false;
                magazineResult.blocksWeaponActions=magazinePhysical->BlocksEquipment();}}
        ApplyMagazinePhysicalActions(out,magazineProfile,magazineResult);
        if(inventoryReloadFixture){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const auto now=HandNanos(stamp.QuadPart);
            inventoryReloadFixture->Observe(magazinePhysical->ProbeState(now),rigPublication::ReadMagazinePackCounters(),now);}
    }
    if(physicalPistolSlide){
        Bc2PhysicalPistolSlideSample sample;sample.owner=magazineOwner;sample.input=handSample;sample.weapon=handItem;
        sample.previousLeft=handPreviousLeft;sample.lifecycle=handLifecycle;
        sample.equipment=feedEquipment;sample.binding=bodyInventory?bodyInventory->ResourceBinding(magazineOwner,handSample.nowNs):std::nullopt;
        sample.asset=asset;sample.grip=input.hands[0].squeeze>=.7f;sample.reloadPressed=(out.pressed&interaction::Reload)!=0;
        sample.selected=selectedMeshes.Read(magazineOwner,handSample.nowNs);
        sample.cancel=!aimValid||!out.active||bodyDraw.pending||pumpDebt||sightPhase!=interaction::SightFlipPhase::Idle||
            (physicalReload&&physicalReload->BlocksEquipment())||magazineResult.blocksWeaponActions;
        const auto frame=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
        if(frame&&frame->aimEvidence){const auto& proof=*frame->aimEvidence;
            sample.raw=frame->controllerMapping;sample.frameSequence=frame->generation;sample.rigFingerprint=proof.rigFingerprint;
            const auto end=std::find(proof.configurationPath.begin(),proof.configurationPath.end(),'\0');
            if(end!=proof.configurationPath.end())sample.configurationPath={proof.configurationPath.data(),std::size_t(end-proof.configurationPath.begin())};
            sample.meshPath=proof.meshPath;
            sample.selectedRigVerified=proof.configurationPathVerified&&proof.selectedRigVerified&&proof.equipment==owner.equipment&&
                proof.configurationSequence&&proof.configurationDeadlineNs>handSample.nowNs&&frame->ownerGeneration==rigEpoch&&
                frame->space==input.spaceGeneration&&frame->deadline>clock.QuadPart;
        }
        pistolSlideResult=physicalPistolSlide->Tick(sample,handOwnership,handIntent);
        if(pistolSlideResult.blocksFire){constexpr auto mask=interaction::Fire|interaction::AlternateFire;
            out.held&=~mask;out.pressed&=~mask;}
        if(holsterBodySample)holsterBodySample->reloadBusy|=pistolSlideResult.blocksEquipment;
    }
    if(physicalReload){
        // Shared right GunHold has just been renewed. RAW controller/pouch and
        // renderer contact stay in their respective original input epochs.
        PhysicalReloadSample sample;sample.replacementFamily=physicalReplacementFamily;
        sample.equipment=ordinaryManualCycles?ordinaryShotgunEquipment:DefaultSpasShotgunEquipment();
        sample.nativeOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        sample.input=handSample;sample.weapon=handItem;sample.geometrySequence=input.generation;sample.trackingEpoch=input.spaceGeneration;
        sample.actionFlagsKnown=true;sample.actionHeld=out.held;sample.actionPressed=out.pressed;
        sample.gripPressed=interaction::ReloadGripActive(input.hands[0].squeeze,handSample,handOwnership.Current(interaction::InteractionHand::Left));
        sample.cancel=pumpGates.cancelReload||(resourceInventoryFixture&&resourceInventoryFixture->CancelShell())||bodyDraw.pending||magazineResult.blocksWeaponActions||(physicalReloadFixture&&physicalReloadFixture->CancelConsumer())||!aimValid||!out.active||((out.pressed|out.held)&(interaction::Fire|interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon));
        sample.asset=asset;sample.meshes=selectedMeshes.Read(sample.nativeOwner,handSample.nowNs);
        sample.raw=rigPublication::ReadReloadContact(sample.nativeOwner);
        if(const auto original=FindHandEvidence(sample.raw.inputEvidence.sequence,owner.weapon))sample.originalHandEvidence=original->sample;
        const auto pouch=bodyInventory?interaction::BodyAnchorHandPose(input,interaction::InteractionHand::Left):PhysicalReloadPouchPose(input);
        if(pouch)sample.bodyFromHand=*pouch;else sample.cancel=true;
        // Record the same pure input predicates separately from the existing
        // cancellation decision. These labels never drive native policy.
        const auto reloadActions=out.pressed|out.held;
        sample.cancelFlags=(bodyDraw.pending?ReloadCancelBodyDraw:0u)|
            (magazineResult.blocksWeaponActions?ReloadCancelMagazineBusy:0u)|
            (((physicalReloadFixture&&physicalReloadFixture->CancelConsumer())||(resourceInventoryFixture&&resourceInventoryFixture->CancelShell()))?ReloadCancelFixture:0u)|
            (!aimValid?ReloadCancelAimInvalid:0u)|(!out.active?ReloadCancelInactive:0u)|
            ((reloadActions&interaction::Fire)?ReloadCancelFire:0u)|((reloadActions&interaction::Use)?ReloadCancelUse:0u)|
            ((reloadActions&interaction::NextWeapon)?ReloadCancelNextWeapon:0u)|((reloadActions&interaction::PreviousWeapon)?ReloadCancelPreviousWeapon:0u)|
            (!pouch?ReloadCancelNoPouch:0u)|(pumpGates.cancelReload?ReloadCancelPumpDebt:0u);
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
        if(armingEmptyFixture&&armingEmptyFixture->CancelConsumer()){sample.cancel=true;sample.cancelFlags|=ReloadCancelFixture;}
#endif
        physicalResult=physicalReload->Tick(sample,handOwnership,handIntent);
        if(fireTrace.observing){const auto d=physicalReload->Diagnostic();auto& r=fireTrace.record.interactions.shell;
            r.observed=true;r.cancelReason=d.cancelReason;r.supplyReason=d.supplyReason;r.flags=d.flags;r.sourceFlags=d.sourceFlags;
            r.bridgePhase=d.bridgePhase;r.cycle=d.cycle;r.active=d.active;r.reloadHeld=physicalResult.reloadHeld;r.ammoOwnsHand=physicalResult.ammoOwnsHand;
            fireTrace.record.shellSource=sample.raw.inputEvidence.sequence;fireTrace.record.shellSourceDeadlineNs=sample.raw.inputEvidence.deadlineNs;}
        // The receipt event is presentation-only. Keep the exact selected
        // manual cycle as audio owner through its stock finish tail; no native
        // reload/pump clock, hand claim or ammunition authority is changed.
        const auto audioGun=handOwnership.Current(interaction::InteractionHand::Right);
        const bool audioOwnsGun=audioGun&&audioGun->token.kind==interaction::HandClaimKind::GunHold&&
            audioGun->token.owner==sample.input.owner&&audioGun->token.item==sample.weapon&&
            audioGun->inputSequence==sample.input.sequence&&audioGun->deadlineNs>sample.input.nowNs;
        PublishShellReloadAudio(sample.nativeOwner,sample.input,sample.weapon,physicalReload->ProbeState(sample.input.nowNs),
            physicalResult.audio,(ordinaryShotgunEquipment||(!ordinaryManualCycles&&asset==SpasReloadAsset))&&audioOwnsGun&&aimValid&&out.active,
            ShotgunEquipmentAudio(sample.equipment));
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
        if(armingEmptyFixture){const auto policy=reloadFlowRuntime::ReadRequestProbeSnapshot();LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
            const auto now=HandNanos(stamp.QuadPart);physicalResult.reloadHeld=armingEmptyFixture->After(physicalReload->ProbeState(now),
                physicalResult.reloadHeld,policy?policy->phase:~0u,policy?policy->cycle:0,input.generation,now);
            if(armingEmptyFixture->CancelConsumer())CancelPhysicalReload(&physicalResult,ReloadCancelFixture);}
#endif
        if(physicalReloadFixture){LARGE_INTEGER observed{};QueryPerformanceCounter(&observed);const auto now=HandNanos(observed.QuadPart);
            physicalReloadFixture->Observe(physicalReload->ProbeState(now),now);if(physicalReloadFixture->CancelConsumer())CancelPhysicalReload(&physicalResult,ReloadCancelFixture);}
        for(unsigned n=0;n<physicalResult.feedbackCount;++n){
            if(writeFeedback&&writeFeedback(physicalResult.feedback[n]))++feedbackSent;else ++feedbackDropped;}
        // Native Reload is only the explicit 100ms cycle-start pulse in this mode.
        // Fire and equipment actions remain ordinary native actions after cancellation.
        if(ordinaryShotgunEquipment||(!ordinaryManualCycles&&asset==SpasReloadAsset)){out.held&=~interaction::Reload;out.pressed&=~interaction::Reload;}
        if(physicalResult.reloadHeld)out.held|=interaction::Reload;
    }
    if(resourceInventoryFixture&&magazinePhysical&&physicalReload&&bodyInventory){
        ResourceInventoryObservation observed;observed.reserve=reloadFlowRuntime::ReadReserve();
        LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const auto now=HandNanos(stamp.QuadPart);
        observed.magazine=magazinePhysical->ResourceProbeState(now);observed.shell=physicalReload->ProbeState(now);
        observed.shellBlocks=physicalReload->BlocksEquipment();
        resourceInventoryFixture->Observe(observed,rigPublication::ReadMagazinePackCounters(),now);
#ifdef FVR_BC2_SHELL_FOCUS_LOSS_PROBE
        if(const auto witness=resourceInventoryFixture->ShellFocusReady(now))
            shellFocusReady.store(std::make_shared<const ShellFocusWitness>(*witness),std::memory_order_release);
#endif
    }
    if(pumpSelected){
        // Shell/resource consumers above may advance the arbiter's processing
        // clock. Preserve the original controller evidence; refresh only now.
        RefreshHandProcessingTime();
        Bc2PhysicalPumpSample sample;sample.nativeOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        sample.input=handSample;sample.item=handItem;sample.asset=asset;
        sample.raw=rigPublication::ReadPumpContact(sample.nativeOwner);
        sample.grip=interaction::ReloadGripActive(input.hands[0].squeeze,handSample,handOwnership.Current(interaction::InteractionHand::Left));
        sample.cancel=(physicalPumpFixture&&physicalPumpFixture->Failed())||(resourcePumpCombination&&resourceInventoryFixture&&resourceInventoryFixture->CancelConsumer())||!aimValid||!out.active||bodyDraw.pending||(physicalReload&&physicalReload->BlocksEquipment())||physicalResult.ammoOwnsHand||magazineResult.blocksWeaponActions||
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon));
        pumpResult=physicalPump->Tick(sample,handOwnership,handIntent);
        PublishMechanismAudio(sample.nativeOwner,sample.input,sample.item,mechanismAudio::Cue::Spas,
            pumpResult.audio,!sample.cancel&&ordinarySelectionReady,ShotgunEquipmentAudio(ordinaryShotgunEquipment));
        if(physicalPumpFixture){LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
            physicalPumpFixture->Observe(pumpResult,reloadFlowRuntime::ReadNativeCycleView(now),rigPublication::ReadPumpPackCounters(),now);}
        if(resourcePumpCombination&&resourceInventoryFixture){LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
            resourceInventoryFixture->ObservePump(pumpResult,reloadFlowRuntime::ReadNativeCycleView(now),rigPublication::ReadPumpPackCounters(),now);
#ifdef FVR_BC2_PUMP_RECEIVER_GAP_PROBE
            if(const auto witness=resourceInventoryFixture->ReceiverGapReady(now))receiverGapReady.store(std::make_shared<const PumpReceiverGapWitness>(*witness),std::memory_order_release);
#endif
        }
        // A Fire intent may cancel shell loading above, but the shot waits for
        // its exact native retirement before pump observation can own updates.
        ApplyPumpActionGate(out,pumpResult.blocksFire);
        ApplyPumpResourceHandoffGate(out,(physicalReload&&physicalReload->BlocksEquipment())||magazineResult.blocksWeaponActions);
    }
    if(ordinaryManualCycles&&!ordinaryShotgunEquipment&&physicalBoltCalibration&&asset==physicalBoltCalibration->asset&&ordinaryBoltPlayer){
        boltContact=ordinaryBoltContact;boltResult=ordinaryBoltResult;
        if(ordinaryBoltSample&&!ordinaryBoltEarlyTick){
            if(RefreshHandProcessingTime())ordinaryBoltSample->input.nowNs=handSample.nowNs;
            const bool magazineHandoff=physicalBoltCustody==interaction::BoltCustodyPhase::Reloading||
                physicalBoltCustody==interaction::BoltCustodyPhase::ReloadResumePending;
            ordinaryBoltSample->cancel=ordinaryBoltSample->cancel||!aimValid||!out.active||bodyDraw.pending||
                (physicalReload&&physicalReload->BlocksEquipment())||(!magazineHandoff&&magazineResult.blocksWeaponActions);
            boltResult=physicalBolt->Tick(*ordinaryBoltSample,handOwnership,handIntent);ordinaryBoltPlayer->Observe(boltResult);
            physicalBoltCustody=boltResult.tracking.custody;
            if(boltResult.custodyChange&&boltResult.custodyChange->transaction.accepted)HandChanged("bolt_player_return");
        }
        if(ordinaryBoltSample)PublishMechanismAudio(ordinaryBoltSample->nativeOwner,ordinaryBoltSample->input,
            ordinaryBoltSample->item,mechanismAudio::Cue::M95,boltResult.audio,!ordinaryBoltSample->cancel&&ordinarySelectionReady,nullptr);
        if(ordinaryBoltInputFixture){LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
            ordinaryBoltInputFixture->Observe(boltResult,reloadFlowRuntime::ReadNativeCycleView(now),rigPublication::ReadBoltPackCounters(),now);}
        ApplyPumpActionGate(out,ordinaryBoltPlayer->BlocksFire());
        const auto triggerGun=handOwnership.Current(interaction::InteractionHand::Right);
        const bool currentTriggerGun=triggerGun&&triggerGun->token.kind==interaction::HandClaimKind::GunHold&&
            triggerGun->token.owner==handSample.owner&&triggerGun->token.item==handItem&&
            triggerGun->inputSequence==handSample.sequence&&triggerGun->deadlineNs>handSample.nowNs&&!handSample.released[1];
        ApplyPumpResourceHandoffGate(out,!ordinarySelectionReady||!ordinaryBoltPlayer->ReferencesReady()||boltResult.blocksFire||!currentTriggerGun);
    }
    if(ordinaryManualCycles&&ordinaryShotgunEquipment)ApplyPumpResourceHandoffGate(out,!ordinarySelectionReady);
    if(!pumpSelected&&!(ordinaryManualCycles&&!ordinaryShotgunEquipment&&physicalBoltCalibration&&asset==physicalBoltCalibration->asset))mechanismAudio::Publish({});
    if(!ordinaryManualCycles&&physicalBolt&&physicalBoltCalibration){
        Bc2PhysicalBoltSample sample;sample.nativeOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        LARGE_INTEGER boltProcessed{};QueryPerformanceCounter(&boltProcessed);handSample.nowNs=HandNanos(boltProcessed.QuadPart);
        sample.input=handSample;sample.item=handItem;sample.asset=asset=="M95_sp"?std::string_view(physicalBoltCalibration->asset):std::string_view{};sample.mesh=physicalBoltCalibration->mesh;
        boltContact=rigPublication::ReadBoltContact(sample.nativeOwner);sample.raw=boltContact.raw;
        auto body=rigPublication::ReadEyeBase(owner.soldier,owner.weak,input.spaceGeneration);
        if(body)for(unsigned n=0;n<3;++n)body->values[3][n]/=input.worldUnitsPerMeter;
        const auto wrists=body?BoltCurrentWrists(boltContact,input,*body):std::nullopt;
        if(wrists){sample.wristWorld=*wrists;sample.weaponWorld=boltContact.weaponWorldMeters;physicalBoltContactStarted=true;}
        sample.gunContacts[0]={{2,handItem.id},handSample.sequence,handSample.deadlineNs,!handSample.released[0]};
        sample.gunContacts[1]={{1,handItem.generation},handSample.sequence,handSample.deadlineNs,!handSample.released[1]};
        sample.grip=input.hands[1].squeeze>=.7f;
        sample.cancel=!wrists||(physicalBoltFixture&&physicalBoltFixture->Failed())||asset!="M95_sp"||!aimValid||!out.active||
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon));
        if(wrists&&physicalBoltFixture){const auto grips=physicalBoltFixture->OriginalGripReferences();
            if(grips)sample.custodyTransfer=BuildBoltCustodyTransfer(handSample,handItem,boltContact,*grips,handOwnership,
                physicalBoltFixture->WantsEnter(),physicalBoltFixture->WantsReturn(),handIntent);}
        if(physicalBoltContactStarted)boltResult=physicalBolt->Tick(sample,handOwnership,handIntent);
        else {boltResult.tracking={true,sample.nativeOwner,handSample,physicalBoltCalibration};boltResult.blocksFire=true;}
        physicalBoltCustody=boltResult.tracking.custody;
        if(physicalBoltFixture){LARGE_INTEGER processed{};QueryPerformanceCounter(&processed);const auto now=HandNanos(processed.QuadPart);
            physicalBoltFixture->Observe(boltResult,reloadFlowRuntime::ReadNativeCycleView(now),rigPublication::ReadBoltPackCounters(),now);}
        ApplyPumpActionGate(out,boltResult.blocksFire);
    }
    // The renderer's authored reference is bound to the original exact
    // equipment/configuration/rig lease; ReadWeaponSupportFrame revalidates it.
    // It admits this contact to the SAME support/claim policy, without enabling
    // any native muzzle, aim-axis, sight, reload or holster feature.
    auto supportEvidence=rigPublication::ReadWeaponSupportFrame(owner.soldier,owner.weak,owner.weapon);
    if(!interaction::WeaponFeatureEnabled(coreProfile,interaction::WeaponFeature::SupportGrip)&&
       (!supportEvidence||!supportEvidence->authoredSupportReference)&&
       !(physicalBolt&&physicalBoltCalibration&&boltContact.raw.valid&&
         (asset==physicalBoltCalibration->asset||(!ordinaryManualCycles&&asset=="M95_sp"))))supportEvidence.reset();
    const auto supportContact=supportEvidence&&supportEvidence->ownerGeneration==rigEpoch&&supportEvidence->space==input.spaceGeneration?
        supportEvidence->support:interaction::SupportGripContact{};
    std::optional<WeaponModeCommand> physicalMode;std::uint64_t physicalRequest=0;
    bool sightOwnsHand=false;rigPublication::SightPreview sightPreview;
    if(fireTrace.observing&&sightFlip){auto& r=fireTrace.record.interactions.sight;r.observed=true;r.phase=unsigned(sightPhase);
        r.admissionFlags=(fixedResult.ammoOwnsHand?1u:0u)|(fixedResult.mechanismOwnsHand?2u:0u)|(boltResult.ownsGunCustody?4u:0u)|
            (pumpResult.ownsHand?8u:0u)|(physicalResult.ammoOwnsHand?16u:0u)|(magazineResult.ownsLeftHand?32u:0u)|
            (magazineResult.blocksWeaponActions?64u:0u)|(bodyDraw.pending?128u:0u);}
    if(sightFlip&&!fixedResult.ammoOwnsHand&&!fixedResult.mechanismOwnsHand&&!boltResult.ownsGunCustody&&!pumpResult.ownsHand&&!physicalResult.ammoOwnsHand&&!magazineResult.ownsLeftHand&&!magazineResult.blocksWeaponActions&&!bodyDraw.pending){
        const auto sightEvidence=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
        // Sight preference and support distance come from one immutable pose.
        // Current and original-source tracking are independently checked before
        // ownership; no left-hand eligibility is inferred from the gun alone.
        const auto* sightAdapter=familyBinding?SightAdapterForFamily(familyBinding->family):nullptr;
        const auto contact=sightAdapter&&SightAdapterEnabled(*sightAdapter)&&sightEvidence&&sightEvidence->ownerGeneration==rigEpoch&&sightEvidence->space==input.spaceGeneration?
            sightEvidence->sight:rigPublication::WeaponSightContact{};
        if(!sightPolicy&&contact.valid){
            interaction::SightFlipConfig config;config.pivotMeters=contact.pivotMeters;config.axis=contact.axis;
            config.grabRadiusMeters=.08f;config.holdRadiusMeters=.20f;config.minLeverMeters=.015f;
            config.thresholdRadians=.40f;config.hysteresisRadians=.08f;config.maxStepRadians=.70f;
            config.detentHoldNs=70000000;config.gestureTimeoutNs=2500000000;config.ackTimeoutNs=1500000000;config.maxSampleGapNs=250000000;
            sightPolicy.emplace(config);
        }
        const bool occupied=sightPhase!=interaction::SightFlipPhase::Idle;
        const bool preferred=occupied||(contact.valid&&contact.contactDistanceMeters<=.08f&&(!supportContact.valid||contact.contactDistanceMeters<supportContact.distanceMeters));
        const bool cancelling=((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon))!=0;
        interaction::SightFlipSample live;
        live.owner={(std::uint64_t(owner.weak)<<32)|owner.soldier,rigEpoch,familyBinding?familyBinding->persistent:0,input.spaceGeneration};
        live.sequence=input.generation;live.nowNs=std::int64_t(GetTickCount64())*1000000;
        live.focused=input.focused&&!cancelling;live.tracked=input.headValid&&input.hands[0].gripTracked&&input.hands[1].gripTracked&&input.hands[1].aimTracked&&(input.hands[0].active&interaction::Squeeze);
        live.squeeze=input.hands[0].squeeze;live.nativeModeValid=bool(familyBinding);
        live.nativeMode=familyBinding&&familyBinding->selectedSecondary?interaction::SightMode::Secondary:interaction::SightMode::Primary;
        live.acknowledgedRequest=sightAcknowledged;
        // Render publication usually trails the current XR packet. Its local
        // hand point must consume that same packet's squeeze, never a newer one.
        const auto packet=sightPackets.Update(
            {live,owner.weapon,inputDeadline,out.active||occupied,sightPhase==interaction::SightFlipPhase::AwaitingAcknowledgement,sightPhase==interaction::SightFlipPhase::Latched},
            {contact.valid,preferred,contact.generation,contact.deadline,contact.handLocalMeters,contact.contactDistanceMeters},
            clock.QuadPart);
        interaction::SightFlipResult sightResult;
        if(sightPolicy){
            // Bind the hinge from the same verified publication as the press,
            // immediately before a new gesture. Never cache an equip-transition
            // pivot for the lifetime of the injected session, nor move a hinge
            // while a gesture/request is in flight.
            if(packet.advanced&&packet.sample.contactValid&&packet.geometrySequence==contact.generation)
                sightPolicy->SetIdleGeometry(contact.pivotMeters,contact.axis);
            // Do not commit a policy edge, preview or native mode request until
            // its hand ownership succeeds. The original contact retains its
            // publication generation and deadline even when raw input is newer.
            auto proposed=*sightPolicy;
            sightResult=proposed.Update(packet.sample);
            if(fireTrace.observing)fireTrace.record.interactions.sight.evaluated=true;
            gatherSightAck=live.acknowledgedRequest;
            if(sightResult.phase!=interaction::SightFlipPhase::Idle){
                const bool geometry=packet.paired&&contact.valid&&packet.geometrySequence==contact.generation;
                // A matched native acknowledgement needs no old-mode geometry.
                // Ownership here is a held interaction reservation, not a pose.
                const bool acknowledged=live.acknowledgedRequest&&
                    (sightPhase==interaction::SightFlipPhase::AwaitingAcknowledgement||sightResult.committedMode);
                const auto source=geometry?packet.geometrySequence:acknowledged?input.generation:0;
                const auto expiry=geometry?contact.deadline:inputDeadline;
                const bool latched=sightPhase==interaction::SightFlipPhase::Latched&&sightResult.phase==interaction::SightFlipPhase::Latched;
                bool owned=latched?interaction::RenewLatchedSightReservation(handOwnership,handSample,handItem,sightPhase,sightResult):
                    source&&OwnLeft(interaction::HandClaimKind::Sight,sightResult.grabbed,source,expiry,owner.weapon);
                // The policy must first confirm the exact pending request, native
                // target and bounded sample gap. Recovery cannot dispatch again.
                if(!owned&&sightLeaseContinuation&&sightResult.committedMode)
                    owned=ReserveAcknowledgedSight();
                if(!owned&&!sightResult.grabbed){
                    const auto existing=handOwnership.Current(interaction::InteractionHand::Left);
                    // Expected mode switch may occur on a duplicate raw packet;
                    // keep the original unexpired reservation without new pose.
                    owned=acknowledged&&existing&&existing->token.kind==interaction::HandClaimKind::Sight;
                }
                if(fireTrace.observing){auto& r=fireTrace.record.interactions.sight;r.ownershipAttempted=true;r.ownershipAccepted=owned;}
                if(!owned){
                    // Cancel the original pending interaction, not the trial
                    // state that may already have consumed its acknowledgement.
                    proposed=*sightPolicy;sightResult=proposed.Update({});
                    sightAcknowledged=0;HandChanged("sight_denied");ReleaseSightOwnership();
                }
            }else ReleaseSightOwnership();
            *sightPolicy=proposed;sightPhase=sightResult.phase;
            gatherSightRequest=sightResult.request?sightResult.request->id:0;gatherSightCommit=bool(sightResult.committedMode);
            RecordSightGesture(packet.sample,sightResult,contact,owner.weapon,packet.advanced);
            if(sightResult.grabbed&&packet.paired&&packet.geometrySequence==contact.generation&&contact.previewValid){
                sightGrasp=interaction::SightGraspBinding::Begin(contact.pivotMeters,contact.axis,contact.sightLocalMeters,
                    contact.handLocalFrameMeters,contact.graspPointMeters,contact.palmPointWristMeters,sightAdapter?sightAdapter->travelRadians:1.570796327f);
                sightAdapterAtGrab=sightAdapter;sightFrontAtGrab=contact.frontLocalMeters;sightPalmAtGrab=contact.palmPointWristMeters;sightGraspMode=packet.sample.nativeMode;++sightGraspToken;
                sightGraspGeneration=packet.geometrySequence;
                const auto raw=interaction::SightVisualHandCapture{contact.rawHandLocalFrameMeters,contact.rawPalmPointWristMeters,packet.geometrySequence};
                sightVisual=sightAdapter&&sightAdapter->assembly==SightAssemblyKind::Subtree?
                    interaction::SightVisualHandoff::BeginWithGeometry(contact.sightLocalMeters,sightGraspMode,contact.pivotMeters,contact.axis,sightAdapter->travelRadians,raw):
                    interaction::SightVisualHandoff::Begin(contact.sightLocalMeters,sightGraspMode,raw);
            }
            // The visual attachment remains captured while native mode selection
            // and its later animation finish. This is a current held reservation,
            // never replacement contact evidence or an additive native write.
            if(sightGrasp&&sightVisual&&sightPhase!=interaction::SightFlipPhase::Idle){
                std::optional<math::Matrix4> nativeRear;std::optional<interaction::SightVisualRawHand> rawHand;
                if(familyBinding&&sightEvidence&&sightEvidence->ownerGeneration==rigEpoch&&sightEvidence->space==input.spaceGeneration&&
                   sightEvidence->generation>=sightGraspGeneration&&sightEvidence->generation<=input.generation&&
                   sightEvidence->deadline>clock.QuadPart&&sightEvidence->nativeSight.valid&&
                   sightEvidence->nativeSight.physicalItem==familyBinding->persistent){
                    nativeRear=sightEvidence->nativeSight.rear;
                    if(sightEvidence->nativeSight.rawHandValid)
                        rawHand=interaction::SightVisualRawHand::Fresh(sightEvidence->nativeSight.rawHand,
                            sightEvidence->generation,input.generation,clock.QuadPart,sightEvidence->deadline);
                }
                const auto progress=sightVisual->Update(sightPhase,sightResult.signedRadians,input.predictedNs,nativeRear,rawHand);
                const auto preview=progress?sightGrasp->Evaluate(progress->appliedRadians,sightGraspMode):std::nullopt;
                const math::Vec3 pivot{sightFrontAtGrab.values[3][0],sightFrontAtGrab.values[3][1],sightFrontAtGrab.values[3][2]};
                const math::Vec3 axis{sightFrontAtGrab.values[0][0],sightFrontAtGrab.values[0][1],sightFrontAtGrab.values[0][2]};
                const bool subtree=sightAdapterAtGrab&&sightAdapterAtGrab->assembly==SightAssemblyKind::Subtree;
                const auto front=preview&&!subtree?interaction::RotateAboutHinge(sightFrontAtGrab,pivot,axis,-preview->appliedRadians):std::nullopt;
                const auto reservation=handOwnership.Current(interaction::InteractionHand::Left);
                const auto visualDeadline=reservation?std::min(inputDeadline,HandTicks(reservation->deadlineNs)):0;
                if(preview&&(subtree||front)&&reservation&&reservation->token.kind==interaction::HandClaimKind::Sight&&visualDeadline>clock.QuadPart){
                    sightPreview.valid=true;sightPreview.token=sightGraspToken;sightPreview.weapon=owner.weapon;
                    sightPreview.generation=input.generation;sightPreview.deadline=visualDeadline;
                    sightPreview.physicalItem=familyBinding?familyBinding->persistent:0;
                    sightPreview.grabGeneration=sightGraspGeneration;sightPreview.phase=sightPhase;
                    sightPreview.adapter=sightAdapterAtGrab;sightPreview.rear=preview->sight;if(front)sightPreview.front=*front;sightPreview.wrist=preview->wrist;
                    sightPreview.palmPointWrist=sightPalmAtGrab;sightPreview.graspPoint=preview->graspPoint;
                    sightPreview.angle=preview->appliedRadians;sightPreview.gestureAngle=sightResult.signedRadians;
                    sightPreview.nativeObserved=progress->nativeObserved;sightPreview.nativeProgress=progress->nativeProgressRadians;
                    sightPreview.rawObserved=progress->rawObserved;sightPreview.rawProgress=progress->rawProgressRadians;sightPreview.rawGeneration=progress->rawGeneration;
                }
            }else if(sightPhase==interaction::SightFlipPhase::Idle){sightGrasp.reset();sightVisual.reset();}
            if(sightResult.cancelled){
                ++sightCancellations;sightAcknowledged=0;
                if(pendingMode&&modeRecords[*pendingMode].gesture==sightResult.cancelledRequest){modeRecords[*pendingMode].cancelled=true;pendingMode.reset();}
            }
            if(sightResult.committedMode){++sightCommits;sightAcknowledged=0;}
            if(sightResult.request){
                const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},[](void*,unsigned at,const char* expected){return Type(at,expected);}};
                const auto mode=weaponModeBindingVerified?ResolveWeaponMode(memory,owner.soldier,owner.weapon):std::nullopt;
                const auto expected=sightResult.request->target==interaction::SightMode::Secondary?33u:36u;
                if(mode&&mode->action==expected&&!pendingMode){
                    physicalMode=mode;physicalRequest=sightResult.request->id;++sightRequests;
                }else{++modeRejections;CancelSight();if(fireTrace.observing)fireTrace.record.interactions.sight.dispatchRejected=true;}
            }
            const auto claim=handOwnership.Current(interaction::InteractionHand::Left);
            sightOwnsHand=sightPhase!=interaction::SightFlipPhase::Idle&&claim&&claim->token.kind==interaction::HandClaimKind::Sight;
            if(!sightOwnsHand)sightPreview={}; // Dispatch rejection/cancellation invalidates the same publication.
        }
        SightRecord record;
        record.ms=GetTickCount64();record.generation=input.generation;record.contactGeneration=contact.generation;
        record.coherentGeneration=packet.sample.sequence;record.request=sightResult.request?sightResult.request->id:0;
        record.weapon=owner.weapon;record.phase=unsigned(sightResult.phase);record.cancel=unsigned(sightResult.reason);
        record.packetReason=unsigned(packet.reason);record.distance=contact.contactDistanceMeters;record.angle=sightResult.signedRadians;
        record.squeeze=live.squeeze;record.pairedSqueeze=packet.sample.squeeze;record.supportDistance=supportContact.distanceMeters;
        record.contact=contact.valid;record.grabbed=sightResult.grabbed;record.committed=bool(sightResult.committedMode);
        record.preferred=preferred;record.tracked=live.tracked;record.familyValid=bool(familyBinding);
        record.paired=packet.paired;record.advanced=packet.advanced;
        RecordSight(record,sightResult.grabbed||sightResult.request||sightResult.cancelled||sightResult.committedMode||sightResult.released);
        if(fireTrace.observing){auto& r=fireTrace.record.interactions.sight;r.packetObserved=true;r.phase=unsigned(sightPhase);r.policyPhase=record.phase;
            r.reason=record.cancel;r.packetReason=record.packetReason;r.request=record.request;r.familyValid=record.familyValid;
            r.adapterEnabled=sightAdapter&&SightAdapterEnabled(*sightAdapter);r.contact=record.contact;r.preferred=record.preferred;
            r.paired=record.paired;r.advanced=record.advanced;r.grabbed=record.grabbed;r.committed=record.committed;
            r.cancelled=sightResult.cancelled||r.cancelled;r.released=sightResult.released||r.released;
            fireTrace.record.sightDistance=record.distance;fireTrace.record.sightAngle=record.angle;
            fireTrace.record.sightSource=contact.generation;fireTrace.record.sightSourceDeadlineNs=HandNanos(contact.deadline);}
    }
    if(sightOwnsHand){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}
    const auto boltRawInput=input; // Preserve actual prepared controller pose before support guidance.
    bool supportHeld=false;std::uint64_t supportToken=0;
    if(twoHandGrip){
        const auto contact=supportContact;
        LARGE_INTEGER supportClock{};QueryPerformanceCounter(&supportClock);
        const bool handBusy=fixedResult.ammoOwnsHand||fixedResult.mechanismOwnsHand||feedResult.ownsHand||pistolSlideResult.ownsHand||boltResult.tracking.custody==interaction::BoltCustodyPhase::Manipulating||pumpResult.ownsHand||physicalResult.ammoOwnsHand||MagazineBlocksSupport(magazineResult,HandNanos(supportClock.QuadPart));
        const bool cancel=bodyDraw.pending||sightOwnsHand||!aimValid||!out.active||
            ((out.pressed|out.held)&(interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon));
        auto proposed=supportGrip;
        const interaction::SupportGripOwner supportOwner{(std::uint64_t(owner.weak)<<32)|owner.soldier,rigEpoch,owner.weapon};
        std::optional<interaction::SupportGripResult> pumpSupport;
        std::optional<interaction::InputFrame> pumpReturnInput;
        if(pumpSelected){
            const auto gun=handOwnership.Current(interaction::InteractionHand::Right);
            if(gun&&!cancel&&!handBusy&&!handOwnership.Current(interaction::InteractionHand::Left)){
                if(const auto submitted=physicalPump->SupportReturnStarted(handSample,*gun))
                    pumpReturnInput=pumpAimContinuation.ApplyReturning(boltRawInput,handSample,*gun,*submitted);
            }
            const auto source=supportEvidence?FindHandEvidence(supportEvidence->generation,owner.weapon):nullptr;
            pumpSupport=physicalPump->ContinueSupport(supportOwner,pumpReturnInput?*pumpReturnInput:input,contact,source?source->sample:interaction::HandInteractionSample{},
                {2,std::uint64_t(owner.weapon)},handOwnership,proposed,handIntent,cancel||handBusy);
            if(pumpSupport)HandChanged("pump_support_return");
        }
        std::optional<interaction::SupportGripResult> shellSupport;
        if(physicalReload&&!pumpSupport){
            const auto source=supportEvidence?FindHandEvidence(supportEvidence->generation,owner.weapon):nullptr;
            // Pump/native polling may have advanced the shared arbiter since
            // shell Tick. Keep input evidence unchanged; refresh processing now.
            const bool current=RefreshHandProcessingTime();
            shellSupport=physicalReload->ContinueSupport(supportOwner,input,contact,source?source->sample:interaction::HandInteractionSample{},
                {2,std::uint64_t(owner.weapon)},handOwnership,proposed,handIntent,cancel||handBusy||!current,handSample);
            if(shellSupport)HandChanged("shell_support_return");
        }
        std::optional<interaction::SupportGripResult> boltSupport;
        if(boltResult.custodyChange&&boltResult.custodyChange->transaction.accepted&&
           boltResult.tracking.custody==interaction::BoltCustodyPhase::Returned&&boltResult.custodyChange->companion){
            const auto gun=handOwnership.Current(interaction::InteractionHand::Right);
            if(gun)boltSupport=proposed.AdoptHeld(supportOwner,input,contact,handSample,boltContact.raw.input,
                *boltResult.custodyChange->companion,*gun);
            if(boltSupport&&boltSupport->holding)HandChanged("bolt_support_return");
        }
        auto supported=boltSupport?*boltSupport:pumpSupport?*pumpSupport:shellSupport?*shellSupport:proposed.Update(supportOwner,input,contact,cancel,handBusy);
        if(supported.holding){
            const bool owned=supportEvidence&&OwnLeft(interaction::HandClaimKind::WeaponSupport,supported.engaged,
                supportEvidence->generation,supportEvidence->deadline,owner.weapon,&supported,cancel);
            if(fireTrace.observing){auto& r=fireTrace.record.interactions.support;r.ownershipAttempted=true;r.ownershipAccepted=owned;}
            if(!owned){
                // Re-evaluate the original state with cancellation: a rejected
                // trial grab is not logged as an accepted grab/release pair.
                supported=supportGrip.Update(supportOwner,input,contact,true);
                proposed=supportGrip;ReleaseLeftOwnership(interaction::HandClaimKind::WeaponSupport);
            }
        }else ReleaseLeftOwnership(interaction::HandClaimKind::WeaponSupport);
        supportGrip=proposed;
        // Support ownership moves to the fore-end mechanism, but that handoff
        // must not rotate the gun under stationary raw controllers. Preserve
        // only the exact previous support/gun pair's aim correction; pump
        // travel still reads the independent original left-controller pose.
        if(pumpSelected){
            const auto left=handOwnership.Current(interaction::InteractionHand::Left);
            const auto gun=handOwnership.Current(interaction::InteractionHand::Right);
            if(supported.holding&&left&&gun){
                if(pumpAimContinuation.Capture(boltRawInput,supported.input,handSample,*left,*gun))++pumpAimCaptures;
            }
            else if(pumpResult.ownsHand&&left&&gun){
                if(const auto guided=pumpAimContinuation.Apply(boltRawInput,handSample,*left,*gun)){supported.input=*guided;++pumpAimApplications;}
                else ++pumpAimRejected;
            }else if(pumpReturnInput){supported.input=*pumpReturnInput;++pumpAimApplications;}
            else pumpAimContinuation.Reset();
        }else pumpAimContinuation.Reset();
        if(ordinaryBoltInputFixture){LARGE_INTEGER supportObserved{};QueryPerformanceCounter(&supportObserved);
            ordinaryBoltInputFixture->ObserveSupport(supported.holding,HandNanos(supportObserved.QuadPart));}
        if(physicalBoltFixture){LARGE_INTEGER supportObserved{};QueryPerformanceCounter(&supportObserved);
            physicalBoltFixture->ObserveSupport(supported.holding,HandNanos(supportObserved.QuadPart));}
        if(pumpSelected)physicalPump->BindSupport(pumpResult.tracking,supported,handOwnership);
        if(physicalPumpFixture){LARGE_INTEGER supportObserved{};QueryPerformanceCounter(&supportObserved);
            physicalPumpFixture->ObserveSupport(pumpResult.tracking,supported,HandNanos(supportObserved.QuadPart));}
        if(resourcePumpCombination&&resourceInventoryFixture){LARGE_INTEGER supportObserved{};QueryPerformanceCounter(&supportObserved);
            const auto source=supportEvidence?FindHandEvidence(supportEvidence->generation,owner.weapon):nullptr;
            resourceInventoryFixture->ObserveSupport(pumpResult.tracking,supported,handSample,source?source->sample:interaction::HandInteractionSample{},
                handOwnership.Current(interaction::InteractionHand::Left),handOwnership.Current(interaction::InteractionHand::Right),shellSupport.has_value(),HandNanos(supportObserved.QuadPart));}
        if(supported.holding)++supportSamples;if(supported.engaged)++supportGrabs;if(supported.released)++supportReleases;
        auto restored=supported.input;restored.hands[1].grip.orientation=input.hands[1].grip.orientation;restored.hands[1].aim.orientation=input.hands[1].aim.orientation;
        if(std::memcmp(&restored,&input,sizeof(input)))++supportPreservationFailures;
        const auto now=GetTickCount64();
        SupportEvent evidence;
        evidence.sample={now,input.generation,contact.distanceMeters,input.hands[0].squeeze,supported.correctionRadians,contact.valid,supported.holding,supported.engaged,supported.released,input.hands[1].aim.orientation,supported.input.hands[1].aim.orientation,itemProfileCandidate?itemProfileCandidate->telemetryKind:0,unsigned(supported.reason),out.pressed|out.held,supported.token};
        evidence.eventMs=now;evidence.previousToken=lastSupportSample.sample.holding?lastSupportSample.sample.token:0;
        evidence.rigEpoch=rigEpoch;evidence.space=input.spaceGeneration;evidence.actor=owner.soldier;evidence.weak=owner.weak;evidence.weapon=owner.weapon;
        evidence.cancelFlags=(sightOwnsHand?1u:0u)|(!aimValid?2u:0u)|(!out.active?4u:0u)|(((out.pressed|out.held)&interaction::Use)?8u:0u)|(((out.pressed|out.held)&interaction::NextWeapon)?16u:0u)|(((out.pressed|out.held)&interaction::PreviousWeapon)?32u:0u)|(physicalResult.ammoOwnsHand?64u:0u)|(MagazineBlocksSupport(magazineResult,HandNanos(supportClock.QuadPart))?128u:0u);
        evidence.focused=input.focused;evidence.headTracked=input.headValid;evidence.leftGripTracked=input.hands[0].gripTracked;evidence.rightGripTracked=input.hands[1].gripTracked;evidence.rightAimTracked=input.hands[1].aimTracked;
        evidence.leftGrip=input.hands[0].grip.position;evidence.rightGrip=input.hands[1].grip.position;
        if(fireTrace.observing){auto& r=fireTrace.record.interactions.support;r.observed=r.evaluated=true;r.reason=evidence.sample.reason;
            r.cancelFlags=evidence.cancelFlags|(bodyDraw.pending?256u:0u)|(fixedResult.ammoOwnsHand?512u:0u)|
                (fixedResult.mechanismOwnsHand?1024u:0u)|(feedResult.ownsHand?2048u:0u)|(pistolSlideResult.ownsHand?4096u:0u)|
                (boltResult.tracking.custody==interaction::BoltCustodyPhase::Manipulating?8192u:0u)|(pumpResult.ownsHand?16384u:0u);
            r.token=supported.token;r.previousToken=evidence.previousToken;r.holding=supported.holding;
            r.engaged=supported.engaged;r.released=supported.released;r.contact=contact.valid;r.handBusy=handBusy;r.cancel=cancel;
            r.inputFlags=(evidence.focused?1u:0u)|(evidence.headTracked?2u:0u)|(evidence.leftGripTracked?4u:0u)|
                (evidence.rightGripTracked?8u:0u)|(evidence.rightAimTracked?16u:0u)|(input.hands[0].squeeze>=.7f?32u:0u);
            r.distanceBand=!contact.valid||!std::isfinite(contact.distanceMeters)?0:
                contact.distanceMeters<=interaction::SupportGripAcquireRadiusMeters?1:
                contact.distanceMeters<=interaction::SupportGripRetentionRadiusMeters?2:3;
            fireTrace.record.supportDistance=contact.distanceMeters;fireTrace.record.leftSqueeze=input.hands[0].squeeze;
            fireTrace.record.supportSource=supportEvidence?supportEvidence->generation:0;
            fireTrace.record.supportSourceDeadlineNs=supportEvidence?HandNanos(supportEvidence->deadline):0;}
        // Retain first/changed failed squeeze, never a per-frame retry stream.
        // Ordinary records roll over before users finish describing a delay.
        const bool failedAttempt=input.hands[0].squeeze>=.7f&&!supported.holding;
        const bool attemptIdentityChanged=supportAttemptRecord.weapon!=evidence.weapon||supportAttemptRecord.rigEpoch!=evidence.rigEpoch||supportAttemptRecord.space!=evidence.space;
        const bool changedAttempt=attemptIdentityChanged||supportAttemptRecord.cancelFlags!=evidence.cancelFlags||supportAttemptRecord.sample.reason!=evidence.sample.reason||
            supportAttemptRecord.sample.contact!=evidence.sample.contact||
            (supportAttemptRecord.sample.distance<=.16f)!=(evidence.sample.distance<=.16f);
        const bool retainedAttempt=failedAttempt&&(!supportAttemptActive||attemptIdentityChanged||
            (changedAttempt&&evidence.eventMs-supportAttemptRecord.eventMs>=100));
        if(supported.engaged||supported.released||retainedAttempt)RetainSupportEvent(evidence);
        if(retainedAttempt){supportAttemptRecord=evidence;supportAttemptActive=true;}
        if(!failedAttempt)supportAttemptActive=false;
        if(supported.released&&unsigned(supported.reason)<supportReleaseCounts.size())++supportReleaseCounts[unsigned(supported.reason)];
        lastSupportSample=evidence;
        if(supported.engaged||supported.released||now-supportRecordTime>=100){
            supportRecords[supportRecordNext]=evidence.sample;supportRecordNext=(supportRecordNext+1)%supportRecords.size();supportRecordCount=std::min(unsigned(supportRecords.size()),supportRecordCount+1);supportRecordTime=now;
        }
        supportHeld=supported.holding;supportToken=supported.token;if(supportHeld&&(out.held&interaction::Fire))++supportFireSamples;
        input=supported.input; // Private copy only; cached raw XR input is unchanged.
    }
    if(reloadRequestMode)reloadFlowRuntime::PublishMagazineInteractionDiagnostic({magazineOwner,
        input.generation,handSample.observedNs,handSample.deadlineNs,supportHeld,
        magazineResult.ownsLeftHand,magazineResult.blocksWeaponActions,unsigned(magazineResult.interaction.phase)});
    constexpr float tau=6.283185307179586f,rad=.017453292519943295f;
    const auto wrap=[](float value){float out=std::fmod(value,tau);return out<0?out+tau:out;};
    using SetAngle=void(__thiscall*)(void*,float);
    const auto setYaw=[&](float yaw){
        reinterpret_cast<SetAngle>(image+aimingProfile.aimerYawSetter)(reinterpret_cast<void*>(aimer),yaw);
        std::memcpy(reinterpret_cast<void*>(owner.aim+12),&yaw,4);
    };
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
    const ReloadStateOwner toolOwner{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
    std::optional<ContextualToolAim> toolAimReceipt;
    const bool toolHeadAim=contextualTool&&bodyHolster&&contextualTool->WantsHeadAim(toolOwner,input,handSample,*bodyHolster);
#endif
    if(motionAim&&aimValid){
        if(!ordinaryBodyPrepared)PrepareTrackedBodyAnchor(owner,input,out,nativeYaw);
        const auto desiredAim=
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
            toolHeadAim?interaction::ControllerAim(input.referenceHead,input.head,lastHeadYaw):
#endif
            input.hands[1].aimTracked?interaction::ControllerAim(input.referenceHead,input.hands[1].aim,lastHandYaw):std::nullopt;
        if(const auto hand=desiredAim){
            lastHandYaw=hand->yaw;setYaw(wrap(baseYaw+hand->yaw));
            // Normal mouse-down lowers native pitch; positive native pitch aims up.
            const float pitch=hand->pitch;
            reinterpret_cast<SetAngle>(image+aimingProfile.aimerPitchSetter)(reinterpret_cast<void*>(aimer),pitch);
            float clamped=0;Read(aimer+16,&clamped,4);std::memcpy(reinterpret_cast<void*>(owner.aim+16),&clamped,4);
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
            if(toolHeadAim){std::array<float,2> actual{},copied{};
                if(Read(aimer+12,actual.data(),8)&&Read(owner.aim+12,copied.data(),8))
                    toolAimReceipt=ObserveContextualToolAim(toolOwner,input,handSample,contextualTool->Operation(),aimer,
                        wrap(baseYaw+hand->yaw),pitch,actual[0],actual[1],copied[0],copied[1]);
            }
#endif
            if(supportHeld){
                float actualYaw=0;Read(aimer+12,&actualYaw,4);
                const float wanted=wrap(baseYaw+hand->yaw),difference=std::remainder(actualYaw-wanted,tau);
                supportAimResidual=std::max(supportAimResidual,std::max(std::abs(difference),std::abs(clamped-pitch)));++supportAimSamples;
            }
            // Native movement follows the gun. Cancel that relative angle so
            // the shared HMD-relative movement remains in the body reference.
            ++aimSamples;
        }
        interaction::RotateMovement(-lastHandYaw,out.forward,out.strafe);
        if(const auto head=interaction::ControllerAim(input.referenceHead,input.head,lastHeadYaw))lastHeadYaw=head->yaw;
        PublishAnchor(owner.soldier,owner.weak,input.spaceGeneration,baseYaw,input.worldUnitsPerMeter);
        if(handPoses){math::Vec3 actorPosition{};
            if(BodyPosition(owner,actorPosition)){
                rigPublication::Tracking sample{input,owner.soldier,owner.weak,owner.weapon,rigEpoch,inputDeadline,baseYaw,consumedOffset,actorPosition};
                sample.controllerOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
                sample.controllerEvidence=handSample;
                sample.selectedMeshes=selectedMeshes.Read(magazineOwner,HandNanos(clock.QuadPart));
                sample.equipmentGeneration=twoHandGrip?handEquip:renderEquipmentEpoch;sample.nativeEquipment=owner.equipment;sample.sightPhysicalItem=familyBinding?familyBinding->persistent:0;sample.sightAdapter=familyBinding?SightAdapterForFamily(familyBinding->family):nullptr;sample.sightSecondary=familyBinding&&familyBinding->selectedSecondary;sample.weaponProfile=itemProfile;sample.assetName=weaponAsset;sample.supportHolding=supportHeld;sample.supportToken=supportToken;sample.sightPreview=sightPreview;sample.reload=physicalResult.tracking;sample.pump=pumpResult.tracking;sample.bolt=boltResult.tracking;sample.boltRawInput=boltRawInput;sample.magazine=magazineResult.tracking;sample.bodyMagazine=magazineResult.bodyAmmo;sample.fixedContainer=fixedResult.tracking;if(fixedResult.bodyAmmo&&fixedResult.bodyVisual){BodyAmmoTracking fixed;fixed.visual=*fixedResult.bodyVisual;fixed.fixed=fixedResult.bodyAmmo;sample.fixedBodyAmmo=std::move(fixed);}sample.feed=feedResult.presentation;if(feedResult.bodyAmmo&&feedResult.bodyVisual){BodyAmmoTracking feed;feed.visual=*feedResult.bodyVisual;feed.feed=feedResult.bodyAmmo;sample.feedBodyAmmo=std::move(feed);}sample.pistolSlide=pistolSlideResult.presentation;sample.weaponActionsBlocked=fixedResult.blocksFire||pistolSlideResult.blocksFire||feedResult.blocksFire||magazineResult.blocksWeaponActions||pumpResult.blocksFire||boltResult.blocksFire;if(bodyHolster||magazineDetached)holsterTracking=sample;else rigPublication::PublishTracking(sample);
            }
            else{++bodyReadFailures;rigPublication::PublishTracking({});}
        }
    }else if(out.turnDegrees){
        if(aimValid){setYaw(wrap(nativeYaw+out.turnDegrees*rad));++turns;}else ++turnUnavailable;
    }
    if(out.active){
        if(input.hands[0].gripTracked&&!input.hands[1].aimTracked)++leftOnlyActions;
        if(!input.hands[0].gripTracked&&input.hands[1].aimTracked)++rightOnlyActions;
        if(out.held&interaction::Fire){if(!input.hands[1].aimTracked)++untrackedFire;else if(!input.hands[0].gripTracked)++leftLossFire;}
        else if(input.hands[1].aimTracked&&input.hands[1].trigger>=.75f)++recoveryFireSuppressed;
    }
    // The automatic equip/shot diagnostic is limited to these inspected
    // conventional firearms. Normal user-driven controls retain native weapons.
    if(equipProbe&&(out.held&interaction::Fire)){
        const bool firearm=interaction::WeaponFeatureEnabled(coreProfile,interaction::WeaponFeature::TranslatedMuzzle);
        if(!firearm){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}
    }
    // This isolated render test never fires, reloads or throws a grenade. All
    // cache writes use the already verified InputOverride path, including an
    // explicit false grenade override so native keyboard input cannot leak.
    if(weaponVisibilityProbe){
        constexpr auto blocked=interaction::Fire|interaction::AlternateFire|interaction::Reload|interaction::NextWeapon|interaction::PreviousWeapon;
        out.held&=~blocked;out.pressed&=~blocked;
    }
    auto grenade=(bodyDraw.blockFire||weaponVisibilityProbe||magazineResult.blocksWeaponActions)?std::optional<bool>{false}:deathProbe?std::optional<bool>{input.hands[1].squeeze>=.75f}:std::nullopt;
    if(grenade&&*grenade)++grenadeSamples;
    std::optional<bool> cycle;
    if(equipProbe){const bool held=input.hands[1].aimTracked&&(input.hands[1].held&interaction::MenuClick);
        cycle=held&&!equipHeld&&out.active;equipHeld=held;if(*cycle)++equipCommands;}
    const auto queueMode=[&](const WeaponModeCommand& mode,std::uint64_t gesture){
        const auto index=modeRecordNext;modeRecordNext=(modeRecordNext+1)%unsigned(modeRecords.size());
        modeRecordCount=(std::min)(modeRecordCount+1,unsigned(modeRecords.size()));
        modeRecords[index]={GetTickCount64(),0,owner.soldier,owner.weak,owner.weapon,mode.targetWeapon,mode.action,rigEpoch,input.spaceGeneration,false,gesture};
        pendingMode=index;++modeRequests;
    };
    std::optional<EntryAction> weaponMode;
    if(physicalMode){
        weaponMode=static_cast<EntryAction>(physicalMode->action);
        queueMode(*physicalMode,physicalRequest);
    }
    if(bodyHolster&&holsterQueuedDraw){bodyDraw.command=holsterQueuedDraw;holsterQueuedDraw.reset();}
    std::optional<EntryAction> holsterEquipment;
    if(bodyDraw.command){
        // Repeat owner/route immediately before staging the one native edge;
        // an item replacement cannot inherit a queued shoulder contact.
        const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},
            [](void*,unsigned at,const char* expected){return Type(at,expected);}};
        Owner nowOwner{};const ReloadStateOwner bodyOwner{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        const auto inventory=ReadBodyInventory(memory,bodyOwner,true);
        const auto command=inventory.snapshot?ResolveBodyDraw(*inventory.snapshot,bodyDraw.command->targetWeapon):std::nullopt;
        bool ordinaryDispatchCurrent=true;
        if(bodyHolster&&bodyHolster->Phase()==BodyHolsterPhase::Held){
            LARGE_INTEGER dispatchNow{};
            ordinaryDispatchCurrent=holsterBodySample&&QueryPerformanceCounter(&dispatchNow)!=FALSE;
            if(ordinaryDispatchCurrent){holsterBodySample->hand.nowNs=HandNanos(dispatchNow.QuadPart);
                ordinaryDispatchCurrent=bodyInventory->OrdinaryDrawDispatchCurrent(*bodyDraw.command,*holsterBodySample);}
        }
        if(ordinaryDispatchCurrent&&!weaponMode&&!pendingMode&&Resolve(nowOwner)&&nowOwner==owner&&command&&
            command->action==bodyDraw.command->action&&command->targetSlot==bodyDraw.command->targetSlot&&command->inventory==bodyDraw.command->inventory){
            if(bodyHolster&&bodyHolster->BlocksActions())holsterEquipment=static_cast<EntryAction>(command->action);
            else if(command->action==7)cycle=true;else weaponMode=static_cast<EntryAction>(command->action);
        }else {InvalidateBodyHolster(false,BodyHolsterLifecycleReason::SelectionRejected);
            // Rejected selection retires the request, not native item identity.
            // The next independent native observation proves replacement/loss.
            bodyInventory->SuspendInteraction();}
    }
    // Bounded diagnostic only. Production mode gestures will feed this same
    // adapter-resolved action after their own physical contact/ack policy.
    if(equipProbe){
        if(!out.active||!input.hands[0].gripTracked||!(input.hands[0].active&interaction::MenuClick))weaponModeHeld=true;
        else{
            const bool held=(input.hands[0].held&interaction::MenuClick)!=0;
            if(held&&!weaponModeHeld&&!pendingMode){
                const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);},[](void*,unsigned at,const char* expected){return Type(at,expected);}};
                const auto mode=weaponModeBindingVerified?ResolveWeaponMode(memory,owner.soldier,owner.weapon):std::nullopt;
                if(mode){
                    weaponMode=static_cast<EntryAction>(mode->action);
                    queueMode(*mode,0);
                }else ++modeRejections;
            }
            weaponModeHeld=held;
        }
    }
    // The magazine can start after the early inventory sample. Do not allow
    // a same-tick shoulder draw to revoke its newly acquired gun prerequisite.
    if(holsterBodySample)holsterBodySample->reloadBusy|=magazineResult.blocksWeaponActions;
    interact.semanticHeld=(out.held&interaction::Use)!=0;interact.reloadBlocked=magazineResult.blocksWeaponActions;
    // Reproduce the actual soldier router's shared native context concept.
    // No alias is inferred from the X button, a nearby object or a past owner.
    bool contextUseVehicleAlias=false;
    if(out.active&&(out.held&interaction::Use)){
        const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        const VehicleRouteBinding binding{unsigned(image+profile.gameplay.managerVtable),unsigned(image+profile.gameplay.inputRouterVtable),unsigned(image+profile.cacheVtable),true};
        const auto routes=ReadOnFootRoutes(memory,binding,{owner.manager,owner.player,owner.soldier,owner.weak,rigEpoch,epoch});
        if(routes.status==VehicleRouteStatus::Okay&&HasOnFootContextUseVehicleAlias(routes.snapshot)&&
           routes.snapshot.identity.entry==owner.entry&&routes.snapshot.identity.router==owner.router&&routes.snapshot.identity.cache==owner.cache){
            contextUseVehicleAlias=true;interact.contextAliasVerified=true;interact.routeFingerprint=routes.snapshot.fingerprint;interact.actionMask|=1u<<16;
        }
        // Reading the router must not extend the input deadline or carry a
        // command across pickup/seat/actor replacement while resolving it.
        Owner current{};if(!QueryPerformanceCounter(&clock)||clock.QuadPart>=inputDeadline||!Resolve(current)||current!=owner||!Writable(owner.cache,InputBytes)){
            fireTrace.record.reasons|=FireRouteContextRecheck;return;}
    }
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
    std::optional<private_grenade::Request> ordinaryGrenadeRequest;
    private_grenade::Facts ordinaryGrenadeFacts;
    if(ordinaryGrenade||playerGrenade){
        const bool visible=holsterBodySample&&holsterBodySample->visible&&
            holsterBodySample->visible->owner==holsterBodySample->owner&&holsterBodySample->visible->deadlineNs>handSample.nowNs;
        ordinaryGrenadeFacts=GrenadeFacts(owner,input,out,visible);
        auto& a=ordinaryGrenadeFacts.source.admission;
        a.bodySettled=bodyInventory&&bodyHolster&&!bodyInventory->Pending()&&!bodyInventory->AutomaticStowPending()&&
            !bodyHolster->BlocksActions()&&!bodyDraw.pending&&!bodyDraw.blockFire&&!bodyDraw.command&&!holsterQueuedDraw&&!holsterEquipment&&!weaponMode&&!cycle.value_or(false);
        // Explain a rejection without changing the existing admission expression.
        const bool bodyFailures[]{!bodyInventory,!bodyHolster,bodyInventory&&bodyInventory->Pending(),
            bodyInventory&&bodyInventory->AutomaticStowPending(),bodyHolster&&bodyHolster->BlocksActions(),
            bodyDraw.pending,bodyDraw.blockFire,bool(bodyDraw.command),bool(holsterQueuedDraw),bool(holsterEquipment),bool(weaponMode),cycle.value_or(false)};
        for(unsigned i=0;i<sizeof(bodyFailures)/sizeof(bodyFailures[0]);++i)
            if(bodyFailures[i])ordinaryGrenadeFacts.bodySettledFailures|=1u<<i;
        a.shellSettled=physicalReload&&!physicalReload->BlocksEquipment()&&!physicalResult.ammoOwnsHand;
        a.magazineSettled=!fixedResult.blocksFire&&!fixedResult.ammoOwnsHand&&!fixedResult.mechanismOwnsHand&&!pistolSlideResult.blocksFire&&!pistolSlideResult.ownsHand&&!feedResult.blocksFire&&!feedResult.ownsHand&&magazinePhysical&&!magazinePhysical->BlocksEquipment()&&!magazineResult.blocksWeaponActions&&!magazineResult.ownsLeftHand&&
            (!magazineDetached||!magazineDetached->BlocksActions());
        // A stock grenade edge excludes outstanding mechanics on ANY weapon.
        a.pumpSettled=!pumpDebt&&!pumpResult.blocksFire&&!pumpResult.ownsHand&&(!physicalPump||!physicalPump->BlocksFire());
        a.boltSettled=!boltResult.blocksFire&&(!physicalBolt||!physicalBolt->BlocksFire());
        a.sightSettled=sightPhase==interaction::SightFlipPhase::Idle&&!sightGrasp&&!physicalMode&&!pendingMode;
        const auto native=GrenadeNative(owner,input,ordinaryGrenadeFacts.source.nowNs);
        {LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);ordinaryGrenadeFacts.source.nowNs=HandNanos(stamp.QuadPart);}
#ifdef FVR_BC2_TWO_GRENADE_DEATH_PROBE
        if(TwoGrenadeDeathAllowsPrepare(native))ordinaryGrenadeRequest=playerGrenade?playerGrenade->Prepare(native,ordinaryGrenadeFacts):ordinaryGrenade->Prepare(native,ordinaryGrenadeFacts);
        else if(playerGrenade&&(!native||twoGrenadeDeathLifetime.Lost()))playerGrenade->Missing(ordinaryGrenadeFacts.source.nowNs);
        grenadeOutcome.observed=true;
        PublishTwoGrenadeDeathReadiness(native,ordinaryGrenadeFacts);
#else
        ordinaryGrenadeRequest=playerGrenade?playerGrenade->Prepare(native,ordinaryGrenadeFacts):ordinaryGrenade->Prepare(native,ordinaryGrenadeFacts);grenadeOutcome.observed=true;
#endif
        if(ordinaryGrenadeRequest){
            const auto current=GrenadeNative(owner,input,ordinaryGrenadeFacts.source.nowNs);
            const auto checked=GrenadeFacts(owner,input,out,visible);
            ordinaryGrenadeFacts.source.nowNs=checked.source.nowNs;
            ordinaryGrenadeFacts.source.input.menu=checked.source.input.menu;
            ordinaryGrenadeFacts.source.admission.ownerCurrent=checked.source.admission.ownerCurrent;
            ordinaryGrenadeFacts.source.admission.nativeFireNeutral=checked.source.admission.nativeFireNeutral;
            ordinaryGrenadeFacts.source.admission.nativeGrenadeNeutral=checked.source.admission.nativeGrenadeNeutral;
            if(
#ifdef FVR_BC2_TWO_GRENADE_DEATH_PROBE
               TwoGrenadeDeathAllowsPrepare(current)&&
#endif
               (playerGrenade?playerGrenade->CanStage(*ordinaryGrenadeRequest,current,ordinaryGrenadeFacts):ordinaryGrenade->CanStage(*ordinaryGrenadeRequest,current,ordinaryGrenadeFacts)))grenade=true;
            else ordinaryGrenadeRequest.reset();
        }
    }
#endif
    // Opening Expack authors its charge operation as ReloadInputAction=Zoom14.
    // Route the existing right-secondary Reload request only after all attack
    // gates: some magazine/cycle gates intentionally leave Reload available.
    // Fire8 remains the plunger; this creates no trigger or selection request.
    if(asset=="Expack_US"&&((out.held|out.pressed|out.released)&interaction::Reload)){
        ++expackReloadRequests;
        std::optional<EntryAction> route;
        const bool settled=out.active&&aimValid&&nativePlaying&&!weaponVisibilityProbe&&!deathProbe&&!equipProbe&&
            !sightOwnsHand&&sightPhase==interaction::SightFlipPhase::Idle&&!sightGrasp&&!physicalMode&&!pendingMode&&
            !bodyDraw.pending&&!bodyDraw.blockFire&&!bodyDraw.command&&!holsterQueuedDraw&&!holsterEquipment&&
            !weaponMode&&!cycle.value_or(false)&&!grenade.value_or(false)&&
            (!bodyHolster||(!bodyHolster->BlocksActions()&&!bodyHolster->TemporaryToolActive()))&&
            !pumpDebt&&!pumpResult.blocksFire&&!pumpResult.ownsHand&&(!physicalPump||!physicalPump->BlocksFire())&&
            !boltResult.blocksFire&&(!physicalBolt||!physicalBolt->BlocksFire())&&
            (!physicalReload||!physicalReload->BlocksEquipment())&&!physicalResult.ammoOwnsHand&&
            (!magazinePhysical||!magazinePhysical->BlocksEquipment())&&!magazineResult.blocksWeaponActions&&
            (!magazineDetached||!magazineDetached->BlocksActions());
        const ReloadStateOwner expected{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        LARGE_INTEGER before{},after{};
        if(settled&&reloadStateBinding&&QueryPerformanceCounter(&before)){
            const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
                [](void*,unsigned at,const char* name){return Type(at,name);}};
            const auto selected=ReadReloadState(memory,*reloadStateBinding,unsigned(image),expected,input.generation,HandNanos(before.QuadPart));
            ++expackReloadReads[unsigned(selected.status)];
            Owner current{};
            if(selected.snapshot&&selected.snapshot->config.weaponData==owner.equipment.data&&
               Resolve(current)&&current==owner&&QueryPerformanceCounter(&after)&&after.QuadPart<inputDeadline){
                const auto hold=handOwnership.Current(interaction::InteractionHand::Right);
                if(hold&&hold->token.kind==interaction::HandClaimKind::GunHold&&hold->token.owner==handSample.owner&&
                   hold->token.item==handItem&&hold->inputSequence==input.generation&&hold->deadlineNs>HandNanos(after.QuadPart))
                    route=VerifiedSelectedReloadAction(*selected.snapshot,expected,input.generation,HandNanos(after.QuadPart));
            }
        }
        if(route==EntryAction::Zoom)++expackReloadZoomRoutes;
        else if(!route)++expackReloadUnresolved;
        ApplySelectedReloadRoute(out,route);
    }
    // Opening SP_01 equips a distinct KNV-1 whose authored attack is melee37.
    // The shared display name only selects this bounded read; it never grants
    // the route. Preserve stock firearm/ordinary-knife Fire8 and all action gates.
    auto verifiedFireAction=EntryAction::Fire;
    if(asset=="KNV-1"){
        std::optional<EntryAction> route;
        const ReloadStateOwner expected{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        LARGE_INTEGER beforeFire{},afterFire{};
        if(out.active&&reloadStateBinding&&QueryPerformanceCounter(&beforeFire)){
            const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
                [](void*,unsigned at,const char* name){return Type(at,name);}};
            const auto selected=ReadReloadState(memory,*reloadStateBinding,unsigned(image),expected,input.generation,HandNanos(beforeFire.QuadPart));
            Owner current{};
            if(selected.snapshot&&QueryPerformanceCounter(&afterFire)&&afterFire.QuadPart<inputDeadline&&
               Resolve(current)&&current==owner){
                route=VerifiedSelectedFireAction(*selected.snapshot,expected,input.generation,HandNanos(afterFire.QuadPart));
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
                // The opening campaign knife is already selected. Publish its
                // measured display source without inventing a chest draw. The
                // selected melee adapter has its own acquisition/attack proof.
                const auto held=handOwnership.Current(interaction::InteractionHand::Right);
                if(route==EntryAction::MeleeAttack&&holsterTracking&&held&&
                   held->token.kind==interaction::HandClaimKind::GunHold&&held->token.owner==handSample.owner&&
                   held->token.item==handItem&&held->inputSequence==handSample.sequence&&
                   held->deadlineNs>HandNanos(afterFire.QuadPart)){
                    SelectedKnifeRenderSource source{*selected.snapshot,owner.equipment,handSample,
                        interaction::GameplayOwnsInput(input.menu,HandNanos(afterFire.QuadPart))};
                    if(selected_knife_detail::Source(source,HandNanos(afterFire.QuadPart))){
                        holsterTracking->selectedKnife=source;
                        const auto rendered=rigPublication::ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
                        const auto meshes=selectedMeshes.Read(expected,HandNanos(afterFire.QuadPart));
                        const bool settled=out.active&&aimValid&&nativePlaying&&!weaponVisibilityProbe&&!deathProbe&&!equipProbe&&
                            !sightOwnsHand&&sightPhase==interaction::SightFlipPhase::Idle&&!sightGrasp&&!physicalMode&&!pendingMode&&
                            !bodyDraw.pending&&!bodyDraw.blockFire&&!bodyDraw.command&&!holsterQueuedDraw&&!holsterEquipment&&
                            !weaponMode&&!cycle.value_or(false)&&!grenade.value_or(false)&&
                            (!bodyInventory||(!bodyInventory->Pending()&&!bodyInventory->AutomaticStowPending()))&&
                            (!bodyHolster||(!bodyHolster->BlocksActions()&&!bodyHolster->TemporaryToolActive()))&&
                            !pumpDebt&&!pumpResult.blocksFire&&!pumpResult.ownsHand&&(!physicalPump||!physicalPump->BlocksFire())&&
                            !boltResult.blocksFire&&(!physicalBolt||!physicalBolt->BlocksFire())&&
                            (!physicalReload||!physicalReload->BlocksEquipment())&&!physicalResult.ammoOwnsHand&&
                            (!magazinePhysical||!magazinePhysical->BlocksEquipment())&&!magazineResult.blocksWeaponActions&&
                            (!magazineDetached||!magazineDetached->BlocksActions())&&
                            !((out.held|out.pressed)&(interaction::Use|interaction::Reload|interaction::NextWeapon|interaction::PreviousWeapon))&&
                            // A held raw trigger whose existing semantic gate
                            // rejected Fire cannot acquire a second route here.
                            (input.hands[1].trigger<.75f||(out.held&interaction::Fire));
                        selectedKnifeOutcome.observed=true;
                        selectedKnifeOutcome.request=selectedKnife::Prepare(source,input,handOwnership,meshes.get(),
                            rendered?rendered->selectedKnife:std::nullopt,rigPublication::BuildCurrentEyeBase(*holsterTracking),settled,inputEpoch);
                    }
                }
#endif
            }
        }
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
        if(route==EntryAction::MeleeAttack){
            // The installed adapter owns every selected-melee trigger/jab,
            // including missing hand/render-source observations. Otherwise an
            // ordinary Fire edge could bypass its one outstanding intent.
            if(!selectedKnifeOutcome.observed){selectedKnifeOutcome.request=selectedKnife::Missing();selectedKnifeOutcome.observed=true;}
            if(selectedKnifeOutcome.request.handled){
                out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;
                if(selectedKnifeOutcome.request.fire){out.held|=interaction::Fire;out.pressed|=interaction::Fire;}
            }
        }
#endif
        if(route)verifiedFireAction=*route;
        else{out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;fireTrace.record.reasons|=FireRouteSelectedRoute;}
    }
    if(resourceInventoryFixture)resourceInventoryFixture->FilterCancelledFire(out);
    if(pistolSlideResult.blocksFire){constexpr auto mask=interaction::Fire|interaction::AlternateFire;
        out.held&=~mask;out.pressed&=~mask;grenade=false;}
    if(feedResult.selected||retainedFeedGate){out.held&=~interaction::Reload;out.pressed&=~interaction::Reload;}
    // Final gate survives later normal tool/grenade/support routing and input
    // loss; retained physical debt, never loaded-count-only readiness.
    if(fixedResult.blocksFire||feedResult.blocksFire||(retainedFeedGate&&retainedFeedGate->blocksFire)||
       (physicalFeed&&feedEquipment&&physicalFeed->BlocksFire(magazineOwner,*feedEquipment,feedResourceBinding))){
        fireTrace.record.reasons|=fixedResult.blocksFire?FireRouteFixedContainer:FireRouteFeed;
        constexpr auto mask=interaction::Fire|interaction::AlternateFire|interaction::Reload;
        out.held&=~mask;out.pressed&=~mask;grenade=false;
    }
    if(fireTrace.observing){auto& r=fireTrace.record;r.reasons&=~FireRouteIncomplete;
        r.effective=(out.held&interaction::Fire)!=0;r.nativeAction=unsigned(verifiedFireAction);r.actionActive=out.active;
        if(r.requested&&!r.mapped)r.reasons|=r.rightAimTracked?FireRouteSemanticGate:FireRouteRightAimMissing;
        if(r.mapped&&!r.effective){
            if(bodyDraw.blockFire)r.reasons|=FireRouteBodyDraw;
            if(magazineResult.blocksWeaponActions)r.reasons|=FireRouteMagazine;
            if(pumpDebt||pumpResult.blocksFire)r.reasons|=FireRoutePump;
            if(boltResult.blocksFire)r.reasons|=FireRouteBolt;
            if(sightOwnsHand)r.reasons|=FireRouteSight;
            if(weaponVisibilityProbe||equipProbe||resourceInventoryFixture)r.reasons|=FireRouteFixture;
            if(pistolSlideResult.blocksFire)r.reasons|=FireRoutePistolSlide;
            if(feedResult.blocksFire)r.reasons|=FireRouteFeed;
            if(fixedResult.blocksFire)r.reasons|=FireRouteFixedContainer;
            if(!r.reasons)r.reasons|=FireRouteOtherFilter;
        }
        // Default before Apply also records its explicit refusal branch below.
        r.write=FireRouteWrite::Refused;
    }
    // Native reads and interaction processing above can outlast the packet.
    // Recheck the original lease and exact owner for ordinary firearms too;
    // Use/selected-knife checks alone leave the common held-Fire path exposed.
    LARGE_INTEGER dispatchClock{};Owner dispatchOwner{};
    const bool dispatchOwnerCurrent=Resolve(dispatchOwner)&&dispatchOwner==owner;
    const bool dispatchCacheWritable=dispatchOwnerCurrent&&Writable(owner.cache,InputBytes);
    const bool dispatchClockOkay=QueryPerformanceCounter(&dispatchClock)!=FALSE;
    if(!GameplayInputDispatchCurrent(gameplayInputLease,dispatchClockOkay?dispatchClock.QuadPart:0,
        dispatchOwnerCurrent,dispatchCacheWritable)){
        fireTrace.record.reasons|=(!dispatchClockOkay||!gameplayInputLease.Fresh(dispatchClock.QuadPart))?
            FireRouteStaleInput:FireRouteOwnerUnavailable;
        // Match a stale reception's semantic invalidation. A processing stall
        // must not leave a held trigger armed after its command was refused.
        (void)actions.Update({}, {owner.soldier,inputEpoch,false,true,rigEpoch},0,
            dispatchClockOkay?HandNanos(dispatchClock.QuadPart):0);
        return;
    }
    if(scope->patch.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},out,grenade,cycle,weaponMode,contextUseVehicleAlias,verifiedFireAction)){++applied;if(out.pressed&interaction::NextWeapon)++nextWeaponCommands;if(out.pressed&interaction::PreviousWeapon)++previousWeaponCommands;if(out.forward||out.strafe)++moving;if(out.held)++buttons;
        // Do not restore at the end of player update: BC2 also consumes this
        // buffer afterward. The next original gather replaces every action.
        std::optional<HolsterSuppressionReceipt> holsterReceipt;
        BodyHolsterSample holsterSample;
        if(bodyHolster){
            holsterSample.nativeOwner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
            holsterSample.hand=handSample;LARGE_INTEGER holsterNow{};
            if(QueryPerformanceCounter(&holsterNow))holsterSample.hand.nowNs=HandNanos(holsterNow.QuadPart);
            else holsterSample.cancel=true;
            holsterSample.nativeTick=scope->nativeTick;holsterSample.cache=owner.cache;
            holsterSample.equipment=owner.equipment;
            holsterSample.ordinaryPair=rigPublication::ReadOrdinaryEquipmentPair(holsterSample.nativeOwner,bodyHolster->RequestId(),holsterSample.hand.nowNs);
            holsterSample.selected=selectedMeshes.Read(holsterSample.nativeOwner,holsterSample.hand.nowNs);
            holsterSample.visibility=rigPublication::ReadWeaponVisibilityReceipt(holsterSample.nativeOwner,bodyHolster->RequestId(),holsterSample.hand.nowNs);
            if(const auto request=bodyHolster->Demand(holsterSample)){HolsterInputOverride patch;
                BodyHolsterCacheChallenge challenge;
                if(bodyHolsterFixture&&!holsterEquipment){
                    LARGE_INTEGER now{};QueryPerformanceCounter(&now);const auto ns=HandNanos(now.QuadPart);
                    const auto previous=ReadBodyHolsterProbe(ns);
                    if(previous&&bodyHolsterFixture->ChallengeWanted(*previous,ns)&&previous->outcome.freeRight)
                        challenge.Stage({reinterpret_cast<std::byte*>(owner.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent},
                            *previous->outcome.freeRight,handOwnership,previous->trialDeadlineNs);
                }
                if(patch.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent},holsterEquipment))holsterReceipt=patch.Commit();
                if(challenge.Evidence().staged){challenge.Complete(holsterReceipt);LARGE_INTEGER now{};QueryPerformanceCounter(&now);
                    bodyHolsterFixture->RecordChallenge(challenge.Evidence(),HandNanos(now.QuadPart));
                    if(!challenge.Evidence().committed)holsterSample.cancel=true;}
                if(holsterReceipt)++holsterSuppressionCommits;else {++holsterSuppressionFailures;holsterSample.cancel=true;}}
        }
        std::optional<HolsterSuppressionReceipt> detachedSuppression;std::int64_t detachedCommittedNs=0;bool detachedDemanded=false;
        if(detachedUsed){if(const auto request=magazineDetached->Demand()){detachedDemanded=true;HolsterInputOverride patch;
            if(patch.Apply({reinterpret_cast<std::byte*>(owner.cache),InputBytes},*request,{nullptr,HolsterCacheCurrent}))detachedSuppression=patch.Commit();
            LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);detachedCommittedNs=HandNanos(stamp.QuadPart);
        }}
        scope->patch.Commit();++committed;interact.committed=true;
        if(fireTrace.observing){auto& r=fireTrace.record;r.write=FireRouteWrite::Applied;
            if(holsterReceipt){r.effective=false;r.reasons|=FireRouteHolsterSuppression;}
            if(detachedSuppression){r.effective=false;r.reasons|=FireRouteDetachedSuppression;}
            LARGE_INTEGER stamp{};if(QueryPerformanceCounter(&stamp))r.writeNs=HandNanos(stamp.QuadPart);
        }
        // An ordinary shoulder request is issued only after the exact routed
        // edge reached the committed cache. Pending creation is not dispatch.
        if(bodyInventory&&bodyHolster&&bodyHolster->Phase()==BodyHolsterPhase::Held&&holsterBodySample&&
           bodyDraw.command&&!holsterReceipt&&!detachedDemanded&&!holsterEquipment&&
           ((bodyDraw.command->action==7&&cycle.value_or(false))||
            (weaponMode&&unsigned(*weaponMode)==bodyDraw.command->action)))
            bodyInventory->ObserveOrdinaryDrawIssued(*bodyDraw.command,*holsterBodySample);
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
        // Input publication alone never settles this intent. The exact native
        // acquisition and matching original attack callback own its outcome.
        selectedKnife::Published(selectedKnifeOutcome.request,true);selectedKnifeOutcome.published=true;
#endif
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
        if(ordinaryGrenadeRequest){
            const auto checked=GrenadeFacts(owner,input,out,ordinaryGrenadeFacts.visible);
            ordinaryGrenadeFacts.source.nowNs=checked.source.nowNs;
            ordinaryGrenadeFacts.source.input.menu=checked.source.input.menu;
            ordinaryGrenadeFacts.source.admission.ownerCurrent=checked.source.admission.ownerCurrent;
            unsigned bits=0;const bool read=Read(owner.cache+0x9c,&bits,4);const bool bit=read&&(bits&(1u<<6));
            if(playerGrenade)playerGrenade->Commit(*ordinaryGrenadeRequest,ordinaryGrenadeFacts,read?std::optional<bool>{bit}:std::nullopt);
            else ordinaryGrenade->Commit(*ordinaryGrenadeRequest,ordinaryGrenadeFacts,bit);
        }
#endif
        if(detachedUsed){
            const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
            LARGE_INTEGER before{};QueryPerformanceCounter(&before);
            const auto pair=rigPublication::ReadMagazineDetachPair(current,HandNanos(before.QuadPart));
            const auto postReserve=reloadFlowRuntime::ReadReserve();
            LARGE_INTEGER after{};QueryPerformanceCounter(&after);const auto now=HandNanos(after.QuadPart);
            magazineResult=magazineDetached->Commit(detachedSuppression,pair,postReserve,handOwnership,handIntent,detachedCommittedNs,now);
            magazineResult.bodyAmmo=magazinePhysical?magazinePhysical->BodyAmmoDisplay(
                magazineResult.tracking,handOwnership.Current(interaction::InteractionHand::Right),now):std::nullopt;
            if(holsterTracking){holsterTracking->magazine=magazineResult.tracking;holsterTracking->bodyMagazine=magazineResult.bodyAmmo;holsterTracking->weaponActionsBlocked=fixedResult.blocksFire||pistolSlideResult.blocksFire||feedResult.blocksFire||magazineResult.blocksWeaponActions;}
            if(magazineDetachedFixture)magazineDetachedFixture->Observe(*magazineDetached,magazineResult,detachedSuppression,pair,
                postReserve,rigPublication::ReadMagazinePackCounters(),detachedDemanded,now);
        }
        if(magazineDetached&&!bodyHolster){if(holsterTracking)rigPublication::PublishTracking(*holsterTracking);else rigPublication::PublishTracking({});}
        // All inventory/claim transitions occur AFTER the actual current native
        // cache commit. Native consumes the equipment pulse later on this tick.
        if(bodyHolster&&bodyInventory&&holsterBodySample){
            holsterSample.suppression=holsterReceipt;
            // Refresh processing time only, preserving original input identity,
            // observation/deadline and all controller poses consumed by contact.
            LARGE_INTEGER holsterNow{};if(QueryPerformanceCounter(&holsterNow))holsterBodySample->hand.nowNs=HandNanos(holsterNow.QuadPart);
            else holsterSample.cancel=true;
            const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
            std::optional<BodyHolsterSample> evaluatedHolster;
#endif
            auto holsterOutcome=bodyInventory->TickHolster(memory,*holsterBodySample,holsterSample,*bodyHolster,handOwnership,handIntent
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
                ,&evaluatedHolster
#endif
            );
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
            if(chestKnife){
                const auto frame=rigPublication::ReadBodyWeaponFrame(owner.soldier,owner.weak,owner.weapon);
                const bool settled=!bodyInventory->Pending()&&!bodyInventory->AutomaticStowPending()&&!holsterQueuedDraw&&!holsterEquipment&&!weaponMode&&
                    !pumpDebt&&!pumpResult.blocksFire&&!pumpResult.ownsHand&&(!physicalPump||!physicalPump->BlocksFire())&&
                    !boltResult.blocksFire&&(!physicalBolt||!physicalBolt->BlocksFire())&&
                    (!physicalReload||!physicalReload->BlocksEquipment())&&!physicalResult.ammoOwnsHand&&
                    (!magazinePhysical||!magazinePhysical->BlocksEquipment())&&!magazineResult.blocksWeaponActions&&!magazineResult.ownsLeftHand&&
                    !fixedResult.blocksFire&&!fixedResult.ammoOwnsHand&&!fixedResult.mechanismOwnsHand&&!pistolSlideResult.blocksFire&&!pistolSlideResult.ownsHand&&!feedResult.blocksFire&&!feedResult.ownsHand&&(!magazineDetached||!magazineDetached->BlocksActions())&&sightPhase==interaction::SightFlipPhase::Idle&&!sightGrasp&&!physicalMode&&!pendingMode;
                chestKnife->Tick(memory,*holsterBodySample,holsterSample,holsterOutcome,frame?frame->chestKnife:std::nullopt,
                    settled,*bodyInventory,*bodyHolster,handOwnership,handIntent);
                if(bodyHolster->TemporaryToolActive()){
                    // TickHolster's earlier free-hand receipt predates the
                    // deliberate tool claim acquired above. Keep an exact
                    // current original hide, but revoke that free-hand output.
                    holsterOutcome.freeRight.reset();holsterOutcome.phase=bodyHolster->Phase();
                    holsterOutcome.blockWeaponActions=true;holsterOutcome.allowAutomaticGunHold=false;
                }
                if(holsterTracking)holsterTracking->chestKnife=chestKnife->Rendering();
            }
#endif
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
            if(contextualTool&&evaluatedHolster){
                auto& toolPost=*evaluatedHolster;
                const bool settled=!bodyInventory->Pending()&&!bodyInventory->AutomaticStowPending()&&!holsterQueuedDraw&&!holsterEquipment&&!weaponMode&&
                    !pumpDebt&&!pumpResult.blocksFire&&!pumpResult.ownsHand&&(!physicalPump||!physicalPump->BlocksFire())&&
                    !boltResult.blocksFire&&(!physicalBolt||!physicalBolt->BlocksFire())&&
                    (!physicalReload||!physicalReload->BlocksEquipment())&&!physicalResult.ammoOwnsHand&&
                    (!magazinePhysical||!magazinePhysical->BlocksEquipment())&&!magazineResult.blocksWeaponActions&&!magazineResult.ownsLeftHand&&
                    !fixedResult.blocksFire&&!fixedResult.ammoOwnsHand&&!fixedResult.mechanismOwnsHand&&!pistolSlideResult.blocksFire&&!pistolSlideResult.ownsHand&&!feedResult.blocksFire&&!feedResult.ownsHand&&(!magazineDetached||!magazineDetached->BlocksActions())&&sightPhase==interaction::SightFlipPhase::Idle&&!sightGrasp&&!physicalMode&&!pendingMode;
                const ContextualToolBoundary boundary{{nullptr,HolsterCacheCurrent},nullptr,
                    [](void*)noexcept->std::int64_t{LARGE_INTEGER stamp{};return QueryPerformanceCounter(&stamp)?HandNanos(stamp.QuadPart):0;},
                    [](void*,const ContextualToolAim& receipt)noexcept{
                        if(!scope||!scope->used)return false;unsigned currentAimer=0;std::array<float,2> actual{},copied{};
                        return Read(scope->owner.aim+8,&currentAimer,4)&&currentAimer==receipt.aimer&&
                            Read(currentAimer+12,actual.data(),8)&&Read(scope->owner.aim+12,copied.data(),8)&&
                            actual[0]==receipt.yaw&&actual[1]==receipt.pitch&&actual==copied;
                    }};
                const auto attachment=rigPublication::ReadContextualToolAttachment(holsterBodySample->owner);
                if(contextualTool->Tick(*holsterBodySample,toolPost,holsterOutcome,attachment,toolAimReceipt,settled,
                    {reinterpret_cast<std::byte*>(owner.cache),InputBytes},boundary,*bodyInventory,*bodyHolster,handOwnership,handIntent)){
                    // The permitted tool exception replaces full suppression.
                    // Never publish the superseded ordinary suppression receipt.
                    holsterReceipt.reset();holsterSample.suppression.reset();
                }
                contextualTool->PublishProbe(*holsterBodySample,toolPost,holsterOutcome,*bodyHolster,handOwnership);
                if(bodyHolster->TemporaryToolActive()){
                    holsterOutcome.freeRight.reset();holsterOutcome.phase=bodyHolster->Phase();
                    holsterOutcome.blockWeaponActions=true;holsterOutcome.allowAutomaticGunHold=false;
                }
            }else if(contextualTool)contextualTool->Missing(holsterBodySample->hand,handOwnership);
#endif
            reloadFlowRuntime::PublishAmmoResourceBinding(bodyInventory->ResourceBinding(holsterBodySample->owner,holsterBodySample->hand.nowNs));
            if(holsterOutcome.blockWeaponActions){std::lock_guard lock(shotPoseMutex);shotHistory.Reset();}
            if(holsterOutcome.select){
                // A holstered item remains native-selected. Drawing that exact
                // item needs fresh show proof, not a redundant equip command.
                if(holsterOutcome.select->id!=holsterSample.nativeOwner.weapon){
                    const auto inventory=ReadBodyInventory(memory,holsterSample.nativeOwner,true);
                    if(inventory.snapshot)holsterQueuedDraw=ResolveBodyDraw(*inventory.snapshot,unsigned(holsterOutcome.select->id));
                    if(!holsterQueuedDraw)InvalidateBodyHolsterState(BodyHolsterLifecycleReason::SelectionRejected);}
            }
            else if(holsterOutcome.ordinaryDraw.command)holsterQueuedDraw=holsterOutcome.ordinaryDraw.command;
            rigPublication::PublishWeaponVisibility(holsterOutcome.visibility);
            rigPublication::PublishOrdinaryEquipment(holsterOutcome.ordinaryRecovery);
            if(!holsterOutcome.freeRight)stowedAmmoReserveCache.Clear();
            if(holsterTracking){holsterTracking->weaponActionsBlocked=fixedResult.blocksFire||pistolSlideResult.blocksFire||feedResult.blocksFire||holsterOutcome.blockWeaponActions||magazineResult.blocksWeaponActions;
                if(holsterOutcome.freeRight){try{holsterTracking->freeRight=std::make_shared<const BodyFreeRightEvidence>(*holsterOutcome.freeRight);
                    holsterTracking->bodyHolsterSlot=bodyInventory->AssignedSlot(holsterSample.nativeOwner.weapon);
                    holsterTracking->bodyAnchors=bodyAnchors;++holsterFreePublications;
                    // Separate display-only reserve for the actually stowed
                    // selected weapon. Never acquire a gun or enter a reload.
                    // The render adapter revalidates owner, hide and input leases.
                    const auto reserve=reloadFlowRuntime::ReadReserveObserved();
                    LARGE_INTEGER reserveNow{};const auto displayNow=QueryPerformanceCounter(&reserveNow)?HandNanos(reserveNow.QuadPart):0;
                    // Display-only cache survives busy/cohort reads through
                    // the original lease. It is never passed to reload or HUD.
                    holsterTracking->stowedAmmoReserve=stowedAmmoReserveCache.Observe(reserve,
                        holsterSample.nativeOwner,displayNow).value_or(Bc2AmmoReserveLease{});
                    holsterTracking->stowedAmmoMagazine=magazineProfile;
                    if(magazineProfile&&magazinePhysical)holsterTracking->stowedAmmoContact=magazinePhysical->BodyAmmoContact();
                    else if((ordinaryShotgunEquipment||(!ordinaryManualCycles&&asset==SpasReloadAsset))&&physicalReload)holsterTracking->stowedAmmoContact=physicalReload->BodyAmmoContact();
                }catch(...){InvalidateBodyHolsterState(BodyHolsterLifecycleReason::FreePoseAllocationFailed);}}
                holsterTracking->bodyAnchors=bodyAnchors;
                if(const auto display=bodyInventory->Display(holsterBodySample->hand.nowNs)){
                    try {holsterTracking->bodyInventoryDisplay=std::make_shared<const BodyInventoryDisplay>(*display);
                        for(unsigned n=0;n<display->count;++n){const auto& slot=display->slots[n];
                            if(slot.assignment.item==display->physicalSelected||slot.native.weapon==display->selectedOwner.weapon)continue;
                            const auto equipment=ReadWeaponEquipmentIdentity(memory,slot.native.weapon);
                            if(!equipment||equipment->data!=slot.native.data||equipment->persistence!=slot.native.persistence)continue;
                            // Only existing installed render-only geometry profiles. No
                            // category/name grants native reload, hide or shot capability.
                            if(std::none_of(BodyEquipmentProfiles.begin(),BodyEquipmentProfiles.end(),[&](const auto& p){return equipment->Asset()==p.asset;}))continue;
                            holsterTracking->bodyCarriedMeshes[n]=selectedMeshes.ReadCarried(display->selectedOwner,display->carried.inventory,
                                slot.native.slot,slot.native.weapon,slot.native.data,slot.native.persistence,display->sequence,
                                display->observedNs,holsterBodySample->hand.nowNs);
                        }
                    }catch(...){holsterTracking->bodyInventoryDisplay.reset();holsterTracking->bodyCarriedMeshes={};}
                }
                rigPublication::PublishTracking(*holsterTracking);}
            else {InvalidateBodyHolsterState(BodyHolsterLifecycleReason::TrackingPublicationUnavailable);rigPublication::PublishTracking({});}
            if(bodyHolster->DiagnosticDeadline()||inventoryReloadFixture||resourceInventoryFixture||ordinaryBoltInputFixture){
                try{auto sample=std::make_shared<BodyHolsterProbeSample>();LARGE_INTEGER now{};QueryPerformanceCounter(&now);
                    sample->sampledNs=HandNanos(now.QuadPart);sample->trialStartNs=bodyHolster->DiagnosticStart();sample->trialDeadlineNs=bodyHolster->DiagnosticDeadline();
                    CaptureBodyHolsterProbeSource(*sample,*holsterBodySample);
                    if(holsterFireFixture){sample->fireTickMs=GetTickCount64();sample->fireRequested=(out.held&interaction::Fire)!=0;
                        sample->fireCacheRead=Read(owner.cache+8+4*unsigned(EntryAction::Fire),&sample->fireCache,4);}
                    sample->nativeTick=scope->nativeTick;sample->request=bodyHolster->RequestId();sample->phase=bodyHolster->Phase();
                    sample->selectedSlot=bodyInventory->AssignedSlot(holsterSample.nativeOwner.weapon);sample->anchors=bodyAnchors;
                    sample->left=handOwnership.Current(interaction::InteractionHand::Left);sample->right=handOwnership.Current(interaction::InteractionHand::Right);
                    sample->outcome=holsterOutcome;sample->visibility=holsterSample.visibility;sample->suppression=holsterReceipt;
                    sample->queuedTarget=holsterQueuedDraw?holsterQueuedDraw->targetWeapon:0;sample->pack=rigPublication::ReadBodyHolsterPackCounters();
                    bodyHolsterSample.store(std::move(sample),std::memory_order_release);
                    if(bodyHolsterFixture){LARGE_INTEGER after{};QueryPerformanceCounter(&after);const auto ns=HandNanos(after.QuadPart);
                        bodyHolsterFixture->Observe(ReadBodyHolsterProbe(ns),ns);
                        bodyHolsterFixtureSample.store(std::make_shared<const BodyHolsterFixtureSample>(bodyHolsterFixture->Sample()),std::memory_order_release);}
                }catch(...){bodyHolsterSample.store({},std::memory_order_release);InvalidateBodyHolsterState(BodyHolsterLifecycleReason::ProbePublicationFailed);}
            }
        }
        if(weaponVisibilityProbe){
            float fire=-1;std::array<unsigned,2> bits{};
            bool quiet=Read(owner.cache+8+4*unsigned(EntryAction::Fire),&fire,4)&&fire==0&&Read(owner.cache+0x98,bits.data(),8);
            for(const auto action:{EntryAction::Zoom,EntryAction::Reload,EntryAction::ThrowGrenade}){
                const auto bit=unsigned(action);quiet&=(bits[bit/32]&(1u<<(bit%32)))==0;
            }
            if(quiet)++weaponVisibilityActionCommits;else ++weaponVisibilityActionFailures;
        }
        // Sample final hand ownership AFTER holster/pickup decisions. Empty
        // hands must not display the still-selected hidden inventory weapon.
        const auto heldGun=handOwnership.Current(interaction::InteractionHand::Right);
        AmmoCounterOwner hud;hud.owner={owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        hud.observedNs=handSample.observedNs;hud.deadlineNs=handSample.deadlineNs;
        hud.alive=out.active&&nativePlaying;hud.onFoot=owner.foot;
        hud.held=heldGun&&heldGun->token.kind==interaction::HandClaimKind::GunHold&&
            heldGun->token.owner==handSample.owner&&heldGun->token.item==handItem&&heldGun->deadlineNs>handSample.nowNs;
        if(heldGun)hud.deadlineNs=(std::min)(hud.deadlineNs,heldGun->deadlineNs);
        // The selected family supplies its own exact native ammo view on the
        // input thread, independently of any retained reload operation.
        if(hud.alive&&hud.onFoot&&hud.held){
            const auto observedAmmo=reloadFlowRuntime::ReadReserveObserved();
            if(fireTrace.observing){LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);
                ObserveFireRouteAmmo(fireTrace.record,observedAmmo.lease,HandNanos(stamp.QuadPart));}
        }
        try{ammoCounterOwner.store(std::make_shared<const AmmoCounterOwner>(hud),std::memory_order_release);}
        catch(...){ammoCounterOwner.store({},std::memory_order_release);}
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
        if(ordinaryGrenade||playerGrenade)GrenadeObserveCurrent(owner,input,out,ordinaryGrenadeFacts.visible);
#endif
    }
    else{
        if(fireTrace.observing){fireTrace.record.reasons|=FireRouteApplyRefused;
            LARGE_INTEGER stamp{};if(QueryPerformanceCounter(&stamp))fireTrace.record.writeNs=HandNanos(stamp.QuadPart);}
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
        // A staged request that never reached Apply is spent. Only a new
        // neutral sample and deliberate gesture may create another request.
        if(playerGrenade&&ordinaryGrenadeRequest)playerGrenade->Missing(ordinaryGrenadeFacts.source.nowNs);
#endif
        ammoCounterOwner.store({},std::memory_order_release);
        const ReloadStateOwner current{owner.player,owner.soldier,owner.weak,owner.weapon,rigEpoch,epoch,input.spaceGeneration};
        // Preserve only a registered exact same-owner timeout transition while
        // ControllerActions is crossing its neutral rearm barrier. No native
        // commit, pose or old receipt is accepted on this failed input Apply.
        const bool rearm=bodyHolster&&BodyHolsterInputRearm(*bodyHolster,current,out,
            grenade.value_or(false)||cycle.value_or(false)||bool(weaponMode)||bool(pendingMode)||bool(bodyDraw.command));
        bool selectionRearm=false;
        if(bodyInventory&&bodyHolster&&holsterBodySample&&bodyHolster->Phase()==BodyHolsterPhase::Held){
            LARGE_INTEGER stamp{};const bool clockOkay=QueryPerformanceCounter(&stamp)!=FALSE;
            if(clockOkay)holsterBodySample->hand.nowNs=HandNanos(stamp.QuadPart);
            const bool onlySemanticRearm=clockOkay&&nativePlaying&&aimValid&&sightPhase==interaction::SightFlipPhase::Idle&&
                !grenade.value_or(false)&&!cycle.value_or(false)&&!weaponMode&&!pendingMode&&!bodyDraw.command&&!holsterEquipment;
            const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
                [](void*,unsigned at,const char* name){return Type(at,name);}};
            selectionRearm=bodyInventory->RetainOrdinaryDrawRearm(memory,*holsterBodySample,out,onlySemanticRearm);
        }
        const bool magazineRearm=!out.active&&out.owner==owner.soldier&&
            !grenade.value_or(false)&&!cycle.value_or(false)&&!weaponMode&&!pendingMode&&!bodyDraw.command&&!holsterEquipment;
        CancelPhysicalReload(nullptr,0,magazineRearm);InvalidateBodyHolster(rearm,rearm?BodyHolsterLifecycleReason::InputRearm:BodyHolsterLifecycleReason::InputApplyRejected);
        if(bodyInventory&&!selectionRearm)bodyInventory->SuspendInteraction();if(weaponMode&&pendingMode){modeRecords[*pendingMode].cancelled=true;pendingMode.reset();--modeRequests;++modeRejections;}
    }
}
void __fastcall UpdateHook(void* self,void*,float dt){
    Callback callback;++updates;
    namespace trace=fvr::bc2::sceneMilestones;
    using fvr::runtime::SceneMilestoneKind;using fvr::runtime::SceneMilestoneStage;
    using fvr::runtime::SceneOwnerOutcome;
    fvr::runtime::SceneMilestoneContext observed{};observed.self=reinterpret_cast<unsigned>(self);
    const auto updateMilestone=trace::Begin(SceneMilestoneKind::GameplayUpdate,observed);
    Scope local{};
    const auto returnOriginal=[&](SceneOwnerOutcome outcome){
        observed.outcome=static_cast<unsigned>(outcome);
        const auto ownerTransition=outcome==SceneOwnerOutcome::Unknown?fvr::runtime::SceneMilestoneToken{}:
            trace::journal.Owner(static_cast<unsigned>(observed.self),outcome,observed,trace::Stamp);
        trace::Mark(ownerTransition,SceneMilestoneStage::CallingOriginal,observed);
        trace::Mark(updateMilestone,SceneMilestoneStage::CallingOriginal,observed);
        updateOriginal(self,dt);
        trace::Mark(ownerTransition,SceneMilestoneStage::OriginalReturned,observed);
        trace::Mark(ownerTransition,SceneMilestoneStage::Exit,observed);
        trace::Mark(updateMilestone,SceneMilestoneStage::OriginalReturned,observed);
        trace::Mark(updateMilestone,SceneMilestoneStage::Exit,observed);
    };
    // Preserve original short-circuit order and exactly one owner read. These
    // diagnostics do not revoke publications, rebind threads or admit an actor.
    if(!enabled.load(std::memory_order_acquire)){returnOriginal(SceneOwnerOutcome::Disabled);return;}
    if(scope){returnOriginal(SceneOwnerOutcome::Unknown);return;}
    const bool resolved=Resolve(local.owner);
    observed.player=local.owner.player;observed.soldier=local.owner.soldier;observed.weak=local.owner.weak;
    observed.controlled=local.owner.controlled;observed.manager=local.owner.manager;observed.entry=local.owner.entry;
    observed.inputThread=inputThread.load(std::memory_order_acquire);
    if(!resolved){returnOriginal(SceneOwnerOutcome::Unresolved);return;}
    if(local.owner.player!=reinterpret_cast<unsigned>(self)){returnOriginal(SceneOwnerOutcome::PlayerMismatch);return;}
    const auto thread=GetCurrentThreadId();unsigned expected=0;inputThread.compare_exchange_strong(expected,thread);
    if(inputThread.load()!=thread){++mismatches;returnOriginal(SceneOwnerOutcome::WrongThread);return;}
    observed.inputThread=thread;observed.outcome=static_cast<unsigned>(SceneOwnerOutcome::Valid);
    observed.actorGeneration=rigEpoch;observed.inputEpoch=inputEpoch;
    const auto localMilestone=trace::Begin(SceneMilestoneKind::LocalGameplayUpdate,observed);
    const auto ownerTransition=trace::journal.Owner(static_cast<unsigned>(observed.self),SceneOwnerOutcome::Valid,observed,trace::Stamp);
    ++localUpdates;if(local.owner.foot)++onFoot;else ++vehicle;
#ifdef FVR_BC2_ORDINARY_RESOURCE_PAUSE_PROBE
    // One scheduling interruption at the final rifle's ordinary reload entry.
    // No input, ammunition, equipment generation or outcome is fabricated.
    bool pausedHere=false;
    if(!nativeInputPauseProbe.ran&&resourceInventoryFixture&&local.owner==lastOwner&&local.owner.foot&&
       resourceInventoryFixture->State()==Bc2ResourceInventoryProbe::Phase::FullReturn){
        nativeInputPauseProbe.ran=true;nativeInputPauseProbe.before=nativeEpochs;
        nativeInputPauseProbe.begin=GetTickCount64();Sleep(180);
        nativeInputPauseProbe.end=GetTickCount64();pausedHere=true;
        // Re-resolve after the pause before using any saved native pointer.
        Owner checked{};
        nativeInputPauseProbe.sameOwner=Resolve(checked)&&checked==local.owner;
        if(!nativeInputPauseProbe.sameOwner){returnOriginal(SceneOwnerOutcome::Unresolved);return;}
    }
#endif
    const auto now=GetTickCount64();
    const bool actorChanged=local.owner.manager!=lastOwner.manager||local.owner.player!=lastOwner.player||local.owner.soldier!=lastOwner.soldier||local.owner.weak!=lastOwner.weak||local.owner.controlled!=lastOwner.controlled||local.owner.entry!=lastOwner.entry;
    if(local.owner!=lastOwner)vehicleShotObserver.Invalidate();
    if(actorChanged)++rigEpoch;
    if(actorChanged||local.owner.equipment!=lastOwner.equipment)++renderEquipmentEpoch;
    // Resolve succeeded on the verified input owner. A requested button,
    // input epoch change or missing read is not an observed vehicle exit.
    if(bodyInventory)bodyInventory->ObserveNativeActor(local.owner.manager,local.owner.player,local.owner.soldier,local.owner.weak,local.owner.foot);
    // Item changes still re-arm semantic actions, but cannot recalibrate anatomy.
    // A pause rearms button input independently of durable native equipment.
    // The complete resolved owner tuple still detects actual replacement.
    const bool ownerChanged=local.owner!=lastOwner;
    const bool inputPause=lastTick&&now-lastTick>150;
    if(inputPause&&!ownerChanged)++nativeInputPauses;
    if(bodyHolster&&!ownerChanged&&local.owner.foot&&inputPause&&epoch){
        const ReloadStateOwner before{local.owner.player,local.owner.soldier,local.owner.weak,local.owner.weapon,rigEpoch,epoch,handSample.owner.space};
        const auto previous=bodyHolster->Phase();
        const bool accepted=bodyHolster->ObserveNativeInputPause(before);auto source=handSample;
        LARGE_INTEGER stamp{};if(QueryPerformanceCounter(&stamp))source.nowNs=HandNanos(stamp.QuadPart);
        bodyHolsterLifecycle.Observe(accepted?BodyHolsterLifecycleReason::NativeInputGapAccepted:BodyHolsterLifecycleReason::NativeInputGapRejected,
            accepted,previous,bodyHolster->Phase(),source,before.equipGeneration);
    }
    const auto advanced=AdvanceNativeInputEpochs(nativeEpochs,ownerChanged,inputPause);
    if(!advanced){ClearReloadOwner();returnOriginal(SceneOwnerOutcome::Unresolved);return;}
    nativeEpochs=*advanced;lastOwner=local.owner;lastTick=now;
#ifdef FVR_BC2_ORDINARY_RESOURCE_PAUSE_PROBE
    if(pausedHere)nativeInputPauseProbe.after=nativeEpochs;
#endif
    struct Current {Scope*& slot;~Current(){slot=nullptr;}} current{scope};scope=&local;
    observed.actorGeneration=rigEpoch;observed.inputEpoch=inputEpoch;
    trace::Mark(localMilestone,SceneMilestoneStage::CallingOriginal,observed);
    trace::Mark(ownerTransition,SceneMilestoneStage::CallingOriginal,observed);
    trace::Mark(updateMilestone,SceneMilestoneStage::CallingOriginal,observed);
    const auto previousAttachedUpdate=attachedCurrentUpdate;
    if(local.owner.role==OwnerRole::AttachedSoldier)attachedCurrentUpdate=++attachedUpdateSerial;
    ObserveAttached(local.owner,AttachedPhase::UpdateBefore,reinterpret_cast<unsigned>(self));
    updateOriginal(self,dt);
    ObserveAttached(local.owner,AttachedPhase::UpdateAfter,reinterpret_cast<unsigned>(self));
    attachedCurrentUpdate=previousAttachedUpdate;
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
    if((ordinaryGrenade&&ordinaryGrenade->Started())||(playerGrenade&&playerGrenade->Started())){
        Owner after{};
        if(Resolve(after)&&after.foot){
            // Observation only: the next regular Update still owns epoch changes.
            // Never issue a request from this post-update view.
            GrenadeObserveCurrent(after,cachedInput,{},false,false);
        }else {LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);if(playerGrenade)playerGrenade->Missing(HandNanos(stamp.QuadPart));else ordinaryGrenade->Missing(HandNanos(stamp.QuadPart));}
    }
#endif
    trace::Mark(localMilestone,SceneMilestoneStage::OriginalReturned,observed);
    trace::Mark(localMilestone,SceneMilestoneStage::Exit,observed);
    trace::Mark(ownerTransition,SceneMilestoneStage::OriginalReturned,observed);
    trace::Mark(ownerTransition,SceneMilestoneStage::Exit,observed);
    trace::Mark(updateMilestone,SceneMilestoneStage::OriginalReturned,observed);
    trace::Mark(updateMilestone,SceneMilestoneStage::Exit,observed);
}
std::optional<std::size_t> Offset(const engine::PeImage& pe,unsigned rva,unsigned n){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&n<=s.rawSize-(rva-s.rva))return std::size_t(s.rawOffset)+rva-s.rva;return {};}
}
bool EnableOrdinaryGrenadeProbe()noexcept {
#ifdef FVR_BC2_ORDINARY_GRENADE_PROBE
    if(enabled.load()||ordinaryGrenade||playerGrenade||!ordinaryGrenadeBindings||!weaponModeBindingVerified||!hooks[0]||!bodyInventory||!bodyHolster||
       !physicalReload||!magazinePhysical||deathProbe||equipProbe||weaponVisibilityProbe||physicalReloadFixture||
       magazinePhysicalFixture||magazineResourceFixture||inventoryReloadFixture||resourceInventoryFixture||ordinaryBoltInputFixture||
       physicalBoltFixture||physicalPumpFixture||m95StockShotFixture||bodyHolsterFixture||reloadRequestFixture)return false;
    try{ordinaryGrenade=std::make_unique<private_grenade::Probe>();return true;}catch(...){return false;}
#else
    return false;
#endif
}
bool EnableTwoGrenadeDeathObservation()noexcept {
#ifdef FVR_BC2_TWO_GRENADE_DEATH_PROBE
    if(enabled.load()||twoGrenadeDeathEnabled||!EnablePlayerGrenades())return false;
    twoGrenadeDeathEnabled=true;return true;
#else
    return false;
#endif
}
std::optional<TwoGrenadeNativeWitness> ReadTwoGrenadeDeathReadiness(std::int64_t now)noexcept {
#ifdef FVR_BC2_TWO_GRENADE_DEATH_PROBE
    if(!twoGrenadeDeathEnabled||now<=0||twoGrenadeDeathGate.test_and_set(std::memory_order_acquire))return {};
    const auto value=twoGrenadeDeathWitness;twoGrenadeDeathGate.clear(std::memory_order_release);
    if(value.observedNs&&value.observedNs<=std::uint64_t(now)&&value.deadlineNs>std::uint64_t(now))return value;
#else
    (void)now;
#endif
    return {};
}
std::optional<ChestKnifeProbeWitness> ReadChestKnifeProbeWitness(std::int64_t now)noexcept {
#ifdef FVR_BC2_CHEST_KNIFE_PROBE
    // The coordinator is installed before Start and retained through Stop.
    if(chestKnife)return chestKnife->ProbeWitness(now);
#else
    (void)now;
#endif
    return {};
}
bool EnableChestKnifeDrawReturn(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t base,bool motionEnabled)noexcept {
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
    // Normal player grenades use the cache/body policy, not this independent
    // firing observer. The finite stock observer remains incompatible.
    if(ordinaryGrenade)return false;
#endif
    if(enabled.load()||chestKnife||!hooks[0]||!weaponModeBindingVerified||
       !bodyInventory||!bodyHolster||!twoHandGrip||!handPoses||!motionAim||!ordinaryManualCycles||
       deathProbe||equipProbe||weaponVisibilityProbe||physicalReloadFixture||magazinePhysicalFixture||magazineResourceFixture||
       inventoryReloadFixture||resourceInventoryFixture||ordinaryBoltInputFixture||physicalBoltFixture||physicalPumpFixture||
       m95StockShotFixture||bodyHolsterFixture||reloadRequestFixture)return false;
    try{
        auto candidate=std::make_unique<Bc2ChestKnifeCoordinator>();
        const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t count){return Read(at,dst,count);},
            [](void*,unsigned at,const char* expected){return Type(at,expected);}};
        if(!chestKnifeNative::Install(bytes,pe,base,memory,motionEnabled))return false;
        chestKnife=std::move(candidate);return true;
    }catch(...){return false;}
#else
    (void)bytes;(void)pe;(void)base;(void)motionEnabled;return false;
#endif
}
std::optional<ContextualToolProbeWitness> ReadContextualToolProbeWitness(std::int64_t now)noexcept {
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
    // Installed before Start, retained until Stop and all native callbacks drain.
    if(contextualTool)return contextualTool->ProbeWitness(now);
#else
    (void)now;
#endif
    return {};
}
bool EnableContextualTools()noexcept {
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
    if(enabled.load()||contextualTool||!hooks[0]||!weaponModeBindingVerified||!reloadStateBinding||
       !bodyInventory||!bodyHolster||!twoHandGrip||!handPoses||!motionAim||!ordinaryManualCycles||
       deathProbe||equipProbe||weaponVisibilityProbe||physicalReloadFixture||magazinePhysicalFixture||magazineResourceFixture||
       inventoryReloadFixture||resourceInventoryFixture||ordinaryBoltInputFixture||physicalBoltFixture||physicalPumpFixture||
       m95StockShotFixture||bodyHolsterFixture||reloadRequestFixture)return false;
    try{
        const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
            [](void*,unsigned at,const char* name){return Type(at,name);}};
        const auto binding=VerifyContextualToolBinding(memory,unsigned(image),imageSize);if(!binding)return false;
        auto candidate=std::make_unique<Bc2ContextualToolAdapter>();
        if(!candidate->Configure(memory,*binding,*reloadStateBinding))return false;
        contextualTool=std::move(candidate);return true;
    }catch(...){return false;}
#else
    return false;
#endif
}
bool EnablePlayerGrenades()noexcept {
#ifdef FVR_BC2_PLAYER_GRENADE
    if(enabled.load()||ordinaryGrenade||playerGrenade||!ordinaryGrenadeBindings||!weaponModeBindingVerified||!hooks[0]||!bodyInventory||!bodyHolster||
       !physicalReload||!magazinePhysical||deathProbe||equipProbe||weaponVisibilityProbe||physicalReloadFixture||
       magazinePhysicalFixture||magazineResourceFixture||inventoryReloadFixture||resourceInventoryFixture||ordinaryBoltInputFixture||
       physicalBoltFixture||physicalPumpFixture||m95StockShotFixture||bodyHolsterFixture||reloadRequestFixture)return false;
    try{playerGrenade=std::make_unique<player_grenade::Controller>();return true;}catch(...){return false;}
#else
    return false;
#endif
}
bool EnableSelectedMeshesObservation(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t base,bool persistent,
    std::string_view exactZoomConfigurationPath)noexcept {
    if(enabled.load()||!reloadStateBinding||base!=image||base>UINT32_MAX)return false;
    const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},nullptr};
    return selectedMeshes.Install(bytes,pe,unsigned(base),memory,true,persistent,exactZoomConfigurationPath);
}
bool EnablePhysicalReloadProbeRepeat()noexcept {
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
    return false; // Separate bounded lifecycle fixture; no two-shell mode combination.
#endif
    if(enabled.load()||bodyInventory||!physicalReloadMode||!physicalReload||!physicalReloadFixture)return false;
    physicalReloadFixture.emplace(true,2);return true;
}
bool EnableEmptyFireProbe()noexcept {
    if(enabled.load()||emptyFireFixture||!hooks[0]||!physicalReloadMode||!magazinePhysical||
       physicalReloadFixture||magazinePhysicalFixture||magazineResourceFixture||magazineDetachedFixture||reloadRequestFixture||
       bodyInventory||weaponVisibilityProbe||deathProbe||equipProbe||sightFlip)return false;
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
    if(physicalReloadFixture||armingEmptyFixture)return false;
    armingEmptyFixture.emplace();
#else
    emptyFireFixture.emplace();
#endif
    return true;
}
bool EnableWeaponVisibilityProbe()noexcept {
    if(enabled.load()||weaponVisibilityProbe||!hooks[0]||!twoHandGrip||!handPoses||!bodyFollow||!motionAim||
        bodyInventory||physicalReloadMode||physicalReloadFixture||reloadRequestFixture||deathProbe||equipProbe||sightFlip)return false;
    weaponVisibilityProbe.emplace(true);return true;
}
std::shared_ptr<const WeaponVisibilityProbeSample> ReadWeaponVisibilityProbe(std::int64_t now)noexcept {
    const auto sample=weaponVisibilitySample.load(std::memory_order_acquire);
    if(!enabled.load()||!sample||sample->sampledNs<=0||sample->sampledNs>now||now-sample->sampledNs>150000000)return {};
    if(sample->intent.enabled&&sample->intent.input.deadlineNs<=now)return {};
    return sample;
}
namespace {
bool VerifyHolsterActions()noexcept {
    // Existing DiscoverInputBinding proved gather/setters/table; verify every
    // additional live enum row before admitting the broader suppression mask.
    for(const auto [id,name]:{std::pair{7u,"EiaSwitchPrimaryWeapon"},std::pair{8u,"EiaFire"},std::pair{12u,"EiaAltFire"},std::pair{14u,"EiaZoom"},
        std::pair{29u,"EiaReload"},std::pair{33u,"EiaGrenadeLauncher"},std::pair{36u,"EiaDynamicGadget2"},std::pair{37u,"EiaMeleeAttack"},std::pair{38u,"EiaThrowGrenade"}}){
        const auto row=unsigned(image+profile.entryActions+id*24);std::array<char,32> value{};
        if(U32(row+16)!=id||std::strlen(name)+1>value.size()||!Read(U32(row),value.data(),std::strlen(name)+1)||std::strcmp(value.data(),name))return false;
    }
    return true;
}
bool HolsterSetupAvailable()noexcept {
    return !enabled.load()&&!bodyHolster&&bodyInventory&&!weaponVisibilityProbe&&!physicalReloadFixture&&!reloadRequestFixture&&!deathProbe&&!equipProbe;
}
}
bool EnableBodyHolsters(BodyHolsterCapabilities acceptance)noexcept {
    if(!HolsterSetupAvailable()||!acceptance.nativeVisibilityAccepted||!acceptance.nativeInputSuppressionAccepted||
        !acceptance.acceptedProfileMask||(acceptance.acceptedProfileMask&~3u)||!VerifyHolsterActions())return false;
    bodyHolster.emplace(acceptance);bodyInventory->EnableAutomaticStow();return true;
}
bool EnableBodyHolsterFixture(BodyHolsterDiagnosticProfile diagnosticProfile)noexcept {
    if(bodyHolsterFixture||!EnableBodyHolsterDiagnostic(15000,diagnosticProfile))return false;
    bodyHolsterFixture.emplace(true,diagnosticProfile);bodyHolsterFixtureSample.store({},std::memory_order_release);return true;
}
std::shared_ptr<const BodyHolsterFixtureSample> ReadBodyHolsterFixture(std::int64_t now)noexcept {
    const auto s=bodyHolsterFixtureSample.load(std::memory_order_acquire);
    if(!enabled.load()||!s||!s->actual||s->sampledNs<=0||s->sampledNs>now||now-s->sampledNs>150000000||
        s->actual->hand.observedNs<=0||s->actual->hand.observedNs>now||s->actual->hand.deadlineNs<=now)return {};
    return s;
}
bool EnableBodyHolsterDiagnostic(std::uint32_t lifetimeMs,BodyHolsterDiagnosticProfile diagnosticProfile)noexcept {
    if(!HolsterSetupAvailable()||!lifetimeMs||lifetimeMs>15000||!VerifyHolsterActions())return false;
    LARGE_INTEGER clock{};if(!QueryPerformanceCounter(&clock))return false;
    const auto now=HandNanos(clock.QuadPart),duration=std::int64_t(lifetimeMs)*1000000;
    if(now<=0||now>INT64_MAX-duration)return false;
    Bc2BodyHolster candidate; // Production acceptance bits remain false/profile0.
    if(!candidate.AdmitDiagnostic(now,now+duration,diagnosticProfile))return false;
    bodyHolster.emplace(std::move(candidate));bodyHolsterSample.store({},std::memory_order_release);return true;
}
std::shared_ptr<const BodyHolsterProbeSample> ReadBodyHolsterProbe(std::int64_t now)noexcept {
    const auto sample=bodyHolsterSample.load(std::memory_order_acquire);
    if(!enabled.load()||!sample||sample->sampledNs<=0||sample->sampledNs>now||now-sample->sampledNs>150000000||
        sample->hand.observedNs<=0||sample->hand.observedNs>now||sample->hand.deadlineNs<=now)return {};
    return sample; // Original source/trial bounds remain immutable, including expiry.
}
bool EnableBoatHeadAim(bool enableFire)noexcept {
    if(enabled.load(std::memory_order_acquire)||!hooks[0]||!hooks[1]||boatHeadAim)return false;
    const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
    const auto verified=VerifyBoatAimCode(memory,unsigned(image));if(!verified.verified)return false;
    boatAimBinding=verified;boatHeadFire=enableFire;boatHeadAim=true;return true;
}
bool EnableM95StockShotProbe(unsigned mode)noexcept {
#if defined(FVR_BC2_M95_STOCK_SHOT_PROBE) && defined(FVR_BC2_MANUAL_CYCLE_CANDIDATE) && !defined(FVR_BC2_AMMO_MOVE_PROBE)
    if((mode!=1&&mode!=2)||enabled.load()||m95StockShotFixture||!hooks[0]||!handPoses||!twoHandGrip||!reloadRequestMode||
       physicalPump||physicalPumpFixture||physicalReloadMode||bodyInventory||reloadRequestFixture||magazineReloadFixture||
       emptyFireFixture||bodyHolsterFixture)return false;
    if(mode==2&&!reloadFlowRuntime::EnableM95EmptyRecording())return false;
    m95StockShotFixture.emplace(mode==2);rigPublication::PublishM95ShotCapturePhase(1);return true;
#else
    return false;
#endif
}
bool EnableOrdinaryManualCycles()noexcept {
#if defined(FVR_BC2_ORDINARY_BOLT) && defined(FVR_BC2_ORDINARY_MANUAL_CYCLES) && defined(FVR_BC2_MANUAL_CYCLE_CANDIDATE) && !defined(FVR_BC2_AMMO_MOVE_PROBE)
    if(enabled.load()||ordinaryManualCycles||physicalBolt||physicalPump||!hooks[0]||!motionAim||!handPoses||!twoHandGrip||!reloadRequestMode||
       physicalBoltFixture||physicalPumpFixture||m95StockShotFixture||emptyFireFixture||physicalReloadFixture||reloadRequestFixture||
       magazineReloadFixture||magazinePhysicalFixture||magazineResourceFixture||inventoryReloadFixture||resourceInventoryFixture||bodyHolsterFixture)return false;
    const auto calibration=M95OrdinaryBoltCalibration235();
    if(!calibration||!BoltCalibrationValid(*calibration)||!PumpCalibrationValid(MeasuredSpasPump225())||
       !reloadFlowRuntime::EnableNativeCycleDispatcher())return false;
#ifdef FVR_BC2_NATIVE_CYCLE_RECOVERY
    if(!reloadFlowRuntime::EnableNativeCycleRecovery())return false;
#endif
    physicalBoltCalibration=std::make_shared<const Bc2BoltCalibration>(*calibration);
    physicalBolt.emplace(physicalBoltCalibration,OrdinaryBoltApi());ordinaryBoltPlayer.emplace(physicalBoltCalibration);
    physicalPump.emplace(std::make_shared<const Bc2PumpCalibration>(MeasuredSpasPump225()),OrdinaryPumpApi());
    if(!reloadFlowRuntime::EnableFixedContainerReload())return false;
    Bc2PhysicalFixedContainerApi fixedApi;
    fixedApi.read=[](void*,const ReloadStateOwner& owner,std::int64_t now)noexcept{return reloadFlowRuntime::ReadAmmoResourceView(owner,now);};
    fixedApi.withLedger=[](void*,const AmmoResourceBinding& binding,const interaction::HandInteractionOwner& owner,void* context,
        bool (*visit)(void*,interaction::AmmunitionLedger&)noexcept)noexcept{return reloadFlowRuntime::WithAmmoResourceLedger(binding,owner,context,visit);};
    fixedApi.submit=[](void*,const AmmoResourceRequest& request)noexcept{return reloadFlowRuntime::SubmitAmmoResourceRequest(request);};
    fixedApi.outcome=[](void*,std::uint64_t id,const interaction::AmmoResourceContext& context)noexcept{return reloadFlowRuntime::ReadAmmoResourceOutcome(id,context);};
    fixedApi.publishGate=[](void*,const FixedContainerGate& gate)noexcept{return reloadFlowRuntime::PublishFixedContainerGate(gate);};
    fixedApi.readGate=[](void*,const interaction::AmmoResourceIdentity& identity)noexcept{return reloadFlowRuntime::ReadFixedContainerGate(identity);};
    fixedApi.nextRequest=[](void*)noexcept{return handIntent==UINT64_MAX?0:++handIntent;};
    physicalFixedContainer.emplace(true,fixedApi);
    const auto pouch=interaction::ChestAmmoSupply(bodyAnchors);
    physicalFixedContainer->SetBodyContact({pouch.pouchCenterMeters,pouch.pouchRadiusMeters});
    ordinaryManualCycles=true;return true;
#else
    return false;
#endif
}
std::optional<PumpReceiverGapWitness> ReadPumpReceiverGapReady(std::int64_t now)noexcept {
#ifdef FVR_BC2_PUMP_RECEIVER_GAP_PROBE
    const auto witness=receiverGapReady.load(std::memory_order_acquire);
    if(witness&&ValidPumpReceiverGapWitness(*witness,now))return *witness;
#else
    (void)now;
#endif
    return {};
}
std::optional<ShellFocusWitness> ReadShellFocusReady(std::int64_t now)noexcept {
#ifdef FVR_BC2_SHELL_FOCUS_LOSS_PROBE
    const auto w=shellFocusReady.load(std::memory_order_acquire);
    if(w&&ValidShellFocusWitness(*w,now))return *w;
#else
    (void)now;
#endif
    return {};
}
bool EnableOrdinaryResourceInputProbe(bool interruptPump,bool emptyPump)noexcept {
#if defined(FVR_BC2_ORDINARY_RESOURCE_INPUT_PROBE) && defined(FVR_BC2_ORDINARY_MANUAL_CYCLES)
    if(enabled.load()||!ordinaryManualCycles||!physicalPump||!physicalBolt||!bodyInventory||!bodyHolster||
       !magazinePhysical||!physicalReload||!magazinePhysicalApi.resourceRead||!magazinePhysicalApi.resourceSubmit||
       resourcePumpCombination||resourceInventoryFixture||ordinaryBoltInputFixture||physicalBoltFixture||physicalPumpFixture||m95StockShotFixture)return false;
#ifndef FVR_BC2_NATIVE_CYCLE_RECOVERY
    if(interruptPump)return false;
#endif
    Bc2ResourceInventoryProbe candidate;
#ifdef FVR_BC2_ORDINARY_SUPPORT_ONLY_PROBE
    if(interruptPump||emptyPump||!candidate.EnableSupportOnly())return false;
#elif defined(FVR_BC2_ORDINARY_RESOURCE_MAGAZINE_ONLY_PROBE)
#ifdef FVR_BC2_ORDINARY_RESOURCE_MAGAZINE_EMPTY_PROBE
    constexpr bool fireMagazineEmpty=true;
#else
    constexpr bool fireMagazineEmpty=false;
#endif
#ifdef FVR_BC2_ORDINARY_RESOURCE_MAGAZINE_POSTFIRE_PROBE
    constexpr bool fireAfterMagazine=true;
#else
    constexpr bool fireAfterMagazine=false;
#endif
    if(interruptPump||emptyPump||!candidate.EnableMagazineOnly(fireMagazineEmpty,fireAfterMagazine,!fireMagazineEmpty))return false;
#else
    if(interruptPump&&emptyPump)return false;
    if(!emptyPump&&!candidate.RequireRifleSupport())return false;
#if defined(FVR_BC2_SHELL_FOCUS_LOSS_PROBE)
    if(interruptPump||emptyPump||!candidate.EnablePump(MeasuredSpasPump225(),
       Bc2PumpControllerProbe::Scenario::HeldSupportNatural,3)||!candidate.EnableTwoShells()||!candidate.EnableShellFocusLoss())return false;
#elif defined(FVR_BC2_PUMP_RECEIVER_GAP_PROBE)
    if(interruptPump||emptyPump||!candidate.EnablePump(MeasuredSpasPump225(),
       Bc2PumpControllerProbe::Scenario::HeldSupportNaturalReceiverGap,3)||!candidate.EnableTwoShells())return false;
#elif defined(FVR_BC2_NATIVE_CYCLE_EXPIRED_CONTROL_PROBE) || defined(FVR_BC2_PUMP_UNREACHABLE_HAND_PROBE)
    if(interruptPump||emptyPump||!candidate.EnablePump(MeasuredSpasPump225(),
       Bc2PumpControllerProbe::Scenario::HeldSupportNatural,3)||!candidate.EnableTwoShells())return false;
#else
    if(!candidate.EnablePump(MeasuredSpasPump225(),interruptPump?Bc2PumpControllerProbe::Scenario::HeldSupportNaturalTrackingLoss:
        emptyPump?Bc2PumpControllerProbe::Scenario::HeldSupportNatural:Bc2PumpControllerProbe::Scenario::None,emptyPump?8:1))return false;
#endif
#ifdef FVR_BC2_PUMP_UNREACHABLE_HAND_PROBE
    if(!candidate.EnableUnreachablePumpPose())return false;
#endif
#ifdef FVR_BC2_ORDINARY_RESOURCE_INSPECTION_PROBE
    if(interruptPump||emptyPump||!candidate.UsePumpInspectionPose())return false;
#endif
#ifdef FVR_BC2_ORDINARY_RESOURCE_REVISIT_PROBE
    if(interruptPump||emptyPump||!candidate.EnablePumpRevisit())return false;
#endif
#ifdef FVR_BC2_ORDINARY_RESOURCE_SCENE_PROBE
#ifdef FVR_BC2_ORDINARY_RESOURCE_SCENE_MAGAZINE_PROBE
    if(!candidate.EnableActorReplacement(true))return false;
#else
    if(!candidate.EnableActorReplacement())return false;
#endif
#endif
#endif
    resourceInventoryFixture.emplace(std::move(candidate));resourcePumpCombination=true;
    return true; // Reuses existing ordinary consumers/dispatcher; no new authority.
#else
    (void)interruptPump;(void)emptyPump;return false;
#endif
}
bool EnableOrdinaryBoltInputProbe(unsigned cycles)noexcept {
#if defined(FVR_BC2_ORDINARY_BOLT_INPUT_PROBE) && defined(FVR_BC2_ORDINARY_BOLT) && defined(FVR_BC2_ORDINARY_MANUAL_CYCLES)
    if((cycles!=1&&cycles!=2&&cycles!=5)||enabled.load()||!ordinaryManualCycles||!ordinaryBoltPlayer||!physicalBoltCalibration||
       ordinaryBoltInputFixture||physicalBoltFixture||physicalPumpFixture||resourceInventoryFixture||m95StockShotFixture)return false;
    ordinaryBoltInputFixture.emplace(*physicalBoltCalibration,cycles,Bc2BoltControllerProbe::Controls::OrdinaryPlayer);return true;
#else
    (void)cycles;return false;
#endif
}
bool EnablePhysicalBoltProbe(unsigned cycles)noexcept {
#if defined(FVR_BC2_MANUAL_BOLT_PROBE) && defined(FVR_BC2_MANUAL_CYCLE_CANDIDATE) && !defined(FVR_BC2_AMMO_MOVE_PROBE)
    if(cycles<1||cycles>2||enabled.load()||physicalBolt||physicalBoltFixture||!hooks[0]||!handPoses||!twoHandGrip||!reloadRequestMode||
       physicalPump||physicalReloadMode||bodyInventory||magazinePhysical||reloadRequestFixture||magazineReloadFixture||
       emptyFireFixture||bodyHolsterFixture||m95StockShotFixture||resourceInventoryFixture)return false;
    const auto calibration=M95PrivateBoltCalibration235();
    if(!calibration||!BoltCalibrationValid(*calibration)||!reloadFlowRuntime::EnableNativeBoltCandidate()||
       !reloadFlowRuntime::EnableNativeBoltRecording(cycles))return false;
    Bc2PhysicalBoltApi api;
    api.control=[](void*,const Bc2NativeCycleControl& c)noexcept{return reloadFlowRuntime::PublishNativeCycleControl(c);};
    api.view=[](void*,std::int64_t now)noexcept{return reloadFlowRuntime::ReadNativeCycleView(now);};
    api.ack=[](void*,const interaction::WeaponCycleReady& r)noexcept{return reloadFlowRuntime::AcknowledgeNativeCycleReady(r);};
    api.cancel=[](void*)noexcept{reloadFlowRuntime::CancelNativeCycle();};
    physicalBoltCalibration=std::make_shared<const Bc2BoltCalibration>(*calibration);
    physicalBolt.emplace(physicalBoltCalibration,api);physicalBoltFixture.emplace(*calibration,cycles);return true;
#else
    (void)cycles;return false;
#endif
}
bool EnablePhysicalPumpProbe(unsigned cycles)noexcept {
#if defined(FVR_BC2_MANUAL_PUMP_PROBE) && defined(FVR_BC2_MANUAL_CYCLE_CANDIDATE) && !defined(FVR_BC2_AMMO_MOVE_PROBE)
    if((cycles!=1&&cycles!=2&&cycles!=8)||physicalPumpFixture||physicalReloadMode||bodyInventory||magazinePhysical||!EnablePhysicalPumpCandidate(MeasuredSpasPump225()))return false;
    if(!reloadFlowRuntime::EnableNativePumpRecording(cycles))return false;
    physicalPumpFixture.emplace(MeasuredSpasPump225(),cycles);return true;
#else
    (void)cycles;return false;
#endif
}
bool EnablePhysicalPumpCandidate(const Bc2PumpCalibration& measured)noexcept {
    if(enabled.load()||physicalPump||!hooks[0]||!handPoses||!twoHandGrip||!reloadRequestMode||
       physicalReloadFixture||reloadRequestFixture||magazineReloadFixture||magazinePhysicalFixture||
       magazineResourceFixture||inventoryReloadFixture||(resourceInventoryFixture&&!resourcePumpCombination)||magazineDetachedFixture||
       !PumpCalibrationValid(measured)||!reloadFlowRuntime::EnableNativePumpCandidate())return false;
    Bc2PhysicalPumpApi api;
    api.control=[](void*,const Bc2NativeCycleControl& c)noexcept{return reloadFlowRuntime::PublishNativeCycleControl(c);};
    api.view=[](void*,std::int64_t now)noexcept{return reloadFlowRuntime::ReadNativeCycleView(now);};
    api.ack=[](void*,const interaction::WeaponCycleReady& r)noexcept{return reloadFlowRuntime::AcknowledgeNativeCycleReady(r);};
    api.cancel=[](void*)noexcept{reloadFlowRuntime::CancelNativeCycle();};
    physicalPump.emplace(std::make_shared<const Bc2PumpCalibration>(measured),api);return true;
}
bool EnableResourcePumpCombination()noexcept {
#if defined(FVR_BC2_RESOURCE_PUMP_PROBE) && defined(FVR_BC2_MANUAL_CYCLE_CANDIDATE) && defined(FVR_BC2_RESOURCE_INVENTORY_PROBE) && defined(FVR_BC2_AMMO_RESOURCE_HANDS)
    if(enabled.load()||resourcePumpCombination||physicalPump||physicalPumpFixture||m95StockShotFixture||
       !resourceInventoryFixture||!bodyInventory||!bodyHolster||!magazinePhysical||!physicalReload||
       !resourceInventoryFixture->EnablePump(MeasuredSpasPump225()))return false;
    resourcePumpCombination=true;
    if(!EnablePhysicalPumpCandidate(MeasuredSpasPump225())){resourcePumpCombination=false;return false;}
    return reloadFlowRuntime::EnableResourcePumpRecording();
#else
    return false;
#endif
}
bool EnableBodyInventory(interaction::BodyAnchorConfig config)noexcept {
    if(enabled.load()||bodyInventory||!hooks[0]||!twoHandGrip||!bodyFollow||!motionAim||!handPoses||!weaponModeBindingVerified||
        deathProbe||equipProbe||physicalReloadFixture||reloadRequestFixture||!physicalReloadMode||!physicalReload||!interaction::ValidBodyAnchors(config))return false;
    if(magazinePhysicalFixture&&!magazinePhysicalFixture->UseChestSupply())return false;
    bodyAnchors=config;bodyInventory.emplace(true,config);
    // Before Start there are no items/reservations/cycles to discard. Existing
    // fixture input follows the same selected chest anchor as the consumer.
    if(physicalReload){physicalReload.emplace(true,physicalReloadApi,interaction::ChestAmmoSupply(config));physicalReload->EnableBeltAmmo(true,interaction::SupplyAnchorFrame::RecenteredBody);}
    if(magazinePhysical){auto pouch=interaction::ChestAmmoSupply(config);
        const auto magazinePouch=Bc2MagazinePhysicalReload::DefaultPouch();pouch.itemNamespace=magazinePouch.itemNamespace;pouch.pouch=magazinePouch.pouch;
        magazinePhysical.emplace(true,magazinePhysicalApi,pouch);magazinePhysical->EnableBodyAmmo(true,interaction::SupplyAnchorFrame::RecenteredBody);}
    return true;
}
std::optional<graphics::VehicleReticleSample> ReadVehicleReticle(std::int64_t nowNs)noexcept {
    if(!enabled.load(std::memory_order_acquire))return {};
    return boatReticle.Read(nowNs);
}
void ReportWeaponRoutes(std::ostream& out){
    // NativeProbe calls after installation/start and before teardown. The
    // consumer is constructed only before Start and lives through this loop;
    // its observer takes a bounded nonblocking snapshot before formatting.
    if(!enabled.load(std::memory_order_acquire)||!magazinePhysical){out<<"{\"active\":false}";return;}
    magazinePhysical->EntryTelemetry().Report(out);
}
graphics::AmmoCounterSample ReadAmmoCounter()noexcept {
    if(!enabled.load(std::memory_order_acquire))return {};
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto current=ammoCounterOwner.load(std::memory_order_acquire);
        if(!current)return {};
        const auto sample=ReadAmmoCounterHostSample(*current,
            []{return reloadFlowRuntime::ReadPublishedReserve();},
            []{LARGE_INTEGER stamp{};return QueryPerformanceCounter(&stamp)?HandNanos(stamp.QuadPart):0;});
        if(ammoCounterOwner.load(std::memory_order_acquire)==current)return sample;
    }return {};
}
bool BodyDisplayNativeCurrent(const BodyInventoryDisplay& d)noexcept {
    const WeaponModeMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
    const auto current=ReadBodyInventory(memory,d.selectedOwner,true);
    if(!current.snapshot)return false;const auto& n=*current.snapshot;
    return n.owner==d.selectedOwner&&BodyCarriedIdentity{n.inventory,n.switching,n.count,n.items}==d.carried;
}
std::shared_ptr<const SelectedMeshesSnapshot> ReadSelectedMeshes(const ReloadStateOwner& owner,std::int64_t now)noexcept {
    return selectedMeshes.Read(owner,now);
}
std::shared_ptr<const SelectedMeshesSnapshot> ReadCurrentSelectedMeshes(std::int64_t now)noexcept {
    return selectedMeshes.ReadCurrent(now);
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t base,InputReader reader,bool enableMotionAim,bool enableBodyFollow,bool enablePoseObservation,bool enableRigPulse,bool enableHandPoses,bool enableDeathProbe,bool enableEquipProbe,bool enableMuzzleFire,bool enableTwoHandGrip,bool enableSightFlip,bool reloadHoldProbe,bool reloadRoundProbe,bool reloadRequestProbe,bool enablePhysicalReload,bool enablePhysicalReloadProbe,unsigned magazineReloadSession){
    if(hooks[0]||hooks[1]||!reader)return false;const auto found=DiscoverInputBinding(bytes,pe);if(!found)return false;
    sightFlip=enableSightFlip;if(sightFlip&&!enableTwoHandGrip)return false;
    LARGE_INTEGER ownershipClock{};if(!QueryPerformanceFrequency(&ownershipClock)||ownershipClock.QuadPart<=0)return false;handFrequency=ownershipClock.QuadPart;
    twoHandGrip=enableTwoHandGrip;if(twoHandGrip&&(!enableMuzzleFire||!enableHandPoses))return false;
    equipProbe=enableEquipProbe;muzzleFire=enableMuzzleFire;if((equipProbe||muzzleFire)&&!enableMotionAim)return false;if(muzzleFire&&!enableHandPoses)return false;
    deathProbe=enableDeathProbe;if(deathProbe&&!enableMotionAim)return false;
    motionAim=enableMotionAim;bodyFollow=enableBodyFollow;observePoses=enablePoseObservation;handPoses=enableHandPoses;if((bodyFollow||observePoses)&&!motionAim)return false;profile=*found;const auto aim=DiscoverAiming(bytes,pe);if(!aim)return false;aimingProfile=*aim;image=base;imageSize=pe.imageSize;readInput=reader;
    const VehicleRouteMemory vehicleMemory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
    campaignVehicleBinding=VerifyCampaignVehicleCode(vehicleMemory,unsigned(image));
    vehicleShotBinding=VerifyVehicleShotCode(vehicleMemory,unsigned(image));
    boatReticleBinding=VerifyVehicleReticleSourceCode(vehicleMemory,unsigned(image));
#ifdef FVR_BC2_VEHICLE_MOUNTED_PROBE
    // All original code fingerprints must be read before the shared Update
    // detour is installed. No fallback to an unverified or occupied observer.
    const auto mountedBinding=VerifyVehicleMountedCode(vehicleMemory,unsigned(image));
    if(!mountedBinding.verified||(!handPoses&&!observePoses))return false;
    VehicleMountedApi mountedApi;mountedApi.context=&vehicleShotObserver;mountedApi.memory=vehicleMemory;
    mountedApi.source=[](void* p,VehicleShotSource& out)noexcept{
        const auto source=static_cast<VehicleShotObserver*>(p)->Source();
        if(!source)return false;out=*source;return true;
    };
    mountedApi.current=[](void* p,const VehicleShotSource& source)noexcept{
        return enabled.load(std::memory_order_acquire)&&static_cast<VehicleShotObserver*>(p)->Current(source);
    };
    mountedApi.seat=[](void*,const VehicleShotSource& source,VehicleRouteSnapshot& out)noexcept{
        Owner current{};const auto& expected=source.owner;
        if(!Resolve(current)||current.foot||current.manager!=expected.manager||current.player!=expected.player||
           current.soldier!=expected.soldier||current.weak!=expected.weak)return false;
        const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},
            [](void*,unsigned at,const char* name){return Type(at,name);}};
        const VehicleRouteBinding binding{unsigned(image+profile.gameplay.managerVtable),unsigned(image+profile.gameplay.inputRouterVtable),unsigned(image+profile.cacheVtable),true};
        const auto routes=ReadVehicleRoutes(memory,binding,expected);
        if(routes.status!=VehicleRouteStatus::Okay)return false;out=routes.snapshot;return true;
    };
    mountedApi.clock=[](void*)noexcept->std::int64_t{
        LARGE_INTEGER now{};return QueryPerformanceCounter(&now)?HandNanos(now.QuadPart):0;
    };
    mountedApi.stack=[](void*,unsigned at,std::size_t bytes)noexcept{
        const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
        const auto low=reinterpret_cast<std::uintptr_t>(tib->StackLimit),high=reinterpret_cast<std::uintptr_t>(tib->StackBase);
        return low<high&&at>=low&&at<high&&bytes<=high-at;
    };
    if(!vehicleMountedObserver.Configure(mountedBinding,mountedApi))return false;
#endif
    if(unsigned(reloadHoldProbe)+unsigned(reloadRoundProbe)+unsigned(reloadRequestProbe)>1||((reloadHoldProbe||reloadRoundProbe||reloadRequestProbe)&&!handPoses)||
        (reloadRequestProbe&&!twoHandGrip))return false;
    if(enablePhysicalReload&&(reloadHoldProbe||reloadRoundProbe||reloadRequestProbe||enableRigPulse||enableDeathProbe||enableEquipProbe||!twoHandGrip||!handPoses))return false;
    if(enablePhysicalReloadProbe&&!enablePhysicalReload)return false;
    if(enablePhysicalReloadProbe){
#ifdef FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO
        physicalReloadFixture.emplace(true,1,static_cast<PhysicalReloadLifecycleScenario>(FVR_BC2_PHYSICAL_RELOAD_LIFECYCLE_SCENARIO));
#else
        physicalReloadFixture.emplace(true);
#endif
    }
    const bool magazinePhysicalProbe=magazineReloadSession==4||magazineReloadSession==5;
#ifdef FVR_BC2_RESOURCE_INVENTORY_PROBE
    const bool resourceInventoryProbe=magazineReloadSession==7;
#else
    constexpr bool resourceInventoryProbe=false;
#endif
    const bool magazineNormal=magazineReloadSession==3||magazinePhysicalProbe||magazineReloadSession==6||magazineReloadSession==7||magazineReloadSession==8;
    const bool magazineProbe=magazineReloadSession==1||magazineReloadSession==2;
    if(magazineReloadSession>8||((magazinePhysicalProbe||magazineReloadSession>=6)&&(enableSightFlip||reloadHoldProbe||reloadRoundProbe||reloadRequestProbe||enableRigPulse||enableDeathProbe||enableEquipProbe))||
       (magazineNormal&&(!enablePhysicalReload||enablePhysicalReloadProbe))||
       (magazineProbe&&(reloadHoldProbe||reloadRoundProbe||reloadRequestProbe||enablePhysicalReload||enablePhysicalReloadProbe||enableSightFlip||enableRigPulse||enableDeathProbe||enableEquipProbe||!handPoses||!twoHandGrip)))return false;
    physicalReloadMode=enablePhysicalReload;reloadRequestMode=reloadRequestProbe||physicalReloadMode||magazineReloadSession!=0;
#ifdef FVR_BC2_MANUAL_CYCLE_CANDIDATE
    // Private trial may install request hooks without a synthetic reload driver.
    if(handPoses&&twoHandGrip&&!reloadHoldProbe&&!reloadRoundProbe&&!reloadRequestProbe&&!enablePhysicalReload&&!magazineReloadSession)
        reloadRequestMode=true;
#endif
    if(handPoses||observePoses){
        reloadStateBinding=DiscoverReloadState(bytes,pe);
        const ReloadStateMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
        if(reloadStateBinding&&!ValidateReloadStateLive(memory,*reloadStateBinding,unsigned(base)))reloadStateBinding.reset();
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
        // Melee installs independent verified listeners, with no shared firing
        // observer or Step/Update hook. Rejection keeps the existing37 fallback.
        if(handPoses&&motionAim&&twoHandGrip&&reloadStateBinding)(void)selectedKnife::Install(bytes,pe,base,memory);
#endif
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
        ordinaryGrenadeServer=DiscoverReloadServer(bytes,pe);
        ordinaryGrenadeBindings=reloadStateBinding&&ordinaryGrenadeServer&&
            ValidateReloadServerLive(memory,*ordinaryGrenadeServer,unsigned(base))&&private_grenade::VerifyStockReturn(memory,unsigned(base));
#endif
        const bool flowInstalled=reloadStateBinding&&reloadFlowRuntime::Install(bytes,pe,base,memory,reloadHoldProbe,reloadRoundProbe,reloadRequestMode&&!magazineProbe,magazineProbe);
        if((reloadHoldProbe||reloadRoundProbe||reloadRequestMode)&&!flowInstalled)return false;
#ifdef FVR_BC2_VEHICLE_MOUNTED_PROBE
        vehicleMountedRegistered=flowInstalled&&reloadFlowRuntime::RegisterOriginalFiringObserver(vehicleMountedObserver.Callbacks());
        vehicleMountedObserver.Registration(vehicleMountedRegistered);
        if(!vehicleMountedRegistered)return false;
#endif
        if(magazineNormal&&!reloadFlowRuntime::EnableMagazineRequestCycles())return false;
        if((magazineReloadSession==7||magazineReloadSession==8)&&!reloadFlowRuntime::DeferInventoryReloadRecords(magazineReloadSession==8||resourceInventoryProbe))return false;
        if(physicalReloadMode){
            PhysicalReloadApi api;
            api.reserve=[](void*)noexcept{return reloadFlowRuntime::ReadReserve();};
            api.identity=[](void*)noexcept{return reloadFlowRuntime::RequestIdentity();};
            api.start=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::StartRequestCycle(c);};
            api.startObserved=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::StartRequestCycleObserved(c);};
            api.keep=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::KeepAliveRequestCycle(c);};
            api.keepObserved=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::KeepAliveRequestCycleObserved(c);};
            api.reserveObserved=[](void*)noexcept{return reloadFlowRuntime::ReadReserveObserved();};
            api.lease=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::RequestLease(id,cycle);};
            api.submit=[](void*,const Bc2ReloadNativeRequest& request)noexcept{return reloadFlowRuntime::SubmitRequest(request);};
            api.ack=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::TakeRequestAcknowledgement(id,cycle);};
            api.cancel=[](void*)noexcept{reloadFlowRuntime::CancelRequestCycle();};
            api.retire=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::RetireRequestCycle(id,cycle);};
            api.clock=[](void*)noexcept{LARGE_INTEGER now{};QueryPerformanceCounter(&now);return HandNanos(now.QuadPart);};
            physicalReloadApi=api;physicalReload.emplace(true,api);if(!enablePhysicalReloadProbe)physicalReload->EnableBeltAmmo();
            if(!enablePhysicalReloadProbe&&magazineReloadSession==3){
                if(!reloadFlowRuntime::EnableFeedEquipmentReload())return false;
                Bc2PhysicalFeedApi feed;
                feed.read=[](void*,const ReloadStateOwner& owner,std::int64_t now)noexcept{return reloadFlowRuntime::ReadFeedEquipmentView(owner,now);};
                feed.submit=[](void*,const FeedEquipmentSubmission& command)noexcept{return reloadFlowRuntime::SubmitFeedEquipmentCommand(command);};
                feed.receipt=[](void*,std::uint64_t lifetime,std::uint64_t request)noexcept{return reloadFlowRuntime::ReadFeedEquipmentReceipt(lifetime,request);};
                feed.readAt=[](void*,const ReloadStateOwner& owner,std::int64_t observed,std::int64_t now)noexcept{
                    return reloadFlowRuntime::ReadFeedEquipmentViewAt(owner,observed,now);};
                feed.submitObserved=[](void*,const FeedEquipmentSubmission& command,bool noEntryOnly)noexcept{
                    return reloadFlowRuntime::SubmitFeedEquipmentCommandResult(command,noEntryOnly);};
                feed.publishGate=[](void*,const FeedEquipmentGate& gate)noexcept{return reloadFlowRuntime::PublishFeedEquipmentGate(gate);};
                feed.readGate=[](void*,const interaction::AmmoResourceIdentity& resource)noexcept{return reloadFlowRuntime::ReadFeedEquipmentGate(resource);};
                feed.retirement=[](void*,std::uint64_t afterEvent)noexcept{return reloadFlowRuntime::ReadFeedEquipmentActorRetirement(afterEvent);};
                physicalFeed.emplace(feed);
                Bc2PhysicalPistolSlideApi slide;
                slide.event=[](void*,std::uint64_t after)noexcept{return reloadFlowRuntime::ReadPistolAfterFeedEvent(after);};
                slide.read=[](void*,const interaction::AmmoResourceIdentity& resource,std::int64_t now)noexcept{
                    return reloadFlowRuntime::ReadPistolAfterFeedState(resource,now);};
                slide.release=[](void*,const PistolAfterFeedRelease& request)noexcept{return reloadFlowRuntime::ReleasePistolAfterFeedGate(request);};
                slide.retirement=[](void*,std::uint64_t after)noexcept{return reloadFlowRuntime::ReadPistolAfterFeedActorRetirement(after);};
                if(!reloadFlowRuntime::EnablePistolAfterFeedPolicy())return false;
                physicalPistolSlide.emplace(slide);
            }
        }
        if(magazineNormal){
            MagazinePhysicalApi api;
            api.reserve=[](void*)noexcept{return reloadFlowRuntime::ReadReserve();};
            api.identity=[](void*)noexcept{return reloadFlowRuntime::RequestIdentity();};
            api.start=[](void*,const ReloadCycleControl& c,const interaction::ManualReloadRequest& r,const ReloadMagazineStartupPulse& pulse)noexcept{return reloadFlowRuntime::StartMagazineRequestCycleObserved(c,r,pulse);};
            api.inspectStart=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle,const std::optional<ReloadMagazineStartupPulse>& pulse)noexcept{return reloadFlowRuntime::InspectMagazineRequestCycleStart(id,cycle,pulse);};
            api.keep=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::KeepAliveRequestCycle(c);};
            api.keepObserved=[](void*,const ReloadCycleControl& c)noexcept{return reloadFlowRuntime::KeepAliveRequestCycleObserved(c);};
            api.reserveObserved=[](void*)noexcept{return reloadFlowRuntime::ReadReserveObserved();};
            api.lease=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::RequestMagazineLease(id,cycle);};
            api.leaseObserved=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::ObserveMagazineLease(id,cycle);};
            api.gate=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::TakeMagazineUnseatAcknowledgement(id,cycle);};
            api.submit=[](void*,const ReloadMagazineNativeRequest& r)noexcept{return reloadFlowRuntime::SubmitMagazineRequest(r);};
            api.ack=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::TakeMagazineAcknowledgement(id,cycle);};
            api.cancel=[](void*)noexcept{reloadFlowRuntime::CancelRequestCycle();};
            api.retire=[](void*,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept{return reloadFlowRuntime::RetireRequestCycle(id,cycle);};
            api.clock=[](void*)noexcept{LARGE_INTEGER now{};QueryPerformanceCounter(&now);return HandNanos(now.QuadPart);};
            if(reloadFlowRuntime::ResourceHandsEnabled()&&(magazineReloadSession==3||magazinePhysicalProbe||resourceInventoryProbe)){
                api.resourceRead=[](void*,const ReloadStateOwner& owner,std::int64_t now)noexcept{return reloadFlowRuntime::ReadAmmoResourceView(owner,now);};
                api.resourceReadObserved=[](void*,const ReloadStateOwner& owner,std::int64_t now)noexcept{return reloadFlowRuntime::ReadAmmoResourceObserved(owner,now);};
                api.resourceSubmit=[](void*,const AmmoResourceRequest& r)noexcept{return reloadFlowRuntime::SubmitAmmoResourceRequest(r);};
                api.resourceOutcome=[](void*,std::uint64_t request,const interaction::AmmoResourceContext& context)noexcept{return reloadFlowRuntime::ReadAmmoResourceOutcome(request,context);};
            }
            magazinePhysicalApi=api;magazinePhysical.emplace(true,api);
    if(magazinePhysicalProbe){
        if(api.resourceRead&&api.resourceSubmit)magazineResourceFixture.emplace(true,magazineReloadSession==5,magazineReloadSession==4,true);
        else magazinePhysicalFixture.emplace(true,magazineReloadSession==5,magazineReloadSession!=5,magazineReloadSession==4);
    }
    if(resourceInventoryProbe){if(!api.resourceRead||!api.resourceSubmit)return false;resourceInventoryFixture.emplace();}
    else if(magazineReloadSession==7||magazineReloadSession==8)inventoryReloadFixture.emplace(magazineReloadSession==8);
            if(MagazineDetachedSessionEnabled(magazineReloadSession))magazineDetached.emplace(true);
            if(magazineReloadSession==6)magazineDetachedFixture.emplace(true);
        }
        if(magazineProbe)magazineReloadFixture.emplace(true,MagazineReloadProbeApi{reloadFlowRuntime::RequestIdentity,reloadFlowRuntime::StartMagazineRequestCycle,reloadFlowRuntime::KeepAliveRequestCycle,reloadFlowRuntime::RequestMagazineLease,reloadFlowRuntime::SubmitMagazineRequest,reloadFlowRuntime::TakeMagazineAcknowledgement,reloadFlowRuntime::CancelRequestCycle,reloadFlowRuntime::ReadRequestProbeSnapshot,[]()noexcept{LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);return HandNanos(clock.QuadPart);},reloadFlowRuntime::RetireRequestCycle,reloadFlowRuntime::ReadReserve,reloadFlowRuntime::TakeMagazineUnseatAcknowledgement},magazineReloadSession==2);
        if(reloadRequestProbe)reloadRequestFixture.emplace(true,ReloadRequestProbeApi{reloadFlowRuntime::RequestIdentity,reloadFlowRuntime::StartRequestCycle,reloadFlowRuntime::KeepAliveRequestCycle,reloadFlowRuntime::RequestLease,reloadFlowRuntime::SubmitRequest,reloadFlowRuntime::TakeRequestAcknowledgement,reloadFlowRuntime::CancelRequestCycle,reloadFlowRuntime::ReadRequestProbeSnapshot,[]()noexcept{LARGE_INTEGER clock{};QueryPerformanceCounter(&clock);return HandNanos(clock.QuadPart);},reloadFlowRuntime::RetireRequestCycle,reloadFlowRuntime::ReadReserve});
    }
    for(const auto [rva,n]:{std::pair{profile.gameplay.playerInputUpdate,0x1ebu},std::pair{profile.gameplay.inputGather,0x232u},std::pair{profile.gameplay.contextGetter,0x79u},std::pair{profile.gameplay.soldierGetter,23u},std::pair{profile.gameplay.inputRouterVtable,20u},std::pair{profile.cacheConstructor,0x41u},std::pair{profile.cacheVtable,4u},std::pair{profile.buttonSetter,0x8cu},std::pair{profile.floatSetter,0x108u},std::pair{profile.controlledGetter,7u},std::pair{profile.attachedPredicate,0x46u},std::pair{aimingProfile.weaponGetter,0x41u},std::pair{aimingProfile.indexGetter,7u},std::pair{aimingProfile.aimGetter,4u},std::pair{aimingProfile.yawGetter,0xa6u},std::pair{aimingProfile.inputPrepare,0x1a3u},std::pair{aimingProfile.absoluteYawSetter,17u},std::pair{aimingProfile.angleCopy,0x5du},std::pair{aimingProfile.aimerYawSetter,0x38u},std::pair{aimingProfile.aimerPitchSetter,0x5fu}}){
        const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);
        if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
    }
    if(equipProbe||sightFlip||physicalReloadMode){
        const auto mode=DiscoverWeaponMode(bytes,pe);
        weaponModeBindingVerified=bool(mode);
        if(mode)for(const auto [rva,n]:{std::pair{mode->selector,0x1b1u},std::pair{mode->update,0x86u},std::pair{mode->edgeReader,0x37u},std::pair{mode->booleanReader,0x88u},std::pair{mode->scalarReader,0x7cu}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);
            if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))weaponModeBindingVerified=false;
        }
    }
    if(equipProbe||muzzleFire){const auto fire=DiscoverFireOrigin(bytes,pe);if(!fire)return false;fireProfile=*fire;
        for(const auto [rva,n]:{std::pair{fireProfile.builder,0x158u},std::pair{fireProfile.serverShoot,0xc90u},std::pair{fireProfile.serverControlledGetter,0x21u},std::pair{fireProfile.serverContextGetter,0x9du},std::pair{fireProfile.serverManagerConstructor,0x39u},std::pair{fireProfile.serverPlayerCreate,0x71u},std::pair{fireProfile.serverPlayerConstructor,0x1fu},std::pair{fireProfile.playerConstructor,0x5au},std::pair{fireProfile.clientShoot,0xb9eu},std::pair{fireProfile.matrixCopy,0x57u},std::pair{fireProfile.compose,0xb0u},std::pair{fireProfile.spreadSeed,8u},std::pair{fireProfile.randomSeed,0x15u}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
        }
#ifdef FVR_BC2_MUZZLE_ENROLLMENT_HEADER
        projectileLaunchBinding=VerifyProjectileLaunchCode({nullptr,[](void*,unsigned at,void* dst,std::size_t size){return Read(at,dst,size);}},unsigned(base),fireProfile.serverShoot);
        if(!projectileLaunchBinding.verified)return false;
        const auto launchHelper=reinterpret_cast<void*>(projectileLaunchBinding.helper);
        if(MH_CreateHook(launchHelper,ProjectileContextHook,reinterpret_cast<void**>(&projectileContextOriginal))!=MH_OK)return false;
        hooks[13]=launchHelper;
#endif
        const auto address=reinterpret_cast<void*>(base+fireProfile.builder);
        if(MH_CreateHook(address,FireOriginHook,reinterpret_cast<void**>(&fireOriginOriginal))!=MH_OK)return false;hooks[8]=address;
        const auto client=reinterpret_cast<void*>(base+fireProfile.clientShoot),copy=reinterpret_cast<void*>(base+fireProfile.matrixCopy);
        if(MH_CreateHook(client,ClientShootHook,reinterpret_cast<void**>(&clientShootOriginal))!=MH_OK)return false;hooks[9]=client;
        if(MH_CreateHook(copy,MatrixCopyHook,reinterpret_cast<void**>(&matrixCopyOriginal))!=MH_OK)return false;hooks[10]=copy;
        const auto server=reinterpret_cast<void*>(base+fireProfile.serverShoot),compose=reinterpret_cast<void*>(base+fireProfile.compose);
        if(MH_CreateHook(server,ServerShootHook,reinterpret_cast<void**>(&serverShootOriginal))!=MH_OK)return false;hooks[11]=server;
        if(MH_CreateHook(compose,ComposeHook,reinterpret_cast<void**>(&composeOriginal))!=MH_OK)return false;hooks[12]=compose;
    }
    if(observePoses){const auto poses=DiscoverFirstPersonPose(bytes,pe);if(!poses)return false;poseProfile=*poses;
        for(const auto [rva,n]:{std::pair{poseProfile.animationUpdate,0x190u},std::pair{poseProfile.worldBuilder,0x204u},std::pair{poseProfile.rootSetter,0x56u}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
        }
        const auto rig=DiscoverRig(bytes,pe);if(!rig)return false;rigProfile=*rig;
        for(const auto [rva,n]:{std::pair{rigProfile.animationGetter,0x2fu},std::pair{rigProfile.animationUpdate,0x2f3u},std::pair{rigProfile.evaluate,0xdau},std::pair{rigProfile.postEvaluate,0xe9u},std::pair{rigProfile.weaponWorld,0x14u},std::pair{rigProfile.boneWorld,0x2bu},std::pair{rigProfile.worldThunk,0x15u},std::pair{rigProfile.worldIndex,0x17u},std::pair{rigProfile.skinSelect,0x15u},std::pair{rigProfile.skinGetter,8u},std::pair{rigProfile.paletteGetter,10u},std::pair{rigProfile.skinThunk,16u},std::pair{rigProfile.skinData,8u}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
        }
        const auto add=[&](unsigned slot,unsigned rva,void* replacement,void** original){const auto address=reinterpret_cast<void*>(base+rva);if(MH_CreateHook(address,replacement,original)!=MH_OK)return false;hooks[slot]=address;return true;};
        if(!add(4,rigProfile.animationUpdate,reinterpret_cast<void*>(AnimationHook),reinterpret_cast<void**>(&animationOriginal))||
           !add(5,rigProfile.evaluate,reinterpret_cast<void*>(EvaluateHook),reinterpret_cast<void**>(&evaluateOriginal))||
           !add(6,rigProfile.postEvaluate,reinterpret_cast<void*>(PostEvaluateHook),reinterpret_cast<void**>(&postOriginal))||
           !add(7,rigProfile.weaponWorld,reinterpret_cast<void*>(WeaponWorldHook),reinterpret_cast<void**>(&weaponWorldOriginal)))return false;
        const auto world=reinterpret_cast<void*>(base+poseProfile.worldBuilder),root=reinterpret_cast<void*>(base+poseProfile.rootSetter);
        if(MH_CreateHook(world,WorldPoseHook,reinterpret_cast<void**>(&worldPoseOriginal))!=MH_OK)return false;hooks[2]=world;
        if(MH_CreateHook(root,RootPoseHook,reinterpret_cast<void**>(&rootPoseOriginal))!=MH_OK)return false;hooks[3]=root;
    }
    if(observePoses||enableRigPulse){
        if(!rigPublication::Install(bytes,pe,base,[](unsigned soldier,unsigned& weak)noexcept{Owner owner{};if(!Resolve(owner))return false;ObserveAttached(owner,AttachedPhase::RigOwner,soldier);if(!owner.foot||owner.soldier!=soldier)return false;weak=owner.weak;return true;},enableRigPulse,handPoses))return false;
    }
    if(bodyFollow){const auto position=DiscoverBodyPosition(bytes,pe);if(!position)return false;bodyProfile=*position;
        for(const auto [rva,n]:{std::pair{bodyProfile.getter,0x18u},std::pair{bodyProfile.fallback,10u}}){
            const auto offset=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!offset||!Read(base+rva,live.data(),n)||std::memcmp(live.data(),bytes.data()+*offset,n))return false;
        }
    }
    {const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};attachedBinding=VerifyAttachedSoldierCode(memory,unsigned(base));}
    const auto update=reinterpret_cast<void*>(base+profile.gameplay.playerInputUpdate),gather=reinterpret_cast<void*>(base+profile.gameplay.inputGather);
    if(MH_CreateHook(update,UpdateHook,reinterpret_cast<void**>(&updateOriginal))!=MH_OK)return false;hooks[0]=update;
    if(MH_CreateHook(gather,GatherHook,reinterpret_cast<void**>(&gatherOriginal))!=MH_OK)return false;hooks[1]=gather;return true;
}
bool AdjustViewBase(std::array<RenderViewCopy,2>& copies,const runtime::TrackingFrame& tracking)noexcept {
    if(!enabled.load(std::memory_order_acquire))return true;
    const auto space=tracking.spaceGeneration;
    Owner viewOwner{};
    if(Resolve(viewOwner)&&!viewOwner.foot){
        ObserveAttached(viewOwner,AttachedPhase::ViewBase,viewOwner.entry);
        std::lock_guard lock(vehicleViewMutex);
        const auto actor=(std::uint64_t(viewOwner.weak)<<32)|viewOwner.soldier;
        LARGE_INTEGER now{};if(!QueryPerformanceCounter(&now))return false;
        const PblCameraAimProof aim{{boatAimSeat.identity.actor,boatAimSeat.identity.actorGeneration,boatAimSeat.identity.controlled,boatAimSeat.identity.entry},boatAimSpace,boatAimReady};
        const auto path=SelectVehicleCameraPath(vehicleView,vehicleCameraMode,
            {actor,viewOwner.controlled,viewOwner.entry,space,HandNanos(now.QuadPart),tracking.focused,tracking.headValid},aim);
        if(path==VehicleCameraPath::Unsupported){++vehicleCameraUnsupportedFrames;return true;}
        // A known seat must wait for current input instead of composing its
        // camera with the old on-foot reference for a single bad frame.
        if(path==VehicleCameraPath::Wait){++vehicleCameraWaits;return false;}
        std::optional<BoatAimSnapshot> renderAim;
        if(path==VehicleCameraPath::PblHeadAim){
            const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
            renderAim=ReadPblDriverAim(memory,boatAimBinding,boatAimSeat);if(!renderAim)return false;
        }
        auto changed=copies;
        for(auto& copy:changed){engine::FrostbiteCameraInput source{};
            std::memcpy(&source.transform,copy.bytes.data()+0x50,64);std::memcpy(&source.nearPlane,copy.bytes.data()+0x1c,4);std::memcpy(&source.farPlane,copy.bytes.data()+0x20,4);source.worldUnitsPerMeter=1;
            const auto native=engine::CanonicalCamera(source);if(!native)return false;
            std::optional<math::Matrix4> seatBase;
            if(renderAim){
                auto stable=interaction::Multiply(renderAim->neutralCameraLocal,renderAim->hull);stable.values[3]=native->camera.values[3];
                // One exact reference drives desired aim and both eyes.
                seatBase=interaction::VehicleAimViewBase(stable,boatAimReference,tracking.referenceHead,tracking.worldUnitsPerMeter);
            }else seatBase=vehicleCameraAnchor.Base(vehicleView.owner,native->camera,tracking.referenceHead,tracking.head,space,tracking.worldUnitsPerMeter);
            if(!seatBase)return false;
            const auto built=BuildTransformCopy(copy,*seatBase);if(!built)return false;copy=*built;
        }
        // The same anchored prototypes feed BuildTrackedViews' two eyes and
        // culling. Original XR poses and current native boat motion survive.
        if(renderAim){QueryPerformanceCounter(&now);if(HandNanos(now.QuadPart)>=vehicleView.deadlineNs){++vehicleCameraWaits;return false;}}
        copies=changed;++vehicleCameraViews;if(renderAim)++boatAimCameraViews;return true;
    }
    if(!motionAim)return true;
    unsigned soldier=0,weak=0;std::uint64_t sourceSpace=0;float yaw=0,consumedX=0,consumedZ=0,units=1;bool captured=false;
    for(unsigned n=0;n<3;++n){const auto seq=anchor.sequence.load(std::memory_order_acquire);if(seq&1)continue;
        soldier=anchor.soldier.load();weak=anchor.weak.load();sourceSpace=anchor.space.load();yaw=anchor.yaw.load();consumedX=anchor.consumedX.load();consumedZ=anchor.consumedZ.load();units=anchor.units.load();
        if(seq==anchor.sequence.load(std::memory_order_acquire)){captured=true;break;}}
    if(!captured)return false;if(!soldier)return true;
    Owner owner{};if(!Resolve(owner)||!owner.foot||owner.soldier!=soldier||owner.weak!=weak)return true;
    if(sourceSpace!=space)return false;
    BaseRecord record{};record.weapon=owner.weapon;record.ms=GetTickCount64();record.space=space;Read(owner.aim+12,&record.nativeYaw,4);Read(owner.aim+16,&record.nativePitch,4);record.bodyYaw=yaw;
    bool primary=true;for(auto& copy:copies){engine::FrostbiteCameraInput input{};
        std::memcpy(&input.transform,copy.bytes.data()+0x50,64);std::memcpy(&input.nearPlane,copy.bytes.data()+0x1c,4);std::memcpy(&input.farPlane,copy.bytes.data()+0x20,4);input.worldUnitsPerMeter=1;
        const auto canonical=engine::CanonicalCamera(input);if(!canonical)return false;
        // BC2 aim heading and the native RenderView forward differ by pi.
        // Live source yaw = native yaw - pi (independently sampled Sept 30).
        auto base=interaction::MakeComfortCamera(canonical->camera,(yaw-3.141592653589793f)*57.29577951308232f);if(!base)return false;
        if(primary){record.sourceYaw=std::atan2(canonical->camera.values[2][0],canonical->camera.values[2][2]);record.viewYaw=std::atan2(base->values[2][0],base->values[2][2]);primary=false;}
        const auto sharedEye=handPoses?rigPublication::ReadEyeBase(owner.soldier,owner.weak,space):std::nullopt;
        if(sharedEye)base=*sharedEye;
        record.sharedEye=bool(sharedEye);record.sourcePosition={canonical->camera.values[3][0],canonical->camera.values[3][1],canonical->camera.values[3][2]};record.eyePosition={base->values[3][0],base->values[3][1],base->values[3][2]};
        if(bodyFollow&&!sharedEye){
            // Counter only observed roomscale body displacement. Joystick travel
            // stays in the native camera; HMD translation is added downstream.
            for(unsigned axis=0;axis<3;++axis)base->values[3][axis]-=units*(consumedX*base->values[0][axis]-consumedZ*base->values[2][axis]);
        }
        const auto changed=BuildTransformCopy(copy,*base);if(!changed)return false;copy=*changed;
    }
    const auto now=GetTickCount64();
    if(now-baseRecordTime>=ViewBasisEvidence<BaseRecord>::MinimumIntervalMs){
        record.player=owner.player;record.soldier=owner.soldier;record.weak=owner.weak;record.controlled=owner.controlled;
        record.inputMaskRead=Read(owner.cache+0x98,&record.inputMask,sizeof(record.inputMask));
        std::lock_guard lock(baseRecordMutex);baseRecords.Observe(record,now);baseRecordTime=now;
    }
    ++baseViews;return true;
}
bool ObserveVehicleCameraPlan(unsigned frame,const runtime::TrackingFrame& tracking,const RenderViewCopy& native,const RenderViewCopy& adjusted,const std::array<RenderViewCopy,2>& planned)noexcept {
    std::lock_guard lock(vehicleViewMutex);
    if(!vehicleView.live||vehicleView.space!=tracking.spaceGeneration||!frame||vehicleCameraRecordCount>=vehicleCameraRecords.size())return false;
    const auto now=GetTickCount64();
    if(vehicleCameraRecordCount){const auto& previous=vehicleCameraRecords[vehicleCameraRecordCount-1];
        const bool changed=previous.owner!=vehicleView.owner||previous.tracking.spaceGeneration!=tracking.spaceGeneration||std::memcmp(&previous.tracking.head,&tracking.head,sizeof(tracking.head));
        if(!changed&&now-previous.ms<500)return false;
    }
    VehicleCameraRecord record;record.owner=vehicleView.owner;record.frame=frame;record.ms=now;record.tracking=tracking;
    if(!CameraEvidenceMatrix(native,record.native)||!CameraEvidenceMatrix(adjusted,record.adjusted)||!CameraEvidenceMatrix(planned[0],record.planned[0])||!CameraEvidenceMatrix(planned[1],record.planned[1]))return false;
    vehicleCameraRecords[vehicleCameraRecordCount++]=record;return true;
}
void ObserveVehicleCameraApplied(unsigned frame,unsigned eye,const RenderViewCopy& actual)noexcept {
    if(eye>1)return;std::lock_guard lock(vehicleViewMutex);
    if(!vehicleCameraRecordCount)return;auto& record=vehicleCameraRecords[vehicleCameraRecordCount-1];
    if(record.frame!=frame||record.appliedMask&(1u<<eye))return;
    if(CameraEvidenceMatrix(actual,record.applied[eye]))record.appliedMask|=1u<<eye;
}
void ReportBoatAimEvidence(std::ostream& out){
    std::lock_guard lock(vehicleViewMutex);out<<",\"boat_head_aim\":"<<(boatHeadAim?"true":"false")<<",\"boat_head_fire\":"<<(boatHeadFire?"true":"false")
        <<",\"boat_aim_samples\":"<<boatAimSamples.load()<<",\"boat_aim_rejects\":"<<boatAimRejects.load()<<",\"boat_aim_commits\":"<<boatAimCommits.load()<<",\"boat_aim_fire_commits\":"<<boatAimFireCommits.load()<<",\"boat_aim_camera_views\":"<<boatAimCameraViews.load()<<",\"boat_aim_records\":[";
    for(unsigned n=0;n<boatAimRecordCount;++n){const auto&r=boatAimRecords[n];if(n)out<<',';
        out<<"{\"ms\":"<<r.ms<<",\"sequence\":"<<r.sequence<<",\"yaw_command\":"<<r.yaw<<",\"pitch_command\":"<<r.pitch<<",\"yaw_error\":"<<r.yawError<<",\"pitch_error\":"<<r.pitchError<<",\"native_yaw\":"<<r.jointYaw<<",\"native_pitch\":"<<r.jointPitch<<",\"fire\":"<<r.fire<<'}';}
    out<<']';
}
void ReportVehicleCameraEvidence(std::ostream& out){
    std::lock_guard lock(vehicleViewMutex);const auto precision=out.precision();out.precision(9);out<<",\"vehicle_camera_evidence\":[";
    const auto matrix=[&](const math::Matrix4& m){out<<'[';for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){if(r||c)out<<',';out<<m.values[r][c];}out<<']';};
    const auto pose=[&](const math::Pose& p){out<<'['<<p.position.x<<','<<p.position.y<<','<<p.position.z<<','<<p.orientation.x<<','<<p.orientation.y<<','<<p.orientation.z<<','<<p.orientation.w<<']';};
    for(unsigned n=0;n<vehicleCameraRecordCount;++n){const auto& r=vehicleCameraRecords[n];if(n)out<<',';
        out<<"{\"native_frame\":"<<r.frame<<",\"ms\":"<<r.ms<<",\"actor\":"<<r.owner.actor<<",\"actor_generation\":"<<r.owner.generation<<",\"vehicle\":"<<r.owner.vehicle<<",\"entry\":"<<r.owner.seat<<",\"tracking_sequence\":"<<r.tracking.generation<<",\"space\":"<<r.tracking.spaceGeneration<<",\"predicted_ns\":"<<r.tracking.predictedNs<<",\"units_per_metre\":"<<r.tracking.worldUnitsPerMeter<<",\"applied_mask\":"<<r.appliedMask<<",\"reference_pose\":";pose(r.tracking.referenceHead);
        out<<",\"head_pose\":";pose(r.tracking.head);out<<",\"eye_poses\":[";pose(r.tracking.eyes[0]);out<<',';pose(r.tracking.eyes[1]);out<<"],\"native_camera\":";matrix(r.native);out<<",\"adjusted_camera\":";matrix(r.adjusted);
        out<<",\"planned_eyes\":[";matrix(r.planned[0]);out<<',';matrix(r.planned[1]);out<<"],\"applied_eyes\":[";matrix(r.applied[0]);out<<',';matrix(r.applied[1]);out<<"]}";
    }out<<']';out.precision(precision);
}
bool SetFeedbackWriter(FeedbackWriter writer)noexcept {
    if(!hooks[0]||enabled.load(std::memory_order_acquire)||writeFeedback||!writer)return false;writeFeedback=writer;return true;
}
bool EnableFireRouteTelemetry()noexcept {
    if(enabled.load(std::memory_order_acquire)||!hooks[0])return false;
    fireRouteTelemetryEnabled=true;return true;
}
void ReportFireRouteTelemetry(std::ostream& out) {
    FireRouteTelemetryBatch batch;
    if(!fireRouteTelemetry.TryDrain(batch)){out<<"{\"schema\":1,\"snapshot_available\":false}";return;}
    WriteFireRouteTelemetry(out,batch);
}
void Start()noexcept {
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    if(chestKnife)chestKnifeNative::Start();
    selectedKnife::Start();
#endif
    reloadFlowRuntime::Start();rigPublication::Start();enabled.store(true,std::memory_order_release);
}
bool PrepareSessionStop()noexcept {
#ifdef FVR_BC2_PLAYER_KNIFE_NORMAL
    if(chestKnife){
        // Stop new gameplay writes; leave native hooks/Update observer alive.
        enabled.store(false,std::memory_order_release);
        chestKnifeNative::RequestSessionClose();
        const auto until=GetTickCount64()+2000;
        do{
            const auto state=chestKnifeNative::PollSessionClose();
            if(state==chestKnifeNative::SessionCloseStatus::Complete)return true;
            if(state==chestKnifeNative::SessionCloseStatus::Unresolved)return false;
            Sleep(5);
        }while(GetTickCount64()<until);
        return false; // No timeout acknowledgement; all dependencies stay alive.
    }
#endif
    return true;
}
bool Stop()noexcept {
    if(!PrepareSessionStop())return false;
    enabled.store(false,std::memory_order_release);
    boatReticle.Invalidate();
    rigPublication::PublishWeaponVisibility({});weaponVisibilitySample.store({},std::memory_order_release);
    bodyHolsterSample.store({},std::memory_order_release);
    bool okay=rigPublication::Stop();
    const bool reloadOkay=reloadFlowRuntime::Stop();okay&=reloadOkay;
#ifdef FVR_BC2_VEHICLE_MOUNTED_PROBE
    if(vehicleMountedRegistered){
        const bool removed=reloadOkay&&reloadFlowRuntime::RemoveOriginalFiringObserver(&vehicleMountedObserver);
        vehicleMountedObserver.Removal(removed);okay&=removed;
        if(removed)vehicleMountedRegistered=false;
    }
#endif
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    // Shared Update callbacks must drain before removing the observer. A live
    // tool interval cannot be reported as drained or silently handed to stock.
    if(chestKnife)okay=chestKnifeNative::Stop()&&okay;
#endif
    for(auto entry:hooks)if(entry){const auto r=MH_DisableHook(entry);okay&=r==MH_OK||r==MH_ERROR_DISABLED;}
    const auto until=GetTickCount64()+2000;while(active.load()&&GetTickCount64()<until)Sleep(1);
    // This candidate observer owns no game state. Remove its trampoline only
    // after every gameplay callback has left; a failed drain preserves it.
    if(okay&&!active.load()&&hooks[13]){
        const auto removed=MH_RemoveHook(hooks[13]);okay&=removed==MH_OK;
        if(removed==MH_OK){hooks[13]=nullptr;projectileLaunchHookRemoved=true;}
    }
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    // Gather may still publish/reject its same input transaction during drain.
    // Keep the selected-melee adapter alive until all those callers have left.
    if(!active.load())okay=selectedKnife::Stop()&&okay;
#endif
    if(!active.load()&&magazineReloadFixture){LARGE_INTEGER now{};QueryPerformanceCounter(&now);magazineReloadFixture->Stop(HandNanos(now.QuadPart));}
    if(!active.load()&&reloadRequestFixture){LARGE_INTEGER now{};QueryPerformanceCounter(&now);reloadRequestFixture->Stop(HandNanos(now.QuadPart));}
    if(!active.load()&&twoHandGrip){
        CancelSight();ResetSupportGrip("session_stop");ResetHandOwnership("session_stop");
        gatherSightRequest=gatherSightAck=0;gatherSightCommit=false;RecordHandOwnership();
    }
    selectedMeshes.Clear();if(bodyInventory&&!active.load())bodyInventory->Cancel();
    if(!active.load()&&weaponVisibilityProbe){LARGE_INTEGER now{};QueryPerformanceCounter(&now);weaponVisibilityProbe->Cancel(9,HandNanos(now.QuadPart));
        rigPublication::PublishWeaponVisibility({});weaponVisibilitySample.store({},std::memory_order_release);}
    return okay&&!active.load();
}
void Report(std::ostream& out){out<<"{\"installed\":"<<(hooks[0]&&hooks[1]?"true":"false")
    <<",\"native_equipment_epoch\":"<<epoch<<",\"native_input_epoch\":"<<inputEpoch<<",\"native_input_pauses\":"<<nativeInputPauses
    <<",\"thread\":"<<inputThread.load()<<",\"updates\":"<<updates.load()<<",\"local_updates\":"<<localUpdates.load()
    <<",\"gathers\":"<<gathers.load()<<",\"on_foot\":"<<onFoot.load()<<",\"vehicle_gated\":"<<vehicle.load()
    <<",\"stale_or_missing\":"<<stale.load()<<",\"applied\":"<<applied.load()<<",\"committed\":"<<committed.load()
    <<",\"movement_samples\":"<<moving.load()<<",\"button_samples\":"<<buttons.load()
    <<",\"death_probe_requested\":"<<(deathProbe?"true":"false")<<",\"grenade_samples\":"<<grenadeSamples.load()
    <<",\"left_only_action_samples\":"<<leftOnlyActions.load()<<",\"right_only_action_samples\":"<<rightOnlyActions.load()
    <<",\"fire_with_left_tracking_missing\":"<<leftLossFire.load()<<",\"untracked_fire_samples\":"<<untrackedFire.load()<<",\"recovery_fire_suppressed\":"<<recoveryFireSuppressed.load()
    <<",\"equip_probe_requested\":"<<(equipProbe?"true":"false")<<",\"equip_commands\":"<<equipCommands.load()
    <<",\"weapon_mode_binding_verified\":"<<(weaponModeBindingVerified?"true":"false")
    <<",\"weapon_mode_requests\":"<<modeRequests.load()<<",\"weapon_mode_rejections\":"<<modeRejections.load()<<",\"weapon_mode_acknowledgements\":"<<modeAcknowledgements.load()
    <<",\"boat_driver_commits\":"<<vehicleDriveCommits.load()<<",\"boat_camera_views\":"<<vehicleCameraViews.load()
    <<",\"boat_camera_stale_waits\":"<<vehicleCameraWaits.load()<<",\"boat_camera_unsupported_frames\":"<<vehicleCameraUnsupportedFrames.load()
    <<",\"vehicle_route_rejects\":"<<vehicleRouteRejects.load()<<",\"vehicle_unsupported_samples\":"<<vehicleUnsupported.load()
    <<",\"vehicle_entity\":"<<vehicleSeatEntity.load()<<",\"vehicle_entry\":"<<vehicleSeatEntry.load()<<",\"vehicle_route_fingerprint\":"<<vehicleRouteFingerprint.load()
    <<",\"vehicle_last_throttle\":"<<vehicleThrottle.load()<<",\"vehicle_last_steer\":"<<vehicleSteer.load()
    <<",\"vehicle_throttle_nonzero_commits\":"<<vehicleThrottleNonzero.load()<<",\"vehicle_steer_nonzero_commits\":"<<vehicleSteerNonzero.load()
    <<",\"vehicle_throttle_max_abs\":"<<vehicleThrottleMaximum.load()<<",\"vehicle_steer_max_abs\":"<<vehicleSteerMaximum.load()<<",\"vehicle_cache_readback_failures\":"<<vehicleReadbackFailures.load()
    <<",\"vehicle_driver_native_look\":false,\"vehicle_driver_native_fire\":false,\"vehicle_gather_evidence\":[";
    for(unsigned i=0;i<vehicleGatherCount.load(std::memory_order_acquire);++i){const auto& row=vehicleGatherRecords[i];if(i)out<<',';
        out<<"{\"exit_held\":"<<(row.exitHeld?"true":"false")<<",\"flags_written\":"<<row.flagsWritten<<",\"flags_observed\":"<<row.flagsObserved<<",\"flags_verified\":"<<(row.flagsVerified?"true":"false");
        out<<",\"entity\":"<<row.entity<<",\"entry\":"<<row.entry<<",\"soldier\":"<<row.soldier<<",\"weak\":"<<row.weak<<",\"actor_generation\":"<<row.actorGeneration<<",\"seat_generation\":"<<row.seatGeneration<<",\"space\":"<<row.space<<",\"input_sequence\":"<<row.inputSequence<<",\"ms\":"<<row.ms<<",\"axis_mask\":"<<row.mask<<",\"permissions\":"<<row.permissions<<",\"before\":["<<row.before[0]<<','<<row.before[1]<<"],\"written\":["<<row.written[0]<<','<<row.written[1]<<"],\"observed\":["<<row.observed[0]<<','<<row.observed[1]<<"],\"readback_verified\":"<<(row.verified?"true":"false");
        out<<",\"role\":"<<row.role<<",\"consumer_driver\":"<<row.consumerDriver<<",\"consumer_camera\":"<<row.consumerCamera<<",\"consumer_weapons\":"<<row.consumerWeapons;
        const auto floats=[&](const char* name,const auto& values){out<<",\""<<name<<"\":[";for(unsigned a=0;a<VehicleAxisCount;++a){if(a)out<<',';out<<values[a];}out<<']';};
        floats("axis_actions",row.actions);floats("axes_before",row.before);floats("axes_written",row.written);floats("axes_observed",row.observed);out<<'}';
    }
    out<<']';ReportVehicleCameraEvidence(out);ReportBoatAimEvidence(out);
    out<<",\"vehicle_shot_observation\":";vehicleShotObserver.Report(out,vehicleShotBinding.verified);
    {const auto reticle=boatReticle.Statistics();out<<",\"boat_reticle\":{\"source_binding\":"<<boatReticleBinding.verified
        <<",\"observations\":"<<reticle.observations<<",\"shot_witnesses\":"<<reticle.witnesses<<",\"rejected_sources\":"<<reticle.rejected<<",\"witness_retained\":"<<reticle.ready
        <<",\"source_stage\":"<<reticle.source.stage<<",\"additional_transform\":"<<reticle.source.additionalTransform<<",\"adjustment_flag\":"<<reticle.source.adjustment
        <<",\"camera_owner\":"<<reticle.source.cameraOwner<<",\"camera_flags\":"<<reticle.source.cameraFlags<<",\"projectile_count\":"<<reticle.source.count<<",\"inherit_speed\":"<<reticle.source.inheritSpeed<<'}';}
#ifdef FVR_BC2_VEHICLE_MOUNTED_PROBE
    out<<",\"vehicle_mounted_update_observation\":";vehicleMountedObserver.Report(out);
#endif
#ifdef FVR_BC2_ORDINARY_RESOURCE_PAUSE_PROBE
    out<<",\"native_input_pause_probe\":{\"ran\":"<<(nativeInputPauseProbe.ran?"true":"false")
       <<",\"same_owner\":"<<(nativeInputPauseProbe.sameOwner?"true":"false")
       <<",\"begin_ms\":"<<nativeInputPauseProbe.begin<<",\"end_ms\":"<<nativeInputPauseProbe.end
       <<",\"equipment_before\":"<<nativeInputPauseProbe.before.equipment<<",\"equipment_after\":"<<nativeInputPauseProbe.after.equipment
       <<",\"input_before\":"<<nativeInputPauseProbe.before.input<<",\"input_after\":"<<nativeInputPauseProbe.after.input
       <<",\"ammunition_written\":false,\"controller_input_written\":false}";
#endif
    out
    <<",\"vehicle_interact_samples\":"<<vehicleInteractions.load()<<",\"next_weapon_commands\":"<<nextWeaponCommands.load()<<",\"previous_weapon_commands\":"<<previousWeaponCommands.load()
    <<",\"native_input_inactive\":"<<nativeInactive.load()<<",\"mismatches\":"<<mismatches.load()
    <<",\"motion_aim_requested\":"<<(motionAim?"true":"false")<<",\"motion_aim_samples\":"<<aimSamples.load()<<",\"independent_view_bases\":"<<baseViews.load()
    <<",\"snap_turns\":"<<turns.load()<<",\"unbound_turn_requests\":"<<turnUnavailable.load()<<",\"independent_weapon_aim\":false,\"body_collision_roomscale\":false";
    out<<",\"expack_reload_route\":{\"request_samples\":"<<expackReloadRequests.load()
       <<",\"zoom_routes\":"<<expackReloadZoomRoutes.load()<<",\"unresolved_routes\":"<<expackReloadUnresolved.load()
       <<",\"native_throw_acknowledged\":false,\"snapshot_status_order\":[\"Observed\",\"InvalidArguments\",\"ReadFailure\",\"OwnerMismatch\",\"MalformedConfiguration\",\"MalformedState\",\"ChangedDuringRead\"],\"snapshot_status_counts\":[";
    for(unsigned i=0;i<expackReloadReads.size();++i){if(i)out<<',';out<<expackReloadReads[i].load();}
    out<<"]}";
    const auto bases=[] {std::lock_guard lock(baseRecordMutex);return baseRecords;}();
    out<<",\"view_basis_retention\":{\"policy\":\"bounded_decimation_with_latest\",\"capacity\":"<<bases.MaxRecords
       <<",\"minimum_interval_ms\":"<<bases.MinimumIntervalMs<<",\"eligible_samples\":"<<bases.Samples()
       <<",\"stride_samples\":"<<bases.Stride()<<",\"compactions\":"<<bases.Compactions()<<",\"covers_all_frames\":false},\"view_basis_evidence\":[";
    for(std::size_t i=0;i<bases.Size();++i){const auto& r=bases.At(i);if(i)out<<',';
        out<<"{\"weapon\":"<<r.weapon<<",\"ms\":"<<r.ms<<",\"space\":"<<r.space<<",\"native_yaw\":"<<r.nativeYaw<<",\"native_pitch\":"<<r.nativePitch<<",\"source_yaw\":"<<r.sourceYaw<<",\"view_yaw\":"<<r.viewYaw<<",\"body_yaw\":"<<r.bodyYaw<<",\"shared_eye\":"<<(r.sharedEye?"true":"false")<<",\"source_position\":["<<r.sourcePosition.x<<','<<r.sourcePosition.y<<','<<r.sourcePosition.z<<"],\"eye_position\":["<<r.eyePosition.x<<','<<r.eyePosition.y<<','<<r.eyePosition.z<<"],\"player\":"<<r.player<<",\"soldier\":"<<r.soldier<<",\"weak\":"<<r.weak<<",\"controlled\":"<<r.controlled<<",\"input_mask_read\":"<<(r.inputMaskRead?"true":"false")<<",\"input_mask\":"<<r.inputMask<<'}';}

    out<<"],\"weapon_mode_records\":[";
    for(unsigned n=0;n<modeRecordCount;++n){const auto& r=modeRecords[(modeRecordNext+modeRecords.size()-modeRecordCount+n)%modeRecords.size()];if(n)out<<',';
        out<<"{\"ms\":"<<r.ms<<",\"ack_ms\":"<<r.ackMs<<",\"actor\":"<<r.actor<<",\"weak\":"<<r.weak<<",\"owner\":"<<r.owner<<",\"space\":"<<r.space<<",\"gesture\":"<<r.gesture<<",\"from\":"<<r.from<<",\"target\":"<<r.target<<",\"action\":"<<r.action<<",\"cancelled\":"<<(r.cancelled?"true":"false")<<'}';
    }
    out<<"],\"hand_ownership\":{\"schema\":1,\"event_capacity\":"<<handEvents.size()
       <<",\"checks\":"<<handChecks<<",\"overlap_failures\":"<<handOverlap
       <<",\"support_without_claim\":"<<handMissingSupport<<",\"sight_without_claim\":"<<handMissingSight
       <<",\"lease_failures\":"<<handLeaseFailures<<",\"claim_rejections\":"<<handClaimRejected
       <<",\"events_total\":"<<handEventsTotal<<",\"events_dropped\":"<<handEventsDropped<<",\"events\":[";
    const auto claimJson=[&](const std::optional<interaction::HandClaim>& claim){
        if(!claim){out<<"null";return;}const auto& t=claim->token;
        out<<"{\"id\":"<<t.id<<",\"kind\":"<<unsigned(t.kind)<<",\"item\":"<<t.item.id<<",\"generation\":"<<t.item.generation
           <<",\"contact\":"<<t.contact.id<<",\"contact_generation\":"<<t.contact.generation<<",\"prerequisite\":"<<t.prerequisiteClaim
           <<",\"input_generation\":"<<claim->inputSequence<<",\"deadline_ns\":"<<claim->deadlineNs<<'}';
    };
    for(unsigned n=0;n<handEventCount;++n){
        if(n)out<<',';const auto& r=handEvents[n];
        out<<"{\"serial\":"<<r.serial<<",\"now_ns\":"<<r.nowNs<<",\"raw_generation\":"<<r.rawGeneration
           <<",\"actor\":"<<unsigned(r.owner.actor)<<",\"weak\":"<<unsigned(r.owner.actor>>32)
           <<",\"rig_epoch\":"<<r.owner.actorGeneration<<",\"equip_generation\":"<<r.owner.equipGeneration<<",\"space\":"<<r.owner.space
           <<",\"weapon\":"<<r.weapon<<",\"physical_item\":"<<r.item.id<<",\"reason\":\""<<r.reason<<"\",\"left\":";claimJson(r.left);
        out<<",\"right\":";claimJson(r.right);
        out<<",\"support_holding\":"<<(r.supportHolding?"true":"false")<<",\"support_token\":"<<r.supportToken
           <<",\"sight_phase\":"<<r.sightPhase<<",\"sight_request\":"<<r.sightRequest<<",\"sight_ack\":"<<r.sightAck
           <<",\"sight_commit\":"<<(r.sightCommit?"true":"false")<<'}';
    }
    out<<"],\"claim_drops_omitted\":"<<handDropJournal.omitted<<",\"claim_drops\":[";
    for(std::size_t n=0;n<handDropJournal.count;++n){
        if(n)out<<',';const auto& d=handDropJournal.entries[n];const auto& s=d.attempted;
        out<<"{\"reason\":"<<unsigned(d.reason)<<",\"claim\":";claimJson(d.claim);
        out<<",\"input\":"<<s.sequence<<",\"observed_ns\":"<<s.observedNs<<",\"deadline_ns\":"<<s.deadlineNs
           <<",\"now_ns\":"<<s.nowNs<<",\"previous_now_ns\":"<<d.previousNowNs
           <<",\"focused\":"<<(s.focused?"true":"false")
           <<",\"tracked\":["<<(s.tracked[0]?"true":"false")<<','<<(s.tracked[1]?"true":"false")
           <<"],\"released\":["<<(s.released[0]?"true":"false")<<','<<(s.released[1]?"true":"false")<<"]}";
    }
    out<<"]}";
    out<<",\"selected_meshes_observer\":";selectedMeshes.Report(out);
    out<<",\"reload_flow\":";reloadFlowRuntime::Report(out);
    if(bodyHolsterFixture){out<<",\"body_holster_probe\":";bodyHolsterFixture->Report(out);}
    if(weaponVisibilityProbe){out<<",\"weapon_visibility_probe\":";weaponVisibilityProbe->Report(out);
        out<<",\"weapon_visibility_action_suppression\":{\"verified_commits\":"<<weaponVisibilityActionCommits.load()
           <<",\"failures\":"<<weaponVisibilityActionFailures.load()<<'}';}
    if(m95StockShotFixture){out<<",\"m95_stock_shot_probe\":";m95StockShotFixture->Report(out);}
    if(emptyFireFixture){out<<",\"empty_fire_probe\":";emptyFireFixture->Report(out);}
#ifdef FVR_BC2_ARMING_EMPTY_PROBE
    if(armingEmptyFixture){out<<",\"arming_empty_probe\":";armingEmptyFixture->Report(out);}
#endif
    if(physicalReloadFixture){out<<",\"physical_reload_probe\":";physicalReloadFixture->Report(out);}
    if(physicalReload){out<<",\"physical_reload\":";physicalReload->Report(out);}
    if(physicalFeed){out<<",\"physical_feed\":";physicalFeed->Report(out);}
    if(physicalPistolSlide){out<<",\"physical_pistol_slide\":";physicalPistolSlide->Report(out);}
    if(physicalPump){out<<",\"physical_pump\":";physicalPump->Report(out);
        out<<",\"pump_aim_continuation\":{\"captures\":"<<pumpAimCaptures.load()<<",\"applications\":"<<pumpAimApplications.load()<<",\"rejected\":"<<pumpAimRejected.load()<<'}';}
    if(physicalPumpFixture){out<<",\"physical_pump_probe\":";physicalPumpFixture->Report(out);}
    if(physicalBoltFixture){out<<",\"physical_bolt_probe\":";physicalBoltFixture->Report(out);}
    if(ordinaryManualCycles){
        out<<",\"ordinary_cycle_selection\":{\"read_gaps\":"<<ordinarySelectionReadGaps
           <<",\"retained_gaps\":"<<ordinarySelectionRetainedGaps<<",\"custody_gaps\":"<<ordinarySelectionCustodyGaps
           <<",\"first_custody_gap_input\":"<<ordinarySelectionFirstCustodyGap
           <<",\"bolt_preparation_skips\":[";
        for(unsigned n=0;n<ordinaryBoltPreparationSkips.size();++n){if(n)out<<',';out<<ordinaryBoltPreparationSkips[n];}
        out<<"],\"bolt_preparation_first_owned_skip_inputs\":[";
        for(unsigned n=0;n<ordinaryBoltPreparationFirstOwnedSkip.size();++n){if(n)out<<',';out<<ordinaryBoltPreparationFirstOwnedSkip[n];}
        out<<"]}";
    }
    if(ordinaryBoltInputFixture){out<<",\"ordinary_bolt_input_probe\":";ordinaryBoltInputFixture->Report(out);}
    out<<",\"context_interact\":";contextInteractEvidence.Report(out);
    if(magazinePhysical){out<<',';magazinePhysical->Report(out);}
    if(magazinePhysicalFixture){out<<",\"magazine_physical_probe\":";magazinePhysicalFixture->Report(out);}
    if(magazineResourceFixture){out<<",\"magazine_resource_probe\":";magazineResourceFixture->Report(out);}
    if(inventoryReloadFixture){out<<",\"inventory_reload_probe\":";inventoryReloadFixture->Report(out);}
    if(resourceInventoryFixture){out<<",\"resource_inventory_probe\":";resourceInventoryFixture->Report(out);}
    if(magazineDetached){out<<',';magazineDetached->Report(out);}
    if(magazineDetached)out<<",\"magazine_recovery_suppression\":{\"commits\":"<<magazineRecoverySuppressionCommits<<",\"failures\":"<<magazineRecoverySuppressionFailures<<'}';
    if(magazineDetachedFixture){out<<",\"magazine_detached_probe\":";magazineDetachedFixture->Report(out);}
    out<<",\"reload_feedback\":{\"writer_installed\":"<<(writeFeedback?"true":"false")<<",\"sent\":"<<feedbackSent<<",\"dropped\":"<<feedbackDropped<<'}';
#if defined(FVR_BC2_ORDINARY_GRENADE_PROBE) || defined(FVR_BC2_PLAYER_GRENADE)
    if(ordinaryGrenade){out<<",\"ordinary_grenade_probe\":";ordinaryGrenade->Report(out);}
    if(playerGrenade){out<<",\"player_grenade\":";playerGrenade->Report(out);}
#ifdef FVR_BC2_TWO_GRENADE_DEATH_PROBE
    out<<",\"two_grenade_death_guard\":{\"enabled\":"<<(twoGrenadeDeathEnabled?"true":"false")<<",\"lifetime_pinned\":"<<(twoGrenadeDeathLifetime.Pinned()?"true":"false")<<",\"lifetime_lost\":"<<(twoGrenadeDeathLifetime.Lost()?"true":"false")<<",\"maximum_native_edges\":2,\"owner\":[";
    for(unsigned i=0;i<twoGrenadeDeathLifetime.Pin().size();++i){if(i)out<<',';out<<twoGrenadeDeathLifetime.Pin()[i];}out<<"]}";
#endif
#endif
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
    if(contextualTool){out<<",\"contextual_tool\":";contextualTool->Report(out);}
#endif
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    if(chestKnife){out<<",\"chest_knife_draw_return\":";chestKnife->Report(out);
        out<<",\"chest_knife_native\":";chestKnifeNative::Report(out);}
    out<<",\"selected_knife_native\":";selectedKnife::Report(out);
#endif
    if(bodyInventory)bodyInventory->Report(out);
    if(bodyHolster){out<<",\"body_holster_transitions\":";bodyHolster->Report(out);out<<",\"body_holster_lifecycle\":";bodyHolsterLifecycle.Report(out);}
    out<<",\"body_holster\":{\"enabled\":"<<(bodyHolster?"true":"false")<<",\"suppression_commits\":"<<holsterSuppressionCommits
       <<",\"suppression_failures\":"<<holsterSuppressionFailures<<",\"free_publications\":"<<holsterFreePublications
       <<",\"diagnostic_profile\":"<<(bodyHolster?unsigned(bodyHolster->DiagnosticProfile()):0)<<",\"diagnostic_start_ns\":"<<(bodyHolster?bodyHolster->DiagnosticStart():0)<<",\"diagnostic_deadline_ns\":"<<(bodyHolster?bodyHolster->DiagnosticDeadline():0)
       <<",\"diagnostic_grants_production_acceptance\":false}";
    if(magazineReloadFixture){out<<",\"magazine_reload_probe\":";magazineReloadFixture->Report(out);}
    if(reloadRequestFixture){out<<",\"reload_request_probe\":";reloadRequestFixture->Report(out);}
    out<<",\"reload_observer\":{\"binding_verified\":"<<(reloadStateBinding?"true":"false")
       <<",\"read_only\":true,\"manual_dispatch\":false,\"manual_cycle_gate\":false,\"chamber_known\":false,\"authority_known\":false,\"atomic_snapshot\":false,\"status_counts\":[";
    for(unsigned n=0;n<reloadStatuses.size();++n){if(n)out<<',';out<<reloadStatuses[n];}out<<"],\"publication_statuses\":[";
    for(unsigned n=0;n<reloadPublicationStatuses.size();++n){if(n)out<<',';out<<reloadPublicationStatuses[n];}out<<"],\"publication_reasons\":[";
    for(unsigned n=0;n<reloadPublicationReasons.size();++n){if(n)out<<',';out<<reloadPublicationReasons[n];}out<<"],\"samples\":[";
    for(unsigned n=0;n<reloadSnapshotCount;++n){const auto& r=reloadSnapshots[n];if(n)out<<',';
        out<<"{\"sequence\":"<<r.sequence<<",\"observed_ns\":"<<r.observedNs<<",\"actor\":"<<r.owner.soldier<<",\"weapon\":"<<r.owner.weapon
           <<",\"actor_generation\":"<<r.owner.actorGeneration<<",\"equip_generation\":"<<r.owner.equipGeneration<<",\"space\":"<<r.owner.space
           <<",\"eligibility_branch\":"<<unsigned(r.eligibilityBranch)<<",\"fire_logic\":"<<r.config.fireLogicType<<",\"reload_type\":"<<r.config.reloadType<<",\"base_capacity\":"<<r.config.baseCapacity
           <<",\"branches_agree_ammo\":"<<(r.branchesAgreeOnAmmo?"true":"false")<<",\"branches\":[";
        for(unsigned b=0;b<2;++b){const auto& f=r.branches[b];if(b)out<<',';
            out<<"{\"address\":"<<f.address<<",\"current\":"<<f.currentState<<",\"previous\":"<<f.previousState<<",\"next\":"<<f.nextState<<",\"phase\":"<<unsigned(f.phase)
               <<",\"loaded\":"<<f.loaded<<",\"reserve\":"<<f.reserve<<",\"effective_capacity\":";if(f.effectiveCapacity)out<<*f.effectiveCapacity;else out<<"null";
            out<<",\"reload_eligibility\":"<<unsigned(f.reloadAmmo)<<'}';
        }out<<"]}";
    }out<<"]}";
    out<<",\"sight_flip\":{\"requested\":"<<(sightFlip?"true":"false")<<",\"requests\":"<<sightRequests.load()<<",\"commits\":"<<sightCommits.load()<<",\"cancellations\":"<<sightCancellations.load()<<",\"headset_verified\":false,\"records_total\":"<<sightRecordsTotal<<",\"events_total\":"<<sightEventsTotal<<",\"records\":[";
    const auto writeSight=[&](const SightRecord& r){
        out<<"{\"ms\":"<<r.ms<<",\"generation\":"<<r.generation<<",\"contact_generation\":"<<r.contactGeneration<<",\"coherent_generation\":"<<r.coherentGeneration<<",\"weapon\":"<<r.weapon<<",\"request\":"<<r.request<<",\"phase\":"<<r.phase<<",\"cancel_reason\":"<<r.cancel<<",\"packet_reason\":"<<r.packetReason
           <<",\"distance_m\":"<<r.distance<<",\"support_distance_m\":"<<r.supportDistance<<",\"signed_radians\":"<<r.angle<<",\"squeeze\":"<<r.squeeze<<",\"paired_squeeze\":"<<r.pairedSqueeze
           <<",\"contact\":"<<(r.contact?"true":"false")<<",\"preferred\":"<<(r.preferred?"true":"false")<<",\"tracked\":"<<(r.tracked?"true":"false")<<",\"family_valid\":"<<(r.familyValid?"true":"false")<<",\"paired\":"<<(r.paired?"true":"false")<<",\"advanced\":"<<(r.advanced?"true":"false")
           <<",\"grabbed\":"<<(r.grabbed?"true":"false")<<",\"committed\":"<<(r.committed?"true":"false")<<'}';
    };
    for(unsigned n=0;n<sightRecordCount;++n){if(n)out<<',';writeSight(sightRecords[(sightRecordNext+sightRecords.size()-sightRecordCount+n)%sightRecords.size()]);}
    out<<"],\"events\":[";
    for(unsigned n=0;n<sightEventCount;++n){if(n)out<<',';writeSight(sightEvents[(sightEventNext+sightEvents.size()-sightEventCount+n)%sightEvents.size()]);}
    out<<"],\"gestures_total\":"<<sightGesturesTotal<<",\"gestures\":[";
    const auto sightVector=[&](math::Vec3 p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};
    for(unsigned n=0;n<sightGestureCount;++n){
        if(n)out<<',';const auto& r=sightGestures[(sightGestureNext+sightGestures.size()-sightGestureCount+n)%sightGestures.size()];
        out<<"{\"id\":"<<r.id<<",\"start_ms\":"<<r.startMs<<",\"end_ms\":"<<r.endMs
           <<",\"start_generation\":"<<r.startGeneration<<",\"end_generation\":"<<r.endGeneration<<",\"weapon\":"<<r.weapon
           <<",\"start_mode\":"<<r.startMode<<",\"request\":"<<r.request<<",\"cancel_reason\":"<<r.reason
           <<",\"fresh_samples\":"<<r.freshSamples<<",\"min_signed_radians\":"<<r.minAngle<<",\"max_signed_radians\":"<<r.maxAngle
           <<",\"peak_progress_radians\":"<<r.peakProgress<<",\"longest_detent_ns\":"<<r.longestDetentNs
           <<",\"max_contact_distance_m\":"<<r.maxContactDistance<<",\"committed\":"<<(r.committed?"true":"false")
           <<",\"finished\":"<<(r.finished?"true":"false")<<",\"configured_pivot_m\":";sightVector(r.pivot);
        out<<",\"configured_axis\":";sightVector(r.axis);out<<",\"native_pivot_at_grab_m\":";sightVector(r.nativePivot);
        out<<",\"start_hand_local_m\":";sightVector(r.startHand);out<<",\"end_hand_local_m\":";sightVector(r.endHand);out<<'}';
    }
    out<<"]}";
    const auto writeSupportRecord=[&](const SupportRecord& r){
        const auto q=[&](math::Quaternion p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<','<<p.w<<']';};
        out<<"{\"ms\":"<<r.ms<<",\"generation\":"<<r.generation<<",\"weapon_kind\":"<<r.weaponKind<<",\"release_reason\":"<<r.reason<<",\"action_bits\":"<<r.actions<<",\"grasp_token\":"<<r.token<<",\"contact\":"<<(r.contact?"true":"false")<<",\"distance_m\":"<<r.distance<<",\"squeeze\":"<<r.squeeze<<",\"holding\":"<<(r.holding?"true":"false")<<",\"engaged\":"<<(r.engaged?"true":"false")<<",\"released\":"<<(r.released?"true":"false")<<",\"correction_radians\":"<<r.angle<<",\"raw_aim\":";q(r.rawAim);out<<",\"aim\":";q(r.aim);out<<'}';
    };
    out<<",\"two_hand_support\":{\"requested\":"<<(twoHandGrip?"true":"false")<<",\"held_samples\":"<<supportSamples.load()<<",\"grabs\":"<<supportGrabs.load()<<",\"releases\":"<<supportReleases.load()<<",\"preservation_failures\":"<<supportPreservationFailures.load()<<",\"fire_samples\":"<<supportFireSamples.load()<<",\"aim_samples\":"<<supportAimSamples.load()<<",\"max_aim_residual_radians\":"<<supportAimResidual<<",\"records\":[";
    for(unsigned n=0;n<supportRecordCount;++n){if(n)out<<',';writeSupportRecord(supportRecords[(supportRecordNext+supportRecords.size()-supportRecordCount+n)%supportRecords.size()]);}
    out<<"],\"event_schema\":1,\"event_capacity\":"<<supportEvents.size()<<",\"events_total\":"<<supportEventTotal<<",\"events_dropped\":"<<supportEventDropped<<",\"forced_resets\":"<<supportForcedResets<<",\"policy_release_counts\":[";
    for(unsigned n=0;n<supportReleaseCounts.size();++n){if(n)out<<',';out<<supportReleaseCounts[n];}
    out<<"],\"events\":[";
    for(unsigned n=0;n<supportEventCount;++n){
        if(n)out<<',';const auto& e=supportEvents[n];
        const auto point=[&](math::Vec3 v){out<<'['<<v.x<<','<<v.y<<','<<v.z<<']';};
        out<<"{\"event_ms\":"<<e.eventMs<<",\"previous_grasp_token\":"<<e.previousToken<<",\"actor\":"<<e.actor<<",\"weak\":"<<e.weak<<",\"weapon\":"<<e.weapon<<",\"rig_epoch\":"<<e.rigEpoch<<",\"space\":"<<e.space<<",\"cancel_flags\":"<<e.cancelFlags;
        out<<",\"focused\":"<<(e.focused?"true":"false")<<",\"head_tracked\":"<<(e.headTracked?"true":"false")<<",\"left_grip_tracked\":"<<(e.leftGripTracked?"true":"false")<<",\"right_grip_tracked\":"<<(e.rightGripTracked?"true":"false")<<",\"right_aim_tracked\":"<<(e.rightAimTracked?"true":"false")<<",\"reset_reason\":";
        if(e.resetReason)out<<'"'<<e.resetReason<<'"';else out<<"null";
        out<<",\"left_grip\":";point(e.leftGrip);out<<",\"right_grip\":";point(e.rightGrip);out<<",\"sample\":";writeSupportRecord(e.sample);out<<'}';
    }
    out<<"]},\"pose_observer_calls\":"<<poseCalls.load()<<",\"pose_observer_rejected\":"<<poseRejected.load()<<",\"pose_writes_enabled\":false,\"pose_evidence\":[";
    for(unsigned i=0;i<poseRecordCount;++i){const auto& r=poseRecords[i];if(i)out<<',';
        out<<"{\"kind\":"<<r.kind<<",\"thread\":"<<r.thread<<",\"caller\":"<<r.caller<<",\"owner\":"<<r.owner<<",\"object\":"<<r.object<<",\"ms\":"<<r.ms<<",\"matrix\":[";
        for(unsigned n=0;n<16;++n){if(n)out<<',';out<<(n%4==3?0:r.matrix[n]);}out<<"]}";
    }out<<"],\"body_follow_requested\":"<<(bodyFollow?"true":"false")<<",\"body_follow_samples\":"<<followSamples.load()<<",\"body_read_failures\":"<<bodyReadFailures.load()<<",\"body_follow_evidence\":[";
    for(unsigned i=0;i<followRecordCount;++i){const auto& r=followRecords[i];if(i)out<<',';out<<"{\"ms\":"<<r.ms<<",\"head\":["<<r.head.x<<','<<r.head.y<<','<<r.head.z<<"],\"body\":["<<r.body.x<<','<<r.body.y<<','<<r.body.z<<"],\"consumed\":["<<r.consumed.x<<','<<r.consumed.y<<','<<r.consumed.z<<"],\"forward\":"<<r.forward<<",\"strafe\":"<<r.strafe<<'}';}
    out<<"],\"rig_attempts\":"<<rigAttempts<<",\"rig_writes_enabled\":false,\"rig\":";
    if(!observedRig)out<<"null";else{const auto& r=*observedRig;
        out<<"{\"animation\":"<<r.identity.animation<<",\"skeleton\":"<<r.identity.skeleton<<",\"pose\":"<<r.identity.pose<<",\"world_matrices\":"<<r.identity.worldMatrices<<",\"weapon_bone\":"<<r.weaponBone<<",\"evaluated_matrices\":"<<r.identity.evaluatedMatrices<<",\"native_ik\":"<<(r.identity.nativeIk?"true":"false")<<",\"skin_consistency_error\":"<<r.skinConsistencyError<<",\"bones\":[";
        for(unsigned n=0;n<r.names.size();++n){if(n)out<<',';out<<"{\"name\":\"";
            for(char c:r.names[n]){if(c=='"'||c=='\\')out<<'\\';out<<c;}out<<"\",\"parent\":"<<r.parents[n]<<",\"world\":[";
            for(unsigned j=0;j<16;++j){if(j)out<<',';out<<r.world[n].values[j/4][j%4];}out<<"]}";
        }out<<"]}";
    }out<<",\"ik_preview\":{\"solved\":"<<ikPreviewSolved<<",\"failed\":"<<ikPreviewFailed<<",\"candidate_bone_edits\":"<<ikPreviewEdits<<",\"reach_clamps\":"<<ikPreviewClamps<<",\"max_segment_length_error\":"<<ikPreviewLengthError<<",\"max_target_error\":"<<ikPreviewTargetError<<",\"writes_applied\":false},\"animation_calls\":"<<animationCalls.load()<<",\"animation_overlaps\":"<<animationOverlap.load()<<",\"phase_failures\":"<<phaseFailures.load()<<",\"animation_phases\":[";
    for(unsigned n=0;n<phaseRecordCount;++n){const auto& r=phaseRecords[n];if(n)out<<',';
        out<<"{\"sequence\":"<<r.sequence<<",\"phase\":"<<r.phase<<",\"thread\":"<<r.thread<<",\"animation\":"<<r.animation<<",\"world\":"<<r.world<<",\"skin\":"<<r.skin<<",\"override\":"<<r.overrideSkin<<",\"override_active\":"<<(r.overrideActive?"true":"false")<<",\"world_hash\":"<<r.worldHash<<",\"skin_hash\":"<<r.skinHash<<",\"override_hash\":"<<r.overrideHash<<'}';
    }out<<"],\"fire_origin_observation\":{\"requested\":"<<((equipProbe||muzzleFire)?"true":"false")<<",\"calls\":"<<fireOriginCalls.load()<<",\"matching_candidates\":"<<fireOriginMatches.load()<<",\"rejected_outputs\":"<<fireOriginRejected.load()<<",\"client_calls\":"<<clientShotCalls.load()<<",\"client_copies\":"<<clientShotCopies.load()<<",\"client_rejected\":"<<clientShotRejected.load()<<",\"fire_writes_enabled\":"<<(muzzleFire?"true":"false")<<",\"client_origin_writes\":"<<clientOriginWrites.load()<<",\"server_origin_writes\":"<<serverOriginWrites.load()<<",\"origin_write_failures\":"<<originWriteFailures.load()<<",\"shot_source_changes\":"<<shotSourceChanges.load()<<",\"origin_fallbacks\":"<<originFallbacks.load()<<",\"origin_writes_after_log_full\":"<<originWritesAfterLogFull.load()<<",\"records\":[";
    const auto firePrecision=out.precision();out.precision(9);
    const auto fireText=[&](std::string_view text){out<<'"';for(unsigned char c:text){
        if(c=='"'||c=='\\')out<<'\\'<<char(c);
        else if(c<32){constexpr char hex[]="0123456789abcdef";out<<"\\u00"<<hex[c>>4]<<hex[c&15];}
        else out<<char(c);}out<<'"';};
    for(unsigned i=0;i<fireRecordCount;++i){const auto& r=fireRecords[i];const auto item=FindWeaponProfileByTelemetryKind(r.weaponKind);if(i)out<<',';
        out<<"{\"shot_token\":"<<r.shotToken<<",\"token_valid\":"<<(r.tokenValid?"true":"false")<<",\"weapon_name\":";
        fireText(!r.equipment.Asset().empty()?r.equipment.Asset():item?item->assetName:std::string_view{"unclassified"});
        out<<",\"weapon_data\":"<<r.equipment.data<<",\"persistence\":"<<r.equipment.persistence
           <<",\"native_invocation\":"<<r.nativeInvocation
           <<",\"native_muzzle_supported\":"<<r.nativeMuzzleSupported<<",\"observed_candidate_unexpired\":"<<r.observedCandidateUnexpired
           <<",\"observed_qpc\":"<<r.observedQpc<<",\"observed_current_generation\":"<<r.observedCurrentGeneration
           <<",\"observed_current_deadline_qpc\":"<<r.observedCurrentDeadline
           <<",\"native_config_unchanged\":"<<r.nativeConfigUnchanged<<",\"muzzle_enrollment\":\""
           <<(r.shot&&r.shot->muzzleBinding?(r.shot->muzzleBinding->profile->admission==MuzzleAdmission::Candidate?"candidate":"native_verified"):"none")<<'"'
           <<",\"matrix_stage\":\""<<(r.phase==2?"native_pre_spread_composition":"native_callback_transform")<<'"'
           <<",\"phase\":"<<r.phase<<",\"origin_written\":"<<(r.originWritten?"true":"false")<<",\"client_path\":"<<(r.clientPath?"true":"false")<<",\"caller\":"<<r.caller<<",\"effects\":"<<r.effects<<",\"server_player\":"<<r.player<<",\"server_soldier\":"<<r.serverSoldier<<",\"client_soldier\":"<<r.clientSoldier<<",\"weapon\":"<<r.weapon<<",\"firing_data\":"<<r.data<<",\"thread\":"<<r.thread<<",\"ms\":"<<r.ms<<",\"native_shot_matrix\":[";
        for(unsigned j=0;j<16;++j){if(j)out<<',';out<<(j%4==3?0:r.matrix[j]);}out<<"],\"client_effects_matrix\":[";
        for(unsigned j=0;j<16;++j){if(j)out<<',';out<<(j%4==3?0:r.clientEffects[j]);}out<<"],\"player_index\":"<<r.playerId<<",\"tracked_shot_candidate\":";
        if(!r.shot)out<<"null";else {out<<"{\"generation\":"<<r.shot->generation<<",\"space\":"<<r.shot->space
            <<",\"owner_generation\":"<<r.shot->ownerGeneration<<",\"deadline_qpc\":"<<r.shot->deadline
            <<",\"units_per_metre\":"<<r.shot->unitsPerMetre<<",\"flash_bone\":"<<r.shot->flashBone;
            const auto matrix=[&](const char* name,const math::Matrix4& m){out<<",\""<<name<<"\":[";for(unsigned n=0;n<16;++n){if(n)out<<',';out<<m.values[n/4][n%4];}out<<']';};
            matrix("native_weapon",r.shot->nativeWeapon);matrix("tracked_weapon",r.shot->trackedWeapon);matrix("native_flash",r.shot->nativeFlash);matrix("tracked_flash",r.shot->trackedFlash);
            if(r.eventPose){matrix("event_muzzle",r.eventPose->muzzle);out<<",\"event_generation\":"<<r.eventPose->generation;}
            if(r.mappedShot)matrix("mapped_shot",*r.mappedShot);
            if(r.observedMuzzleCandidate)matrix("read_only_muzzle_candidate",*r.observedMuzzleCandidate);
            out<<",\"aim_evidence\":";
            if(!r.shot->aimEvidence)out<<"null";else {const auto& a=*r.shot->aimEvidence;
                const auto point=[&](const math::Vec3& p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};
                const auto pose=[&](const math::Pose& p){out<<'['<<p.position.x<<','<<p.position.y<<','<<p.position.z<<','
                    <<p.orientation.x<<','<<p.orientation.y<<','<<p.orientation.z<<','<<p.orientation.w<<']';};
                out<<"{\"weapon\":"<<a.equipment.weapon<<",\"weapon_data\":"<<a.equipment.data<<",\"persistence\":"<<a.equipment.persistence
                   <<",\"asset\":";fireText(a.equipment.Asset());
                out<<",\"configuration_path_verified\":"<<a.configurationPathVerified<<",\"configuration_path\":";
                const auto end=std::find(a.configurationPath.begin(),a.configurationPath.end(),'\0');
                fireText(end==a.configurationPath.end()?std::string_view{}:std::string_view(a.configurationPath.data(),std::size_t(end-a.configurationPath.begin())));
                out<<",\"configuration_sequence\":"<<a.configurationSequence<<",\"configuration_deadline_ns\":"<<a.configurationDeadlineNs
                   <<",\"authored_binding\":"<<a.authoredBinding<<",\"authored_axes\":"<<a.authoredAxes
                   <<",\"mesh\":";fireText(a.meshPath);out<<",\"binding_digest\":";fireText(a.bindingDigest);
                out<<",\"axis_evidence\":";fireText(a.axisEvidence);out<<",\"rig_fingerprint\":"<<a.rigFingerprint
                   <<",\"model_forward\":";point(a.modelForward);out<<",\"model_up\":";point(a.modelUp);
                out<<",\"body_yaw\":"<<a.bodyYaw<<",\"reference_pose\":";pose(a.reference);
                out<<",\"effective_grip\":";pose(a.effectiveGrip);out<<",\"effective_aim\":";pose(a.effectiveAim);matrix("body",a.body);
                out<<",\"pre_support_input_available\":"<<a.preSupportInputAvailable
                   <<",\"pre_support_generation\":"<<a.preSupportGeneration<<",\"pre_support_space\":"<<a.preSupportSpace;
                out<<",\"pre_support_reference_pose\":";if(a.preSupportInputAvailable)pose(a.preSupportReference);else out<<"null";
                out<<",\"pre_support_grip\":";if(a.preSupportInputAvailable)pose(a.preSupportGrip);else out<<"null";
                out<<",\"pre_support_aim\":";if(a.preSupportInputAvailable)pose(a.preSupportAim);else out<<"null";
                out<<'}';}out<<'}';
        }out<<",\"native_config\":";
        if(!r.nativeConfig)out<<"null";else {const auto& c=*r.nativeConfig;
            out<<"{\"effects\":"<<c.effects<<",\"firing_data\":"<<c.firingData<<",\"function\":"<<c.function<<",\"configuration\":"<<c.config
               <<",\"muzzle_explosion\":"<<c.muzzleExplosion<<",\"projectile_data\":"<<c.projectileData
               <<",\"active_force_spawn_to_camera\":"<<unsigned(c.cameraFlags[0])<<",\"force_spawn_to_camera\":"<<unsigned(c.cameraFlags[1])
               <<",\"relative_target_aiming\":"<<unsigned(c.cameraFlags[2])<<",\"bullets_per_shell\":"<<c.bulletsPerShell<<",\"bullets_per_shot\":"<<c.bulletsPerShot
               <<",\"inherited_speed\":"<<c.inheritedSpeed
               <<",\"initial_speed\":["<<c.speed[0]<<','<<c.speed[1]<<','<<c.speed[2]<<']'
               <<",\"initial_direction\":["<<c.direction[0]<<','<<c.direction[1]<<','<<c.direction[2]<<']'
               <<",\"initial_position\":["<<c.position[0]<<','<<c.position[1]<<','<<c.position[2]<<"]}";}
        out<<",\"output_matrix\":[";for(unsigned j=0;j<16;++j){if(j)out<<',';out<<(j%4==3?0:r.after[j]);}out<<"]}";
    }out<<"]},\"projectile_launch_observation\":{\"read_only\":true,\"binding_verified\":"<<(projectileLaunchBinding.verified?"true":"false")
        <<",\"hook_removed\":"<<(projectileLaunchHookRemoved?"true":"false")
        <<",\"helper\":"<<projectileLaunchBinding.helper<<",\"helper_return\":"<<projectileLaunchBinding.helperReturn
        <<",\"virtual_call\":"<<projectileLaunchBinding.virtualCall<<",\"virtual_return\":"<<projectileLaunchBinding.virtualReturn
        <<",\"qpc_frequency\":"<<handFrequency<<",\"calls\":"<<projectileLaunchCalls.load()<<",\"rejected\":"<<projectileLaunchRejected.load()
        <<",\"dropped\":"<<projectileLaunchDropped.load()<<",\"busy\":"<<projectileLaunchBusy.load()<<",\"server_return_unrecorded\":"<<projectileLaunchReturnUnrecorded.load()
        <<",\"capacity\":"<<projectileLaunchRecords.size()<<",\"dispatch_instruction_verified\":"<<(projectileLaunchBinding.verified?"true":"false")
        <<",\"launch_return_observed\":false,\"receiver_stored_velocity_verified\":false,\"records\":[";
    const auto launchMatrix=[&](const std::array<float,16>& m){out<<'[';for(unsigned n=0;n<16;++n){if(n)out<<',';out<<(n%4==3?0.f:m[n]);}out<<']';};
    const auto launchArguments=[&](const std::optional<ProjectileLaunchArguments>& value){
        if(!value){out<<"null";return;}const auto& a=*value;
        out<<"{\"context_output\":"<<a.contextOutput<<",\"composed_address\":"<<a.composedAddress<<",\"secondary_address\":"<<a.secondaryAddress
           <<",\"velocity_address\":"<<a.velocityAddress<<",\"token\":"<<a.token<<",\"primary\":"<<a.primary<<",\"projectiles_per_shell\":"<<a.projectilesPerShell
           <<",\"context\":"<<a.context<<",\"derived_context\":"<<a.derivedContext<<",\"effects_scalar\":"<<a.effectsScalar<<",\"multiplier\":"<<a.multiplier
           <<",\"native_composed\":";launchMatrix(a.composed);out<<",\"native_secondary\":";launchMatrix(a.secondary);
        out<<",\"native_velocity\":["<<a.velocity[0]<<','<<a.velocity[1]<<','<<a.velocity[2]<<"]}";
    };
    for(unsigned n=0;n<projectileLaunchCount.load();++n){if(n)out<<',';const auto& r=projectileLaunchRecords[n];
        out<<"{\"native_invocation\":"<<r.invocation<<",\"ordinal\":"<<r.ordinal<<",\"effects\":"<<r.effects<<",\"configuration\":"<<r.configuration
           <<",\"server_player\":"<<r.player<<",\"server_soldier\":"<<r.soldier<<",\"weapon\":"<<r.equipment.weapon<<",\"weapon_data\":"<<r.equipment.data
           <<",\"persistence\":"<<r.equipment.persistence<<",\"asset\":";fireText(r.equipment.Asset());
        out<<",\"thread\":"<<r.thread<<",\"caller\":"<<r.caller<<",\"token\":"<<r.token<<",\"before_qpc\":"<<r.beforeQpc<<",\"after_qpc\":"<<r.afterQpc
           <<",\"candidate_generation\":"<<r.candidateGeneration<<",\"owner_generation\":"<<r.ownerGeneration<<",\"space\":"<<r.space
           <<",\"helper_result_matches\":"<<r.helperResultMatches<<",\"owner_config_current\":"<<r.ownerAndConfigCurrent
           <<",\"accepted\":"<<r.accepted<<",\"server_shoot_returned\":"<<r.serverShootReturned<<",\"composition_origin_written\":"<<r.compositionWritten
           <<",\"composition_output\":";if(r.composedOutput)launchMatrix(*r.composedOutput);else out<<"null";
        out<<",\"event_generation\":"<<(r.eventPose?r.eventPose->generation:0)<<",\"event_deadline_qpc\":"<<(r.eventPose?r.eventPose->deadline:0)
           <<",\"before_helper\":";launchArguments(r.before);out<<",\"after_helper\":";launchArguments(r.after);out<<'}';
    }
    out<<"],\"server_fire_context\":{\"read_only\":true,\"complete_session_coverage\":false,\"shot_acknowledgement\":false,\"hit_acknowledgement\":false"
       <<",\"scope\":\"already_matched_on_foot_server_scopes_normal_returns_only\""
       <<",\"context_scope\":\"entry_candidate_and_final_nested_context_copies\""
       <<",\"native_lifetime_continuity_verified\":false,\"trigger_state_available\":false,\"trigger_continuity_verified\":false"
       <<",\"retention\":\"16_recent_observed_identity_cohorts_counts_since_admission\""
       <<",\"span_scope\":\"original_server_call_including_existing_nested_observers\""
       <<",\"gap_scope\":\"adjacent_retained_same_cohort_returns_begin_to_begin_not_fire_cadence\""
       <<",\"controller_scope\":\"copied_candidate_controller_mapping_original_input_not_trigger_state\""
       <<",\"detailed_argument_read_capacity_unchanged\":"<<projectileLaunchRecords.size()
       <<",\"scope_starts\":"<<serverFireScopeStarts.load()<<",\"scope_returns\":"<<serverFireScopeReturns.load()
       <<",\"busy_unrecorded_returns\":"<<serverFireObservationBusy.load()<<",\"callbacks_drained\":"<<!active.load()
       <<",\"counters_and_snapshot_atomic\":false,\"snapshot_available\":";
    ServerFireObservation fireSnapshot;
    const bool fireSnapshotAvailable=!serverFireObservationGate.test_and_set(std::memory_order_acquire);
    if(fireSnapshotAvailable){fireSnapshot=serverFireObservation;serverFireObservationGate.clear(std::memory_order_release);}
    out<<fireSnapshotAvailable<<",\"snapshot\":";if(fireSnapshotAvailable)fireSnapshot.Report(out);else out<<"null";
    out<<"}},\"rig_publication\":";out.precision(firePrecision);rigPublication::Report(out);ReportAttached(out);out<<'}';}
}
