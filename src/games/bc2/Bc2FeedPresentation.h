#pragma once
#include "Bc2FeedEquipmentProfile.h"
#include "Bc2PhysicalFeed.h"
#include "Bc2Rig.h"
#include "Bc2SightContact.h"
#include "fvr/interaction/TrackedRig.h"

namespace fvr::bc2 {
// Part order is pouch, cover, then the four separately articulated belt bones.
// This binding grants private palette geometry only, never native reload state.
struct Bc2FeedPartBinding {
    RigIdentity rig{};
    std::array<std::uint32_t,6> parts{};
    std::uint64_t fingerprint=0;
};
namespace feed_presentation_detail {
// Exact measured t=0 cover frame from implementation15/geometry-evidence.json.
// The motion hinge's axes differ from the actual cover bone axes. Applying the
// hinge frame directly to bone9 would rotate the closed model by 90 degrees.
inline const auto CoverReference=bc2_feed_profile_detail::MeasuredFrame({
    1.f,5.3931e-6f,5.3928e-6f,0.f,
    -5.3928e-6f,.9999999991f,-4.31197e-5f,0.f,
    -5.3931e-6f,4.31197e-5f,.9999999991f,0.f,
    2.63e-8f,.0745346174f,-.4183450341f,1.f});
// Structural helper for deterministic fixtures. Production additionally pins
// the complete names/parent/inverse-bind fingerprint in BindBc2FeedParts.
inline std::optional<Bc2FeedPartBinding> Derive(const RigSnapshot& rig)noexcept {
    constexpr std::array<std::string_view,6> names{
        "jntWpn_6","jntWpn_9","jntWpn_12","jntWpn_13","jntWpnwpnJnt_14","jntWpn_15"};
    constexpr std::array<unsigned,6> indices{41,34,38,100,114,116};
    constexpr std::array<unsigned,6> parents{10,10,10,38,100,114};
    if(rig.identity.count!=rig.names.size()||rig.names.size()!=rig.parents.size()||
       rig.names.size()!=rig.evaluatedWorld.size()||rig.weaponBone!=10||rig.names.size()<=116||
       rig.names[10]!="jntWpn_1")return {};
    for(unsigned i=0;i<indices.size();++i){
        if(rig.names[indices[i]]!=names[i]||rig.parents[indices[i]]!=std::int32_t(parents[i])||
           std::count(rig.names.begin(),rig.names.end(),names[i])!=1||
           std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),indices[i])!=rig.nativeHiddenLeaves.end())return {};
    }
    return Bc2FeedPartBinding{rig.identity,indices,0};
}
inline math::Matrix4 ScaleTranslation(math::Matrix4 value,float units)noexcept {
    for(unsigned i=0;i<3;++i)value.values[3][i]*=units;return value;
}
inline bool Window(const interaction::HandInteractionSample& s,std::int64_t now)noexcept {
    return s.sequence&&s.observedNs>0&&s.deadlineNs>s.observedNs&&
        s.deadlineNs-s.observedNs<=100000000&&now>=s.observedNs&&now<s.deadlineNs&&
        s.focused&&s.tracked[0]&&s.tracked[1];
}
template<std::size_t N> inline std::string_view Text(const std::array<char,N>& value)noexcept {
    const auto end=std::find(value.begin(),value.end(),'\0');
    return end==value.end()?std::string_view{}:std::string_view(value.data(),std::size_t(end-value.begin()));
}
}
// Native equipment generation and HandInteraction equipment generation are
// distinct domains. The current actual hand owner validates the latter.
inline bool Bc2FeedSourceFresh(const Bc2PhysicalFeedPresentation& p,const ReloadStateOwner& owner,
    const interaction::HandInteractionSample& current,const WeaponEquipmentIdentity& equipment,
    const RigIdentity& rig,std::int64_t now)noexcept {
    using namespace feed_presentation_detail;
    return owner.player&&owner.soldier&&owner.weak&&owner.weapon&&owner.actorGeneration&&owner.equipGeneration&&owner.space&&
        p.owner==owner&&p.rig==rig&&rig.soldier==owner.soldier&&rig.weak==owner.weak&&
        p.lifetime&&p.revision&&p.rigFingerprint&&Window(p.input,now)&&Window(current,now)&&
        p.input.owner==current.owner&&p.input.sequence<=current.sequence&&p.input.observedNs<=current.observedNs&&
        current.owner.actor==((std::uint64_t(owner.weak)<<32)|owner.soldier)&&
        current.owner.actorGeneration==owner.actorGeneration&&current.owner.space==owner.space&&current.owner.equipGeneration&&
        p.weapon.id==owner.weapon&&p.weapon.generation==current.owner.equipGeneration&&equipment.weapon==owner.weapon&&
        equipment.data&&equipment.Asset()==bc2_feed_profile_detail::M60Geometry.asset&&
        interaction::feed_mechanism_detail::Supply(p.supply,true)&&!(p.carriedPartsMask&~std::uint8_t(61))&&
        interaction::feed_mechanism_detail::Pose(p.originalWeaponWorldMeters);
}
inline bool Bc2FeedMechanismClaimFresh(const Bc2PhysicalFeedPresentation& p,std::int64_t now)noexcept {
    if(!p.mechanismClaim||!p.mechanismWristWorldMeters||!feed_presentation_detail::Window(p.input,now))return false;
    const auto& claim=*p.mechanismClaim;const auto& token=claim.token;
    return token.id&&token.hand==interaction::InteractionHand::Left&&token.kind==interaction::HandClaimKind::Mechanism&&
        token.owner==p.input.owner&&token.item==p.weapon&&
        token.contact.id&&token.contact.generation==p.lifetime&&token.prerequisiteClaim&&
        claim.inputSequence==p.input.sequence&&claim.deadlineNs>now&&claim.deadlineNs<=p.input.deadlineNs&&
        !p.input.released[0]&&p.operation!=interaction::FeedPhysicalOperation::None&&
        p.targetKind!=interaction::FeedInteractionTargetKind::None&&
        interaction::feed_mechanism_detail::Pose(*p.mechanismWristWorldMeters);
}
inline bool Bc2FeedSelected(const SelectedMeshesSnapshot& s,const Bc2PhysicalFeedPresentation& p,
    const WeaponEquipmentIdentity& equipment,std::uint64_t currentSequence,std::int64_t now)noexcept {
    using namespace feed_presentation_detail;const auto& geometry=bc2_feed_profile_detail::M60Geometry;
    if(s.owner!=p.owner||!s.sequence||s.sequence>currentSequence||s.observedNs<=0||now<s.observedNs||now>=s.deadlineNs||
       s.deadlineNs<=s.observedNs||s.deadlineNs-s.observedNs>250000000||s.weaponData!=equipment.data||
       !s.configurationPathVerified||!s.configurationPathPointer||!s.inventory||!s.stateTypeInfo||!s.meshTypeInfo||
       s.stateCount!=1||!s.soleConfiguredArray||
       Text(s.weaponName)!=equipment.Asset()||
       !FindBc2M60FeedGeometry(equipment.Asset(),Text(s.configurationPath),geometry.meshPath,p.rigFingerprint))return false;
    const auto& state=s.states[0];if(!state.state||state.array!=s.soleConfiguredArray||!state.count||state.count>state.meshes.size())return false;
    unsigned matches=0;for(unsigned i=0;i<state.count;++i)if(Text(state.meshes[i].assetPath)==geometry.meshPath){
        if(state.meshes[i].address<0x10000||!state.meshes[i].typeInfo||!state.meshes[i].namePointer)return false;
        ++matches;
    }
    return matches==1;
}
// Preserve the original selected/configuration lease and object identities.
// A newer read can confirm them, but cannot renew the prepared pose.
inline bool Bc2FeedSelectedRetained(const SelectedMeshesSnapshot& old,const SelectedMeshesSnapshot& current,
    const Bc2PhysicalFeedPresentation& p,const WeaponEquipmentIdentity& equipment,
    std::uint64_t sequence,std::int64_t now)noexcept {
    return Bc2FeedSelected(old,p,equipment,sequence,now)&&Bc2FeedSelected(current,p,equipment,sequence,now)&&
        current.sequence>=old.sequence&&current.observedNs>=old.observedNs&&current.owner==old.owner&&
        current.weaponData==old.weaponData&&current.inventory==old.inventory&&current.selectedSlot==old.selectedSlot&&
        current.stateTypeInfo==old.stateTypeInfo&&current.meshTypeInfo==old.meshTypeInfo&&current.weaponName==old.weaponName&&
        current.soleConfiguredArray==old.soleConfiguredArray&&current.states==old.states&&
        current.configurationPath==old.configurationPath&&current.configurationPathPointer==old.configurationPathPointer;
}
inline bool Bc2FeedPresentationRetained(const Bc2PhysicalFeedPresentation& original,
    const Bc2PhysicalFeedPresentation& current,bool posedHand,std::int64_t now)noexcept {
    if(!feed_presentation_detail::Window(original.input,now)||!feed_presentation_detail::Window(current.input,now)||
       original.owner!=current.owner||original.rig!=current.rig||original.rigFingerprint!=current.rigFingerprint||
       original.input.owner!=current.input.owner||current.input.sequence<original.input.sequence||
       original.weapon!=current.weapon||original.supply!=current.supply||original.lifetime!=current.lifetime||
       current.revision<original.revision||original.carriedPartsMask!=current.carriedPartsMask)return false;
    return !posedHand||(Bc2FeedMechanismClaimFresh(original,now)&&Bc2FeedMechanismClaimFresh(current,now)&&
        original.mechanismClaim->token==current.mechanismClaim->token&&original.operation==current.operation&&
        original.targetKind==current.targetKind);
}
inline std::optional<std::array<math::Matrix4,6>> Bc2FeedWorldBases(
    const Bc2PhysicalFeedPresentation& p,const math::Matrix4& placedWeaponWorldMeters)noexcept {
    if(p.carriedPartsMask&~std::uint8_t(61))return {};
    std::array<math::Matrix4,6> bases;for(auto& frame:bases)frame=placedWeaponWorldMeters;
    if(p.carriedPartsMask&1)bases[0]=p.pouchCarryWeaponWorldMeters;
    for(unsigned i=2;i<6;++i)if(p.carriedPartsMask&(1u<<i))bases[i]=p.chainCarryWeaponWorldMeters;
    for(const auto& frame:bases)if(!interaction::feed_mechanism_detail::Pose(frame))return {};
    return bases;
}
inline std::optional<Bc2FeedPartBinding> BindBc2FeedParts(const RigSnapshot& rig,
    std::string_view asset,std::string_view configuration,std::string_view mesh)noexcept {
    const auto hash=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
    if(!FindBc2M60FeedGeometry(asset,configuration,mesh,hash))return {};
    auto out=feed_presentation_detail::Derive(rig);if(out)out->fingerprint=hash;return out;
}
// All matrices below are canonical row-vector metres. An explicitly carried
// part uses the original weapon world basis of its accepted raw input; attached
// parts use the current placed weapon. The caller, not geometry, chooses this.
inline std::optional<std::array<math::Matrix4,6>> Bc2FeedPartWorldFrames(
    const Bc2M60FeedPresentation& parts,const math::Matrix4& weaponFromCoverHinge,
    const std::array<math::Matrix4,6>& worldFromWeapon,float units)noexcept {
    using namespace interaction;
    if(!feed_mechanism_detail::Supply(parts.supply,true)||!std::isfinite(units)||units<=0||
       !feed_mechanism_detail::Pose(weaponFromCoverHinge))return {};
    const auto hingeFromCover=feed_interaction_detail::Multiply(feed_presentation_detail::CoverReference,
        feed_interaction_detail::Inverse(bc2_feed_profile_detail::M60Geometry.weaponFromHinge));
    std::array<math::Matrix4,6> local{parts.weaponFromPouch,
        feed_interaction_detail::Multiply(hingeFromCover,weaponFromCoverHinge),
        parts.weaponFromChain[0],parts.weaponFromChain[1],parts.weaponFromChain[2],parts.weaponFromChain[3]};
    for(unsigned i=0;i<local.size();++i){
        if(!feed_mechanism_detail::Pose(local[i])||!feed_mechanism_detail::Pose(worldFromWeapon[i]))return {};
        local[i]=feed_presentation_detail::ScaleTranslation(
            feed_interaction_detail::Multiply(local[i],worldFromWeapon[i]),units);
    }
    return local;
}
// Preserve all unowned animation. Each known child overrides only its own
// subtree, so articulated belt segments are never flattened into one rigid part.
inline std::optional<std::vector<interaction::BoneWrite>> BuildBc2FeedPartWrites(
    const Bc2FeedPartBinding& binding,const RigSnapshot& rig,
    const std::array<math::Matrix4,6>& targets) {
    const auto actual=feed_presentation_detail::Derive(rig);
    if(!actual||actual->rig!=binding.rig||actual->parts!=binding.parts)return {};
    std::vector<interaction::BoneWrite> result;
    for(unsigned i=0;i<binding.parts.size();++i){
        if(!interaction::feed_mechanism_detail::Pose(targets[i]))return {};
        const auto writes=interaction::RetargetRigSubtree(rig.parents,rig.evaluatedWorld,
            binding.parts[i],targets[i],rig.nativeHiddenLeaves);
        if(!writes)return {};
        for(const auto& write:*writes){
            const auto old=std::find_if(result.begin(),result.end(),[&](const auto& x){return x.index==write.index;});
            if(old==result.end())result.push_back(write);else *old=write;
        }
    }
    return result;
}
}
