#pragma once
#include "Bc2NativeCycleService.h"
namespace fvr::bc2 {
// Separate, exact native convergence channel. This cannot renew a held timer
// lease or make a pending normal/native-held release disappear.
struct Bc2NativeCycleRecoveryView {
    ReloadHoldIdentity native{};
    std::optional<interaction::WeaponCycleIdleDebtLease> authority;
    std::optional<interaction::WeaponCycleIdleDebtReady> ready;
    bool blocksFire=true;
};
struct Bc2PhysicalCycleRecoveryApi {
    std::optional<Bc2NativeCycleRecoveryView> (*view)(void*,std::int64_t)noexcept=nullptr;
    bool (*submit)(void*,const interaction::WeaponCycleIdleDebtRelease&,
        const interaction::HandInteractionSample&)noexcept=nullptr;
    bool (*ack)(void*,const interaction::WeaponCycleIdleDebtReady&)noexcept=nullptr;
    bool Complete()const noexcept{return view&&submit&&ack;}
};
namespace reloadFlowRuntime {
std::optional<Bc2NativeCycleRecoveryView> ReadNativeCycleRecoveryView(std::int64_t now)noexcept;
}
}
