#pragma once
#include "Bc2FeedEquipmentProfile.h"
#include "Bc2FixedContainerNative.h"
#include "Bc2MagazineNativeProfile.h"
#include "Bc2ManualPumpConfig.h"
#include "Bc2UnderbarrelAmmoConfig.h"
#include <optional>

namespace fvr::bc2 {
enum class WeaponFamilyAmmoSource : unsigned {Magazine,Tube,Feed,Fixed,Underbarrel};

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
    auto* underbarrel=FindUnderbarrelAmmoConfig(config);
    // A measured underbarrel reuses this exact descriptor in the fixed family.
    // Count that one registration once; unrelated overlapping families still
    // fail closed below. Unmeasured selectors retain their read-only family.
    if(fixed&&fixed==underbarrel)underbarrel=nullptr;
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
