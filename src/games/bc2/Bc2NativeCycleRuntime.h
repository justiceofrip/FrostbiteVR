#pragma once
#include "Bc2NativeCycleService.h"
namespace fvr::bc2::reloadFlowRuntime {
// Explicit private SPAS trial, pre-Start only. Normal compilation returns
// false. FVR_BC2_MANUAL_CYCLE_CANDIDATE is not a profile admission flag; exact
// measured contact/rig and controller capability remain caller prerequisites.
// This first trial excludes reload/resource transactions and diagnostics.
bool EnableNativePumpCandidate()noexcept;
// Separate ordinary capability, pre-Start/default OFF. No finite input driver.
bool EnableNativeCycleDispatcher()noexcept;
// Caller has retired physical custody. Drains exact native/request owners;
// retains unresolved debt and returns false while it cannot change selection.
bool SelectNativeCycleMode(Bc2NativeCycleMode,const ReloadStateOwner&)noexcept;
// Exact M95_sp private trial, independently selected before Start.
bool EnableNativeBoltCandidate()noexcept;
bool EnableNativeBoltRecording(unsigned cycles)noexcept;
// Pre-Start diagnostic capacity only; cannot select or acknowledge a cycle.
bool EnableOrdinaryBoltInputRecording(unsigned cycles)noexcept;
bool EnableOrdinaryResourceInputRecording()noexcept;
// Finite diagnostic only, before Start: eight cycles receive a fixed 60-second
// window and storage. No call can extend a running recorder or native lease.
bool EnableNativePumpRecording(unsigned cycles)noexcept;
// Pre-Start only; does not extend an opened recorder or a native control lease.
bool EnableResourcePumpRecording()noexcept;
bool PublishNativeCycleControl(const Bc2NativeCycleControl&)noexcept;
std::optional<Bc2NativeCycleView> ReadNativeCycleView(std::int64_t now)noexcept;
bool AcknowledgeNativeCycleReady(const interaction::WeaponCycleReady&)noexcept;
void CancelNativeCycle()noexcept;
}
