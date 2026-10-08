#include "Bc2WeaponProfiles.h"
#include <array>

namespace fvr::bc2 {
namespace {
using interaction::WeaponFeatureEvidence;
using interaction::WeaponVerification;
constexpr std::string_view alignment="docs/WEAPON-ALIGNMENT-20261001.md";
constexpr std::string_view support="docs/TWO-HAND-SUPPORT-20261001.md";
constexpr std::string_view muzzle="docs/FIRING-ORIGIN-20260930.md";
constexpr std::string_view headset="reports/headset-grips-success-20261001-072621/acceptance.json";
constexpr std::array<WeaponFeatureEvidence,3> verifiedFeatures{{
    {WeaponVerification::NativeVerified,alignment,{}},
    {WeaponVerification::HeadsetAccepted,support,headset},
    {WeaponVerification::NativeVerified,muzzle,{}}
}};
// Authored wrist/weapon relations are sampled per equipment instance after
// animation settles. These entries do not bake controller offsets or bones.
// Visible -Z barrel/+Y up is native-verified for these exact two assets.
// Headset acceptance covers grip/handling only, not projectile-impact accuracy.
constexpr std::string_view launcherEvidence="docs/LAUNCHER-PILOT-20261001.md";
constexpr std::array<WeaponFeatureEvidence,3> launcherFeatures{{
    {WeaponVerification::NativeVerified,launcherEvidence,{}},
    {WeaponVerification::NativeVerified,launcherEvidence,{}},
    {} // jntWpn_Flash still resolves to the rifle barrel; grenade origin is unverified.
}};
constexpr std::array<Bc2WeaponProfile,3> profiles{{
    {"SPAS12_sp",1,{"bc2:SPAS12_sp",1,{0,0,-1},{0,1,0},verifiedFeatures}},
    {"XM8_sp_s",2,{"bc2:XM8_sp_s",1,{0,0,-1},{0,1,0},verifiedFeatures}},
    {"40mmgl",3,{"bc2:40mmgl:XM8_scoped",1,{0,0,-1},{0,1,0},launcherFeatures}}
}};
}
const Bc2WeaponProfile* FindWeaponProfile(std::string_view assetName) noexcept {
    for(const auto& profile:profiles)if(profile.assetName==assetName)return &profile;
    return nullptr;
}
const Bc2WeaponProfile* FindWeaponProfileByTelemetryKind(std::uint32_t kind) noexcept {
    if(!kind)return nullptr;
    for(const auto& profile:profiles)if(profile.telemetryKind==kind)return &profile;
    return nullptr;
}
std::span<const Bc2WeaponProfile> WeaponProfiles() noexcept {return profiles;}
}
