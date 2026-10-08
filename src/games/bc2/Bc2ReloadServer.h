#pragma once
#include "Bc2ReloadFlow.h"
namespace fvr::bc2 {
struct ReloadServerBinding {
    std::uint32_t preferredBase=0,imageSize=0,contextRva=0,managerVtableRva=0,controlledGetterRva=0;
    std::array<ReloadCodeProof,11> code{};
};
std::optional<ReloadServerBinding> DiscoverReloadServer(std::span<const std::byte>,const engine::PeImage&);
bool ValidateReloadServerLive(const ReloadStateMemory&,const ReloadServerBinding&,std::uint32_t base)noexcept;
// Every pointer is a borrowed native identity and is re-read before/after use.
// Arrays retain the exact bounded membership rather than assuming slot parity.
struct ReloadServerLinks {
    std::uint32_t manager=0,players=0,capacity=0,playerId=0,player=0,playerTable=0;
    std::uint32_t soldier=0,soldierTable=0,soldierData=0,inventory=0,inventoryData=0,begin=0,end=0,slot=0;
    std::uint32_t item=0,effects=0,effectsTable=0,callback=0,firing=0;
    std::uint32_t clientInventory=0,clientBegin=0,clientEnd=0,clientSlot=0;
    std::uint8_t clientPlayerFlags=0,clientSoldierFlags=0;
    std::array<std::uint32_t,64> items{},clientItems{};
    bool operator==(const ReloadServerLinks&)const=default;
};
struct ReloadServerSnapshot {
    ReloadStateSnapshot client{};ReloadServerLinks links{};ReloadFiringObservation state{};
    static constexpr bool authorityProven=false,manualGateEnabled=false;
};
// Fresh publication-time typed validation, then exact relation recheck. No calls,
// writes or hooks. A server-owned firing object is evidence, not ammo authority.
std::optional<ReloadServerSnapshot> ReadReloadServerState(const ReloadStateMemory&,const ReloadServerBinding&,
    const ReloadStateBinding&,std::uint32_t base,const ReloadStateSnapshot&)noexcept;
// Read-only failure evidence; no rejected observation grants access.
enum class ReloadServerBoundaryFailure : std::uint8_t {None,Lease,ScopeBefore,PublishedLinks,State,ScopeAfter,ChangedLinks};
struct ReloadServerBoundaryDiagnostic {
    ReloadServerBoundaryFailure failure=ReloadServerBoundaryFailure::None;
    std::uint8_t expectedSoldierFlags=0,observedSoldierFlags=0;
    bool differsOnlySoldierFlags=false;
    std::uint8_t afterSoldierFlags=0,changedBefore=0,changedAfter=0;
    unsigned stateFailure=0,changedOffset=UINT32_MAX;
    bool changedOnlySoldierFlags=false;
};
// Fast callback-time relation/state validation under the same <=200ms lease.
// Reflection is checked at publication and exact vtable identity thereafter.
std::optional<ReloadFiringObservation> ReadReloadServerBoundary(const ReloadStateMemory&,const ReloadServerBinding&,
    const ReloadStateBinding&,std::uint32_t base,const ReloadServerSnapshot&,
    std::int64_t deadlineNs,std::int64_t nowNs,ReloadServerBoundaryDiagnostic* diagnostic=nullptr)noexcept;
// Exact structural retention under the published typed lease. No mutable state
// is returned and this result cannot authorize a hold, transfer or ammo source.
bool ReadReloadServerOwner(const ReloadStateMemory&,const ReloadServerBinding&,
    const ReloadStateBinding&,std::uint32_t base,const ReloadServerSnapshot&,
    std::int64_t deadlineNs,std::int64_t nowNs,ReloadServerBoundaryDiagnostic* diagnostic=nullptr)noexcept;
}
