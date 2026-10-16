#include "Bc2NativeCycleFailureJournal.h"
#include "Bc2NativeCycleDispatch.h"
#include "Test.h"
#include <barrier>
#include <sstream>
#include <thread>
#include <vector>
using namespace fvr::bc2;
NativeCycleFirstFailure Failure(unsigned seed=1){
    NativeCycleFirstFailure f;f.view.phase=Bc2NativeCyclePhase::Cancelled;f.view.failure=Bc2NativeCycleFailure::Owner;
    f.previousPhase=Bc2NativeCyclePhase::Held;f.observedNs=1000000000+seed;
    f.view.failureInvocation=seed;f.view.cycle=seed*2;f.view.shot=seed*3;f.view.blocksFire=true;
    f.view.loaded=7;f.view.reserve=24;f.view.capacity=8;f.control.sequence=seed*4;
    f.control.observedNs=900000000;f.control.deadlineNs=1100000000;f.control.nowNs=f.observedNs;
    f.control.focused=true;f.control.tracked={true,true};f.control.released={false,false};return f;
}
int FirstCauseSurvivesTeardown(){
    NativeCycleFailureJournal journal;CHECK(!journal.Read());auto invalid=Failure();invalid.view.phase=Bc2NativeCyclePhase::Watching;
    CHECK(!journal.Capture(invalid)&&!journal.Read());
    auto first=Failure(7);CHECK(journal.Capture(first));auto cleanup=Failure(100);cleanup.view.failure=Bc2NativeCycleFailure::Control;
    CHECK(!journal.Capture(cleanup));const auto got=journal.Read();CHECK(got&&got->view.failureInvocation==7);
    CHECK(got->view.failure==Bc2NativeCycleFailure::Owner&&got->previousPhase==Bc2NativeCyclePhase::Held);
    CHECK(got->observedNs==first.observedNs&&got->control.deadlineNs==first.control.deadlineNs);
    std::ostringstream stream;NativeCycleFailureJournal::WriteJson(stream,*got);
    CHECK(stream.str().find("\"invocation\":7,")!=std::string::npos);
    CHECK(stream.str().find("\"tracked\":[true,true]")!=std::string::npos);
    CHECK(stream.str().ends_with("}}\n"));return 0;
}
int ConcurrentPublicationHasNoPartialValue(){
    for(unsigned round=0;round<30;++round){
        NativeCycleFailureJournal journal;std::barrier start(9);std::vector<std::jthread> writers;
        std::atomic<unsigned> won=0;std::atomic<bool> invalid=false;
        for(unsigned seed=1;seed<=8;++seed)writers.emplace_back([&,seed]{start.arrive_and_wait();if(journal.Capture(Failure(seed)))++won;});
        start.arrive_and_wait();
        for(unsigned read=0;read<1000;++read)if(auto f=journal.Read()){
            const auto seed=f->view.failureInvocation;
            if(!seed||seed>8||f->view.cycle!=seed*2||f->view.shot!=seed*3||f->control.sequence!=seed*4||
               f->observedNs!=1000000000+std::int64_t(seed)||f->view.reserve!=24)invalid=true;
        }
        writers.clear();CHECK(won==1&&!invalid&&journal.Read());
    }return 0;
}
int BusyReadAndUndispatchedControlPreserveDebt(){
    // Exercise the exact runtime gate, including its real deadline branch.
    // All observation/control sites remain retryable; missed native callbacks
    // still invalidate observation because native simulation could have advanced.
    for(unsigned value=0;value<=unsigned(NativeCycleAccess::ExplicitCancellation);++value){
        const auto access=NativeCycleAccess(value);std::atomic_flag gate=ATOMIC_FLAG_INIT;
        std::atomic<std::uint64_t> epoch=17;gate.test_and_set();std::int64_t now=1000000;
        NativeCycleContentionJournal journal;
        {
            NativeCyclePolicyAccess lock(gate,epoch,access,[&]{return now;},[&]{now+=ReloadPolicyLock::WaitNs;});
            CHECK(!lock.Held()&&lock.Evidence().failure==ReloadPolicyLockFailure::Deadline);
            const auto expected=17u+(NativeCycleAccessRequiresCancellation(access)?1u:0u);
            CHECK(epoch==expected&&lock.Before()==17&&lock.After()==expected);
            CHECK(journal.Capture({access,lock.Evidence(),lock.Before(),lock.After(),91,0x12340,7}));
        }
        // The unsuccessful waiter cannot clear another thread's ownership.
        CHECK(gate.test_and_set());gate.clear();
        {
            NativeCyclePolicyAccess retry(gate,epoch,access,[&]{return now;},[]{});
            CHECK(retry.Held());
            CHECK(!journal.Capture({access,retry.Evidence(),0,0,0,0,0}));
        }
        const auto first=journal.Read();CHECK(first&&first->invocation==91&&first->access==access);
        CHECK(first->firing==0x12340&&first->cancelBefore==17);
        std::ostringstream out;NativeCycleContentionJournal::WriteJson(out,*first);
        CHECK(out.str().find("\"invocation\":91,")!=std::string::npos);
    }
    // A brief overlap that resolves within budget is not a missed callback.
    std::atomic_flag gate=ATOMIC_FLAG_INIT;gate.test_and_set();std::atomic<std::uint64_t> epoch=4;
    std::int64_t now=100;
    NativeCyclePolicyAccess recovered(gate,epoch,NativeCycleAccess::NativeUpdate,
        [&]{return ++now;},[&]{gate.clear();});
    CHECK(recovered.Held()&&recovered.Evidence().contended&&epoch==4);return 0;
}
int main(){if(FirstCauseSurvivesTeardown()||ConcurrentPublicationHasNoPartialValue()||BusyReadAndUndispatchedControlPreserveDebt())return 1;
    std::puts("First native-cycle failure retained; concurrent readers observe an immutable complete value.");return 0;}
