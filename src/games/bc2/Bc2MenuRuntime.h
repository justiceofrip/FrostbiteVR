#pragma once
#include "fvr/engine/PeImage.h"
#include <dxgi.h>
#include <ostream>
#include <string>
namespace fvr::bc2::menu {
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,const std::wstring&);
void Start()noexcept;
bool Stop()noexcept;
void Present(IDXGISwapChain*)noexcept;
void Report(std::ostream&);
}
