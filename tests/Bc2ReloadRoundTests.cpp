#include "Test.h"
#include "Bc2ReloadRound.h"
#include <limits>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
struct Fixture {
    ReloadRoundLease lease{};ManualReloadRequest request{};
    Fixture(){lease.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};lease.identity.firing={0x50000,0x60000,0x70000};
        lease.identity.serverPlayer=0x80000;lease.identity.serverSoldier=0x90000;lease.identity.serverItem=0xa0000;
        lease.cycle=10;lease.sequence=20;lease.observedNs=1000;lease.deadlineNs=100001000;lease.loaded=2;lease.reserve=8;lease.capacity=8;
        lease.nativeBindingVerified=lease.allThreeHeld=true;request={1,{0x20000,1,0x40000,2,3},ReloadOperation::InsertRound,0,0};}
    bool Begin(ReloadRoundCompletion& c){return c.Begin(request,lease,2000,1000000000);}
    ReloadRoundTransfer Transfer(unsigned branch){return {lease.identity,lease.cycle,100+branch,3000,4000,branch,2,8,3,7,true,true};}
    ReloadRoundSample Sample(){ReloadRoundSample s;s.identity=lease.identity;s.cycle=lease.cycle;s.sequence=21;s.observedNs=5000;s.deadlineNs=100005000;
        s.nativeBindingVerified=s.allThreeHeld=true;
        for(unsigned n=0;n<3;++n){auto& b=s.branches[n];b.address=lease.identity.firing[n];b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;b.loaded=3;b.reserve=7;b.currentState=11;b.nextState=12;b.phaseTimer=.2f;}return s;}
};
int ExactOnceAfterServerAndSettledCopies(){Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(0))&&c.AdvancedMask()==1);
    CHECK(c.Observe(f.Transfer(1))&&c.AdvancedMask()==3);CHECK(!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));auto s=f.Sample();CHECK(!c.Observe(s,5001));
    CHECK(c.Observe(f.Transfer(2)));CHECK(c.Observe(f.Transfer(2)));CHECK(!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));++s.sequence;s.observedNs=6000;
    CHECK(c.Observe(s,6001)&&c.Phase()==ReloadRoundPhase::Complete);const auto ack=c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000);CHECK(ack&&ack->semantic.request==1&&ack->semantic.owner==f.request.owner&&ack->semantic.operation==ReloadOperation::InsertRound&&ack->semantic.status==ReloadAcknowledgement::Applied&&ack->cycle==f.lease.cycle&&ack->identity==f.lease.identity&&ack->serverInvocation==102&&ack->sampleSequence==s.sequence);
    CHECK(!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000)&&!f.Begin(c));return 0;}
int ClientRestoreIsNotServerAuthority(){Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));auto s=f.Sample();CHECK(!c.Observe(s,5001));CHECK(c.AdvancedMask()==7&&!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));
    CHECK(c.Observe(f.Transfer(2)));++s.sequence;s.observedNs=6000;CHECK(c.Observe(s,6001)&&c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));return 0;}
int IdentityExpiryAndNoDoubleRound(){for(unsigned test=0;test<8;++test){Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));auto e=f.Transfer(2);
    switch(test){case 0:e.identity.owner.space++;break;case 1:e.cycle++;break;case 2:e.endNs=1000000000;break;case 3:e.identityRetained=false;break;
        case 4:e.ordinaryState12Verified=false;break;case 5:e.loadedAfter++;break;case 6:e.reserveAfter++;break;case 7:e.invocation=0;break;}
    CHECK(!c.Observe(e)&&c.Phase()==ReloadRoundPhase::Failed&&!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));}
    Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));auto e=f.Transfer(2);CHECK(c.Observe(e));++e.invocation;CHECK(!c.Observe(e)&&c.Failure()==ReloadRoundFailure::RepeatedTransfer);return 0;}
int RevertedCopyAndUnknownSettlement(){Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(0)));auto s=f.Sample();s.branches[0].loaded=2;s.branches[0].reserve=8;
    CHECK(!c.Observe(s,5001)&&c.Failure()==ReloadRoundFailure::Reverted);
    ReloadRoundCompletion pending(true);CHECK(f.Begin(pending));CHECK(pending.Observe(f.Transfer(2)));s=f.Sample();s.allThreeHeld=false;CHECK(!pending.Observe(s,5001)&&!pending.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));
    ++s.sequence;s.allThreeHeld=true;s.branches[0].currentState=12;CHECK(!pending.Observe(s,5001)&&pending.Failure()==ReloadRoundFailure::NotSettled);return 0;}
