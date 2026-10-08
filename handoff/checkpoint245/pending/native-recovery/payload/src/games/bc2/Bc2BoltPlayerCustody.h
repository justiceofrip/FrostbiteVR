#pragma once
#include "Bc2BoltCustodyBridge.h"
#include "Bc2AmmoReserve.h"
namespace fvr::bc2 {
#if defined(FVR_BC2_ORDINARY_BOLT)
inline constexpr bool OrdinaryBoltCompiled=true;
#else
inline constexpr bool OrdinaryBoltCompiled=false;
#endif
// Policy only: consumes the player's unchanged controls. The native service
// must already be configured for the exact fixed family by its reviewed host.
// Prepare MUST precede the shared hands.Update on a real gun release edge.
struct Bc2BoltPlayerInput {
    ReloadStateOwner owner{};interaction::HandInteractionKey item{};
    std::string_view asset{},mesh{};
    const interaction::InputFrame* controls=nullptr;
    interaction::HandInteractionSample safety{};Bc2BoltBodyFrame body{};
    Bc2BoltControllerContact contact{};
    std::optional<Bc2NativeCycleView> native;
    std::optional<Bc2AmmoReserveLease> ammunition;
    std::optional<Bc2NativeCycleRecoveryView> recovery;
};
class Bc2BoltPlayerCustody {
public:
    explicit Bc2BoltPlayerCustody(std::shared_ptr<const Bc2BoltCalibration> calibration)noexcept:calibration_(std::move(calibration)){}
    std::optional<Bc2PhysicalBoltSample> Prepare(const Bc2BoltPlayerInput&,const interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    void Observe(const Bc2PhysicalBoltResult&)noexcept;
    bool ReferencesReady()const noexcept{return references_.has_value();}
    bool BlocksFire()const noexcept{return debt_||retirementRequired_;}
    bool NeedsOwnerRetirement()const noexcept{return retirementRequired_;}
    bool OwnsGunCustody()const noexcept{return custody_==interaction::BoltCustodyPhase::Manipulating;}
    std::optional<std::array<math::Matrix4,2>> References()const noexcept{return references_;}
private:
    void Invalidate()noexcept;
    void Capture(const Bc2BoltPlayerInput&)noexcept;
    std::shared_ptr<const Bc2BoltCalibration> calibration_;
    ReloadStateOwner owner_{};interaction::HandInteractionKey item_{};float units_=0;
    std::optional<std::array<math::Matrix4,2>> references_,candidate_;
    interaction::HandInteractionSample previous_{};
    // Retained original geometry/identity only. Restoring player coordination
    // still requires a new typed idle-debt grant and the SAME live left gun.
    std::optional<std::array<math::Matrix4,2>> recoveryReferences_;
    std::optional<interaction::WeaponCycleLease> debtLease_;
    std::optional<interaction::HandClaimToken> leftCustody_;
    std::int64_t selectedAt_=0,stableAt_=0,lastNow_=0;
    std::uint64_t lastRaw_=0,submitted_=0,neutral_=0,returnPress_=0;
    interaction::BoltCustodyPhase custody_=interaction::BoltCustodyPhase::Idle;
    bool debt_=false,ready_=false,retirementRequired_=false,rightArmed_=false,rightHeld_=false;
};
}
