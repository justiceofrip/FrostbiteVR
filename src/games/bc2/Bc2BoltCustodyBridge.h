#pragma once
#include "Bc2BoltTrackedMapping.h"
namespace fvr::bc2 {
// Original renderer geometry and current input remain separate. This builder
// supplies intentions only; TransferGunCustody validates original history,
// release barriers, both old tokens and both destinations atomically.
inline std::optional<interaction::HandGunCustodyTransfer> BuildBoltCustodyTransfer(
    const interaction::HandInteractionSample& current,interaction::HandInteractionKey item,
    const Bc2BoltControllerContact& raw,const std::array<math::Matrix4,2>& originalGrips,
    const interaction::HandInteraction& hands,bool enter,bool returnGun,std::uint64_t& intent,bool releaseDepartingGun=false)noexcept {
    using namespace interaction;
    if(enter==returnGun||!raw.raw.valid||!raw.mappingValid||raw.raw.input.owner!=current.owner||
       raw.raw.input.sequence>current.sequence||!weapon_cycle_detail::Window(raw.raw.input.observedNs,raw.raw.input.deadlineNs,current.nowNs)||
       current.released[0]||(current.released[1]&&!(enter&&releaseDepartingGun))||
       raw.raw.input.released[0]||(raw.raw.input.released[1]&&!enter)||
       (releaseDepartingGun&&(!enter||!current.released[1]))||
       !current.tracked[0]||!current.tracked[1]||!current.focused||intent>UINT64_MAX-2)return {};
    const auto inverse=InverseRigid(raw.weaponWorldMeters);if(!inverse)return {};
    for(unsigned n=0;n<2;++n){
        if(enter&&n==1)continue;
        const auto local=Multiply(raw.rawWristWorldMeters[n],*inverse);
        if(!feed_mechanism_detail::Pose(local)||!feed_mechanism_detail::Pose(originalGrips[n]))return {};
        float squared=0;for(unsigned axis=0;axis<3;++axis){const auto d=local.values[3][axis]-originalGrips[n].values[3][axis];squared+=d*d;}
        const auto distance=std::sqrt(squared);
        if(!std::isfinite(distance)||distance>(n==0?.16f:.025f))return {};
        if(n==1){float trace=0;for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)trace+=local.values[a][b]*originalGrips[n].values[a][b];
            if(std::acos(std::clamp((trace-1)*.5f,-1.f,1.f))>.10f)return {};}
    }
    const auto left=hands.Current(InteractionHand::Left),right=hands.Current(InteractionHand::Right);
    HandGunCustodyTransfer t;
    const auto target=[&](InteractionHand hand,HandClaimKind kind,HandInteractionKey contact){
        return HandCustodyTarget{{current.owner,hand,kind,item,{contact,raw.raw.input.sequence,raw.raw.input.deadlineNs,true},++intent,0},raw.raw.input};
    };
    if(enter){
        if(!right||!left||right->token.kind!=HandClaimKind::GunHold||left->token.kind!=HandClaimKind::WeaponSupport||
           left->token.prerequisiteClaim!=right->token.id||right->token.item!=item||left->token.item!=item)return {};
        t.gun=right->token;t.companion=left->token;t.releaseDepartingGun=releaseDepartingGun;
        t.nextGun=target(InteractionHand::Left,HandClaimKind::GunHold,left->token.contact);
    }else{
        if(!left||right||left->token.kind!=HandClaimKind::GunHold||left->token.item!=item)return {};
        t.gun=left->token;t.nextGun=target(InteractionHand::Right,HandClaimKind::GunHold,{1,item.generation});
        t.nextCompanion=target(InteractionHand::Left,HandClaimKind::WeaponSupport,{2,item.id});
    }
    return t;
}
}
