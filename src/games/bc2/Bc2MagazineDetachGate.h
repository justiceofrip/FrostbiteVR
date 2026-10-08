#pragma once
#include "Bc2MagazineDetachEvidence.h"
#include "Bc2MagazineInteraction.h"
namespace fvr::bc2 {
enum class MagazineDetachPhase:unsigned {Idle,Suppressing,Detached,Returning,Recovering};
struct MagazineDetachGateSample {
 MagazineFamilyEvidence family{};Bc2AmmoReserveLease reserve{};
 interaction::HandInteractionSample input{};interaction::HandClaim gun{};
 std::shared_ptr<const SelectedMeshesSnapshot> selected;
 std::uint64_t nativeTick=0;std::uint32_t cache=0;
 bool nativeMagazineAttached=false;
};
// This operation never calls the reload adapter and never writes ammunition.
// The caller executes Demand through HolsterInputOverride at the actual gather
// boundary, then supplies its real Commit receipt. Renderer bytes issue Pair.
class Bc2MagazineDetachGate {
public:
 bool Begin(const MagazineDetachGateSample&)noexcept;
 std::optional<HolsterSuppressionRequest> Demand(const MagazineDetachGateSample&)const noexcept;
 bool Commit(const MagazineDetachGateSample&,const HolsterSuppressionReceipt&,std::int64_t committedNs)noexcept;
 bool Return(const MagazineDetachGateSample&,const interaction::ReloadInsertionSeat&)noexcept;
 bool Finish(const MagazineDetachGateSample&,const MagazineDetachPairReceipt&)noexcept;
 void Invalidate()noexcept;
 // Stop-only recovery may use the original expired input packet, but never
 // renews its pose deadline or produces an attachment/return authorization.
 std::optional<HolsterSuppressionRequest> RecoveryDemand(const ReloadStateOwner&,
  const interaction::HandInteractionSample&,std::uint64_t nativeTick,std::uint32_t cache)const noexcept;
 std::optional<HolsterSuppressionRequest> ReplacementDemand(const ReloadStateOwner&,
  const interaction::HandInteractionSample&,std::uint64_t nativeTick,std::uint32_t cache)const noexcept;
 bool RetireReplacedOwner(const HolsterSuppressionRequest&,const HolsterSuppressionReceipt&)noexcept;
 MagazineDetachPhase Phase()const noexcept{return phase_;}
 bool BlocksActions()const noexcept{return phase_!=MagazineDetachPhase::Idle;}
 const std::optional<MagazineDetachAuthorization>& Authorization()const noexcept{return authorization_;}
 const std::optional<interaction::OriginalMagazine>& Original()const noexcept{return original_;}
 unsigned Returned()const noexcept{return returned_;}unsigned Recovered()const noexcept{return recovered_;}
private:
 bool Current(const MagazineDetachGateSample&,bool sameCounts)const noexcept;
 bool Sample(const MagazineDetachGateSample&)const noexcept;
 MagazineDetachPhase phase_=MagazineDetachPhase::Idle;
 std::uint64_t next_=0,request_=0;std::int64_t began_=0,returnNs_=0;
 unsigned returned_=0,recovered_=0;
 Bc2AmmoReserveLease baseline_{};MagazineFamilyBinding binding_{};
 interaction::HandInteractionSample recoveryInput_{};
 std::optional<interaction::OriginalMagazine> original_;
 std::optional<MagazineDetachAuthorization> authorization_;
};
}
