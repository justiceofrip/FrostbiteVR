#pragma once
#include <atomic>
#include <optional>
#include <type_traits>
namespace fvr::graphics {
// One lifetime: the native graphics callback captures plain borrowed metadata.
// A setup worker only reads the published value; it never invokes the graphics
// interfaces or owns references. Cancellation cannot admit a late completion.
// Keep this object and callback code alive until all installed hooks retire.
template<class Snapshot> class RenderBootstrap {
    static_assert(std::is_trivially_copyable_v<Snapshot>);
public:
    enum class Phase {Waiting,Capturing,Ready,Failed,Cancelled};
private:
    std::atomic<Phase> phase_{Phase::Waiting};
    Snapshot snapshot_{};
public:
    Phase State()const noexcept{return phase_.load(std::memory_order_acquire);}
    template<class Capture> void Observe(Capture&& capture)noexcept {
        auto expected=Phase::Waiting;
        if(!phase_.compare_exchange_strong(expected,Phase::Capturing,std::memory_order_acq_rel))return;
        Snapshot next{};
        const bool okay=capture(next); // All temporary native references die here.
        if(okay)snapshot_=next;
        expected=Phase::Capturing;
        phase_.compare_exchange_strong(expected,okay?Phase::Ready:Phase::Failed,std::memory_order_release);
    }
    std::optional<Snapshot> Read()const noexcept {
        if(State()!=Phase::Ready)return std::nullopt;
        const auto copy=snapshot_;
        if(State()!=Phase::Ready)return std::nullopt;
        return copy;
    }
    void Cancel()noexcept {phase_.exchange(Phase::Cancelled,std::memory_order_acq_rel);}
};
}
