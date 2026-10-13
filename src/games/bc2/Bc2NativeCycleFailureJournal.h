#pragma once
#include "Bc2NativeCycleService.h"
#include <atomic>
#include <ostream>

namespace fvr::bc2 {
// One immutable diagnostic publication per module lifetime. The native callback
// copies values only; file IO and formatting belong to the probe worker. Readers
// do not acquire the gameplay policy gate or renew any native/input authority.
struct NativeCycleFirstFailure {
    Bc2NativeCycleView view{};
    Bc2NativeCyclePhase previousPhase=Bc2NativeCyclePhase::Watching;
    Bc2NativeCycleMode mode=Bc2NativeCycleMode::Spas;
    interaction::HandInteractionSample control{};
    std::int64_t observedNs=0;
    std::uint64_t cancelEpoch=0,contention=0;
    unsigned thread=0;
};
class NativeCycleFailureJournal {
public:
    bool Capture(const NativeCycleFirstFailure& value)noexcept {
        if(value.view.phase!=Bc2NativeCyclePhase::Cancelled||
           value.view.failure==Bc2NativeCycleFailure::None||value.observedNs<=0)return false;
        unsigned empty=0;
        if(!state_.compare_exchange_strong(empty,1,std::memory_order_relaxed))return false;
        value_=value;
        state_.store(2,std::memory_order_release);return true;
    }
    std::optional<NativeCycleFirstFailure> Read()const noexcept {
        if(state_.load(std::memory_order_acquire)!=2)return {};
        return value_;
    }
    static void WriteJson(std::ostream& out,const NativeCycleFirstFailure& f) {
        const auto& v=f.view;const auto& n=v.native;const auto& c=f.control;
        out<<"{\"schema\":\"fvr.bc2.native-cycle-first-failure.v1\",\"observed_ns\":"<<f.observedNs
           <<",\"thread\":"<<f.thread<<",\"mode\":"<<unsigned(f.mode)<<",\"phase_before\":"<<unsigned(f.previousPhase)
           <<",\"phase\":"<<unsigned(v.phase)<<",\"failure\":"<<unsigned(v.failure)
           <<",\"invocation\":"<<v.failureInvocation<<",\"cycle\":"<<v.cycle<<",\"shot\":"<<v.shot
           <<",\"blocks_fire\":"<<(v.blocksFire?"true":"false")<<",\"loaded\":"<<v.loaded<<",\"reserve\":"<<v.reserve
           <<",\"capacity\":"<<v.capacity<<",\"cancel_epoch\":"<<f.cancelEpoch<<",\"contention\":"<<f.contention
           <<",\"owner\":{\"player\":"<<n.owner.player<<",\"soldier\":"<<n.owner.soldier<<",\"weapon\":"<<n.owner.weapon
           <<",\"weak\":"<<n.owner.weak<<",\"actor_generation\":"<<n.owner.actorGeneration
           <<",\"equip_generation\":"<<n.owner.equipGeneration<<",\"space\":"<<n.owner.space<<'}'
           <<",\"firing\":["<<n.firing[0]<<','<<n.firing[1]<<','<<n.firing[2]<<']'
           <<",\"controller\":{\"sequence\":"<<c.sequence<<",\"observed_ns\":"<<c.observedNs
           <<",\"deadline_ns\":"<<c.deadlineNs<<",\"processing_ns\":"<<c.nowNs
           <<",\"focused\":"<<(c.focused?"true":"false")<<",\"tracked\":["<<(c.tracked[0]?"true":"false")
           <<','<<(c.tracked[1]?"true":"false")<<"],\"released\":["<<(c.released[0]?"true":"false")
           <<','<<(c.released[1]?"true":"false")<<"]}}\n";
    }
private:
    std::atomic<unsigned> state_{0};
    NativeCycleFirstFailure value_{};
};
}
