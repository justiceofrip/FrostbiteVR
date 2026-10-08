#pragma once
#include "Bc2NativeCycleService.h"
#include "Bc2Rig.h"
#include "fvr/interaction/BoltWeaponCustody.h"
#include <memory>
#include <string>

namespace fvr::bc2 {
// Immutable selected-profile geometry. Authored measurements alone must leave
// nativeJoined false; the adapter never derives admission from matching a name.
struct Bc2BoltCalibration {
    std::string asset,mesh,partName;
    std::uint64_t rigFingerprint=0;
    interaction::WeaponCycleProfile profile{};
    math::Matrix4 wristFromPart{};
    bool nativeJoined=false;
};
bool BoltCalibrationValid(const Bc2BoltCalibration&)noexcept;
struct Bc2BoltRawContact {
    ReloadStateOwner nativeOwner{};RigIdentity rig{};std::uint64_t rigFingerprint=0;
    interaction::HandInteractionSample input{};
    math::Matrix4 mechanismWristInWeapon{};
    bool valid=false;
};
struct Bc2BoltTracking {
    bool enabled=false;
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    std::shared_ptr<const Bc2BoltCalibration> calibration;
    std::optional<interaction::WeaponCycleLease> held;
    std::optional<interaction::PhysicalWeaponCycleTarget> target;
    std::optional<interaction::BoltCustodyWeaponTarget> weapon;
    std::optional<interaction::HandClaim> mechanism,gun;
    interaction::BoltCustodyPhase custody=interaction::BoltCustodyPhase::Idle;
    interaction::WeaponCyclePhase mechanismPhase=interaction::WeaponCyclePhase::Idle;
};
struct Bc2PhysicalBoltApi {
    void* context=nullptr;
    bool (*control)(void*,const Bc2NativeCycleControl&)noexcept=nullptr;
    std::optional<Bc2NativeCycleView> (*view)(void*,std::int64_t)noexcept=nullptr;
    bool (*ack)(void*,const interaction::WeaponCycleReady&)noexcept=nullptr;
    void (*cancel)(void*)noexcept=nullptr;
};
struct Bc2PhysicalBoltSample {
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey item{};
    std::string_view asset{},mesh{};
    Bc2BoltRawContact raw{};
    // Current real controller poses, in canonical world metres. This is not
    // renderer N-1 mechanism contact and must belong to input.sequence.
    std::array<math::Matrix4,2> wristWorld{};
    math::Matrix4 weaponWorld{};
    interaction::HandContactProof gunContact{};
    std::array<interaction::HandContactProof,2> gunContacts{};
    // Explicit current grip intentions plus original per-destination contact.
    // Passing a transfer grants no authority until the shared atomic arbiter
    // validates both old claims and both original destination evidence packets.
    std::optional<interaction::HandGunCustodyTransfer> custodyTransfer;
    bool grip=false,cancel=false;
};
// Observation only: retain original evidence at the first shared recognizer
// cancellation. This cannot renew a claim, grant a target or settle native debt.
struct Bc2BoltPhysicalCancellation {
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    Bc2BoltRawContact raw{};
    interaction::WeaponCycleLease lease{};
    interaction::HandContactProof gunContact{};
    math::Matrix4 gunWristInWorld{};
    std::array<std::optional<interaction::HandClaim>,2> before{},after{};
    interaction::WeaponCycleFailure failure=interaction::WeaponCycleFailure::None;
    float travel=0,rotation=0;
    bool rawAdmitted=false,grip=false;
};
struct Bc2PhysicalBoltResult {
    Bc2BoltTracking tracking{};
    bool blocksFire=false,ownsGunCustody=false,ownsMechanism=false;
    std::optional<interaction::HandGunCustodyResult> custodyChange;
    std::optional<interaction::WeaponCycleReady> settled;
    std::optional<Bc2BoltPhysicalCancellation> firstCancellation;
};
// One instance per interaction domain. The ordinary gun/support adapter must
// yield its claims while ownsGunCustody is true and adopt custodyChange claims
// together. No native activation, hook, pointer write or input action lives here.
class Bc2PhysicalBolt {
public:
    Bc2PhysicalBolt(std::shared_ptr<const Bc2BoltCalibration>,Bc2PhysicalBoltApi)noexcept;
    Bc2PhysicalBoltResult Tick(const Bc2PhysicalBoltSample&,interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    bool BlocksFire()const noexcept{return blocks_;}
    void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
private:
    std::shared_ptr<const Bc2BoltCalibration> calibration_;
    Bc2PhysicalBoltApi api_{};
    interaction::BoltWeaponCustody custody_;
    std::optional<interaction::WeaponCycleLease> cycle_;
    std::optional<interaction::WeaponCycleRelease> pending_;
    std::optional<interaction::WeaponCycleReady> acknowledged_;
    ReloadStateOwner owner_{};
    std::optional<Bc2BoltPhysicalCancellation> firstCancellation_;
    bool blocks_=false,settled_=false;
};
}
