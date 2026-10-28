#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
namespace fvr::graphics {
enum class GraphicsCallKind:unsigned {Present,Indexed,Draw,ClearDepth,CreateTexture2D,MenuPump,MenuReset,Count};
struct GraphicsCallIdentity {
    std::uintptr_t device=0,context=0,chain=0;unsigned ownerThread=0;
    bool Valid()const noexcept{return device&&context&&chain&&ownerThread;}
};
struct GraphicsCallEntry {
    unsigned kind=0,thread=0,ownerThread=0,overlapThread=0,bucket=0;
    std::uintptr_t device=0,context=0,chain=0,caller=0;
    std::uint64_t ordinal=0;std::int64_t beginQpc=0;
};
struct GraphicsCallExit {std::int64_t endQpc=0;std::int32_t result=0;bool resultKnown=false;};
struct GraphicsCallCounts {std::uint64_t calls=0,foreign=0,overlap=0,omitted=0;};
// Value-only diagnostic. No D3D calls, COM references, allocation, waits or I/O.
// Distinct per-kind quotas preserve later foreign/overlap observations even
// after ordinary calls fill their first four slots. Duplicates retain ordinal.
class GraphicsCallProvenance {
public:
    static constexpr unsigned Kinds=unsigned(GraphicsCallKind::Count),Threads=8,PerKind=16,Rows=Kinds*PerKind;
    struct Ticket {bool admitted=false;unsigned threadSlot=Threads,count=0;std::array<unsigned,3> rows{};};
private:
    struct Slot {GraphicsCallEntry entry{};GraphicsCallExit exit{};std::atomic<bool> entered=false,returned=false;};
    struct Counts {std::atomic<std::uint64_t> calls=0,foreign=0,overlap=0,omitted=0;};
    std::array<Slot,Rows> rows_{};std::array<Counts,Kinds> counts_{};
    std::array<std::atomic<unsigned>,Threads> threads_{},depth_{};
    std::atomic<bool> configured_=false,accepting_=false;std::atomic<unsigned> active_=0;
    std::atomic<std::uint64_t> rejectedIdentity_=0,threadOverflow_=0;
    GraphicsCallIdentity identity_{};
    void Store(Ticket& t,unsigned index,GraphicsCallEntry entry,unsigned bucket)noexcept {
        entry.bucket=bucket;rows_[index].entry=entry;rows_[index].entered.store(true,std::memory_order_release);t.rows[t.count++]=index;
    }
public:
    bool Configure(GraphicsCallIdentity identity)noexcept {
        if(!identity.Valid())return false;bool expected=false;
        if(!configured_.compare_exchange_strong(expected,true))return false;
        identity_=identity;accepting_.store(true,std::memory_order_release);return true;
    }
    bool Enabled()const noexcept{return accepting_.load(std::memory_order_acquire);}
    bool Matches(GraphicsCallKind kind,std::uintptr_t device,std::uintptr_t context,std::uintptr_t chain)const noexcept {
        if(!Enabled())return false;
        switch(kind){
        case GraphicsCallKind::Present:case GraphicsCallKind::MenuPump:return chain&&chain==identity_.chain;
        case GraphicsCallKind::CreateTexture2D:return device&&device==identity_.device;
        case GraphicsCallKind::MenuReset:return device==identity_.device&&context==identity_.context;
        case GraphicsCallKind::Indexed:case GraphicsCallKind::Draw:case GraphicsCallKind::ClearDepth:return context&&context==identity_.context;
        default:return false;
        }
    }
    Ticket Begin(GraphicsCallKind kind,std::uintptr_t device,std::uintptr_t context,std::uintptr_t chain,
                 std::uintptr_t caller,unsigned thread,std::int64_t qpc)noexcept {
        Ticket ticket;if(!Enabled())return ticket;
        active_.fetch_add(1,std::memory_order_acq_rel);
        if(!Enabled()){active_.fetch_sub(1,std::memory_order_release);return ticket;}
        if(!thread||unsigned(kind)>=Kinds||!Matches(kind,device,context,chain)){
            rejectedIdentity_.fetch_add(1,std::memory_order_relaxed);active_.fetch_sub(1,std::memory_order_release);return ticket;
        }
        ticket.admitted=true;const unsigned k=unsigned(kind);auto& counts=counts_[k];
        GraphicsCallEntry entry{k,thread,identity_.ownerThread,0,0,device,context,chain,caller,counts.calls.fetch_add(1)+1,qpc};
        for(unsigned i=0;i<Threads;++i){auto t=threads_[i].load(std::memory_order_acquire);
            if(!t){unsigned empty=0;threads_[i].compare_exchange_strong(empty,thread,std::memory_order_acq_rel);t=threads_[i].load(std::memory_order_acquire);}
            if(t==thread){ticket.threadSlot=i;depth_[i].fetch_add(1,std::memory_order_acq_rel);break;}
        }
        if(ticket.threadSlot==Threads)threadOverflow_.fetch_add(1,std::memory_order_relaxed);
        for(unsigned i=0;i<Threads;++i){const auto other=threads_[i].load(std::memory_order_acquire);
            if(other&&other!=thread&&depth_[i].load(std::memory_order_acquire)){entry.overlapThread=other;break;}}
        const unsigned base=k*PerKind;
        if(entry.ordinal<=4)Store(ticket,base+unsigned(entry.ordinal-1),entry,0);else counts.omitted.fetch_add(1);
        if(thread!=identity_.ownerThread){const auto n=counts.foreign.fetch_add(1);if(n<8)Store(ticket,base+4+unsigned(n),entry,1);else counts.omitted.fetch_add(1);}
        if(entry.overlapThread){const auto n=counts.overlap.fetch_add(1);if(n<4)Store(ticket,base+12+unsigned(n),entry,2);else counts.omitted.fetch_add(1);}
        return ticket;
    }
    void End(Ticket& ticket,GraphicsCallExit exit)noexcept {
        if(!ticket.admitted)return;ticket.admitted=false;
        for(unsigned i=0;i<ticket.count;++i){auto& s=rows_[ticket.rows[i]];s.exit=exit;s.returned.store(true,std::memory_order_release);}
        if(ticket.threadSlot<Threads)depth_[ticket.threadSlot].fetch_sub(1,std::memory_order_release);
        active_.fetch_sub(1,std::memory_order_release);
    }
    bool Stop()noexcept {accepting_.store(false,std::memory_order_release);return active_.load(std::memory_order_acquire)==0;}
    unsigned Active()const noexcept{return active_.load(std::memory_order_acquire);}
    GraphicsCallIdentity Identity()const noexcept{return identity_;} // Configure is complete before worker reads.
    GraphicsCallCounts CountsFor(unsigned kind)const noexcept {
        if(kind>=Kinds)return {};const auto& c=counts_[kind];return {c.calls.load(),c.foreign.load(),c.overlap.load(),c.omitted.load()};
    }
    std::uint64_t RejectedIdentity()const noexcept{return rejectedIdentity_.load();}
    std::uint64_t ThreadOverflow()const noexcept{return threadOverflow_.load();}
    std::optional<GraphicsCallEntry> Entry(unsigned index)const noexcept {
        if(index>=Rows||!rows_[index].entered.load(std::memory_order_acquire))return {};return rows_[index].entry;
    }
    std::optional<GraphicsCallExit> Exit(unsigned index)const noexcept {
        if(index>=Rows||!rows_[index].returned.load(std::memory_order_acquire))return {};return rows_[index].exit;
    }
};
}
