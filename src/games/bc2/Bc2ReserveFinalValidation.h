#pragma once
#include <cstdint>
namespace fvr::bc2 {
enum class ReserveFinalVerdict:unsigned {Available,Rejected,CohortGap};
// Snapshot authority is never renewed by overlap. A gap exports no counts.
constexpr ReserveFinalVerdict ValidateReserveFinal(bool structuralCurrent,bool sourceDeadlineCurrent,
 unsigned activeCallbacks,std::uint64_t capturedRevision,std::uint64_t currentRevision)noexcept {
 if(!structuralCurrent||!sourceDeadlineCurrent)return ReserveFinalVerdict::Rejected;
 return activeCallbacks==1&&capturedRevision==currentRevision?ReserveFinalVerdict::Available:ReserveFinalVerdict::CohortGap;
}
}
