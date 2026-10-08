#pragma once
#include "Bc2ReloadPresentation.h"
namespace fvr::bc2 {
enum class Bc2ReloadPaletteReason:std::uint8_t {
    None,Disabled,PresentationRejected,InvalidBasePose,ArmRejected,WristUnreachable,InvalidPalette
};
struct Bc2ReloadPaletteSample {
    bool enabled=false;
    // Existing accepted torso/weapon/right-hand writes. This function adds a
    // reload overlay in private memory; the native source is never modified.
    std::span<const interaction::BoneWrite> baseWrites;
    // Same snapshot/anatomical solve basis used by RigPublication for this pose.
    std::span<const math::Matrix4> armNativeWorld;
    math::Vec3 leftPoleDirection{};
    std::optional<math::Vec3> leftShoulder;
    Bc2ReloadPresentationObservation presentation{};
};
struct Bc2ReloadPalettePlan {
    Bc2ReloadPaletteReason reason=Bc2ReloadPaletteReason::Disabled;
    Bc2ReloadPresentationReason presentationReason=Bc2ReloadPresentationReason::None;
    float leftWristError=0;
    std::vector<interaction::BoneWrite> writes;
    std::optional<RigPosePlan> palette;
    std::optional<std::uint32_t> ownedShellVisibility;
};
// No GPU draw/per-eye gate is needed to compose a candidate private palette.
// Existing presentation section/native-cycle/claim guards remain mandatory.
// Only its explicitly authorized bound shell can leave the private hidden-leaf
// exclusion list; configured Meshes1p alone never grants that exception.
// Production obtains binding via BindBc2ReloadPresentation. Publication still
// requires the existing owned GetA/GetB source/count/exact-byte checks.
Bc2ReloadPalettePlan BuildBc2ReloadPalette(const RigSnapshot&,
    const Bc2ReloadPresentationBinding&,const Bc2ReloadPaletteSample&,
    const Bc2ReloadTargets&);
}
