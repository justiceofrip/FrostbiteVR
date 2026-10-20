#pragma once
// Windows side of the virtual gamepad: MinHook on XInputGetState / XInputGetCapabilities
// (BF4 imports XINPUT9_1_0; on Windows 10+ that forwards to xinput1_4, which is what gets
// hooked). While active, user index 0 reports the VR-driven pad; otherwise every call goes
// to the real XInput, so a physical controller keeps working (with none at user 0, a neutral
// pad answers there, so the device BF4 made for it stays connected).
#include "fvr/interaction/VirtualPad.h"

#include <string>

namespace fvr::bf4 {

class XInputSink {
public:
    bool Install(); // MinHook must already be initialised
    void Apply(const interaction::PadState& pad, bool active);
    void Deactivate();
    // "virtual_pad active= user0_polls= virtual= idle=": whether BF4 polls user 0 at all.
    static std::string Describe();
};

} // namespace fvr::bf4
