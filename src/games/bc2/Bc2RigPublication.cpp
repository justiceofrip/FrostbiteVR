#include "Bc2TrackedBodyBase.h"
#include <Windows.h>
#include <intrin.h>
#include <MinHook.h>
#include "Bc2RigPublication.h"
#include "Bc2NativeCycleRuntime.h"
#include "Bc2PumpCapture.h"
#include "Bc2M95ShotPartCapture.h"
#include "Bc2WeaponFrameAccess.h"
#include "Bc2MagazineFallbackObservation.h"
#include "Bc2AuthoredSupport.h"
#ifdef FVR_BC2_AUTHORED_SUPPORT_HEADER
#include FVR_BC2_AUTHORED_SUPPORT_HEADER
#endif
#include "Bc2AuthoredGrip.h"
#include "Bc2Gameplay.h"
#include "Bc2BodyAmmo.h"
#include "Bc2ReticleDrawObservation.h"
#ifdef FVR_BC2_AUTHORED_GRIP_HEADER
#include FVR_BC2_AUTHORED_GRIP_HEADER
#endif
#include "Bc2Rig.h"
#include "Bc2HandPose.h"
#include "fvr/interaction/GripAttachment.h"
#include "fvr/interaction/HandTouch.h"
#include "Bc2WeaponCapture.h"
#include <sstream>
#include <bit>
#include "fvr/interaction/ArmIk.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/ComfortCamera.h"
#include <mutex>
#include <memory>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <cmath>
#include <algorithm>
namespace fvr::bc2::rigPublication {
namespace {
using Getter=unsigned(__thiscall*)(void*);
using Pack=std::uintptr_t(__cdecl*)(const void*,void*,unsigned);
Getter getterA=nullptr,getterB=nullptr;Pack packOriginal=nullptr;
RigConsumerCandidates profile{};std::uintptr_t base=0;ResolveOwner resolveOwner=nullptr;
std::array<void*,3> hooks{};std::atomic<bool> enabled=false;std::atomic<unsigned> inFlight=0;
bool pulse=false,hands=false;ULONGLONG started=0;
std::mutex frameMutex;Tracking tracking;interaction::TrackedRig calibratedRig;WeaponCapture weaponCapture;PumpPartCapture pumpPartCapture;ULONGLONG captureTime=0;
std::atomic<unsigned> m95ShotCapturePhase=0;M95ShotPartCapture m95ShotPartCapture;
struct PublishedPose {std::optional<AuthoredGripBinding> authoredGrip;std::optional<OrdinaryEquipmentRequest> ordinaryRecovery;WeaponEquipmentIdentity equipment{};std::shared_ptr<const BeltAmmoPalette> belt;ReloadPackedSource drawSource{};RigIdentity identity;unsigned weapon=0;std::uint64_t generation=0,space=0,ownerGeneration=0,equipmentGeneration=0;std::int64_t deadline=0,reloadDeadlineNs=0;std::vector<std::array<std::byte,64>> before,posed,previewBase,reloadBase;std::uint64_t previewToken=0;std::shared_ptr<const MagazineTracking> magazine;std::vector<std::array<std::byte,64>> magazineBase;std::shared_ptr<const ReloadPreview> reloadPreview;std::shared_ptr<const ReloadShellHidePlan> shellHide;std::shared_ptr<const WeaponVisibilityPlan> visibility;std::shared_ptr<const BodyFreeRightEvidence> freeRight;std::shared_ptr<const Bc2PumpTracking> pump;std::vector<std::array<std::byte,64>> pumpBase;std::shared_ptr<const Bc2BoltTracking> bolt;std::vector<std::array<std::byte,64>> boltBase;std::optional<WeaponShotFrame> shot;};
std::shared_ptr<const PublishedPose> published;
struct ShotGuard {unsigned soldier=0,weak=0,weapon=0;std::uint64_t owner=0,space=0,generation=0;std::int64_t deadline=0;bool valid=false,leftTracked=false;std::uint64_t previewToken=0;bool weaponActionsBlocked=false;std::uint64_t equipmentGeneration=0;WeaponEquipmentIdentity equipment{};};
struct ShotPublication {WeaponShotFrame frame;ShotGuard identity;std::optional<AuthoredGripBinding> authoredGrip;};
std::atomic<std::shared_ptr<const ShotGuard>> shotGuard;
std::atomic<std::shared_ptr<const ShotPublication>> shotPublication;
std::atomic<std::shared_ptr<const ReloadTracking>> reloadGuard;
std::atomic<std::shared_ptr<const Bc2PumpTracking>> pumpGuard;
std::atomic<std::shared_ptr<const Bc2BoltTracking>> boltGuard;
std::atomic<unsigned> boltContacts=0,boltPoses=0,boltCopies=0,boltPairs=0,boltFallbacks=0;
struct BoltPairedTarget {Bc2BoltTracking source;std::int64_t packedNs=0;std::uint64_t serial=0;};
struct BoltPairedPhase {std::optional<BoltPairedTarget> first,last;unsigned pairs=0;};
std::mutex boltPairMutex;std::array<BoltPairedPhase,16> boltPairedPhases{};
std::atomic<unsigned> boltEvidenceDrops=0,boltEvidenceRejected=0;
void RetainBoltPairedTarget(const Bc2BoltTracking& source,std::uint64_t serial,std::int64_t now){
    if(!source.target)return;
    if(!BoltTargetFresh(source,now)){++boltEvidenceRejected;return;}
    std::unique_lock lock(boltPairMutex,std::try_to_lock);if(!lock.owns_lock()){++boltEvidenceDrops;return;}
    BoltPairedPhase* selected=nullptr;
    for(auto& phase:boltPairedPhases)if(phase.first&&phase.first->source.held->cycle==source.held->cycle&&
        phase.first->source.mechanismPhase==source.mechanismPhase){selected=&phase;break;}
    if(!selected)for(auto& phase:boltPairedPhases)if(!phase.first){selected=&phase;break;}
    if(!selected){++boltEvidenceDrops;return;}
    const BoltPairedTarget receipt{source,now,serial};
    if(!selected->first)selected->first=receipt;
    if(!selected->last||selected->last->source.input.sequence<=source.input.sequence)selected->last=receipt;
    ++selected->pairs;
}
std::atomic<unsigned> pumpContacts=0,pumpPlanAttempts=0,pumpPoses=0,pumpReachRejects=0,pumpPoseRejects=0,pumpFallbacks=0,pumpSourceRejects=0,pumpCopies=0,pumpPairs=0;
std::atomic<std::shared_ptr<const MagazineTracking>> magazineGuard;
std::atomic<std::shared_ptr<const MagazineDetachPairReceipt>> magazineDetachPair;
std::atomic<unsigned> magazinePoses=0,magazineFallbacks=0,magazineCopies=0,magazinePairs=0,magazineContacts=0;
MagazineFallbackJournal<> magazineFallbackJournal;
std::array<std::atomic<unsigned>,4> magazineRoleCopies{},magazineRolePairs{};
std::atomic<unsigned> magazinePlanAttempts=0,magazineBindingRejects=0,magazinePlanRejects=0,magazineReachRejects=0,magazinePoseRejects=0,magazineHiddenRejects=0,magazineFreshRejects=0;
struct MagazineReachRecord {std::uint64_t sequence=0;unsigned role=0;float error=0;bool solved=false,clamped=false;};
std::array<MagazineReachRecord,24> magazineReachRecords{};unsigned magazineReachCount=0;
std::atomic<std::shared_ptr<const WeaponVisibilityRequest>> visibilityGuard;
std::atomic<std::shared_ptr<const WeaponVisibilityReceipt>> visibilityReceipt;
std::atomic<std::shared_ptr<const OrdinaryEquipmentRequest>> ordinaryEquipmentGuard;
std::atomic<std::shared_ptr<const OrdinaryEquipmentPair>> ordinaryEquipmentPair;
std::atomic<unsigned> ordinaryEquipmentCopies=0,ordinaryEquipmentPairs=0;
std::atomic<std::shared_ptr<const BodyFreeRightEvidence>> freeRightGuard;
std::atomic<unsigned> freeRightPoses=0,freeRightFallbacks=0;
std::atomic<unsigned> freeRightPackedCopies=0,freeRightPairedCopies=0;
std::atomic<unsigned> visibilityPlans=0,visibilityRejected=0,visibilityHiddenCopies=0,visibilityShownCopies=0,visibilityFallbacks=0,visibilityPairedReceipts=0;
std::atomic<unsigned> beltPlans=0,beltCopies=0,beltPairs=0,beltFallbacks=0,beltSourceRejected=0;
std::atomic<unsigned> reloadPreviewPoses=0,reloadPreviewRejected=0,reloadPreviewFallbacks=0,reloadRawContacts=0;
std::atomic<unsigned> reloadNativeVisibleContacts=0,reloadNativeHiddenContacts=0;
std::atomic<unsigned> reloadOwnedShellVisibilityPoses=0;
std::atomic<unsigned> reloadShellHidePlans=0,reloadShellHideCopies=0,reloadShellHidePairs=0,reloadShellHideFallbacks=0,reloadShellHideSourceRejects=0;
std::array<std::atomic<unsigned>,3> reloadPhasePoses{};
std::array<std::atomic<unsigned>,3> reloadVerifiedPacks{},reloadVerifiedPairedPacks{};
std::atomic<unsigned> reloadVerifiedFallbackPacks=0,reloadVerifiedPairedFallbackPacks=0;
std::array<std::atomic<unsigned>,8> reloadPresentationRejections{};
struct NativeEye {unsigned soldier=0,weak=0;math::Matrix4 camera{};ULONGLONG ms=0;};
std::atomic<std::shared_ptr<const NativeEye>> nativeEye;
std::atomic<std::shared_ptr<const Tracking>> eyeTracking;
std::atomic<unsigned> shotFrameRaces=0,shotFrameExpired=0,shotFrameUnavailable=0,supportFramesWhileFireBlocked=0;

struct HandRecord {std::uint64_t generation=0;math::Matrix4 nativeWeapon{},placedWeapon{},eyeBase{},resolvedRight{},nativeRight{},nativeLeft{};bool attachmentPending=false,authoredGrip=false,supportAttached=false,sightAttached=false;std::array<math::Vec3,2> localGrips{};math::Vec3 grip{},actorPosition{},nativeRoot{},bodyRoot{},anatomyRoot{};std::array<math::Vec3,2> grips{},shoulders{},anchors{},targets{},resolved{};std::array<float,2> errors{};std::array<bool,2> tracked{};unsigned weapon=0;std::uint64_t ownerGeneration=0,space=0;};
std::array<HandRecord,256> handRecords{};unsigned handRecordCount=0,handRecordNext=0;ULONGLONG handRecordTime=0;
std::atomic<unsigned> torsoPoses=0,hiddenLeafPoses=0,supportAttachedPoses=0;
struct SightPreviewRecord {
    SightPreview preview{};std::uint64_t inputGeneration=0,owner=0,space=0;
    math::Matrix4 nativeRear{},nativeFront{},rawHand{},resolvedHand{};
    float palmError=0;bool clamped=false;
};
std::array<SightPreviewRecord,128> sightPreviewRecords{};unsigned sightPreviewCount=0,sightPreviewNext=0;ULONGLONG sightPreviewRecordTime=0;
std::atomic<unsigned> sightPreviewPoses=0,sightPreviewBaseFallbacks=0,sightPreviewRejected=0;
interaction::GripAttachment supportAttachment;
std::optional<Bc2HandBinding> handBinding,rightHandBinding;
std::uint64_t handBindingFingerprint=0;float handBindingUnits=0;
std::array<std::atomic<unsigned>,3> handRolePoses{};std::atomic<unsigned> handPoseRejected=0;
struct FingerEvidence {unsigned index=0,parent=0;math::Matrix4 reference{},native{},posed{};};
struct HandPoseEvidence {
    std::uint64_t generation=0,space=0,owner=0;unsigned weapon=0;interaction::HandPoseRole role{};
    float squeeze=0,trigger=0;unsigned touchActive=0,touched=0;math::Matrix4 wrist{},grip{};math::Vec3 mechanismPoint{};
    std::array<FingerEvidence,15> fingers{};
};
std::array<HandPoseEvidence,256> handPoseRecords{};unsigned handPoseCount=0,handPoseNext=0;
ULONGLONG handPoseRecordTime=0;
struct TriggerEvidence {std::uint64_t generation=0;unsigned weapon=0;float trigger=0;bool wristPreserved=false,otherBranchesPreserved=false;std::array<unsigned,3> indices{};std::array<math::Matrix4,3> before{},after{};};
std::array<TriggerEvidence,128> triggerRecords{};unsigned triggerCount=0,triggerNext=0;ULONGLONG triggerRecordTime=0;
std::atomic<unsigned> triggerPoses=0,triggerFailures=0;


std::array<std::atomic<unsigned>,unsigned(AuthoredGripStatus::Count)> authoredGripStatus{};
std::atomic<unsigned> authoredGripExpired=0,authoredGripCopies=0;
std::span<const AuthoredGripProfile> AuthoredGripProfiles()noexcept {
#ifdef FVR_BC2_AUTHORED_GRIP_HEADER
    return generated::AuthoredGrips;
#else
    return {};
#endif
}
std::atomic<unsigned> trackedPoses=0,trackingRejected=0,trackingUnavailable=0,weaponOutputs=0,calibrations=0,poseMisses=0;
constexpr const char* rejectionNames[]={"owner","rig_snapshot","weapon_output","eye_base","physical_torso","head_bone","bind_landmarks","bind_frame","inverse_bind_frame","bind_arms","bind_torso","tracked_targets","torso_subtree","arm_solve","weapon_subtree","palette_plan","output_deadline"};
std::array<std::atomic<unsigned>,17> rejectionCounts{};
void RejectTracking(unsigned reason){++trackingRejected;++rejectionCounts[reason];}
std::array<std::atomic<unsigned>,2> handPoses{},handCalibrations{};std::atomic<unsigned> partialPoses=0,fallbackFailures=0;
bool Fresh(std::int64_t deadline)noexcept {LARGE_INTEGER q{};return QueryPerformanceCounter(&q)&&q.QuadPart>0&&q.QuadPart<deadline;}
std::int64_t ReloadNanos(std::int64_t ticks=0)noexcept {
    LARGE_INTEGER frequency{},now{};
    if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return 0;
    if(!ticks){if(!QueryPerformanceCounter(&now))return 0;ticks=now.QuadPart;}
    if(ticks<=0||ticks/frequency.QuadPart>INT64_MAX/1000000000)return 0;
    return (ticks/frequency.QuadPart)*1000000000+std::int64_t(static_cast<long double>(ticks%frequency.QuadPart)*1000000000/frequency.QuadPart);
}

bool AuthoredCurrent(const std::optional<AuthoredGripBinding>& binding)noexcept {
    if(!binding)return true;
    const auto current=shotGuard.load(std::memory_order_acquire);const auto ns=ReloadNanos();
    if(!current||!current->valid||!Fresh(current->deadline))return false;
    const auto selected=gameplay::ReadSelectedMeshes(binding->selected->owner,ns);
    return AuthoredGripCurrent(*binding,selected.get(),current->equipment,
        {current->soldier,current->weak,current->owner,current->equipmentGeneration,current->space,
         current->generation,ReloadNanos(current->deadline)},ns);
}
bool FreeRightCurrent(const BodyFreeRightEvidence& source)noexcept {
    const auto current=freeRightGuard.load(std::memory_order_acquire);const auto ns=ReloadNanos();
    const auto visibility=visibilityGuard.load(std::memory_order_acquire);
    return BodyFreeRightRenderCurrent(source,current.get(),visibility.get(),ns);
}
std::atomic<unsigned> owners=0,copies=0,changedCopies=0,rejected=0,sourceChanges=0,packingFailures=0,pairedCopies=0;
std::atomic<std::uint64_t> drawPackSerial=0;
struct Scope {std::optional<AuthoredGripBinding> authoredGrip;std::optional<OrdinaryEquipmentRequest> ordinaryRecovery;RigIdentity ordinaryRig{};unsigned ordinaryMask=0;std::int64_t ordinaryDeadlineNs=0;std::shared_ptr<const BeltAmmoPalette> belt;unsigned beltMask=0;bool beltVisible=false;ReloadPackedSource drawSource{};std::uint64_t drawSerial=0;unsigned soldier=0,weak=0,sourceA=0,sourceB=0,count=0;int reloadPack=-1;unsigned visibilityMask=0,freeRightMask=0,shellHideMask=0,magazineMask=0,pumpMask=0,boltMask=0;std::shared_ptr<const MagazineTracking> magazine;std::vector<std::array<std::byte,64>> magazineBase;bool shellHidden=false;std::shared_ptr<const ReloadShellHidePlan> shellHide;std::shared_ptr<const WeaponVisibilityPlan> visibility;std::shared_ptr<const BodyFreeRightEvidence> freeRight;std::shared_ptr<const Bc2PumpTracking> pump;std::vector<std::array<std::byte,64>> pumpBase;std::shared_ptr<const Bc2BoltTracking> bolt;std::vector<std::array<std::byte,64>> boltBase;bool changed=false;std::uint64_t equipmentGeneration=0;WeaponEquipmentIdentity equipment{};std::vector<std::array<std::byte,64>> before,posed;};
thread_local Scope scope;
struct Callback {Callback(){++inFlight;}~Callback(){--inFlight;}};
bool Read(unsigned at,void* out,std::size_t n)noexcept {SIZE_T got=0;return at>=0x10000&&n&&n<=65536&&std::uint64_t(at)+n<=UINT32_MAX&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,n,&got)&&got==n;}
unsigned U32(unsigned at)noexcept {unsigned v=0;Read(at,&v,4);return v;}
bool OrdinaryGuardCurrent(const OrdinaryEquipmentRequest& source)noexcept {
    const auto current=ordinaryEquipmentGuard.load(std::memory_order_acquire);
    const auto shot=shotGuard.load(std::memory_order_acquire);
    const auto reload=reloadGuard.load(std::memory_order_acquire);
    const auto magazine=magazineGuard.load(std::memory_order_acquire);
    return current&&OrdinaryEquipmentCurrent(source,*current,ReloadNanos())&&shot&&shot->valid&&shot->leftTracked&&
        shot->soldier==source.owner.soldier&&shot->weak==source.owner.weak&&shot->weapon==source.owner.weapon&&
        shot->owner==source.owner.actorGeneration&&shot->space==source.owner.space&&
        shot->equipmentGeneration==source.input.owner.equipGeneration&&shot->equipment==source.equipment&&
        shot->generation>=source.input.sequence&&!shot->previewToken&&Fresh(shot->deadline)&&
        !visibilityGuard.load(std::memory_order_acquire)&&!freeRightGuard.load(std::memory_order_acquire)&&
        (!reload||(!reload->preview&&!reload->shellControl&&!reload->belt))&&(!magazine||!magazine->target);
}
bool EquipmentStillCurrent(const WeaponEquipmentIdentity& expected)noexcept {
    // The exact data pointer was reflected by Gameplay. Native addresses and
    // contents are re-read coherently at consumption; no native call occurs.
    const WeaponModeMemory memory{const_cast<WeaponEquipmentIdentity*>(&expected),
        [](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);},
        [](void* p,unsigned at,const char*){return static_cast<const WeaponEquipmentIdentity*>(p)->data==at;}};
    return WeaponEquipmentStillCurrent(memory,expected);
}
std::optional<std::size_t> Offset(const engine::PeImage& pe,unsigned rva,unsigned n){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&n<=s.rawSize-(rva-s.rva))return std::size_t(s.rawOffset)+rva-s.rva;return {};}
unsigned __fastcall GetA(void* self,void*){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=getterA(self);
    if(!enabled.load(std::memory_order_acquire)||caller!=base+profile.getCallerA)return result;
    scope={};unsigned weak=0;if(resolveOwner&&resolveOwner(reinterpret_cast<unsigned>(self),weak)){
        scope.soldier=reinterpret_cast<unsigned>(self);scope.weak=weak;scope.sourceA=result;scope.drawSerial=++drawPackSerial;++owners;
    }return result;
}
unsigned __fastcall GetB(void* self,void*){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const auto result=getterB(self);
    if(enabled.load(std::memory_order_acquire)&&caller==base+profile.getCallerB&&scope.soldier==reinterpret_cast<unsigned>(self))scope.sourceB=result;
    return result;
}
bool MakePulse(unsigned count){
    const RigMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    const auto rig=ReadFirstPersonRig(memory,scope.soldier,scope.weak);
    if(!rig||rig->identity.count!=count||rig->identity.evaluatedMatrices!=scope.sourceA||scope.sourceB!=scope.sourceA)return false;
    const auto& native=rig->evaluatedWorld;
    const auto pole=[&](interaction::ArmJoints a){const auto& e=native[a.elbow].values[3];const auto& s=native[a.shoulder].values[3];return math::Vec3{e[0]-s[0],e[1]-s[1],e[2]-s[2]};};
    auto left=native[rig->left.wrist],right=native[rig->right.wrist];left.values[3][1]+=.08f;right.values[3][1]+=.08f;
    const auto solved=interaction::SolveTrackedArms(rig->parents,native,rig->left,rig->right,{left,pole(rig->left)},{right,pole(rig->right)});
    if(!solved)return false;const auto plan=BuildRigPosePlan(*rig,solved->writes);if(!plan)return false;
    scope.before=rig->nativeEvaluated;scope.posed=scope.before;scope.count=count;
    for(const auto& edit:plan->edits)scope.posed[edit.index]=edit.after;
    // The snapshot is still the exact source about to be copied. No writes to it.
    std::vector<std::array<std::byte,64>> current(count);if(!Read(scope.sourceA,current.data(),count*64)||current!=scope.before)return false;
    scope.changed=true;return true;
}
std::uintptr_t __cdecl PackHook(const void* source,void* destination,unsigned count){
    Callback callback;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());const bool first=caller==base+profile.packCallerA,second=caller==base+profile.packCallerB;
    if(!enabled.load(std::memory_order_acquire)||(!first&&!second)||!scope.soldier||count<6||count>1024||reinterpret_cast<unsigned>(source)!=(first?scope.sourceA:scope.sourceB))return packOriginal(source,destination,count);
    unsigned weak=0;if(!resolveOwner||!resolveOwner(scope.soldier,weak)||weak!=scope.weak){++rejected;scope={};return packOriginal(source,destination,count);}
    ++copies;const auto elapsed=GetTickCount64()-started;
    if(first&&hands){
        std::shared_ptr<const PublishedPose> pose;bool previewAllowed=false;
        {std::unique_lock lock(frameMutex,std::try_to_lock);if(lock.owns_lock()&&Fresh(tracking.deadline)&&published&&published->weapon==tracking.weapon&&published->space==tracking.input.spaceGeneration&&published->ownerGeneration==tracking.ownerGeneration&&published->equipmentGeneration==tracking.equipmentGeneration){
            pose=published;previewAllowed=tracking.sightPreview.valid&&tracking.sightPreview.token==pose->previewToken&&
                Fresh(tracking.sightPreview.deadline)&&tracking.input.focused&&tracking.input.headValid&&
                tracking.input.hands[0].gripTracked&&tracking.input.hands[1].gripTracked&&tracking.input.hands[0].squeeze>.35f;
        }}
        // PublishTracking always updates this guard, even if frameMutex was
        // busy. A release/recenter/item change must invalidate a latched preview
        // without waiting for another successful pose publication.
        const auto currentGuard=shotGuard.load(std::memory_order_acquire);
        previewAllowed=previewAllowed&&currentGuard&&currentGuard->valid&&currentGuard->leftTracked&&pose&&
            currentGuard->previewToken==pose->previewToken&&currentGuard->weapon==pose->weapon&&
            currentGuard->soldier==scope.soldier&&currentGuard->weak==scope.weak&&
            currentGuard->owner==pose->ownerGeneration&&currentGuard->space==pose->space&&currentGuard->equipmentGeneration==pose->equipmentGeneration&&Fresh(currentGuard->deadline);
        if(pose&&currentGuard&&currentGuard->valid&&currentGuard->weapon==pose->weapon&&currentGuard->owner==pose->ownerGeneration&&currentGuard->space==pose->space&&currentGuard->equipmentGeneration==pose->equipmentGeneration&&currentGuard->equipment==pose->equipment&&EquipmentStillCurrent(pose->equipment)&&pose->identity.soldier==scope.soldier&&pose->identity.weak==scope.weak&&pose->identity.evaluatedMatrices==scope.sourceA&&scope.sourceA==scope.sourceB&&pose->identity.count==count&&Fresh(pose->deadline)&&AuthoredCurrent(pose->authoredGrip)&&(!pose->freeRight||FreeRightCurrent(*pose->freeRight))){
            std::vector<std::array<std::byte,64>> current(count);
            if(Read(scope.sourceA,current.data(),count*64)&&current==pose->before){
                scope.before=pose->before;scope.authoredGrip=pose->authoredGrip;scope.drawSource=pose->drawSource;scope.freeRight=pose->freeRight;scope.equipment=currentGuard->equipment;scope.equipmentGeneration=currentGuard->equipmentGeneration;
                const bool fallback=pose->previewToken&&!previewAllowed&&pose->previewBase.size()==count;
                const auto reload=reloadGuard.load(std::memory_order_acquire);
                const auto reloadNow=ReloadNanos();
                const bool reloadGuardMatches=reload&&currentGuard&&currentGuard->valid&&currentGuard->leftTracked&&
                    currentGuard->soldier==scope.soldier&&currentGuard->weak==scope.weak&&currentGuard->weapon==pose->weapon&&
                    currentGuard->owner==pose->ownerGeneration&&currentGuard->space==pose->space&&currentGuard->equipmentGeneration==pose->equipmentGeneration&&Fresh(currentGuard->deadline)&&
                    reload->inputEvidence.sequence==currentGuard->generation&&currentGuard->generation>=pose->generation;
                scope.posed=fallback?pose->previewBase:pose->posed;
                if(pose->pump){scope.pump=pose->pump;scope.pumpBase=pose->pumpBase;}
                if(pose->bolt){scope.bolt=pose->bolt;scope.boltBase=pose->boltBase;}
                if(pose->reloadPreview){
                    const auto choice=SelectReloadPackedPalette(*pose->reloadPreview,reload.get(),reloadNow,pose->reloadDeadlineNs,
                        pose->posed,pose->reloadBase,reloadGuardMatches);
                    if(choice.bytes.empty())scope.posed=pose->before;
                    else scope.posed.assign(choice.bytes.begin(),choice.bytes.end());
                    if(choice.fallback)++reloadPreviewFallbacks;
                    if(!choice.bytes.empty())scope.reloadPack=choice.fallback?3:int(pose->reloadPreview->phase);
                }
                if(pose->belt&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->visibility)scope.belt=pose->belt;
                if(pose->magazine){scope.magazine=pose->magazine;scope.magazineBase=pose->magazineBase;}
                if(pose->shellHide&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->visibility)scope.shellHide=pose->shellHide;
                if(pose->visibility&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&scope.posed==pose->visibility->ordinary){
                    const auto requested=visibilityGuard.load(std::memory_order_acquire);
                    if(requested&&WeaponVisibilityCurrent(*pose->visibility,*requested,reloadNow)&&(!reload||!reload->preview)&&currentGuard&&currentGuard->valid&&
                        !currentGuard->previewToken&&
                        currentGuard->leftTracked&&currentGuard->generation>=pose->generation&&Fresh(currentGuard->deadline)){
                        scope.visibility=pose->visibility;scope.posed=scope.visibility->privatePalette;
                    }else ++visibilityFallbacks;
                }
                if(fallback)++sightPreviewBaseFallbacks;
                if(pose->ordinaryRecovery&&!scope.visibility&&!scope.freeRight&&!scope.magazine&&!scope.shellHide&&!scope.belt&&
                   !pose->reloadPreview&&!pose->previewToken&&OrdinaryGuardCurrent(*pose->ordinaryRecovery)){
                    scope.ordinaryRecovery=pose->ordinaryRecovery;scope.ordinaryRig=pose->identity;
                    scope.ordinaryDeadlineNs=std::min(ReloadNanos(pose->deadline),pose->ordinaryRecovery->input.deadlineNs);
                }
                scope.count=count;scope.changed=true;
            }
        }
        if(!scope.changed)++poseMisses;
    }
    if(first&&pulse&&elapsed>=600&&elapsed<1800&&!MakePulse(count)){++rejected;scope.changed=false;}
    // A same-address replacement between the native pack callbacks cannot use
    // any old private pose, claim or paired receipt, even before the next Gather.
    if(scope.changed&&hands){const auto current=shotGuard.load(std::memory_order_acquire);
        if(!current||!current->valid||current->equipmentGeneration!=scope.equipmentGeneration||
           current->equipment!=scope.equipment||!EquipmentStillCurrent(scope.equipment)||!AuthoredCurrent(scope.authoredGrip)){
            scope.changed=false;scope.pump.reset();scope.pumpMask=0;scope.authoredGrip.reset();scope.visibility.reset();scope.freeRight.reset();scope.shellHide.reset();scope.magazine.reset();
            scope.belt.reset();scope.beltMask=0;scope.beltVisible=false;
            scope.visibilityMask=scope.freeRightMask=scope.shellHideMask=scope.magazineMask=0;scope.reloadPack=-1;
        }}
    // A release/equip/recenter between native Pack callbacks must restore the
    // exact ordinary palette immediately, and can never mint a paired receipt.
    if(scope.visibility){
        const auto requested=visibilityGuard.load(std::memory_order_acquire);
        const auto current=shotGuard.load(std::memory_order_acquire);
        const auto reload=reloadGuard.load(std::memory_order_acquire);
        if(!requested||!WeaponVisibilityCurrent(*scope.visibility,*requested,ReloadNanos())||!current||!current->valid||!current->leftTracked||
            current->previewToken||(reload&&reload->preview)||
            current->soldier!=scope.soldier||current->weak!=scope.weak||current->weapon!=scope.visibility->nativeOwner.weapon||
            current->owner!=scope.visibility->nativeOwner.actorGeneration||current->space!=scope.visibility->nativeOwner.space||
            current->generation<scope.visibility->inputSequence||!Fresh(current->deadline)){
            scope.posed=scope.visibility->ordinary;scope.visibility.reset();scope.visibilityMask=0;++visibilityFallbacks;
        }
    }
    // Shell suppression has its own current cycle authority, not an AmmoObject
    // claim. Revalidate before BOTH native Pack callbacks; cancellation between
    // eyes restores the exact ordinary palette and cannot mint a paired receipt.
    if(scope.shellHide){
        const auto current=shotGuard.load(std::memory_order_acquire);
        const auto reload=reloadGuard.load(std::memory_order_acquire);
        const auto& sourceControl=scope.shellHide->source;
        const bool coherent=current&&current->valid&&current->leftTracked&&reload&&
            current->soldier==scope.soldier&&current->weak==scope.weak&&current->weapon==sourceControl.owner.weapon&&
            current->owner==sourceControl.owner.actorGeneration&&current->space==sourceControl.owner.space&&
            current->generation>=sourceControl.inputEvidence.sequence&&reload->inputEvidence.sequence==current->generation&&
            !current->previewToken&&Fresh(current->deadline);
        std::vector<std::array<std::byte,64>> currentSource(count);
        if(!Read(reinterpret_cast<unsigned>(source),currentSource.data(),count*64)||currentSource!=scope.before){
            scope.changed=false;scope.shellHide.reset();scope.shellHideMask=0;scope.shellHidden=false;++reloadShellHideSourceRejects;
        }else{
            const auto chosen=SelectReloadShellPackedPalette(*scope.shellHide,reload.get(),ReloadNanos(),coherent);
            scope.shellHidden=!chosen.fallback;
            if(chosen.bytes.empty()){scope.changed=false;scope.shellHide.reset();scope.shellHideMask=0;}
            else scope.posed.assign(chosen.bytes.begin(),chosen.bytes.end());
            if(chosen.fallback){scope.shellHideMask=0;++reloadShellHideFallbacks;}
        }
    }
    // The single idle shell is only a pool handle. Check current supply, exact
    // native source and original pose deadlines before EACH native Pack.
    if(scope.belt){
        const auto shot=shotGuard.load(std::memory_order_acquire);
        const auto reload=reloadGuard.load(std::memory_order_acquire);
        const auto& old=scope.belt->source;
        const bool coherent=shot&&shot->valid&&shot->leftTracked&&!shot->weaponActionsBlocked&&reload&&reload->belt&&
            ReloadBeltCompatible(*reload,ReloadNanos())&&!shot->previewToken&&
            shot->soldier==scope.soldier&&shot->weak==scope.weak&&shot->weapon==old.owner.weapon&&
            shot->owner==old.owner.actorGeneration&&shot->space==old.owner.space&&
            shot->equipmentGeneration==old.visual.input.owner.equipGeneration&&EquipmentStillCurrent(shot->equipment)&&
            shot->generation>=old.visual.input.sequence&&shot->generation==reload->inputEvidence.sequence&&Fresh(shot->deadline);
        std::vector<std::array<std::byte,64>> actual(count);
        if(!Read(reinterpret_cast<unsigned>(source),actual.data(),count*64)||actual!=scope.before){
            scope.changed=false;scope.belt.reset();scope.beltMask=0;++beltSourceRejected;
        }else{
            const bool cycleHidden=scope.shellHide&&scope.shellHidden&&scope.belt->source.heldCycle&&
                scope.shellHide->source.shellControl&&scope.shellHide->source.shellControl->cycle==scope.belt->source.heldCycle->cycle;
            const auto fallback=scope.shellHide?std::span<const std::array<std::byte,64>>(scope.posed):
                std::span<const std::array<std::byte,64>>(scope.belt->ordinary);
            const auto choice=SelectSpasBeltOverlay(*scope.belt,reload&&reload->belt?reload->belt.get():nullptr,
                ReloadNanos(),coherent,fallback,cycleHidden);
            scope.beltVisible=!choice.fallback;
            if(choice.bytes.empty()){scope.changed=false;scope.belt.reset();scope.beltMask=0;}
            else if(choice.bytes.data()!=scope.posed.data())scope.posed.assign(choice.bytes.begin(),choice.bytes.end());
            if(choice.fallback){scope.beltMask=0;++beltFallbacks;}
        }
    }
    // A magazine's original owner/claims and source deadlines are checked at
    // each native copy. A newer packet cannot renew an older private palette.
    if(scope.magazine){
        const auto current=magazineGuard.load(std::memory_order_acquire);
        const auto shot=shotGuard.load(std::memory_order_acquire);
        const auto& old=*scope.magazine;
        const bool coherent=shot&&shot->valid&&shot->leftTracked&&current&&
            shot->soldier==scope.soldier&&shot->weak==scope.weak&&shot->weapon==old.owner.weapon&&
            shot->owner==old.owner.actorGeneration&&shot->space==old.owner.space&&
            shot->generation>=old.inputEvidence.sequence&&shot->generation==current->inputEvidence.sequence&&
            !shot->previewToken&&Fresh(shot->deadline);
        const auto observeFallback=[&](std::uint64_t flags,std::int64_t now){
            MagazineFallbackEvent e;e.flags=flags;e.nowNs=now;e.drawSerial=scope.drawSerial;
            e.source=reinterpret_cast<std::uintptr_t>(source);e.count=count;e.pack=first?1u:2u;
            e.coherent=coherent;e.original=MagazineFallbackSnapshotOf(&old);e.current=MagazineFallbackSnapshotOf(current.get());
            e.shotPresent=bool(shot);if(shot){e.shotSequence=shot->generation;e.shotDeadlineQpcTicks=shot->deadline;e.shotValid=shot->valid;e.leftTracked=shot->leftTracked;}
            else e.flags|=MagazineFallbackBit(MagazineFallbackReason::ShotMissing);
            magazineFallbackJournal.Record(e);
        };
        std::vector<std::array<std::byte,64>> actual(count);
        const bool sourceRead=Read(reinterpret_cast<unsigned>(source),actual.data(),count*64);
        if(!sourceRead||actual!=scope.before){
            observeFallback(MagazineFallbackBit(sourceRead?MagazineFallbackReason::SourceChanged:MagazineFallbackReason::SourceReadFailed),ReloadNanos());
            scope.changed=false;scope.magazine.reset();scope.magazineMask=0;++magazineFallbacks;
        }else{
            const auto now=ReloadNanos();
            const auto choice=SelectMagazinePackedPalette(old,current.get(),now,scope.posed,scope.magazineBase,coherent);
            if(choice.fallback)observeFallback(ClassifyMagazinePaletteFallback(old,current.get(),now,scope.posed,coherent),now);
            if(choice.bytes.empty())scope.changed=false;
            else if(choice.fallback)scope.posed.assign(choice.bytes.begin(),choice.bytes.end());
            if(choice.fallback){scope.magazine.reset();scope.magazineMask=0;++magazineFallbacks;}
        }
    }
    if(scope.pump){
        const auto now=ReloadNanos();const auto current=pumpGuard.load(std::memory_order_acquire);
        const auto native=reloadFlowRuntime::ReadNativeCycleView(now);const auto shot=shotGuard.load(std::memory_order_acquire);
        const bool valid=current&&native&&native->held&&shot&&shot->valid&&shot->leftTracked&&
            shot->generation==current->input.sequence&&shot->soldier==scope.soldier&&shot->weak==scope.weak&&
            shot->weapon==current->nativeOwner.weapon&&shot->owner==current->nativeOwner.actorGeneration&&
            shot->space==current->nativeOwner.space&&Fresh(shot->deadline)&&
            PumpTargetRetained(*scope.pump,*current,now)&&interaction::weapon_cycle_detail::Same(*native->held,*current->held);
        std::vector<std::array<std::byte,64>> actual(count);
        if(!Read(reinterpret_cast<unsigned>(source),actual.data(),count*64)||actual!=scope.before){scope.changed=false;scope.pump.reset();scope.pumpMask=0;++pumpSourceRejects;}
        else if(!valid){
            if(scope.pumpBase.size()==count)scope.posed=scope.pumpBase;else scope.changed=false;
            scope.pump.reset();scope.pumpMask=0;++pumpFallbacks;
        }
    }
    if(scope.bolt){
        const auto now=ReloadNanos();const auto current=boltGuard.load(std::memory_order_acquire);
        const auto native=reloadFlowRuntime::ReadNativeCycleView(now);const auto shot=shotGuard.load(std::memory_order_acquire);
        bool valid=current&&native&&shot&&shot->valid&&shot->leftTracked&&native->native.owner==current->nativeOwner&&
            shot->generation==current->input.sequence&&shot->soldier==scope.soldier&&shot->weak==scope.weak&&
            shot->weapon==current->nativeOwner.weapon&&shot->owner==current->nativeOwner.actorGeneration&&
            shot->space==current->nativeOwner.space&&Fresh(shot->deadline)&&BoltCustodyRetained(*scope.bolt,*current,now);
        if(valid&&scope.bolt->target)valid=native->held&&current->held&&interaction::weapon_cycle_detail::Same(*native->held,*current->held);
        std::vector<std::array<std::byte,64>> actual(count);
        if(!Read(reinterpret_cast<unsigned>(source),actual.data(),count*64)||actual!=scope.before){scope.changed=false;valid=false;}
        if(!valid){if(scope.boltBase.size()==count)scope.posed=scope.boltBase;else scope.changed=false;
            scope.bolt.reset();scope.boltMask=0;++boltFallbacks;}
    }
    if(scope.freeRight&&(!scope.visibility||!FreeRightCurrent(*scope.freeRight))){
        scope.posed=scope.before;scope.freeRight.reset();scope.visibility.reset();scope.visibilityMask=scope.freeRightMask=0;++freeRightFallbacks;
    }
    if(scope.ordinaryRecovery&&(!scope.changed||!OrdinaryGuardCurrent(*scope.ordinaryRecovery)||
       !EquipmentStillCurrent(scope.ordinaryRecovery->equipment)||ReloadNanos()>=scope.ordinaryDeadlineNs)){
        scope.ordinaryRecovery.reset();scope.ordinaryMask=0;
    }
    const bool changed=scope.changed&&scope.count==count&&scope.sourceA==scope.sourceB;
    const auto* input=changed?scope.posed.data():source;
    // The verified packer only reads source, copies twelve scalars per bone to
    // the native request arena, and returns. Private source memory cannot escape.
    const auto result=packOriginal(input,destination,count);
    if(changed){++changedCopies;if(second)++pairedCopies;
        std::vector<std::array<std::byte,64>> after(count);
        const bool unchanged=Read(reinterpret_cast<unsigned>(source),after.data(),count*64)&&after==scope.before;
        if(!unchanged)++sourceChanges;
        const auto afterGuard=shotGuard.load(std::memory_order_acquire);
        const bool equipmentRetained=!hands||(afterGuard&&afterGuard->valid&&afterGuard->equipmentGeneration==scope.equipmentGeneration&&
            afterGuard->equipment==scope.equipment&&EquipmentStillCurrent(scope.equipment)&&AuthoredCurrent(scope.authoredGrip));
        std::vector<std::byte> packed(count*48);bool match=equipmentRetained&&Read(reinterpret_cast<unsigned>(destination),packed.data(),packed.size());
        if(match)for(unsigned n=0;n<count&&match;++n)for(unsigned col=0;col<3&&match;++col)for(unsigned row=0;row<4;++row)
            if(std::memcmp(packed.data()+n*48+col*16+row*4,scope.posed[n].data()+row*16+col*4,4)){match=false;break;}
        if(!match)++packingFailures;
        if(match&&unchanged&&scope.authoredGrip)++authoredGripCopies;
        if(match&&unchanged&&scope.ordinaryRecovery&&OrdinaryGuardCurrent(*scope.ordinaryRecovery)&&
           EquipmentStillCurrent(scope.ordinaryRecovery->equipment)&&ReloadNanos()<scope.ordinaryDeadlineNs){
            ++ordinaryEquipmentCopies;scope.ordinaryMask|=first?1u:2u;
            if(second&&scope.ordinaryMask==3){
                const OrdinaryEquipmentPair receipt{*scope.ordinaryRecovery,scope.ordinaryRig,scope.drawSerial,
                    ReloadNanos(),scope.ordinaryDeadlineNs,scope.ordinaryMask};
                if(OrdinaryEquipmentPairCurrent(receipt,*scope.ordinaryRecovery,receipt.observedNs)){
                    try{ordinaryEquipmentPair.store(std::make_shared<const OrdinaryEquipmentPair>(receipt),std::memory_order_release);++ordinaryEquipmentPairs;}
                    catch(...){ordinaryEquipmentPair.store({},std::memory_order_release);}
                }
            }
        }else scope.ordinaryMask=0;
        if(match&&unchanged&&scope.bolt){++boltCopies;scope.boltMask|=first?1u:2u;
            if(second&&scope.boltMask==3){++boltPairs;RetainBoltPairedTarget(*scope.bolt,scope.drawSerial,ReloadNanos());}
        }else scope.boltMask=0;
        if(match&&unchanged&&scope.pump){
            ++pumpCopies;scope.pumpMask|=first?1u:2u;
            if(second&&scope.pumpMask==3)++pumpPairs;
        }else scope.pumpMask=0;
        if(match&&unchanged&&scope.magazine){
            ++magazineCopies;scope.magazineMask|=first?1u:2u;
            if(second&&scope.magazineMask==3){++magazinePairs;
                const auto current=magazineGuard.load(std::memory_order_acquire);
                if(scope.magazine->detach&&current&&current->detach&&scope.magazine->target&&
                   !scope.magazine->retainedVisualSuppression&&!current->retainedVisualSuppression){
                    const auto pair=MagazineDetachPackedPair(*scope.magazine->detach,*current->detach,
                        scope.magazine->target->role==interaction::MagazinePropRole::Attached,scope.drawSerial,scope.magazineMask,
                        match&&unchanged,ReloadNanos());
                    if(pair)magazineDetachPair.store(std::make_shared<const MagazineDetachPairReceipt>(*pair),std::memory_order_release);
                }
            }
            const auto role=scope.magazine->target?unsigned(scope.magazine->target->role):4u;
            if(role<4){++magazineRoleCopies[role];if(second&&scope.magazineMask==3)++magazineRolePairs[role];}
        }else scope.magazineMask=0;
        if(match&&unchanged&&scope.belt&&scope.beltVisible){
            ++beltCopies;scope.beltMask|=first?1u:2u;
            if(second&&scope.beltMask==3)++beltPairs;
        }else scope.beltMask=0;
        if(match&&unchanged&&scope.shellHide&&scope.shellHidden&&!scope.beltVisible){
            ++reloadShellHideCopies;scope.shellHideMask|=first?1u:2u;
            if(second&&scope.shellHideMask==3)++reloadShellHidePairs;
        }
        if(match&&unchanged&&scope.freeRight&&FreeRightCurrent(*scope.freeRight)){
            ++freeRightPackedCopies;scope.freeRightMask|=first?1u:2u;
            if(second&&scope.freeRightMask==3)++freeRightPairedCopies;
        }else scope.freeRightMask=0;
        // Diagnostic only: count a private palette only after both original
        // source preservation and actual packed destination byte comparison.
        if(match&&unchanged&&scope.reloadPack>=0&&scope.reloadPack<=3){
            if(scope.reloadPack==3){++reloadVerifiedFallbackPacks;if(second)++reloadVerifiedPairedFallbackPacks;}
            else {++reloadVerifiedPacks[scope.reloadPack];if(second)++reloadVerifiedPairedPacks[scope.reloadPack];}
        }
        if(scope.visibility){
            if(match&&unchanged){
                scope.visibilityMask|=first?1u:2u;
                if(scope.visibility->hidden)++visibilityHiddenCopies;else ++visibilityShownCopies;
                const auto requested=visibilityGuard.load(std::memory_order_acquire);const auto ns=ReloadNanos();
                const auto current=shotGuard.load(std::memory_order_acquire);
                const auto reload=reloadGuard.load(std::memory_order_acquire);
                const auto& owner=scope.visibility->nativeOwner;
                // Reacquire guards after the native copy too: its completion
                // cannot acknowledge an intent revoked while the packer ran.
                const bool receiptCurrent=requested&&WeaponVisibilityCurrent(*scope.visibility,*requested,ns)&&
                    current&&current->valid&&current->leftTracked&&!current->previewToken&&(!reload||!reload->preview)&&
                    current->soldier==owner.soldier&&current->weak==owner.weak&&current->weapon==owner.weapon&&
                    current->owner==owner.actorGeneration&&current->space==owner.space&&
                    current->generation>=scope.visibility->inputSequence&&current->equipmentGeneration==scope.equipmentGeneration&&current->equipment==scope.equipment&&Fresh(current->deadline)&&EquipmentStillCurrent(scope.equipment);
                if(!receiptCurrent){scope.visibilityMask=0;visibilityReceipt.store({},std::memory_order_release);}
                else if(scope.visibilityMask==3){
                    const auto& p=*scope.visibility;
                    try{visibilityReceipt.store(std::make_shared<const WeaponVisibilityReceipt>(WeaponVisibilityReceipt{
                        p.nativeOwner,p.rig,p.request,p.inputSequence,p.physicalEquipGeneration,scope.drawSerial,ns,
                        std::min(p.deadlineNs,requested->input.deadlineNs),3,p.hidden,scope.visibility}),std::memory_order_release);++visibilityPairedReceipts;}catch(...){}
                }
            }else {scope.visibilityMask=0;visibilityReceipt.store({},std::memory_order_release);}
        }
        if(match&&ReloadProducerBinding().Enabled()){
            LARGE_INTEGER now{},frequency{};
            if(QueryPerformanceCounter(&now)&&QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0){
                const auto ns=(now.QuadPart/frequency.QuadPart)*1000000000+(now.QuadPart%frequency.QuadPart)*1000000000/frequency.QuadPart;
                ReloadProducerBinding().ObservePacked(scope.drawSerial,first,scope.drawSource,
                    reinterpret_cast<std::uintptr_t>(destination),ns,
                    {reinterpret_cast<const std::byte*>(input),std::size_t(count)*64},packed);
            }
        }
    }
    // The loop returns its advanced source pointer in EAX. Preserve that native
    // incidental result even for the two known callers that do not inspect it.
    const auto adjusted=changed?result-reinterpret_cast<std::uintptr_t>(input)+reinterpret_cast<std::uintptr_t>(source):result;
    if(second)scope={};return adjusted;
}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t image,ResolveOwner resolver,bool enablePulse,bool enableHands){
    if(hooks[0]||!resolver)return false;const auto found=DiscoverRigConsumer(bytes,pe);if(!found)return false;
    profile=*found;base=image;resolveOwner=resolver;pulse=enablePulse;hands=enableHands;if(pulse&&hands)return false;
    for(const auto [rva,n]:{std::pair{profile.prepare,0x314u},std::pair{profile.getterA,0x13u},std::pair{profile.getterB,0x13u},std::pair{profile.pack,0xceu}}){
        const auto at=Offset(pe,rva,n);std::vector<std::byte> live(n);if(!at||!Read(unsigned(base+rva),live.data(),n)||std::memcmp(live.data(),bytes.data()+*at,n))return false;
    }
    const auto add=[&](unsigned slot,unsigned rva,void* callback,void** original){const auto address=reinterpret_cast<void*>(base+rva);if(MH_CreateHook(address,callback,original)!=MH_OK)return false;hooks[slot]=address;return true;};
    return add(0,profile.getterA,reinterpret_cast<void*>(GetA),reinterpret_cast<void**>(&getterA))&&add(1,profile.getterB,reinterpret_cast<void*>(GetB),reinterpret_cast<void**>(&getterB))&&add(2,profile.pack,reinterpret_cast<void*>(PackHook),reinterpret_cast<void**>(&packOriginal));
}
std::optional<ReloadProducerOwner> ReadReloadProducerOwner()noexcept {
    const auto guard=shotGuard.load(std::memory_order_acquire);
    if(!ReloadProducerBinding().Enabled()||!guard||!guard->valid||!Fresh(guard->deadline)||!EquipmentStillCurrent(guard->equipment))return {};
    return ReloadProducerOwner{guard->soldier,guard->weak,guard->weapon,guard->owner,guard->space};
}
std::uint64_t ReadReticleEquipmentGeneration()noexcept {
    const auto guard=shotGuard.load(std::memory_order_acquire);
    if(!ReloadProducerBinding().Enabled()||!guard||!guard->valid||!Fresh(guard->deadline)||!EquipmentStillCurrent(guard->equipment))return 0;
    return guard->equipmentGeneration;
}
void PublishNativeEye(unsigned soldier,unsigned weak,const math::Matrix4& camera)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire)||!interaction::InverseAnimatedTransform(camera))return;
    nativeEye.store(std::make_shared<const NativeEye>(NativeEye{soldier,weak,camera,GetTickCount64()}),std::memory_order_release);
}
std::optional<math::Matrix4> BuildCurrentEyeBase(const Tracking& next)noexcept {
    if(!next.deadline||!Fresh(next.deadline)||!next.input.focused||!next.input.headValid||!interaction::ValidInput(next.input))return {};
    const auto eye=nativeEye.load(std::memory_order_acquire);const auto now=GetTickCount64();
    if(!eye||eye->soldier!=next.soldier||eye->weak!=next.weak||now<eye->ms||now-eye->ms>150)return {};
    return BuildTrackedBodyBase(eye->camera,next.bodyYaw,next.input.worldUnitsPerMeter,next.actorPosition,next.consumed);
}
std::optional<math::Matrix4> ReadEyeBase(unsigned soldier,unsigned weak,std::uint64_t space)noexcept {
    const auto eye=eyeTracking.load(std::memory_order_acquire);
    if(!eye||!eye->eyeBaseValid||eye->soldier!=soldier||eye->weak!=weak||eye->input.spaceGeneration!=space||
       !Fresh(eye->deadline)||!eye->input.focused||!eye->input.headValid)return {};
    return eye->eyeBase;
}
void PublishWeaponVisibility(const WeaponVisibilityRequest& request)noexcept {
    if(!request.enabled){visibilityGuard.store({},std::memory_order_release);visibilityReceipt.store({},std::memory_order_release);return;}
    const auto old=visibilityGuard.load(std::memory_order_acquire);
    if(!old||old->request!=request.request||old->nativeOwner!=request.nativeOwner||old->hide!=request.hide||
        old->input.owner!=request.input.owner)visibilityReceipt.store({},std::memory_order_release);
    try{visibilityGuard.store(std::make_shared<const WeaponVisibilityRequest>(request),std::memory_order_release);}
    catch(...){visibilityGuard.store({},std::memory_order_release);visibilityReceipt.store({},std::memory_order_release);}
}
void PublishOrdinaryEquipment(const std::optional<OrdinaryEquipmentRequest>& request)noexcept {
    if(!request||!OrdinaryEquipmentFresh(*request,ReloadNanos())){
        ordinaryEquipmentGuard.store({},std::memory_order_release);ordinaryEquipmentPair.store({},std::memory_order_release);return;
    }
    const auto old=ordinaryEquipmentGuard.load(std::memory_order_acquire);
    if(!old||!SameOrdinaryEquipment(*old,*request))ordinaryEquipmentPair.store({},std::memory_order_release);
    try{ordinaryEquipmentGuard.store(std::make_shared<const OrdinaryEquipmentRequest>(*request),std::memory_order_release);}
    catch(...){ordinaryEquipmentGuard.store({},std::memory_order_release);ordinaryEquipmentPair.store({},std::memory_order_release);}
}
std::optional<OrdinaryEquipmentPair> ReadOrdinaryEquipmentPair(const ReloadStateOwner& owner,std::uint64_t request,std::int64_t now)noexcept {
    const auto pair=ordinaryEquipmentPair.load(std::memory_order_acquire);
    const auto guard=ordinaryEquipmentGuard.load(std::memory_order_acquire);
    if(!pair||!guard||guard->owner!=owner||guard->request!=request||!OrdinaryGuardCurrent(pair->source)||
       !EquipmentStillCurrent(guard->equipment)||!OrdinaryEquipmentPairCurrent(*pair,*guard,now))return {};
    return *pair;
}
std::optional<WeaponVisibilityReceipt> ReadWeaponVisibilityReceipt(const ReloadStateOwner& owner,std::uint64_t request,std::int64_t now)noexcept {
    const auto receipt=visibilityReceipt.load(std::memory_order_acquire);const auto guard=visibilityGuard.load(std::memory_order_acquire);
    const auto current=shotGuard.load(std::memory_order_acquire);
    const auto reload=reloadGuard.load(std::memory_order_acquire);
    if(!enabled.load(std::memory_order_acquire)||!receipt||!guard||!guard->enabled||receipt->nativeOwner!=owner||receipt->request!=request||
        receipt->verifiedCopyMask!=3||receipt->observedNs<=0||receipt->observedNs>now||receipt->deadlineNs<=now||
        guard->nativeOwner!=owner||guard->request!=request||guard->hide!=receipt->hidden||guard->input.sequence<receipt->inputSequence||
        guard->input.observedNs>now||guard->input.deadlineNs<=now||!guard->input.focused||!guard->input.tracked[0]||!guard->input.tracked[1]||
        guard->input.owner.equipGeneration!=receipt->physicalEquipGeneration||!receipt->evidence||
        !WeaponVisibilityCurrent(*receipt->evidence,*guard,now)||!current||!current->valid||!current->leftTracked||
        current->previewToken||(reload&&reload->preview)||current->generation<receipt->inputSequence||
        current->soldier!=owner.soldier||current->weak!=owner.weak||current->weapon!=owner.weapon||
        current->owner!=owner.actorGeneration||current->space!=owner.space||current->equipmentGeneration!=receipt->physicalEquipGeneration||!Fresh(current->deadline)||!EquipmentStillCurrent(current->equipment))return {};
    return *receipt;
}
std::optional<BodyAmmoRenderSource> ReadBodyAmmoRenderSource(std::int64_t now)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    try {
        // PublishTracking already validates magazine/reload members before
        // storing this immutable Tracking. Reading four separately published
        // guards can observe N/N+1 and spuriously hide otherwise-current ammo.
        // One snapshot supplies both its geometry and original source leases.
        const auto input=eyeTracking.load(std::memory_order_acquire);
        if(!input||!input->eyeBaseValid||!input->input.focused||!input->input.headValid||
            !input->input.hands[0].gripTracked||!input->input.hands[1].gripTracked||!input->input.hands[1].aimTracked||
            !interaction::ValidInput(input->input)||!Fresh(input->deadline)||
            now>=ReloadNanos(input->deadline)||!EquipmentStillCurrent(input->nativeEquipment)||input->freeRight)return {};
        BodyAmmoTracking source;
        if(input->bodyMagazine){
            if(!input->magazine.enabled)return {};
            source.magazine=input->magazine;source.visual=*input->bodyMagazine;
        }else{
            const auto& reload=input->reload;
            if(!reload.enabled||!reload.belt||reload.inputEvidence.sequence!=input->input.generation||
                !ReloadBeltCompatible(reload,now))return {};
            source.shell=*reload.belt;source.visual=reload.belt->visual;
        }
        if(!BodyAmmoFresh(source,now))return {};
        const auto& nativeOwner=source.magazine?source.magazine->owner:source.shell->owner;
        const auto& physicalOwner=source.visual.input.owner;
        if(nativeOwner.soldier!=input->soldier||nativeOwner.weak!=input->weak||nativeOwner.weapon!=input->weapon||
            nativeOwner.actorGeneration!=input->ownerGeneration||nativeOwner.space!=input->input.spaceGeneration||
            physicalOwner.actor!=((std::uint64_t(input->weak)<<32)|input->soldier)||
            physicalOwner.actorGeneration!=input->ownerGeneration||physicalOwner.equipGeneration!=input->equipmentGeneration||
            physicalOwner.space!=input->input.spaceGeneration)return {};
        if(source.magazine&&source.magazine->family.binding.profile&&
            source.magazine->family.carried&&
            source.magazine->family.binding.equipment!=input->nativeEquipment)return {};
        const auto pose=BuildBodyAmmoPose(source,input->input,input->eyeBase,now);if(!pose)return {};
        // Reject an input publication which changed during this bounded read.
        if(eyeTracking.load(std::memory_order_acquire)!=input||!enabled.load(std::memory_order_acquire)||
            !EquipmentStillCurrent(input->nativeEquipment))return {};
        BodyAmmoRenderSource result;result.prop={pose->source.visual,pose->partWorld};
        result.authority=std::make_shared<const BodyAmmoTracking>(pose->source);
        result.asset=pose->asset;result.mesh=pose->mesh;result.part=pose->part;result.rigFingerprint=pose->rigFingerprint;
        result.geometry=MakeBodyAmmoGeometryKey(pose->asset,pose->mesh,pose->part,pose->rigFingerprint);
        return result;
    }catch(...){return {};}
}
std::optional<BodyHolsteredRenderSource> ReadHolsteredBodyRenderSource(std::int64_t now)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    const auto input=eyeTracking.load(std::memory_order_acquire);
    const auto guard=shotGuard.load(std::memory_order_acquire);
    const auto free=freeRightGuard.load(std::memory_order_acquire);
    const auto visibility=visibilityGuard.load(std::memory_order_acquire);
    if(!input||!guard||!free||!visibility||!input->freeRight||input->freeRight!=free||!input->bodyHolsterSlot||
       !input->eyeBaseValid||!input->weaponActionsBlocked||!guard->valid||!guard->leftTracked||!guard->weaponActionsBlocked||
       guard->generation!=input->input.generation||guard->space!=input->input.spaceGeneration||
       guard->soldier!=input->soldier||guard->weak!=input->weak||guard->weapon!=input->weapon||
       guard->owner!=input->ownerGeneration||guard->equipmentGeneration!=input->equipmentGeneration||
       guard->equipment!=input->nativeEquipment||!Fresh(guard->deadline)||now>=ReloadNanos(input->deadline)||
       !EquipmentStillCurrent(input->nativeEquipment)||!BodyFreeRightRenderCurrent(*free,free.get(),visibility.get(),now))return {};
    const auto& owner=free->visibility.nativeOwner;const auto& physical=free->input.owner;
    if(owner.soldier!=input->soldier||owner.weak!=input->weak||owner.weapon!=input->weapon||
       owner.actorGeneration!=input->ownerGeneration||owner.space!=input->input.spaceGeneration||
       physical.actor!=((std::uint64_t(input->weak)<<32)|input->soldier)||physical.actorGeneration!=input->ownerGeneration||
       physical.equipGeneration!=input->equipmentGeneration||physical.space!=input->input.spaceGeneration)return {};
    auto result=BuildBodyHolsteredSource(free,*input->bodyHolsterSlot,input->nativeEquipment,input->input,input->eyeBase,input->bodyAnchors,now);
    if(!result)return {};
    result->prop.deadlineNs=std::min({result->prop.deadlineNs,ReloadNanos(input->deadline),ReloadNanos(guard->deadline)});
    if(eyeTracking.load(std::memory_order_acquire)!=input||shotGuard.load(std::memory_order_acquire)!=guard||
       freeRightGuard.load(std::memory_order_acquire)!=free||visibilityGuard.load(std::memory_order_acquire)!=visibility||
       !EquipmentStillCurrent(input->nativeEquipment)||!BodyHolsteredSourceFresh(*result,now))return {};
    return result;
}
std::optional<BodyCarriedRenderBatch> ReadCarriedBodyRenderSource(std::int64_t now)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    const auto input=eyeTracking.load(std::memory_order_acquire);const auto guard=shotGuard.load(std::memory_order_acquire);
    if(!input||!guard||!input->bodyInventoryDisplay||!input->eyeBaseValid||!guard->valid||
       guard->generation!=input->input.generation||guard->space!=input->input.spaceGeneration||
       guard->soldier!=input->soldier||guard->weak!=input->weak||guard->weapon!=input->weapon||
       guard->owner!=input->ownerGeneration||guard->equipmentGeneration!=input->equipmentGeneration||
       guard->equipment!=input->nativeEquipment||!Fresh(guard->deadline)||now>=ReloadNanos(input->deadline)||
       !EquipmentStillCurrent(input->nativeEquipment))return {};
    const auto& d=*input->bodyInventoryDisplay;const auto& owner=d.selectedOwner;
    if(owner.soldier!=input->soldier||owner.weak!=input->weak||owner.weapon!=input->weapon||owner.actorGeneration!=input->ownerGeneration||
       owner.space!=input->input.spaceGeneration||d.physicalOwner.equipGeneration!=input->equipmentGeneration||
       !gameplay::BodyDisplayNativeCurrent(d))return {};
    auto out=BuildBodyCarriedBatch(input->bodyInventoryDisplay,input->bodyCarriedMeshes,input->input,input->eyeBase,input->bodyAnchors,now);
    if(!out)return {};
    const auto itemsCurrent=[&](){for(unsigned n=0;n<out->count;++n){const auto e=body_carried_detail::Equipment(*out->configured[n]);
        if(!e||!EquipmentStillCurrent(*e))return false;}return true;};
    if(!itemsCurrent())return {};
    for(unsigned n=0;n<out->count;++n)out->instances[n].deadlineNs=std::min({out->instances[n].deadlineNs,ReloadNanos(input->deadline),ReloadNanos(guard->deadline)});
    if(eyeTracking.load(std::memory_order_acquire)!=input||shotGuard.load(std::memory_order_acquire)!=guard||
       !EquipmentStillCurrent(input->nativeEquipment)||!gameplay::BodyDisplayNativeCurrent(d)||!itemsCurrent()||!BodyCarriedBatchFresh(*out,ReloadNanos()))return {};
    return out;
}
void PublishTracking(const Tracking& source)noexcept {
    auto next=source;
    if(next.freeRight){const auto& p=*next.freeRight;const auto& o=p.visibility.nativeOwner;
        if(!next.weaponActionsBlocked||!BodyFreeRightEvidenceCurrent(p,ReloadNanos())||p.input.sequence!=next.input.generation||
            p.input.owner.space!=next.input.spaceGeneration||o.soldier!=next.soldier||o.weak!=next.weak||o.weapon!=next.weapon||
            o.actorGeneration!=next.ownerGeneration||p.input.deadlineNs>ReloadNanos(next.deadline)||
            !next.input.focused||!next.input.headValid||!next.input.hands[0].gripTracked||!next.input.hands[1].gripTracked)next.freeRight.reset();
    }
    if(!next.freeRight)next.bodyHolsterSlot.reset();
    if(next.bodyInventoryDisplay&&(!BodyInventoryDisplayFresh(*next.bodyInventoryDisplay,ReloadNanos())||
       next.bodyInventoryDisplay->sequence!=next.input.generation||next.bodyInventoryDisplay->physicalOwner.space!=next.input.spaceGeneration||
       next.bodyInventoryDisplay->deadlineNs>ReloadNanos(next.deadline)||!next.input.focused||!next.input.headValid)){
        next.bodyInventoryDisplay.reset();next.bodyCarriedMeshes={};
    }
    freeRightGuard.store(next.freeRight,std::memory_order_release);
    const auto& boltOwner=next.bolt.nativeOwner;
    if(!interaction::ValidInput(next.input)||!interaction::ValidInput(next.boltRawInput)||
       next.boltRawInput.generation!=next.input.generation||next.boltRawInput.spaceGeneration!=next.input.spaceGeneration||
       next.boltRawInput.predictedNs!=next.input.predictedNs||next.boltRawInput.worldUnitsPerMeter!=next.input.worldUnitsPerMeter||
       !next.boltRawInput.focused||!next.boltRawInput.headValid||!next.boltRawInput.hands[0].gripTracked||!next.boltRawInput.hands[1].gripTracked||
       next.bolt.input.sequence!=next.input.generation||
       boltOwner.soldier!=next.soldier||boltOwner.weak!=next.weak||boltOwner.weapon!=next.weapon||
       boltOwner.actorGeneration!=next.ownerGeneration||boltOwner.space!=next.input.spaceGeneration||
       next.bolt.input.deadlineNs>ReloadNanos(next.deadline)||!BoltTrackingFresh(next.bolt,ReloadNanos()))next.bolt={};
    boltGuard.store(next.bolt.enabled?std::make_shared<const Bc2BoltTracking>(next.bolt):std::shared_ptr<const Bc2BoltTracking>{},std::memory_order_release);
    const auto& pumpOwner=next.pump.nativeOwner;
    if(!interaction::ValidInput(next.input)||next.pump.input.sequence!=next.input.generation||
       pumpOwner.soldier!=next.soldier||pumpOwner.weak!=next.weak||pumpOwner.weapon!=next.weapon||
       pumpOwner.actorGeneration!=next.ownerGeneration||pumpOwner.space!=next.input.spaceGeneration||
       next.pump.input.deadlineNs>ReloadNanos(next.deadline)||!PumpTrackingFresh(next.pump,ReloadNanos()))next.pump={};
    pumpGuard.store(next.pump.enabled?std::make_shared<const Bc2PumpTracking>(next.pump):std::shared_ptr<const Bc2PumpTracking>{},std::memory_order_release);
    const auto& magazineOwner=next.magazine.owner;
    if(!interaction::ValidInput(next.input)||!next.input.focused||!next.input.headValid||
       !next.input.hands[0].gripTracked||!next.input.hands[1].gripTracked||!next.input.hands[1].aimTracked||
       magazineOwner.soldier!=next.soldier||magazineOwner.weak!=next.weak||magazineOwner.weapon!=next.weapon||
       magazineOwner.actorGeneration!=next.ownerGeneration||magazineOwner.space!=next.input.spaceGeneration||
       next.magazine.inputEvidence.sequence!=next.input.generation||
       next.magazine.inputEvidence.deadlineNs>ReloadNanos(next.deadline)||
       !MagazineTrackingFresh(next.magazine,ReloadNanos()))next.magazine={};
    magazineGuard.store(next.magazine.enabled?std::make_shared<const MagazineTracking>(next.magazine):
        std::shared_ptr<const MagazineTracking>{},std::memory_order_release);
    const auto& reloadOwner=next.reload.owner;
    if(!interaction::ValidInput(next.input)||!next.input.focused||!next.input.headValid||!next.input.hands[0].gripTracked||!next.input.hands[1].gripTracked||
       !next.input.hands[1].aimTracked||reloadOwner.soldier!=next.soldier||reloadOwner.weak!=next.weak||
       reloadOwner.weapon!=next.weapon||reloadOwner.actorGeneration!=next.ownerGeneration||reloadOwner.space!=next.input.spaceGeneration||
       next.reload.inputEvidence.sequence!=next.input.generation||next.reload.inputEvidence.deadlineNs>ReloadNanos(next.deadline)||
       !ReloadTrackingFresh(next.reload,ReloadNanos()))next.reload={};
    if(next.input.hands[0].squeeze<=.35f)next.reload.preview.reset();
    if(next.reload.belt&&(next.weaponActionsBlocked||!ReloadBeltCompatible(next.reload,ReloadNanos())))next.reload.belt.reset();
    // Always publish invalidation, including while the pose mutex is occupied.
    reloadGuard.store(next.reload.enabled?std::make_shared<const ReloadTracking>(next.reload):std::shared_ptr<const ReloadTracking>{},std::memory_order_release);
    if(const auto currentEye=BuildCurrentEyeBase(next)){next.eyeBase=*currentEye;next.eyeBaseValid=true;}
    eyeTracking.store(next.eyeBaseValid?std::make_shared<const Tracking>(next):std::shared_ptr<const Tracking>{},std::memory_order_release);
    if(!hands)return;
    const bool valid=next.deadline&&next.input.focused&&next.input.headValid&&next.input.hands[1].gripTracked&&next.input.hands[1].aimTracked&&interaction::ValidInput(next.input);
    shotGuard.store(std::make_shared<const ShotGuard>(ShotGuard{next.soldier,next.weak,next.weapon,next.ownerGeneration,next.input.spaceGeneration,next.input.generation,next.deadline,valid,next.input.hands[0].gripTracked,
        valid&&next.sightPreview.valid&&next.input.hands[0].gripTracked&&next.input.hands[0].squeeze>.35f&&Fresh(next.sightPreview.deadline)?next.sightPreview.token:0,next.weaponActionsBlocked,next.equipmentGeneration,next.nativeEquipment}),std::memory_order_release);
    if(!valid){shotPublication.store({},std::memory_order_release);PublishOrdinaryEquipment({});}
    std::unique_lock lock(frameMutex,std::try_to_lock);if(!lock.owns_lock())return;
    tracking=next;if(!next.deadline)published.reset();
}
std::optional<WeaponShotFrame> ReadWeaponFrame(unsigned soldier,unsigned weak,unsigned weapon,WeaponFrameUse use)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    // An ordinary input publication may advance while these immutable pointers
    // are read. Retry a bounded number of times; do not weaken invalidation.
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=shotGuard.load(std::memory_order_acquire);const auto pose=shotPublication.load(std::memory_order_acquire);
        if(!guard||!pose||!WeaponFrameAdmitted(*guard,pose->identity,soldier,weak,weapon,use)){++shotFrameUnavailable;return {};}
        if(!Fresh(guard->deadline)||!Fresh(pose->frame.deadline)||!EquipmentStillCurrent(guard->equipment)||!AuthoredCurrent(pose->authoredGrip)){++shotFrameExpired;return {};}
        if(shotGuard.load(std::memory_order_acquire)!=guard){++shotFrameRaces;continue;}
        if(use==WeaponFrameUse::Support&&guard->weaponActionsBlocked)++supportFramesWhileFireBlocked;
        return pose->frame;
    }
    return {};
}

