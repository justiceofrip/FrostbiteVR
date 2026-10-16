#pragma once
#include "Bc2BodyInventory.h"
#include "Bc2InputBinding.h"
#include "fvr/interaction/ControllerInput.h"

namespace fvr::bc2 {
// Read-only native evidence. These functions create neither hand custody nor a
// native selection acknowledgement. The eventual adapter must enter the shared
// TemporaryTool interval before dispatching a deliberate selection edge.
struct ContextualToolBinding {
    std::uint32_t imageBase=0,imageSize=0;
    bool verified=false;
};
std::optional<ContextualToolBinding> VerifyContextualToolBinding(
    const WeaponModeMemory&,std::uint32_t imageBase,std::uint32_t imageSize)noexcept;
struct ContextualToolData {
    BodyNativeItem item{};
    std::uint32_t firing=0;
    std::array<char,192> asset{};
    bool operator==(const ContextualToolData&)const=default;
};
struct ContextualToolObservation {
    BodyNativeInventory inventory{};
    ContextualToolData tool{},primary{};
    std::uint32_t lastPrimary=0;
    bool operator==(const ContextualToolObservation&)const=default;
};
// The class's native metadata and runtime asset identity are both verified.
// Null Persistence is valid for an authored campaign tool, never invented.
std::optional<ContextualToolObservation> ReadContextualTool(
    const WeaponModeMemory&,const ContextualToolBinding&,const ReloadStateOwner&)noexcept;
std::optional<WeaponModeCommand> ResolveStaticToolEquip(const ContextualToolObservation&)noexcept;
std::optional<WeaponModeCommand> ResolveStaticToolReturn(const ContextualToolObservation&,
    const BodyNativeItem& original)noexcept;
// Reports eligibility for Zoom14, only while this exact tool is selected and the caller's
// actual tool GunHold is still current. It does not modify ordinary firearm ADS.
// This is one admission component; a future native writer also needs current
// shared TemporaryTool custody and a distinct, truthful suppression receipt.
class ContextualToolSecondary {
public:
    bool Update(const ContextualToolObservation&,const interaction::InputFrame&,
        const interaction::HandInteractionSample&,const interaction::HandClaimToken&,
        const interaction::HandInteraction&)noexcept;
    void Reset()noexcept{armed_=false;lastSequence_=0;lastObserved_=lastDeadline_=lastPredicted_=0;lastTrigger_=0;identity_.reset();}
private:
    struct Identity {ReloadStateOwner owner{};ContextualToolData tool{};
        interaction::HandClaimToken hold{};bool operator==(const Identity&)const=default;};
    std::optional<Identity> identity_;
    std::uint64_t lastSequence_=0;std::int64_t lastObserved_=0,lastDeadline_=0,lastPredicted_=0;
    float lastTrigger_=0;
    bool armed_=false;
};
}
