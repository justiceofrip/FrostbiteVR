#include "Bc2ReloadRequestBridge.h"
#include "Test.h"
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace
{
struct Fixture
{
    ReloadRoundLease native{};
    Bc2ReloadInteractionSample sample{};
    ReloadInsertionSeat seat{};
    ManualReloadRequest request{};
    Bc2ReloadTargets target{};
    Fixture()
    {
        native.identity.owner = {0x10000, 0x20000, 0x30000, 0x40000, 5, 3, 7};
        native.identity.firing = {0x50000, 0x60000, 0x70000};
        native.identity.serverPlayer = 0x80000;
        native.identity.serverSoldier = 0x90000;
        native.identity.serverItem = 0xa0000;
        native.cycle = 12;
        native.sequence = 20;
        native.observedNs = 1000000000;
        native.deadlineNs = 1100000000;
        native.loaded = 2;
        native.reserve = 8;
        native.capacity = 8;
        native.nativeBindingVerified = native.allThreeHeld = true;
        auto &s = sample.insertion;
        const HandInteractionOwner owner{(std::uint64_t(0x30000) << 32) | 0x20000, 5, 17, 7};
        s.identity = {owner, {0x40000, 17}, {0xb0000, 23}, 29};
        s.sequence = s.geometrySequence = 50;
        s.observedNs = 1000000000;
        s.nowNs = 1000001000;
        s.deadlineNs = 1100000000;
        s.focused = s.itemTracked = s.weaponTracked = s.held = s.eligible = true;
        s.weaponClaim = {
            {1, owner, InteractionHand::Right, HandClaimKind::GunHold, s.identity.weapon, {1, 17}, 0}, 1080000000, 50};
        s.itemClaim = {
            {2, owner, InteractionHand::Left, HandClaimKind::AmmoObject, s.identity.item, {2, 23}, 1}, 1080000000, 50};
        sample.assetName = SpasReloadAsset;
        sample.meshPath = SpasReloadMesh;
        sample.rigFingerprint = SpasReloadRig;
        sample.selectedMeshIdentityVerified = true;
        sample.rawLeftWristWorldMeters = sample.weaponWorldMeters = reload_insertion_detail::Identity();
        const auto profile = SpasReloadInsertionProfile();
        seat = {31,
                50,
                s.identity,
                {profile.id, profile.revision},
                s.itemClaim.token,
                s.weaponClaim.token,
                ReloadOperation::InsertRound};
        request = {41,
                   {owner.actor, owner.actorGeneration, s.identity.weapon.id, owner.equipGeneration, owner.space},
                   ReloadOperation::InsertRound,
                   0,
                   0};
        target = {s.identity,
                  s.itemClaim.token,
                  s.weaponClaim.token,
                  50,
                  12,
                  1000000000,
                  1050000000,
                  reload_insertion_detail::Identity(),
                  reload_insertion_detail::Identity()};
        SyncNative();
    }
    void SyncNative()
    {
        const auto map = BindBc2ReloadOwners(sample.insertion.identity.owner, sample.insertion.identity.weapon, native,
                                             sample.insertion.nowNs);
        if (map)
            sample.native = *ToBc2ReloadInteractionLease(*map, native, sample.insertion.nowNs);
    }
    Bc2ReloadBridgeResult Begin(Bc2ReloadRequestBridge &b)
    {
        return b.Begin(sample, native, seat, request, target);
    }
    void Next(bool held = false)
    {
        auto &s = sample.insertion;
        s.nowNs += 10000000;
        s.observedNs = s.nowNs;
        s.deadlineNs = s.nowNs + 100000000;
        ++s.sequence;
        s.geometrySequence = s.sequence;
        s.weaponClaim.inputSequence = s.itemClaim.inputSequence = s.sequence;
        s.weaponClaim.deadlineNs = s.itemClaim.deadlineNs = s.nowNs + 80000000;
        native.observedNs = s.nowNs;
        native.deadlineNs = s.nowNs + 100000000;
        ++native.sequence;
        native.allThreeHeld = held;
        SyncNative();
    }
    Bc2ReloadAckEvidence Ack()
    {
        const auto &o = native.identity.owner;
        return {{{request.id,
                  {o.soldier, o.actorGeneration, o.weapon, o.equipGeneration, o.space},
                  ReloadOperation::InsertRound,
                  ReloadAcknowledgement::Applied},
                 native.identity,
                 native.cycle,
                 100,
                 200},
                sample.insertion.nowNs,
                sample.insertion.nowNs + 50000000,
                true};
    }
};
int ExplicitDualIdentityAndTruthfulLease()
{
    Fixture f;
    const auto map = BindBc2ReloadOwners(f.sample.insertion.identity.owner, f.sample.insertion.identity.weapon,
                                         f.native, f.sample.insertion.nowNs);
    CHECK(map && map->physical.actor != map->native.owner.soldier && map->physical.equipGeneration == 17 &&
          map->native.owner.equipGeneration == 3);
    f.native.allThreeHeld = false;
    const auto lease = ToBc2ReloadInteractionLease(*map, f.native, f.sample.insertion.nowNs);
    CHECK(lease && !lease->allFiringCopiesHeld && lease->owner == map->physical);
    for (unsigned test = 0; test < 5; ++test)
    {
        Fixture g;
        auto p = g.sample.insertion.identity.owner;
        auto w = g.sample.insertion.identity.weapon;
        switch (test)
        {
        case 0:
            p.actor = g.native.identity.owner.soldier;
            break;
        case 1:
            p.actorGeneration++;
            break;
        case 2:
            w.id++;
            break;
        case 3:
            w.generation++;
            break;
        case 4:
            p.space++;
            break;
        }
        CHECK(!BindBc2ReloadOwners(p, w, g.native, g.sample.insertion.nowNs));
    }
    return 0;
}
int TranslateRequestAndMatchingAcknowledgementOnce()
{
    Fixture f;
    Bc2ReloadRequestBridge b(true);
    auto started = f.Begin(b);
    CHECK(started.submit && started.presentation && b.Phase() == Bc2ReloadBridgePhase::Pending);
    CHECK(started.submit->request.id == 41 && started.submit->request.owner.actor == 0x20000 &&
          started.submit->request.owner.equipGeneration == 3 && started.submit->heldLease.cycle == 12);
    CHECK(!f.Begin(b).submit);
    f.Next();
    CHECK(!f.sample.native.allFiringCopiesHeld);
    auto pending = b.Update(f.sample, f.native);
    CHECK(pending.phase == Bc2ReloadBridgePhase::Pending && pending.presentation &&
          pending.presentation->inputSequence == 51 && !pending.submit);
    const auto done = b.Update(f.sample, f.native, f.Ack());
    CHECK(done.acknowledged && done.acknowledged->owner == f.request.owner && done.consumed &&
          done.consumed->claim == f.seat.itemClaim && done.consumed->request == 41);
    CHECK(!b.Update(f.sample, f.native, f.Ack()).acknowledged && !f.Begin(b).submit);
    return 0;
}
int ReleaseAndReplacementCannotConsumeNewItem()
{
    Fixture f;
    Bc2ReloadRequestBridge b(true);
    CHECK(f.Begin(b).submit);
    f.Next();
    f.sample.insertion.held = false;
    auto released = b.Update(f.sample, f.native);
    CHECK(released.phase == Bc2ReloadBridgePhase::Pending && !released.presentation && !released.consumed &&
          !released.releasedReservation);
    f.Next();
    auto &s = f.sample.insertion;
    s.held = true;
    s.identity.item = {0xc0000, 24};
    s.itemClaim.token.id = 3;
    s.itemClaim.token.item = s.identity.item;
    auto replaced = b.Update(f.sample, f.native);
    CHECK(replaced.phase == Bc2ReloadBridgePhase::Pending && !replaced.presentation);
    const auto done = b.Update(f.sample, f.native, f.Ack());
    CHECK(done.acknowledged && done.consumed->item == f.seat.identity.item &&
          done.consumed->claim == f.seat.itemClaim && done.consumed->claim != s.itemClaim.token);
    return 0;
}
int ClaimAndSeatFailuresAtBegin()
{
    for (unsigned test = 0; test < 10; ++test)
    {
        Fixture f;
        Bc2ReloadRequestBridge b(true);
        auto &s = f.sample.insertion;
        switch (test)
        {
        case 0:
            s.itemClaim.deadlineNs = s.nowNs;
            break;
        case 1:
            s.weaponClaim.inputSequence--;
            break;
        case 2:
            f.seat.itemClaim.id++;
            break;
        case 3:
            f.target.shellClaim.id++;
            break;
        case 4:
            s.geometrySequence--;
            break;
        case 5:
            f.native.allThreeHeld = false;
            f.SyncNative();
            break;
        case 6:
            f.seat.identity.item.generation++;
            break;
        case 7:
            f.request.owner.equipGeneration = 3;
            break;
        case 8:
            f.target.deadlineNs = s.nowNs;
            break;
        case 9:
            s.identity.item = {};
            s.itemClaim.token.item = {};
            f.seat.identity = s.identity;
            f.seat.itemClaim = s.itemClaim.token;
            f.target.identity = s.identity;
            f.target.shellClaim = s.itemClaim.token;
            break;
        }
        CHECK(!f.Begin(b).submit && b.Phase() == Bc2ReloadBridgePhase::Idle);
    }
    return 0;
}
int OwnerEquipSpaceCycleCancelWithoutRollback()
{
    for (unsigned test = 0; test < 7; ++test)
    {
        Fixture f;
        Bc2ReloadRequestBridge b(true);
        CHECK(f.Begin(b).submit);
        const auto old = f.Ack();
        f.Next();
        switch (test)
        {
        case 0:
            f.native.identity.owner.equipGeneration++;
            break;
        case 1:
            f.sample.insertion.identity.owner.equipGeneration++;
            f.sample.insertion.identity.weapon.generation++;
            break;
        case 2:
            f.native.identity.owner.space++;
            f.sample.insertion.identity.owner.space++;
            break;
        case 3:
            f.native.identity.owner.weak++;
            break;
        case 4:
            f.native.cycle++;
            break;
        case 5:
            f.sample.insertion.identity.trackingEpoch++;
            break;
        case 6:
            f.sample.insertion.weaponClaim.token.id++;
            break;
        }
        f.SyncNative();
        const auto cancelled = b.Update(f.sample, f.native, old);
        CHECK(cancelled.phase == Bc2ReloadBridgePhase::Cancelled && cancelled.unresolvedNative &&
              cancelled.releasedReservation && !cancelled.consumed && !cancelled.acknowledged);
        const auto late = b.Update(f.sample, f.native, old);
        CHECK(late.unresolvedNative && !late.consumed && !late.acknowledged && !late.releasedReservation);
    }
    return 0;
}
int PendingPoseExpiryAndFreshGeometry()
{
    Fixture f;
    Bc2ReloadRequestBridge b(true);
    CHECK(f.Begin(b).submit);
    f.sample.insertion.nowNs = 1060000000;
    f.native.allThreeHeld = false;
    f.SyncNative();
    auto r = b.Update(f.sample, f.native);
    CHECK(r.phase == Bc2ReloadBridgePhase::Pending && !r.presentation);
    f.Next();
    r = b.Update(f.sample, f.native);
    CHECK(r.presentation && r.presentation->observedNs == f.sample.insertion.observedNs &&
          r.presentation->deadlineNs <= f.sample.insertion.observedNs + 100000000);
    const auto previous = *r.presentation;
    f.sample.insertion.nowNs += 1000;
    f.sample.rawLeftWristWorldMeters.values[3][0] = .01f;
    r = b.Update(f.sample, f.native);
    CHECK(r.presentation && r.presentation->deadlineNs == previous.deadlineNs &&
          r.presentation->inputSequence == previous.inputSequence);
    f.Next();
    f.sample.insertion.geometrySequence--;
    CHECK(!b.Update(f.sample, f.native).presentation);
    f.Next();
    f.sample.rawLeftWristWorldMeters.values[3][0] = .2f;
    CHECK(!b.Update(f.sample, f.native).presentation && b.Phase() == Bc2ReloadBridgePhase::Pending);
    CHECK(b.Update(f.sample, f.native, f.Ack()).acknowledged);
    return 0;
}
int WrongOrStaleAcknowledgementsDoNotReconcile()
{
    for (unsigned test = 0; test < 10; ++test)
    {
        Fixture f;
        Bc2ReloadRequestBridge b(true);
        CHECK(f.Begin(b).submit);
        f.Next();
        auto ack = f.Ack();
        switch (test)
        {
        case 0:
            ack.acknowledgement.semantic.request++;
            break;
        case 1:
            ack.acknowledgement.cycle++;
            break;
        case 2:
            ack.acknowledgement.identity.owner.equipGeneration++;
            break;
        case 3:
            ack.acknowledgement.semantic.owner = f.request.owner;
            break;
        case 4:
            ack.deadlineNs = f.sample.insertion.nowNs;
            break;
        case 5:
            ack.verified = false;
            break;
        case 6:
            ack.acknowledgement.serverInvocation = 0;
            break;
        case 7:
            ack.acknowledgement.semantic.status = ReloadAcknowledgement::Rejected;
            break;
        case 8:
            ack.observedNs = 1000000000; // Fresh by age, but predates this request.
            break;
        case 9:
            ack.acknowledgement.sampleSequence = 20; // Original lease is not completion evidence.
            break;
        }
        auto ignored = b.Update(f.sample, f.native, ack);
        CHECK(ignored.phase == Bc2ReloadBridgePhase::Pending && !ignored.acknowledged && !ignored.consumed);
        CHECK(b.Update(f.sample, f.native, f.Ack()).acknowledged);
    }
    return 0;
}
int DisabledTimeoutAndNoFakeHeldState()
{
    Fixture f;
    Bc2ReloadRequestBridge off;
    CHECK(!f.Begin(off).submit && off.Phase() == Bc2ReloadBridgePhase::Disabled);
    Bc2ReloadRequestBridge b(true);
    CHECK(f.Begin(b).submit);
    f.Next();
    f.sample.native.allFiringCopiesHeld = true;
    CHECK(b.Update(f.sample, f.native).phase == Bc2ReloadBridgePhase::Cancelled);
    Fixture g;
    Bc2ReloadRequestBridge timeout(true);
    CHECK(g.Begin(timeout).submit);
    g.sample.insertion.nowNs += 1500000000;
    auto expired = timeout.Update(g.sample, g.native);
    CHECK(expired.reason == Bc2ReloadBridgeReason::Expired && expired.unresolvedNative && !expired.consumed);
    return 0;
}
int CancelThenNewSeatRejectsOldAcknowledgement()
{
    Fixture f;
    Bc2ReloadRequestBridge b(true);
    CHECK(f.Begin(b).submit);
    const auto oldAck = f.Ack();
    const auto cancelled = b.Cancel();
    CHECK(cancelled.releasedReservation && cancelled.unresolvedNative && !cancelled.consumed);
    CHECK(!b.Cancel().releasedReservation);
    f.Next(true);
    auto &s = f.sample.insertion;
    s.identity.item = {0xc0000, 24};
    s.itemClaim.token.id = 3;
    s.itemClaim.token.item = s.identity.item;
    ++f.seat.id;
    f.seat.identity = s.identity;
    f.seat.inputSequence = s.sequence;
    f.seat.itemClaim = s.itemClaim.token;
    ++f.request.id;
    f.request.step = 2;
    f.request.repetition = 1;
    f.target.identity = s.identity;
    f.target.shellClaim = s.itemClaim.token;
    f.target.inputSequence = s.sequence;
    f.target.observedNs = s.observedNs;
    f.target.deadlineNs = s.nowNs + 50000000;
    const auto next = f.Begin(b);
    CHECK(next.submit && next.submit->request.id == 42 && next.submit->request.step == 2 &&
          next.submit->request.repetition == 1);
    const auto stale = b.Update(f.sample, f.native, oldAck);
    CHECK(stale.phase == Bc2ReloadBridgePhase::Pending && !stale.acknowledged && !stale.consumed);
    f.Next();
    const auto done = b.Update(f.sample, f.native, f.Ack());
    CHECK(done.acknowledged && done.consumed->item == s.identity.item && done.consumed->claim == s.itemClaim.token &&
          done.consumed->request == 42);
    return 0;
}
int RollbackAndFocusCannotReplayInsertion()
{
    for (unsigned test = 0; test < 5; ++test)
    {
        Fixture f;
        Bc2ReloadRequestBridge b(true);
        CHECK(f.Begin(b).submit);
        switch (test)
        {
        case 0:
            f.sample.insertion.sequence--;
            break;
        case 1:
            f.native.sequence--;
            break;
        case 2:
            f.sample.insertion.nowNs--;
            break;
        case 3:
            f.sample.insertion.observedNs--;
            break;
        case 4:
            f.sample.insertion.focused = false;
            break;
        }
        const auto result = b.Update(f.sample, f.native, f.Ack());
        CHECK(result.phase == Bc2ReloadBridgePhase::Cancelled && result.unresolvedNative && !result.acknowledged &&
              !result.consumed && result.releasedReservation);
        CHECK(!f.Begin(b).submit);
    }
    return 0;
}
} // namespace
int main()
{
    if (ExplicitDualIdentityAndTruthfulLease() || TranslateRequestAndMatchingAcknowledgementOnce() ||
        ReleaseAndReplacementCannotConsumeNewItem() || ClaimAndSeatFailuresAtBegin() ||
        OwnerEquipSpaceCycleCancelWithoutRollback() || PendingPoseExpiryAndFreshGeometry() ||
        WrongOrStaleAcknowledgementsDoNotReconcile() || DisabledTimeoutAndNoFakeHeldState() ||
        CancelThenNewSeatRejectsOldAcknowledgement() || RollbackAndFocusCannotReplayInsertion())
        return 1;
    std::printf(
        "Ten physical/native reload request bridge groups passed; native calls and ammo writes remain absent.\n");
    return 0;
}
