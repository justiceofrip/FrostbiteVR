#pragma once
#include "Bc2SelectedKnifeRendering.h"
#include "fvr/interaction/SelectedMeleeAttack.h"
#include <cstring>

namespace fvr::bc2::selectedKnife {
// Continuity is distinct from the original admission deadline. A published
// native request may be consumed after newer packets arrive; their timestamps
// never rewrite the immutable intent or create another attack edge.
inline bool PublishedSourceContinues(const interaction::SelectedMeleeIntent& intent,
    const interaction::HandInteractionSample& source,std::uint64_t inputEpoch)noexcept {
    return source.owner==intent.identity.owner&&inputEpoch==intent.identity.inputEpoch&&
        source.focused&&source.tracked[1]&&!source.released[1]&&source.sequence>=intent.sourceSequence&&
        source.observedNs>=intent.observedNs&&source.nowNs>=source.observedNs&&source.nowNs<source.deadlineNs&&
        source.deadlineNs-source.observedNs<=150000000&&
        (source.sequence==intent.sourceSequence?
            (source.observedNs==intent.observedNs&&source.deadlineNs==intent.deadlineNs):source.observedNs>intent.observedNs);
}
// A Gather publication can precede several more Gathers before native firing
// consumes the cache. Retain that one input level, never its admission lease.
// Native readiness admits a NEW intent; client prediction may leave idle before
// the same published level reaches the server and is not another admission.
struct InputDeliveryAuthority {
    bool actionsAvailable=false,gameplayOwned=false,ownerCurrent=false,admissionCurrent=false;
};
struct InputDelivery {
    std::uint64_t publications=0;
    std::int64_t firstPublishedNs=0,lastPublishedNs=0,consumedNs=0;
    void Published(std::int64_t now)noexcept {
        if(now<=0||now<lastPublishedNs)return;
        if(!publications)firstPublishedNs=now;
        if(publications!=UINT64_MAX)++publications;
        lastPublishedNs=now;
    }
    // This acknowledges only native consumption. It never resolves the policy
    // or claims acquisition, attack return, target damage or campaign progress.
    void Consumed(std::int64_t now)noexcept {
        if(publications&&!consumedNs&&now>=firstPublishedNs)consumedNs=now;
    }
    bool CanRepeat(const interaction::SelectedMeleeIntent& intent,
        interaction::SelectedMeleePending pending,interaction::SelectedMeleeReason continuation,
        const interaction::HandInteractionSample& source,
        const interaction::HandClaimToken& hold,std::uint64_t inputEpoch,
        const InputDeliveryAuthority& authority,std::int64_t now)const noexcept {
        const bool validContinuation=continuation==interaction::SelectedMeleeReason::Pending||
            continuation==interaction::SelectedMeleeReason::Duplicate||
            continuation==interaction::SelectedMeleeReason::NativeUnavailable;
        return publications&&!consumedNs&&pending==interaction::SelectedMeleePending::Staged&&validContinuation&&
            authority.actionsAvailable&&authority.gameplayOwned&&authority.ownerCurrent&&authority.admissionCurrent&&
            hold==intent.hold&&now>=lastPublishedNs&&now>=source.nowNs&&now<intent.deadlineNs&&now<source.deadlineNs&&
            PublishedSourceContinues(intent,source,inputEpoch);
    }
};
// The local stroke and native aim intentionally use separate frames: a fixed
// tracking frame recognizes motion, while current body mapping positions it.
struct Geometry {private_knife::KnifeMotionGeometry motion{};math::Matrix4 nativeAcquisition{};};
inline std::optional<Geometry> MapGeometry(const interaction::InputFrame& input,
    const SelectedKnifeRenderReceipt& rendered,const math::Matrix4& currentBody)noexcept {
    if(!interaction::ValidInput(input)||!input.focused||!input.headValid||!input.hands[1].gripTracked||
       !interaction::InverseRigid(currentBody)||rendered.binding.grip.profile!=&SelectedKnifeGripProfiles()[0])return {};
    const auto motion=private_knife::KnifeMotionFromTrackedGrip(input,rendered.wristInGrip);
    auto relative=math::MakeRelativePose(input.referenceHead,input.hands[1].grip);
    if(!motion||!relative)return {};
    relative->position.x*=input.worldUnitsPerMeter;relative->position.y*=input.worldUnitsPerMeter;
    relative->position.z*=input.worldUnitsPerMeter;
    const auto view=math::MakeLhViewFromOpenXRPose(*relative);if(!view)return {};
    const auto grip=interaction::InverseRigid(*view);if(!grip)return {};
    auto wristInKnife=private_knife::RightHandInKnife;
    for(unsigned a=0;a<3;++a)wristInKnife.values[3][a]*=input.worldUnitsPerMeter;
    const auto inverseWrist=interaction::InverseRigid(wristInKnife);if(!inverseWrist)return {};
    const auto knife=interaction::Multiply(*inverseWrist,
        interaction::Multiply(rendered.wristInGrip,interaction::Multiply(*grip,currentBody)));
    if(!interaction::InverseRigid(knife))return {};
    // Native search577790 uses its composed third basis row as the forward
    // cone. Convert physical vectors into native coordinates directly; applying
    // S*M*S to an aim frame would reverse that row and point behind the blade.
    const auto transform=[&](math::Vec3 p,float w){std::array<float,3> out{};
        for(unsigned c=0;c<3;++c)out[c]=p.x*knife.values[0][c]+p.y*knife.values[1][c]+p.z*knife.values[2][c]+w*knife.values[3][c];
        out[2]=-out[2];return out;};
    const auto cross=[](auto a,auto b){return std::array<float,3>{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    auto tip=private_knife::BladeTipInKnife;tip.x*=input.worldUnitsPerMeter;tip.y*=input.worldUnitsPerMeter;tip.z*=input.worldUnitsPerMeter;
    const auto position=transform(tip,1),forward=transform(private_knife::BladeAxisInKnife,0),up=transform({0,1,0},0);
    const auto right=cross(up,forward),correctedUp=cross(forward,right);
    Geometry out;out.motion=*motion;
    for(unsigned c=0;c<3;++c){out.nativeAcquisition.values[0][c]=right[c];out.nativeAcquisition.values[1][c]=correctedUp[c];
        out.nativeAcquisition.values[2][c]=forward[c];out.nativeAcquisition.values[3][c]=position[c];}
    out.nativeAcquisition.values[3][3]=1;
    if(!interaction::InverseRigid(out.nativeAcquisition))return {};
    return out;
}
// Native Shot.InitialDirection/Position remain authoritative. Search577790
// builds a basis from InitialDirection (4182e0), not Euler-angle offsets.
// The exact SP_01 Knife_Equipable authored/live neutral form is +Z and zero
// position; its third row therefore preserves the measured blade direction.
// No field is modified to make a configuration fit this narrow capability.
inline bool NeutralShotOffsets(const std::array<float,3>& direction,const std::array<float,3>& position)noexcept {
    for(unsigned n=0;n<3;++n)if(!std::isfinite(direction[n])||!std::isfinite(position[n])||
        direction[n]!=(n==2?1.f:0.f)||position[n]!=0.f)return false;
    return true;
}
inline std::optional<std::array<std::byte,64>> AcquisitionCopy(std::span<const std::byte> original,
    const math::Matrix4& blade,float units)noexcept {
    if(original.size()!=64||!std::isfinite(units)||units<=0||!interaction::InverseRigid(blade))return {};
    math::Matrix4 native;std::memcpy(&native,original.data(),64);
    for(unsigned r=0;r<4;++r){for(unsigned c=0;c<3;++c)if(!std::isfinite(native.values[r][c]))return {};
        native.values[r][3]=r==3?1.f:0.f;}
    if(!interaction::InverseRigid(native)||std::hypot(native.values[3][0]-blade.values[3][0],
        native.values[3][1]-blade.values[3][1],native.values[3][2]-blade.values[3][2])>2.f*units)return {};
    std::array<std::byte,64> copy;std::memcpy(copy.data(),original.data(),64);
    for(unsigned r=0;r<4;++r)std::memcpy(copy.data()+r*16,blade.values[r].data(),12);
    return copy;
}
}
