#include "fvr/game/Capability.h"

namespace fvr::game {

std::string_view FeatureName(Feature feature) noexcept {
    switch (feature) {
    case Feature::TheaterView: return "TheaterView";
    case Feature::ModMenuPointer: return "ModMenuPointer";
    case Feature::GameMenuPointer: return "GameMenuPointer";
    case Feature::HeadTrackedCamera: return "HeadTrackedCamera";
    case Feature::StereoNative: return "StereoNative";
    case Feature::StereoAlternateEye: return "StereoAlternateEye";
    case Feature::StereoDriver: return "StereoDriver";
    case Feature::ControllerAim: return "ControllerAim";
    case Feature::ButtonReload: return "ButtonReload";
    case Feature::VirtualGamepad: return "VirtualGamepad";
    case Feature::ManualReload: return "ManualReload";
    case Feature::Holsters: return "Holsters";
    case Feature::ArmIk: return "ArmIk";
    case Feature::FullBodyIk: return "FullBodyIk";
    case Feature::Vehicles: return "Vehicles";
    case Feature::Count: break;
    }
    return "Unknown";
}

std::string_view FeatureStateName(FeatureState state) noexcept {
    switch (state) {
    case FeatureState::Disabled: return "disabled";
    case FeatureState::Candidate: return "candidate";
    case FeatureState::Enabled: return "enabled";
    }
    return "unknown";
}

std::string_view EvidenceName(Evidence evidence) noexcept {
    switch (evidence) {
    case Evidence::None: return "none";
    case Evidence::Emulated: return "emulated";
    case Evidence::ActualGame: return "actual-game";
    case Evidence::Headset: return "headset";
    }
    return "unknown";
}

CapabilityRegistry::CapabilityRegistry() {
    // Roadmap features are explicitly disabled with a plain reason from the start.
    Set(Feature::ManualReload, FeatureState::Disabled, "roadmap M5: not implemented");
    Set(Feature::Holsters, FeatureState::Disabled, "roadmap M4: not implemented");
    Set(Feature::ArmIk, FeatureState::Disabled, "roadmap M6: not implemented");
    Set(Feature::FullBodyIk, FeatureState::Disabled, "roadmap M6+: not implemented");
    Set(Feature::Vehicles, FeatureState::Disabled, "roadmap M7: not implemented");
    Set(Feature::StereoDriver, FeatureState::Disabled, "stereo off in launcher settings");
}

void CapabilityRegistry::Set(Feature feature, FeatureState state, std::string reason) {
    auto& entry = status_[static_cast<std::size_t>(feature)];
    entry.state = state;
    entry.reason = std::move(reason);
}

void CapabilityRegistry::RecordEvidence(Feature feature, Evidence evidence) {
    auto& entry = status_[static_cast<std::size_t>(feature)];
    if (evidence > entry.evidence) entry.evidence = evidence;
}

const FeatureStatus& CapabilityRegistry::Get(Feature feature) const {
    return status_[static_cast<std::size_t>(feature)];
}

bool CapabilityRegistry::IsUsable(Feature feature, bool allowCandidates) const {
    const FeatureState state = Get(feature).state;
    return state == FeatureState::Enabled || (allowCandidates && state == FeatureState::Candidate);
}

void CapabilityRegistry::DisableAll(std::string_view reason) {
    for (auto& entry : status_) {
        entry.state = FeatureState::Disabled;
        entry.reason = std::string(reason);
    }
}

std::string CapabilityRegistry::Describe() const {
    std::string out;
    for (std::size_t i = 0; i < status_.size(); ++i) {
        const auto feature = static_cast<Feature>(i);
        out += FeatureName(feature);
        out += ": ";
        out += FeatureStateName(status_[i].state);
        out += " (";
        out += status_[i].reason;
        out += ")";
        if (status_[i].evidence != Evidence::None) {
            out += " [";
            out += EvidenceName(status_[i].evidence);
            out += "]";
        }
        out += "\n";
    }
    return out;
}

} // namespace fvr::game
