#pragma once
#include "Bc2MagazineReload.h"
#include <optional>
namespace fvr::bc2 {
enum class ReloadMagazineObservationResult { Ready, Deferred, Rejected };
struct ReloadMagazineLeaseObservation {
 ReloadMagazineObservationResult result=ReloadMagazineObservationResult::Rejected;
 std::optional<ReloadMagazineLease> lease;
};
} // namespace fvr::bc2
