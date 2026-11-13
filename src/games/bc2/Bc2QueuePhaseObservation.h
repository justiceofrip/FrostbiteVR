#pragma once
#include "Bc2ReloadRecordWindow.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

// Value-only diagnostic storage. No native pointers, authority, allocation,
// locks, overwrite or rearm. The host retains this journal until process exit.
namespace fvr::bc2::queuePhase {
inline constexpr unsigned Capacity=4096,TargetRva=0x80d010,TargetSize=305;
enum class Kind:unsigned {Input,Consumer};
enum class Window:unsigned {Off,Armed,Opening,Collecting,Expired,Capped};
struct Stamp {std::int64_t qpc=0,ns=0;};
struct Identity {unsigned thread=0;std::uint64_t birth=0;};
struct ThreadState {std::uint64_t current=0,originalInput=0;unsigned inputDepth=0,consumerDepth=0;};
struct Row {
    std::uint64_t invocation=0,parent=0,originalInput=0;
    Kind kind{};Identity thread{};
    std::int64_t begin=0,end=0,originalEnter=0,originalReturn=0,acceptedAt=0;
    unsigned caller=0,self=0,owner=0,queue=0,stream=0,outElement=0,opaqueThird=0,result=0;
    unsigned inputDepth=0,consumerDepth=0,endInputDepth=0,endConsumerDepth=0;
    unsigned beginActiveInput=0,beginActiveConsumer=0,endActiveInput=0,endActiveConsumer=0;
    unsigned outcome=0,inputThread=0;std::uint64_t actorGeneration=0,inputEpoch=0;
    bool matched=false,acceptedLocal=false,abnormal=false,crossedDeadline=false,clockError=false;
};
struct Token {ThreadState previous{};Row row{};unsigned slot=Capacity;bool active=false;};
static_assert(std::is_trivially_destructible_v<Token>); // __finally leaves propagate stock SEH.
struct Counts {
    Window window{};bool closed=false,unrecordedOverlap=false;unsigned reserved=0,capped=0,dropped=0,clockErrors=0;
    unsigned activeInput=0,activeConsumer=0;std::int64_t frequency=0,activation=0,deadline=0,closedAt=0;
};
class Journal {
    friend struct JournalTestAccess;
    struct Slot {Row row{};std::atomic<bool> published{false};};
    std::array<Slot,Capacity> rows_{};
    ReloadRecordWindow window_;
    std::atomic<Window> phase_{Window::Off};std::atomic<bool> closed_{false};
    std::atomic<unsigned> reserved_{0},capped_{0},dropped_{0},clockErrors_{0},inputs_{0},consumers_{0};
    std::atomic<std::uint64_t> serial_{0};std::atomic<std::int64_t> activation_{0},closedAt_{0};
    std::atomic<std::int64_t> lastUnrecordedEnd_{0};std::atomic<bool> unrecordedOverlap_{false};
    std::int64_t frequency_=0;bool configured_=false;
    void PublishOpening(Stamp now)noexcept {
        // Pair with RecordUnrecorded's sequentially consistent publication:
        // either opener or ending interval must observe the other's bookend.
        activation_.store(now.qpc);window_.OpenDeferred(now.ns);
        if(lastUnrecordedEnd_.load()>=now.qpc)unrecordedOverlap_.store(true,std::memory_order_release);
        phase_.store(Window::Collecting,std::memory_order_release);
    }
    void RecordUnrecorded(const Row& row)noexcept {
        auto activation=activation_.load();
        if(!activation){
            auto last=lastUnrecordedEnd_.load();
            while(last<row.end&&!lastUnrecordedEnd_.compare_exchange_weak(last,row.end)){}
            activation=activation_.load();
        }
        // Carry-in and Opening intervals need not have an AcceptedLocal call.
        // Their evidence remains incomplete even after expiry and full drain.
        if(activation&&row.end>=activation&&row.begin<activation+frequency_*20)
            unrecordedOverlap_.store(true,std::memory_order_release);
    }
    void Reserve(Token& token,Stamp now)noexcept {
        if(token.slot!=Capacity||closed_.load(std::memory_order_acquire)||
           phase_.load(std::memory_order_acquire)!=Window::Collecting)return;
        if(!window_.Contains(now.ns)){
            if(now.ns>=window_.StartNs()){
                auto collecting=Window::Collecting;phase_.compare_exchange_strong(collecting,Window::Expired);
            }else ClockFailure();
            return;
        }
        const auto slot=reserved_.fetch_add(1,std::memory_order_relaxed);
        if(slot>=Capacity){
            capped_.fetch_add(1,std::memory_order_relaxed);dropped_.fetch_add(1,std::memory_order_relaxed);
            auto collecting=Window::Collecting;phase_.compare_exchange_strong(collecting,Window::Capped);return;
        }
        token.slot=slot;
    }
public:
    bool Configure(std::int64_t frequency)noexcept {
        if(configured_||closed_.load()||frequency<=0||frequency>INT64_MAX/20)return false;
        configured_=true;frequency_=frequency;return window_.Defer();
    }
    bool Start()noexcept {
        if(!configured_||closed_.load()||phase_.load()!=Window::Off)return false;
        window_.Start(1);phase_.store(Window::Armed,std::memory_order_release);return true;
    }
    void Close(Stamp now={})noexcept {
        if(!closed_.exchange(true,std::memory_order_acq_rel))closedAt_.store(now.qpc,std::memory_order_release);
    }
    bool Admitting()const noexcept {
        const auto phase=phase_.load(std::memory_order_acquire);
        return !closed_.load(std::memory_order_acquire)&&
            (phase==Window::Armed||phase==Window::Opening||phase==Window::Collecting);
    }
    void ClockFailure()noexcept {clockErrors_.fetch_add(1,std::memory_order_relaxed);Close();}
    Token Begin(Kind kind,ThreadState& thread,Stamp now,Identity who,unsigned self,unsigned caller)noexcept {
        Token token{};const auto phase=phase_.load(std::memory_order_acquire);
        if(closed_.load(std::memory_order_acquire)||
           (phase!=Window::Armed&&phase!=Window::Opening&&phase!=Window::Collecting))return token;
        if(now.qpc<=0||now.ns<=0||!who.thread||!who.birth){
            if(phase==Window::Collecting)dropped_.fetch_add(1,std::memory_order_relaxed);
            ClockFailure();return token;
        }
        token.active=true;token.previous=thread;auto& row=token.row;
        row.invocation=serial_.fetch_add(1,std::memory_order_relaxed)+1;
        row.parent=thread.current;row.originalInput=thread.originalInput;row.kind=kind;
        row.thread=who;row.begin=now.qpc;row.self=self;row.caller=caller;
        thread.current=row.invocation;
        if(kind==Kind::Input){++thread.inputDepth;inputs_.fetch_add(1,std::memory_order_relaxed);}
        else {++thread.consumerDepth;consumers_.fetch_add(1,std::memory_order_relaxed);}
        row.inputDepth=thread.inputDepth;row.consumerDepth=thread.consumerDepth;
        row.beginActiveInput=inputs_.load(std::memory_order_relaxed);
        row.beginActiveConsumer=consumers_.load(std::memory_order_relaxed);
        Reserve(token,now);return token;
    }
    void AcceptedLocal(Token& token,Stamp now,unsigned inputThread,std::uint64_t actor,std::uint64_t epoch)noexcept {
        if(!token.active||token.row.kind!=Kind::Input)return;
        if(now.qpc<=0||now.ns<=0||now.qpc>INT64_MAX-frequency_*20){ClockFailure();return;}
        auto& row=token.row;row.acceptedLocal=true;row.acceptedAt=now.qpc;
        row.inputThread=inputThread;row.actorGeneration=actor;row.inputEpoch=epoch;
        if(closed_.load(std::memory_order_acquire))return;
        auto armed=Window::Armed;
        if(phase_.compare_exchange_strong(armed,Window::Opening,std::memory_order_acq_rel)){
            PublishOpening(now);
        }
        Reserve(token,now); // First accepted input retains its actual entry bookend.
    }
    void OriginalEnter(Token& token,ThreadState& thread,Stamp now)noexcept {
        if(!token.active)return;token.row.originalEnter=now.qpc;
        if(token.row.kind==Kind::Input)thread.originalInput=token.row.invocation;
    }
    void OriginalReturn(Token& token,ThreadState& thread,Stamp now)noexcept {
        if(!token.active)return;token.row.originalReturn=now.qpc;
        if(token.row.kind==Kind::Input)thread.originalInput=token.previous.originalInput;
    }
    void End(Token& token,ThreadState& thread,Stamp now,bool normal,unsigned result=0)noexcept {
        if(!token.active)return;auto& row=token.row;
        row.end=now.qpc;row.result=result;row.abnormal=!normal;
        row.clockError=now.qpc<row.begin||(row.originalEnter&&row.originalEnter<row.begin)||
            (row.originalReturn&&row.originalReturn<row.originalEnter)||
            (normal&&(!row.originalEnter||!row.originalReturn||row.end<row.originalReturn));
        if(row.clockError)ClockFailure();
        row.endInputDepth=thread.inputDepth;row.endConsumerDepth=thread.consumerDepth;
        row.endActiveInput=inputs_.load(std::memory_order_relaxed);
        row.endActiveConsumer=consumers_.load(std::memory_order_relaxed);
        row.crossedDeadline=window_.StartNs()>0&&!window_.Contains(now.ns);
        if(token.slot==Capacity)RecordUnrecorded(row);
        thread=token.previous;
        if(row.kind==Kind::Input)inputs_.fetch_sub(1,std::memory_order_relaxed);
        else consumers_.fetch_sub(1,std::memory_order_relaxed);
        if(token.slot<Capacity){
            rows_[token.slot].row=row;
            rows_[token.slot].published.store(true,std::memory_order_release);
        }
        token.active=false;
    }
    bool Read(unsigned index,Row& out)const noexcept {
        if(index>=Capacity||!rows_[index].published.load(std::memory_order_acquire))return false;
        out=rows_[index].row;return true; // Published rows are immutable forever.
    }
    Counts Snapshot()const noexcept {
        Counts c{};c.window=phase_.load(std::memory_order_acquire);c.closed=closed_.load(std::memory_order_acquire);
        c.unrecordedOverlap=unrecordedOverlap_.load(std::memory_order_acquire);
        const auto offered=reserved_.load(std::memory_order_acquire);c.reserved=offered<Capacity?offered:Capacity;
        c.capped=capped_.load();c.dropped=dropped_.load();c.clockErrors=clockErrors_.load();c.activeInput=inputs_.load();c.activeConsumer=consumers_.load();
        c.frequency=frequency_;c.activation=activation_.load(std::memory_order_acquire);
        c.deadline=c.activation?c.activation+frequency_*20:0;c.closedAt=closedAt_.load(std::memory_order_acquire);return c;
    }
    bool Drained(bool hookDisabled,unsigned callbacks)const noexcept {
        return closed_.load(std::memory_order_acquire)&&hookDisabled&&!callbacks&&!inputs_.load()&&!consumers_.load();
    }
};
} // namespace fvr::bc2::queuePhase
