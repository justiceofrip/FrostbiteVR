// Roadmap M4 policy: body frame + holsters. Pure VR logic, no game binding.
#include "TestHarness.h"

#include "fvr/interaction/Holsters.h"

using namespace fvr;
using namespace fvr::interaction;
using fvrtest::Near;

namespace {
math::Pose Head(float yawRad = 0.0f) {
    math::Pose p;
    p.position = {0, 1.65f, 0};
    p.orientation = math::FromAxisAngle({0, 1, 0}, yawRad);
    return p;
}
math::Pose At(math::Vec3 p) {
    math::Pose pose;
    pose.position = p;
    return pose;
}
} // namespace

TEST_CASE(BodyFrameFollowsHeadOutsideDeadzone) {
    BodyFrameEstimator est;
    const math::Pose idle = At({0, 1.0f, -0.3f});
    BodyFrame f = est.Update(Head(), idle, idle, 0.0);
    const auto hipR = f.anchors[static_cast<int>(BodyAnchor::HipRight)].position;
    const auto pelvis = f.anchors[static_cast<int>(BodyAnchor::Pelvis)].position;
    CHECK(f.valid && hipR.x > 0.1f && hipR.y < 1.2f && Near(hipR.y, pelvis.y));
    CHECK(f.anchors[static_cast<int>(BodyAnchor::BackRight)].position.z > 0.0f); // behind the player

    // Glancing 30 degrees: the torso stays put.
    est.Update(Head(0.52f), idle, idle, 0.5);
    CHECK(Near(est.BodyYaw(), 0.0f));
    // Turning 90 degrees and holding: the torso catches up to within the deadzone.
    for (int i = 1; i <= 90; ++i) est.Update(Head(1.5708f), idle, idle, 0.5 + i / 90.0);
    CHECK(Near(est.BodyYaw(), 1.5708f - 0.65f, 1e-3f));

    // Looking straight down at the belt must not spin the torso.
    BodyFrameEstimator down;
    math::Pose look = Head();
    look.orientation = math::FromAxisAngle({1, 0, 0}, -1.5f);
    down.Update(look, idle, idle, 0.0);
    CHECK(std::fabs(down.BodyYaw()) < 1e-3f);
}

TEST_CASE(HolsterDrawStowPersistentOwnership) {
    BodyFrameEstimator est;
    const math::Pose idle = At({0.4f, 1.3f, -0.4f});
    const BodyFrame body = est.Update(Head(), idle, idle, 0.0);
    HolsterSystem h(DefaultHolsterLayout());
    const int hipR = h.FindSlot("hip_right"), hipL = h.FindSlot("hip_left");
    const math::Pose atHipR = At(h.SlotWorldPosition(body, hipR));
    const math::Pose atHipL = At(h.SlotWorldPosition(body, hipL));

    // Grip at the right hip draws the secondary into the right hand.
    auto r = h.Update(body, idle, false, atHipR, true);
    CHECK(r.event == HolsterEvent::Draw && r.slotId == "hip_right" && !r.leftHand);
    CHECK(h.HeldSlot(false) == hipR && !h.Occupied(hipR));
    // Repeated packets with the grip held change nothing (AGENTS.md persistent state).
    for (int i = 0; i < 5; ++i) CHECK(h.Update(body, idle, false, atHipR, true).event == HolsterEvent::None);
    // The other hand cannot draw from the now-empty slot.
    CHECK(h.Update(body, atHipR, true, atHipR, true).event == HolsterEvent::None);
    h.Update(body, idle, false, atHipR, true);

    // Left hand draws a grenade, then lets go at the (empty) secondary holster: wrong
    // category, so it goes home to the left hip instead.
    r = h.Update(body, atHipL, true, atHipR, true);
    CHECK(r.event == HolsterEvent::Draw && r.slotId == "hip_left" && r.leftHand);
    r = h.Update(body, atHipR, false, atHipR, true);
    CHECK(r.event == HolsterEvent::Stow && r.slotId == "hip_left" && h.Occupied(hipL) && !h.Occupied(hipR));

    // Releasing the secondary at its holster stows it there.
    r = h.Update(body, idle, false, atHipR, false);
    CHECK(r.event == HolsterEvent::Stow && r.slotId == "hip_right" && h.Occupied(hipR) && h.HeldSlot(false) < 0);
}

TEST_CASE(HolsterSimultaneousEventsAreQueuedNotDropped) {
    BodyFrameEstimator est;
    const math::Pose idle = At({0.4f, 1.3f, -0.4f});
    const BodyFrame body = est.Update(Head(), idle, idle, 0.0);
    HolsterSystem h(DefaultHolsterLayout());
    const math::Pose atHipR = At(h.SlotWorldPosition(body, h.FindSlot("hip_right")));
    const math::Pose atHipL = At(h.SlotWorldPosition(body, h.FindSlot("hip_left")));
    const auto first = h.Update(body, atHipL, true, atHipR, true);
    const auto second = h.Update(body, atHipL, true, atHipR, true);
    CHECK(first.event == HolsterEvent::Draw && second.event == HolsterEvent::Draw);
    CHECK(first.leftHand != second.leftHand);
    CHECK(h.Update(body, atHipL, true, atHipR, true).event == HolsterEvent::None);

    // Death: hands empty, loadout restocked; a grip held through it must be released first.
    h.OnLifetimeTransition();
    CHECK(h.HeldSlot(true) < 0 && h.HeldSlot(false) < 0 && h.Occupied(h.FindSlot("hip_right")));
    CHECK(h.Update(body, atHipL, true, atHipR, true).event == HolsterEvent::None);
}

TEST_CASE(HolsterAnalogGripHysteresis) {
    BodyFrameEstimator est;
    const math::Pose idle = At({0.4f, 1.3f, -0.4f});
    const BodyFrame body = est.Update(Head(), idle, idle, 0.0);
    HolsterSystem h(DefaultHolsterLayout());
    const math::Pose atHipR = At(h.SlotWorldPosition(body, h.FindSlot("hip_right")));
    CHECK(h.UpdateAnalog(body, idle, 0, atHipR, 0.6f).event == HolsterEvent::None); // below press
    CHECK(h.UpdateAnalog(body, idle, 0, atHipR, 0.8f).event == HolsterEvent::Draw);
    CHECK(h.UpdateAnalog(body, idle, 0, atHipR, 0.5f).event == HolsterEvent::None); // inside band: still held
    CHECK(h.UpdateAnalog(body, idle, 0, atHipR, 0.3f).event == HolsterEvent::Stow);
}
