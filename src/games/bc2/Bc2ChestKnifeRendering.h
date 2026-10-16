#pragma once
#include "KnifeGeometry.h"
#include "Bc2AuthoredGrip.h"
#include "Bc2BodyInventory.h"
#include "ChestKnifeCustody.h"
#include "Bc2KnifeMotionGeometry.h"
namespace fvr::bc2 {
inline bool ChestKnifePoseCustody(const interaction::ChestKnifeCustody& policy,const interaction::HandInteraction& hands,
    const interaction::HandInteractionSample& input)noexcept {
    if(policy.NeedsInitialHeldPose())return true;
    const auto held=hands.Current(interaction::InteractionHand::Right);
    return policy.Hold()&&held&&held->token==*policy.Hold()&&held->token.kind==interaction::HandClaimKind::GunHold&&
        held->token.owner==input.owner&&held->inputSequence==input.sequence&&held->deadlineNs>input.nowNs;
}
// Display-only custody, published from the actual accepted native selection and
// current original hand source. It grants neither attack nor native selection.
struct ChestKnifeRenderSource {
    ReloadStateOwner owner{};interaction::HandInteractionSample input{};
    interaction::BodyItemKey knife{};std::uint64_t bodyOperation=0;
};
struct ChestKnifeRenderReceipt {
    ChestKnifeRenderSource source{};std::uint64_t rigFingerprint=0;
    math::Matrix4 wristInGrip{}; // Actual retained TrackedRig calibration, no pose lease.
};
inline bool ChestKnifeRenderCurrent(const ChestKnifeRenderSource& s,const ReloadStateOwner& owner,
    const interaction::HandInteractionSample& input,std::int64_t now)noexcept {
    return s.bodyOperation&&s.knife.id==owner.weapon&&s.knife.generation&&s.owner==owner&&s.input.owner==input.owner&&
        input.owner.actor==((std::uint64_t(owner.weak)<<32)|owner.soldier)&&input.owner.actorGeneration==owner.actorGeneration&&
        input.owner.space==owner.space&&s.input.focused&&s.input.tracked[1]&&!s.input.released[1]&&
        input.observedNs>0&&now>=input.observedNs&&input.deadlineNs-input.observedNs<=100000000&&
        s.input.sequence&&s.input.sequence<=input.sequence&&s.input.observedNs>0&&s.input.observedNs<=input.observedNs&&
        s.input.deadlineNs>now&&s.input.deadlineNs-s.input.observedNs<=100000000&&input.deadlineNs>now&&
        input.focused&&input.tracked[1]&&!input.released[1];
}
inline const std::array<AuthoredGripProfile,1>& ChestKnifeGripProfiles()noexcept {
    // Both matrices are exact saved static authored measurements. The left
    // matrix satisfies the existing binding schema; left grasp stays disabled.
    static const std::array<AuthoredGripProfile,1> profiles{{{"KNV-1",private_knife::KnifeMesh,
        private_knife::ReferenceBindingDigest,"503047dd626a723e39e3b2c325863b6b049e0ed5985bd7d811dee4eb646d04c7",private_knife::SkeletonSha256,
        0xa7f219a1426216abull,private_knife::RightHandInKnife,[] {math::Matrix4 m;m.values={{
{0.744314902066f,0.446674005868f,-0.49646516398f,0.0f},
{0.653793949468f,-0.638975081088f,0.405295345875f,0.0f},
{-0.13619397271f,-0.626253286004f,-0.767631437323f,0.0f},
{-0.253332671452f,-0.0234942306453f,0.247122105309f,1.0f}
}};return m;}(),false}}};return profiles;
}
}
