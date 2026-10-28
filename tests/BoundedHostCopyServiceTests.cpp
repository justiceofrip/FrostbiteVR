#include "Test.h"
#include "fvr/xr/PrivateHostCopyService.h"
#include "fvr/xr/HostCopyLease.h"
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <iostream>
#endif

using namespace fvr;
using graphics::FrameProgressStop;
using xr::HostCopyWaitResult;

namespace {
struct Provider final:runtime::IFrameProvider {
    unsigned positive=0,negative=0,admissions=0,suspends=0,flushes=0;
    bool gateBusy=true,deferred=false,released=false,queued=true,cancelQueued=false;
    graphics::PairTicket acknowledged{};
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override {++admissions;return false;}
    void PairConsumed(const graphics::PairTicket& ticket,bool consumed)noexcept override {
        acknowledged=ticket;
        if(consumed){++positive;deferred=true;}else ++negative;
    }
    void Suspend()noexcept override {++suspends;cancelQueued=true;}
    void ServiceFeedback()noexcept override {
        ++flushes;if(gateBusy)return;
        if(cancelQueued)queued=false;
        if(deferred){deferred=false;released=true;}
    }
};
graphics::PairTicket Ticket()noexcept {
    graphics::PairTicket t{};t.session[0]=19;t.resourceEpoch=3;t.sequence=42;
    t.frameId=73;t.trackingGeneration=91;t.spaceGeneration=2;t.predictedNs=100000;
    return t;
}
// Exercise the real lease's completion/retirement with deterministic query and
// clock callbacks. No GPU, thread, sleep, IPC admission or native game is used.
struct Fixture {
    Provider provider;
    const graphics::PairTicket expected=Ticket();
    std::unique_ptr<xr::HostCopyLease> lease=std::make_unique<xr::HostCopyLease>(provider,expected);
    unsigned owner=7,context=11,pumps=0,sleeps=0,completeAt=0,faultAt=0;
    unsigned cancelAt=0,pauseAction=0,clockCalls=0,zeroClockAt=0;
    bool allowed=true,backwards=false;
    xr::PrivateHostCopyServiceReport* serviceReport=nullptr;
    void* waitContext=nullptr;
    HostCopyWaitResult(*waitPause)(void*)noexcept=nullptr;
    std::uint64_t tick=10000,sleepTicks=1000,pumpTicks=0;
    graphics::FrameProgressResult Run(std::uint64_t budget=2000)noexcept {
        return xr::ServiceBoundedHostCopy(budget,
            [&]()noexcept{return (!serviceReport||serviceReport->available)&&allowed&&owner==7&&context==11&&lease&&!lease->Cancelled()&&
                !std::memcmp(&lease->ticket,&expected,sizeof(expected));},
            [&]()noexcept{return lease&&lease->Copying();},
            [&]()noexcept{
                ++pumps;tick+=pumpTicks;
                if(pumps==faultAt){lease.reset();return;}
                if(pumps==cancelAt){lease->CancelPresentation();return;}
                if(pumps==completeAt)lease->Complete();
            },
            [&]()noexcept{++clockCalls;return clockCalls==zeroClockAt?std::uint64_t(0):tick;},
            [&]()noexcept{
                ++sleeps;if(backwards)--tick;else tick+=sleepTicks;
                switch(pauseAction){
                case 1:lease->CancelPresentation();break;
                case 2:context=12;break;
                case 3:owner=8;break;
                case 4:lease.reset();break;
                case 5:{auto next=expected;++next.sequence;lease.reset();lease=std::make_unique<xr::HostCopyLease>(provider,next);break;}
                case 6:allowed=false;break;
                default:break;
                }
                return waitPause?waitPause(waitContext):HostCopyWaitResult::Signaled;
            });
    }
};

int ConfigurationAndAdmission(){
    CHECK(xr::ParseHostCopyBudget(L"1")==1&&xr::ParseHostCopyBudget(L"2")==2);
    for(const auto value:{L"",L"0",L"3",L"16",L"01",L" 1",L"1 ",L"+1",L"-1",L"2ms"})
        CHECK(xr::ParseHostCopyBudget(value)==0);
    {Fixture f;const auto r=f.Run(0);CHECK(r.stop==FrameProgressStop::AdmissionLost&&!f.pumps&&!f.sleeps&&!f.clockCalls);}
    {Fixture f;f.allowed=false;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::AdmissionLost&&!f.pumps&&!f.sleeps);}
    {Fixture f;f.lease.reset();const auto r=f.Run();CHECK(r.stop==FrameProgressStop::AdmissionLost&&!f.pumps&&!f.sleeps);}
    return 0;
}

