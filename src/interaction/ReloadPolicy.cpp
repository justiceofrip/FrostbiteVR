#include "fvr/interaction/ReloadPolicy.h"

namespace fvr::interaction {

std::string_view ReloadModeName(ReloadMode mode) noexcept {
    return mode == ReloadMode::Manual ? "manual" : "button";
}

std::optional<ReloadMode> ParseReloadMode(std::string_view text) noexcept {
    if (text == "button" || text == "button_press" || text == "buttonpress") return ReloadMode::ButtonPress;
    if (text == "manual") return ReloadMode::Manual;
    return std::nullopt;
}

ReloadModeResolution ResolveReloadMode(ReloadMode requested,
                                       const game::CapabilityRegistry& caps,
                                       bool allowCandidates) {
    ReloadModeResolution r;
    r.requested = requested;
    r.effective = requested;
    if (requested == ReloadMode::Manual && !caps.IsUsable(game::Feature::ManualReload, allowCandidates)) {
        r.effective = ReloadMode::ButtonPress;
        r.note = "Manual reload isn't available yet (" + caps.Get(game::Feature::ManualReload).reason +
                 "); using button press.";
    }
    // Button reload works through a native binding or through the virtual gamepad.
    if (r.effective == ReloadMode::ButtonPress && !caps.IsUsable(game::Feature::ButtonReload, allowCandidates) &&
        !caps.IsUsable(game::Feature::VirtualGamepad, allowCandidates)) {
        r.note += r.note.empty() ? "" : " ";
        r.note += "Button reload is not active yet (" + caps.Get(game::Feature::ButtonReload).reason +
                  "); the game's own reload key still works.";
    }
    return r;
}

ReloadCommand ButtonReloadRouter::Update(const ReloadFrameInput& input) {
    const bool rising = input.reloadButtonDown && !wasDown_;
    wasDown_ = input.reloadButtonDown;

    // A press that starts inside a menu must be released before it can reload.
    if (input.inMenu) {
        armed_ = false;
        return ReloadCommand::None;
    }
    if (!input.reloadButtonDown) armed_ = true;
    if (!rising || !armed_) return ReloadCommand::None;
    if (!input.weaponCanReload) return ReloadCommand::None;
    if (input.timeSeconds - lastRequest_ < minInterval_) return ReloadCommand::None;

    lastRequest_ = input.timeSeconds;
    return ReloadCommand::RequestNativeReload;
}

void ButtonReloadRouter::Reset() {
    wasDown_ = false;
    armed_ = false;
    lastRequest_ = -1e9;
}

} // namespace fvr::interaction
