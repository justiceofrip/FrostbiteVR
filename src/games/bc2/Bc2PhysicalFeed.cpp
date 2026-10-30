#include "Bc2PhysicalFeed.h"
#include <atomic>
#include <limits>

namespace fvr::bc2 {
namespace {
using namespace interaction;
using namespace feed_interaction_detail;
using feed_mechanism_detail::Pose;
constexpr std::uint64_t ProfileId=0x4d363046454544ull;
std::atomic<std::uint64_t> nextLifetime=0;
std::atomic<std::shared_ptr<const Bc2PhysicalFeedPresentation>> publication;
std::uint64_t Lifetime()noexcept {
    auto old=nextLifetime.load();
    while(old!=UINT64_MAX)if(nextLifetime.compare_exchange_weak(old,old+1))return old+1;
    return 0;
}
FeedMechanismProfile Mechanism()noexcept {
    FeedMechanismProfile p;p.family=FeedFamily::Belt;p.id=ProfileId;p.revision=1;p.ammunitionKind=1;
    p.cover=p.container=p.containerNeedsOpen=p.feedNeedsOpen=true;
    p.sameContainerReplenishment=p.coupledContainerDetach=true;p.maxAgeNs=100000000;
    // Named interaction policy: 8cm anatomical reach, 0.65rad grasp alignment.
    p.contacts[unsigned(FeedPart::Cover)]={1,.08f,.65f};
    p.contacts[unsigned(FeedPart::Container)]={2,.08f,.65f};
    p.contacts[unsigned(FeedPart::Feed)]={3,.08f,.65f};return p;
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
    const WeaponEquipmentIdentity& equipment,const std::optional<AmmoResourceBinding>& binding)noexcept {
    const auto& o=original.identity.owner;
    return equipment.weapon==owner.weapon&&o.player==owner.player&&o.soldier==owner.soldier&&o.weak==owner.weak&&
        o.actorGeneration==owner.actorGeneration&&o.weapon==owner.weapon&&original.binding.data==equipment.data&&
        original.binding.persistence==equipment.persistence&&equipment.Asset()==bc2_feed_profile_detail::M60Geometry.asset&&
        (!binding||(binding->owner==owner&&binding->context.resource==original.binding.context.resource&&
            binding->data==equipment.data&&binding->persistence==equipment.persistence&&
            binding->inventory==original.binding.inventory&&binding->switching==original.binding.switching));
}
bool Selected(const Bc2PhysicalFeedSample& s,const FeedEquipmentNativeView& v)noexcept {
    const auto* g=FindBc2M60FeedGeometry(s.asset,s.configurationPath,s.meshPath,s.rigFingerprint);
    if(!g||!s.equipment||!CurrentEquipment(v,s.owner,*s.equipment,s.binding)||
       (s.binding&&!AmmoResourceBindingFresh(*s.binding,s.owner,s.input.nowNs))||
       !s.selectedRigVerified||!s.selected||!NativeHand(s)||!s.raw.valid||s.raw.owner!=s.owner||
       s.raw.rig.soldier!=s.owner.soldier||s.raw.rig.weak!=s.owner.weak||
       s.raw.rigFingerprint!=g->rigFingerprint||s.frameSequence!=s.raw.inputEvidence.sequence||
       !Pose(s.raw.rawLeftWristWorldMeters)||!Pose(s.raw.weaponWorldMeters)||
       !FeedEquipmentViewFresh(v,s.input.nowNs)||v.identity.owner!=s.owner)return false;
    const auto& m=*s.selected;
    if(m.owner!=s.owner||!m.sequence||!m.configurationPathVerified||!m.configurationPathPointer||
       !ReloadDescriptorText(m.configurationPath,g->configurationPath)||!ReloadDescriptorText(m.weaponName,g->asset)||
       m.weaponData!=v.binding.data||m.inventory!=v.binding.inventory||
       m.observedNs<=0||s.input.nowNs<m.observedNs||s.input.nowNs>=m.deadlineNs||
       m.stateCount!=1||!m.soleConfiguredArray)return false;
    unsigned matches=0;const auto& state=m.states[0];
    if(state.array!=m.soleConfiguredArray||!state.count||state.count>state.meshes.size())return false;
    for(unsigned i=0;i<state.count;++i)if(ReloadDescriptorText(state.meshes[i].assetPath,g->meshPath)&&state.meshes[i].address>=0x10000)++matches;
    return matches==1;
}
math::Matrix4 BlendRigid(const math::Matrix4& a,const math::Matrix4& b,float t)noexcept {
    if(t<=0)return a;if(t>=1)return b;
    return reload_insertion_detail::BlendRigid(a,b,t);
}
}
Bc2FeedInteractionPolicy Bc2M60FeedInteractionPolicy()noexcept {
    // 95% hinge travel; 2cm/0.18rad placement acceptance; bounded 8cm/0.35rad
    // interpacket motion; 60ms endpoint dwell, 10s gesture, 100ms source gap.
    return {.95f,.15f,.02f,.18f,.08f,.35f,60000000,10000000000ll,100000000};
}
std::shared_ptr<const Bc2PhysicalFeedPresentation> ReadBc2PhysicalFeedPresentation(
    const ReloadStateOwner& owner,std::int64_t now)noexcept {
    const auto p=publication.load(std::memory_order_acquire);
    return p&&p->owner==owner&&Window(p->input,now)?p:nullptr;
}
struct Bc2PhysicalFeed::State {
    struct Packet {ReloadStateOwner owner{};HandInteractionSample input{};HandInteractionKey weapon{};
        std::optional<FeedEquipmentNativeView> native;bool grip=false,reload=false;};
    struct Equipment {
        FeedEquipmentNativeView original{};
        HandInteractionKey weapon{};FeedSupplyKey supply{};FeedKey mechanism{};
        FeedMechanismProfile profile=Mechanism();FeedInteractionProfile motions{};
        Bc2M60FeedPose pose=bc2_feed_profile_detail::M60Geometry.poses[0];
        Bc2M60FeedPose gestureStart{};
        FeedCoverState cover=FeedCoverState::Closed;FeedContainerState container=FeedContainerState::Attached;
        FeedSeatState seat=FeedSeatState::Seated;FeedRefillObservation refill{FeedRefillState::NotRequired,0,0};
        float coverRadians=0,gestureCoverRadians=0;std::uint64_t lifetime=0,revision=0,lastInput=0;
        std::unique_ptr<FeedEquipmentSession> session;
        std::optional<FeedEquipmentSubmission> pending;
        std::optional<HandClaimToken> claim;
        std::optional<FeedObservedContact> recoveryContact;
        std::optional<math::Matrix4> presentationSource;
        std::optional<FeedEquipmentGate> gate;
        math::Matrix4 pouchBasis{},chainBasis{};
        std::uint8_t carriedPartsMask=0;
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
        else e.gateFailed=true; // Contention is not permission to clear old native debt.
    }
    static bool SameResource(const Equipment& e,const FeedEquipmentNativeView& v)noexcept {
        return e.original.binding.context.resource==v.binding.context.resource&&
            e.original.binding.data==v.binding.data&&e.original.binding.persistence==v.binding.persistence;
    }
    FeedMechanismSnapshot Snapshot(Equipment& e,const FeedAmmunitionSnapshot& a,const HandInteractionSample& input)noexcept {
        FeedMechanismSnapshot s;s.owner=a.owner;s.mechanism=e.mechanism;s.profileId=e.profile.id;s.profileRevision=e.profile.revision;
        s.sequence=a.sequence;s.inputSequence=input.sequence;s.observedNs=a.observedNs;s.reconciled=true;
        s.cover=e.cover;s.latch=FeedLatchState::Absent;s.container=e.container;s.feed=e.seat;s.cycle=FeedCycleState::Absent;
        s.attached=e.container==FeedContainerState::Attached?e.supply:FeedSupplyKey{};
        s.seated=e.seat==FeedSeatState::Seated?e.supply:FeedSupplyKey{};
        s.supply={e.supply,e.profile.ammunitionKind,FeedSupplyState::Usable};s.refill=e.refill;
        // Contacts are frozen gesture sources, never the guided hand pose.
        for(auto op:{FeedPhysicalOperation::OpenCover,FeedPhysicalOperation::DetachContainer,FeedPhysicalOperation::SeatFeed}){
            if(op==FeedPhysicalOperation::OpenCover&&e.cover==FeedCoverState::Open)op=FeedPhysicalOperation::CloseCover;
            if(op==FeedPhysicalOperation::DetachContainer&&e.container==FeedContainerState::Detached)op=FeedPhysicalOperation::AttachContainer;
            const auto& motion=e.motions.operations[unsigned(op)];const auto part=unsigned(feed_mechanism_detail::OperationPart(op));
            s.contacts[part]={e.profile.contacts[part].role,motion.kind==FeedMotionKind::FreePlacement?
                feed_interaction_detail::Multiply(motion.placement.sourceFromGrip,motion.placement.weaponFromSource):feed_interaction_detail::Multiply(motion.motionFromContact,motion.weaponFromMotion)};
        }
        if(e.recoveryContact){const auto part=unsigned(feed_mechanism_detail::OperationPart(e.activeOperation));
            if(part<s.contacts.size())s.contacts[part]=*e.recoveryContact;}
        return s;
    }
    FeedAmmunitionSnapshot Ammo(const Equipment& e,const FeedEquipmentNativeView& v)noexcept {
        return {FeedEquipmentOwner(v.identity.owner),e.mechanism,e.lifetime,v.sequence,v.observedNs,v.deadlineNs,v.counts,e.supply,true};
    }
    FeedReloadBinding Binding(const Equipment& e)noexcept {
        FeedReloadBinding b;b.key={ProfileId,e.original.binding.context.resource.weaponGeneration};
        b.owner=FeedEquipmentOwner(e.original.identity.owner);b.mechanism=e.mechanism;b.profileId=e.profile.id;b.profileRevision=e.profile.revision;b.verified=true;
        for(auto op:{FeedPhysicalOperation::OpenCover,FeedPhysicalOperation::DetachContainer,FeedPhysicalOperation::AttachContainer,
            FeedPhysicalOperation::SeatFeed,FeedPhysicalOperation::CloseCover})b.operations[unsigned(op)]=true;return b;
    }
    FeedAmmunitionAuthority Authority(const Equipment& e,const FeedAmmunitionSnapshot& a,FeedPhysicalOperation op)noexcept {
        const auto binding=Binding(e);FeedAmmunitionAuthority x;
        x.key={ProfileId+unsigned(op),e.lifetime};x.binding=binding.key;x.owner=a.owner;x.mechanism=e.mechanism;
        x.lifetime=e.lifetime;x.profileId=e.profile.id;x.profileRevision=e.profile.revision;x.operation=op;x.verified=true;
        x.effect=FeedAmmunitionEffect::PhysicalOnly;
        if(op==FeedPhysicalOperation::AttachContainer){x.effect=FeedAmmunitionEffect::RefillLoaded;x.supply=e.supply;
            x.rounds=(std::min)(a.counts.capacity-a.counts.loaded,a.counts.reserve);}
        return x;
    }
    void ReprojectCarry(Equipment& e,const math::Matrix4& currentWeapon)noexcept {
        const auto inverse=Inverse(currentWeapon);
        if(e.carriedPartsMask&1){e.pose.weaponFromPouch=feed_interaction_detail::Multiply(feed_interaction_detail::Multiply(e.pose.weaponFromPouch,e.pouchBasis),inverse);e.pouchBasis=currentWeapon;}
        if(e.carriedPartsMask&60){e.pose.weaponFromFeedRoot=feed_interaction_detail::Multiply(feed_interaction_detail::Multiply(e.pose.weaponFromFeedRoot,e.chainBasis),inverse);e.chainBasis=currentWeapon;}
    }
    void PoseTarget(Equipment& e,const FeedInteractionResult& r,const math::Matrix4& weaponWorld)noexcept {
        if(!r.target)return;const auto& g=bc2_feed_profile_detail::M60Geometry;const auto op=e.activeOperation;
        if(op==FeedPhysicalOperation::OpenCover||op==FeedPhysicalOperation::CloseCover){
            const float direction=op==FeedPhysicalOperation::OpenCover?1.f:-1.f;
            e.coverRadians=std::clamp(e.gestureCoverRadians+direction*float(r.progress)*g.coverTravelRadians,0.f,g.coverTravelRadians);return;}
        if(r.targetKind!=FeedInteractionTargetKind::Object)return;
        const auto destination=op==FeedPhysicalOperation::DetachContainer?Bc2M60FeedPoseId::PouchWithdrawn:
            op==FeedPhysicalOperation::AttachContainer?Bc2M60FeedPoseId::PouchAttachedFeedUnseated:Bc2M60FeedPoseId::FeedSeated;
        const auto& end=g.poses[unsigned(destination)];const auto& motion=e.motions.operations[unsigned(op)];
        const auto total=Distance(e.presentationSource.value_or(motion.placement.weaponFromSource),motion.placement.weaponFromDestination);
        const auto remaining=Distance(*r.target,motion.placement.weaponFromDestination);
        const float t=total>1e-5?float(std::clamp(1-remaining/total,0.,1.)):0.f;
        auto articulation=e.gestureStart;
        for(unsigned i=0;i<3;++i)articulation.parentFromChainChild[i]=BlendRigid(e.gestureStart.parentFromChainChild[i],end.parentFromChainChild[i],t);
        articulation.weaponFromFeedRoot=BlendRigid(e.gestureStart.weaponFromFeedRoot,end.weaponFromFeedRoot,t);
        articulation.weaponFromPouch=op==FeedPhysicalOperation::SeatFeed?e.pose.weaponFromPouch:
            BlendRigid(e.gestureStart.weaponFromPouch,end.weaponFromPouch,t);
        const auto p=op==FeedPhysicalOperation::SeatFeed?PlaceBc2M60FeedTerminal(e.supply,articulation,*r.target):
            PlaceBc2M60FeedPouch(e.supply,articulation,*r.target);
        if(p){e.pose=articulation;e.pose.weaponFromPouch=p->weaponFromPouch;e.pose.weaponFromFeedRoot=p->weaponFromChain[0];
            e.chainBasis=weaponWorld;e.carriedPartsMask=60;
            if(op!=FeedPhysicalOperation::SeatFeed){e.pouchBasis=weaponWorld;e.carriedPartsMask|=1;}}
    }
    void Applied(Equipment& e,const FeedAmmunitionReceipt& r)noexcept {
        switch(r.command.request.completion.candidate.operation){
        case FeedPhysicalOperation::OpenCover:e.cover=FeedCoverState::Open;break;
        case FeedPhysicalOperation::DetachContainer:e.container=FeedContainerState::Detached;e.seat=FeedSeatState::Unseated;break;
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
    for(const auto& e:state_->equipment)if(e&&CurrentEquipment(e->original,owner,equipment,binding)&&e->Blocked())return true;
    return false;
}
std::optional<FeedEquipmentGate> Bc2PhysicalFeed::RetainedGate(const ReloadStateOwner& owner,const WeaponEquipmentIdentity& equipment,
    const std::optional<AmmoResourceBinding>& binding)const noexcept {
    if(!state_)return {};
    for(const auto& e:state_->equipment)if(e){const auto& original=e->original;
        if(!CurrentEquipment(original,owner,equipment,binding))continue;
        auto retained=e->gate;
        if(state_->api.readGate)if(const auto current=state_->api.readGate(state_->api.context,original.binding.context.resource))retained=current;
        if(retained){retained->blocksFire=retained->blocksFire||e->Blocked();return retained;}
    }return {};
}
void Bc2PhysicalFeed::Suspend(const HandInteractionSample& input,HandInteraction& hands)noexcept {
    publication.store({},std::memory_order_release);if(!state_)return;
    for(auto& e:state_->equipment)if(e){if(e->claim){(void)hands.Release(input,*e->claim);e->claim.reset();}
        if(e->session)e->motionDirty=true;
        e->neutral.reset();if(e->session){FeedEquipmentSessionSample sample;sample.nowNs=input.nowNs;(void)e->session->Update(sample);}}
}
Bc2PhysicalFeedResult Bc2PhysicalFeed::Tick(const Bc2PhysicalFeedSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    const auto blocksFire=[&](){return s.equipment&&BlocksFire(s.owner,*s.equipment,s.binding);};
    Bc2PhysicalFeedResult out;out.blocksFire=blocksFire();publication.store({},std::memory_order_release);
    if(!state_)return out;auto& state=*state_;const auto now=s.input.nowNs;
    state.RetireActors(s.input,hands);out.blocksFire=blocksFire();
    if(!state.api.read||!state.api.submit||!state.api.receipt){Suspend(s.input,hands);return out;}
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
        if(accepted.receiptAccepted){old.pending.reset();old.refused=false;old.motionDirty=true;old.recoveryContact.reset();old.presentationSource.reset();++old.revision;}
        else {old.cover=cover;old.container=container;old.seat=seat;old.refill=refill;old.carriedPartsMask=carried;}
    }
    for(auto& e:state.equipment)if(e)state.PublishGate(*e);
    out.blocksFire=blocksFire();
    const auto view=state.api.read(state.api.context,s.owner,now);
    bool recorded=false;for(const auto& p:state.packets)if(p.owner==s.owner&&p.input.sequence==s.input.sequence){recorded=true;break;}
    if(!recorded){const auto source=state.api.readAt?state.api.readAt(state.api.context,s.owner,s.input.observedNs,now):view;
        const State::Packet* previous=nullptr;
        for(const auto& packet:state.packets)if(packet.owner==s.owner&&packet.input.sequence<s.input.sequence&&
            (!previous||packet.input.sequence>previous->input.sequence))previous=&packet;
        const bool grip=s.grip||(!s.input.released[0]&&previous&&previous->grip);
        state.packets[state.nextPacket]={s.owner,s.input,s.weapon,source,grip,s.reloadPressed};state.nextPacket=(state.nextPacket+1)%state.packets.size();}
    State::Equipment* e=nullptr;
    if(view)for(auto& p:state.equipment)if(p&&State::SameResource(*p,*view)){e=p.get();break;}
    if(!view||!Selected(s,*view)||s.cancel||!Window(s.input,now)){Suspend(s.input,hands);return out;}
    out.selected=true;
    if(!e){for(auto& slot:state.equipment)if(!slot){try{
        slot=std::make_unique<State::Equipment>();e=slot.get();e->original=*view;e->weapon=s.weapon;
        const auto generation=view->binding.context.resource.weaponGeneration;
        e->supply={{view->binding.data,generation},{view->identity.owner.weapon,generation}};
        e->mechanism={ProfileId,generation};e->motions=BuildBc2M60FeedInteractionProfile(bc2_feed_profile_detail::M60Geometry,
            e->profile.id,e->profile.revision,Bc2M60FeedInteractionPolicy());
        e->lifetime=Lifetime();e->pouchBasis=e->chainBasis=s.raw.weaponWorldMeters;state.PublishGate(*e);
    }catch(...){return out;}break;}}
    if(!e){out.blocksFire=true;return out;}
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
            }
            if(!migrated){out.blocksFire=true;return out;}
        }else e->session.reset();
        e->original=*view;e->weapon=s.weapon;e->neutral.reset();e->lastInput=0;e->motionDirty=true;
    }
    const auto right=hands.Current(InteractionHand::Right);
    const bool gun=right&&right->token.owner==s.input.owner&&right->token.item==s.weapon&&
        right->token.kind==HandClaimKind::GunHold&&!right->token.prerequisiteClaim&&right->deadlineNs>now;
    const State::Packet* source=nullptr;
    for(const auto& p:state.packets)if(p.owner==s.owner&&p.weapon==s.weapon&&SamePacket(p.input,s.raw.inputEvidence)){source=&p;break;}
    const bool raw=source&&source->native&&Window(source->input,now)&&source->input.observedNs>=source->native->observedNs&&
        FeedEquipmentViewFresh(*source->native,now)&&FeedEquipmentSameItem(*source->native,*view)&&
        source->native->counts==view->counts&&hands.HasRecordedEvidence(source->input,InteractionHand::Left);
    if(!gun||!raw){Suspend(s.input,hands);out.blocksFire=e->Blocked();return out;}
    auto sourceInput=source->input;sourceInput.nowNs=now;
    const auto local=feed_interaction_detail::Multiply(s.raw.rawLeftWristWorldMeters,Inverse(s.raw.weaponWorldMeters));
    const auto left=hands.Current(InteractionHand::Left);
    bool available=!left||(e->claim&&left->token==*e->claim);
    if(e->claim&&(!left||left->token!=*e->claim))e->claim.reset();
    // A physical cycle starts only on a new raw grip after a real neutral
    // cover contact. The saved neutral contains its own geometry/count packet.
    const bool idle=!e->session||e->session->State().status==FeedEquipmentStatus::Complete;
    if(idle){
        const auto& m=e->motions.operations[unsigned(FeedPhysicalOperation::OpenCover)];
        const auto contact=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        const auto& policy=e->profile.contacts[unsigned(FeedPart::Cover)];
        const bool touch=Distance(local,contact)<=policy.radiusMeters&&Angle(local,contact)<=policy.maxAngleRadians;
        if(!source->grip&&sourceInput.released[0]&&available&&touch&&sourceInput.sequence>e->lastInput){
            FeedEquipmentSessionSample neutral;neutral.interaction.contact.weaponFromHandContact=local;
            neutral.interaction.contact.observedNs=sourceInput.observedNs;neutral.interaction.contact.inputSequence=sourceInput.sequence;
            neutral.nowNs=now;neutral.ammunition=state.Ammo(*e,*source->native);neutral.interaction.focused=true;neutral.interaction.contact.tracked=true;
            e->neutral=neutral;
        }
        if(source->grip&&touch&&available&&e->neutral&&sourceInput.sequence>e->neutral->interaction.contact.inputSequence&&
           view->counts.loaded<view->counts.capacity&&view->counts.reserve>0&&
           now-e->neutral->interaction.contact.observedNs<e->profile.maxAgeNs){
            const auto lifetime=Lifetime();if(!lifetime){out.blocksFire=true;return out;}
            e->lifetime=lifetime;e->refill={FeedRefillState::Required,lifetime,0};e->refused=false;
            try{e->session=std::make_unique<FeedEquipmentSession>(e->profile,e->motions,FeedEquipmentOwner(s.owner),e->mechanism,e->supply,lifetime,
                e->original.binding.context.resource);}catch(...){out.blocksFire=true;return out;}
            state.PublishGate(*e);
            auto neutral=*e->neutral;neutral.ammunition.lifetime=lifetime;neutral.snapshot=state.Snapshot(*e,neutral.ammunition,sourceInput);
            neutral.snapshot.inputSequence=neutral.interaction.contact.inputSequence;
            neutral.binding=state.Binding(*e);neutral.authority=state.Authority(*e,neutral.ammunition,FeedPhysicalOperation::OpenCover);
            neutral.interaction.contact.owner=neutral.snapshot.owner;neutral.interaction.contact.mechanism=e->mechanism;
            neutral.interaction.contact.snapshotSequence=neutral.snapshot.sequence;neutral.interaction.contact.handAvailable=true;
            neutral.interaction.hand={1,sourceInput.owner.equipGeneration};neutral.interaction.trackingGeneration=sourceInput.owner.space;neutral.held=true;
            (void)e->session->Update(neutral);e->neutral.reset();
        }
    }
    e->lastInput=(std::max)(e->lastInput,sourceInput.sequence);
    if(e->session){
        auto ammunition=state.Ammo(*e,*source->native);
        FeedEquipmentSessionSample sample;sample.ammunition=ammunition;sample.snapshot=state.Snapshot(*e,ammunition,sourceInput);
        sample.nowNs=now;sample.binding=state.Binding(*e);
        const auto prior=e->session->State();
        bool recoveryEstablished=false;
        const auto query=EvaluateFeedMechanism(e->profile,sample.snapshot,e->supply,now);
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
                const auto motion=BuildBc2M60FeedMotionFromRetainedPose(bc2_feed_profile_detail::M60Geometry,op,Bc2M60FeedInteractionPolicy(),e->pose);
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
        sample.held=available&&(source->grip||sourceInput.released[0])&&!e->refused&&!e->gateFailed&&!e->motionDirty;
        if(prior.session.reload.pending&&prior.ammunition.lastReceipt&&!prior.ammunition.pending&&
           prior.ammunition.lastReceipt->command.request==*prior.session.reload.pending){
            auto ack=prior.ammunition.lastReceipt->acknowledgement;
            ack.snapshotSequence=sample.snapshot.sequence;ack.observedNs=sample.snapshot.observedNs;
            sample.acknowledgement=ack; // Fresh exact native-count corroboration, original receipt unchanged.
        }
        if(source->grip&&sample.held&&query.candidate&&!prior.ammunition.pending&&!prior.session.reload.pending){
            const auto contact=EvaluateFeedContact(e->profile,sample.snapshot,e->supply,*query.candidate,sample.interaction.contact,now);
            // AcquireFrom/RenewFrom preserve the exact recorded raw packet.
            // Native authority has its own independently checked deadline.
            const HandContactProof proof{{ProfileId+unsigned(query.candidate->part),e->lifetime},sourceInput.sequence,
                sourceInput.deadlineNs,true};
            if(e->claim){const auto renewed=hands.RenewFrom(s.input,sourceInput,*e->claim,proof);out.handReason=renewed.reason;
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
        state.PoseTarget(*e,result.session.interaction,s.raw.weaponWorldMeters);
        if(!result.session.interaction.target&&!result.session.interaction.completion&&
           (result.session.interaction.status==FeedInteractionStatus::Interrupted||result.session.interaction.status==FeedInteractionStatus::OffPath||
            result.session.interaction.status==FeedInteractionStatus::MotionJump||result.session.interaction.status==FeedInteractionStatus::Expired))e->motionDirty=true;
        if(result.dispatch){FeedEquipmentSubmission submission;submission.command=*result.dispatch;submission.source=*source->native;
            submission.geometry={e->profile,e->motions.operations[unsigned(result.dispatch->authority.operation)],s.rigFingerprint,
                sourceInput.sequence,sourceInput.observedNs,(std::min)(sourceInput.deadlineNs,source->native->deadlineNs),true};
            e->pending=submission;
            if(state.api.submitObserved){const auto submitted=state.api.submitObserved(state.api.context,submission,false);
                e->refused=submitted==FeedEquipmentSubmitResult::RetentionBusy||submitted==FeedEquipmentSubmitResult::Invalid;
                out.submitRefused=submitted!=FeedEquipmentSubmitResult::Submitted;
            }else {e->refused=!state.api.submit(state.api.context,submission);out.submitRefused=e->refused;}
        }
        if(e->claim&&(!source->grip||result.dispatch||!sample.held)){
            (void)hands.Release(s.input,*e->claim);e->claim.reset();}
        }else out.status=e->session->State().status;
        out.pending=e->pending.has_value();out.ownsHand=e->claim.has_value();
    }
    state.PublishGate(*e);out.blocksFire=e->Blocked();
    if(const auto pose=ComposeBc2M60FeedPresentation(e->supply,e->pose))try{
        auto p=std::make_shared<Bc2PhysicalFeedPresentation>();p->owner=s.owner;p->rig=s.raw.rig;p->input=sourceInput;p->weapon=s.weapon;
        p->supply=e->supply;p->lifetime=e->lifetime;p->revision=++e->revision;p->rigFingerprint=s.rigFingerprint;
        p->weaponFromPouch=pose->weaponFromPouch;p->weaponFromChain=pose->weaponFromChain;p->coverRadians=e->coverRadians;
        p->carriedPartsMask=e->carriedPartsMask;p->pouchCarryWeaponWorldMeters=e->pouchBasis;p->chainCarryWeaponWorldMeters=e->chainBasis;
        p->originalWeaponWorldMeters=s.raw.weaponWorldMeters;p->operation=e->activeOperation;
        if(e->claim){const auto claim=hands.Current(InteractionHand::Left);
            if(claim&&claim->token==*e->claim&&claim->inputSequence==sourceInput.sequence&&claim->deadlineNs>now){
                p->mechanismClaim=claim;p->mechanismWristWorldMeters=s.raw.rawLeftWristWorldMeters;
                p->targetKind=e->motions.operations[unsigned(e->activeOperation)].kind==FeedMotionKind::FreePlacement?
                    FeedInteractionTargetKind::Object:FeedInteractionTargetKind::Contact;}}
        math::Matrix4 identity{};for(unsigned i=0;i<4;++i)identity.values[i][i]=1;
        p->weaponFromCoverHinge=feed_interaction_detail::Multiply(Along(identity,FeedMotionKind::Hinge,e->coverRadians),bc2_feed_profile_detail::M60Geometry.weaponFromHinge);
        p->blocksFire=out.blocksFire;out.presentation=p;publication.store(std::move(p),std::memory_order_release);
    }catch(...){}
    return out;
}
}
