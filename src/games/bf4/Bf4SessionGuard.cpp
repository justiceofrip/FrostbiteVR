#include "Bf4SessionGuard.h"

#include <cctype>

namespace fvr::bf4 {

namespace {
bool StartsWithNoCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}
} // namespace

bool SessionGuard::IsCampaignLevel(std::string_view levelName) {
    // Campaign levels: "Levels/SP/SP_Prologue/SP_Prologue" on build 179547 (live, 2026-10-19);
    // the older "Levels/SP_" form is kept. Everything else (Levels/MP/..., test levels) is not.
    return StartsWithNoCase(levelName, "Levels/SP/") || StartsWithNoCase(levelName, "Levels/SP_");
}

bool SessionGuard::IsFrontEndLevel(std::string_view levelName) {
    return StartsWithNoCase(levelName, "Levels/FrontEnd/");
}

void SessionGuard::Observe(const SessionObservation& obs) {
    if (mode_ == SessionMode::Locked) return; // sticky for the process lifetime

    if (obs.punkBusterModuleLoaded) {
        mode_ = SessionMode::Locked;
        reason_ = "PunkBuster module loaded (multiplayer)";
        return;
    }
    if (obs.remoteKnown && obs.connectedToRemoteServer) {
        mode_ = SessionMode::Locked;
        reason_ = "connected to a remote server (multiplayer)";
        return;
    }
    if (obs.levelKnown && IsFrontEndLevel(obs.levelName)) {
        mode_ = SessionMode::FrontEnd;
        reason_ = "front end";
        return;
    }
    if (obs.levelKnown && !obs.levelName.empty() && !IsCampaignLevel(obs.levelName)) {
        mode_ = SessionMode::Locked;
        reason_ = "non-campaign level: " + std::string(obs.levelName);
        return;
    }
    if (obs.levelKnown && obs.levelName.empty()) {
        mode_ = SessionMode::FrontEnd;
        reason_ = "front end";
        return;
    }
    if (obs.levelKnown && obs.remoteKnown && !obs.connectedToRemoteServer) {
        mode_ = SessionMode::Campaign;
        reason_ = "campaign level, local server";
        return;
    }
    // Missing signals: stay (or fall back to) Unknown -> presentation only.
    mode_ = SessionMode::Unknown;
    reason_ = obs.levelKnown ? "campaign level but server signal unbound" : "level signal unbound";
}

} // namespace fvr::bf4
