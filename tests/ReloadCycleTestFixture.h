#include "Bc2ReloadRequestCycle.h"
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
} // namespace
