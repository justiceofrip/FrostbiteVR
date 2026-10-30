#include "Bc2BoltPart.h"
#include "Bc2SightContact.h"
namespace fvr::bc2 {
using namespace interaction;
namespace {
bool Base(const Bc2BoltTracking& t,const RigSnapshot& r,std::int64_t now)noexcept {
    return t.enabled&&t.calibration&&BoltCalibrationValid(*t.calibration)&&t.input.owner.actor==
        ((std::uint64_t(t.nativeOwner.weak)<<32)|t.nativeOwner.soldier)&&
        t.input.owner.actorGeneration==t.nativeOwner.actorGeneration&&t.input.owner.space==t.nativeOwner.space&&
        t.nativeOwner.soldier==r.identity.soldier&&t.nativeOwner.weak==r.identity.weak&&
        t.input.focused&&t.input.tracked[0]&&t.input.tracked[1]&&t.input.sequence&&
        weapon_cycle_detail::Window(t.input.observedNs,t.input.deadlineNs,now);
}
bool Units(float units)noexcept{return std::isfinite(units)&&units>0&&units<=1000;}
}
std::optional<Bc2BoltPartBinding> DeriveBoltPart(const RigSnapshot& rig,std::string_view partName){
    const auto n=rig.names.size();if(!n||n>1024||rig.parents.size()!=n||rig.inverseBind.size()!=n||partName.empty())return {};
    const auto named=[&](std::string_view name)->std::optional<unsigned>{const auto i=std::find(rig.names.begin(),rig.names.end(),name);
        if(i==rig.names.end()||std::find(i+1,rig.names.end(),name)!=rig.names.end())return {};return unsigned(i-rig.names.begin());};
    const auto weapon=named("jntWpn_1"),part=named(partName);
    if(!weapon||!part||*weapon==*part||rig.weaponBone!=*weapon||rig.parents[*part]!=int(*weapon)||
        std::find(rig.parents.begin(),rig.parents.end(),int(*part))!=rig.parents.end()||
        std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),*part)!=rig.nativeHiddenLeaves.end())return {};
    for(unsigned index=0;index<n;++index){int at=int(index);unsigned count=0;
        while(at!=-1){if(at<0||unsigned(at)>=n||++count>n)return {};at=rig.parents[at];}}
    const auto fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
    if(!fingerprint)return {};return Bc2BoltPartBinding{fingerprint,*weapon,*part};
}
Bc2BoltRawContact BuildBoltRawContact(const Bc2BoltTracking& t,const RigSnapshot& rig,
    const math::Matrix4& rawWrist,const math::Matrix4& placed,float units,std::int64_t now)noexcept {
    if(!Base(t,rig,now)||!Units(units)||!feed_mechanism_detail::Pose(rawWrist))return {};
    const auto binding=DeriveBoltPart(rig,t.calibration->partName);const auto inverse=InverseRigid(placed);
    if(!binding||binding->fingerprint!=t.calibration->rigFingerprint||!inverse)return {};
    auto wrist=Multiply(rawWrist,*inverse);for(unsigned n=0;n<3;++n)wrist.values[3][n]/=units;
    if(!feed_mechanism_detail::Pose(wrist))return {};
    return {t.nativeOwner,rig.identity,binding->fingerprint,t.input,wrist,true};
}
std::optional<BoneWrite> BuildPausedBoltPart(const Bc2BoltTracking& t,const RigSnapshot& rig,
    const math::Matrix4& placed,float units,std::int64_t now)noexcept {
    if(!Base(t,rig,now)||!Units(units)||!t.gun||!t.weapon||t.target||t.recoveryTarget||
       bool(t.reloadPause)==bool(t.reloadMechanicalPose)||!feed_mechanism_detail::Pose(placed))return {};
    const auto& p=t.calibration->profile;
    const auto& pause=t.reloadPause?*t.reloadPause:t.reloadMechanicalPose->pause;
    auto input=t.input;input.nowNs=now;
    if(t.reloadPause){
        if(!t.reloadContact||t.mechanismPhase!=WeaponCyclePhase::ReloadPaused||
           (t.custody!=BoltCustodyPhase::ReloadPaused&&t.custody!=BoltCustodyPhase::Reloading&&
            t.custody!=BoltCustodyPhase::ReloadResumePending)||now>=pause.pauseDeadlineNs)return {};
    }else{
        const auto& pose=*t.reloadMechanicalPose;const auto& a=pose.authority;
        if(t.custody!=BoltCustodyPhase::Manipulating||t.mechanismPhase!=WeaponCyclePhase::AwaitingGrip||
           t.mechanism||t.held||!t.recovery||pose.gun!=t.gun->token||pose.resumedNs<pause.pausedNs||
           pose.resumedNs>=pause.pauseDeadlineNs||now<pose.resumedNs||now>=pose.cycleDeadlineNs||
           pause.cycleDeadlineNs<pause.pausedNs||pose.resumedNs>INT64_MAX-(pause.cycleDeadlineNs-pause.pausedNs)||
           pose.cycleDeadlineNs!=pose.resumedNs+(pause.cycleDeadlineNs-pause.pausedNs)||
           !weapon_cycle_detail::Same(a,*t.recovery)||!weapon_cycle_detail::Lease(*t.recovery,now)||
           !weapon_cycle_detail::AmmoRebase(a.grant.debt)||a.grant.debt.ammunitionRebase->pause!=pause.pause||
           a.owner!=pause.owner||a.item!=pause.item||a.mechanism!=pause.mechanism||a.cycle!=pause.cycle||a.shot!=pause.shot)return {};
    }
    if(pause.owner!=input.owner||pause.item!=HandInteractionKey{t.nativeOwner.weapon,input.owner.equipGeneration}||
       pause.mechanism!=HandInteractionKey{p.id,input.owner.equipGeneration}||pause.profile!=p.id||pause.revision!=p.revision||
       !pause.pause||now<pause.pausedNs||!std::isfinite(pause.travel)||!std::isfinite(pause.rotation)||pause.travel<0||pause.travel>p.stroke||
       pause.rotation<std::min(0.f,p.unlockRadians)||pause.rotation>std::max(0.f,p.unlockRadians)||
       !CurrentBoltCustodyWeaponTarget(*t.weapon,input,*t.gun))return {};
    const auto part=DeriveBoltPart(rig,t.calibration->partName);
    if(!part||part->fingerprint!=t.calibration->rigFingerprint)return {};
    auto local=weapon_cycle_detail::Target(p,pause.travel,pause.rotation);
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
        if((t.reloadContact&&std::abs(local.values[row][col]-t.reloadContact->values[row][col])>1e-5f)||
           std::abs(placed.values[row][col]-t.weapon->weaponInWorld.values[row][col]*(row==3&&col<3?units:1.f))>1e-5f)return {};
    for(unsigned n=0;n<3;++n)local.values[3][n]*=units;
    return BoneWrite{part->part,Multiply(local,placed)};
}
std::optional<Bc2BoltPresentation> BuildBoltPresentation(const Bc2BoltTracking& t,const RigSnapshot& rig,
    const math::Matrix4& placed,float units,std::int64_t now)noexcept {
    if(!Base(t,rig,now)||!Units(units)||!t.mechanism||!t.gun||!t.weapon||
        t.custody!=BoltCustodyPhase::Manipulating||!feed_mechanism_detail::Pose(placed))return {};
    const auto& p=t.calibration->profile;
    auto input=t.input;input.nowNs=now;
    const auto build=[&](const auto& target,const auto& authority)->std::optional<Bc2BoltPresentation>{
    if(!CurrentPhysicalWeaponCycleTarget(target,authority,input,*t.mechanism,*t.gun,p.hands)||
       authority.item!=HandInteractionKey{t.nativeOwner.weapon,t.input.owner.equipGeneration}||
       !CurrentBoltCustodyWeaponTarget(*t.weapon,input,*t.gun)||target.profile!=p.id||target.revision!=p.revision||
       target.travel>p.stroke||target.rotation<std::min(0.f,p.unlockRadians)||target.rotation>std::max(0.f,p.unlockRadians))return {};
    const auto binding=DeriveBoltPart(rig,t.calibration->partName);
    if(!binding||binding->fingerprint!=t.calibration->rigFingerprint)return {};
    const auto expected=weapon_cycle_detail::Target(p,target.travel,target.rotation);
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)
        if(std::abs(expected.values[row][col]-target.contact.values[row][col])>1e-5f||
           std::abs(placed.values[row][col]-t.weapon->weaponInWorld.values[row][col]*(row==3&&col<3?units:1.f))>1e-5f)return {};
    auto part=target.contact,wrist=Multiply(t.calibration->wristFromPart,part);
    for(unsigned n=0;n<3;++n){part.values[3][n]*=units;wrist.values[3][n]*=units;}
    // Absolute closed-reference target; animated native poses are never added.
    return Bc2BoltPresentation{{binding->part,Multiply(part,placed)},Multiply(wrist,placed)};
    };
    if(t.target&&t.held&&!t.recoveryTarget&&!t.recovery)return build(*t.target,*t.held);
    if(t.recoveryTarget&&t.recovery&&!t.target&&!t.held)return build(*t.recoveryTarget,*t.recovery);
    return {};
}
std::optional<RigPosePlan> BuildBoltPartPlan(const Bc2BoltTracking& t,const RigSnapshot& rig,
    const math::Matrix4& placed,float units,std::int64_t now){
    const auto presentation=BuildBoltPresentation(t,rig,placed,units,now);if(!presentation)return {};
    return BuildRigPosePlan(rig,std::span<const BoneWrite>(&presentation->part,1));
}
}
