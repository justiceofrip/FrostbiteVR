#pragma once
#include <cstdint>
#include <atomic>

namespace fvr::runtime {
struct RenderOwner {
    std::uint64_t world=0,request=0;
    bool operator==(const RenderOwner&)const=default;
    explicit operator bool()const noexcept{return world&&request;}
};
enum class RecoveryReadiness {Unchanged,Invalid,WaitForBoundary,DrainTransaction,Ready};
inline bool ShouldCancelUnwritten(std::uint64_t requested,std::uint64_t current,bool unwritten)noexcept {
    return requested&&requested==current&&unwritten;
}
// A rejected lease can reach visibility before the graphics thread drains its
// cancellation. Gate the whole camera-write callback; leave the token intact
// for that drain. A cancellation for an older request cannot reject a new one.
template<class Apply>
bool ApplyUnwrittenFrame(std::uint64_t requested,std::uint64_t current,Apply&& apply)noexcept {
    if(!current||ShouldCancelUnwritten(requested,current,true))return false;
    apply();return true;
}
template<class Finish>
bool DrainUnwrittenFrameCancellation(std::atomic<std::uint64_t>& requested,
    std::uint64_t current,bool unwritten,Finish&& finish)noexcept {
    auto token=requested.load(std::memory_order_acquire);
    if(!ShouldCancelUnwritten(token,current,unwritten))return false;
    // Clearing first would let visibility apply the rejected camera while its
    // graphics-thread cancellation is still in progress. Preserve a newer token.
    finish();requested.compare_exchange_strong(token,0,std::memory_order_acq_rel);
    return true;
}
// An address is not a generation. Observe a freshly verified replacement owner
// and two advancing frames of THAT owner, without comparing its counter to the
// retired world's frame. Native graph validation remains the adapter's job.
class RenderOwnerRecovery {
public:
    RecoveryReadiness Observe(RenderOwner bound,RenderOwner current,std::uint32_t frame,
        bool verified,bool transactionOutstanding,bool restorationFailed)noexcept {
        if(bound==current){Reset();return RecoveryReadiness::Unchanged;}
        if(!bound||!current||!verified||restorationFailed){Reset();return RecoveryReadiness::Invalid;}
        if(current!=candidate_){candidate_=current;first_=frame;return RecoveryReadiness::WaitForBoundary;}
        const auto elapsed=std::uint32_t(frame-first_);
        if(elapsed>=0x80000000u){first_=frame;return RecoveryReadiness::WaitForBoundary;}
        if(elapsed<2)return RecoveryReadiness::WaitForBoundary;
        return transactionOutstanding?RecoveryReadiness::DrainTransaction:RecoveryReadiness::Ready;
    }
    void Reset()noexcept{candidate_={};first_=0;}
private:
    RenderOwner candidate_{};std::uint32_t first_=0;
};

enum class OwnerTransitionResult {Busy,Complete,Failed};
// One update owner publishes a verified native rebuild. One graphics owner
// retires the old sources before allowing new camera admission. Pending data
// stays immutable until the graphics owner publishes its terminal result.
class RenderOwnerTransition {
public:
    enum class State : unsigned {Idle,Pending,Applying,Complete,Failed};
    struct Request {
        RenderOwner oldOwner{},newOwner{};std::uint64_t frame=0,deviceEpoch=0;
    };
    bool Arm(const Request& request)noexcept {
        const auto state=state_.load(std::memory_order_acquire);
        if((state!=State::Idle&&state!=State::Complete)||!request.oldOwner||!request.newOwner||
           request.oldOwner==request.newOwner||!request.frame||!request.deviceEpoch||
           (state==State::Complete&&(request.oldOwner!=request_.newOwner||request.deviceEpoch!=request_.deviceEpoch)))return false;
        request_=request;state_.store(State::Pending,std::memory_order_release);return true;
    }
    bool Settled()const noexcept {
        const auto state=state_.load(std::memory_order_acquire);return state==State::Idle||state==State::Complete;
    }
    bool AllowsNativeRetirement(bool sampling)const noexcept{return !sampling||Settled();}
    bool Allows(RenderOwner owner)const noexcept {
        const auto state=state_.load(std::memory_order_acquire);
        return state==State::Idle||(state==State::Complete&&owner==request_.newOwner);
    }
    template<class Apply> void Service(bool graphicsOwner,Apply&& apply)noexcept {
        if(!graphicsOwner)return;
        auto expected=State::Pending;
        if(!state_.compare_exchange_strong(expected,State::Applying,std::memory_order_acq_rel))return;
        const auto result=apply(request_);
        state_.store(result==OwnerTransitionResult::Complete?State::Complete:
            result==OwnerTransitionResult::Busy?State::Pending:State::Failed,std::memory_order_release);
    }
    State Status()const noexcept{return state_.load(std::memory_order_acquire);}
    // Update-owner rejection before publishing a request. Never overwrite work
    // already owned by the graphics thread.
    void Reject()noexcept {if(Settled())state_.store(State::Failed,std::memory_order_release);}
private:
    Request request_{};std::atomic<State> state_=State::Idle;
};

enum class RetirementStep {Untouched,OwnerRetained,RootReleased,RegistryRetired,Complete};
// Native destruction can release the owner before it has finished callbacks.
// The temporary owner reference spans root destruction AND registry retirement.
// A failed postcondition retains that reference and is never retried implicitly.
template<class Calls>
bool RetireOwnedRenderRoot(Calls& calls,RetirementStep& step)noexcept {
    if(step!=RetirementStep::Untouched||!calls.RetainOwner())return false;
    step=RetirementStep::OwnerRetained;
    if(!calls.ReleaseRoot())return false;
    step=RetirementStep::RootReleased;
    if(!calls.Detached()||!calls.RetireRegistry())return false;
    step=RetirementStep::RegistryRetired;
    if(!calls.ReleaseOwner())return false;
    step=RetirementStep::Complete;return true;
}
}
