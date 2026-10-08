#pragma once
#include "Bc2BodyHolsterObservation.h"
#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2ReloadInterruptionProbe.h"
namespace fvr::bc2 {
// Explicit bounded controller driver. Observations come from the ordinary
// persistent inventory/hand/reload consumers. No native calls or fake receipts.
class Bc2InventoryReloadProbe {
public:
 explicit Bc2InventoryReloadProbe(bool interrupt=false)noexcept:interrupt_(interrupt){}
 enum class Phase:unsigned {Warmup,ReachInitial,DrawInitial,ClearInitial,ReachStow,Stow,ClearEmpty,
     ReachOther,DrawOther,ClearOther,ReachOtherStow,StowOther,ClearOtherEmpty,
     ReachOriginal,DrawOriginal,ClearOriginal,Reload,Done,Failed,Recovery};
 void Prepare(interaction::InputFrame&,const ReloadStateOwner&,std::string_view,
     std::shared_ptr<const BodyHolsterProbeSample>,const std::optional<BodyInventoryDisplay>&,
     const MagazineRawContact&,const MagazinePhysicalProbeState&,
     std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept;
 void Observe(const MagazinePhysicalProbeState&,const MagazinePackCounters&,std::int64_t now)noexcept;
 void ActionInput(interaction::InputFrame& in)const noexcept {if(phase_==Phase::Recovery)recovery_.ActionInput(in);}
 bool CancelConsumer()const noexcept{return phase_==Phase::Failed;}
 bool Completed()const noexcept{return phase_==Phase::Done;}
 Phase State()const noexcept{return phase_;}
 void Report(std::ostream&)const;
private:
 void To(Phase,std::int64_t)noexcept;
 void Fail(unsigned,std::int64_t)noexcept;
 bool Current(const BodyHolsterProbeSample&,std::int64_t)const noexcept;
 bool Held(const BodyHolsterProbeSample&,const interaction::BodySlotAssignment&,std::int64_t)const noexcept;
 bool Empty(const BodyHolsterProbeSample&,std::int64_t)const noexcept;
 Phase phase_=Phase::Warmup;unsigned failure_=0;
 std::int64_t first_=0,phaseAt_=0,lastNow_=0,observed_=0,deadline_=0;
 std::uint64_t lastInput_=0,baselineClaim_=0,otherClaim_=0,restoredClaim_=0;
 ReloadStateOwner initial_{};std::optional<interaction::BodySlotAssignment> original_,other_;
 interaction::BodyAnchorConfig anchors_{};math::Pose command_{};float squeeze_=0;
 Bc2MagazinePhysicalProbe reload_{true,false,true,true,true};
 bool interrupt_=false,recoveryBegun_=false;Bc2ReloadInterruptionProbe recovery_;
 MagazinePackCounters packs_{};MagazineRawContact recoveryRaw_{};std::string interruptedReport_;
 struct Row {unsigned phase=0,reason=0,weapon=0,bodyPhase=0,slot=0;
  std::int64_t now=0;std::uint64_t input=0,request=0,claim=0,committed=0;};
 std::array<Row,128> rows_{};unsigned count_=0;
 Row actual_{};
};
}
