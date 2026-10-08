#include "Bc2ReloadRequestCycle.h"
#include "Bc2ReloadPolicyLock.h"
#include "Test.h"
#include <cstring>
#include <limits>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace
{
ReloadHoldInput Input()
{
    ReloadHoldInput i;
    i.verified = true;
    i.branch = 0;
    i.nowNs = 1000000000;
    i.leaseDeadlineNs = i.nowNs + 100000000;
    i.identity.owner = {0x10000, 0x20000, 0x30000, 0x40000, 1, 2, 3};
    i.identity.firing = {0x50000, 0x60000, 0x70000};
    i.identity.serverPlayer = 0x80000;
    i.identity.serverSoldier = 0x90000;
    i.identity.serverItem = 0xa0000;
    auto &c = i.config;
    std::memcpy(c.assetName.data(), "SPAS12_sp", sizeof("SPAS12_sp"));
    std::memcpy(c.assetPath.data(), "Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12",
                sizeof("Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12"));
    c.weaponData = 0xb0000;
    c.firingData = 0xc0000;
    c.primaryFire = 0xd0000;
    c.ammoAddress = c.primaryFire + 0x170;
    c.fireLogicType = 1;
    c.reloadType = 0;
    c.fireInputAction = 8;
    c.reloadInputAction = 29;
    c.baseCapacity = c.numberOfMagazines = 4;
    c.reloadDelay = .06f;
    c.reloadTime = .72f;
    c.reloadThreshold = c.postReloadTime = 1;
    c.boltDelay = .5f;
    i.context.deltaSeconds = .005f;
    i.context.reloadTimeMultiplier = 1;
    i.context.flags24Through28[0] = true;
    for (unsigned n = 0; n < 3; ++n)
    {
        auto &b = i.branches[n];
        b.address = i.identity.firing[n];
        b.wrapperOffset = n == 0 ? 0x3c : n == 1 ? 0x40 : 0x10;
        b.currentState = 11;
        b.nextState = 12;
        b.phaseTimer = .2f;
        b.loaded = 6;
        b.reserve = 21;
        i.capacities[n] = 8;
    }
    return i;
}
struct Simulation
{
    Bc2ReloadRequestCycle gate{true};
    ReloadHoldInput input = Input();
    ReloadCycleControl control{};
    std::uint64_t id = 0, transfers = 1000;
    unsigned originals = 0;
    Simulation()
    {
        control = {input.identity, 93, 1, input.nowNs, input.nowNs + 100000000, true};
    }
    bool Tick(std::int64_t delta = 1000)
    {
        input.nowNs += delta;
        input.leaseDeadlineNs = input.nowNs + 100000000;
        ++control.sequence;
        control.observedNs = input.nowNs;
        control.deadlineNs = input.leaseDeadlineNs;
        return gate.KeepAlive(control, input.nowNs);
    }
    ReloadRequestDecision Begin(unsigned b, bool stable = true, bool timing = true)
    {
        input.branch = b;
        return gate.Evaluate(input, timing, stable, ++id);
    }
    bool End(const ReloadRequestDecision &d, bool okay = true)
    {
        ++originals;
        ReloadDeltaOverride patch;
        if (d.hold)
        {
            patch.applied = patch.restored = okay;
            patch.original = 1;
        }
        const bool result = gate.Finish(d, input.branches[d.branch], input.nowNs + 100, true, patch);
        input.nowNs += 100;
        return result;
    }
    bool Arm()
    {
        if (!gate.Start(control, input.nowNs))
            return false;
        for (unsigned n = 0; n < 3; ++n)
        {
            if (!Tick())
                return false;
            const auto d = Begin(n);
            if (n < 2)
            {
                if (d.tracked)
                    return false;
            }
            else if (!d.hold || !gate.Allows(d, input.nowNs) || !End(d))
                return false;
        }
        return HoldAll();
    }
    bool HoldAll(bool stable = true)
    {
        for (unsigned n = 0; n < 3; ++n)
        {
            if (!Tick())
                return false;
            auto d = Begin(n, stable);
            if (!d.hold || !gate.Allows(d, input.nowNs) || !End(d))
                return false;
        }
        return true;
    }
    std::optional<ReloadRoundLease> Lease()
    {
        return gate.Lease(input.identity, control.cycle, input.nowNs);
    }
    Bc2ReloadNativeRequest Request(std::uint64_t requestId = 47)
    {
        const auto lease = Lease();
        const auto &o = input.identity.owner;
        HandInteractionOwner physical{(std::uint64_t(o.weak) << 32) | o.soldier, o.actorGeneration, 17, o.space};
        HandInteractionKey item{0xe0000 + requestId, requestId};
        HandClaimToken claim{requestId + 20, physical, InteractionHand::Left, HandClaimKind::AmmoObject, item,
                             {7, 1},         10};
        return {{requestId,
                 {o.soldier, o.actorGeneration, o.weapon, o.equipGeneration, o.space},
                 ReloadOperation::InsertRound,
                 3,
                 2},
                lease.value_or(ReloadRoundLease{}),
                {item, claim, requestId + 10, requestId, control.cycle}};
    }
    bool Advance(unsigned branch)
    {
        if (!Tick())
            return false;
        const auto d = Begin(branch);
        if (!d.tracked || d.hold)
            return false;
        auto &b = input.branches[branch];
        ReloadRoundTransfer t{input.identity,  control.cycle, ++transfers, input.nowNs + 1,
                              input.nowNs + 2, branch,        b.loaded,    b.reserve,
                              b.loaded + 1,    b.reserve - 1, true,        true};
        if (!gate.Transfer(t, d.update))
            return false;
        ++b.loaded;
        --b.reserve;
        b.phaseTimer = .719995f;
        if (b.loaded == input.capacities[branch] || !b.reserve)
        {
            b.currentState = b.nextState = 1;
            b.phaseTimer = .9999f;
        }
        return End(d);
    }
    bool Rehold(unsigned branch, bool stable = true)
    {
        if (!Tick())
            return false;
        const auto d = Begin(branch, stable);
        return d.hold && End(d);
    }
    bool CompleteRound(const std::array<unsigned, 3> &order = {0, 1, 2})
    {
        for (auto branch : order)
        {
            if (!Advance(branch))
                return false;
            if (input.branches[branch].loaded < input.capacities[branch] && input.branches[branch].reserve > 0 &&
                !Rehold(branch))
                return false;
        }
        if (!Tick())
            return false;
        auto d = Begin(0);
        return d.tracked ? End(d) : gate.Phase() == ReloadRequestCyclePhase::Finished;
    }
};
int ExplicitRequestOnlyAndTruthfulHold()
{
    Simulation s;
    Bc2ReloadRequestCycle disabled;
    CHECK(!disabled.Start(s.control, s.input.nowNs));
    CHECK(s.Arm());
    auto lease = s.Lease();
    CHECK(lease && lease->allThreeHeld && lease->cycle == 93 && !s.gate.PendingRequest());
    for (unsigned n = 0; n < 200; ++n)
    {
        CHECK(s.Tick(10000000) && s.HoldAll());
        CHECK(!s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs));
    }
    for (const auto &b : s.input.branches)
        CHECK(b.loaded == 6 && b.reserve == 21);
    const auto request = s.Request();
    CHECK(s.gate.Submit(request, s.input.nowNs));
    CHECK(!s.gate.Submit(request, s.input.nowNs));
    lease = s.Lease();
    CHECK(lease && !lease->allThreeHeld && s.gate.PendingRequest() == 47);
    return 0;
}
int RealRequestTwoRoundsAndFinalAck()
{
    Simulation s;
    CHECK(s.Arm());
    auto first = s.Request(417);
    CHECK(s.gate.Submit(first, s.input.nowNs) && s.CompleteRound({2, 0, 1}));
    CHECK(s.gate.Phase() == ReloadRequestCyclePhase::Holding);
    const auto held = s.Lease();
    CHECK(held && held->loaded == 7 && held->reserve == 20 && held->allThreeHeld);
    CHECK(!s.gate.Submit(s.Request(418), s.input.nowNs));
    const auto ack = s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs);
    CHECK(ack && ack->acknowledgement.semantic.request == 417 && ack->acknowledgement.cycle == 93 &&
          ack->acknowledgement.serverInvocation == 1001 && ack->observedNs <= s.input.nowNs);
    CHECK(!s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs) && !s.gate.Submit(first, s.input.nowNs));
    CHECK(s.gate.Submit(s.Request(418), s.input.nowNs) && s.CompleteRound());
    CHECK(s.gate.Phase() == ReloadRequestCyclePhase::Finished);
    const auto final = s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs);
    CHECK(final && final->acknowledgement.semantic.request == 418);
    for (const auto &b : s.input.branches)
        CHECK(b.loaded == 8 && b.reserve == 19);
    CHECK(!s.Lease()->allThreeHeld);
    return 0;
}
int QuietCohortAndActualReholds()
{
    Simulation s;
    CHECK(s.Arm() && s.gate.Submit(s.Request(), s.input.nowNs));
    for (unsigned n = 0; n < 3; ++n)
        CHECK(s.Advance(n));
    CHECK(!s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs));
    for (unsigned n = 0; n < 3; ++n)
        CHECK(s.Rehold(n, false));
    CHECK(!s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs));
    CHECK(s.Rehold(0, true));
    CHECK(s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs));
    return 0;
}
int NoBorrowedLeaseDuringOriginal()
{
    Simulation s;
    CHECK(s.Arm() && s.Tick());
    auto d = s.Begin(0);
    CHECK(d.hold);
    CHECK(!s.Lease() && !s.gate.Submit(s.Request(), s.input.nowNs));
    CHECK(s.End(d) && s.Lease());
    return 0;
}
int OwnerControlAndTimingCancel()
{
    for (unsigned test = 0; test < 9; ++test)
    {
        Simulation s;
        CHECK(s.Arm() && s.gate.Submit(s.Request(), s.input.nowNs));
        CHECK(s.Tick());
        switch (test)
        {
        case 0:
            s.input.identity.owner.space++;
            break;
        case 1:
            s.input.identity.owner.equipGeneration++;
            break;
        case 2:
            s.input.context.reloadTimeMultiplier = .5f;
            break;
        case 3:
            s.input.context.deltaSeconds = .100001f;
            break;
        case 4:
            s.input.config.reloadTime = .73f;
            break;
        case 5:
            s.input.capacities[1] = 9;
            break;
        case 6:
            s.input.context.fireRequested = true;
            break;
        case 7:
            s.input.verified = false;
            break;
        case 8:
            s.input.branches[2].phaseTimer = std::numeric_limits<float>::quiet_NaN();
            break;
        }
        CHECK(!s.Begin(0).tracked && s.gate.Phase() == ReloadRequestCyclePhase::Cancelled &&
              s.gate.UnresolvedRequest());
        CHECK(!s.Lease() && !s.gate.TakeAcknowledgement(s.control.identity, 93, s.input.nowNs));
    }
    return 0;
}
int DeadmanAndExactDuplicateControl()
{
    for (unsigned test = 0; test < 4; ++test)
    {
        Simulation s;
        CHECK(s.Arm());
        auto c = s.control;
        if (test == 0)
            c.deadlineNs++;
        if (test == 1)
            c.permitted = false;
        if (test == 2)
            c.identity.owner.space++;
        if (test == 3)
            s.input.nowNs = c.deadlineNs;
        CHECK(!s.gate.KeepAlive(c, s.input.nowNs) && s.gate.Phase() == ReloadRequestCyclePhase::Cancelled);
    }
    Simulation okay;
    CHECK(okay.Arm() && okay.gate.KeepAlive(okay.control, okay.input.nowNs));
    return 0;
}
int PatchFailureAndNativeTransferMismatch()
{
    for (unsigned test = 0; test < 5; ++test)
    {
        Simulation s;
        CHECK(s.Arm() && s.gate.Submit(s.Request(), s.input.nowNs) && s.Tick());
        auto d = s.Begin(0);
        CHECK(d.tracked && !d.hold);
        ReloadRoundTransfer t{
            s.input.identity, 93, 1001, s.input.nowNs + 1, s.input.nowNs + 2, 0, 6, 21, 7, 20, true, true};
        if (test == 0)
            t.cycle++;
        if (test == 1)
            t.identity.owner.weapon++;
        if (test == 2)
            t.loadedAfter = 8;
        if (test == 3)
            t.ordinaryState12Verified = false;
        CHECK(!s.gate.Transfer(t, test == 4 ? d.update + 1 : d.update) && s.gate.UnresolvedRequest());
    }
    Simulation patch;
    CHECK(patch.Arm() && patch.Tick());
    auto d = patch.Begin(0);
    CHECK(d.hold && !patch.End(d, false) && patch.gate.Failure() == ReloadRequestCycleFailure::Patch);
    return 0;
}
int ExpiredAckCannotBeRestampedOrReplayed()
{
    Simulation s;
    CHECK(s.Arm() && s.gate.Submit(s.Request(), s.input.nowNs) && s.CompleteRound());
    const auto completionTime = s.input.nowNs;
    CHECK(s.Tick(40000000) && s.HoldAll());
    const auto ack = s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs);
    CHECK(ack && ack->observedNs <= completionTime && ack->observedNs < s.input.nowNs);
    Simulation expired;
    CHECK(expired.Arm() && expired.gate.Submit(expired.Request(), expired.input.nowNs) && expired.CompleteRound());
    for (unsigned n = 0; n < 20 && expired.gate.Phase() != ReloadRequestCyclePhase::Cancelled; ++n)
    {
        if (!expired.Tick(10000000))
            break;
        CHECK(expired.HoldAll());
    }
    CHECK(expired.gate.Phase() == ReloadRequestCyclePhase::Cancelled && expired.gate.UnresolvedRequest() &&
          !expired.gate.TakeAcknowledgement(expired.input.identity, 93, expired.input.nowNs));
    return 0;
}
int CancelAfterNativeCompletionDoesNotRollback()
{
    Simulation s;
    CHECK(s.Arm() && s.gate.Submit(s.Request(), s.input.nowNs) && s.CompleteRound());
    s.gate.Cancel();
    CHECK(s.gate.UnresolvedRequest() && s.gate.PendingRequest() == 47 &&
          !s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs));
    for (const auto &b : s.input.branches)
        CHECK(b.loaded == 7 && b.reserve == 20);
    return 0;
}
int PersistentBeyondObserverLifetime()
{
    Simulation s;
    CHECK(s.Arm());
    s.gate.Cancel();
    s.input.nowNs = 40000000000ll;
    s.input.leaseDeadlineNs = s.input.nowNs + 100000000;
    s.control = {s.input.identity, 94, 9000, s.input.nowNs, s.input.leaseDeadlineNs, true};
    CHECK(s.Arm() && s.Lease()->cycle == 94 && s.gate.Submit(s.Request(999), s.input.nowNs) && s.CompleteRound());
    CHECK(s.gate.TakeAcknowledgement(s.input.identity, 94, s.input.nowNs)->acknowledgement.semantic.request == 999);
    return 0;
}
int PhysicalBridgeIntegration()
{
    Simulation s;
    CHECK(s.Arm());
    const auto native = s.Lease();
    CHECK(native);
    const auto &o = native->identity.owner;
    Bc2ReloadInteractionSample sample;
    auto &input = sample.insertion;
    HandInteractionOwner physical{(std::uint64_t(o.weak) << 32) | o.soldier, o.actorGeneration, 17, o.space};
    input.identity = {physical, {o.weapon, 17}, {0xf0000, 4}, 21};
    input.sequence = input.geometrySequence = 10;
    input.nowNs = s.input.nowNs;
    input.observedNs = input.nowNs;
    input.deadlineNs = input.nowNs + 50000000;
    input.focused = input.itemTracked = input.weaponTracked = input.held = input.eligible = true;
    input.weaponClaim = {
        {10, physical, InteractionHand::Right, HandClaimKind::GunHold, input.identity.weapon, {1, 17}, 0},
        input.deadlineNs,
        10};
    input.itemClaim = {
        {11, physical, InteractionHand::Left, HandClaimKind::AmmoObject, input.identity.item, {2, 4}, 10},
        input.deadlineNs,
        10};
    sample.assetName = SpasReloadAsset;
    sample.meshPath = SpasReloadMesh;
    sample.rigFingerprint = SpasReloadRig;
    sample.selectedMeshIdentityVerified = true;
    sample.rawLeftWristWorldMeters = sample.weaponWorldMeters = reload_insertion_detail::Identity();
    const auto map = BindBc2ReloadOwners(physical, input.identity.weapon, *native, input.nowNs);
    CHECK(map);
    sample.native = *ToBc2ReloadInteractionLease(*map, *native, input.nowNs);
    const auto profile = SpasReloadInsertionProfile();
    ReloadInsertionSeat seat{88,
                             10,
                             input.identity,
                             {profile.id, profile.revision},
                             input.itemClaim.token,
                             input.weaponClaim.token,
                             ReloadOperation::InsertRound};
    ManualReloadRequest request{
        777, {physical.actor, physical.actorGeneration, o.weapon, 17, o.space}, ReloadOperation::InsertRound, 2, 0};
    Bc2ReloadTargets targets{input.identity,
                             input.itemClaim.token,
                             input.weaponClaim.token,
                             10,
                             93,
                             input.observedNs,
                             input.deadlineNs,
                             reload_insertion_detail::Identity(),
                             reload_insertion_detail::Identity()};
    Bc2ReloadRequestBridge bridge(true);
    const auto started = bridge.Begin(sample, *native, seat, request, targets);
    CHECK(started.submit && s.gate.Submit(*started.submit, s.input.nowNs));
    const auto advancing = s.Lease();
    CHECK(advancing && !advancing->allThreeHeld);
    sample.native = *ToBc2ReloadInteractionLease(*map, *advancing, input.nowNs);
    input.held = false;
    CHECK(!bridge.Update(sample, *advancing).presentation);
    CHECK(s.CompleteRound());
    input.nowNs = s.input.nowNs;
    const auto settled = s.Lease();
    CHECK(settled);
    sample.native = *ToBc2ReloadInteractionLease(*map, *settled, input.nowNs);
    const auto ack = s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs);
    CHECK(ack);
    const auto done = bridge.Update(sample, *settled, ack);
    CHECK(done.acknowledged && done.acknowledged->owner == request.owner && done.acknowledged->request == 777 &&
          done.consumed->claim == seat.itemClaim);
    return 0;
}
int MixedCohortRetainsOriginalPublication()
{
    Simulation s;
    CHECK(s.Arm() && s.gate.Submit(s.Request(), s.input.nowNs));
    CHECK(s.Advance(0));
    const auto baseline = s.Lease();
    CHECK(baseline && !baseline->allThreeHeld && baseline->loaded == 6);
    CHECK(s.Rehold(0));
    const auto mixed = s.Lease();
    CHECK(mixed && mixed->observedNs == baseline->observedNs && mixed->sequence == baseline->sequence);
    CHECK(s.Advance(1) && s.Rehold(1) && s.Advance(2));
    CHECK(s.Rehold(2, false)); // Noncoherent publication must also retain its stamp.
    CHECK(s.Lease()->observedNs == baseline->observedNs);
    CHECK(s.Rehold(0, true));
    const auto converged = s.Lease();
    CHECK(converged && converged->observedNs > baseline->observedNs && converged->loaded == 7 &&
          converged->reserve == 20 && converged->allThreeHeld);
    return 0;
}
int WrongReservationCannotReleaseNativeCopies()
{
    for (unsigned test = 0; test < 8; ++test)
    {
        Simulation s;
        CHECK(s.Arm());
        auto request = s.Request();
        switch (test)
        {
        case 0:
            request.reservation.claim.owner.actor = s.input.identity.owner.soldier;
            break;
        case 1:
            request.reservation.claim.owner.space++;
            break;
        case 2:
            request.reservation.request++;
            break;
        case 3:
            request.reservation.cycle++;
            break;
        case 4:
            request.heldLease.loaded--;
            break;
        case 5:
            request.heldLease.deadlineNs = s.input.nowNs;
            break;
        case 6:
            request.request.owner.equipGeneration++;
            break;
        case 7:
            request.heldLease.allThreeHeld = false;
            break;
        }
        CHECK(!s.gate.Submit(request, s.input.nowNs) && s.gate.Phase() == ReloadRequestCyclePhase::Holding &&
              s.Lease()->allThreeHeld && !s.gate.PendingRequest());
    }
    return 0;
}
int InsertionDeadlineAndFinalPatchCheck()
{
    Simulation s;
    CHECK(s.Arm() && s.gate.Submit(s.Request(), s.input.nowNs));
    // Native progress stalls while valid controls continue; this may not wait forever.
    for (unsigned n = 0; n < 151 && s.gate.Phase() != ReloadRequestCyclePhase::Cancelled; ++n)
        if (!s.Tick(10000000))
            break;
    CHECK(s.gate.Failure() == ReloadRequestCycleFailure::Expired && s.gate.UnresolvedRequest());
    Simulation edge;
    CHECK(edge.Arm() && edge.Tick());
    const auto d = edge.Begin(0);
    CHECK(d.hold && !edge.gate.Allows(d, d.deadlineNs));
    CHECK(edge.gate.Phase() == ReloadRequestCyclePhase::Cancelled);
    return 0;
}
int SameCycleEightShellReloadAcrossFortySeconds()
{
    Simulation s;
    for (auto &branch : s.input.branches)
        branch.loaded = 0;
    CHECK(s.Arm());
    const auto started = s.input.nowNs;
    for (unsigned shell = 0; shell < 8; ++shell)
    {
        // Six seconds between shells: the user's pace is not a cycle timeout.
        for (unsigned wait = 0; wait < 600; ++wait)
            CHECK(s.Tick(10000000) && s.HoldAll());
        const auto lease = s.Lease();
        CHECK(lease && lease->cycle == 93 && lease->allThreeHeld && lease->loaded == int(shell));
        CHECK(s.gate.Submit(s.Request(500 + shell), s.input.nowNs) && s.CompleteRound());
        const auto ack = s.gate.TakeAcknowledgement(s.input.identity, 93, s.input.nowNs);
        CHECK(ack && ack->acknowledgement.cycle == 93 && ack->acknowledgement.semantic.request == 500 + shell);
    }
    CHECK(s.input.nowNs - started > 48000000000ll && s.gate.Cycle() == 93 &&
          s.gate.Phase() == ReloadRequestCyclePhase::Finished && !s.gate.UnresolvedRequest());
    for (const auto &branch : s.input.branches)
        CHECK(branch.loaded == 8 && branch.reserve == 13);
    return 0;
}
int DeadlineOverflowCannotReleaseHeldCopies()
{
    Simulation s;
    s.input.nowNs = std::numeric_limits<std::int64_t>::max() - 1000000000;
    s.input.leaseDeadlineNs = s.input.nowNs + 100000000;
    s.control = {s.input.identity, 93, 1, s.input.nowNs, s.input.leaseDeadlineNs, true};
    CHECK(s.Arm());
    CHECK(!s.gate.Submit(s.Request(), s.input.nowNs) && !s.gate.PendingRequest() &&
          s.gate.Phase() == ReloadRequestCyclePhase::Holding && s.Lease()->allThreeHeld);
    Simulation safe;
    safe.input.nowNs = std::numeric_limits<std::int64_t>::max() - 2000000000;
    safe.input.leaseDeadlineNs = safe.input.nowNs + 100000000;
    safe.control = {safe.input.identity, 93, 1, safe.input.nowNs, safe.input.leaseDeadlineNs, true};
    CHECK(safe.Arm() && safe.gate.Submit(safe.Request(), safe.input.nowNs) && safe.CompleteRound());
    CHECK(safe.gate.TakeAcknowledgement(safe.input.identity, 93, safe.input.nowNs));
    return 0;
}
} // namespace
int CancelledOrphanRequiresProvenCallbackDrain()
{
    Simulation s;
    CHECK(s.Arm() && s.gate.Submit(s.Request(), s.input.nowNs) && s.Tick());
    const auto orphan = s.Begin(0);
    CHECK(orphan.tracked && !orphan.hold);
    s.gate.Cancel();
    auto next = s.control;
    next.cycle++;
    CHECK(!s.gate.DrainCancelledInvocations(false) && !s.gate.Start(next, s.input.nowNs));
    CHECK(s.gate.DrainCancelledInvocations(true) && s.gate.Phase() == ReloadRequestCyclePhase::Cancelled &&
          s.gate.UnresolvedRequest() && s.gate.PendingRequest() == 47);
    CHECK(!s.gate.Start(s.control, s.input.nowNs)); // Drain does not reset cycle history.
    CHECK(s.gate.Start(next, s.input.nowNs));
    Simulation live;
    CHECK(live.Arm() && live.Tick());
    const auto active = live.Begin(0);
    CHECK(!live.gate.DrainCancelledInvocations(true) && live.End(active));
    return 0;
}
int PreholdMissCannotReuseContextOrExtendLease()
{
    Simulation s;CHECK(s.gate.Start(s.control,s.input.nowNs));
    CHECK(!s.Begin(0).tracked&&!s.Begin(1).tracked);
    const auto original=s.control;
    CHECK(s.gate.MissingEvidence(s.input.nowNs+100)&&s.gate.Phase()==ReloadRequestCyclePhase::Arming);
    s.input.nowNs+=200;
    CHECK(!s.Begin(2).tracked&&!s.Begin(0).tracked);
    const auto complete=s.Begin(1);CHECK(complete.tracked&&complete.hold&&s.End(complete));
    CHECK(!s.gate.MissingEvidence(s.input.nowNs)&&s.gate.Phase()==ReloadRequestCyclePhase::Cancelled);
    Simulation expired;CHECK(expired.gate.Start(expired.control,expired.input.nowNs));
    CHECK(!expired.gate.MissingEvidence(expired.control.deadlineNs)&&expired.gate.Phase()==ReloadRequestCyclePhase::Cancelled);
    CHECK(!expired.gate.KeepAlive({expired.control.identity,93,2,expired.control.deadlineNs,expired.control.deadlineNs+100000000,true},expired.control.deadlineNs));
    return 0;
}
int ActiveMissingEvidenceAlwaysCancels()
{
    for(unsigned advancing=0;advancing<2;++advancing){Simulation s;CHECK(s.Arm());
        if(advancing)CHECK(s.gate.Submit(s.Request(),s.input.nowNs));
        CHECK(s.Tick());const auto decision=s.Begin(0);CHECK(decision.tracked);
        CHECK(!s.gate.MissingEvidence(s.input.nowNs)&&s.gate.Phase()==ReloadRequestCyclePhase::Cancelled);
        CHECK(!s.gate.Allows(decision,s.input.nowNs)&&!s.Lease());}
    return 0;
}
int NativeObservationCanBeOvertakenByControl()
{
    Simulation s; CHECK(s.Arm());
    const auto start=s.input.nowNs;
    auto sampled=s.input; sampled.branch=0; sampled.nowNs=start+1000000;
    const auto originalDeadline=sampled.leaseDeadlineNs;
    auto control=s.control; ++control.sequence;
    control.observedNs=start+2000000; control.deadlineNs=control.observedNs+100000000;
    CHECK(s.gate.KeepAlive(control,control.observedNs));
    // Native read completed first, but a controller/API call acquired the policy lock first.
    const auto decision=s.gate.Evaluate(sampled,true,true,++s.id,start+3000000);
    CHECK(decision.hold && decision.decisionNs==start+3000000);
    CHECK(s.gate.Allows(decision,start+3000001));
    const auto exitObserved=start+4000000;
    ++control.sequence; control.observedNs=start+5000000; control.deadlineNs=control.observedNs+100000000;
    CHECK(s.gate.KeepAlive(control,control.observedNs));
    ReloadDeltaOverride patch; patch.applied=patch.restored=true;
    CHECK(s.gate.Finish(decision,sampled.branches[0],exitObserved,true,patch,start+6000000));
    const auto lease=s.gate.Lease(sampled.identity,control.cycle,start+6000000);
    CHECK(lease && lease->allThreeHeld && lease->observedNs==sampled.nowNs && lease->deadlineNs==originalDeadline);
    CHECK(!s.gate.ClockFailure());
    // Neither read nor hold freshness was extended to its later processing time.
    ++control.sequence; control.observedNs=start+55000000; control.deadlineNs=control.observedNs+100000000;
    CHECK(s.gate.KeepAlive(control,control.observedNs));
    CHECK(!s.gate.Lease(sampled.identity,control.cycle,control.observedNs)->allThreeHeld);
    return 0;
}
int DelayedEvidenceDoesNotExtendAnyDeadline()
{
    for(unsigned condition=0;condition<4;++condition){
        Simulation s; CHECK(s.Arm()); const auto before=s.input.nowNs;
        auto sample=s.input; sample.branch=0; sample.nowNs=before+1000;
        std::int64_t now=before+2000;
        if(condition==0) now=s.control.deadlineNs;
        if(condition==1) sample.leaseDeadlineNs=now;
        if(condition==2) now=sample.nowNs+ReloadHoldProbe::ContextFreshNs+1;
        if(condition==3) sample.nowNs=now+1;
        CHECK(!s.gate.Evaluate(sample,true,true,++s.id,now).tracked);
        CHECK(s.gate.Phase()==ReloadRequestCyclePhase::Cancelled);
        if(condition==0){const auto& evidence=s.gate.ClockFailure();
            CHECK(evidence && !evidence->regression && evidence->nowNs==s.control.deadlineNs &&
                  evidence->sourceNs==s.control.observedNs && evidence->deadlineNs==s.control.deadlineNs);}
    }
    Simulation ended; CHECK(ended.Arm()); const auto decision=ended.Begin(0); CHECK(decision.hold);
    auto renewed=ended.control; ++renewed.sequence; renewed.observedNs=decision.deadlineNs-1;
    renewed.deadlineNs=renewed.observedNs+100000000;
    CHECK(ended.gate.KeepAlive(renewed,renewed.observedNs));
    ReloadDeltaOverride patch; patch.applied=patch.restored=true;
    CHECK(!ended.gate.Finish(decision,ended.input.branches[0],decision.deadlineNs-1,true,patch,decision.deadlineNs));
    CHECK(ended.gate.Phase()==ReloadRequestCyclePhase::Cancelled);
    return 0;
}
int TrueProcessingClockRegressionStillCancels()
{
    Simulation s; CHECK(s.Arm()); const auto now=s.input.nowNs;
    CHECK(s.gate.KeepAlive(s.control,now+1000));
    CHECK(!s.gate.Lease(s.input.identity,s.control.cycle,now+999));
    const auto& evidence=s.gate.ClockFailure();
    CHECK(evidence && evidence->regression && evidence->nowNs==now+999 && evidence->lastNowNs==now+1000 &&
          evidence->deadlineNs==s.control.deadlineNs && evidence->sequence==s.control.sequence);
    CHECK(!s.gate.KeepAlive(s.control,now+1001));
    return 0;
}
int PolicyWaitCannotExtendLeaseOrAuthorizeAnOverlappingOriginal()
{
    Simulation expired;CHECK(expired.Arm()&&expired.Tick());const auto decision=expired.Begin(0);CHECK(decision.hold);
    std::atomic_flag gate{};gate.test_and_set();unsigned clocks=0;
    const auto beforeDeadline=decision.deadlineNs-100000,afterDeadline=decision.deadlineNs+1;
    {ReloadPolicyLock lock(gate,[&]{return ++clocks==1?beforeDeadline:afterDeadline;},[&]{gate.clear(std::memory_order_release);});
        CHECK(lock.Held()&&lock.Evidence().contended); // Lock itself recovered within its 250us bound.
        CHECK(!expired.gate.Allows(decision,afterDeadline)); // Original native/control deadline still expired.
        CHECK(expired.gate.Phase()==ReloadRequestCyclePhase::Cancelled);}
    CHECK(decision.deadlineNs==expired.control.deadlineNs&&!gate.test());
    Simulation overlap;CHECK(overlap.Arm()&&overlap.Tick());const auto open=overlap.Begin(0);CHECK(open.hold);
    // Releasing the policy mutex never drains an outstanding original or mints
    // another decision for the same branch. It is only short-term serialization.
    {ReloadPolicyLock lock(gate,[]{return 100ll;},[]{});CHECK(lock.Held());
        CHECK(!overlap.Begin(0).tracked&&overlap.gate.Failure()==ReloadRequestCycleFailure::Overlap);}
    CHECK(!gate.test());return 0;
}
int CurrentShellHoldSurvivesBoundedFrameHitch()
{
    for(float delta:{.0596221f,.1f}){
        Simulation s;CHECK(s.Arm());const auto loaded=s.input.branches[0].loaded;
        CHECK(s.Tick(60000000));s.input.context.deltaSeconds=delta;
        CHECK(s.HoldAll());const auto waiting=s.Lease();
        CHECK(waiting&&!waiting->allThreeHeld);
        CHECK(s.HoldAll());const auto lease=s.Lease();
        CHECK(lease&&lease->allThreeHeld&&lease->loaded==loaded);
        CHECK(s.gate.Phase()==ReloadRequestCyclePhase::Holding);
    }
    Simulation expired;CHECK(expired.Arm());expired.input.context.deltaSeconds=.0596221f;
    CHECK(!expired.Tick(100000000)&&expired.gate.Failure()==ReloadRequestCycleFailure::Control);
    return 0;
}
int main()
{
    if(CurrentShellHoldSurvivesBoundedFrameHitch())return 1;
    if (ExplicitRequestOnlyAndTruthfulHold() || RealRequestTwoRoundsAndFinalAck() || QuietCohortAndActualReholds() ||
        NoBorrowedLeaseDuringOriginal() || OwnerControlAndTimingCancel() || DeadmanAndExactDuplicateControl() ||
        PatchFailureAndNativeTransferMismatch() || ExpiredAckCannotBeRestampedOrReplayed() ||
        CancelAfterNativeCompletionDoesNotRollback() || PersistentBeyondObserverLifetime() ||
        PhysicalBridgeIntegration() || MixedCohortRetainsOriginalPublication() ||
        WrongReservationCannotReleaseNativeCopies() || InsertionDeadlineAndFinalPatchCheck() ||
        SameCycleEightShellReloadAcrossFortySeconds() || DeadlineOverflowCannotReleaseHeldCopies() ||
        CancelledOrphanRequiresProvenCallbackDrain() || PreholdMissCannotReuseContextOrExtendLease() || ActiveMissingEvidenceAlwaysCancels() ||
        NativeObservationCanBeOvertakenByControl() || DelayedEvidenceDoesNotExtendAnyDeadline() || TrueProcessingClockRegressionStillCancels() || PolicyWaitCannotExtendLeaseOrAuthorizeAnOverlappingOriginal())
        return 1;
    std::printf("Twenty-three request-driven reload lifecycle groups passed; capability remains off.\n");
    return 0;
}
