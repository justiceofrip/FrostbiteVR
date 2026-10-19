// Manual-reload archetypes from BF4's own weapon data (Bf4ReloadClassifier): every
// produced descriptor must satisfy the framework's mechanism contract, and anything the
// data cannot place stays on button reload.
#include "TestHarness.h"

#include "../../src/games/bf4/Bf4FiringCensus.h"
#include "../../src/games/bf4/Bf4ReloadClassifier.h"

#include <cmath>
#include <limits>

using namespace fvr;
using namespace fvr::bf4;
using interaction::ChamberKnowledge;
using interaction::WeaponAction;
using interaction::WeaponFeed;
using interaction::WeaponMechanismIntent;
using interaction::WeaponMechanismStatus;

namespace {
Bf4WeaponReloadData Gun(Bf4WeaponClass c, Bf4ReloadType r, Bf4FireLogic f, int capacity) {
    Bf4WeaponReloadData d;
    d.weaponClass = static_cast<int>(c);
    d.reloadType = static_cast<int>(r);
    d.fireLogic = static_cast<int>(f);
    d.magazineCapacity = capacity;
    d.reloadDelay = 0.2f;
    d.reloadTime = 2.4f;
    return d;
}
bool Valid(const Bf4ReloadClassification& c) { return interaction::ValidWeaponMechanismDescriptor(c.descriptor); }
} // namespace

TEST_CASE(ReloadClassifierPlacesEveryFamily) {
    // Assault rifle, 30+1: magazine, chamber kept on a tactical reload, charge when empty.
    auto ar = ClassifyReload(1, Gun(Bf4WeaponClass::Assault, Bf4ReloadType::MagazineWithShorterReload, Bf4FireLogic::Automatic, 31));
    CHECK(ar.archetype == Bf4ReloadArchetype::Magazine && Valid(ar));
    CHECK(ar.descriptor.feed == WeaponFeed::DetachableMagazine && ar.descriptor.tacticalRetainsChamber);
    CHECK(ar.descriptor.afterEmptyFeed == WeaponAction::ChargingHandle && ar.descriptor.afterShot == WeaponAction::Automatic);
    const auto tactical = interaction::SelectWeaponMechanismPlan(ar.descriptor, WeaponMechanismIntent::TacticalFeed, ChamberKnowledge::Occupied);
    CHECK(tactical.status == WeaponMechanismStatus::Plan && tactical.plan->action == WeaponAction::Automatic);
    const auto empty = interaction::SelectWeaponMechanismPlan(ar.descriptor, WeaponMechanismIntent::EmptyFeed, ChamberKnowledge::Empty);
    CHECK(empty.status == WeaponMechanismStatus::Plan && empty.plan->action == WeaponAction::ChargingHandle);

    // Pistol: slide after an empty magazine. ReloadType 1 keeps no chamber distinction.
    auto pistol = ClassifyReload(2, Gun(Bf4WeaponClass::Handgun, Bf4ReloadType::Magazine, Bf4FireLogic::SingleFire, 17));
    CHECK(pistol.archetype == Bf4ReloadArchetype::Magazine && Valid(pistol));
    CHECK(pistol.descriptor.afterEmptyFeed == WeaponAction::Slide && !pistol.descriptor.tacticalRetainsChamber);

    // Bolt sniper with a magazine: bolt after every shot and to chamber after a reload.
    auto sniper = Gun(Bf4WeaponClass::Sniper, Bf4ReloadType::Magazine, Bf4FireLogic::SingleFireWithBoltAction, 5);
    sniper.boltActionTime = 0.9f;
    auto bolt = ClassifyReload(3, sniper);
    CHECK(bolt.archetype == Bf4ReloadArchetype::Magazine && Valid(bolt));
    CHECK(bolt.descriptor.afterShot == WeaponAction::Bolt && bolt.descriptor.afterEmptyFeed == WeaponAction::Bolt);
    const auto shot = interaction::SelectWeaponMechanismPlan(bolt.descriptor, WeaponMechanismIntent::AfterShot, ChamberKnowledge::Empty);
    CHECK(shot.status == WeaponMechanismStatus::Plan && shot.plan->gestureFamily == interaction::WeaponCycleFamily::Bolt);
    sniper.boltActionTime = 0.0f; // contradictory data: never guess
    CHECK(ClassifyReload(3, sniper).archetype == Bf4ReloadArchetype::ButtonOnly);

    // Pump shotgun: tube, one round per transaction, pump after a shot and after loading empty.
    auto pumpData = Gun(Bf4WeaponClass::Shotgun, Bf4ReloadType::SingleBullet, Bf4FireLogic::SingleFireWithBoltAction, 7);
    pumpData.boltActionTime = 0.5f;
    auto pump = ClassifyReload(4, pumpData);
    CHECK(pump.archetype == Bf4ReloadArchetype::Tube && Valid(pump));
    CHECK(pump.descriptor.feed == WeaponFeed::InternalTube && pump.descriptor.afterShot == WeaponAction::Pump);
    CHECK(pump.descriptor.feedPlan.stepCount == 1 && pump.descriptor.feedPlan.steps[0].repeats == 1);
    // Semi-automatic tube shotgun: no stroke owed.
    auto semi = ClassifyReload(5, Gun(Bf4WeaponClass::Shotgun, Bf4ReloadType::SingleBullet, Bf4FireLogic::SingleFire, 7));
    CHECK(semi.archetype == Bf4ReloadArchetype::Tube && semi.descriptor.afterShot == WeaponAction::Automatic && Valid(semi));

    // Launcher / underbarrel: single shot, open-load-close.
    auto rpg = ClassifyReload(6, Gun(Bf4WeaponClass::At, Bf4ReloadType::Magazine, Bf4FireLogic::SingleFire, 1));
    CHECK(rpg.archetype == Bf4ReloadArchetype::Single && rpg.descriptor.feed == WeaponFeed::Single && Valid(rpg));

    // Authored profiles where the data is silent.
    auto lmg = Gun(Bf4WeaponClass::Lmg, Bf4ReloadType::Magazine, Bf4FireLogic::Automatic, 100);
    CHECK(ClassifyReload(7, lmg).archetype == Bf4ReloadArchetype::Magazine); // box-fed unless authored
    auto belt = ClassifyReload(7, lmg, Bf4AuthoredFeed::Belt);
    CHECK(belt.archetype == Bf4ReloadArchetype::Belt && belt.descriptor.feed == WeaponFeed::Belt && Valid(belt));
    CHECK(ClassifyReload(8, Gun(Bf4WeaponClass::Assault, Bf4ReloadType::Magazine, Bf4FireLogic::Automatic, 30), Bf4AuthoredFeed::Belt)
              .archetype == Bf4ReloadArchetype::ButtonOnly); // a belt profile on a rifle is a data mismatch
    auto revolver = ClassifyReload(9, Gun(Bf4WeaponClass::Handgun, Bf4ReloadType::Magazine, Bf4FireLogic::SingleFire, 6),
                                   Bf4AuthoredFeed::Cylinder);
    CHECK(revolver.archetype == Bf4ReloadArchetype::Cylinder && revolver.descriptor.feed == WeaponFeed::Clip && Valid(revolver));
}

