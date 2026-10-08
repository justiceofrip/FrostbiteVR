#pragma once
#include "Bc2PreholdCleanup.h"
#include "Bc2ReloadNativePolicy.h"
#include <array>
namespace fvr::bc2 {
struct PreholdOperationBranchReceipt {
 unsigned reason=0;std::uint64_t invocation=0;
 ReloadFiringObservation before{},helperAfter{},updateAfter{};
 bool called=false,helperExact=false,updateKnown=false,updateIdle=false;
};
struct PreholdOperationRecord {
 std::uint64_t epoch=0,operation=0;
 ReloadNativeFamily family=ReloadNativeFamily::SpasTube;
 NativeMagazineProfileId profile=NativeMagazineProfileId::ScopedXm8;
 ReloadRequestCyclePhase policyPhase=ReloadRequestCyclePhase::Idle;
 unsigned policyFailure=0;
 std::optional<ReloadCancellationOrigin> cancellation;
 std::optional<PreholdCleanupEvidence> original,lastCoherent;
 std::int64_t cleanupDeadlineNs=0,archivedNs=0,authoritativeIdleNs=0,firstFailureNs=0;
 unsigned ledgerPhase=0,entered=0,called=0,exact=0,idle=0,firstFailureReason=0;
 bool failed=false;
 std::array<PreholdOperationBranchReceipt,3> branches{};
};
// First records retained, never overwritten by later successful operations.
// Telemetry only. Caller proves callback drain/exclusion and policy lock;
// this cannot authorize cleanup, counts, or continued controller interaction.
class PreholdOperationJournal {
public:
 static constexpr unsigned Capacity=16;
 bool Archive(PreholdOperationRecord record,std::int64_t now,bool excluded)noexcept {
  if(!excluded||!record.operation||now<=0)return false;
  for(unsigned n=0;n<count_;++n)if(rows_[n].epoch==record.epoch&&rows_[n].operation==record.operation)return false;
  record.archivedNs=now;
  if(count_==Capacity){++dropped_;return false;}
  rows_[count_++]=record;return true;
 }
 unsigned Count()const noexcept{return count_;}
 unsigned Dropped()const noexcept{return dropped_;}
 const PreholdOperationRecord& Row(unsigned n)const noexcept{return rows_[n];}
private:
 std::array<PreholdOperationRecord,Capacity> rows_{};unsigned count_=0,dropped_=0;
};
}
