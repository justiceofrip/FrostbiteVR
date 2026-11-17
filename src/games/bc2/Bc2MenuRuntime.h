#pragma once
#include "fvr/engine/PeImage.h"
#include "Bc2MenuState.h"
#include <memory>
#include <dxgi.h>
#include <ostream>
#include <string>
namespace fvr::bc2::menu {
enum class MenuDisplayState : unsigned {Unknown,Gameplay,Menu,Passive};
// Thread-safe, short-lived observation for presentation only; never input or
// native write authority. A stalled UI source returns Unknown.
MenuDisplayState CurrentDisplayState()noexcept;
// Immutable UI-pump observation only. Null includes unsupported, unknown and
// expired presentation; it grants no camera, input or native-write authority.
std::shared_ptr<const MenuPresentationSnapshot> CurrentPresentation()noexcept;
// Presentation/cleanup evidence for a session that never created a world.
// Does not claim gameplay stereo or human headset acceptance.
bool CompletedMenuPresentation()noexcept;
// Startup telemetry only: the menu uses its own input/raster lane and does not
// execute world DrawHook. These observations grant no native input authority.
struct MenuConnectionTelemetry {
    bool ready=false;
    std::uint64_t inputSamples=0,published=0,graphicsFailures=0;
};
MenuConnectionTelemetry ReadConnectionTelemetry()noexcept;
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,const std::wstring&);
void Start()noexcept;
bool Stop()noexcept;
// Returns this Present's observation, not a cached UI-thread state. Unknown
// leaves the native backbuffer intact at a contended/loading transition.
MenuDisplayState Present(IDXGISwapChain*)noexcept;
void Report(std::ostream&);
}
