#pragma once
#include "fvr/graphics/BoundedFrameProgress.h"
#include <algorithm>
#include <ostream>
#include <utility>

namespace fvr::xr {
constexpr unsigned ParseHostCopyBudget(std::wstring_view text)noexcept {
    return text.size()==1&&text[0]>=L'0'&&text[0]<=L'2'?unsigned(text[0]-L'0'):0;
}
enum class HostCopyWaitResult { Signaled, TimedOut, Failed };
// Only the newly submitted legacy copy is eligible. Admitted must retain the
// exact owner/context/ticket through Ready; failure/retirement must revoke it.
// Pending becomes false only after the actual query succeeds. This wrapper
// cannot fetch another pair and has at most three polls/two timer waits.
template<class Admitted,class Pending,class Pump,class Clock,class Pause>
graphics::FrameProgressResult ServiceBoundedHostCopy(std::uint64_t ticks,
        Admitted&& admitted,Pending&& pending,Pump&& pump,Clock&& clock,Pause&& pause)noexcept {
    static_assert(noexcept(admitted())&&noexcept(pause()));
    auto waitResult=HostCopyWaitResult::Signaled;bool expiredByWait=false;
    const auto eligible=[&]()noexcept {
        // Genuine owner/cancel/API failure takes precedence over a timeout.
        if(!admitted())return false;
        if(waitResult==HostCopyWaitResult::TimedOut){expiredByWait=true;return false;}
        return waitResult!=HostCopyWaitResult::Failed;
    };
    const auto wait=[&]()noexcept {waitResult=pause();};
    auto result=graphics::ServiceBoundedFrameProgress<3>(ticks,eligible,
        std::forward<Pending>(pending),std::forward<Pump>(pump),std::forward<Clock>(clock),wait);
    // A bounded wait timeout ends this episode before another query. It is not
    // completion or permanent loss of service; the ordinary async copy survives.
    // Keep a failed QPC read or actual admission loss classified independently.
    if(expiredByWait&&result.stop==graphics::FrameProgressStop::AdmissionLost)
        result.stop=graphics::FrameProgressStop::BudgetExpired;
    return result;
}
struct PrivateHostCopyServiceReport {
    unsigned budgetMs=0;bool available=false;
    bool timerCreated=false;
    std::uint64_t timerCreateFailures=0,timerArmFailures=0,timerWaitFailures=0,timerWaitTimeouts=0;
    std::uint32_t lastWaitError=0;
    std::uint64_t frequency=0,episodes=0,completed=0,expired=0,admissionLost=0,clockFailed=0,pollLimit=0;
    std::uint64_t polls=0,sleeps=0,totalTicks=0,maxTicks=0,overshootEpisodes=0,maxOvershootTicks=0;
    std::uint64_t ConfiguredBudgetTicks()const noexcept {
        return budgetMs<=2?(frequency/1000)*budgetMs+(frequency%1000)*budgetMs/1000:0;
    }
    std::uint64_t BudgetTicks()const noexcept {return available?ConfiguredBudgetTicks():0;}
    void Observe(const graphics::FrameProgressResult& r)noexcept {
        ++episodes;polls+=r.pumps;sleeps+=r.sleeps;totalTicks+=r.ticks;maxTicks=(std::max)(maxTicks,r.ticks);
        switch(r.stop){
        case graphics::FrameProgressStop::Completed:++completed;break;
        case graphics::FrameProgressStop::AdmissionLost:++admissionLost;break;
        case graphics::FrameProgressStop::BudgetExpired:++expired;break;
        case graphics::FrameProgressStop::ClockFailed:++clockFailed;break;
        case graphics::FrameProgressStop::PollLimit:++pollLimit;break;
        }
        // A failed wait revokes admission before Observe; preserve the original
        // requested deadline when calculating that episode's elapsed overshoot.
        const auto budget=ConfiguredBudgetTicks();if(r.ticks>budget){++overshootEpisodes;maxOvershootTicks=(std::max)(maxOvershootTicks,r.ticks-budget);}
    }
    void Write(std::ostream& o)const {
        o<<"{\"schema\":\"fvr.host-copy-service.v1\",\"configured_ms\":"<<budgetMs
         <<",\"available\":"<<(available?"true":"false")<<",\"enabled\":"<<(budgetMs&&available?"true":"false")
         <<",\"qpc_frequency\":"<<frequency<<",\"episodes\":"<<episodes<<",\"completed\":"<<completed
         <<",\"expired\":"<<expired<<",\"admission_lost\":"<<admissionLost<<",\"clock_failed\":"<<clockFailed
         <<",\"poll_limit\":"<<pollLimit<<",\"polls\":"<<polls<<",\"sleeps\":"<<sleeps
         <<",\"total_ticks\":"<<totalTicks<<",\"max_ticks\":"<<maxTicks<<",\"overshoot_episodes\":"<<overshootEpisodes
         <<",\"max_overshoot_ticks\":"<<maxOvershootTicks
         <<",\"wait_kind\":\"high_resolution_auto_reset_waitable_timer\",\"timer_created\":"<<(timerCreated?"true":"false")
         <<",\"timer_create_failures\":"<<timerCreateFailures<<",\"timer_arm_failures\":"<<timerArmFailures
         <<",\"timer_wait_failures\":"<<timerWaitFailures<<",\"last_wait_error\":"<<lastWaitError
         <<",\"timer_wait_timeouts\":"<<timerWaitTimeouts<<",\"wait_timeout_semantics\":\"episode_expired_copy_retained\""
         <<",\"requested_wait_us\":1000,\"wait_timeout_ms\":2,\"sleep_counter_semantics\":\"timer_wait_attempts\""
         <<",\"busy_spin\":false,\"global_timer_policy_changed\":false"
         <<",\"completed_means\":\"observed_query_success\",\"elapsed_semantics\":\"host_elapsed_including_driver_calls_and_wait\"}";
    }
};
// Backend owns only a private timer handle. No copy/IPC operation is exposed,
// so an unavailable timer cannot acknowledge or retire the pending GPU lease.
// All calls occur on the Host owner. Only API failure permanently revokes
// service; a normal bounded timeout leaves the one-shot timer reusable.
template<class Backend> class PrivateHostCopyWait {
    PrivateHostCopyServiceReport& report_;
    Backend backend_;
    bool attempted_=false,created_=false;
public:
    explicit PrivateHostCopyWait(PrivateHostCopyServiceReport& report,Backend backend={})noexcept
        :report_(report),backend_(std::move(backend)){}
    PrivateHostCopyWait(const PrivateHostCopyWait&)=delete;
    PrivateHostCopyWait& operator=(const PrivateHostCopyWait&)=delete;
    ~PrivateHostCopyWait(){if(created_)backend_.Close();}
    bool Initialize()noexcept {
        if(attempted_)return report_.available;
        attempted_=true;report_.available=false;
        if(!report_.budgetMs||report_.budgetMs>2||report_.frequency<1000)return false;
        if(!backend_.Create()){
            ++report_.timerCreateFailures;report_.lastWaitError=backend_.Error();return false;
        }
        created_=report_.timerCreated=report_.available=true;return true;
    }
    HostCopyWaitResult Pause()noexcept {
        if(!created_||!report_.available)return HostCopyWaitResult::Failed;
        if(!backend_.Arm()){
            ++report_.timerArmFailures;report_.lastWaitError=backend_.Error();report_.available=false;return HostCopyWaitResult::Failed;
        }
        const auto result=backend_.Wait();
        if(result==HostCopyWaitResult::TimedOut){
            ++report_.timerWaitTimeouts;
        }else if(result==HostCopyWaitResult::Failed){
            ++report_.timerWaitFailures;report_.lastWaitError=backend_.Error();report_.available=false;
        }
        return result;
    }
};
}
