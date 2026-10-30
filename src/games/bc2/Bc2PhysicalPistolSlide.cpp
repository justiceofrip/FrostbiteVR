#include "Bc2PhysicalPistolSlide.h"
#include <limits>

namespace fvr::bc2 {
namespace {
using namespace interaction;
constexpr std::int64_t EvidenceLifetime=100000000;
bool SamePacket(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
bool Window(const HandInteractionSample& s,std::int64_t now)noexcept {
    return weapon_cycle_detail::Owner(s.owner)&&s.sequence&&s.observedNs>0&&s.deadlineNs>s.observedNs&&
        s.deadlineNs-s.observedNs<=EvidenceLifetime&&now>=s.observedNs&&now<s.deadlineNs&&
        s.focused&&s.tracked[0]&&s.tracked[1]&&!s.released[1];
}
bool SameItem(const AmmoResourceBinding& a,const AmmoResourceBinding& b)noexcept {
    return a.context.resource==b.context.resource&&a.owner.player==b.owner.player&&a.owner.soldier==b.owner.soldier&&
        a.owner.weak==b.owner.weak&&a.owner.actorGeneration==b.owner.actorGeneration&&a.owner.weapon==b.owner.weapon&&
        a.inventory==b.inventory&&a.switching==b.switching&&a.data==b.data&&a.persistence==b.persistence;
}
bool Current(const ReloadStateOwner& owner,const WeaponEquipmentIdentity& equipment,const AmmoResourceBinding& binding)noexcept {
    return binding.owner==owner&&equipment.weapon==owner.weapon&&equipment.data==binding.data&&
        equipment.persistence==binding.persistence&&equipment.Asset()==bc2_pistol_slide_profile_detail::Mp443Geometry.selection.asset&&
        AmmoResourceBindingFresh(binding,owner,binding.observedNs);
}
bool Selected(const Bc2PhysicalPistolSlideSample& s)noexcept {
    const auto& g=bc2_pistol_slide_profile_detail::Mp443Geometry.selection;
    if(s.asset!=g.asset||s.configurationPath!=g.configurationPath||s.meshPath!=g.meshPath||s.rigFingerprint!=g.rigFingerprint||
       !s.selectedRigVerified||!s.selected||!s.equipment||!s.binding||!Current(s.owner,*s.equipment,*s.binding)||
       !AmmoResourceBindingFresh(*s.binding,s.owner,s.input.nowNs))return false;
    const auto& m=*s.selected;
    if(m.owner!=s.owner||!m.sequence||!m.configurationPathVerified||!m.configurationPathPointer||
       !ReloadDescriptorText(m.configurationPath,g.configurationPath)||!ReloadDescriptorText(m.weaponName,g.asset)||
       m.weaponData!=s.binding->data||m.inventory!=s.binding->inventory||m.observedNs<=0||
       s.input.nowNs<m.observedNs||s.input.nowNs>=m.deadlineNs||m.stateCount!=1||!m.soleConfiguredArray)return false;
    const auto& state=m.states[0];
    if(state.array!=m.soleConfiguredArray||!state.count||state.count>state.meshes.size())return false;
    unsigned matches=0;for(unsigned i=0;i<state.count;++i)
        if(ReloadDescriptorText(state.meshes[i].assetPath,g.meshPath)&&state.meshes[i].address>=0x10000)++matches;
    return matches==1;
}
bool NativeHand(const Bc2PhysicalPistolSlideSample& s)noexcept {
    return s.input.owner.actor==((std::uint64_t(s.owner.weak)<<32)|s.owner.soldier)&&
        s.input.owner.actorGeneration==s.owner.actorGeneration&&s.input.owner.equipGeneration&&
        s.input.owner.space==s.owner.space&&s.weapon.id==s.owner.weapon&&s.weapon.generation==s.input.owner.equipGeneration;
}
math::Matrix4 Identity()noexcept {math::Matrix4 p{};for(unsigned i=0;i<4;++i)p.values[i][i]=1;return p;}
}
struct Bc2PhysicalPistolSlide::State {
    struct Equipment {
        AmmoResourceBinding original{};
        ReloadHoldIdentity identity{};
        PistolAfterFeedPolicy policy;
        HandInteractionKey mechanism{};
        std::uint64_t lastEvent=0,revision=0,migration=0;
        bool gap=false,nativeBlocked=true,nativePending=false,releaseRefused=false;
        bool Blocked()const noexcept{return gap||nativeBlocked||policy.BlocksFire();}
    };
    struct Packet {
        ReloadStateOwner owner{};HandInteractionSample input{};HandInteractionKey weapon{};
        std::optional<AmmoResourceBinding> binding;std::optional<WeaponEquipmentIdentity> equipment;
        std::optional<HandClaim> gun,beforeMechanism;HandInteractionResult lifecycle{};bool grip=false;
    };
    Bc2PhysicalPistolSlideApi api{};
    std::array<std::unique_ptr<Equipment>,32> equipment{};
    std::array<Packet,32> packets{};std::size_t nextPacket=0;
    std::array<ReloadHoldIdentity,32> retiredActors{};std::size_t nextRetired=0;
    std::uint64_t lastEvent=0,lastRetirement=0;
    bool eventGap=false;
    Equipment* Find(const AmmoResourceIdentity& resource)noexcept {
        for(auto& e:equipment)if(e&&e->original.context.resource==resource)return e.get();return nullptr;
    }
    bool Retired(const ReloadHoldIdentity& identity)const noexcept {
        for(const auto& old:retiredActors)if(old.owner.actorGeneration==identity.owner.actorGeneration&&
            AmmoResourceSameNativeActor(old,identity))return true;return false;
    }
    void Events(const HandInteractionSample& safety,HandInteraction& hands)noexcept {
        if(!api.event)return;
        for(unsigned count=0;count<128;++count){
            const auto event=api.event(api.context,lastEvent);if(!event||!event->event||event->event<=lastEvent)break;
            if(event->event!=lastEvent+1){eventGap=true;for(auto& e:equipment)if(e)e->gap=true;}
            lastEvent=event->event;const auto& v=*event;
            if(Retired(v.identity)||v.kind==PistolAfterFeedEventKind::ActorRetired)continue;
            auto* e=Find(v.command.context.resource);
            if(!e){for(auto& slot:equipment)if(!slot){try{
                slot=std::make_unique<Equipment>();e=slot.get();e->original=v.request.binding;e->identity=v.identity;
                e->mechanism={bc2_pistol_slide_profile_detail::Mp443ModPolicy.id,v.command.context.resource.weaponGeneration};
                const auto profile=BuildBc2Mp443ModSlideProfile(bc2_pistol_slide_profile_detail::Mp443Geometry.selection);
                e->gap=eventGap||v.kind!=PistolAfterFeedEventKind::Admitted||!profile||
                    !PistolAfterFeedContextMatches(v.command.context,v.identity)||v.request.binding.context!=v.command.context||
                    !e->policy.Bind({v.admission,v.initialPolicyReadyVerified},v.admission.observedNs)||
                    !e->policy.ConfigureMotion(*profile,e->mechanism);
            }catch(...){eventGap=true;}break;}}
            if(!e)continue;
            e->lastEvent=v.event;
            if(!SameItem(e->original,v.request.binding)||v.request.binding.owner!=v.identity.owner||
               !PistolAfterFeedContextMatches(v.command.context,v.identity)){e->gap=true;continue;}
            if(e->gap)continue;
            // Only original event clocks reconstruct historical transactions.
            // Polling late never creates a fresh command, contact or witness.
            e->policy.Suspend(safety,hands);
            if(v.kind==PistolAfterFeedEventKind::Admitted&&e->policy.Context()!=v.command.context){
                const PistolAfterFeedMigration migration{e->policy.Context(),v.command.context,++e->migration,
                    v.admission.observedNs,v.admission.deadlineNs,true};
                if(!e->policy.Migrate(migration,v.admission.observedNs)){e->gap=true;continue;}
            }
            if(v.kind==PistolAfterFeedEventKind::Admitted){
                if(!e->policy.BeginTransaction(v.request.intent,v.command,v.command.requestedNs))e->gap=true;
                e->nativeBlocked=true;e->nativePending=true;e->releaseRefused=false;
            }else if(v.kind==PistolAfterFeedEventKind::Completed){
                if(!v.outcome||!PistolAfterFeedOutcomeExact(*v.outcome,v.request,v.command,v.identity)||
                   !e->policy.CompleteTransaction(v.request.intent,*v.outcome->receipt,v.outcome->resolvedNs))e->gap=true;
                else e->nativePending=false;
            }else if(v.kind==PistolAfterFeedEventKind::RejectedBeforeNative){
                if(!v.outcome||v.outcome->request!=v.request||v.outcome->state!=AmmoResourceRequestState::Rejected||
                   v.outcome->nativeDispatched||v.outcome->receipt||!e->policy.Pending()||
                   v.outcome->resolvedNs<=0||v.outcome->resolvedNs>INT64_MAX-EvidenceLifetime)e->gap=true;
                else {
                    // Non-dispatch is an immutable terminal fact. Its bounded
                    // proof is evaluated at the recorded resolution, never now.
                    const auto at=v.outcome->resolvedNs;
                    if(!e->policy.RejectUndispatched({*e->policy.Pending(),at,at+EvidenceLifetime,true},at))e->gap=true;
                    else e->nativePending=false;
                }
            }
        }
    }
    void Retirements(const HandInteractionSample& safety,HandInteraction& hands)noexcept {
        if(!api.retirement)return;
        for(unsigned count=0;count<32;++count){const auto r=api.retirement(api.context,lastRetirement);
            if(!r||r->event<=lastRetirement)break;lastRetirement=r->event;
            if(!r->event||!r->callbackRevision||!r->lifetimeEndEpoch||r->observedNs<=0||
               r->observedNs>INT64_MAX-EvidenceLifetime||r->retiredItems>r->resources.size()||
               !AmmoResourceActorReplacement(r->previous,r->replacement))continue;
            retiredActors[nextRetired++%retiredActors.size()]=r->previous;
            for(auto& slot:equipment)if(slot&&AmmoResourceSameNativeActor(slot->identity,r->previous)&&
                slot->original.context.resource.actorGeneration==r->previous.owner.actorGeneration){
                bool listed=false;for(std::size_t i=0;i<r->retiredItems;++i)listed|=r->resources[i]==slot->original.context.resource;
                if(!listed)continue;
                slot->policy.Suspend(safety,hands);
                const PistolAfterFeedRetirement proof{slot->original.context.resource,r->event,r->observedNs,
                    r->observedNs+EvidenceLifetime,true,true};
                // The backend's drained actor transition retires even a slot
                // whose earlier event history was incomplete. No ammo rollback.
                (void)slot->policy.Retire(proof,r->observedNs);slot.reset();
            }
            for(auto& p:packets)if(p.owner.soldier==r->previous.owner.soldier&&p.owner.weak==r->previous.owner.weak&&
                p.owner.actorGeneration==r->previous.owner.actorGeneration)p={};
        }
    }
};
Bc2PhysicalPistolSlide::Bc2PhysicalPistolSlide(Bc2PhysicalPistolSlideApi api)noexcept {
    try{state_=std::make_unique<State>();state_->api=api;}catch(...){}
}
Bc2PhysicalPistolSlide::~Bc2PhysicalPistolSlide()=default;
void Bc2PhysicalPistolSlide::Suspend(const HandInteractionSample& input,HandInteraction& hands)noexcept {
    if(state_)for(auto& e:state_->equipment)if(e)e->policy.Suspend(input,hands);
}
bool Bc2PhysicalPistolSlide::BlocksFire(const ReloadStateOwner& owner,const WeaponEquipmentIdentity& equipment,
    const std::optional<AmmoResourceBinding>& binding)const noexcept {
    if(!state_||!binding||!Current(owner,equipment,*binding))return false;
    for(const auto& e:state_->equipment)if(e&&SameItem(e->original,*binding))return e->Blocked();return false;
}
Bc2PhysicalPistolSlideResult Bc2PhysicalPistolSlide::Tick(const Bc2PhysicalPistolSlideSample& s,
    HandInteraction& hands,std::uint64_t& intent)noexcept {
    Bc2PhysicalPistolSlideResult out;if(!state_)return out;auto& state=*state_;const auto now=s.input.nowNs;
    state.Events(s.input,hands);state.Retirements(s.input,hands);
    State::Equipment* e=nullptr;
    if(s.equipment&&s.binding&&Current(s.owner,*s.equipment,*s.binding)){
        e=state.Find(s.binding->context.resource);if(e&&!SameItem(e->original,*s.binding))e=nullptr;
    }
    const auto finish=[&](){if(e){out.evidenceGap=out.evidenceGap||e->gap;out.blocksFire=e->Blocked()||out.evidenceGap;
        out.pending=e->policy.Pending().has_value()||e->nativePending;out.blocksEquipment=out.pending||out.ownsHand;
        out.phase=e->policy.Phase();out.releaseRefused=e->releaseRefused;}return out;};
    if(!state.api.event||!state.api.read||!state.api.release){Suspend(s.input,hands);return finish();}
    // Capture the original Gameplay packet once, before consuming renderer raw
    // geometry. GunHold is the existing arbiter claim, never a synthetic root.
    bool recorded=false;for(const auto& p:state.packets)if(p.owner==s.owner&&p.input.sequence==s.input.sequence)recorded=true;
    if(!recorded&&s.input.sequence){state.packets[state.nextPacket]={s.owner,s.input,s.weapon,s.binding,s.equipment,
        hands.Current(InteractionHand::Right),s.previousLeft,s.lifecycle,s.grip};state.nextPacket=(state.nextPacket+1)%state.packets.size();}
    const auto native=s.binding?state.api.read(state.api.context,s.binding->context.resource,now):std::nullopt;
    if(e&&native&&native->resource==e->original.context.resource){e->nativeBlocked=native->blocksFire;e->nativePending=native->pending;}
    if(!Selected(s)){Suspend(s.input,hands);if(!e&&native)out.blocksFire=native->blocksFire;return finish();}
    out.selected=true;
    if(!e){if(native){out.blocksFire=native->blocksFire;out.pending=native->pending;out.blocksEquipment=out.pending;
        out.evidenceGap=state.eventGap||native->blocksFire;}return out;}
    // A command remains in its original context through stow. Reconcile its
    // original terminal event before migrating the surviving stable resource.
    if(e->policy.Pending()||e->nativePending){e->policy.Suspend(s.input,hands);return finish();}
    // Context migration comes only from the exact freshly verified current
    // inventory binding. Stable weapon debt survives equip and space epochs.
    if(e->policy.Context()!=s.binding->context){e->policy.Suspend(s.input,hands);
        const PistolAfterFeedMigration migration{e->policy.Context(),s.binding->context,++e->migration,
            s.binding->observedNs,s.binding->deadlineNs,true};
        if(!e->policy.Migrate(migration,now)){out.evidenceGap=true;return finish();}}
    if(e->gap){e->policy.Suspend(s.input,hands);return finish();}
    // Native readiness can clear a tactical policy without any controller
    // gesture. Empty policy consumes it only after a real released full stroke.
    if(native&&native->ready&&native->ready->currentIdentity.owner==s.owner&&
       native->ready->semantic.context==s.binding->context&&
       (!e->policy.Owed()||e->policy.Stroke())){
        bool later=true;if(e->policy.Stroke())for(const auto& row:native->ready->semantic.updates)
            later=later&&row.beganNs>=e->policy.Stroke()->observedNs;
        if(later)(void)e->policy.ObserveReady(native->ready->semantic,now);
    }
    if(e->nativeBlocked||e->releaseRefused)if(const auto clearance=e->policy.Clearance(now)){
        e->releaseRefused=!state.api.release(state.api.context,{*clearance});
        if(!e->releaseRefused)e->nativeBlocked=false;
    }
    for(auto& old:state.equipment)if(old&&old.get()!=e)old->policy.Suspend(s.input,hands);
    if(s.cancel||!Window(s.input,now)||!NativeHand(s)||!s.raw.valid||s.raw.owner!=s.owner||
       s.raw.rig.soldier!=s.owner.soldier||s.raw.rig.weak!=s.owner.weak||!s.raw.rig.count||
       s.raw.rigFingerprint!=s.rigFingerprint||s.frameSequence!=s.raw.inputEvidence.sequence){
        e->policy.Suspend(s.input,hands);return finish();}
    const State::Packet* source=nullptr;
    for(const auto& p:state.packets)if(p.owner==s.owner&&p.weapon==s.weapon&&SamePacket(p.input,s.raw.inputEvidence))source=&p;
    const auto gun=hands.Current(InteractionHand::Right);
    if(!source||!source->binding||!source->equipment||!source->gun||!gun||
       source->gun->token!=gun->token||gun->token.kind!=HandClaimKind::GunHold||gun->token.hand!=InteractionHand::Right||
       gun->token.owner!=s.input.owner||gun->token.item!=s.weapon||gun->token.prerequisiteClaim||gun->deadlineNs<=now||
       source->gun->inputSequence!=source->input.sequence||source->gun->deadlineNs<=now||
       !Current(source->owner,*source->equipment,*source->binding)||!SameItem(*source->binding,*s.binding)||
       source->binding->context!=e->policy.Context()||source->binding->observedNs>source->input.nowNs||
       !AmmoResourceBindingFresh(*source->binding,source->owner,now)||!Window(source->input,now)){
        e->policy.Suspend(s.input,hands);return finish();}
    Bc2PistolSlideSource raw;raw.input=source->input;raw.input.nowNs=now;raw.rig=s.raw.rig;raw.weapon=s.weapon;
    raw.mechanism=e->mechanism;raw.gunClaim=*source->gun;raw.rawSourceWorldMeters=s.raw.rawLeftWristWorldMeters;
    raw.sourceFromWristMeters=Identity();raw.originalWeaponWorldMeters=s.raw.weaponWorldMeters;
    const auto local=Bc2PistolSlideRawWristInWeapon(raw);if(!local){e->policy.Suspend(s.input,hands);return finish();}
    const auto left=hands.Current(InteractionHand::Left);
    if(left&&(left->token.kind!=HandClaimKind::Mechanism||left->token.contact!=e->mechanism||
        left->token.item!=s.weapon||left->token.prerequisiteClaim!=gun->token.id)){
        e->policy.Suspend(s.input,hands);return finish();}
    PistolAfterFeedMotionSample motion;motion.source=raw.input;motion.currentSafety=s.input;
    motion.sourceObservation=PistolAfterFeedHandObservation{source->input,source->beforeMechanism,source->lifecycle,source->gun};
    motion.currentObservation=PistolAfterFeedHandObservation{s.input,s.previousLeft,s.lifecycle,gun};
    motion.context=e->policy.Context();motion.binding={source->binding->context,source->input.owner,source->weapon,
        source->input.sequence,source->binding->observedNs,source->binding->deadlineNs,true};
    motion.feedRequest=e->policy.Feed()?e->policy.Feed()->intent.id:0;motion.rawContact=*local;motion.grip=source->grip;
    motion.gun=gun->token;motion.gunContact={gun->token.contact,s.input.sequence,s.input.deadlineNs,true};
    motion.contact={e->mechanism,source->input.sequence,source->input.deadlineNs,true};
    if(intent!=UINT64_MAX)motion.acquireIntent=++intent;
    const auto result=e->policy.UpdateMotion(motion,hands);out.failure=result.failure;out.ownsHand=result.ownsMechanism;
    if(result.target){const auto mechanism=hands.Current(InteractionHand::Left),currentGun=hands.Current(InteractionHand::Right);
        if(mechanism&&currentGun&&currentGun->token==raw.gunClaim.token&&
           mechanism->token==result.target->mechanism&&mechanism->inputSequence==source->input.sequence){
            raw.mechanismClaim=*mechanism;
            if(Bc2PistolSlideSourceFresh(raw,raw.input,raw.gunClaim,raw.mechanismClaim,raw.rig,now))try{
                auto p=std::make_shared<Bc2PhysicalPistolSlidePresentation>();p->owner=s.owner;p->source=raw;
                p->currentSafety=s.input;p->currentGun=*currentGun;p->currentMechanism=*mechanism;
                p->selection=bc2_pistol_slide_profile_detail::Mp443Geometry.selection;p->target=*result.target;p->revision=++e->revision;
                p->signedDisplacementMeters=bc2_pistol_slide_profile_detail::Mp443ModPolicy.sourceDisplacementMeters+result.target->travel;
                p->blocksFire=e->Blocked();out.presentation=std::move(p);
            }catch(...){}
        }
    }
    return finish();
}
}
