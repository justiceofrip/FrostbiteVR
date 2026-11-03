#pragma once
// Passive same-publication diagnostics; never projectile-write enrollment.
// Profile storage supplied to Capture must outlive every returned Frame.
#include "Bc2Rig.h"
#include "Bc2EquipmentIdentity.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace fvr::bc2::muzzle_part_observation {
// Static data generated from retained294, never an inferred name/offset rule.
struct Profile {
    std::string_view configuration,asset,mesh,part,parent,tupleSha256;
    std::uint64_t rigFingerprint=0;
    unsigned expectedIndex=0,state=0;
    math::Vec3 localExitMeters{},localOutward{};
};
struct Frame {
    const Profile* profile=nullptr; // Compiled immutable table only.
    RigIdentity rig{};
    WeaponEquipmentIdentity equipment{};
    std::uint64_t generation=0,ownerGeneration=0,equipmentGeneration=0,space=0;
    std::int64_t deadlineQpc=0,configurationDeadlineNs=0;
    std::uint64_t configurationSequence=0;
    unsigned bone=0;float unitsPerMetre=1;
    // Same evaluated source and final write used to prepare WeaponShotFrame.
    // Canonical row-vector LH, world units. No bind/sample/flash replacement.
    math::Matrix4 nativePart{},trackedPart{},nativeWeapon{},trackedWeapon{};
};
static_assert(std::is_trivially_copyable_v<Frame>);
struct Source {
    WeaponEquipmentIdentity equipment{};
    std::string_view configuration,mesh;
    std::uint64_t rigFingerprint=0,generation=0,ownerGeneration=0,equipmentGeneration=0,space=0;
    std::int64_t nowQpc=0,deadlineQpc=0,nowNs=0,configurationDeadlineNs=0;
    std::uint64_t configurationSequence=0;
    float unitsPerMetre=1;
    bool selectedRigVerified=false,configurationVerified=false,attachmentReady=false;
    // Caller excludes late palette fallback/hide/overlay paths, see CANDIDATE.txt.
    bool ordinaryPublishedPalette=false;
};
// Takes only already-validated rig/publication values. No native reads, IO,
// allocation, locks, clock sampling, lease renewal, or firing side effects.
inline std::optional<Frame> Capture(std::span<const Profile> profiles,const Source& s,
    const RigSnapshot& rig,std::span<const interaction::BoneWrite> writes,
    std::span<const std::array<std::byte,64>> finalPacked,const math::Matrix4& trackedWeapon)noexcept {
    const auto count=rig.identity.count;
    if(!s.selectedRigVerified||!s.configurationVerified||!s.attachmentReady||!s.ordinaryPublishedPalette||
       !s.equipment.weapon||!s.equipment.data||!s.generation||!s.ownerGeneration||
       !s.equipmentGeneration||!s.space||!s.configurationSequence||s.configurationSequence>s.generation||
       s.nowQpc<=0||s.nowQpc>=s.deadlineQpc||s.nowNs<=0||s.nowNs>=s.configurationDeadlineNs||
       !std::isfinite(s.unitsPerMetre)||s.unitsPerMetre<=0||!count||count>1024||
       rig.names.size()!=count||rig.parents.size()!=count||rig.evaluatedWorld.size()!=count||
       rig.inverseBind.size()!=count||rig.nativeEvaluated.size()!=count||finalPacked.size()!=count||
       rig.weaponBone>=count||writes.empty()||writes.size()>count)return {};
    const Profile* found=nullptr;
    for(const auto& p:profiles)if(p.configuration==s.configuration&&p.asset==s.equipment.Asset()&&
        p.mesh==s.mesh&&p.rigFingerprint==s.rigFingerprint){if(found)return {};found=&p;}
    if(!found||found->state!=0||found->expectedIndex>=count)return {};
    unsigned bone=count;
    for(unsigned i=0;i<count;++i)if(rig.names[i]==found->part){if(bone!=count)return {};bone=i;}
    if(bone!=found->expectedIndex||rig.parents[bone]<0||unsigned(rig.parents[bone])>=count||
       rig.names[rig.parents[bone]]!=found->parent)return {};
    // The weapon root is itself valid for fixed front-loading bores (AN94/GP30).
    bool descendant=bone==rig.weaponBone;
    for(unsigned hops=0,at=bone;!descendant&&hops<count;++hops){
        const auto parent=rig.parents[at];if(parent<0||unsigned(parent)>=count)break;
        at=unsigned(parent);descendant=at==rig.weaponBone;
    }
    if(!descendant||std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),bone)!=rig.nativeHiddenLeaves.end())return {};
    const interaction::BoneWrite* write=nullptr;
    for(const auto& w:writes)if(w.index==bone){if(write)return {};write=&w;}
    if(!write||!interaction::InverseAnimatedTransform(write->transform)||
       !interaction::InverseAnimatedTransform(rig.evaluatedWorld[bone])||
       !interaction::InverseAnimatedTransform(rig.evaluatedWorld[rig.weaponBone])||
       !interaction::InverseAnimatedTransform(trackedWeapon)||
       !interaction::InverseAnimatedTransform(rig.inverseBind[bone]))return {};
    // Same packing convention as BuildRigPosePlan; exact final palette bytes,
    // including original SIMD padding. A later hidden/replaced part is rejected.
    auto skin=interaction::Multiply(rig.inverseBind[bone],write->transform);
    for(unsigned i=0;i<4;++i){skin.values[2][i]=-skin.values[2][i];skin.values[i][2]=-skin.values[i][2];}
    auto expected=rig.nativeEvaluated[bone];
    for(unsigned row=0;row<4;++row)std::memcpy(expected.data()+row*16,skin.values[row].data(),12);
    if(expected!=finalPacked[bone])return {};
    Frame f;f.profile=found;f.rig=rig.identity;f.equipment=s.equipment;
    f.generation=s.generation;f.ownerGeneration=s.ownerGeneration;f.equipmentGeneration=s.equipmentGeneration;f.space=s.space;
    f.deadlineQpc=s.deadlineQpc;f.configurationDeadlineNs=s.configurationDeadlineNs;f.configurationSequence=s.configurationSequence;
    f.bone=bone;f.unitsPerMetre=s.unitsPerMetre;f.nativePart=rig.evaluatedWorld[bone];f.trackedPart=write->transform;
    f.nativeWeapon=rig.evaluatedWorld[rig.weaponBone];f.trackedWeapon=trackedWeapon;return f;
}
// Reconstruct only already-published identity values. No selected-mesh/native read.
// A missing aim record leaves the verification booleans false.
template<class Shot>
inline Source SourceFromShot(const Shot& shot,std::uint64_t equipmentGeneration,
    std::int64_t nowQpc,std::int64_t nowNs)noexcept {
    Source s;s.generation=shot.generation;s.ownerGeneration=shot.ownerGeneration;
    s.equipmentGeneration=equipmentGeneration;s.space=shot.space;
    s.nowQpc=nowQpc;s.deadlineQpc=shot.deadline;s.nowNs=nowNs;s.unitsPerMetre=shot.unitsPerMetre;
    s.attachmentReady=s.ordinaryPublishedPalette=true;
    if(shot.aimEvidence){const auto& a=*shot.aimEvidence;s.equipment=a.equipment;
        const auto end=static_cast<const char*>(std::memchr(a.configurationPath.data(),0,a.configurationPath.size()));
        if(end)s.configuration={a.configurationPath.data(),std::size_t(end-a.configurationPath.data())};
        s.mesh=a.meshPath;s.rigFingerprint=a.rigFingerprint;s.configurationSequence=a.configurationSequence;
        s.configurationDeadlineNs=a.configurationDeadlineNs;s.selectedRigVerified=a.selectedRigVerified;
        s.configurationVerified=a.configurationPathVerified;}
    return s;
}
inline bool Current(const Frame& f,const Source& s)noexcept {
    return f.profile&&s.selectedRigVerified&&s.configurationVerified&&s.attachmentReady&&s.ordinaryPublishedPalette&&
        f.equipment==s.equipment&&f.profile->configuration==s.configuration&&f.profile->asset==s.equipment.Asset()&&
        f.profile->mesh==s.mesh&&f.profile->rigFingerprint==s.rigFingerprint&&f.profile->state==0&&
        f.generation==s.generation&&f.ownerGeneration==s.ownerGeneration&&f.equipmentGeneration==s.equipmentGeneration&&
        f.space==s.space&&f.deadlineQpc==s.deadlineQpc&&f.configurationSequence==s.configurationSequence&&
        f.configurationDeadlineNs==s.configurationDeadlineNs&&f.unitsPerMetre==s.unitsPerMetre&&
        s.nowQpc>0&&s.nowQpc<f.deadlineQpc&&s.nowNs>0&&s.nowNs<f.configurationDeadlineNs;
}
template<class Shot>
inline void RetainForShot(Shot& shot,const WeaponEquipmentIdentity& equipment,
    std::uint64_t equipmentGeneration,unsigned soldier,unsigned weak,
    std::int64_t nowQpc,std::int64_t nowNs,bool firingAllowed)noexcept {
    if(!shot.muzzlePartObservation)return;
    const auto& f=*shot.muzzlePartObservation;
    if(!firingAllowed||f.equipment!=equipment||f.rig.soldier!=soldier||f.rig.weak!=weak||
       !Current(f,SourceFromShot(shot,equipmentGeneration,nowQpc,nowNs)))shot.muzzlePartObservation.reset();
}
// Helper consumes the scope's original value, never reads a newer publication.
// Validated existing helper owner/config checks are additionally required.
inline bool SameAcceptedHelper(const Frame& f,const WeaponEquipmentIdentity& equipment,
    unsigned soldier,unsigned weak,std::uint64_t generation,std::uint64_t ownerGeneration,std::uint64_t space,
    std::int64_t beforeQpc,std::int64_t afterQpc,std::int64_t afterNs,bool accepted)noexcept {
    return accepted&&f.profile&&f.equipment==equipment&&f.rig.soldier==soldier&&f.rig.weak==weak&&
        f.generation==generation&&f.ownerGeneration==ownerGeneration&&f.space==space&&
        beforeQpc>0&&afterQpc>=beforeQpc&&afterQpc<f.deadlineQpc&&afterNs>0&&afterNs<f.configurationDeadlineNs;
}
} // namespace fvr::bc2::muzzle_part_observation
