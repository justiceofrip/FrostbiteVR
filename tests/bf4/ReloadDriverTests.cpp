// Input-only manual reload (Bf4ReloadDriver) against the emulated firing state machine:
// the gun cannot fire with the magazine out, the game's own reload moves the ammunition,
// acknowledgements come only from observed native ammunition, owed strokes persist, and
// the framework's ManualReload coordinator completes a whole magazine change.
#include "TestHarness.h"

#include "FakeFiringMachine.h"
#include "../../src/games/bf4/Bf4PadLayout.h"

using namespace fvr;
using namespace fvr::bf4;
using namespace fvr::bf4::emu;
using interaction::ManualReloadRequest;
using interaction::ReloadAcknowledgement;
using interaction::ReloadOperation;
using interaction::WeaponAction;

namespace {
constexpr std::int64_t kFrameNs = 11'111'111; // 90 Hz
const std::uint16_t kReload = DefaultPadLayout().reload.padButton;

Bf4WeaponReloadData Data(Bf4WeaponClass c, Bf4ReloadType r, Bf4FireLogic f, int capacity, float bolt = 0) {
    Bf4WeaponReloadData d;
    d.weaponClass = static_cast<int>(c);
    d.reloadType = static_cast<int>(r);
    d.fireLogic = static_cast<int>(f);
    d.magazineCapacity = capacity;
    d.reloadDelay = 0.2f;
    d.reloadTime = 2.4f;
    d.boltActionTime = bolt;
    return d;
}

interaction::ManualReloadOwner Owner() { return {1, 1, 7, 1, 1}; }

// One frame: driver -> pad (player holds the trigger if `trigger`) -> game.
struct Rig {
    Bf4ReloadDriver driver;
    FakeFiringMachine gun;
    std::int64_t now = 1'000'000'000;
    std::uint64_t nextRequest = 0;
    explicit Rig(const Bf4ReloadClassification& c) : driver(c) {}
    ReloadDriverOutput Frame(bool trigger, std::optional<ReloadOperation> op = {}, bool cycleDone = false) {
        ReloadDriverInput in;
        in.nowNs = now;
        in.native = gun.Sample();
        if (op) in.request = ManualReloadRequest{++nextRequest, Owner(), *op, 0, 0};
        in.cycleGestureCompleted = cycleDone;
        const auto out = driver.Update(in);
        interaction::PadState pad;
        pad.rightTrigger = trigger ? 255 : 0;
        ApplyReloadDriverToPad(pad, out, kReload);
        gun.Tick(kFrameNs / 1e9f, pad, kReload);
        now += kFrameNs;
        return out;
    }
    // Runs until an acknowledgement appears (or the frame budget runs out).
    std::optional<interaction::ManualReloadAck> UntilAck(int frames) {
        for (int i = 0; i < frames; ++i)
            if (auto out = Frame(false); out.ack) return out.ack;
        return std::nullopt;
    }
};
} // namespace

TEST_CASE(ReloadDriverTacticalMagazineChange) {
    Rig r(ClassifyReload(1, Data(Bf4WeaponClass::Assault, Bf4ReloadType::MagazineWithShorterReload, Bf4FireLogic::Automatic, 31)));
    r.gun.loaded = 10;
    auto out = r.Frame(false, ReloadOperation::UnseatMagazine);
    CHECK(out.ack && out.ack->status == ReloadAcknowledgement::Applied && out.gate == ReloadGate::MagazineOut);
    for (int i = 0; i < 30; ++i) { r.Frame(true); r.Frame(false); } // trigger pulls with the magazine out
    CHECK(r.gun.shots == 0 && r.gun.loaded == 10);
    out = r.Frame(false, ReloadOperation::SeatMagazine);
    CHECK(!out.ack && out.pressReload && out.gate == ReloadGate::NativeReload);
    const auto ack = r.UntilAck(400);
    CHECK(ack && ack->status == ReloadAcknowledgement::Applied && ack->operation == ReloadOperation::SeatMagazine);
    CHECK(r.gun.loaded == 31 && r.gun.reserve == 99); // 30 + the round that stayed chambered
    out = r.Frame(false);
    CHECK(out.gate == ReloadGate::None && !out.blockFire); // chamber was occupied: nothing owed
    for (int i = 0; i < 200 && r.gun.state != 3; ++i) r.Frame(false); // the game finishes its reload
    r.Frame(true);
    CHECK(r.gun.shots == 1);
}

