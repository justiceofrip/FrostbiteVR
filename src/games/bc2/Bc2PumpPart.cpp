#include "Bc2PumpPart.h"
#include "Bc2SightContact.h"
#include "Bc2PumpFixedParts.h"
namespace fvr::bc2 {
using namespace interaction;
namespace bc2_pump_detail {
std::optional<std::vector<BoneWrite>> FixedAssembly(const RigSnapshot& rig,const math::Matrix4& placed,float units,
    const Bc2PumpGeometry& geometry,std::span<const Bc2PumpFixedPart> parts){
    if(!PumpGeometryValid(geometry)||parts.size()>64||rig.names.size()!=rig.parents.size()||rig.names.size()>1024||rig.weaponBone>=rig.names.size()||
       rig.names[rig.weaponBone]!=geometry.root||!InverseRigid(placed)||!std::isfinite(units)||units<=0||units>1000)return {};
    std::vector<BoneWrite> out;
    for(const auto& part:parts){
        if(part.name==geometry.part||part.name==geometry.root||!reload_insertion_detail::Rigid(part.inWeapon))return {};
        if(std::count_if(parts.begin(),parts.end(),[&](const auto& p){return p.name==part.name;})!=1)return {};
        const auto at=std::find(rig.names.begin(),rig.names.end(),part.name);
        if(at==rig.names.end()||std::find(at+1,rig.names.end(),part.name)!=rig.names.end())return {};
        const auto index=unsigned(at-rig.names.begin());const auto parent=rig.parents[index];
        if(parent<0||unsigned(parent)>=rig.names.size()||rig.names[parent]!=part.parent||
           std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),index)!=rig.nativeHiddenLeaves.end())return {};
        auto ancestor=parent;unsigned steps=0;
        while(ancestor!=int(rig.weaponBone)){
            if(ancestor<0||unsigned(ancestor)>=rig.parents.size()||++steps>rig.parents.size())return {};
            if(rig.names[ancestor]==geometry.part)return {};
            ancestor=rig.parents[ancestor];
        }
        auto local=part.inWeapon;for(unsigned axis=0;axis<3;++axis)local.values[3][axis]*=units;
        out.push_back({index,Multiply(local,placed)});
    }
    return out;
}
std::optional<std::vector<BoneWrite>> FixedAssembly(const RigSnapshot& rig,const math::Matrix4& placed,float units){
    return FixedAssembly(rig,placed,units,SpasPumpGeometry,SpasFixedParts);
}
std::optional<Bc2PumpPartBinding> DerivePart(const RigSnapshot& rig,const Bc2PumpGeometry& geometry){
    const auto n=rig.names.size();if(!PumpGeometryValid(geometry)||!n||n>1024||rig.parents.size()!=n||rig.inverseBind.size()!=n)return {};
    const auto named=[&](std::string_view name)->std::optional<unsigned>{const auto i=std::find(rig.names.begin(),rig.names.end(),name);
        if(i==rig.names.end()||std::find(i+1,rig.names.end(),name)!=rig.names.end())return {};return unsigned(i-rig.names.begin());};
    const auto weapon=named(geometry.root),part=named(geometry.part);
    if(!weapon||!part||rig.weaponBone!=*weapon||rig.parents[*part]!=int(*weapon)||
        std::find(rig.parents.begin(),rig.parents.end(),int(*part))!=rig.parents.end())return {};
    for(unsigned index=0;index<n;++index){int at=int(index);unsigned count=0;
        while(at!=-1){if(at<0||unsigned(at)>=n||++count>n)return {};at=rig.parents[at];}}
    const auto fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
    if(!fingerprint)return {};return Bc2PumpPartBinding{fingerprint,*weapon,*part};
}
std::optional<Bc2PumpPartBinding> DerivePart(const RigSnapshot& rig){return DerivePart(rig,SpasPumpGeometry);}
}
std::optional<Bc2PumpPartBinding> BindSpasPumpPart(const RigSnapshot& rig,std::string_view asset,std::string_view mesh){
    if(!BindBc2ReloadPresentation(rig,asset,mesh))return {};return bc2_pump_detail::DerivePart(rig);
}
template<class Authority> std::optional<RigPosePlan> BuildPumpPartImpl(const RigSnapshot& rig,const Bc2PumpPartBinding& binding,
    const Bc2PumpGeometry& geometry,const Bc2PumpPartSourceT<Authority>& source,const Authority& current,const HandInteractionSample& input,const HandClaim& mechanism,const HandClaim& gun,
    const math::Matrix4& weapon,float units,float travel){
    using namespace weapon_cycle_detail;
    const auto derived=bc2_pump_detail::DerivePart(rig,geometry);
    if(!derived||derived->fingerprint!=binding.fingerprint||derived->weapon!=binding.weapon||derived->part!=binding.part||
        !source.nativeBoundaryVerified||source.rig!=rig.identity||
        source.lease.owner.actor!=((std::uint64_t(rig.identity.weak)<<32)|rig.identity.soldier)||
        !Lease(source.lease,input.nowNs)||!Lease(current,input.nowNs)||
        !Same(source.lease,current)||current.sequence<source.lease.sequence||current.observedNs<source.lease.observedNs||
        (current.sequence==source.lease.sequence&&(current.observedNs!=source.lease.observedNs||current.deadlineNs!=source.lease.deadlineNs))||
        input.owner!=source.lease.owner||!input.focused||!input.tracked[0]||!input.tracked[1]||!input.sequence||
        !Window(input.observedNs,input.deadlineNs,input.nowNs)||source.inputSequence>input.sequence||!source.inputSequence||
        !Window(source.observedNs,source.deadlineNs,input.nowNs)||source.deadlineNs>source.lease.deadlineNs||
        source.mechanism.owner!=input.owner||source.gun.owner!=input.owner||!source.mechanism.id||!source.gun.id||
        source.mechanism.kind!=HandClaimKind::Mechanism||source.mechanism.hand!=InteractionHand::Left||
        source.gun.kind!=HandClaimKind::GunHold||source.gun.hand!=InteractionHand::Right||
        source.mechanism.item!=source.lease.item||source.gun.item!=source.lease.item||source.mechanism.contact!=source.lease.mechanism||source.mechanism.prerequisiteClaim!=source.gun.id||
        mechanism.token!=source.mechanism||gun.token!=source.gun||mechanism.inputSequence<source.inputSequence||mechanism.inputSequence>input.sequence||gun.inputSequence!=input.sequence||
        mechanism.deadlineNs<=input.nowNs||gun.deadlineNs<=input.nowNs||mechanism.deadlineNs>input.deadlineNs||gun.deadlineNs>input.deadlineNs||
        !reload_insertion_detail::Rigid(source.closedPartFromWeapon)||!reload_insertion_detail::Rigid(weapon)||
        (source.rearDirection!=1&&source.rearDirection!=-1)||
        !std::isfinite(units)||units<=0||units>1000||!std::isfinite(travel)||travel<0||travel>geometry.stroke)return {};
    auto local=source.closedPartFromWeapon;
    // Translate the immutable native boundary on the explicitly measured axis.
    // Source animation cannot add another stroke or alter lateral placement.
    for(unsigned k=0;k<3;++k){local.values[3][k]+=geometry.axis[k]*source.rearDirection*travel;local.values[3][k]*=units;}
    const std::array<BoneWrite,1> writes{{{binding.part,Multiply(local,weapon)}}};
    return BuildRigPosePlan(rig,writes);
}
template<class Authority> std::optional<RigPosePlan> BuildPumpCyclePartImpl(const RigSnapshot& rig,const Bc2PumpPartBinding& binding,
    const Bc2PumpGeometry& geometry,const Bc2PumpPartSourceT<Authority>& source,const WeaponCycleProfile& profile,const PhysicalWeaponCycleTargetT<Authority>& target,
    const Authority& current,const HandInteractionSample& input,const HandClaim& mechanism,const HandClaim& gun,
    const math::Matrix4& weapon,float units){
    if(!PumpGeometryValid(geometry)||!weapon_cycle_detail::Profile(profile)||profile.family!=WeaponCycleFamily::Pump||
       profile.id!=target.profile||profile.revision!=target.revision||
       std::abs(profile.stroke-geometry.stroke)>1e-6f||
       profile.axis[0]!=geometry.axis[0]*source.rearDirection||profile.axis[1]!=geometry.axis[1]*source.rearDirection||profile.axis[2]!=geometry.axis[2]*source.rearDirection||
       !CurrentPhysicalWeaponCycleTarget(target,current,input,mechanism,gun)||
       source.lease!=target.lease||source.mechanism!=target.mechanism||source.gun!=target.gun||
       source.inputSequence!=target.inputSequence||source.observedNs!=target.observedNs||source.deadlineNs!=target.deadlineNs||
       target.travel>profile.stroke||std::abs(target.rotation)>profile.rotationTolerance)return {};
    const auto expected=weapon_cycle_detail::Target(profile,target.travel,target.rotation);
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)
        if(std::abs(expected.values[row][column]-target.contact.values[row][column])>1e-5f)return {};
    return BuildPumpPartImpl(rig,binding,geometry,source,current,input,mechanism,gun,weapon,units,target.travel);
}

