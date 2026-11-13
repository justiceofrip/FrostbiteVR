#include "Bc2PhysicalFeed.h"
#include <atomic>
#include <limits>
#include <ostream>

namespace fvr::bc2 {
namespace {
using namespace interaction;
using namespace feed_interaction_detail;
using feed_mechanism_detail::Pose;
using FeedExit=Bc2PhysicalFeedExit;
const char* FeedExitName(FeedExit reason)noexcept {
    switch(reason){
    case FeedExit::None:return "none";case FeedExit::Updated:return "updated";
    case FeedExit::ApiUnavailable:return "api_unavailable";case FeedExit::ViewUnavailable:return "view_unavailable";
    case FeedExit::SelectionOrInput:return "selection_or_input_unverified";
    case FeedExit::StorageUnavailable:return "equipment_storage_unavailable";
    case FeedExit::MigrationUnverified:return "migration_unverified";
    case FeedExit::GunOrRawUnverified:return "gun_or_raw_unverified";
    case FeedExit::LifetimeExhausted:return "lifetime_exhausted";
    case FeedExit::SessionUnavailable:return "session_unavailable";
    case FeedExit::Blocked:return "mechanism_blocked";case FeedExit::PolicyStatus:return "policy_or_hand_status";}
    return "unknown";
}
std::atomic<std::uint64_t> nextLifetime=0;
std::atomic<std::shared_ptr<const Bc2PhysicalFeedPresentation>> publication;
struct FeedRetentionChecks {
    std::uint64_t checked=0,passed=0;
    unsigned firstFailure=~0u;
    bool Check(unsigned bit,bool pass)noexcept {
        checked|=1ull<<bit;if(pass)passed|=1ull<<bit;else if(firstFailure==~0u)firstFailure=bit;
        return pass;
    }
    bool operator==(const FeedRetentionChecks&)const=default;
};
constexpr const char* RetentionEntryPredicates[]={"publication_unavailable","not_cancelled","current_grip",
    "not_released","current_input_window","equipment_present"};
constexpr const char* GunPredicates[]={"right_claim","right_owner","right_item","right_gun_hold",
    "right_no_prerequisite","right_lease"};
constexpr const char* RawPredicates[]={"gun_hold","source_packet","source_native","source_input_window",
    "input_after_native","source_native_fresh","native_same_item","native_same_counts","recorded_left_evidence"};
constexpr const char* RetentionSlotPredicates[]={"slot_present","claim_present","session_present","session_ready",
    "no_pending","not_refused","motion_clean","no_release_wait","active_operation",
    "weapon_match","current_equipment","right_claim","right_owner","right_item","right_gun_hold",
    "right_no_prerequisite","right_lease","left_claim","left_token","left_prerequisite","held_packet",
    "held_native","held_contact","held_grip","held_owner","held_sequence","left_input_sequence",
    "left_deadline","held_input_window","recorded_left_evidence","held_native_fresh","native_identity","selected","current_native_matches"};
struct FeedRetentionSlot {
    FeedRetentionChecks checks{};
    std::uint64_t registrationKey=0,lifetime=0,claim=0,heldInputSequence=0,nativeSequence=0;
    std::int64_t heldObservedNs=0,heldDeadlineNs=0,nativeObservedNs=0,nativeDeadlineNs=0;
    unsigned selectionFailure=~0u;
};
struct FeedAdmissionDecision {
    bool rawPresent=false,currentGrip=false,sourceLookedUp=false,sourcePresent=false,sourceGrip=false;
    bool eligibilityEvaluated=false,available=false,heldBeforeOwnership=false,heldAtUpdate=false;
    bool needsSupply=false,supplied=false,pickupAwaitingRaw=false,chestNeutral=false,supplyNeutral=false;
    bool refused=false,gateFailed=false,motionDirty=false,nativePending=false,ammoPending=false,reloadPending=false;
    bool recoveryEstablished=false,recognitionEnabled=false,ownershipAccepted=false,ownershipInputValid=false;
    unsigned queryStatus=~0u,candidateOperation=~0u,candidatePart=~0u,contactStatus=~0u;
    unsigned contactStage=0,ownershipAction=0,ownershipReason=0,interactionEvidence=~0u;
    std::uint64_t leftClaim=0,rightClaim=0,resultClaim=0;
    bool operator==(const FeedAdmissionDecision&)const=default;
};
struct FeedAdmissionAttempt {
    ReloadStateOwner owner{};HandInteractionKey weapon{};
    HandInteractionSample current{},raw{};std::optional<HandInteractionSample> originalPacket;
    std::optional<HandClaim> leftBefore,rightBefore,ownershipClaim;
    Bc2PhysicalFeedDiagnostic diagnostic{};FeedAdmissionDecision decision{};
    std::uint64_t registrationKey=0,lifetime=0;
    unsigned leftFeedSlot=~0u;
    std::uint64_t leftFeedRegistrationKey=0,leftFeedLifetime=0;
    bool leftFeedClaimLost=false;
    bool cancel=false,contactGeometryEvaluated=false;
    double distanceMeters=0;float angleRadians=0,radiusMeters=0,maxAngleRadians=0;
    unsigned orientation=0;
    FeedRetentionChecks retention{},gun{};
    std::array<FeedRetentionSlot,8> retentionSlots{};
};
enum class FeedSupplyLossSite {TickSuspend,ExternalSuspend,ReleaseWait,ReleaseContinuationUnavailable,
    ReleaseContinuationUnverified,SupplyUpdate,SupplyBind,ActorRetirement,ClaimUnavailable,
    ClaimRenewalRejected,PolicyClear,ContextReset,LifetimeReset};
const char* SupplyLossSiteName(FeedSupplyLossSite site)noexcept {
    switch(site){
    case FeedSupplyLossSite::TickSuspend:return "tick_suspend";
    case FeedSupplyLossSite::ExternalSuspend:return "external_suspend";
    case FeedSupplyLossSite::ReleaseWait:return "release_wait";
    case FeedSupplyLossSite::ReleaseContinuationUnavailable:return "release_continuation_unavailable";
    case FeedSupplyLossSite::ReleaseContinuationUnverified:return "release_continuation_unverified";
    case FeedSupplyLossSite::SupplyUpdate:return "supply_update";
    case FeedSupplyLossSite::SupplyBind:return "supply_bind";
    case FeedSupplyLossSite::ActorRetirement:return "actor_retirement";
    case FeedSupplyLossSite::ClaimUnavailable:return "claim_unavailable";
    case FeedSupplyLossSite::ClaimRenewalRejected:return "claim_renewal_rejected";
    case FeedSupplyLossSite::PolicyClear:return "policy_clear";
    case FeedSupplyLossSite::ContextReset:return "context_reset";
    case FeedSupplyLossSite::LifetimeReset:return "lifetime_reset";}
    return "unknown";
}
// Copied before the mutating call. A lost hand is not a lost reserve resource,
// an insertion request, or evidence that any native transfer completed.
struct FeedHeldSupplyLoss {
    FeedAdmissionAttempt before{};
    AmmoSupplyObject held{};
    std::optional<Bc2FeedSupplyCustody> custody;
    FeedSupplyLossSite site=FeedSupplyLossSite::TickSuspend;
    bool nativePending=false,refused=false,gateFailed=false,motionDirty=false,supplyNeutral=false,releaseWait=false;
};
// The arbiter may already have revoked the claim before Tick. Keep the exact
// last observed receipt separately from the current hand; never renew it.
struct FeedHeldMechanismLoss {
    FeedAdmissionAttempt before{};
    HandClaimToken token{};
    std::optional<HandClaim> heldClaim;
    std::optional<HandInteractionSample> heldInput,releaseInput;
    std::uint64_t nativeSequence=0;
    std::int64_t nativeObservedNs=0,nativeDeadlineNs=0,releaseDeadlineNs=0;
    FeedSupplyLossSite site=FeedSupplyLossSite::TickSuspend;
    bool nativePending=false,refused=false,gateFailed=false,motionDirty=false,releaseWait=false;
    bool outcomeEvaluated=false,completion=false,dispatch=false;
    unsigned outcomeStatus=0;
    Bc2PhysicalFeedSuspendContext external{};
};
struct FeedAdmissionObservation {
    std::optional<FeedAdmissionAttempt> firstPress;
    FeedAdmissionAttempt latestChange{};
    std::optional<FeedAdmissionAttempt> failedRetention;
    std::optional<FeedHeldSupplyLoss> firstSupplyLoss;
    std::optional<FeedHeldMechanismLoss> firstMechanismLoss;
};
std::atomic<std::int64_t> admissionObservationUntil=0;
std::atomic<std::shared_ptr<const FeedAdmissionObservation>> admissionObservation;
bool SameAdmissionDecision(const FeedAdmissionAttempt& a,const FeedAdmissionAttempt& b)noexcept {
    const auto& x=a.diagnostic;const auto& y=b.diagnostic;
    if(a.retention!=b.retention||a.gun!=b.gun)return false;
    for(unsigned i=0;i<a.retentionSlots.size();++i){const auto& p=a.retentionSlots[i];const auto& q=b.retentionSlots[i];
        if(p.checks!=q.checks||p.registrationKey!=q.registrationKey||p.lifetime!=q.lifetime||p.claim!=q.claim||
            p.selectionFailure!=q.selectionFailure)return false;}
    // Time/sequence and sub-radius jitter alone do not allocate or publish on
    // every frame. The first squeeze remains intact when a gate later changes.
    return a.owner==b.owner&&a.weapon==b.weapon&&a.registrationKey==b.registrationKey&&a.lifetime==b.lifetime&&
        a.leftFeedSlot==b.leftFeedSlot&&a.leftFeedRegistrationKey==b.leftFeedRegistrationKey&&
        a.leftFeedLifetime==b.leftFeedLifetime&&a.leftFeedClaimLost==b.leftFeedClaimLost&&
        a.cancel==b.cancel&&a.decision==b.decision&&x.exit==y.exit&&x.flags==y.flags&&x.status==y.status&&
        a.current.released==b.current.released&&a.current.tracked==b.current.tracked&&a.current.focused==b.current.focused&&
        (!a.originalPacket||!b.originalPacket||(a.originalPacket->released==b.originalPacket->released&&
            a.originalPacket->tracked==b.originalPacket->tracked&&a.originalPacket->focused==b.originalPacket->focused))&&
        x.rawChecked==y.rawChecked&&x.rawPassed==y.rawPassed&&x.admissionChecked==y.admissionChecked&&
        x.admissionPassed==y.admissionPassed&&x.selectionFailure==y.selectionFailure&&
        x.idleChecked==y.idleChecked&&x.idlePassed==y.idlePassed&&x.policyEvaluated==y.policyEvaluated;
}
std::uint64_t Lifetime()noexcept {
    auto old=nextLifetime.load();
    while(old!=UINT64_MAX)if(nextLifetime.compare_exchange_weak(old,old+1))return old+1;
    return 0;
}
bool SupportedAsset(std::string_view asset)noexcept {
    for(const auto& row:RegisteredBc2FeedProfiles())if(row.geometry.asset==asset&&FindBc2FeedRegistration(row.key)==&row)return true;
    return false;
}
std::uint16_t ChainMask(unsigned count)noexcept {
    return count&&count<=MaxBc2FeedChainBones?std::uint16_t(((1u<<count)-1u)<<2):0;
}
std::uint64_t PartKey(std::uint64_t key,unsigned part)noexcept {
    return key>UINT64_MAX-part?key-(UINT64_MAX-part):key+part;
}
bool SamePacket(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
bool Window(const HandInteractionSample& s,std::int64_t now)noexcept {
    return s.sequence&&s.observedNs>0&&s.deadlineNs>s.observedNs&&s.deadlineNs-s.observedNs<=100000000&&
        now>=s.observedNs&&now<s.deadlineNs&&s.focused&&s.tracked[0]&&s.tracked[1];
}
bool NativeHand(const Bc2PhysicalFeedSample& s)noexcept {
    return s.input.owner.actor==((std::uint64_t(s.owner.weak)<<32)|s.owner.soldier)&&
        s.input.owner.actorGeneration==s.owner.actorGeneration&&s.input.owner.space==s.owner.space&&
        s.weapon.id==s.owner.weapon&&s.weapon.generation==s.input.owner.equipGeneration;
}
bool CurrentEquipment(const FeedEquipmentNativeView& original,const ReloadStateOwner& owner,
    const WeaponEquipmentIdentity& equipment,const std::optional<AmmoResourceBinding>& binding,
    const Bc2FeedRegistration& row)noexcept {
    const auto& o=original.identity.owner;
    return equipment.weapon==owner.weapon&&o.player==owner.player&&o.soldier==owner.soldier&&o.weak==owner.weak&&
        o.actorGeneration==owner.actorGeneration&&o.weapon==owner.weapon&&original.binding.data==equipment.data&&
        original.binding.persistence==equipment.persistence&&equipment.Asset()==row.geometry.asset&&
        (!binding||(binding->owner==owner&&binding->context.resource==original.binding.context.resource&&
            binding->data==equipment.data&&binding->persistence==equipment.persistence&&
            binding->inventory==original.binding.inventory&&binding->switching==original.binding.switching));
}
bool SelectedMeshesCurrent(const SelectedMeshesSnapshot& m,const ReloadStateOwner& owner,const FeedEquipmentNativeView& v,
    const Bc2FeedRegistration& row,std::int64_t now,unsigned& failure)noexcept {
    const auto& g=row.geometry;
    const auto reject=[&](unsigned reason,bool rejected){if(rejected)failure=reason;return rejected;};
    if(reject(18,m.owner!=owner)||reject(19,!m.sequence)||reject(20,!m.configurationPathVerified)||reject(21,!m.configurationPathPointer)||
       reject(22,!ReloadDescriptorText(m.configurationPath,g.configurationPath))||reject(23,!ReloadDescriptorText(m.weaponName,g.asset))||
       reject(24,m.weaponData!=v.binding.data)||reject(25,m.inventory!=v.binding.inventory)||
       reject(26,m.observedNs<=0)||reject(27,now<m.observedNs)||reject(28,now>=m.deadlineNs)||
       reject(29,m.stateCount!=1)||reject(30,!m.soleConfiguredArray))return false;
    unsigned matches=0;const auto& state=m.states[0];
    if(reject(31,state.array!=m.soleConfiguredArray)||reject(32,!state.count)||reject(33,state.count>state.meshes.size()))return false;
    for(unsigned i=0;i<state.count;++i)if(ReloadDescriptorText(state.meshes[i].assetPath,g.meshPath)&&state.meshes[i].address>=0x10000)++matches;
    failure=matches==1?0:34;return matches==1;
}
bool Selected(const Bc2PhysicalFeedSample& s,const FeedEquipmentNativeView& v,const Bc2FeedRegistration& row,unsigned& failure)noexcept {
    const auto& g=row.geometry;
    const auto reject=[&](unsigned reason,bool rejected){if(rejected)failure=reason;return rejected;};
    if(reject(1,FindBc2FeedRegistration(row.key,v.config,s.asset,s.configurationPath,s.meshPath,s.rigFingerprint)!=&row)||
       reject(2,!s.equipment)||reject(3,!CurrentEquipment(v,s.owner,*s.equipment,s.binding,row))||
       reject(4,s.binding&&!AmmoResourceBindingFresh(*s.binding,s.owner,s.input.nowNs))||
       reject(5,!s.selectedRigVerified)||reject(6,!s.selected)||reject(7,!NativeHand(s))||reject(8,!s.raw.valid)||reject(9,s.raw.owner!=s.owner)||
       reject(10,s.raw.rig.soldier!=s.owner.soldier)||reject(11,s.raw.rig.weak!=s.owner.weak)||
       reject(12,s.raw.rigFingerprint!=g.rigFingerprint)||reject(13,s.frameSequence!=s.raw.inputEvidence.sequence)||
       reject(14,!Pose(s.raw.rawLeftWristWorldMeters))||reject(15,!Pose(s.raw.weaponWorldMeters))||
       reject(16,!FeedEquipmentViewFresh(v,s.input.nowNs))||reject(17,v.identity.owner!=s.owner))return false;
    return SelectedMeshesCurrent(*s.selected,s.owner,v,row,s.input.nowNs,failure);
}
math::Matrix4 BlendRigid(const math::Matrix4& a,const math::Matrix4& b,float t)noexcept {
    if(t<=0)return a;if(t>=1)return b;
    return reload_insertion_detail::BlendRigid(a,b,t);
}
}
std::shared_ptr<const Bc2PhysicalFeedPresentation> ReadBc2PhysicalFeedPresentation(
    const ReloadStateOwner& owner,std::int64_t now)noexcept {
    const auto p=publication.load(std::memory_order_acquire);
    return p&&p->owner==owner&&Window(p->displayInput?*p->displayInput:p->input,now)?p:nullptr;
}
void RequestBc2PhysicalFeedAdmissionObservation(std::int64_t now)noexcept {
    if(now>0&&now<=INT64_MAX-1000000000ll)admissionObservationUntil.store(now+1000000000ll,std::memory_order_relaxed);
}
namespace {
void WriteFeedAdmission(std::ostream& out,const FeedAdmissionObservation* observation,bool compact){
    if(!observation){out<<"{\"observationOnly\":true,\"status\":\"capture_pending_or_no_press\"}";return;}
    const auto owner=[&](const HandInteractionOwner& o){out<<'['<<o.actor<<','<<o.actorGeneration<<','<<o.equipGeneration<<','<<o.space<<']';};
    const auto input=[&](const HandInteractionSample& s){out<<"{\"owner\":";owner(s.owner);
        out<<",\"sequence\":"<<s.sequence<<",\"observedNs\":"<<s.observedNs<<",\"deadlineNs\":"<<s.deadlineNs<<",\"nowNs\":"<<s.nowNs
            <<",\"focused\":"<<s.focused<<",\"tracked\":["<<s.tracked[0]<<','<<s.tracked[1]<<"],\"released\":["<<s.released[0]<<','<<s.released[1]<<"]}";};
    const auto token=[&](const HandClaimToken& t){
        out<<"{\"id\":"<<t.id<<",\"kind\":"<<unsigned(t.kind)<<",\"hand\":"<<unsigned(t.hand)<<",\"owner\":";owner(t.owner);
        out<<",\"item\":["<<t.item.id<<','<<t.item.generation<<"],\"contact\":["<<t.contact.id<<','<<t.contact.generation
            <<"],\"prerequisiteClaim\":"<<t.prerequisiteClaim;};
    const auto claim=[&](const std::optional<HandClaim>& c){if(!c){out<<"null";return;}token(c->token);
        out<<",\"inputSequence\":"<<c->inputSequence<<",\"deadlineNs\":"<<c->deadlineNs<<'}';};
    const auto scalar=[&](double x){if(std::isfinite(x))out<<x;else out<<"null";};
    const auto status=[&](unsigned x){if(x==~0u)out<<"null";else out<<x;};
    const auto checks=[&](const FeedRetentionChecks& c,const auto& names){
        out<<"\"checked\":"<<c.checked<<",\"passed\":"<<c.passed<<",\"firstFailure\":";
        if(c.firstFailure==~0u)out<<"null";else out<<'"'<<names[c.firstFailure]<<'"';
    };
    const auto attempt=[&](const FeedAdmissionAttempt& a,bool essential=false){const auto& d=a.diagnostic;const auto& v=a.decision;
        out<<"{\"owner\":["<<a.owner.player<<','<<a.owner.soldier<<','<<a.owner.weak<<','<<a.owner.weapon<<','
            <<a.owner.actorGeneration<<','<<a.owner.equipGeneration<<','<<a.owner.space<<"],\"weapon\":["<<a.weapon.id<<','<<a.weapon.generation
            <<"],\"registrationKey\":"<<a.registrationKey<<",\"lifetime\":"<<a.lifetime<<",\"cancel\":"<<a.cancel<<",\"current\":";input(a.current);
        out<<",\"currentGrip\":"<<v.currentGrip<<",\"rawPresent\":"<<v.rawPresent<<",\"rawInput\":";input(a.raw);
        out<<",\"sourceLookedUp\":"<<v.sourceLookedUp<<",\"originalPacket\":";
        if(a.originalPacket){out<<"{\"grip\":"<<v.sourceGrip<<",\"input\":";input(*a.originalPacket);out<<'}';}else out<<"null";
        out<<",\"sourceNative\":{\"sequence\":"<<d.sourceNativeSequence<<",\"observedNs\":"<<d.sourceNativeObservedNs<<",\"deadlineNs\":"<<d.sourceNativeDeadlineNs
            <<"},\"exit\":\""<<FeedExitName(d.exit)<<'"'
            <<",\"rawChecked\":"<<d.rawChecked<<",\"rawPassed\":"<<d.rawPassed<<",\"gun\":{";
        checks(a.gun,GunPredicates);out<<'}';
        if(!essential){out<<",\"admissionChecked\":"<<d.admissionChecked<<",\"admissionPassed\":"<<d.admissionPassed
            <<",\"selectionFailure\":";status(d.selectionFailure);
        out<<",\"idleChecked\":"<<d.idleChecked<<",\"idlePassed\":"<<d.idlePassed
            <<",\"neutralAgeNs\":"<<d.neutralAgeNs<<",\"candidate\":{\"status\":";status(v.queryStatus);
        out<<",\"operation\":";status(v.candidateOperation);out<<",\"part\":";status(v.candidatePart);out<<"},\"eligibility\":";
        if(!v.eligibilityEvaluated)out<<"null";else out<<"{\"available\":"<<v.available<<",\"heldBeforeOwnership\":"<<v.heldBeforeOwnership
            <<",\"heldAtUpdate\":"<<v.heldAtUpdate<<",\"needsSupply\":"<<v.needsSupply<<",\"supplied\":"<<v.supplied
            <<",\"pickupAwaitingRaw\":"<<v.pickupAwaitingRaw<<",\"chestNeutral\":"<<v.chestNeutral<<",\"supplyNeutral\":"<<v.supplyNeutral
            <<",\"refused\":"<<v.refused<<",\"gateFailed\":"<<v.gateFailed<<",\"motionDirty\":"<<v.motionDirty
            <<",\"nativePending\":"<<v.nativePending<<",\"ammoPending\":"<<v.ammoPending<<",\"reloadPending\":"<<v.reloadPending<<'}';
        out<<",\"contact\":";
        if(!v.contactStage)out<<"null";else{out<<"{\"kind\":\""<<(v.contactStage==1?"idle_cover":"candidate")<<"\",\"status\":";status(v.contactStatus);
            out<<",\"geometryEvaluated\":"<<a.contactGeometryEvaluated<<",\"distanceMeters\":";if(a.contactGeometryEvaluated)scalar(a.distanceMeters);else out<<"null";
            out<<",\"angleRadians\":";if(a.contactGeometryEvaluated)scalar(a.angleRadians);else out<<"null";
            out<<",\"radiusMeters\":";scalar(a.radiusMeters);out<<",\"maxAngleRadians\":";scalar(a.maxAngleRadians);out<<",\"orientation\":"<<a.orientation<<'}';}}
        out<<",\"leftBefore\":";claim(a.leftBefore);out<<",\"rightBefore\":";claim(a.rightBefore);
        out<<",\"ownership\":{\"action\":\""<<(v.ownershipAction==1?"acquire":v.ownershipAction==2?"renew":v.ownershipAction==3?"supply_evidence":"not_attempted")
            <<"\",\"inputValid\":"<<v.ownershipInputValid<<",\"accepted\":"<<v.ownershipAccepted<<",\"reason\":"<<v.ownershipReason<<",\"claim\":";claim(a.ownershipClaim);
        out<<"},\"leftFeedClaim\":";
        if(a.leftFeedSlot==~0u)out<<"null";
        else out<<"{\"slot\":"<<a.leftFeedSlot<<",\"registrationKey\":"<<a.leftFeedRegistrationKey
            <<",\"lifetime\":"<<a.leftFeedLifetime<<",\"lost\":"<<a.leftFeedClaimLost<<'}';
        if(essential){out<<",\"omitted\":[\"admissionChecks\",\"candidate\",\"eligibility\",\"contact\",\"policy\",\"retention\"]}";return;}
        out<<",\"policy\":{\"evaluated\":"<<d.policyEvaluated<<",\"recoveryEstablished\":"<<v.recoveryEstablished
            <<",\"recognitionEnabled\":"<<v.recognitionEnabled<<",\"equipmentStatus\":"<<(d.status&255)
            <<",\"interactionStatus\":"<<((d.status>>8)&255)<<",\"interactionEvidence\":";status(v.interactionEvidence);
        out<<",\"reloadStatus\":"<<((d.status>>16)&255)<<",\"reloadPhase\":";status(d.reloadPhase);out<<"},\"retention\":{";
        checks(a.retention,RetentionEntryPredicates);out<<",\"slots\":[";bool comma=false;
        for(unsigned i=0;i<a.retentionSlots.size();++i){const auto& r=a.retentionSlots[i];if(!r.checks.checked)continue;
            if(comma)out<<',';comma=true;out<<"{\"index\":"<<i<<",\"registrationKey\":"<<r.registrationKey
                <<",\"lifetime\":"<<r.lifetime<<",\"claim\":"<<r.claim<<',';checks(r.checks,RetentionSlotPredicates);
            out<<",\"selectionFailure\":";status(r.selectionFailure);
            out<<",\"heldInput\":{\"sequence\":"<<r.heldInputSequence<<",\"observedNs\":"<<r.heldObservedNs<<",\"deadlineNs\":"<<r.heldDeadlineNs
                <<"},\"heldNative\":{\"sequence\":"<<r.nativeSequence<<",\"observedNs\":"<<r.nativeObservedNs<<",\"deadlineNs\":"<<r.nativeDeadlineNs<<"}}";
        }out<<"]}}";
    };
    out<<"{\"observationOnly\":true,\"retainedPress\":"<<(observation->firstPress?"true":"false")<<",\"unreachedFieldsAreDefaultValues\":true,\"ownerFields\":[\"player\",\"soldier\",\"weak\",\"weapon\",\"actorGeneration\",\"equipGeneration\",\"space\"]"
        <<",\"inputOwnerFields\":[\"actor\",\"actorGeneration\",\"equipGeneration\",\"space\"],\"firstPress\":";
    if(observation->firstPress)attempt(*observation->firstPress,compact);else out<<"null";
    out<<",\"latestChange\":";
    // First failures take priority over later changes, including neutral ones.
    // Their compact payload cannot grow merely because the grip was released;
    // the full observation still carries the complete latest change.
    if(compact&&(observation->latestChange.decision.currentGrip||observation->failedRetention||
        observation->firstMechanismLoss||observation->firstSupplyLoss))out<<"{\"omitted\":\"payload_limit\"}";
    else attempt(observation->latestChange,compact);
    out<<",\"failedRetention\":";if(observation->failedRetention)attempt(*observation->failedRetention);else out<<"null";
    const auto names=[&](const auto& values){out<<'[';bool comma=false;for(const auto* name:values){if(comma)out<<',';comma=true;out<<'"'<<name<<'"';}out<<']';};
    out<<",\"retentionEntryPredicates\":";names(RetentionEntryPredicates);
    out<<",\"retentionSlotPredicates\":";names(RetentionSlotPredicates);
    out<<",\"rawPredicates\":";names(RawPredicates);out<<",\"gunPredicates\":";names(GunPredicates);
    out<<",\"firstMechanismLoss\":";
    if(!observation->firstMechanismLoss)out<<"null";
    else {const auto& loss=*observation->firstMechanismLoss;
        out<<"{\"site\":\""<<SupplyLossSiteName(loss.site)<<"\",\"tickContext\":"<<(loss.site!=FeedSupplyLossSite::ExternalSuspend)
            <<",\"before\":";attempt(loss.before);
        out<<",\"heldToken\":";token(loss.token);out<<'}';
        out<<",\"heldClaim\":";claim(loss.heldClaim);
        out<<",\"heldInput\":";if(loss.heldInput)input(*loss.heldInput);else out<<"null";
        out<<",\"heldNative\":{\"sequence\":"<<loss.nativeSequence<<",\"observedNs\":"<<loss.nativeObservedNs
            <<",\"deadlineNs\":"<<loss.nativeDeadlineNs<<"},\"operationBefore\":";status(loss.before.diagnostic.operation);
        out<<",\"nativePending\":"<<loss.nativePending<<",\"refused\":"<<loss.refused<<",\"gateFailed\":"<<loss.gateFailed
            <<",\"motionDirty\":"<<loss.motionDirty<<",\"releaseWait\":"<<loss.releaseWait<<",\"releaseInput\":";
        if(loss.releaseInput)input(*loss.releaseInput);else out<<"null";
        out<<",\"releaseDeadlineNs\":"<<loss.releaseDeadlineNs<<",\"outcome\":{\"evaluated\":"<<loss.outcomeEvaluated
            <<",\"completion\":"<<loss.completion<<",\"dispatch\":"<<loss.dispatch<<",\"status\":"<<loss.outcomeStatus<<"},\"external\":{";
        const char* source="unknown";
        switch(loss.external.source){
        case Bc2PhysicalFeedSuspendSource::ReloadOwnerLoss:source="reload_owner_loss";break;
        case Bc2PhysicalFeedSuspendSource::HandOwnershipReset:source="hand_ownership_reset";break;
        case Bc2PhysicalFeedSuspendSource::ArmingFixture:source="arming_fixture";break;
        case Bc2PhysicalFeedSuspendSource::ReloadFixture:source="reload_fixture";break;
        case Bc2PhysicalFeedSuspendSource::InputApplyRejected:source="input_apply_rejected";break;
        default:break;}
        out<<"\"source\":\""<<source<<"\",\"detail\":"<<loss.external.detail<<",\"sourceFlags\":"<<loss.external.sourceFlags
            <<",\"preserveMagazine\":"<<loss.external.preserveMagazine<<"}}";
    }
    out<<",\"firstSupplyLoss\":";
    if(!observation->firstSupplyLoss)out<<"null";
    else {const auto& loss=*observation->firstSupplyLoss;
        const auto key=[&](const HandInteractionKey& k){out<<'['<<k.id<<','<<k.generation<<']';};
        const auto identity=[&](const AmmoSupplyIdentity& i){out<<"{\"owner\":";owner(i.owner);
            out<<",\"weapon\":";key(i.weapon);out<<",\"profile\":";key(i.profile);out<<",\"pool\":";key(i.pool);
            out<<",\"trackingEpoch\":"<<i.trackingEpoch<<'}';};
        const auto object=[&](const AmmoSupplyObject& o){out<<"{\"identity\":";identity(o.identity);out<<",\"item\":";key(o.item);
            out<<",\"claim\":";claim(o.claim);out<<",\"family\":"<<unsigned(o.family)<<",\"units\":"<<o.units<<'}';};
        const auto body=[&](const Bc2FeedBodyAmmo& b){const auto& n=b.native;const auto& o=n.identity.owner;
            const auto& r=n.binding.context.resource;
            out<<"{\"owner\":["<<o.player<<','<<o.soldier<<','<<o.weak<<','<<o.weapon<<','<<o.actorGeneration<<','<<o.equipGeneration<<','<<o.space
                <<"],\"resource\":["<<r.actor<<','<<r.actorGeneration<<','<<r.weapon<<','<<r.weaponGeneration
                <<"],\"context\":["<<n.binding.context.equipGeneration<<','<<n.binding.context.space
                <<"],\"registrationKey\":"<<b.registrationKey<<",\"lifetime\":"<<b.lifetime
                <<",\"sequence\":"<<n.sequence<<",\"observedNs\":"<<n.observedNs<<",\"deadlineNs\":"<<n.deadlineNs
                <<",\"counts\":["<<n.counts.loaded<<','<<n.counts.reserve<<','<<n.counts.capacity<<"]}";};
        out<<"{\"site\":\""<<SupplyLossSiteName(loss.site)<<"\",\"tickContext\":"<<(loss.site!=FeedSupplyLossSite::ExternalSuspend)
            <<",\"before\":";attempt(loss.before);
        out<<",\"held\":";object(loss.held);
        out<<",\"operationBefore\":";status(loss.before.diagnostic.operation);
        out<<",\"nativePending\":"<<loss.nativePending<<",\"refused\":"<<loss.refused<<",\"gateFailed\":"<<loss.gateFailed<<",\"motionDirty\":"<<loss.motionDirty
            <<",\"supplyNeutral\":"<<loss.supplyNeutral<<",\"releaseWait\":"<<loss.releaseWait<<",\"custody\":";
        if(!loss.custody)out<<"null";
        else {const auto& c=*loss.custody;out<<"{\"object\":";object(c.object);out<<",\"handOwner\":";owner(c.handOwner);
            out<<",\"origin\":";body(c.origin);out<<",\"context\":";body(c.context);
            out<<",\"source\":{\"identity\":";identity(c.source.identity);
            out<<",\"sequence\":"<<c.source.sequence<<",\"observedNs\":"<<c.source.observedNs<<",\"deadlineNs\":"<<c.source.deadlineNs
                <<",\"reserveUnits\":"<<c.source.reserveUnits<<",\"objectUnits\":"<<c.source.objectUnits
                <<",\"verified\":"<<c.source.verified<<"},\"pending\":"<<bool(c.pending)<<'}';}
        out<<'}';}
    out<<'}';
}
}
void WriteBc2PhysicalFeedAdmissionObservation(std::ostream& out,std::ostream* compact){
    const auto observation=admissionObservation.load(std::memory_order_acquire);
    WriteFeedAdmission(out,observation.get(),false);
    if(compact)WriteFeedAdmission(*compact,observation.get(),true);
}
struct Bc2PhysicalFeed::State {
    // Single Gameplay writer; Report reads only relaxed atomics. These are
    // independent observations, never a coherent native snapshot or authority.
    struct Diagnostics {
        std::atomic<std::uint64_t> ticks=0,transitions=0,blocked=0,pending=0,claims=0,suspends=0;
        std::atomic<std::uint64_t> submissions=0,submitRefusals=0,receipts=0,gateRefusals=0;
        std::atomic<FeedExit> reason=FeedExit::None,lastIssue=FeedExit::None;
        std::atomic<unsigned> flags=0,status=0,asset=0,lastIssueFlags=0,lastIssueStatus=0;
        std::atomic<std::uint64_t> registrationKey=0,lastSelectedKey=0;
        std::atomic<int> loaded=-1,reserve=-1,capacity=-1;
    } diagnostics;
    struct PolicyContact {RigIdentity rig{};math::Matrix4 local{},weaponWorld{};};
    struct Packet {ReloadStateOwner owner{};HandInteractionSample input{};HandInteractionKey weapon{};
        std::optional<FeedEquipmentNativeView> native;bool grip=false,reload=false;std::optional<math::Matrix4> bodyFromHand;
        std::optional<PolicyContact> contact;std::shared_ptr<const SelectedMeshesSnapshot> selection;};
    struct ReleaseWait {HandInteractionSample input{};std::uint64_t heldInput=0;std::int64_t deadlineNs=0;};
    struct SupportNeutral {HandInteractionSample input{};FeedPhysicalOperation operation=FeedPhysicalOperation::None;};
    struct HandGrip {
        HandClaimToken claim{};
        FeedInteractionTargetKind kind=FeedInteractionTargetKind::None;
        math::Matrix4 targetFromWrist{},weaponFromContact{};
    };
    struct OriginalDrop {
        Bc2FeedPose pose{};
        math::Matrix4 pouchBasis{},chainBasis{};
        std::int64_t releasedNs=0;
        std::uint16_t carriedPartsMask=0;
        bool applied=false;
    };
    struct Equipment {
        explicit Equipment(const Bc2FeedRegistration& row)noexcept
            :registration(row),pose(row.geometry.poses[unsigned(Bc2FeedPoseId::Resting)]){}
        const Bc2FeedRegistration& registration;
        FeedEquipmentNativeView original{};
        HandInteractionKey weapon{};FeedSupplyKey supply{};FeedKey mechanism{};
        FeedInteractionProfile motions{};
        Bc2FeedPose pose{},gestureStart{};
        FeedCoverState cover=FeedCoverState::Closed;FeedContainerState container=FeedContainerState::Attached;
        FeedSeatState seat=FeedSeatState::Seated;FeedRefillObservation refill{FeedRefillState::NotRequired,0,0};
        float coverRadians=0,gestureCoverRadians=0;std::uint64_t lifetime=0,revision=0,lastInput=0,rejectedInput=0;
        std::unique_ptr<FeedEquipmentSession> session;
        Bc2FeedAmmoSupply ammunitionSupply;
        bool supplyNeutral=false;
        std::uint64_t supplyAcquiredClaim=0;
        std::optional<FeedEquipmentSubmission> pending;
        std::optional<HandClaimToken> claim;
        std::optional<HandClaim> observedMechanismClaim;
        std::optional<HandGrip> handGrip;
        std::optional<FeedObservedContact> recoveryContact;
        std::optional<math::Matrix4> presentationSource;
        std::shared_ptr<const Bc2PhysicalFeedPresentation> lastPresentation;
        std::optional<FeedEquipmentGate> gate;
        math::Matrix4 pouchBasis{},chainBasis{};
        std::uint16_t carriedPartsMask=0;
        std::optional<OriginalDrop> originalDrop;
        std::optional<Bc2FeedPose> originalExtractionOrigin;
        bool originalExtractionRestart=false;
        bool gateFailed=false;
        bool refused=false,motionDirty=false,completedHold=false;
        FeedPhysicalOperation activeOperation=FeedPhysicalOperation::None;
        std::optional<FeedEquipmentSessionSample> neutral;
        std::optional<SupportNeutral> supportNeutral;
        std::optional<ReleaseWait> releaseWait;
        bool Blocked()const noexcept {return pending||refused||gateFailed||cover!=FeedCoverState::Closed||
            container!=FeedContainerState::Attached||seat!=FeedSeatState::Seated||
            (session&&session->State().status!=FeedEquipmentStatus::Complete);}
    };
    Bc2PhysicalFeedApi api{};std::array<Packet,32> packets{};unsigned nextPacket=0;
    // Gameplay alone mutates this pair; worker snapshots read its immutable copy.
    std::shared_ptr<const FeedAdmissionObservation> admission;
    bool admissionPressed=false;
    std::array<std::unique_ptr<Equipment>,8> equipment{};
    std::uint64_t retirementEvent=0;
    explicit State(Bc2PhysicalFeedApi a):api(a){}
    static FeedAdmissionAttempt LossAttempt(const Equipment& e,const HandInteractionSample& input,
        const HandInteraction& hands,FeedExit reason,const FeedAdmissionAttempt* attempt,
        const Bc2PhysicalFeedDiagnostic* diagnostic)noexcept {
        FeedAdmissionAttempt a;if(attempt)a=*attempt;if(diagnostic)a.diagnostic=*diagnostic;
        if(!attempt){a.owner=e.original.identity.owner;a.weapon=e.weapon;a.current=input;}
        a.registrationKey=e.registration.key;a.lifetime=e.lifetime;a.diagnostic.exit=reason;
        a.leftBefore=hands.Current(InteractionHand::Left);a.rightBefore=hands.Current(InteractionHand::Right);
        a.diagnostic.operation=unsigned(e.activeOperation);
        if(e.session){const auto& session=e.session->State();a.diagnostic.reloadPhase=unsigned(session.session.reload.phase);
            a.diagnostic.status=unsigned(session.status)|(unsigned(session.session.interaction.status)<<8)|
                (unsigned(session.session.reload.status)<<16);}
        return a;
    }
    static FeedHeldSupplyLoss SupplyLoss(const Equipment& e,const HandInteractionSample& input,
        const HandInteraction& hands,FeedSupplyLossSite site,FeedExit reason,
        const FeedAdmissionAttempt* attempt=nullptr,const Bc2PhysicalFeedDiagnostic* diagnostic=nullptr)noexcept {
        FeedHeldSupplyLoss loss;loss.site=site;loss.held=*e.ammunitionSupply.Held();loss.custody=e.ammunitionSupply.Custody();
        loss.before=LossAttempt(e,input,hands,reason,attempt,diagnostic);
        loss.nativePending=bool(e.pending);loss.refused=e.refused;loss.gateFailed=e.gateFailed;
        loss.motionDirty=e.motionDirty;loss.supplyNeutral=e.supplyNeutral;loss.releaseWait=bool(e.releaseWait);
        return loss;
    }
    FeedHeldMechanismLoss MechanismLoss(const Equipment& e,const HandInteractionSample& input,
        const HandInteraction& hands,FeedSupplyLossSite site,FeedExit reason,
        const FeedAdmissionAttempt* attempt=nullptr,const Bc2PhysicalFeedDiagnostic* diagnostic=nullptr)const noexcept {
        FeedHeldMechanismLoss loss;loss.site=site;loss.token=*e.claim;
        loss.before=LossAttempt(e,input,hands,reason,attempt,diagnostic);
        const auto left=hands.Current(InteractionHand::Left);
        if(left&&left->token==loss.token)loss.heldClaim=left;
        else if(e.observedMechanismClaim&&e.observedMechanismClaim->token==loss.token)loss.heldClaim=e.observedMechanismClaim;
        for(const auto& packet:packets)if(packet.owner==e.original.identity.owner&&packet.weapon==e.weapon&&
            packet.input.sequence==(loss.heldClaim?loss.heldClaim->inputSequence:e.lastInput)){
            loss.heldInput=packet.input;
            if(packet.native){loss.nativeSequence=packet.native->sequence;loss.nativeObservedNs=packet.native->observedNs;
                loss.nativeDeadlineNs=packet.native->deadlineNs;}break;}
        loss.nativePending=bool(e.pending);loss.refused=e.refused;loss.gateFailed=e.gateFailed;loss.motionDirty=e.motionDirty;
        loss.releaseWait=bool(e.releaseWait);
        if(e.releaseWait){loss.releaseInput=e.releaseWait->input;loss.releaseDeadlineNs=e.releaseWait->deadlineNs;}
        return loss;
    }
    void Suspend(const HandInteractionSample& input,HandInteraction& hands)noexcept {
        diagnostics.suspends.fetch_add(1,std::memory_order_relaxed);
        for(auto& e:equipment)if(e){if(e->claim){(void)hands.Release(input,*e->claim);e->claim.reset();}
            e->handGrip.reset();e->completedHold=false;
            e->ammunitionSupply.Suspend(input,hands);e->supplyNeutral=false;e->releaseWait.reset();
            if(e->session)e->motionDirty=true;
            // A rejected current packet may be newer than the last renderer
            // source. Its delayed release cannot re-arm this equipment lifetime.
            const auto& owner=e->original.identity.owner;
            if(input.owner==HandInteractionOwner{(std::uint64_t(owner.weak)<<32)|owner.soldier,
                owner.actorGeneration,e->weapon.generation,owner.space})
                e->rejectedInput=(std::max)(e->rejectedInput,input.sequence);
            e->neutral.reset();e->supportNeutral.reset();if(e->session){FeedEquipmentSessionSample sample;sample.nowNs=input.nowNs;(void)e->session->Update(sample);}}
    }
    template<class Observe>void RetireActors(const HandInteractionSample& input,HandInteraction& hands,Observe&& observe)noexcept {
        if(!api.retirement)return;
        for(unsigned n=0;n<64;++n){
            const auto proof=api.retirement(api.context,retirementEvent);
            if(!proof||!FeedEquipmentActorRetirementValid(*proof)||proof->event<=retirementEvent||proof->observedNs>input.nowNs)break;
            // This is a native actor-end receipt, not an ammunition success.
            // The backend preserves dispatched/ambiguous debt in its audit.
            for(auto& slot:equipment)if(slot&&
                AmmoResourceSameNativeActor(slot->original.identity,proof->previous)&&
                slot->original.identity.owner.actorGeneration==proof->previous.owner.actorGeneration){
                observe(*slot,FeedSupplyLossSite::ActorRetirement,FeedExit::None);
                if(slot->claim)(void)hands.Release(input,*slot->claim);
                slot->ammunitionSupply.Suspend(input,hands);
                slot.reset();
            }
            for(auto& packet:packets)if(packet.owner.player==proof->previous.owner.player&&
                packet.owner.soldier==proof->previous.owner.soldier&&packet.owner.weak==proof->previous.owner.weak&&
                packet.owner.actorGeneration==proof->previous.owner.actorGeneration)packet={};
            retirementEvent=proof->event;
        }
    }
    void PublishGate(Equipment& e,const Bc2PhysicalFeedSample& s,const HandInteraction& hands)noexcept {
        if((!api.publishGate&&!api.publishGateObserved)||!e.lifetime)return;
        const bool debt=e.pending||e.refused||e.cover!=FeedCoverState::Closed||e.container!=FeedContainerState::Attached||
            e.seat!=FeedSeatState::Seated||(e.session&&e.session->State().status!=FeedEquipmentStatus::Complete);
        const FeedEquipmentGate gate{e.original.binding.context.resource,e.lifetime,++e.revision,debt,!debt};
        const auto result=api.publishGateObserved?api.publishGateObserved(api.context,gate):
            api.publishGate(api.context,gate)?FeedEquipmentGateResult::Published:FeedEquipmentGateResult::Rejected;
        if(result==FeedEquipmentGateResult::Published){e.gate=gate;e.gateFailed=false;return;}
        diagnostics.gateRefusals.fetch_add(1,std::memory_order_relaxed);
        const auto left=hands.Current(InteractionHand::Left),right=hands.Current(InteractionHand::Right);
        // Contention cannot replace an accepted blocker or create a claim. An
        // unchanged blocker permits the existing held gesture to reach the
        // ordinary raw/native/policy checks below; none of its leases are renewed here.
        const bool retained=result==FeedEquipmentGateResult::Busy&&!e.gateFailed&&e.gate&&
            e.gate->resource==gate.resource&&e.gate->lifetime==gate.lifetime&&
            e.gate->blocksFire&&!e.gate->closedVerified&&gate.blocksFire&&!gate.closedVerified&&
            e.claim&&left&&right&&left->token==*e.claim&&left->token.kind==HandClaimKind::Mechanism&&
            left->token.owner==s.input.owner&&left->token.item==s.weapon&&left->deadlineNs>s.input.nowNs&&
            right->token.kind==HandClaimKind::GunHold&&!right->token.prerequisiteClaim&&
            right->token.owner==s.input.owner&&right->token.item==s.weapon&&right->deadlineNs>s.input.nowNs&&
            left->token.prerequisiteClaim==right->token.id&&s.grip&&!s.cancel&&!s.input.released[0]&&
            Window(s.input,s.input.nowNs)&&NativeHand(s)&&s.weapon==e.weapon&&s.owner==e.original.identity.owner&&
            s.equipment&&CurrentEquipment(e.original,s.owner,*s.equipment,s.binding,e.registration);
        if(!retained)e.gateFailed=true;
    }
    static bool SameResource(const Equipment& e,const FeedEquipmentNativeView& v)noexcept {
        return e.original.binding.context.resource==v.binding.context.resource&&
            e.original.binding.data==v.binding.data&&e.original.binding.persistence==v.binding.persistence;
    }
    FeedMechanismSnapshot Snapshot(Equipment& e,const FeedAmmunitionSnapshot& a,const HandInteractionSample& input)noexcept {
        const auto& profile=e.registration.mechanism;
        FeedMechanismSnapshot s;s.owner=a.owner;s.mechanism=e.mechanism;s.profileId=profile.id;s.profileRevision=profile.revision;
        s.sequence=a.sequence;s.inputSequence=input.sequence;s.observedNs=a.observedNs;s.reconciled=true;
        s.cover=e.cover;s.latch=FeedLatchState::Absent;s.container=e.container;s.feed=e.seat;s.cycle=FeedCycleState::Absent;
        s.attached=e.container==FeedContainerState::Attached?e.supply:FeedSupplyKey{};
        s.seated=e.seat==FeedSeatState::Seated?e.supply:FeedSupplyKey{};
        s.supply={e.supply,profile.ammunitionKind,FeedSupplyState::Usable};s.refill=e.refill;
        // Contacts are frozen gesture sources, never the guided hand pose.
        for(auto op:{FeedPhysicalOperation::OpenCover,FeedPhysicalOperation::DetachContainer,FeedPhysicalOperation::SeatFeed}){
            if(op==FeedPhysicalOperation::OpenCover&&e.cover==FeedCoverState::Open)op=FeedPhysicalOperation::CloseCover;
            if(op==FeedPhysicalOperation::DetachContainer&&e.container==FeedContainerState::Detached)op=FeedPhysicalOperation::AttachContainer;
            const auto& motion=e.motions.operations[unsigned(op)];const auto part=unsigned(feed_mechanism_detail::OperationPart(op));
            s.contacts[part]={profile.contacts[part].role,motion.kind==FeedMotionKind::FreePlacement?
                feed_interaction_detail::Multiply(motion.placement.sourceFromGrip,motion.placement.weaponFromSource):feed_interaction_detail::Multiply(motion.motionFromContact,motion.weaponFromMotion)};
        }
        if(e.recoveryContact){const auto part=unsigned(feed_mechanism_detail::OperationPart(e.activeOperation));
            if(part<s.contacts.size())s.contacts[part]=*e.recoveryContact;}
        return s;
    }
    FeedAmmunitionSnapshot Ammo(const Equipment& e,const FeedEquipmentNativeView& v)noexcept {
        return {FeedEquipmentOwner(v.identity.owner),e.mechanism,e.lifetime,v.sequence,v.observedNs,v.deadlineNs,v.counts,e.supply,true};
    }
    Bc2FeedBodyAmmo BodyAmmo(const Equipment& e,const FeedEquipmentNativeView& v)const noexcept {
        return {v,e.registration.key,e.lifetime,e.supply};
    }
    FeedReloadBinding Binding(const Equipment& e)noexcept {
        const auto& profile=e.registration.mechanism;
        FeedReloadBinding b;b.key={e.registration.key,e.original.binding.context.resource.weaponGeneration};
        b.owner=FeedEquipmentOwner(e.original.identity.owner);b.mechanism=e.mechanism;b.profileId=profile.id;b.profileRevision=profile.revision;b.verified=true;
        for(auto op:{FeedPhysicalOperation::OpenCover,FeedPhysicalOperation::DetachContainer,FeedPhysicalOperation::AttachContainer,
            FeedPhysicalOperation::SeatFeed,FeedPhysicalOperation::CloseCover})b.operations[unsigned(op)]=true;return b;
    }
    FeedAmmunitionAuthority Authority(const Equipment& e,const FeedAmmunitionSnapshot& a,FeedPhysicalOperation op)noexcept {
        const auto binding=Binding(e);FeedAmmunitionAuthority x;
        x.key={PartKey(e.registration.key,unsigned(op)),e.lifetime};x.binding=binding.key;x.owner=a.owner;x.mechanism=e.mechanism;
        x.lifetime=e.lifetime;x.profileId=e.registration.mechanism.id;x.profileRevision=e.registration.mechanism.revision;x.operation=op;x.verified=true;
        x.effect=FeedAmmunitionEffect::PhysicalOnly;
        if(op==FeedPhysicalOperation::AttachContainer){x.effect=FeedAmmunitionEffect::RefillLoaded;x.supply=e.supply;
            x.rounds=(std::min)(a.counts.capacity-a.counts.loaded,a.counts.reserve);}
        return x;
    }
    Bc2FeedPose PoseInWeaponFrame(const Equipment& e,const math::Matrix4& currentWeapon)const noexcept {
        auto pose=e.pose;const auto inverse=Inverse(currentWeapon);
        // Preparing a gesture is not movement of the retained object. In
        // particular, detached/no-supply suspension may prepare on every
        // publication. Always derive from the original carry basis rather
        // than accumulating float inverse/reprojection error in custody.
        if(e.carriedPartsMask&1)pose.weaponFromPouch=feed_interaction_detail::Multiply(feed_interaction_detail::Multiply(e.pose.weaponFromPouch,e.pouchBasis),inverse);
        if(e.carriedPartsMask&ChainMask(e.registration.geometry.chainCount))pose.weaponFromFeedRoot=feed_interaction_detail::Multiply(feed_interaction_detail::Multiply(e.pose.weaponFromFeedRoot,e.chainBasis),inverse);
        return pose;
    }
    static void PresentOriginalDrop(const Equipment& e,Bc2PhysicalFeedPresentation& p,std::int64_t now)noexcept {
        p.hiddenPartsMask=0;p.droppedOriginalAtNs=0;
        if(!e.originalDrop||!e.originalDrop->applied||e.container!=FeedContainerState::Detached)return;
        const auto& drop=*e.originalDrop;
        const auto parts=ComposeBc2FeedPresentation(e.supply,drop.pose);if(!parts)return;
        constexpr std::int64_t duration=750000000;
        const auto elapsed=std::clamp(now-drop.releasedNs,std::int64_t{0},duration);
        const auto seconds=double(elapsed)*1e-9;const auto fall=float(4.905*seconds*seconds);
        p.weaponFromPouch=parts->weaponFromPouch;p.weaponFromChain=parts->weaponFromChain;
        p.carriedPartsMask=drop.carriedPartsMask;p.droppedOriginalAtNs=drop.releasedNs;
        p.pouchCarryWeaponWorldMeters=drop.pouchBasis;p.chainCarryWeaponWorldMeters=drop.chainBasis;
        p.pouchCarryWeaponWorldMeters.values[3][1]-=fall;p.chainCarryWeaponWorldMeters.values[3][1]-=fall;
        if(elapsed==duration)p.hiddenPartsMask=drop.carriedPartsMask;
    }
    static void ReleaseUnsubmittedOriginal(Equipment& e,const FeedEquipmentNativeView& view,
        const HandInteractionSample& input)noexcept {
        if(!e.originalExtractionOrigin||!e.carriedPartsMask||!e.session||e.pending||e.refused||e.gateFailed||
           e.originalDrop||e.ammunitionSupply.Custody()||e.activeOperation!=FeedPhysicalOperation::DetachContainer||
           e.container!=FeedContainerState::Attached||view.counts.loaded||!input.focused||
           !input.tracked[0]||!input.tracked[1]||!input.released[0]||!Window(input,input.nowNs)||
           !FeedEquipmentViewFresh(view,input.nowNs)||e.original.identity!=view.identity||!SameResource(e,view))return;
        const auto prior=e.session->State();
        if(prior.ammunition.pending||prior.ammunition.needsReconciliation||prior.session.reload.pending||
           prior.ammunition.snapshot.counts!=view.counts)return;
        // This empty original never left native custody. A real release that
        // could not dispatch extraction cancels only its provisional carry;
        // the open lid, resource, receipts and session watermarks stay owned.
        e.pose=*e.originalExtractionOrigin;e.gestureStart=e.pose;e.carriedPartsMask=0;
        e.originalExtractionRestart=true;e.motionDirty=true;e.handGrip.reset();
        e.recoveryContact.reset();e.presentationSource.reset();e.neutral.reset();e.supportNeutral.reset();
    }
    void PoseTarget(Equipment& e,const FeedInteractionResult& r,const math::Matrix4& weaponWorld)noexcept {
        if(!r.target)return;const auto& g=e.registration.geometry;const auto op=e.activeOperation;
        if(op==FeedPhysicalOperation::OpenCover||op==FeedPhysicalOperation::CloseCover){
            const float direction=op==FeedPhysicalOperation::OpenCover?1.f:-1.f;
            e.coverRadians=std::clamp(e.gestureCoverRadians+direction*float(r.progress)*g.coverTravelRadians,
                (std::min)(0.f,g.coverTravelRadians),(std::max)(0.f,g.coverTravelRadians));return;}
        if(r.targetKind!=FeedInteractionTargetKind::Object)return;
        if(op==FeedPhysicalOperation::DetachContainer&&e.container==FeedContainerState::Attached&&
           !e.ammunitionSupply.Custody()&&!e.carriedPartsMask&&!e.originalExtractionOrigin)
            e.originalExtractionOrigin=e.pose;
        // An abandoned extraction's old recovery proof cannot relocate the
        // restored box. Only new measured movement mints a usable proof again.
        if(e.originalExtractionRestart&&(r.status==FeedInteractionStatus::Moving||r.status==FeedInteractionStatus::Endpoint)&&
           (Distance(e.pose.weaponFromPouch,*r.target)>1e-5||Angle(e.pose.weaponFromPouch,*r.target)>1e-5))
            e.originalExtractionRestart=false;
        const auto destination=op==FeedPhysicalOperation::DetachContainer?Bc2FeedPoseId::PouchWithdrawn:
            op==FeedPhysicalOperation::AttachContainer?Bc2FeedPoseId::PouchAttachedFeedUnseated:Bc2FeedPoseId::FeedSeated;
        const auto& end=g.poses[unsigned(destination)];const auto& motion=e.motions.operations[unsigned(op)];
        const auto total=Distance(e.presentationSource.value_or(motion.placement.weaponFromSource),motion.placement.weaponFromDestination);
        const auto remaining=Distance(*r.target,motion.placement.weaponFromDestination);
        const float t=total>1e-5?float(std::clamp(1-remaining/total,0.,1.)):0.f;
        auto articulation=e.gestureStart;
        for(unsigned i=0;i+1<g.chainCount;++i)articulation.parentFromChainChild[i]=BlendRigid(e.gestureStart.parentFromChainChild[i],end.parentFromChainChild[i],t);
        articulation.weaponFromFeedRoot=BlendRigid(e.gestureStart.weaponFromFeedRoot,end.weaponFromFeedRoot,t);
        articulation.weaponFromPouch=op==FeedPhysicalOperation::SeatFeed?e.pose.weaponFromPouch:
            BlendRigid(e.gestureStart.weaponFromPouch,end.weaponFromPouch,t);
        const auto p=op==FeedPhysicalOperation::SeatFeed?PlaceBc2FeedTerminal(e.supply,articulation,*r.target):
            PlaceBc2FeedPouch(e.supply,articulation,*r.target);
        if(p){e.pose=articulation;e.pose.weaponFromPouch=p->weaponFromPouch;e.pose.weaponFromFeedRoot=p->weaponFromChain[0];
            e.chainBasis=weaponWorld;e.carriedPartsMask=ChainMask(g.chainCount);
            if(op!=FeedPhysicalOperation::SeatFeed){e.pouchBasis=weaponWorld;e.carriedPartsMask|=1;}}
    }
    void GripTarget(Equipment& e,const HandClaimToken& claim,const math::Matrix4& rawWrist,
        const math::Matrix4& target,FeedInteractionTargetKind kind,bool acquired)noexcept {
        if(!Pose(rawWrist)||!Pose(target)||kind==FeedInteractionTargetKind::None){e.handGrip.reset();return;}
        if(acquired||!e.handGrip||e.handGrip->claim!=claim||e.handGrip->kind!=kind){
            auto relative=feed_interaction_detail::Multiply(rawWrist,Inverse(target));
            // Contact is the measured anatomical wrist frame, not the cover
            // bone or hinge. Constrain position, retaining the actual grasp's
            // orientation. A loose object instead retains the complete actual
            // wrist-to-object pickup offset, including its translation.
            if(kind==FeedInteractionTargetKind::Contact)for(unsigned axis=0;axis<3;++axis)relative.values[3][axis]=0;
            if(!Pose(relative)){e.handGrip.reset();return;}
            e.handGrip=HandGrip{claim,kind,relative,target};
        }
        e.handGrip->weaponFromContact=target;
    }
    void Applied(Equipment& e,const FeedAmmunitionReceipt& r)noexcept {
        switch(r.command.request.completion.candidate.operation){
        case FeedPhysicalOperation::OpenCover:e.cover=FeedCoverState::Open;break;
        case FeedPhysicalOperation::DetachContainer:e.container=FeedContainerState::Detached;e.seat=FeedSeatState::Unseated;
            e.supplyNeutral=false;break;
        case FeedPhysicalOperation::AttachContainer:e.container=FeedContainerState::Attached;e.seat=FeedSeatState::Unseated;
            e.refill={FeedRefillState::Applied,e.lifetime,r.command.request.id};e.carriedPartsMask=0;break;
        case FeedPhysicalOperation::SeatFeed:e.seat=FeedSeatState::Seated;e.carriedPartsMask=0;break;
        case FeedPhysicalOperation::CloseCover:e.cover=FeedCoverState::Closed;break;
        default:break;
        }
    }
};
Bc2PhysicalFeed::Bc2PhysicalFeed(Bc2PhysicalFeedApi api)noexcept {try{state_=std::make_unique<State>(api);}catch(...){}}
Bc2PhysicalFeed::~Bc2PhysicalFeed()=default;
bool Bc2PhysicalFeed::BlocksFire(const ReloadStateOwner& owner,const WeaponEquipmentIdentity& equipment,
    const std::optional<AmmoResourceBinding>& binding)const noexcept {
    if(!state_)return false;
    for(const auto& e:state_->equipment)if(e&&CurrentEquipment(e->original,owner,equipment,binding,e->registration)&&e->Blocked())return true;
    return false;
}
std::optional<FeedEquipmentGate> Bc2PhysicalFeed::RetainedGate(const ReloadStateOwner& owner,const WeaponEquipmentIdentity& equipment,
    const std::optional<AmmoResourceBinding>& binding)const noexcept {
    if(!state_)return {};
    for(const auto& e:state_->equipment)if(e){const auto& original=e->original;
        if(!CurrentEquipment(original,owner,equipment,binding,e->registration))continue;
        auto retained=e->gate;
        if(state_->api.readGate)if(const auto current=state_->api.readGate(state_->api.context,original.binding.context.resource))retained=current;
        if(retained){retained->blocksFire=retained->blocksFire||e->Blocked();return retained;}
    }return {};
}
bool Bc2PhysicalFeed::HasCurrentOwnerEvidence(const ReloadStateOwner& owner,std::int64_t now)const noexcept {
    if(!state_||!state_->api.read)return false;
    const auto view=state_->api.read(state_->api.context,owner,now);
    if(!view||view->identity.owner!=owner||!FeedEquipmentViewFresh(*view,now))return false;
    const auto* registration=FindBc2FeedRegistration(view->config);if(!registration)return false;
    for(const auto& e:state_->equipment)if(e&&e->session&&e->original.identity==view->identity&&
        &e->registration==registration&&State::SameResource(*e,*view))return true;
    return false;
}
void Bc2PhysicalFeed::Suspend(const HandInteractionSample& input,HandInteraction& hands,Bc2PhysicalFeedSuspendContext context)noexcept {
    publication.store({},std::memory_order_release);if(!state_)return;
    std::optional<FeedHeldSupplyLoss> loss;
    std::optional<FeedHeldMechanismLoss> mechanismLoss;
    if(input.nowNs>0&&input.nowNs<admissionObservationUntil.load(std::memory_order_relaxed))
        for(const auto& e:state_->equipment)if(e){
            if(!loss&&(!state_->admission||!state_->admission->firstSupplyLoss)&&e->ammunitionSupply.Held())
                loss=State::SupplyLoss(*e,input,hands,FeedSupplyLossSite::ExternalSuspend,FeedExit::None);
            if(!mechanismLoss&&(!state_->admission||!state_->admission->firstMechanismLoss)&&e->claim){
                mechanismLoss=state_->MechanismLoss(*e,input,hands,FeedSupplyLossSite::ExternalSuspend,FeedExit::None);
                mechanismLoss->external=context;}
        }
    state_->Suspend(input,hands);
    if(loss||mechanismLoss)try{
        auto next=state_->admission?std::make_shared<FeedAdmissionObservation>(*state_->admission):std::make_shared<FeedAdmissionObservation>();
        if(loss){next->latestChange=loss->before;next->firstSupplyLoss=std::move(loss);}
        if(mechanismLoss){next->latestChange=mechanismLoss->before;next->firstMechanismLoss=std::move(mechanismLoss);}
        state_->admission=next;admissionObservation.store(std::move(next),std::memory_order_release);
    }catch(...){} // Observation failure cannot affect suspension/custody.
}
Bc2PhysicalFeedResult Bc2PhysicalFeed::Tick(const Bc2PhysicalFeedSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    const auto blocksFire=[&](){return s.equipment&&BlocksFire(s.owner,*s.equipment,s.binding);};
    const auto blocksEquipment=[&](){
        if(state_&&s.equipment)for(const auto& e:state_->equipment)
            if(e&&CurrentEquipment(e->original,s.owner,*s.equipment,s.binding,e->registration)&&
               (e->pending||e->claim||e->ammunitionSupply.Held()))return true;
        return false;
    };
    Bc2PhysicalFeedResult out;out.blocksFire=blocksFire();out.blocksEquipment=blocksEquipment();publication.store({},std::memory_order_release);
    if(!state_){out.diagnostic.exit=FeedExit::StorageUnavailable;return out;}auto& state=*state_;const auto now=s.input.nowNs;
    // Neutral loss of a paired raw/native packet can remove the presentation
    // while debt still blocks fire. Journal that gate change without inventing
    // a squeeze or extending any input/native/claim lease.
    const bool requested=now>0&&now<admissionObservationUntil.load(std::memory_order_relaxed);
    const bool observe=requested&&(s.grip||out.blocksFire)&&SupportedAsset(s.asset);
    bool observeSupply=false;
    if(requested)for(const auto& slot:state.equipment)if(slot&&(slot->ammunitionSupply.Held()||slot->claim)){observeSupply=true;break;}
    const bool observePress=observe&&s.grip;
    const bool newPress=observePress&&(!state.admissionPressed||!state.admission||state.admission->latestChange.owner!=s.owner||
        state.admission->latestChange.weapon!=s.weapon);
    // A changed claim, lifetime, or resumed observation is not a new squeeze.
    // Keep supply loss until a newer actual held packet follows observed neutral.
    const bool newSupplyPress=newPress&&state.admission&&!s.input.released[0]&&
        state.admission->latestChange.current.released[0]&&state.admission->latestChange.current.owner==s.input.owner&&
        s.input.sequence>state.admission->latestChange.current.sequence;
    state.admissionPressed=observePress;
    std::optional<FeedAdmissionAttempt> observation;
    if(observe||observeSupply){observation.emplace();auto& a=*observation;
        a.owner=s.owner;a.weapon=s.weapon;a.current=s.input;a.raw=s.raw.inputEvidence;a.cancel=s.cancel;
        a.decision.currentGrip=s.grip;a.decision.rawPresent=s.raw.valid;
        a.leftBefore=hands.Current(InteractionHand::Left);a.rightBefore=hands.Current(InteractionHand::Right);
        a.decision.leftClaim=a.leftBefore?a.leftBefore->token.id:0;a.decision.rightClaim=a.rightBefore?a.rightBefore->token.id:0;
        if(a.leftBefore)for(unsigned i=0;i<state.equipment.size();++i){const auto& slot=state.equipment[i];
            if(slot&&slot->claim&&*slot->claim==a.leftBefore->token){
                a.leftFeedSlot=i;a.leftFeedRegistrationKey=slot->registration.key;a.leftFeedLifetime=slot->lifetime;break;}}
    }
    State::Equipment* e=nullptr;
    const Bc2FeedRegistration* registration=nullptr;
    std::optional<FeedEquipmentNativeView> presentationView;
    std::optional<FeedHeldSupplyLoss> supplyLoss;
    std::optional<FeedHeldMechanismLoss> mechanismLoss;
    const auto captureSupplyLoss=[&](const State::Equipment& equipment,FeedSupplyLossSite site,FeedExit reason){
        if(observation&&!supplyLoss&&(newSupplyPress||!state.admission||!state.admission->firstSupplyLoss)&&equipment.ammunitionSupply.Held())
            supplyLoss=State::SupplyLoss(equipment,s.input,hands,site,reason,&*observation,&out.diagnostic);
    };
    const auto captureMechanismLoss=[&](const State::Equipment& equipment,FeedSupplyLossSite site,FeedExit reason){
        if(observation&&!mechanismLoss&&(newSupplyPress||!state.admission||!state.admission->firstMechanismLoss)&&equipment.claim)
            mechanismLoss=state.MechanismLoss(equipment,s.input,hands,site,reason,&*observation,&out.diagnostic);
    };
    const auto captureHeldLoss=[&](const State::Equipment& equipment,FeedSupplyLossSite site,FeedExit reason){
        captureSupplyLoss(equipment,site,reason);captureMechanismLoss(equipment,site,reason);
    };
    const auto suspend=[&](FeedExit reason,FeedSupplyLossSite site=FeedSupplyLossSite::TickSuspend){
        for(const auto& slot:state.equipment)if(slot)captureHeldLoss(*slot,site,reason);
        state.Suspend(s.input,hands);
    };
    const auto finish=[&](FeedExit reason){
        // A missed raw/render packet cannot close an already owned lid.
        // Recompose only local mechanical state, corroborated by a CURRENT
        // native owner/item/selection. The saved raw packet stays unchanged;
        // no motion, claim renewal, hand target or transaction uses this path.
        if(!out.presentation&&presentationView&&!s.cancel&&NativeHand(s)&&Window(s.input,now)&&s.equipment&&s.selected){
            const auto& current=presentationView;
            if(current&&current->identity.owner==s.owner&&FeedEquipmentViewFresh(*current,now))
                for(const auto& slot:state.equipment)if(slot&&slot->lastPresentation){
                    const auto& prior=*slot->lastPresentation;const auto& g=slot->registration.geometry;
                    unsigned failure=0;
                    if(prior.owner!=s.owner||prior.weapon!=s.weapon||prior.lifetime!=slot->lifetime||
                       prior.input.owner!=s.input.owner||slot->original.identity!=current->identity||
                       !State::SameResource(*slot,*current)||!CurrentEquipment(slot->original,s.owner,*s.equipment,s.binding,slot->registration)||
                       s.asset!=g.asset||(!s.configurationPath.empty()&&s.configurationPath!=g.configurationPath)||
                       (!s.meshPath.empty()&&s.meshPath!=g.meshPath)||(s.rigFingerprint&&s.rigFingerprint!=g.rigFingerprint)||
                       (s.binding&&!AmmoResourceBindingFresh(*s.binding,s.owner,now))||
                       !SelectedMeshesCurrent(*s.selected,s.owner,*current,slot->registration,now,failure)||
                       (s.raw.valid&&(!Selected(s,*current,slot->registration,failure)||s.raw.rig!=prior.rig)))continue;
                    const auto parts=ComposeBc2FeedPresentation(slot->supply,slot->pose);if(!parts)continue;
                    try{
                        auto p=std::make_shared<Bc2PhysicalFeedPresentation>(prior);
                        p->displayInput=s.input;p->displayEquipment=*s.equipment;p->revision=++slot->revision;
                        p->displayInput->deadlineNs=(std::min)({s.input.deadlineNs,current->deadlineNs,s.selected->deadlineNs});
                        p->supply=slot->supply;p->weaponFromPouch=parts->weaponFromPouch;p->weaponFromChain=parts->weaponFromChain;
                        p->chainCount=parts->chainCount;p->coverRadians=slot->coverRadians;p->carriedPartsMask=slot->carriedPartsMask;
                        p->pouchCarryWeaponWorldMeters=slot->pouchBasis;p->chainCarryWeaponWorldMeters=slot->chainBasis;
                        State::PresentOriginalDrop(*slot,*p,now);
                        math::Matrix4 identity{};for(unsigned i=0;i<4;++i)identity.values[i][i]=1;
                        p->weaponFromCoverHinge=feed_interaction_detail::Multiply(Along(identity,FeedMotionKind::Hinge,slot->coverRadians),g.weaponFromHinge);
                        p->operation=FeedPhysicalOperation::None;p->targetKind=FeedInteractionTargetKind::None;
                        p->mechanismClaim.reset();p->mechanismInput.reset();p->mechanismWristWorldMeters.reset();p->supplyObject.reset();
                        p->blocksFire=slot->Blocked();out.presentation=p;publication.store(std::move(p),std::memory_order_release);
                    }catch(...){} // Display allocation failure grants no authority.
                    break;
                }
        }
        auto& d=state.diagnostics;
        const auto session=e&&e->session?e->session->State():FeedEquipmentSessionResult{};
        const unsigned flags=unsigned(out.selected)|(unsigned(out.blocksFire||(e&&e->Blocked()))<<1)|
            (unsigned(out.pending||(e&&e->pending))<<2)|(unsigned(out.ownsHand||(e&&e->claim))<<3)|
            (unsigned(out.submitRefused||(e&&e->refused))<<4)|(unsigned(e&&e->gateFailed)<<5);
        const unsigned status=unsigned(out.status==FeedEquipmentStatus::Unbound?session.status:out.status)|
            (unsigned(out.interactionStatus)<<8)|(unsigned(out.reloadStatus)<<16)|(unsigned(out.handReason)<<24);
        out.diagnostic.exit=reason;out.diagnostic.flags=flags;out.diagnostic.status=status;
        if(e){out.diagnostic.operation=unsigned(e->activeOperation);
            if(e->session)out.diagnostic.reloadPhase=unsigned(session.session.reload.phase);}
        if(observation){auto& a=*observation;a.diagnostic=out.diagnostic;
            a.registrationKey=registration?registration->key:0;a.lifetime=e?e->lifetime:0;
            if(a.leftFeedSlot!=~0u){const auto leftAfter=hands.Current(InteractionHand::Left);
                a.leftFeedClaimLost=!leftAfter||leftAfter->token!=a.leftBefore->token;}
            // Bounded observation only. This preserves a claim receipt when a
            // subsequent arbiter Update revokes it before the next feed Tick.
            for(auto& slot:state.equipment)if(slot&&slot->claim){const auto left=hands.Current(InteractionHand::Left);
                if(left&&left->token==*slot->claim)slot->observedMechanismClaim=left;}
            if(newPress||!state.admission||supplyLoss||mechanismLoss||!SameAdmissionDecision(state.admission->latestChange,a))try{
                auto next=std::make_shared<FeedAdmissionObservation>();
                if(newPress)next->firstPress=a;
                else if(state.admission){next->firstPress=state.admission->firstPress;next->failedRetention=state.admission->failedRetention;}
                if(state.admission&&!newSupplyPress)next->firstSupplyLoss=state.admission->firstSupplyLoss;
                if(state.admission&&!newSupplyPress)next->firstMechanismLoss=state.admission->firstMechanismLoss;
                next->latestChange=a;
                if(!next->failedRetention&&(a.retention.passed&1)&&a.leftFeedSlot!=~0u&&
                    a.leftFeedClaimLost&&!out.ownsHand)next->failedRetention=a;
                if(!next->firstSupplyLoss&&supplyLoss)next->firstSupplyLoss=std::move(supplyLoss);
                if(!next->firstMechanismLoss&&mechanismLoss)next->firstMechanismLoss=std::move(mechanismLoss);
                state.admission=next;admissionObservation.store(std::move(next),std::memory_order_release);
            }catch(...){} // Observation allocation cannot alter admission.
        }
        bool changed=d.reason.exchange(reason,std::memory_order_relaxed)!=reason;
        changed=(d.flags.exchange(flags,std::memory_order_relaxed)!=flags)||changed;
        changed=(d.status.exchange(status,std::memory_order_relaxed)!=status)||changed;
        if(changed)d.transitions.fetch_add(1,std::memory_order_relaxed);
        const unsigned asset=s.asset.empty()?0:SupportedAsset(s.asset)?1:2;
        d.asset.store(asset,std::memory_order_relaxed);
        d.registrationKey.store(registration?registration->key:0,std::memory_order_relaxed);
        auto issue=reason;
        if(reason==FeedExit::Updated){
            if(out.handReason!=HandInteractionReason::None||out.interactionStatus>=FeedInteractionStatus::InvalidProfile)
                issue=FeedExit::PolicyStatus;
            else if(flags&2)issue=FeedExit::Blocked;
        }
        // Other weapons and absent selection cannot erase the supported asset's
        // last refusal/debt. Store refusal even before Selected has succeeded.
        if(asset==1&&issue!=FeedExit::Updated){d.lastIssue.store(issue,std::memory_order_relaxed);
            d.lastIssueFlags.store(flags,std::memory_order_relaxed);d.lastIssueStatus.store(status,std::memory_order_relaxed);}
        if(out.selected&&registration)d.lastSelectedKey.store(registration->key,std::memory_order_relaxed);
        d.ticks.fetch_add(1,std::memory_order_relaxed);
        if(flags&2)d.blocked.fetch_add(1,std::memory_order_relaxed);
        if(flags&4)d.pending.fetch_add(1,std::memory_order_relaxed);
        if(flags&8)d.claims.fetch_add(1,std::memory_order_relaxed);
        return out;
    };
    state.RetireActors(s.input,hands,captureHeldLoss);out.blocksFire=blocksFire();
    if(!state.api.read||!state.api.submit||!state.api.receipt){suspend(FeedExit::ApiUnavailable);return finish(FeedExit::ApiUnavailable);}
    // Completion belongs to the original equipment, including while another
    // item is selected or tracking is unavailable. Never wait for a pose to
    // commit an already verified native transfer.
    for(auto& slot:state.equipment)if(slot&&slot->session&&slot->pending){
        auto& old=*slot;const auto receipt=state.api.receipt(state.api.context,old.lifetime,old.pending->command.request.id);
        if(old.refused&&state.api.submitObserved){const auto retention=state.api.submitObserved(state.api.context,*old.pending,true);
            old.refused=retention!=FeedEquipmentSubmitResult::NoEntryRetained;}
        if(!receipt||receipt->command!=old.pending->command)continue;
        const auto cover=old.cover;const auto container=old.container;const auto seat=old.seat;const auto refill=old.refill;const auto carried=old.carriedPartsMask;
        if(receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied)state.Applied(old,*receipt);
        FeedEquipmentSessionSample completed;completed.nowNs=now;completed.receipt=receipt;completed.ammunition=receipt->post;
        completed.snapshot=state.Snapshot(old,receipt->post,s.input);completed.binding=state.Binding(old);
        const auto accepted=old.session->Update(completed);
        if(accepted.receiptAccepted){state.diagnostics.receipts.fetch_add(1,std::memory_order_relaxed);
            const auto operation=receipt->command.authority.operation;
            if(operation==FeedPhysicalOperation::DetachContainer&&receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied){
                old.originalExtractionOrigin.reset();old.originalExtractionRestart=false;
            }
            if(operation==FeedPhysicalOperation::DetachContainer&&old.originalDrop){
                if(receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied)old.originalDrop->applied=true;
                else old.originalDrop.reset();
            }
            if(operation==FeedPhysicalOperation::AttachContainer&&receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied)
                old.originalDrop.reset();
            if(receipt->command.authority.effect==FeedAmmunitionEffect::RefillLoaded)
                (void)old.ammunitionSupply.Complete(*receipt);
            old.pending.reset();old.refused=false;old.motionDirty=true;old.recoveryContact.reset();old.presentationSource.reset();old.supportNeutral.reset();++old.revision;}
        else {old.cover=cover;old.container=container;old.seat=seat;old.refill=refill;old.carriedPartsMask=carried;}
    }
    for(auto& e:state.equipment)if(e)state.PublishGate(*e,s,hands);
    out.blocksFire=blocksFire();out.blocksEquipment=blocksEquipment();
    const auto view=state.api.read(state.api.context,s.owner,now);presentationView=view;
    if(view)registration=FindBc2FeedRegistration(view->config);
    if(view){state.diagnostics.loaded.store(view->counts.loaded,std::memory_order_relaxed);
        state.diagnostics.reserve.store(view->counts.reserve,std::memory_order_relaxed);
        state.diagnostics.capacity.store(view->counts.capacity,std::memory_order_relaxed);}
    bool recorded=false;for(const auto& p:state.packets)if(p.owner==s.owner&&p.input.sequence==s.input.sequence){recorded=true;break;}
    if(!recorded){const auto source=state.api.readAt?state.api.readAt(state.api.context,s.owner,s.input.observedNs,now):view;
        const State::Packet* previous=nullptr;
        for(const auto& packet:state.packets)if(packet.owner==s.owner&&packet.input.sequence<s.input.sequence&&
            (!previous||packet.input.sequence>previous->input.sequence))previous=&packet;
        const bool grip=s.grip||(!s.input.released[0]&&previous&&previous->grip);
        state.packets[state.nextPacket]={s.owner,s.input,s.weapon,source,grip,s.reloadPressed,s.bodyFromHand};state.nextPacket=(state.nextPacket+1)%state.packets.size();}
    if(view)for(auto& p:state.equipment)if(p&&State::SameResource(*p,*view)){e=p.get();break;}
    State::Packet* source=nullptr;
    for(auto& p:state.packets)if(p.owner==s.owner&&p.weapon==s.weapon&&SamePacket(p.input,s.raw.inputEvidence)&&
        (!p.contact||p.contact->rig==s.raw.rig)){source=&p;break;}
    // A renderer packet can exist while its original readAt publication was
    // absent. Keep that absence immutable; a later current view cannot pair
    // itself with older geometry or grant motion from this packet.
    const bool missingSourceNative=source&&!source->native&&source->grip&&!source->input.released[0]&&
        source->input.owner==s.input.owner&&source->input.sequence<=s.input.sequence&&
        Window(source->input,now)&&hands.HasRecordedEvidence(source->input,InteractionHand::Left);
    State::Packet* currentPacket=nullptr;
    for(auto& packet:state.packets)if(packet.owner==s.owner&&packet.weapon==s.weapon&&SamePacket(packet.input,s.input)){
        currentPacket=&packet;break;}
    // Supply ownership follows current controller safety, while mechanism
    // geometry may still be N-1. Do not renew the carried object's claim onto
    // N when N has no original native observation: its later render would
    // otherwise replace the last admitted hand proof with an unpaired packet.
    const bool missingCurrentSupplyNative=e&&e->ammunitionSupply.Held()&&currentPacket&&!currentPacket->native&&
        currentPacket->grip&&!currentPacket->input.released[0]&&currentPacket->input.owner==s.input.owner&&
        Window(currentPacket->input,now)&&hands.HasRecordedEvidence(currentPacket->input,InteractionHand::Left);
    // Observe the original short-circuit results only. Slot checks are separate
    // so a later empty/unrelated slot cannot erase the held slot's rejection.
    const auto retainEntry=[&](unsigned bit,bool pass){return observation?observation->retention.Check(bit,pass):pass;};
    if(retainEntry(0,!view||!s.raw.valid||!s.selected||missingSourceNative||missingCurrentSupplyNative)&&retainEntry(1,!s.cancel)&&retainEntry(2,s.grip)&&retainEntry(3,!s.input.released[0])&&
        retainEntry(4,Window(s.input,now))&&retainEntry(5,bool(s.equipment))){
        const auto right=hands.Current(InteractionHand::Right);
        const auto left=hands.Current(InteractionHand::Left);
        unsigned slotIndex=0;
        for(auto& slot:state.equipment){auto* observed=observation?&observation->retentionSlots[slotIndex]:nullptr;++slotIndex;
            const auto* heldSupply=slot&&slot->ammunitionSupply.Held()?&*slot->ammunitionSupply.Held():nullptr;
            const auto* heldClaim=slot&&slot->claim?&*slot->claim:heldSupply?&heldSupply->claim.token:nullptr;
            if(observed&&slot){observed->registrationKey=slot->registration.key;observed->lifetime=slot->lifetime;
                observed->claim=heldClaim?heldClaim->id:0;}
            const auto retain=[&](unsigned bit,bool pass){return observed?observed->checks.Check(bit,pass):pass;};
            if(retain(0,bool(slot))&&retain(1,bool(heldClaim))&&retain(2,bool(slot->session))&&
            retain(3,slot->session->State().status==FeedEquipmentStatus::Ready||slot->completedHold)&&retain(4,!slot->pending||slot->completedHold)&&retain(5,!slot->refused)&&
            retain(6,!slot->motionDirty||slot->completedHold)&&retain(7,!slot->releaseWait)&&retain(8,slot->activeOperation!=FeedPhysicalOperation::None)&&
            retain(9,slot->weapon==s.weapon)&&retain(10,CurrentEquipment(slot->original,s.owner,*s.equipment,s.binding,slot->registration))){
            const State::Packet* held=nullptr;
            for(const auto& packet:state.packets)if(packet.owner==s.owner&&packet.weapon==s.weapon&&
                packet.input.sequence==(heldSupply?heldSupply->claim.inputSequence:slot->lastInput)){held=&packet;break;}
            if(observed&&held){observed->heldInputSequence=held->input.sequence;observed->heldObservedNs=held->input.observedNs;
                observed->heldDeadlineNs=held->input.deadlineNs;
                if(held->native){observed->nativeSequence=held->native->sequence;observed->nativeObservedNs=held->native->observedNs;
                    observed->nativeDeadlineNs=held->native->deadlineNs;}}
            unsigned selectionFailure=0;
            const auto selected=[&](){
                // Retain only a selection already admitted with this packet.
                // Its immutable deadline is never replaced by this gap.
                bool pass=held->selection&&SelectedMeshesCurrent(*held->selection,s.owner,*held->native,
                    slot->registration,now,selectionFailure);
                if(pass&&s.raw.valid){auto bounded=s;if(!bounded.selected)bounded.selected=held->selection;
                    pass=Selected(bounded,*held->native,slot->registration,selectionFailure);}
                else if(pass){
                    const auto& g=slot->registration.geometry;
                    // An absent renderer frame grants no pose. Any identity or
                    // selected-mesh evidence that IS present must still agree.
                    pass=NativeHand(s)&&s.asset==g.asset&&
                        (!s.binding||AmmoResourceBindingFresh(*s.binding,s.owner,now))&&
                        (s.configurationPath.empty()||s.configurationPath==g.configurationPath)&&
                        (s.meshPath.empty()||s.meshPath==g.meshPath)&&(!s.rigFingerprint||s.rigFingerprint==g.rigFingerprint)&&
                        (s.raw.owner==ReloadStateOwner{}||s.raw.owner==s.owner)&&
                        (!s.raw.rig.soldier||s.raw.rig.soldier==s.owner.soldier)&&(!s.raw.rig.weak||s.raw.rig.weak==s.owner.weak)&&
                        (!s.raw.rigFingerprint||s.raw.rigFingerprint==g.rigFingerprint)&&
                        (!s.selected||SelectedMeshesCurrent(*s.selected,s.owner,*held->native,slot->registration,now,selectionFailure));
                }
                if(observed)observed->selectionFailure=selectionFailure;return pass;};
            if(retain(11,bool(right))&&retain(12,right->token.owner==s.input.owner)&&retain(13,right->token.item==s.weapon)&&
                retain(14,right->token.kind==HandClaimKind::GunHold)&&retain(15,!right->token.prerequisiteClaim)&&retain(16,right->deadlineNs>now)&&
                retain(17,bool(left))&&retain(18,left->token==*heldClaim)&&
                retain(19,left->token.prerequisiteClaim==(heldSupply?0:right->token.id))&&
                retain(20,bool(held))&&retain(21,bool(held->native))&&retain(22,heldSupply?bool(held->bodyFromHand):bool(held->contact))&&retain(23,held->grip)&&retain(24,held->input.owner==s.input.owner)&&
                retain(25,held->input.sequence<=s.input.sequence&&(!missingSourceNative||held->input.sequence<=source->input.sequence))&&
                retain(26,left->inputSequence==held->input.sequence)&&
                retain(27,heldSupply?(left->deadlineNs==heldSupply->claim.deadlineNs&&left->deadlineNs<=held->input.deadlineNs&&left->deadlineNs>now):
                    left->deadlineNs==held->input.deadlineNs)&&retain(28,Window(held->input,now))&&
                retain(29,hands.HasRecordedEvidence(held->input,InteractionHand::Left))&&
                retain(30,FeedEquipmentViewFresh(*held->native,now))&&retain(31,slot->original.identity==held->native->identity)&&
                retain(32,selected())&&retain(33,!view||(FeedEquipmentViewFresh(*view,now)&&
                    view->identity==held->native->identity&&FeedEquipmentSameItem(*held->native,*view)&&
                    FindBc2FeedRegistration(view->config)==&slot->registration&&view->counts==held->native->counts&&
                    view->sequence>=held->native->sequence&&view->observedNs>=held->native->observedNs&&
                    (view->sequence!=held->native->sequence||(view->observedNs==held->native->observedNs&&
                        view->deadlineNs==held->native->deadlineNs))))){
                // Missing publication is not a lifetime-revocation receipt.
                // Retain the admitted mechanism or supply claim within its
                // original input, native, selection and hand leases only.
                // No pose, motion, renewal or native submission comes from it.
                e=slot.get();registration=&e->registration;
                out.diagnostic.admissionChecked=1;
                out.ownsHand=true;out.blocksEquipment=true;out.blocksFire=e->Blocked();
                out.status=e->session->State().status;
                return finish(view?FeedExit::GunOrRawUnverified:FeedExit::ViewUnavailable);
            }
        }
        }
    }
    // Preserve the original short-circuit order. A bit absent from checked was
    // never evaluated; passed bits are observations, never admission authority.
    const auto admission=[&](unsigned bit,bool passed){out.diagnostic.admissionChecked|=1u<<bit;
        if(passed)out.diagnostic.admissionPassed|=1u<<bit;return passed;};
    if(!admission(0,bool(view))||!admission(1,bool(registration))||!admission(2,!e||&e->registration==registration)||
       !admission(3,Selected(s,*view,*registration,out.diagnostic.selectionFailure))||!admission(4,!s.cancel)||!admission(5,Window(s.input,now))){suspend(view?FeedExit::SelectionOrInput:FeedExit::ViewUnavailable);
        return finish(view?FeedExit::SelectionOrInput:FeedExit::ViewUnavailable);}
    out.selected=true;
    // The packet's admitted selected publication can justify passive custody
    // through a later missing frame; retaining it never extends its lease.
    for(auto& packet:state.packets)if(packet.owner==s.owner&&packet.weapon==s.weapon&&
        packet.input.sequence==s.input.sequence&&!packet.selection){packet.selection=s.selected;break;}
    if(!e){for(auto& slot:state.equipment)if(!slot){try{
        slot=std::make_unique<State::Equipment>(*registration);e=slot.get();e->original=*view;e->weapon=s.weapon;
        const auto generation=view->binding.context.resource.weaponGeneration;
        e->supply={{view->binding.data,generation},{view->identity.owner.weapon,generation}};
        e->mechanism={registration->key,generation};e->motions=BuildBc2FeedInteractionProfile(registration->geometry,
            registration->mechanism.id,registration->mechanism.revision,registration->interactionPolicy);
        e->lifetime=Lifetime();e->pouchBasis=e->chainBasis=s.raw.weaponWorldMeters;state.PublishGate(*e,s,hands);
    }catch(...){return finish(FeedExit::StorageUnavailable);}break;}}
    if(!e){out.blocksFire=true;return finish(FeedExit::StorageUnavailable);}
    const auto& profile=e->registration.mechanism;
    // Keep the old immutable transaction and debt when a different native
    // equip/space lifetime appears. A name/count match cannot migrate it.
    if(e->original.identity!=view->identity||e->weapon!=s.weapon){
        if(e->Blocked()){
            suspend(FeedExit::MigrationUnverified);bool migrated=false;
            if(e->session)if(const auto before=e->session->ContextObservation()){
                const auto afterAmmo=state.Ammo(*e,*view);auto after=before->snapshot;
                after.owner=afterAmmo.owner;after.sequence=afterAmmo.sequence;after.observedNs=afterAmmo.observedNs;after.inputSequence=s.input.sequence;
                const auto nativeObservation=[](const AmmoResourceContext& context,const FeedAmmunitionSnapshot& ammo){
                    return AmmunitionSnapshot{context,ammo.sequence,ammo.observedNs,ammo.deadlineNs,
                        {ammo.counts.loaded,ammo.counts.reserve,ammo.counts.capacity},ammo.coherentIdleVerified};};
                const FeedContextMigration migration{nativeObservation(e->original.binding.context,before->ammunition),
                    nativeObservation(view->binding.context,afterAmmo)};
                migrated=e->session->MigrateContext(migration,before->snapshot,before->ammunition,after,afterAmmo,now);
                if(migrated)migrated=e->ammunitionSupply.Migrate(migration,state.BodyAmmo(*e,*view),s.input);
            }
            if(!migrated){out.blocksFire=true;return finish(FeedExit::MigrationUnverified);}
        }else {captureMechanismLoss(*e,FeedSupplyLossSite::ContextReset,FeedExit::MigrationUnverified);e->session.reset();}
        e->original=*view;e->weapon=s.weapon;e->neutral.reset();e->supportNeutral.reset();e->lastInput=e->rejectedInput=0;e->motionDirty=true;
    }
    const auto right=hands.Current(InteractionHand::Right);
    const auto gunCheck=[&](unsigned bit,bool pass){return observation?observation->gun.Check(bit,pass):pass;};
    const bool gun=gunCheck(0,bool(right))&&gunCheck(1,right->token.owner==s.input.owner)&&gunCheck(2,right->token.item==s.weapon)&&
        gunCheck(3,right->token.kind==HandClaimKind::GunHold)&&gunCheck(4,!right->token.prerequisiteClaim)&&gunCheck(5,right->deadlineNs>now);
    if(observation){auto& a=*observation;a.decision.sourceLookedUp=true;a.decision.sourcePresent=bool(source);
        if(source){a.originalPacket=source->input;a.decision.sourceGrip=source->grip;}}
    if(source&&source->native){out.diagnostic.sourceNativeSequence=source->native->sequence;
        out.diagnostic.sourceNativeObservedNs=source->native->observedNs;
        out.diagnostic.sourceNativeDeadlineNs=source->native->deadlineNs;}
    const auto rawCheck=[&](unsigned bit,bool passed){out.diagnostic.rawChecked|=1u<<bit;
        if(passed)out.diagnostic.rawPassed|=1u<<bit;return passed;};
    (void)rawCheck(0,gun);
    const bool rawContext=rawCheck(1,bool(source))&&rawCheck(2,bool(source->native))&&rawCheck(3,Window(source->input,now))&&
        rawCheck(4,source->input.observedNs>=source->native->observedNs)&&rawCheck(5,FeedEquipmentViewFresh(*source->native,now))&&
        rawCheck(6,FeedEquipmentSameItem(*source->native,*view))&&rawCheck(7,source->native->counts==view->counts);
    const bool raw=rawContext&&rawCheck(8,hands.HasRecordedEvidence(source->input,InteractionHand::Left));
    const auto suspendRaw=[&](FeedSupplyLossSite site=FeedSupplyLossSite::TickSuspend){
        // releaseWait exists only after a recorded, same-owner real release.
        // If its original leases expire before raw geometry arrives, no
        // completion can be issued. Do not retain a handless provisional box.
        if(gun&&!s.cancel&&!s.grip&&e->releaseWait)
            State::ReleaseUnsubmittedOriginal(*e,*view,s.input);
        suspend(FeedExit::GunOrRawUnverified,site);
        // Tracking recovery can leave the renderer on a rejected N-1 packet.
        // Record N's real released input for the body supply only, using N's
        // original native pair. This grants no pickup, raw pose or feed motion.
        if(gun&&!s.cancel&&!s.grip&&s.input.released[0]&&
           e->container==FeedContainerState::Detached&&!e->pending&&
           currentPacket&&currentPacket->native&&currentPacket->native->observedNs<=s.input.observedNs){
            const auto currentBody=state.BodyAmmo(*e,*currentPacket->native);
            if(Bc2FeedBodyRetained(currentBody,state.BodyAmmo(*e,*view)))
                (void)e->ammunitionSupply.Update(currentBody,s.input,s.bodyFromHand,false,false,hands,intent);
        }
    };
    if(!raw&&gun&&source&&source->native&&e->claim&&!e->releaseWait&&e->session&&
       (e->session->State().status==FeedEquipmentStatus::Ready||e->completedHold)&&(!e->pending||e->completedHold)&&
       !e->refused&&!e->gateFailed&&(!e->motionDirty||e->completedHold)&&
       e->activeOperation!=FeedPhysicalOperation::None&&s.grip&&!s.input.released[0]&&source->grip&&
       Window(source->input,now)&&hands.HasRecordedEvidence(source->input,InteractionHand::Left)&&
       FeedEquipmentViewFresh(*source->native,source->input.observedNs)&&now>=source->native->deadlineNs&&
       view->sequence>source->native->sequence&&view->observedNs>=source->native->observedNs&&
       FeedEquipmentSameItem(*source->native,*view)&&source->native->counts==view->counts){
        const auto claim=hands.Current(InteractionHand::Left);
        const State::Packet* held=nullptr;
        for(const auto& packet:state.packets)if(packet.owner==s.owner&&packet.weapon==s.weapon&&
            packet.input.sequence==e->lastInput){held=&packet;break;}
        if(claim&&claim->token==*e->claim&&claim->token.prerequisiteClaim==right->token.id&&
           held&&held->contact&&held->native&&held->grip&&claim->inputSequence==held->input.sequence&&
           claim->deadlineNs==held->input.deadlineNs&&Window(held->input,now)&&
           hands.HasRecordedEvidence(held->input,InteractionHand::Left)&&source->input.sequence>=held->input.sequence&&
           source->native->sequence>=held->native->sequence&&source->native->observedNs>=held->native->observedNs&&
           FeedEquipmentSameItem(*held->native,*view)&&held->native->counts==view->counts){
            // A delayed renderer packet may outlive its older native sample.
            // The current exact item/count observation preserves existing
            // ownership only within the admitted hand's original lease. It
            // cannot replace that packet's snapshot: newer native time cannot
            // authorize older geometry. Wait for a separately paired raw
            // packet without policy progress, claim renewal or presentation.
            out.ownsHand=true;out.blocksEquipment=true;out.blocksFire=e->Blocked();
            out.status=e->session->State().status;
            return finish(FeedExit::GunOrRawUnverified);
        }
    }
    if(e->releaseWait||(!raw&&gun&&rawContext)){
        const auto heldInput=e->releaseWait?e->releaseWait->heldInput:e->lastInput;
        const State::Packet* held=nullptr;
        for(const auto& packet:state.packets)if(packet.owner==s.owner&&packet.weapon==s.weapon&&packet.input.sequence==heldInput){held=&packet;break;}
        const bool released=s.input.released[0]&&!s.grip&&source&&source->input.owner==s.input.owner;
        const bool retained=e->session&&e->session->State().status==FeedEquipmentStatus::Ready&&
            !e->pending&&!e->refused&&!e->gateFailed&&!e->motionDirty&&
            e->activeOperation!=FeedPhysicalOperation::None&&
            (e->releaseWait||e->claim||e->ammunitionSupply.Held());
        const bool fresh=held&&held->grip&&held->contact&&held->native&&source&&source->native&&
            Window(held->input,now)&&FeedEquipmentViewFresh(*held->native,now)&&
            hands.HasRecordedReleaseContinuation(held->input,e->releaseWait?e->releaseWait->input:s.input,InteractionHand::Left)&&
            Bc2FeedBodyRetained(state.BodyAmmo(*e,*held->native),state.BodyAmmo(*e,*source->native))&&
            Bc2FeedBodyRetained(state.BodyAmmo(*e,*held->native),state.BodyAmmo(*e,*view));
        if(gun&&released&&retained&&fresh&&(!e->releaseWait||now<e->releaseWait->deadlineNs)){
            if(e->releaseWait&&raw&&SamePacket(source->input,e->releaseWait->input)&&!source->grip){
                e->releaseWait.reset(); // Only the real recorded release may complete the retained gesture.
            }else if(!raw&&rawContext&&source->grip&&
                source->input.sequence<(e->releaseWait?e->releaseWait->input.sequence:s.input.sequence)){
                // Current release revokes older held hand authority before its
                // rendered raw packet arrives. Preserve the admitted gesture,
                // not that stale raw pose or claim, within the original leases.
                captureMechanismLoss(*e,FeedSupplyLossSite::ReleaseWait,FeedExit::GunOrRawUnverified);
                if(!e->releaseWait)e->releaseWait=State::ReleaseWait{s.input,heldInput,
                    (std::min)({held->input.deadlineNs,held->native->deadlineNs,s.input.deadlineNs})};
                captureSupplyLoss(*e,FeedSupplyLossSite::ReleaseWait,FeedExit::GunOrRawUnverified);
                if(e->claim){(void)hands.Release(s.input,*e->claim);e->claim.reset();}
                e->ammunitionSupply.Suspend(s.input,hands);
                out.blocksFire=e->Blocked();out.blocksEquipment=false;
                return finish(FeedExit::GunOrRawUnverified);
            }else {suspendRaw(FeedSupplyLossSite::ReleaseContinuationUnavailable);out.blocksFire=e->Blocked();return finish(FeedExit::GunOrRawUnverified);}
        }else {suspendRaw(FeedSupplyLossSite::ReleaseContinuationUnverified);out.blocksFire=e->Blocked();return finish(FeedExit::GunOrRawUnverified);}
    }
    if(!gun||!raw){suspendRaw();out.blocksFire=e->Blocked();return finish(FeedExit::GunOrRawUnverified);}
    auto sourceInput=source->input;sourceInput.nowNs=now;
    // The renderer solves the placed weapon again on each native publication,
    // even while controllerEvidence still names the same original packet.
    // Keep that packet's first admitted policy geometry immutable, just like
    // its native snapshot. Recomputed display transforms remain current below;
    // they are neither a new motion sample nor permission to extend a lease.
    if(!source->contact)source->contact=State::PolicyContact{s.raw.rig,
        feed_interaction_detail::Multiply(s.raw.rawLeftWristWorldMeters,Inverse(s.raw.weaponWorldMeters)),s.raw.weaponWorldMeters};
    if(!source->selection)source->selection=s.selected;
    const auto& local=source->contact->local;
    const auto body=state.BodyAmmo(*e,*view);
    auto supplyBody=body;bool supplyPaired=false;
    if(currentPacket&&currentPacket->native&&currentPacket->native->observedNs<=s.input.observedNs){
        // Both acquisition and renewal keep the packet's original native and
        // selected-item proof. A newer current view corroborates that proof;
        // it cannot become the source of a claim on older controller input.
        supplyBody=state.BodyAmmo(*e,*currentPacket->native);
        supplyPaired=Bc2FeedBodyRetained(supplyBody,body);
    }
    const bool hadSupplyLoss=bool(supplyLoss);
    captureSupplyLoss(*e,FeedSupplyLossSite::SupplyUpdate,FeedExit::Updated);
    if(!supplyPaired)e->ammunitionSupply.Suspend(s.input,hands);
    const bool acquiredSupply=supplyPaired&&e->ammunitionSupply.Update(supplyBody,s.input,s.bodyFromHand,s.grip,
        e->container==FeedContainerState::Detached&&!e->pending&&!s.cancel,hands,intent);
    if(!hadSupplyLoss&&e->ammunitionSupply.Held())supplyLoss.reset();
    if(acquiredSupply){
        e->originalDrop.reset();
        // Move the acquired reserve representation from the chest grasp into
        // the existing articulated feed presentation. Loaded rounds stay in
        // their original native store until the exact refill receipt.
        const auto& motion=e->motions.operations[unsigned(FeedPhysicalOperation::AttachContainer)];
        auto acquired=e->registration.geometry.poses[unsigned(Bc2FeedPoseId::PouchAttachSource)];
        if(const auto placed=PlaceBc2FeedPouch(e->supply,acquired,motion.placement.weaponFromSource)){
            acquired.weaponFromPouch=placed->weaponFromPouch;acquired.weaponFromFeedRoot=placed->weaponFromChain[0];
            e->pose=acquired;e->gestureStart=acquired;e->pouchBasis=e->chainBasis=s.raw.weaponWorldMeters;
            e->carriedPartsMask=std::uint16_t(1|ChainMask(e->registration.geometry.chainCount));
        }
    }
    const auto left=hands.Current(InteractionHand::Left);
    const auto& supplyHeld=e->ammunitionSupply.Held();
    const bool supplyHand=supplyHeld&&left&&left->token==supplyHeld->claim.token;
    if(acquiredSupply&&supplyHand){
        // Until the pickup's raw packet arrives, use the real delayed wrist
        // and the acquired object's exact pose; the newer claim is independent.
        state.GripTarget(*e,left->token,local,e->pose.weaponFromPouch,FeedInteractionTargetKind::Object,true);
    }
    const bool idle=!e->session||e->session->State().status==FeedEquipmentStatus::Complete;
    const auto supportNeutral=e->supportNeutral;
    // Current grip can reach ordinary support while the feed recognizer still
    // sees the preceding released render packet. Transfer only that same-gun
    // support claim when the actual, armed feed press arrives. Neither a held
    // approach nor a repeated packet can create another acquisition edge.
    const bool supportPair=left&&left->token.kind==HandClaimKind::WeaponSupport&&
        left->token.owner==sourceInput.owner&&left->token.item==s.weapon&&left->token.prerequisiteClaim==right->token.id&&
        left->inputSequence&&left->inputSequence<=sourceInput.sequence&&left->deadlineNs>now&&left->deadlineNs<=sourceInput.deadlineNs&&
        supportNeutral&&supportNeutral->operation==(idle?FeedPhysicalOperation::OpenCover:e->activeOperation)&&
        supportNeutral->input.owner==sourceInput.owner&&Window(supportNeutral->input,now)&&
        hands.HasRecordedEvidence(supportNeutral->input,InteractionHand::Left)&&
        left->inputSequence>=supportNeutral->input.sequence&&s.grip&&!s.input.released[0];
    const bool supportTransfer=supportPair&&sourceInput.sequence>supportNeutral->input.sequence&&
        sourceInput.observedNs>supportNeutral->input.observedNs&&source->grip;
    // A duplicate of that original neutral can wait for its press, without
    // renewing the support claim or issuing a feed hand/pose authority.
    const bool supportWaiting=supportPair&&!source->grip&&sourceInput.released[0]&&
        SamePacket(sourceInput,supportNeutral->input)&&s.input.sequence>sourceInput.sequence;
    if(source->grip)e->supportNeutral.reset();
    bool available=!left||(e->claim&&left->token==*e->claim)||supplyHand||supportTransfer||supportWaiting;
    bool ordinaryReleaseCandidate=false;
    if(e->claim&&(!left||left->token!=*e->claim)){
        const bool alreadyCaptured=bool(mechanismLoss);
        captureMechanismLoss(*e,FeedSupplyLossSite::ClaimUnavailable,FeedExit::PolicyStatus);
        ordinaryReleaseCandidate=!alreadyCaptured&&mechanismLoss&&!source->grip&&sourceInput.released[0];
        e->claim.reset();}
    // Neutral arms the input edge, not contact. Like FeedInteraction, accept a
    // released hand away from the cover, then consume that neutral on the next
    // grip packet. Only that edge may acquire the exact cover contact.
    if(e->completedHold&&(!e->claim||!source->grip||!s.grip||s.input.released[0]||e->refused||e->gateFailed)){
        if(e->claim)(void)hands.Release(s.input,*e->claim);
        e->claim.reset();e->handGrip.reset();e->completedHold=false;
    }
    const bool completedHold=e->completedHold;
    if(completedHold){
        const HandContactProof proof{e->claim->contact,sourceInput.sequence,sourceInput.deadlineNs,true};
        const auto renewed=hands.RenewFrom(s.input,sourceInput,*e->claim,proof);out.handReason=renewed.reason;
        if(!renewed.accepted){e->claim.reset();e->handGrip.reset();e->completedHold=false;}
        // Fresh native evidence may corroborate the accepted receipt. Keeping
        // the hand here grants no motion, next-stage neutral, or second dispatch.
        FeedEquipmentSessionSample completed;completed.nowNs=now;
        completed.ammunition=state.Ammo(*e,*source->native);
        completed.snapshot=state.Snapshot(*e,completed.ammunition,sourceInput);
        completed.binding=state.Binding(*e);
        const auto prior=e->session->State();
        if(prior.session.reload.pending&&prior.ammunition.lastReceipt&&!prior.ammunition.pending&&
           prior.ammunition.lastReceipt->command.request==*prior.session.reload.pending){
            auto ack=prior.ammunition.lastReceipt->acknowledgement;
            ack.snapshotSequence=completed.snapshot.sequence;ack.observedNs=completed.snapshot.observedNs;
            completed.acknowledgement=ack; // Corroborate the original receipt without another gesture.
        }
        out.status=e->session->Update(completed).status;
        e->lastInput=(std::max)(e->lastInput,sourceInput.sequence);
        out.pending=bool(e->pending);out.ownsHand=bool(e->claim);
        out.interactionStatus=FeedInteractionStatus::Endpoint;
    }
    if(!completedHold){
    if(idle){
        const auto& m=e->motions.operations[unsigned(FeedPhysicalOperation::OpenCover)];
        const auto contact=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        const auto& policy=profile.contacts[unsigned(FeedPart::Cover)];
        const auto distance=Distance(local,contact),angle=Angle(local,contact);
        if(observation){auto& a=*observation;a.decision.contactStage=1;a.contactGeometryEvaluated=true;
            a.distanceMeters=distance;a.angleRadians=float(angle);a.radiusMeters=(std::max)(policy.radiusMeters,m.acquisitionRadiusMeters);
            a.maxAngleRadians=policy.maxAngleRadians;a.orientation=unsigned(policy.orientation);}
        // A cover grasp binds the actual anatomical wrist at the measured
        // contact; the recognizer constrains motion relative to that grasp.
        const bool orientation=policy.orientation==FeedContactOrientation::Acquired||angle<=policy.maxAngleRadians;
        const auto acquisitionRadius=(std::max)(policy.radiusMeters,m.acquisitionRadiusMeters);
        const bool touch=distance<=acquisitionRadius&&orientation;
        const bool freshSequence=sourceInput.sequence>e->lastInput&&sourceInput.sequence>e->rejectedInput;
        if(!source->grip&&sourceInput.released[0]&&available&&freshSequence){
            FeedEquipmentSessionSample neutral;neutral.interaction.contact.weaponFromHandContact=local;
            neutral.interaction.contact.observedNs=sourceInput.observedNs;neutral.interaction.contact.inputSequence=sourceInput.sequence;
            neutral.nowNs=now;neutral.ammunition=state.Ammo(*e,*source->native);neutral.interaction.focused=true;neutral.interaction.contact.tracked=true;
            e->neutral=neutral;
            e->supportNeutral=State::SupportNeutral{sourceInput,FeedPhysicalOperation::OpenCover};
        }
        const auto neutral=e->neutral;
        auto& diagnostic=out.diagnostic;diagnostic.coverDistanceMeters=float(distance);diagnostic.coverAngleRadians=float(angle);
        if(neutral)diagnostic.neutralAgeNs=now-neutral->interaction.contact.observedNs;
        const auto idleCheck=[&](unsigned bit,bool passed){diagnostic.idleChecked|=1u<<bit;
            if(passed)diagnostic.idlePassed|=1u<<bit;return passed;};
        (void)idleCheck(0,available);(void)idleCheck(1,!source->grip&&sourceInput.released[0]);
        (void)idleCheck(2,freshSequence);(void)idleCheck(3,source->grip);
        (void)idleCheck(4,distance<=acquisitionRadius);(void)idleCheck(5,orientation);
        (void)idleCheck(6,bool(neutral));
        const bool afterNeutral=idleCheck(7,neutral&&sourceInput.sequence>neutral->interaction.contact.inputSequence);
        const bool freshNeutral=idleCheck(8,neutral&&diagnostic.neutralAgeNs>=0&&diagnostic.neutralAgeNs<profile.maxAgeNs);
        (void)idleCheck(9,view->counts.loaded<view->counts.capacity);(void)idleCheck(10,view->counts.reserve>0);
        // A missed, expired or unavailable press cannot turn into acquisition
        // by entering contact while still held, including on repeated packets.
        if(source->grip&&afterNeutral)e->neutral.reset();
        if(source->grip&&afterNeutral&&freshNeutral&&touch&&available&&
           view->counts.loaded<view->counts.capacity&&view->counts.reserve>0){
            const auto lifetime=Lifetime();if(!lifetime){out.blocksFire=true;return finish(FeedExit::LifetimeExhausted);}
            captureMechanismLoss(*e,FeedSupplyLossSite::LifetimeReset,FeedExit::PolicyStatus);
            e->lifetime=lifetime;e->refill={FeedRefillState::Required,lifetime,0};e->refused=false;
            e->originalDrop.reset();
            e->originalExtractionOrigin.reset();e->originalExtractionRestart=false;
            try{e->session=std::make_unique<FeedEquipmentSession>(profile,e->motions,FeedEquipmentOwner(s.owner),e->mechanism,e->supply,lifetime,
                e->original.binding.context.resource);}catch(...){out.blocksFire=true;return finish(FeedExit::SessionUnavailable);}
            state.PublishGate(*e,s,hands);
            auto armed=*neutral;armed.ammunition.lifetime=lifetime;armed.snapshot=state.Snapshot(*e,armed.ammunition,sourceInput);
            armed.snapshot.inputSequence=armed.interaction.contact.inputSequence;
            armed.binding=state.Binding(*e);armed.authority=state.Authority(*e,armed.ammunition,FeedPhysicalOperation::OpenCover);
            armed.interaction.contact.owner=armed.snapshot.owner;armed.interaction.contact.mechanism=e->mechanism;
            armed.interaction.contact.snapshotSequence=armed.snapshot.sequence;armed.interaction.contact.handAvailable=true;
            armed.interaction.hand={1,sourceInput.owner.equipGeneration};armed.interaction.trackingGeneration=sourceInput.owner.space;armed.held=true;
            (void)e->session->Update(armed);
        }
    }
    e->lastInput=(std::max)(e->lastInput,sourceInput.sequence);
    if(e->session){
        auto ammunition=state.Ammo(*e,*source->native);
        FeedEquipmentSessionSample sample;sample.ammunition=ammunition;sample.snapshot=state.Snapshot(*e,ammunition,sourceInput);
        sample.nowNs=now;sample.binding=state.Binding(*e);
        const bool needsSupply=e->container==FeedContainerState::Detached;
        const bool supplied=e->ammunitionSupply.Ready(state.BodyAmmo(*e,*source->native),sourceInput);
        const auto& custody=e->ammunitionSupply.Custody();
        // The renderer may still be delivering the real N-1 neutral packet
        // when the current packet acquires supply. Preserve that exact neutral
        // until its pickup packet arrives; it cannot dispatch a refill.
        const bool pickupAwaitingRaw=needsSupply&&custody&&e->supplyNeutral&&!source->grip&&sourceInput.released[0]&&
            sourceInput.sequence<custody->object.claim.inputSequence&&sourceInput.owner==custody->object.identity.owner;
        if(supplied&&source->grip&&(!custody->acquisition||
           custody->acquisition->Object().claim.token.id==e->supplyAcquiredClaim))e->supplyNeutral=false;
        const bool chestNeutral=needsSupply&&!e->ammunitionSupply.Custody()&&!source->grip&&sourceInput.released[0]&&
            available&&source->bodyFromHand&&Bc2FeedSupplyContact(*source->bodyFromHand);
        // The real pickup already proved belt contact. Its render packet may
        // be skipped while the player carries the box toward the gun; admit
        // from the next current hand pose within the original pickup leases.
        const bool pickupReady=needsSupply&&supplied&&supplyHand&&custody->acquisition&&source->grip&&
            custody->acquisition->Object().claim.token.id!=e->supplyAcquiredClaim&&
            sourceInput.sequence>=custody->acquisition->Sample().input.sequence&&
            custody->acquisition->Object().claim.token==supplyHeld->claim.token&&
            custody->acquisition->Sample().input.deadlineNs>now&&custody->acquisition->Sample().source.deadlineNs>now&&
            source->bodyFromHand&&Bc2FeedSupplyContact(custody->acquisition->Sample().bodyFromHand);
        std::optional<FeedSupplyAcquisition> pickupAdmission;
        // Follow a meaningful released approach within the broader supply
        // region before pickup. Rebase only against newer paired native and
        // input evidence: suspending on an identical native observation would
        // erase its arm, while changing that snapshot's source would mutate
        // its evidence. Small jitter keeps the admitted source unchanged.
        bool refreshChest=false;
        if(chestNeutral&&e->supplyNeutral&&!e->motionDirty&&!e->pending){
            const auto previous=e->session->ContextObservation();
            const auto part=unsigned(FeedPart::Container);
            refreshChest=previous&&sourceInput.sequence>previous->snapshot.inputSequence&&
                sourceInput.observedNs>previous->ammunition.observedNs&&
                ammunition.sequence>previous->ammunition.sequence&&ammunition.observedNs>previous->ammunition.observedNs&&
                Distance(local,sample.snapshot.contacts[part].weaponFromContact)>profile.contacts[part].radiusMeters*.5;
        }
        // Contact loss invalidates neutral preparation. A newly acquired or
        // reclaimed object may prepare once from its real rendered pickup;
        // an already admitted carry keeps its existing motion.
        if(needsSupply&&!custody&&!chestNeutral)e->supplyNeutral=false;
        if((chestNeutral&&(!e->supplyNeutral||e->motionDirty||refreshChest))||
           (pickupReady&&(!e->supplyNeutral||e->motionDirty))){
            // Rebase from actual neutral contact or the opaque real pickup.
            // Only pickup moves the representation. No held packet is turned
            // into a released input to satisfy the feed session's neutral gate.
            FeedEquipmentSessionSample pause;pause.nowNs=now;(void)e->session->Update(pause);
            auto pose=e->registration.geometry.poses[unsigned(Bc2FeedPoseId::PouchAttachSource)];
            const auto& grip=e->registration.geometry.poses[unsigned(Bc2FeedPoseId::PouchWithdrawn)].sourceFromWrist;
            const auto atHand=feed_interaction_detail::Multiply(Inverse(grip),local);
            if(const auto placed=PlaceBc2FeedPouch(e->supply,pose,atHand)){
                pose.weaponFromPouch=placed->weaponFromPouch;pose.weaponFromFeedRoot=placed->weaponFromChain[0];
                const auto motion=BuildBc2FeedMotionFromRetainedPose(e->registration.geometry,FeedPhysicalOperation::AttachContainer,
                    e->registration.interactionPolicy,pose);
                const auto query=EvaluateFeedMechanism(profile,sample.snapshot,e->supply,now);
                if(motion&&query.candidate){auto rebased=sample.snapshot;
                    rebased.contacts[unsigned(FeedPart::Container)].weaponFromContact=
                        feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,motion->placement.weaponFromSource);
                    if(e->session->UpdateMotion(rebased,sample.ammunition,*query.candidate,*motion,now)){
                        e->motions.operations[unsigned(FeedPhysicalOperation::AttachContainer)]=*motion;
                        e->motionDirty=false;e->supplyNeutral=true;e->recoveryContact.reset();
                        sample.snapshot=state.Snapshot(*e,ammunition,sourceInput);
                        if(pickupReady){
                            e->pose=pose;e->gestureStart=pose;e->pouchBasis=e->chainBasis=s.raw.weaponWorldMeters;
                            e->carriedPartsMask=std::uint16_t(1|ChainMask(e->registration.geometry.chainCount));
                        }
                    }
                }
            }
        }
        if(pickupReady&&e->supplyNeutral&&!e->motionDirty){
            pickupAdmission=FeedSupplyAcquisition{*custody->acquisition,FeedEquipmentOwner(s.owner),e->mechanism,e->supply,
                custody->context.native.binding.context.resource,e->lifetime,profile.id,profile.revision};
        }
        const auto prior=e->session->State();
        bool recoveryEstablished=false;
        const auto query=EvaluateFeedMechanism(profile,sample.snapshot,e->supply,now);
        if((supportTransfer||supportWaiting)&&(!query.candidate||query.candidate->operation!=supportNeutral->operation))available=false;
        if(observation){auto& v=observation->decision;v.queryStatus=unsigned(query.status);
            if(query.candidate){v.candidateOperation=unsigned(query.candidate->operation);v.candidatePart=unsigned(query.candidate->part);}}
        if(query.candidate){const auto op=query.candidate->operation;
            const auto recovery=e->session->RecoveryMotion();
            const bool recoverable=recovery&&!e->originalExtractionRestart&&Same(recovery->Candidate(),*query.candidate);
            if(e->motionDirty&&recoverable){
                auto held=recovery->WeaponFromHeld();std::optional<FeedRecoveryFrame> frame;
                if(recovery->Kind()==FeedMotionKind::FreePlacement&&recovery->WeaponWorld()){
                    held=feed_interaction_detail::Multiply(feed_interaction_detail::Multiply(held,*recovery->WeaponWorld()),Inverse(s.raw.weaponWorldMeters));
                    frame=FeedRecoveryFrame{sample.snapshot.owner,e->mechanism,sample.snapshot.sequence,sourceInput.sequence,
                        sourceInput.observedNs,s.raw.weaponWorldMeters};
                }
                const auto contact=recovery->Kind()==FeedMotionKind::FreePlacement?
                    feed_interaction_detail::Multiply(recovery->Motion().placement.sourceFromGrip,held):held;
                auto recovered=sample.snapshot;recovered.contacts[unsigned(query.candidate->part)].weaponFromContact=contact;
                if(e->session->RecoverMotion(recovered,sample.ammunition,*recovery,now,frame)){
                    e->gestureStart=state.PoseInWeaponFrame(*e,s.raw.weaponWorldMeters);e->presentationSource=held;
                    e->recoveryContact=recovered.contacts[unsigned(query.candidate->part)];e->motionDirty=false;sample.snapshot=recovered;
                    recoveryEstablished=true;
                }
            }else if(e->motionDirty&&op!=FeedPhysicalOperation::OpenCover&&op!=FeedPhysicalOperation::CloseCover){
                const auto pose=state.PoseInWeaponFrame(*e,s.raw.weaponWorldMeters);
                const auto motion=BuildBc2FeedMotionFromRetainedPose(e->registration.geometry,op,e->registration.interactionPolicy,pose);
                auto rebased=sample.snapshot;
                if(motion)rebased.contacts[unsigned(query.candidate->part)].weaponFromContact=
                    feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,motion->placement.weaponFromSource);
                if(motion&&e->session->UpdateMotion(rebased,sample.ammunition,*query.candidate,*motion,now)){
                    e->motions.operations[unsigned(op)]=*motion;e->motionDirty=false;e->gestureStart=pose;
                    sample.snapshot=state.Snapshot(*e,sample.ammunition,sourceInput);}
            }else if(e->motionDirty&&!recoverable)e->motionDirty=false;
            sample.authority=state.Authority(*e,sample.ammunition,op);
            if(e->activeOperation!=op){e->activeOperation=op;e->gestureStart=state.PoseInWeaponFrame(*e,s.raw.weaponWorldMeters);e->gestureCoverRadians=e->coverRadians;
                e->recoveryContact.reset();e->presentationSource.reset();}
        }
        // RecoverMotion binds the retained proof to this observation. Its first
        // neutral/gesture packet must be newer; replaying this one would revoke
        // that proof as InvalidInput before the player can regrasp.
        if(observation)observation->decision.recoveryEstablished=recoveryEstablished;
        if(!recoveryEstablished){
        sample.interaction.contact={sample.snapshot.owner,e->mechanism,sample.snapshot.sequence,sourceInput.sequence,
            sourceInput.observedNs,true,available,local};
        sample.interaction.hand={1,sourceInput.owner.equipGeneration};sample.interaction.trackingGeneration=sourceInput.owner.space;
        sample.interaction.focused=s.input.focused;sample.interaction.grip=source->grip;
        sample.interaction.supplyAcquisition=pickupAdmission;
        if(e->motions.operations[unsigned(e->activeOperation)].kind==FeedMotionKind::FreePlacement)
            sample.interaction.weaponWorld=source->contact->weaponWorld;
        sample.held=available&&(source->grip||sourceInput.released[0])&&!e->refused&&!e->gateFailed&&!e->motionDirty&&
            (!needsSupply||supplied||pickupAwaitingRaw||(chestNeutral&&e->supplyNeutral));
        // Retained reserve custody is not a current hand claim. Reclaim its
        // exact AmmoObject before a held attachment can acquire feed geometry.
        if(needsSupply&&supplied&&source->grip&&!supplyHand)sample.held=false;
        if(observation){auto& v=observation->decision;v.eligibilityEvaluated=true;v.available=available;v.heldBeforeOwnership=sample.held;
            v.needsSupply=needsSupply;v.supplied=supplied;v.pickupAwaitingRaw=pickupAwaitingRaw;v.chestNeutral=chestNeutral;v.supplyNeutral=e->supplyNeutral;
            v.refused=e->refused;v.gateFailed=e->gateFailed;v.motionDirty=e->motionDirty;v.nativePending=bool(e->pending);
            v.ammoPending=bool(prior.ammunition.pending);v.reloadPending=bool(prior.session.reload.pending);}
        if(prior.session.reload.pending&&prior.ammunition.lastReceipt&&!prior.ammunition.pending&&
           prior.ammunition.lastReceipt->command.request==*prior.session.reload.pending){
            auto ack=prior.ammunition.lastReceipt->acknowledgement;
            ack.snapshotSequence=sample.snapshot.sequence;ack.observedNs=sample.snapshot.observedNs;
            sample.acknowledgement=ack; // Fresh exact native-count corroboration, original receipt unchanged.
        }
        if(source->grip&&sample.held&&query.candidate&&!prior.ammunition.pending&&!prior.session.reload.pending){
            const auto contact=EvaluateFeedInteractionContact(profile,e->motions.operations[unsigned(query.candidate->operation)],
                sample.snapshot,e->supply,*query.candidate,sample.interaction.contact,now);
            if(observation){auto& a=*observation;const auto& policy=profile.contacts[unsigned(query.candidate->part)];
                a.decision.contactStage=2;a.decision.contactStatus=unsigned(contact.status);a.contactGeometryEvaluated=bool(contact.target);
                a.distanceMeters=contact.distanceMeters;a.angleRadians=contact.angleRadians;
                a.radiusMeters=(std::max)(policy.radiusMeters,e->motions.operations[unsigned(query.candidate->operation)].acquisitionRadiusMeters);
                a.maxAngleRadians=policy.maxAngleRadians;a.orientation=unsigned(policy.orientation);}
            // AcquireFrom/RenewFrom preserve the exact recorded raw packet.
            // Native authority has its own independently checked deadline.
            const HandContactProof proof{{PartKey(e->registration.key,unsigned(query.candidate->part)),e->lifetime},sourceInput.sequence,
                sourceInput.deadlineNs,true};
            if(supplyHand&&needsSupply&&supplied){
                // AmmoSupply owns this exact hand; do not manufacture or steal
                // a Mechanism claim for a carried reserve object.
                if(!hands.HasRecordedEvidence(sourceInput,InteractionHand::Left))sample.held=false;
                if(observation){auto& v=observation->decision;v.ownershipAction=3;v.ownershipAccepted=sample.held;}
            }else if(e->claim){const bool alreadyCaptured=bool(mechanismLoss);
                captureMechanismLoss(*e,FeedSupplyLossSite::ClaimRenewalRejected,FeedExit::PolicyStatus);
                const auto renewed=hands.RenewFrom(s.input,sourceInput,*e->claim,proof);out.handReason=renewed.reason;
                if(observation){auto& a=*observation;a.decision.ownershipAction=2;a.decision.ownershipInputValid=renewed.inputValid;
                    a.decision.ownershipAccepted=renewed.accepted;a.decision.ownershipReason=unsigned(renewed.reason);
                    a.ownershipClaim=renewed.claim;a.decision.resultClaim=renewed.claim?renewed.claim->token.id:0;}
                if(!alreadyCaptured&&mechanismLoss){
                    if(renewed.accepted)mechanismLoss.reset();
                    else {mechanismLoss->before.decision=observation->decision;
                        mechanismLoss->before.ownershipClaim=observation->ownershipClaim;}}
                if(!renewed.accepted){e->claim.reset();sample.held=false;}}
            else if(contact.status==FeedMechanismStatus::ContactMatch&&intent!=UINT64_MAX){
                const HandClaimRequest request{s.input.owner,InteractionHand::Left,HandClaimKind::Mechanism,
                    s.weapon,proof,++intent,right->token.id};
                const auto acquired=supportTransfer?hands.TransferFrom(s.input,sourceInput,left->token,request):
                    hands.AcquireFrom(s.input,sourceInput,request);
                if(observation){auto& a=*observation;a.decision.ownershipAction=1;a.decision.ownershipInputValid=acquired.inputValid;
                    a.decision.ownershipAccepted=acquired.accepted;a.decision.ownershipReason=unsigned(acquired.reason);
                    a.ownershipClaim=acquired.claim;a.decision.resultClaim=acquired.claim?acquired.claim->token.id:0;}
                out.handReason=acquired.reason;
                if(acquired.claim)e->claim=acquired.claim->token;else sample.held=false;
            }else sample.held=false;
        }
        const bool alreadyCaptured=bool(mechanismLoss);
        captureMechanismLoss(*e,FeedSupplyLossSite::PolicyClear,FeedExit::PolicyStatus);
        const auto result=e->session->Update(sample);out.status=result.status;
        if(pickupAdmission&&result.session.interaction.status==FeedInteractionStatus::Acquired&&
           result.session.reload.status==FeedReloadStatus::AwaitingPhysicalCompletion)
            e->supplyAcquiredClaim=pickupAdmission->pickup.Object().claim.token.id;
        if(!source->grip&&sourceInput.released[0]&&result.session.recognitionEnabled&&query.candidate&&
           result.session.interaction.status==FeedInteractionStatus::Armed)
            e->supportNeutral=State::SupportNeutral{sourceInput,query.candidate->operation};
        else if(!result.session.recognitionEnabled||result.session.interaction.status!=FeedInteractionStatus::Duplicate)
            e->supportNeutral.reset();
        const bool successfulCompletion=bool(result.session.interaction.completion)||bool(result.dispatch);
        if(mechanismLoss&&mechanismLoss->before.lifetime==e->lifetime&&mechanismLoss->before.weapon==e->weapon&&
           (mechanismLoss->site==FeedSupplyLossSite::ClaimUnavailable||
            mechanismLoss->site==FeedSupplyLossSite::ClaimRenewalRejected||mechanismLoss->site==FeedSupplyLossSite::PolicyClear)){
            if((ordinaryReleaseCandidate&&successfulCompletion)||(!alreadyCaptured&&
                (successfulCompletion||(source->grip&&sample.held))))mechanismLoss.reset();
            else {mechanismLoss->outcomeEvaluated=true;mechanismLoss->completion=bool(result.session.interaction.completion);
                mechanismLoss->dispatch=bool(result.dispatch);mechanismLoss->outcomeStatus=unsigned(result.status)|
                    (unsigned(result.session.interaction.status)<<8)|(unsigned(result.session.reload.status)<<16);}}
        if(observation){auto& v=observation->decision;v.heldAtUpdate=sample.held;v.recognitionEnabled=result.session.recognitionEnabled;
            v.interactionEvidence=unsigned(result.session.interaction.evidence);}
        out.interactionStatus=result.session.interaction.status;out.reloadStatus=result.session.reload.status;
        out.diagnostic.policyEvaluated=true;
        state.PoseTarget(*e,result.session.interaction,s.raw.weaponWorldMeters);
        const auto& interaction=result.session.interaction;
        if(interaction.target){
            const auto hand=hands.Current(InteractionHand::Left);
            if(hand&&((e->claim&&hand->token==*e->claim)||
                (e->ammunitionSupply.Held()&&hand->token==e->ammunitionSupply.Held()->claim.token)))
                state.GripTarget(*e,hand->token,local,*interaction.target,interaction.targetKind,
                    interaction.status==FeedInteractionStatus::Acquired);
        }else if(interaction.status!=FeedInteractionStatus::Duplicate&&!acquiredSupply&&
            !(supplyHand&&pickupAwaitingRaw&&interaction.status==FeedInteractionStatus::Armed))e->handGrip.reset();
        if(!result.session.interaction.target&&!result.session.interaction.completion&&
           (result.session.interaction.status==FeedInteractionStatus::Interrupted||result.session.interaction.status==FeedInteractionStatus::OffPath||
            result.session.interaction.status==FeedInteractionStatus::MotionJump||result.session.interaction.status==FeedInteractionStatus::Expired))e->motionDirty=true;
        if(!result.dispatch&&!result.session.interaction.completion&&!source->grip&&!s.grip&&
           sourceInput.released[0]&&s.input.released[0]&&!s.cancel&&
           result.session.interaction.status==FeedInteractionStatus::Interrupted)
            State::ReleaseUnsubmittedOriginal(*e,*view,sourceInput);
        if(result.dispatch){
            // Only the actual neutral release completing an empty ORIGINAL
            // detach can seed a visual drop. Wait for its accepted receipt;
            // suspension, replacement custody and partial originals retain
            // their exact mechanical/custody geometry and ammunition.
            if(result.dispatch->authority.operation==FeedPhysicalOperation::DetachContainer&&
               !source->grip&&sourceInput.released[0]&&!s.grip&&s.input.released[0]&&
               !source->native->counts.loaded&&!result.dispatch->before.counts.loaded&&
               !e->ammunitionSupply.Custody())
                e->originalDrop=State::OriginalDrop{e->pose,e->pouchBasis,e->chainBasis,
                    sourceInput.observedNs,e->carriedPartsMask,false};
            e->completedHold=e->claim&&source->grip&&
                (e->activeOperation==FeedPhysicalOperation::OpenCover||e->activeOperation==FeedPhysicalOperation::CloseCover);
            FeedEquipmentSubmission submission;submission.command=*result.dispatch;submission.source=*source->native;
            const bool hadBindLoss=bool(supplyLoss);
            if(result.dispatch->authority.effect==FeedAmmunitionEffect::RefillLoaded)
                captureSupplyLoss(*e,FeedSupplyLossSite::SupplyBind,FeedExit::Updated);
            const bool supplyBound=result.dispatch->authority.effect!=FeedAmmunitionEffect::RefillLoaded||
                e->ammunitionSupply.Bind(*result.dispatch,state.BodyAmmo(*e,*source->native),sourceInput,hands);
            if(!hadBindLoss&&e->ammunitionSupply.Held())supplyLoss.reset();
            state.diagnostics.submissions.fetch_add(1,std::memory_order_relaxed);
            submission.geometry={profile,e->motions.operations[unsigned(result.dispatch->authority.operation)],s.rigFingerprint,
                sourceInput.sequence,sourceInput.observedNs,(std::min)(sourceInput.deadlineNs,source->native->deadlineNs),true,e->registration.key};
            e->pending=submission;
            if(!supplyBound){e->refused=true;out.submitRefused=true;
            }else if(state.api.submitObserved){const auto submitted=state.api.submitObserved(state.api.context,submission,false);
                e->refused=submitted==FeedEquipmentSubmitResult::RetentionBusy||submitted==FeedEquipmentSubmitResult::Invalid;
                out.submitRefused=submitted!=FeedEquipmentSubmitResult::Submitted;
            }else {e->refused=!state.api.submit(state.api.context,submission);out.submitRefused=e->refused;}
            if(out.submitRefused)state.diagnostics.submitRefusals.fetch_add(1,std::memory_order_relaxed);
        }
        if(e->claim&&(!source->grip||(result.dispatch&&!e->completedHold)||!sample.held)){
            (void)hands.Release(s.input,*e->claim);e->claim.reset();}
        }else out.status=e->session->State().status;
        out.pending=e->pending.has_value();out.ownsHand=e->claim.has_value()||e->ammunitionSupply.Held().has_value();
    }
    } // Ordinary recognition is disabled for an already completed held cover.
    state.PublishGate(*e,s,hands);out.blocksFire=e->Blocked();out.blocksEquipment=e->pending.has_value()||e->claim.has_value()||e->ammunitionSupply.Held().has_value();
    if(!e->ammunitionSupply.Custody()&&!e->pending)if(const auto supply=Bc2FeedBodySource(body,s.input,now)){
        const auto config=Bc2FeedSupplyConfig();AmmoSupplyVisualSample visual{true,false,*supply,s.input,*right,
            {config.pouchCenterMeters,config.pouchRadiusMeters},SupplyAnchorFrame::RecenteredBody};
        if(AmmoSupplyVisualFresh(visual,now)){out.bodyAmmo=body;out.bodyVisual=visual;}
    }
    if(const auto pose=ComposeBc2FeedPresentation(e->supply,e->pose))try{
        auto p=std::make_shared<Bc2PhysicalFeedPresentation>();p->owner=s.owner;p->rig=s.raw.rig;p->input=sourceInput;p->weapon=s.weapon;
        p->supply=e->supply;p->lifetime=e->lifetime;p->revision=++e->revision;p->rigFingerprint=s.rigFingerprint;
        p->registrationKey=e->registration.key;p->chainCount=pose->chainCount;
        p->weaponFromPouch=pose->weaponFromPouch;p->weaponFromChain=pose->weaponFromChain;p->coverRadians=e->coverRadians;
        p->carriedPartsMask=e->carriedPartsMask;p->pouchCarryWeaponWorldMeters=e->pouchBasis;p->chainCarryWeaponWorldMeters=e->chainBasis;
        State::PresentOriginalDrop(*e,*p,now);
        p->originalWeaponWorldMeters=s.raw.weaponWorldMeters;p->operation=e->activeOperation;
        if(e->claim){const auto claim=hands.Current(InteractionHand::Left);
            if(claim&&claim->token==*e->claim&&claim->inputSequence==sourceInput.sequence&&claim->deadlineNs>now){
                p->mechanismClaim=claim;p->mechanismInput=sourceInput;}}
        else if(const auto& held=e->ammunitionSupply.Held();held&&held->claim.inputSequence==s.input.sequence&&held->claim.deadlineNs>now){
            p->supplyObject=held;p->mechanismClaim=held->claim;p->mechanismInput=s.input;
        }
        if(p->mechanismClaim&&e->handGrip&&e->handGrip->claim==p->mechanismClaim->token){
            const auto& grip=*e->handGrip;
            const bool terminal=e->activeOperation==FeedPhysicalOperation::SeatFeed;
            const auto& target=grip.kind==FeedInteractionTargetKind::Contact?grip.weaponFromContact:
                terminal?pose->weaponFromChain[pose->chainCount-1]:pose->weaponFromPouch;
            const auto& basis=grip.kind==FeedInteractionTargetKind::Contact?s.raw.weaponWorldMeters:
                terminal?e->chainBasis:e->pouchBasis;
            const auto wrist=feed_interaction_detail::Multiply(feed_interaction_detail::Multiply(grip.targetFromWrist,target),basis);
            if(Pose(wrist)){p->mechanismWristWorldMeters=wrist;p->targetKind=grip.kind;}
        }else if(!p->mechanismClaim)e->handGrip.reset();
        if(!p->mechanismWristWorldMeters){
            // Logical supply ownership survives a targetless policy update.
            // Magnetic hand consumers still require the validated claim,
            // input and wrist together; custody alone grants no pose.
            p->mechanismClaim.reset();p->mechanismInput.reset();
        }
        math::Matrix4 identity{};for(unsigned i=0;i<4;++i)identity.values[i][i]=1;
        p->weaponFromCoverHinge=feed_interaction_detail::Multiply(Along(identity,FeedMotionKind::Hinge,e->coverRadians),e->registration.geometry.weaponFromHinge);
        p->blocksFire=out.blocksFire;e->lastPresentation=p;out.presentation=p;publication.store(std::move(p),std::memory_order_release);
    }catch(...){}
    return finish(FeedExit::Updated);
}
void Bc2PhysicalFeed::Report(std::ostream& out)const {
    if(!state_){out<<"{\"available\":false}";return;}
    const auto& d=state_->diagnostics;const auto flags=d.flags.load(std::memory_order_relaxed);
    const auto status=d.status.load(std::memory_order_relaxed),asset=d.asset.load(std::memory_order_relaxed);
    const auto issueStatus=d.lastIssueStatus.load(std::memory_order_relaxed);
    const auto lastSelectedKey=d.lastSelectedKey.load(std::memory_order_relaxed);
    out<<"{\"observations\":\"independent_atomics_not_native_proof\",\"ticks\":"<<d.ticks.load(std::memory_order_relaxed)
       <<",\"transitions\":"<<d.transitions.load(std::memory_order_relaxed)<<",\"blocked_ticks\":"<<d.blocked.load(std::memory_order_relaxed)
       <<",\"pending_ticks\":"<<d.pending.load(std::memory_order_relaxed)<<",\"claim_ticks\":"<<d.claims.load(std::memory_order_relaxed)
       <<",\"suspends\":"<<d.suspends.load(std::memory_order_relaxed)<<",\"submit_attempts\":"<<d.submissions.load(std::memory_order_relaxed)
       <<",\"submit_refusals\":"<<d.submitRefusals.load(std::memory_order_relaxed)<<",\"receipts_accepted\":"<<d.receipts.load(std::memory_order_relaxed)
       <<",\"gate_refusals\":"<<d.gateRefusals.load(std::memory_order_relaxed)
       <<",\"latest_tick\":{\"reason\":\""<<FeedExitName(d.reason.load(std::memory_order_relaxed))
       <<"\",\"asset_class\":\""<<(asset==1?"registered_feed":asset==2?"other":"none")
       <<"\",\"registration_key\":"<<d.registrationKey.load(std::memory_order_relaxed)
       <<",\"last_selected_asset\":\""<<(lastSelectedKey?"registered_feed":"none")
       <<"\",\"last_selected_registration_key\":"<<lastSelectedKey
       <<",\"selected\":"<<((flags&1)?"true":"false")<<",\"blocked\":"<<((flags&2)?"true":"false")
       <<",\"pending\":"<<((flags&4)?"true":"false")<<",\"claim\":"<<((flags&8)?"true":"false")
       <<",\"submit_refused\":"<<((flags&16)?"true":"false")<<",\"gate_failed\":"<<((flags&32)?"true":"false")
       <<",\"equipment_status_id\":"<<(status&255)<<",\"interaction_status_id\":"<<((status>>8)&255)
       <<",\"reload_status_id\":"<<((status>>16)&255)<<",\"hand_reason_id\":"<<(status>>24)
       <<"},\"last_supported_issue\":{\"reason\":\""<<FeedExitName(d.lastIssue.load(std::memory_order_relaxed))
       <<"\",\"flags_selected_blocked_pending_claim_refused_gate_failed\":"<<d.lastIssueFlags.load(std::memory_order_relaxed)
       <<",\"equipment_status_id\":"<<(issueStatus&255)<<",\"interaction_status_id\":"<<((issueStatus>>8)&255)
       <<",\"reload_status_id\":"<<((issueStatus>>16)&255)<<",\"hand_reason_id\":"<<(issueStatus>>24)
       <<"},\"last_view_counts\":{\"loaded\":"<<d.loaded.load(std::memory_order_relaxed)
       <<",\"reserve\":"<<d.reserve.load(std::memory_order_relaxed)<<",\"capacity\":"<<d.capacity.load(std::memory_order_relaxed)<<"}}";
}
}
