#include "Bc2ReloadPresentation.h"
#include "Bc2ShotgunEquipment.h"
#include "Bc2SightContact.h"
#include <algorithm>
#include <cstring>
namespace fvr::bc2 {
namespace {
math::Matrix4 Units(math::Matrix4 m,float units)noexcept {
    for(unsigned n=0;n<3;++n)m.values[3][n]*=units;return m;
}
}
std::optional<math::Matrix4> SpasShellBoneAtCenter(const math::Matrix4& center,float units)noexcept {
    return ShotgunShellBoneAtCenter(DefaultSpasShotgunEquipment(),center,units);
}
std::optional<math::Matrix4> ShotgunShellBoneAtCenter(const ShotgunReloadEquipmentProfile* equipment,const math::Matrix4& center,float units)noexcept {
    const auto geometry=ShotgunShellGeometry(equipment);
    if(!geometry||!std::isfinite(units)||units<=0||!interaction::reload_insertion_detail::Rigid(center))return {};
    const auto inverse=interaction::InverseRigid(geometry->centerInBone);if(!inverse)return {};
    return interaction::Multiply(Units(*inverse,units),center);
}
namespace bc2_reload_detail {
std::optional<Bc2ReloadPresentationBinding> DerivePresentationBinding(const RigSnapshot& rig,const ShotgunReloadEquipmentProfile* equipment) {
    const auto geometry=ShotgunShellGeometry(equipment);if(!geometry||!geometry->handShape)return {};
    const auto size=rig.names.size();
    if(!size||size>1024||rig.parents.size()!=size||rig.inverseBind.size()!=size||rig.evaluatedWorld.size()!=size)return {};
    const auto named=[&](std::string_view name)->std::optional<std::uint32_t>{
        const auto it=std::find(rig.names.begin(),rig.names.end(),name);
        if(it==rig.names.end()||std::find(it+1,rig.names.end(),name)!=rig.names.end())return {};
        return std::uint32_t(it-rig.names.begin());
    };
    const auto root=named("jntWpn_1"),shell=named(geometry->bone),parent=named(geometry->parent),wrist=named("LeftHand");
    if(!root||!shell||!parent||!wrist||rig.weaponBone!=*root||rig.parents[*shell]!=std::int32_t(*parent))return {};
    // Entire graph is bounded and acyclic, even for unrelated branches.
    for(std::size_t n=0;n<size;++n){auto at=std::int32_t(n);std::size_t steps=0;
        while(at!=-1){if(at<0||std::size_t(at)>=size||++steps>size)return {};at=rig.parents[at];}}
    Bc2ReloadPresentationBinding out;out.weapon=*root;out.shell=*shell;out.wrist=*wrist;out.equipment=geometry->equipment;
    auto ancestor=std::int32_t(*shell);while(ancestor!=-1&&ancestor!=std::int32_t(*root))ancestor=rig.parents[ancestor];
    if(ancestor!=std::int32_t(*root))return {};
    for(const auto name:geometry->hiddenDescendants){const auto index=named(name);if(!index||*index==*shell)return {};
        auto at=rig.parents[*index];while(at!=-1&&at!=std::int32_t(*shell))at=rig.parents[at];
        if(at!=std::int32_t(*shell)||std::find(out.hiddenDescendants.begin(),out.hiddenDescendants.end(),*index)!=out.hiddenDescendants.end())return {};
        out.hiddenDescendants.push_back(*index);
    }
    for(std::size_t n=0;n<size;++n){if(n==*shell)continue;auto at=rig.parents[n];
        while(at!=-1&&at!=std::int32_t(*shell))at=rig.parents[at];
        if(at==std::int32_t(*shell)&&std::find(out.hiddenDescendants.begin(),out.hiddenDescendants.end(),n)==out.hiddenDescendants.end())return {};
    }
    for(unsigned finger=0;finger<5;++finger)if(geometry->handShape->counts[finger]!=3)return {};
    const std::array<const char*,5> fingers{"Thumb","Index","Middle","Ring","Pinky"};
    unsigned slot=0;
    for(const auto name:fingers){auto parent=*wrist;for(unsigned joint=1;joint<=3;++joint){
        const auto at=named(std::string("LeftHand")+name+std::to_string(joint));
        if(!at||rig.parents[*at]!=std::int32_t(parent))return {};
        out.fingers[slot++]=*at;parent=*at;
    }}
    // An unaccounted hand descendant would retain a stale attachment; reject it.
    for(std::size_t n=0;n<size;++n){if(n==*wrist)continue;auto at=rig.parents[n];
        while(at!=-1&&at!=std::int32_t(*wrist))at=rig.parents[at];
        if(at==std::int32_t(*wrist)&&std::find(out.fingers.begin(),out.fingers.end(),n)==out.fingers.end())return {};}
    out.fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);if(!out.fingerprint)return {};
    return out;
}
}
namespace bc2_reload_detail {
bool KnownHiddenShell(const RigSnapshot& rig,const Bc2ReloadPresentationBinding& binding)noexcept {
    const auto index=binding.shell;const auto g=ShotgunShellGeometry(binding.equipment);
    if(!g||index>=rig.names.size()||index>=rig.parents.size()||index>=rig.nativeWorld.size()||index>=rig.nativeEvaluated.size()||
       rig.names[index]!=g->bone||binding.weapon!=rig.weaponBone||rig.parents[index]<0||
       std::size_t(rig.parents[index])>=rig.names.size()||rig.names[rig.parents[index]]!=g->parent||
       std::count(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),index)!=1)return false;
    if(rig.parents.size()!=rig.names.size()||binding.hiddenDescendants.size()!=g->hiddenDescendants.size())return false;
    for(std::size_t n=0;n<rig.names.size();++n){
        auto at=std::int32_t(n);std::size_t steps=0;bool descendant=false;
        while(at!=-1){if(at<0||std::size_t(at)>=rig.parents.size()||++steps>rig.parents.size())return false;
            if(at==std::int32_t(index)&&n!=index)descendant=true;at=rig.parents[at];}
        if(descendant){const auto named=std::find(g->hiddenDescendants.begin(),g->hiddenDescendants.end(),rig.names[n]);
            if(named==g->hiddenDescendants.end()||std::count(binding.hiddenDescendants.begin(),binding.hiddenDescendants.end(),n)!=1)return false;}
    }
    for(std::size_t n=0;n<binding.hiddenDescendants.size();++n){const auto child=binding.hiddenDescendants[n];
        if(child>=rig.names.size()||rig.names[child]!=g->hiddenDescendants[n])return false;}
    const auto collapsed=[](const std::array<std::byte,64>& bytes){
        // xyz only: the original SIMD padding is opaque and preserved.
        for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col){float value=0;
            std::memcpy(&value,bytes.data()+row*16+col*4,4);
            if(!std::isfinite(value)||(row<3&&std::abs(value-(row==col?.0001f:0.f))>1e-7f))return false;
        }return true;
    };
    return collapsed(rig.nativeWorld[index])&&collapsed(rig.nativeEvaluated[index]);
}
std::optional<RigPosePlan> BuildShellVisibilityPalette(const RigSnapshot& rig,
    const Bc2ReloadPresentationBinding& binding,std::span<const interaction::BoneWrite> writes,
    std::optional<std::uint32_t> ownedShell) {
    const auto derived=DerivePresentationBinding(rig,binding.equipment);
    if(!derived||derived->fingerprint!=binding.fingerprint||derived->shell!=binding.shell||derived->weapon!=binding.weapon||
       derived->wrist!=binding.wrist||derived->fingers!=binding.fingers||derived->hiddenDescendants!=binding.hiddenDescendants)return {};
    auto privateRig=rig;
    if(ownedShell){if(*ownedShell!=binding.shell||!KnownHiddenShell(rig,binding))return {};
        std::erase(privateRig.nativeHiddenLeaves,binding.shell);}
    std::vector<interaction::BoneWrite> visible;
    for(const auto& w:writes)if(std::find(binding.hiddenDescendants.begin(),binding.hiddenDescendants.end(),w.index)==binding.hiddenDescendants.end())visible.push_back(w);
    auto plan=BuildRigPosePlan(privateRig,visible);if(!plan)return {};
    // A single owned shell never displays the authored second round. Collapse
    // only the exact declared descendants in the private final skin palette.
    for(const auto index:binding.hiddenDescendants){if(index>=rig.nativeEvaluated.size())return {};
        RigBoneEdit edit{index,rig.identity.evaluatedMatrices+index*64,rig.nativeEvaluated[index],rig.nativeEvaluated[index]};
        for(unsigned row=0;row<4;++row)std::memset(edit.after.data()+row*16,0,12);
        plan->edits.push_back(edit);
    }
    return plan;
}
}
std::optional<Bc2ReloadPresentationBinding> BindBc2ReloadPresentation(
    const RigSnapshot& rig,std::string_view asset,std::string_view mesh,const ShotgunReloadEquipmentProfile* requested) {
    const auto* e=KnownShotgunEquipment(requested);if(!e)return {};
    if(!ShotgunEquipmentMatches(e,asset,mesh,SightRigFingerprint(rig.names,rig.parents,rig.inverseBind),e->native->config->assetPath))return {};
    return bc2_reload_detail::DerivePresentationBinding(rig,e);
}
Bc2ReloadPresentationPlan BuildBc2ReloadPresentation(const RigSnapshot& rig,
    const Bc2ReloadPresentationBinding& binding,const Bc2ReloadPresentationObservation& s,
    const Bc2ReloadTargets& target) {
    using namespace interaction;
    const auto reject=[](Bc2ReloadPresentationReason reason){Bc2ReloadPresentationPlan p;p.reason=reason;return p;};
    if(!s.enabled)return reject(Bc2ReloadPresentationReason::Disabled);
    const auto* equipment=KnownShotgunEquipment(s.equipment);
    if(!equipment||!ShotgunEquipmentMatches(equipment,s.assetName,s.meshPath,equipment->rigFingerprint,s.configurationPath)||
       KnownShotgunEquipment(binding.equipment)!=equipment||KnownShotgunEquipment(target.equipment)!=equipment)
        return reject(Bc2ReloadPresentationReason::UnsupportedAsset);
    const auto geometry=ShotgunShellGeometry(equipment);if(!geometry||!geometry->handShape)return reject(Bc2ReloadPresentationReason::InvalidGeometry);
    if(target.identity!=s.identity||!target.inputSequence||target.inputSequence!=s.inputSequence||
       (s.carried?(target.nativeCycle||s.nativeCycle):(!target.nativeCycle||target.nativeCycle!=s.nativeCycle))||target.observedNs<=0||s.nowNs<target.observedNs||
       target.deadlineNs<=s.nowNs||target.deadlineNs-target.observedNs>100000000ll||
       target.shellClaim.owner!=target.identity.owner||target.weaponClaim.owner!=target.identity.owner||
       target.shellClaim.item!=target.identity.item||target.weaponClaim.item!=target.identity.weapon||
       !target.shellClaim.id||!target.weaponClaim.id||target.shellClaim.id==target.weaponClaim.id||
       target.shellClaim.hand!=InteractionHand::Left||target.weaponClaim.hand!=InteractionHand::Right||
       target.shellClaim.kind!=HandClaimKind::AmmoObject||target.weaponClaim.kind!=HandClaimKind::GunHold)
        return reject(Bc2ReloadPresentationReason::StaleOwnership);
    const auto derived=bc2_reload_detail::DerivePresentationBinding(rig,equipment);
    if(!derived||derived->fingerprint!=binding.fingerprint||derived->weapon!=binding.weapon||
       derived->shell!=binding.shell||derived->wrist!=binding.wrist||derived->fingers!=binding.fingers||derived->hiddenDescendants!=binding.hiddenDescendants)
        return reject(Bc2ReloadPresentationReason::InvalidBinding);
    const bool hidden=std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),binding.shell)!=rig.nativeHiddenLeaves.end();
    const bool ownedVisibility=hidden&&s.allowOwnedShellVisibility&&bc2_reload_detail::KnownHiddenShell(rig,binding);
    if(hidden&&!ownedVisibility)
        return reject(Bc2ReloadPresentationReason::NativeShellHidden);
    if(!s.selectedMeshIdentityVerified||!s.sectionOwnershipVerified||(!s.shellSectionVisible&&!ownedVisibility))
        return reject(Bc2ReloadPresentationReason::VisibilityUnverified);
    using namespace reload_insertion_detail;
    if(!std::isfinite(s.unitsPerMetre)||s.unitsPerMetre<=0||!Rigid(s.placedWeaponWorld)||
       !Rigid(target.weaponFromShellCenterMeters)||!Rigid(target.weaponFromLeftWristMeters)||
       (!ownedVisibility&&!Rigid(rig.evaluatedWorld[binding.shell])))return reject(Bc2ReloadPresentationReason::InvalidGeometry);
    const auto shellPose=ShotgunShellBoneAtCenter(equipment,Multiply(Units(target.weaponFromShellCenterMeters,s.unitsPerMetre),s.placedWeaponWorld),s.unitsPerMetre);
    if(!shellPose)return reject(Bc2ReloadPresentationReason::InvalidGeometry);
    const auto shell=*shellPose;
    const auto wrist=Multiply(Units(target.weaponFromLeftWristMeters,s.unitsPerMetre),s.placedWeaponWorld);
    Bc2ReloadPresentationPlan out;out.writes.reserve(17);
    out.writes.push_back({binding.shell,shell});out.writes.push_back({binding.wrist,wrist});
    for(unsigned n=0;n<binding.fingers.size();++n)
        out.writes.push_back({binding.fingers[n],Multiply(Units(geometry->handShape->wristFromJoint[n/3][n%3],s.unitsPerMetre),wrist)});
    if(ownedVisibility)out.ownedShellVisibility=binding.shell;
    out.palette=bc2_reload_detail::BuildShellVisibilityPalette(rig,binding,out.writes,out.ownedShellVisibility);
    if(!out.palette)return reject(Bc2ReloadPresentationReason::InvalidGeometry);
    return out;
}
}
