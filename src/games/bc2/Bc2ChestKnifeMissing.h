#pragma once
#include "ChestKnifeCustody.h"

namespace fvr::bc2 {
// Preserve the actual coordinator ordering: resolve/revoke first-boundary native
// work before portable policy sees an unknown observation. No source is renewed
// or native outcome manufactured here. Rendering is separately cleared by caller.
template<class BeforePrepare>
void PrepareChestKnifeMissing(std::int64_t now,interaction::HandInteraction& hands,
    interaction::ChestKnifeCustody& policy,BeforePrepare&& beforePrepare)noexcept {
    interaction::KnifeSample missing;missing.source.nowNs=now;
    if(beforePrepare(missing,policy))policy.Prepare(missing,hands);
}
}
