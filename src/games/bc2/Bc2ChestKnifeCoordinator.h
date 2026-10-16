#pragma once
#include "Bc2ChestKnifeNative.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeProbeWitness.h"
#include <atomic>
namespace fvr::bc2 {
// Actual gather coordinator. Owns no transport, hand allocator or inventory.
// Tick follows current suppression + TickHolster, before Tracking publication.
class Bc2ChestKnifeCoordinator {
public:
    Bc2ChestKnifeCoordinator()noexcept;
    void Tick(const WeaponModeMemory&,const BodyDrawSample&,BodyHolsterSample,
        const BodyHolsterResult&,const std::optional<ChestKnifeRenderReceipt>&,
        bool otherMechanicsSettled,Bc2BodyInventory&,Bc2BodyHolster&,
        interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    // Input absence revokes unconsumed stages; accepted native custody remains.
    void Missing(std::int64_t nowNs,interaction::HandInteraction&)noexcept;
    bool ClaimsHand()const noexcept{return policy_.Hold().has_value()||nativeOwned_;}
    const std::optional<ChestKnifeRenderSource>& Rendering()const noexcept{return rendering_;}
    std::optional<ChestKnifeProbeWitness> ProbeWitness(std::int64_t nowNs)const noexcept;
    void Report(std::ostream&)const;
private:
    interaction::ChestKnifeCustody policy_;
    interaction::KnifeRecognitionDiagnostic recognition_;
    std::optional<private_knife::Anchor> anchor_;
    std::optional<ChestKnifeRenderSource> rendering_;
    std::uint64_t bodyOperation_=0,draws_=0,returns_=0,rejected_=0,readGaps_=0;
    bool nativeOwned_=false;
    mutable std::atomic_flag witnessGate_=ATOMIC_FLAG_INIT;
    ChestKnifeProbeWitness witness_{};
};
}
