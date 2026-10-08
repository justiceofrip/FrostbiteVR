#pragma once
#include "Bc2SightContact.h"
#include "fvr/interaction/SightGrasp.h"
#include <array>

namespace fvr::bc2 {
// Offline measured geometry only. Exact native mode/family, selected ownership,
// hand claims and write/restoration admission remain the adapter's obligation.
// In particular, a shared "40mmgl" asset name is never a family identity.
struct AuthoredSightGeometry {
    std::string_view meshPath,primaryAsset,primaryConfiguration,secondaryAsset,secondaryConfiguration;
    std::span<const std::string_view> boneChain; // Sight -> actual parent(s) -> weapon.
    std::uint64_t rigFingerprint=0;
    math::Vec3 minimumMeters{},maximumMeters{},axisPart{};
    float travelRadians=0;
};
struct AuthoredSightObservation {
    std::string_view selectedMeshPath,asset,configuration;
    std::uint64_t rigFingerprint=0,generation=0;
    std::int64_t now=0,deadline=0;
    std::span<const std::string> names;
    std::span<const std::int32_t> parents;
    std::span<const math::Matrix4> native;
    std::span<const unsigned> hidden;
    math::Matrix4 placedWeapon{},rawAnatomicalWrist{};
    // From the existing complete procedural MechanismGrip, not a weapon grip
    // or an authored AltDeploy hand pose mislabeled as contact.
    math::Vec3 mechanismPointWristMeters{};
    float unitsPerMetre=1;
    bool attachmentReady=false;
};
inline WeaponSightContact MeasureAuthoredSightContact(const AuthoredSightGeometry& p,
    const AuthoredSightObservation& s)noexcept {
    using namespace interaction;
    using namespace sight_grasp_detail;
    const bool mode=(s.asset==p.primaryAsset&&s.configuration==p.primaryConfiguration)||
        (s.asset==p.secondaryAsset&&s.configuration==p.secondaryConfiguration);
    if(!mode||p.meshPath.empty()||p.primaryAsset.empty()||p.secondaryAsset.empty()||
       p.primaryConfiguration.empty()||p.secondaryConfiguration.empty()||p.primaryConfiguration==p.secondaryConfiguration||
       s.selectedMeshPath!=p.meshPath||!p.rigFingerprint||s.rigFingerprint!=p.rigFingerprint||
       p.boneChain.size()<2||p.boneChain.size()>16||s.names.empty()||s.names.size()>1024||
       s.names.size()!=s.parents.size()||s.names.size()!=s.native.size()||!s.attachmentReady||
       !s.generation||s.now<=0||s.deadline<=s.now||!std::isfinite(s.unitsPerMetre)||s.unitsPerMetre<=0||
       !Unit(p.axisPart)||!Finite(p.minimumMeters)||!Finite(p.maximumMeters)||!Finite(s.mechanismPointWristMeters)||
       !std::isfinite(p.travelRadians)||p.travelRadians<=0||p.travelRadians>1.570796327f||
       !Proper(s.placedWeapon)||!Proper(s.rawAnatomicalWrist))return {};
    const std::array<float,3> lo{p.minimumMeters.x,p.minimumMeters.y,p.minimumMeters.z},
        hi{p.maximumMeters.x,p.maximumMeters.y,p.maximumMeters.z};
    for(unsigned axis=0;axis<3;++axis)if(hi[axis]<=lo[axis]||hi[axis]-lo[axis]>.3f||std::abs(lo[axis])>.5f||std::abs(hi[axis])>.5f)return {};
    std::array<unsigned,16> indices{};
    for(unsigned n=0;n<p.boneChain.size();++n){
        if(p.boneChain[n].empty())return {};
        const auto found=std::find(s.names.begin(),s.names.end(),p.boneChain[n]);
        if(found==s.names.end()||std::find(found+1,s.names.end(),p.boneChain[n])!=s.names.end())return {};
        indices[n]=unsigned(found-s.names.begin());
        if(std::find(indices.begin(),indices.begin()+n,indices[n])!=indices.begin()+n||
           std::find(s.hidden.begin(),s.hidden.end(),indices[n])!=s.hidden.end()||!Proper(s.native[indices[n]]))return {};
        if(n&&s.parents[indices[n-1]]!=std::int32_t(indices[n]))return {};
    }
    const auto inverseNative=InverseAnimatedTransform(s.native[indices[p.boneChain.size()-1]]);
    const auto inversePlaced=InverseAnimatedTransform(s.placedWeapon);
    if(!inverseNative||!inversePlaced)return {};
    auto sight=Multiply(s.native[indices[0]],*inverseNative),hand=Multiply(s.rawAnatomicalWrist,*inversePlaced);
    for(unsigned n=0;n<3;++n){sight.values[3][n]/=s.unitsPerMetre;hand.values[3][n]/=s.unitsPerMetre;}
    const auto inverseSight=InverseAnimatedTransform(sight);if(!inverseSight)return {};
    const auto palm=Point(s.mechanismPointWristMeters,hand),partPalm=Point(palm,*inverseSight);
    const math::Vec3 partContact{std::clamp(partPalm.x,lo[0],hi[0]),std::clamp(partPalm.y,lo[1],hi[1]),std::clamp(partPalm.z,lo[2],hi[2])};
    const auto origin=Point({},sight),direction=Point(p.axisPart,sight);
    math::Vec3 axis{direction.x-origin.x,direction.y-origin.y,direction.z-origin.z};
    const float length=std::hypot(axis.x,axis.y,axis.z);if(!std::isfinite(length)||length<.9f||length>1.1f)return {};
    axis={axis.x/length,axis.y/length,axis.z/length};
    const auto contact=Point(partContact,sight);const float distance=std::hypot(palm.x-contact.x,palm.y-contact.y,palm.z-contact.z);
    if(!Finite(palm)||!Finite(contact)||!std::isfinite(distance))return {};
    WeaponSightContact out;out.valid=true;out.generation=s.generation;out.deadline=s.deadline;
    out.pivotMeters=origin;out.axis=axis;out.handLocalMeters=palm;out.graspPointMeters=contact;out.contactDistanceMeters=distance;
    out.sightLocalMeters=sight;out.handLocalFrameMeters=out.rawHandLocalFrameMeters=hand;
    out.palmPointWristMeters=out.rawPalmPointWristMeters=s.mechanismPointWristMeters;
    // No invented second/front leaf. Leave legacy two-leaf previewValid false;
    // the profile consumer uses valid + this sight's matrix with BeginWithGeometry.
    // Native presentation must account for the entire actual sight subtree.
    return out;
}
}
