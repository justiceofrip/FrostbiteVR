#pragma once
#include "Bc2MagazinePhysicalReload.h"
#include "fvr/interaction/ControllerInput.h"
namespace fvr::bc2 {
struct MagazinePackCounters {
 unsigned copies=0,pairs=0,fallbacks=0;
 std::array<unsigned,4> roleCopies{},rolePairs{};
};
// Bounded, explicit diagnostic. Modifies only the private synthetic controller
// input BEFORE shared hand ownership. Native Start/Seat/ack, supply claims,
// geometry evidence and packed-copy receipts are never manufactured here.
class Bc2MagazinePhysicalProbe {
public:
 explicit Bc2MagazinePhysicalProbe(bool enabled=false,bool originalReturn=false,bool carryChallenge=false,bool returnThenReplace=false)noexcept:enabled_(enabled),originalReturn_(originalReturn||returnThenReplace),carryChallenge_(carryChallenge&&!originalReturn&&!returnThenReplace),returnThenReplace_(returnThenReplace){}
 void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view asset,const MagazineRawContact&,
     const MagazinePhysicalProbeState&,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
 void Observe(const MagazinePhysicalProbeState&,const MagazinePackCounters&,std::int64_t now)noexcept;
 bool CancelConsumer()const noexcept{return phase_==Phase::Done||phase_==Phase::Failed;}
 bool Completed()const noexcept{return phase_==Phase::Done;}
 void Report(std::ostream&)const;
private:
 enum class Phase:unsigned {Warmup,ApproachMagazine,Grip,Pull,Release,Pouch,GrabReplacement,ApproachRail,Enter,Stroke,WaitAck,WaitBaseline,Done,Failed,Carry};
 void PhaseTo(Phase,std::int64_t)noexcept;
 void Fail(unsigned,std::int64_t)noexcept;
 struct Input {interaction::InputFrame frame{};ReloadStateOwner owner{};std::int64_t observed=0,deadline=0;};
 std::array<std::optional<Input>,32> history_{};unsigned historyNext_=0;
 bool enabled_=false,originalReturn_=false,carryChallenge_=false;
 bool returnThenReplace_=false,secondCycle_=false;std::string firstCycleReport_;
 bool carryReturning_=false,carryExercised_=false;std::optional<math::Matrix4> carryStart_;
 std::optional<math::Matrix4> carryPrevious_;
 std::int64_t carryWaitAt_=0,carryFailureAt_=0;unsigned carryWaits_=0,carryFailureFlags_=0;
 float carryStepTranslation_=0,carryStepAngle_=0;
 float carryDisplacement_=0,carryAngle_=0;unsigned carrySamples_=0;Phase phase_=Phase::Warmup;unsigned failure_=0;
 std::int64_t first_=0,phaseAt_=0,lastNow_=0,alignedAt_=0,rowAt_=0;
 ReloadStateOwner owner_{};std::optional<interaction::InputFrame> lastInput_;
 math::Pose command_{};std::uint64_t lastRaw_=0;
 int loadedBefore_=-1,reserveBefore_=-1,capacityBefore_=-1,expectedUnits_=0;
 unsigned completed_=0,submitted_=0,acquired_=0,originalReturns_=0;
 std::optional<interaction::OriginalMagazine> original_;
 std::optional<interaction::OriginalMagazineReturnReceipt> originalReceipt_;
 std::uint64_t originalCycle_=0;
 MagazinePackCounters packs_{},baseline_{};
 bool removedPair_=false,hiddenPair_=false,replacementPair_=false,receiptVerified_=false;
 std::array<std::optional<unsigned>,4> phasePairs_{};
 struct Row {unsigned phase=0,reason=0;std::int64_t now=0;std::uint64_t input=0,raw=0,cycle=0;
  float positionError=0,angleError=0,rail=0;int loaded=-1,reserve=-1;unsigned pairs=0;};
 std::array<Row,192> rows_{};unsigned rowCount_=0,dropped_=0;
};
} // namespace fvr::bc2
