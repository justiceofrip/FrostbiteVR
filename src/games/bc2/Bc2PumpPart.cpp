#include "Bc2PumpPart.h"
#include "Bc2SightContact.h"
namespace fvr::bc2 {
using namespace interaction;
namespace bc2_pump_detail {
std::optional<Bc2PumpPartBinding> DerivePart(const RigSnapshot& rig){
    const auto n=rig.names.size();if(!n||n>1024||rig.parents.size()!=n||rig.inverseBind.size()!=n)return {};
    const auto named=[&](std::string_view name)->std::optional<unsigned>{const auto i=std::find(rig.names.begin(),rig.names.end(),name);
        if(i==rig.names.end()||std::find(i+1,rig.names.end(),name)!=rig.names.end())return {};return unsigned(i-rig.names.begin());};
    const auto weapon=named("jntWpn_1"),part=named(SpasForeEndBone);
    if(!weapon||!part||rig.weaponBone!=*weapon||rig.parents[*part]!=int(*weapon)||
        std::find(rig.parents.begin(),rig.parents.end(),int(*part))!=rig.parents.end())return {};
    for(unsigned index=0;index<n;++index){int at=int(index);unsigned count=0;
        while(at!=-1){if(at<0||unsigned(at)>=n||++count>n)return {};at=rig.parents[at];}}
    const auto fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
    if(!fingerprint)return {};return Bc2PumpPartBinding{fingerprint,*weapon,*part};
}
}
std::optional<Bc2PumpPartBinding> BindSpasPumpPart(const RigSnapshot& rig,std::string_view asset,std::string_view mesh){
    if(!BindBc2ReloadPresentation(rig,asset,mesh))return {};return bc2_pump_detail::DerivePart(rig);
}
std::optional<RigPosePlan> BuildSpasPumpPart(const RigSnapshot& rig,const Bc2PumpPartBinding& binding,
    const Bc2PumpPartSource& source,const WeaponCycleLease& current,const HandInteractionSample& input,const HandClaim& mechanism,const HandClaim& gun,
    const math::Matrix4& weapon,float units,float travel){
    using namespace weapon_cycle_detail;
    const auto derived=bc2_pump_detail::DerivePart(rig);
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
        mechanism.token!=source.mechanism||gun.token!=source.gun||mechanism.inputSequence!=input.sequence||gun.inputSequence!=input.sequence||
        mechanism.deadlineNs<=input.nowNs||gun.deadlineNs<=input.nowNs||mechanism.deadlineNs>input.deadlineNs||gun.deadlineNs>input.deadlineNs||
        !reload_insertion_detail::Rigid(source.closedPartFromWeapon)||!reload_insertion_detail::Rigid(weapon)||
        (source.rearDirection!=1&&source.rearDirection!=-1)||
        !std::isfinite(units)||units<=0||units>1000||!std::isfinite(travel)||travel<0||travel>SpasObservedForeEndStroke)return {};
    auto local=source.closedPartFromWeapon;
    // The observed channel translates along weapon-local Z. Its rotation and
    // lateral relation are immutable, so ordinary fire animation cannot add a
    // second offset to this private fore-end target.
    local.values[3][2]+=source.rearDirection*travel;for(unsigned k=0;k<3;++k)local.values[3][k]*=units;
    const std::array<BoneWrite,1> writes{{{binding.part,Multiply(local,weapon)}}};
    return BuildRigPosePlan(rig,writes);
}
}
