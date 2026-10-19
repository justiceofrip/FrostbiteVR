#pragma once
// The two single-player signals read live (docs/BF4-SESSION-RE.md) turned into a
// SessionObservation for the SessionGuard, shaped by the first live sessions (2026-10-19):
//  - campaign levels are "Levels/SP/<name>/<name>", the menus run as
//    "Levels/FrontEnd/FrontEnd", and the level name is empty while loading;
//  - isMultiplayer flips between 0, 1 and unreadable (-1) in the menus and while loading,
//    and reads 0 or -1 inside a campaign level.
// So isMultiplayer counts only inside a level that is not the front end, an unreadable read
// keeps the last value seen in the same level, and a 1 must hold for kConfirmMs before it
// is reported (the guard's lock is permanent). Multiplayer maps are locked by their names.
#include "Bf4SessionGuard.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace fvr::bf4 {

class SessionSignalFilter {
public:
    static constexpr std::uint64_t kConfirmMs = 2000;

    // chainReadable: the level-info object was readable this time. isMultiplayer: 0, 1, or
    // -1 when its pointer chain was unreadable. The returned levelName lives until the next call.
    SessionObservation Update(bool chainReadable, std::string_view level, int isMultiplayer, std::uint64_t nowMs) {
        SessionObservation o;
        if (!chainReadable) return o; // no level signal: the guard stays Unknown (presentation only)
        level_.assign(level.data(), level.size());
        o.levelKnown = true;
        o.levelName = level_;
        if (level_.empty() || SessionGuard::IsFrontEndLevel(level_)) {
            levelSeen_.clear();
            lastMp_ = -1;
            mpSinceMs_ = 0;
            return o; // the flag is noise outside a level
        }
        if (level_ != levelSeen_) { // a new level: forget the previous level's reading
            levelSeen_ = level_;
            lastMp_ = -1;
            mpSinceMs_ = 0;
        }
        int mp = isMultiplayer;
        if (mp == -1) mp = lastMp_;
        else lastMp_ = mp;
        if (mp == 0) {
            mpSinceMs_ = 0;
            o.remoteKnown = true;
            o.connectedToRemoteServer = false;
        } else if (mp == 1) {
            if (!mpSinceMs_) mpSinceMs_ = nowMs ? nowMs : 1;
            if (nowMs - mpSinceMs_ >= kConfirmMs) {
                o.remoteKnown = true;
                o.connectedToRemoteServer = true;
            }
        }
        return o;
    }

private:
    std::string level_, levelSeen_;
    int lastMp_ = -1;
    std::uint64_t mpSinceMs_ = 0;
};

} // namespace fvr::bf4