int FullWeaponCanFinishNativeReturn(){Fixture f;f.lease.loaded=7;f.lease.reserve=1;ReloadRoundCompletion c(true);CHECK(f.Begin(c));auto e=f.Transfer(2);e.loadedBefore=7;e.reserveBefore=1;e.loadedAfter=8;e.reserveAfter=0;CHECK(c.Observe(e));
    auto s=f.Sample();s.allThreeHeld=false;for(auto& b:s.branches){b.loaded=8;b.reserve=0;b.currentState=b.nextState=2;}CHECK(c.Observe(s,5001)&&c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));return 0;}
int ReserveExhaustionCanFinishNativeReturn(){Fixture f;f.lease.reserve=1;ReloadRoundCompletion c(true);CHECK(f.Begin(c));auto e=f.Transfer(2);e.reserveBefore=1;e.reserveAfter=0;CHECK(c.Observe(e));
    auto s=f.Sample();s.allThreeHeld=false;for(auto& b:s.branches){b.reserve=0;b.currentState=1;b.nextState=2;}CHECK(c.Observe(s,5001));CHECK(c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));return 0;}
int AckCannotOutliveCurrentOwnerOrFreshness(){for(unsigned test=0;test<4;++test){Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(2)));CHECK(c.Observe(f.Sample(),5001));
    auto identity=f.lease.identity;auto cycle=f.lease.cycle;std::int64_t now=7000;
    switch(test){case 0:identity.owner.equipGeneration++;break;case 1:cycle++;break;case 2:now=100005000;break;case 3:now=4999;break;}
    CHECK(!c.TakeAcknowledgement(identity,cycle,now)&&c.Phase()==ReloadRoundPhase::Failed);}
    return 0;}
int FreshCompleteEvidenceAndNewRequest(){
    for(unsigned test=0;test<8;++test){Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(2)));auto s=f.Sample();
        switch(test){case 0:s.branches[1].address++;break;case 1:s.branches[1].wrapperOffset=0x3c;break;case 2:s.sequence=19;break;case 3:s.nativeBindingVerified=false;break;
            case 4:s.branches[0].loaded++;break;case 5:s.branches[1].phaseTimer=std::numeric_limits<float>::quiet_NaN();break;case 6:s.branches[2].flagsA8=8;break;case 7:s.observedNs=1000;break;}
        CHECK(!c.Observe(s,5001)&&c.Phase()==ReloadRoundPhase::Failed&&!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));}
    Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(2)));CHECK(c.Observe(f.Sample(),5001));CHECK(c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,7000));
    ++f.request.id;++f.lease.cycle;f.lease.loaded=3;f.lease.reserve=7;f.lease.observedNs=8000;f.lease.deadlineNs=100008000;f.lease.sequence=22;
    CHECK(c.Begin(f.request,f.lease,9000,1000000000));auto e=f.Transfer(2);e.beginNs=10000;e.endNs=11000;e.loadedBefore=3;e.reserveBefore=7;e.loadedAfter=4;e.reserveAfter=6;
    CHECK(c.Observe(e));auto s=f.Sample();s.observedNs=12000;s.deadlineNs=100012000;s.sequence=23;for(auto& b:s.branches){b.loaded=4;b.reserve=6;}
    CHECK(c.Observe(s,12001));const auto ack=c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,13000);CHECK(ack&&ack->semantic.request==2&&ack->cycle==11);return 0;}
int DefaultOffAndRequestLeaseRejection(){Fixture f;ReloadRoundCompletion off;CHECK(!f.Begin(off));
    for(unsigned test=0;test<7;++test){Fixture g;ReloadRoundCompletion c(true);switch(test){case 0:g.request.operation=ReloadOperation::SeatMagazine;break;case 1:g.request.owner.actorGeneration++;break;case 2:g.lease.allThreeHeld=false;break;
        case 3:g.lease.nativeBindingVerified=false;break;case 4:g.lease.loaded=8;break;case 5:g.lease.reserve=0;break;case 6:g.lease.deadlineNs=1500;break;}CHECK(!g.Begin(c));}return 0;}
