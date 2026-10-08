#pragma once
#include "ControllerInput.h"
#include "TrackingMath.h"
#include "ArmIk.h"
namespace fvr::interaction {
struct TrackedRigOwner {
    std::uint64_t actor=0,generation=0,equipped=0,skeleton=0;
    // Equipment lifetime is independent of native pointer reuse and actor anatomy.
    std::uint64_t equipmentGeneration=0;
    bool operator==(const TrackedRigOwner&)const=default;
};
// Authored wrist relative to the weapon root, canonical row-vector metres.
// This is an explicit attachment source, never a claim about active animation.
struct AuthoredGripAttachment {math::Matrix4 rightInWeapon{};};
struct TrackedRigPose {math::Matrix4 left{},right{},weapon{};std::array<ArmAnchor,2> arms{};std::array<bool,2> tracked{},calibratedHands{};bool calibrated=false,weaponAttachmentPending=false,weaponAttachmentAuthored=false;std::optional<math::Matrix4> torso;};
// Keeps animation sway/reload from translating tracked hands horizontally.
// Vertical animation is retained for the adapter's native stance/crouch height.
// Actor translation and actual roomscale consumption remain authoritative.
// Compose translated, gravity-aligned physical anatomy from the tracking base.
// Relative HMD yaw affects shoulders/torso only; it never changes wrist targets.
// Recover an upright anatomical frame from bind head/shoulder landmarks.
std::optional<math::Matrix4> BindAnatomyFrame(math::Vec3 head,math::Vec3 leftShoulder,math::Vec3 rightShoulder);
std::optional<math::Matrix4> TrackedAimFrame(const InputFrame&,const math::Matrix4& trackingBody);
std::optional<math::Matrix4> PhysicalTorsoFrame(const InputFrame&,const math::Matrix4& trackingBody);
class TrackedBodyFrame {
public:
    std::optional<math::Matrix4> Update(const TrackedRigOwner&,const InputFrame&,
        const math::Matrix4& uprightAnimatedBody,math::Vec3 actorPosition,math::Vec3 consumedLocalMeters={});
private:
    TrackedRigOwner owner_{};std::uint64_t space_=0;float units_=0;
    math::Vec3 offset_{};
};
class TrackedRig {
public:
    // The adapter provides an upright body origin already compensated for actual
    // roomscale locomotion. Anatomy can supply a separate physical torso frame,
    // so shoulders follow roomscale/crouch. HMD yaw never rotates hand targets.
    std::optional<TrackedRigPose> Update(const TrackedRigOwner&,const InputFrame&,
        const math::Matrix4& body,const math::Matrix4& nativeLeft,
        const math::Matrix4& nativeRight,const math::Matrix4& nativeWeapon,
        const std::array<ArmAnchor,2>& nativeArms,std::optional<math::Matrix4> anatomyBody={},std::optional<math::Matrix4> nativeTorso={},std::optional<math::Matrix4> weaponOrientation={},std::optional<AuthoredGripAttachment> authoredGrip={});
    void Reset()noexcept {*this={};}
private:
    TrackedRigOwner owner_{};std::uint64_t space_=0;float units_=0;
    std::array<math::Matrix4,2> attachment_{};
    math::Matrix4 weaponAttachment_{},torsoAttachment_{};bool weaponReady_=false,torsoReady_=false,attachmentPending_=false,authoredAttachment_=false;
    math::Matrix4 attachmentCandidate_{};std::int64_t equipStarted_=0,attachmentStableSince_=0;
    std::array<bool,2> calibrated_{};
    std::array<math::Vec3,2> calibrationPosition_{};
    // Calibrated in the upright body frame, never the weapon aiming frame.
    std::array<ArmAnchor,2> anchors_{};
};
}
