#pragma once
#include "Bc2NativeCycleService.h"

namespace fvr::bc2 {
// The runtime's optional view may be unavailable while its nonblocking gate is
// busy. Selection identity persists independently of that observation. This
// tracker holds no native lease, counts, readiness or permission to fire.
class Bc2NativeCycleSelectionTracker {
public:
    bool Observe(Bc2NativeCycleMode requested,const ReloadStateOwner& current,
                 const std::optional<Bc2NativeCycleView>& view)noexcept {
        if(view){mode_=view->selectedMode;owner_=view->selectedOwner;}
        return mode_&&*mode_==requested&&owner_==current;
    }
    // Call only after the native selector has accepted this exact identity.
    void Selected(Bc2NativeCycleMode mode,const ReloadStateOwner& owner)noexcept {
        mode_=mode;owner_=owner;
    }
    // Route new-space input to the existing adapter without accepting a new
    // selection. Its native rebase still requires drained callbacks, exact
    // actor/item/configuration and preserved debt. This grants no fire authority.
    bool RoutesTrackingRecovery(Bc2NativeCycleMode requested,const ReloadStateOwner& current)const noexcept {
        auto previous=current;previous.space=owner_.space;
        return requested==Bc2NativeCycleMode::Spas&&mode_&&*mode_==requested&&
            owner_.space&&current.space>owner_.space&&previous==owner_;
    }
private:
    std::optional<Bc2NativeCycleMode> mode_;
    ReloadStateOwner owner_{};
};
}