TEST_CASE(ReloadDriverEmptyReloadOwesTheChargingHandle) {
    Rig r(ClassifyReload(1, Data(Bf4WeaponClass::Assault, Bf4ReloadType::MagazineWithShorterReload, Bf4FireLogic::Automatic, 31)));
    r.gun.loaded = 0;
    r.Frame(false, ReloadOperation::UnseatMagazine);
    r.Frame(false, ReloadOperation::SeatMagazine);
    CHECK(r.UntilAck(400).has_value());
    CHECK(r.gun.loaded == 30); // from empty: no round in the chamber yet
    for (int i = 0; i < 200; ++i) { // the owed stroke survives frames and repeated samples
        const auto out = r.Frame(i % 2 == 0);
        if (out.owedCycle != WeaponAction::ChargingHandle || !out.blockFire) { CHECK(false); break; }
    }
    CHECK(r.gun.shots == 0);
    auto out = r.Frame(false, std::nullopt, /*cycleDone=*/true);
    CHECK(out.gate == ReloadGate::None);
    r.Frame(true);
    CHECK(r.gun.shots == 1 && r.gun.loaded == 29);
}

TEST_CASE(ReloadDriverNeverStrandsTheGun) {
    // No reserve: the same magazine goes back in, no reload is pressed.
    Rig dry(ClassifyReload(1, Data(Bf4WeaponClass::Assault, Bf4ReloadType::Magazine, Bf4FireLogic::Automatic, 30)));
    dry.gun.reloadType = 1;
    dry.gun.capacity = 30;
    dry.gun.loaded = 4;
    dry.gun.reserve = 0;
    dry.Frame(false, ReloadOperation::UnseatMagazine);
    auto out = dry.Frame(false, ReloadOperation::SeatMagazine);
    CHECK(out.ack && out.ack->status == ReloadAcknowledgement::Applied && !out.pressReload && out.gate == ReloadGate::None);
    CHECK(dry.gun.loaded == 4);
    // The game declines the reload (sprinting): rejected after the start window, gun usable.
    Rig busy(ClassifyReload(1, Data(Bf4WeaponClass::Assault, Bf4ReloadType::MagazineWithShorterReload, Bf4FireLogic::Automatic, 31)));
    busy.gun.loaded = 12;
    busy.gun.refuseReload = true;
    busy.Frame(false, ReloadOperation::UnseatMagazine);
    busy.Frame(false, ReloadOperation::SeatMagazine);
    const auto ack = busy.UntilAck(200);
    CHECK(ack && ack->status == ReloadAcknowledgement::Rejected);
    CHECK(busy.Frame(false).gate == ReloadGate::None && busy.gun.loaded == 12);
    // A second request while the game is reloading is refused, not queued.
    Rig twice(ClassifyReload(1, Data(Bf4WeaponClass::Assault, Bf4ReloadType::MagazineWithShorterReload, Bf4FireLogic::Automatic, 31)));
    twice.gun.loaded = 5;
    twice.Frame(false, ReloadOperation::UnseatMagazine);
    twice.Frame(false, ReloadOperation::SeatMagazine);
    out = twice.Frame(false, ReloadOperation::UnseatMagazine);
    CHECK(out.ack && out.ack->status == ReloadAcknowledgement::Rejected && out.gate == ReloadGate::NativeReload);
}

TEST_CASE(ReloadDriverBoltOwedAfterEveryShot) {
    Rig r(ClassifyReload(3, Data(Bf4WeaponClass::Sniper, Bf4ReloadType::Magazine, Bf4FireLogic::SingleFireWithBoltAction, 5, 0.8f)));
    r.gun.reloadType = 1;
    r.gun.capacity = 5;
    r.gun.loaded = 5;
    r.gun.boltAction = true;
    r.Frame(true);
    CHECK(r.gun.shots == 1);
    for (int i = 0; i < 300; ++i) r.Frame(i % 2 == 0); // the game finishes its own bolt; the VR bolt is still owed
    CHECK(r.gun.shots == 1 && r.gun.state == 3);
    auto out = r.Frame(false);
    CHECK(out.gate == ReloadGate::OwedCycle && out.owedCycle == WeaponAction::Bolt);
    r.Frame(false, std::nullopt, /*cycleDone=*/true);
    r.Frame(true);
    CHECK(r.gun.shots == 2 && r.Frame(false).owedCycle == WeaponAction::Bolt); // and owed again
}

