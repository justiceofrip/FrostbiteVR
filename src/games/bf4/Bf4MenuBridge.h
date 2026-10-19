#pragma once
// Win32 sink for PointerInjector events (game-menu laser pointer). The ordering and
// no-stuck-button guarantees live in the platform-neutral PointerInjector; this class
// only maps events to cursor hooks and window messages.
//
//   Win32PointerSink  GetCursorPos/SetCursorPos/GetAsyncKeyState hooks + posted
//                     WM_MOUSEMOVE / WM_LBUTTON* to the game window          (implemented)
//   RawInputSink      GetRawInputData hook if BF4's UI reads raw deltas       (RE task R7)
//   EngineCursorSink  UI cursor write through the UI.CursorRead binding       (RE task R7)
#include "Bf4PointerInjector.h"

#include <span>

namespace fvr::bf4 {

class Win32PointerSink {
public:
    bool Install();   // MinHook must already be initialised
    void Uninstall(); // cursor ownership returns to the game (hooks go with MH_ALL_HOOKS)
    void Apply(void* gameWindow, std::span<const PointerEvent> events); // HWND, opaque here
};

} // namespace fvr::bf4
