#include "Bf4BodyGestures.h"

#include "fvr/interaction/VirtualPad.h"

#include <cstdio>

namespace fvr::bf4 {

using interaction::EquipmentCategory;

std::vector<interaction::HolsterSlot> Bf4HolsterLayout() {
    using interaction::BodyAnchor;
    return {
        {"back_right", BodyAnchor::BackRight, {0, 0, 0}, 0.18f, EquipmentCategory::Primary, -1},
        {"hip_right", BodyAnchor::HipRight, {0, 0, 0}, 0.13f, EquipmentCategory::Secondary, -1},
        {"hip_left", BodyAnchor::HipLeft, {0, 0, 0}, 0.13f, EquipmentCategory::Grenade, -1},
        {"chest", BodyAnchor::Chest, {0, 0, -0.10f}, 0.10f, EquipmentCategory::Knife, -1},
        {"belt_front_left", BodyAnchor::Pelvis, {-0.10f, 0.05f, -0.12f}, 0.10f, EquipmentCategory::Magazine, -1},
    };
}

BodyGestures::BodyGestures() : holsters_(Bf4HolsterLayout()) {}

math::Vec3 BodyGestures::Magwell(const math::Pose& rightGrip) {
    // OpenXR grip space: -Z along the barrel, +Y up the back of the hand. The well sits
    // below the hand and slightly ahead of it.
    return rightGrip.position + math::Rotate(rightGrip.orientation, math::Vec3{0, -0.07f, -0.06f});
}

math::Vec3 BodyGestures::SlotPosition(std::string_view id) const {
    const int slot = holsters_.FindSlot(id);
    if (slot < 0 || !lastBody_.valid) return {};
    return holsters_.SlotWorldPosition(lastBody_, slot);
}

void BodyGestures::PressKey(std::uint16_t key, double now) {
    key_ = key;
    keyUntil_ = now + kKeyHoldSeconds;
    ++stats_.keyPresses;
}

void BodyGestures::PulseReload(double now) { reloadUntil_ = now + kReloadPulseSeconds; }

void BodyGestures::Select(Bf4Gun gun, double now) {
    current_ = gun;
    PressKey(gun == Bf4Gun::Primary ? gamekey::Primary : gamekey::Secondary, now);
    ++stats_.draws;
}

BodyGestureOutput BodyGestures::Update(const BodyGestureInput& in) {
    BodyGestureOutput out;
    const double now = in.timeSeconds;
    const auto& c = in.controls;
    const bool usable = in.engage && c.focused && c.headValid && interaction::ValidInput(c);

    // Grip edges are tracked whenever a packet arrives, so a grip already held when the
    // gestures engage (menu closed, level loaded) never counts as a fresh grab.
    std::array<bool, 2> rising{}, falling{};
    if (c.focused) {
        for (int h = 0; h < 2; ++h) {
            const float g = (c.hands[h].active & interaction::Squeeze) ? c.hands[h].squeeze : 0.0f;
            const bool down = grip_[h] ? g > kGripRelease : g >= kGripPress;
            rising[h] = down && !grip_[h];
            falling[h] = !down && grip_[h];
            grip_[h] = down;
        }
    }
    const bool reloadDown = c.focused && in.reloadButton, switchDown = c.focused && in.switchButton;
    const bool reloadRising = reloadDown && !reloadWas_, switchRising = switchDown && !switchWas_;
    reloadWas_ = reloadDown;
    switchWas_ = switchDown;

    if (!usable) {
        // Temporary input ends now (keys, pulses). Held items wait for their grip: a grip
        // let go meanwhile puts a grenade back and drops a magazine (no action is sent).
        key_ = 0;
        reloadUntil_ = 0;
        for (int h = 0; h < 2; ++h) {
            if (!falling[h]) continue;
            held_[h] = Held::None;
            if (h == 0 && magazineInHand_) {
                magazineInHand_ = false;
                ++stats_.drops;
            }
            gripOwned_[h] = false;
        }
        out.gripOwned = gripOwned_;
        return out;
    }

    if (switchRising) current_ = current_ == Bf4Gun::Primary ? Bf4Gun::Secondary : Bf4Gun::Primary; // pad Y toggles

    const interaction::BodyFrame body = body_.Update(c.head, c.hands[0].grip, c.hands[1].grip, now);
    lastBody_ = body;
    const auto slots = holsters_.Slots();
    auto zoneAt = [&](const math::Vec3& p) -> int {
        int best = -1;
        float bestDistance = 1e9f;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const float d = math::Length(holsters_.SlotWorldPosition(body, static_cast<int>(i)) - p);
            if (d <= slots[i].radius && d < bestDistance) {
                best = static_cast<int>(i);
                bestDistance = d;
            }
        }
        return best;
    };

