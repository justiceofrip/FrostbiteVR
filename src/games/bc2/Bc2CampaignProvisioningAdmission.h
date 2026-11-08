#pragma once
#include "Bc2AmmoResourceService.h"
#include "Bc2NativeCycleService.h"
namespace fvr::bc2::campaignProvisioning {
// Same production views used by the ordinary manual ammunition/cycle routes.
// Completed receipt remains evidence; it is never erased/refunded by admission.
inline bool SettledItem(const AmmoResourceView& view,const ReloadStateOwner& owner,std::int64_t now,
                        const std::optional<Bc2NativeCycleView>& cycle,bool cycleRequired)noexcept {
    return view.identity.owner==owner&&AmmoResourceViewFresh(view,now)&&
        view.phase==interaction::AmmunitionLedgerPhase::Ready&&!view.command&&!view.wellEmpty&&
        view.originalState!=interaction::MagazineResourceState::Held&&
        view.requestState!=AmmoResourceRequestState::Queued&&view.requestState!=AmmoResourceRequestState::Dispatched&&
        view.requestState!=AmmoResourceRequestState::Uncertain&&(!cycleRequired||cycle.has_value())&&
        (!cycle||(!cycle->blocksFire&&!cycle->held&&!cycle->ready&&!cycle->heldSuspended));
}
}
