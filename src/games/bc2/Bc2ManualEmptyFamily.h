#pragma once
#include "Bc2MagazineNativeProfile.h"
#include "Bc2ReloadFamily.h"
#include "Bc2ReloadRoundGate.h"
namespace fvr::bc2 {
// Typed selection of already reviewed manual native reload families. This is
// configuration/timing evidence only: owner, invocation, phase, context and
// current branch receipts remain mandatory at the scoped native Step boundary.
// Unknown guns and underbarrel attachments retain ordinary native reloads.
inline bool ManualEmptyFamilyConfig(ReloadNativeFamily family,NativeMagazineProfileId id,
    const ReloadObservedConfig& config)noexcept {
    if(family==ReloadNativeFamily::SpasTube)return MatchesReloadDescriptor(config,SpasReloadDescriptor);
    if(family!=ReloadNativeFamily::Xm8Magazine)return false;
    const auto* profile=ResolveMagazineNativeProfile(id);
    return profile&&profile->Matches(config)&&config.fireLogicType==2&&config.reloadType==1;
}
inline bool ReadManualEmptyFamilyTiming(const ReloadStateMemory& memory,ReloadNativeFamily family,
    NativeMagazineProfileId id,const ReloadObservedConfig& config)noexcept {
    if(!ManualEmptyFamilyConfig(family,id,config))return false;
    if(family==ReloadNativeFamily::SpasTube)return ReadReloadRoundTiming(memory,config);
    const auto* profile=ResolveMagazineNativeProfile(id);
    return profile&&profile->ReadTiming(memory,config);
}
}
