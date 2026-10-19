#include "fvr/interaction/Holsters.h"

#include <algorithm>
#include <cmath>

namespace fvr::interaction {

using math::Pose;
using math::Rotate;
using math::Vec3;

namespace {
constexpr float kPi = 3.14159265f;

float Wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// Yaw of the head that stays stable when looking straight down or up: the head's up
// vector points forward when looking down and backward when looking up.
float HeadYaw(const math::Quat& q) {
    const Vec3 f = Rotate(q, {0, 0, -1});
    Vec3 h = f;
    if (std::sqrt(f.x * f.x + f.z * f.z) < 0.35f) {
        const Vec3 up = Rotate(q, {0, 1, 0});
        h = f.y < 0 ? up : up * -1.0f;
    }
    return std::atan2(-h.x, -h.z);
}

std::size_t Index(BodyAnchor a) { return static_cast<std::size_t>(a); }
} // namespace

BodyFrame BodyFrameEstimator::Update(const Pose& head, const Pose&, const Pose&, double t) {
    const float headYaw = HeadYaw(head.orientation);
    if (!initialised_) {
        initialised_ = true;
        yaw_ = headYaw;
        lastTime_ = t;
    }
    const float dt = static_cast<float>(std::clamp(t - lastTime_, 0.0, 0.1));
    lastTime_ = t;
    const float delta = Wrap(headYaw - yaw_);
    if (std::fabs(delta) > config_.yawDeadzone) {
        const float step = std::min(std::fabs(delta) - config_.yawDeadzone, config_.yawFollowRate * dt);
        yaw_ = Wrap(yaw_ + (delta > 0 ? step : -step));
    }

    const float s = config_.playerHeight / 1.75f;
    const math::Quat body = math::FromAxisAngle({0, 1, 0}, yaw_);
    const Vec3 up{0, 1, 0};
    const Vec3 right = Rotate(body, {1, 0, 0});
    const Vec3 fwd = Rotate(body, {0, 0, -1});
    // Neck pivot sits below and behind the eyes, so looking down at a hip doesn't drag the torso.
    const Vec3 neck = head.position + Rotate(head.orientation, Vec3{0, -0.10f, 0.09f} * s);

    BodyFrame f;
    f.playerHeightMetres = config_.playerHeight;
    auto put = [&](BodyAnchor a, Vec3 p) { Pose anchor; anchor.orientation = body; anchor.position = p; f.anchors[Index(a)] = anchor; };
    f.anchors[Index(BodyAnchor::Head)] = head;
    const Vec3 chest = neck + up * (-0.20f * s);
    const Vec3 pelvis = neck + up * (-0.55f * s);
    const Vec3 shoulder = neck + up * (-0.08f * s);
    put(BodyAnchor::Chest, chest);
    put(BodyAnchor::Pelvis, pelvis);
    put(BodyAnchor::HipLeft, pelvis + right * (-0.17f * s));
    put(BodyAnchor::HipRight, pelvis + right * (0.17f * s));
    put(BodyAnchor::ThighLeft, pelvis + up * (-0.18f * s) + right * (-0.11f * s) + fwd * (0.04f * s));
    put(BodyAnchor::ThighRight, pelvis + up * (-0.18f * s) + right * (0.11f * s) + fwd * (0.04f * s));
    put(BodyAnchor::ShoulderLeft, shoulder + right * (-0.18f * s));
    put(BodyAnchor::ShoulderRight, shoulder + right * (0.18f * s));
    put(BodyAnchor::BackLeft, shoulder + right * (-0.15f * s) + fwd * (-0.14f * s) + up * (0.05f * s));
    put(BodyAnchor::BackRight, shoulder + right * (0.15f * s) + fwd * (-0.14f * s) + up * (0.05f * s));
    f.valid = true;
    return f;
}

std::vector<HolsterSlot> DefaultHolsterLayout() {
    return {
        {"back_right", BodyAnchor::BackRight, {0, 0, 0}, 0.18f, EquipmentCategory::Primary, -1},
        {"hip_right", BodyAnchor::HipRight, {0, 0, 0}, 0.13f, EquipmentCategory::Secondary, -1},
        {"hip_left", BodyAnchor::HipLeft, {0, 0, 0}, 0.13f, EquipmentCategory::Grenade, -1},
        {"chest", BodyAnchor::Chest, {0, 0, -0.10f}, 0.10f, EquipmentCategory::Knife, -1},
        {"belt_front_left", BodyAnchor::Pelvis, {-0.10f, 0.05f, -0.12f}, 0.10f, EquipmentCategory::Magazine, -1},
    };
}

HolsterSystem::HolsterSystem(std::vector<HolsterSlot> slots, Config config) : slots_(std::move(slots)), config_(config) {
    ids_.reserve(slots_.size());
    for (auto& s : slots_) {
        ids_.emplace_back(s.id);
        s.id = ids_.back(); // own the id storage; callers may pass temporaries
    }
    occupied_.assign(slots_.size(), true); // spawn with a full loadout
}

int HolsterSystem::FindSlot(std::string_view id) const {
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].id == id) return static_cast<int>(i);
    return -1;
}

