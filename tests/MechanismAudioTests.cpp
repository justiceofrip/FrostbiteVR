#include "fvr/interaction/MechanismAudio.h"
#include <iostream>
using namespace fvr::interaction;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";return 1;}}while(false)
int main(){
    const MechanismAudioIdentity playing{1,2,3,4,5,6};
    auto observed=playing;observed.cycle=observed.shot=0;
    CHECK(MechanismAudioSourceCurrent(observed,playing));
    observed.cycle=playing.cycle;CHECK(MechanismAudioSourceCurrent(observed,playing));
    observed.shot=playing.shot+1;CHECK(!MechanismAudioSourceCurrent(observed,playing));
    observed=playing;observed.cycle=playing.cycle+1;CHECK(!MechanismAudioSourceCurrent(observed,playing));
    observed=playing;observed.itemGeneration++;CHECK(!MechanismAudioSourceCurrent(observed,playing));
    observed=playing;observed.actorGeneration++;CHECK(!MechanismAudioSourceCurrent(observed,playing));
    CHECK(!MechanismAudioSourceCurrent({},playing));
    MechanismAudioRoutePolicy route;
    CHECK(!route.Fresh(1000000000));CHECK(route.Open(1000000000));
    CHECK(route.Fresh(1099999999)&&!route.CheckDue(1099999999));
    CHECK(route.CheckDue(1100000000)&&route.Validate(1100000000,true));
    CHECK(!route.Validate(1110000000,false));
    CHECK(!route.Fresh(1110000000)&&!route.Validate(1120000000,true)&&!route.Open(1120000000));
    MechanismAudioRoutePolicy expired;CHECK(expired.Open(1000000000));
    CHECK(!expired.Fresh(1250000000)&&!expired.Validate(1250000000,true)&&!expired.Open(1250000001));
    CHECK(!MechanismAudioRouteFresh(1000000000,999999999));
    MechanismAudioSuppressionLedger<1> suppressed;
    MechanismAudioStockPassLatch stock;
    stock.ObserveStart(false,false);CHECK(stock.AllowsReplacement());
    stock.ObserveStart(true,true);CHECK(stock.AllowsReplacement());
    stock.ObserveStart(true,false);CHECK(!stock.AllowsReplacement());
    stock.ObserveStart(true,true);CHECK(!stock.AllowsReplacement());
    stock.ObserveStart(false,false);CHECK(!stock.AllowsReplacement());
    stock.ResetSession();CHECK(stock.AllowsReplacement());
    // A stock start that passed through while output was unavailable cannot
    // turn into replacement playback merely because output becomes ready.
    CHECK(!suppressed.Contains(playing));CHECK(route.Fresh(1120000000)==false);
    auto unknown=playing;unknown.cycle=0;CHECK(!suppressed.Record(unknown));
    CHECK(!suppressed.Contains(playing));CHECK(suppressed.Record(playing));CHECK(suppressed.Contains(playing));
    auto next=playing;++next.cycle;++next.shot;
    CHECK(!suppressed.Contains(next));CHECK(suppressed.Record(next));CHECK(!suppressed.Contains(playing));
    CHECK(!suppressed.Record(playing));
    auto mismatch=next;++mismatch.itemGeneration;CHECK(!suppressed.Contains(mismatch));
    ++mismatch.actorGeneration;CHECK(!suppressed.Contains(mismatch));
    suppressed.RetireActor(playing.actor,playing.actorGeneration);CHECK(!suppressed.Contains(next));
    MechanismAudioBufferLease clean;
    CHECK(clean.CanPrepare()&&clean.Prepared(true)&&clean.Submitted(true));
    CHECK(!clean.CanPrepare()&&!clean.CanUnprepare());
    CHECK(clean.Returned(true)&&clean.CanUnprepare()&&clean.Unprepared(true)&&clean.CanPrepare());
    CHECK(clean.Closed(true)&&!clean.CanPrepare());
    MechanismAudioBufferLease writeFailure;
    CHECK(writeFailure.Prepared(true)&&!writeFailure.Submitted(false)&&writeFailure.CanUnprepare());
    CHECK(writeFailure.Unprepared(true)&&writeFailure.Closed(true));
    MechanismAudioBufferLease resetFailure;
    CHECK(resetFailure.Prepared(true)&&resetFailure.Submitted(true)&&!resetFailure.Returned(false));
    CHECK(resetFailure.Retained()&&!resetFailure.CanPrepare()&&!resetFailure.Unprepared(true)&&!resetFailure.Closed(true));
    MechanismAudioBufferLease unprepareFailure;
    CHECK(unprepareFailure.Prepared(true)&&unprepareFailure.Submitted(true)&&unprepareFailure.Returned(true));
    CHECK(!unprepareFailure.Unprepared(false)&&unprepareFailure.Retained()&&!unprepareFailure.CanPrepare());
    MechanismAudioBufferLease closeFailure;
    CHECK(!closeFailure.Closed(false)&&closeFailure.Retained()&&!closeFailure.CanPrepare());
    MechanismAudioLedger<> ledger;
    MechanismAudioEvent e{{1,2,3,4,5,6},MechanismAudioStage::RearTravel,7,1000000000,1100000000,true,true,true};
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Duplicate);
    // Fresh packet/regrip does not replay an already heard stage.
    ++e.sequence;e.observedNs+=1000000;
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Duplicate);
    e.stage=MechanismAudioStage::Closure;e.tracked=false;
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Invalid);
    e.tracked=true;
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Duplicate);
    // Older debt cannot be revived with a newly stamped packet.
    --e.identity.cycle;++e.sequence;
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Stale);
    e.identity.cycle+=2;++e.identity.shot;
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Order);
    e.stage=MechanismAudioStage::RearTravel;
    CHECK(ledger.Accept(e,e.deadlineNs)==MechanismAudioAdmission::Stale);
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
    // A bolt uses the same policy, independent of the pump item's receipt.
    e.identity.item=30;e.identity.itemGeneration=40;e.identity.cycle=1;e.identity.shot=1;
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
    e.stage=MechanismAudioStage::Closure;
    CHECK(ledger.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
    MechanismAudioLedger<1> bounded;e.stage=MechanismAudioStage::RearTravel;
    CHECK(bounded.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
    ++e.identity.item;
    CHECK(bounded.Accept(e,e.observedNs)==MechanismAudioAdmission::Capacity);
    bounded.RetireActor(1,2);
    CHECK(bounded.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
    const auto old=e;
    for(unsigned generation=0;generation<100;++generation){
        ++e.identity.itemGeneration;++e.sequence;++e.observedNs;e.identity.cycle=1;e.identity.shot=1;
        CHECK(bounded.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
        e.stage=MechanismAudioStage::Closure;
        CHECK(bounded.Accept(e,e.observedNs)==MechanismAudioAdmission::Accepted);
        e.stage=MechanismAudioStage::RearTravel;
    }
    auto replay=old;replay.sequence=e.sequence+1;replay.observedNs=e.observedNs+1;
    CHECK(bounded.Accept(replay,replay.observedNs)==MechanismAudioAdmission::Stale);
    std::cout<<"mechanism audio: route expiry, stock suppression receipts, driver buffer ownership, dedup, recovery and capacity passed\n";
}
