#include "Bc2BeltAmmo.h"
#include <algorithm>
namespace fvr::bc2 {
using namespace interaction;
bool SpasBeltAmmoFresh(const BeltAmmoTracking& t,std::int64_t now)noexcept {
    const auto& r=t.reserve;
    bool available=r.allThreeIdle;
    if(t.heldCycle){const auto& h=*t.heldCycle;
        available=h.nativeBindingVerified&&h.allThreeHeld&&h.cycle&&h.sequence&&h.identity==r.identity&&
            h.observedNs>0&&h.observedNs<=now&&h.deadlineNs>now&&h.deadlineNs-h.observedNs<=200000000&&
            h.loaded==r.loaded&&h.reserve==r.reserve&&h.capacity==r.capacity&&h.loaded<h.capacity&&h.reserve>0;
    }
    if(!AmmoSupplyVisualFresh(t.visual,now)||!available||t.reserve.identity.owner!=t.owner||!t.meshes||
        !t.meshes->sequence||t.meshes->observedNs<=0||t.meshes->observedNs>now||t.meshes->deadlineNs<=now||
        t.meshes->deadlineNs-t.meshes->observedNs>250000000)return false;
    const auto* selected=FindSelectedMesh(*t.meshes,t.owner,SelectedMeshKind::Spas12,now);
    if(!selected||std::find(selected->assetPath.begin(),selected->assetPath.end(),'\0')==selected->assetPath.end()||
        std::string_view(selected->assetPath.data())!=SpasReloadMesh)return false;
    Bc2ReloadInteractionSample sample;
    sample.insertion.identity={t.visual.input.owner,t.visual.source.identity.weapon,{},t.visual.source.identity.trackingEpoch};
    sample.insertion.nowNs=now;sample.assetName=SpasReloadAsset;sample.meshPath=SpasReloadMesh;
    sample.rigFingerprint=SpasReloadRig;sample.selectedMeshIdentityVerified=true;
    const auto source=SpasAmmoSupplySource(sample,t.reserve);
    return source&&*source==t.visual.source;
}
bool SpasBeltAmmoRetained(const BeltAmmoTracking& a,const BeltAmmoTracking& b,std::int64_t now)noexcept {
    const bool sameMode=bool(a.heldCycle)==bool(b.heldCycle);
    const bool sameCycle=!a.heldCycle||(b.heldCycle&&a.heldCycle->identity==b.heldCycle->identity&&
        a.heldCycle->cycle==b.heldCycle->cycle&&a.heldCycle->sequence<=b.heldCycle->sequence&&
        a.heldCycle->observedNs<=b.heldCycle->observedNs&&
        (a.heldCycle->sequence!=b.heldCycle->sequence||
         (a.heldCycle->observedNs==b.heldCycle->observedNs&&a.heldCycle->deadlineNs==b.heldCycle->deadlineNs)));
    return sameMode&&sameCycle&&SpasBeltAmmoFresh(a,now)&&SpasBeltAmmoFresh(b,now)&&a.owner==b.owner&&
        AmmoSupplyVisualRetained(a.visual,b.visual,now)&&a.reserve.identity==b.reserve.identity&&
        a.reserve.loaded==b.reserve.loaded&&a.reserve.capacity==b.reserve.capacity&&
        a.meshes->owner==b.meshes->owner&&a.meshes->weaponData==b.meshes->weaponData&&
        a.meshes->soleConfiguredArray==b.meshes->soleConfiguredArray&&a.meshes->states==b.meshes->states&&
        a.meshes->sequence<=b.meshes->sequence;
}
namespace belt_ammo_detail {
std::optional<std::vector<std::array<std::byte,64>>> PlaceIdleShell(const RigSnapshot& rig,
    const Bc2ReloadPresentationBinding& binding,std::span<const std::array<std::byte,64>> ordinary,
    const math::Matrix4& center,float units) {
    if(ordinary.size()!=rig.names.size()||rig.nativeEvaluated.size()!=ordinary.size()||
        !std::isfinite(units)||units<=0||!reload_insertion_detail::Rigid(center))return {};
    // Idle only: never move a native reload/eject prop which is already visible.
    if(!bc2_reload_detail::KnownHiddenShell(rig,binding))return {};
    const auto bone=SpasShellBoneAtCenter(center,units);if(!bone)return {};
    const std::array<BoneWrite,1> writes{{{binding.shell,*bone}}};
    const auto plan=bc2_reload_detail::BuildShellVisibilityPalette(rig,binding,writes,binding.shell);if(!plan)return {};
    auto out=std::vector<std::array<std::byte,64>>(ordinary.begin(),ordinary.end());
    if(plan->edits.size()!=1||plan->edits.front().index!=binding.shell)return {};
    out[binding.shell]=plan->edits.front().after;return out;
}
}
std::optional<BeltAmmoPalette> BuildSpasBeltAmmoPalette(const BeltAmmoTracking& t,const RigSnapshot& rig,
    std::span<const std::array<std::byte,64>> ordinary,const InputFrame& input,
    const math::Matrix4& eyeBase,std::string_view asset,std::int64_t now) {
    if(!SpasBeltAmmoFresh(t,now)||asset!=SpasReloadAsset||rig.identity.soldier!=t.owner.soldier||
        rig.identity.weak!=t.owner.weak||!rig.identity.evaluatedMatrices||rig.identity.count!=rig.names.size())return {};
    const auto binding=BindBc2ReloadPresentation(rig,asset,SpasReloadMesh);if(!binding)return {};
    const auto center=AmmoSupplyAnchorWorld(input,eyeBase,t.visual);if(!center)return {};
    const auto posed=belt_ammo_detail::PlaceIdleShell(rig,*binding,ordinary,*center,input.worldUnitsPerMeter);if(!posed)return {};
    return BeltAmmoPalette{t,rig.identity,binding->shell,{ordinary.begin(),ordinary.end()},*posed};
}
BeltAmmoPaletteChoice SelectSpasBeltOverlay(const BeltAmmoPalette& p,const BeltAmmoTracking* current,
    std::int64_t now,bool coherent,std::span<const std::array<std::byte,64>> currentBase,bool hidden)noexcept {
    if(currentBase.empty()||currentBase.size()!=p.ordinary.size())return {};
    const auto chosen=SelectSpasBeltAmmoPalette(p,current,now,coherent);
    const auto fallback=p.source.heldCycle?currentBase:std::span<const std::array<std::byte,64>>(p.ordinary);
    if(chosen.fallback||(p.source.heldCycle&&!hidden))return {fallback,true};
    return chosen;
}
BeltAmmoPaletteChoice SelectSpasBeltAmmoPalette(const BeltAmmoPalette& p,const BeltAmmoTracking* current,
    std::int64_t now,bool coherent)noexcept {
    if(p.ordinary.empty()||p.ordinary.size()>1024||p.posed.size()!=p.ordinary.size()||p.shell>=p.posed.size())return {};
    if(current&&coherent&&SpasBeltAmmoRetained(p.source,*current,now))return {p.posed,false};
    return {p.ordinary,true};
}
}