std::optional<WeaponShotFrame> ReadWeaponShotFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept {return ReadWeaponFrame(soldier,weak,weapon,WeaponFrameUse::Firing);}
std::optional<WeaponShotFrame> ReadBodyWeaponFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept {return ReadWeaponFrame(soldier,weak,weapon,WeaponFrameUse::BodyObservation);}

std::optional<WeaponSupportFrame> ReadWeaponSupportFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept {
    const auto pose=ReadWeaponFrame(soldier,weak,weapon,WeaponFrameUse::Support);
    if(!pose)return {};
    return WeaponSupportFrame{pose->support,pose->generation,pose->deadline,pose->ownerGeneration,pose->space,pose->authoredSupportReference};
}

MagazineRawContact ReadMagazineContact(const ReloadStateOwner& owner)noexcept {
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=magazineGuard.load(std::memory_order_acquire);const auto now=ReloadNanos();
        if(!guard||guard->owner!=owner||!MagazineTrackingFresh(*guard,now))return {};
        // Suppressed Fire does not suppress raw reload contact. This getter is
        // private to magazine interaction and never grants a firing pose.
        const auto shot=ReadWeaponFrame(owner.soldier,owner.weak,owner.weapon,WeaponFrameUse::BodyObservation);
        if(!shot||!shot->magazine.valid||shot->magazine.owner!=owner||
           shot->magazine.inputEvidence.sequence!=shot->generation||
           shot->magazine.inputEvidence.sequence>guard->inputEvidence.sequence||
           shot->magazine.inputEvidence.observedNs>now||shot->magazine.inputEvidence.deadlineNs<=now)return {};
        if(magazineGuard.load(std::memory_order_acquire)!=guard)continue;
        return shot->magazine;
    }return {};
}
Bc2PumpPackCounters ReadPumpPackCounters()noexcept {
    return {pumpContacts.load(),pumpPoses.load(),pumpCopies.load(),pumpPairs.load(),pumpFallbacks.load()};
}
Bc2BoltPackCounters ReadBoltPackCounters()noexcept {
    return {boltContacts.load(),boltPoses.load(),boltCopies.load(),boltPairs.load(),boltFallbacks.load()};
}
Bc2BoltControllerContact ReadBoltContact(const ReloadStateOwner& owner)noexcept {
    const auto shot=ReadBodyWeaponFrame(owner.soldier,owner.weak,owner.weapon);
    if(!shot||shot->bolt.raw.nativeOwner!=owner||!shot->bolt.raw.valid||!shot->bolt.mappingValid||
       !interaction::weapon_cycle_detail::Window(shot->bolt.raw.input.observedNs,shot->bolt.raw.input.deadlineNs,ReloadNanos()))return {};
    return shot->bolt;
}
Bc2PumpRawContact ReadPumpContact(const ReloadStateOwner& owner)noexcept {
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=pumpGuard.load(std::memory_order_acquire);const auto now=ReloadNanos();
        if(!guard||guard->nativeOwner!=owner||!PumpTrackingFresh(*guard,now))return {};
        const auto shot=ReadWeaponFrame(owner.soldier,owner.weak,owner.weapon,WeaponFrameUse::BodyObservation);
        if(!shot||!shot->pump.valid||shot->pump.nativeOwner!=owner||shot->pump.input.sequence!=shot->generation||
           shot->pump.input.sequence>guard->input.sequence||
           !interaction::weapon_cycle_detail::Window(shot->pump.input.observedNs,shot->pump.input.deadlineNs,now))return {};
        if(pumpGuard.load(std::memory_order_acquire)==guard)return shot->pump;
    }return {};
}
ReloadRawContact ReadReloadContact(const ReloadStateOwner& owner)noexcept {
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=reloadGuard.load(std::memory_order_acquire);const auto now=ReloadNanos();
        if(!guard||guard->owner!=owner||!ReloadTrackingFresh(*guard,now))return {};
        const auto shot=ReadWeaponShotFrame(owner.soldier,owner.weak,owner.weapon);
        if(!shot||!shot->reload.valid||shot->reload.owner!=owner||shot->reload.inputEvidence.sequence!=shot->generation||
           shot->reload.inputEvidence.sequence>guard->inputEvidence.sequence||shot->reload.inputEvidence.observedNs>now||
           shot->reload.inputEvidence.deadlineNs<=now)return {};
        if(reloadGuard.load(std::memory_order_acquire)!=guard)continue;
        return shot->reload;
    }return {};
}
interaction::SupportGripContact ReadSupportContact(unsigned soldier,unsigned weak,unsigned weapon,std::uint64_t owner,std::uint64_t space)noexcept {
    const auto pose=ReadWeaponSupportFrame(soldier,weak,weapon);
    return pose&&pose->ownerGeneration==owner&&pose->space==space?pose->support:interaction::SupportGripContact{};
}
WeaponSightContact ReadSightContact(unsigned soldier,unsigned weak,unsigned weapon,std::uint64_t owner,std::uint64_t space)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return {};
    for(unsigned attempt=0;attempt<3;++attempt){
        const auto guard=shotGuard.load(std::memory_order_acquire);const auto pose=shotPublication.load(std::memory_order_acquire);
        if(!guard||!guard->valid||guard->weaponActionsBlocked||!guard->leftTracked||!pose||!pose->frame.sight.valid||!pose->identity.leftTracked||
           guard->soldier!=soldier||guard->weak!=weak||guard->weapon!=weapon||guard->owner!=owner||guard->space!=space||
           pose->identity.soldier!=soldier||pose->identity.weak!=weak||pose->identity.weapon!=weapon||
           pose->identity.owner!=owner||pose->identity.space!=space||pose->identity.equipmentGeneration!=guard->equipmentGeneration||pose->identity.generation>guard->generation||
           pose->frame.sight.generation!=pose->identity.generation||pose->frame.sight.deadline!=pose->frame.deadline||
           !Fresh(guard->deadline)||!Fresh(pose->frame.sight.deadline)||!AuthoredCurrent(pose->authoredGrip))return {};
        if(shotGuard.load(std::memory_order_acquire)!=guard||!EquipmentStillCurrent(guard->equipment))continue;
        return pose->frame.sight;
    }
    return {};
}
void RetargetWeapon(unsigned animation,void* destination)noexcept {
    if(!hands||!enabled.load(std::memory_order_acquire))return;Callback callback;
    Tracking input;
    {std::unique_lock lock(frameMutex,std::try_to_lock);if(!lock.owns_lock()||!Fresh(tracking.deadline))return;input=tracking;
        if(!input.input.focused||!input.input.headValid||(!input.input.hands[0].gripTracked&&!input.input.hands[1].gripTracked)){++trackingUnavailable;published.reset();return;}
    }
    unsigned weak=0;if(!resolveOwner||!resolveOwner(input.soldier,weak)||weak!=input.weak){RejectTracking(0);return;}
    // The producer already reflected this exact SoldierWeaponData. Here require
    // its unchanged pointer and coherent immutable asset/persistence snapshot.
    const WeaponModeMemory equipmentMemory{&input.nativeEquipment,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);},
        [](void* context,unsigned at,const char*){return static_cast<const WeaponEquipmentIdentity*>(context)->data==at;}};
    const auto currentEquipment=ReadWeaponEquipmentIdentity(equipmentMemory,input.weapon);
    if(!currentEquipment||*currentEquipment!=input.nativeEquipment){RejectTracking(0);return;}
    const RigMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Read(at,out,n);}};
    const auto m95Phase=m95ShotCapturePhase.load(std::memory_order_acquire);
    const auto m95Before=m95Phase>=1&&m95Phase<=3?reloadFlowRuntime::ReadM95ShotPartDiagnosticSnapshot():std::nullopt;
    const auto pumpBefore=reloadFlowRuntime::ReadPumpPartDiagnosticSnapshot();
    const auto rig=ReadFirstPersonRig(memory,input.soldier,input.weak);
    const auto m95After=m95Before?reloadFlowRuntime::ReadM95ShotPartDiagnosticSnapshot():std::nullopt;
    const auto pumpAfter=pumpBefore?reloadFlowRuntime::ReadPumpPartDiagnosticSnapshot():std::nullopt;
    if(!rig||rig->identity.animation!=animation){RejectTracking(1);return;}
    std::array<std::byte,64> original{};const auto at=reinterpret_cast<unsigned>(destination);if(!Read(at,original.data(),64)){RejectTracking(2);return;}
    math::Matrix4 weapon{};std::memcpy(&weapon,original.data(),64);
    for(unsigned row=0;row<4;++row)weapon.values[row][3]=row==3?1.f:0.f;
    for(unsigned n=0;n<4;++n){weapon.values[2][n]=-weapon.values[2][n];weapon.values[n][2]=-weapon.values[n][2];}
    // The verified native eye height and collision actor define the same
    // compensated reference origin used by HMD views.
    auto body=input.eyeBaseValid?std::optional<math::Matrix4>{input.eyeBase}:std::nullopt;
    if(!body){RejectTracking(3);return;}
    std::unique_lock lock(frameMutex,std::try_to_lock);
    if(!lock.owns_lock()||!Fresh(input.deadline)||tracking.soldier!=input.soldier||tracking.weak!=input.weak||tracking.weapon!=input.weapon||tracking.ownerGeneration!=input.ownerGeneration||tracking.equipmentGeneration!=input.equipmentGeneration||tracking.nativeEquipment!=input.nativeEquipment||tracking.input.spaceGeneration!=input.input.spaceGeneration)return;
    const interaction::TrackedRigOwner owner{(std::uint64_t(input.weak)<<32)|input.soldier,input.ownerGeneration,input.weapon,rig->identity.skeleton,input.equipmentGeneration};
    const auto animatedBody=*body;
    const auto& native=rig->evaluatedWorld;
    if(pumpBefore&&pumpAfter&&pumpBefore->identity.owner.soldier==input.soldier&&pumpBefore->identity.owner.weak==input.weak&&
       pumpBefore->identity.owner.weapon==input.weapon&&pumpBefore->identity.owner.actorGeneration==input.ownerGeneration&&
       pumpBefore->identity.owner.space==input.input.spaceGeneration&&pumpBefore->config.weaponData==input.nativeEquipment.data){
        const auto now=ReloadNanos();const auto selected=gameplay::ReadSelectedMeshes(pumpBefore->identity.owner,now);
        if(selected&&EquipmentStillCurrent(input.nativeEquipment))pumpPartCapture.Observe(*pumpBefore,*pumpAfter,*rig,*selected,
            input.input.generation,0,ReloadNanos(input.deadline),input.input.worldUnitsPerMeter,now);
    }
    if(m95Before&&m95After&&m95ShotCapturePhase.load(std::memory_order_acquire)==m95Phase&&
       m95Before->identity.owner.soldier==input.soldier&&m95Before->identity.owner.weak==input.weak&&
       m95Before->identity.owner.weapon==input.weapon&&m95Before->identity.owner.actorGeneration==input.ownerGeneration&&
       m95Before->identity.owner.space==input.input.spaceGeneration&&m95Before->config.weaponData==input.nativeEquipment.data){
        const auto now=ReloadNanos();const auto selected=gameplay::ReadSelectedMeshes(m95Before->identity.owner,now);
        if(selected&&EquipmentStillCurrent(input.nativeEquipment))m95ShotPartCapture.Observe(*m95Before,*m95After,*rig,*selected,
            input.input.generation,0,ReloadNanos(input.deadline),input.input.worldUnitsPerMeter,now,m95Phase);
    }
    const auto captureNow=GetTickCount64();
    if(input.assetName[0]&&m95Phase<4&&captureNow-captureTime>=100){
        captureTime=captureNow;
        WeaponCaptureSample sample;sample.assetName=input.assetName.data();
        sample.provenance=WeaponCaptureProvenance{input.equipmentGeneration,input.nativeEquipment.data,input.nativeEquipment.persistence,rig->identity};
        sample.unitsPerMeter=input.input.worldUnitsPerMeter;sample.generation=input.input.generation;sample.predictedNs=input.input.predictedNs;sample.capturedMs=captureNow;
        sample.space=input.input.spaceGeneration;sample.ownerGeneration=input.ownerGeneration;sample.actor=input.soldier;sample.weapon=input.weapon;
        sample.nativeWeapon=native[rig->weaponBone];sample.nativeLeft=native[rig->left.wrist];sample.nativeRight=native[rig->right.wrist];
        sample.rootName=rig->names[rig->weaponBone];sample.leftName=rig->names[rig->left.wrist];sample.rightName=rig->names[rig->right.wrist];
        std::uint64_t fingerprint=14695981039346656037ull;
        const auto hashWord=[&](std::uint32_t word){for(unsigned n=0;n<4;++n){fingerprint^=(word>>(n*8))&255;fingerprint*=1099511628211ull;}};
        for(unsigned n=0;n<rig->names.size();++n){
            for(unsigned char c:rig->names[n]){fingerprint^=c;fingerprint*=1099511628211ull;}
            hashWord(0);hashWord(std::uint32_t(rig->parents[n]));
            for(const auto& row:rig->inverseBind[n].values)for(float v:row)hashWord(std::bit_cast<std::uint32_t>(v));
            bool weaponChild=n==rig->weaponBone;
            for(auto at=rig->parents[n];at!=-1&&!weaponChild;at=rig->parents[at])weaponChild=unsigned(at)==rig->weaponBone;
            if(weaponChild){
                if(sample.nativeWeaponBones.size()<WeaponCapture::MaxWeaponBones){
                    WeaponBoneCapture bone;bone.name=rig->names[n];if(rig->parents[n]!=-1)bone.parentName=rig->names[rig->parents[n]];
                    bone.native=native[n];bone.inverseBind=rig->inverseBind[n];bone.hidden=std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)!=rig->nativeHiddenLeaves.end();
                    sample.nativeWeaponBones.push_back(std::move(bone));
                }else ++sample.weaponBonesDropped;
            }
            // Retain authored finger motion alongside the weapon component
            // poses, from this same snapshot BEFORE VR hand/IK edits below.
            bool leftHandChild=n==rig->left.wrist;
            for(auto at=rig->parents[n];at!=-1&&!leftHandChild;at=rig->parents[at])leftHandChild=unsigned(at)==rig->left.wrist;
            if(leftHandChild){
                if(sample.nativeLeftHandBones.size()<WeaponCapture::MaxLeftHandBones){
                    WeaponBoneCapture bone;bone.name=rig->names[n];if(rig->parents[n]!=-1)bone.parentName=rig->names[rig->parents[n]];
                    bone.native=native[n];bone.inverseBind=rig->inverseBind[n];
                    bone.hidden=std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)!=rig->nativeHiddenLeaves.end();
                    sample.nativeLeftHandBones.push_back(std::move(bone));
                }else ++sample.leftHandBonesDropped;
            }
            if(rig->names[n]=="jntWpn_Flash"){
                bool descendant=false;for(auto at=rig->parents[n];at!=-1;at=rig->parents[at])if(unsigned(at)==rig->weaponBone){descendant=true;break;}
                if(descendant&&interaction::InverseAnimatedTransform(native[n])){sample.muzzleName=rig->names[n];sample.nativeMuzzle=native[n];}
            }
        }
        sample.weaponBonesComplete=sample.weaponBonesDropped==0;
        sample.leftHandBonesCaptured=true;sample.leftHandBonesComplete=sample.leftHandBonesDropped==0;
        std::ostringstream identity;identity<<"fnv1a64:"<<std::hex<<fingerprint;sample.skeleton=identity.str();
        weaponCapture.Observe(std::move(sample));
    }
    const auto point=[](const math::Matrix4& m){return math::Vec3{m.values[3][0],m.values[3][1],m.values[3][2]};};
    // Physical torso translation follows the tracked head, independently of
    // native collider lag and its allowed lean radius. Hands already contain
    // the same physical travel. Using only collider motion strands shoulders
    // during a step/crouch even though the controller-to-head pose is unchanged.
    // Physical heading rotates anatomy only. Hands/weapon/aim retain the shared
    // tracking base, independent of HMD yaw (no double rotation).
    const auto torso=interaction::PhysicalTorsoFrame(input.input,*body);
    if(!torso){RejectTracking(4);published.reset();return;}const auto& anatomy=*torso;
    // Stabilize the shared torso parent as well as arm branches. Otherwise the
    // native gun animation still drags clavicle/shoulder vertices between arms.
    unsigned torsoRoot=rig->parents.size();
    for(auto p=rig->parents[rig->left.shoulder];p!=-1&&torsoRoot==rig->parents.size();p=rig->parents[p])
        for(auto q=rig->parents[rig->right.shoulder];q!=-1;q=rig->parents[q])if(p==q){torsoRoot=unsigned(p);break;}
    bool weaponBelowTorso=false;
    for(auto p=std::int32_t(rig->weaponBone);p!=-1;p=rig->parents[p])if(unsigned(p)==torsoRoot)weaponBelowTorso=true;
    const bool stabilizeTorso=torsoRoot<rig->parents.size()&&torsoRoot!=0&&!weaponBelowTorso&&input.input.hands[0].gripTracked&&input.input.hands[1].gripTracked;
    const auto headBone=std::find(rig->names.begin(),rig->names.end(),"Head");
    if(headBone==rig->names.end()){RejectTracking(5);return;}
    const auto bindHead=interaction::InverseAnimatedTransform(rig->inverseBind[headBone-rig->names.begin()]);
    const auto bindLeft=interaction::InverseAnimatedTransform(rig->inverseBind[rig->left.shoulder]);
    const auto bindRight=interaction::InverseAnimatedTransform(rig->inverseBind[rig->right.shoulder]);
    if(!bindHead||!bindLeft||!bindRight){RejectTracking(6);return;}
    const auto bindFrame=interaction::BindAnatomyFrame(point(*bindHead),point(*bindLeft),point(*bindRight));
    if(!bindFrame){RejectTracking(7);return;}const auto inverseBindFrame=interaction::InverseRigid(*bindFrame);
    if(!inverseBindFrame){RejectTracking(8);return;}
    const auto bindToBody=interaction::Multiply(*inverseBindFrame,anatomy);
    const auto restAnchor=[&](interaction::ArmJoints arm)->std::optional<interaction::ArmAnchor>{
        const auto shoulder=interaction::InverseAnimatedTransform(rig->inverseBind[arm.shoulder]),elbow=interaction::InverseAnimatedTransform(rig->inverseBind[arm.elbow]);
        if(!shoulder||!elbow)return {};
        const auto s=point(interaction::Multiply(*shoulder,bindToBody)),e=point(interaction::Multiply(*elbow,bindToBody));
        return interaction::ArmAnchor{s,{e.x-s.x,e.y-s.y,e.z-s.z}};
    };
    const auto leftAnchor=restAnchor(rig->left),rightAnchor=restAnchor(rig->right);
    if(!leftAnchor||!rightAnchor){RejectTracking(9);return;}
    std::optional<math::Matrix4> restTorso;
    if(stabilizeTorso){const auto bind=interaction::InverseAnimatedTransform(rig->inverseBind[torsoRoot]);if(!bind){RejectTracking(10);return;}restTorso=interaction::Multiply(*bind,bindToBody);}
    std::optional<math::Matrix4> weaponOrientation;
    if(input.weaponProfile&&interaction::WeaponFeatureEnabled(&input.weaponProfile->core,interaction::WeaponFeature::AimAlignment)&&input.input.hands[1].gripTracked&&input.input.hands[1].aimTracked){
        weaponOrientation=interaction::TrackedAimFrame(input.input,*body);
        if(!weaponOrientation){RejectTracking(11);return;}
        // Axis conversion belongs to the verified adapter profile; every item
        // uses the same XR aim and authored attachment pipeline.
        weaponOrientation=interaction::WeaponAimFrame(input.weaponProfile->core,*weaponOrientation);
        if(!weaponOrientation){RejectTracking(11);return;}
    }
    const auto rigFingerprint=SightRigFingerprint(rig->names,rig->parents,rig->inverseBind);
    constexpr bool selectAcceptedAuthored=
