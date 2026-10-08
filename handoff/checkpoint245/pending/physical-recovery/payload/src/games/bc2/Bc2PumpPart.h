#pragma once
#include "Bc2ReloadPresentation.h"
#include "fvr/interaction/WeaponCycle.h"
#include "fvr/interaction/PhysicalWeaponCycle.h"
namespace fvr::bc2 {
// Derived from 96 native SPAS poses in trace041114. This is a candidate part
// measurement, not a runtime feature gate or a grab-point calibration.
inline constexpr float SpasObservedForeEndStroke=.09495844f;
inline constexpr std::string_view SpasForeEndBone="jntWpn_4";
struct Bc2PumpPartBinding {std::uint64_t fingerprint=0;std::uint32_t weapon=0,part=0;};
namespace bc2_pump_detail {
std::optional<Bc2PumpPartBinding> DerivePart(const RigSnapshot&);

}
std::optional<Bc2PumpPartBinding> BindSpasPumpPart(const RigSnapshot&,std::string_view asset,std::string_view mesh);
template<class Authority> struct Bc2PumpPartSourceT {
    Authority lease{};
    RigIdentity rig{};
    // Immutable closed part captured at the held native7->8 boundary. A
    // renderer must not substitute its latest animated/snap-guided part here.
    math::Matrix4 closedPartFromWeapon{};
    float rearDirection=0; // +1 or -1 local Z, verified from a state-labelled stroke; default unbound.
    interaction::HandClaimToken mechanism{},gun{};
    std::uint64_t inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool nativeBoundaryVerified=false;
};
using Bc2PumpPartSource=Bc2PumpPartSourceT<interaction::WeaponCycleLease>;
using Bc2PumpRecoveryPartSource=Bc2PumpPartSourceT<interaction::WeaponCycleIdleDebtLease>;
// Private one-part palette plan. No installation/calls/native writes exist.
// Normal rendering never calls this until the native fired-cohort test and
// closed-part/hand binding are accepted. The ordinary animation remains source.
std::optional<RigPosePlan> BuildSpasPumpPart(const RigSnapshot&,const Bc2PumpPartBinding&,
    const Bc2PumpPartSource&,const interaction::WeaponCycleLease& current,
    const interaction::HandInteractionSample&,const interaction::HandClaim& currentMechanism,
    const interaction::HandClaim& currentGun,const math::Matrix4& placedWeapon,
    float unitsPerMetre,float travelMetres);
// Consumer-facing path: presentation must originate from the current shared
// physical cycle, not an unrelated travel float. Native boundary/contact
// calibration remains an explicit adapter prerequisite, disabled by default.
std::optional<RigPosePlan> BuildSpasPumpCyclePart(const RigSnapshot&,const Bc2PumpPartBinding&,
    const Bc2PumpPartSource&,const interaction::WeaponCycleProfile&,const interaction::PhysicalWeaponCycleTarget&,
    const interaction::WeaponCycleLease& current,const interaction::HandInteractionSample&,
    const interaction::HandClaim& currentMechanism,const interaction::HandClaim& currentGun,
    const math::Matrix4& placedWeapon,float unitsPerMetre);
std::optional<RigPosePlan> BuildSpasPumpRecoveryPart(const RigSnapshot&,const Bc2PumpPartBinding&,
    const Bc2PumpRecoveryPartSource&,const interaction::WeaponCycleProfile&,const interaction::PhysicalWeaponCycleIdleDebtTarget&,
    const interaction::WeaponCycleIdleDebtLease& current,const interaction::HandInteractionSample&,
    const interaction::HandClaim& currentMechanism,const interaction::HandClaim& currentGun,
    const math::Matrix4& placedWeapon,float unitsPerMetre);
}
