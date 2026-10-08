#pragma once
#include "Bc2ReloadState.h"
#include "Bc2NativeOperationCapability.h"
#include <span>
#include <string_view>

namespace fvr::bc2 {
// Borrowed executable bytes must outlive this binding and every read. Discovery
// is offline. Runtime reads are callback-only: no native calls, hooks or writes.
struct SelectedMeshesBinding {
    std::span<const std::byte> executable;
    engine::PeImage pe;
    std::uint32_t preferredBase=0;
    std::array<ReloadCodeProof,4> code{}; // context, manager, soldier, ClassInfo
    std::uint32_t context=0,managerTable=0;
    NativeOperationBinding operationBinding{};
};
std::optional<SelectedMeshesBinding> DiscoverSelectedMeshes1p(std::span<const std::byte>,const engine::PeImage&);
enum class SelectedMeshKind:std::uint8_t {Unknown,Spas12,Xm8,Acog4x};
// Shared classification for native reads and offline compiled registry reports.
// Unknown paths remain usable by exact configured descriptors; no new enum case
// or name branch is required for a generated weapon configuration.
SelectedMeshKind ClassifySelectedMeshPath(std::string_view)noexcept;
struct SelectedMeshAsset {
    std::uint32_t address=0,typeInfo=0,namePointer=0;
    std::array<char,512> assetPath{};
    SelectedMeshKind kind=SelectedMeshKind::Unknown;
    bool operator==(const SelectedMeshAsset&)const=default;
};
struct SelectedMeshState {
    std::uint32_t state=0,array=0;
    std::array<std::uint32_t,5> header{};
    std::array<SelectedMeshAsset,8> meshes{};
    std::uint8_t count=0;
    bool operator==(const SelectedMeshState&)const=default;
};
struct SelectedMeshesSnapshot {
    ReloadStateOwner owner{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    std::uint32_t weaponData=0,stateTypeInfo=0,meshTypeInfo=0,inventory=0,selectedSlot=0;
    std::array<char,128> weaponName{};
    std::array<SelectedMeshState,8> states{};
    std::uint8_t stateCount=0;
    // Nonzero only for exactly one authored state. This is the configured
    // Meshes1p array address, NEVER the mesh asset/skin instance/GPU resource.
    std::uint32_t soleConfiguredArray=0;
    NativeOperationBinding operationBinding{};
    // Future exact reflected configuration-path reader must repeat this with
    // the same owner/data/configured array. Existing readers leave it absent;
    // mesh paths and a weapon label cannot manufacture this proof.
    std::array<char,512> configurationPath{};
    bool configurationPathVerified=false;
    std::uint32_t configurationPathPointer=0;
    static constexpr bool atomicNativeSnapshot=false,activeStateVerified=false,
        submittedSkinVerified=false,renderSuppressionAllowed=false;
};
enum class SelectedMeshesStatus:std::uint8_t {
    Observed,Disabled,InvalidArguments,BindingMismatch,ReadFailure,ReadBudget,
    OwnerMismatch,MalformedMetadata,ArrayBounds,ChangedDuringRead
};
struct SelectedMeshesResult {
    SelectedMeshesStatus status=SelectedMeshesStatus::Disabled;
    std::optional<SelectedMeshesSnapshot> snapshot;
    std::uint32_t readCalls=0,readBytes=0;
};
// Explicitly disabled by default. All four owner links, the bounded inventory,
// reflected metadata, authored arrays and each mesh identity are repeated.
// Caller supplies semantic generations and a <=250ms original freshness lease.
// Run as a bounded diagnostic outside the draw hot path, retaining the immutable
// result only to deadlineNs and only for the identical complete owner token.
SelectedMeshesResult ReadSelectedMeshes1p(const ReloadStateMemory&,const SelectedMeshesBinding&,
    std::uint32_t imageBase,const ReloadStateOwner&,std::uint64_t sequence,
    std::int64_t observedNs,std::int64_t deadlineNs,bool enabled=false) noexcept;
// Same reflected config reader for a genuinely carried, UNSELECTED slot.
// selected.owner remains the actual selected native owner; it is never rewritten
// to pretend the carried weapon is selected. These values grant no hide/shot proof.
// Deliberately not SelectedMeshesSnapshot: a nonselected mesh observation must
// not be passable to existing selected-only hide/reload/shot consumers.
struct CarriedMeshConfiguration {
    ReloadStateOwner owner{}; // actual current selected owner, not this item
    std::uint64_t sequence=0;std::int64_t observedNs=0,deadlineNs=0;
    std::uint32_t weaponData=0,stateTypeInfo=0,meshTypeInfo=0,inventory=0,selectedSlot=0;
    std::array<char,128> weaponName{};std::array<SelectedMeshState,8> states{};
    std::uint8_t stateCount=0;std::uint32_t soleConfiguredArray=0;
    static constexpr bool activeStateVerified=false,renderSuppressionAllowed=false;
};
struct CarriedMeshesSnapshot {
    CarriedMeshConfiguration configured{};
    std::uint32_t weapon=0,nativeSlot=0,persistence=0;
};
struct CarriedMeshesResult {SelectedMeshesStatus status=SelectedMeshesStatus::Disabled;std::optional<CarriedMeshesSnapshot> snapshot;};
CarriedMeshesResult ReadCarriedMeshes1p(const ReloadStateMemory&,const SelectedMeshesBinding&,
    std::uint32_t imageBase,const ReloadStateOwner&,std::uint32_t inventory,std::uint32_t nativeSlot,
    std::uint32_t weapon,std::uint32_t data,std::uint32_t persistence,std::uint64_t sequence,
    std::int64_t observedNs,std::int64_t deadlineNs,bool enabled=false)noexcept;
// Unique exact asset in the sole configured authored state. Duplicate names,
// multiple states and expired/different owners never become a guessed binding.
const SelectedMeshAsset* FindSelectedMesh(const SelectedMeshesSnapshot&,const ReloadStateOwner&,
    SelectedMeshKind,std::int64_t nowNs) noexcept;
}
