#pragma once
#include "Bc2ReloadHold.h"

namespace fvr::bc2 {
// Evidence from the signature-verified native Restore hook and its unchanged
// copied snapshot. Counts alone never authorize prediction rollback.
struct ReloadPredictionRestore {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,invocation=0;
    std::int64_t beginNs=0,endNs=0;
    unsigned branch=3,caller=0;
    int loadedBefore=0,reserveBefore=0,loadedAfter=0,reserveAfter=0;
    bool immutableSnapshotMatched=false,identityRetained=false;
    ReloadObservedConfig config{};
};
using ReloadMagazinePredictionRestore=ReloadPredictionRestore;
}
