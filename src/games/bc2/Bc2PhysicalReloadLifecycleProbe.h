#pragma once
#include "Bc2PhysicalReloadProbe.h"
#include "Bc2PreholdEntryObservation.h"
namespace fvr::bc2 {
enum class PhysicalReloadLifecycleScenario:unsigned {Normal,ArmingCancel,HoldingCancel,CompleteThenArmingCancel,RepeatArmingCancel};
// Private synthetic fixture only. Cancellation is the existing Gameplay
// CancelConsumer path, never a native callback-side policy mutation.
class Bc2PhysicalReloadLifecycleProbe {
public:
 Bc2PhysicalReloadLifecycleProbe(bool enabled=false,unsigned rounds=1,PhysicalReloadLifecycleScenario scenario=PhysicalReloadLifecycleScenario::Normal)noexcept:
  enabled_(enabled),scenario_(scenario),delegate_(enabled,rounds){}
 void Entry(std::optional<ReloadPreholdEntryObservation> e)noexcept {entry_=e;}
 void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,const ReloadRawContact&,const PhysicalReloadProbeState&,std::int64_t,std::int64_t,std::int64_t)noexcept;
 void Observe(const PhysicalReloadProbeState&,std::int64_t)noexcept;
 bool CancelConsumer()const noexcept{return stopping_||failed_||done_||delegate_.CancelConsumer();}
 void Report(std::ostream&)const;
private:
 PhysicalReloadProbeState Local(const PhysicalReloadProbeState&)const noexcept;
 void Stop(const PhysicalReloadProbeState&,std::int64_t,bool normal)noexcept;
 bool enabled_=false,stopping_=false,failed_=false,done_=false,normalCompletion_=false;
 // No owner is committed to the delegate until startup input/native/render
 // evidence agrees. This grants no hand claim, operation or native lease.
 bool startupCommitted_=false;
 std::int64_t startupFirstNs_=0,startupCandidateNs_=0,startupLastNow_=0;
 std::uint64_t startupFirstInput_=0,startupLastInput_=0;
 ReloadStateOwner startupOwner_{};
 unsigned startupWaits_=0,startupOwnerChanges_=0,startupReason_=0;
 PhysicalReloadLifecycleScenario scenario_;
 Bc2PhysicalReloadProbe delegate_;
 std::optional<ReloadPreholdEntryObservation> entry_;
 unsigned attempt_=0,cancelCount_=0,baseAcquired_=0,baseSubmitted_=0,baseCompleted_=0;
 std::int64_t stopAt_=0;int originalLoaded_=-1,originalReserve_=-1;
 std::uint64_t stoppedCycle_=0;
 struct Row {std::uint64_t cycle=0;std::int64_t now=0;unsigned prior=0,entered=0;int loaded=-1,reserve=-1;bool normal=false;};
 std::array<Row,4> rows_{};unsigned rowCount_=0;
};
}
