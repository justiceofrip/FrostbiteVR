#pragma once
#include "TrackingMath.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace fvr::interaction {
// This is policy data, not an engine layout or an IPC structure. Adapters own
// exact asset lookup, native attachment sampling and the evidence behind gates.
enum class WeaponFeature : std::uint8_t {AimAlignment,SupportGrip,TranslatedMuzzle,Count};
enum class WeaponVerification : std::uint8_t {Unverified,NativeVerified,HeadsetAccepted};
enum class WeaponStatus : std::uint8_t {UnknownProfile,InvalidProfile,Unverified,NativeVerified,HeadsetAccepted};
struct WeaponFeatureEvidence {
    WeaponVerification verification=WeaponVerification::Unverified;
    std::string_view nativeEvidence{},headsetEvidence{};
};
struct WeaponProfile {
    std::string_view stableId{};
    std::uint32_t revision=0;
    // Unit, perpendicular directions in the adapter's canonical model frame.
    // Both zero means absent; only aim alignment requires verified model axes.
    math::Vec3 modelForward{},modelUp{};
    std::array<WeaponFeatureEvidence,static_cast<std::size_t>(WeaponFeature::Count)> features{};
};

// Pure model-frame validation. This does not grant a feature verification status.
inline bool ValidModelAxes(math::Vec3 f,math::Vec3 u)noexcept {
    const auto finite=[](math::Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    const auto dot=[](math::Vec3 a,math::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    return finite(f)&&finite(u)&&std::abs(dot(f,f)-1.f)<=.0001f&&
        std::abs(dot(u,u)-1.f)<=.0001f&&std::abs(dot(f,u))<=.0001f;
}
inline bool ValidateWeaponProfile(const WeaponProfile& profile) noexcept {
    if(profile.stableId.empty()||profile.stableId.find('\0')!=std::string_view::npos||!profile.revision)return false;
    const auto& f=profile.modelForward;const auto& u=profile.modelUp;
    const bool axesAbsent=f.x==0&&f.y==0&&f.z==0&&u.x==0&&u.y==0&&u.z==0;
    if(axesAbsent){
        if(profile.features[static_cast<std::size_t>(WeaponFeature::AimAlignment)].verification!=WeaponVerification::Unverified)return false;
    }else if(!ValidModelAxes(f,u))return false;
    for(const auto& evidence:profile.features){
        switch(evidence.verification){
        case WeaponVerification::Unverified:break;
        case WeaponVerification::NativeVerified:
            if(evidence.nativeEvidence.empty())return false;
            break;
        case WeaponVerification::HeadsetAccepted:
            if(evidence.nativeEvidence.empty()||evidence.headsetEvidence.empty())return false;
            break;
        default:return false;
        }
    }
    return true;
}
inline WeaponStatus WeaponFeatureStatus(const WeaponProfile* profile,WeaponFeature feature) noexcept {
    if(!profile)return WeaponStatus::UnknownProfile;
    const auto index=static_cast<std::size_t>(feature);
    if(index>=profile->features.size()||!ValidateWeaponProfile(*profile))return WeaponStatus::InvalidProfile;
    switch(profile->features[index].verification){
    case WeaponVerification::NativeVerified:return WeaponStatus::NativeVerified;
    case WeaponVerification::HeadsetAccepted:return WeaponStatus::HeadsetAccepted;
    default:return WeaponStatus::Unverified;
    }
}
inline bool WeaponFeatureEnabled(const WeaponProfile* profile,WeaponFeature feature) noexcept {
    const auto status=WeaponFeatureStatus(profile,feature);
    return status==WeaponStatus::NativeVerified||status==WeaponStatus::HeadsetAccepted;
}
inline const char* WeaponStatusName(WeaponStatus status) noexcept {
    switch(status){
    case WeaponStatus::UnknownProfile:return "unknown_profile";
    case WeaponStatus::InvalidProfile:return "invalid_profile";
    case WeaponStatus::Unverified:return "unverified";
    case WeaponStatus::NativeVerified:return "native_verified";
    case WeaponStatus::HeadsetAccepted:return "headset_accepted";
    default:return "invalid_profile";
    }
}
inline const char* WeaponFeatureName(WeaponFeature feature) noexcept {
    switch(feature){
    case WeaponFeature::AimAlignment:return "aim_alignment";
    case WeaponFeature::SupportGrip:return "support_grip";
    case WeaponFeature::TranslatedMuzzle:return "translated_muzzle";
    default:return "invalid_feature";
    }
}
// Convert a canonical aim basis (+Z forward, +Y up) to model orientation.
// Translation is deliberately zero; authored grip placement remains dynamic.
// Reject reflections as well as malformed matrices rather than accepting a
// mirrored basis through the general near-rigid inverse helper.
// Pure conversion: the caller must independently authorize its source. Native
// WeaponProfile consumers retain their verification gate in WeaponAimFrame.
inline std::optional<math::Matrix4> ModelAimFrame(math::Vec3 f,math::Vec3 u,const math::Matrix4& canonicalAim) noexcept {
    if(!ValidModelAxes(f,u)||!InverseRigid(canonicalAim))return {};
    const auto& a=canonicalAim.values;
    const float determinant=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-
        a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if(!std::isfinite(determinant)||determinant<.99f)return {};
    const math::Vec3 right{u.y*f.z-u.z*f.y,u.z*f.x-u.x*f.z,u.x*f.y-u.y*f.x};
    math::Matrix4 modelBasis{};
    modelBasis.values[0]={right.x,right.y,right.z,0};
    modelBasis.values[1]={u.x,u.y,u.z,0};
    modelBasis.values[2]={f.x,f.y,f.z,0};modelBasis.values[3][3]=1;
    const auto modelToAim=InverseRigid(modelBasis);if(!modelToAim)return {};
    auto result=Multiply(*modelToAim,canonicalAim);
    result.values[3]={0,0,0,1};
    return result;
}
inline std::optional<math::Matrix4> WeaponAimFrame(const WeaponProfile& profile,const math::Matrix4& canonicalAim) noexcept {
    if(!WeaponFeatureEnabled(&profile,WeaponFeature::AimAlignment))return {};
    return ModelAimFrame(profile.modelForward,profile.modelUp,canonicalAim);
}
}
