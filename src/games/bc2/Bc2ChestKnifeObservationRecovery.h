#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace fvr::bc2::chestKnifeNative {
// Current readiness is separate from the never-cleared historical gap. Only
// idle, normally returned, unmodified callbacks may reach a fresh rejoin.
// One atomic packs an idle epoch, gap revision, in-flight count and flags.
// Exhaustion refuses recovery; there is no allocation, wait or callback replay.
class ObservationRecovery {
    static constexpr std::uint64_t Idle=1,Needs=2,Blocked=4,HadGap=8,Recovered=16;
    static constexpr std::uint64_t Pending=0xff00,Revision=0xffff0000,Epoch=0xffffffff00000000ull;
public:
    struct State {
        std::uint64_t stamp=0,epoch=0,revision=0;
        std::int64_t completedNs=0;
        bool ready=false,pending=false,blocked=false,recovered=false,needsRejoin=false;
    };
    State Read()const noexcept {
        const auto v=state_.load(std::memory_order_acquire);
        return {v,v>>32,(v&Revision)>>16,completed_.load(std::memory_order_acquire),
            bool(v&Idle)&&!(v&(Needs|Blocked|Pending)),bool(v&Pending),bool(v&Blocked),bool(v&Recovered),bool(v&Needs)};
    }
    void Reset()noexcept {
        auto v=state_.load(std::memory_order_acquire);
        for(;;){const auto next=(v&Epoch)==Epoch?v|Blocked:((v&Epoch)+(1ull<<32));
            if(state_.compare_exchange_weak(v,next,std::memory_order_acq_rel))return;}
    }
    void IdleNow()noexcept {state_.fetch_or(Idle,std::memory_order_acq_rel);}
    void Unknown()noexcept {state_.fetch_or(Blocked|HadGap|Needs,std::memory_order_acq_rel);}
    // Every transaction/lifetime transition ends the idle epoch before it can
    // perform native work. A callback crossing that boundary cannot recover.
    void Revoke(std::int64_t now)noexcept {
        Completed(now);
        auto v=state_.load(std::memory_order_acquire);
        for(;;){const bool exhausted=(v&Epoch)==Epoch;
            const auto flags=(v&HadGap)|((v&(Blocked|Needs|Pending))||exhausted?Blocked:0)|((v&HadGap)?Needs:0);
            const auto next=exhausted?(v|Blocked)&~Idle:((v&Epoch)+(1ull<<32))|(v&Revision)|flags;
            if(state_.compare_exchange_weak(v,next,std::memory_order_acq_rel))return;}
    }
    bool TakeReady(std::int64_t)noexcept {
        auto v=state_.load(std::memory_order_acquire);
        for(;;){
            if(!(v&Idle)||(v&(Needs|Blocked|Pending))||(v&Epoch)==Epoch)return false;
            const auto next=((v&Epoch)+(1ull<<32))|(v&Revision)|(v&(HadGap|Recovered));
            if(state_.compare_exchange_weak(v,next,std::memory_order_acq_rel))return true;
        }
    }
    std::uint64_t IdleEpoch()const noexcept {
        const auto s=Read();return s.epoch&&!s.blocked&&(s.stamp&Idle)?s.epoch:0;
    }
    // expectedEpoch is supplied by a successful Enter for a Leave lock miss.
    // A missed Enter uses zero and captures the current idle epoch atomically.
    std::uint64_t Miss(std::uint64_t expectedEpoch=0)noexcept {
        auto v=state_.load(std::memory_order_acquire);
        for(;;){const bool eligible=(v&Idle)&&!(v&Blocked)&&(v>>32)&&
                (!expectedEpoch||expectedEpoch==(v>>32))&&(v&Pending)!=Pending&&(v&Revision)!=Revision;
            const auto next=eligible?((v+0x10100ull)|Needs|HadGap)&~Recovered:v|Blocked|Needs|HadGap;
            if(state_.compare_exchange_weak(v,next,std::memory_order_acq_rel))return eligible?v>>32:0;}
    }
    void Returned(std::uint64_t epoch,std::int64_t now,bool normal)noexcept {
        if(!epoch)return;
        // Publish before retiring the final pending callback. This watermark
        // may conservatively delay another epoch's rejoin; it is not the
        // immutable completion proof of an already successful rejoin.
        Completed(now);
        auto v=state_.load(std::memory_order_acquire);
        for(;;){if((v>>32)!=epoch)return; // Retired callbacks cannot alter current state.
            const auto next=(v&Pending)?(v-0x100ull)|(normal?0:Blocked):v|Blocked;
            if(state_.compare_exchange_weak(v,next,std::memory_order_acq_rel))return;}
    }
    bool Rejoin(const State& captured,std::int64_t sourceObservedNs)noexcept {
        if(!captured.epoch||!captured.needsRejoin||captured.pending||captured.blocked||
           !(captured.stamp&Idle)||captured.completedNs<=0||sourceObservedNs<=captured.completedNs)return false;
        auto expected=captured.stamp;
        return state_.compare_exchange_strong(expected,(expected&~Needs)|Recovered,std::memory_order_acq_rel);
    }
private:
    void Completed(std::int64_t now)noexcept {
        auto previous=completed_.load(std::memory_order_relaxed);
        while(previous<now&&!completed_.compare_exchange_weak(previous,now,std::memory_order_release,std::memory_order_relaxed)){}
    }
    std::atomic<std::uint64_t> state_{1ull<<32};
    std::atomic<std::int64_t> completed_{0};
};
struct ObservationRecoveryProof {
    ObservationRecovery::State state{};
    std::uint64_t lifetime=0,bodyOperation=0,input=0;
    std::int64_t observedNs=0,nowNs=0;
};
// The native gate owns this proof, created only after a successful Rejoin from
// its captured state. Keep that completion bound; a retired callback may still
// advance the shared watermark after Rejoin, including before proof creation.
// TakeReady carries Recovered across an accepted attack in this same hold.
// Reset/Revoke/Miss clear or block recovery. Every current flag/stamp/epoch
// remains live.
inline ObservationRecoveryProof RefreshObservationRecoveryProof(ObservationRecoveryProof proof,
    const ObservationRecovery::State& current,std::uint64_t lifetime,std::uint64_t bodyOperation)noexcept {
    const auto completed=proof.state.completedNs;
    const bool retained=current.recovered&&current.revision&&current.revision==proof.state.revision&&
        proof.lifetime&&proof.lifetime==lifetime&&proof.bodyOperation&&proof.bodyOperation==bodyOperation&&
        completed>0&&proof.observedNs>completed;
    proof.state=current;
    if(retained)proof.state.completedNs=completed;
    return proof;
}
}