    for (int h = 0; h < 2; ++h) {
        const auto& hand = c.hands[h];
        if (falling[h]) {
            if (held_[h] == Held::Grenade && hand.gripTracked) {
                const int hip = holsters_.FindSlot("hip_left");
                const float away = hip >= 0 ? math::Length(hand.grip.position - holsters_.SlotWorldPosition(body, hip)) : 1.0f;
                if (away > kThrowDistance) {
                    PressKey(gamekey::Grenade, now);
                    ++stats_.throws;
                    out.haptic[h] = {true, true};
                }
            }
            if (h == 0 && magazineInHand_) { // let go before the well: the magazine is gone
                magazineInHand_ = false;
                ++stats_.drops;
            }
            held_[h] = Held::None;
            gripOwned_[h] = false;
        }
        if (!rising[h] || !hand.gripTracked || !in.holsters) continue;
        const int zone = zoneAt(hand.grip.position);
        if (zone < 0) continue;
        switch (slots[static_cast<std::size_t>(zone)].accepts) {
        case EquipmentCategory::Primary: Select(Bf4Gun::Primary, now); break;
        case EquipmentCategory::Secondary: Select(Bf4Gun::Secondary, now); break;
        case EquipmentCategory::Grenade: held_[h] = Held::Grenade; break;
        case EquipmentCategory::Knife:
            if (h != 0) continue; // the gun hand rests near the chest while aiming: left hand only
            PressKey(gamekey::Melee, now);
            ++stats_.knives;
            break;
        case EquipmentCategory::Magazine:
            if (!in.manualReload || h != 0 || magazineInHand_) continue;
            magazineInHand_ = true;
            ++stats_.grabs;
            break;
        case EquipmentCategory::Gadget: continue;
        }
        gripOwned_[h] = true;
        out.haptic[h] = {true, false};
    }

    if (in.manualReload) {
        auto& gunOut = magazineOut_[static_cast<int>(current_)];
        if (reloadRising) {
            reloadDownAt_ = now;
            quickDone_ = false;
            if (!gunOut) {
                gunOut = true;
                ++stats_.ejects;
                out.haptic[1] = {true, false};
            }
        }
        if (reloadDown && !quickDone_ && now - reloadDownAt_ >= kQuickReloadSeconds) {
            quickDone_ = true;
            gunOut = false;
            PulseReload(now);
            ++stats_.quickReloads;
            out.haptic[1] = {true, true};
        }
        if (magazineInHand_ && gunOut && c.hands[0].gripTracked && c.hands[1].gripTracked &&
            math::Length(c.hands[0].grip.position - Magwell(c.hands[1].grip)) <= kMagwellRadius) {
            magazineInHand_ = false; // seated; the grip stays owned until it is released
            gunOut = false;
            PulseReload(now);
            ++stats_.seats;
            out.haptic[0] = {true, true};
            out.haptic[1] = {true, true};
        }
        out.blockFire = gunOut;
    }

    if (now < keyUntil_) out.keys[0] = key_;
    else key_ = 0;
    if (now < reloadUntil_) out.padButtons |= interaction::pad::X;
    out.gripOwned = gripOwned_;
    return out;
}

void BodyGestures::OnLifetimeTransition() {
    magazineOut_ = {};
    magazineInHand_ = false;
    held_ = {};
    current_ = Bf4Gun::Primary;
    key_ = 0;
    reloadUntil_ = 0;
    body_.Reset();
    // Grips held through the transition must be released before they act again.
    for (int h = 0; h < 2; ++h) gripOwned_[h] = grip_[h];
}

std::string BodyGestures::Describe() const {
    char line[320];
    std::snprintf(line, sizeof(line),
                  "gestures gun=%s magazine_out=%d/%d magazine_in_hand=%d draws=%llu throws=%llu knives=%llu ejects=%llu "
                  "grabs=%llu seats=%llu drops=%llu quick_reloads=%llu key_presses=%llu",
                  current_ == Bf4Gun::Primary ? "primary" : "sidearm", magazineOut_[0] ? 1 : 0, magazineOut_[1] ? 1 : 0,
                  magazineInHand_ ? 1 : 0, static_cast<unsigned long long>(stats_.draws),
                  static_cast<unsigned long long>(stats_.throws), static_cast<unsigned long long>(stats_.knives),
                  static_cast<unsigned long long>(stats_.ejects), static_cast<unsigned long long>(stats_.grabs),
                  static_cast<unsigned long long>(stats_.seats), static_cast<unsigned long long>(stats_.drops),
                  static_cast<unsigned long long>(stats_.quickReloads), static_cast<unsigned long long>(stats_.keyPresses));
    return line;
}

} // namespace fvr::bf4
