#include "fvr/interaction/FeedEquipmentSession.h"
#include "Test.h"
#include <memory>
#include <type_traits>
using namespace fvr::interaction;
namespace {
using Op=FeedPhysicalOperation;using Effect=FeedAmmunitionEffect;using Status=FeedEquipmentStatus;
using Gesture=FeedInteractionStatus;using Matrix=fvr::math::Matrix4;
constexpr Op order[]={Op::ReleaseLatch,Op::OpenCover,Op::WithdrawFeed,Op::DetachContainer,
    Op::AttachContainer,Op::SeatFeed,Op::CloseCover,Op::EngageLatch,Op::CycleAction};
Matrix Pose(float x=0,float y=0,float z=0,float angle=0){
    Matrix m{};const auto c=std::cos(angle),s=std::sin(angle);
    m.values[0]={c,s,0,0};m.values[1]={-s,c,0,0};m.values[2][2]=m.values[3][3]=1;
    m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;
}
Matrix Product(const Matrix& a,const Matrix& b){Matrix m{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
    for(unsigned k=0;k<4;++k)m.values[i][j]+=a.values[i][k]*b.values[k][j];return m;}
struct Fixture {
    FeedMechanismProfile profile{};FeedInteractionProfile geometry{};FeedEquipmentSessionSample x{};
    FeedSupplyKey original{{10,1},{20,1}},goal{{30,2},{40,2}};
    std::unique_ptr<FeedEquipmentSession> equipment{};FeedEquipmentSessionResult result{};
    unsigned dispatches=0;
    explicit Fixture(Op first=Op::ReleaseLatch,int loaded=17,int reserve=80){
        profile.family=FeedFamily::Belt;profile.id=1;profile.revision=2;profile.ammunitionKind=3;profile.maxAgeNs=100;
        profile.cover=profile.latch=profile.container=profile.charge=true;
        profile.containerNeedsOpen=profile.feedNeedsOpen=profile.detachNeedsUnseated=true;
        auto& s=x.snapshot;s.owner={1,2,3,4,5};s.mechanism={6,7};s.profileId=1;s.profileRevision=2;
        x.nowNs=s.observedNs=1000;s.sequence=10;s.inputSequence=20;s.reconciled=true;
        s.cover=FeedCoverState::Closed;s.latch=FeedLatchState::Engaged;s.container=FeedContainerState::Attached;
        s.feed=FeedSeatState::Seated;s.cycle=FeedCycleState::Required;s.attached=s.seated=original;
        s.supply={goal,3,FeedSupplyState::Usable};
        for(unsigned i=0;i<profile.contacts.size();++i){profile.contacts[i]={100+i,.025f,.1f};s.contacts[i]={100+i,Pose()};}
        geometry.profileId=1;geometry.profileRevision=2;
        for(unsigned i=1;i<geometry.operations.size();++i){auto& m=geometry.operations[i];
            m.operation=static_cast<Op>(i);m.kind=FeedMotionKind::Linear;m.measured=true;
            m.weaponFromMotion=m.motionFromContact=Pose();m.travel=.1f;m.completeFraction=.9f;m.releaseFraction=.8f;
            m.pathToleranceMeters=.005f;m.orientationToleranceRadians=.08f;m.maxStepMeters=.04f;m.maxStepRadians=.25f;
            m.endpointDwellNs=20;m.maximumGestureNs=1000;m.maxInputGapNs=40;
            if(i>=static_cast<unsigned>(Op::WithdrawFeed)&&i<=static_cast<unsigned>(Op::SeatFeed)){
                m.kind=FeedMotionKind::FreePlacement;m.maxStepMeters=.06f;
                m.placement={Pose(.02f,.01f,-.02f),Pose(.12f,.07f,.04f,.45f),Pose(.01f,-.02f,.015f)};
                s.contacts[static_cast<unsigned>(feed_mechanism_detail::OperationPart(m.operation))].weaponFromContact=
                    Product(m.placement.sourceFromGrip,m.placement.weaponFromSource);
            }
        }
        for(auto op:order){if(op==first)break;Observe(op);}
        auto& a=x.ammunition;a.owner=s.owner;a.mechanism=s.mechanism;a.lifetime=500;a.sequence=s.sequence;
        a.observedNs=x.nowNs;a.deadlineNs=x.nowNs+100;a.coherentIdleVerified=true;a.counts={loaded,reserve,50};
        a.loadedSupply=first==Op::SeatFeed?goal:original;
        auto& b=x.binding;b.key={80,1};b.owner=s.owner;b.mechanism=s.mechanism;b.profileId=1;b.profileRevision=2;
        b.verified=true;b.operations.fill(true);
        auto& authority=x.authority;authority.key={81,1};authority.binding=b.key;authority.owner=s.owner;
        authority.mechanism=s.mechanism;authority.lifetime=500;authority.profileId=1;authority.profileRevision=2;authority.verified=true;
        x.held=true;x.interaction.hand={90,1};x.interaction.trackingGeneration=1;x.interaction.focused=true;
        x.interaction.contact.tracked=x.interaction.contact.handAvailable=true;
        Configure();Start();
    }
    void Start(AmmoResourceIdentity resource={}){equipment=std::make_unique<FeedEquipmentSession>(profile,geometry,x.snapshot.owner,x.snapshot.mechanism,goal,500,resource);}
    Op Next()const {const auto q=EvaluateFeedMechanism(profile,x.snapshot,goal,x.nowNs);
        return q.candidate?q.candidate->operation:Op::CycleAction;}
    void Configure(){auto& a=x.authority;a.operation=Next();a.effect=Effect::PhysicalOnly;a.supply={};a.rounds=0;
        if(profile.sameContainerReplenishment){
            if(a.operation==Op::AttachContainer){a.effect=Effect::RefillLoaded;a.supply=goal;
                a.rounds=std::min(x.ammunition.counts.capacity-x.ammunition.counts.loaded,x.ammunition.counts.reserve);}
            return;
        }
        switch(a.operation){
        case Op::WithdrawFeed:a.effect=Effect::ExtractLoaded;a.supply=original;a.rounds=x.ammunition.counts.loaded;break;
        case Op::DetachContainer:a.effect=Effect::ReturnSupply;a.supply=original;a.rounds=17;break;
        case Op::AttachContainer:a.effect=Effect::AcquireReserve;a.supply=goal;a.rounds=50;break;
        case Op::SeatFeed:a.effect=Effect::LoadSupply;a.supply=goal;a.rounds=50;break;
        default:break;}
    }
    Matrix Contact(float progress=0)const {
        const auto& m=geometry.operations[static_cast<unsigned>(Next())];
        if(m.kind==FeedMotionKind::FreePlacement)return Product(m.placement.sourceFromGrip,m.placement.weaponFromSource);
        return Pose(progress*m.travel);
    }
    void Prepare(float progress=0,bool grip=true,std::int64_t advance=10){
        x.nowNs+=advance;auto& s=x.snapshot;++s.sequence;++s.inputSequence;s.observedNs=x.nowNs;
        auto& a=x.ammunition;a.sequence=s.sequence;a.observedNs=x.nowNs;a.deadlineNs=x.nowNs+100;
        auto& c=x.interaction.contact;c.owner=s.owner;c.mechanism=s.mechanism;c.snapshotSequence=s.sequence;
        c.inputSequence=s.inputSequence;c.observedNs=x.nowNs;c.weaponFromHandContact=Contact(progress);x.interaction.grip=grip;
    }
    FeedEquipmentSessionResult Run(){result=equipment->Update(x);if(result.dispatch)++dispatches;return result;}
    FeedEquipmentSessionResult Step(float progress=0,bool grip=true,std::int64_t advance=10){Prepare(progress,grip,advance);return Run();}
    bool Acquire(){return Step(0,false).session.reload.status==FeedReloadStatus::Armed&&Step().session.interaction.status==Gesture::Acquired;}
    FeedEquipmentSessionResult Carry(const Matrix& object,bool grip=true){Prepare(0,grip);
        const auto& m=geometry.operations[static_cast<unsigned>(Next())];
        x.interaction.contact.weaponFromHandContact=Product(m.placement.sourceFromGrip,object);return Run();}
    FeedEquipmentSessionResult Finish(){
        const auto& m=geometry.operations[static_cast<unsigned>(Next())];
        if(m.kind==FeedMotionKind::FreePlacement){
            // Irregular 3D carry deliberately leaves any straight rail.
            for(const auto& object:{Pose(.04f,.04f,-.03f,.1f),Pose(.06f,.015f,0,.05f),Pose(.08f,.045f,.025f,.2f),
                Pose(.10f,.07f,.04f,.35f),m.placement.weaponFromDestination,m.placement.weaponFromDestination,
                m.placement.weaponFromDestination})Carry(object);
            return Carry(m.placement.weaponFromDestination,false);
        }
        for(float p:{.2f,.4f,.6f,.8f,.95f,.95f,.95f})Step(p);return result;
    }
    void Observe(Op op){auto& s=x.snapshot;switch(op){
        case Op::ReleaseLatch:s.latch=FeedLatchState::Released;break;
        case Op::OpenCover:s.cover=FeedCoverState::Open;break;
        case Op::WithdrawFeed:s.feed=FeedSeatState::Unseated;s.seated={};break;
        case Op::DetachContainer:s.container=FeedContainerState::Detached;s.attached={};
            if(profile.coupledContainerDetach){s.feed=FeedSeatState::Unseated;s.seated={};}break;
        case Op::AttachContainer:s.container=FeedContainerState::Attached;s.attached=goal;break;
        case Op::SeatFeed:s.feed=FeedSeatState::Seated;s.seated=goal;break;
        case Op::CloseCover:s.cover=FeedCoverState::Closed;break;
        case Op::EngageLatch:s.latch=FeedLatchState::Engaged;break;
        case Op::CycleAction:s.cycle=FeedCycleState::NativeReady;break;
        default:break;}
    }
    FeedAmmunitionReceipt Receipt(const FeedAmmunitionCommand& command,bool applied=true,bool mechanismEffect=true){
        if(applied&&mechanismEffect)Observe(command.request.completion.candidate.operation);
        if(applied&&mechanismEffect&&profile.sameContainerReplenishment&&command.authority.effect==Effect::RefillLoaded)
            x.snapshot.refill={FeedRefillState::Applied,command.request.lifetime,command.request.id};
        x.ammunition.counts=applied?command.after:command.before.counts;
        x.ammunition.loadedSupply=applied?command.loadedSupplyAfter:command.before.loadedSupply;
        Prepare(0,false);FeedAmmunitionReceipt r;r.command=command;r.post=x.ammunition;
        r.acknowledgement={command.request,applied?FeedReloadAcknowledgement::Applied:FeedReloadAcknowledgement::Rejected,
            r.post.sequence,r.post.observedNs};r.authorityInvocation=command.request.id;
        r.beganNs=command.request.requestedNs;r.completedNs=x.nowNs;r.authorityVerified=r.copiesVerified=true;
        r.noTransferEntry=!applied;return r;
    }
    FeedEquipmentSessionResult Native(const FeedAmmunitionCommand& command,bool applied=true,bool mechanismEffect=true){
        x.receipt=Receipt(command,applied,mechanismEffect);const auto r=Run();x.receipt.reset();Configure();return r;
    }
    FeedReloadAck FreshAck(const FeedAmmunitionCommand& command,FeedReloadAcknowledgement status=FeedReloadAcknowledgement::Applied)const {
        return {command.request,status,x.snapshot.sequence,x.snapshot.observedNs};
    }
    int Held(FeedSupplyKey supply)const {for(const auto& r:equipment->State().ammunition.resources)if(r&&r->supply==supply)return r->rounds;return 0;}
    int Total()const {const auto s=equipment->State().ammunition;int total=s.snapshot.counts.loaded+s.snapshot.counts.reserve;
        for(const auto& r:s.resources)if(r)total+=r->rounds;return total;}
};
int NineMeasuredStagesDispatchOneJoinedCommand(){
    Fixture f;unsigned count=0;
    for(const auto op:order){CHECK(f.Next()==op&&f.Acquire());const auto before=f.x.snapshot;
        const auto sent=f.Finish();CHECK(sent.dispatch&&sent.status==Status::Dispatched);
        CHECK(!sent.session.reload.dispatch&&sent.session.interaction.completion);
        const auto command=*sent.dispatch;CHECK(command.request.completion.candidate.operation==op&&command.request.id==count+1);
        CHECK(command.before.sequence==f.x.snapshot.sequence&&command.before.observedNs==f.x.snapshot.observedNs);
        CHECK(feed_reload_detail::SameSnapshot(before,f.x.snapshot)==false); // Only observation metadata changed.
        CHECK(before.feed==f.x.snapshot.feed&&before.cover==f.x.snapshot.cover&&before.container==f.x.snapshot.container);
        CHECK(f.Total()==97&&f.equipment->State().ammunition.acceptedOperations==count);
        for(unsigned i=0;i<3;++i)CHECK(!f.Run().dispatch&&!f.result.session.reload.dispatch&&!f.result.session.recognitionEnabled);
        const auto applied=f.Native(command);++count;
        CHECK(applied.receiptAccepted&&applied.acknowledgementDelivered&&!applied.session.reload.pending&&!applied.ammunition.pending);
        CHECK(!applied.session.recognitionEnabled&&!applied.dispatch&&applied.ammunition.acceptedOperations==count);
        CHECK(applied.session.reload.acceptedOperations==count&&f.Total()==97);
    }
    CHECK(f.dispatches==9&&f.result.status==Status::Complete);
    CHECK(f.result.ammunition.snapshot.counts.loaded==50&&f.result.ammunition.snapshot.counts.reserve==47);
    CHECK(f.Held(f.original)==0&&f.Held(f.goal)==0);return 0;
}
int ExplicitDirectRefillHandlesPartialAndEmpty(){
    for(unsigned kind=0;kind<3;++kind){const int loaded=kind==0?0:17,reserve=kind==2?8:80;
        Fixture f(Op::SeatFeed,loaded,reserve);f.x.authority.effect=Effect::RefillLoaded;
        f.x.authority.rounds=std::min(50-loaded,reserve);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
        const auto& c=*sent.dispatch;CHECK(c.after.loaded==loaded+f.x.authority.rounds&&c.after.reserve==reserve-f.x.authority.rounds);
        CHECK(f.equipment->State().ammunition.snapshot.counts.loaded==loaded&&f.Held(f.goal)==0);
        const auto done=f.Native(c);CHECK(done.receiptAccepted&&!done.session.reload.pending&&f.Total()==loaded+reserve);
        CHECK(f.Held(f.goal)==0&&f.dispatches==1);
    }return 0;
}
int NewGestureRequiresExactJoinedAuthority(){
    for(unsigned kind=0;kind<13;++kind){Fixture f;f.Prepare(0,false);
        switch(kind){case 0:++f.x.ammunition.sequence;break;case 1:--f.x.ammunition.observedNs;break;
        case 2:++f.x.ammunition.owner.equipGeneration;break;case 3:++f.x.ammunition.mechanism.generation;break;
        case 4:++f.x.ammunition.lifetime;break;case 5:f.x.ammunition.coherentIdleVerified=false;break;
        case 6:f.x.authority.verified=false;break;case 7:++f.x.authority.binding.generation;break;
        case 8:f.x.authority.effect=Effect::Unavailable;break;case 9:f.x.binding.operations[1]=false;break;
        case 10:++f.x.authority.lifetime;break;case 11:f.x.ammunition.deadlineNs=f.x.nowNs;break;
        default:f.x.authority.rounds=1;break;}
        const auto r=f.Run();CHECK(r.status==Status::PreflightRejected&&!r.session.recognitionEnabled&&!r.dispatch);
        CHECK(!r.session.reload.pending&&!r.ammunition.pending&&r.ammunition.acceptedOperations==0);
    }return 0;
}
int AuthorityCannotChangeDuringGesture(){
    Fixture f;CHECK(f.Acquire());f.Step(.3f);++f.x.authority.key.generation;
    CHECK(!f.Step(.5f).session.recognitionEnabled&&!f.result.dispatch);
    CHECK(f.Step().session.interaction.status==Gesture::NeedNeutral);CHECK(f.Acquire());CHECK(f.Finish().dispatch);return 0;
}
int UpdatedCountsAndForgedAckCannotReplaceReceipt(){
    Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);const auto c=*sent.dispatch;
    const auto real=f.Receipt(c);f.x.acknowledgement=real.acknowledgement;
    const auto forged=f.Run();CHECK(forged.status==Status::AcknowledgementRejected&&!forged.acknowledgementDelivered);
    CHECK(forged.ammunition.pending&&forged.session.reload.pending&&forged.ammunition.acceptedOperations==0);
    CHECK(forged.session.reload.acceptedOperations==0&&f.Held(f.original)==0&&!forged.session.recognitionEnabled);
    f.x.receipt=real;CHECK(f.Run().receiptAccepted&&f.result.acknowledgementDelivered&&!f.result.session.reload.pending);
    CHECK(f.Held(f.original)==17&&f.dispatches==1);return 0;
}
int AppliedReceiptWithoutMechanismPoststateRetainsEffect(){
    Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);const auto c=*sent.dispatch;
    const auto receipt=f.Receipt(c,true,false);f.x.receipt=receipt;const auto waiting=f.Run();
    CHECK(waiting.receiptAccepted&&!waiting.acknowledgementDelivered&&waiting.status==Status::AwaitingCorroboration);
    CHECK(!waiting.ammunition.pending&&waiting.session.reload.pending&&waiting.ammunition.acceptedOperations==1);
    CHECK(waiting.session.reload.acceptedOperations==0&&f.Held(f.original)==17);
    f.x.receipt.reset();f.Observe(Op::WithdrawFeed);CHECK(f.Step(0,false).session.reload.pending);
    f.Prepare(0,false);f.x.acknowledgement=f.FreshAck(c);const auto resolved=f.Run();
    CHECK(resolved.acknowledgementDelivered&&!resolved.session.reload.pending&&resolved.session.reload.acceptedOperations==1);
    CHECK(resolved.ammunition.lastReceipt==receipt&&f.Total()==97&&!resolved.dispatch);return 0;
}
int HistoricalReceiptNeedsExplicitFreshCorroboration(){
    Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);const auto c=*sent.dispatch;
    const auto receipt=f.Receipt(c);f.x.receipt=receipt;f.x.held=false;f.x.nowNs+=1000;
    const auto late=f.Run();CHECK(late.receiptAccepted&&!late.acknowledgementDelivered&&f.Held(f.original)==17);
    CHECK(late.ammunition.lastReceipt==receipt&&late.session.reload.pending&&late.ammunition.acceptedOperations==1);
    f.x.receipt.reset();CHECK(f.Step(0,false).session.reload.pending&&!f.result.acknowledgementDelivered);
    f.Prepare(0,false);f.x.acknowledgement=f.FreshAck(c,FeedReloadAcknowledgement::Rejected);
    CHECK(f.Run().status==Status::AcknowledgementRejected&&f.result.session.reload.pending);
    f.Prepare(0,false);f.x.receipt=receipt;f.x.acknowledgement=f.FreshAck(c);
    const auto done=f.Run();CHECK(done.receiptAccepted&&done.acknowledgementDelivered&&!done.session.reload.pending);
    CHECK(done.ammunition.lastReceipt==receipt&&done.session.reload.suspended&&done.ammunition.suspended);
    CHECK(done.ammunition.acceptedOperations==1&&done.session.reload.acceptedOperations==1&&f.dispatches==1);return 0;
}
int TrackingAndStowRetainHeldRoundsUntilExplicitReturn(){
    for(const bool stow:{false,true}){Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
        if(stow)f.x.held=false;else f.x.interaction.contact.tracked=false;
        const auto done=f.Native(*sent.dispatch);CHECK(done.receiptAccepted&&f.Held(f.original)==17&&f.Total()==97);
        CHECK(done.session.reload.suspended&&done.ammunition.suspended&&!done.session.reload.pending);
        for(unsigned i=0;i<3;++i)CHECK(!f.Step(0,false).dispatch&&f.Held(f.original)==17);
        f.x.held=f.x.interaction.contact.tracked=true;CHECK(f.Acquire());const auto returned=f.Finish();CHECK(returned.dispatch);
        CHECK(returned.dispatch->authority.effect==Effect::ReturnSupply&&f.Held(f.original)==17);
        CHECK(f.Native(*returned.dispatch).receiptAccepted&&f.Held(f.original)==0&&f.Total()==97);
        CHECK(f.equipment->State().ammunition.snapshot.counts.reserve==97&&f.dispatches==2);
    }return 0;
}
int LatchedAcknowledgementCannotReconcileWithoutCurrentCountJoin(){
    for(const bool stow:{false,true}){Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
        const auto command=*sent.dispatch;const auto dispatched=f.x;
        const auto receipt=f.Receipt(command);const auto callback=f.x;
        // More recent pre-effect observations arrived before this delayed
        // callback. Its genuine fresh ack can latch, but its old snapshot
        // cannot replace the reload coordinator's newer observation.
        f.x=dispatched;f.Step();f.Step();const auto newer=f.x;
        f.x=callback;f.x.nowNs=newer.nowNs;f.x.receipt=receipt;
        const auto latched=f.Run();CHECK(latched.receiptAccepted&&latched.acknowledgementDelivered);
        CHECK(latched.session.reload.phase==FeedReloadPhase::AwaitingSnapshot&&latched.session.reload.pending);
        CHECK(latched.session.reload.acceptedOperations==1&&latched.ammunition.acceptedOperations==1&&f.Held(f.original)==17);
        f.x.receipt.reset();f.x.snapshot.sequence=newer.snapshot.sequence;f.x.snapshot.inputSequence=newer.snapshot.inputSequence;
        if(stow)f.x.held=false;else f.x.interaction.contact.tracked=false;
        ++f.x.ammunition.counts.loaded;
        const auto unjoined=f.Step(0,false);CHECK(unjoined.session.reload.pending&&unjoined.status==Status::AwaitingCorroboration);
        CHECK(!unjoined.dispatch&&!unjoined.session.recognitionEnabled&&!unjoined.session.interaction.target);
        CHECK(!unjoined.session.interaction.completion&&unjoined.ammunition.lastReceipt==receipt&&f.Held(f.original)==17);
        f.x.ammunition.counts=command.after;
        const auto reconciled=f.Step(0,false);CHECK(!reconciled.session.reload.pending&&!reconciled.dispatch);
        CHECK(reconciled.session.reload.acceptedOperations==1&&reconciled.ammunition.lastReceipt==receipt&&f.Total()==97);
    }return 0;
}
int RejectedReceiptRequiresAnotherGesture(){
    Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
    const auto rejected=f.Native(*sent.dispatch,false);CHECK(rejected.receiptAccepted&&!rejected.session.reload.pending);
    CHECK(rejected.ammunition.acceptedOperations==0&&rejected.session.reload.acceptedOperations==0);
    CHECK(!f.Step().dispatch&&f.result.session.interaction.status==Gesture::NeedNeutral);
    CHECK(f.Acquire());const auto retry=f.Finish();CHECK(retry.dispatch&&retry.dispatch->request.id==2&&f.dispatches==2);return 0;
}
int MalformedReceiptCannotAdvanceEitherComponent(){
    for(unsigned kind=0;kind<5;++kind){Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
        auto receipt=f.Receipt(*sent.dispatch);const auto exact=receipt;
        switch(kind){case 0:receipt.copiesVerified=false;break;case 1:++receipt.post.counts.loaded;break;
        case 2:++receipt.command.authority.key.generation;break;case 3:receipt.authorityInvocation=0;break;
        default:++receipt.acknowledgement.request.binding.generation;break;}
        f.x.receipt=receipt;f.x.acknowledgement=exact.acknowledgement;
        const auto bad=f.Run();CHECK(bad.status==Status::ReceiptRejected&&bad.ammunition.pending&&bad.session.reload.pending);
        CHECK(bad.ammunition.acceptedOperations==0&&bad.session.reload.acceptedOperations==0&&!bad.acknowledgementDelivered);
        f.x.receipt=exact;CHECK(f.Run().receiptAccepted&&!f.result.session.reload.pending&&f.Held(f.original)==17);
    }return 0;
}
int RetirementCannotAbandonPendingWorkOrCustody(){
    Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
    f.Prepare();auto retiring=f.equipment->Retire(f.x.snapshot,f.x.ammunition,f.x.nowNs);
    CHECK(retiring.status==Status::RetireNeedsReconciliation&&retiring.ammunition.pending&&retiring.session.reload.pending);
    CHECK(!retiring.ammunition.retired&&retiring.session.reload.phase!=FeedReloadPhase::Retired);
    CHECK(f.Native(*sent.dispatch).receiptAccepted&&f.Held(f.original)==17);
    retiring=f.equipment->Retire(f.x.snapshot,f.x.ammunition,f.x.nowNs);
    CHECK(retiring.status==Status::RetireNeedsReconciliation&&!retiring.ammunition.retired);
    f.Prepare(0,false);retiring=f.equipment->Retire(f.x.snapshot,f.x.ammunition,f.x.nowNs);
    CHECK(retiring.status==Status::Retired&&retiring.ammunition.retired&&retiring.session.reload.phase==FeedReloadPhase::Retired);
    CHECK(f.Held(f.original)==17&&f.Total()==97&&retiring.ammunition.lastReceipt);
    CHECK(f.Run().status==Status::Retired&&!f.result.dispatch);return 0;
}
int RetirementRefusalNeverRetiresOnlyOneHalf(){
    for(unsigned kind=0;kind<3;++kind){Fixture f;CHECK(f.Acquire());f.Prepare(0,false);
        if(kind==0)++f.x.ammunition.counts.loaded; // Ledger refuses.
        else if(kind==1){ // Fresh for the older ledger, behind the reload's clock.
            f.x.nowNs-=15;f.x.snapshot.observedNs=f.x.ammunition.observedNs=f.x.nowNs;
            f.x.ammunition.deadlineNs=f.x.nowNs+100;
        }else f.x.snapshot.inputSequence-=2; // Reload's private input watermark refuses.
        const auto originalCounts=f.equipment->State().ammunition.snapshot.counts;
        const auto refused=f.equipment->Retire(f.x.snapshot,f.x.ammunition,f.x.nowNs);
        CHECK(refused.status==Status::RetireNeedsReconciliation&&!refused.ammunition.retired);
        CHECK(refused.session.reload.phase!=FeedReloadPhase::Retired);
        f.x.ammunition.counts=originalCounts;f.Prepare(0,false,30);
        const auto done=f.equipment->Retire(f.x.snapshot,f.x.ammunition,f.x.nowNs);
        CHECK(done.status==Status::Retired&&done.ammunition.retired&&done.session.reload.phase==FeedReloadPhase::Retired);
    }return 0;
}
int WrongLifetimeCannotRebindOrReleaseRetainedEffects(){
    Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
    const auto owner=f.x.snapshot.owner;++f.x.snapshot.owner.equipGeneration;f.x.ammunition.owner=f.x.snapshot.owner;
    CHECK(!f.Step(0,false).dispatch&&f.result.session.reload.pending&&f.result.ammunition.pending);
    CHECK(f.result.ammunition.snapshot.owner==owner&&f.result.ammunition.snapshot.counts.loaded==17);
    f.x.snapshot.owner=f.x.ammunition.owner=owner;CHECK(f.Native(*sent.dispatch).receiptAccepted&&f.Held(f.original)==17);
    return 0;
}
void Replenishment(Fixture& f,bool detached=false){
    f.profile.sameContainerReplenishment=f.profile.coupledContainerDetach=true;f.profile.detachNeedsUnseated=false;
    f.profile.latch=f.profile.charge=false;f.profile.contacts[1]=f.profile.contacts[4]={};
    auto& s=f.x.snapshot;s.latch=FeedLatchState::Absent;s.cycle=FeedCycleState::Absent;
    f.goal=f.original;s.supply.key=f.goal;s.refill={FeedRefillState::Required,500,0};
    if(detached){s.cover=FeedCoverState::Open;s.container=FeedContainerState::Detached;s.attached={};
        s.feed=FeedSeatState::Unseated;s.seated={};}
    f.Configure();f.Start();
}
int FiveStageSameContainerRefillPreservesPartialAndEmptyCounts(){
    for(unsigned kind=0;kind<3;++kind){const int loaded=kind==0?0:17,reserve=kind==2?8:80;
        Fixture f(Op::ReleaseLatch,loaded,reserve);Replenishment(f);unsigned count=0;
        const int transfer=std::min(50-loaded,reserve);
        for(const auto op:{Op::OpenCover,Op::DetachContainer,Op::AttachContainer,Op::SeatFeed,Op::CloseCover}){
            CHECK(f.Next()==op&&f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);const auto command=*sent.dispatch;
            CHECK(command.request.completion.candidate.goal==f.original&&command.before.loadedSupply==f.original);
            CHECK(command.authority.effect==(op==Op::AttachContainer?Effect::RefillLoaded:Effect::PhysicalOnly));
            CHECK(command.mutationRequired==(op==Op::AttachContainer));
            if(op!=Op::AttachContainer)CHECK(command.before.counts==command.after&&command.loadedSupplyAfter==f.original);
            if(op==Op::DetachContainer)CHECK(command.before.counts.loaded==loaded&&command.before.counts.reserve==reserve);
            CHECK(f.equipment->State().ammunition.snapshot.counts==command.before.counts);
            const auto applied=f.Native(command);++count;
            CHECK(applied.receiptAccepted&&applied.acknowledgementDelivered&&!applied.session.reload.pending&&!applied.ammunition.pending);
            CHECK(applied.ammunition.snapshot.loadedSupply==f.original&&f.Total()==loaded+reserve&&f.Held(f.original)==0);
            CHECK(applied.session.reload.completed==(op==Op::CloseCover));
            CHECK(applied.ammunition.acceptedOperations==count&&applied.session.reload.acceptedOperations==count);
            if(op==Op::DetachContainer)CHECK(f.x.snapshot.feed==FeedSeatState::Unseated&&f.x.snapshot.refill.state==FeedRefillState::Required);
            if(op==Op::AttachContainer)CHECK(f.Next()==Op::SeatFeed&&f.x.snapshot.refill.requestId==command.request.id);
        }
        CHECK(f.dispatches==5&&f.result.status==Status::Complete);
        CHECK(f.result.ammunition.snapshot.counts.loaded==loaded+transfer&&f.result.ammunition.snapshot.counts.reserve==reserve-transfer);
        CHECK(!f.Step(0,false).dispatch&&f.dispatches==5); // Completed cycle cannot replenish again.
    }return 0;
}
int SameContainerRejectsExtractionReplacementAndPhysicalOnlyRefill(){
    for(unsigned kind=0;kind<4;++kind){Fixture f;Replenishment(f,kind>=2);
        if(kind==0){f.x.snapshot.cover=FeedCoverState::Open;f.Configure();f.x.authority.effect=Effect::ExtractLoaded;
            f.x.authority.supply=f.original;f.x.authority.rounds=17;}
        if(kind==1){f.x.ammunition.loadedSupply={{33,1},{44,1}};}
        if(kind==2){f.x.authority.effect=Effect::PhysicalOnly;f.x.authority.supply={};f.x.authority.rounds=0;}
        if(kind==3){f.x.authority.effect=Effect::AcquireReserve;}
        const auto r=f.Step(0,false);CHECK(!r.dispatch&&!r.session.recognitionEnabled&&r.status==Status::PreflightRejected);
        CHECK(r.ammunition.acceptedOperations==0&&r.session.reload.acceptedOperations==0);
    }return 0;
}
int RefillReceiptAndExactProgressMustBothCorroborate(){
    for(unsigned kind=0;kind<4;++kind){Fixture f;Replenishment(f,true);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
        const auto command=*sent.dispatch;const auto receipt=f.Receipt(command);const auto valid=f.x.snapshot.refill;
        if(kind==0){f.x.acknowledgement=receipt.acknowledgement; // Counts plus an ack never substitute the exact ledger receipt.
            CHECK(!f.Run().acknowledgementDelivered&&f.result.ammunition.pending&&f.result.session.reload.pending);
            f.x.acknowledgement.reset();}
        if(kind==1)f.x.snapshot.refill={FeedRefillState::Required,500,0};
        if(kind==2)++f.x.snapshot.refill.requestId;
        if(kind==3)++f.x.snapshot.refill.lifetime;
        f.x.receipt=receipt;const auto applied=f.Run();CHECK(applied.receiptAccepted);
        CHECK(applied.ammunition.snapshot.counts==command.after&&applied.ammunition.snapshot.loadedSupply==f.original);
        if(kind){CHECK(!applied.acknowledgementDelivered&&applied.session.reload.pending&&!applied.session.reload.completed);
            f.x.receipt.reset();f.x.snapshot.refill=valid;f.Prepare(0,false);f.x.acknowledgement=f.FreshAck(command);
            CHECK(f.Run().acknowledgementDelivered&&!f.result.session.reload.pending);
        }else CHECK(applied.acknowledgementDelivered&&!applied.session.reload.pending);
        CHECK(f.dispatches==1&&f.Held(f.original)==0&&f.Total()==97);
    }return 0;
}
int SourceRebaseRequiresCurrentCountsAndPreservesRetainedRounds(){
    Fixture f(Op::WithdrawFeed);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
    CHECK(f.Native(*sent.dispatch).receiptAccepted&&f.Held(f.original)==17);
    auto& motion=f.geometry.operations[static_cast<unsigned>(Op::DetachContainer)];
    motion.placement.weaponFromSource.values[3][1]+=.002f;
    f.x.snapshot.contacts[static_cast<unsigned>(FeedPart::Container)].weaponFromContact=
        Product(motion.placement.sourceFromGrip,motion.placement.weaponFromSource);
    f.Prepare();const auto candidate=*EvaluateFeedMechanism(f.profile,f.x.snapshot,f.goal,f.x.nowNs).candidate;
    auto changed=f.x.ammunition;++changed.counts.reserve;
    CHECK(!f.equipment->UpdateMotion(f.x.snapshot,changed,candidate,motion,f.x.nowNs));
    const auto retained=f.equipment->State();
    CHECK(f.equipment->UpdateMotion(f.x.snapshot,f.x.ammunition,candidate,motion,f.x.nowNs));
    const auto rebased=f.equipment->State();
    CHECK(rebased.ammunition.resources==retained.ammunition.resources&&rebased.ammunition.lastReceipt==retained.ammunition.lastReceipt);
    CHECK(rebased.session.reload.lastApplied==retained.session.reload.lastApplied&&f.Held(f.original)==17&&f.Total()==97);
    CHECK(f.Run().session.interaction.status==Gesture::NeedNeutral);CHECK(f.Acquire());
    auto current=*EvaluateFeedMechanism(f.profile,f.x.snapshot,f.goal,f.x.nowNs).candidate;
    CHECK(!f.equipment->UpdateMotion(f.x.snapshot,f.x.ammunition,current,motion,f.x.nowNs));
    const auto next=f.Finish();CHECK(next.dispatch&&next.dispatch->request.id==sent.dispatch->request.id+1);
    current=*EvaluateFeedMechanism(f.profile,f.x.snapshot,f.goal,f.x.nowNs).candidate;
    CHECK(!f.equipment->UpdateMotion(f.x.snapshot,f.x.ammunition,current,motion,f.x.nowNs));
    CHECK(f.Held(f.original)==17&&f.Total()==97);return 0;
}
AmmunitionSnapshot NativeContext(const FeedAmmunitionSnapshot& a,AmmoResourceIdentity resource){
    AmmunitionSnapshot n;n.context={resource,a.owner.equipGeneration,a.owner.space};n.sequence=a.sequence;
    n.observedNs=a.observedNs;n.deadlineNs=a.deadlineNs;n.counts={a.counts.loaded,a.counts.reserve,a.counts.capacity};
    n.coherentIdleVerified=true;return n;
}
struct Migration {
    FeedEquipmentContextObservation before{},after{};FeedContextMigration native{};std::int64_t now=0;
    explicit Migration(Fixture& f){
        before=*f.equipment->ContextObservation();after=before;now=before.snapshot.observedNs+1000;
        ++after.snapshot.owner.equipGeneration;++after.snapshot.owner.space;++after.snapshot.sequence;++after.snapshot.inputSequence;
        after.snapshot.observedNs=now;after.ammunition.owner=after.snapshot.owner;after.ammunition.sequence=after.snapshot.sequence;
        after.ammunition.observedNs=now;after.ammunition.deadlineNs=now+100;
        native.before=NativeContext(before.ammunition,{1,2,3,9});native.after=NativeContext(after.ammunition,{1,2,3,9});
    }
    bool Apply(Fixture& f){return f.equipment->MigrateContext(native,before.snapshot,before.ammunition,
        after.snapshot,after.ammunition,now);}
};
int LongStowMigrationPreservesCustodyHistoryAndCounters(){
    Fixture f(Op::WithdrawFeed);f.Start({1,2,3,9});CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
    CHECK(f.Native(*sent.dispatch).receiptAccepted&&f.Held(f.original)==17);
    f.x.held=false;f.Step(0,false);CHECK(f.equipment->ContextObservation());const auto retained=f.equipment->State();
    Migration migration(f);CHECK(migration.native.before.deadlineNs<migration.now);CHECK(migration.Apply(f));
    const auto migrated=f.equipment->State();
    CHECK(migrated.ammunition.suspended&&migrated.session.reload.suspended);
    CHECK(migrated.ammunition.resources==retained.ammunition.resources&&migrated.ammunition.lastReceipt==retained.ammunition.lastReceipt);
    CHECK(migrated.session.reload.lastApplied==retained.session.reload.lastApplied&&f.Total()==97&&f.Held(f.original)==17);
    CHECK(migrated.ammunition.lastRequestId==sent.dispatch->request.id&&!migration.Apply(f));
    f.x.snapshot=migration.after.snapshot;f.x.ammunition=migration.after.ammunition;f.x.nowNs=migration.now;
    f.x.binding.owner=f.x.authority.owner=f.x.snapshot.owner;f.x.held=true;f.Configure();
    CHECK(f.Step().session.interaction.status==Gesture::NeedNeutral&&!f.result.dispatch);
    CHECK(f.Acquire());const auto retry=f.Finish();CHECK(retry.dispatch);
    CHECK(retry.dispatch->request.id==sent.dispatch->request.id+1&&
        retry.dispatch->request.completion.id==sent.dispatch->request.completion.id+1);
    CHECK(retry.dispatch->before.owner==migration.after.snapshot.owner&&f.Held(f.original)==17);return 0;
}
int ContextMigrationRejectsSubstitutionStaleHistoryAndAmbiguity(){
    for(unsigned kind=0;kind<12;++kind){Fixture f;f.Start(kind==11?AmmoResourceIdentity{}:AmmoResourceIdentity{1,2,3,9});
        f.Step(0,false);f.x.held=false;f.Step(0,false);CHECK(f.equipment->ContextObservation());Migration m(f);
        switch(kind){case 0:++m.native.after.context.resource.weaponGeneration;break;
        case 1:++m.native.after.context.resource.actorGeneration;break;
        case 2:++m.after.ammunition.counts.reserve;++m.native.after.counts.reserve;break;
        case 3:++m.before.snapshot.observedNs;++m.before.ammunition.observedNs;++m.native.before.observedNs;break;
        case 4:m.native.after.coherentIdleVerified=false;break;case 5:m.now+=101;break;
        case 6:m.after.snapshot.owner=m.before.snapshot.owner;m.after.ammunition.owner=m.before.ammunition.owner;
            m.native.after.context=m.native.before.context;break;
        case 7:++m.after.snapshot.mechanism.generation;++m.after.ammunition.mechanism.generation;break;
        case 8:++m.native.after.counts.loaded;break;case 9:m.after.snapshot.cover=FeedCoverState::Open;break;
        case 10:++m.after.ammunition.lifetime;break;default:break;}
        const auto retained=f.equipment->State();CHECK(!m.Apply(f));
        CHECK(f.equipment->State().ammunition.snapshot==retained.ammunition.snapshot&&
            f.equipment->State().session.reload.lastApplied==retained.session.reload.lastApplied);
    }
    Fixture pending;pending.Start({1,2,3,9});CHECK(pending.Acquire());CHECK(pending.Finish().dispatch);
    pending.x.held=false;pending.Step(0,false);Migration unresolved(pending);CHECK(!unresolved.Apply(pending));
    CHECK(pending.equipment->State().ammunition.pending&&pending.equipment->State().session.reload.pending);return 0;
}
int EndpointRecoveryJoinsCountsAndRejectedRequestUsesNewIds(){
    Fixture f(Op::AttachContainer);CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
    const auto proof=f.equipment->RecoveryMotion();CHECK(proof);
    f.Prepare(0,false);CHECK(!f.equipment->RecoverMotion(f.x.snapshot,f.x.ammunition,*proof,f.x.nowNs));
    CHECK(f.Native(*sent.dispatch,false).receiptAccepted);CHECK(!f.equipment->State().session.reload.pending);
    const auto held=proof->WeaponFromHeld();const auto& motion=proof->Motion();
    f.x.snapshot.contacts[static_cast<unsigned>(FeedPart::Container)].weaponFromContact=Product(motion.placement.sourceFromGrip,held);
    f.Prepare(0,false);auto wrong=f.x.ammunition;++wrong.counts.reserve;
    CHECK(!f.equipment->RecoverMotion(f.x.snapshot,wrong,*proof,f.x.nowNs));
    CHECK(f.equipment->RecoverMotion(f.x.snapshot,f.x.ammunition,*proof,f.x.nowNs));
    CHECK(f.Carry(held,false).session.interaction.status==Gesture::Armed);
    CHECK(f.Carry(held).session.interaction.status==Gesture::Acquired);
    for(unsigned i=0;i<3;++i)CHECK(!f.Carry(held).dispatch);
    const auto retry=f.Carry(held,false);CHECK(retry.dispatch&&retry.dispatch->request.id==sent.dispatch->request.id+1);
    CHECK(retry.dispatch->request.completion.id==sent.dispatch->request.completion.id+1&&f.Total()==97);
    CHECK(retry.dispatch->request.completion.candidate.supply==f.goal);return 0;
}
int PartialHingeRecoverySurvivesVerifiedContextMigration(){
    Fixture f(Op::OpenCover);auto& motion=f.geometry.operations[static_cast<unsigned>(Op::OpenCover)];
    motion.kind=FeedMotionKind::Hinge;motion.motionFromContact=Pose(.2f);motion.travel=1;motion.maxStepMeters=.06f;
    f.x.snapshot.contacts[static_cast<unsigned>(FeedPart::Cover)].weaponFromContact=motion.motionFromContact;f.Start({1,2,3,9});
    const auto step=[&](float progress,bool grip=true){f.Prepare(0,grip);
        f.x.interaction.contact.weaponFromHandContact=Product(feed_interaction_detail::Along(motion.motionFromContact,
            motion.kind,progress*motion.travel),motion.weaponFromMotion);return f.Run();};
    CHECK(step(0,false).session.interaction.status==Gesture::Armed);CHECK(step(0).session.interaction.status==Gesture::Acquired);
    step(.2f);step(.4f);const auto oldProof=f.equipment->RecoveryMotion();CHECK(oldProof);
    f.x.snapshot.contacts[static_cast<unsigned>(FeedPart::Cover)].weaponFromContact=oldProof->WeaponFromHeld();
    f.x.held=false;step(.4f);Migration migration(f);CHECK(migration.Apply(f));
    const auto proof=f.equipment->RecoveryMotion();CHECK(proof&&proof->Candidate().owner==migration.after.snapshot.owner);
    CHECK(proof->WeaponFromHeld().values==oldProof->WeaponFromHeld().values&&
        feed_interaction_detail::Same(proof->Motion(),oldProof->Motion()));
    f.x.snapshot=migration.after.snapshot;f.x.ammunition=migration.after.ammunition;f.x.nowNs=migration.now;
    f.x.binding.owner=f.x.authority.owner=f.x.snapshot.owner;f.x.held=true;f.Prepare(0,false);
    CHECK(!f.equipment->RecoverMotion(f.x.snapshot,f.x.ammunition,*oldProof,f.x.nowNs));
    CHECK(f.equipment->RecoverMotion(f.x.snapshot,f.x.ammunition,*proof,f.x.nowNs));
    CHECK(step(.4f,false).session.interaction.status==Gesture::Armed);
    CHECK(step(.4f).session.interaction.status==Gesture::Acquired&&Near(static_cast<float>(f.result.session.interaction.progress),.4f));
    for(float p:{.6f,.8f,.95f,.95f,.95f})CHECK(!step(p).dispatch);
    CHECK(step(.95f,false).dispatch&&f.result.dispatch->request.id==1&&f.Total()==97);return 0;
}
}
int main(){
    static_assert(!std::is_copy_constructible_v<FeedEquipmentSession>&&!std::is_move_constructible_v<FeedEquipmentSession>);
    if(NineMeasuredStagesDispatchOneJoinedCommand()||ExplicitDirectRefillHandlesPartialAndEmpty()||
       NewGestureRequiresExactJoinedAuthority()||AuthorityCannotChangeDuringGesture()||UpdatedCountsAndForgedAckCannotReplaceReceipt()||
       AppliedReceiptWithoutMechanismPoststateRetainsEffect()||HistoricalReceiptNeedsExplicitFreshCorroboration()||
       TrackingAndStowRetainHeldRoundsUntilExplicitReturn()||LatchedAcknowledgementCannotReconcileWithoutCurrentCountJoin()||
       RejectedReceiptRequiresAnotherGesture()||
       MalformedReceiptCannotAdvanceEitherComponent()||RetirementCannotAbandonPendingWorkOrCustody()||
       RetirementRefusalNeverRetiresOnlyOneHalf()||WrongLifetimeCannotRebindOrReleaseRetainedEffects()||
       FiveStageSameContainerRefillPreservesPartialAndEmptyCounts()||
       SameContainerRejectsExtractionReplacementAndPhysicalOnlyRefill()||RefillReceiptAndExactProgressMustBothCorroborate()||
       SourceRebaseRequiresCurrentCountsAndPreservesRetainedRounds()||LongStowMigrationPreservesCustodyHistoryAndCounters()||
       ContextMigrationRejectsSubstitutionStaleHistoryAndAmbiguity()||EndpointRecoveryJoinsCountsAndRejectedRequestUsesNewIds()||
       PartialHingeRecoverySurvivesVerifiedContextMigration())return 1;
    std::puts("FeedEquipmentSessionTests: 22 deterministic measured equipment/ammunition cases passed (no native/headset claim)");
    return 0;
}
