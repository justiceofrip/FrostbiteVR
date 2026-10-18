#pragma once
#include "fvr/engine/PeImage.h"
#include <iosfwd>
namespace fvr::bc2::menu::startup {
// Owns a temporary UI-update vtable entry only while removing absent logo
// movies. No global input, settings/asset writes, or campaign/menu selection.
bool Prepare(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t image);
void Start()noexcept;
bool Stop()noexcept;
void Report(std::ostream&);
}
