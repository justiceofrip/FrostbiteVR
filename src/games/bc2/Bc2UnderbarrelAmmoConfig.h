#pragma once
#include "Bc2ReloadConfigDescriptor.h"

namespace fvr::bc2 {
// Typed native reads and inactive-stock-bolt proof only. Manual authority is
// granted separately by the exact measured fixed-container registration.
#include "../../../profiles/weapons54-underbarrel/AmmoConfigs.generated.h"
inline const ReloadConfigDescriptor* FindUnderbarrelAmmoConfig(const ReloadObservedConfig& config)noexcept {
    const ReloadConfigDescriptor* found=nullptr;
    for(const auto& row:UnderbarrelAmmoConfigs)if(MatchesReloadDescriptor(config,row)){
        if(found)return nullptr;found=&row;
    }
    return found;
}
}
