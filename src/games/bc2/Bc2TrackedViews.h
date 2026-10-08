#pragma once
#include "Bc2Camera.h"
#include <cstring>
#include "fvr/runtime/FrameCoordinator.h"
namespace fvr::bc2 {
struct TrackedViews {std::array<std::array<RenderViewCopy,2>,2> camera;};
inline std::optional<TrackedViews> BuildTrackedViews(const std::array<RenderViewCopy,2>& prototype,const runtime::TrackingFrame& tracking,float units) noexcept {
    if(!tracking.focused||!tracking.headValid)return {};
    engine::FrostbiteCameraInput input{};std::memcpy(&input.transform,prototype[0].bytes.data()+0x50,64);
    std::memcpy(&input.nearPlane,prototype[0].bytes.data()+0x1c,4);std::memcpy(&input.farPlane,prototype[0].bytes.data()+0x20,4);input.worldUnitsPerMeter=units;
    const auto canonical=engine::CanonicalCamera(input);if(!canonical)return {};
    const auto head=math::ComposeRuntimeHeadWithLhCamera(canonical->camera,tracking.referenceHead,tracking.head,units);
    const auto left=math::ComposeRuntimeHeadWithLhCamera(canonical->camera,tracking.referenceHead,tracking.eyes[0],units);
    const auto right=math::ComposeRuntimeHeadWithLhCamera(canonical->camera,tracking.referenceHead,tracking.eyes[1],units);
    if(!head||!left||!right)return {};
    const auto envelope=math::EncloseStereoFrusta(*head,{*left,*right},tracking.fov,input.nearPlane,input.farPlane);if(!envelope)return {};
    const auto culling=BuildCullingViewCopy(prototype[1],*envelope);if(!culling)return {};
    TrackedViews result{};
    for(unsigned eye=0;eye<2;++eye){const auto render=BuildRenderViewCopy(prototype[0],eye?*right:*left,tracking.fov[eye],input.nearPlane,input.farPlane);if(!render)return {};result.camera[eye]={*render,*culling};}
    return result;
}
}
