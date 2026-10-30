#pragma once
#include "Bc2MagazineNativeProfile.h"
#include "Bc2ManualPumpConfig.h"
namespace fvr::bc2 {
// Observation-only exact data gate. Does not select a native request family,
// enable a reload capability or authorize any count/state write.
inline bool DiagnosticFireReserveConfig(const ReloadStateMemory& memory,const ReloadObservedConfig& config)noexcept {
 if(ManualPumpConfig(config))return ReadManualPumpReloadTiming(memory,config);
 const auto* row=FindMagazineNativeProfile(config);
 return row&&row->enabled&&row->profile&&row->profile->ReadTiming(memory,config);
}
}
