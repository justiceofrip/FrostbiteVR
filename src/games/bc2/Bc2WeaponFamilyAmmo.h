#pragma once
#include "Bc2FeedEquipmentProfile.h"
#include "Bc2FixedContainerNative.h"
#include "Bc2MagazineNativeProfile.h"
#include "Bc2ManualPumpConfig.h"
#include <optional>

namespace fvr::bc2 {
// These descriptors authorize typed reads only. They are deliberately absent
// from the manual magazine, pump, feed and fixed-container registrations.
#include "../../../profiles/weapons54-underbarrel/AmmoConfigs.generated.h"
enum class WeaponFamilyAmmoSource : unsigned {Magazine,Tube,Feed,Fixed,Underbarrel};

inline const ReloadConfigDescriptor* FindUnderbarrelAmmoConfig(const ReloadObservedConfig& config)noexcept {
    const ReloadConfigDescriptor* found=nullptr;
    for(const auto& row:UnderbarrelAmmoConfigs)if(MatchesReloadDescriptor(config,row)){
        if(found)return nullptr;found=&row;
    }
    return found;
}

// Read-only admission for the selected native configuration, independent of a
// retained reload operation's family/profile. Each family keeps its reviewed
// exact descriptor and doubled native timing reads. This supplies no request,
// transfer, mechanism or hand ownership authority.
inline std::optional<WeaponFamilyAmmoSource> ReadWeaponFamilyAmmoConfig(
    const ReloadStateMemory& memory,const ReloadObservedConfig& config)noexcept {
    const auto* magazine=FindMagazineNativeProfile(config);
    const bool tube=ManualPumpConfig(config);
    const auto* feed=FindBc2FeedRegistration(config);
    const auto* fixed=ResolveFixedContainerNative(config);
    const auto* underbarrel=FindUnderbarrelAmmoConfig(config);
    // Conflicting registrations must not silently choose an operation family.
    if(unsigned(magazine!=nullptr)+unsigned(tube)+unsigned(feed!=nullptr)+unsigned(fixed!=nullptr)+unsigned(underbarrel!=nullptr)!=1)return {};
    if(magazine&&magazine->enabled&&magazine->profile&&magazine->profile->ReadTiming(memory,config))
        return WeaponFamilyAmmoSource::Magazine;
    if(tube&&ReadManualPumpReloadTiming(memory,config))return WeaponFamilyAmmoSource::Tube;
    if(feed&&ReadReloadDescriptorTiming(memory,config,feed->config))return WeaponFamilyAmmoSource::Feed;
    if(fixed&&ReadReloadDescriptorTiming(memory,config,*fixed))return WeaponFamilyAmmoSource::Fixed;
    if(underbarrel&&ReadReloadDescriptorTiming(memory,config,*underbarrel))return WeaponFamilyAmmoSource::Underbarrel;
    return {};
}
}