std::optional<RigPosePlan> BuildSpasPumpPart(const RigSnapshot& rig,const Bc2PumpPartBinding& binding,
    const Bc2PumpPartSource& source,const WeaponCycleLease& current,const HandInteractionSample& input,
    const HandClaim& mechanism,const HandClaim& gun,const math::Matrix4& weapon,float units,float travel){
    return BuildPumpPartImpl(rig,binding,SpasPumpGeometry,source,current,input,mechanism,gun,weapon,units,travel);
}
std::optional<RigPosePlan> BuildSpasPumpCyclePart(const RigSnapshot& rig,const Bc2PumpPartBinding& binding,
    const Bc2PumpPartSource& source,const WeaponCycleProfile& profile,const PhysicalWeaponCycleTarget& target,
    const WeaponCycleLease& current,const HandInteractionSample& input,const HandClaim& mechanism,const HandClaim& gun,
    const math::Matrix4& weapon,float units){return BuildPumpCyclePartImpl(rig,binding,SpasPumpGeometry,source,profile,target,current,input,mechanism,gun,weapon,units);}
std::optional<RigPosePlan> BuildSpasPumpRecoveryPart(const RigSnapshot& rig,const Bc2PumpPartBinding& binding,
    const Bc2PumpRecoveryPartSource& source,const WeaponCycleProfile& profile,const PhysicalWeaponCycleIdleDebtTarget& target,
    const WeaponCycleIdleDebtLease& current,const HandInteractionSample& input,const HandClaim& mechanism,const HandClaim& gun,
    const math::Matrix4& weapon,float units){return BuildPumpCyclePartImpl(rig,binding,SpasPumpGeometry,source,profile,target,current,input,mechanism,gun,weapon,units);}
std::optional<RigPosePlan> BuildPumpCyclePart(const RigSnapshot& rig,const Bc2PumpPartBinding& binding,
    const Bc2PumpGeometry& geometry,const Bc2PumpPartSource& source,const WeaponCycleProfile& profile,const PhysicalWeaponCycleTarget& target,
    const WeaponCycleLease& current,const HandInteractionSample& input,const HandClaim& mechanism,const HandClaim& gun,
    const math::Matrix4& weapon,float units){return BuildPumpCyclePartImpl(rig,binding,geometry,source,profile,target,current,input,mechanism,gun,weapon,units);}
std::optional<RigPosePlan> BuildPumpRecoveryPart(const RigSnapshot& rig,const Bc2PumpPartBinding& binding,
    const Bc2PumpGeometry& geometry,const Bc2PumpRecoveryPartSource& source,const WeaponCycleProfile& profile,const PhysicalWeaponCycleIdleDebtTarget& target,
    const WeaponCycleIdleDebtLease& current,const HandInteractionSample& input,const HandClaim& mechanism,const HandClaim& gun,
    const math::Matrix4& weapon,float units){return BuildPumpCyclePartImpl(rig,binding,geometry,source,profile,target,current,input,mechanism,gun,weapon,units);}
}
