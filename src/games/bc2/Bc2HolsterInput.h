#pragma once
#include "Bc2InputBinding.h"
#include "Bc2ReloadState.h"
#include "fvr/interaction/HandInteraction.h"

namespace fvr::bc2 {
struct HolsterSuppressionRequest {
    ReloadStateOwner owner{};
    interaction::HandInteractionSample input{};
    std::uint64_t request=0,nativeTick=0;
    std::uint32_t cache=0;
};
// Produced only after this native gather's actual cache was staged, read back,
// and committed while the exact full owner/cache/tick still matched. It cannot
// prove suppression during another gather, even if the XR packet is unchanged.
struct HolsterSuppressionReceipt : HolsterSuppressionRequest {bool equipmentDispatched=false;};
struct HolsterInputOwner {
    void* context=nullptr;
    bool (*current)(void*,const HolsterSuppressionRequest&)noexcept=nullptr;
};
bool HolsterSuppressionCurrent(const HolsterSuppressionReceipt&,const HolsterSuppressionRequest&)noexcept;
// Used AFTER ordinary InputOverride staging, BEFORE its Commit. This owns only
// weapon-action fields. Failure restores only unchanged writes; no native call.
// A deliberate adapter-verified equipment pulse may run after suppression, but
// then no EmptyHands suppression receipt is issued on that dispatch tick.
class HolsterInputOverride {
public:
    ~HolsterInputOverride(){Restore();}
    HolsterInputOverride()=default;
    HolsterInputOverride(const HolsterInputOverride&)=delete;
    HolsterInputOverride& operator=(const HolsterInputOverride&)=delete;
    bool Apply(std::span<std::byte>,const HolsterSuppressionRequest&,HolsterInputOwner,std::optional<EntryAction> verifiedEquipment={})noexcept;
    std::optional<HolsterSuppressionReceipt> Commit()noexcept;
    bool Restore()noexcept;
private:
    std::span<std::byte> cache_{};HolsterSuppressionRequest request_{};HolsterInputOwner owner_{};
    std::array<std::uint32_t,4> before_{},written_{};bool active_=false,equipment_=false;
};
}
