#pragma once
#include "fvr/interaction/TrackingMath.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace fvr::bc2 {
struct WeaponSightContact {
    bool valid=false;
    std::uint64_t generation=0;std::int64_t deadline=0;
    math::Vec3 pivotMeters{},axis{},handLocalMeters{};
    float contactDistanceMeters=0;
    math::Matrix4 sightLocalMeters{},frontLocalMeters{},handLocalFrameMeters{};
    // Preserve accepted raw contact geometry before a visual hand-role override.
    math::Matrix4 rawHandLocalFrameMeters{};math::Vec3 rawPalmPointWristMeters{};
    math::Vec3 graspPointMeters{},palmPointWristMeters{};
    bool previewValid=false;
};
// Same ordered names/parents/canonical inverse-bind fingerprint as the source
// capture. It is metadata evidence, not a memory address or inherited bone index.
inline std::uint64_t SightRigFingerprint(std::span<const std::string> names,
    std::span<const std::int32_t> parents,std::span<const math::Matrix4> inverseBind)noexcept {
    if(names.empty()||names.size()>1024||parents.size()!=names.size()||inverseBind.size()!=names.size())return 0;
    std::uint64_t fingerprint=14695981039346656037ull;
    const auto word=[&](std::uint32_t value){for(unsigned n=0;n<4;++n){fingerprint^=(value>>(n*8))&255;fingerprint*=1099511628211ull;}};
    for(unsigned n=0;n<names.size();++n){
        for(unsigned char c:names[n]){fingerprint^=c;fingerprint*=1099511628211ull;}
        word(0);word(std::uint32_t(parents[n]));
        for(const auto& row:inverseBind[n].values)for(float value:row)word(std::bit_cast<std::uint32_t>(value));
    }
    return fingerprint;
}
// Use authored finger-base landmarks to measure the visible hand's grip area.
// The wrist origin is ~7cm behind it in the verified rig; snapping that origin
// into the ladder would put the fingers beyond the intended contact.
inline std::optional<math::Vec3> MeasureSightPalm(std::span<const std::string> names,
    std::span<const std::int32_t> parents,std::span<const math::Matrix4> native,
    unsigned wrist,float unitsPerMetre)noexcept {
    if(names.size()!=parents.size()||names.size()!=native.size()||wrist>=names.size()||
       names[wrist]!="LeftHand"||!std::isfinite(unitsPerMetre)||unitsPerMetre<=0)return {};
    const auto inverse=interaction::InverseAnimatedTransform(native[wrist]);if(!inverse)return {};
    math::Vec3 center{};
    for(const char* name:{"LeftHandIndex1","LeftHandMiddle1","LeftHandRing1","LeftHandPinky1"}){
        const auto found=std::find(names.begin(),names.end(),name);if(found==names.end())return {};
        const auto index=unsigned(found-names.begin());
        if(parents[index]!=std::int32_t(wrist)||!interaction::InverseAnimatedTransform(native[index]))return {};
        const auto local=interaction::Multiply(native[index],*inverse);
        center.x+=local.values[3][0]/(4*unitsPerMetre);center.y+=local.values[3][1]/(4*unitsPerMetre);center.z+=local.values[3][2]/(4*unitsPerMetre);
    }
    if(!std::isfinite(center.x)||!std::isfinite(center.y)||!std::isfinite(center.z))return {};
    return center;
}
struct WeaponSightObservation {
    std::string_view assetName,rootName,sightName,sightParentName;
    std::uint64_t skeletonFingerprint=0,generation=0;std::int64_t deadline=0;
    math::Matrix4 nativeWeapon{},nativeSight{},placedWeapon{},rawLeftTarget{};
    float unitsPerMetre=1,segmentLengthMeters=.11f,minGrabAlongMeters=.04f;
    math::Vec3 palmPointWristMeters{};
    bool palmValid=false,frontValid=false;
    // Display role is independent of the accepted raw gesture contact above.
    // A mechanism may anchor between fingers instead of at the gun-grip palm.
    bool visualHandValid=false;
    math::Matrix4 visualLeftTarget{};
    math::Vec3 visualPointWristMeters{};
    math::Matrix4 nativeFront{};
    // Equip-transition palettes can collapse the hinge onto the weapon root.
    // Only the adapter's settled authored attachment is contact evidence.
    bool hidden=false,attachmentReady=false;
};
// October 1 source/projection evidence: scoped XM8 and its XM320 mode share this
// rig. jntWpn_9 is the rear ladder, direct child of jntWpn_1. Its observed ladder
// extends about11cm along joint-local -Z; row0 is the hinge axis. This function
// measures contact only. It never edits native sight matrices or enables a mode.
inline WeaponSightContact MeasureWeaponSightContact(const WeaponSightObservation& s)noexcept {
    WeaponSightContact out;
    if((s.assetName!="XM8_sp_s"&&s.assetName!="40mmgl")||s.skeletonFingerprint!=0xa7f219a1426216abull||
       s.rootName!="jntWpn_1"||s.sightName!="jntWpn_9"||s.sightParentName!=s.rootName||s.hidden||!s.attachmentReady||!s.palmValid||
       !s.generation||s.deadline<=0||!std::isfinite(s.unitsPerMetre)||s.unitsPerMetre<=0||
       !std::isfinite(s.segmentLengthMeters)||s.segmentLengthMeters<=0||s.segmentLengthMeters>.3f||
       !std::isfinite(s.minGrabAlongMeters)||s.minGrabAlongMeters<0||s.minGrabAlongMeters>=s.segmentLengthMeters||
       !std::isfinite(s.palmPointWristMeters.x)||!std::isfinite(s.palmPointWristMeters.y)||!std::isfinite(s.palmPointWristMeters.z))return out;
    const auto inverseNative=interaction::InverseAnimatedTransform(s.nativeWeapon);
    const auto inversePlaced=interaction::InverseAnimatedTransform(s.placedWeapon);
    if(!inverseNative||!inversePlaced||!interaction::InverseAnimatedTransform(s.nativeSight)||
       !interaction::InverseAnimatedTransform(s.rawLeftTarget))return out;
    const auto relative=interaction::Multiply(s.nativeSight,*inverseNative);
    auto hand=interaction::Multiply(s.rawLeftTarget,*inversePlaced);
    for(unsigned n=0;n<3;++n)hand.values[3][n]/=s.unitsPerMetre;
    const auto normalize=[](math::Vec3 value)->std::optional<math::Vec3>{
        const float length=std::hypot(value.x,value.y,value.z);
        if(!std::isfinite(length)||length<.9f||length>1.1f)return {};
        return math::Vec3{value.x/length,value.y/length,value.z/length};
    };
    const auto axis=normalize({relative.values[0][0],relative.values[0][1],relative.values[0][2]});
    const auto direction=normalize({-relative.values[2][0],-relative.values[2][1],-relative.values[2][2]});
    if(!axis||!direction)return out;
    out.pivotMeters={relative.values[3][0]/s.unitsPerMetre,relative.values[3][1]/s.unitsPerMetre,relative.values[3][2]/s.unitsPerMetre};
    const auto palm=s.palmPointWristMeters;
    out.handLocalMeters={palm.x*hand.values[0][0]+palm.y*hand.values[1][0]+palm.z*hand.values[2][0]+hand.values[3][0],
        palm.x*hand.values[0][1]+palm.y*hand.values[1][1]+palm.z*hand.values[2][1]+hand.values[3][1],
        palm.x*hand.values[0][2]+palm.y*hand.values[1][2]+palm.z*hand.values[2][2]+hand.values[3][2]};
    const math::Vec3 delta{out.handLocalMeters.x-out.pivotMeters.x,out.handLocalMeters.y-out.pivotMeters.y,out.handLocalMeters.z-out.pivotMeters.z};
    const float along=std::clamp(delta.x*direction->x+delta.y*direction->y+delta.z*direction->z,s.minGrabAlongMeters,s.segmentLengthMeters);
    out.contactDistanceMeters=std::hypot(delta.x-along*direction->x,delta.y-along*direction->y,delta.z-along*direction->z);
    if(!std::isfinite(out.contactDistanceMeters))return {};
    out.graspPointMeters={out.pivotMeters.x+along*direction->x,out.pivotMeters.y+along*direction->y,out.pivotMeters.z+along*direction->z};
    out.handLocalFrameMeters=hand;out.palmPointWristMeters=palm;out.sightLocalMeters=relative;
    out.rawHandLocalFrameMeters=hand;out.rawPalmPointWristMeters=palm;
    for(unsigned n=0;n<3;++n)out.sightLocalMeters.values[3][n]/=s.unitsPerMetre;
    if(s.frontValid&&interaction::InverseAnimatedTransform(s.nativeFront)){
        out.frontLocalMeters=interaction::Multiply(s.nativeFront,*inverseNative);
        for(unsigned n=0;n<3;++n)out.frontLocalMeters.values[3][n]/=s.unitsPerMetre;
        out.previewValid=true;
    }
    if(s.visualHandValid){
        const auto p=s.visualPointWristMeters;
        if(!interaction::InverseAnimatedTransform(s.visualLeftTarget)||
           !std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)){
            out.previewValid=false;
        }else{
            out.handLocalFrameMeters=interaction::Multiply(s.visualLeftTarget,*inversePlaced);
            for(unsigned n=0;n<3;++n)out.handLocalFrameMeters.values[3][n]/=s.unitsPerMetre;
            out.palmPointWristMeters=p;
        }
    }
    out.axis=*axis;out.generation=s.generation;out.deadline=s.deadline;out.valid=true;return out;
}
}