TEST_CASE(ReloadClassifierKeepsUnknownsOnButtonReload) {
    const auto rifle = Gun(Bf4WeaponClass::Assault, Bf4ReloadType::Magazine, Bf4FireLogic::Automatic, 30);
    CHECK(ClassifyReload(0, rifle).archetype == Bf4ReloadArchetype::ButtonOnly); // no identity
    auto bad = rifle;
    bad.reloadType = 7;
    CHECK(ClassifyReload(1, bad).archetype == Bf4ReloadArchetype::ButtonOnly);
    bad = rifle;
    bad.reloadTime = std::numeric_limits<float>::quiet_NaN();
    CHECK(ClassifyReload(1, bad).archetype == Bf4ReloadArchetype::ButtonOnly);
    bad = rifle;
    bad.weaponClass = 28; // wcKnife
    CHECK(ClassifyReload(1, bad).reason == "not a reloadable gun class");
    auto bow = Gun(Bf4WeaponClass::Sniper, Bf4ReloadType::Magazine, Bf4FireLogic::HoldAndRelease, 1);
    CHECK(ClassifyReload(1, bow).archetype == Bf4ReloadArchetype::ButtonOnly);
    auto mine = Gun(Bf4WeaponClass::At, Bf4ReloadType::Magazine, Bf4FireLogic::Detonated, 1);
    CHECK(ClassifyReload(1, mine).archetype == Bf4ReloadArchetype::ButtonOnly);
    // Timeouts follow the stock reload: 2 x (0.2 + 2.4) s = 5.2 s, inside 1..10 s.
    const auto c = ClassifyReload(1, rifle);
    CHECK(c.descriptor.feedPlan.ackTimeoutNs == 5200ll * 1000000ll);
}

