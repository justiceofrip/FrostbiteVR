#pragma once
#include <memory>

namespace fvr::xr {
// One nonblocking opportunity before composition. A newly submitted normal
// copy may already be ready; a Busy result keeps its exact lease for later.
// The private bounded service shares polled so it never gets an extra poll.
// Newly submitted GPU-ordered copies retain their existing composition path.
template<class Pending,class Poll>
bool ServiceUncomposedCopyBeforePresentation(std::unique_ptr<Pending>& pending,
    bool submitted,bool& polled,Poll&& poll){
    if(!pending||!pending->Copying()||pending->Composed()||polled||
       (submitted&&pending->gpuOrdered))return false;
    polled=true;
    poll(); // May complete or retire this lease, never replace it.
    return true;
}
enum class ComposedCopyServiceResult {Skipped,Pending,Completed,Retired,ArchiveFailed};
// Called after current input/menu/reference cancellation, before the one provider
// admission. poll operates only on this held lease: it may complete or retire it,
// never replace it. archive preserves completed exact-ticket diagnostics before
// reset. The same helper is exercised with deterministic query outcomes in tests;
// it does not infer GPU completion, issue feedback, or admit a new delivery.
template<class Pending,class Poll,class Archive>
ComposedCopyServiceResult ServiceComposedCopyAfterInput(std::unique_ptr<Pending>& pending,
    Poll&& poll,Archive&& archive){
    if(!pending||!pending->gpuOrdered||!pending->Composed()||!pending->Copying())
        return ComposedCopyServiceResult::Skipped;
    poll();
    if(!pending)return ComposedCopyServiceResult::Retired;
    if(!pending->Ready())return ComposedCopyServiceResult::Pending;
    const bool preserved=archive(*pending);
    pending.reset(); // Ready destructor sends no second acknowledgement.
    return preserved?ComposedCopyServiceResult::Completed:ComposedCopyServiceResult::ArchiveFailed;
}
}
