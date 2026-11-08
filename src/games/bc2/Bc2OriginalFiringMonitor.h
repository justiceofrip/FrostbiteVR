#pragma once
#include "Bc2OriginalFiringObserver.h"
namespace fvr::bc2 {
// A side observer never owns a copied native input or the thread-local primary
// scope. The dispatcher still invokes native Update exactly once.
inline bool ValidOriginalFiringMonitor(const OriginalFiringObserver& observer)noexcept {
    return observer.context&&observer.enter&&observer.leave&&observer.abort&&!observer.copyInput;
}
class OriginalFiringMonitorScope {
public:
    OriginalFiringMonitorScope(const OriginalFiringObserver& observer,OriginalFiringUpdate call)noexcept
        :observer_(ValidOriginalFiringMonitor(observer)?observer:OriginalFiringObserver{}),call_(call){
        if(observer_.enter)token_=observer_.enter(observer_.context,call_);
    }
    ~OriginalFiringMonitorScope(){
        if(!closed_&&observer_.leave)observer_.leave(observer_.context,call_,token_);
    }
    void Abort()noexcept {
        if(closed_)return;
        closed_=true;
        if(observer_.abort)observer_.abort(observer_.context,call_,token_);
    }
    OriginalFiringMonitorScope(const OriginalFiringMonitorScope&)=delete;
    OriginalFiringMonitorScope& operator=(const OriginalFiringMonitorScope&)=delete;
private:
    OriginalFiringObserver observer_{};OriginalFiringUpdate call_{};OriginalFiringToken token_{};bool closed_=false;
};
#ifdef FVR_BC2_CAMPAIGN_PROVISIONING
namespace reloadFlowRuntime {
// One passive private observer, installed before Start. Never replaces the
// existing input-copy owner; callbacks remain process-retained after Stop.
bool RegisterOriginalFiringMonitor(const OriginalFiringObserver&)noexcept;
}
#endif
}
