#pragma once
#include "fvr/graphics/D3D11FrameBridge.h"

namespace fvr::bc2 {
// Only for the NativeProbe host whose loader retains the module until exit.
// Explicit CloseGraphics on the original graphics owner releases resources.
// If that close cannot finish and no further callback arrives, retain the
// entire bridge (including its channel feedback target and COM ownership).
// Never register its destructor: process-exit CRT teardown may be off-owner,
// and a destructor's failed CloseGraphics cannot retain its dying members.
// The closed bridge shell also remains until process exit. This does
// not permit module unload or promise eventual cleanup after a failed close.
inline graphics::D3D11FrameBridge& MakeProcessRetainedFrameBridge(
    graphics::D3D11FrameBridge::PublicationObserver observer=nullptr) {
    return *new graphics::D3D11FrameBridge(observer);
}
}
