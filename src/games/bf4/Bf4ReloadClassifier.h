#pragma once
// Manual-reload archetype of the equipped BF4 weapon, from the weapon's own reflected data
// (docs/BF4-MANUAL-RELOAD.md, sections 1 and 3). Pure BF4 adapter logic: it produces the
// framework's WeaponMechanismDescriptor and never guesses. Anything it cannot place stays
// on button reload. Producing a descriptor grants no native binding or permission.
#include "fvr/interaction/WeaponMechanism.h"

#include <cstdint>
#include <string_view>

namespace fvr::bf4 {

// Values as stored by bf4.exe build 179547 (reflection enum tables).
enum class Bf4ReloadType : std::int32_t { SingleBullet = 0, Magazine = 1, MagazineWithShorterReload = 2 };
enum class Bf4FireLogic : std::int32_t {
    SingleFire = 0, SingleFireWithBoltAction = 1, Automatic = 2, Burst = 3, HoldAndRelease = 4, Detonated = 5
};
enum class Bf4WeaponClass : std::int32_t {
    Assault = 16, Shotgun = 17, Smg = 18, Lmg = 19, Sniper = 20, Ugl = 21, Handgun = 22, At = 23
};

// Raw fields read from the equipped weapon (FiringFunctionData/AmmoConfigData, the
// magazine modifier already applied to capacity). -1 = not read.
struct Bf4WeaponReloadData {
    std::int32_t reloadType = -1;
    std::int32_t fireLogic = -1;   // the active fire mode's FireLogicType
    std::int32_t weaponClass = -1;
    std::int32_t magazineCapacity = 0;
    float reloadDelay = 0, reloadTime = 0, boltActionTime = 0;
    bool pumpActionSignal = false; // the weapon's animation uses the PumpAction signal
};

// What the data cannot say, from an authored, evidence-backed profile.
enum class Bf4AuthoredFeed : std::uint8_t { None, Belt, Cylinder };

enum class Bf4ReloadArchetype : std::uint8_t { ButtonOnly, Magazine, Tube, Single, Belt, Cylinder };
std::string_view Bf4ReloadArchetypeName(Bf4ReloadArchetype archetype) noexcept;

struct Bf4ReloadClassification {
    Bf4ReloadArchetype archetype = Bf4ReloadArchetype::ButtonOnly;
    interaction::WeaponMechanismDescriptor descriptor{}; // valid only when archetype != ButtonOnly
    std::string_view reason;                             // why, in player-readable words
};

inline constexpr std::uint64_t kBf4ReloadClassifierRevision = 1;

// weaponId: the adapter's session identity for this weapon (non-zero).
Bf4ReloadClassification ClassifyReload(std::uint64_t weaponId, const Bf4WeaponReloadData& data,
                                       Bf4AuthoredFeed authored = Bf4AuthoredFeed::None);

} // namespace fvr::bf4
