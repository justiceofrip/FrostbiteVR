#pragma once
// Windows side of the virtual gamepad: MinHook on XInputGetState / XInputGetCapabilities
// (BF4 imports XINPUT9_1_0; on Windows 10+ that forwards to xinput1_4, which is what gets
// hooked). While active, user index 0 reports the VR-driven pad; otherwise every call goes
// to the real XInput, so a physical controller keeps working.
#include "fvr/interaction/VirtualPad.h"

namespace fvr::bf4 {

class XInputSink {
public:
    bool Install(); // MinHook must already be initialised
    void Apply(const interaction::PadState& pad, bool active);
    void Deactivate();
};

} // namespace fvr::bf4
