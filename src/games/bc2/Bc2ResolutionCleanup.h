#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace fvr::bc2 {
class ResolutionCleanupDeadline {
public:
    using Clock=std::chrono::steady_clock;
    explicit ResolutionCleanupDeadline(std::chrono::milliseconds budget):end_(Clock::now()+budget){}
    Clock::time_point End()const noexcept{return end_;}
    std::uint32_t RemainingMs()const noexcept {
        const auto remaining=std::chrono::ceil<std::chrono::milliseconds>(end_-Clock::now()).count();
        return remaining>0?static_cast<std::uint32_t>(remaining):0;
    }
private:
    Clock::time_point end_;
};
struct ResolutionDisableResult {
    bool disabled=false,attempted=false;
    int status=0;
};
struct ResolutionCleanupResult {
    bool configurationReady=false,contended=false,disableCompleted=false,drained=false,timedOut=false;
    ResolutionDisableResult disable{};
    bool Complete()const noexcept{return configurationReady&&disableCompleted&&disable.disabled&&drained&&!timedOut;}
};
class ResolutionCleanup {
public:
    // Configuration, a concurrent native disable, and callback drain consume
    // one deadline. Only the thread performing disable owns that work; a
    // waiting caller timing out must neither cancel it nor publish completion.
    template<class Configuration,class Disable,class Drain,class Contention>
    ResolutionCleanupResult Complete(const ResolutionCleanupDeadline& deadline,Configuration configuration,
        Disable disable,Drain drain,Contention contention)noexcept {
        ResolutionCleanupResult out{};
        out.configurationReady=configuration(deadline);
        if(!out.configurationReady){out.timedOut=!deadline.RemainingMs();return out;}
        {
            std::unique_lock lock(mutex_);
            if(running_){
                out.contended=true;contention();
                if(!completed_.wait_until(lock,deadline.End(),[this]{return finished_;})){out.timedOut=true;return out;}
            }
            if(!finished_){
                if(!deadline.RemainingMs()){out.timedOut=true;return out;}
                running_=true;lock.unlock();
                const auto result=disable();
                lock.lock();result_=result;finished_=true;running_=false;
                completed_.notify_all();
            }
            out.disableCompleted=true;out.disable=result_;
        }
        // Cache the actual disable outcome, including failure. Contention is
        // not permission for a second MinHook call that hides the first error.
        // A later call can still observe a safely completed callback drain.
        if(out.disable.disabled&&deadline.RemainingMs())out.drained=drain(deadline);
        out.timedOut=!deadline.RemainingMs();
        return out;
    }
    template<class Configuration,class Disable,class Drain>
    ResolutionCleanupResult Complete(const ResolutionCleanupDeadline& deadline,Configuration configuration,
        Disable disable,Drain drain)noexcept {
        return Complete(deadline,configuration,disable,drain,[]{});
    }
private:
    std::mutex mutex_;
    std::condition_variable completed_;
    bool running_=false,finished_=false;
    ResolutionDisableResult result_{};
};
}
