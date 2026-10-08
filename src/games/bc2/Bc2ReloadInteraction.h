#pragma once
#include "fvr/interaction/ReloadInsertion.h"
#include <string_view>
namespace fvr::bc2 {
inline constexpr std::string_view SpasReloadAsset="SPAS12_sp";
inline constexpr std::string_view SpasReloadMesh="Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh";
inline constexpr std::uint64_t SpasReloadRig=0xa7f219a1426216abull;
// Measured grasp/terminal transforms, with explicitly authored draft tolerances.
// No runtime configuration or capability is enabled by constructing this profile.
interaction::ReloadInsertionProfile SpasReloadInsertionProfile()noexcept;
struct Bc2ReloadNativeLease {
    interaction::HandInteractionOwner owner{};
    interaction::HandInteractionKey weapon{};
    std::uint64_t cycle=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool allFiringCopiesHeld=false; // Adapter proof, never a gesture acknowledgement.
};
struct Bc2ReloadInteractionSample {
    interaction::ReloadInsertionSample insertion{};
    Bc2ReloadNativeLease native{};
    std::string_view assetName,meshPath;
    std::uint64_t rigFingerprint=0;
    bool selectedMeshIdentityVerified=false;
    // Coherent RAW pre-IK wrist and placed weapon worlds, canonical metres.
    math::Matrix4 rawLeftWristWorldMeters{},weaponWorldMeters{};
};
struct Bc2ReloadTargets {
    interaction::ReloadInsertionIdentity identity{};
    interaction::HandClaimToken shellClaim{},weaponClaim{};
    std::uint64_t inputSequence=0,nativeCycle=0;
    std::int64_t observedNs=0,deadlineNs=0;
    math::Matrix4 weaponFromShellCenterMeters{},weaponFromLeftWristMeters{};
};
enum class Bc2ReloadInteractionReason : std::uint8_t {
    None,Disabled,UnsupportedAsset,MeshUnverified,NativeGateUnavailable,NativeCycleChanged,
    WrongHands,InvalidWorldPose,InsertionRejected
};
struct Bc2ReloadInteractionResult {
    Bc2ReloadInteractionReason reason=Bc2ReloadInteractionReason::None;
    interaction::ReloadInsertionResult insertion{};
    std::optional<Bc2ReloadTargets> targets{};
};
// Consumes existing HandInteraction AmmoObject/GunHold leases. It never acquires
// a competing hand, spawns ammunition, writes ammo or dispatches native reload.
class Bc2ReloadInteraction {
public:
    explicit Bc2ReloadInteraction(bool enabled=false)noexcept:enabled_(enabled),insertion_(SpasReloadInsertionProfile()){}
    Bc2ReloadInteractionResult Update(const Bc2ReloadInteractionSample&)noexcept;
    void Reset()noexcept {insertion_.Reset();cycle_=0;}
private:
    Bc2ReloadInteractionResult Reject(Bc2ReloadInteractionReason)noexcept;
    bool enabled_=false;
    std::uint64_t cycle_=0;
    interaction::ReloadInsertion insertion_;
};
}
