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
// Three bounded outcomes travel with the same immutable shot and helper row.
// They neither authorize a value nor retain another native/pose snapshot.
enum class Gate : std::uint8_t {
    NotAttempted,Passed,Absent,NoShot,AimEvidence,SelectedRig,Configuration,Attachment,Palette,Equipment,Generation,Owner,EquipmentGeneration,Space,Sequence,InputTime,ConfigurationTime,Units,RigCount,RigLayout,WeaponBone,Writes,ProfileMissing,ProfileAmbiguous,ProfileState,PartIndex,PartNameAmbiguous,PartHierarchy,PartHidden,WriteMissing,WriteAmbiguous,TrackedPartMatrix,NativePartMatrix,NativeWeaponMatrix,TrackedWeaponMatrix,InverseBindMatrix,PackedPalette,ProfileTuple,Deadline,ConfigurationDeadline,FiringBlocked,Soldier,Weak,HelperRejected,HelperTiming
};
struct Diagnostic {Gate capture=Gate::NotAttempted,retention=Gate::NotAttempted,helper=Gate::NotAttempted;};
static_assert(std::is_trivially_copyable_v<Diagnostic>&&sizeof(Diagnostic)==3);
inline const char* GateName(Gate gate)noexcept {
    switch(gate){
    case Gate::NotAttempted:return "not_attempted";
    case Gate::Passed:return "passed";
    case Gate::Absent:return "observation_absent";
    case Gate::NoShot:return "shot_absent";
    case Gate::AimEvidence:return "aim_evidence_absent";
    case Gate::SelectedRig:return "selected_rig_unverified";
    case Gate::Configuration:return "configuration_unverified";
    case Gate::Attachment:return "attachment_pending";
    case Gate::Palette:return "alternate_palette";
    case Gate::Equipment:return "equipment";
    case Gate::Generation:return "input_generation";
    case Gate::Owner:return "owner_generation";
    case Gate::EquipmentGeneration:return "equipment_generation";
    case Gate::Space:return "space";
    case Gate::Sequence:return "configuration_sequence";
    case Gate::InputTime:return "input_time";
    case Gate::ConfigurationTime:return "configuration_time";
    case Gate::Units:return "units";
    case Gate::RigCount:return "rig_count";
    case Gate::RigLayout:return "rig_layout";
    case Gate::WeaponBone:return "weapon_bone";
    case Gate::Writes:return "writes";
    case Gate::ProfileMissing:return "profile_missing";
    case Gate::ProfileAmbiguous:return "profile_ambiguous";
    case Gate::ProfileState:return "profile_state";
    case Gate::PartIndex:return "part_index";
    case Gate::PartNameAmbiguous:return "part_name_ambiguous";
    case Gate::PartHierarchy:return "part_hierarchy";
    case Gate::PartHidden:return "part_hidden";
    case Gate::WriteMissing:return "write_missing";
    case Gate::WriteAmbiguous:return "write_ambiguous";
    case Gate::TrackedPartMatrix:return "tracked_part_matrix";
    case Gate::NativePartMatrix:return "native_part_matrix";
    case Gate::NativeWeaponMatrix:return "native_weapon_matrix";
    case Gate::TrackedWeaponMatrix:return "tracked_weapon_matrix";
    case Gate::InverseBindMatrix:return "inverse_bind_matrix";
    case Gate::PackedPalette:return "packed_palette";
    case Gate::ProfileTuple:return "profile_tuple";
    case Gate::Deadline:return "input_deadline";
    case Gate::ConfigurationDeadline:return "configuration_deadline";
    case Gate::FiringBlocked:return "firing_blocked";
    case Gate::Soldier:return "soldier";
    case Gate::Weak:return "weak";
    case Gate::HelperRejected:return "helper_rejected";
    case Gate::HelperTiming:return "helper_timing";
    }return "unknown";
}
inline bool Rejected(Gate* outcome,Gate reason)noexcept {if(outcome)*outcome=reason;return false;}
inline bool Passed(Gate* outcome)noexcept {if(outcome)*outcome=Gate::Passed;return true;}
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
    std::span<const std::array<std::byte,64>> finalPacked,const math::Matrix4& trackedWeapon,Gate* outcome=nullptr)noexcept {
    const auto reject=[&](Gate reason)->std::optional<Frame>{Rejected(outcome,reason);return {};};
    const auto count=rig.identity.count;
    if(!s.selectedRigVerified)return reject(Gate::SelectedRig);
    if(!s.configurationVerified)return reject(Gate::Configuration);
    if(!s.attachmentReady)return reject(Gate::Attachment);
    if(!s.ordinaryPublishedPalette)return reject(Gate::Palette);
    if(!s.equipment.weapon||!s.equipment.data)return reject(Gate::Equipment);
    if(!s.generation)return reject(Gate::Generation);
    if(!s.ownerGeneration)return reject(Gate::Owner);
    if(!s.equipmentGeneration)return reject(Gate::EquipmentGeneration);
    if(!s.space)return reject(Gate::Space);
    if(!s.configurationSequence||s.configurationSequence>s.generation)return reject(Gate::Sequence);
    if(s.nowQpc<=0||s.nowQpc>=s.deadlineQpc)return reject(Gate::InputTime);
    if(s.nowNs<=0||s.nowNs>=s.configurationDeadlineNs)return reject(Gate::ConfigurationTime);
    if(!std::isfinite(s.unitsPerMetre)||s.unitsPerMetre<=0)return reject(Gate::Units);
    if(!count||count>1024)return reject(Gate::RigCount);
    if(rig.names.size()!=count||rig.parents.size()!=count||rig.evaluatedWorld.size()!=count||
       rig.inverseBind.size()!=count||rig.nativeEvaluated.size()!=count||finalPacked.size()!=count)return reject(Gate::RigLayout);
    if(rig.weaponBone>=count)return reject(Gate::WeaponBone);
    if(writes.empty()||writes.size()>count)return reject(Gate::Writes);
    const Profile* found=nullptr;
    for(const auto& p:profiles)if(p.configuration==s.configuration&&p.asset==s.equipment.Asset()&&
        p.mesh==s.mesh&&p.rigFingerprint==s.rigFingerprint){if(found)return reject(Gate::ProfileAmbiguous);found=&p;}
    if(!found)return reject(Gate::ProfileMissing);
    if(found->state!=0)return reject(Gate::ProfileState);
    if(found->expectedIndex>=count)return reject(Gate::PartIndex);
    unsigned bone=count;
    for(unsigned i=0;i<count;++i)if(rig.names[i]==found->part){if(bone!=count)return reject(Gate::PartNameAmbiguous);bone=i;}
    if(bone!=found->expectedIndex||rig.parents[bone]<0||unsigned(rig.parents[bone])>=count||
       rig.names[rig.parents[bone]]!=found->parent)return reject(Gate::PartHierarchy);
    // The weapon root is itself valid for fixed front-loading bores (AN94/GP30).
    bool descendant=bone==rig.weaponBone;
    for(unsigned hops=0,at=bone;!descendant&&hops<count;++hops){
        const auto parent=rig.parents[at];if(parent<0||unsigned(parent)>=count)break;
        at=unsigned(parent);descendant=at==rig.weaponBone;
    }
    if(!descendant)return reject(Gate::PartHierarchy);
    if(std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),bone)!=rig.nativeHiddenLeaves.end())return reject(Gate::PartHidden);
    const interaction::BoneWrite* write=nullptr;
    for(const auto& w:writes)if(w.index==bone){if(write)return reject(Gate::WriteAmbiguous);write=&w;}
    if(!write)return reject(Gate::WriteMissing);
    if(!interaction::InverseAnimatedTransform(write->transform))return reject(Gate::TrackedPartMatrix);
    if(!interaction::InverseAnimatedTransform(rig.evaluatedWorld[bone]))return reject(Gate::NativePartMatrix);
    if(!interaction::InverseAnimatedTransform(rig.evaluatedWorld[rig.weaponBone]))return reject(Gate::NativeWeaponMatrix);
    if(!interaction::InverseAnimatedTransform(trackedWeapon))return reject(Gate::TrackedWeaponMatrix);
    if(!interaction::InverseAnimatedTransform(rig.inverseBind[bone]))return reject(Gate::InverseBindMatrix);
    // Same packing convention as BuildRigPosePlan; exact final palette bytes,
    // including original SIMD padding. A later hidden/replaced part is rejected.
    auto skin=interaction::Multiply(rig.inverseBind[bone],write->transform);
    for(unsigned i=0;i<4;++i){skin.values[2][i]=-skin.values[2][i];skin.values[i][2]=-skin.values[i][2];}
    auto expected=rig.nativeEvaluated[bone];
    for(unsigned row=0;row<4;++row)std::memcpy(expected.data()+row*16,skin.values[row].data(),12);
    if(expected!=finalPacked[bone])return reject(Gate::PackedPalette);
    Frame f;f.profile=found;f.rig=rig.identity;f.equipment=s.equipment;
    f.generation=s.generation;f.ownerGeneration=s.ownerGeneration;f.equipmentGeneration=s.equipmentGeneration;f.space=s.space;
    f.deadlineQpc=s.deadlineQpc;f.configurationDeadlineNs=s.configurationDeadlineNs;f.configurationSequence=s.configurationSequence;
    f.bone=bone;f.unitsPerMetre=s.unitsPerMetre;f.nativePart=rig.evaluatedWorld[bone];f.trackedPart=write->transform;
    f.nativeWeapon=rig.evaluatedWorld[rig.weaponBone];f.trackedWeapon=trackedWeapon;Passed(outcome);return f;
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
inline bool Current(const Frame& f,const Source& s,Gate* outcome=nullptr)noexcept {
    if(!f.profile)return Rejected(outcome,Gate::ProfileMissing);
    if(!s.selectedRigVerified)return Rejected(outcome,Gate::SelectedRig);
    if(!s.configurationVerified)return Rejected(outcome,Gate::Configuration);
    if(!s.attachmentReady)return Rejected(outcome,Gate::Attachment);
    if(!s.ordinaryPublishedPalette)return Rejected(outcome,Gate::Palette);
    if(f.equipment!=s.equipment)return Rejected(outcome,Gate::Equipment);
    if(f.profile->configuration!=s.configuration||f.profile->asset!=s.equipment.Asset()||
       f.profile->mesh!=s.mesh||f.profile->rigFingerprint!=s.rigFingerprint||f.profile->state!=0)return Rejected(outcome,Gate::ProfileTuple);
    if(f.generation!=s.generation)return Rejected(outcome,Gate::Generation);
    if(f.ownerGeneration!=s.ownerGeneration)return Rejected(outcome,Gate::Owner);
    if(f.equipmentGeneration!=s.equipmentGeneration)return Rejected(outcome,Gate::EquipmentGeneration);
    if(f.space!=s.space)return Rejected(outcome,Gate::Space);
    if(f.deadlineQpc!=s.deadlineQpc)return Rejected(outcome,Gate::Deadline);
    if(f.configurationSequence!=s.configurationSequence)return Rejected(outcome,Gate::Sequence);
    if(f.configurationDeadlineNs!=s.configurationDeadlineNs)return Rejected(outcome,Gate::ConfigurationDeadline);
    if(f.unitsPerMetre!=s.unitsPerMetre)return Rejected(outcome,Gate::Units);
    if(s.nowQpc<=0||s.nowQpc>=f.deadlineQpc)return Rejected(outcome,Gate::InputTime);
    if(s.nowNs<=0||s.nowNs>=f.configurationDeadlineNs)return Rejected(outcome,Gate::ConfigurationTime);
    return Passed(outcome);
}
template<class Shot>
inline void RetainForShot(Shot& shot,const WeaponEquipmentIdentity& equipment,
    std::uint64_t equipmentGeneration,unsigned soldier,unsigned weak,
    std::int64_t nowQpc,std::int64_t nowNs,bool firingAllowed,Gate* outcome=nullptr)noexcept {
    if(!shot.muzzlePartObservation){Rejected(outcome,Gate::Absent);return;}
    const auto& f=*shot.muzzlePartObservation;Gate reason=Gate::Passed;
    if(!firingAllowed)reason=Gate::FiringBlocked;
    else if(f.equipment!=equipment)reason=Gate::Equipment;
    else if(f.rig.soldier!=soldier)reason=Gate::Soldier;
    else if(f.rig.weak!=weak)reason=Gate::Weak;
    else Current(f,SourceFromShot(shot,equipmentGeneration,nowQpc,nowNs),&reason);
    if(reason!=Gate::Passed)shot.muzzlePartObservation.reset();
    if(outcome)*outcome=reason;
}
// Helper consumes the scope's original value, never reads a newer publication.
// Validated existing helper owner/config checks are additionally required.
inline bool SameAcceptedHelper(const Frame& f,const WeaponEquipmentIdentity& equipment,
    unsigned soldier,unsigned weak,std::uint64_t generation,std::uint64_t ownerGeneration,std::uint64_t space,
    std::int64_t beforeQpc,std::int64_t afterQpc,std::int64_t afterNs,bool accepted,Gate* outcome=nullptr)noexcept {
    if(!accepted)return Rejected(outcome,Gate::HelperRejected);
    if(!f.profile)return Rejected(outcome,Gate::ProfileMissing);
    if(f.equipment!=equipment)return Rejected(outcome,Gate::Equipment);
    if(f.rig.soldier!=soldier)return Rejected(outcome,Gate::Soldier);
    if(f.rig.weak!=weak)return Rejected(outcome,Gate::Weak);
    if(f.generation!=generation)return Rejected(outcome,Gate::Generation);
    if(f.ownerGeneration!=ownerGeneration)return Rejected(outcome,Gate::Owner);
    if(f.space!=space)return Rejected(outcome,Gate::Space);
    if(beforeQpc<=0||afterQpc<beforeQpc)return Rejected(outcome,Gate::HelperTiming);
    if(afterQpc>=f.deadlineQpc)return Rejected(outcome,Gate::InputTime);
    if(afterNs<=0||afterNs>=f.configurationDeadlineNs)return Rejected(outcome,Gate::ConfigurationTime);
    return Passed(outcome);
}
} // namespace fvr::bc2::muzzle_part_observation
