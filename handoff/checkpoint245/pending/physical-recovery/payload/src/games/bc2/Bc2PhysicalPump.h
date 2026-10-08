#pragma once
#include "Bc2NativeCycleService.h"
#include "Bc2NativeCycleRecovery.h"
#include "Bc2PumpPart.h"
#include "fvr/interaction/SupportGrip.h"
#include <atomic>
#include <memory>
#include <iosfwd>
#include <variant>
namespace fvr::bc2 {
// Explicit measured calibration; there is deliberately no default accepted row.
// Canonical weapon-local metres, from the original state-labelled native rig.
struct Bc2PumpCalibration {
    std::uint64_t revision=0,rigFingerprint=0;
    math::Matrix4 closedPart{},closedWrist{};
    float rearDirection=0;
    bool measured=false;
};
bool PumpCalibrationValid(const Bc2PumpCalibration&)noexcept;
struct Bc2PumpRawContact {
    ReloadStateOwner nativeOwner{};RigIdentity rig{};std::uint64_t rigFingerprint=0;
    interaction::HandInteractionSample input{};
    math::Matrix4 contact{};math::Vec3 pointWrist{};
    bool valid=false;
    // Original renderer measurements for finite controller diagnostics only.
    // These never replace contact or authorize a native/physical cycle.
    math::Matrix4 trackingBodyWorldMeters{},weaponWorldMeters{},rawWristWorldMeters{};
    bool mappingValid=false;
};
struct Bc2PumpSupportCapture {
    std::uint64_t token=0;interaction::HandClaimToken support{},gun{};
};
struct Bc2PumpTracking {
    bool enabled=false;
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    std::shared_ptr<const Bc2PumpCalibration> calibration;
    interaction::WeaponCycleProfile profile{};
    std::optional<interaction::WeaponCycleLease> held;
    std::optional<interaction::PhysicalWeaponCycleTarget> target;
    std::optional<interaction::HandClaim> mechanism,gun;
    math::Vec3 pointWrist{};
    std::optional<Bc2PumpSupportCapture> supportCapture;
    std::optional<interaction::WeaponCycleIdleDebtLease> recovery;
    std::optional<interaction::PhysicalWeaponCycleIdleDebtTarget> recoveryTarget;
};
// Returned support is a new current support claim, never the expired mechanism.
std::optional<math::Matrix4> ResolvePumpSupportWrist(const Bc2PumpTracking&,const RigSnapshot&,
    std::uint64_t supportToken,std::int64_t now)noexcept;
bool PumpTrackingFresh(const Bc2PumpTracking&,std::int64_t now)noexcept;
bool PumpTargetFresh(const Bc2PumpTracking&,std::int64_t now)noexcept;
bool PumpTargetRetained(const Bc2PumpTracking&,const Bc2PumpTracking&,std::int64_t now)noexcept;
Bc2PumpRawContact BuildPumpRawContact(const Bc2PumpTracking&,const RigSnapshot&,const math::Matrix4& rawWrist,
    const math::Matrix4& placedWeapon,math::Vec3 pointWrist,float units,std::int64_t now,
    const math::Matrix4* trackingBody=nullptr)noexcept;
struct Bc2PumpPresentation {
    std::optional<math::Matrix4> wrist;
    std::optional<interaction::BoneWrite> part;
};
Bc2PumpPresentation BuildPumpPresentation(const Bc2PumpTracking&,const RigSnapshot&,const math::Matrix4& placedWeapon,
    float units,std::int64_t now)noexcept;
struct Bc2PhysicalPumpApi {
    void* context=nullptr;
    bool (*control)(void*,const Bc2NativeCycleControl&)noexcept=nullptr;
    std::optional<Bc2NativeCycleView> (*view)(void*,std::int64_t)noexcept=nullptr;
    bool (*ack)(void*,const interaction::WeaponCycleReady&)noexcept=nullptr;
    void (*cancel)(void*)noexcept=nullptr;
    Bc2PhysicalCycleRecoveryApi recovery{};
};
struct Bc2PhysicalPumpSample {
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey item{};
    Bc2PumpRawContact raw{};
    bool grip=false,cancel=false;
    std::string_view asset{};
};
struct Bc2PhysicalPumpResult {
    Bc2PumpTracking tracking{};
    bool blocksFire=false,ownsHand=false;
    // Observational copy emitted only after exact physical reconciliation and
    // successful acknowledgement by the real native API.
    std::optional<interaction::WeaponCycleReady> settled;
    std::optional<interaction::WeaponCycleIdleDebtReady> recovered;
};
struct Bc2PumpPackCounters {unsigned contacts=0,poses=0,copies=0,pairs=0,fallbacks=0;};
class Bc2PhysicalPump {
public:
    Bc2PhysicalPump(std::shared_ptr<const Bc2PumpCalibration>,Bc2PhysicalPumpApi)noexcept;
    Bc2PhysicalPumpResult Tick(const Bc2PhysicalPumpSample&,interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    std::optional<interaction::SupportGripResult> ContinueSupport(const interaction::SupportGripOwner&,
        const interaction::InputFrame&,const interaction::SupportGripContact&,const interaction::HandInteractionSample& original,
        interaction::HandInteractionKey supportContact,interaction::HandInteraction&,interaction::SupportGrip&,std::uint64_t& intent,bool cancel)noexcept;
    void BindSupport(Bc2PumpTracking&,const interaction::SupportGripResult&,const interaction::HandInteraction&)noexcept;
    bool BlocksFire()const noexcept{return blocks_;}
    void Report(std::ostream&)const;
private:
    void DropPhysical(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    Bc2PhysicalPumpResult TickRecovery(const Bc2PhysicalPumpSample&,const Bc2NativeCycleRecoveryView&,
        interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    std::shared_ptr<const Bc2PumpCalibration> calibration_;
    Bc2PhysicalPumpApi api_{};interaction::PhysicalWeaponCycle physical_;
    interaction::WeaponCycleProfile profile_{};
    std::optional<interaction::WeaponCycleLease> cycle_;
    std::optional<interaction::WeaponCycleRelease> pending_;
    std::optional<interaction::WeaponCycleReady> acknowledged_;
    interaction::PhysicalWeaponCycleIdleDebt recoveryPhysical_;
    std::optional<interaction::WeaponCycleIdleDebtLease> recoveryCycle_;
    std::optional<interaction::WeaponCycleIdleDebtRelease> recoveryPending_;
    std::optional<interaction::WeaponCycleIdleDebtReady> recoveryAcknowledged_;
    math::Vec3 pointWrist_{};
    struct SupportReturn {
        std::variant<interaction::WeaponCycleRelease,interaction::WeaponCycleIdleDebtRelease> release{};
        interaction::HandClaimToken gun{};ReloadStateOwner nativeOwner{};
        interaction::HandInteractionSample last{};std::int64_t readyNs=0;std::uint64_t releasedAtSequence=0;
    };
    std::optional<SupportReturn> supportReturn_;
    std::optional<Bc2PumpSupportCapture> supportCapture_;
    Bc2PumpRawContact supportRaw_{};
    bool blocks_=false;
    std::atomic<std::uint64_t> supportReturns_=0,supportReturnLost_=0;
    std::atomic<std::uint64_t> ticks_=0,cancels_=0,controlRejected_=0,rawRejected_=0,begins_=0,targets_=0,releases_=0,acks_=0;
};
}

