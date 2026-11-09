#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace fvr::bc2::campaignProvisioning {
enum class InputHoldKind:std::uint64_t {None,Update,Gather,Control};
enum class InputHoldPhase:std::uint64_t {None,Hook,Original,Returned,Releasing};
// All fixed-width words: independent private diagnostic wire data, never authority.
struct InputHoldSnapshot {
    std::uint64_t coherent=0,revision=0,generation=0,thread=0,kind=0,phase=0;
    std::uint64_t acquiredNs=0,changedNs=0,updateDepth=0,gatherDepth=0,controlDepth=0;
    std::uint64_t updateOriginalDepth=0,gatherOriginalDepth=0,releasedGeneration=0;
};
struct InputHoldAttempt {
    std::uint64_t sampled=0,bracketed=0,beforeNs=0,afterNs=0;
    InputHoldSnapshot before{},after{};
};
static_assert(sizeof(InputHoldSnapshot)==112&&sizeof(InputHoldAttempt)==256);

// One writer at a time, exclusively under the EXISTING input mutex. Readers
// use only lock-free atomics; one read attempt, no spin, retry, or native read.
// seq_cst on every word gives the single-pass sequence check a C++ memory-model
// proof (a plain non-atomic seqlock payload would instead be a data race).
class InputHoldDiagnostics {
public:
    using Thread=std::uint32_t(*)()noexcept;
    using Clock=std::int64_t(*)()noexcept;
    struct Scope {InputHoldSnapshot previous{};InputHoldKind kind=InputHoldKind::None;bool active=false,original=false;};
    explicit InputHoldDiagnostics(Thread thread=nullptr,Clock clock=nullptr):thread_(thread),clock_(clock){}
    Scope Enter(InputHoldKind kind)noexcept {
        if(!thread_||!clock_)return {};
        Scope token{current_,kind,true,false};
        if(!depth_){
            if(nextGeneration_==std::numeric_limits<std::uint64_t>::max()){Disable();return {};}
            current_={};current_.generation=++nextGeneration_;current_.thread=thread_();current_.acquiredNs=Now();
        }
        ++depth_;current_.kind=std::uint64_t(kind);current_.phase=std::uint64_t(InputHoldPhase::Hook);
        if(kind==InputHoldKind::Update)++current_.updateDepth;
        if(kind==InputHoldKind::Gather)++current_.gatherDepth;
        if(kind==InputHoldKind::Control)++current_.controlDepth;
        Publish();return token;
    }
    void OriginalEnter(Scope& token)noexcept {
        if(!token.active||token.original)return;
        token.original=true;current_.phase=std::uint64_t(InputHoldPhase::Original);
        if(token.kind==InputHoldKind::Update)++current_.updateOriginalDepth;
        if(token.kind==InputHoldKind::Gather)++current_.gatherOriginalDepth;
        Publish();
    }
    void OriginalReturned(Scope& token)noexcept {
        if(!token.active||!token.original)return;
        token.original=false;current_.phase=std::uint64_t(InputHoldPhase::Returned);
        if(token.kind==InputHoldKind::Update)--current_.updateOriginalDepth;
        if(token.kind==InputHoldKind::Gather)--current_.gatherOriginalDepth;
        Publish();
    }
    std::uint64_t Leaving(Scope& token)noexcept {
        if(!token.active)return 0;
        token.active=false;
        if(--depth_){current_=token.previous;Publish();return 0;}
        // This is BEFORE unlock, deliberately not called a released mutex.
        current_.phase=std::uint64_t(InputHoldPhase::Releasing);Publish();return current_.generation;
    }
    void Released(std::uint64_t generation)noexcept {
        // AFTER the real outer unlock. This single tagged word may be an older
        // completion if its thread was descheduled after unlock; never infer
        // newest-generation order, duration, or an idle gate from its value.
        if(generation)released_.store(generation);
    }
    std::uint64_t Now()const noexcept{return clock_?std::uint64_t(clock_()):0;}
    InputHoldSnapshot Read()const noexcept {
        InputHoldSnapshot out{};const auto first=revision_.load();
        if(!first||(first&1))return out;
        std::array<std::uint64_t,11> words{};
        for(unsigned i=0;i<words.size();++i)words[i]=words_[i].load();
        const auto released=released_.load();const auto last=revision_.load();
        if(first!=last)return out;
        out={1,first,words[0],words[1],words[2],words[3],words[4],words[5],
            words[6],words[7],words[8],words[9],words[10],released};return out;
    }
    static bool Bracketed(const InputHoldAttempt& attempt)noexcept {
        const auto& a=attempt.before;const auto& b=attempt.after;
        return attempt.sampled&&a.coherent&&b.coherent&&a.revision==b.revision&&a.thread&&a.generation&&
            a.releasedGeneration==b.releasedGeneration&&a.releasedGeneration!=a.generation&&
            a.phase!=std::uint64_t(InputHoldPhase::Releasing)&&attempt.beforeNs&&attempt.afterNs>=attempt.beforeNs;
    }
private:
    void Disable()noexcept{disabled_=true;revision_.store(std::numeric_limits<std::uint64_t>::max());}
    void Publish()noexcept {
        if(disabled_)return;
        if(revisionValue_>=std::numeric_limits<std::uint64_t>::max()-2){Disable();return;}
        current_.changedNs=Now();revision_.store(++revisionValue_);
        const std::array<std::uint64_t,11> values{current_.generation,current_.thread,current_.kind,current_.phase,
            current_.acquiredNs,current_.changedNs,current_.updateDepth,current_.gatherDepth,current_.controlDepth,
            current_.updateOriginalDepth,current_.gatherOriginalDepth};
        for(unsigned i=0;i<values.size();++i)words_[i].store(values[i]);
        revision_.store(++revisionValue_);
    }
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    Thread thread_=nullptr;Clock clock_=nullptr;
    InputHoldSnapshot current_{};std::uint64_t depth_=0,nextGeneration_=0,revisionValue_=0;bool disabled_=false;
    std::atomic<std::uint64_t> revision_{0},released_{0};
    std::array<std::atomic<std::uint64_t>,11> words_{};
};
}
