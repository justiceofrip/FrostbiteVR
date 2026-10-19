#include "Bf4ReloadClassifier.h"

#include <algorithm>
#include <cmath>

namespace fvr::bf4 {

using interaction::ReloadOperation;
using interaction::WeaponAction;
using interaction::WeaponFeed;
using interaction::WeaponMechanismDescriptor;

std::string_view Bf4ReloadArchetypeName(Bf4ReloadArchetype a) noexcept {
    switch (a) {
    case Bf4ReloadArchetype::ButtonOnly: return "button only";
    case Bf4ReloadArchetype::Magazine: return "magazine";
    case Bf4ReloadArchetype::Tube: return "tube (one round at a time)";
    case Bf4ReloadArchetype::Single: return "single shot";
    case Bf4ReloadArchetype::Belt: return "belt";
    case Bf4ReloadArchetype::Cylinder: return "cylinder";
    }
    return "?";
}

namespace {
constexpr std::int64_t kMs = 1000000;

bool ReloadableClass(std::int32_t c) {
    return c >= static_cast<std::int32_t>(Bf4WeaponClass::Assault) && c <= static_cast<std::int32_t>(Bf4WeaponClass::At);
}

Bf4ReloadClassification ButtonOnly(std::string_view why) { return {Bf4ReloadArchetype::ButtonOnly, {}, why}; }

void Plan(WeaponMechanismDescriptor& d, std::initializer_list<interaction::ManualReloadStep> steps,
          const Bf4WeaponReloadData& data) {
    auto& p = d.feedPlan;
    p.stepCount = 0;
    for (const auto& s : steps) p.steps[p.stepCount++] = s;
    // The native reload acknowledges after ReloadDelay + ReloadTime x threshold at most;
    // allow twice the full stock reload, within 1..10 s.
    const float stock = std::max(0.0f, data.reloadDelay) + std::max(0.0f, data.reloadTime);
    p.maxSampleGapNs = 100 * kMs;
    p.ackTimeoutNs = std::clamp(static_cast<std::int64_t>(2.0f * stock * 1000.0f) * kMs, 1000 * kMs, 10000 * kMs);
    p.transactionTimeoutNs = 60000 * kMs;
}

// The action that follows a shot, from the fire logic and (for pump vs bolt) the
// animation signal or the shotgun class.
WeaponAction CycleAfterShot(const Bf4WeaponReloadData& d) {
    if (d.fireLogic != static_cast<std::int32_t>(Bf4FireLogic::SingleFireWithBoltAction)) return WeaponAction::Automatic;
    return d.pumpActionSignal || d.weaponClass == static_cast<std::int32_t>(Bf4WeaponClass::Shotgun) ? WeaponAction::Pump
                                                                                                    : WeaponAction::Bolt;
}
} // namespace

Bf4ReloadClassification ClassifyReload(std::uint64_t weaponId, const Bf4WeaponReloadData& data, Bf4AuthoredFeed authored) {
    if (!weaponId) return ButtonOnly("no weapon identity");
    if (data.reloadType < 0 || data.reloadType > 2 || data.fireLogic < 0 || data.fireLogic > 5 || data.magazineCapacity < 1 ||
        !std::isfinite(data.reloadDelay) || !std::isfinite(data.reloadTime) || !std::isfinite(data.boltActionTime))
        return ButtonOnly("weapon data unreadable");
    if (!ReloadableClass(data.weaponClass)) return ButtonOnly("not a reloadable gun class");
    if (data.fireLogic == static_cast<std::int32_t>(Bf4FireLogic::Detonated)) return ButtonOnly("detonated weapon");
    if (data.fireLogic == static_cast<std::int32_t>(Bf4FireLogic::HoldAndRelease))
        return ButtonOnly("bow: nock and draw not modelled yet");
    const WeaponAction afterShot = CycleAfterShot(data);
    if (afterShot == WeaponAction::Bolt && !(data.boltActionTime > 0.0f))
        return ButtonOnly("bolt action without bolt timing");

    WeaponMechanismDescriptor d;
    d.id = weaponId;
    d.revision = kBf4ReloadClassifierRevision;
    d.afterShot = afterShot;
    const auto type = static_cast<Bf4ReloadType>(data.reloadType);
    const bool handgun = data.weaponClass == static_cast<std::int32_t>(Bf4WeaponClass::Handgun);

    if (authored == Bf4AuthoredFeed::Belt) {
        if (data.weaponClass != static_cast<std::int32_t>(Bf4WeaponClass::Lmg) || type == Bf4ReloadType::SingleBullet)
            return ButtonOnly("belt profile does not match this weapon's data");
        d.feed = WeaponFeed::Belt;
        d.afterEmptyFeed = WeaponAction::ChargingHandle;
        Plan(d, {{ReloadOperation::OpenBreech, 1}, {ReloadOperation::InsertRound, 1}, {ReloadOperation::CloseBreech, 1}}, data);
        return {Bf4ReloadArchetype::Belt, d, "belt-fed: open cover, lay belt, close cover"};
    }
    if (authored == Bf4AuthoredFeed::Cylinder) {
        if (!handgun || type == Bf4ReloadType::SingleBullet || data.magazineCapacity < 2)
            return ButtonOnly("cylinder profile does not match this weapon's data");
        d.feed = WeaponFeed::Clip; // swing-out cylinder + speedloader: open, insert, close
        Plan(d, {{ReloadOperation::OpenBreech, 1}, {ReloadOperation::InsertRound, 1}, {ReloadOperation::CloseBreech, 1}}, data);
        return {Bf4ReloadArchetype::Cylinder, d, "revolver: open cylinder, speedloader, close"};
    }
    if (type == Bf4ReloadType::SingleBullet) {
        // The native reload adds one round per loop; one VR round is one transaction.
        d.feed = WeaponFeed::InternalTube;
        d.afterEmptyFeed = afterShot; // a pump/bolt tube gun chambers with the same stroke
        Plan(d, {{ReloadOperation::InsertRound, 1}}, data);
        return {Bf4ReloadArchetype::Tube, d, "one round at a time"};
    }
    if (data.magazineCapacity == 1) {
        d.feed = WeaponFeed::Single;
        Plan(d, {{ReloadOperation::OpenBreech, 1}, {ReloadOperation::InsertRound, 1}, {ReloadOperation::CloseBreech, 1}}, data);
        return {Bf4ReloadArchetype::Single, d, "single shot: open, load, close"};
    }
    d.feed = WeaponFeed::DetachableMagazine;
    d.tacticalRetainsChamber = type == Bf4ReloadType::MagazineWithShorterReload;
    d.afterEmptyFeed = afterShot != WeaponAction::Automatic ? afterShot
                       : handgun                           ? WeaponAction::Slide
                                                           : WeaponAction::ChargingHandle;
    Plan(d, {{ReloadOperation::UnseatMagazine, 1}, {ReloadOperation::SeatMagazine, 1}}, data);
    return {Bf4ReloadArchetype::Magazine, d, "magazine: out, in, then charge when empty"};
}

} // namespace fvr::bf4