int CompletionOwnsLocalImages(){
    for(unsigned afterSleep=0;afterSleep<=2;++afterSleep){
        Fixture f;f.completeAt=afterSleep+1;f.sleepTicks=500;
        const auto r=f.Run();
        CHECK(r.stop==FrameProgressStop::Completed&&r.pumps==afterSleep+1&&r.sleeps==afterSleep);
        CHECK(f.lease&&f.lease->Ready()&&!f.lease->Copying()&&f.provider.positive==1&&!f.provider.negative);
        CHECK(!std::memcmp(&f.provider.acknowledged,&f.expected,sizeof(f.expected)));
        CHECK(!f.lease->Complete());
        // Completion releases sources exactly once, but Ready/local pixels stay
        // owned. Re-entering service cannot poll them or admit a successor.
        const auto again=f.Run();
        CHECK(again.stop==FrameProgressStop::Completed&&!again.pumps&&!again.sleeps);
        CHECK(f.lease->Ready()&&f.provider.positive==1&&!f.provider.admissions);
        // Deferred feedback remains honest. A subsequent cancel precedes the
        // exact receipt flush and cannot turn a completed read into a rejection.
        f.lease->CancelPresentation();f.provider.gateBusy=false;f.provider.ServiceFeedback();
        CHECK(f.provider.released&&!f.provider.deferred&&!f.provider.queued);
        f.lease.reset();CHECK(f.provider.positive==1&&!f.provider.negative);
    }
    return 0;
}

int BusyDeadlineAndDriverFailure(){
    {
        Fixture f;const auto r=f.Run();
        CHECK(r.stop==FrameProgressStop::BudgetExpired&&r.pumps==2&&r.sleeps==2&&r.ticks==2000);
        CHECK(f.lease&&f.lease->Copying()&&!f.provider.positive&&!f.provider.negative);
        // Exhausting this optional service does not expire/cancel the real copy.
        f.completeAt=f.pumps+1;const auto later=f.Run();
        CHECK(later.stop==FrameProgressStop::Completed&&f.lease->Ready()&&f.provider.positive==1);
        CHECK(!f.provider.admissions);
    }
    {Fixture f;f.sleepTicks=5000;const auto r=f.Run();
        CHECK(r.stop==FrameProgressStop::BudgetExpired&&r.pumps==1&&r.sleeps==1&&r.ticks==5000);
        CHECK(f.lease->Copying()&&!f.provider.positive&&!f.provider.negative);}
    // A slow zero-wait driver call may itself overshoot. Never follow it with
    // another poll/sleep; its genuine success is still acknowledged exactly once.
    for(bool completes:{false,true}){
        Fixture f;f.pumpTicks=5000;f.completeAt=completes?1u:0u;const auto r=f.Run();
        CHECK(r.stop==(completes?FrameProgressStop::Completed:FrameProgressStop::BudgetExpired));
        CHECK(r.pumps==1&&!r.sleeps&&r.ticks==5000);
        CHECK(f.provider.positive==(completes?1u:0u)&&!f.provider.negative);
    }
    for(unsigned failureAt:{1u,2u}){
        Fixture f;f.faultAt=failureAt;const auto r=f.Run();
        CHECK(r.stop==FrameProgressStop::AdmissionLost&&r.pumps==failureAt&&r.sleeps==failureAt-1);
        CHECK(!f.lease&&!f.provider.positive&&f.provider.negative==1&&!f.provider.admissions);
        CHECK(!std::memcmp(&f.provider.acknowledged,&f.expected,sizeof(f.expected)));
    }
    return 0;
}

