#pragma once
#include "Bc2ReloadDrawCapture.h"
#include "Bc2ReticleAperture.h"
#include "Bc2SelectedMeshes1p.h"
#include <algorithm>
#include <cmath>
namespace fvr::bc2 {
// Diagnostic evidence only. Configured mesh + matching matrix never proves
// that the selected native skin instance actually issued this GPU draw.
enum ReticleObservationMissing:unsigned {
    ReticleGeometry=1,ReticleDrawScope=2,ReticleProducer=4,ReticleSourceLease=8,
    ReticleSelected=16,ReticleEquipment=32,ReticlePackedOptic=64,
    ReticleConstantsIncomplete=128,ReticleConstantMatch=256,ReticleSkinMatrix=512,
    ReticleGpuInstanceAssociation=1024
};
struct ReticleDrawObservation {
    unsigned missing=ReticleGpuInstanceAssociation;
    math::ApertureVisibility visibility=math::ApertureVisibility::Invalid;
    math::Vec3 eyeBind{};bool evaluated=false,hiddenCandidate=false;
    // There is deliberately no conversion to draw-suppression authority.
};
inline bool ReticleSelectedValid(const SelectedMeshesSnapshot& s,std::int64_t now)noexcept {
    const auto& o=s.owner;
    if(!o.player||!o.soldier||!o.weak||!o.weapon||!o.actorGeneration||!o.equipGeneration||!o.space||
       !s.sequence||s.observedNs<=0||now<s.observedNs||now>=s.deadlineNs||s.deadlineNs-s.observedNs>250000000||
       !s.weaponData||!s.inventory||!s.stateTypeInfo||!s.meshTypeInfo||s.stateCount!=1||
       !s.states[0].state||!s.soleConfiguredArray||s.states[0].array!=s.soleConfiguredArray)return false;
    const auto* asset=FindSelectedMesh(s,o,SelectedMeshKind::Acog4x,now);
    if(!asset||!asset->address||!asset->typeInfo||!asset->namePointer)return false;
    const auto end=std::find(asset->assetPath.begin(),asset->assetPath.end(),'\0');
    return end!=asset->assetPath.end()&&std::string_view(asset->assetPath.data(),std::size_t(end-asset->assetPath.begin()))==AcogReticleApertureProfile().mesh;
}
inline bool ReticleSelectedRetained(const SelectedMeshesSnapshot& original,const SelectedMeshesSnapshot& current,
    std::int64_t now)noexcept {
    return ReticleSelectedValid(original,now)&&ReticleSelectedValid(current,now)&&original.owner==current.owner&&
        current.sequence>=original.sequence&&original.weaponData==current.weaponData&&original.weaponName==current.weaponName&&
        original.inventory==current.inventory&&original.selectedSlot==current.selectedSlot&&
        original.stateTypeInfo==current.stateTypeInfo&&original.meshTypeInfo==current.meshTypeInfo&&
        original.soleConfiguredArray==current.soleConfiguredArray&&original.states==current.states;
}
inline ReticleDrawObservation ObserveReticleAperture(const ReloadDrawFrameEvidence& frame,const ReloadReticleDrawCurrent& draw,
    bool exactGeometry,bool allConstantsCaptured,unsigned packedOpticMatches)noexcept {
    ReticleDrawObservation r;const auto& p=frame.producer;const auto now=draw.nowNs;
    if(!exactGeometry)r.missing|=ReticleGeometry;
    if(!draw.eyeWorldValid||!draw.world||!draw.request||!draw.view||!draw.nativeFrame||draw.eye>1||
       draw.world!=frame.world||draw.request!=frame.request||draw.view!=frame.view||draw.nativeFrame!=frame.nativeFrame||draw.eye!=frame.eye||
       now<frame.nowNs||!std::isfinite(draw.eyeCanonicalLh.x)||!std::isfinite(draw.eyeCanonicalLh.y)||!std::isfinite(draw.eyeCanonicalLh.z))r.missing|=ReticleDrawScope;
    if(!p.exactRequestAssociation||p.request!=frame.request||p.nativeFrame!=frame.nativeFrame||
       !p.actor||!p.weak||!p.weapon||!p.ownerGeneration||!p.space||!p.inputGeneration||!p.rigPose||
       p.rigFingerprint!=0xa7f219a1426216abull)r.missing|=ReticleProducer;
    if(p.observedNs<=0||now<p.observedNs||now>=p.deadlineNs||p.deadlineNs-p.observedNs>250000000)r.missing|=ReticleSourceLease;
    if(!p.opticSelected||!draw.selected||!ReticleSelectedRetained(*p.opticSelected,*draw.selected,now))r.missing|=ReticleSelected;
    else {const auto& o=p.opticSelected->owner;
        if(o.soldier!=p.actor||o.weak!=p.weak||o.weapon!=p.weapon||o.actorGeneration!=p.ownerGeneration||o.space!=p.space||
           p.opticSelected->sequence>p.inputGeneration)r.missing|=ReticleSelected;}
    if(!p.physicalEquipmentGeneration||p.physicalEquipmentGeneration!=draw.physicalEquipmentGeneration||p.actor!=draw.actor||
       p.weak!=draw.weak||p.weapon!=draw.weapon||p.ownerGeneration!=draw.ownerGeneration||p.space!=draw.space)r.missing|=ReticleEquipment;
    if(!p.packedOpticValid)r.missing|=ReticlePackedOptic;
    if(!allConstantsCaptured)r.missing|=ReticleConstantsIncomplete;
    if(packedOpticMatches!=1)r.missing|=ReticleConstantMatch;
    const auto eye=p.packedOpticValid?AcogEyeInBindSpace(p.packedOptic,draw.eyeCanonicalLh):std::nullopt;
    if(!eye)r.missing|=ReticleSkinMatrix;
    if(r.missing==ReticleGpuInstanceAssociation&&eye){const auto& profile=AcogReticleApertureProfile();
        r.eyeBind=*eye;r.visibility=math::EvaluateOpticAperture(profile.apertures,profile.reticle,*eye).visibility;
        r.evaluated=r.visibility!=math::ApertureVisibility::Invalid;r.hiddenCandidate=r.visibility==math::ApertureVisibility::Hidden;
    }
    return r;
}
}
