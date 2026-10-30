#pragma once
#include "Bc2PistolSlideProfile.h"
#include "Bc2Rig.h"
#include "Bc2SightContact.h"
#include "Bc2EquipmentIdentity.h"
#include "Bc2SelectedMeshes1p.h"
#include "fvr/interaction/PistolAfterFeedPolicy.h"

namespace fvr::bc2 {
// Freeze these raw poses and their original input/claims before any visual
// hand or weapon override. All three frames are canonical row-vector metres.
// The raw source may be a controller grip or the existing pre-IK mapped wrist.
// sourceFromWrist maps the wrist into that exact source frame (identity for
// the already mapped wrist); it never uses a solved/rendered hand.
struct Bc2PistolSlideSource {
    interaction::HandInteractionSample input{};
    RigIdentity rig{};
    interaction::HandInteractionKey weapon{},mechanism{};
    interaction::HandClaim gunClaim{},mechanismClaim{};
    math::Matrix4 rawSourceWorldMeters{},sourceFromWristMeters{},originalWeaponWorldMeters{};
};
struct Bc2PistolSlideBinding {
    RigIdentity rig{};
    std::uint64_t fingerprint=0;
    std::uint32_t leaf=0;
    std::uint64_t profileKey=0;
};
struct Bc2PistolSlideWorldFrames {
    // Only these completed world-frame translations are converted to native
    // units. Rotation and the source/controller/weapon frames stay untouched.
    math::Matrix4 slide{},authoredWrist{};
};
namespace bc2_pistol_slide_presentation_detail {
inline bool Packet(const interaction::HandInteractionSample& a,const interaction::HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.nowNs==b.nowNs&&a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
template<std::size_t N> inline std::string_view Text(const std::array<char,N>& value)noexcept {
    const auto end=std::find(value.begin(),value.end(),'\0');
    return end==value.end()?std::string_view{}:std::string_view(value.data(),std::size_t(end-value.begin()));
}
inline bool Window(const interaction::HandInteractionSample& s,std::int64_t now)noexcept {
    return interaction::weapon_cycle_detail::Owner(s.owner)&&s.sequence&&s.observedNs>0&&
        s.deadlineNs>s.observedNs&&s.deadlineNs-s.observedNs<=100000000&&
        s.nowNs>=s.observedNs&&s.nowNs<s.deadlineNs&&now>=s.nowNs&&now<s.deadlineNs&&
        s.focused&&s.tracked[0]&&s.tracked[1]&&!s.released[0]&&!s.released[1];
}
inline bool Claims(const Bc2PistolSlideSource& p,const interaction::HandInteractionSample& input,
    const interaction::HandClaim& gun,const interaction::HandClaim& mechanism,std::int64_t now)noexcept {
    using namespace interaction;
    const auto common=[&](const HandClaim& claim){return claim.token.id&&claim.token.owner==input.owner&&
        claim.token.item==p.weapon&&weapon_cycle_detail::Key(claim.token.contact)&&
        claim.inputSequence==input.sequence&&claim.deadlineNs>now&&claim.deadlineNs<=input.deadlineNs;};
    return weapon_cycle_detail::Key(p.weapon)&&weapon_cycle_detail::Key(p.mechanism)&&
        p.weapon.generation==input.owner.equipGeneration&&common(gun)&&common(mechanism)&&
        gun.token.kind==HandClaimKind::GunHold&&gun.token.hand==InteractionHand::Right&&
        mechanism.token.kind==HandClaimKind::Mechanism&&mechanism.token.hand==InteractionHand::Left&&
        mechanism.token.contact==p.mechanism&&mechanism.token.prerequisiteClaim==gun.token.id&&
        mechanism.token.id!=gun.token.id;
}
// Structural fixture helper; production must ALSO verify the complete rig
// fingerprint and exact selector. Reviewed slide geometry requires a direct leaf.
inline bool Topology(const RigSnapshot& rig,const Bc2PistolSlideGeometry& g)noexcept {
    if(rig.identity.count!=rig.names.size()||rig.names.size()!=rig.parents.size()||
       rig.names.size()!=rig.evaluatedWorld.size()||rig.names.size()!=rig.inverseBind.size()||
       rig.names.size()<=g.slideIndex||rig.names.size()<=g.rootIndex||rig.names.size()>1024||rig.weaponBone!=g.rootIndex||
       rig.names[g.rootIndex]!=g.root||rig.names[g.slideIndex]!=g.slide||
       rig.parents[g.slideIndex]!=std::int32_t(g.rootIndex)||
       std::count(rig.names.begin(),rig.names.end(),g.root)!=1||
       std::count(rig.names.begin(),rig.names.end(),g.slide)!=1||
       std::find(rig.parents.begin(),rig.parents.end(),std::int32_t(g.slideIndex))!=rig.parents.end()||
       std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),g.slideIndex)!=rig.nativeHiddenLeaves.end())return false;
    // Reject cycles/out-of-range topology without requiring parent-before-child
    // ordering (the measured rig has later-index parents on some branches).
    for(std::size_t i=0;i<rig.parents.size();++i){
        auto parent=std::int32_t(i);std::size_t depth=0;
        while(parent!=-1){
            if(parent<0||std::size_t(parent)>=rig.parents.size()||++depth>rig.parents.size())return false;
            parent=rig.parents[std::size_t(parent)];
        }
    }
    return true;
}
inline std::optional<interaction::BoneWrite> LeafWrite(const RigSnapshot& rig,
    const Bc2PistolSlideGeometry& geometry,const math::Matrix4& target)noexcept {
    if(!Topology(rig,geometry)||!interaction::feed_mechanism_detail::Pose(target))return {};
    // Absolute leaf target, independent of the old palette or restoration.
    // No inverse of a previous mod write can become input gesture geometry.
    return interaction::BoneWrite{geometry.slideIndex,target};
}
inline math::Matrix4 ScaleTranslation(math::Matrix4 frame,float units)noexcept {
    for(unsigned i=0;i<3;++i)frame.values[3][i]*=units;return frame;
}
}
[[nodiscard]] inline bool Bc2PistolSlideSourceFresh(const Bc2PistolSlideSource& source,
    const interaction::HandInteractionSample& current,const interaction::HandClaim& currentGun,
    const interaction::HandClaim& currentMechanism,const RigIdentity& currentRig,std::int64_t now)noexcept {
    using namespace bc2_pistol_slide_presentation_detail;
    using interaction::feed_mechanism_detail::Pose;
    return Window(source.input,now)&&Window(current,now)&&source.input.owner==current.owner&&
        current.sequence>=source.input.sequence&&current.observedNs>=source.input.observedNs&&
        (current.sequence!=source.input.sequence||
            (current.observedNs==source.input.observedNs&&current.deadlineNs==source.input.deadlineNs))&&
        (current.sequence==source.input.sequence||current.observedNs>source.input.observedNs)&&
        current.nowNs>=source.input.nowNs&&source.rig==currentRig&&source.rig.count&&
        source.rig.soldier&&source.rig.weak&&
        source.input.owner.actor==((std::uint64_t(source.rig.weak)<<32)|source.rig.soldier)&&
        Claims(source,source.input,source.gunClaim,source.mechanismClaim,now)&&
        currentGun.token.owner==current.owner&&currentGun.token.item==source.weapon&&
        currentGun.inputSequence==current.sequence&&currentGun.deadlineNs>now&&currentGun.deadlineNs<=current.deadlineNs&&
        currentMechanism.token.owner==current.owner&&currentMechanism.token.item==source.weapon&&
        currentMechanism.inputSequence>=source.input.sequence&&currentMechanism.inputSequence<=current.sequence&&
        currentMechanism.deadlineNs>now&&currentMechanism.deadlineNs<=current.deadlineNs&&
        source.gunClaim.token==currentGun.token&&source.mechanismClaim.token==currentMechanism.token&&
        Pose(source.rawSourceWorldMeters)&&Pose(source.sourceFromWristMeters)&&Pose(source.originalWeaponWorldMeters);
}
[[nodiscard]] inline bool Bc2PistolSlideSourceRetained(const Bc2PistolSlideSource& original,
    const Bc2PistolSlideSource& current,const interaction::HandInteractionSample& safety,
    const interaction::HandClaim& gun,const interaction::HandClaim& mechanism,const RigIdentity& rig,std::int64_t now)noexcept {
    if(!Bc2PistolSlideSourceFresh(original,safety,gun,mechanism,rig,now)||
       !Bc2PistolSlideSourceFresh(current,safety,gun,mechanism,rig,now)||
       current.weapon!=original.weapon||current.mechanism!=original.mechanism||
       current.input.sequence<original.input.sequence||current.input.observedNs<original.input.observedNs||
       current.input.nowNs<original.input.nowNs)return false;
    if(current.input.sequence!=original.input.sequence)return current.input.observedNs>original.input.observedNs;
    // Repeated processing may advance nowNs, but cannot restamp the original
    // packet, claims or raw geometry. A distinct packet may only confirm them.
    return current.input.observedNs==original.input.observedNs&&current.input.deadlineNs==original.input.deadlineNs&&
        current.gunClaim.inputSequence==original.gunClaim.inputSequence&&current.gunClaim.deadlineNs==original.gunClaim.deadlineNs&&
        current.mechanismClaim.inputSequence==original.mechanismClaim.inputSequence&&
        current.mechanismClaim.deadlineNs==original.mechanismClaim.deadlineNs&&
        current.rawSourceWorldMeters.values==original.rawSourceWorldMeters.values&&
        current.sourceFromWristMeters.values==original.sourceFromWristMeters.values&&
        current.originalWeaponWorldMeters.values==original.originalWeaponWorldMeters.values;
}
// Typed policy output must still describe this exact source packet and actual
// claim pair. Geometry does not manufacture a target or native readiness.
[[nodiscard]] inline bool Bc2PistolSlideTargetFresh(const Bc2PistolSlideSource& source,
    const ReloadStateOwner& owner,const Bc2PistolSlideRegistration& row,
    const interaction::PistolAfterFeedTarget& target,float displacement,std::int64_t now)noexcept {
    const auto profile=BuildBc2PistolSlideProfile(row);
    const auto& context=target.context;const auto& resource=context.resource;
    if(!profile||!target.feedRequest||target.inputSequence!=source.input.sequence||
       target.observedNs!=source.input.observedNs||now<target.observedNs||now>=target.deadlineNs||
       target.deadlineNs>source.input.deadlineNs||target.deadlineNs>source.gunClaim.deadlineNs||
       target.deadlineNs>source.mechanismClaim.deadlineNs||target.gun!=source.gunClaim.token||
       target.mechanism!=source.mechanismClaim.token||source.rig.soldier!=owner.soldier||source.rig.weak!=owner.weak||
       source.weapon.id!=owner.weapon||source.input.owner.actorGeneration!=owner.actorGeneration||
       source.input.owner.space!=owner.space||resource.actor!=owner.soldier||resource.actorGeneration!=owner.actorGeneration||
       resource.weapon!=owner.weapon||!resource.weaponGeneration||resource.weaponGeneration!=source.mechanism.generation||
       context.equipGeneration!=owner.equipGeneration||context.space!=owner.space||
       !std::isfinite(target.travel)||target.travel<0||target.travel>profile->stroke||
       displacement!=row.interaction->sourceDisplacementMeters+target.travel)return false;
    auto expected=profile->closedContact;
    for(unsigned axis=0;axis<3;++axis)expected.values[3][axis]+=profile->axis[axis]*target.travel;
    return target.contact.values==expected.values&&interaction::feed_mechanism_detail::Pose(target.contact);
}
[[nodiscard]] inline bool Bc2PistolSlideTargetFresh(const Bc2PistolSlideSource& source,
    const ReloadStateOwner& owner,const Bc2PistolSlideSelection& selection,
    const interaction::PistolAfterFeedTarget& target,float displacement,std::int64_t now)noexcept {
    const auto* row=FindBc2PistolSlideRegistration(selection);
    return row&&Bc2PistolSlideTargetFresh(source,owner,*row,target,displacement,now);
}
[[nodiscard]] inline bool Bc2PistolSlideTargetRetained(const interaction::PistolAfterFeedTarget& original,
    const interaction::PistolAfterFeedTarget& current)noexcept {
    // Freshness/geometry of each target is checked independently. Advancing
    // travel can confirm an older private pose, never replace its geometry.
    return original.context==current.context&&original.feedRequest==current.feedRequest&&
        original.gun==current.gun&&original.mechanism==current.mechanism&&
        current.inputSequence>=original.inputSequence&&current.observedNs>=original.observedNs;
}
[[nodiscard]] inline bool Bc2PistolSlideSelected(const SelectedMeshesSnapshot& s,const ReloadStateOwner& owner,
    const Bc2PistolSlideRegistration& row,const WeaponEquipmentIdentity& equipment,
    std::uint64_t sequence,std::int64_t now)noexcept {
    using namespace bc2_pistol_slide_presentation_detail;
    if(!Bc2PistolSlideRegistrationValid(row))return false;
    const auto& selection=row.geometry->selection;
    if(s.owner!=owner||equipment.weapon!=owner.weapon||
       equipment.Asset()!=selection.asset||!equipment.data||!s.sequence||s.sequence>sequence||
       s.observedNs<=0||now<s.observedNs||now>=s.deadlineNs||s.deadlineNs-s.observedNs>250000000||
       s.weaponData!=equipment.data||!s.inventory||!s.stateTypeInfo||!s.meshTypeInfo||
       !s.configurationPathVerified||!s.configurationPathPointer||Text(s.configurationPath)!=selection.configurationPath||
       Text(s.weaponName)!=selection.asset||s.stateCount!=1||!s.soleConfiguredArray)return false;
    const auto& state=s.states[0];
    if(!state.state||state.array!=s.soleConfiguredArray||!state.count||state.count>state.meshes.size())return false;
    unsigned matches=0;
    for(unsigned i=0;i<state.count;++i)if(Text(state.meshes[i].assetPath)==selection.meshPath){
        if(state.meshes[i].address<0x10000||!state.meshes[i].typeInfo||!state.meshes[i].namePointer)return false;
        ++matches;
    }
    return matches==1;
}
[[nodiscard]] inline bool Bc2PistolSlideSelected(const SelectedMeshesSnapshot& s,const ReloadStateOwner& owner,
    const Bc2PistolSlideSelection& selection,const WeaponEquipmentIdentity& equipment,
    std::uint64_t sequence,std::int64_t now)noexcept {
    const auto* row=FindBc2PistolSlideRegistration(selection);
    return row&&Bc2PistolSlideSelected(s,owner,*row,equipment,sequence,now);
}
// A newer selected read confirms object/configuration identity, never extends
// the original selected lease retained with the prepared private palette.
[[nodiscard]] inline bool Bc2PistolSlideSelectedRetained(const SelectedMeshesSnapshot& original,
    const SelectedMeshesSnapshot& current,const ReloadStateOwner& owner,const Bc2PistolSlideRegistration& row,
    const WeaponEquipmentIdentity& equipment,std::uint64_t sequence,std::int64_t now)noexcept {
    return Bc2PistolSlideSelected(original,owner,row,equipment,sequence,now)&&
        Bc2PistolSlideSelected(current,owner,row,equipment,sequence,now)&&
        current.sequence>=original.sequence&&current.observedNs>=original.observedNs&&
        current.weaponData==original.weaponData&&current.inventory==original.inventory&&current.selectedSlot==original.selectedSlot&&
        current.stateTypeInfo==original.stateTypeInfo&&current.meshTypeInfo==original.meshTypeInfo&&
        current.soleConfiguredArray==original.soleConfiguredArray&&current.states==original.states&&
        current.configurationPath==original.configurationPath&&current.configurationPathPointer==original.configurationPathPointer&&
        current.weaponName==original.weaponName;
}
[[nodiscard]] inline bool Bc2PistolSlideSelectedRetained(const SelectedMeshesSnapshot& original,
    const SelectedMeshesSnapshot& current,const ReloadStateOwner& owner,const Bc2PistolSlideSelection& selection,
    const WeaponEquipmentIdentity& equipment,std::uint64_t sequence,std::int64_t now)noexcept {
    const auto* row=FindBc2PistolSlideRegistration(selection);
    return row&&Bc2PistolSlideSelectedRetained(original,current,owner,*row,equipment,sequence,now);
}
// Recognizer input only. No evaluated/rendered hand or newly placed weapon is
// accepted; the three raw source frames belong to one immutable input packet.
[[nodiscard]] inline std::optional<math::Matrix4> Bc2PistolSlideRawWristInWeapon(
    const Bc2PistolSlideSource& source)noexcept {
    using namespace interaction;
    if(!feed_mechanism_detail::Pose(source.rawSourceWorldMeters)||
       !feed_mechanism_detail::Pose(source.sourceFromWristMeters)||
       !feed_mechanism_detail::Pose(source.originalWeaponWorldMeters))return {};
    const auto wrist=feed_interaction_detail::Multiply(source.sourceFromWristMeters,source.rawSourceWorldMeters);
    const auto local=feed_interaction_detail::Multiply(wrist,feed_interaction_detail::Inverse(source.originalWeaponWorldMeters));
    return feed_mechanism_detail::Pose(local)?std::optional<math::Matrix4>{local}:std::nullopt;
}
[[nodiscard]] inline std::optional<Bc2PistolSlideBinding> BindBc2PistolSlide(
    const RigSnapshot& rig,const Bc2PistolSlideRegistration& row)noexcept {
    if(!Bc2PistolSlideRegistrationValid(row))return {};
    const auto* geometry=row.geometry;
    if(!bc2_pistol_slide_presentation_detail::Topology(rig,*geometry))return {};
    const auto fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
    if(fingerprint!=geometry->selection.rigFingerprint)return {};
    return Bc2PistolSlideBinding{rig.identity,fingerprint,geometry->slideIndex,row.key};
}
[[nodiscard]] inline std::optional<Bc2PistolSlideBinding> BindBc2PistolSlide(
    const RigSnapshot& rig,const Bc2PistolSlideSelection& selection)noexcept {
    const auto* row=FindBc2PistolSlideRegistration(selection);return row?BindBc2PistolSlide(rig,*row):std::nullopt;
}
// This is pure presentation math for an already accepted displacement. It does
// not recognize a gesture, establish a hand claim, authorize writes or resolve
// slide debt. The attached slide follows the currently placed held weapon;
// that separate render basis never changes the immutable gesture source.
[[nodiscard]] inline std::optional<Bc2PistolSlideWorldFrames> ComposeBc2PistolSlideWorldFrames(
    const Bc2PistolSlideRegistration& row,const Bc2PistolSlideSource& source,
    const math::Matrix4& placedWeaponWorldMeters,float signedDisplacementMeters,float unitsPerMetre,std::int64_t now)noexcept {
    using namespace interaction;
    if(!std::isfinite(unitsPerMetre)||unitsPerMetre<=0||!feed_mechanism_detail::Pose(placedWeaponWorldMeters)||
       !Bc2PistolSlideSourceFresh(source,source.input,source.gunClaim,source.mechanismClaim,source.rig,now))return {};
    const auto local=Bc2PistolSlideLocalFrame(row,signedDisplacementMeters);if(!local)return {};
    const auto slide=feed_interaction_detail::Multiply(*local,placedWeaponWorldMeters);
    const auto wrist=feed_interaction_detail::Multiply(
        row.geometry->slideFromAuthoredWrist,slide);
    Bc2PistolSlideWorldFrames out{
        bc2_pistol_slide_presentation_detail::ScaleTranslation(slide,unitsPerMetre),
        bc2_pistol_slide_presentation_detail::ScaleTranslation(wrist,unitsPerMetre)};
    if(!feed_mechanism_detail::Pose(out.slide)||!feed_mechanism_detail::Pose(out.authoredWrist))return {};
    return out;
}
[[nodiscard]] inline std::optional<Bc2PistolSlideWorldFrames> ComposeBc2PistolSlideWorldFrames(
    const Bc2PistolSlideSelection& selection,const Bc2PistolSlideSource& source,
    const math::Matrix4& placedWeaponWorldMeters,float signedDisplacementMeters,float unitsPerMetre,std::int64_t now)noexcept {
    const auto* row=FindBc2PistolSlideRegistration(selection);
    return row?ComposeBc2PistolSlideWorldFrames(*row,source,placedWeaponWorldMeters,signedDisplacementMeters,unitsPerMetre,now):std::nullopt;
}
// One candidate leaf write; all other native animation remains untouched.
// The caller owns exact-write restoration and native publication authority.
[[nodiscard]] inline std::optional<interaction::BoneWrite> BuildBc2PistolSlideWrite(
    const Bc2PistolSlideRegistration& row,const Bc2PistolSlideBinding& binding,
    const RigSnapshot& rig,const math::Matrix4& target)noexcept {
    if(!Bc2PistolSlideRegistrationValid(row))return {};
    const auto& g=*row.geometry;
    if(binding.profileKey!=row.key||binding.rig!=rig.identity||binding.leaf!=g.slideIndex||binding.fingerprint!=g.selection.rigFingerprint||
       SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)!=binding.fingerprint)return {};
    return bc2_pistol_slide_presentation_detail::LeafWrite(rig,g,target);
}
[[nodiscard]] inline std::optional<interaction::BoneWrite> BuildBc2PistolSlideWrite(
    const Bc2PistolSlideBinding& binding,const RigSnapshot& rig,const math::Matrix4& target)noexcept {
    const auto* row=ResolveBc2PistolSlideRegistration(binding.profileKey);
    return row?BuildBc2PistolSlideWrite(*row,binding,rig,target):std::nullopt;
}
}