TEST_CASE(ReloadDriverWeaponSwitchEndsItsState) {
    Rig r(ClassifyReload(1, Data(Bf4WeaponClass::Assault, Bf4ReloadType::MagazineWithShorterReload, Bf4FireLogic::Automatic, 31)));
    r.gun.loaded = 3;
    r.Frame(false, ReloadOperation::UnseatMagazine);
    r.Frame(false, ReloadOperation::SeatMagazine);
    r.gun.identity = 0x5a5a0002; // switched weapons mid-reload
    r.gun.state = 3;
    const auto out = r.Frame(false);
    CHECK(!out.ack && !out.pressReload && out.gate == ReloadGate::None); // no invented acknowledgement
}

TEST_CASE(ReloadDriverSingleShotAndUnsupported) {
    Rig rpg(ClassifyReload(6, Data(Bf4WeaponClass::At, Bf4ReloadType::Magazine, Bf4FireLogic::SingleFire, 1)));
    rpg.gun.reloadType = 1;
    rpg.gun.capacity = 1;
    rpg.gun.loaded = 0;
    rpg.gun.reserve = 3;
    CHECK(rpg.Frame(false, ReloadOperation::OpenBreech).ack->status == ReloadAcknowledgement::Applied);
    CHECK(rpg.Frame(false, ReloadOperation::InsertRound).ack->status == ReloadAcknowledgement::Applied);
    auto out = rpg.Frame(false, ReloadOperation::CloseBreech);
    CHECK(!out.ack && out.pressReload);
    const auto ack = rpg.UntilAck(400);
    CHECK(ack && ack->status == ReloadAcknowledgement::Applied && rpg.gun.loaded == 1 && rpg.gun.reserve == 2);
    CHECK(rpg.Frame(false, ReloadOperation::UnseatMagazine).ack->status == ReloadAcknowledgement::Rejected); // wrong mechanism

    auto pumpData = Data(Bf4WeaponClass::Shotgun, Bf4ReloadType::SingleBullet, Bf4FireLogic::SingleFireWithBoltAction, 7, 0.5f);
    Rig tube(ClassifyReload(4, pumpData)); // one round at a time: needs native per-round evidence first
    CHECK(!tube.driver.Supported());
    out = tube.Frame(true, ReloadOperation::InsertRound);
    CHECK(!out.ack && !out.blockFire && !out.pressReload && tube.gun.shots == 1);
}

TEST_CASE(ReloadDriverCompletesTheCoordinatorPlan) {
    const auto c = ClassifyReload(1, Data(Bf4WeaponClass::Assault, Bf4ReloadType::MagazineWithShorterReload, Bf4FireLogic::Automatic, 31));
    interaction::ManualReload plan(c.descriptor.feedPlan);
    CHECK(plan.ValidConfig());
    Bf4ReloadDriver driver(c);
    FakeFiringMachine gun;
    gun.loaded = 8;
    std::int64_t now = 1'000'000'000;
    std::uint64_t sequence = 0;
    interaction::ManualReloadAck lastAck{};
    bool completed = false;
    // Gestures from the VR side: magazine out on frame 2, new magazine seated on frame 40.
    for (int frame = 0; frame < 500 && !completed; ++frame) {
        interaction::ManualReloadSample s;
        s.owner = Owner();
        s.sequence = ++sequence;
        s.nowNs = now;
        s.focused = s.tracked = s.bindingsVerified = true;
        if (frame == 2) s.gesture = {1, ReloadOperation::UnseatMagazine};
        else if (frame == 40) s.gesture = {2, ReloadOperation::SeatMagazine};
        else s.neutral = true;
        s.acknowledgement = lastAck;
        const auto result = plan.Update(s);
        completed = result.completed;
        ReloadDriverInput in;
        in.nowNs = now;
        in.native = gun.Sample();
        in.request = result.request;
        in.transactionCancelled = result.cancelled;
        const auto out = driver.Update(in);
        lastAck = out.ack ? *out.ack : interaction::ManualReloadAck{};
        interaction::PadState pad;
        ApplyReloadDriverToPad(pad, out, kReload);
        gun.Tick(kFrameNs / 1e9f, pad, kReload);
        now += kFrameNs;
    }
    CHECK(completed && gun.loaded == 31 && gun.reserve == 97);
}
