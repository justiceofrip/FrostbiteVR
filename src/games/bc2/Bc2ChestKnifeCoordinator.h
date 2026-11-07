#pragma once
#include "Bc2ChestKnifeNative.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeProbeWitness.h"
#include "Bc2ChestKnifeLiveObservation.h"
#include <atomic>
namespace fvr::bc2 {
// Actual gather coordinator. Owns no transport, hand allocator or inventory.
// Tick follows current suppression + TickHolster, before Tracking publication.
class Bc2ChestKnifeCoordinator {
public:
    Bc2ChestKnifeCoordinator()noexcept;
    bool TickIdleHeldGun(const BodyDrawSample&,const Bc2BodyHolster&,
        interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    void Tick(const WeaponModeMemory&,const BodyDrawSample&,BodyHolsterSample,
        const BodyHolsterResult&,const std::optional<ChestKnifeRenderReceipt>&,
        bool otherMechanicsSettled,Bc2BodyInventory&,Bc2BodyHolster&,
        interaction::HandInteraction&,std::uint64_t& sharedIntent,
        const std::optional<math::Matrix4>& currentBody=std::nullopt)noexcept;
    // Input absence revokes unconsumed stages; accepted native custody remains.
    void Missing(std::int64_t nowNs,interaction::HandInteraction&)noexcept;
    bool ClaimsHand()const noexcept{return policy_.Hold().has_value()||nativeOwned_;}
    const std::optional<ChestKnifeRenderSource>& Rendering()const noexcept{return rendering_;}
    std::optional<ChestKnifeProbeWitness> ProbeWitness(std::int64_t nowNs)const noexcept;
    void RequestLiveObservation(std::int64_t nowNs)noexcept{liveObservation_.Request(nowNs);}
    std::shared_ptr<const ChestKnifeLiveObservation> LiveObservation(std::int64_t nowNs)const noexcept{return liveObservation_.Read(nowNs);}
    std::shared_ptr<const ChestKnifeReturnObservation> ReturnObservation()const noexcept{return returnObservation_.Read();}
    void Report(std::ostream&)const;
private:
    void InvalidateLiveObservation()noexcept{liveObservation_.Invalidate();}
    interaction::ChestKnifeCustody policy_;
    interaction::KnifeRecognitionDiagnostic recognition_;
    std::optional<private_knife::Anchor> anchor_;
    std::optional<ChestKnifeRenderSource> rendering_;
    std::uint64_t bodyOperation_=0,draws_=0,returns_=0,rejected_=0,readGaps_=0;
    bool nativeOwned_=false;
    mutable std::atomic_flag witnessGate_=ATOMIC_FLAG_INIT;
    ChestKnifeProbeWitness witness_{};
    ChestKnifeLivePublication liveObservation_;
    ChestKnifeReturnPublication returnObservation_;
    std::optional<interaction::KnifeCommand> drawCommand_;
    std::optional<interaction::KnifeReceipt> drawReceipt_;
    std::optional<interaction::BodyItemKey> originalGun_;
};
}
