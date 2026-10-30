#pragma once
#include "Bc2BoltActionNativeProfile.h"
namespace fvr::bc2 {
inline bool ManualPumpConfig(const ReloadObservedConfig& config)noexcept {
    const auto* row=ResolveNativeCycleProfile(config);
    return row&&row->family==Bc2NativeCycleFamily::Pump;
}
inline bool ReadManualBoltConfig(const ReloadStateMemory& memory,const ReloadObservedConfig& config)noexcept {
    return ResolveBoltActionNativeProfile(config)&&ReadNativeCycleProfile(memory,config);
}
inline bool ReadManualPumpConfig(const ReloadStateMemory& memory,const ReloadObservedConfig& config)noexcept {
    return ManualPumpConfig(config)&&ReadNativeCycleProfile(memory,config);
}
inline bool ReadManualPumpReloadTiming(const ReloadStateMemory& memory,const ReloadObservedConfig& config)noexcept {
    const auto* row=ResolveNativeCycleProfile(config);
    if(!row||row->family!=Bc2NativeCycleFamily::Pump)return false;
    const auto& v=row->config->values;
    const std::array<ReloadTimingWord,2> timing{{
        {0x10,std::bit_cast<std::uint32_t>(v.reloadThreshold),ReloadTimingComparison::Float},
        {0x18,std::bit_cast<std::uint32_t>(v.reloadTime),ReloadTimingComparison::Float}}};
    auto descriptor=*row->config;descriptor.timing=timing;
    return ReadReloadDescriptorTiming(memory,config,descriptor);
}
}
