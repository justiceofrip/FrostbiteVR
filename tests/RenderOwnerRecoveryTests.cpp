#include "fvr/runtime/RenderOwnerRecovery.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <array>
using namespace fvr::runtime;
void Check(bool value){if(!value)std::abort();}
void ReplacementFrameCounter(){
    RenderOwnerRecovery p;const RenderOwner old{1,2},fresh{3,4};
    Check(p.Observe(old,fresh,1,true,false,false)==RecoveryReadiness::WaitForBoundary);
    Check(p.Observe(old,fresh,1,true,false,false)==RecoveryReadiness::WaitForBoundary);
    Check(p.Observe(old,fresh,2,true,false,false)==RecoveryReadiness::WaitForBoundary);
    Check(p.Observe(old,fresh,3,true,false,false)==RecoveryReadiness::Ready);
    // Main-view address reuse is deliberately not an owner identity input.
    Check(p.Observe(old,old,999999,true,false,false)==RecoveryReadiness::Unchanged);
    Check(p.Observe(old,fresh,4,true,false,false)==RecoveryReadiness::WaitForBoundary);
}
void ChurnAndRestoration(){
    RenderOwnerRecovery p;const RenderOwner old{1,2},a{3,4},b{5,6};
    p.Observe(old,a,100,true,false,false);
    Check(p.Observe(old,b,103,true,false,false)==RecoveryReadiness::WaitForBoundary);
    Check(p.Observe(old,b,105,true,true,false)==RecoveryReadiness::DrainTransaction);
    Check(p.Observe(old,b,106,true,false,false)==RecoveryReadiness::Ready);
    Check(p.Observe(old,b,107,true,false,true)==RecoveryReadiness::Invalid);
    Check(p.Observe(old,b,108,true,false,false)==RecoveryReadiness::WaitForBoundary);
    Check(p.Observe(old,b,110,false,false,false)==RecoveryReadiness::Invalid);
}
void WrapAndRegression(){
    RenderOwnerRecovery p;const RenderOwner old{1,2},fresh{3,4};
    p.Observe(old,fresh,0xfffffffe,true,false,false);
    Check(p.Observe(old,fresh,0,true,false,false)==RecoveryReadiness::Ready);
    p.Reset();p.Observe(old,fresh,100,true,false,false);
    Check(p.Observe(old,fresh,99,true,false,false)==RecoveryReadiness::WaitForBoundary);
    Check(p.Observe(old,fresh,101,true,false,false)==RecoveryReadiness::Ready);
}
struct Calls {
    unsigned failure=99,refs=7,nodes=7;bool rootReleased=false;std::vector<unsigned> order;
    bool RetainOwner()noexcept{order.push_back(0);if(failure==0)return false;++refs;return true;}
    bool ReleaseRoot()noexcept{order.push_back(1);if(failure==1)return false;rootReleased=true;refs-=nodes;nodes=0;Check(refs==1);return true;}
    bool Detached()noexcept{order.push_back(2);return failure!=2&&rootReleased&&nodes==0&&refs==1;}
    bool RetireRegistry()noexcept{order.push_back(3);Check(refs==1);return failure!=3;}
    bool ReleaseOwner()noexcept{order.push_back(4);if(failure==4)return false;Check(rootReleased&&nodes==0&&refs==1);--refs;return true;}
};
void OwnerOutlivesCallbacks(){
    Calls c;auto step=RetirementStep::Untouched;Check(RetireOwnedRenderRoot(c,step));
    Check(step==RetirementStep::Complete&&c.refs==0&&c.order==std::vector<unsigned>({0,1,2,3,4}));
    Check(!RetireOwnedRenderRoot(c,step)&&c.order.size()==5);
}
void FailedRetirementNeverRetries(){
    for(unsigned fail=0;fail<5;++fail){Calls c;c.failure=fail;auto step=RetirementStep::Untouched;
        Check(!RetireOwnedRenderRoot(c,step));Check(c.order.size()==fail+1);
        if(fail){const auto size=c.order.size();Check(!RetireOwnedRenderRoot(c,step)&&c.order.size()==size&&c.refs>=1);}
    }
}
void CancellationKeepsRequestIdentity(){
    Check(ShouldCancelUnwritten(8,8,true));
    Check(!ShouldCancelUnwritten(8,9,true)); // old rejection delivered after a new lease
    Check(!ShouldCancelUnwritten(8,8,false)); // written cameras need exact restoration
    Check(!ShouldCancelUnwritten(0,0,true));
}
void CancelledVisibilityWritesNothing(){
    std::array<std::uint8_t,0x460> camera{},written{};camera.fill(0x37);written.fill(0xa5);
    const auto before=camera;unsigned writes=0;
    auto apply=[&]()noexcept{camera=written;++writes;};
    // The graphics thread has not consumed the matching cancellation yet.
    // The same production gate wraps every camera write in ApplyTrackedView.
    Check(!ApplyUnwrittenFrame(41,41,apply));Check(writes==0&&camera==before);
    Check(!ApplyUnwrittenFrame(41,41,apply));Check(writes==0&&camera==before);
    Check(!ApplyUnwrittenFrame(0,0,apply));Check(writes==0&&camera==before);
    // A later lease is admitted without clearing or reusing the old token.
    Check(ApplyUnwrittenFrame(41,42,apply));Check(writes==1&&camera==written);
    camera=before;Check(ApplyUnwrittenFrame(0,43,apply));Check(writes==2&&camera==written);
}
void CancellationRemainsVisibleUntilFinished(){
    std::atomic<std::uint64_t> pending=8;unsigned writes=0,finishes=0;
    Check(DrainUnwrittenFrameCancellation(pending,8,true,[&]()noexcept{
        ++finishes;Check(pending.load()==8);
        Check(!ApplyUnwrittenFrame(pending.load(),8,[&]()noexcept{++writes;}));
        pending.store(9); // A later rejection arriving during the drain survives.
    }));
    Check(writes==0&&finishes==1&&pending.load()==9);
    Check(!DrainUnwrittenFrameCancellation(pending,8,true,[&]()noexcept{++finishes;}));
    Check(!DrainUnwrittenFrameCancellation(pending,9,false,[&]()noexcept{++finishes;}));
    Check(finishes==1&&pending.load()==9); // Written cameras require their exact-restore path.
    Check(DrainUnwrittenFrameCancellation(pending,9,true,[&]()noexcept{
        ++finishes;Check(!ApplyUnwrittenFrame(pending.load(),9,[&]()noexcept{++writes;}));
    }));
    Check(writes==0&&finishes==2&&pending.load()==0);
}
void GraphicsDrainKeepsAdmissionBlocked(){
    RenderOwnerTransition transition;const RenderOwner old{1,2},fresh{3,4},later{5,6};unsigned calls=0;
    Check(transition.Allows(old)&&transition.Settled());
    Check(!transition.Arm({old,old,1,1}));Check(!transition.Arm({old,fresh,0,1}));
    Check(transition.Arm({old,fresh,1,1})); // The replacement frame counter may restart.
    Check(!transition.Allows(old)&&!transition.Allows(fresh)&&!transition.Settled());
    transition.Service(false,[&](const auto&)noexcept{++calls;return OwnerTransitionResult::Failed;});
    Check(calls==0&&transition.Status()==RenderOwnerTransition::State::Pending);
    Check(!transition.AllowsNativeRetirement(true)&&transition.AllowsNativeRetirement(false));
    transition.Service(true,[&](const auto& request)noexcept{
        ++calls;Check(request.oldOwner==old&&request.newOwner==fresh&&request.frame==1);
        Check(!transition.Arm({fresh,later,2,1})); // Never overwrite graphics-owned data.
        Check(!transition.Allows(fresh));return OwnerTransitionResult::Busy;
    });
    Check(!transition.Settled()&&!transition.Allows(fresh));
    transition.Service(true,[&](const auto& request)noexcept{
        ++calls;Check(request.oldOwner==old&&request.newOwner==fresh&&request.frame==1);
        return OwnerTransitionResult::Complete;
    });
    Check(calls==2&&transition.Settled()&&transition.Allows(fresh)&&!transition.Allows(old));
    transition.Service(true,[&](const auto&)noexcept{++calls;return OwnerTransitionResult::Complete;});
    Check(calls==2); // Exactly one terminal application.
    Check(!transition.Arm({old,later,2,1}));Check(!transition.Arm({fresh,later,2,2}));
    Check(transition.Arm({fresh,later,1,1}));
    transition.Service(true,[](const auto&)noexcept{return OwnerTransitionResult::Complete;});
    Check(transition.Allows(later)&&!transition.Allows(fresh));
}
void FailedSourceTransitionCannotReopen(){
    RenderOwnerTransition transition;const RenderOwner old{1,2},fresh{3,4};unsigned calls=0;
    Check(transition.Arm({old,fresh,1,1}));
    transition.Service(true,[&](const auto&)noexcept{++calls;return OwnerTransitionResult::Failed;});
    Check(!transition.Settled()&&!transition.Allows(old)&&!transition.Allows(fresh));
    Check(!transition.Arm({old,fresh,2,1}));
    transition.Service(true,[&](const auto&)noexcept{++calls;return OwnerTransitionResult::Complete;});
    Check(calls==1);transition.Reject();Check(!transition.Allows(fresh));
    Check(!transition.AllowsNativeRetirement(true)&&transition.AllowsNativeRetirement(false));
    RenderOwnerTransition rejected;rejected.Reject();Check(!rejected.Allows(old)&&!rejected.Arm({old,fresh,1,1}));
}
int main(){ReplacementFrameCounter();ChurnAndRestoration();WrapAndRegression();OwnerOutlivesCallbacks();FailedRetirementNeverRetries();CancellationKeepsRequestIdentity();
    CancelledVisibilityWritesNothing();CancellationRemainsVisibleUntilFinished();GraphicsDrainKeepsAdmissionBlocked();FailedSourceTransitionCannotReopen();
    std::cout<<"10 render owner recovery groups passed\n";}
