#pragma once
// Seams between the BF4 adapter and the existing FrostbiteVR runtime pieces:
//   frames  -> FvrD3D11FrameBridge / FvrFrameChannel (shared texture to the XR host)
//   input   -> InputProtocol (controller snapshot + pointer events from the XR host)
// The XR host stays out-of-process (same host as BC2): crash isolation, one OpenXR
// implementation, one menu renderer. The host runs its laser pointer for every surface
// and sends the adapter only game-screen pointer events.
#include "../../engines/frostbite/Fb3CameraMath.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/math/Vec.h"

#include <cstdint>
#include <memory>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace fvr::bf4 {

struct SessionInfo;

struct FrameMeta {
    std::uint64_t frameIndex = 0;
    std::uint32_t width = 0, height = 0;
    int eye = -1;             // -1 mono (theater), 0 left, 1 right (stereo modes)
    bool menuVisible = false;
    // World lane (head-tracked view in the headset). The core asks for it; the platform adds
    // what the view override last submitted (Bf4WorldView.h decides when a frame carries
    // the lease's pose).
    bool world = false;
    std::uint32_t worldSettle = 2;
    std::uint64_t poseId = 0;
    std::uint32_t poseRun = 0;
    float renderFovY = 0;     // vertical FOV the tagged frames were rendered with (radians)
};

class IFrameSink {
public:
    virtual ~IFrameSink() = default;
    virtual bool Publish(ID3D11Device* device, ID3D11DeviceContext* context,
                         ID3D11Texture2D* backbuffer, const FrameMeta& meta) = 0;
    virtual void ReleaseSwapchainResources() = 0; // before ResizeBuffers
    // An adapter-owned image (the mod menu panel) on the same lane, same device as the game.
    virtual bool PublishTexture(ID3D11Texture2D*, std::uint32_t /*width*/, std::uint32_t /*height*/) { return false; }
    virtual std::string Describe() const { return {}; } // one status line (diagnostics)
};

struct GamePointer {
    bool active = false; // beam is on the game screen quad
    float u = 0.0f, v = 0.0f;
    bool down = false;
};

struct EyeView {
    math::Pose pose;           // tracking space (OpenXR xrLocateViews)
    frostbite::FovAngles fov;  // OpenXR XrFovf, radians
};

// The XR host's current eye-pair request (world lane), as handed to the camera: the head
// pose to render and the eyes' FOV tangents. poseId tags the pose through the view override.
struct WorldRequest {
    bool valid = false;
    std::uint64_t poseId = 0;
    math::Pose head;
    math::FovTangents fov[2];
};

struct ControllerSnapshot {
    bool valid = false;
    math::Pose head, leftHand, rightHand;
    EyeView eyes[2];
    float leftTrigger = 0, rightTrigger = 0, leftGrip = 0, rightGrip = 0;
    bool reloadButton = false;   // mapped by the host's action set
    bool menuButton = false;
    bool recenterRequested = false; // host's recenter gesture (both stick clicks held 1 s)
    GamePointer pointer;
    double timeSeconds = 0.0;
    // Full controller sample from the XR host (frame channel): sticks, triggers, grips,
    // buttons, poses. Drives the virtual gamepad; focused=false when nothing arrived.
    interaction::InputFrame controls;
    WorldRequest world;
};

class IControllerSource {
public:
    virtual ~IControllerSource() = default;
    virtual bool Poll(ControllerSnapshot& out) = 0;
};

// Real implementations live in Bf4FrameworkGlue.cpp behind FVR_BF4_FRAMEWORK_GLUE
// (links the existing FrostbiteVR channel/bridge libraries). Without it, frames are
// counted but not sent, and input is empty: enough for the emulated test.
std::unique_ptr<IFrameSink> CreateFrameSink(const SessionInfo& session);
std::unique_ptr<IControllerSource> CreateControllerSource(const SessionInfo& session);

} // namespace fvr::bf4
