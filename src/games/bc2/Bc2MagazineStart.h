#pragma once
#include "Bc2ReloadHold.h"
namespace fvr::bc2 {
// Unknown grants no authority. RegisteredCancelled means a cycle was accepted
// but startup was not delivered successfully: caller cancels and retires it.
// It also describes an exact prior registration found by the cleanup query;
// the query itself does not cancel or acknowledge native work.
enum class MagazineCycleStartResult:unsigned {Unknown,NotStarted,Started,RegisteredCancelled};
inline MagazineCycleStartResult InspectMagazineStartRegistration(
    bool callbacksDrained,bool magazineFamily,const ReloadHoldIdentity& current,
    std::uint64_t currentCycle,const ReloadHoldIdentity& requested,std::uint64_t requestedCycle)noexcept {
    if(!callbacksDrained||!magazineFamily||!requestedCycle)return MagazineCycleStartResult::Unknown;
    if(currentCycle==requestedCycle&&current==requested)return MagazineCycleStartResult::RegisteredCancelled;
    // Per-family cycle IDs are monotonic. A lower current ID proves this unique
    // attempted cycle has never registered. There is no asynchronous Start queue.
    if(currentCycle<requestedCycle)return MagazineCycleStartResult::NotStarted;
    return MagazineCycleStartResult::Unknown;
}
}
