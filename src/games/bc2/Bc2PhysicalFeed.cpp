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
    const auto& m=*s.selected;
    if(reject(18,m.owner!=s.owner)||reject(19,!m.sequence)||reject(20,!m.configurationPathVerified)||reject(21,!m.configurationPathPointer)||
       reject(22,!ReloadDescriptorText(m.configurationPath,g.configurationPath))||reject(23,!ReloadDescriptorText(m.weaponName,g.asset))||
       reject(24,m.weaponData!=v.binding.data)||reject(25,m.inventory!=v.binding.inventory)||
       reject(26,m.observedNs<=0)||reject(27,s.input.nowNs<m.observedNs)||reject(28,s.input.nowNs>=m.deadlineNs)||
       reject(29,m.stateCount!=1)||reject(30,!m.soleConfiguredArray))return false;
    unsigned matches=0;const auto& state=m.states[0];
    if(reject(31,state.array!=m.soleConfiguredArray)||reject(32,!state.count)||reject(33,state.count>state.meshes.size()))return false;
    for(unsigned i=0;i<state.count;++i)if(ReloadDescriptorText(state.meshes[i].assetPath,g.meshPath)&&state.meshes[i].address>=0x10000)++matches;
    failure=matches==1?0:34;return matches==1;
}
math::Matrix4 BlendRigid(const math::Matrix4& a,const math::Matrix4& b,float t)noexcept {
    if(t<=0)return a;if(t>=1)return b;
    return reload_insertion_detail::BlendRigid(a,b,t);
}
}
std::shared_ptr<const Bc2PhysicalFeedPresentation> ReadBc2PhysicalFeedPresentation(
    const ReloadStateOwner& owner,std::int64_t now)noexcept {
    const auto p=publication.load(std::memory_order_acquire);
    return p&&p->owner==owner&&Window(p->input,now)?p:nullptr;
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
    struct Packet {ReloadStateOwner owner{};HandInteractionSample input{};HandInteractionKey weapon{};
        std::optional<FeedEquipmentNativeView> native;bool grip=false,reload=false;std::optional<math::Matrix4> bodyFromHand;};
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
        std::optional<FeedEquipmentSubmission> pending;
        std::optional<HandClaimToken> claim;
        std::optional<FeedObservedContact> recoveryContact;
        std::optional<math::Matrix4> presentationSource;
        std::optional<FeedEquipmentGate> gate;
        math::Matrix4 pouchBasis{},chainBasis{};
        std::uint16_t carriedPartsMask=0;
        bool gateFailed=false;
        bool refused=false,motionDirty=false;
        FeedPhysicalOperation activeOperation=FeedPhysicalOperation::None;
        std::optional<FeedEquipmentSessionSample> neutral;
        bool Blocked()const noexcept {return pending||refused||gateFailed||cover!=FeedCoverState::Closed||
            container!=FeedContainerState::Attached||seat!=FeedSeatState::Seated||
            (session&&session->State().status!=FeedEquipmentStatus::Complete);}
    };
    Bc2PhysicalFeedApi api{};std::array<Packet,32> packets{};unsigned nextPacket=0;
    std::array<std::unique_ptr<Equipment>,8> equipment{};
    std::uint64_t retirementEvent=0;
    explicit State(Bc2PhysicalFeedApi a):api(a){}
    void RetireActors(const HandInteractionSample& input,HandInteraction& hands)noexcept {
        if(!api.retirement)return;
        for(unsigned n=0;n<64;++n){
            const auto proof=api.retirement(api.context,retirementEvent);
            if(!proof||!FeedEquipmentActorRetirementValid(*proof)||proof->event<=retirementEvent||proof->observedNs>input.nowNs)break;
            // This is a native actor-end receipt, not an ammunition success.
            // The backend preserves dispatched/ambiguous debt in its audit.
            for(auto& slot:equipment)if(slot&&
                AmmoResourceSameNativeActor(slot->original.identity,proof->previous)&&
                slot->original.identity.owner.actorGeneration==proof->previous.owner.actorGeneration){
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
    void PublishGate(Equipment& e)noexcept {
        if(!api.publishGate||!e.lifetime)return;
        const bool debt=e.pending||e.refused||e.cover!=FeedCoverState::Closed||e.container!=FeedContainerState::Attached||
            e.seat!=FeedSeatState::Seated||(e.session&&e.session->State().status!=FeedEquipmentStatus::Complete);
        const FeedEquipmentGate gate{e.original.binding.context.resource,e.lifetime,++e.revision,debt,!debt};
        if(api.publishGate(api.context,gate)){e.gate=gate;e.gateFailed=false;}
        else {e.gateFailed=true;diagnostics.gateRefusals.fetch_add(1,std::memory_order_relaxed);} // Contention is not permission to clear old native debt.
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
    void ReprojectCarry(Equipment& e,const math::Matrix4& currentWeapon)noexcept {
        const auto inverse=Inverse(currentWeapon);
        if(e.carriedPartsMask&1){e.pose.weaponFromPouch=feed_interaction_detail::Multiply(feed_interaction_detail::Multiply(e.pose.weaponFromPouch,e.pouchBasis),inverse);e.pouchBasis=currentWeapon;}
        if(e.carriedPartsMask&ChainMask(e.registration.geometry.chainCount)){e.pose.weaponFromFeedRoot=feed_interaction_detail::Multiply(feed_interaction_detail::Multiply(e.pose.weaponFromFeedRoot,e.chainBasis),inverse);e.chainBasis=currentWeapon;}
    }
    void PoseTarget(Equipment& e,const FeedInteractionResult& r,const math::Matrix4& weaponWorld)noexcept {
        if(!r.target)return;const auto& g=e.registration.geometry;const auto op=e.activeOperation;
        if(op==FeedPhysicalOperation::OpenCover||op==FeedPhysicalOperation::CloseCover){
            const float direction=op==FeedPhysicalOperation::OpenCover?1.f:-1.f;
            e.coverRadians=std::clamp(e.gestureCoverRadians+direction*float(r.progress)*g.coverTravelRadians,
                (std::min)(0.f,g.coverTravelRadians),(std::max)(0.f,g.coverTravelRadians));return;}
        if(r.targetKind!=FeedInteractionTargetKind::Object)return;
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
void Bc2PhysicalFeed::Suspend(const HandInteractionSample& input,HandInteraction& hands)noexcept {
    publication.store({},std::memory_order_release);if(!state_)return;
    state_->diagnostics.suspends.fetch_add(1,std::memory_order_relaxed);
    for(auto& e:state_->equipment)if(e){if(e->claim){(void)hands.Release(input,*e->claim);e->claim.reset();}
        e->ammunitionSupply.Suspend(input,hands);e->supplyNeutral=false;
        if(e->session)e->motionDirty=true;
        // A rejected current packet may be newer than the last renderer
        // source. Its delayed release cannot re-arm this equipment lifetime.
        const auto& owner=e->original.identity.owner;
        if(input.owner==HandInteractionOwner{(std::uint64_t(owner.weak)<<32)|owner.soldier,
            owner.actorGeneration,e->weapon.generation,owner.space})
            e->rejectedInput=(std::max)(e->rejectedInput,input.sequence);
        e->neutral.reset();if(e->session){FeedEquipmentSessionSample sample;sample.nowNs=input.nowNs;(void)e->session->Update(sample);}}
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
    State::Equipment* e=nullptr;
    const Bc2FeedRegistration* registration=nullptr;
    const auto finish=[&](FeedExit reason){
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
    state.RetireActors(s.input,hands);out.blocksFire=blocksFire();
    if(!state.api.read||!state.api.submit||!state.api.receipt){Suspend(s.input,hands);return finish(FeedExit::ApiUnavailable);}
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
            if(receipt->command.authority.effect==FeedAmmunitionEffect::RefillLoaded)
                (void)old.ammunitionSupply.Complete(*receipt);
            old.pending.reset();old.refused=false;old.motionDirty=true;old.recoveryContact.reset();old.presentationSource.reset();++old.revision;}
        else {old.cover=cover;old.container=container;old.seat=seat;old.refill=refill;old.carriedPartsMask=carried;}
    }
    for(auto& e:state.equipment)if(e)state.PublishGate(*e);
    out.blocksFire=blocksFire();out.blocksEquipment=blocksEquipment();
    const auto view=state.api.read(state.api.context,s.owner,now);
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
    // Preserve the original short-circuit order. A bit absent from checked was
    // never evaluated; passed bits are observations, never admission authority.
    const auto admission=[&](unsigned bit,bool passed){out.diagnostic.admissionChecked|=1u<<bit;
        if(passed)out.diagnostic.admissionPassed|=1u<<bit;return passed;};
    if(!admission(0,bool(view))||!admission(1,bool(registration))||!admission(2,!e||&e->registration==registration)||
       !admission(3,Selected(s,*view,*registration,out.diagnostic.selectionFailure))||!admission(4,!s.cancel)||!admission(5,Window(s.input,now))){Suspend(s.input,hands);
        return finish(view?FeedExit::SelectionOrInput:FeedExit::ViewUnavailable);}
    out.selected=true;
    if(!e){for(auto& slot:state.equipment)if(!slot){try{
        slot=std::make_unique<State::Equipment>(*registration);e=slot.get();e->original=*view;e->weapon=s.weapon;
        const auto generation=view->binding.context.resource.weaponGeneration;
        e->supply={{view->binding.data,generation},{view->identity.owner.weapon,generation}};
        e->mechanism={registration->key,generation};e->motions=BuildBc2FeedInteractionProfile(registration->geometry,
            registration->mechanism.id,registration->mechanism.revision,registration->interactionPolicy);
        e->lifetime=Lifetime();e->pouchBasis=e->chainBasis=s.raw.weaponWorldMeters;state.PublishGate(*e);
    }catch(...){return finish(FeedExit::StorageUnavailable);}break;}}
    if(!e){out.blocksFire=true;return finish(FeedExit::StorageUnavailable);}
    const auto& profile=e->registration.mechanism;
    // Keep the old immutable transaction and debt when a different native
    // equip/space lifetime appears. A name/count match cannot migrate it.
    if(e->original.identity!=view->identity||e->weapon!=s.weapon){
        if(e->Blocked()){
            Suspend(s.input,hands);bool migrated=false;
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
        }else e->session.reset();
        e->original=*view;e->weapon=s.weapon;e->neutral.reset();e->lastInput=e->rejectedInput=0;e->motionDirty=true;
    }
    const auto right=hands.Current(InteractionHand::Right);
    const bool gun=right&&right->token.owner==s.input.owner&&right->token.item==s.weapon&&
        right->token.kind==HandClaimKind::GunHold&&!right->token.prerequisiteClaim&&right->deadlineNs>now;
    const State::Packet* source=nullptr;
    for(const auto& p:state.packets)if(p.owner==s.owner&&p.weapon==s.weapon&&SamePacket(p.input,s.raw.inputEvidence)){source=&p;break;}
    const auto rawCheck=[&](unsigned bit,bool passed){out.diagnostic.rawChecked|=1u<<bit;
        if(passed)out.diagnostic.rawPassed|=1u<<bit;return passed;};
    (void)rawCheck(0,gun);
    const bool raw=rawCheck(1,bool(source))&&rawCheck(2,bool(source->native))&&rawCheck(3,Window(source->input,now))&&
        rawCheck(4,source->input.observedNs>=source->native->observedNs)&&rawCheck(5,FeedEquipmentViewFresh(*source->native,now))&&
        rawCheck(6,FeedEquipmentSameItem(*source->native,*view))&&rawCheck(7,source->native->counts==view->counts)&&
        rawCheck(8,hands.HasRecordedEvidence(source->input,InteractionHand::Left));
    if(!gun||!raw){Suspend(s.input,hands);out.blocksFire=e->Blocked();return finish(FeedExit::GunOrRawUnverified);}
    auto sourceInput=source->input;sourceInput.nowNs=now;
    const auto local=feed_interaction_detail::Multiply(s.raw.rawLeftWristWorldMeters,Inverse(s.raw.weaponWorldMeters));
    const auto body=state.BodyAmmo(*e,*view);
    const bool acquiredSupply=e->ammunitionSupply.Update(body,s.input,s.bodyFromHand,s.grip,
        e->container==FeedContainerState::Detached&&e->supplyNeutral&&!e->pending,hands,intent);
    if(acquiredSupply){
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
    bool available=!left||(e->claim&&left->token==*e->claim)||supplyHand;
    if(e->claim&&(!left||left->token!=*e->claim))e->claim.reset();
    // Neutral arms the input edge, not contact. Like FeedInteraction, accept a
    // released hand away from the cover, then consume that neutral on the next
    // grip packet. Only that edge may acquire the exact cover contact.
    const bool idle=!e->session||e->session->State().status==FeedEquipmentStatus::Complete;
    if(idle){
        const auto& m=e->motions.operations[unsigned(FeedPhysicalOperation::OpenCover)];
        const auto contact=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        const auto& policy=profile.contacts[unsigned(FeedPart::Cover)];
        const auto distance=Distance(local,contact),angle=Angle(local,contact);
        const bool touch=distance<=policy.radiusMeters&&angle<=policy.maxAngleRadians;
        const bool freshSequence=sourceInput.sequence>e->lastInput&&sourceInput.sequence>e->rejectedInput;
        if(!source->grip&&sourceInput.released[0]&&available&&freshSequence){
            FeedEquipmentSessionSample neutral;neutral.interaction.contact.weaponFromHandContact=local;
            neutral.interaction.contact.observedNs=sourceInput.observedNs;neutral.interaction.contact.inputSequence=sourceInput.sequence;
            neutral.nowNs=now;neutral.ammunition=state.Ammo(*e,*source->native);neutral.interaction.focused=true;neutral.interaction.contact.tracked=true;
            e->neutral=neutral;
        }
        const auto neutral=e->neutral;
        auto& diagnostic=out.diagnostic;diagnostic.coverDistanceMeters=float(distance);diagnostic.coverAngleRadians=float(angle);
        if(neutral)diagnostic.neutralAgeNs=now-neutral->interaction.contact.observedNs;
        const auto idleCheck=[&](unsigned bit,bool passed){diagnostic.idleChecked|=1u<<bit;
            if(passed)diagnostic.idlePassed|=1u<<bit;return passed;};
        (void)idleCheck(0,available);(void)idleCheck(1,!source->grip&&sourceInput.released[0]);
        (void)idleCheck(2,freshSequence);(void)idleCheck(3,source->grip);
        (void)idleCheck(4,distance<=policy.radiusMeters);(void)idleCheck(5,angle<=policy.maxAngleRadians);
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
            e->lifetime=lifetime;e->refill={FeedRefillState::Required,lifetime,0};e->refused=false;
            try{e->session=std::make_unique<FeedEquipmentSession>(profile,e->motions,FeedEquipmentOwner(s.owner),e->mechanism,e->supply,lifetime,
                e->original.binding.context.resource);}catch(...){out.blocksFire=true;return finish(FeedExit::SessionUnavailable);}
            state.PublishGate(*e);
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
        if(supplied&&source->grip)e->supplyNeutral=false;
        const bool chestNeutral=needsSupply&&!e->ammunitionSupply.Custody()&&!source->grip&&sourceInput.released[0]&&
            available&&source->bodyFromHand&&Bc2FeedSupplyContact(*source->bodyFromHand);
        if(chestNeutral){
            // Arm from the actual neutral chest/belt contact. A detached pouch
            // is not moved by this preparation; the pickup edge moves it.
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
                    }
                }
            }
        }
        const auto prior=e->session->State();
        bool recoveryEstablished=false;
        const auto query=EvaluateFeedMechanism(profile,sample.snapshot,e->supply,now);
        if(query.candidate){const auto op=query.candidate->operation;
            const auto recovery=e->session->RecoveryMotion();
            const bool recoverable=recovery&&Same(recovery->Candidate(),*query.candidate);
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
                    state.ReprojectCarry(*e,s.raw.weaponWorldMeters);e->gestureStart=e->pose;e->presentationSource=held;
                    e->recoveryContact=recovered.contacts[unsigned(query.candidate->part)];e->motionDirty=false;sample.snapshot=recovered;
                    recoveryEstablished=true;
                }
            }else if(e->motionDirty&&op!=FeedPhysicalOperation::OpenCover&&op!=FeedPhysicalOperation::CloseCover){
                state.ReprojectCarry(*e,s.raw.weaponWorldMeters);
                const auto motion=BuildBc2FeedMotionFromRetainedPose(e->registration.geometry,op,e->registration.interactionPolicy,e->pose);
                auto rebased=sample.snapshot;
                if(motion)rebased.contacts[unsigned(query.candidate->part)].weaponFromContact=
                    feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,motion->placement.weaponFromSource);
                if(motion&&e->session->UpdateMotion(rebased,sample.ammunition,*query.candidate,*motion,now)){
                    e->motions.operations[unsigned(op)]=*motion;e->motionDirty=false;e->gestureStart=e->pose;
                    sample.snapshot=state.Snapshot(*e,sample.ammunition,sourceInput);}
            }else if(e->motionDirty&&!recoverable)e->motionDirty=false;
            sample.authority=state.Authority(*e,sample.ammunition,op);
            if(e->activeOperation!=op){e->activeOperation=op;e->gestureStart=e->pose;e->gestureCoverRadians=e->coverRadians;
                e->recoveryContact.reset();e->presentationSource.reset();}
        }
        // RecoverMotion binds the retained proof to this observation. Its first
        // neutral/gesture packet must be newer; replaying this one would revoke
        // that proof as InvalidInput before the player can regrasp.
        if(!recoveryEstablished){
        sample.interaction.contact={sample.snapshot.owner,e->mechanism,sample.snapshot.sequence,sourceInput.sequence,
            sourceInput.observedNs,true,available,local};
        sample.interaction.hand={1,sourceInput.owner.equipGeneration};sample.interaction.trackingGeneration=sourceInput.owner.space;
        sample.interaction.focused=s.input.focused;sample.interaction.grip=source->grip;
        if(e->motions.operations[unsigned(e->activeOperation)].kind==FeedMotionKind::FreePlacement)
            sample.interaction.weaponWorld=s.raw.weaponWorldMeters;
        sample.held=available&&(source->grip||sourceInput.released[0])&&!e->refused&&!e->gateFailed&&!e->motionDirty&&
            (!needsSupply||supplied||pickupAwaitingRaw||(chestNeutral&&e->supplyNeutral));
        if(prior.session.reload.pending&&prior.ammunition.lastReceipt&&!prior.ammunition.pending&&
           prior.ammunition.lastReceipt->command.request==*prior.session.reload.pending){
            auto ack=prior.ammunition.lastReceipt->acknowledgement;
            ack.snapshotSequence=sample.snapshot.sequence;ack.observedNs=sample.snapshot.observedNs;
            sample.acknowledgement=ack; // Fresh exact native-count corroboration, original receipt unchanged.
        }
        if(source->grip&&sample.held&&query.candidate&&!prior.ammunition.pending&&!prior.session.reload.pending){
            const auto contact=EvaluateFeedContact(profile,sample.snapshot,e->supply,*query.candidate,sample.interaction.contact,now);
            // AcquireFrom/RenewFrom preserve the exact recorded raw packet.
            // Native authority has its own independently checked deadline.
            const HandContactProof proof{{PartKey(e->registration.key,unsigned(query.candidate->part)),e->lifetime},sourceInput.sequence,
                sourceInput.deadlineNs,true};
            if(supplyHand&&needsSupply&&supplied){
                // AmmoSupply owns this exact hand; do not manufacture or steal
                // a Mechanism claim for a carried reserve object.
                if(!hands.HasRecordedEvidence(sourceInput,InteractionHand::Left))sample.held=false;
            }else if(e->claim){const auto renewed=hands.RenewFrom(s.input,sourceInput,*e->claim,proof);out.handReason=renewed.reason;
                if(!renewed.accepted){e->claim.reset();sample.held=false;}}
            else if(contact.status==FeedMechanismStatus::ContactMatch&&intent!=UINT64_MAX){
                const HandClaimRequest request{s.input.owner,InteractionHand::Left,HandClaimKind::Mechanism,
                    s.weapon,proof,++intent,right->token.id};
                const auto acquired=hands.AcquireFrom(s.input,sourceInput,request);
                out.handReason=acquired.reason;
                if(acquired.claim)e->claim=acquired.claim->token;else sample.held=false;
            }else sample.held=false;
        }
        const auto result=e->session->Update(sample);out.status=result.status;
        out.interactionStatus=result.session.interaction.status;out.reloadStatus=result.session.reload.status;
        out.diagnostic.policyEvaluated=true;
        state.PoseTarget(*e,result.session.interaction,s.raw.weaponWorldMeters);
        if(!result.session.interaction.target&&!result.session.interaction.completion&&
           (result.session.interaction.status==FeedInteractionStatus::Interrupted||result.session.interaction.status==FeedInteractionStatus::OffPath||
            result.session.interaction.status==FeedInteractionStatus::MotionJump||result.session.interaction.status==FeedInteractionStatus::Expired))e->motionDirty=true;
        if(result.dispatch){FeedEquipmentSubmission submission;submission.command=*result.dispatch;submission.source=*source->native;
            const bool supplyBound=result.dispatch->authority.effect!=FeedAmmunitionEffect::RefillLoaded||
                e->ammunitionSupply.Bind(*result.dispatch,state.BodyAmmo(*e,*source->native),sourceInput,hands);
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
        if(e->claim&&(!source->grip||result.dispatch||!sample.held)){
            (void)hands.Release(s.input,*e->claim);e->claim.reset();}
        }else out.status=e->session->State().status;
        out.pending=e->pending.has_value();out.ownsHand=e->claim.has_value()||e->ammunitionSupply.Held().has_value();
    }
    state.PublishGate(*e);out.blocksFire=e->Blocked();out.blocksEquipment=e->pending.has_value()||e->claim.has_value()||e->ammunitionSupply.Held().has_value();
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
        p->originalWeaponWorldMeters=s.raw.weaponWorldMeters;p->operation=e->activeOperation;
        if(e->claim){const auto claim=hands.Current(InteractionHand::Left);
            if(claim&&claim->token==*e->claim&&claim->inputSequence==sourceInput.sequence&&claim->deadlineNs>now){
                p->mechanismClaim=claim;p->mechanismWristWorldMeters=s.raw.rawLeftWristWorldMeters;
                p->targetKind=e->motions.operations[unsigned(e->activeOperation)].kind==FeedMotionKind::FreePlacement?
                    FeedInteractionTargetKind::Object:FeedInteractionTargetKind::Contact;}}
        else if(const auto& held=e->ammunitionSupply.Held();held&&held->claim.inputSequence==sourceInput.sequence){
            p->supplyObject=held;p->mechanismClaim=held->claim;p->mechanismWristWorldMeters=s.raw.rawLeftWristWorldMeters;
            p->targetKind=FeedInteractionTargetKind::Object;
        }
        math::Matrix4 identity{};for(unsigned i=0;i<4;++i)identity.values[i][i]=1;
        p->weaponFromCoverHinge=feed_interaction_detail::Multiply(Along(identity,FeedMotionKind::Hinge,e->coverRadians),e->registration.geometry.weaponFromHinge);
        p->blocksFire=out.blocksFire;out.presentation=p;publication.store(std::move(p),std::memory_order_release);
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
