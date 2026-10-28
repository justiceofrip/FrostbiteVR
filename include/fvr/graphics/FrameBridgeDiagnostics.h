#pragma once
#include "fvr/graphics/SharedTextureProtocol.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace fvr::graphics {
// Local diagnostics only: no wire layout, scheduling decision or GPU authority.
// QPC ticks describe when the CPU observed a boundary, not GPU execution time.
struct FrameBridgeTimingKey {
    std::uint64_t channel=0,request=0,owner=0,frame=0,device=0,tracking=0,space=0;
    std::int64_t predictedNs=0;
    bool operator==(const FrameBridgeTimingKey&)const=default;
};
enum class FrameBridgeTimingStage:unsigned {
    Begin,CaptureLeftBegin,CaptureLeftQueued,CaptureRightBegin,CaptureRightQueued,
    Restored,ExportBegin,RelayCopiesReady,ProducerFenceReady,Staged,Published,
    Reaped,Released,Cancelled,CaptureReleaseBegin,CaptureReleased,Count
};
inline constexpr std::array<const char*,unsigned(FrameBridgeTimingStage::Count)> FrameBridgeTimingStageNames{
    "begin","capture_left_begin","capture_left_queued","capture_right_begin","capture_right_queued",
    "restored","export_begin","relay_copies_ready","producer_fence_ready","staged","published",
    "reaped","released","cancelled","capture_release_begin","capture_released"};
enum class FrameBridgeTimingEnd:unsigned {Pending,Consumed,Discarded,Cancelled,Abandoned,Superseded};
inline constexpr std::array<const char*,6> FrameBridgeTimingEndNames{
    "pending","consumed","discarded","cancelled","abandoned","superseded"};
struct FrameBridgeTimingRow {
    FrameBridgeTimingKey key{};
    PairTicket ticket{};
    std::array<std::int64_t,unsigned(FrameBridgeTimingStage::Count)> qpc{};
    std::array<std::int64_t,4> depthQueuedQpc{};
    std::int64_t lastExportBusyQpc=0,ackObservedQpc=0;
    std::uint32_t width=0,height=0,sharingMode=0,depthSamples=0,depthQuality=0,depthFormat=0,depthMask=0;
    std::uint32_t exportAttempts=0,exportBusy=0;
    FrameBridgeTimingEnd end=FrameBridgeTimingEnd::Pending;
};
inline constexpr unsigned FrameBridgeTimingCapacity=256;
struct FrameBridgeTimingSnapshot {
    std::array<FrameBridgeTimingRow,FrameBridgeTimingCapacity> rows{};
    FrameBridgeTimingRow active{},retiring{},firstNonConsumed{};
    std::uint64_t frequency=0,completed=0,overwritten=0,dropped=0,unmatched=0,invalidClock=0;
    unsigned count=0;
    bool available=false,hasActive=false,hasRetiring=false,hasFirstNonConsumed=false;
};
class FrameBridgeDiagnostics {
public:
    void SetFrequency(std::uint64_t frequency)noexcept {
        Lock lock(*this);if(lock)frequency_=frequency;
    }
    void Begin(const FrameBridgeTimingKey& key,unsigned width,unsigned height,std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock)return;
        if(active_.key.request){active_.end=FrameBridgeTimingEnd::Superseded;Finish();}
        active_={};active_.key=key;active_.width=width;active_.height=height;
        Stamp(FrameBridgeTimingStage::Begin,qpc);
    }
    void Mark(const FrameBridgeTimingKey& key,FrameBridgeTimingStage stage,std::int64_t qpc)noexcept {
        Lock lock(*this);if(lock&&Matches(key))Stamp(stage,qpc);
    }
    void Export(const FrameBridgeTimingKey& key,unsigned sharingMode,std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock||!Matches(key))return;
        active_.sharingMode=sharingMode;++active_.exportAttempts;Stamp(FrameBridgeTimingStage::ExportBegin,qpc);
    }
    void ExportBusy(const FrameBridgeTimingKey& key,std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock||!Matches(key))return;
        ++active_.exportBusy;if(Clock(qpc))active_.lastExportBusyQpc=qpc;
    }
    void Depth(const FrameBridgeTimingKey& key,unsigned eye,unsigned stage,unsigned samples,unsigned quality,
               unsigned format,std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock||!Matches(key))return;
        if(eye>1||stage>1){++unmatched_;return;}
        active_.depthSamples=samples;active_.depthQuality=quality;active_.depthFormat=format;
        const auto i=eye*2+stage;active_.depthMask|=1u<<i;
        if(Clock(qpc)&&!active_.depthQueuedQpc[i])active_.depthQueuedQpc[i]=qpc;
    }
    void Ticket(const FrameBridgeTimingKey& key,const PairTicket& ticket)noexcept {
        Lock lock(*this);if(!lock||!Matches(key))return;
        if(ticket.frameId!=key.frame||ticket.trackingGeneration!=key.tracking||ticket.spaceGeneration!=key.space||
           ticket.predictedNs!=key.predictedNs||!ticket.sequence){++unmatched_;return;}
        active_.ticket=ticket;
    }
    // Opt-in logical acknowledgement can precede graphics-owned cleanup. Keep
    // exactly one old row, without stamping Reaped/Released at this boundary.
    // The return value is diagnostic only and cannot authorize frame admission.
    bool MarkRetiring(const FrameBridgeTimingKey& key,const PairTicket& ticket,std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock)return false;
        if(retiring_.key.request){++unmatched_;return false;}
        if(!Matches(key)||!Matches(ticket))return false;
        if(Clock(qpc))active_.ackObservedQpc=qpc;
        retiring_=active_;active_={};return true;
    }
    void Reaped(const FrameBridgeTimingKey& key,const PairTicket& ticket,std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock)return;
        auto* row=Find(key);if(!row)return;
        if(!SameTicket(*row,ticket)){++unmatched_;return;}
        Stamp(*row,FrameBridgeTimingStage::Reaped,qpc);
    }
    void Released(const PairTicket& ticket,bool consumed,std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock)return;auto* row=Find(ticket);if(!row)return;
        Stamp(*row,FrameBridgeTimingStage::Released,qpc);
        row->end=consumed?FrameBridgeTimingEnd::Consumed:
            (row->qpc[unsigned(FrameBridgeTimingStage::Reaped)]?FrameBridgeTimingEnd::Discarded:FrameBridgeTimingEnd::Abandoned);
        Finish(*row);
    }
    void Cancel(const FrameBridgeTimingKey& key,std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock||!Matches(key))return;
        Stamp(FrameBridgeTimingStage::Cancelled,qpc);active_.end=FrameBridgeTimingEnd::Cancelled;Finish();
    }
    void Close(std::int64_t qpc)noexcept {
        Lock lock(*this);if(!lock)return;
        // Closing the journal is not evidence that a deferred graphics callback
        // ran. Keep its release stamp absent if that boundary was never observed.
        if(retiring_.key.request){retiring_.end=FrameBridgeTimingEnd::Abandoned;Finish(retiring_);}
        if(active_.key.request){Stamp(FrameBridgeTimingStage::Released,qpc);active_.end=FrameBridgeTimingEnd::Abandoned;Finish();}
    }
    bool Snapshot(FrameBridgeTimingSnapshot& out)const noexcept {
        out={};Lock lock(*this);out.dropped=dropped_.load(std::memory_order_relaxed);if(!lock)return false;
        out.frequency=frequency_;out.completed=completed_;out.overwritten=overwritten_;out.unmatched=unmatched_;
        out.invalidClock=invalidClock_;out.count=count_;out.available=true;out.active=active_;out.hasActive=active_.key.request!=0;
        out.retiring=retiring_;out.hasRetiring=retiring_.key.request!=0;
        out.firstNonConsumed=firstNonConsumed_;out.hasFirstNonConsumed=firstNonConsumed_.key.request!=0;
        const auto first=(next_+FrameBridgeTimingCapacity-count_)%FrameBridgeTimingCapacity;
        for(unsigned i=0;i<count_;++i)out.rows[i]=rows_[(first+i)%FrameBridgeTimingCapacity];
        return true;
    }