TEST_CASE(FiringCensusRulesMatchTheStaticLayout) {
    // A firing object and its FiringFunctionData laid out as in docs/BF4-MANUAL-RELOAD.md 2.1.
    std::uint8_t obj[kFiringObjectBytes] = {};
    std::uint8_t data[kFiringDataBytes] = {};
    auto put = [](std::uint8_t* p, std::size_t off, auto v) { std::memcpy(p + off, &v, sizeof(v)); };
    const std::uint64_t dataAddr = 0x7ff6a0001000ull;
    put(obj, 0x128, std::uint64_t{0x7ff6b0002000ull});        // WeaponFiringData
    put(obj, 0x130, dataAddr + kAmmoConfigInFiringData);      // its AmmoConfigData
    put(obj, 0x148, std::int32_t{3});
    put(obj, 0x150, std::int32_t{3});
    put(obj, 0x17C, 1.0f);
    put(obj, 0x188, std::int32_t{-1});
    put(obj, 0x1A0, std::int32_t{31});
    put(obj, 0x1A4, std::int32_t{120});
    put(obj, 0x1BC, std::int32_t{0});
    put(obj, 0x1C0, std::int32_t{-1});
    obj[0x209] = 2;
    put(data, 0x1DC, 2.4f);
    put(data, 0x1EC, 0.6f);
    put(data, 0x204, std::int32_t{2});
    put(data, 0x208, std::int32_t{2});
    put(data, 0x220, std::int32_t{31});
    put(data, 0x224, std::int32_t{4});
    const auto o = ParseFiringObject(obj);
    CHECK(o && o->loaded == 31 && o->reserve == 120 && o->ammoConfig - kAmmoConfigInFiringData == dataAddr);
    CHECK(o && !o->UnlimitedAmmo() && o->flags209 == 2);
    const auto d = ParseFiringData(data);
    CHECK(d && d->reloadType == 2 && d->capacity == 31 && d->magazines == 4 && !d->unlimitedForAi);

    auto broken = [&](std::size_t off, auto v) {
        std::uint8_t copy[kFiringObjectBytes];
        std::memcpy(copy, obj, sizeof(copy));
        put(copy, off, v);
        return !ParseFiringObject(copy).has_value();
    };
    CHECK(broken(0x148, std::int32_t{16}));               // no such state
    CHECK(broken(0x1A0, std::int32_t{-1}));               // negative ammo
    CHECK(broken(0x17C, std::numeric_limits<float>::infinity()));
    CHECK(broken(0x130, std::uint64_t{0x7ff6a0001003ull})); // misaligned pointer
    obj[0x208] = 0x10;                                    // unlimited-ammo instance (AI)
    CHECK(ParseFiringObject(obj)->UnlimitedAmmo());
    put(data, 0x204, std::int32_t{3});                    // no such reload type
    CHECK(!ParseFiringData(data).has_value());

    // Holders: the client weapon keeps its firing object at +0x8, the server weapon at +0xA0.
    const std::uint64_t base = 0x140000000ull;
    CHECK(ClassifyFiringHolder(base + kClientWeaponVtableRva, 0, base) == FiringHolder::ClientWeapon);
    CHECK(ClassifyFiringHolder(base + kClientWeaponBVtableRva, 0, base) == FiringHolder::ClientWeaponB);
    CHECK(ClassifyFiringHolder(base + 0x1D46760, 0, base) == FiringHolder::Unknown); // the abstract base is never live
    // The client weapon entity: class A at +0xA8, class B at +0xA0, its vtable first.
    const std::uint64_t parent = 0x7ff600001000ull;
    auto memory = [&](std::uint64_t address) { return address == parent ? base + kClientWeaponParentVtableRva : 0; };
    CHECK(ClientWeaponParent(FiringHolder::ClientWeapon, parent + 0xA8, base, memory) == parent);
    CHECK(ClientWeaponParent(FiringHolder::ClientWeaponB, parent + 0xA0, base, memory) == parent);
    CHECK(ClientWeaponParent(FiringHolder::ClientWeapon, parent + 0xA0, base, memory) == 0); // A is never at B's slot
    CHECK(ClientWeaponParent(FiringHolder::ServerWeapon, parent + 0xA8, base, memory) == 0);
    CHECK(ClassifyFiringHolder(0, base + kServerWeaponVtableRva, base) == FiringHolder::ServerWeapon);
    CHECK(ClassifyFiringHolder(base + kServerWeaponVtableRva, base + kClientWeaponVtableRva, base) == FiringHolder::Unknown);
}
