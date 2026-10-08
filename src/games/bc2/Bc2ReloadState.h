#pragma once
#include "fvr/engine/PeImage.h"
#include <array>
#include <cstdint>
#include <optional>

namespace fvr::bc2 {
struct ReloadStateMemory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
    bool (*type)(void*,std::uint32_t,const char*)=nullptr;
};
struct ReloadCodeProof {std::uint32_t rva=0,size=0;std::uint64_t fingerprint=0;};
struct ReloadStateBinding {
    std::uint32_t preferredBase=0,imageSize=0,firingVtableRva=0;
    std::array<std::uint32_t,3> firingVtableWords{};
    std::array<ReloadCodeProof,6> code{};
};
// This first observer recognizes the specifically inspected native layout.
// Unique code signatures plus complete function fingerprints establish the
// offsets; unsupported/relocated builds fail rather than borrowing a gun name.
std::optional<ReloadStateBinding> DiscoverReloadState(std::span<const std::byte>,const engine::PeImage&);
bool ValidateReloadStateLive(const ReloadStateMemory&,const ReloadStateBinding&,std::uint32_t imageBase) noexcept;
struct ReloadStateOwner {
    std::uint32_t player=0,soldier=0,weak=0,weapon=0;
    std::uint64_t actorGeneration=0,equipGeneration=0,space=0;
    bool operator==(const ReloadStateOwner&)const=default;
};
enum class ReloadObservedPhase:std::uint8_t {
    Unknown,ReturnWait,InputProcessing,ShotStep,BoltHold,BoltCycle,ReloadBegin,ReloadWait,ReloadTransfer
};
enum class ReloadAmmoEligibility:std::uint8_t {
    Unknown,NativePhaseBusy,NoRoom,NoReserve,ObservedCandidate
};
struct ReloadObservedConfig {
    std::uint32_t weaponData=0,firingData=0,primaryFire=0,ammoAddress=0;
    std::array<char,64> assetName{};
    std::array<char,192> assetPath{};
    std::int32_t fireLogicType=0,reloadType=0,fireInputAction=0,reloadInputAction=0;
    std::int32_t baseCapacity=0,numberOfMagazines=0;
    float reloadDelay=0,reloadTime=0,reloadThreshold=0,postReloadTime=0,boltDelay=0,boltTime=0;
    bool holdBoltUntilFireRelease=false,holdBoltUntilZoomRelease=false;
    // Authored states are not current firing-state objects. No active authored
    // state index was proven; mixed handling remains visible, not guessed.
    std::array<bool,8> authoredPumpHandling{};
    std::uint8_t authoredStateCount=0;
    bool operator==(const ReloadObservedConfig&)const=default;
};
struct ReloadFiringObservation {
    std::uint32_t wrapperOffset=0,address=0;
    std::uint32_t currentState=0,previousState=0,nextState=0;
    float phaseTimer=0,capacityMultiplier=0,reserveMultiplier=0;
    std::int32_t loaded=0,reserve=0,capacityOverride=0;
    std::uint8_t flagsA8=0;
    std::optional<std::int32_t> effectiveCapacity;
    ReloadObservedPhase phase=ReloadObservedPhase::Unknown;
    ReloadAmmoEligibility reloadAmmo=ReloadAmmoEligibility::Unknown;
    // Configured native bolt cycle is not a verified physical pump/bolt gate.
    bool nativeBoltCycleConfigured=false;
};
struct ReloadStateSnapshot {
    ReloadStateOwner owner{};std::uint64_t sequence=0;std::int64_t observedNs=0;
    std::uint32_t inventory=0,selectedSlot=0;std::uint8_t soldierFlags=0,eligibilityBranch=0;
    ReloadObservedConfig config{};
    std::array<ReloadFiringObservation,2> branches{}; // wrapper +3c, then +40
    bool branchesAgreeOnAmmo=false,branchesAgreeOnPhase=false;
    // Stable repeated reads establish coherence, never simulation atomicity or
    // authoritative branch selection. These capabilities deliberately stay off.
    static constexpr bool atomicNativeSnapshot=false,chamberKnown=false,
        authoritativeBranchKnown=false,manualCycleGate=false,manualReloadDispatch=false;
};
enum class ReloadStateStatus:std::uint8_t {
    Observed,InvalidArguments,ReadFailure,OwnerMismatch,MalformedConfiguration,MalformedState,ChangedDuringRead
};
struct ReloadStateResult {ReloadStateStatus status=ReloadStateStatus::InvalidArguments;std::optional<ReloadStateSnapshot> snapshot;};
// Call only after DiscoverReloadState and ValidateReloadStateLive succeed for
// this process/module. Never caches actor/item pointers. All native reads are
// callback-driven; there are no native calls, hooks, writes or acknowledgement.
// The caller owns semantic generation advancement and freshness deadlines.
ReloadStateResult ReadReloadState(const ReloadStateMemory&,const ReloadStateBinding&,std::uint32_t imageBase,
    const ReloadStateOwner&,std::uint64_t sequence,std::int64_t observedNs) noexcept;
}
