#pragma once
// Emulated BF4 weapon-firing state machine for deterministic tests, following the static
// analysis in docs/BF4-MANUAL-RELOAD.md 2.2: ready (3) -> reload requested (10) ->
// delay (11) -> ammo transfer at ReloadThreshold (12) -> rest of the reload -> ready;
// single-bullet guns loop per round; bolt guns run their own bolt states (7, 8) after a
// shot. It reads the virtual pad exactly as the game reads XInput. State numbers past 12
// that the static notes do not name are fixture-only (13 = finishing).
#include "../../src/games/bf4/Bf4ReloadDriver.h"

#include <algorithm>
#include <cstdint>

namespace fvr::bf4::emu {

struct FakeFiringMachine {
    // Weapon data (FireLogicData / AmmoConfigData). Default: an assault rifle, 30+1.
    std::int32_t reloadType = 2, capacity = 31;
    float reloadDelay = 0.2f, reloadTime = 2.4f, tacticalTime = 2.0f, threshold = 0.6f;
    bool boltAction = false;
    float boltDelay = 0.1f, boltTime = 0.8f;
    bool refuseReload = false; // e.g. DisableReloadWhileSprinting while sprinting

    // Runtime firing object.
    std::uint64_t identity = 0x5a5a0001;
    std::int32_t state = 3, loaded = 31, reserve = 120, shots = 0;
    float timer = 0, total = 0;
    bool fireHeld = false;

    void Tick(float dt, const interaction::PadState& pad, std::uint16_t reloadButton) {
        const bool fire = pad.rightTrigger > 128;
        const bool fireEdge = fire && !fireHeld;
        fireHeld = fire;
        timer -= dt;
        switch (state) {
        case 3:
            if ((pad.buttons & reloadButton) && !refuseReload && reserve > 0 && loaded < capacity) {
                state = 10;
            } else if (fireEdge && loaded > 0) {
                --loaded;
                ++shots;
                if (boltAction) { state = 7; timer = boltDelay; }
            }
            break;
        case 10: {
            const bool partial = loaded > 0 && reloadType != 1;
            total = partial ? tacticalTime : reloadTime;
            timer = loaded == 0 ? reloadDelay : 0.0f;
            state = 11;
            break;
        }
        case 11:
            if (timer <= 0) { timer = total * threshold; state = 12; }
            break;
        case 12:
            if (timer <= 0) {
                Transfer();
                if (reloadType == 0 && loaded < capacity && reserve > 0) { state = 11; timer = 0; }
                else { timer = total * (1 - threshold); state = 13; }
            }
            break;
        case 13:
            if (timer <= 0) state = 3;
            break;
        case 7:
            if (timer <= 0) { state = 8; timer = boltTime; }
            break;
        case 8:
            if (timer <= 0) state = 3;
            break;
        default:
            break;
        }
    }

    // +0x9BC910: one round for single-bullet guns, capacity - 1 from empty for guns that
    // keep a chambered round, otherwise top up; never more than the reserve.
    void Transfer() {
        const std::int32_t needed = reloadType == 0 ? 1 : (reloadType == 2 && loaded == 0 ? capacity - 1 : capacity - loaded);
        const std::int32_t n = std::min(needed, reserve);
        loaded += n;
        reserve -= n;
    }

    NativeFiringSample Sample() const { return {true, identity, state, loaded, reserve, capacity}; }
};

} // namespace fvr::bf4::emu
