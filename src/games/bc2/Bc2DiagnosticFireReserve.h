#pragma once
#include "Bc2MagazineNativeProfile.h"
namespace fvr::bc2 {
// Observation-only exact data gate. Does not select a native request family,
// enable a reload capability or authorize any count/state write.
inline bool DiagnosticFireReserveConfig(const ReloadStateMemory& memory,const ReloadObservedConfig& config)noexcept {
 if(MatchesReloadDescriptor(config,SpasReloadDescriptor))
  return ReadReloadDescriptorTiming(memory,config,SpasReloadDescriptor);
 const auto* row=FindMagazineNativeProfile(config);
 return row&&row->enabled&&row->profile&&row->profile->ReadTiming(memory,config);
}
}
