#pragma once
#include "Bc2BodyHolster.h"
namespace fvr::bc2 {
struct BodyHolsterPackCounters {
    std::uint64_t poses=0,packedCopies=0,pairedCopies=0,fallbacks=0;
};
// Immutable diagnostic observation, never a command or an acknowledgement.
// Original input/hand timestamps survive unchanged. Cumulative pack counters
// are not request-scoped proof; use the genuine visibility receipt for that.
struct BodyHolsterProbeSample {
    std::int64_t sampledNs=0,trialStartNs=0,trialDeadlineNs=0;
    ReloadStateOwner nativeOwner{};
    interaction::InputFrame input{};
    interaction::HandInteractionSample hand{};
    // Exact physical key consumed by TickHolster; XM8 uses its shared family,
    // which is distinct from the selected native rifle/inventory item.
    interaction::HandInteractionKey physicalGun{};
    std::uint64_t nativeTick=0,request=0;
    BodyHolsterPhase phase=BodyHolsterPhase::Disabled;
    std::optional<interaction::BodySlotAssignment> selectedSlot;
    interaction::BodyAnchorConfig anchors{};
    std::optional<interaction::HandClaim> left,right;
    BodyHolsterResult outcome;
    std::optional<BodyVisibleRig> ordinaryVisible; // Original read-only presentation; no hide/show authority.
    std::optional<WeaponVisibilityReceipt> visibility;
    std::optional<HolsterSuppressionReceipt> suppression;
    std::uint32_t queuedTarget=0;
    BodyHolsterPackCounters pack;
    // Post-commit ordinary native input readback, not a shot receipt.
    std::uint64_t fireTickMs=0;bool fireRequested=false,fireCacheRead=false;float fireCache=0;
    static constexpr bool productionVisibilityAccepted=false,productionInputAccepted=false;
};
// TickHolster consumes its command sample by value. Original rig visibility
// belongs to BodyDrawSample, including the unsupported-profile ordinary route;
// copying the caller's unfilled command sample would discard genuine evidence.
inline void CaptureBodyHolsterProbeSource(BodyHolsterProbeSample& out,const BodyDrawSample& source)noexcept {
    out.nativeOwner=source.owner;out.input=source.input;out.hand=source.hand;
    out.physicalGun=source.gun;out.ordinaryVisible=source.visible;
}
}