private:
    struct Lock {
        const FrameBridgeDiagnostics& owner;bool held;
        explicit Lock(const FrameBridgeDiagnostics& d)noexcept:owner(d),held(!d.gate_.test_and_set(std::memory_order_acquire)) {
            if(!held)d.dropped_.fetch_add(1,std::memory_order_relaxed);
        }
        ~Lock(){if(held)owner.gate_.clear(std::memory_order_release);}
        explicit operator bool()const noexcept{return held;}
    };
    bool Matches(const FrameBridgeTimingKey& key)noexcept {
        if(key.request&&active_.key==key)return true;++unmatched_;return false;
    }
    bool Matches(const PairTicket& ticket)noexcept {
        if(SameTicket(active_,ticket))return true;
        ++unmatched_;return false;
    }
    static bool SameTicket(const FrameBridgeTimingRow& row,const PairTicket& ticket)noexcept {
        return row.key.request&&row.ticket.sequence&&std::memcmp(&row.ticket,&ticket,sizeof(ticket))==0;
    }
    FrameBridgeTimingRow* Find(const FrameBridgeTimingKey& key)noexcept {
        const bool active=key.request&&active_.key==key,retiring=key.request&&retiring_.key==key;
        if(active!=retiring)return active?&active_:&retiring_;++unmatched_;return nullptr;
    }
    FrameBridgeTimingRow* Find(const PairTicket& ticket)noexcept {
        const bool active=SameTicket(active_,ticket),retiring=SameTicket(retiring_,ticket);
        if(active!=retiring)return active?&active_:&retiring_;++unmatched_;return nullptr;
    }
    bool Clock(std::int64_t qpc)noexcept {if(qpc>0)return true;++invalidClock_;return false;}
    void Stamp(FrameBridgeTimingStage stage,std::int64_t qpc)noexcept {
        Stamp(active_,stage,qpc);
    }
    void Stamp(FrameBridgeTimingRow& row,FrameBridgeTimingStage stage,std::int64_t qpc)noexcept {
        const auto i=unsigned(stage);if(i>=row.qpc.size()){++unmatched_;return;}
        if(Clock(qpc)&&!row.qpc[i])row.qpc[i]=qpc;
    }
    void Finish()noexcept {Finish(active_);}
    void Finish(FrameBridgeTimingRow& row)noexcept {
        // Preserve the first terminal loss even after the ordinary ring wraps.
        // This copy is evidence only; missing boundaries remain missing.
        if(!firstNonConsumed_.key.request&&row.end!=FrameBridgeTimingEnd::Pending&&
           row.end!=FrameBridgeTimingEnd::Consumed)firstNonConsumed_=row;
        rows_[next_]=row;next_=(next_+1)%FrameBridgeTimingCapacity;++completed_;
        if(count_<FrameBridgeTimingCapacity)++count_;else ++overwritten_;row={};
    }
    mutable std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    mutable std::atomic<std::uint64_t> dropped_=0;
    std::uint64_t frequency_=0,completed_=0,overwritten_=0,unmatched_=0,invalidClock_=0;
    std::array<FrameBridgeTimingRow,FrameBridgeTimingCapacity> rows_{};
    FrameBridgeTimingRow active_{},retiring_{},firstNonConsumed_{};unsigned next_=0,count_=0;
};
}
