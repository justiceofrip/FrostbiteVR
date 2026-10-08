#pragma once
#include "Bc2ReloadRequestCycle.h"
namespace fvr::bc2 {
// Observation only: copied from an already validated owned Update. No Held,
// ammunition credit, helper permission, refreshed source, or native write.
struct ReloadPreholdEntryObservation {
 ReloadHoldIdentity identity{};std::uint64_t cycle=0,sequence=0;
 std::int64_t observedNs=0,deadlineNs=0;
 unsigned enteredMask=0;
 ReloadRequestCyclePhase phase=ReloadRequestCyclePhase::Idle;
};
inline bool FreshPreholdEntry(const ReloadPreholdEntryObservation& e,const ReloadHoldIdentity& id,std::uint64_t cycle,std::int64_t now)noexcept {
 return e.identity==id&&e.cycle==cycle&&cycle&&e.sequence&&e.enteredMask&&!(e.enteredMask&~7u)&&e.observedNs>0&&
  now>=e.observedNs&&now<e.deadlineNs&&now-e.observedNs<=ReloadHoldProbe::ContextFreshNs;
}
}
