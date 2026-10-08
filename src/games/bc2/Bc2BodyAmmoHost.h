#pragma once
#include "Bc2BodyAmmo.h"
#include "Bc2BodyAmmoRenderer.h"
#include "fvr/graphics/BodyPropFrame.h"
#include <algorithm>
namespace fvr::bc2 {
// Collapse the original typed dependency interval for render-only IPC. This
// never extends any selected-mesh, native-family, ammo, input or claim lease.
inline std::optional<graphics::BodyPropInstance> BodyAmmoHostInstance(
    const BodyAmmoRenderSource& before,const BodyAmmoRenderSource& current,std::int64_t now)noexcept {
    if(!BodyAmmoSourceRetained(before,current,now))return {};
    graphics::BodyPropInstance result{before.geometry,before.prop.partWorld,0,INT64_MAX};
    const auto& owner=before.prop.source.input.owner;
    result.actorGeneration=owner.actorGeneration;result.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Ammo,owner.equipGeneration);result.spaceGeneration=owner.space;
    const auto bound=[&](std::int64_t observed,std::int64_t deadline){
        result.observedNs=(std::max)(result.observedNs,observed);
        result.deadlineNs=(std::min)(result.deadlineNs,deadline);
    };
    for(const auto* s:{&before,&current}){
        const auto& a=*s->authority;const auto& v=a.visual;
        bound(v.input.observedNs,v.input.deadlineNs);bound(v.source.observedNs,v.source.deadlineNs);
        result.deadlineNs=(std::min)(result.deadlineNs,v.gun.deadlineNs);
        if(a.magazine){const auto& m=*a.magazine;
            bound(m.inputEvidence.observedNs,m.inputEvidence.deadlineNs);
            bound(m.reserve.observedNs,m.reserve.deadlineNs);bound(m.selected->observedNs,m.selected->deadlineNs);
            bound(m.family.observedNs,m.family.deadlineNs);
            if(m.family.carried)bound(m.family.carried->observedNs,m.family.carried->deadlineNs);
        }else{const auto& shell=*a.shell;bound(shell.reserve.observedNs,shell.reserve.deadlineNs);bound(shell.meshes->observedNs,shell.meshes->deadlineNs);
            if(shell.heldCycle)bound(shell.heldCycle->observedNs,shell.heldCycle->deadlineNs);
        }
    }
    return graphics::BodyPropFresh(result,now)?std::optional{result}:std::nullopt;
}
// Inverse of engine::NativeEye's handedness conversion. Native +0x220/+0x2e0
// matrices are copied after that exact eye's Draw, never from a dirty prototype.
inline std::optional<graphics::BodyPropEye> CanonicalBodyPropEye(
    math::Matrix4 nativeView,math::Matrix4 nativeProjection)noexcept {
    for(unsigned n=0;n<4;++n){nativeView.values[2][n]=-nativeView.values[2][n];nativeView.values[n][2]=-nativeView.values[n][2];}
    for(float& f:nativeProjection.values[2])f=-f;
    math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;
    if(!graphics::RigidPropClipTransform(identity,nativeView,nativeProjection))return {};
    graphics::BodyPropEye out;out.view=nativeView;out.projection=nativeProjection;return out;
}
} // namespace fvr::bc2
