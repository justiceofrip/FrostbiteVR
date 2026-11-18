#pragma once
#include "Bc2MenuState.h"
namespace fvr::bc2 {
// Invoke only after the independently owned vehicle-camera path has returned.
// Fresh committed LetterBox presentation suppresses our optional on-foot base
// rewrite. It neither identifies a cinematic camera nor grants native writes.
// Both original prototypes continue through the existing tracked-eye/culling
// construction. Unknown/expired observations retain the existing gameplay path.
template<class GameplayAdjustment>
bool ApplyOnFootViewBasePolicy(const MenuPresentationSnapshot* presentation,
    std::int64_t nowNs,GameplayAdjustment&& adjust)noexcept {
    if(presentation&&MenuPresentationFresh(*presentation,nowNs)&&
       presentation->observation.state==MenuPresentationState::LetterBox)return true;
    return adjust();
}
}
