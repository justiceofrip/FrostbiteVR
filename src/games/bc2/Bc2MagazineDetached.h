#pragma once
#include "Bc2MagazineDetachGate.h"
#include "Bc2MagazinePhysicalReload.h"
namespace fvr::bc2 {
// Positive-loaded full/zero-reserve interaction. Uses the shared raw-contact,
// hand claim, insertion rail and visual binding; no reload API or AmmoSupply.
// Prepare is BEFORE the native input-cache commit; Commit is AFTER its actual
// suppression receipt. Publish only Commit's tracking. Default disabled.
class Bc2MagazineDetached {
public:
 explicit Bc2MagazineDetached(bool enabled=false)noexcept:enabled_(enabled){}
 MagazinePhysicalResult Prepare(const MagazinePhysicalSample&,const std::optional<Bc2AmmoReserveLease>&,
  std::uint64_t nativeTick,std::uint32_t cache,interaction::HandInteraction&)noexcept;
 std::optional<HolsterSuppressionRequest> Demand()const noexcept;
 std::optional<HolsterSuppressionRequest> RecoveryDemand(const ReloadStateOwner& owner,
  const interaction::HandInteractionSample& input,std::uint64_t tick,std::uint32_t cache)const noexcept {
  return enabled_?gate_.RecoveryDemand(owner,input,tick,cache):std::nullopt;
 }
 MagazinePhysicalResult Commit(const std::optional<HolsterSuppressionReceipt>&,
  const std::optional<MagazineDetachPairReceipt>&,const std::optional<Bc2AmmoReserveLease>& postCommitReserve,
  interaction::HandInteraction&,std::uint64_t& intent,std::int64_t committedNs,std::int64_t nowNs)noexcept;
 bool BlocksActions()const noexcept{return gate_.BlocksActions();}
 bool Handles(const Bc2AmmoReserveLease& r)const noexcept{return enabled_&&(BlocksActions()||
  (r.verified&&r.allThreeIdle&&r.loaded>0&&(r.loaded==r.capacity||r.reserve==0)));}
 bool Routes(const std::optional<Bc2AmmoReserveLease>& r,bool physicalCycleBlocks)const noexcept {
  return enabled_&&(BlocksActions()||(!physicalCycleBlocks&&r&&Handles(*r)));
 }
 void Invalidate()noexcept{gate_.Invalidate();}
 void Report(std::ostream&)const;
 MagazineDetachPhase Phase()const noexcept{return gate_.Phase();}
 const auto& Original()const noexcept{return gate_.Original();}
 unsigned Returned()const noexcept{return gate_.Returned();}
 unsigned Recovered()const noexcept{return gate_.Recovered();}
 unsigned Grabs()const noexcept{return grabs_;}unsigned Seats()const noexcept{return seats_;}
 const auto& Authorization()const noexcept{return gate_.Authorization();}
private:
 void ResetRecognition(bool newOwner)noexcept;
 // A previous successful post-commit read may bridge an absent PRE-read only
 // for suppression admission. It retains its exact original expiry; it never
 // substitutes for the new post-commit read required to move/seat/publish.
 std::optional<Bc2AmmoReserveLease> lastCommittedReserve_;
 std::uint64_t preReadMissing_=0,preReadDeferred_=0,postReadMissing_=0;
 struct VisualGap{unsigned kind=0,phase=0;std::uint64_t input=0,request=0;std::int64_t now=0,deadline=0;};
 std::array<VisualGap,64> visualGaps_{};std::uint64_t visualGapTotal_=0,visualRetained_=0,visualRejected_=0;unsigned visualGapKind_=0;
 void RecordVisualGap(unsigned kind)noexcept;
 unsigned cancelReason_=0;std::uint64_t cancelInput_=0;std::int64_t cancelNs_=0;
 void Cancel(unsigned reason)noexcept;
 struct Raw {interaction::HandInteractionSample input{};interaction::HandClaim gun{};std::optional<interaction::HandClaim> held;};
 bool enabled_=false,neutral_=false,pulled_=false;
 Bc2MagazineDetachGate gate_;
 MagazineDetachGateSample native_{};MagazinePhysicalSample sample_{};
 std::optional<HolsterSuppressionRequest> replacement_;
 std::array<std::optional<Raw>,32> history_{};std::size_t historyNext_=0;
 std::uint64_t lastInput_=0,lastPrepared_=0,lastGeometry_=0;std::optional<interaction::HandClaim> held_;
 std::optional<ReloadStateOwner> recognitionNative_;
 std::optional<interaction::HandInteractionOwner> recognitionOwner_;
 interaction::HandInteractionKey recognitionWeapon_{};
 std::optional<MagazineFamilyBinding> recognitionFamily_;
 std::optional<interaction::ReloadInsertion> insertion_;
 math::Matrix4 grab_{},previous_{},itemFromGrip_{};
 unsigned grabs_=0,seats_=0,releases_=0;
 MagazinePhysicalResult last_{};
};
}
