#include "Bc2PhysicalFixedContainer.h"
#include <algorithm>
#include <limits>

namespace fvr::bc2 {
using namespace interaction;
namespace {
constexpr std::uint64_t SupplyNamespace=0x4649584544414d4dull;
FeedSupplyKey Supply(FixedContainerKind kind,std::uint64_t lifetime)noexcept {
    return {{SupplyNamespace,lifetime},kind==FixedContainerKind::EnBlocClip?
        FeedKey{SupplyNamespace+1,lifetime}:FeedKey{}};
}
bool Fresh(const HandInteractionSample& i,std::int64_t now)noexcept {
    return i.sequence&&i.focused&&i.tracked[0]&&i.tracked[1]&&i.observedNs>0&&i.observedNs<=now&&now<i.deadlineNs&&i.deadlineNs-i.observedNs<=100000000;
}
math::Matrix4 Contact(const FeedMotionProfile& m)noexcept {
    return m.kind==FeedMotionKind::FreePlacement?Multiply(m.placement.sourceFromGrip,m.placement.weaponFromSource):Multiply(m.motionFromContact,m.weaponFromMotion);
}
bool Near(const math::Matrix4& a,const math::Matrix4& b,const FeedContactBinding& c)noexcept {
    return feed_mechanism_detail::Pose(a)&&feed_mechanism_detail::Pose(b)&&
        feed_interaction_detail::Distance(a,b)<=c.radiusMeters&&feed_interaction_detail::Angle(a,b)<=c.maxAngleRadians;
}
}
struct Bc2PhysicalFixedContainer::Slot {
    AmmoResourceBinding binding{};HandInteractionOwner handOwner{};
    const FixedContainerEquipmentProfile* equipment=nullptr;
    std::unique_ptr<FixedContainerReload> policy;
    AmmunitionLedger* ledger=nullptr;
    FeedSupplyKey supply{};std::uint64_t lifetime=0,revision=0,lastRaw=0;
    std::optional<HandClaim> hand;
    bool carried=false,seedNeutral=false,submissionFailed=false,restart=false,geometryBound=false,recoveryPrepared=false;
    std::optional<math::Matrix4> carriedSupply,target;
    FixedContainerStep targetStep=FixedContainerStep::Open;
    std::array<math::Matrix4,16> parts{};
    std::optional<AmmoResourceRequest> request;
    std::optional<FixedContainerRequest> physicalRequest;
};
Bc2PhysicalFixedContainer::Bc2PhysicalFixedContainer(bool enabled,Bc2PhysicalFixedContainerApi api)noexcept:enabled_(enabled),api_(api){}
Bc2PhysicalFixedContainer::~Bc2PhysicalFixedContainer()=default;
void Bc2PhysicalFixedContainer::Cancel(const HandInteractionSample& input,HandInteraction& hands)noexcept {
    if(!active_)return;
    if(active_->hand){(void)hands.Release(input,active_->hand->token);active_->hand.reset();}
    if(active_->policy)active_->policy->Suspend();
    active_->recoveryPrepared=false;
    active_->target.reset();active_=nullptr;
}
Bc2PhysicalFixedContainerResult Bc2PhysicalFixedContainer::Tick(const Bc2PhysicalFixedContainerSample& s,
    HandInteraction& hands,std::uint64_t& intent)noexcept {
    ++ticks_;Bc2PhysicalFixedContainerResult out;
    const auto* e=KnownFixedContainerEquipment(s.equipment);
    if(!enabled_||!api_.read||!api_.withLedger||!api_.submit||!api_.outcome||!api_.publishGate||!api_.nextRequest||!e||
       e!=FindFixedContainerEquipment(s.configurationPath,s.mesh,s.rigFingerprint)||s.cancel||!Fresh(s.input,s.input.nowNs)){
        Cancel(s.input,hands);return out;
    }
    const auto view=api_.read(api_.context,s.nativeOwner,s.input.nowNs);
    if(!view||!AmmoResourceViewFresh(*view,s.input.nowNs)||!FixedContainerHandOwner(view->binding,s.input.owner)||
       s.weapon!=HandInteractionKey{view->binding.context.resource.weapon,s.input.owner.equipGeneration}||
       view->snapshot.counts.capacity!=e->interaction.capacity){Cancel(s.input,hands);return out;}
    const auto gun=hands.Current(InteractionHand::Right);
    if(!gun||gun->token.kind!=HandClaimKind::GunHold||gun->token.item!=s.weapon||gun->token.owner!=s.input.owner||
       gun->inputSequence!=s.input.sequence||gun->deadlineNs<=s.input.nowNs){Cancel(s.input,hands);return out;}
    Slot* slot=nullptr;
    for(auto& item:slots_)if(item&&item->binding.context.resource==view->binding.context.resource){slot=item.get();break;}
    if(slot&&slot->equipment!=e){Cancel(s.input,hands);return out;}
    if(!slot){
        for(auto& item:slots_)if(!item){
            try{item=std::make_unique<Slot>();}catch(...){return out;}
            slot=item.get();slot->equipment=e;slot->binding=view->binding;slot->handOwner=s.input.owner;
            if(nextLifetime_==UINT64_MAX){item.reset();return out;}
            slot->lifetime=++nextLifetime_;slot->supply=Supply(e->interaction.kind,slot->lifetime);
            for(unsigned n=0;n<e->parts.size()&&n<slot->parts.size();++n)slot->parts[n]=e->parts[n].weaponFromClosed;
            break;
        }
    }
    if(!slot||e->parts.size()>slot->parts.size())return out;
    if(active_&&active_!=slot)Cancel(s.input,hands);active_=slot;
    const auto terminal=slot->request&&api_.outcome?api_.outcome(api_.context,slot->request->intent.id,slot->request->intent.context):std::nullopt;
    const auto& raw=s.raw;
    const bool releasedRaw=s.releaseEvidence&&slot->hand&&s.input.released[0]&&
        s.releaseEvidence->before.token==slot->hand->token&&s.releaseEvidence->released.token==slot->hand->token&&
        s.releaseEvidence->released.reason==HandInteractionReason::Released&&s.releaseEvidence->before.deadlineNs>s.input.nowNs&&
        s.releaseEvidence->input.sequence==s.input.sequence&&s.releaseEvidence->input.owner==s.input.owner&&
        hands.HasRecordedEvidence(s.releaseEvidence->input,InteractionHand::Left)&&hands.HasRecordedEvidence(raw.input,InteractionHand::Right);
    const bool rawFresh=raw.valid&&raw.equipment==e&&raw.nativeOwner==s.nativeOwner&&raw.input.owner==s.input.owner&&
        raw.input.sequence<=s.input.sequence&&raw.input.observedNs<=s.input.observedNs&&Fresh(raw.input,s.input.nowNs)&&
        raw.rig.soldier==s.nativeOwner.soldier&&raw.rig.weak==s.nativeOwner.weak&&
        feed_mechanism_detail::Pose(raw.weaponFromWrist)&&feed_mechanism_detail::Pose(raw.weaponWorldMeters)&&
        (hands.HasRecordedEvidence(raw.input,InteractionHand::Left)||releasedRaw);
    if(slot->policy&&slot->policy->Step()==FixedContainerStep::Complete&&!slot->request&&!slot->restart&&rawFresh&&raw.gripPressed&&
       view->snapshot.counts.loaded<view->snapshot.counts.capacity&&view->snapshot.counts.reserve>0){
        if(nextLifetime_==UINT64_MAX)return out;
        slot->lifetime=++nextLifetime_;slot->supply=Supply(e->interaction.kind,slot->lifetime);slot->restart=true;
    }
    // Interlock native fire before any physical state can move. A failed
    // publication leaves this input unconsumed and the gesture unstarted.
    const bool priorBlocked=!slot->geometryBound||(slot->policy&&slot->policy->BlocksFire());
    const bool touching=rawFresh&&raw.grip&&(raw.gripPressed||slot->hand||slot->carried);
    if(slot->revision>UINT64_MAX-2)return out;
    const FixedContainerGate beforeGate{view->binding,s.input.owner,slot->lifetime,++slot->revision,s.input.sequence,
        s.input.observedNs,s.input.deadlineNs,priorBlocked||touching,!(priorBlocked||touching)};
    if(!api_.publishGate(api_.context,beforeGate)){out.blocksFire=priorBlocked;return out;}
    FixedContainerResult result;std::optional<AmmoResourceRequest> dispatch;
    struct Work {Bc2PhysicalFixedContainer* self;Slot* slot;const Bc2PhysicalFixedContainerSample* sample;
        const AmmoResourceView* view;const std::optional<AmmoResourceOutcome>* terminal;HandInteraction* hands;
        std::uint64_t* intent;bool rawFresh;FixedContainerResult* result;std::optional<AmmoResourceRequest>* dispatch;};
    Work work{this,slot,&s,&*view,&terminal,&hands,&intent,rawFresh,&result,&dispatch};
    const bool updated=api_.withLedger(api_.context,view->binding,s.input.owner,&work,[](void* opaque,AmmunitionLedger& ledger)noexcept {
        auto& w=*static_cast<Work*>(opaque);auto& self=*w.self;auto& z=*w.slot;const auto& x=*w.sample;const auto& v=*w.view;auto& h=*w.hands;
        const auto& profile=z.equipment->interaction;const auto now=x.input.nowNs;
        if(z.ledger&&z.ledger!=&ledger)return false;
        if(!z.policy){
            try{z.policy=std::make_unique<FixedContainerReload>(profile,v.binding.context,FeedKey{profile.id,z.lifetime},
                FeedKey{profile.id,profile.revision},z.supply,z.lifetime,ledger,true,x.input.owner);}catch(...){return false;}
            if(!z.policy->Bind(v.snapshot,now)){z.policy.reset();return false;}z.ledger=&ledger;
        }
        if(z.physicalRequest&&z.request){
            const auto& terminal=*w.terminal;
            if(terminal&&terminal->request==*z.request&&terminal->receipt&&terminal->state==AmmoResourceRequestState::Completed&&
               (z.binding.context==v.binding.context?
                z.policy->Complete(*z.physicalRequest,*terminal->receipt,v.snapshot,now):
                z.policy->CompleteAfterContextChange(*z.physicalRequest,*terminal->receipt,v.snapshot,now))){
                ++self.completed_;z.request.reset();z.physicalRequest.reset();z.carried=false;z.carriedSupply.reset();z.target.reset();z.recoveryPrepared=false;
                if(z.hand){(void)h.Release(x.input,z.hand->token);z.hand.reset();}
            }else if(terminal&&terminal->request==*z.request&&!terminal->nativeDispatched&&
                     terminal->state==AmmoResourceRequestState::Rejected&&z.policy->RejectUndispatched(*z.physicalRequest)){
                ++self.rejected_;z.request.reset();z.physicalRequest.reset();z.seedNeutral=true;
            }else if(z.submissionFailed){
                // The channel never accepted this command. Cancel only the
                // exact still-queued command before asking policy to release it.
                if(ledger.Phase()==AmmunitionLedgerPhase::Queued&&ledger.Pending()&&*ledger.Pending()==z.physicalRequest->ammunition)
                    ledger.Cancel();
                if(z.policy->RejectUndispatched(*z.physicalRequest)){
                    ++self.rejected_;z.request.reset();z.physicalRequest.reset();z.submissionFailed=false;z.seedNeutral=true;
                }
            }
        }
        if(z.binding.context!=v.binding.context||z.handOwner!=x.input.owner){
            if(!z.policy->RebindContext(v.snapshot,x.input.owner,now))return false;
            z.hand.reset();z.target.reset();z.handOwner=x.input.owner;z.recoveryPrepared=false;
        }
        z.binding=v.binding;
        if(w.rawFresh)z.geometryBound=true;
        // Start another reload only after the prior physical cycle truly closed.
        if(z.restart){
            try{z.policy=std::make_unique<FixedContainerReload>(profile,v.binding.context,FeedKey{profile.id,z.lifetime},
                FeedKey{profile.id,profile.revision},z.supply,z.lifetime,ledger,true,x.input.owner);}catch(...){return false;}
            if(!z.policy->Bind(v.snapshot,now))return false;z.lastRaw=0;z.restart=false;z.seedNeutral=true;
        }
        if(!w.rawFresh||x.raw.input.sequence<=z.lastRaw){
            if(!w.rawFresh){z.policy->Suspend();z.target.reset();z.recoveryPrepared=false;if(z.hand){(void)h.Release(x.input,z.hand->token);z.hand.reset();}}
            return true;
        }
        const auto& r=x.raw;auto original=r.input;original.nowNs=now;
        auto step=z.policy->Step();if(step==FixedContainerStep::Complete)return true;
        const auto index=unsigned(step);const auto& g=profile.geometry[index];auto motion=g.motion;
        auto expected=Contact(motion);
        const auto recovery=z.policy->RecoveryMotion();
        if(recovery){
            auto held=recovery->WeaponFromHeld();
            if(recovery->Kind()==FeedMotionKind::FreePlacement&&recovery->WeaponWorld())
                held=Multiply(Multiply(held,*recovery->WeaponWorld()),feed_interaction_detail::Inverse(r.weaponWorldMeters));
            expected=recovery->Kind()==FeedMotionKind::FreePlacement?Multiply(motion.placement.sourceFromGrip,held):held;
            if(recovery->Kind()==FeedMotionKind::FreePlacement&&z.carried)z.carriedSupply=held;
        }
        if(step==FixedContainerStep::Insert&&z.carriedSupply)expected=Multiply(motion.placement.sourceFromGrip,*z.carriedSupply);
        const bool pressed=r.gripPressed&&x.grip;
        const bool releasing=x.releaseEvidence&&z.hand&&
            x.releaseEvidence->released.reason==HandInteractionReason::Released&&x.releaseEvidence->released.token==z.hand->token&&
            x.releaseEvidence->before.token==z.hand->token&&x.input.released[0];
        if(z.hand&&r.grip&&!releasing){
            const auto renewed=h.RenewFrom(x.input,original,z.hand->token,{z.hand->token.contact,original.sequence,original.deadlineNs,true});
            if(renewed.claim)z.hand=renewed.claim;else z.hand.reset();
        }
        if(!z.hand&&pressed&&*w.intent!=UINT64_MAX){
            const bool insertion=step==FixedContainerStep::Insert;
            const bool eligible=insertion?(z.carried?Near(r.weaponFromWrist,expected,g.contact):r.pouchContact):Near(r.weaponFromWrist,expected,g.contact);
            if(eligible&&(!insertion||(v.snapshot.counts.loaded<v.snapshot.counts.capacity&&v.snapshot.counts.reserve>0))){
                if(insertion&&!z.carried){
                    motion.placement.weaponFromSource=Multiply(feed_interaction_detail::Inverse(motion.placement.sourceFromGrip),r.weaponFromWrist);
                    if(!z.policy->RebaseInsertionSource(v.snapshot,motion,now))return true;
                    z.carriedSupply=motion.placement.weaponFromSource;expected=r.weaponFromWrist;z.seedNeutral=true;
                }
                const auto gun=h.Current(InteractionHand::Right);if(!gun)return false;
                HandClaimRequest q{x.input.owner,InteractionHand::Left,insertion?HandClaimKind::AmmoObject:HandClaimKind::Mechanism,
                    insertion?HandInteractionKey{z.supply.resource.id,z.supply.resource.generation}:x.weapon,
                    {{g.contact.role,profile.revision},original.sequence,original.deadlineNs,true},++*w.intent,insertion?0:gun->token.id};
                const auto acquired=h.AcquireFrom(x.input,original,q);if(acquired.claim){z.hand=acquired.claim;if(insertion)z.carried=true;}
            }
        }
        FixedContainerSample sample;sample.ammunition=v.snapshot;sample.input=original;sample.currentSafety=x.input;
        sample.releaseEvidence=x.releaseEvidence;
        sample.held=true;sample.finishLoading=x.finishLoading;sample.weaponFromContact=expected;
        if(z.hand)sample.claim=z.hand->token;
        const auto& c=v.binding.context;sample.interaction.contact={{c.resource.actor,c.resource.actorGeneration,c.resource.weapon,c.equipGeneration,c.space},
            {profile.id,z.lifetime},v.snapshot.sequence,original.sequence,original.observedNs,true,true,r.weaponFromWrist};
        sample.interaction.hand={x.input.owner.actor,1};sample.interaction.trackingGeneration=x.trackingEpoch?x.trackingEpoch:x.input.owner.space;
        sample.interaction.focused=true;sample.interaction.grip=r.grip&&!z.seedNeutral&&!releasing;sample.interaction.operationPermitted=true;
        sample.interaction.weaponWorld=r.weaponWorldMeters;
        if(recovery&&!z.recoveryPrepared&&!z.hand&&!r.grip){
            const FeedRecoveryFrame frame{sample.interaction.contact.owner,sample.interaction.contact.mechanism,
                v.snapshot.sequence,original.sequence,original.observedNs,r.weaponWorldMeters};
            if(z.policy->RecoverMotion(sample,*recovery,frame)){
                // Recovery preparation uses this original frame. Arming and
                // the next grip must consume strictly newer contact evidence.
                z.recoveryPrepared=true;z.lastRaw=original.sequence;return true;
            }
        }
        *w.result=z.policy->Update(sample,h);z.lastRaw=original.sequence;z.seedNeutral=false;
        const auto& result=*w.result;
        if(result.status==FixedContainerStatus::Suspended||result.status==FixedContainerStatus::Invalid){
            // The recognizer has revoked this grip. Keep its mechanical state
            // and recovery contact, but release the matching arbiter claim.
            if(z.hand){(void)h.Release(x.input,z.hand->token);z.hand.reset();}
            z.target.reset();
        }
        if(result.status!=FixedContainerStatus::Gesture||result.interaction.status>=FeedInteractionStatus::InvalidProfile)
            z.recoveryPrepared=false;
        if(result.interaction.target){
            z.target=result.interaction.target;z.targetStep=step;
            if(step==FixedContainerStep::Insert)z.carriedSupply=result.interaction.target;
            else {
                const auto delta=Multiply(feed_interaction_detail::Inverse(Contact(g.motion)),*result.interaction.target);
                for(unsigned n=0;n<z.equipment->parts.size();++n)if(z.equipment->parts[n].coupledOpening)
                    z.parts[n]=Multiply(z.equipment->parts[n].weaponFromStepStart[index],delta);
            }
        }
        if(result.status==FixedContainerStatus::PhysicalStep){
            z.recoveryPrepared=false;
            for(unsigned n=0;n<z.equipment->parts.size();++n)if(z.equipment->parts[n].coupledOpening)
                z.parts[n]=g.motion.returnToStart?z.equipment->parts[n].weaponFromStepStart[index]:z.equipment->parts[n].weaponFromStepEnd[index];
            z.target.reset();if(z.hand){(void)h.Release(x.input,z.hand->token);z.hand.reset();}
        }
        if(result.dispatch){
            const auto requestId=self.api_.nextRequest(self.api_.context);
            if(!requestId){
                if(ledger.Phase()==AmmunitionLedgerPhase::Queued&&ledger.Pending()&&*ledger.Pending()==result.dispatch->ammunition)ledger.Cancel();
                (void)z.policy->RejectUndispatched(*result.dispatch);z.seedNeutral=true;return true;
            }
            AmmoResourceRequest request{v.binding,{v.binding.context,requestId,original.observedNs,original.deadlineNs,
                AmmunitionOperation::InsertRounds,{},profile.roundsPerInsertion}};
            request.preparedCommand=result.dispatch->ammunition;
            request.sourceProof=AmmoResourceSourceProof{now,original.sequence,v.snapshot.sequence,v.snapshot.observedNs,v.snapshot.deadlineNs};
            z.request=request;z.physicalRequest=result.dispatch;*w.dispatch=request;
            if(z.hand){(void)h.Release(x.input,z.hand->token);z.hand.reset();}z.target.reset();
        }
        if((!r.grip||releasing)&&!result.dispatch&&z.hand){(void)h.Release(x.input,z.hand->token);z.hand.reset();z.target.reset();
            if(step==FixedContainerStep::Insert){z.carried=false;z.carriedSupply.reset();}}
        return true;
    });
    if(!updated){out.blocksFire=!slot->geometryBound||(slot->policy&&slot->policy->BlocksFire());return out;}
    out.blocksFire=!slot->geometryBound||slot->policy->BlocksFire();out.status=result.status;
    const FixedContainerGate gate{view->binding,s.input.owner,slot->lifetime,++slot->revision,s.input.sequence,
        s.input.observedNs,s.input.deadlineNs,out.blocksFire,!out.blocksFire};
    const bool published=api_.publishGate(api_.context,gate);
    if(dispatch){
        // The service gate is established before native admission, outside its
        // ledger lock. Neither accepted submission nor final counts are a receipt.
        if(!published||!out.blocksFire||!api_.submit(api_.context,*dispatch))slot->submissionFailed=true;else ++submitted_;
    }
    auto& t=out.tracking;t.enabled=published;t.source={e,view->binding,view->snapshot};t.input=s.input;t.gun=*gun;t.hand=slot->hand;
    t.blocksFire=out.blocksFire;t.carried=slot->carried;t.pending=bool(slot->request);t.step=slot->policy->Step();t.targetStep=slot->targetStep;
    t.lifetime=slot->lifetime;t.revision=slot->revision;t.target=slot->target;t.carriedSupply=slot->carriedSupply;t.parts=slot->parts;t.partCount=unsigned(e->parts.size());
    t.grip=s.grip;t.gripPressed=s.gripPressed;
    if(s.geometrySequence==s.input.sequence&&s.trackingEpoch==s.input.owner.space&&feed_mechanism_detail::Pose(s.bodyFromHand)){
        const auto contact=pouch_;double d=0;
        for(unsigned n=0;n<3;++n){const auto delta=s.bodyFromHand.values[3][n]-contact.centerMeters[n];d+=delta*delta;}
        t.pouchContact=d<=contact.radiusMeters*contact.radiusMeters;
    }
    out.ammoOwnsHand=slot->hand&&slot->hand->token.kind==HandClaimKind::AmmoObject;
    out.mechanismOwnsHand=slot->hand&&slot->hand->token.kind==HandClaimKind::Mechanism;
    if(!slot->carried&&!slot->request){
        if(const auto source=FixedContainerBodySource(t.source,s.input,s.input.nowNs)){
            AmmoSupplyVisualSample visual{true,false,*source,s.input,*gun,pouch_,frame_};
            if(AmmoSupplyVisualFresh(visual,s.input.nowNs)){out.bodyAmmo=t.source;out.bodyVisual=visual;}
        }
    }
    return out;
}
void Bc2PhysicalFixedContainer::Report(std::ostream& out)const {
    out<<"{\"ticks\":"<<ticks_<<",\"submitted\":"<<submitted_<<",\"completed\":"<<completed_<<",\"rejected\":"<<rejected_<<'}';
}
}
