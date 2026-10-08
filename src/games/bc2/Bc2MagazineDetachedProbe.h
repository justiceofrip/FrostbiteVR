#pragma once
#include "Bc2MagazineDetached.h"
#include "Bc2MagazinePhysicalProbe.h"
namespace fvr::bc2 {
// Explicit monitor-only motion script. Every suppression and pair receipt comes
// from the real controller/native renderer; this class never creates authority.
class Bc2MagazineDetachedProbe {
public:
 explicit Bc2MagazineDetachedProbe(bool enabled=false)noexcept:enabled_(enabled){}
 void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,const MagazineRawContact&,
  const std::optional<Bc2AmmoReserveLease>&,MagazineDetachPhase,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
 void Observe(const Bc2MagazineDetached&,const MagazinePhysicalResult&,const std::optional<HolsterSuppressionReceipt>&,
  const std::optional<MagazineDetachPairReceipt>&,const std::optional<Bc2AmmoReserveLease>&,
  const MagazinePackCounters&,bool suppressionRequested,std::int64_t now)noexcept;
 bool CancelConsumer()const noexcept{return phase_==Phase::Failed;}
 bool Finished()const noexcept{return phase_==Phase::Done||phase_==Phase::Failed;}
 void Report(std::ostream&)const;
private:
 enum class Phase:unsigned{Warmup,Approach,Grip,Pull,Withdraw,Enter,Stroke,WaitAttached,WaitAvailable,Done,Failed};
 void Move(Phase,std::int64_t)noexcept;void Fail(unsigned,std::int64_t)noexcept;
 struct Input{interaction::InputFrame frame{};ReloadStateOwner owner{};std::int64_t observed=0,deadline=0;};
 std::array<std::optional<Input>,32> history_{};unsigned historyNext_=0;
 bool enabled_=false,claimed_=false,removedPair_=false,attachedPair_=false,released_=false;
 Phase phase_=Phase::Warmup;unsigned failure_=0,returned_=0;
 interaction::ReloadInsertionPhase insertionPhase_=interaction::ReloadInsertionPhase::Free;
 std::int64_t first_=0,phaseAt_=0,lastNow_=0,alignedAt_=0,rowAt_=0,firstCommit_=0,firstRemoved_=0,seatAt_=0,finishedAt_=0,neutralSince_=0;
 ReloadStateOwner owner_{};math::Pose command_{};std::optional<interaction::InputFrame> lastInput_;std::uint64_t lastRaw_=0;
 std::optional<Bc2AmmoReserveLease> baseline_;std::int64_t baselineAccepted_=0;
 std::optional<MagazineDetachAuthorization> authorization_,returnAuthorization_;
 std::optional<interaction::ReloadInsertionSeat> seat_;
 std::optional<MagazineDetachPairReceipt> attachedReceipt_;
 unsigned commits_=0,removedPublications_=0,attachedPublications_=0,neutralCallbacks_=0;
 std::uint64_t firstCommitTick_=0,lastCommitTick_=0,firstRemovedTick_=0;
 MagazinePackCounters packs_{};unsigned removedPairBaseline_=0;
 // Diagnostic-only prerequisite counts; source timestamps are never renewed.
 struct WarmupEvidence{unsigned samples=0,reserveAbsent=0,reserveFuture=0,reserveInvalid=0,reserveNonIdle=0,
  reserveIneligible=0,rawAbsent=0,rawUnmatched=0,rawRepeated=0,rawDetached=0;
  std::int64_t now=0,reserveObserved=0,reserveDeadline=0;std::uint64_t input=0,raw=0;};
 WarmupEvidence warmup_{};
 struct Row{unsigned phase=0,reason=0;std::int64_t now=0;std::uint64_t input=0,raw=0,tick=0;
  float position=0,angle=0,rail=0;int loaded=-1,reserve=-1;};
 std::array<Row,192> rows_{};unsigned rowCount_=0,dropped_=0;
};
}