ReloadPredictionRestore Restore(const Fixture& f,unsigned branch){return {f.lease.identity,f.lease.cycle,200,4500,4600,branch,0x8bb438,3,7,2,8,true,true};}
int ExactClientRestoreRetainsOneShellTransaction(){
    for(unsigned branch:{0u,1u}){
        Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(branch)));
        const auto restore=Restore(f,branch);CHECK(c.ObservePredictionRestore(restore));
        CHECK(c.AdvancedMask()==0&&!c.ObservePredictionRestore(restore));
        auto sample=f.Sample();sample.branches[branch].loaded=2;sample.branches[branch].reserve=8;
        CHECK(!c.Observe(sample,5001)&&c.Phase()==ReloadRoundPhase::Awaiting);
        CHECK(!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,5001));
        auto replay=f.Transfer(branch);replay.invocation=201;replay.beginNs=5100;replay.endNs=5200;
        CHECK(c.Observe(replay));CHECK(c.Observe(replay));
        CHECK(!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,5300));
        auto server=f.Transfer(2);server.invocation=202;server.beginNs=5400;server.endNs=5500;CHECK(c.Observe(server));
        sample=f.Sample();++sample.sequence;sample.observedNs=5600;
        CHECK(c.Observe(sample,5601));const auto ack=c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,5700);
        CHECK(ack&&ack->serverInvocation==202&&ack->semantic.request==f.request.id);
        CHECK(!c.TakeAcknowledgement(f.lease.identity,f.lease.cycle,5700));
        CHECK(!c.ObservePredictionRestore(restore));
    }
    return 0;
}
int ClientRestoreCannotForgeCompletionOrRenewAuthority(){
    for(unsigned mutation=0;mutation<16;++mutation){
        Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(1)));
        auto r=Restore(f,1);
        switch(mutation){
            case 0:r.branch=2;break;case 1:r.identity.owner.equipGeneration++;break;case 2:r.cycle++;break;
            case 3:r.invocation=0;break;case 4:r.caller=0;break;case 5:r.immutableSnapshotMatched=false;break;
            case 6:r.identityRetained=false;break;case 7:r.beginNs=1999;break;case 8:r.endNs=r.beginNs-1;break;
            case 9:r.endNs=1000000000;break;case 10:r.branch=0;break;case 11:r.invocation=101;break;
            case 12:r.beginNs=3999;break;case 13:r.loadedBefore++;break;case 14:r.reserveAfter--;break;
            case 15:CHECK(c.Observe(f.Transfer(2)));break;
        }
        CHECK(!c.ObservePredictionRestore(r));CHECK(c.AdvancedMask()&2u);
    }
    for(unsigned stale=0;stale<2;++stale){
        Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(1)));
        CHECK(c.ObservePredictionRestore(Restore(f,1)));auto old=f.Transfer(1);
        if(stale){old.invocation=201;old.beginNs=4599;old.endNs=4700;}
        CHECK(!c.Observe(old)&&c.Failure()==ReloadRoundFailure::InvalidEvidence);
    }
    Fixture f;ReloadRoundCompletion c(true);CHECK(f.Begin(c));CHECK(c.Observe(f.Transfer(1)));
    CHECK(c.ObservePredictionRestore(Restore(f,1)));auto late=f.Transfer(1);
    late.invocation=201;late.beginNs=999999999;late.endNs=1000000000;
    CHECK(!c.Observe(late)&&c.Failure()==ReloadRoundFailure::Expired);
    return 0;
}
}
int main(){if(ExactClientRestoreRetainsOneShellTransaction()||ClientRestoreCannotForgeCompletionOrRenewAuthority()||ExactOnceAfterServerAndSettledCopies()||ClientRestoreIsNotServerAuthority()||IdentityExpiryAndNoDoubleRound()||RevertedCopyAndUnknownSettlement()||FullWeaponCanFinishNativeReturn()||ReserveExhaustionCanFinishNativeReturn()||AckCannotOutliveCurrentOwnerOrFreshness()||FreshCompleteEvidenceAndNewRequest()||DefaultOffAndRequestLeaseRejection())return 1;
    std::printf("Eleven single-round completion groups passed; no runtime capability enabled.\n");return 0;}
