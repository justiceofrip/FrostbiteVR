#include "Bc2MagazineReloadCycle.h"
#include "Bc2ReloadConfigDescriptor.h"
#include <limits>
#include <bit>
#include <cstring>
namespace fvr::bc2
{
namespace
{
bool Fresh(std::int64_t observed, std::int64_t deadline, std::int64_t now, std::int64_t limit = 200000000)
{
    return observed > 0 && now >= observed && deadline > now && deadline - observed <= limit;
}
bool Identity(const ReloadHoldIdentity &i)
{
    const auto &o = i.owner;
    return o.player >= 0x10000 && o.soldier >= 0x10000 && o.weak >= 0x10000 && o.weapon >= 0x10000 &&
           o.actorGeneration && o.equipGeneration && o.space && i.serverPlayer >= 0x10000 &&
           i.serverSoldier >= 0x10000 && i.serverItem >= 0x10000 &&
           std::all_of(i.firing.begin(), i.firing.end(), [](auto x) { return x >= 0x10000; }) &&
           i.firing[0] != i.firing[1] && i.firing[0] != i.firing[2] && i.firing[1] != i.firing[2];
}
bool Control(const ReloadCycleControl &c, std::int64_t now)
{
    return c.permitted && c.cycle && c.sequence && Identity(c.identity) &&
           Fresh(c.observedNs, c.deadlineNs, now, 150000000);
}
bool Safe(const ReloadUpdateContext &c)
{
    return std::isfinite(c.deltaSeconds) && c.deltaSeconds > 0 && c.deltaSeconds <= .05f &&
           c.reloadTimeMultiplier == 1 && !c.inputFlags && !c.fireRequested && !c.orderRequested &&
           !c.reloadRequested && !c.flags24Through28[2] && !c.flags24Through28[3] && !c.flags24Through28[4];
}
bool SameHeld(const ReloadFiringObservation &a, const ReloadFiringObservation &b)
{
    return a.address == b.address && a.wrapperOffset == b.wrapperOffset && a.currentState == b.currentState &&
           a.nextState == b.nextState && a.phaseTimer == b.phaseTimer && a.loaded == b.loaded && a.reserve == b.reserve;
}
} // namespace
bool IsXm8MagazineConfig(const ReloadObservedConfig& c)noexcept {
    return Xm8MagazineNativeProfile.Matches(c);
}
bool ReadXm8MagazineTiming(const ReloadStateMemory& memory,const ReloadObservedConfig& c)noexcept {
    return Xm8MagazineNativeProfile.ReadTiming(memory,c);
}
bool Bc2MagazineReloadCycle::Open() const noexcept
{
    return std::any_of(open_.begin(), open_.end(), [](auto v) { return v != 0; });
}
void Bc2MagazineReloadCycle::Cancel(ReloadRequestCycleFailure reason) noexcept
{
    if (phase_ == ReloadRequestCyclePhase::Disabled || phase_ == ReloadRequestCyclePhase::Idle ||
        phase_ == ReloadRequestCyclePhase::Cancelled)
        return;
    phase_ = ReloadRequestCyclePhase::Cancelled;
    failure_ = reason;
    unresolved_ = pendingRequest_ != 0;
    published_.reset();
    ack_.reset();
    completion_.Cancel();
}
bool Bc2MagazineReloadCycle::DrainCancelledInvocations(bool callbacksDrained) noexcept
{
    if (!callbacksDrained || phase_ != ReloadRequestCyclePhase::Cancelled)
        return false;
    open_ = {};
    return true;
}
bool Bc2MagazineReloadCycle::MissingEvidence(std::int64_t now) noexcept
{
    if (Current(now) && phase_ == ReloadRequestCyclePhase::Arming && !Open())
    {
        contexts_ = {};
        holds_ = {};
        published_.reset();
        return true;
    }
    Cancel(ReloadRequestCycleFailure::Owner);
    return false;
}
bool Bc2MagazineReloadCycle::Start(const ReloadCycleControl &c,const interaction::ManualReloadRequest& unseat,std::int64_t now,std::optional<ReloadMagazineStartupPulse> pulse) noexcept
{
    if ((phase_ != ReloadRequestCyclePhase::Idle && phase_ != ReloadRequestCyclePhase::Finished &&
         phase_ != ReloadRequestCyclePhase::Cancelled) ||
        Open() || (phase_ == ReloadRequestCyclePhase::Finished && pendingRequest_) || !Control(c, now) ||
        (pulse&&(!pulse->ValidFor(c)||pulse->endNs<=now))||
        c.cycle <= lastCycle_ || !unseat.id || unseat.id<=lastUnseat_ || unseat.id<=lastRequest_ ||
        unseat.operation!=interaction::ReloadOperation::UnseatMagazine ||
        unseat.owner!=interaction::ManualReloadOwner{c.identity.owner.soldier,c.identity.owner.actorGeneration,
            c.identity.owner.weapon,c.identity.owner.equipGeneration,c.identity.owner.space})
        return false;
    unseatRequest_=unseat;lastUnseat_=unseat.id;unseatAckTaken_=false;units_=0;
    control_ = c;startupPulse_=pulse;firstHoldingNs_=0;armingContexts_={};
    lastCycle_ = c.cycle;
    lastNow_ = now;
    advanceDeadline_ = 0;
    pendingRequest_ = 0;
    config_ = {};
    configured_ = unresolved_ = false;
    clockFailure_.reset();
    loaded_ = reserve_ = capacity_ = 0;
    contexts_ = {};
    holds_ = {};
    open_ = {};
    published_.reset();
    ack_.reset();
    phase_ = ReloadRequestCyclePhase::Arming;
    failure_ = ReloadRequestCycleFailure::None;
    return true;
}
bool Bc2MagazineReloadCycle::Current(std::int64_t now) noexcept
{
    if (phase_ == ReloadRequestCyclePhase::Disabled || phase_ == ReloadRequestCyclePhase::Idle ||
        phase_ == ReloadRequestCyclePhase::Cancelled)
        return false;
    if (now < lastNow_ || !Control(control_, now))
    {
        if (!clockFailure_)
            clockFailure_ = ReloadRequestClockFailure{now, lastNow_, control_.observedNs, control_.deadlineNs,
                                                     control_.sequence, now < lastNow_};
        Cancel(ReloadRequestCycleFailure::Control);
        return false;
    }
    lastNow_ = now;
    if ((phase_ == ReloadRequestCyclePhase::Advancing && now >= advanceDeadline_) || (ack_ && now >= ack_->deadlineNs))
    {
        Cancel(ReloadRequestCycleFailure::Expired);
        return false;
    }
    return true;
}
bool Bc2MagazineReloadCycle::KeepAlive(const ReloadCycleControl &c, std::int64_t now) noexcept
{
    // A new packet must not resurrect a cycle whose old deadman lease expired.
    if (!Current(now))
        return false;
    if (!Control(c, now) || c.identity != control_.identity || c.cycle != control_.cycle ||
        c.sequence < control_.sequence || c.observedNs < control_.observedNs ||
        (c.sequence == control_.sequence && c != control_))
    {
        Cancel(ReloadRequestCycleFailure::Control);
        return false;
    }
    control_ = c;
    return true;
}
bool Bc2MagazineReloadCycle::Recent(const std::array<std::int64_t, 3> &times, std::int64_t now) const noexcept
{
    return std::all_of(times.begin(), times.end(),
                       [&](auto t) { return t > 0 && t <= now && now - t <= ReloadHoldProbe::ContextFreshNs; });
}
bool Bc2MagazineReloadCycle::Common(const ReloadHoldInput &in, bool timing, std::int64_t now) noexcept
{
    if (!Current(now))
        return false;
    if (!in.verified || in.branch >= 3 || in.identity != control_.identity ||
        !Fresh(in.nowNs, in.leaseDeadlineNs, now) || now - in.nowNs > ReloadHoldProbe::ContextFreshNs ||
        !profile_.Matches(in.config) ||
        (configured_ && in.config != config_))
    {
        Cancel(ReloadRequestCycleFailure::Owner);
        return false;
    }
    if (!timing || !Safe(in.context))
    {
        if (phase_ == ReloadRequestCyclePhase::Arming)
        {
            contexts_[in.branch] = 0;
            return false;
        }
        Cancel(ReloadRequestCycleFailure::Timing);
        return false;
    }
    for (unsigned n = 0; n < 3; ++n)
    {
        const auto &b = in.branches[n];
        if (b.address != in.identity.firing[n] ||
            b.wrapperOffset != (n == 0   ? 0x3cu
                                : n == 1 ? 0x40u
                                         : 0x10u) ||
            b.currentState > 15 || b.previousState > 15 || b.nextState > 15 || !std::isfinite(b.phaseTimer) ||
            b.loaded < 0 || b.loaded > 1000000 || b.reserve < 0 || b.reserve > 1000000 || (b.flagsA8 & (8 | 16)) ||
            in.capacities[n] <= 0 || in.capacities[n] > 1000000 || in.capacities[n] != in.capacities[0] ||
            (capacity_ && in.capacities[n] != capacity_))
        {
            Cancel(ReloadRequestCycleFailure::State);
            return false;
        }
    }
    if (!configured_)
    {
        config_ = in.config;
        configured_ = true;
    }
    const auto contextObserved=startupPulse_?in.contextObservedNs:in.nowNs;
    if(startupPulse_&&(contextObserved<=0||contextObserved>in.nowNs||now<contextObserved||
        now-contextObserved>ReloadHoldProbe::ContextFreshNs)){
        if(phase_==ReloadRequestCyclePhase::Arming){contexts_[in.branch]=0;return false;}
        Cancel(ReloadRequestCycleFailure::Owner);return false;
    }
    contexts_[in.branch] = contextObserved;
    return true;
}
void Bc2MagazineReloadCycle::Publish(const ReloadHoldInput &in, bool stable, std::int64_t now) noexcept
{
    if (!stable || Open())
        return;
    // A mixed predicted/server cohort has no honest single ammo count. Retain
    // the previous publication with its ORIGINAL timestamp until convergence;
    // never relabel baseline counts as a fresh observation during advancement.
    for (const auto &b : in.branches)
        if (b.loaded != in.branches[0].loaded || b.reserve != in.branches[0].reserve)
            return;
    bool held = phase_ == ReloadRequestCyclePhase::Holding && Recent(holds_, now) && Recent(contexts_, now);
    for (const auto &b : in.branches)
        held &= profile_.Wait(b) && b.loaded == loaded_ && b.reserve == reserve_;
    published_ = ReloadMagazineLease{control_.identity,
                                  control_.cycle,
                                  ++sequence_,
                                  in.nowNs,
                                  std::min(in.leaseDeadlineNs, control_.deadlineNs),
                                  in.branches[0].loaded,
                                  in.branches[0].reserve,
                                  capacity_,
                                  true,
                                  held};
}
bool Bc2MagazineReloadCycle::Complete(const ReloadHoldInput &in, bool stable, std::int64_t now) noexcept
{
    if (!stable || Open())
        return true;
    ReloadMagazineSample s{control_.identity,
                        control_.cycle,
                        ++sequence_,
                        in.nowNs,
                        std::min({in.leaseDeadlineNs, control_.deadlineNs, advanceDeadline_}),
                        in.branches,
                        true,
                        false};
    const bool done = completion_.Observe(s, now);
    if (completion_.Phase() == ReloadMagazinePhase::Failed)
    {
        Cancel(ReloadRequestCycleFailure::Completion);
        return false;
    }
    if (done)
    {
        const auto a = completion_.TakeAcknowledgement(control_.identity, control_.cycle, now);
        if (!a)
        {
            Cancel(ReloadRequestCycleFailure::Completion);
            return false;
        }
        ack_ = ReloadMagazineAckEvidence{*a, s.observedNs, s.deadlineNs, true};
        loaded_+=units_;
        reserve_-=units_;
        phase_=ReloadRequestCyclePhase::Finished;
    }
    return true;
}
ReloadRequestDecision Bc2MagazineReloadCycle::Track(const ReloadHoldInput &in, std::uint64_t update, bool hold,
                                                 std::int64_t now) noexcept
{
    if (!update || open_[in.branch])
    {
        Cancel(ReloadRequestCycleFailure::Overlap);
        return {};
    }
    open_[in.branch] = update;
    auto deadline = std::min(control_.deadlineNs, in.leaseDeadlineNs);
    if (phase_ == ReloadRequestCyclePhase::Advancing)
        deadline = std::min(deadline, advanceDeadline_);
    return {update, control_.cycle, in.branch, true, hold, now, deadline, in.branches[in.branch]};
}
ReloadRequestDecision Bc2MagazineReloadCycle::Evaluate(const ReloadHoldInput &in, bool timing, bool stable,
                                                      std::uint64_t update) noexcept
{
    return Evaluate(in, timing, stable, update, in.nowNs);
}
ReloadRequestDecision Bc2MagazineReloadCycle::Evaluate(const ReloadHoldInput &in, bool timing, bool stable,
                                                      std::uint64_t update, std::int64_t now) noexcept
{
    if (phase_ == ReloadRequestCyclePhase::Finished || !Common(in, timing, now))
        return {};
    if (phase_ == ReloadRequestCyclePhase::Arming)
    {
        if (!stable || !Recent(contexts_, now)||
            (startupPulse_&&(now<startupPulse_->endNs||!std::all_of(contexts_.begin(),contexts_.end(),
                [&](auto observed){return observed>=startupPulse_->endNs;}))))
            return {};
        for (const auto &b : in.branches)
            if (!profile_.Wait(b) || b.phaseTimer < .1f || b.phaseTimer > profile_.HoldCeiling() || b.reserve <= 0 ||
                b.loaded >= in.capacities[0] || b.loaded != in.branches[0].loaded ||
                b.reserve != in.branches[0].reserve)
                return {};
        loaded_ = in.branches[0].loaded;
        reserve_ = in.branches[0].reserve;
        capacity_ = in.capacities[0];
        firstHoldingNs_=now;armingContexts_=contexts_;
        phase_ = ReloadRequestCyclePhase::Holding;
    }
    if (phase_ == ReloadRequestCyclePhase::Advancing && !Complete(in, stable, now))
        return {};
    Publish(in, stable, now);
    if (phase_ == ReloadRequestCyclePhase::Finished)
        return {};
    const auto &b = in.branches[in.branch];
    if (phase_ == ReloadRequestCyclePhase::Holding)
    {
        for (const auto &v : in.branches)
            if (!profile_.Wait(v) || v.loaded != loaded_ || v.reserve != reserve_)
            {
                Cancel(ReloadRequestCycleFailure::State);
                return {};
            }
        return Track(in, update, true, now);
    }
    if (b.loaded == loaded_ && b.reserve == reserve_)
    {
        if ((completion_.AdvancedMask() & (1u << in.branch)) || !profile_.Wait(b) || !Recent(contexts_, now))
        {
            Cancel(ReloadRequestCycleFailure::State);
            return {};
        }
        return Track(in, update, false, now);
    }
    if (b.loaded == loaded_ + units_ && b.reserve == reserve_ - units_)
    {
        // The reviewed state12 envelope completes the remaining native reload tail. It must
        // progress naturally; it is not the SPAS next-round hold interval.
        if (profile_.Tail(b) ||
            ((b.loaded == capacity_ || !b.reserve) && (b.currentState == 1 || b.currentState == 2) &&
            (b.nextState == 1 || b.nextState == 2)))
            return Track(in, update, false, now);
    }
    Cancel(ReloadRequestCycleFailure::State);
    return {};
}
bool Bc2MagazineReloadCycle::Allows(const ReloadRequestDecision &d, std::int64_t now) noexcept
{
    if (!d.tracked || !d.hold || d.branch >= 3 || d.cycle != control_.cycle || open_[d.branch] != d.update ||
        !Current(now))
        return false;
    if (now < d.decisionNs || now >= d.deadlineNs)
    {
        Cancel(ReloadRequestCycleFailure::Expired);
        return false;
    }
    return true;
}
bool Bc2MagazineReloadCycle::Finish(const ReloadRequestDecision &d, const ReloadFiringObservation &after,
                                   std::int64_t observed, bool retained, const ReloadDeltaOverride &delta) noexcept
{
    return Finish(d, after, observed, retained, delta, observed);
}
bool Bc2MagazineReloadCycle::Finish(const ReloadRequestDecision &d, const ReloadFiringObservation &after,
                                   std::int64_t observed, bool retained, const ReloadDeltaOverride &delta,
                                   std::int64_t now) noexcept
{
    if (!d.tracked)
        return true;
    if (d.branch >= 3 || d.cycle != control_.cycle || open_[d.branch] != d.update)
    {
        Cancel(ReloadRequestCycleFailure::Overlap);
        return false;
    }
    open_[d.branch] = 0;
    if (!Current(now))
        return false;
    if (!retained || observed < d.decisionNs || now < observed ||
        now - observed > ReloadHoldProbe::ContextFreshNs || now >= d.deadlineNs ||
        after.address != control_.identity.firing[d.branch] ||
        after.wrapperOffset != d.before.wrapperOffset)
    {
        Cancel(ReloadRequestCycleFailure::Owner);
        return false;
    }
    if (d.hold)
    {
        if (!delta.applied || !delta.restored || delta.unexpectedNativeWrite || !SameHeld(d.before, after))
        {
            Cancel(ReloadRequestCycleFailure::Patch);
            return false;
        }
        holds_[d.branch] = observed;
    }
    else
    {
        if (delta.applied || phase_ != ReloadRequestCyclePhase::Advancing)
        {
            Cancel(ReloadRequestCycleFailure::Patch);
            return false;
        }
        const bool baseline = after.loaded == loaded_ && after.reserve == reserve_,
                   advanced = after.loaded == loaded_ + units_ && after.reserve == reserve_ - units_;
        if ((!baseline && !advanced) || (baseline && (completion_.AdvancedMask() & (1u << d.branch))) ||
            (!profile_.Wait(after) &&
             !(advanced&&profile_.Tail(after)) &&
             !((after.loaded == capacity_ || !after.reserve) && (after.currentState == 1 || after.currentState == 2) &&
               (after.nextState == 1 || after.nextState == 2))))
        {
            Cancel(ReloadRequestCycleFailure::Completion);
            return false;
        }
    }
    return true;
}
bool Bc2MagazineReloadCycle::Transfer(const ReloadMagazineTransfer &t, std::uint64_t parent) noexcept
{
    if (phase_ != ReloadRequestCyclePhase::Advancing || t.branch >= 3 || !parent || open_[t.branch] != parent ||
        !completion_.Observe(t))
    {
        Cancel(ReloadRequestCycleFailure::Completion);
        return false;
    }
    return true;
}
ReloadMagazineLeaseObservation Bc2MagazineReloadCycle::ObserveLease(const ReloadHoldIdentity& id,std::uint64_t cycle,std::int64_t now) noexcept {
    if(!Current(now)||id!=control_.identity||cycle!=control_.cycle)return {};
    // Missing publication is not a negative all-three-hold receipt. Current()
    // retains original control, operation and acknowledgement expiry checks.
    if(Open()||!published_||!Fresh(published_->observedNs,published_->deadlineNs,now))
        return {ReloadMagazineObservationResult::Deferred,{}};
    // Holding has no fresh negative publication: actual state/context/owner
    // or patch rejection cancels the cycle under the same policy exclusion.
    // A missing positive receipt, including a pre-Finish publication, grants
    // only bounded waiting; it never becomes new all-three hold authority.
    if(phase_==ReloadRequestCyclePhase::Holding&&
       (!published_->allThreeHeld||!Recent(holds_,now)||!Recent(contexts_,now)))
        return {ReloadMagazineObservationResult::Deferred,{}};
    auto lease=*published_;
    lease.allThreeHeld &= phase_==ReloadRequestCyclePhase::Holding&&Recent(holds_,now)&&Recent(contexts_,now);
    return {ReloadMagazineObservationResult::Ready,lease};
}
std::optional<ReloadMagazineLease> Bc2MagazineReloadCycle::Lease(const ReloadHoldIdentity &id, std::uint64_t cycle,
                                                             std::int64_t now) noexcept
{
    if (!Current(now) || id != control_.identity || cycle != control_.cycle || Open() || !published_ ||
        !Fresh(published_->observedNs, published_->deadlineNs, now))
        return {};
    auto lease = *published_;
    lease.allThreeHeld &= phase_ == ReloadRequestCyclePhase::Holding && Recent(holds_, now) && Recent(contexts_, now);
    return lease;
}
bool Bc2MagazineReloadCycle::Submit(const ReloadMagazineNativeRequest &request, std::int64_t now) noexcept
{
    const auto current = Lease(control_.identity, control_.cycle, now);
    if (!current || phase_ != ReloadRequestCyclePhase::Holding || !unseatAckTaken_ || pendingRequest_ || ack_ || !current->allThreeHeld ||
        now > std::numeric_limits<std::int64_t>::max() - profile_.completionDeadlineNs)
        return false;
    const auto &l = request.heldLease;
    const auto &r = request.reservation;
    const auto &owner = control_.identity.owner;
    if (!l.nativeBindingVerified || !l.allThreeHeld || l.identity != current->identity || l.cycle != current->cycle ||
        !l.sequence || l.sequence > current->sequence || !Fresh(l.observedNs, l.deadlineNs, now) ||
        l.observedNs > current->observedNs || l.loaded != loaded_ || l.reserve != reserve_ || l.capacity != capacity_ ||
        !request.request.id || request.request.id<=lastUnseat_ || request.request.id <= lastRequest_ ||
        request.reservedUnits!=unsigned(std::min(capacity_-loaded_,reserve_)) || r.request != request.request.id ||
        r.cycle != control_.cycle || !r.seat || !r.claim.id || !r.item.id || !r.item.generation ||
        r.claim.item != r.item || r.claim.kind != interaction::HandClaimKind::AmmoObject ||
        r.claim.hand != interaction::InteractionHand::Left ||
        r.claim.owner.actor != ((std::uint64_t(owner.weak) << 32) | owner.soldier) ||
        r.claim.owner.actorGeneration != owner.actorGeneration || r.claim.owner.space != owner.space ||
        !r.claim.owner.equipGeneration)
        return false;
    advanceDeadline_ = now + profile_.completionDeadlineNs;
    if (!completion_.Begin(request.request, *current, now, advanceDeadline_, profile_.completionDeadlineNs))
        return false;
    units_=std::min(capacity_-loaded_,reserve_);
    pendingRequest_ = lastRequest_ = request.request.id;
    phase_ = ReloadRequestCyclePhase::Advancing;
    holds_ = {};
    published_->allThreeHeld = false;
    return true;
}
std::optional<ReloadMagazineAckEvidence> Bc2MagazineReloadCycle::TakeAcknowledgement(const ReloadHoldIdentity &id,
                                                                               std::uint64_t cycle,
                                                                               std::int64_t now) noexcept
{
    if (!Current(now) || Open() || id != control_.identity || cycle != control_.cycle || !ack_)
        return {};
    auto out = ack_;
    ack_.reset();
    pendingRequest_ = 0;
    return out;
}

std::optional<ReloadMagazineGateAcknowledgement> Bc2MagazineReloadCycle::TakeUnseatAcknowledgement(
    const ReloadHoldIdentity& identity,std::uint64_t cycle,std::int64_t now)noexcept {
    const auto held=Lease(identity,cycle,now);
    if(!held||!held->allThreeHeld||unseatAckTaken_||phase_!=ReloadRequestCyclePhase::Holding)return {};
    unseatAckTaken_=true;
    return ReloadMagazineGateAcknowledgement{{unseatRequest_.id,unseatRequest_.owner,
        unseatRequest_.operation,interaction::ReloadAcknowledgement::Applied},*held};
}
} // namespace fvr::bc2
