#pragma once
// Reload mode is a player setting from day one. ButtonPress is implemented now;
// Manual (physical magazine) is reserved and resolves to ButtonPress with an explicit
// note until the Manual pipeline earns evidence (roadmap M5). The router interface is
// the seam the manual implementation will plug into, so no caller changes later.
#include "fvr/game/Capability.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fvr::interaction {

enum class ReloadMode : std::uint8_t { ButtonPress = 0, Manual = 1 };

std::string_view ReloadModeName(ReloadMode mode) noexcept;
std::optional<ReloadMode> ParseReloadMode(std::string_view text) noexcept; // "button" | "manual"

struct ReloadModeResolution {
    ReloadMode requested = ReloadMode::ButtonPress;
    ReloadMode effective = ReloadMode::ButtonPress;
    std::string note; // shown in the mod menu and feedback; empty when requested == effective
};

ReloadModeResolution ResolveReloadMode(ReloadMode requested,
                                       const game::CapabilityRegistry& caps,
                                       bool allowCandidates);

// Per-frame facts the router needs. Filled by the game adapter from native state + input.
struct ReloadFrameInput {
    double timeSeconds = 0.0;
    bool reloadButtonDown = false;  // mapped action, weapon hand by default
    bool inMenu = false;            // UI owns input
    bool weaponCanReload = true;    // adapter's view of the held weapon (false: knife, full mag...)
};

enum class ReloadCommand : std::uint8_t {
    None,
    RequestNativeReload // adapter asserts the engine's reload input concept for one tick
};

class IReloadRouter {
public:
    virtual ~IReloadRouter() = default;
    virtual ReloadCommand Update(const ReloadFrameInput& input) = 0;
    virtual void Reset() = 0; // lifetime transitions only (death, level change), never per packet
};

// Rising-edge press -> one native reload request. Suppressed in menus and debounced so a
// held or chattering button cannot spam requests.
class ButtonReloadRouter final : public IReloadRouter {
public:
    explicit ButtonReloadRouter(double minIntervalSeconds = 0.25) : minInterval_(minIntervalSeconds) {}
    ReloadCommand Update(const ReloadFrameInput& input) override;
    void Reset() override;

private:
    double minInterval_;
    bool wasDown_ = false;
    bool armed_ = false;
    double lastRequest_ = -1e9;
};

} // namespace fvr::interaction
