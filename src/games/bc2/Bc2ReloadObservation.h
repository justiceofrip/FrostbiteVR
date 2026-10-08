#pragma once
#include "Bc2AmmoSupply.h"
#include "Bc2ReloadKeepAlive.h"
namespace fvr::bc2 {
enum class ReloadObservationResult:unsigned {Available,Deferred,Rejected,CohortGap};
struct ReloadReserveObservation {
 ReloadObservationResult result=ReloadObservationResult::Rejected;
 std::optional<Bc2AmmoReserveLease> lease;
};
}
