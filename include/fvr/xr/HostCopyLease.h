#pragma once
#include "fvr/runtime/FrameProvider.h"

namespace fvr::xr {
// Source ownership ends at GPU completion. Local images remain owned by the
// containing object until presentation or drop; neither phase admits reuse.
class HostCopyLease {
public:
    enum class Phase {Copying,ReadyForPresentation};
    runtime::IFrameProvider& provider;
    const graphics::PairTicket ticket;
    explicit HostCopyLease(runtime::IFrameProvider& p,const graphics::PairTicket& t)noexcept:provider(p),ticket(t){}
    HostCopyLease(const HostCopyLease&)=delete;
    HostCopyLease& operator=(const HostCopyLease&)=delete;
    ~HostCopyLease(){if(Copying())provider.PairConsumed(ticket,false);}
    bool Copying()const noexcept{return phase_==Phase::Copying;}
    bool Ready()const noexcept{return phase_==Phase::ReadyForPresentation;}
    bool Cancelled()const noexcept{return cancelled_;}
    // Command submission is independent of source-read completion. The caller
    // marks this only after both XR images have been released. It is neither
    // an acknowledgement nor evidence that EndFrame/GPU target writes finished.
    bool Composed()const noexcept{return composed_;}
    bool MarkComposed()noexcept {
        if(cancelled_||composed_)return false;
        composed_=true;return true;
    }
    bool Complete()noexcept {
        if(!Copying())return false;
        phase_=Phase::ReadyForPresentation;
        provider.PairConsumed(ticket,true);return true;
    }
    void CancelPresentation()noexcept {
        cancelled_=true;
        // Repeatable: a contended IPC gate must retain cancellation ahead of
        // feedback. Completed pixels need no second or contradictory receipt.
        provider.Suspend();
    }
private:
    Phase phase_=Phase::Copying;
    bool cancelled_=false,composed_=false;
};
}