#ifdef FVR_BC2_AUTHORED_GRIP_ACCEPTED_BASELINES
        true;
#else
        false;
#endif
    const auto authoredNow=ReloadNanos();
    const auto authored=BindAuthoredGrip(AuthoredGripProfiles(),gameplay::ReadCurrentSelectedMeshes(authoredNow),
        input.nativeEquipment,{input.soldier,input.weak,input.ownerGeneration,input.equipmentGeneration,
        input.input.spaceGeneration,input.input.generation,ReloadNanos(input.deadline)},rigFingerprint,
        authoredNow,FindWeaponProfile(input.nativeEquipment.Asset())!=nullptr,selectAcceptedAuthored);
    ++authoredGripStatus[unsigned(authored.status)];
    if(authored.status!=AuthoredGripStatus::NoProfile&&authored.status!=AuthoredGripStatus::PreservedBaseline&&!authored.binding){
        // A known generated entry with missing/expired identity restores the
        // current ordinary output/palette. Never reuse a previous static pose.
        published.reset();shotPublication.store({},std::memory_order_release);return;
    }
    if(authored.binding&&!authored.binding->profile->authoredAxisEvidence.empty()&&
       input.input.hands[1].gripTracked&&input.input.hands[1].aimTracked){
        const auto aim=interaction::TrackedAimFrame(input.input,*body);
        weaponOrientation=aim?AuthoredWeaponAimFrame(*authored.binding,*aim):std::nullopt;
        if(!weaponOrientation){RejectTracking(11);published.reset();shotPublication.store({},std::memory_order_release);return;}
    }
    const auto explicitGrip=authored.binding?std::optional<interaction::AuthoredGripAttachment>{{authored.binding->profile->rightInWeapon}}:std::nullopt;
    const auto targets=calibratedRig.Update(owner,input.input,*body,native[rig->left.wrist],native[rig->right.wrist],weapon,{*leftAnchor,*rightAnchor},anatomy,restTorso,weaponOrientation,explicitGrip);
    if(!targets){RejectTracking(11);published.reset();return;}
    if(rigFingerprint!=handBindingFingerprint||input.input.worldUnitsPerMeter!=handBindingUnits){
        handBinding=BindLeftHandPose(rig->names,rig->parents,rig->inverseBind,input.input.worldUnitsPerMeter);
        rightHandBinding=BindRightHandPose(rig->names,rig->parents,rig->inverseBind,input.input.worldUnitsPerMeter);
        handBindingFingerprint=rigFingerprint;handBindingUnits=input.input.worldUnitsPerMeter;
    }
    // Free/mechanism wrists and raw interaction contact share one anatomical
    // controller mapping. Native support calibration stays independent.
    auto freeLeft=targets->left;math::Matrix4 leftGripWorld{};bool handRoleReady=false;
    if(handBinding&&targets->tracked[0]){
        auto relative=math::MakeRelativePose(input.input.referenceHead,input.input.hands[0].grip);
        if(relative){
            relative->position.x*=input.input.worldUnitsPerMeter;relative->position.y*=input.input.worldUnitsPerMeter;relative->position.z*=input.input.worldUnitsPerMeter;
            if(const auto view=math::MakeLhViewFromOpenXRPose(*relative))if(const auto grip=interaction::InverseRigid(*view)){
                leftGripWorld=interaction::Multiply(*grip,*body);
                freeLeft=interaction::Multiply(handBinding->wristToGrip,leftGripWorld);
                // Existing absolute wrist position includes the roomscale mapping.
                freeLeft.values[3]=targets->left.values[3];
                handRoleReady=bool(interaction::InverseAnimatedTransform(freeLeft));
            }
        }
    }
    auto rawRight=targets->right;bool boltRightReady=false;
    if(input.bolt.enabled&&rightHandBinding&&targets->tracked[1]){
        if(auto relative=math::MakeRelativePose(input.boltRawInput.referenceHead,input.boltRawInput.hands[1].grip)){
            relative->position.x*=input.input.worldUnitsPerMeter;relative->position.y*=input.input.worldUnitsPerMeter;relative->position.z*=input.input.worldUnitsPerMeter;
            if(const auto view=math::MakeLhViewFromOpenXRPose(*relative))if(const auto grip=interaction::InverseRigid(*view)){
                rawRight=interaction::Multiply(rightHandBinding->wristToGrip,interaction::Multiply(*grip,*body));
                rawRight.values[3]=targets->right.values[3];boltRightReady=bool(interaction::InverseRigid(rawRight));
            }
        }
    }
    const bool boltCustody=boltRightReady&&handRoleReady&&BoltCustodyFresh(input.bolt,ReloadNanos());
    auto rightTarget=targets->right;bool freeRightReady=false;
    if(input.freeRight&&rightHandBinding&&targets->tracked[1]&&FreeRightCurrent(*input.freeRight)){
        if(auto relative=math::MakeRelativePose(input.input.referenceHead,input.input.hands[1].grip)){
            relative->position.x*=input.input.worldUnitsPerMeter;relative->position.y*=input.input.worldUnitsPerMeter;relative->position.z*=input.input.worldUnitsPerMeter;
            if(const auto view=math::MakeLhViewFromOpenXRPose(*relative))if(const auto grip=interaction::InverseRigid(*view)){
                const auto rawGrip=interaction::Multiply(*grip,*body);
                if(const auto target=HolsterRightTarget(*input.freeRight,ReloadNanos(),*rightHandBinding,rawGrip,targets->right)){
                    rightTarget=*target;freeRightReady=true;
                }
            }
        }
        if(!freeRightReady){RejectTracking(13);published.reset();return;}
    }
    if(boltCustody&&input.bolt.custody==interaction::BoltCustodyPhase::Manipulating)rightTarget=rawRight;
    // Native aiming rotates BOTH shoulders with the gun. Keeping those origins
    // would force the stationary off-hand to chase the gun at the reach limit.
    // Use calibrated body anchors, while preserving current native limb lengths,
    // finger animation and all original source buffers.
    auto armNative=native;std::vector<interaction::BoneWrite> torsoWrites;
    if(stabilizeTorso&&targets->torso&&targets->tracked[0]&&targets->tracked[1]){
        const auto posed=interaction::RetargetRigSubtree(rig->parents,native,torsoRoot,*targets->torso);
        if(!posed){RejectTracking(12);return;}torsoWrites=*posed;
        for(const auto& write:torsoWrites)armNative[write.index]=write.transform;++torsoPoses;
    }
    auto solved=interaction::SolveTrackedArms(rig->parents,armNative,rig->left,rig->right,
        {freeLeft,targets->arms[0].poleDirection,targets->arms[0].shoulder,targets->tracked[0]},
        {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,targets->tracked[1]});
    if(!solved){RejectTracking(13);published.reset();return;}
    auto placed=targets->weapon;
    // If a wrist hits arm reach, move the gun with that resolved wrist too.
    if(!boltCustody)for(const auto& write:solved->writes)if(write.index==rig->right.wrist){const auto inverse=interaction::InverseAnimatedTransform(targets->right);if(!inverse)return;placed=interaction::Multiply(interaction::Multiply(placed,*inverse),write.transform);break;}
    if(boltCustody){
        placed=input.bolt.weapon->weaponInWorld;
        for(unsigned n=0;n<3;++n)placed.values[3][n]*=input.input.worldUnitsPerMeter;
        if(solved->reachClamped[0]||solved->targetError[0]>.002f*input.input.worldUnitsPerMeter){published.reset();return;}
    }
    Bc2BoltControllerContact boltContact;
    if(boltRightReady&&handRoleReady&&rightHandBinding&&handBinding){
        boltContact.raw=BuildBoltRawContact(input.bolt,*rig,rawRight,placed,input.input.worldUnitsPerMeter,ReloadNanos());
        const auto inverseNativeWeapon=interaction::InverseRigid(native[rig->weaponBone]);
        if(boltContact.raw.valid&&inverseNativeWeapon){
            boltContact.bodyWorldMeters=*body;boltContact.weaponWorldMeters=placed;
            boltContact.rawWristWorldMeters={freeLeft,rawRight};boltContact.wristToGrip={handBinding->wristToGrip,rightHandBinding->wristToGrip};
            boltContact.nativeWristInWeapon={interaction::Multiply(native[rig->left.wrist],*inverseNativeWeapon),interaction::Multiply(native[rig->right.wrist],*inverseNativeWeapon)};
            for(auto* matrix:{&boltContact.bodyWorldMeters,&boltContact.weaponWorldMeters,&boltContact.rawWristWorldMeters[0],&boltContact.rawWristWorldMeters[1],
                &boltContact.nativeWristInWeapon[0],&boltContact.nativeWristInWeapon[1]})
                for(unsigned n=0;n<3;++n)matrix->values[3][n]/=input.input.worldUnitsPerMeter;
            boltContact.units=input.input.worldUnitsPerMeter;
            const auto part=DeriveBoltPart(*rig,input.bolt.calibration->partName);
            if(part&&part->part<native.size()){
                boltContact.nativePartInWeapon=interaction::Multiply(native[part->part],*inverseNativeWeapon);
                for(unsigned axis=0;axis<3;++axis)boltContact.nativePartInWeapon.values[3][axis]/=input.input.worldUnitsPerMeter;
                boltContact.nativePartValid=interaction::feed_mechanism_detail::Pose(boltContact.nativePartInWeapon);
            }
            boltContact.mappingValid=true;++boltContacts;
        }
    }
    // Contact and carried ammunition use the SAME anatomical controller wrist
    // as the free hand. targets->left retains the old native support calibration;
    // switching to it on pickup can invert the wrist despite unchanged tracking.
    // No support attachment, guided insertion or solved wrist feeds contact.
    const auto reloadContact=handRoleReady?
        BuildReloadRawContact(input.reload,*rig,input.assetName.data(),freeLeft,placed,input.input.worldUnitsPerMeter,ReloadNanos(),&*body):ReloadRawContact{};
    const auto magazineContact=handRoleReady?
        BuildMagazineRawContact(input.magazine,*rig,input.assetName.data(),freeLeft,placed,input.input.worldUnitsPerMeter,ReloadNanos(),&*body):MagazineRawContact{};
    const auto pumpContact=handRoleReady?BuildPumpRawContact(input.pump,*rig,freeLeft,placed,
        handBinding->mechanismPointWristMeters,input.input.worldUnitsPerMeter,ReloadNanos(),&*body):Bc2PumpRawContact{};
    if(pumpContact.valid)++pumpContacts;
    auto leftTarget=freeLeft;bool supportAttached=false;
    const auto inverseSupportItem=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
    if(!inverseSupportItem){RejectTracking(13);return;}
    const AuthoredSupportProfile* measuredSupport=nullptr;
