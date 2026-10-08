#pragma once
#include "Bc2EquipmentIdentity.h"
#include "Bc2SelectedMeshes1p.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/WeaponProfile.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string_view>

namespace fvr::bc2 {
// Generated from exact typed animation/skeleton references and constant ancestor
// controls. No game asset bytes, native capability or active-animation claim.
struct AuthoredGripProfile {
    std::string_view assetName,meshPath,bindingDigest,clipSha256,skeletonSha256;
    std::uint64_t rigFingerprint=0;
    math::Matrix4 rightInWeapon{},leftInWeapon{}; // canonical row-vector metres
    // Exact authored wcAssault/wcSmg reference, not native/headset verification.
    // Older generated data stays grip-only until explicitly regenerated.
    bool authoredRifleSupport=false;
    // Optional explicitly reviewed model geometry, never inferred from class.
    // This is experimental visual alignment, not NativeVerified fire geometry.
    math::Vec3 modelForward{},modelUp{};
    std::string_view authoredAxisEvidence{};
};
struct AuthoredGripOwner {
    unsigned soldier=0,weak=0;
    std::uint64_t actorGeneration=0,equipmentGeneration=0,space=0,inputSequence=0;
    std::int64_t inputDeadlineNs=0;
};
enum class AuthoredGripStatus : unsigned {
    NoProfile,PreservedBaseline,MissingSnapshot,InvalidProfile,IdentityMismatch,
    Expired,AmbiguousConfiguration,RigMismatch,Bound,Count
};
struct AuthoredGripBinding {
    const AuthoredGripProfile* profile=nullptr; // immutable compiled registry
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
    WeaponEquipmentIdentity equipment{};
    AuthoredGripOwner owner{};
    static constexpr bool activeAnimationVerified=false,submittedSkinVerified=false,
        aimCapability=false,muzzleCapability=false,reloadCapability=false,holsterCapability=false;
};
struct AuthoredGripResult {AuthoredGripStatus status=AuthoredGripStatus::NoProfile;std::optional<AuthoredGripBinding> binding;};
namespace authored_grip_detail {
template<std::size_t N> inline std::string_view Text(const std::array<char,N>& bytes)noexcept {
    const auto end=static_cast<const char*>(std::memchr(bytes.data(),0,N));
    return end?std::string_view(bytes.data(),std::size_t(end-bytes.data())):std::string_view{};
}
inline bool Digest(std::string_view value)noexcept {
    return value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
inline bool Matrix(const math::Matrix4& value)noexcept {
    return interaction::InverseRigid(value).has_value()&&
        std::hypot(value.values[3][0],value.values[3][1],value.values[3][2])<=1.5f;
}
inline bool ModelAxes(const AuthoredGripProfile& p)noexcept {
    const auto f=p.modelForward,u=p.modelUp;
    if(p.authoredAxisEvidence.empty())return f.x==0&&f.y==0&&f.z==0&&u.x==0&&u.y==0&&u.z==0;
    return Digest(p.authoredAxisEvidence)&&interaction::ValidModelAxes(f,u);
}
inline bool Profile(const AuthoredGripProfile& p)noexcept {
    return ModelAxes(p)&& !p.assetName.empty()&&!p.meshPath.empty()&&p.rigFingerprint&&
        Digest(p.bindingDigest)&&Digest(p.clipSha256)&&Digest(p.skeletonSha256)&&Matrix(p.rightInWeapon)&&Matrix(p.leftInWeapon);
}
inline bool Fresh(const SelectedMeshesSnapshot& s,std::int64_t now)noexcept {
    return s.sequence&&s.observedNs>0&&now>=s.observedNs&&s.deadlineNs>now&&s.deadlineNs-s.observedNs<=250000000;
}
inline bool Configuration(const AuthoredGripProfile& p,const SelectedMeshesSnapshot& s)noexcept {
    if(s.stateCount!=1||!s.soleConfiguredArray||s.states[0].array!=s.soleConfiguredArray||
       !s.states[0].state||!s.states[0].count||s.states[0].count>s.states[0].meshes.size()||
       !s.stateTypeInfo||!s.meshTypeInfo||!s.inventory)return false;
    unsigned matches=0;
    for(unsigned n=0;n<s.states[0].count;++n){const auto& m=s.states[0].meshes[n];
        if(!m.address||m.typeInfo!=s.meshTypeInfo||!m.namePointer||Text(m.assetPath).empty())return false;
        if(Text(m.assetPath)==p.meshPath)++matches;
    }
    return matches==1;
}
}
// Existing accepted weapon profiles remain untouched unless the caller explicitly
// selects this experimental authored source. A duplicate catalog entry rejects.
inline AuthoredGripResult BindAuthoredGrip(std::span<const AuthoredGripProfile> profiles,
    std::shared_ptr<const SelectedMeshesSnapshot> snapshot,const WeaponEquipmentIdentity& equipment,
    const AuthoredGripOwner& owner,std::uint64_t rigFingerprint,std::int64_t now,
    bool acceptedBaseline,bool explicitlySelect=false)noexcept {
    if(acceptedBaseline&&!explicitlySelect)return {AuthoredGripStatus::PreservedBaseline,{}};
    const AuthoredGripProfile* p=nullptr;bool foundAsset=false;
    for(const auto& entry:profiles)if(entry.assetName==equipment.Asset()){
        foundAsset=true;
        if(!authored_grip_detail::Profile(entry))return {AuthoredGripStatus::InvalidProfile,{}};
        if(snapshot&&authored_grip_detail::Configuration(entry,*snapshot)){
            if(p)return {AuthoredGripStatus::AmbiguousConfiguration,{}};p=&entry;
        }
    }
    if(!foundAsset)return {};
    if(!snapshot)return {AuthoredGripStatus::MissingSnapshot,{}};
    if(!p)return {AuthoredGripStatus::AmbiguousConfiguration,{}};
    const auto& s=*snapshot;const auto& o=s.owner;
    if(!equipment.weapon||!equipment.data||!equipment.persistence||!owner.soldier||!owner.weak||
       !owner.actorGeneration||!owner.equipmentGeneration||!owner.space||!owner.inputSequence||
       !o.player||!o.equipGeneration||o.soldier!=owner.soldier||o.weak!=owner.weak||o.weapon!=equipment.weapon||
       o.actorGeneration!=owner.actorGeneration||o.space!=owner.space||s.weaponData!=equipment.data||
       authored_grip_detail::Text(s.weaponName)!=equipment.Asset())return {AuthoredGripStatus::IdentityMismatch,{}};
    if(!authored_grip_detail::Fresh(s,now)||owner.inputDeadlineNs<=now||s.sequence>owner.inputSequence)return {AuthoredGripStatus::Expired,{}};
    if(!authored_grip_detail::Configuration(*p,s))return {AuthoredGripStatus::AmbiguousConfiguration,{}};
    if(rigFingerprint!=p->rigFingerprint)return {AuthoredGripStatus::RigMismatch,{}};
    return {AuthoredGripStatus::Bound,AuthoredGripBinding{p,std::move(snapshot),equipment,owner}};
}
// Snapshot refresh may confirm the same configuration; it NEVER extends the old
// pose's input/configuration deadlines. Current native equipment is read separately
// by the adapter before output, every native Pack, and any firing/contact consumer.
inline bool AuthoredGripCurrent(const AuthoredGripBinding& b,const SelectedMeshesSnapshot* current,
    const WeaponEquipmentIdentity& equipment,const AuthoredGripOwner& owner,std::int64_t now)noexcept {
    if(!b.profile||!b.selected||!current||!authored_grip_detail::Profile(*b.profile)||
       b.equipment!=equipment||b.owner.soldier!=owner.soldier||b.owner.weak!=owner.weak||
       b.owner.actorGeneration!=owner.actorGeneration||b.owner.equipmentGeneration!=owner.equipmentGeneration||
       b.owner.space!=owner.space||b.owner.inputSequence>owner.inputSequence||b.owner.inputDeadlineNs<=now||owner.inputDeadlineNs<=now)return false;
    const auto& old=*b.selected;
    return authored_grip_detail::Fresh(old,now)&&authored_grip_detail::Fresh(*current,now)&&
        current->owner==old.owner&&current->sequence>=old.sequence&&current->observedNs>=old.observedNs&&
        current->weaponData==old.weaponData&&current->inventory==old.inventory&&current->selectedSlot==old.selectedSlot&&
        current->stateTypeInfo==old.stateTypeInfo&&current->meshTypeInfo==old.meshTypeInfo&&
        current->weaponName==old.weaponName&&current->stateCount==old.stateCount&&
        current->soleConfiguredArray==old.soleConfiguredArray&&current->states==old.states;
}
inline bool AuthoredRifleSupport(const AuthoredGripBinding& b)noexcept {
    if(!b.profile||!b.selected||!b.profile->authoredRifleSupport||!authored_grip_detail::Profile(*b.profile))return false;
    const auto& l=b.profile->leftInWeapon.values[3];const auto& r=b.profile->rightInWeapon.values[3];
    const float separation=std::hypot(l[0]-r[0],l[1]-r[1],l[2]-r[2]);
    // Same supported separation domain as the portable two-hand policy.
    return separation>=.1f&&separation<=1.2f;
}
// Call only with a freshly admitted binding; output readers/Pack retain its
// immutable source deadlines and exact equipment/configuration/rig checks.
inline std::optional<math::Matrix4> AuthoredWeaponAimFrame(const AuthoredGripBinding& b,const math::Matrix4& aim)noexcept {
    if(!b.profile||!b.selected||!authored_grip_detail::Profile(*b.profile)||b.profile->authoredAxisEvidence.empty())return {};
    return interaction::ModelAimFrame(b.profile->modelForward,b.profile->modelUp,aim);
}
inline math::Matrix4 AuthoredGripWorldUnits(math::Matrix4 metres,float units)noexcept {
    for(unsigned n=0;n<3;++n)metres.values[3][n]*=units;return metres;
}
} // namespace fvr::bc2
