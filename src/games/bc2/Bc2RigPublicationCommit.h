#pragma once
#include "Bc2RigPaletteCopy.h"
#include <type_traits>

namespace fvr::bc2 {
// All fallible pose/visibility/publication preparation belongs before this
// boundary. The caller holds its pose mutex, but tracking invalidation does not
// take that mutex: sample the independent guard before AND after native checks.
// A later packet can confirm the same owner, never extend the captured lease.
// This grants presentation only; no input, ammunition or hand claim is created.
template<class Observe,class Validate,class Commit>
bool TryCommitRigPublication(const RigPaletteCopyState& captured,
    Observe&& observe,Validate&& validate,Commit&& commit) {
    static_assert(std::is_nothrow_invocable_v<Commit>);
    const auto first=observe();
    if(!RigPaletteCopyFresh(captured,first)||!validate())return false;
    const auto last=observe();
    if(!RigPaletteCopyFresh(captured,last)||last.now<first.now||
       last.state.input<first.state.input)return false;
    // No allocation, callback preparation, optional rejection or native read
    // may be deferred into this commit. Later consumers still recheck lifetime.
    commit();return true;
}
}
