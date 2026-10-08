#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace fvr::bc2 {
enum class NativeCycleCallbackKind {Update,Commit,Restore,Transfer};

// One state per selected firing branch. This observes concurrency; it never
// waits for, excludes, or serializes an original native callback. A conflict
// invalidates the owning sample even if the competing callback already ended.
struct NativeCycleCallbackState {
    std::atomic<unsigned> active{0};
    std::atomic<std::uint64_t> conflicts{0};
    std::atomic<bool> exhausted{false};
};
class NativeCycleCallbackScope {
public:
    NativeCycleCallbackScope(NativeCycleCallbackState* state,std::uint32_t firing,
        std::uint32_t thread,NativeCycleCallbackKind kind,
        const NativeCycleCallbackScope* parent=nullptr,std::uint64_t parentInvocation=0)noexcept:
        state_(state),firing_(firing),thread_(thread),kind_(kind) {
        if(!state_||!firing_||!thread_)return;
        // Only the direct Commit child of this exact same-thread Update may
        // share its observation. Its native record must still pass CycleCommit.
        if(kind==NativeCycleCallbackKind::Commit&&parent&&parent->state_==state_&&
           parent->firing_==firing&&parent->thread_==thread&&
           parent->kind_==NativeCycleCallbackKind::Update&&parentInvocation&&
           parent->invocation_==parentInvocation&&parent->Isolated()){
            parent_=parent;return;
        }
        revision_=state_->conflicts.load(std::memory_order_acquire);
        counted_=true;
        const auto prior=state_->active.fetch_add(1,std::memory_order_acq_rel);
        if(prior){
            const auto revision=state_->conflicts.fetch_add(1,std::memory_order_acq_rel);
            if(revision==std::numeric_limits<std::uint64_t>::max())state_->exhausted.store(true,std::memory_order_release);
        }
        if(prior==std::numeric_limits<unsigned>::max())state_->exhausted.store(true,std::memory_order_release);
    }
    ~NativeCycleCallbackScope(){if(counted_)state_->active.fetch_sub(1,std::memory_order_acq_rel);}
    NativeCycleCallbackScope(const NativeCycleCallbackScope&)=delete;
    NativeCycleCallbackScope& operator=(const NativeCycleCallbackScope&)=delete;
    void BindInvocation(std::uint64_t invocation)noexcept {if(!invocation_)invocation_=invocation;}
    bool Isolated()const noexcept {
        if(!state_||!invocation_||state_->exhausted.load(std::memory_order_acquire))return false;
        if(parent_)return parent_->Isolated();
        return counted_&&state_->active.load(std::memory_order_acquire)==1&&
            state_->conflicts.load(std::memory_order_acquire)==revision_;
    }
private:
    NativeCycleCallbackState* state_=nullptr;
    const NativeCycleCallbackScope* parent_=nullptr;
    std::uint64_t revision_=0,invocation_=0;
    std::uint32_t firing_=0,thread_=0;
    NativeCycleCallbackKind kind_{};
    bool counted_=false;
};
}