#ifdef FVR_BC2_AUTHORED_SUPPORT_HEADER
    const auto supportSnapshot=gameplay::ReadCurrentSelectedMeshes(authoredNow);
    if(supportSnapshot)measuredSupport=BindAuthoredSupport(generated::AuthoredSupports,*supportSnapshot,input.nativeEquipment,
        {input.soldier,input.weak,input.ownerGeneration,input.equipmentGeneration,input.input.spaceGeneration,input.input.generation,ReloadNanos(input.deadline)},rigFingerprint,authoredNow);
#endif
    auto authoredSupportLocal=authored.binding?AuthoredGripWorldUnits(authored.binding->profile->leftInWeapon,input.input.worldUnitsPerMeter):
        measuredSupport?AuthoredGripWorldUnits(measuredSupport->leftInWeapon,input.input.worldUnitsPerMeter):interaction::Multiply(native[rig->left.wrist],*inverseSupportItem);
    if(const auto returned=ResolvePumpSupportWrist(input.pump,*rig,input.supportToken,ReloadNanos())){
        authoredSupportLocal=*returned;for(unsigned n=0;n<3;++n)authoredSupportLocal.values[3][n]*=input.input.worldUnitsPerMeter;
    }
    const auto fixedSupportLocal=supportAttachment.Update(
        {owner.actor,owner.generation,owner.equipped,owner.skeleton,input.input.spaceGeneration},
        input.supportHolding&&targets->tracked[0]&&targets->tracked[1]?input.supportToken:0,authoredSupportLocal);
    // Attach only the published hand while explicit grip is held. Contact and
    // steering below still use the independent raw target, so pulling away or
    // releasing cannot leave a self-sustaining grab.
    if(!boltCustody&&input.supportHolding&&fixedSupportLocal&&targets->tracked[0]&&targets->tracked[1]){
        const auto inverseWeapon=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
        if(!inverseWeapon){RejectTracking(13);return;}
        leftTarget=interaction::Multiply(*fixedSupportLocal,placed);
        solved=interaction::SolveTrackedArms(rig->parents,armNative,rig->left,rig->right,
            {leftTarget,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
            {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,true});
        if(!solved){RejectTracking(13);published.reset();return;}
        supportAttached=true;
    }
    // The first-person gun geometry shares this rig's skin palette; changing
    // only the attachment getter moves effects/root consumers, not those verts.
    auto writes=torsoWrites;
    const auto merge=[&](const auto& added){for(const auto& write:added){
        const auto found=std::find_if(writes.begin(),writes.end(),[&](const auto& prior){return prior.index==write.index;});
        if(found==writes.end())writes.push_back(write);else *found=write;
    }};
    merge(solved->writes);
    auto handRole=interaction::HandPoseRole::Free;
    const auto poseHand=[&](interaction::HandPoseRole role,const auto& armWrites)->bool{
        if(!handRoleReady)return false;
        const auto wrist=std::find_if(armWrites.begin(),armWrites.end(),[&](const auto& w){return w.index==rig->left.wrist;});
        if(wrist==armWrites.end())return false;
        const auto desired=interaction::ApplyHandTouch(LeftHandTargets(role,input.input.hands[0].squeeze,input.input.hands[0].trigger),input.input.hands[0]);
        const auto hand=interaction::GenerateHandPose(rig->parents,handBinding->referenceWorld,native,handBinding->pose,wrist->transform,desired);
        if(!hand){++handPoseRejected;return false;}
        merge(hand->writes);return true;
    };
    bool handPosed=false;
    if(supportAttached){handRole=interaction::HandPoseRole::WeaponSupport;handPosed=handRoleReady;}
    else if(handRoleReady)handPosed=poseHand(handRole,solved->writes);
    if(targets->tracked[1]){
        const auto weaponWrites=interaction::RetargetRigSubtree(rig->parents,native,rig->weaponBone,placed,rig->nativeHiddenLeaves);
        if(!weaponWrites){RejectTracking(14);published.reset();return;}
        merge(*weaponWrites);
    }
    std::optional<TriggerEvidence> triggerEvidence;
    const auto poseTrigger=[&]()->bool{
        if(!targets->tracked[1]||!rightHandBinding)return true;
        auto current=native;for(const auto& w:writes)current[w.index]=w.transform;
        const auto posed=PoseRightTrigger(rig->parents,current,*rightHandBinding,input.input.hands[1].trigger);
        if(!posed){++triggerFailures;return false;}
        auto after=current;for(const auto& w:posed->writes)after[w.index]=w.transform;
        TriggerEvidence evidence;evidence.generation=input.input.generation;evidence.weapon=input.weapon;evidence.trigger=input.input.hands[1].trigger;
        evidence.wristPreserved=after[rig->right.wrist].values==current[rig->right.wrist].values;
        evidence.otherBranchesPreserved=true;
        const auto& index=rightHandBinding->pose.fingers[std::size_t(interaction::HandFinger::Index)];
        for(unsigned n=0;n<rig->parents.size();++n){
            bool below=false;for(auto atBone=std::int32_t(n);atBone!=-1;atBone=rig->parents[atBone])if(unsigned(atBone)==index.joints[0].index){below=true;break;}
            if(!below&&after[n].values!=current[n].values)evidence.otherBranchesPreserved=false;
        }
        for(unsigned j=0;j<3;++j){
            const auto n=index.joints[j].index,parent=unsigned(rig->parents[n]);evidence.indices[j]=n;
            const auto a=interaction::InverseAnimatedTransform(current[parent]),b=interaction::InverseAnimatedTransform(after[parent]);
            if(!a||!b){++triggerFailures;return false;}
            evidence.before[j]=interaction::Multiply(current[n],*a);evidence.after[j]=interaction::Multiply(after[n],*b);
        }
        if(!evidence.wristPreserved||!evidence.otherBranchesPreserved){++triggerFailures;return false;}
        triggerEvidence=evidence;merge(posed->writes);return true;
    };
    if(freeRightReady){
        const auto wrist=std::find_if(solved->writes.begin(),solved->writes.end(),[&](const auto& w){return w.index==rig->right.wrist;});
        if(wrist==solved->writes.end()){RejectTracking(15);return;}
        auto current=native;for(const auto& write:writes)current[write.index]=write.transform;
        const auto freePose=PoseHolsteredRight(*input.freeRight,ReloadNanos(),*rightHandBinding,rig->parents,current,wrist->transform,input.input.hands[1]);
        if(!freePose){RejectTracking(15);return;}merge(freePose->writes);++freeRightPoses;
    }else if(!poseTrigger()){RejectTracking(15);return;}
    auto plan=BuildRigPosePlan(*rig,writes);if(!plan){RejectTracking(15);published.reset();return;}
    if(boltCustody&&input.bolt.target){
        const auto presentation=BuildBoltPresentation(input.bolt,*rig,placed,input.input.worldUnitsPerMeter,ReloadNanos());
        if(!presentation){published.reset();return;}
        const auto boltSolved=interaction::SolveTrackedArms(rig->parents,armNative,rig->left,rig->right,
            {{},{},{},false},{presentation->mechanismWrist,targets->arms[1].poleDirection,targets->arms[1].shoulder,true});
        if(!boltSolved||boltSolved->reachClamped[1]||boltSolved->targetError[1]>.001f*input.input.worldUnitsPerMeter){published.reset();return;}
        merge(boltSolved->writes);const std::array<interaction::BoneWrite,1> part{presentation->part};merge(part);
        for(const auto& finger:M95BoltFingers){
            const auto bone=std::find(rig->names.begin(),rig->names.end(),finger.name);
            if(bone==rig->names.end()||std::find(bone+1,rig->names.end(),finger.name)!=rig->names.end()){published.reset();return;}
            auto local=finger.wristFromFinger;for(unsigned n=0;n<3;++n)local.values[3][n]*=input.input.worldUnitsPerMeter;
            const std::array<interaction::BoneWrite,1> write{{{unsigned(bone-rig->names.begin()),interaction::Multiply(local,presentation->mechanismWrist)}}};merge(write);
        }
        plan=BuildRigPosePlan(*rig,writes);if(!plan){published.reset();return;}
        ++boltPoses;
    }
    std::vector<std::array<std::byte,64>> pumpBase;bool pumpAttached=false;
    if(handRoleReady&&input.pump.target&&!input.supportHolding&&!input.sightPreview.valid&&!input.reload.preview&&
       !input.magazine.target&&!targets->weaponAttachmentPending){
        ++pumpPlanAttempts;const auto presentation=BuildPumpPresentation(input.pump,*rig,placed,input.input.worldUnitsPerMeter,ReloadNanos());
        if(presentation.wrist&&presentation.part){
            const auto pumpSolved=interaction::SolveTrackedArms(rig->parents,armNative,rig->left,rig->right,
                {*presentation.wrist,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},{{},{},{},false});
            if(pumpSolved&&!pumpSolved->reachClamped[0]&&pumpSolved->targetError[0]<=.001f*input.input.worldUnitsPerMeter){
                const auto baseWrites=writes;merge(pumpSolved->writes);
                const bool posed=poseHand(interaction::HandPoseRole::MechanismGrip,pumpSolved->writes)&&poseTrigger();
                const std::array<interaction::BoneWrite,1> part{*presentation.part};merge(part);
                const auto candidate=posed?BuildRigPosePlan(*rig,writes):std::optional<RigPosePlan>{};
                if(candidate){pumpBase=rig->nativeEvaluated;for(const auto& edit:plan->edits)pumpBase[edit.index]=edit.after;
                    plan=candidate;solved->writes=writes;solved->targetError[0]=pumpSolved->targetError[0];solved->reachClamped[0]=false;
                    leftTarget=*presentation.wrist;handRole=interaction::HandPoseRole::MechanismGrip;handPosed=true;pumpAttached=true;++pumpPoses;
                }else{writes=baseWrites;++pumpPoseRejects;}
            }else ++pumpReachRejects;
        }else ++pumpPoseRejects;
    }
    std::vector<std::array<std::byte,64>> previewBase;bool sightAttached=false;
    std::optional<SightPreviewRecord> sightEvidence;
    const auto& preview=input.sightPreview;
    if(handRoleReady&&preview.valid&&preview.token&&preview.weapon==input.weapon&&preview.generation<=input.input.generation&&
       preview.generation&&Fresh(preview.deadline)&&!input.supportHolding&&
       preview.physicalItem&&preview.physicalItem==input.sightPhysicalItem&&
       preview.phase!=interaction::SightFlipPhase::Idle&&
       (!targets->weaponAttachmentPending||preview.phase==interaction::SightFlipPhase::AwaitingAcknowledgement||preview.phase==interaction::SightFlipPhase::Latched)&&
       targets->tracked[0]&&targets->tracked[1]&&input.input.hands[0].squeeze>.35f&&
       input.sightAdapter&&input.sightAdapter->nativeSightAccepted&&preview.adapter==input.sightAdapter&&
       rigFingerprint==0xa7f219a1426216abull){
        const auto rear=std::find(rig->names.begin(),rig->names.end(),input.sightAdapter->rear);
        const auto front=std::find(rig->names.begin(),rig->names.end(),input.sightAdapter->front);
        const auto visibleLeaf=[&](auto it){
            if(it==rig->names.end())return false;const auto n=unsigned(it-rig->names.begin());
            return rig->parents[n]==std::int32_t(rig->weaponBone)&&
                std::find(rig->parents.begin(),rig->parents.end(),std::int32_t(n))==rig->parents.end()&&
                std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)==rig->nativeHiddenLeaves.end();
        };
        const auto world=[&](math::Matrix4 local){for(unsigned n=0;n<3;++n)local.values[3][n]*=input.input.worldUnitsPerMeter;return interaction::Multiply(local,placed);};
        const auto desiredHand=world(preview.wrist),desiredRear=world(preview.rear),desiredFront=world(preview.front);
        const bool subtree=input.sightAdapter->assembly==SightAssemblyKind::Subtree;
        const auto attachmentWrites=BuildSightAssemblyWrites(*input.sightAdapter,rig->names,rig->parents,native,rig->nativeHiddenLeaves,desiredRear,subtree?std::nullopt:std::optional(desiredFront));
        if(attachmentWrites&&(subtree||(visibleLeaf(rear)&&visibleLeaf(front)))&&interaction::InverseAnimatedTransform(desiredHand)){
            const auto previewSolved=interaction::SolveTrackedArms(rig->parents,armNative,rig->left,rig->right,
                {desiredHand,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
                {rightTarget,targets->arms[1].poleDirection,targets->arms[1].shoulder,true});
            if(previewSolved){
                const auto baseWrites=writes;
                merge(previewSolved->writes);
                const bool mechanismPosed=poseHand(interaction::HandPoseRole::MechanismGrip,previewSolved->writes)&&poseTrigger();
                merge(*attachmentWrites);
                const auto previewPlan=mechanismPosed?BuildRigPosePlan(*rig,writes):std::optional<RigPosePlan>{};
                if(previewPlan){
                    previewBase=rig->nativeEvaluated;for(const auto& edit:plan->edits)previewBase[edit.index]=edit.after;
                    plan=previewPlan;solved=previewSolved;leftTarget=desiredHand;sightAttached=true;
                    handRole=interaction::HandPoseRole::MechanismGrip;handPosed=true;
                    SightPreviewRecord record;record.preview=preview;record.inputGeneration=input.input.generation;
                    record.owner=input.ownerGeneration;record.space=input.input.spaceGeneration;
                    const auto inverseNative=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
                    const auto inversePlaced=interaction::InverseAnimatedTransform(placed);
                    if(inverseNative&&inversePlaced){
                        record.nativeRear=interaction::Multiply(native[rear-rig->names.begin()],*inverseNative);
                        if(!subtree)record.nativeFront=interaction::Multiply(native[front-rig->names.begin()],*inverseNative);
                        record.rawHand=interaction::Multiply(freeLeft,*inversePlaced);
                        for(const auto& write:solved->writes)if(write.index==rig->left.wrist)record.resolvedHand=interaction::Multiply(write.transform,*inversePlaced);
                        for(auto matrix:{&record.nativeRear,&record.nativeFront,&record.rawHand,&record.resolvedHand})
                            for(unsigned n=0;n<3;++n)matrix->values[3][n]/=input.input.worldUnitsPerMeter;
                        const auto p=preview.palmPointWrist;const auto& m=record.resolvedHand.values;
                        const math::Vec3 palm{p.x*m[0][0]+p.y*m[1][0]+p.z*m[2][0]+m[3][0],p.x*m[0][1]+p.y*m[1][1]+p.z*m[2][1]+m[3][1],p.x*m[0][2]+p.y*m[1][2]+p.z*m[2][2]+m[3][2]};
                        record.palmError=std::hypot(palm.x-preview.graspPoint.x,palm.y-preview.graspPoint.y,palm.z-preview.graspPoint.z);
                        record.clamped=solved->targetError[0]>.001f*input.input.worldUnitsPerMeter;sightEvidence=record;
                    }
                }else{writes=baseWrites;++sightPreviewRejected;}
            }else ++sightPreviewRejected;
        }else ++sightPreviewRejected;
    }
    std::vector<std::array<std::byte,64>> reloadBase;bool reloadAttached=false;std::int64_t reloadDeadline=0;
    std::optional<std::uint32_t> reloadVisibleShell;
    if(input.reload.preview&&!sightAttached&&!supportAttached&&!targets->weaponAttachmentPending){
        Bc2ReloadPaletteSample sample;sample.enabled=true;sample.baseWrites=writes;sample.armNativeWorld=armNative;
        sample.leftPoleDirection=targets->arms[0].poleDirection;sample.leftShoulder=targets->arms[0].shoulder;
        sample.presentation.unitsPerMetre=input.input.worldUnitsPerMeter;sample.presentation.placedWeaponWorld=placed;
        const auto reloadNow=ReloadNanos();
        auto reloadPlan=BuildReloadPreviewPalette(*input.reload.preview,input.reload,reloadContact,*rig,sample,input.assetName.data(),reloadNow);
        const auto reloadTargets=ResolveReloadPreviewTargets(*input.reload.preview,input.reload,reloadContact,reloadNow);
        if(reloadPlan.palette&&reloadTargets){
            reloadBase=rig->nativeEvaluated;for(const auto& edit:plan->edits)reloadBase[edit.index]=edit.after;
            writes=std::move(reloadPlan.writes);plan=std::move(reloadPlan.palette);reloadAttached=true;reloadDeadline=reloadTargets->deadlineNs;
            reloadVisibleShell=reloadPlan.ownedShellVisibility;
            solved->writes=writes;solved->targetError[0]=reloadPlan.leftWristError;solved->reachClamped[0]=false;
            for(const auto& write:writes)if(write.index==rig->left.wrist){leftTarget=write.transform;break;}
            handPosed=true;handRole=interaction::HandPoseRole::MechanismGrip;++reloadPreviewPoses;
            ++reloadPhasePoses[unsigned(input.reload.preview->phase)];
        }else {++reloadPreviewRejected;
            const auto reason=unsigned(reloadPlan.presentationReason);
            if(reason<reloadPresentationRejections.size())++reloadPresentationRejections[reason];
        }
    }
    std::vector<std::array<std::byte,64>> magazineBase;
    std::optional<MagazinePresentationBinding> magazineBinding;
    bool magazineAttached=false,magazineHidden=false;
    if(input.magazine.target&&!reloadAttached&&!sightAttached&&!supportAttached&&!targets->weaponAttachmentPending){
        ++magazinePlanAttempts;
        if(const auto binding=input.magazine.family.binding.profile?BindMagazinePresentation(*rig,*input.magazine.family.binding.profile):std::nullopt){
            const auto presentation=BuildMagazinePresentation(*rig,*binding,input.magazine,placed,input.input.worldUnitsPerMeter,ReloadNanos());
            if(presentation.binding){
                const auto baseWrites=writes;bool valid=true;
                auto magazineSolved=solved;
                if(presentation.wristTarget){
                    magazineSolved=interaction::SolveTrackedArms(rig->parents,armNative,rig->left,rig->right,
                        {*presentation.wristTarget,targets->arms[0].poleDirection,targets->arms[0].shoulder,true},
                        {{},{},{},false});
                    valid=magazineSolved&&!magazineSolved->reachClamped[0]&&magazineSolved->targetError[0]<=.0001f*input.input.worldUnitsPerMeter;
                    if(!valid){++magazineReachRejects;if(magazineReachCount<magazineReachRecords.size())
                        magazineReachRecords[magazineReachCount++]={input.input.generation,unsigned(input.magazine.target->role),magazineSolved?magazineSolved->targetError[0]:-1.f,bool(magazineSolved),magazineSolved&&magazineSolved->reachClamped[0]};}
                    if(valid)merge(magazineSolved->writes);
                }
                if(valid){
                    merge(presentation.writes);
                    if(const auto candidate=BuildRigPosePlan(*rig,writes)){
                        magazineBase=rig->nativeEvaluated;
                        for(const auto& e:plan->edits)magazineBase[e.index]=e.after;
                        plan=candidate;magazineBinding=binding;magazineAttached=true;magazineHidden=presentation.hideMagazine;
                        if(presentation.wristTarget){solved->writes=writes;solved->targetError[0]=magazineSolved->targetError[0];solved->reachClamped[0]=false;leftTarget=*presentation.wristTarget;
                            handPosed=true;handRole=interaction::HandPoseRole::MechanismGrip;}
                        ++magazinePoses;
                    }else {valid=false;++magazinePoseRejects;}
                }
                if(!valid)writes=baseWrites;
            }else {++magazinePlanRejects;
                if(std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),binding->magazine)!=rig->nativeHiddenLeaves.end())++magazineHiddenRejects;
                if(!MagazineTargetFresh(input.magazine,ReloadNanos()))++magazineFreshRejects;}
        }else ++magazineBindingRejects;
    }
    auto pose=std::make_shared<PublishedPose>();pose->authoredGrip=authored.binding;pose->equipment=input.nativeEquipment;pose->identity=rig->identity;pose->weapon=input.weapon;pose->generation=input.input.generation;pose->ownerGeneration=input.ownerGeneration;pose->equipmentGeneration=input.equipmentGeneration;pose->space=input.input.spaceGeneration;pose->deadline=input.deadline;pose->before=rig->nativeEvaluated;pose->posed=pose->before;
    // Diagnostic metadata travels WITH the immutable palette actually packed.
    // No selected Meshes1p association is invented from the item display name.
    if(ReloadProducerBinding().Enabled()){
        LARGE_INTEGER now{},frequency{};
        if(QueryPerformanceCounter(&now)&&QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0){
            const auto ns=[&](std::int64_t tick){return (tick/frequency.QuadPart)*1000000000+(tick%frequency.QuadPart)*1000000000/frequency.QuadPart;};
            auto& d=pose->drawSource;d.owner={input.soldier,input.weak,input.weapon,input.ownerGeneration,input.input.spaceGeneration};
            d.rigPose=rig->identity.pose;d.rigFingerprint=SightRigFingerprint(rig->names,rig->parents,rig->inverseBind);
            d.inputGeneration=input.input.generation;d.observedNs=ns(now.QuadPart);d.deadlineNs=ns(input.deadline);d.boneCount=rig->identity.count;
            d.physicalEquipmentGeneration=input.equipmentGeneration;
            const auto selected=gameplay::ReadCurrentSelectedMeshes(d.observedNs);
            if(selected&&ReticleSelectedValid(*selected,d.observedNs)&&selected->sequence<=input.input.generation&&
               selected->owner.soldier==input.soldier&&selected->owner.weak==input.weak&&selected->owner.weapon==input.weapon&&
               selected->owner.actorGeneration==input.ownerGeneration&&selected->owner.space==input.input.spaceGeneration&&
               selected->weaponData==input.nativeEquipment.data){
                d.opticSelected=selected;d.observedNs=std::max(d.observedNs,selected->observedNs);d.deadlineNs=std::min(d.deadlineNs,selected->deadlineNs);
            }
            const std::string_view item(input.assetName.data());
            for(unsigned n=0;n<rig->names.size();++n)if(rig->parents[n]==std::int32_t(rig->weaponBone)){
                if(item=="SPAS12_sp"&&rig->names[n]=="jntWpn_7"){
                    d.shellNamed=true;d.shellIndex=n;d.shellHidden=std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),n)!=rig->nativeHiddenLeaves.end();}
                if((item=="XM8_sp_s"||item=="40mmgl")&&rig->names[n]=="jntWpn_10"){d.opticNamed=true;d.opticIndex=n;}
            }
        }
    }
    for(const auto& edit:plan->edits)pose->posed[edit.index]=edit.after;
    if(magazineAttached){
        if(magazineHidden){
            const auto hidden=HideMagazinePackedPalette(*rig,*magazineBinding,pose->posed);
            if(!hidden){RejectTracking(15);published.reset();return;}pose->posed=*hidden;
        }
        pose->magazine=std::make_shared<const MagazineTracking>(input.magazine);pose->magazineBase=std::move(magazineBase);
    }
    if(boltCustody){pose->bolt=std::make_shared<const Bc2BoltTracking>(input.bolt);pose->boltBase=rig->nativeEvaluated;}
    if(pumpAttached){pose->pump=std::make_shared<const Bc2PumpTracking>(input.pump);pose->pumpBase=std::move(pumpBase);}
    if(freeRightReady)pose->freeRight=input.freeRight;
    if(sightAttached){pose->previewToken=preview.token;pose->previewBase=std::move(previewBase);}
    if(reloadAttached){pose->reloadPreview=input.reload.preview;pose->reloadBase=std::move(reloadBase);pose->reloadDeadlineNs=reloadDeadline;}
    for(auto leaf:rig->nativeHiddenLeaves)if(pose->posed[leaf]!=pose->before[leaf]&&
        !(reloadAttached&&reloadVisibleShell&&leaf==*reloadVisibleShell)){++fallbackFailures;published.reset();return;}
    // Independently unavailable arms must remain byte-for-byte native, including
    // their fingers/twists. Also leave the weapon chain native without right grip.
    const auto unchangedBranch=[&](unsigned root){for(unsigned n=0;n<rig->parents.size();++n){
        bool below=false;for(auto at=std::int32_t(n);at!=-1;at=rig->parents[at])if(unsigned(at)==root){below=true;break;}
        if(below&&pose->posed[n]!=pose->before[n])return false;
    }return true;};
    if((!targets->tracked[0]&&!unchangedBranch(rig->left.shoulder))||
       (!targets->tracked[1]&&(!unchangedBranch(rig->right.shoulder)||!unchangedBranch(rig->weaponBone)))){
        ++fallbackFailures;published.reset();return;
    }
    // This attachment is observed by its native name and validated ancestry,
    // never an inherited or hard-coded bone index. The firing consumer binds its own event.
    if(targets->tracked[1])for(unsigned n=0;n<rig->names.size();++n)if(rig->names[n]=="jntWpn_Flash"){
        bool child=false;for(auto parent=rig->parents[n];parent!=-1;parent=rig->parents[parent])if(unsigned(parent)==rig->weaponBone){child=true;break;}
        if(child)for(const auto& write:writes)if(write.index==n){
            pose->shot=WeaponShotFrame{weapon,placed,native[n],write.transform,input.input.generation,input.deadline,n,input.input.worldUnitsPerMeter,input.ownerGeneration,input.input.spaceGeneration};break;
        }
        break;
    }

    if(pose->shot&&authored.binding)pose->shot->authoredSupportReference=AuthoredRifleSupport(*authored.binding);
    // The native support wrist is authored relative to this evaluated weapon
    // root. Contact always uses the independent tracked target, including while
    // the visible hand is attached. No guessed attachment offset.
    if(pose->shot&&targets->tracked[0]&&targets->tracked[1]){
        const auto inverseWeapon=interaction::InverseAnimatedTransform(native[rig->weaponBone]);
        const auto inverseBody=interaction::InverseAnimatedTransform(*body);
        const auto left=math::MakeRelativePose(input.input.referenceHead,input.input.hands[0].grip);
        const auto right=math::MakeRelativePose(input.input.referenceHead,input.input.hands[1].grip);
        if(inverseWeapon&&inverseBody&&left&&right){
            const auto support=interaction::Multiply(fixedSupportLocal?*fixedSupportLocal:authoredSupportLocal,placed);
            const auto l=point(targets->left),r=point(targets->right),s=point(support);
            const float units=input.input.worldUnitsPerMeter;
            const math::Vec3 delta{l.x-r.x,l.y-r.y,l.z-r.z};
            const auto& b=inverseBody->values;
            const math::Vec3 relative{(delta.x*b[0][0]+delta.y*b[1][0]+delta.z*b[2][0])/units,
                (delta.x*b[0][1]+delta.y*b[1][1]+delta.z*b[2][1])/units,
                -(delta.x*b[0][2]+delta.y*b[1][2]+delta.z*b[2][2])/units};
            pose->shot->support={true,std::hypot(l.x-s.x,l.y-s.y,l.z-s.z)/units,
                {relative.x-(left->position.x-right->position.x),relative.y-(left->position.y-right->position.y),relative.z-(left->position.z-right->position.z)}};
        }
    }
    // Contact is computed from the independent left controller target, never
    // the support-snapped or reach-clamped visible wrist. Source sight geometry
    // remains authored by BC2; only its relation to the placed gun is measured.
    if(pose->shot&&handRoleReady&&targets->tracked[0]&&targets->tracked[1]&&
       input.sightAdapter&&input.sightAdapter->nativeSightAccepted&&input.sightAdapter->assembly==SightAssemblyKind::OpposedLeaves){
        const auto sight=std::find(rig->names.begin(),rig->names.end(),"jntWpn_9");
        if(sight!=rig->names.end()){
            const auto index=unsigned(sight-rig->names.begin());
            if(rig->parents[index]==std::int32_t(rig->weaponBone)){
                WeaponSightObservation observation;
                observation.assetName=input.assetName.data();observation.rootName=rig->names[rig->weaponBone];
                observation.sightName=*sight;observation.sightParentName=rig->names[rig->parents[index]];
                observation.skeletonFingerprint=rigFingerprint;
                observation.generation=input.input.generation;observation.deadline=input.deadline;
                observation.nativeWeapon=native[rig->weaponBone];observation.nativeSight=native[index];
                observation.placedWeapon=placed;observation.rawLeftTarget=freeLeft;
                observation.unitsPerMetre=input.input.worldUnitsPerMeter;
                observation.attachmentReady=!targets->weaponAttachmentPending;
                // The recognition point and captured mechanism pose use the same
                // bind-derived landmark and independent pre-IK anatomical wrist.
                // Native grip animation cannot move the activation point, and no
                // support-snapped or preview wrist can sustain its own contact.
                observation.palmValid=true;observation.palmPointWristMeters=handBinding->mechanismPointWristMeters;
                observation.visualHandValid=true;observation.visualLeftTarget=freeLeft;
                observation.visualPointWristMeters=handBinding->mechanismPointWristMeters;
                const auto front=std::find(rig->names.begin(),rig->names.end(),"jntWpn_11");
                if(front!=rig->names.end()){
                    const auto frontIndex=unsigned(front-rig->names.begin());
                    observation.frontValid=rig->parents[frontIndex]==std::int32_t(rig->weaponBone)&&
                        std::find(rig->parents.begin(),rig->parents.end(),std::int32_t(frontIndex))==rig->parents.end()&&
                        std::find(rig->parents.begin(),rig->parents.end(),std::int32_t(index))==rig->parents.end()&&
                        std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),frontIndex)==rig->nativeHiddenLeaves.end();
                    observation.nativeFront=native[frontIndex];
                }
                observation.hidden=std::find(rig->nativeHiddenLeaves.begin(),rig->nativeHiddenLeaves.end(),index)!=rig->nativeHiddenLeaves.end();
                pose->shot->sight=MeasureWeaponSightContact(observation);
                // Source animation can be observed while raw-item attachment
                // calibration is pending. It cannot arm or renew contact; the
                // held visual uses only its measured rotation catching up.
                if(input.sightPhysicalItem&&rigFingerprint==0xa7f219a1426216abull&&
                   observation.rootName=="jntWpn_1"&&!observation.hidden&&observation.frontValid&&
                   std::isfinite(observation.unitsPerMetre)&&observation.unitsPerMetre>0){
                    const auto inverse=interaction::InverseAnimatedTransform(observation.nativeWeapon);
                    if(inverse&&interaction::InverseAnimatedTransform(observation.nativeSight)&&interaction::InverseAnimatedTransform(observation.nativeFront)){
                        NativeSightState state;
                        state.rear=interaction::Multiply(observation.nativeSight,*inverse);
                        state.front=interaction::Multiply(observation.nativeFront,*inverse);
                        for(auto matrix:{&state.rear,&state.front})for(unsigned n=0;n<3;++n)matrix->values[3][n]/=observation.unitsPerMetre;
                        state.physicalItem=input.sightPhysicalItem;
                        // Use the independent target computed before any support,
                        // mechanism pose or arm IK, never the displayed wrist.
                        const auto inversePlaced=interaction::InverseAnimatedTransform(placed);
                        if(inversePlaced&&interaction::InverseAnimatedTransform(freeLeft)){
                            state.rawHand=interaction::Multiply(freeLeft,*inversePlaced);
                            for(unsigned n=0;n<3;++n)state.rawHand.values[3][n]/=observation.unitsPerMetre;
                            state.rawHandValid=bool(interaction::InverseAnimatedTransform(state.rawHand));
                        }
                        state.valid=true;pose->shot->nativeSight=state;
                    }
                }
            }
        }
    }
    if(pose->shot&&handRoleReady&&targets->tracked[0]&&targets->tracked[1]&&input.sightAdapter&&
       input.sightAdapter->nativeSightAccepted&&input.sightAdapter->assembly==SightAssemblyKind::Subtree&&
       input.sightAdapter->authored&&input.sightPhysicalItem&&authored.binding&&authored.binding->profile&&
       authored.binding->profile->meshPath==input.sightAdapter->authored->meshPath){
        const auto& p=*input.sightAdapter;
        AuthoredSightObservation observation;
        observation.selectedMeshPath=authored.binding->profile->meshPath;
        observation.asset=input.assetName.data();observation.configuration=input.sightSecondary?p.family->secondaryConfiguration:p.family->primaryConfiguration;
        observation.rigFingerprint=rigFingerprint;observation.generation=input.input.generation;
        LARGE_INTEGER sightClock{};QueryPerformanceCounter(&sightClock);observation.now=sightClock.QuadPart;observation.deadline=input.deadline;observation.names=rig->names;observation.parents=rig->parents;
        observation.native=native;observation.hidden=rig->nativeHiddenLeaves;observation.placedWeapon=placed;
        observation.rawAnatomicalWrist=freeLeft;observation.mechanismPointWristMeters=handBinding->mechanismPointWristMeters;
        observation.unitsPerMetre=input.input.worldUnitsPerMeter;observation.attachmentReady=!targets->weaponAttachmentPending;
        pose->shot->sight=MeasureSightAdapterContact(p,observation);
        const auto nativeProgress=ReadSightAdapterNativeFrame(p,observation);
        if(nativeProgress){
            auto& state=pose->shot->nativeSight;state.rear=nativeProgress->rear;
            state.rawHand=nativeProgress->rawHand;state.rawHandValid=true;
            state.physicalItem=input.sightPhysicalItem;state.valid=true;
        }
    }
    if(pose->shot&&boltContact.raw.valid)pose->shot->bolt=boltContact;
    if(pose->shot&&pumpContact.valid)pose->shot->pump=pumpContact;
    if(pose->shot&&magazineContact.valid){pose->shot->magazine=magazineContact;++magazineContacts;}
    if(pose->shot&&reloadContact.valid){pose->shot->reload=reloadContact;++reloadRawContacts;
        if(reloadContact.nativeShellVisible)++reloadNativeVisibleContacts;else ++reloadNativeHiddenContacts;
    }
    const auto canonicalPlaced=placed;
    for(unsigned n=0;n<4;++n){placed.values[2][n]=-placed.values[2][n];placed.values[n][2]=-placed.values[n][2];}
    std::array<std::byte,64> current{};if(!Read(at,current.data(),64)||current!=original||!Fresh(input.deadline)||!EquipmentStillCurrent(input.nativeEquipment)||!AuthoredCurrent(authored.binding)){if(authored.binding)++authoredGripExpired;RejectTracking(16);return;}
    // Only the verified weapon getter's caller-owned output is replaced. Bone
    // animation buffers remain untouched; native root/effect consumers run once.
    if(targets->tracked[1]){for(unsigned row=0;row<4;++row)std::memcpy(reinterpret_cast<std::byte*>(destination)+row*16,placed.values[row].data(),12);++weaponOutputs;}
    if(pose->shot){
        const ShotGuard identity{input.soldier,input.weak,input.weapon,input.ownerGeneration,input.input.spaceGeneration,input.input.generation,input.deadline,true,targets->tracked[0],0,input.weaponActionsBlocked,input.equipmentGeneration,input.nativeEquipment};
        shotPublication.store(std::make_shared<const ShotPublication>(ShotPublication{*pose->shot,identity,authored.binding}),std::memory_order_release);
    }else shotPublication.store({},std::memory_order_release);
    if(const auto requested=visibilityGuard.load(std::memory_order_acquire);requested&&requested->enabled&&
        requested->input.sequence==input.input.generation&&requested->nativeOwner.soldier==input.soldier&&requested->nativeOwner.weak==input.weak&&
        requested->nativeOwner.weapon==input.weapon&&requested->nativeOwner.actorGeneration==input.ownerGeneration&&
        requested->nativeOwner.space==input.input.spaceGeneration&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken){
        auto visibility=BuildWeaponVisibilityPalette(*rig,pose->posed,*requested,ReloadNanos());
        if(visibility.reason==WeaponVisibilityReason::None){pose->visibility=std::make_shared<const WeaponVisibilityPlan>(std::move(visibility));++visibilityPlans;}
        else ++visibilityRejected;
    }
    if(!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->visibility&&input.reload.shellControl){
        if(auto hidden=BuildReloadShellHidePalette(input.reload,*rig,pose->posed,input.assetName.data(),ReloadNanos())){
            pose->shellHide=std::make_shared<const ReloadShellHidePlan>(std::move(*hidden));++reloadShellHidePlans;
        }
    }
    if(!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->visibility&&!pose->freeRight&&
        !input.weaponActionsBlocked&&!targets->weaponAttachmentPending&&ReloadBeltCompatible(input.reload,ReloadNanos())&&
        (!input.reload.belt->heldCycle||pose->shellHide)){
        if(auto belt=BuildSpasBeltAmmoPalette(*input.reload.belt,*rig,pose->posed,input.input,*body,input.assetName.data(),ReloadNanos())){
            pose->belt=std::make_shared<const BeltAmmoPalette>(std::move(*belt));++beltPlans;
        }
    }
    if(pose->freeRight&&(!pose->visibility||!pose->visibility->hidden)){RejectTracking(15);published.reset();return;}
    if(const auto request=ordinaryEquipmentGuard.load(std::memory_order_acquire);request&&
        OrdinaryGuardCurrent(*request)&&request->input.sequence==input.input.generation&&
        request->equipment==input.nativeEquipment&&request->input.owner.equipGeneration==input.equipmentGeneration&&
        targets->tracked[0]&&targets->tracked[1]&&!targets->weaponAttachmentPending&&
        !pose->visibility&&!pose->freeRight&&!pose->magazine&&!pose->reloadPreview&&!pose->previewToken&&!pose->shellHide&&!pose->belt)
        pose->ordinaryRecovery=*request;
    published=std::move(pose);
    if(reloadVisibleShell)++reloadOwnedShellVisibilityPoses;
    const auto now=GetTickCount64();if(now-handRecordTime>=100){auto& record=handRecords[handRecordNext];handRecordNext=(handRecordNext+1)%unsigned(handRecords.size());handRecordCount=(std::min)(handRecordCount+1,unsigned(handRecords.size()));record={};record.attachmentPending=targets->weaponAttachmentPending;record.authoredGrip=targets->weaponAttachmentAuthored;record.supportAttached=supportAttached;record.sightAttached=sightAttached;record.nativeRight=native[rig->right.wrist];record.nativeLeft=native[rig->left.wrist];record.generation=input.input.generation;record.space=input.input.spaceGeneration;record.eyeBase=*body;
        for(unsigned side=0;side<2;++side)if(input.input.hands[side].gripTracked)if(const auto local=math::MakeRelativePose(input.input.referenceHead,input.input.hands[side].grip))record.localGrips[side]=local->position;
        record.nativeWeapon=weapon;record.placedWeapon=canonicalPlaced;record.grip=input.input.hands[1].gripTracked?input.input.hands[1].grip.position:math::Vec3{};
        record.resolvedRight=native[rig->right.wrist];
        record.actorPosition=input.actorPosition;record.nativeRoot=point(animatedBody);record.bodyRoot=point(*body);record.anatomyRoot=point(anatomy);
        record.tracked=targets->tracked;record.weapon=input.weapon;record.ownerGeneration=input.ownerGeneration;
        for(unsigned n=0;n<2;++n)record.grips[n]=input.input.hands[n].gripTracked?input.input.hands[n].grip.position:math::Vec3{};
        record.resolved={point(native[rig->left.wrist]),point(native[rig->right.wrist])};
        record.shoulders={point(native[rig->left.shoulder]),point(native[rig->right.shoulder])};record.anchors={targets->arms[0].shoulder,targets->arms[1].shoulder};record.targets={point(leftTarget),point(targets->right)};record.errors=solved->targetError;
        for(const auto& w:solved->writes){if(w.index==rig->left.wrist)record.resolved[0]=point(w.transform);if(w.index==rig->right.wrist){record.resolved[1]=point(w.transform);record.resolvedRight=w.transform;}}handRecordTime=now;}
    if(triggerEvidence){
        if(triggerEvidence->trigger>0)++triggerPoses;
        if(now-triggerRecordTime>=100){
            triggerRecords[triggerNext]=*triggerEvidence;triggerNext=(triggerNext+1)%triggerRecords.size();
            triggerCount=std::min(unsigned(triggerRecords.size()),triggerCount+1);triggerRecordTime=now;
        }
    }
    if(handPosed){
        ++handRolePoses[unsigned(handRole)];
        if(now-handPoseRecordTime>=100){
            HandPoseEvidence record;record.generation=input.input.generation;record.space=input.input.spaceGeneration;
            record.owner=input.ownerGeneration;record.weapon=input.weapon;record.role=handRole;
            record.squeeze=input.input.hands[0].squeeze;record.trigger=input.input.hands[0].trigger;
            record.touchActive=input.input.hands[0].touchActive;record.touched=input.input.hands[0].touched;
            record.grip=leftGripWorld;record.mechanismPoint=handBinding->mechanismPointWristMeters;
            auto evaluated=native;for(const auto& w:writes)evaluated[w.index]=w.transform;
            record.wrist=evaluated[rig->left.wrist];bool valid=true;unsigned atFinger=0;
            for(const auto& finger:handBinding->pose.fingers)for(unsigned joint=0;joint<finger.count;++joint){
                const auto index=finger.joints[joint].index,parent=unsigned(rig->parents[index]);
                const auto r=interaction::InverseAnimatedTransform(handBinding->referenceWorld[parent]);
                const auto n=interaction::InverseAnimatedTransform(native[parent]);
                const auto p=interaction::InverseAnimatedTransform(evaluated[parent]);
                if(!r||!n||!p||atFinger>=record.fingers.size()){valid=false;break;}
                record.fingers[atFinger++]={index,parent,interaction::Multiply(handBinding->referenceWorld[index],*r),
                    interaction::Multiply(native[index],*n),interaction::Multiply(evaluated[index],*p)};
            }
            if(valid&&atFinger==record.fingers.size()){
                handPoseRecords[handPoseNext]=record;handPoseNext=(handPoseNext+1)%handPoseRecords.size();
                handPoseCount=std::min(unsigned(handPoseRecords.size()),handPoseCount+1);handPoseRecordTime=now;
            }
        }
    }
    if(!rig->nativeHiddenLeaves.empty())++hiddenLeafPoses;
    if(supportAttached)++supportAttachedPoses;
    if(sightAttached){
        ++sightPreviewPoses;
        if(sightEvidence&&now-sightPreviewRecordTime>=20){
            sightPreviewRecords[sightPreviewNext]=*sightEvidence;sightPreviewNext=(sightPreviewNext+1)%sightPreviewRecords.size();
            sightPreviewCount=std::min(unsigned(sightPreviewRecords.size()),sightPreviewCount+1);sightPreviewRecordTime=now;
        }
    }
    ++trackedPoses;if(targets->calibrated)++calibrations;
    for(unsigned n=0;n<2;++n){if(targets->tracked[n])++handPoses[n];if(targets->calibratedHands[n])++handCalibrations[n];}
    if(!targets->tracked[0]||!targets->tracked[1])++partialPoses;
}
void Start()noexcept {started=GetTickCount64();enabled.store(true,std::memory_order_release);}
std::optional<MagazineDetachPairReceipt> ReadMagazineDetachPair(const ReloadStateOwner& owner,std::int64_t now)noexcept {
    const auto pair=magazineDetachPair.load(std::memory_order_acquire);
    const auto current=magazineGuard.load(std::memory_order_acquire);
    if(!pair||pair->authorization.current.identity.owner!=owner||!current||!current->detach||current->owner!=owner||
       !MagazineDetachPairCurrent(*pair,*current->detach,pair->attached,now))return {};
    return *pair;
}
MagazinePackCounters ReadMagazinePackCounters()noexcept {
    MagazinePackCounters out;out.copies=magazineCopies.load();out.pairs=magazinePairs.load();out.fallbacks=magazineFallbacks.load();
    for(unsigned n=0;n<4;++n){out.roleCopies[n]=magazineRoleCopies[n].load();out.rolePairs[n]=magazineRolePairs[n].load();}return out;
}
BodyHolsterPackCounters ReadBodyHolsterPackCounters()noexcept {
    return {freeRightPoses.load(),freeRightPackedCopies.load(),freeRightPairedCopies.load(),freeRightFallbacks.load()};
}
bool Stop()noexcept {enabled.store(false,std::memory_order_release);pumpGuard.store({},std::memory_order_release);PublishOrdinaryEquipment({});freeRightGuard.store({},std::memory_order_release);visibilityGuard.store({},std::memory_order_release);visibilityReceipt.store({},std::memory_order_release);bool okay=true;for(auto entry:hooks)if(entry){auto r=MH_DisableHook(entry);okay&=r==MH_OK||r==MH_ERROR_DISABLED;}const auto until=GetTickCount64()+2000;while(inFlight.load()&&GetTickCount64()<until)Sleep(1);return okay&&!inFlight.load();}
void PublishM95ShotCapturePhase(unsigned phase)noexcept {if(phase<=5)m95ShotCapturePhase.store(phase,std::memory_order_release);}
void Report(std::ostream& out){out<<std::setprecision(9);out<<'{';weaponCapture.Report(out);out<<',';pumpPartCapture.Report(out);out<<',';m95ShotPartCapture.Report(out);out<<",\"free_right_poses\":"<<freeRightPoses<<",\"free_right_fallbacks\":"<<freeRightFallbacks<<",\"free_right_packed_copies\":"<<freeRightPackedCopies<<",\"free_right_paired_copies\":"<<freeRightPairedCopies;
    out<<",\"bolt_presentation\":{\"contacts\":"<<boltContacts.load()<<",\"poses\":"<<boltPoses.load()
       <<",\"copies\":"<<boltCopies.load()<<",\"pairs\":"<<boltPairs.load()<<",\"fallbacks\":"<<boltFallbacks.load()
       <<",\"evidence_drops\":"<<boltEvidenceDrops.load()<<",\"evidence_rejected\":"<<boltEvidenceRejected.load()<<",\"paired_targets\":[";
    {std::lock_guard lock(boltPairMutex);bool comma=false;
     for(const auto& phase:boltPairedPhases)if(phase.first&&phase.last)for(unsigned edge=0;edge<2;++edge){
        const auto& r=edge?*phase.last:*phase.first;const auto& s=r.source;const auto& t=*s.target;const auto& l=t.lease;
        if(comma)out<<',';comma=true;
        out<<"{\"edge\":\""<<(edge?"last":"first")<<"\",\"phase_pairs\":"<<phase.pairs<<",\"mechanism_phase\":"<<unsigned(s.mechanismPhase)
           <<",\"cycle\":"<<l.cycle<<",\"shot\":"<<l.shot<<",\"profile\":"<<t.profile<<",\"revision\":"<<t.revision
           <<",\"rig_fingerprint\":"<<s.calibration->rigFingerprint<<",\"packed_ns\":"<<r.packedNs<<",\"draw_serial\":"<<r.serial
           <<",\"travel\":"<<t.travel<<",\"rotation\":"<<t.rotation<<",\"source_sequence\":"<<t.inputSequence
           <<",\"source_observed_ns\":"<<t.observedNs<<",\"source_deadline_ns\":"<<t.deadlineNs
           <<",\"input_sequence\":"<<s.input.sequence<<",\"input_observed_ns\":"<<s.input.observedNs<<",\"input_deadline_ns\":"<<s.input.deadlineNs
           <<",\"held_sequence\":"<<l.sequence<<",\"held_observed_ns\":"<<l.observedNs<<",\"held_deadline_ns\":"<<l.deadlineNs
           <<",\"actor\":"<<l.owner.actor<<",\"actor_generation\":"<<l.owner.actorGeneration<<",\"equipment_generation\":"<<l.owner.equipGeneration
           <<",\"space\":"<<l.owner.space<<",\"item\":"<<l.item.id<<",\"item_generation\":"<<l.item.generation
           <<",\"mechanism\":"<<l.mechanism.id<<",\"mechanism_generation\":"<<l.mechanism.generation
           <<",\"mechanism_claim\":"<<t.mechanism.id<<",\"mechanism_hand\":"<<unsigned(t.mechanism.hand)<<",\"mechanism_parent\":"<<t.mechanism.prerequisiteClaim
           <<",\"gun_claim\":"<<t.gun.id<<",\"gun_hand\":"<<unsigned(t.gun.hand)<<",\"native_weapon\":"<<s.nativeOwner.weapon<<'}';
     }}out<<"]}";
    out<<",\"pump_presentation\":{\"contacts\":"<<pumpContacts.load()<<",\"attempts\":"<<pumpPlanAttempts.load()
       <<",\"poses\":"<<pumpPoses.load()<<",\"reach_rejects\":"<<pumpReachRejects.load()<<",\"pose_rejects\":"<<pumpPoseRejects.load()
       <<",\"copies\":"<<pumpCopies.load()<<",\"pairs\":"<<pumpPairs.load()<<",\"fallbacks\":"<<pumpFallbacks.load()
       <<",\"source_rejects\":"<<pumpSourceRejects.load()<<'}';
    out<<",\"magazine_presentation\":{\"poses\":"<<magazinePoses.load()<<",\"contacts\":"<<magazineContacts.load()
       <<",\"copies\":"<<magazineCopies.load()<<",\"pairs\":"<<magazinePairs.load()<<",\"fallbacks\":"<<magazineFallbacks.load()
       <<",\"attempts\":"<<magazinePlanAttempts.load()<<",\"binding_rejects\":"<<magazineBindingRejects.load()<<",\"plan_rejects\":"<<magazinePlanRejects.load()
       <<",\"reach_rejects\":"<<magazineReachRejects.load()<<",\"pose_rejects\":"<<magazinePoseRejects.load()
       <<",\"hidden_rejects\":"<<magazineHiddenRejects.load()<<",\"fresh_rejects\":"<<magazineFreshRejects.load()<<",\"reach_records\":[";
    for(unsigned n=0;n<magazineReachCount;++n){if(n)out<<',';const auto&r=magazineReachRecords[n];out<<"{\"sequence\":"<<r.sequence<<",\"role\":"<<r.role<<",\"error\":"<<r.error<<",\"solved\":"<<r.solved<<",\"clamped\":"<<r.clamped<<'}';}out<<"],\"fallback_observation\":{\"total\":"<<magazineFallbackJournal.Total()<<",\"dropped\":"<<magazineFallbackJournal.Dropped()<<",\"reason_counts\":{";
    for(unsigned n=0;n<MagazineFallbackNames.size();++n){if(n)out<<',';out<<'"'<<MagazineFallbackNames[n]<<"\":"<<magazineFallbackJournal.ReasonCount(MagazineFallbackReason(n));}
    out<<"},\"events\":[";bool firstFallback=true;
    magazineFallbackJournal.ForEach([&](const MagazineFallbackEvent& e){
        if(!firstFallback)out<<',';firstFallback=false;
        out<<"{\"flags\":"<<e.flags<<",\"now_ns\":"<<e.nowNs<<",\"draw_serial\":"<<e.drawSerial<<",\"source\":"<<e.source<<",\"bone_count\":"<<e.count<<",\"pack\":"<<e.pack
           <<",\"shot_present\":"<<e.shotPresent<<",\"shot_valid\":"<<e.shotValid<<",\"left_tracked\":"<<e.leftTracked<<",\"shot_coherent\":"<<e.coherent<<",\"shot_sequence\":"<<e.shotSequence<<",\"shot_deadline_qpc_ticks\":"<<e.shotDeadlineQpcTicks;
        const auto snapshot=[&](const char* name,const MagazineFallbackSnapshot& s){
            out<<",\""<<name<<"\":{\"present\":"<<s.present<<",\"target_present\":"<<s.targetPresent<<",\"role\":"<<s.role
               <<",\"owner\":{\"soldier\":"<<s.owner.soldier<<",\"weak\":"<<s.owner.weak<<",\"weapon\":"<<s.owner.weapon<<",\"actor_generation\":"<<s.owner.actorGeneration<<",\"equip_generation\":"<<s.owner.equipGeneration<<",\"space\":"<<s.owner.space<<'}'
               <<",\"target_owner\":{\"actor\":"<<s.targetOwner.actor<<",\"actor_generation\":"<<s.targetOwner.actorGeneration<<",\"equip_generation\":"<<s.targetOwner.equipGeneration<<",\"space\":"<<s.targetOwner.space<<'}'
               <<",\"cycle\":"<<s.cycle<<",\"input_sequence\":"<<s.input<<",\"reserve_sequence\":"<<s.reserveSequence<<",\"target_input_sequence\":"<<s.targetInput<<",\"input_observed_ns\":"<<s.inputObservedNs<<",\"target_observed_ns\":"<<s.targetObservedNs
               <<",\"item\":"<<s.item<<",\"item_generation\":"<<s.itemGeneration<<",\"hand_claim\":"<<s.hand.id<<",\"gun_claim\":"<<s.gun.id<<",\"hand_item\":"<<s.hand.item.id<<",\"gun_item\":"<<s.gun.item.id
               <<",\"removal_frame\":"<<s.removalFrame<<",\"replacement_frame\":"<<s.replacementFrame<<",\"deadlines_ns\":[";
            for(unsigned n=0;n<s.deadlines.size();++n){if(n)out<<',';out<<s.deadlines[n];}out<<"]}";
        };snapshot("original",e.original);snapshot("current",e.current);out<<'}';
    });out<<"]}}";
    out<<",\"belt_ammo\":{\"plans\":"<<beltPlans.load()<<",\"copies\":"<<beltCopies.load()<<",\"pairs\":"<<beltPairs.load()
       <<",\"fallbacks\":"<<beltFallbacks.load()<<",\"source_rejected\":"<<beltSourceRejected.load()<<",\"independent_magazine_instance\":false}";
    out<<",\"reload_presentation\":{\"poses\":"<<reloadPreviewPoses.load()<<",\"rejected\":"<<reloadPreviewRejected.load()
       <<",\"base_fallbacks\":"<<reloadPreviewFallbacks.load()<<",\"cycle_shell_hide_plans\":"<<reloadShellHidePlans.load()
        <<",\"cycle_shell_hide_copies\":"<<reloadShellHideCopies.load()<<",\"cycle_shell_hide_pairs\":"<<reloadShellHidePairs.load()
        <<",\"cycle_shell_hide_fallbacks\":"<<reloadShellHideFallbacks.load()<<",\"cycle_shell_source_rejects\":"<<reloadShellHideSourceRejects.load()
        <<",\"raw_contacts\":"<<reloadRawContacts.load()
       <<",\"ordinary_equipment_copies\":"<<ordinaryEquipmentCopies.load()<<",\"ordinary_equipment_pairs\":"<<ordinaryEquipmentPairs.load()
       <<",\"native_visible_contacts\":"<<reloadNativeVisibleContacts.load()<<",\"native_hidden_contacts\":"<<reloadNativeHiddenContacts.load()
       <<",\"owned_shell_visibility_poses\":"<<reloadOwnedShellVisibilityPoses.load()
       <<",\"verified_private_packs\":["<<reloadVerifiedPacks[0].load()<<','<<reloadVerifiedPacks[1].load()<<','<<reloadVerifiedPacks[2].load()<<']'
       <<",\"verified_paired_private_packs\":["<<reloadVerifiedPairedPacks[0].load()<<','<<reloadVerifiedPairedPacks[1].load()<<','<<reloadVerifiedPairedPacks[2].load()<<']'
       <<",\"verified_fallback_packs\":"<<reloadVerifiedFallbackPacks.load()
       <<",\"verified_paired_fallback_packs\":"<<reloadVerifiedPairedFallbackPacks.load()
       <<",\"native_hidden_shell_rejections\":"<<reloadPresentationRejections[unsigned(Bc2ReloadPresentationReason::NativeShellHidden)].load()
       <<",\"phase_poses\":["<<reloadPhasePoses[0].load()<<','<<reloadPhasePoses[1].load()<<','<<reloadPhasePoses[2].load()
       <<"],\"rejections_by_presentation_reason\":[";
    for(unsigned i=0;i<reloadPresentationRejections.size();++i){if(i)out<<',';out<<reloadPresentationRejections[i].load();}
    out<<"],\"native_animation_written\":false}";
    out<<",\"weapon_visibility\":{\"private_palette_consumer_integrated\":true,\"plans\":"<<visibilityPlans.load()
        <<",\"rejected\":"<<visibilityRejected.load()<<",\"verified_hidden_copies\":"<<visibilityHiddenCopies.load()
        <<",\"verified_shown_copies\":"<<visibilityShownCopies.load()<<",\"ordinary_fallbacks\":"<<visibilityFallbacks.load()
        <<",\"paired_pack_receipts\":"<<visibilityPairedReceipts.load()
        <<",\"native_animation_written\":false,\"submitted_visibility_verified\":false,\"headset_verified\":false}";
    out<<",\"authored_grip\":{\"experimental\":true,\"active_animation_verified\":false,\"copies\":"<<authoredGripCopies.load()<<",\"expired_before_output\":"<<authoredGripExpired.load()<<",\"status_counts\":[";
    for(unsigned n=0;n<unsigned(AuthoredGripStatus::Count);++n){if(n)out<<',';out<<authoredGripStatus[n].load();}
    out<<"],\"profiles\":[";bool authoredComma=false;for(const auto& p:AuthoredGripProfiles()){
        if(authoredComma)out<<',';authoredComma=true;out<<"{\"asset\":\""<<p.assetName<<"\",\"binding_digest\":\""<<p.bindingDigest<<"\",\"authored_rifle_support\":"<<(p.authoredRifleSupport?"true":"false")<<",\"authored_model_axis_evidence\":\""<<p.authoredAxisEvidence<<"\"}";
    }out<<"]}";
    out<<",\"weapon_profiles\":[";
    bool comma=false;for(const auto& item:WeaponProfiles()){if(comma)out<<',';comma=true;
        out<<"{\"asset_name\":\""<<item.assetName<<"\",\"profile_id\":\""<<item.core.stableId<<"\",\"revision\":"<<item.core.revision<<",\"features\":{";
        for(unsigned n=0;n<unsigned(interaction::WeaponFeature::Count);++n){if(n)out<<',';const auto feature=interaction::WeaponFeature(n);out<<'"'<<interaction::WeaponFeatureName(feature)<<"\":\""<<interaction::WeaponStatusName(interaction::WeaponFeatureStatus(&item.core,feature))<<'"';}out<<"}}";
    }out<<"],\"support_attached_poses\":"<<supportAttachedPoses.load()<<",\"shot_frame_races\":"<<shotFrameRaces.load()<<",\"shot_frame_expired\":"<<shotFrameExpired.load()<<",\"shot_frame_unavailable\":"<<shotFrameUnavailable.load()<<",\"support_frames_while_fire_blocked\":"<<supportFramesWhileFireBlocked.load()<<",\"pulse_requested\":"<<(pulse?"true":"false")<<",\"hands_requested\":"<<(hands?"true":"false")<<",\"torso_stabilized_poses\":"<<torsoPoses.load()<<",\"hidden_leaf_poses\":"<<hiddenLeafPoses.load()<<",\"tracked_poses\":"<<trackedPoses.load()<<",\"tracking_rejected\":"<<trackingRejected.load()<<",\"tracking_unavailable\":"<<trackingUnavailable.load()<<",\"weapon_outputs\":"<<weaponOutputs.load()<<",\"calibrations\":"<<calibrations.load()<<",\"pose_misses\":"<<poseMisses.load()<<",\"owners\":"<<owners.load()<<",\"copies\":"<<copies.load()<<",\"changed_copies\":"<<changedCopies.load()<<",\"paired_copies\":"<<pairedCopies.load()<<",\"rejected\":"<<rejected.load()<<",\"source_changes\":"<<sourceChanges.load()<<",\"packing_failures\":"<<packingFailures.load()<<",\"partial_poses\":"<<partialPoses.load()<<",\"fallback_failures\":"<<fallbackFailures.load()<<",\"hand_poses\":["<<handPoses[0].load()<<','<<handPoses[1].load()<<"],\"hand_calibrations\":["<<handCalibrations[0].load()<<','<<handCalibrations[1].load()<<"],\"native_animation_written\":false,\"hand_evidence\":[";
    for(unsigned n=0;n<handRecordCount;++n){const auto& r=handRecords[(handRecordNext+handRecords.size()-handRecordCount+n)%handRecords.size()];if(n)out<<',';out<<"{\"attachment_pending\":"<<(r.attachmentPending?"true":"false")<<",\"authored_grip\":"<<(r.authoredGrip?"true":"false")<<",\"support_attached\":"<<(r.supportAttached?"true":"false")<<",\"sight_attached\":"<<(r.sightAttached?"true":"false")<<",\"generation\":"<<r.generation<<",\"weapon\":"<<r.weapon<<",\"owner_generation\":"<<r.ownerGeneration<<",\"space\":"<<r.space<<",\"grip\":["<<r.grip.x<<','<<r.grip.y<<','<<r.grip.z<<"],\"native\":[";
        for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.nativeWeapon.values[k/4][k%4];}out<<"],\"eye_base\":[";
        for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.eyeBase.values[k/4][k%4];}out<<"],\"local_grips\":[";
        for(unsigned side=0;side<2;++side){if(side)out<<',';const auto p=r.localGrips[side];out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}out<<"],\"placed\":[";
        for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.placedWeapon.values[k/4][k%4];}out<<"],\"native_left_wrist\":[";for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.nativeLeft.values[k/4][k%4];}out<<"],\"native_right_wrist\":[";for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.nativeRight.values[k/4][k%4];}out<<"],\"right_wrist_matrix\":[";
        for(unsigned k=0;k<16;++k){if(k)out<<',';out<<r.resolvedRight.values[k/4][k%4];}out<<"],\"actor_position\":["<<r.actorPosition.x<<','<<r.actorPosition.y<<','<<r.actorPosition.z<<"],\"native_root\":["<<r.nativeRoot.x<<','<<r.nativeRoot.y<<','<<r.nativeRoot.z<<"],\"body_root\":["<<r.bodyRoot.x<<','<<r.bodyRoot.y<<','<<r.bodyRoot.z<<"],\"anatomy_root\":["<<r.anatomyRoot.x<<','<<r.anatomyRoot.y<<','<<r.anatomyRoot.z<<"],\"arms\":[";
        for(unsigned side=0;side<2;++side){if(side)out<<',';const auto vector=[&](math::Vec3 p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};out<<"{\"tracked\":"<<(r.tracked[side]?"true":"false")<<",\"grip\":";vector(r.grips[side]);out<<",\"shoulder\":";vector(r.shoulders[side]);out<<",\"anchor\":";vector(r.anchors[side]);out<<",\"target\":";vector(r.targets[side]);out<<",\"resolved\":";vector(r.resolved[side]);out<<",\"error\":"<<r.errors[side]<<'}';}out<<"]}";
    }out<<"],\"sight_preview\":{\"poses\":"<<sightPreviewPoses.load()<<",\"base_fallbacks\":"<<sightPreviewBaseFallbacks.load()<<",\"rejected\":"<<sightPreviewRejected.load()<<",\"records\":[";
    const auto matrix=[&](const math::Matrix4& m){out<<'[';for(unsigned n=0;n<16;++n){if(n)out<<',';out<<m.values[n/4][n%4];}out<<']';};
    const auto vector=[&](math::Vec3 p){out<<'['<<p.x<<','<<p.y<<','<<p.z<<']';};
    for(unsigned n=0;n<sightPreviewCount;++n){
        if(n)out<<',';const auto& r=sightPreviewRecords[(sightPreviewNext+sightPreviewRecords.size()-sightPreviewCount+n)%sightPreviewRecords.size()];
        out<<"{\"token\":"<<r.preview.token<<",\"generation\":"<<r.preview.generation<<",\"input_generation\":"<<r.inputGeneration<<",\"owner\":"<<r.owner<<",\"space\":"<<r.space<<",\"weapon\":"<<r.preview.weapon
           <<",\"physical_item\":"<<r.preview.physicalItem<<",\"grab_generation\":"<<r.preview.grabGeneration<<",\"phase\":"<<unsigned(r.preview.phase)
           <<",\"gesture_angle\":"<<r.preview.gestureAngle<<",\"native_progress\":"<<r.preview.nativeProgress<<",\"native_observed\":"<<(r.preview.nativeObserved?"true":"false")
           <<",\"raw_observed\":"<<(r.preview.rawObserved?"true":"false")<<",\"raw_progress\":"<<r.preview.rawProgress<<",\"raw_generation\":"<<r.preview.rawGeneration
           <<",\"angle\":"<<r.preview.angle<<",\"palm_error_m\":"<<r.palmError<<",\"reach_clamped\":"<<(r.clamped?"true":"false")<<",\"native_rear\":";matrix(r.nativeRear);
        out<<",\"native_front\":";matrix(r.nativeFront);out<<",\"rear\":";matrix(r.preview.rear);out<<",\"front\":";matrix(r.preview.front);
        out<<",\"wrist\":";matrix(r.preview.wrist);out<<",\"resolved_wrist\":";matrix(r.resolvedHand);out<<",\"raw_wrist\":";matrix(r.rawHand);
        out<<",\"palm_wrist\":";vector(r.preview.palmPointWrist);out<<",\"grasp\":";vector(r.preview.graspPoint);out<<'}';
    }
    out<<"]},\"hand_roles\":{\"binding_fingerprint\":\""<<std::hex<<handBindingFingerprint<<std::dec<<"\",\"rejected\":"<<handPoseRejected.load()<<",\"poses\":[";
    for(unsigned role=0;role<3;++role){if(role)out<<',';out<<handRolePoses[role].load();}
    out<<"],\"records\":[";
    for(unsigned n=0;n<handPoseCount;++n){
        if(n)out<<',';const auto& r=handPoseRecords[(handPoseNext+handPoseRecords.size()-handPoseCount+n)%handPoseRecords.size()];
        out<<"{\"generation\":"<<r.generation<<",\"owner\":"<<r.owner<<",\"space\":"<<r.space<<",\"weapon\":"<<r.weapon<<",\"role\":"<<unsigned(r.role)
           <<",\"squeeze\":"<<r.squeeze<<",\"trigger\":"<<r.trigger<<",\"touch_active\":"<<r.touchActive<<",\"touched\":"<<r.touched<<",\"wrist\":";matrix(r.wrist);out<<",\"grip\":";matrix(r.grip);
        out<<",\"mechanism_point\":";vector(r.mechanismPoint);out<<",\"fingers\":[";
        for(unsigned j=0;j<r.fingers.size();++j){if(j)out<<',';const auto& f=r.fingers[j];out<<"{\"index\":"<<f.index<<",\"parent\":"<<f.parent<<",\"reference\":";matrix(f.reference);out<<",\"native\":";matrix(f.native);out<<",\"posed\":";matrix(f.posed);out<<'}';}
        out<<"]}";
    }
    out<<"]},\"right_trigger\":{\"poses\":"<<triggerPoses.load()<<",\"failures\":"<<triggerFailures.load()<<",\"free_hand_binding_available\":"<<(rightHandBinding?"true":"false")<<",\"empty_hands_enabled\":false,\"mesh_trigger_enabled\":false,\"records\":[";
    for(unsigned n=0;n<triggerCount;++n){
        if(n)out<<',';const auto& t=triggerRecords[(triggerNext+triggerRecords.size()-triggerCount+n)%triggerRecords.size()];
        out<<"{\"generation\":"<<t.generation<<",\"weapon\":"<<t.weapon<<",\"trigger\":"<<t.trigger<<",\"wrist_preserved\":"<<(t.wristPreserved?"true":"false")<<",\"other_branches_preserved\":"<<(t.otherBranchesPreserved?"true":"false")<<",\"joints\":[";
        for(unsigned j=0;j<3;++j){if(j)out<<',';out<<"{\"index\":"<<t.indices[j]<<",\"before\":";matrix(t.before[j]);out<<",\"after\":";matrix(t.after[j]);out<<'}';}out<<"]}";
    }
    out<<"]},\"tracking_rejection_reasons\":{";for(unsigned n=0;n<rejectionCounts.size();++n){if(n)out<<',';out<<'"'<<rejectionNames[n]<<"\":"<<rejectionCounts[n].load();}out<<"}}";}
}
