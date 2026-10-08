#include "fvr/runtime/RenderOwnerRecovery.h"
#include <cstdlib>
#include <iostream>
#include <vector>
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
int main(){ReplacementFrameCounter();ChurnAndRestoration();WrapAndRegression();OwnerOutlivesCallbacks();FailedRetirementNeverRetries();CancellationKeepsRequestIdentity();
    std::cout<<"6 render owner recovery groups passed\n";}
