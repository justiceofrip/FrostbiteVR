#pragma once
#include "fvr/interaction/WeaponProfile.h"
#include <span>
namespace fvr::bc2 {
struct Bc2WeaponProfile {
    std::string_view assetName{};
    // Preserve the existing native shot/support telemetry IDs.
    std::uint32_t telemetryKind=0;
    interaction::WeaponProfile core{};
};
// Exact, case-sensitive asset identity; no prefix/class-name inheritance.
// Unknown assets return nullptr and every shared feature gate remains disabled.
const Bc2WeaponProfile* FindWeaponProfile(std::string_view assetName) noexcept;
const Bc2WeaponProfile* FindWeaponProfileByTelemetryKind(std::uint32_t kind) noexcept;
std::span<const Bc2WeaponProfile> WeaponProfiles() noexcept;
}
