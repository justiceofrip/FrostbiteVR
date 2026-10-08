#include "Bc2ReloadPreview.h"
#include "Bc2WeaponVisibility.h"
#include <algorithm>
namespace fvr::bc2 {
namespace {
using namespace interaction;
bool Fresh(std::int64_t observed,std::int64_t deadline,std::int64_t now,std::int64_t limit)noexcept {
    return observed>0&&now>=observed&&deadline>now&&deadline-observed<=limit;
}
bool NativeOwner(const ReloadStateOwner& o)noexcept {
    return o.player>=0x10000&&o.soldier>=0x10000&&o.weak>=0x10000&&o.weapon>=0x10000&&o.actorGeneration&&o.equipGeneration&&o.space;
}
bool NativeIdentity(const ReloadHoldIdentity& i,const ReloadStateOwner& o)noexcept {
    return i.owner==o&&i.serverPlayer>=0x10000&&i.serverSoldier>=0x10000&&i.serverItem>=0x10000&&
        i.firing[0]>=0x10000&&i.firing[1]>=0x10000&&i.firing[2]>=0x10000&&
        i.firing[0]!=i.firing[1]&&i.firing[0]!=i.firing[2]&&i.firing[1]!=i.firing[2];
}
bool Claim(const HandClaim& c,const ReloadTracking& t,InteractionHand hand,HandClaimKind kind,std::int64_t now)noexcept {
    return c.token.id&&c.token.owner==t.inputEvidence.owner&&c.token.hand==hand&&c.token.kind==kind&&
        c.token.item.id&&c.token.item.generation&&c.inputSequence&&c.inputSequence<=t.inputEvidence.sequence&&
        c.deadlineNs>now&&c.deadlineNs<=t.inputEvidence.deadlineNs;
}
}
bool ReloadTrackingFresh(const ReloadTracking& t,std::int64_t now)noexcept {
    const auto& s=t.inputEvidence;
    return t.enabled&&NativeOwner(t.owner)&&s.owner.actor==((std::uint64_t(t.owner.weak)<<32)|t.owner.soldier)&&
        s.owner.actorGeneration==t.owner.actorGeneration&&s.owner.space==t.owner.space&&s.owner.equipGeneration&&
        s.sequence&&s.focused&&s.tracked[0]&&s.tracked[1]&&
        Fresh(s.observedNs,s.deadlineNs,now,150000000);
}
bool ReloadPreviewFresh(const ReloadPreview& p,const ReloadTracking& t,std::int64_t now)noexcept {
    if(!ReloadTrackingFresh(t,now)||t.inputEvidence.released[0]||!p.selectedMeshes||
       !Claim(p.shellClaim,t,InteractionHand::Left,HandClaimKind::AmmoObject,now)||
       !Claim(p.weaponClaim,t,InteractionHand::Right,HandClaimKind::GunHold,now)||
       p.shellClaim.token.id==p.weaponClaim.token.id||p.shellClaim.token.item==p.weaponClaim.token.item||
       p.weaponClaim.token.item.id!=t.owner.weapon||p.weaponClaim.token.item.generation!=t.inputEvidence.owner.equipGeneration||
       (p.shellClaim.token.prerequisiteClaim&&p.shellClaim.token.prerequisiteClaim!=p.weaponClaim.token.id))return false;
    const auto& meshes=*p.selectedMeshes;
    const auto mesh=FindSelectedMesh(meshes,t.owner,SelectedMeshKind::Spas12,now);
    if(!mesh||!meshes.sequence||!Fresh(meshes.observedNs,meshes.deadlineNs,now,250000000)||
       std::find(mesh->assetPath.begin(),mesh->assetPath.end(),'\0')==mesh->assetPath.end()||
       std::string_view(mesh->assetPath.data())!=SpasReloadMesh)return false;
    if(p.phase==ReloadPreviewPhase::Carried){
        const auto& r=p.reserve;
        return NativeIdentity(r.identity,t.owner)&&r.verified&&r.sequence&&Fresh(r.observedNs,r.deadlineNs,now,200000000)&&
            r.loaded>=0&&r.capacity>r.loaded&&r.capacity<=1000000&&r.reserve>0&&r.reserve<=1000000&&
            !p.native.cycle&&!p.targets.nativeCycle;
    }
    if(p.phase!=ReloadPreviewPhase::Guided&&p.phase!=ReloadPreviewPhase::Pending)return false;
    const auto& n=p.native;const auto& v=p.targets;
    return NativeIdentity(n.identity,t.owner)&&n.nativeBindingVerified&&n.allThreeHeld&&n.cycle&&n.sequence&&
        Fresh(n.observedNs,n.deadlineNs,now,200000000)&&n.loaded>=0&&n.capacity>n.loaded&&n.capacity<=1000000&&n.reserve>0&&n.reserve<=1000000&&
        v.nativeCycle==n.cycle&&v.identity.owner==t.inputEvidence.owner&&v.identity.weapon==p.weaponClaim.token.item&&
        v.identity.item==p.shellClaim.token.item&&v.identity.trackingEpoch&&
        v.shellClaim==p.shellClaim.token&&v.weaponClaim==p.weaponClaim.token&&v.inputSequence&&v.inputSequence<=t.inputEvidence.sequence&&
        Fresh(v.observedNs,v.deadlineNs,now,100000000)&&v.deadlineNs<=n.deadlineNs&&
        v.deadlineNs<=p.shellClaim.deadlineNs&&v.deadlineNs<=p.weaponClaim.deadlineNs;
}
bool ReloadPreviewRetained(const ReloadPreview& p,const ReloadTracking& t,std::int64_t now)noexcept {
    if(!t.preview||!ReloadPreviewFresh(*t.preview,t,now))return false;
    const auto& current=*t.preview;
    if(p.phase!=current.phase||p.shellClaim.token!=current.shellClaim.token||p.weaponClaim.token!=current.weaponClaim.token||
       !p.selectedMeshes||p.selectedMeshes->owner!=t.owner)return false;
    // The old rendered pose retains its original leases and never borrows a new
    // deadline. Claims may have been renewed on a newer real input packet.
    // Claim sequence provenance is checked at publication; only
    // current safety plus the old independent deadlines apply at consumption.
    if(!p.shellClaim.inputSequence||!p.weaponClaim.inputSequence||p.shellClaim.inputSequence>t.inputEvidence.sequence||
       p.weaponClaim.inputSequence>t.inputEvidence.sequence||p.shellClaim.deadlineNs<=now||p.weaponClaim.deadlineNs<=now||
       !FindSelectedMesh(*p.selectedMeshes,t.owner,SelectedMeshKind::Spas12,now))return false;
    if(p.phase==ReloadPreviewPhase::Carried)return p.reserve.identity==current.reserve.identity&&
        p.reserve.verified&&Fresh(p.reserve.observedNs,p.reserve.deadlineNs,now,200000000);
    return p.native.identity==current.native.identity&&p.native.cycle==current.native.cycle&&p.targets.identity==current.targets.identity&&
        p.native.nativeBindingVerified&&p.native.allThreeHeld&&Fresh(p.native.observedNs,p.native.deadlineNs,now,200000000)&&
        Fresh(p.targets.observedNs,p.targets.deadlineNs,now,100000000);
}
ReloadPackedPaletteChoice SelectReloadPackedPalette(const ReloadPreview& p,const ReloadTracking* current,
    std::int64_t now,std::int64_t deadline,std::span<const std::array<std::byte,64>> posed,
    std::span<const std::array<std::byte,64>> base,bool coherent)noexcept {
    if(!posed.empty()&&posed.size()<=1024&&current&&coherent&&now>0&&deadline>now&&ReloadPreviewRetained(p,*current,now))return {posed,false};
    if(!posed.empty()&&posed.size()<=1024&&base.size()==posed.size())return {base,true};
    return {{},true};
}
bool ReloadShellControlFresh(const ReloadTracking& t,std::int64_t now)noexcept {
    if(!ReloadTrackingFresh(t,now)||!t.shellControl)return false;
    const auto& c=*t.shellControl;const auto& r=c.reserve;
    if(!c.cycle||!NativeIdentity(r.identity,t.owner)||!r.verified||!r.sequence||
       !Fresh(r.observedNs,r.deadlineNs,now,200000000)||r.loaded<0||r.capacity<r.loaded||r.capacity<=0||r.reserve<0||
       !Claim(c.weaponClaim,t,InteractionHand::Right,HandClaimKind::GunHold,now)||t.inputEvidence.released[1]||
       c.weaponClaim.token.item.id!=t.owner.weapon||c.weaponClaim.token.item.generation!=t.inputEvidence.owner.equipGeneration||
       !c.selectedMeshes)return false;
    const auto& meshes=*c.selectedMeshes;
    const auto* mesh=FindSelectedMesh(meshes,t.owner,SelectedMeshKind::Spas12,now);
    return mesh&&meshes.sequence&&Fresh(meshes.observedNs,meshes.deadlineNs,now,250000000)&&
        std::find(mesh->assetPath.begin(),mesh->assetPath.end(),'\0')!=mesh->assetPath.end()&&std::string_view(mesh->assetPath.data())==SpasReloadMesh;
}
bool ReloadBeltCompatible(const ReloadTracking& t,std::int64_t now)noexcept {
    if(!t.belt||t.preview||!ReloadTrackingFresh(t,now)||!SpasBeltAmmoFresh(*t.belt,now)||
       t.belt->owner!=t.owner||t.belt->visual.input.owner!=t.inputEvidence.owner||
       t.belt->visual.input.sequence!=t.inputEvidence.sequence)return false;
    if(!t.belt->heldCycle)return !t.shellControl;
    if(!ReloadShellControlFresh(t,now))return false;
    const auto& b=*t.belt;const auto& c=*t.shellControl;
    return b.heldCycle->cycle==c.cycle&&b.reserve.identity==c.reserve.identity&&b.reserve.loaded==c.reserve.loaded&&
        b.reserve.reserve==c.reserve.reserve&&b.reserve.capacity==c.reserve.capacity&&
        b.visual.gun.token==c.weaponClaim.token&&b.meshes->owner==c.selectedMeshes->owner&&
        b.meshes->weaponData==c.selectedMeshes->weaponData&&b.meshes->states==c.selectedMeshes->states;
}
bool ReloadShellControlRetained(const ReloadTracking& source,const ReloadTracking& current,std::int64_t now)noexcept {
    if(source.preview||current.preview||!ReloadShellControlFresh(source,now)||!ReloadShellControlFresh(current,now)||
       source.owner!=current.owner||source.inputEvidence.owner!=current.inputEvidence.owner||
       source.inputEvidence.sequence>current.inputEvidence.sequence||source.inputEvidence.observedNs>current.inputEvidence.observedNs)return false;
    if(source.inputEvidence.sequence==current.inputEvidence.sequence&&
       (source.inputEvidence.observedNs!=current.inputEvidence.observedNs||source.inputEvidence.deadlineNs!=current.inputEvidence.deadlineNs))return false;
    const auto& a=*source.shellControl;const auto& b=*current.shellControl;
    return a.cycle==b.cycle&&a.reserve.identity==b.reserve.identity&&a.weaponClaim.token==b.weaponClaim.token&&
        b.reserve.sequence>=a.reserve.sequence&&b.reserve.observedNs>=a.reserve.observedNs&&
        a.selectedMeshes->owner==b.selectedMeshes->owner&&a.selectedMeshes->sequence<=b.selectedMeshes->sequence&&
        a.selectedMeshes->weaponData==b.selectedMeshes->weaponData&&a.selectedMeshes->soleConfiguredArray==b.selectedMeshes->soleConfiguredArray&&
        a.selectedMeshes->states==b.selectedMeshes->states;
}
std::optional<ReloadShellHidePlan> BuildReloadShellHidePalette(const ReloadTracking& t,const RigSnapshot& rig,
    std::span<const std::array<std::byte,64>> ordinary,std::string_view asset,std::int64_t now) {
    if(t.preview||!ReloadShellControlFresh(t,now)||rig.identity.soldier!=t.owner.soldier||rig.identity.weak!=t.owner.weak||
       !rig.identity.evaluatedMatrices||rig.identity.count!=rig.names.size()||ordinary.size()!=rig.names.size()||
       rig.nativeEvaluated.size()!=rig.names.size())return {};
    const auto binding=BindBc2ReloadPresentation(rig,asset,SpasReloadMesh);if(!binding)return {};
    const std::array<std::uint32_t,1> indices{binding->shell};
    const auto collapsed=weapon_visibility_detail::CollapseWeightedPalette(ordinary,indices);if(!collapsed)return {};
    ReloadShellHidePlan p;p.rig=rig.identity;p.source=t;p.shell=binding->shell;p.ordinary.assign(ordinary.begin(),ordinary.end());p.hidden=*collapsed;return p;
}
ReloadPackedPaletteChoice SelectReloadShellPackedPalette(const ReloadShellHidePlan& p,const ReloadTracking* current,
    std::int64_t now,bool coherent)noexcept {
    if(p.ordinary.empty()||p.ordinary.size()>1024||p.ordinary.size()!=p.hidden.size()||p.shell>=p.ordinary.size())return {{},true};
    if(current&&coherent&&ReloadShellControlRetained(p.source,*current,now))return {p.hidden,false};
    return {p.ordinary,true};
}
ReloadRawContact BuildReloadRawContact(const ReloadTracking& t,const RigSnapshot& rig,std::string_view asset,
    const math::Matrix4& raw,const math::Matrix4& weapon,float units,std::int64_t now,const math::Matrix4* trackingBody) {
    ReloadRawContact out;
    if(!ReloadTrackingFresh(t,now)||rig.identity.soldier!=t.owner.soldier||rig.identity.weak!=t.owner.weak||
       !std::isfinite(units)||units<=0||!interaction::reload_insertion_detail::Rigid(raw)||
       !interaction::reload_insertion_detail::Rigid(weapon))return out;
    const auto binding=BindBc2ReloadPresentation(rig,asset,SpasReloadMesh);if(!binding)return out;
    out.valid=true;out.owner=t.owner;out.rig=rig.identity;out.rigFingerprint=binding->fingerprint;out.inputEvidence=t.inputEvidence;
    out.nativeShellVisible=std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),binding->shell)==rig.nativeHiddenLeaves.end()&&
        interaction::reload_insertion_detail::Rigid(rig.evaluatedWorld[binding->shell]);
    out.rawLeftWristWorldMeters=raw;out.weaponWorldMeters=weapon;
    if(trackingBody&&interaction::reload_insertion_detail::Rigid(*trackingBody)){out.trackingBodyWorldMeters=*trackingBody;for(unsigned axis=0;axis<3;++axis)out.trackingBodyWorldMeters.values[3][axis]/=units;}
    for(unsigned axis=0;axis<3;++axis){out.rawLeftWristWorldMeters.values[3][axis]/=units;out.weaponWorldMeters.values[3][axis]/=units;}
    return out;
}
std::optional<Bc2ReloadTargets> ResolveReloadPreviewTargets(const ReloadPreview& p,const ReloadTracking& t,const ReloadRawContact& c,std::int64_t now)noexcept {
    if(!ReloadPreviewFresh(p,t,now)||!c.valid||c.owner!=t.owner||c.rigFingerprint!=SpasReloadRig||
       c.inputEvidence.owner!=t.inputEvidence.owner||c.inputEvidence.sequence!=t.inputEvidence.sequence||
       c.inputEvidence.observedNs!=t.inputEvidence.observedNs||c.inputEvidence.deadlineNs!=t.inputEvidence.deadlineNs)return {};
    if(p.phase!=ReloadPreviewPhase::Carried)return p.targets;
    using namespace interaction;using namespace reload_insertion_detail;
    if(!Rigid(c.rawLeftWristWorldMeters)||!Rigid(c.weaponWorldMeters))return {};
    Bc2ReloadTargets v;v.identity={t.inputEvidence.owner,p.weaponClaim.token.item,p.shellClaim.token.item,t.owner.space};
    v.shellClaim=p.shellClaim.token;v.weaponClaim=p.weaponClaim.token;v.inputSequence=t.inputEvidence.sequence;
    v.observedNs=t.inputEvidence.observedNs;v.deadlineNs=std::min({t.inputEvidence.deadlineNs,p.shellClaim.deadlineNs,p.weaponClaim.deadlineNs,
        p.reserve.deadlineNs,p.selectedMeshes->deadlineNs,t.inputEvidence.observedNs+100000000});
    v.weaponFromLeftWristMeters=Multiply(c.rawLeftWristWorldMeters,*InverseRigid(c.weaponWorldMeters));
    v.weaponFromShellCenterMeters=Multiply(*InverseRigid(SpasReloadInsertionProfile().itemFromHand),v.weaponFromLeftWristMeters);
    if(v.deadlineNs<=now)return {};return v;
}
Bc2ReloadPalettePlan BuildReloadPreviewPalette(const ReloadPreview& p,const ReloadTracking& t,const ReloadRawContact& c,
    const RigSnapshot& rig,const Bc2ReloadPaletteSample& source,std::string_view asset,std::int64_t now) {
    Bc2ReloadPalettePlan rejected;rejected.reason=Bc2ReloadPaletteReason::PresentationRejected;
    rejected.presentationReason=Bc2ReloadPresentationReason::StaleOwnership;
    if(c.rig!=rig.identity)return rejected;
    const auto target=ResolveReloadPreviewTargets(p,t,c,now);if(!target)return rejected;
    const auto binding=BindBc2ReloadPresentation(rig,asset,SpasReloadMesh);if(!binding)return rejected;
    auto sample=source;auto& s=sample.presentation;
    s.enabled=true;s.assetName=asset;s.meshPath=SpasReloadMesh;s.identity=target->identity;s.inputSequence=target->inputSequence;
    s.nativeCycle=target->nativeCycle;s.nowNs=now;s.carried=p.phase==ReloadPreviewPhase::Carried;
    s.selectedMeshIdentityVerified=true;
    // Exact SPAS/rig binding above + authored single-entry shell skin + 77
    // native stereo frames matching this owned packed bone in BOTH sections.
    // This grants only this private bone operation, never draw suppression.
    s.sectionOwnershipVerified=true;s.shellSectionVisible=c.nativeShellVisible;
    // Native reload animation hides this shell even while all three firing
    // copies are held. Exact owned ammo keeps ONLY this bound private leaf
    // visible; original native matrices and all other hidden leaves survive.
    s.allowOwnedShellVisibility=true;
    return BuildBc2ReloadPalette(rig,*binding,sample,*target);
}
}
