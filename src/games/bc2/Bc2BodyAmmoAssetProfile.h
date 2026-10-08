#pragma once
#include "Bc2BeltPropProfiles.h"
namespace fvr::bc2 {
// Derived, reviewed metadata; no vertex or index bytes.
struct BodyAmmoAssetProfile {
    const char* asset;const char* mesh;const char* part;
    std::uint64_t rig;
    math::Matrix4 inverseBind;
    std::span<const BeltPropSectionProfile> sections;
};
}
