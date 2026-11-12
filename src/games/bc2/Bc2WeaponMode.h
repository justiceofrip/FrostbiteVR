#pragma once
#include "fvr/engine/PeImage.h"
#include "Bc2WeaponModeFamilies.h"
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
namespace fvr::bc2 {
struct WeaponModeMemory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
    bool (*type)(void*,std::uint32_t,const char*)=nullptr;
};
struct WeaponModeCommand {
    std::uint32_t action=0,targetWeapon=0,targetSlot=0,inventory=0,persistent=0;
    const WeaponModeFamilyProfile* family=nullptr;bool selectedSecondary=false;
    // Reciprocal action observed in this inventory's map, not family metadata.
    // Body selection commands without a linked mode family leave this zero.
    std::uint32_t primaryAction=0;
};
inline constexpr std::int64_t WeaponModeCacheMaxAgeNs=500000000;
// The current raw-input lease must fit inside the original native observation.
// This only schedules a real source reread; it never renews the cached proof.
inline constexpr bool WeaponModeCacheLeaseCovers(std::int64_t observedNs,std::int64_t nowNs,
    std::int64_t inputDeadlineNs)noexcept {
    return observedNs>0&&observedNs<=nowNs&&inputDeadlineNs>nowNs&&
        inputDeadlineNs-observedNs<=WeaponModeCacheMaxAgeNs;
}
// Optional, bounded observation of the resolver's original reads. Unreached
// fields retain defaults; none of these fields grant command admission.
struct WeaponModeItemObservation {
    std::uint32_t weapon=0,data=0,slot=0,persistent=0;
    std::string asset,configuration;
};
struct WeaponModeRouteObservation {
    const char* status="not_reached";
    std::uint32_t from=0,action=0,wanted=0,row=0,state=0,input=0,first=0,last=0;
    std::uint32_t matching=0,targetsRead=0,firstOccupiedSlot=UINT32_MAX,firstOccupiedWeapon=0;
    std::array<std::uint32_t,11> targets{};
};
struct WeaponModeCandidateObservation {
    const WeaponModeFamilyProfile* family=nullptr;
    const char* status="not_reached";
    std::uint32_t inventory=0,switching=0,begin=0,end=0,selected=0,flags=0,itemCount=0;
    std::array<std::uint32_t,64> items{};
    std::uint64_t typedItems=0,namedItems=0;
    WeaponModeItemObservation lastItem,primary,secondary;
    std::string persistentId;
    std::uint32_t mapBegin=0,mapEnd=0;
    std::array<WeaponModeRouteObservation,2> routes{};
    std::array<WeaponModeRouteObservation,2> primaryRoutes{};
    std::uint64_t bookendAddress=0;std::uint32_t bookendExpected=0,bookendObserved=0;bool bookendRead=false;
};
struct WeaponModeResolution {
    const char* status="not_observed";
    std::uint32_t soldier=0,weapon=0,data=0,candidateCount=0,candidatesDropped=0;
    std::string asset,configuration;
    // The enrolled roster currently has at most six matches for a selected
    // configuration. Extra attempts remain authoritative but are counted only.
    std::array<WeaponModeCandidateObservation,8> candidates{};
};
void WriteWeaponModeResolution(std::ostream&,const WeaponModeResolution&);
// Read-only candidate for an exact enrolled linked pair. Native
// eligibility, ammo, equipment animation and selection remain authoritative.
// Invoke no native function and never write the equipped pointer.
std::optional<WeaponModeCommand> ResolveWeaponMode(const WeaponModeMemory&,
    std::uint32_t soldier,std::uint32_t currentWeapon,WeaponModeResolution* observation=nullptr);
// Read-only evidence seam. A returned command is not native dispatch admission.
std::optional<WeaponModeCommand> ObserveWeaponModeFamily(const WeaponModeMemory&,
    std::uint32_t soldier,std::uint32_t currentWeapon,const WeaponModeFamilyProfile&,
    WeaponModeCandidateObservation* observation=nullptr);
struct WeaponModeCandidates {
    std::uint32_t selector=0,update=0,edgeReader=0,booleanReader=0,scalarReader=0;
};
// Validate static action IDs, cache readers and the original selector/update
// call graph before enabling the separate opt-in mode command.
std::optional<WeaponModeCandidates> DiscoverWeaponMode(std::span<const std::byte>,
    const engine::PeImage&);
}
