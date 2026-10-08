#include "Bc2ReloadRequestBridge.h"
namespace fvr::bc2
{
namespace
{
bool Fresh(std::int64_t observed, std::int64_t deadline, std::int64_t now, std::int64_t limit = 200000000)
{
    return observed > 0 && now >= observed && deadline > now && deadline - observed <= limit;
}
bool Native(const ReloadRoundLease &l, std::int64_t now)
{
    const auto &o = l.identity.owner;
    return l.nativeBindingVerified && l.cycle && l.sequence && Fresh(l.observedNs, l.deadlineNs, now) &&
           o.player >= 0x10000 && o.soldier >= 0x10000 && o.weak >= 0x10000 && o.weapon >= 0x10000 &&
           o.actorGeneration && o.equipGeneration && o.space && l.identity.serverPlayer >= 0x10000 &&
           l.identity.serverSoldier >= 0x10000 && l.identity.serverItem >= 0x10000 && l.identity.firing[0] >= 0x10000 &&
           l.identity.firing[1] >= 0x10000 && l.identity.firing[2] >= 0x10000 &&
           l.identity.firing[0] != l.identity.firing[1] && l.identity.firing[0] != l.identity.firing[2] &&
           l.identity.firing[1] != l.identity.firing[2];
}
interaction::ManualReloadOwner PhysicalOwner(const Bc2ReloadOwnerMap &m)
{
    return {m.physical.actor, m.physical.actorGeneration, m.weapon.id, m.physical.equipGeneration, m.physical.space};
}
interaction::ManualReloadOwner NativeOwner(const Bc2ReloadOwnerMap &m)
{
    const auto &o = m.native.owner;
    return {o.soldier, o.actorGeneration, o.weapon, o.equipGeneration, o.space};
}
bool Profile(const Bc2ReloadInteractionSample &s)
{
    return s.assetName == SpasReloadAsset && s.meshPath == SpasReloadMesh && s.rigFingerprint == SpasReloadRig &&
           s.selectedMeshIdentityVerified;
}
bool Claim(const interaction::HandClaim &c, const interaction::ReloadInsertionSample &s,
           interaction::InteractionHand hand, interaction::HandClaimKind kind, interaction::HandInteractionKey item)
{
    return c.token.id && c.token.owner == s.identity.owner && c.token.hand == hand && c.token.kind == kind &&
           c.token.item == item && c.inputSequence == s.sequence && c.deadlineNs > s.nowNs;
}
bool Input(const interaction::ReloadInsertionSample &s)
{
    return s.sequence && s.identity.trackingEpoch && Fresh(s.observedNs, s.deadlineNs, s.nowNs, 150000000) &&
           s.focused && s.weaponTracked && !s.cancel;
}
bool InteractionLease(const Bc2ReloadInteractionSample &s, const Bc2ReloadOwnerMap &map, const ReloadRoundLease &native)
{
    const auto translated = ToBc2ReloadInteractionLease(map, native, s.insertion.nowNs);
    return translated && s.native.owner == translated->owner && s.native.weapon == translated->weapon &&
           s.native.cycle == translated->cycle && s.native.observedNs == translated->observedNs &&
           s.native.deadlineNs == translated->deadlineNs &&
           s.native.allFiringCopiesHeld == translated->allFiringCopiesHeld;
}
} // namespace
std::optional<Bc2ReloadOwnerMap> BindBc2ReloadOwners(const interaction::HandInteractionOwner &p,
                                                     interaction::HandInteractionKey weapon,
                                                     const ReloadRoundLease &native, std::int64_t now) noexcept
{
    if (!Native(native, now))
        return {};
    const auto &o = native.identity.owner;
    const auto packed = (std::uint64_t(o.weak) << 32) | o.soldier;
    if (p.actor != packed || p.actorGeneration != o.actorGeneration || !p.equipGeneration || p.space != o.space ||
        weapon.id != o.weapon || weapon.generation != p.equipGeneration)
        return {};
    return Bc2ReloadOwnerMap{native.identity, p, weapon, native.cycle};
}
std::optional<Bc2ReloadNativeLease> ToBc2ReloadInteractionLease(const Bc2ReloadOwnerMap &map,
                                                                const ReloadRoundLease &native,
                                                                std::int64_t now) noexcept
{
    const auto current = BindBc2ReloadOwners(map.physical, map.weapon, native, now);
    if (!current || *current != map)
        return {};
    return Bc2ReloadNativeLease{map.physical,      map.weapon,        map.cycle,
                                native.observedNs, native.deadlineNs, native.allThreeHeld};
}
Bc2ReloadBridgeResult Bc2ReloadRequestBridge::Snapshot(Bc2ReloadBridgeReason reason) const noexcept
{
    Bc2ReloadBridgeResult r;
    r.phase = phase_;
    r.reason = reason;
    r.unresolvedNative = unresolved_;
    return r;
}
Bc2ReloadBridgeResult Bc2ReloadRequestBridge::Cancel(Bc2ReloadBridgeReason why) noexcept
{
    if (phase_ != Bc2ReloadBridgePhase::Pending)
        return Snapshot(why);
    phase_ = Bc2ReloadBridgePhase::Cancelled;
    unresolved_ = true;
    presentation_.reset();
    auto r = Snapshot(why);
    r.releasedReservation = reservation_;
    return r;
}
Bc2ReloadBridgeResult Bc2ReloadRequestBridge::Begin(const Bc2ReloadInteractionSample &sample,
                                                    const ReloadRoundLease &native,
                                                    const interaction::ReloadInsertionSeat &seat,
                                                    const interaction::ManualReloadRequest &request,
                                                    const Bc2ReloadTargets &target) noexcept
{
    using namespace interaction;
    const auto &s = sample.insertion;
    if (!enabled_)
        return Snapshot(Bc2ReloadBridgeReason::Disabled);
    if (phase_ == Bc2ReloadBridgePhase::Pending)
        return Snapshot(Bc2ReloadBridgeReason::Busy);
    if (!seat.id || seat.id <= lastSeat_ || !request.id || request.id <= lastRequest_)
        return Snapshot(Bc2ReloadBridgeReason::StaleSeat);
    if (!Native(native, s.nowNs))
        return Snapshot(Bc2ReloadBridgeReason::NativeUnavailable);
    const auto map = BindBc2ReloadOwners(s.identity.owner, s.identity.weapon, native, s.nowNs);
    if (!map)
        return Snapshot(Bc2ReloadBridgeReason::OwnerChanged);
    if (!Profile(sample) || !Input(s) || !s.itemTracked || !s.held || !s.eligible ||
        s.nowNs > std::numeric_limits<std::int64_t>::max() - 1500000000)
        return Snapshot(Bc2ReloadBridgeReason::TrackingLost);
    if (!native.allThreeHeld || native.loaded < 0 || native.reserve <= 0 || native.reserve > 1000000 ||
        native.capacity > 1000000 || native.capacity <= native.loaded || !InteractionLease(sample, *map, native))
        return Snapshot(Bc2ReloadBridgeReason::NativeUnavailable);
    if (request.owner != PhysicalOwner(*map) || request.operation != ReloadOperation::InsertRound)
        return Snapshot(Bc2ReloadBridgeReason::InvalidRequest);
    const auto profile = SpasReloadInsertionProfile();
    if (!reload_insertion_detail::Key(s.identity.item) || seat.operation != ReloadOperation::InsertRound ||
        seat.identity != s.identity || seat.inputSequence != s.sequence ||
        seat.profile != HandInteractionKey{profile.id, profile.revision} || s.geometrySequence != s.sequence)
        return Snapshot(Bc2ReloadBridgeReason::InvalidSeat);
    if (!Claim(s.itemClaim, s, InteractionHand::Left, HandClaimKind::AmmoObject, s.identity.item) ||
        !Claim(s.weaponClaim, s, InteractionHand::Right, HandClaimKind::GunHold, s.identity.weapon) ||
        seat.itemClaim != s.itemClaim.token || seat.weaponClaim != s.weaponClaim.token ||
        s.itemClaim.token.id == s.weaponClaim.token.id ||
        (s.itemClaim.token.prerequisiteClaim && s.itemClaim.token.prerequisiteClaim != s.weaponClaim.token.id))
        return Snapshot(Bc2ReloadBridgeReason::InvalidClaim);
    if (target.identity != seat.identity || target.shellClaim != seat.itemClaim ||
        target.weaponClaim != seat.weaponClaim || target.inputSequence != s.sequence ||
        target.nativeCycle != native.cycle || target.observedNs != s.observedNs ||
        !Fresh(target.observedNs, target.deadlineNs, s.nowNs, 100000000) ||
        target.deadlineNs >
            std::min({s.deadlineNs, native.deadlineNs, s.itemClaim.deadlineNs, s.weaponClaim.deadlineNs}) ||
        !reload_insertion_detail::Rigid(sample.rawLeftWristWorldMeters) ||
        !reload_insertion_detail::Rigid(sample.weaponWorldMeters) ||
        !reload_insertion_detail::Rigid(target.weaponFromShellCenterMeters) ||
        !reload_insertion_detail::Rigid(target.weaponFromLeftWristMeters))
        return Snapshot(Bc2ReloadBridgeReason::InvalidSeat);
    owners_ = *map;
    physicalRequest_ = request;
    nativeRequest_ = request;
    nativeRequest_.owner = NativeOwner(owners_);
    reservation_ = {seat.identity.item, seat.itemClaim, seat.id, request.id, native.cycle};
    gunClaim_ = seat.weaponClaim;
    anchor_ = target;
    presentation_ = target;
    lastSeat_ = seat.id;
    lastRequest_ = request.id;
    lastSequence_ = s.sequence;
    lastNativeSequence_ = native.sequence;
    initialNativeSequence_ = native.sequence;
    trackingEpoch_ = s.identity.trackingEpoch;
    lastNow_ = s.nowNs;
    lastObserved_ = s.observedNs;
    startedNs_ = s.nowNs;
    deadline_ = s.nowNs + 1500000000;
    phase_ = Bc2ReloadBridgePhase::Pending;
    unresolved_ = false;
    auto r = Snapshot();
    r.submit = Bc2ReloadNativeRequest{nativeRequest_, native, reservation_};
    r.presentation = presentation_;
    return r;
}
void Bc2ReloadRequestBridge::UpdatePresentation(const Bc2ReloadInteractionSample &sample,
                                                const ReloadRoundLease &native, bool fresh) noexcept
{
    using namespace interaction;
    const auto &s = sample.insertion;
    if (!s.itemTracked || !s.held || s.identity.item != reservation_.item || s.itemClaim.token != reservation_.claim ||
        !Claim(s.itemClaim, s, InteractionHand::Left, HandClaimKind::AmmoObject, reservation_.item))
    {
        presentation_.reset();
        return;
    }
    if (!fresh)
    {
        if (presentation_ && s.nowNs >= presentation_->deadlineNs)
            presentation_.reset();
        return;
    }
    using namespace reload_insertion_detail;
    if (s.geometrySequence != s.sequence || !Rigid(sample.rawLeftWristWorldMeters) || !Rigid(sample.weaponWorldMeters))
    {
        presentation_.reset();
        return;
    }
    const auto wrist = Multiply(sample.rawLeftWristWorldMeters, *InverseRigid(sample.weaponWorldMeters));
    const auto profile = SpasReloadInsertionProfile();
    // Only fresh raw wrist/placed-weapon geometry can renew the fixed seated
    // landmark. Pulling away drops the guided pose, not the issued insertion.
    if (Distance(wrist, anchor_.weaponFromLeftWristMeters) > profile.releaseDistanceMeters ||
        Angle(wrist, anchor_.weaponFromLeftWristMeters) > profile.releaseAngleRadians)
    {
        presentation_.reset();
        return;
    }
    auto pose = anchor_;
    pose.inputSequence = s.sequence;
    pose.observedNs = s.observedNs;
    pose.deadlineNs = std::min({s.deadlineNs, native.deadlineNs, s.itemClaim.deadlineNs, s.weaponClaim.deadlineNs});
    if (pose.deadlineNs - pose.observedNs > 100000000)
        pose.deadlineNs = pose.observedNs + 100000000;
    if (pose.deadlineNs <= s.nowNs)
    {
        presentation_.reset();
        return;
    }
    presentation_ = pose;
}
Bc2ReloadBridgeResult Bc2ReloadRequestBridge::ObservePendingInput(const Bc2ReloadInteractionSample &sample) noexcept
{
    using namespace interaction;
    const auto &s = sample.insertion;
    if (phase_ != Bc2ReloadBridgePhase::Pending)
        return Snapshot();
    if (s.nowNs < lastNow_ || s.sequence < lastSequence_ || s.observedNs < lastObserved_)
        return Cancel(Bc2ReloadBridgeReason::SequenceRollback);
    if (s.nowNs >= deadline_)
        return Cancel(Bc2ReloadBridgeReason::Expired);
    if (s.identity.owner != owners_.physical || s.identity.weapon != owners_.weapon ||
        s.identity.trackingEpoch != trackingEpoch_)
        return Cancel(Bc2ReloadBridgeReason::OwnerChanged);
    if (!Input(s) || !Profile(sample))
        return Cancel(Bc2ReloadBridgeReason::TrackingLost);
    if (s.weaponClaim.token != gunClaim_ ||
        !Claim(s.weaponClaim, s, InteractionHand::Right, HandClaimKind::GunHold, owners_.weapon))
        return Cancel(Bc2ReloadBridgeReason::InvalidClaim);
    lastSequence_ = s.sequence;
    lastNow_ = s.nowNs;
    lastObserved_ = s.observedNs;
    presentation_.reset();
    return Snapshot();
}
Bc2ReloadBridgeResult Bc2ReloadRequestBridge::Update(const Bc2ReloadInteractionSample &sample,
                                                     const ReloadRoundLease &native,
                                                     const std::optional<Bc2ReloadAckEvidence> &evidence) noexcept
{
    using namespace interaction;
    const auto &s = sample.insertion;
    if (phase_ != Bc2ReloadBridgePhase::Pending)
        return Snapshot();
    if (s.nowNs < lastNow_ || s.sequence < lastSequence_ || s.observedNs < lastObserved_ ||
        native.sequence < lastNativeSequence_)
        return Cancel(Bc2ReloadBridgeReason::SequenceRollback);
    if (s.nowNs >= deadline_)
        return Cancel(Bc2ReloadBridgeReason::Expired);
    if (!Native(native, s.nowNs))
        return Cancel(Bc2ReloadBridgeReason::NativeUnavailable);
    const auto map = BindBc2ReloadOwners(s.identity.owner, s.identity.weapon, native, s.nowNs);
    if (!map || *map != owners_ || s.identity.trackingEpoch != trackingEpoch_)
        return Cancel(Bc2ReloadBridgeReason::OwnerChanged);
    if (!Input(s) || !Profile(sample))
        return Cancel(Bc2ReloadBridgeReason::TrackingLost);
    if (!InteractionLease(sample, owners_, native))
        return Cancel(Bc2ReloadBridgeReason::NativeUnavailable);
    if (s.weaponClaim.token != gunClaim_ ||
        !Claim(s.weaponClaim, s, InteractionHand::Right, HandClaimKind::GunHold, owners_.weapon))
        return Cancel(Bc2ReloadBridgeReason::InvalidClaim);
    const bool fresh = s.sequence > lastSequence_;
    lastSequence_ = s.sequence;
    lastNativeSequence_ = native.sequence;
    lastNow_ = s.nowNs;
    lastObserved_ = s.observedNs;
    if (evidence)
    {
        const auto &e = *evidence;
        const auto &a = e.acknowledgement;
        if (e.verified && Fresh(e.observedNs, e.deadlineNs, s.nowNs) && e.observedNs >= startedNs_ &&
            a.identity == owners_.native && a.cycle == owners_.cycle && a.serverInvocation &&
            a.sampleSequence > initialNativeSequence_ && a.semantic.request == nativeRequest_.id &&
            a.semantic.owner == nativeRequest_.owner && a.semantic.operation == ReloadOperation::InsertRound &&
            a.semantic.status == ReloadAcknowledgement::Applied)
        {
            phase_ = Bc2ReloadBridgePhase::Complete;
            presentation_.reset();
            auto r = Snapshot();
            r.acknowledged = ManualReloadAck{physicalRequest_.id, physicalRequest_.owner, physicalRequest_.operation,
                                             ReloadAcknowledgement::Applied};
            r.consumed = reservation_;
            r.releasedReservation = reservation_;
            return r;
        }
    }
    UpdatePresentation(sample, native, fresh);
    auto r = Snapshot();
    r.presentation = presentation_;
    return r;
}
} // namespace fvr::bc2
