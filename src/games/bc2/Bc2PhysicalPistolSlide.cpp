#include "Bc2PhysicalPistolSlide.h"
#include <limits>
#include <atomic>
#include <ostream>

namespace fvr::bc2 {
namespace {
using namespace interaction;
constexpr std::int64_t EvidenceLifetime=100000000;
enum class SlideExit:unsigned {None,Updated,ApiUnavailable,SelectionUnverified,OwnerUnavailable,Pending,
    MigrationUnverified,EvidenceGap,UnsafeInput,SourceUnverified,GeometryUnverified,ClaimConflict,Blocked,MotionFailure};
const char* SlideExitName(SlideExit reason)noexcept {
    switch(reason){
    case SlideExit::None:return "none";case SlideExit::Updated:return "motion_updated";
    case SlideExit::ApiUnavailable:return "api_unavailable";case SlideExit::SelectionUnverified:return "selection_unverified";
    case SlideExit::OwnerUnavailable:return "owner_unavailable";case SlideExit::Pending:return "pending";
    case SlideExit::MigrationUnverified:return "migration_unverified";case SlideExit::EvidenceGap:return "evidence_gap";
    case SlideExit::UnsafeInput:return "unsafe_input";case SlideExit::SourceUnverified:return "source_unverified";
    case SlideExit::GeometryUnverified:return "geometry_unverified";case SlideExit::ClaimConflict:return "claim_conflict";
    case SlideExit::Blocked:return "policy_blocked";case SlideExit::MotionFailure:return "motion_failure";}
    return "unknown";
}
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
    // Gameplay is the sole writer. Report never reads mutable policy/equipment;
    // relaxed atomic observations are not a coherent native/chamber snapshot.
    struct Diagnostics {
        std::atomic<std::uint64_t> ticks=0,transitions=0,blocked=0,debt=0,pending=0,claims=0,gaps=0,suspends=0;
        std::atomic<std::uint64_t> releaseAttempts=0,releaseRefusals=0,lastEvent=0;
        std::atomic<SlideExit> reason=SlideExit::None,lastIssue=SlideExit::None;
        std::atomic<unsigned> flags=0,status=0,lastFailure=0,asset=0,eventKind=0,lastIssueFlags=0,lastIssueStatus=0;
        std::atomic<bool> selectedBefore=false;
        std::atomic<int> loaded=-1,reserve=-1,capacity=-1;
    } diagnostics;
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
            diagnostics.lastEvent.store(v.event,std::memory_order_relaxed);
            diagnostics.eventKind.store(unsigned(v.kind),std::memory_order_relaxed);
            if(v.kind==PistolAfterFeedEventKind::Admitted){
                diagnostics.loaded.store(v.admission.counts.loaded,std::memory_order_relaxed);
                diagnostics.reserve.store(v.admission.counts.reserve,std::memory_order_relaxed);
                diagnostics.capacity.store(v.admission.counts.capacity,std::memory_order_relaxed);}
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
    if(state_){state_->diagnostics.suspends.fetch_add(1,std::memory_order_relaxed);
        for(auto& e:state_->equipment)if(e)e->policy.Suspend(input,hands);}
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
    const auto finish=[&](SlideExit reason){if(e){out.evidenceGap=out.evidenceGap||e->gap;out.blocksFire=e->Blocked()||out.evidenceGap;
        out.pending=e->policy.Pending().has_value()||e->nativePending;out.blocksEquipment=out.pending||out.ownsHand;
        out.phase=e->policy.Phase();out.releaseRefused=e->releaseRefused;}
        auto& d=state.diagnostics;
        const unsigned flags=unsigned(out.selected)|(unsigned(out.blocksFire)<<1)|(unsigned(out.pending)<<2)|
            (unsigned(out.ownsHand)<<3)|(unsigned(out.evidenceGap||state.eventGap)<<4)|
            (unsigned(out.releaseRefused)<<5)|(unsigned(e&&e->policy.Owed())<<6);
        const unsigned status=unsigned(out.phase)|(unsigned(out.failure)<<8);
        bool changed=d.reason.exchange(reason,std::memory_order_relaxed)!=reason;
        changed=(d.flags.exchange(flags,std::memory_order_relaxed)!=flags)||changed;
        changed=(d.status.exchange(status,std::memory_order_relaxed)!=status)||changed;
        if(changed)d.transitions.fetch_add(1,std::memory_order_relaxed);
        if(out.failure!=PistolSlideFailure::None)d.lastFailure.store(unsigned(out.failure),std::memory_order_relaxed);
        const unsigned asset=s.asset.empty()?0:s.asset==bc2_pistol_slide_profile_detail::Mp443Geometry.selection.asset?1:2;
        d.asset.store(asset,std::memory_order_relaxed);
        auto issue=reason;
        if(reason==SlideExit::Updated){
            if(out.failure!=PistolSlideFailure::None)issue=SlideExit::MotionFailure;
            else if(flags&2)issue=SlideExit::Blocked;
        }
        // Preserve the last MP443 refusal/debt across stow and other weapons,
        // including a refused binding/selection for a requested MP443 asset.
        if(asset==1&&issue!=SlideExit::Updated){d.lastIssue.store(issue,std::memory_order_relaxed);
            d.lastIssueFlags.store(flags,std::memory_order_relaxed);d.lastIssueStatus.store(status,std::memory_order_relaxed);}
        if(out.selected)d.selectedBefore.store(true,std::memory_order_relaxed);
        d.ticks.fetch_add(1,std::memory_order_relaxed);
        if(flags&2)d.blocked.fetch_add(1,std::memory_order_relaxed);
        if(flags&4)d.pending.fetch_add(1,std::memory_order_relaxed);
        if(flags&8)d.claims.fetch_add(1,std::memory_order_relaxed);
        if(flags&16)d.gaps.fetch_add(1,std::memory_order_relaxed);
        if(flags&64)d.debt.fetch_add(1,std::memory_order_relaxed);
        return out;
    };
    if(!state.api.event||!state.api.read||!state.api.release){Suspend(s.input,hands);return finish(SlideExit::ApiUnavailable);}
    // Capture the original Gameplay packet once, before consuming renderer raw
    // geometry. GunHold is the existing arbiter claim, never a synthetic root.
    bool recorded=false;for(const auto& p:state.packets)if(p.owner==s.owner&&p.input.sequence==s.input.sequence)recorded=true;
    if(!recorded&&s.input.sequence){state.packets[state.nextPacket]={s.owner,s.input,s.weapon,s.binding,s.equipment,
        hands.Current(InteractionHand::Right),s.previousLeft,s.lifecycle,s.grip};state.nextPacket=(state.nextPacket+1)%state.packets.size();}
    const auto native=s.binding?state.api.read(state.api.context,s.binding->context.resource,now):std::nullopt;
    if(e&&native&&native->resource==e->original.context.resource){e->nativeBlocked=native->blocksFire;e->nativePending=native->pending;}
    if(!Selected(s)){Suspend(s.input,hands);if(!e&&native)out.blocksFire=native->blocksFire;return finish(SlideExit::SelectionUnverified);}
    out.selected=true;
    if(!e){if(native){out.blocksFire=native->blocksFire;out.pending=native->pending;out.blocksEquipment=out.pending;
        out.evidenceGap=state.eventGap||native->blocksFire;}return finish(SlideExit::OwnerUnavailable);}
    // A command remains in its original context through stow. Reconcile its
    // original terminal event before migrating the surviving stable resource.
    if(e->policy.Pending()||e->nativePending){e->policy.Suspend(s.input,hands);return finish(SlideExit::Pending);}
    // Context migration comes only from the exact freshly verified current
    // inventory binding. Stable weapon debt survives equip and space epochs.
    if(e->policy.Context()!=s.binding->context){e->policy.Suspend(s.input,hands);
        const PistolAfterFeedMigration migration{e->policy.Context(),s.binding->context,++e->migration,
            s.binding->observedNs,s.binding->deadlineNs,true};
        if(!e->policy.Migrate(migration,now)){out.evidenceGap=true;return finish(SlideExit::MigrationUnverified);}}
    if(e->gap){e->policy.Suspend(s.input,hands);return finish(SlideExit::EvidenceGap);}
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
        state.diagnostics.releaseAttempts.fetch_add(1,std::memory_order_relaxed);
        e->releaseRefused=!state.api.release(state.api.context,{*clearance});
        if(e->releaseRefused)state.diagnostics.releaseRefusals.fetch_add(1,std::memory_order_relaxed);
        if(!e->releaseRefused)e->nativeBlocked=false;
    }
    for(auto& old:state.equipment)if(old&&old.get()!=e)old->policy.Suspend(s.input,hands);
    if(s.cancel||!Window(s.input,now)||!NativeHand(s)||!s.raw.valid||s.raw.owner!=s.owner||
       s.raw.rig.soldier!=s.owner.soldier||s.raw.rig.weak!=s.owner.weak||!s.raw.rig.count||
       s.raw.rigFingerprint!=s.rigFingerprint||s.frameSequence!=s.raw.inputEvidence.sequence){
        e->policy.Suspend(s.input,hands);return finish(SlideExit::UnsafeInput);}
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
        e->policy.Suspend(s.input,hands);return finish(SlideExit::SourceUnverified);}
    Bc2PistolSlideSource raw;raw.input=source->input;raw.input.nowNs=now;raw.rig=s.raw.rig;raw.weapon=s.weapon;
    raw.mechanism=e->mechanism;raw.gunClaim=*source->gun;raw.rawSourceWorldMeters=s.raw.rawLeftWristWorldMeters;
    raw.sourceFromWristMeters=Identity();raw.originalWeaponWorldMeters=s.raw.weaponWorldMeters;
    const auto local=Bc2PistolSlideRawWristInWeapon(raw);if(!local){e->policy.Suspend(s.input,hands);return finish(SlideExit::GeometryUnverified);}
    const auto left=hands.Current(InteractionHand::Left);
    if(left&&(left->token.kind!=HandClaimKind::Mechanism||left->token.contact!=e->mechanism||
        left->token.item!=s.weapon||left->token.prerequisiteClaim!=gun->token.id)){
        e->policy.Suspend(s.input,hands);return finish(SlideExit::ClaimConflict);}
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
    return finish(SlideExit::Updated);
}
void Bc2PhysicalPistolSlide::Report(std::ostream& out)const {
    if(!state_){out<<"{\"available\":false}";return;}
    const auto& d=state_->diagnostics;const auto flags=d.flags.load(std::memory_order_relaxed);
    const auto status=d.status.load(std::memory_order_relaxed),asset=d.asset.load(std::memory_order_relaxed);
    const auto issueStatus=d.lastIssueStatus.load(std::memory_order_relaxed);
    const auto name=bc2_pistol_slide_profile_detail::Mp443Geometry.selection.asset;
    out<<"{\"observations\":\"independent_atomics_not_native_or_chamber_proof\",\"ticks\":"<<d.ticks.load(std::memory_order_relaxed)
       <<",\"transitions\":"<<d.transitions.load(std::memory_order_relaxed)<<",\"blocked_ticks\":"<<d.blocked.load(std::memory_order_relaxed)
       <<",\"owed_ticks\":"<<d.debt.load(std::memory_order_relaxed)<<",\"pending_ticks\":"<<d.pending.load(std::memory_order_relaxed)
       <<",\"claim_ticks\":"<<d.claims.load(std::memory_order_relaxed)<<",\"evidence_gap_ticks\":"<<d.gaps.load(std::memory_order_relaxed)
       <<",\"suspends\":"<<d.suspends.load(std::memory_order_relaxed)<<",\"release_attempts\":"<<d.releaseAttempts.load(std::memory_order_relaxed)
       <<",\"release_refusals\":"<<d.releaseRefusals.load(std::memory_order_relaxed)
       <<",\"latest_tick\":{\"reason\":\""<<SlideExitName(d.reason.load(std::memory_order_relaxed))
       <<"\",\"asset_class\":\""<<(asset==1?name:asset==2?std::string_view("other"):std::string_view("none"))
       <<"\",\"last_selected_asset\":\""<<(d.selectedBefore.load(std::memory_order_relaxed)?name:std::string_view("none"))
       <<"\",\"selected\":"<<((flags&1)?"true":"false")<<",\"blocked\":"<<((flags&2)?"true":"false")
       <<",\"pending\":"<<((flags&4)?"true":"false")<<",\"claim\":"<<((flags&8)?"true":"false")
       <<",\"evidence_gap\":"<<((flags&16)?"true":"false")<<",\"release_refused\":"<<((flags&32)?"true":"false")
       <<",\"owed\":"<<((flags&64)?"true":"false")<<",\"phase_id\":"<<(status&255)<<",\"failure_id\":"<<(status>>8)
       <<",\"last_failure_id\":"<<d.lastFailure.load(std::memory_order_relaxed)
       <<"},\"last_supported_issue\":{\"reason\":\""<<SlideExitName(d.lastIssue.load(std::memory_order_relaxed))
       <<"\",\"flags_selected_blocked_pending_claim_gap_refused_owed\":"<<d.lastIssueFlags.load(std::memory_order_relaxed)
       <<",\"phase_id\":"<<(issueStatus&255)<<",\"failure_id\":"<<(issueStatus>>8)
       <<"},\"last_event\":{\"id\":"<<d.lastEvent.load(std::memory_order_relaxed)<<",\"kind_id\":"<<d.eventKind.load(std::memory_order_relaxed)
       <<"},\"last_admission_counts\":{\"loaded\":"<<d.loaded.load(std::memory_order_relaxed)
       <<",\"reserve\":"<<d.reserve.load(std::memory_order_relaxed)<<",\"capacity\":"<<d.capacity.load(std::memory_order_relaxed)<<"}}";
}
}
