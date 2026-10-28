#pragma once
#include "Bc2RigPaletteCopy.h"
#include <atomic>
#include <memory>

namespace fvr::bc2 {
// Publish the already-allocated complete packet after its independent guard.
// No scratch mutex, allocation, retry or success-only branch belongs here: an
// invalid latest packet must replace earlier input too. A concurrent reader of
// the older packet is still subject to the newer guard and original expiry.
template<class Tracking,class PublishGuard>
void PublishRigInputAfterGuard(std::atomic<std::shared_ptr<const Tracking>>& publication,
    const std::shared_ptr<const Tracking>& input,PublishGuard&& publishGuard) {
    publishGuard();
    publication.store(input,std::memory_order_release);
}
// Complete input publication is independent of the pose builder's scratch
// ownership. The returned snapshot keeps its original deadline; an invalid or
// expired latest input never falls back to a previous packet. The caller still
// checks independent current ownership/modes again at the actual root commit.
template<class Tracking,class Fresh>
std::shared_ptr<const Tracking> ReadRigInputForPose(
    const std::atomic<std::shared_ptr<const Tracking>>& publication,Fresh&& fresh) {
    auto input=publication.load(std::memory_order_acquire);
    if(!input||!fresh(input->deadline))return {};
    return input;
}
// The prepared immutable pose outlives its producer's scratch mutex. Loading it
// never takes that mutex or allocates another pose. Current atomic ownership
// still gates admission; native source and lifetime are rechecked at EACH Pack.
// An accepted shared pointer is storage lifetime only, never renewed authority.
template<class Pose,class Describe,class Observe>
std::shared_ptr<const Pose> ReadRigPoseForCopy(
    const std::atomic<std::shared_ptr<const Pose>>& publication,
    Describe&& describe,Observe&& observe) {
    auto pose=publication.load(std::memory_order_acquire);
    if(!pose||!RigPaletteCopyFresh(describe(*pose),observe()))return {};
    return pose;
}
inline bool RigSightPreviewForCopy(std::uint64_t capturedToken,std::uint64_t currentToken,
    bool leftTracked,std::int64_t originalDeadline,std::int64_t now)noexcept {
    return capturedToken&&capturedToken==currentToken&&leftTracked&&
        now>0&&now<originalDeadline;
}
}
