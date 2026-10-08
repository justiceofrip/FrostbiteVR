#include "fvr/interaction/ManualReload.h"
#include "Test.h"
using namespace fvr::interaction;
namespace {
ManualReloadConfig Magazine(){
    ManualReloadConfig c{};c.stepCount=3;
    c.steps[0]={ReloadOperation::UnseatMagazine,1};c.steps[1]={ReloadOperation::SeatMagazine,1};
    c.steps[2]={ReloadOperation::CycleAction,1};c.maxSampleGapNs=100;c.ackTimeoutNs=500;c.transactionTimeoutNs=5000;
    return c;
}
struct Fixture {
    ManualReload policy;ManualReloadSample sample{};
    explicit Fixture(ManualReloadConfig config=Magazine()):policy(config){
        sample.owner={1,2,3,4,5};sample.nowNs=1000;sample.focused=sample.tracked=sample.bindingsVerified=true;
    }
    ManualReloadResult Send(bool neutral=false,ReloadOperation operation=ReloadOperation::None,std::uint64_t id=0){
        ++sample.sequence;sample.nowNs+=10;sample.neutral=neutral;sample.gesture={id,operation};sample.acknowledgement={};
        return policy.Update(sample);
    }
    ManualReloadResult Ack(const ManualReloadRequest& request,ReloadAcknowledgement status=ReloadAcknowledgement::Applied,bool fresh=true){
        if(fresh)++sample.sequence;sample.nowNs+=10;sample.neutral=false;sample.gesture={};
        sample.acknowledgement={request.id,request.owner,request.operation,status};return policy.Update(sample);
    }
    ManualReloadResult Start(){Send(true);return Send(false,ReloadOperation::UnseatMagazine,1);}
};
int MagazineOrder(){
    Fixture f;auto r=f.Start();CHECK(r.request&&r.request->step==0&&r.request->repetition==0);
    const auto first=*r.request;r=f.Ack(first);CHECK(r.acknowledged&&!r.completed&&r.acceptedOperations==1);
    CHECK(r.phase==ManualReloadPhase::Active&&r.expected==ReloadOperation::SeatMagazine);
    f.Send(true);r=f.Send(false,ReloadOperation::SeatMagazine,2);CHECK(r.request&&r.request->step==1);
    CHECK(r.request->id>first.id);r=f.Ack(*r.request);CHECK(r.acknowledged&&!r.completed);
    f.Send(true);r=f.Send(false,ReloadOperation::CycleAction,3);CHECK(r.request&&r.request->step==2);
    r=f.Ack(*r.request);CHECK(r.completed&&r.acceptedOperations==3&&!r.expected);
    CHECK(r.phase==ManualReloadPhase::Complete&&!r.request);return 0;
}
int RepeatedShells(){
    auto c=Magazine();c.stepCount=1;c.steps[0]={ReloadOperation::InsertRound,3};Fixture f(c);
    for(unsigned i=0;i<3;++i){
        f.Send(true);auto r=f.Send(false,ReloadOperation::InsertRound,i+1);CHECK(r.request);
        CHECK(r.request->step==0&&r.request->repetition==i);r=f.Ack(*r.request);
        CHECK(r.acceptedOperations==i+1&&r.completed==(i==2));
    }
    return 0;
}
int BreechSequence(){
    auto c=Magazine();c.steps[0]={ReloadOperation::OpenBreech,1};c.steps[1]={ReloadOperation::InsertRound,1};
    c.steps[2]={ReloadOperation::CloseBreech,1};Fixture f(c);
    for(unsigned i=0;i<3;++i){f.Send(true);auto r=f.Send(false,c.steps[i].operation,i+1);CHECK(r.request);r=f.Ack(*r.request);CHECK(r.completed==(i==2));}
    return 0;
}
int StartupHeld(){
    Fixture f;CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,1).request);
    CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,2).request);
    f.Send(true);CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,2).request);
    CHECK(f.Send(false,ReloadOperation::UnseatMagazine,3).request);return 0;
}
int DuplicateGesture(){
    Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;
    CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,1).request);r=f.Ack(request);CHECK(r.acknowledged);
    f.Send(true);r=f.Send(false,ReloadOperation::SeatMagazine,1);CHECK(!r.request&&!r.cancelled);
    CHECK(f.Send(false,ReloadOperation::SeatMagazine,2).request);return 0;
}
int DuplicatePacketCannotRearm(){
    Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;
    r=f.Ack(request,ReloadAcknowledgement::Applied,false);CHECK(r.acknowledged);
    f.sample.neutral=true;f.sample.acknowledgement={};++f.sample.nowNs;
    CHECK(!f.policy.Update(f.sample).request);
    CHECK(!f.Send(false,ReloadOperation::SeatMagazine,2).request);
    f.Send(true);CHECK(f.Send(false,ReloadOperation::SeatMagazine,3).request);return 0;
}
int PendingDoesNotQueue(){
    Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;
    f.Send(true);CHECK(!f.Send(false,ReloadOperation::SeatMagazine,2).request);
    CHECK(f.Ack(request).acknowledged);f.Send(true);
    CHECK(!f.Send(false,ReloadOperation::SeatMagazine,2).request);
    CHECK(f.Send(false,ReloadOperation::SeatMagazine,3).request);return 0;
}
int WrongOperation(){
    Fixture f;f.Send(true);auto r=f.Send(false,ReloadOperation::CycleAction,1);
    CHECK(!r.request&&r.reason==ManualReloadCancel::UnexpectedOperation);
    CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,2).request);
    f.Send(true);CHECK(f.Send(false,ReloadOperation::UnseatMagazine,3).request);return 0;
}
int ExactAcknowledgement(){
    Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;auto wrong=request;
    ++wrong.id;CHECK(!f.Ack(wrong).acknowledged);
    wrong=request;++wrong.owner.equipGeneration;CHECK(!f.Ack(wrong).acknowledged);
    wrong=request;wrong.operation=ReloadOperation::SeatMagazine;CHECK(!f.Ack(wrong).acknowledged);
    CHECK(!f.Ack(request,ReloadAcknowledgement::None).acknowledged);
    CHECK(f.Ack(request).acknowledged);CHECK(!f.Ack(request).acknowledged);return 0;
}
int RejectionPreservesProgress(){
    Fixture f;auto r=f.Start();CHECK(r.request);CHECK(f.Ack(*r.request).acknowledged);
    f.Send(true);r=f.Send(false,ReloadOperation::SeatMagazine,2);CHECK(r.request);const auto request=*r.request;
    r=f.Ack(request,ReloadAcknowledgement::Rejected);
    CHECK(r.cancelled&&r.cancelledRequest==request.id&&r.acceptedOperations==1);
    CHECK(r.reason==ManualReloadCancel::NativeRejected&&!r.request&&r.phase==ManualReloadPhase::Idle);
    CHECK(!f.Ack(request).acknowledged);return 0;
}
int IdentityChange(){
    for(unsigned field=0;field<5;++field){
        Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;
        switch(field){case 0:++f.sample.owner.actor;break;case 1:++f.sample.owner.actorGeneration;break;
        case 2:++f.sample.owner.weapon;break;case 3:++f.sample.owner.equipGeneration;break;default:++f.sample.owner.space;break;}
        r=f.Ack(request);CHECK(r.cancelled&&r.cancelledRequest==request.id);
        CHECK(r.reason==ManualReloadCancel::IdentityChanged&&!r.acknowledged);
        f.Send(true);r=f.Send(false,ReloadOperation::UnseatMagazine,2);CHECK(r.request&&r.request->id>request.id);
        CHECK(!f.Ack(request).acknowledged);
    }
    return 0;
}
int TrackingAndBindingLoss(){
    for(unsigned field=0;field<3;++field){
        Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;
        if(field==0)f.sample.focused=false;else if(field==1)f.sample.tracked=false;else f.sample.bindingsVerified=false;
        r=f.Ack(request);CHECK(r.cancelled&&!r.acknowledged&&r.cancelledRequest==request.id);
        CHECK(r.reason==(field==2?ManualReloadCancel::UnverifiedBinding:ManualReloadCancel::TrackingLost));
        f.sample.focused=f.sample.tracked=f.sample.bindingsVerified=true;
        CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,2).request);
        f.Send(true);CHECK(f.Send(false,ReloadOperation::UnseatMagazine,3).request);
    }
    return 0;
}
int CancellationBeatsAcknowledgement(){
    Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;f.sample.cancel=true;
    r=f.Ack(request);CHECK(r.cancelled&&!r.acknowledged&&r.reason==ManualReloadCancel::Explicit);
    CHECK(r.cancelledRequest==request.id&&!r.completed);return 0;
}
int Rollbacks(){
    for(unsigned kind=0;kind<3;++kind){
        Fixture f;auto r=f.Start();CHECK(r.request);
        f.sample.neutral=false;f.sample.gesture={};f.sample.acknowledgement={};
        if(kind==0)--f.sample.nowNs;
        else if(kind==1){--f.sample.sequence;++f.sample.nowNs;}
        else {f.Send(false,ReloadOperation::SeatMagazine,4);++f.sample.sequence;++f.sample.nowNs;f.sample.gesture={3,ReloadOperation::SeatMagazine};}
        r=f.policy.Update(f.sample);CHECK(r.cancelled&&!r.request);
        CHECK(r.reason==(kind==0?ManualReloadCancel::ClockDiscontinuity:kind==1?ManualReloadCancel::SequenceRollback:ManualReloadCancel::GestureRollback));
    }
    return 0;
}
int StaleAndGap(){
    for(bool fresh:{false,true}){
        Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;
        f.sample.nowNs+=101;if(fresh)++f.sample.sequence;
        f.sample.gesture={};f.sample.acknowledgement={request.id,request.owner,request.operation,ReloadAcknowledgement::Applied};
        r=f.policy.Update(f.sample);CHECK(r.cancelled&&!r.acknowledged&&r.reason==ManualReloadCancel::StaleTracking);
    }
    return 0;
}
int AcknowledgementTimeout(){
    auto c=Magazine();c.ackTimeoutNs=30;Fixture f(c);auto r=f.Start();CHECK(r.request);const auto request=*r.request;
    f.Send();f.Send();f.Send();r=f.Ack(request);
    CHECK(r.cancelled&&!r.acknowledged&&r.reason==ManualReloadCancel::AcknowledgementTimeout);return 0;
}
int TransactionTimeout(){
    auto c=Magazine();c.ackTimeoutNs=30;c.transactionTimeoutNs=50;Fixture f(c);auto r=f.Start();CHECK(r.request);
    CHECK(f.Ack(*r.request).acknowledged);f.Send();f.Send();f.Send();f.Send();r=f.Send(true);
    CHECK(r.cancelled&&r.acceptedOperations==1&&r.reason==ManualReloadCancel::TransactionTimeout);return 0;
}
int CompletionLatch(){
    auto c=Magazine();c.stepCount=1;Fixture f(c);auto r=f.Start();CHECK(r.request);const auto request=*r.request;
    r=f.Ack(request);CHECK(r.completed);
    CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,2).request);
    CHECK(!f.Ack(request).completed);f.Send(true);
    CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,2).request);
    r=f.Send(false,ReloadOperation::UnseatMagazine,3);CHECK(r.request&&r.request->id>request.id);return 0;
}
int ResetNeverReusesRequest(){
    Fixture f;auto r=f.Start();CHECK(r.request);const auto request=*r.request;
    r=f.policy.Reset();CHECK(r.cancelled&&r.cancelledRequest==request.id);
    CHECK(!f.Ack(request).acknowledged);f.Send(true);r=f.Send(false,ReloadOperation::UnseatMagazine,2);
    CHECK(r.request&&r.request->id>request.id);return 0;
}
int InvalidConfigurations(){
    ManualReload policy({});CHECK(!policy.ValidConfig());ManualReloadSample sample{};
    CHECK(policy.Update(sample).reason==ManualReloadCancel::InvalidConfig);
    for(unsigned kind=0;kind<7;++kind){
        auto c=Magazine();switch(kind){case 0:c.stepCount=9;break;case 1:c.steps[0].repeats=0;break;
        case 2:c.steps[0].operation=static_cast<ReloadOperation>(255);break;case 3:c.maxSampleGapNs=0;break;
        case 4:c.ackTimeoutNs=0;break;case 5:c.transactionTimeoutNs=c.ackTimeoutNs-1;break;
        default:for(auto& step:c.steps)step={ReloadOperation::InsertRound,32};c.stepCount=3;break;}
        CHECK(!ManualReload(c).ValidConfig());
    }
    return 0;
}
int InvalidSample(){
    for(unsigned kind=0;kind<6;++kind){
        Fixture f;auto r=f.Start();CHECK(r.request);++f.sample.sequence;++f.sample.nowNs;
        switch(kind){case 0:f.sample.owner.weapon=0;break;case 1:f.sample.sequence=0;break;
        case 2:f.sample.nowNs=0;break;case 3:f.sample.gesture={0,ReloadOperation::InsertRound};break;
        case 4:f.sample.gesture={9,ReloadOperation::None};break;default:f.sample.neutral=true;break;}
        r=f.policy.Update(f.sample);CHECK(r.cancelled&&r.reason==ManualReloadCancel::InvalidSample&&!r.request);
    }
    return 0;
}
int DefaultBindingDisabled(){
    Fixture f;f.sample.bindingsVerified=false;
    CHECK(f.Send(true).reason==ManualReloadCancel::UnverifiedBinding);
    CHECK(!f.Send(false,ReloadOperation::UnseatMagazine,1).request);return 0;
}
}
int main(){
    if(MagazineOrder()||RepeatedShells()||BreechSequence()||StartupHeld()||DuplicateGesture()||
       DuplicatePacketCannotRearm()||PendingDoesNotQueue()||WrongOperation()||ExactAcknowledgement()||
       RejectionPreservesProgress()||IdentityChange()||TrackingAndBindingLoss()||CancellationBeatsAcknowledgement()||
       Rollbacks()||StaleAndGap()||AcknowledgementTimeout()||TransactionTimeout()||CompletionLatch()||
       ResetNeverReusesRequest()||InvalidConfigurations()||InvalidSample()||DefaultBindingDisabled())return 1;
    std::puts("ManualReload: 22 deterministic cases passed (no native/runtime binding).");return 0;
}
