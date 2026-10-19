#pragma once
// Single-player only. The guard fails closed: VR gameplay features run only after the
// session is positively identified as the campaign; any multiplayer evidence locks the
// whole mod off for the rest of the process (hooks detach, frames pass through).
// BF4 multiplayer still runs PunkBuster + FairFight; this mod must never be active there.
#include <cstdint>
#include <string>
#include <string_view>

namespace fvr::bf4 {

enum class SessionMode : std::uint8_t { Unknown, FrontEnd, Campaign, Locked };

struct SessionObservation {
    bool levelKnown = false;          // level-name binding available this frame
    std::string_view levelName;       // e.g. "Levels/SP_Tashgar/SP_Tashgar"
    bool remoteKnown = false;         // server-connection signal available
    bool connectedToRemoteServer = false;
    bool punkBusterModuleLoaded = false; // pbcl*/pbag* seen in the module list: MP evidence
};

class SessionGuard {
public:
    void Observe(const SessionObservation& obs);

    SessionMode Mode() const { return mode_; }
    bool Locked() const { return mode_ == SessionMode::Locked; }
    // Theater view + menus: allowed unless locked.
    bool PresentationAllowed() const { return mode_ != SessionMode::Locked; }
    // Camera, aim, reload, holsters...: campaign only.
    bool GameplayAllowed() const { return mode_ == SessionMode::Campaign; }
    const std::string& Reason() const { return reason_; }

    static bool IsCampaignLevel(std::string_view levelName);
    // The menus run as a level of their own ("Levels/FrontEnd/FrontEnd" in bf4.exe 179547).
    static bool IsFrontEndLevel(std::string_view levelName);

private:
    SessionMode mode_ = SessionMode::Unknown;
    std::string reason_ = "no observation yet";
};

} // namespace fvr::bf4
