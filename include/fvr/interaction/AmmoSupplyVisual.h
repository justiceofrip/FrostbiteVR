#pragma once
#include "fvr/interaction/BodyAnchors.h"

namespace fvr::interaction {
// One visual pickup handle for an existing pool, never an item or ammunition
// grant. A magazine-shaped handle does not imply a count of stored magazines.
enum class SupplyAnchorFrame : std::uint8_t {HeadYaw, RecenteredBody};
struct AmmoSupplyVisualSample {
    bool enabled=false,occupied=false;
    AmmoSupplySource source{};
    HandInteractionSample input{};
    HandClaim gun{};
    AmmoSupplyContact contact{};
    SupplyAnchorFrame frame=SupplyAnchorFrame::HeadYaw;
};
inline bool AmmoSupplyVisualFresh(const AmmoSupplyVisualSample& s,std::int64_t now)noexcept {
    const auto& i=s.input;const auto& r=s.source;const auto& id=r.identity;const auto& g=s.gun;
    const auto fresh=[&](std::int64_t a,std::int64_t b,std::int64_t limit){return a>0&&a<=now&&b>now&&b-a<=limit;};
    const auto key=[](HandInteractionKey k){return k.id&&k.generation;};
    return s.enabled&&!s.occupied&&now>0&&i.sequence&&i.focused&&i.tracked[0]&&i.tracked[1]&&!i.released[1]&&
        i.owner.actor&&i.owner.actorGeneration&&i.owner.equipGeneration&&i.owner.space&&
        fresh(i.observedNs,i.deadlineNs,150000000)&&r.verified&&r.sequence&&fresh(r.observedNs,r.deadlineNs,200000000)&&
        id.owner==i.owner&&id.trackingEpoch==i.owner.space&&key(id.weapon)&&key(id.profile)&&key(id.pool)&&
        id.weapon.generation==i.owner.equipGeneration&&r.objectUnits&&r.reserveUnits>=r.objectUnits&&
        (r.family==ReloadInsertionFamily::SingleShell||r.family==ReloadInsertionFamily::Magazine)&&
        g.token.id&&g.token.owner==i.owner&&g.token.item==id.weapon&&g.token.kind==HandClaimKind::GunHold&&
        g.token.hand==InteractionHand::Right&&g.inputSequence==i.sequence&&g.deadlineNs>now&&g.deadlineNs<=i.deadlineNs&&
        ValidBodyAnchor({1,{s.contact.centerMeters[0],s.contact.centerMeters[1],s.contact.centerMeters[2]},s.contact.radiusMeters})&&
        (s.frame==SupplyAnchorFrame::HeadYaw||s.frame==SupplyAnchorFrame::RecenteredBody);
}
inline bool AmmoSupplyVisualRetained(const AmmoSupplyVisualSample& old,const AmmoSupplyVisualSample& current,
    std::int64_t now)noexcept {
    if(!AmmoSupplyVisualFresh(old,now)||!AmmoSupplyVisualFresh(current,now))return false;
    return old.source.identity==current.source.identity&&old.gun.token==current.gun.token&&old.frame==current.frame&&
        old.contact.centerMeters==current.contact.centerMeters&&old.contact.radiusMeters==current.contact.radiusMeters&&
        old.source.family==current.source.family&&old.source.reserveUnits==current.source.reserveUnits&&
        old.source.objectUnits==current.source.objectUnits&&old.source.sequence<=current.source.sequence&&
        old.source.observedNs<=current.source.observedNs&&old.input.sequence<=current.input.sequence&&
        old.input.observedNs<=current.input.observedNs&&
        (old.input.sequence!=current.input.sequence||(old.input.observedNs==current.input.observedNs&&old.input.deadlineNs==current.input.deadlineNs))&&
        (old.source.sequence!=current.source.sequence||(old.source.observedNs==current.source.observedNs&&old.source.deadlineNs==current.source.deadlineNs));
}
// The exact inverse frame used by BodyAnchorHandPose / PhysicalReloadPouchPose.
// anchor coordinates are metres; the output follows the renderer's native scale.
inline std::optional<math::Matrix4> AmmoSupplyAnchorWorld(const InputFrame& input,const math::Matrix4& eyeBase,
    const AmmoSupplyVisualSample& s)noexcept {
    if(!ValidInput(input)||!input.focused||!input.headValid||input.generation!=s.input.sequence||
        input.spaceGeneration!=s.input.owner.space||!InverseRigid(eyeBase))return {};
    auto reference=UprightReference(s.frame==SupplyAnchorFrame::RecenteredBody?input.referenceHead:input.head);
    if(!reference)return {};reference->position=input.head.position;
    const auto relative=math::MakeRelativePose(input.referenceHead,*reference);if(!relative)return {};
    auto scaled=*relative;scaled.position.x*=input.worldUnitsPerMeter;scaled.position.y*=input.worldUnitsPerMeter;scaled.position.z*=input.worldUnitsPerMeter;
    const auto view=math::MakeLhViewFromOpenXRPose(scaled);if(!view)return {};
    const auto frame=InverseRigid(*view);if(!frame)return {};
    auto anchor=reload_insertion_detail::Identity();
    for(unsigned n=0;n<3;++n)anchor.values[3][n]=s.contact.centerMeters[n]*input.worldUnitsPerMeter;
    return Multiply(Multiply(anchor,*frame),eyeBase);
}
} // namespace fvr::interaction
