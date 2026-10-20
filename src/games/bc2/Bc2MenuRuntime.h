#pragma once
#include "fvr/engine/PeImage.h"
#include <dxgi.h>
#include <ostream>
#include <string>
namespace fvr::bc2::menu {
enum class MenuDisplayState : unsigned {Unknown,Gameplay,Menu};
// Thread-safe, short-lived observation for presentation only; never input or
// native write authority. A stalled UI source returns Unknown.
MenuDisplayState CurrentDisplayState()noexcept;
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
void Present(IDXGISwapChain*)noexcept;
void Report(std::ostream&);
}
