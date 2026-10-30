#pragma once
#include "Bc2AuthoredGrip.h"
#include "Bc2AuthoredSupport.h"
#include "fvr/interaction/FiringPoseHistory.h"

namespace fvr::bc2 {
// Candidate enrollment is explicit compiled test authorization, not a claim of
// native verification. Ordinary builds have no generated enrollment by default.
enum class MuzzleAdmission : unsigned { Disabled, Candidate, NativeVerified };
struct MuzzleEnrollmentProfile {
    std::string_view asset,configurationPath,meshPath,bindingDigest,axisEvidence;
    std::string_view configurationDigest,firingDigest,evidence;
    std::uint64_t rigFingerprint=0;
    math::Matrix4 flashInWeapon{}; // reviewed canonical metres
    MuzzleAdmission admission=MuzzleAdmission::Disabled;
    std::array<float,3> initialSpeed{},initialDirection{},initialPosition{};
    float inheritedWeaponSpeed=0;
    unsigned bulletsPerShell=0,bulletsPerShot=0;
    bool activeForceSpawnToCamera=false;
    // Support-backed origin references also pin their exact authored sources.
    // axisEvidence remains a provenance digest; it never grants aim rotation.
    std::string_view clipSha256,skeletonSha256;
};
struct MuzzleBinding {
    const MuzzleEnrollmentProfile* profile=nullptr;
    AuthoredGripBinding authored{}; // original configuration/input leases
    std::optional<AuthoredSupportBinding> support;
};
namespace muzzle_detail {
inline bool Profile(const MuzzleEnrollmentProfile& p)noexcept {
    return (p.admission==MuzzleAdmission::Candidate||p.admission==MuzzleAdmission::NativeVerified)&&
        !p.asset.empty()&&!p.configurationPath.empty()&&!p.meshPath.empty()&&!p.evidence.empty()&&p.rigFingerprint&&
        authored_grip_detail::Digest(p.bindingDigest)&&authored_grip_detail::Digest(p.axisEvidence)&&
        authored_grip_detail::Digest(p.configurationDigest)&&authored_grip_detail::Digest(p.firingDigest)&&
        interaction::InverseRigid(p.flashInWeapon)&&p.bulletsPerShell&&p.bulletsPerShot&&
        p.initialDirection==std::array<float,3>{0,0,1}&&p.initialPosition==std::array<float,3>{0,0,0}&&
        p.initialSpeed[0]==0&&p.initialSpeed[1]==0&&std::isfinite(p.initialSpeed[2])&&p.initialSpeed[2]>0&&
        p.inheritedWeaponSpeed==0;
}
inline bool Configuration(const MuzzleEnrollmentProfile& p,const SelectedMeshesSnapshot& s)noexcept {
    return s.configurationPathVerified&&s.configurationPathPointer&&
        authored_grip_detail::Text(s.configurationPath)==p.configurationPath;
}
inline bool Descriptor(const MuzzleEnrollmentProfile& p,const AuthoredGripBinding& b)noexcept {
    return Profile(p)&&b.profile&&b.selected&&authored_grip_detail::Profile(*b.profile)&&
        p.asset==b.equipment.Asset()&&p.asset==b.profile->assetName&&p.meshPath==b.profile->meshPath&&
        p.bindingDigest==b.profile->bindingDigest&&p.axisEvidence==b.profile->authoredAxisEvidence&&
        p.rigFingerprint==b.profile->rigFingerprint&&Configuration(p,*b.selected);
}
inline bool Descriptor(const MuzzleEnrollmentProfile& p,const AuthoredSupportBinding& b)noexcept {
    return Profile(p)&&b.profile&&b.selected&&
        p.asset==b.equipment.Asset()&&p.asset==b.profile->assetName&&p.meshPath==b.profile->meshPath&&
        p.bindingDigest==b.profile->bindingDigest&&p.rigFingerprint==b.profile->rigFingerprint&&
        authored_grip_detail::Digest(p.clipSha256)&&authored_grip_detail::Digest(p.skeletonSha256)&&
        p.clipSha256==b.profile->clipSha256&&p.skeletonSha256==b.profile->skeletonSha256&&
        Configuration(p,*b.selected);
}
inline bool Flash(const MuzzleEnrollmentProfile& p,const math::Matrix4& weapon,
                  const math::Matrix4& flash,float units)noexcept {
    if(!std::isfinite(units)||units<=0||!interaction::InverseAnimatedTransform(flash))return false;
    const auto inverse=interaction::InverseAnimatedTransform(weapon);if(!inverse)return false;
    auto local=interaction::Multiply(flash,*inverse);
    for(unsigned n=0;n<3;++n)local.values[3][n]/=units;
    // One millimetre/local-basis 0.001 permits float cancellation at the recorded
    // campaign world coordinates, not a different attachment or guessed offset.
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
        if(!std::isfinite(local.values[r][c])||std::abs(local.values[r][c]-p.flashInWeapon.values[r][c])>.001f)return false;
    return true;
}
}
inline std::optional<MuzzleBinding> BindAuthoredMuzzle(std::span<const MuzzleEnrollmentProfile> profiles,
    const AuthoredGripBinding& authored,const math::Matrix4& weapon,const math::Matrix4& flash,
    float units,std::int64_t now)noexcept {
    if(!authored.selected||!AuthoredGripCurrent(authored,authored.selected.get(),authored.equipment,authored.owner,now))return {};
    const MuzzleEnrollmentProfile* found=nullptr;
    for(const auto& p:profiles)if(muzzle_detail::Descriptor(p,authored)){
        if(found)return {};found=&p;
    }
    if(!found||!muzzle_detail::Flash(*found,weapon,flash,units))return {};
    return MuzzleBinding{found,authored};
}
inline std::optional<MuzzleBinding> BindAuthoredMuzzle(std::span<const MuzzleEnrollmentProfile> profiles,
    const AuthoredSupportBinding& support,const math::Matrix4& weapon,const math::Matrix4& flash,
    float units,std::int64_t now)noexcept {
    if(!support.selected||!AuthoredSupportCurrent(support,support.selected.get(),support.equipment,support.owner,now))return {};
    const MuzzleEnrollmentProfile* found=nullptr;
    for(const auto& p:profiles)if(muzzle_detail::Descriptor(p,support)){
        if(found)return {};found=&p;
    }
    if(!found||!muzzle_detail::Flash(*found,weapon,flash,units))return {};
    return MuzzleBinding{found,{},support};
}
inline const SelectedMeshesSnapshot* MuzzleBindingSelected(const MuzzleBinding& b)noexcept {
    return b.support?b.support->selected.get():b.authored.selected.get();
}
inline bool MuzzleBindingCurrent(const MuzzleBinding& b,const SelectedMeshesSnapshot* current,
    const WeaponEquipmentIdentity& equipment,const AuthoredGripOwner& owner,std::int64_t now)noexcept {
    if(b.support){const auto& s=*b.support;
        return b.profile&&current&&!b.authored.profile&&muzzle_detail::Descriptor(*b.profile,s)&&
            AuthoredSupportCurrent(s,current,equipment,owner,now)&&muzzle_detail::Configuration(*b.profile,*current)&&
            current->configurationPath==s.selected->configurationPath&&
            current->configurationPathPointer==s.selected->configurationPathPointer;
    }
    return b.profile&&current&&muzzle_detail::Descriptor(*b.profile,b.authored)&&
        AuthoredGripCurrent(b.authored,current,equipment,owner,now)&&muzzle_detail::Configuration(*b.profile,*current)&&
        current->configurationPath==b.authored.selected->configurationPath&&
        current->configurationPathPointer==b.authored.selected->configurationPathPointer;
}
inline bool SameMuzzleSource(const MuzzleBinding& a,const MuzzleBinding& b)noexcept {
    if(a.support||b.support){
        if(!a.profile||a.profile!=b.profile||!a.support||!b.support||a.authored.profile||b.authored.profile)return false;
        const auto& x=*a.support;const auto& y=*b.support;
        return x.profile==y.profile&&x.selected&&y.selected&&x.equipment==y.equipment&&
            x.owner.soldier==y.owner.soldier&&x.owner.weak==y.owner.weak&&
            x.owner.actorGeneration==y.owner.actorGeneration&&x.owner.equipmentGeneration==y.owner.equipmentGeneration&&
            x.owner.space==y.owner.space&&x.selected->owner==y.selected->owner&&
            x.selected->configurationPath==y.selected->configurationPath&&
            x.selected->configurationPathPointer==y.selected->configurationPathPointer&&
            x.selected->soleConfiguredArray==y.selected->soleConfiguredArray&&x.selected->states==y.selected->states;
    }
    if(!a.profile||a.profile!=b.profile||!a.authored.selected||!b.authored.selected)return false;
    const auto& x=a.authored;const auto& y=b.authored;
    return x.equipment==y.equipment&&x.owner.soldier==y.owner.soldier&&x.owner.weak==y.owner.weak&&
        x.owner.actorGeneration==y.owner.actorGeneration&&x.owner.equipmentGeneration==y.owner.equipmentGeneration&&
        x.owner.space==y.owner.space&&x.selected->owner==y.selected->owner&&
        x.selected->configurationPath==y.selected->configurationPath&&
        x.selected->configurationPathPointer==y.selected->configurationPathPointer&&
        x.selected->soleConfiguredArray==y.selected->soleConfiguredArray&&x.selected->states==y.selected->states;
}
inline std::int64_t MuzzleBindingDeadlineNs(const MuzzleBinding& binding)noexcept {
    if(binding.support){const auto& s=*binding.support;return s.selected?std::min(s.owner.inputDeadlineNs,s.selected->deadlineNs):0;}
    return binding.authored.selected?std::min(binding.authored.owner.inputDeadlineNs,binding.authored.selected->deadlineNs):0;
}
inline bool MuzzleBindingContinues(const MuzzleBinding& captured,const MuzzleBinding& live,std::int64_t now)noexcept {
    if(live.support)return SameMuzzleSource(captured,live)&&MuzzleBindingCurrent(captured,live.support->selected.get(),
        live.support->equipment,live.support->owner,now);
    return SameMuzzleSource(captured,live)&&MuzzleBindingCurrent(captured,live.authored.selected.get(),
        live.authored.equipment,live.authored.owner,now);
}

// The existing native normal-shot layout, read twice without calling the game.
// False/zero values are accepted only after successful reads.
struct NativeMuzzleConfig {
    unsigned effects=0,firingData=0,function=0,config=0,muzzleExplosion=0,projectileData=0;
    std::array<float,3> speed{},direction{},position{};
    float inheritedSpeed=0;
    unsigned bulletsPerShell=0,bulletsPerShot=0;
    std::array<unsigned char,3> cameraFlags{}; // active, force, relative
    bool operator==(const NativeMuzzleConfig&)const=default;
};
struct MuzzleReadMemory {void* context=nullptr;bool(*read)(void*,unsigned,void*,std::size_t)=nullptr;};
inline std::optional<NativeMuzzleConfig> ReadNativeMuzzleConfig(const MuzzleReadMemory& m,
    unsigned effects,unsigned config)noexcept {
    if(!m.read)return {};
    const auto read=[&](unsigned at,void* out,std::size_t n){return at>=0x10000&&std::uint64_t(at)+n<=UINT32_MAX&&m.read(m.context,at,out,n);};
    const auto sample=[&]()->std::optional<NativeMuzzleConfig>{
        NativeMuzzleConfig s;s.effects=effects;s.config=config;
        if(effects<0x10000||effects>UINT32_MAX-0x14||!read(effects+0x10,&s.firingData,4)||
           s.firingData<0x10000||s.firingData>UINT32_MAX-0x44||!read(s.firingData+0x40,&s.function,4)||
           s.function<0x10000||s.function>UINT32_MAX-0xeb||config!=s.function+0xa0||
           !read(config,s.speed.data(),12)||!read(config+0x10,s.direction.data(),12)||!read(config+0x20,s.position.data(),12)||
           !read(config+0x34,&s.bulletsPerShell,4)||!read(config+0x38,&s.bulletsPerShot,4)||
           !read(config+0x3c,&s.inheritedSpeed,4)||!read(config+0x40,&s.muzzleExplosion,4)||
           !read(config+0x44,&s.projectileData,4)||!read(config+0x48,s.cameraFlags.data(),3))return {};
        for(const auto* values:{&s.speed,&s.direction,&s.position})for(float v:*values)if(!std::isfinite(v))return {};
        if(!std::isfinite(s.inheritedSpeed)||std::any_of(s.cameraFlags.begin(),s.cameraFlags.end(),[](auto v){return v>1;}))return {};
        return s;
    };
    const auto a=sample(),b=sample();return a&&b&&*a==*b?a:std::nullopt;
}
inline bool OrdinaryMuzzleConfig(const NativeMuzzleConfig& c)noexcept {
    // ForceSpawnToCamera can overwrite the composed origin downstream. Active
    // alone is nested inside that branch and is allowed for the reviewed Nambu.
    return !c.cameraFlags[1]&&!c.cameraFlags[2]&&!c.muzzleExplosion;
}
inline bool MatchesMuzzleEnrollment(const NativeMuzzleConfig& c,const MuzzleBinding& b)noexcept {
    if(!b.profile||!OrdinaryMuzzleConfig(c)||
        (b.support?b.authored.profile||!muzzle_detail::Descriptor(*b.profile,*b.support):!muzzle_detail::Descriptor(*b.profile,b.authored)))return false;
    const auto& p=*b.profile;
    return c.speed==p.initialSpeed&&c.direction==p.initialDirection&&c.position==p.initialPosition&&
        c.inheritedSpeed==p.inheritedWeaponSpeed&&c.bulletsPerShell==p.bulletsPerShell&&
        c.bulletsPerShot==p.bulletsPerShot&&c.cameraFlags[0]==unsigned(p.activeForceSpawnToCamera)&&c.projectileData>=0x10000;
}
// Keep event identity separate from the original tracking lease. A new frame
// cannot renew the lease; a configuration/equip replacement cannot reuse history.
struct MuzzleEventSource {
    WeaponEquipmentIdentity equipment{};
    const MuzzleEnrollmentProfile* enrollment=nullptr;
    unsigned firingData=0,configuration=0;
    std::uint64_t equipmentGeneration=0;
    bool operator==(const MuzzleEventSource&)const=default;
};
inline std::optional<interaction::FiringPoseSample> ResolveMuzzleEvent(interaction::FiringPoseHistory& history,
    std::optional<MuzzleEventSource>& previous,const MuzzleEventSource& source,
    const interaction::FiringPoseKey& key,const interaction::FiringPoseSample& sample,std::int64_t now)noexcept {
    if(!source.equipment.weapon||!source.equipment.data||!source.firingData||!source.configuration||
       (source.enrollment&&!source.equipmentGeneration))return {};
    if(!previous||*previous!=source){history.Reset();previous=source;}
    return history.Resolve(key,sample,now);
}
} // namespace fvr::bc2
