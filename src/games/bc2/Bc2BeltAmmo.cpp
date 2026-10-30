#include "Bc2BeltAmmo.h"
#include "Bc2ShotgunEquipment.h"
#include <algorithm>
namespace fvr::bc2 {
using namespace interaction;
bool ShotgunBeltAmmoFresh(const BeltAmmoTracking& t,std::int64_t now)noexcept {
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
    const auto* equipment=KnownShotgunEquipment(t.equipment);
    if(!equipment||!ShotgunEquipmentSelected(*t.meshes,t.owner,equipment,now))return false;
    Bc2ReloadInteractionSample sample;
    sample.insertion.identity={t.visual.input.owner,t.visual.source.identity.weapon,{},t.visual.source.identity.trackingEpoch};
    sample.insertion.nowNs=now;sample.assetName=equipment->native->config->assetName;sample.meshPath=equipment->meshPath;
    sample.rigFingerprint=equipment->rigFingerprint;sample.selectedMeshIdentityVerified=true;
    sample.equipment=equipment;sample.configurationPath=equipment->native->config->assetPath;
    const auto source=ShotgunAmmoSupplySource(sample,t.reserve);
    return source&&*source==t.visual.source;
}
bool ShotgunBeltAmmoRetained(const BeltAmmoTracking& a,const BeltAmmoTracking& b,std::int64_t now)noexcept {
    const bool sameMode=bool(a.heldCycle)==bool(b.heldCycle);
    const bool sameCycle=!a.heldCycle||(b.heldCycle&&a.heldCycle->identity==b.heldCycle->identity&&
        a.heldCycle->cycle==b.heldCycle->cycle&&a.heldCycle->sequence<=b.heldCycle->sequence&&
        a.heldCycle->observedNs<=b.heldCycle->observedNs&&
        (a.heldCycle->sequence!=b.heldCycle->sequence||
         (a.heldCycle->observedNs==b.heldCycle->observedNs&&a.heldCycle->deadlineNs==b.heldCycle->deadlineNs)));
    return KnownShotgunEquipment(a.equipment)==KnownShotgunEquipment(b.equipment)&&sameMode&&sameCycle&&ShotgunBeltAmmoFresh(a,now)&&ShotgunBeltAmmoFresh(b,now)&&a.owner==b.owner&&
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
    const auto bone=ShotgunShellBoneAtCenter(binding.equipment,center,units);if(!bone)return {};
    const std::array<BoneWrite,1> writes{{{binding.shell,*bone}}};
    const auto plan=bc2_reload_detail::BuildShellVisibilityPalette(rig,binding,writes,binding.shell);if(!plan)return {};
    auto out=std::vector<std::array<std::byte,64>>(ordinary.begin(),ordinary.end());
    if(plan->edits.size()!=1+binding.hiddenDescendants.size()||plan->edits.front().index!=binding.shell)return {};
    for(const auto& edit:plan->edits)out[edit.index]=edit.after;return out;
}
}
std::optional<BeltAmmoPalette> BuildShotgunBeltAmmoPalette(const BeltAmmoTracking& t,const RigSnapshot& rig,
    std::span<const std::array<std::byte,64>> ordinary,const InputFrame& input,
    const math::Matrix4& eyeBase,std::string_view asset,std::int64_t now) {
    const auto* equipment=KnownShotgunEquipment(t.equipment);
    if(!equipment||!ShotgunBeltAmmoFresh(t,now)||asset!=equipment->native->config->assetName||rig.identity.soldier!=t.owner.soldier||
        rig.identity.weak!=t.owner.weak||!rig.identity.evaluatedMatrices||rig.identity.count!=rig.names.size())return {};
    const auto binding=BindBc2ReloadPresentation(rig,asset,equipment->meshPath,equipment);if(!binding)return {};
    const auto center=AmmoSupplyAnchorWorld(input,eyeBase,t.visual);if(!center)return {};
    const auto posed=belt_ammo_detail::PlaceIdleShell(rig,*binding,ordinary,*center,input.worldUnitsPerMeter);if(!posed)return {};
    return BeltAmmoPalette{t,rig.identity,binding->shell,{ordinary.begin(),ordinary.end()},*posed};
}
BeltAmmoPaletteChoice SelectShotgunBeltOverlay(const BeltAmmoPalette& p,const BeltAmmoTracking* current,
    std::int64_t now,bool coherent,std::span<const std::array<std::byte,64>> currentBase,bool hidden)noexcept {
    if(currentBase.empty()||currentBase.size()!=p.ordinary.size())return {};
    const auto chosen=SelectShotgunBeltAmmoPalette(p,current,now,coherent);
    const auto fallback=p.source.heldCycle?currentBase:std::span<const std::array<std::byte,64>>(p.ordinary);
    if(chosen.fallback||(p.source.heldCycle&&!hidden))return {fallback,true};
    return chosen;
}
BeltAmmoPaletteChoice SelectShotgunBeltAmmoPalette(const BeltAmmoPalette& p,const BeltAmmoTracking* current,
    std::int64_t now,bool coherent)noexcept {
    if(p.ordinary.empty()||p.ordinary.size()>1024||p.posed.size()!=p.ordinary.size()||p.shell>=p.posed.size())return {};
    if(current&&coherent&&ShotgunBeltAmmoRetained(p.source,*current,now))return {p.posed,false};
    return {p.ordinary,true};
}
bool SpasBeltAmmoFresh(const BeltAmmoTracking& t,std::int64_t now)noexcept {
    return KnownShotgunEquipment(t.equipment)==DefaultSpasShotgunEquipment()&&ShotgunBeltAmmoFresh(t,now);
}
bool SpasBeltAmmoRetained(const BeltAmmoTracking& a,const BeltAmmoTracking& b,std::int64_t now)noexcept {
    return SpasBeltAmmoFresh(a,now)&&SpasBeltAmmoFresh(b,now)&&ShotgunBeltAmmoRetained(a,b,now);
}
std::optional<BeltAmmoPalette> BuildSpasBeltAmmoPalette(const BeltAmmoTracking& t,const RigSnapshot& rig,
    std::span<const std::array<std::byte,64>> ordinary,const InputFrame& input,const math::Matrix4& eyeBase,std::string_view asset,std::int64_t now) {
    if(KnownShotgunEquipment(t.equipment)!=DefaultSpasShotgunEquipment())return {};
    return BuildShotgunBeltAmmoPalette(t,rig,ordinary,input,eyeBase,asset,now);
}
BeltAmmoPaletteChoice SelectSpasBeltAmmoPalette(const BeltAmmoPalette& p,const BeltAmmoTracking* current,std::int64_t now,bool coherent)noexcept {
    return SelectShotgunBeltAmmoPalette(p,current,now,coherent&&KnownShotgunEquipment(p.source.equipment)==DefaultSpasShotgunEquipment());
}
BeltAmmoPaletteChoice SelectSpasBeltOverlay(const BeltAmmoPalette& p,const BeltAmmoTracking* current,std::int64_t now,bool coherent,
    std::span<const std::array<std::byte,64>> base,bool hidden)noexcept {
    return SelectShotgunBeltOverlay(p,current,now,coherent&&KnownShotgunEquipment(p.source.equipment)==DefaultSpasShotgunEquipment(),base,hidden);
}

}
