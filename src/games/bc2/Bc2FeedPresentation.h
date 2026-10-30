#pragma once
#include "Bc2FeedEquipmentProfile.h"
#include "Bc2PhysicalFeed.h"
#include "Bc2Rig.h"
#include "Bc2SightContact.h"
#include "fvr/interaction/TrackedRig.h"

namespace fvr::bc2 {
// Part order is pouch, cover, selected articulated chain, rigid cover companions.
// This binding grants private palette geometry only, never native reload state.
using Bc2FeedPartFrames=std::array<math::Matrix4,MaxBc2FeedParts>;
struct Bc2FeedPartBinding {
    RigIdentity rig{};
    std::array<std::uint32_t,MaxBc2FeedParts> parts{};
    std::uint64_t fingerprint=0,registrationKey=0;
    unsigned partCount=0;
    // Production points into the immutable registry. Structural fixtures must
    // retain their supplied geometry for the binding's lifetime.
    const Bc2FeedGeometry* geometry=nullptr;
};
namespace feed_presentation_detail {
// Exact measured t=0 cover frame from implementation15/geometry-evidence.json.
// The motion hinge's axes differ from the actual cover bone axes. Applying the
// hinge frame directly to bone9 would rotate the closed model by 90 degrees.
inline const auto& CoverReference=bc2_feed_profile_detail::M60Geometry.weaponFromCoverReference;
// Structural helper for deterministic fixtures. Production additionally pins
// the complete names/parent/inverse-bind fingerprint in BindBc2FeedParts.
inline std::optional<Bc2FeedPartBinding> Derive(const RigSnapshot& rig,const Bc2FeedGeometry& g)noexcept {
    if(rig.identity.count!=rig.names.size()||rig.names.size()!=rig.parents.size()||
       rig.names.size()!=rig.evaluatedWorld.size()||!ValidateBc2FeedGeometry(g)||
       rig.weaponBone!=g.rootBone.index||g.rootBone.index>=rig.names.size()||
       rig.names[g.rootBone.index]!=g.rootBone.bone||std::count(rig.names.begin(),rig.names.end(),g.rootBone.bone)!=1)return {};
    Bc2FeedPartBinding out;out.rig=rig.identity;out.partCount=Bc2FeedPartCount(g);out.geometry=&g;
    for(unsigned i=0;i<out.partCount;++i){
        const auto& b=i==0?g.pouchBone:i==1?g.coverBone:
            i<g.chainCount+2?g.chain[i-2]:g.coverCompanions[i-g.chainCount-2].bone;
        if(b.index>=rig.names.size()||b.parentIndex>=rig.names.size()||rig.names[b.index]!=b.bone||
           rig.parents[b.index]!=std::int32_t(b.parentIndex)||rig.names[b.parentIndex]!=b.parent||
           std::count(rig.names.begin(),rig.names.end(),b.bone)!=1||
           std::count(rig.names.begin(),rig.names.end(),b.parent)!=1||
           std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),b.index)!=rig.nativeHiddenLeaves.end())return {};
        auto ancestor=std::int32_t(b.index);unsigned depth=0;
        while(ancestor!=std::int32_t(g.rootBone.index)){
            if(ancestor<0||std::size_t(ancestor)>=rig.parents.size()||++depth>rig.parents.size())return {};
            ancestor=rig.parents[std::size_t(ancestor)];
        }
        out.parts[i]=b.index;
    }
    return out;
}
inline std::optional<Bc2FeedPartBinding> Derive(const RigSnapshot& rig)noexcept {
    return Derive(rig,bc2_feed_profile_detail::M60Geometry);
}
inline std::uint16_t CarryMask(unsigned chainCount)noexcept {
    // Cover companions follow the current weapon, never carried feed custody.
    return chainCount&&chainCount<=MaxBc2FeedChainBones?std::uint16_t(((1u<<(chainCount+2))-1u)&~2u):0;
}
inline std::optional<Bc2FeedPartFrames> WorldBases(const Bc2FeedGeometry& geometry,
    const Bc2PhysicalFeedPresentation& p,const math::Matrix4& placedWeaponWorldMeters)noexcept {
    if(!geometry.chainCount||geometry.chainCount>MaxBc2FeedChainBones||geometry.coverCompanionCount>MaxBc2FeedCoverCompanions||
       p.chainCount!=geometry.chainCount||p.carriedPartsMask&~CarryMask(p.chainCount))return {};
    Bc2FeedPartFrames bases{};
    for(unsigned i=0;i<Bc2FeedPartCount(geometry);++i)bases[i]=placedWeaponWorldMeters;
    if(p.carriedPartsMask&1)bases[0]=p.pouchCarryWeaponWorldMeters;
    for(unsigned i=2;i<p.chainCount+2;++i)if(p.carriedPartsMask&(1u<<i))bases[i]=p.chainCarryWeaponWorldMeters;
    for(unsigned i=0;i<Bc2FeedPartCount(geometry);++i)if(!interaction::feed_mechanism_detail::Pose(bases[i]))return {};
    return bases;
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
    const auto* row=FindBc2FeedRegistration(p.registrationKey);
    return row&&p.chainCount==row->geometry.chainCount&&p.rigFingerprint==row->geometry.rigFingerprint&&
        owner.player&&owner.soldier&&owner.weak&&owner.weapon&&owner.actorGeneration&&owner.equipGeneration&&owner.space&&
        p.owner==owner&&p.rig==rig&&rig.soldier==owner.soldier&&rig.weak==owner.weak&&
        p.lifetime&&p.revision&&p.rigFingerprint&&Window(p.input,now)&&Window(current,now)&&
        p.input.owner==current.owner&&p.input.sequence<=current.sequence&&p.input.observedNs<=current.observedNs&&
        current.owner.actor==((std::uint64_t(owner.weak)<<32)|owner.soldier)&&
        current.owner.actorGeneration==owner.actorGeneration&&current.owner.space==owner.space&&current.owner.equipGeneration&&
        p.weapon.id==owner.weapon&&p.weapon.generation==current.owner.equipGeneration&&equipment.weapon==owner.weapon&&
        equipment.data&&equipment.Asset()==row->geometry.asset&&
        interaction::feed_mechanism_detail::Supply(p.supply,true)&&!(p.carriedPartsMask&~CarryMask(p.chainCount))&&
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
    using namespace feed_presentation_detail;const auto* row=FindBc2FeedRegistration(p.registrationKey);
    if(!row||p.chainCount!=row->geometry.chainCount)return false;const auto& geometry=row->geometry;
    if(s.owner!=p.owner||!s.sequence||s.sequence>currentSequence||s.observedNs<=0||now<s.observedNs||now>=s.deadlineNs||
       s.deadlineNs<=s.observedNs||s.deadlineNs-s.observedNs>250000000||s.weaponData!=equipment.data||
       !s.configurationPathVerified||!s.configurationPathPointer||!s.inventory||!s.stateTypeInfo||!s.meshTypeInfo||
       s.stateCount!=1||!s.soleConfiguredArray||
       Text(s.weaponName)!=equipment.Asset()||
       FindBc2FeedRegistration(equipment.Asset(),Text(s.configurationPath),geometry.meshPath,p.rigFingerprint)!=row)return false;
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
    const auto* row=FindBc2FeedRegistration(original.registrationKey);
    if(!feed_presentation_detail::Window(original.input,now)||!feed_presentation_detail::Window(current.input,now)||
       original.owner!=current.owner||original.rig!=current.rig||original.rigFingerprint!=current.rigFingerprint||
       original.registrationKey!=current.registrationKey||original.chainCount!=current.chainCount||
       !row||original.chainCount!=row->geometry.chainCount||original.rigFingerprint!=row->geometry.rigFingerprint||
       (original.carriedPartsMask&~feed_presentation_detail::CarryMask(original.chainCount))||
       original.input.owner!=current.input.owner||current.input.sequence<original.input.sequence||
       original.weapon!=current.weapon||original.supply!=current.supply||original.lifetime!=current.lifetime||
       current.revision<original.revision||original.carriedPartsMask!=current.carriedPartsMask)return false;
    return !posedHand||(Bc2FeedMechanismClaimFresh(original,now)&&Bc2FeedMechanismClaimFresh(current,now)&&
        original.mechanismClaim->token==current.mechanismClaim->token&&original.operation==current.operation&&
        original.targetKind==current.targetKind);
}
inline std::optional<Bc2FeedPartFrames> Bc2FeedWorldBases(
    const Bc2PhysicalFeedPresentation& p,const math::Matrix4& placedWeaponWorldMeters)noexcept {
    const auto* row=FindBc2FeedRegistration(p.registrationKey);
    return row?feed_presentation_detail::WorldBases(row->geometry,p,placedWeaponWorldMeters):std::nullopt;
}
inline std::optional<Bc2FeedPartBinding> BindBc2FeedParts(const Bc2FeedRegistration& row,const RigSnapshot& rig)noexcept {
    if(FindBc2FeedRegistration(row.key)!=&row)return {};
    const auto hash=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
    if(hash!=row.geometry.rigFingerprint)return {};
    auto out=feed_presentation_detail::Derive(rig,row.geometry);
    if(out){out->fingerprint=hash;out->registrationKey=row.key;}return out;
}
inline std::optional<Bc2FeedPartBinding> BindBc2FeedParts(const RigSnapshot& rig,
    std::string_view asset,std::string_view configuration,std::string_view mesh)noexcept {
    const auto hash=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
    const auto* row=FindBc2FeedRegistration(asset,configuration,mesh,hash);
    return row?BindBc2FeedParts(*row,rig):std::nullopt;
}
// All matrices below are canonical row-vector metres. An explicitly carried
// part uses the original weapon world basis of its accepted raw input; attached
// parts use the current placed weapon. The caller, not geometry, chooses this.
inline std::optional<Bc2FeedPartFrames> Bc2FeedPartWorldFrames(const Bc2FeedGeometry& geometry,
    const Bc2FeedPresentation& parts,const math::Matrix4& weaponFromCoverHinge,
    const Bc2FeedPartFrames& worldFromWeapon,float units)noexcept {
    using namespace interaction;
    if(!ValidateBc2FeedGeometry(geometry)||parts.chainCount!=geometry.chainCount||
       !feed_mechanism_detail::Supply(parts.supply,true)||!std::isfinite(units)||units<=0||
       !feed_mechanism_detail::Pose(weaponFromCoverHinge))return {};
    const auto hingeFromCover=feed_interaction_detail::Multiply(geometry.weaponFromCoverReference,
        feed_interaction_detail::Inverse(geometry.weaponFromHinge));
    Bc2FeedPartFrames local{};local[0]=parts.weaponFromPouch;
    local[1]=feed_interaction_detail::Multiply(hingeFromCover,weaponFromCoverHinge);
    for(unsigned i=0;i<parts.chainCount;++i)local[i+2]=parts.weaponFromChain[i];
    const auto coverFromReference=feed_interaction_detail::Inverse(geometry.weaponFromCoverReference);
    for(unsigned i=0;i<geometry.coverCompanionCount;++i)
        local[parts.chainCount+2+i]=feed_interaction_detail::Multiply(
            feed_interaction_detail::Multiply(geometry.coverCompanions[i].weaponFromReference,coverFromReference),local[1]);
    for(unsigned i=0;i<Bc2FeedPartCount(geometry);++i){
        if(!feed_mechanism_detail::Pose(local[i])||!feed_mechanism_detail::Pose(worldFromWeapon[i]))return {};
        local[i]=feed_presentation_detail::ScaleTranslation(
            feed_interaction_detail::Multiply(local[i],worldFromWeapon[i]),units);
        if(!feed_mechanism_detail::Pose(local[i]))return {};
    }
    return local;
}
inline std::optional<Bc2FeedPartFrames> Bc2FeedPartWorldFrames(const Bc2FeedPresentation& parts,
    const math::Matrix4& hinge,const Bc2FeedPartFrames& bases,float units)noexcept {
    return Bc2FeedPartWorldFrames(bc2_feed_profile_detail::M60Geometry,parts,hinge,bases,units);
}
// Preserve all unowned animation. Each known child overrides only its own
// subtree, so articulated belt segments are never flattened into one rigid part.
inline std::optional<std::vector<interaction::BoneWrite>> BuildBc2FeedPartWrites(
    const Bc2FeedPartBinding& binding,const RigSnapshot& rig,
    const Bc2FeedPartFrames& targets) {
    if(!binding.geometry)return {};
    const auto actual=feed_presentation_detail::Derive(rig,*binding.geometry);
    if(!actual||actual->rig!=binding.rig||actual->parts!=binding.parts||actual->partCount!=binding.partCount||
       (binding.fingerprint&&SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)!=binding.fingerprint))return {};
    if(binding.registrationKey){const auto* row=FindBc2FeedRegistration(binding.registrationKey);
        if(!row||&row->geometry!=binding.geometry||row->geometry.rigFingerprint!=binding.fingerprint)return {};}
    // Ancestor overrides precede descendants even when a different measured
    // rig nests the pouch or cover. Each owned root retains its own target.
    std::array<unsigned,MaxBc2FeedParts> order{},depth{};
    for(unsigned i=0;i<binding.partCount;++i){order[i]=i;auto bone=std::int32_t(binding.parts[i]);
        while(bone>=0){
            if(std::size_t(bone)>=rig.parents.size()||++depth[i]>rig.parents.size())return {};
            bone=rig.parents[std::size_t(bone)];
        }
    }
    std::sort(order.begin(),order.begin()+binding.partCount,[&](unsigned a,unsigned b){return depth[a]<depth[b];});
    std::vector<interaction::BoneWrite> result;
    for(unsigned n=0;n<binding.partCount;++n){const auto i=order[n];
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