Vec3 HolsterSystem::SlotWorldPosition(const BodyFrame& body, int slot) const {
    const auto& s = slots_[static_cast<std::size_t>(slot)];
    const Pose& a = body.anchors[Index(s.anchor)];
    return a.position + Rotate(a.orientation, s.offset * (body.playerHeightMetres / 1.75f));
}

int HolsterSystem::Nearest(const BodyFrame& body, const Vec3& p, bool wantOccupied, EquipmentCategory category,
                           bool anyCategory) const {
    int best = -1;
    float bestDistance = 1e9f;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (occupied_[i] != wantOccupied) continue;
        if (!anyCategory && slots_[i].accepts != category) continue;
        const float d = math::Length(SlotWorldPosition(body, static_cast<int>(i)) - p);
        if (d <= slots_[i].radius && d < bestDistance) {
            best = static_cast<int>(i);
            bestDistance = d;
        }
    }
    return best;
}

HolsterResult HolsterSystem::Step(const BodyFrame& body, int handIndex, const Pose& hand, bool grip) {
    Hand& h = hands_[handIndex];
    const bool rising = grip && !h.gripDown;
    const bool falling = !grip && h.gripDown;
    h.gripDown = grip;
    HolsterResult r;
    r.leftHand = handIndex == 0;
    if (rising && h.heldSlot < 0) {
        const int s = Nearest(body, hand.position, true, EquipmentCategory::Primary, true);
        if (s >= 0) {
            h.heldSlot = s;
            occupied_[static_cast<std::size_t>(s)] = false;
            r.event = HolsterEvent::Draw;
            r.slotId = slots_[static_cast<std::size_t>(s)].id;
        }
    } else if (falling && h.heldSlot >= 0) {
        const int origin = h.heldSlot;
        const auto category = slots_[static_cast<std::size_t>(origin)].accepts;
        int target = Nearest(body, hand.position, false, category, false);
        if (target < 0 && config_.returnToOriginOnRelease && !occupied_[static_cast<std::size_t>(origin)]) target = origin;
        h.heldSlot = -1;
        if (target >= 0) {
            occupied_[static_cast<std::size_t>(target)] = true;
            r.event = HolsterEvent::Stow;
            r.slotId = slots_[static_cast<std::size_t>(target)].id;
        }
    }
    return r;
}

HolsterResult HolsterSystem::Update(const BodyFrame& body, const Pose& leftHand, bool leftGrip, const Pose& rightHand,
                                    bool rightGrip) {
    // Both hands are processed every frame; if both produce an event, the second is
    // queued for the next frame instead of being dropped.
    HolsterResult out;
    if (!queued_.empty()) {
        out = queued_.front();
        queued_.erase(queued_.begin());
    }
    if (body.valid) {
        for (auto [index, pose, grip] : {std::tuple{1, rightHand, rightGrip}, std::tuple{0, leftHand, leftGrip}}) {
            const HolsterResult r = Step(body, index, pose, grip);
            if (r.event == HolsterEvent::None) continue;
            if (out.event == HolsterEvent::None) out = r; else queued_.push_back(r);
        }
    }
    return out;
}

HolsterResult HolsterSystem::UpdateAnalog(const BodyFrame& body, const Pose& leftHand, float leftGrip,
                                          const Pose& rightHand, float rightGrip) {
    auto hysteresis = [&](bool& state, float v) {
        if (!state && v >= config_.gripPress) state = true;
        else if (state && v <= config_.gripRelease) state = false;
        return state;
    };
    const bool l = hysteresis(analog_[0], leftGrip);
    const bool r = hysteresis(analog_[1], rightGrip);
    return Update(body, leftHand, l, rightHand, r);
}

void HolsterSystem::OnLifetimeTransition() {
    // Death / level load: hands empty, loadout restocked, pending events void. Grip
    // states are kept so a grip held through the transition must be released first.
    for (auto& h : hands_) h.heldSlot = -1;
    occupied_.assign(slots_.size(), true);
    queued_.clear();
}

} // namespace fvr::interaction