int ClockAndFiniteWork(){
    {Fixture f;f.zeroClockAt=1;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::ClockFailed&&!r.pumps&&!r.sleeps);}
    {Fixture f;f.zeroClockAt=3;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::ClockFailed&&r.pumps==1&&!r.sleeps);CHECK(f.lease->Copying()&&!f.provider.positive);}
    {Fixture f;f.backwards=true;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::ClockFailed&&r.pumps==1&&r.sleeps==1);}
    {Fixture f;f.sleepTicks=0;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::PollLimit&&r.pumps==3&&r.sleeps==2);CHECK(f.lease->Copying()&&!f.provider.positive);}
    // A clock failure after a genuine query success must not undo ownership.
    {Fixture f;f.completeAt=1;f.zeroClockAt=3;const auto r=f.Run();
        CHECK(r.stop==FrameProgressStop::ClockFailed&&r.pumps==1&&!r.sleeps);
        CHECK(f.lease->Ready()&&f.provider.positive==1&&!f.provider.negative);
        f.lease.reset();CHECK(f.provider.positive==1&&!f.provider.negative);}
    return 0;
}

int CancelAndIdentityChange(){
    for(unsigned cause=1;cause<=6;++cause){
        Fixture f;f.pauseAction=cause;const auto r=f.Run();
        CHECK(r.stop==FrameProgressStop::AdmissionLost&&r.pumps==1&&r.sleeps==1);
        CHECK(!f.provider.positive&&!f.provider.admissions);
        if(cause==1){CHECK(f.lease->Cancelled()&&f.lease->Copying()&&f.provider.suspends==1);}
        if(cause==5){CHECK(f.lease->ticket.sequence==f.expected.sequence+1&&f.lease->Copying());}
    }
    {Fixture f;f.cancelAt=1;const auto r=f.Run();
        CHECK(r.stop==FrameProgressStop::AdmissionLost&&r.pumps==1&&!r.sleeps);
        CHECK(f.lease->Cancelled()&&f.lease->Copying()&&!f.provider.positive);}
    return 0;
}
int ReportAccounting(){
    xr::PrivateHostCopyServiceReport report;
    CHECK(!report.BudgetTicks());
    std::ostringstream off;report.Write(off);
    CHECK(off.str().find("\"enabled\":false")!=std::string::npos);
    report.budgetMs=2;report.available=true;report.frequency=10000003;
    CHECK(report.BudgetTicks()==20000);
    report.Observe({FrameProgressStop::Completed,2,1,13000});
    report.Observe({FrameProgressStop::BudgetExpired,2,2,27000});
    report.Observe({FrameProgressStop::AdmissionLost,1,0,200});
    report.Observe({FrameProgressStop::ClockFailed,0,0,0});
    report.Observe({FrameProgressStop::PollLimit,3,2,0});
    CHECK(report.episodes==5&&report.completed==1&&report.expired==1&&report.admissionLost==1&&report.clockFailed==1&&report.pollLimit==1);
    CHECK(report.polls==8&&report.sleeps==5&&report.totalTicks==40200&&report.maxTicks==27000);
    CHECK(report.overshootEpisodes==1&&report.maxOvershootTicks==7000);
    std::ostringstream out;report.Write(out);const auto json=out.str();
    for(const auto expected:{"\"schema\":\"fvr.host-copy-service.v1\"","\"configured_ms\":2","\"enabled\":true",
            "\"episodes\":5","\"completed\":1","\"expired\":1","\"admission_lost\":1","\"clock_failed\":1",
            "\"poll_limit\":1","\"polls\":8","\"sleeps\":5","\"total_ticks\":40200",
            "\"overshoot_episodes\":1","\"max_overshoot_ticks\":7000","\"completed_means\":\"observed_query_success\"",
            "\"wait_kind\":\"high_resolution_auto_reset_waitable_timer\"","\"timer_created\":false",
            "\"timer_create_failures\":0","\"timer_arm_failures\":0","\"timer_wait_failures\":0",
            "\"timer_wait_timeouts\":0","\"wait_timeout_semantics\":\"episode_expired_copy_retained\"",
            "\"global_timer_policy_changed\":false","\"busy_spin\":false",
            "\"elapsed_semantics\":\"host_elapsed_including_driver_calls_and_wait\""})
        CHECK(json.find(expected)!=std::string::npos);
    report.budgetMs=3;CHECK(!report.BudgetTicks());
    report.budgetMs=1;report.available=false;CHECK(!report.BudgetTicks());
    return 0;
}
struct TimerState {
    unsigned creates=0,arms=0,waits=0,closes=0;
    unsigned failure=0,timeouts=0,clearedLateSignals=0;
    bool active=false,signaled=false;
};
struct FakeTimer {
    TimerState* state=nullptr;
    bool Create()noexcept {++state->creates;return state->failure!=1;}
    bool Arm()noexcept {
        ++state->arms;if(state->failure==2)return false;
        // Model the documented reset of an active or already signaled timer.
        if(state->signaled)++state->clearedLateSignals;
        state->signaled=false;state->active=true;return true;
    }
    HostCopyWaitResult Wait()noexcept {
        ++state->waits;if(state->failure==3)return HostCopyWaitResult::Failed;
        if(state->timeouts){--state->timeouts;return HostCopyWaitResult::TimedOut;}
        state->active=false;state->signaled=false;return HostCopyWaitResult::Signaled;
    }
    std::uint32_t Error()const noexcept {return state->failure==3?6u:5u;}
    void Close()noexcept {++state->closes;}
};
int TimerFailureRetainsAsyncCopy(){
    // Disabled configuration must not create an OS timer, even if queried twice.
    {TimerState state;xr::PrivateHostCopyServiceReport report;report.frequency=1000000;
        {xr::PrivateHostCopyWait<FakeTimer> wait(report,{&state});CHECK(!wait.Initialize());CHECK(!wait.Initialize());wait.Pause();}
        CHECK(!state.creates&&!state.arms&&!state.waits&&!state.closes&&!report.timerCreated);}
    for(unsigned failure=1;failure<=3;++failure){
        TimerState state;state.failure=failure;xr::PrivateHostCopyServiceReport report;report.budgetMs=2;report.frequency=1000000;
        {
            xr::PrivateHostCopyWait<FakeTimer> wait(report,{&state});
            CHECK(wait.Initialize()==(failure!=1));CHECK(wait.Initialize()==(failure!=1));CHECK(state.creates==1);
            Fixture f;f.serviceReport=&report;f.waitContext=&wait;
            f.waitPause=[](void* value)noexcept {return static_cast<xr::PrivateHostCopyWait<FakeTimer>*>(value)->Pause();};
            const auto result=f.Run();report.Observe(result);
            CHECK(result.stop==FrameProgressStop::AdmissionLost&&!report.available);
            CHECK(result.pumps==(failure==1?0u:1u)&&result.sleeps==(failure==1?0u:1u));
            CHECK(f.lease&&f.lease->Copying()&&!f.lease->Cancelled()&&!f.provider.positive&&!f.provider.negative&&!f.provider.admissions);
            CHECK(report.timerCreateFailures==(failure==1?1u:0u)&&report.timerArmFailures==(failure==2?1u:0u)&&report.timerWaitFailures==(failure==3?1u:0u));
            CHECK(report.lastWaitError==(failure==3?6u:5u));CHECK(!report.overshootEpisodes&&!report.timerWaitTimeouts);
            const auto arms=state.arms,waits=state.waits;
            wait.Pause();CHECK(!wait.Initialize());CHECK(state.creates==1&&state.arms==arms&&state.waits==waits);
            // Normal asynchronous completion remains usable after optional
            // servicing failed; failure itself never issued an acknowledgement.
            CHECK(f.lease->Complete()&&f.lease->Ready()&&f.provider.positive==1);
            f.lease.reset();CHECK(!f.provider.negative);
        }
        CHECK(state.closes==(failure==1?0u:1u));
    }
    // One timer is reused, with two waits and a single ACK on the third poll.
    {TimerState state;xr::PrivateHostCopyServiceReport report;report.budgetMs=2;report.frequency=1000000;
        {xr::PrivateHostCopyWait<FakeTimer> wait(report,{&state});CHECK(wait.Initialize());
            Fixture f;f.sleepTicks=500;f.completeAt=3;f.serviceReport=&report;f.waitContext=&wait;
            f.waitPause=[](void* value)noexcept {return static_cast<xr::PrivateHostCopyWait<FakeTimer>*>(value)->Pause();};
            const auto result=f.Run();CHECK(result.stop==FrameProgressStop::Completed&&result.pumps==3&&result.sleeps==2);
            CHECK(state.creates==1&&state.arms==2&&state.waits==2&&!state.closes&&f.provider.positive==1);}
        CHECK(state.closes==1);}
    // Revoking availability must not turn all elapsed time into overshoot.
    {xr::PrivateHostCopyServiceReport report;report.budgetMs=2;report.frequency=1000000;report.available=false;
        report.Observe({FrameProgressStop::AdmissionLost,1,1,5000});CHECK(report.maxOvershootTicks==3000);}
    return 0;
}
int TimerTimeoutRetainsAsyncCopy(){
    // Timeout ends the episode even if the sampled clock has not advanced; a
    // late wake also keeps its actual overshoot. Neither permits a second poll.
    for(const auto elapsed:{0u,2000u,7000u}){
        TimerState state;state.timeouts=1;xr::PrivateHostCopyServiceReport report;
        report.budgetMs=2;report.frequency=1000000;
        {
            xr::PrivateHostCopyWait<FakeTimer> wait(report,{&state});CHECK(wait.Initialize());
            Fixture f;f.sleepTicks=elapsed;f.completeAt=2;f.serviceReport=&report;f.waitContext=&wait;
            f.waitPause=[](void* value)noexcept {return static_cast<xr::PrivateHostCopyWait<FakeTimer>*>(value)->Pause();};
            const auto result=f.Run();report.Observe(result);
            CHECK(result.stop==FrameProgressStop::BudgetExpired&&result.pumps==1&&result.sleeps==1&&result.ticks==elapsed);
            CHECK(report.available&&report.expired==1&&!report.completed&&report.timerWaitTimeouts==1);
            CHECK(!report.timerWaitFailures&&!report.lastWaitError&&report.maxOvershootTicks==(elapsed>2000?elapsed-2000:0));
            CHECK(f.lease->Copying()&&!f.lease->Cancelled()&&!f.provider.positive&&!f.provider.negative&&!f.provider.admissions);
            CHECK(state.creates==1&&state.arms==1&&state.waits==1&&state.active&&!state.closes);
            // A late timer signal does not complete the GPU query or ACK it.
            state.active=false;state.signaled=true;
            CHECK(!f.provider.positive&&f.lease->Copying());
            // Only the ordinary async path's actual success can complete it.
            CHECK(f.lease->Complete()&&f.provider.positive==1);f.lease.reset();CHECK(!f.provider.negative);
            Fixture next;next.sleepTicks=500;next.completeAt=2;next.serviceReport=&report;
            next.waitContext=&wait;next.waitPause=f.waitPause;
            const auto later=next.Run();report.Observe(later);
            CHECK(later.stop==FrameProgressStop::Completed&&later.pumps==2&&later.sleeps==1);
            CHECK(next.lease->Ready()&&next.provider.positive==1&&!next.provider.negative&&!next.provider.admissions);
            CHECK(report.available&&report.completed==1&&report.timerWaitTimeouts==1&&!report.timerWaitFailures);
            CHECK(state.creates==1&&state.arms==2&&state.waits==2&&state.clearedLateSignals==1&&!state.signaled);
        }
        CHECK(state.closes==1);
        std::ostringstream json;report.Write(json);
        CHECK(json.str().find("\"timer_wait_timeouts\":1")!=std::string::npos);
        CHECK(json.str().find("\"timer_wait_failures\":0")!=std::string::npos);
        CHECK(json.str().find("\"last_wait_error\":0")!=std::string::npos);
    }
    // A timeout does not mask simultaneous cancellation, owner/context loss,
    // retirement, successor replacement, or a failed post-wait clock read.
    for(unsigned cause=1;cause<=7;++cause){
        TimerState state;state.timeouts=1;xr::PrivateHostCopyServiceReport report;
        report.budgetMs=2;report.frequency=1000000;
        xr::PrivateHostCopyWait<FakeTimer> wait(report,{&state});CHECK(wait.Initialize());
        Fixture f;f.serviceReport=&report;f.waitContext=&wait;f.sleepTicks=2000;
        if(cause<=6)f.pauseAction=cause;else f.zeroClockAt=4;
        f.waitPause=[](void* value)noexcept {return static_cast<xr::PrivateHostCopyWait<FakeTimer>*>(value)->Pause();};
        const auto result=f.Run();
        CHECK(result.stop==(cause==7?FrameProgressStop::ClockFailed:FrameProgressStop::AdmissionLost));
        CHECK(result.pumps==1&&result.sleeps==1&&report.available&&report.timerWaitTimeouts==1&&!report.timerWaitFailures);
        CHECK(!f.provider.positive&&!f.provider.admissions);
        if(cause==1)CHECK(f.lease->Cancelled()&&f.lease->Copying());
        if(cause==5)CHECK(f.lease->ticket.sequence==f.expected.sequence+1&&f.lease->Copying());
    }
    return 0;
}
#ifdef _WIN32
// Optional bounded API smoke, not a GPU or scheduler-deadline acceptance test.
// These are the same Win32 flags/relative due time/timeout as the Host backend.
int TimerApiSmoke(){
    LARGE_INTEGER frequency{};CHECK(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0);
    const auto timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
    CHECK(timer!=nullptr);
    std::array<std::uint64_t,16> ticks{};unsigned attempts=0,completed=0,timeouts=0;DWORD error=0;
    for(auto& elapsed:ticks){
        LARGE_INTEGER before{},after{},due{};due.QuadPart=-10000;
        if(!QueryPerformanceCounter(&before)||!SetWaitableTimerEx(timer,&due,0,nullptr,nullptr,nullptr,0)){error=GetLastError();break;}
        const auto result=WaitForSingleObject(timer,2);
        if(result!=WAIT_OBJECT_0&&result!=WAIT_TIMEOUT){error=result==WAIT_FAILED?GetLastError():result;break;}
        if(!QueryPerformanceCounter(&after)||after.QuadPart<before.QuadPart){error=ERROR_INVALID_DATA;break;}
        elapsed=std::uint64_t(after.QuadPart-before.QuadPart);++attempts;
        if(result==WAIT_TIMEOUT)++timeouts;else ++completed;
    }
    CloseHandle(timer);
    std::cout<<"{\"schema\":\"fvr.host-copy-timer-api-smoke.v1\",\"passed\":"<<(attempts==ticks.size()?"true":"false")
        <<",\"attempts\":"<<attempts<<",\"completed\":"<<completed<<",\"timeouts\":"<<timeouts
        <<",\"error\":"<<error<<",\"qpc_frequency\":"<<frequency.QuadPart
        <<",\"no_timing_threshold\":true,\"game_actions\":false,\"global_timer_policy_changed\":false,\"ticks\":[";
    for(unsigned i=0;i<attempts;++i){if(i)std::cout<<',';std::cout<<ticks[i];}std::cout<<"]}\n";
    return attempts==ticks.size()?0:1;
}
#endif
}
int main(int argc,char** argv){
#ifdef _WIN32
    if(argc==2&&std::string(argv[1])=="--timer")return TimerApiSmoke();
#else
    (void)argc;(void)argv;
#endif
    CHECK(ConfigurationAndAdmission()==0);
    CHECK(CompletionOwnsLocalImages()==0);
    CHECK(BusyDeadlineAndDriverFailure()==0);
    CHECK(ClockAndFiniteWork()==0);
    CHECK(CancelAndIdentityChange()==0);
    CHECK(ReportAccounting()==0);
    CHECK(TimerFailureRetainsAsyncCopy()==0);
    CHECK(TimerTimeoutRetainsAsyncCopy()==0);
    return 0;
}
