#pragma once
#include <cstdint>

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
