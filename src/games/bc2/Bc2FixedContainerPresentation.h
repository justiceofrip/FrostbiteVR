#pragma once
#include "Bc2FixedContainerEquipment.h"
#include "Bc2Rig.h"
#include "fvr/graphics/BodyPropFrame.h"
#include <memory>

namespace fvr::bc2 {
struct Bc2FixedContainerRawContact {
    ReloadStateOwner nativeOwner{};RigIdentity rig{};
    const FixedContainerEquipmentProfile* equipment=nullptr;
    interaction::HandInteractionSample input{};
    math::Matrix4 weaponFromWrist{},weaponWorldMeters{};
    bool valid=false,grip=false,gripPressed=false,pouchContact=false;
};
struct Bc2FixedContainerTracking {
    bool enabled=false,blocksFire=false,carried=false,pending=false,grip=false,gripPressed=false,pouchContact=false;
    FixedContainerBodyAmmo source{};
    interaction::HandInteractionSample input{};
    interaction::HandClaim gun{};
    std::optional<interaction::HandClaim> hand;
    interaction::FixedContainerStep step=interaction::FixedContainerStep::Open;
    interaction::FixedContainerStep targetStep=interaction::FixedContainerStep::Open;
    std::uint64_t lifetime=0,revision=0;
    std::optional<math::Matrix4> target,carriedSupply;
    // Persistent mod-owned transforms. They do not renew controller authority.
    std::array<math::Matrix4,16> parts{};unsigned partCount=0;
};
inline bool FixedContainerTrackingFresh(const Bc2FixedContainerTracking& t,std::int64_t now)noexcept {
    using namespace interaction;
    const auto fresh=[now](std::int64_t a,std::int64_t b){return a>0&&a<=now&&now<b&&b-a<=100000000;};
    const auto* e=KnownFixedContainerEquipment(t.source.equipment);const auto& a=t.source.ammunition;const auto& i=t.input;const auto& g=t.gun;
    return t.enabled&&e&&t.lifetime&&t.revision&&FixedContainerHandOwner(t.source.binding,i.owner)&&
        AmmoResourceBindingFresh(t.source.binding,t.source.binding.owner,now)&&a.context==t.source.binding.context&&
        a.coherentIdleVerified&&a.sequence&&fresh(a.observedNs,a.deadlineNs)&&a.counts.capacity==e->interaction.capacity&&
        a.counts.loaded>=0&&a.counts.loaded<=a.counts.capacity&&a.counts.reserve>=0&&a.counts.reserve<=1000000&&
        i.sequence&&i.focused&&i.tracked[0]&&i.tracked[1]&&!i.released[1]&&fresh(i.observedNs,i.deadlineNs)&&
        g.token.id&&g.token.owner==i.owner&&g.token.kind==HandClaimKind::GunHold&&g.token.hand==InteractionHand::Right&&
        g.token.item==HandInteractionKey{a.context.resource.weapon,i.owner.equipGeneration}&&g.inputSequence==i.sequence&&
        g.deadlineNs>now&&g.deadlineNs<=i.deadlineNs&&t.partCount==e->parts.size()&&t.partCount<=t.parts.size();
}
bool FixedContainerTrackingRetained(const Bc2FixedContainerTracking&,const Bc2FixedContainerTracking&,std::int64_t now)noexcept;
bool FixedContainerRigMatches(const FixedContainerEquipmentProfile&,const RigSnapshot&)noexcept;
Bc2FixedContainerRawContact BuildFixedContainerRawContact(const Bc2FixedContainerTracking&,const RigSnapshot&,
    const math::Matrix4& rawWrist,const math::Matrix4& placedWeapon,float units,std::int64_t now)noexcept;
struct FixedContainerRenderedPart {std::string_view bone;math::Matrix4 world{};unsigned ordinal=0;};
struct Bc2FixedContainerPresentation {
    bool valid=false;
    std::optional<math::Matrix4> wrist;
    const interaction::AuthoredHandShape* handShape=nullptr;
    std::vector<interaction::BoneWrite> parts;
    // Native supply geometry is concealed; independent cached parts preserve
    // both retained loaded rounds and the separately carried compound supply.
    std::vector<std::uint32_t> hiddenSupplyBones;
    std::vector<FixedContainerRenderedPart> supply;
};
Bc2FixedContainerPresentation BuildFixedContainerPresentation(const Bc2FixedContainerTracking&,const RigSnapshot&,
    const math::Matrix4& placedWeapon,float units,std::int64_t now)noexcept;
std::vector<graphics::BodyPropInstance> FixedContainerHostInstances(const Bc2FixedContainerTracking& before,
    const Bc2FixedContainerTracking& current,const RigSnapshot&,const math::Matrix4& placedWeapon,float units,std::int64_t now);
struct FixedContainerRenderSource {
    std::shared_ptr<const Bc2FixedContainerTracking> tracking;
    std::vector<graphics::BodyPropInstance> instances;
};
std::optional<FixedContainerRenderSource> BuildFixedContainerRenderSource(const Bc2FixedContainerTracking&,
    const RigSnapshot&,const math::Matrix4& placedWeapon,float units,std::int64_t now);
std::vector<graphics::BodyPropInstance> RetainFixedContainerHostInstances(const FixedContainerRenderSource&,
    const FixedContainerRenderSource&,std::int64_t now);
}
