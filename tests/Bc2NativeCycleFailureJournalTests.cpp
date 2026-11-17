#include "Bc2NativeCycleFailureJournal.h"
#include "Bc2NativeCycleDispatch.h"
#include "Test.h"
#include <barrier>
#include <cstring>
#include <limits>
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
ReloadFlowRecord RestoreEvent(unsigned seed=1){
    ReloadFlowRecord r;r.id=seed;r.finished=true;r.identityRetained=true;
    auto& a=r.entry;a.kind=ReloadFlowEvent::Restore;a.nativeInvocation=seed;a.thread=19;a.depth=1;
    a.caller=0x551100;a.context=0x661100;a.nowNs=1000000000;a.tickMs=1000;
    a.boundary.owner={0x10000,0x20000,0x30000,0x40000,2,2,3};
    a.boundary.snapshotSequence=99;a.boundary.firing=0x50000;a.boundary.wrapperOffset=0x40;
    a.boundary.branch=1;a.boundary.current=2;a.boundary.previous=1;a.boundary.next=2;
    a.boundary.loaded=5;a.boundary.reserve=24;a.snapshotCopied=true;
    const auto put=[&](std::size_t at,const auto& value){std::memcpy(a.copiedSnapshot.data()+at,&value,sizeof(value));};
    put(0,2u);put(4,2u);put(8,0.f);put(0x18,5);put(0x1c,24);
    auto& b=r.exit;b.thread=a.thread;b.nowNs=a.nowNs+1000;b.tickMs=a.tickMs;
    b.boundary=a.boundary;b.boundary->previous=2;b.snapshotCopied=true;b.copiedSnapshot=a.copiedSnapshot;
    return r;
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
    CHECK(stream.str().find("\"triggering_event\":null")!=std::string::npos);
    CHECK(stream.str().ends_with("}\n"));return 0;
}
int FirstEventIsAnIndependentValueCopy(){
    NativeCycleFailureJournal journal;auto first=Failure(8748);first.view.failure=Bc2NativeCycleFailure::Restore;
    first.previousPhase=Bc2NativeCyclePhase::Complete;first.policyAccess=unsigned(NativeCycleAccess::NativeRestore);
    auto event=RestoreEvent(8748);CHECK(ReloadRestoreMatched(event));
    Bc2NativeCycleDiagnostic before;before.recoveryActive=true;before.concurrentCycle=false;
    before.releasePresent=true;before.readyPresent=true;before.firedMask=before.heldMask=before.readyMask=7;
    before.tail={3,3,3};before.cycleState={2,2,2};before.lastInvocation={8746,8747,8745};
    before.openUpdate={0,0,8745};before.finishedAt={900,901,902};
    before.beganNs=100;before.deadlineNs=100000;before.releaseSubmittedNs=200;before.releaseObservedNs=190;
    auto after=before;after.readyPresent=false;after.heldMask=after.readyMask=0;after.openUpdate={};
    CHECK(journal.Capture(first,&event,&before,&after)&&journal.Published());
    // Both the caller's record and later teardown can change immediately after capture.
    event.id=10000;event.entry.boundary.loaded=99;event.exit.boundary->current=7;
    event.entry.copiedSnapshot.fill(std::byte{0xff});event.exit.copiedSnapshot.fill(std::byte{0xaa});
    first.control.sequence=0;first.policyAccess=0;
    before={};after={};
    CHECK(!journal.Capture(Failure(10000),&event));
    const auto got=journal.Read();CHECK(got&&got->triggeringEvent);
    const auto& kept=*got->triggeringEvent;
    CHECK(kept.id==8748&&kept.entry.nativeInvocation==8748&&kept.entry.boundary.loaded==5);
    CHECK(kept.exit.boundary&&kept.exit.boundary->current==2&&ReloadRestoreMatched(kept));
    CHECK(got->control.sequence==8748*4&&got->policyAccess==unsigned(NativeCycleAccess::NativeRestore));
    CHECK(got->policyBefore&&got->policyBefore->recoveryActive&&got->policyBefore->readyMask==7);
    CHECK(got->policyBefore->openUpdate[2]==8745&&got->policyBefore->releaseObservedNs==190);
    CHECK(got->policyAfter&&!got->policyAfter->readyPresent&&got->policyAfter->heldMask==0);
    std::ostringstream out;NativeCycleFailureJournal::WriteJson(out,*got);const auto json=out.str();
    CHECK(json.find("\"matches_failure_invocation\":true")!=std::string::npos);
    CHECK(json.find("\"restore_matched\":true")!=std::string::npos);
    CHECK(json.find("\"snapshot_unchanged\":true")!=std::string::npos);
    CHECK(json.find("\"snapshot_sequence\":99")!=std::string::npos);
    CHECK(json.find("\"recovery_active\":true")!=std::string::npos);
    CHECK(json.find("\"last_invocation\":[8746,8747,8745]")!=std::string::npos);
    CHECK(json.find("\"hex\":\"020000000200000000000000")!=std::string::npos);
    CHECK(json.find("\"fnv1a64\":\"")!=std::string::npos&&json.size()<8192);
    // Never attach an unrelated later event to a first failure without one.
    NativeCycleFailureJournal noEvent;CHECK(noEvent.Capture(Failure(1)));
    CHECK(!noEvent.Capture(Failure(2),&event)&&!noEvent.Read()->triggeringEvent);return 0;
}
int RejectedAndIncompleteEventsRemainDiagnostic(){
    auto event=RestoreEvent(12);event.exit.copiedSnapshot[0x20]=std::byte{1};
    event.exit.boundary->timer=std::numeric_limits<float>::quiet_NaN();
    NativeCycleFailureJournal changed;CHECK(changed.Capture(Failure(12),&event));
    std::ostringstream out;NativeCycleFailureJournal::WriteJson(out,*changed.Read());const auto json=out.str();
    CHECK(json.find("\"snapshot_unchanged\":false")!=std::string::npos);
    CHECK(json.find("\"restore_matched\":false")!=std::string::npos);
    CHECK(json.find("\"timer\":null,\"timer_bits\":")!=std::string::npos);
    // An entry-time Update failure has no invented end boundary or snapshot.
    event={};event.id=13;event.entry.kind=ReloadFlowEvent::Update;event.entry.nativeInvocation=13;
    event.entry.nativeParent=11;event.entry.nativeUpdate=13;event.entry.parent=11;event.entry.update=13;
    event.entry.contextCopied=true;event.entry.copiedContext[0x28]=std::byte{1};
    NativeCycleFailureJournal partial;CHECK(partial.Capture(Failure(14),&event));
    const auto kept=partial.Read();CHECK(kept&&kept->triggeringEvent&&!kept->triggeringEvent->finished);
    CHECK(kept->triggeringEvent->entry.copiedContext[0x28]==std::byte{1});
    std::ostringstream partialOut;NativeCycleFailureJournal::WriteJson(partialOut,*kept);const auto partialJson=partialOut.str();
    CHECK(partialJson.find("\"matches_failure_invocation\":false")!=std::string::npos);
    CHECK(partialJson.find("\"native_parent\":11,\"native_update\":13")!=std::string::npos);
    CHECK(partialJson.find("\"boundary\":null")!=std::string::npos);
    CHECK(partialJson.find("\"copied\":false,\"hex\":null,\"fnv1a64\":null")!=std::string::npos);return 0;
}
int FirstAdmissionKeepsOriginalScopeGuardsAndSourceReads(){
    Bc2NativeCycleView original;original.phase=Bc2NativeCyclePhase::Cancelled;original.failure=Bc2NativeCycleFailure::Owner;
    original.native.owner={0x10000,0x20000,0x30000,0x40000,2,3,4};original.native.firing={0x50000,0x60000,0x70000};
    original.loaded=4;original.reserve=45;original.capacity=5;original.cycle=1;original.shot=2;
    original.failureInvocation=60843;original.blocksFire=true;const NativeCycleFailureOutcome outcome=original;
    CHECK(outcome.phase==original.phase&&outcome.failure==original.failure&&outcome.native==original.native);
    CHECK(outcome.loaded==4&&outcome.reserve==45&&outcome.capacity==5&&outcome.cycle==1&&outcome.shot==2);
    CHECK(outcome.failureInvocation==60843&&outcome.blocksFire);
    NativeCycleFailureJournal journal;auto first=Failure(60843);first.previousPhase=Bc2NativeCyclePhase::Advancing;
    auto event=RestoreEvent(60843);event.entry.kind=ReloadFlowEvent::Update;event.entry.nativeUpdate=60843;
    event.entry.contextCopied=true;event.entry.copiedContext[0x26]=std::byte{1};event.finished=false;
    NativeCycleRecoveryAdmissionDiagnostic d;d.reason=NativeCycleAdmissionReason::InvocationOrder;d.invocation=60843;
    d.scopePresent=d.sourcePresent=d.active=true;d.expected.identity=first.view.native;
    d.expected.config.assetName[0]='M';d.expected.config.assetPath[191]='Z';d.expected.config.baseCapacity=5;
    d.expected.config.boltTime=std::bit_cast<float>(std::uint32_t{0x7fc01234});
    d.supplied=d.expected;d.supplied.identity.owner.equipGeneration=9;d.supplied.config.assetPath[191]='Y';
    d.open={0,17,0};d.last={51290,51288,60843};d.deadlineNs=329026095433300;
    d.beganNs=329010524499900;d.paidNativeFloorNs=329025995434100;d.acknowledgedAtNs=329025995434000;
    d.capacity=6;d.expectedCapacity=5;d.expectedLoaded=4;d.expectedReserve=45;d.paidRecoveryDrain=60840;
    d.profile={true,false,0,2.3f};auto& s=d.source;s.firstFailed=NativeCycleSourcePredicate::Current;
    s.evaluated=(1u<<12)-2;s.passed=(1u<<11)-2;s.caller=0x6ec72f;s.expectedCaller=0x6ec72f;
    s.context=0x24000;s.stackLow=0x20000;s.stackHigh=0x30000;s.capacity=5;s.baseCapacity=5;
    s.currentNowNs=329025995434200;s.leaseObservedNs=329025910361900;s.leaseDeadlineNs=329026095433300;
    s.leaseRevision=17;s.currentRevision=18;s.currentRevisionRead=s.currentEnabled=s.memoryReadAvailable=true;
    s.capacityAttempted=s.capacityRead=15;s.capacityWords={0x3f800000,0xffffffff,0x3f800000,0xffffffff};
    s.configAttempted=7;s.configRead=3;s.configWords[0]=0x11223344;s.configWords[1]=0xaabbccdd;
    CHECK(journal.Capture(first,&event,nullptr,nullptr,&d));d={};event.entry.copiedContext={};
    CHECK(!journal.Capture(Failure(70000),nullptr,nullptr,nullptr,&d));const auto kept=journal.Read();
    CHECK(kept&&kept->admission&&kept->triggeringEvent);const auto& saved=*kept->admission;
    CHECK(saved.reason==NativeCycleAdmissionReason::InvocationOrder&&saved.invocation==60843);
    CHECK(saved.open[1]==17&&saved.last[2]==60843&&saved.paidRecoveryDrain==60840);
    CHECK(saved.supplied.config.assetPath[191]=='Y'&&saved.expected.config.assetPath[191]=='Z');
    CHECK(saved.supplied.identity.owner.equipGeneration==9&&saved.expected.config.baseCapacity==5);
    CHECK(std::bit_cast<std::uint32_t>(saved.expected.config.boltTime)==0x7fc01234);
    CHECK(saved.source.currentRevision==18&&saved.source.leaseRevision==17&&saved.source.configRead==3);
    CHECK(saved.source.configWords[0]==0x11223344&&saved.source.configWords[1]==0xaabbccdd);
    CHECK(kept->triggeringEvent->entry.copiedContext[0x26]==std::byte{1});
    std::ostringstream output;NativeCycleFailureJournal::WriteJson(output,*kept);const auto json=output.str();
    CHECK(json.find("\"reason\":\"invocation_order\",\"invocation\":60843")!=std::string::npos);
    CHECK(json.find("\"open\":[0,17,0],\"last\":[51290,51288,60843]")!=std::string::npos);
    CHECK(json.find("\"capacity\":6,\"expected_loaded\":4,\"expected_reserve\":45,\"expected_capacity\":5")!=std::string::npos);
    CHECK(json.find("\"first_failed\":\"current\"")!=std::string::npos);
    CHECK(json.find("\"current_now_ns\":329025995434200")!=std::string::npos);
    CHECK(json.find("\"config_words\":[287454020,2864434397,0")!=std::string::npos);
    CHECK(json.find("nan")==std::string::npos&&json.size()<16384&&json.ends_with("}\n"));
    return 0;
}
int ConcurrentPublicationHasNoPartialValue(){
    for(unsigned round=0;round<30;++round){
        NativeCycleFailureJournal journal;std::barrier start(9);std::vector<std::jthread> writers;
        std::atomic<unsigned> won=0;std::atomic<bool> invalid=false;
        for(unsigned seed=1;seed<=8;++seed)writers.emplace_back([&,seed]{auto event=RestoreEvent(seed);start.arrive_and_wait();if(journal.Capture(Failure(seed),&event))++won;});
        start.arrive_and_wait();
        for(unsigned read=0;read<1000;++read)if(auto f=journal.Read()){
            const auto seed=f->view.failureInvocation;
            if(!seed||seed>8||f->view.cycle!=seed*2||f->view.shot!=seed*3||f->control.sequence!=seed*4||
               f->observedNs!=1000000000+std::int64_t(seed)||f->view.reserve!=24||!f->triggeringEvent||
               f->triggeringEvent->id!=seed||!ReloadRestoreMatched(*f->triggeringEvent))invalid=true;
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
int main(){if(FirstCauseSurvivesTeardown()||FirstEventIsAnIndependentValueCopy()||RejectedAndIncompleteEventsRemainDiagnostic()||
    FirstAdmissionKeepsOriginalScopeGuardsAndSourceReads()||ConcurrentPublicationHasNoPartialValue()||BusyReadAndUndispatchedControlPreserveDebt())return 1;
    std::puts("First native-cycle failure retained; concurrent readers observe an immutable complete value.");return 0;}
