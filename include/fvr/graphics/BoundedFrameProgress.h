#pragma once
#include <cstdint>
#include <string_view>

namespace fvr::graphics {
enum class FrameProgressStop { Completed, AdmissionLost, BudgetExpired, ClockFailed, PollLimit };
struct FrameProgressResult {
    FrameProgressStop stop=FrameProgressStop::AdmissionLost;
    unsigned pumps=0,sleeps=0;
    std::uint64_t ticks=0;
};
// Private experiment only; malformed or out-of-range values cannot enable it.
constexpr unsigned ParseFrameProgressBudget(std::wstring_view text)noexcept {
    if(text.empty()||text.size()>2)return 0;
    unsigned value=0;
    for(const auto c:text){if(c<L'0'||c>L'9')return 0;value=value*10+unsigned(c-L'0');}
    return value<=16?value:0;
}

// Only service an already restored, exact pair. Observe must return false when
// its lease disappears OR changes; a queued successor is never a reason to wait.
// All callbacks are nonblocking except Pause (one scheduler sleep). No mutex or
// channel guard may be retained by a callback. The finite poll cap also bounds
// work if the clock stops advancing. OS scheduling can overshoot the deadline;
// after waking, never pump again past it.
template<unsigned MaxPolls=17,class Admitted,class Pending,class Pump,class Clock,class Pause>
FrameProgressResult ServiceBoundedFrameProgress(std::uint64_t budgetTicks,
        Admitted&& admitted,Pending&& pending,Pump&& pump,Clock&& clock,Pause&& pause)noexcept {
    static_assert(MaxPolls>0&&MaxPolls<=17);
    static_assert(noexcept(admitted())&&noexcept(pending())&&noexcept(pump())&&
                  noexcept(clock())&&noexcept(pause()));
    FrameProgressResult out;
    if(!budgetTicks||!admitted())return out;
    const auto start=clock();
    if(!start){out.stop=FrameProgressStop::ClockFailed;return out;}
    auto last=start;
    const auto sample=[&]()noexcept {
        const auto now=clock();
        if(!now||now<last){out.stop=FrameProgressStop::ClockFailed;return false;}
        last=now;out.ticks=now-start;
        return true;
    };
    for(unsigned poll=0;poll<MaxPolls;++poll){
        if(!sample())return out;
        if(!admitted()){out.stop=FrameProgressStop::AdmissionLost;return out;}
        if(!pending()){out.stop=FrameProgressStop::Completed;return out;}
        if(out.ticks>=budgetTicks){out.stop=FrameProgressStop::BudgetExpired;return out;}
        pump();++out.pumps;
        if(!sample())return out;
        if(!admitted()){out.stop=FrameProgressStop::AdmissionLost;return out;}
        if(!pending()){out.stop=FrameProgressStop::Completed;return out;}
        if(out.ticks>=budgetTicks){out.stop=FrameProgressStop::BudgetExpired;return out;}
        if(poll+1==MaxPolls){out.stop=FrameProgressStop::PollLimit;return out;}
        pause();++out.sleeps;
    }
    out.stop=FrameProgressStop::PollLimit;return out;
}
}
