#pragma once
#include "Bc2ReloadRequestCycle.h"
namespace fvr::bc2 {
// This grants no reload authority. Only a verified neutral, loaded, exact-idle
// cohort can pass its original Update without a policy decision on contention.
// Entered reload, empty ammunition, explicit input and nonzero timers remain
// decision-critical and retain the existing cancellation behavior.
inline bool ReloadPreentryEvaluationMayDefer(const ReloadHoldInput& in) noexcept {
 if(!in.verified||in.branch>=3||in.context.inputFlags||in.context.fireRequested||
    in.context.orderRequested||in.context.reloadRequested)return false;
 const auto& first=in.branches[0];
 if(first.loaded<=0||first.reserve<0)return false;
 for(unsigned n=0;n<3;++n){const auto& b=in.branches[n];
  if(b.address!=in.identity.firing[n]||b.currentState!=2||b.nextState!=2||b.phaseTimer!=0||
     b.loaded!=first.loaded||b.reserve!=first.reserve||in.capacities[n]<=0||b.loaded>in.capacities[n])return false;
 }
 return true;
}
}

