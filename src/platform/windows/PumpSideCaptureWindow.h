#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>
namespace fvr::probe {
// Receiver render-request scheduling only. Never reads or changes controllers,
// native state, witness ownership, input generation or source deadlines.
class PumpSideCaptureWindow {
public:
    static constexpr unsigned TotalPairs=240,MaximumPixels=120;
    bool Configure(std::uint64_t start,std::uint64_t duration,std::uint64_t interval)noexcept {
        if(enabled_||start<5000||start>45000||duration<500||duration>6000||
           interval<50||interval>250||start+duration>50000)return false;
        const auto pixels=(duration+interval-1)/interval;
        if(!pixels||pixels>MaximumPixels)return false;
        start_=start;duration_=duration;interval_=interval;planned_=unsigned(pixels);
        const unsigned ordinary=TotalPairs-planned_;unsigned count=0;
        // Reserve a fixed pixel budget in the original55s pair/60s input run.
        // Ordinary pairs remain before/after the window; no extra simulation.
        for(unsigned n=0;n<ordinary;++n){auto due=std::uint64_t(n)*(55000-duration_)/ordinary;
            if(due>=start_)due+=duration_;due_[count++]=due;}
        for(unsigned n=0;n<planned_;++n)due_[count++]=start_+std::uint64_t(n)*interval_;
        std::sort(due_.begin(),due_.end());enabled_=true;return true;
    }
    bool Enabled()const noexcept{return enabled_;}
    bool Due(std::uint64_t elapsed,unsigned consumed)const noexcept {
        return enabled_&&consumed<TotalPairs&&elapsed>=due_[consumed];
    }
    bool Pixels(std::uint64_t elapsed)const noexcept {
        return enabled_&&!invalid_&&elapsed>=start_&&elapsed-start_<duration_&&attempted_<planned_;
    }
    bool Observe(std::uint64_t elapsed,std::int64_t now,bool queued)noexcept {
        if(!Pixels(elapsed)||now<=0||(lastNs_&&now<=lastNs_)){invalid_=true;return false;}
        if(!attempted_)firstNs_=now;
        else maxGapNs_=(std::max)(maxGapNs_,now-lastNs_);
        lastNs_=now;++attempted_;if(queued)++queued_;return true;
    }
    bool Complete()const noexcept{return enabled_&&!invalid_&&attempted_==planned_&&queued_==attempted_;}
    unsigned Planned()const noexcept{return planned_;}
    unsigned Attempted()const noexcept{return attempted_;}
    void Report(std::ostream& out)const {
        out<<"{\"schema\":1,\"start_ms\":"<<start_<<",\"duration_ms\":"<<duration_
           <<",\"interval_ms\":"<<interval_<<",\"planned_pixel_pairs\":"<<planned_
           <<",\"attempted_pixel_pairs\":"<<attempted_<<",\"queued_pixel_pairs\":"<<queued_
           <<",\"first_sample_ns\":"<<firstNs_<<",\"last_sample_ns\":"<<lastNs_<<",\"max_gap_ns\":"<<maxGapNs_
           <<",\"invalid_clock_or_capture\":"<<invalid_<<",\"complete\":"<<Complete()
           <<",\"total_pair_budget\":240,\"maximum_pixel_pairs\":120,\"input_modified\":false,\"native_authority\":false,\"appearance_accepted\":false}";
    }
private:
    std::array<std::uint64_t,TotalPairs> due_{};
    std::uint64_t start_=0,duration_=0,interval_=0;
    std::int64_t firstNs_=0,lastNs_=0,maxGapNs_=0;
    unsigned planned_=0,attempted_=0,queued_=0;bool enabled_=false,invalid_=false;
};
}
