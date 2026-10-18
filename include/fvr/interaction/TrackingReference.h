#pragma once
#include <cstdint>
#include <limits>

namespace fvr::interaction {
// A manual reference change leaves the runtime's pose coordinates comparable.
// Recovery may have replaced them without an explicit runtime event, so it
// deliberately starts a new basis too. Zero denotes an unknown basis.
enum class TrackingReferenceChange {ManualRecenter,AutomaticRecovery,RuntimeSpaceReplacement,SessionRestart};
inline bool AdvanceTrackingReference(std::uint64_t& space,std::uint32_t& basis,
    TrackingReferenceChange change)noexcept {
    const bool replacement=change!=TrackingReferenceChange::ManualRecenter;
    if(!basis||space==std::numeric_limits<std::uint64_t>::max()||
       (replacement&&basis==std::numeric_limits<std::uint32_t>::max()))return false;
    ++space;if(replacement)++basis;return true;
}
}
