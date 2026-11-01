#include "Bc2PhysicalFixedContainer.h"
#include <algorithm>
#include <iomanip>
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
    std::optional<Bc2FixedContainerCompletionObservation> lastCompletion;
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
    if(s.observation){
        *s.observation={};s.observation->owner=s.nativeOwner;s.observation->input=s.input;
        s.observation->equipment=e;s.observation->raw=s.raw;s.observation->pouch=pouch_;s.observation->frame=frame_;
    }
    Slot* observedSlot=nullptr;
    struct Capture {
        Bc2FixedContainerObservation* observation;const Bc2PhysicalFixedContainerResult& result;
        Slot*& slot;HandInteraction& hands;
        ~Capture(){if(!observation)return;
            observation->tracking=result.tracking;observation->status=result.status;
            observation->leftClaim=hands.Current(InteractionHand::Left);
            if(slot){observation->supply=slot->supply;observation->pendingRequest=slot->request;observation->lastCompletion=slot->lastCompletion;}
        }
    } capture{s.observation,out,observedSlot,hands};
    if(!enabled_||!api_.read||!api_.withLedger||!api_.submit||!api_.outcome||!api_.publishGate||!api_.nextRequest||!e||
       e!=FindFixedContainerEquipment(s.configurationPath,s.mesh,s.rigFingerprint)||s.cancel||!Fresh(s.input,s.input.nowNs)){
        Cancel(s.input,hands);return out;
    }
    const auto view=api_.read(api_.context,s.nativeOwner,s.input.nowNs);
    if(s.observation)s.observation->view=view;
    const auto physical=view?FixedContainerHandItem({e,view->binding,view->snapshot,s.family},s.input.owner,s.input.nowNs):std::nullopt;
    if(!view||view->binding.owner!=s.nativeOwner||!AmmoResourceViewFresh(*view,s.input.nowNs)||!physical||s.weapon!=*physical||
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
    observedSlot=slot;
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
    if(s.observation)s.observation->rawAdmitted=rawFresh;
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
                FeedKey{profile.id,profile.revision},z.supply,z.lifetime,ledger,true,x.input.owner,x.weapon);}catch(...){return false;}
            if(!z.policy->Bind(v.snapshot,now)){z.policy.reset();return false;}z.ledger=&ledger;
        }
        if(z.physicalRequest&&z.request){
            const auto& terminal=*w.terminal;
            if(terminal&&terminal->request==*z.request&&terminal->receipt&&terminal->state==AmmoResourceRequestState::Completed&&
               (z.binding.context==v.binding.context?
                z.policy->Complete(*z.physicalRequest,*terminal->receipt,v.snapshot,now):
                z.policy->CompleteAfterContextChange(*z.physicalRequest,*terminal->receipt,v.snapshot,now))){
                z.lastCompletion=Bc2FixedContainerCompletionObservation{z.lifetime,z.supply,*terminal,v.snapshot,now};
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
            if(!z.policy->RebindContext(v.snapshot,x.input.owner,now,x.weapon))return false;
            z.hand.reset();z.target.reset();z.handOwner=x.input.owner;z.recoveryPrepared=false;
        }
        z.binding=v.binding;
        if(w.rawFresh)z.geometryBound=true;
        // Start another reload only after the prior physical cycle truly closed.
        if(z.restart){
            try{z.policy=std::make_unique<FixedContainerReload>(profile,v.binding.context,FeedKey{profile.id,z.lifetime},
                FeedKey{profile.id,profile.revision},z.supply,z.lifetime,ledger,true,x.input.owner,x.weapon);}catch(...){return false;}
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
                    z.parts[n]=g.motion.kind==FeedMotionKind::Linear?
                        reload_insertion_detail::BlendRigid(z.equipment->parts[n].weaponFromStepStart[index],
                            z.equipment->parts[n].weaponFromStepEnd[index],std::clamp(result.interaction.progress,0.,1.)):
                        Multiply(z.equipment->parts[n].weaponFromStepStart[index],delta);
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
    auto& t=out.tracking;t.enabled=published;t.source={e,view->binding,view->snapshot,s.family};t.input=s.input;t.gun=*gun;t.hand=slot->hand;
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
bool FixedContainerObservationJoined(const Bc2FixedContainerObservation& o,const ReloadStateOwner& owner,
    const HandInteractionSample& input,const FixedContainerEquipmentProfile* equipment,std::int64_t now)noexcept {
    return KnownFixedContainerEquipment(equipment)&&o.equipment==equipment&&o.owner==owner&&
        o.input.owner==input.owner&&o.input.sequence==input.sequence&&o.input.observedNs==input.observedNs&&
        o.input.deadlineNs==input.deadlineNs&&o.input.observedNs<=o.input.nowNs&&
        o.input.nowNs<=input.nowNs&&input.nowNs<=now&&o.input.focused==input.focused&&
        o.input.tracked==input.tracked&&o.input.released==input.released&&Fresh(o.input,now);
}
void WriteFixedContainerObservation(std::ostream& out,const Bc2FixedContainerObservation* observation,
    const ReloadStateOwner& owner,const HandInteractionSample& input,const RigIdentity& rig,
    const FixedContainerEquipmentProfile* equipment,std::int64_t now){
    out<<std::boolalpha<<std::setprecision(9);
    const auto text=[&](std::string_view s){out<<'"';for(const unsigned char c:s){
        if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32||c>=127){constexpr char h[]="0123456789abcdef";out<<"\\u00"<<h[c>>4]<<h[c&15];}
        else out<<char(c);}out<<'"';};
    out<<"{\"schema\":\"bc2vr.fixed-container-observation.v1\",\"available\":";
    const auto* e=KnownFixedContainerEquipment(equipment);
    if(!e||!observation||!FixedContainerObservationJoined(*observation,owner,input,e,now)){
        out<<"false,\"reason\":";text(!e?"unsupported_configuration":!observation?"capture_pending_or_not_requested":"source_mismatched_or_expired");out<<'}';return;
    }
    const auto& o=*observation;const auto& t=o.tracking;const auto& r=o.raw;
    const auto scalar=[&](float value){if(std::isfinite(value))out<<value;else out<<"null";};
    const auto matrix=[&](const math::Matrix4& value){
        if(!feed_mechanism_detail::Pose(value)){out<<"null";return;}
        out<<'[';for(unsigned a=0;a<4;++a){if(a)out<<',';out<<'[';for(unsigned b=0;b<4;++b){if(b)out<<',';out<<value.values[a][b];}out<<']';}out<<']';};
    const auto nativeOwner=[&](const ReloadStateOwner& v){out<<"{\"player\":"<<v.player<<",\"soldier\":"<<v.soldier<<",\"weak\":"<<v.weak
        <<",\"weapon\":"<<v.weapon<<",\"actorGeneration\":"<<v.actorGeneration<<",\"equipGeneration\":"<<v.equipGeneration<<",\"space\":"<<v.space<<'}';};
    const auto handOwner=[&](const HandInteractionOwner& v){out<<"{\"actor\":"<<v.actor<<",\"actorGeneration\":"<<v.actorGeneration
        <<",\"equipGeneration\":"<<v.equipGeneration<<",\"space\":"<<v.space<<'}';};
    const auto evidence=[&](const HandInteractionSample& v){out<<"{\"owner\":";handOwner(v.owner);
        out<<",\"sequence\":"<<v.sequence<<",\"observedNs\":"<<v.observedNs<<",\"deadlineNs\":"<<v.deadlineNs<<",\"nowNs\":"<<v.nowNs
            <<",\"focused\":"<<v.focused<<",\"tracked\":["<<v.tracked[0]<<','<<v.tracked[1]<<"],\"released\":["<<v.released[0]<<','<<v.released[1]<<"]}";};
    const auto key=[&](const auto& v){out<<"{\"id\":"<<v.id<<",\"generation\":"<<v.generation<<'}';};
    const auto supply=[&](const FeedSupplyKey& v){out<<"{\"resource\":";key(v.resource);out<<",\"container\":";key(v.container);out<<'}';};
    const auto claim=[&](const HandClaim& v){const auto& k=v.token;out<<"{\"token\":{\"id\":"<<k.id<<",\"owner\":";handOwner(k.owner);
        out<<",\"hand\":"<<unsigned(k.hand)<<",\"kind\":"<<unsigned(k.kind)<<",\"item\":";key(k.item);out<<",\"contact\":";key(k.contact);
        out<<",\"prerequisiteClaim\":"<<k.prerequisiteClaim<<"},\"deadlineNs\":"<<v.deadlineNs<<",\"inputSequence\":"<<v.inputSequence<<'}';};
    const auto resource=[&](const AmmoResourceIdentity& v){out<<"{\"actor\":"<<v.actor<<",\"actorGeneration\":"<<v.actorGeneration
        <<",\"weapon\":"<<v.weapon<<",\"weaponGeneration\":"<<v.weaponGeneration<<'}';};
    const auto context=[&](const AmmoResourceContext& v){out<<"{\"resource\":";resource(v.resource);out<<",\"equipGeneration\":"<<v.equipGeneration<<",\"space\":"<<v.space<<'}';};
    const auto counts=[&](const AmmunitionCounts& v){out<<"{\"loaded\":"<<v.loaded<<",\"reserve\":"<<v.reserve<<",\"capacity\":"<<v.capacity<<'}';};
    const auto binding=[&](const AmmoResourceBinding& v){out<<"{\"owner\":";nativeOwner(v.owner);out<<",\"context\":";context(v.context);
        out<<",\"inventory\":"<<v.inventory<<",\"switching\":"<<v.switching<<",\"data\":"<<v.data<<",\"persistence\":"<<v.persistence
            <<",\"observedNs\":"<<v.observedNs<<",\"deadlineNs\":"<<v.deadlineNs<<'}';};
    const auto snapshot=[&](const AmmunitionSnapshot& v){out<<"{\"context\":";context(v.context);out<<",\"sequence\":"<<v.sequence
        <<",\"observedNs\":"<<v.observedNs<<",\"deadlineNs\":"<<v.deadlineNs<<",\"counts\":";counts(v.counts);out<<",\"coherentIdleVerified\":"<<v.coherentIdleVerified<<'}';};
    const auto original=[&](const std::optional<RemovedMagazineResource>& v){if(!v){out<<"null";return;}out<<"{\"owner\":";resource(v->owner);
        out<<",\"id\":"<<v->id<<",\"rounds\":"<<v->rounds<<",\"capacity\":"<<v->capacity<<'}';};
    const auto command=[&](const AmmunitionCommand& v){out<<"{\"context\":";context(v.context);out<<",\"id\":"<<v.id<<",\"sourceSequence\":"<<v.sourceSequence
        <<",\"requestedNs\":"<<v.requestedNs<<",\"admissionDeadlineNs\":"<<v.admissionDeadlineNs<<",\"deadlineNs\":"<<v.deadlineNs
        <<",\"operation\":"<<unsigned(v.operation)<<",\"before\":";counts(v.before);out<<",\"after\":";counts(v.after);
        out<<",\"original\":";original(v.original);out<<",\"mutationRequired\":"<<v.mutationRequired<<'}';};
    const auto receipt=[&](const AmmunitionReceipt& v){out<<"{\"command\":";command(v.command);
        out<<",\"authorityInvocation\":"<<v.authorityInvocation<<",\"beganNs\":"<<v.beganNs<<",\"completedNs\":"<<v.completedNs<<",\"before\":";counts(v.before);
        out<<",\"after\":";counts(v.after);out<<",\"authorityVerified\":"<<v.authorityVerified<<",\"copiesVerified\":"<<v.copiesVerified<<'}';};
    const auto request=[&](const AmmoResourceRequest& v){out<<"{\"binding\":";binding(v.binding);const auto& i=v.intent;
        out<<",\"intent\":{\"context\":";context(i.context);out<<",\"id\":"<<i.id<<",\"observedNs\":"<<i.observedNs<<",\"deadlineNs\":"<<i.deadlineNs
            <<",\"operation\":"<<unsigned(i.operation)<<",\"original\":";original(i.original);out<<",\"rounds\":"<<i.rounds<<"},\"discard\":"<<v.discard<<",\"sourceProof\":";
        if(!v.sourceProof)out<<"null";else{const auto& p=*v.sourceProof;out<<"{\"constructedNs\":"<<p.constructedNs<<",\"inputSequence\":"<<p.inputSequence
            <<",\"snapshotSequence\":"<<p.snapshotSequence<<",\"snapshotObservedNs\":"<<p.snapshotObservedNs<<",\"snapshotDeadlineNs\":"<<p.snapshotDeadlineNs<<'}';}
        out<<",\"preparedCommand\":";if(v.preparedCommand)command(*v.preparedCommand);else out<<"null";out<<'}';};
    const auto identity=[&](const ReloadHoldIdentity& v){out<<"{\"owner\":";nativeOwner(v.owner);out<<",\"firing\":["<<v.firing[0]<<','<<v.firing[1]<<','<<v.firing[2]
        <<"],\"serverPlayer\":"<<v.serverPlayer<<",\"serverSoldier\":"<<v.serverSoldier<<",\"serverItem\":"<<v.serverItem<<'}';};
    const auto outcome=[&](const AmmoResourceOutcome& v){out<<"{\"request\":";request(v.request);out<<",\"state\":"<<unsigned(v.state)<<",\"receipt\":";
        if(v.receipt)receipt(*v.receipt);else out<<"null";out<<",\"resolvedNs\":"<<v.resolvedNs<<",\"nativeDispatched\":"<<v.nativeDispatched<<",\"actorRetirement\":";
        if(!v.actorRetirement)out<<"null";else{const auto& a=*v.actorRetirement;out<<"{\"event\":"<<a.event<<",\"callbackRevision\":"<<a.callbackRevision
            <<",\"lifetimeEndEpoch\":"<<a.lifetimeEndEpoch<<",\"observedNs\":"<<a.observedNs<<",\"previous\":";identity(a.previous);
            out<<",\"replacement\":";identity(a.replacement);out<<",\"pending\":";if(a.pending)command(*a.pending);else out<<"null";
            out<<",\"retiredItems\":"<<a.retiredItems<<",\"nativeDispatched\":"<<a.nativeDispatched<<'}';}out<<'}';};
    out<<"true,\"observationOnly\":true,\"retainedEventsAreHistorical\":true,\"owner\":";nativeOwner(o.owner);out<<",\"input\":";evidence(o.input);
    const auto& p=e->interaction;
    out<<",\"status\":"<<unsigned(o.status)<<",\"profile\":{\"id\":"<<p.id<<",\"revision\":"<<p.revision<<",\"kind\":"<<unsigned(p.kind)
        <<",\"ammunitionKind\":"<<p.ammunitionKind<<",\"capacity\":"<<p.capacity<<",\"roundsPerInsertion\":"<<p.roundsPerInsertion<<",\"maxAgeNs\":"<<p.maxAgeNs
        <<",\"configurationPath\":";text(e->configurationPath);out<<",\"meshPath\":";text(e->mesh);out<<",\"rigFingerprint\":"<<e->rigFingerprint<<",\"geometry\":[";
    for(unsigned n=0;n<p.geometry.size();++n){if(n)out<<',';
        if(((p.kind==FixedContainerKind::SingleRoundMuzzle||p.kind==FixedContainerKind::EnBlocClip)&&n!=2)||
           (p.kind==FixedContainerKind::SingleRoundBreech&&n==1)){out<<"null";continue;}
        const auto& g=p.geometry[n];const auto& m=g.motion;
        out<<"{\"contact\":{\"role\":"<<g.contact.role<<",\"radiusMeters\":";scalar(g.contact.radiusMeters);out<<",\"maxAngleRadians\":";scalar(g.contact.maxAngleRadians);
        out<<",\"orientation\":"<<unsigned(g.contact.orientation)<<"},\"motion\":{\"kind\":";text(m.kind==FeedMotionKind::Hinge?"Hinge":m.kind==FeedMotionKind::FreePlacement?"FreePlacement":"Linear");
        out<<",\"measured\":"<<m.measured<<",\"returnToStart\":"<<m.returnToStart<<",\"travel\":";scalar(m.travel);
        out<<",\"policy\":{\"completeFraction\":";scalar(m.completeFraction);out<<",\"releaseFraction\":";scalar(m.releaseFraction);
        out<<",\"pathToleranceMeters\":";scalar(m.pathToleranceMeters);out<<",\"orientationToleranceRadians\":";scalar(m.orientationToleranceRadians);
        out<<",\"maxStepMeters\":";scalar(m.maxStepMeters);out<<",\"maxStepRadians\":";scalar(m.maxStepRadians);
        out<<",\"endpointDwellNs\":"<<m.endpointDwellNs<<",\"maximumGestureNs\":"<<m.maximumGestureNs<<",\"maxInputGapNs\":"<<m.maxInputGapNs<<"},\"weaponFromMotion\":";
        matrix(m.weaponFromMotion);out<<",\"motionFromContact\":";matrix(m.motionFromContact);out<<",\"placement\":{\"weaponFromSource\":";matrix(m.placement.weaponFromSource);
        out<<",\"weaponFromDestination\":";matrix(m.placement.weaponFromDestination);out<<",\"sourceFromGrip\":";matrix(m.placement.sourceFromGrip);out<<"}}}";
    }
    out<<"]},\"tracking\":{\"enabled\":"<<t.enabled<<",\"current\":"<<FixedContainerTrackingFresh(t,now)<<",\"blocksFire\":"<<t.blocksFire
        <<",\"carried\":"<<t.carried<<",\"pending\":"<<t.pending<<",\"step\":"<<unsigned(t.step)<<",\"targetStep\":"<<unsigned(t.targetStep)
        <<",\"lifetime\":"<<t.lifetime<<",\"revision\":"<<t.revision<<",\"gun\":";claim(t.gun);out<<",\"hand\":";if(t.hand)claim(*t.hand);else out<<"null";
    out<<",\"target\":";if(t.target)matrix(*t.target);else out<<"null";out<<",\"carriedSupply\":";if(t.carriedSupply)matrix(*t.carriedSupply);else out<<"null";
    out<<",\"pouchContact\":"<<t.pouchContact<<",\"grip\":"<<t.grip<<",\"gripPressed\":"<<t.gripPressed<<"},\"leftClaim\":";if(o.leftClaim)claim(*o.leftClaim);else out<<"null";
    const bool rawCurrent=r.valid&&r.nativeOwner==o.owner&&r.rig==rig&&r.equipment==e&&r.input.owner==o.input.owner&&
        r.input.sequence<=o.input.sequence&&r.input.observedNs<=o.input.observedNs&&Fresh(r.input,now)&&
        feed_mechanism_detail::Pose(r.weaponFromWrist)&&feed_mechanism_detail::Pose(r.weaponWorldMeters);
    const auto* rawEquipment=KnownFixedContainerEquipment(r.equipment);
    out<<",\"raw\":{\"valid\":"<<r.valid<<",\"current\":"<<rawCurrent<<",\"admittedAtTick\":"<<o.rawAdmitted<<",\"ownerMatches\":"<<(r.nativeOwner==o.owner)<<",\"rigMatches\":"<<(r.rig==rig)
        <<",\"profileMatches\":"<<(r.equipment==e)<<",\"rigFingerprint\":"<<(rawEquipment?rawEquipment->rigFingerprint:0)<<",\"input\":";evidence(r.input);
    out<<",\"owner\":";nativeOwner(r.nativeOwner);const auto& rr=r.rig;
    out<<",\"rig\":{\"soldier\":"<<rr.soldier<<",\"weak\":"<<rr.weak<<",\"animation\":"<<rr.animation<<",\"skeleton\":"<<rr.skeleton
        <<",\"pose\":"<<rr.pose<<",\"worldHeader\":"<<rr.worldHeader<<",\"worldMatrices\":"<<rr.worldMatrices<<",\"skinMatrices\":"<<rr.skinMatrices
        <<",\"evaluatedMatrices\":"<<rr.evaluatedMatrices<<",\"count\":"<<rr.count<<",\"nativeIk\":"<<rr.nativeIk<<'}';
    out<<",\"weaponFromWrist\":";matrix(r.weaponFromWrist);out<<",\"weaponWorldMeters\":";matrix(r.weaponWorldMeters);
    out<<",\"grip\":"<<r.grip<<",\"gripPressed\":"<<r.gripPressed<<",\"pouchContact\":"<<r.pouchContact<<"},\"pouch\":{\"frame\":"<<unsigned(o.frame)<<",\"centerMeters\":[";
    for(unsigned n=0;n<3;++n){if(n)out<<',';scalar(o.pouch.centerMeters[n]);}out<<"],\"radiusMeters\":";scalar(o.pouch.radiusMeters);out<<"},\"supply\":";supply(o.supply);
    out<<",\"viewFresh\":"<<(o.view&&o.view->binding.owner==o.owner&&FixedContainerHandOwner(o.view->binding,o.input.owner)&&
        o.view->snapshot.counts.capacity==p.capacity&&AmmoResourceViewFresh(*o.view,now))<<",\"view\":";
    if(!o.view)out<<"null";else{const auto& v=*o.view;out<<"{\"binding\":";binding(v.binding);out<<",\"identity\":";identity(v.identity);out<<",\"snapshot\":";snapshot(v.snapshot);
        out<<",\"phase\":"<<unsigned(v.phase)<<",\"command\":";if(v.command)command(*v.command);else out<<"null";
        out<<",\"request\":"<<v.request<<",\"requestState\":"<<unsigned(v.requestState)<<",\"wellEmpty\":"<<v.wellEmpty<<",\"receipt\":";
        if(v.receipt)receipt(*v.receipt);else out<<"null";out<<",\"terminal\":";if(v.terminal)outcome(*v.terminal);else out<<"null";out<<'}';}
    out<<",\"physicalFamily\":";
    if(!t.source.family)out<<"null";else{const auto& proof=*t.source.family;const auto& m=proof.mode;
        out<<"{\"owner\":";nativeOwner(proof.owner);out<<",\"observedNs\":"<<proof.observedNs<<",\"deadlineNs\":"<<proof.deadlineNs
            <<",\"current\":"<<bool(FixedContainerHandItem(t.source,t.input.owner,now))<<",\"physicalItem\":"<<m.persistent
            <<",\"inventory\":"<<m.inventory<<",\"selectedSecondary\":"<<m.selectedSecondary<<",\"targetWeapon\":"<<m.targetWeapon
            <<",\"targetSlot\":"<<m.targetSlot<<",\"action\":"<<m.action<<",\"primaryAction\":"<<m.primaryAction<<",\"configurationPath\":";
        text(m.family?m.family->secondaryConfiguration:std::string_view{});out<<",\"asset\":";text(m.family?m.family->secondaryAsset:std::string_view{});
        out<<",\"persistentId\":";text(m.family?m.family->persistentId:std::string_view{});out<<'}';}
    out<<",\"pendingRequest\":";if(o.pendingRequest)request(*o.pendingRequest);else out<<"null";
    out<<",\"lastCompletion\":";if(!o.lastCompletion)out<<"null";else{const auto& c=*o.lastCompletion;
        out<<"{\"lifetime\":"<<c.lifetime<<",\"supply\":";supply(c.supply);out<<",\"outcome\":";outcome(c.outcome);
        out<<",\"postSnapshot\":";snapshot(c.postSnapshot);out<<",\"acceptedNs\":"<<c.acceptedNs<<'}';}out<<'}';
}
}
