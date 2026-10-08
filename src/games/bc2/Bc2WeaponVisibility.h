#pragma once
#include "Bc2Rig.h"
#include "Bc2SelectedMeshes1p.h"
#include "fvr/interaction/HandInteraction.h"
#include <memory>

namespace fvr::bc2 {
// Pure descriptor/legacy coverage resolution. Returned names grant no native
// write or admission; diagnostic use still requires independent bounded intent.
std::span<const std::string_view> ResolveWeaponVisibilityNames(const SelectedMeshesSnapshot&,
    const ReloadStateOwner&,std::int64_t nowNs,bool diagnostic=false)noexcept;
// Render intent only; inventory policy owns permission and request lifecycle.
// This helper cannot grant holster/draw acknowledgement or mutate native state.
struct WeaponVisibilityRequest {
    bool enabled=false,hide=false;
    std::uint64_t request=0;
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
    std::int64_t authorizationDeadlineNs=0; // Optional independent diagnostic limit; input lease remains original.
};
enum class WeaponVisibilityReason:unsigned {
    None,Disabled,StaleInput,OwnerMismatch,UnsupportedMeshes,UnverifiedRig,
    InvalidPalette,WeightedHandOverlap
};
struct WeaponVisibilityPlan {
    WeaponVisibilityReason reason=WeaponVisibilityReason::Disabled;
    RigIdentity rig{};ReloadStateOwner nativeOwner{};
    std::uint64_t request=0,inputSequence=0,physicalEquipGeneration=0,meshSequence=0;
    std::int64_t observedNs=0,deadlineNs=0,inputDeadlineNs=0;
    bool hidden=false;
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
    std::vector<std::uint32_t> weightedBones;
    std::vector<std::array<std::byte,64>> originalNative,ordinary,privatePalette;
    // The renderer must retain the original request/owner/source/expiry, choose
    // ordinary on release/expiry, and prove both actual Pack callbacks. Even a
    // successful Pack is not an all-sections visibility acknowledgement.
    static constexpr bool nativeAnimationWritten=false,submittedVisibilityVerified=false;
};
// Explicitly off by default. Exact known assets+attachments, full rig and fresh
// complete owner required. Uses final evaluated SKIN matrices, not hierarchy
// writes: all weighted weapon vertices become a point while all other palette
// entries and opaque SIMD padding are byte-identical to the ordinary palette.
WeaponVisibilityPlan BuildWeaponVisibilityPalette(const RigSnapshot&,
    std::span<const std::array<std::byte,64>> ordinary,const WeaponVisibilityRequest&,std::int64_t nowNs);
bool WeaponVisibilityCurrent(const WeaponVisibilityPlan&,const WeaponVisibilityRequest&,std::int64_t nowNs)noexcept;
// Observation of the existing native packer's two callbacks. This is not a
// promise that every material/particle was submitted or visible on the GPU.
struct WeaponVisibilityReceipt {
    ReloadStateOwner nativeOwner{};RigIdentity rig{};
    std::uint64_t request=0,inputSequence=0,physicalEquipGeneration=0,drawSerial=0;
    std::int64_t observedNs=0,deadlineNs=0;
    unsigned verifiedCopyMask=0;
    bool hidden=false;
    std::shared_ptr<const WeaponVisibilityPlan> evidence;
    static constexpr bool submittedVisibilityVerified=false;
};

namespace weapon_visibility_detail {
// Pure bounded mechanics for synthetic tests. This is not a native binding.
std::optional<std::vector<std::uint32_t>> ResolveWeightedBones(std::span<const std::string> rigNames,
    std::span<const std::string_view> exactWeightedNames);
std::optional<std::vector<std::array<std::byte,64>>> CollapseWeightedPalette(
    std::span<const std::array<std::byte,64>>,std::span<const std::uint32_t> indices);
}
}
