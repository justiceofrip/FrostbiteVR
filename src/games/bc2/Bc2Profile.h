#pragma once
#include "fvr/engine/PeImage.h"
#include <optional>
#include <span>
namespace fvr::bc2 {
struct DiscoveryProfile {
    std::uint32_t preferredBase=0,imageSize=0,rendererGlobal=0,gameRendererGlobal=0;
    std::uint32_t frame=0,dispatch=0,present=0,presentWrapper=0,rendererVtable=0;
};
// Derives addresses from unique executable signatures and code/table links.
// This is read-only discovery evidence, NOT authorization to hook a frame twice.
struct RenderPathCandidates {
    std::uint32_t subsystemVtable=0,subsystemDraw=0,worldRendererVtable=0,worldRender=0;
    std::uint32_t prepareView=0,drawView=0,updateViewCache=0;
};
struct VisibilityPathCandidates {
    std::uint32_t worldUpdate=0,prepareVisibility=0;
};
std::optional<VisibilityPathCandidates> DiscoverVisibilityPath(std::span<const std::byte>,const engine::PeImage&);
struct ViewLifecycleCandidates {
    std::uint32_t requestVtable=0,createView=0,constructor=0,addRef=0,release=0;
    std::uint32_t setActive=0,deletingDestructor=0,destructor=0;
};
std::optional<ViewLifecycleCandidates> DiscoverViewLifecycle(std::span<const std::byte>,const engine::PeImage&);
struct ViewCallbackCandidates {std::uint32_t rememberMain=0,registerView=0,unregisterView=0;};
std::optional<ViewCallbackCandidates> DiscoverViewCallbacks(std::span<const std::byte>,const engine::PeImage&);
struct ViewInitializationCandidates {std::uint32_t rebuild=0,parentGetter=0,refreshRegistered=0;};
std::optional<ViewInitializationCandidates> DiscoverViewInitialization(std::span<const std::byte>,const engine::PeImage&);
// Verified native context camera setter; observation does not grant write capability.
std::optional<std::uint32_t> DiscoverContextCamera(std::span<const std::byte>,const engine::PeImage&);
struct ProjectionOverrideCandidates {std::uint32_t setter=0,meshCaller=0,terrainCaller=0;};
std::optional<ProjectionOverrideCandidates> DiscoverProjectionOverrides(std::span<const std::byte>,const engine::PeImage&);
struct CameraCacheCandidates {
    std::uint32_t getView=0,getProjection=0,getFrustum=0;
    std::uint32_t updateView=0,updateProjection=0,updateFrustum=0;
};
std::optional<CameraCacheCandidates> DiscoverCameraCaches(std::span<const std::byte>,const engine::PeImage&);
struct ViewLayoutCandidates {
    std::uint32_t vtable=0,setPrimary=0,setSecondary=0,copyRenderView=0;
    std::uint32_t ownerRequestOffset=0,primaryOffset=0,secondaryOffset=0;
    std::uint32_t thirdOffset=0,fourthOffset=0,activeOffset=0,viewportOffset=0;
};
std::optional<ViewLayoutCandidates> DiscoverViewLayout(std::span<const std::byte>,const engine::PeImage&);
// Code/ownership relationships only. No replay-safety capability is granted.
std::optional<RenderPathCandidates> DiscoverRenderPath(std::span<const std::byte>,const engine::PeImage&);
std::optional<DiscoveryProfile> DiscoverProfile(std::span<const std::byte>,const engine::PeImage&);
}
namespace fvr::bc2 {
struct GameplayCandidates {
    std::uint32_t contextObject=0,contextGetter=0,managerVtable=0;
    std::uint32_t localPlayerGetter=0,soldierGetter=0,playerInputUpdate=0,inputGather=0,inputRouterVtable=0;
    std::uint32_t localPlayerOffset=0,soldierWeakOffset=0,inputCacheOffset=0;
};
// Evidence for input observation; does not grant movement, weapon or collision writes.
std::optional<GameplayCandidates> DiscoverGameplay(std::span<const std::byte>,const engine::PeImage&);
}

namespace fvr::bc2 {
struct InputBindingCandidates {
    GameplayCandidates gameplay{};
    std::uint32_t cacheConstructor=0,cacheVtable=0,buttonSetter=0,floatSetter=0;
    std::uint32_t controlledGetter=0,attachedPredicate=0,entryActions=0;
};
std::optional<InputBindingCandidates> DiscoverInputBinding(std::span<const std::byte>,const engine::PeImage&);
}

namespace fvr::bc2 {
struct AimCandidates {std::uint32_t weaponGetter=0,indexGetter=0,aimGetter=0,yawGetter=0,inputPrepare=0,absoluteYawSetter=0,angleCopy=0,aimerYawSetter=0,aimerPitchSetter=0;};
std::optional<AimCandidates> DiscoverAiming(std::span<const std::byte>,const engine::PeImage&);
}

namespace fvr::bc2 {
struct ViewAnchorCandidates {std::uint32_t getter=0,setter=0,objectPrepare=0,projectionCaller=0;};
std::optional<ViewAnchorCandidates> DiscoverViewAnchor(std::span<const std::byte>,const engine::PeImage&);
}

namespace fvr::bc2 {
// First-person animation/effect pose observation only; no write capability.
struct FirstPersonPoseCandidates {std::uint32_t animationUpdate=0,worldBuilder=0,rootSetter=0;};
std::optional<FirstPersonPoseCandidates> DiscoverFirstPersonPose(std::span<const std::byte>,const engine::PeImage&);
}

namespace fvr::bc2 {
struct BodyPositionCandidates {std::uint32_t getter=0,fallback=0;};
std::optional<BodyPositionCandidates> DiscoverBodyPosition(std::span<const std::byte>,const engine::PeImage&);
}

namespace fvr::bc2 {
// Read-only native animation phase observation. Not an animation write gate.
struct RigCandidates {
    std::uint32_t animationGetter=0,animationUpdate=0,evaluate=0,postEvaluate=0;
    std::uint32_t weaponWorld=0,boneWorld=0,worldThunk=0,worldIndex=0;
    std::uint32_t skinSelect=0,skinGetter=0,paletteGetter=0,skinThunk=0,skinData=0;
};
std::optional<RigCandidates> DiscoverRig(std::span<const std::byte>,const engine::PeImage&);
}

namespace fvr::bc2 {
struct RigConsumerCandidates {std::uint32_t prepare=0,getterA=0,getterB=0,pack=0,getCallerA=0,getCallerB=0,packCallerA=0,packCallerB=0;};
std::optional<RigConsumerCandidates> DiscoverRigConsumer(std::span<const std::byte>,const engine::PeImage&);
}

namespace fvr::bc2 {
// Pass-through server shot-transform observation; no firing write capability.
struct FireOriginCandidates {
 std::uint32_t builder=0,serverShoot=0,callerA=0,callerB=0,serverControlledGetter=0;
 std::uint32_t serverContextGetter=0,serverContext=0,serverManagerConstructor=0,serverManagerVtable=0;
 std::uint32_t serverPlayerCreate=0,serverPlayerConstructor=0,playerConstructor=0;
 std::uint32_t clientShoot=0,matrixCopy=0,clientCopyA=0,clientCopyB=0;
 std::uint32_t compose=0,clientCompose=0,serverCompose=0,spreadSeed=0,randomSeed=0;
};
std::optional<FireOriginCandidates> DiscoverFireOrigin(std::span<const std::byte>,const engine::PeImage&);
}
