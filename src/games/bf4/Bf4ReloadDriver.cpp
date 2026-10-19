#include "Bf4ReloadDriver.h"

namespace fvr::bf4 {

using interaction::ManualReloadAck;
using interaction::ManualReloadRequest;
using interaction::ReloadAcknowledgement;
using interaction::ReloadOperation;
using interaction::WeaponAction;

std::string_view ReloadGateName(ReloadGate g) noexcept {
    switch (g) {
    case ReloadGate::None: return "none";
    case ReloadGate::MagazineOut: return "magazine out";
    case ReloadGate::BreechOpen: return "breech open";
    case ReloadGate::NativeReload: return "reloading";
    case ReloadGate::OwedCycle: return "cycle owed";
    }
    return "?";
}

Bf4ReloadDriver::Bf4ReloadDriver(const Bf4ReloadClassification& c)
    : descriptor_(c.descriptor), archetype_(c.archetype) {
    supported_ = archetype_ == Bf4ReloadArchetype::Magazine || archetype_ == Bf4ReloadArchetype::Single ||
                 archetype_ == Bf4ReloadArchetype::Belt || archetype_ == Bf4ReloadArchetype::Cylinder;
}

ManualReloadAck Bf4ReloadDriver::Ack(const ManualReloadRequest& r, ReloadAcknowledgement status) {
    ManualReloadAck a;
    a.request = r.id;
    a.owner = r.owner;
    a.operation = r.operation;
    a.status = status;
    return a;
}

void Bf4ReloadDriver::EndWeapon() {
    // Lifetime transition for this weapon: its gates and owed step end with it. A native
    // reload already under way is the game's business; nothing is undone.
    magazineOut_ = breechOpen_ = roundStaged_ = false;
    owed_ = WeaponAction::Automatic;
    pending_.reset();
    haveLast_ = false;
}

void Bf4ReloadDriver::StartNative(const ManualReloadRequest& r, const NativeFiringSample& n, std::int64_t now) {
    pending_ = Pending{r, now, n.loaded, n.reserve, n.state != kNativeReadyState};
}

void Bf4ReloadDriver::Handle(const ManualReloadRequest& r, const NativeFiringSample& n, std::int64_t now,
                             ReloadDriverOutput& out) {
    const bool magazineFeed = archetype_ == Bf4ReloadArchetype::Magazine;
    const bool nothingToLoad = n.reserve <= 0 || (n.capacity > 0 && n.loaded >= n.capacity);
    switch (r.operation) {
    case ReloadOperation::UnseatMagazine:
        if (!magazineFeed) break;
        magazineOut_ = true; // VR-only step: the game keeps its rounds until a seat reloads it
        out.ack = Ack(r, ReloadAcknowledgement::Applied);
        return;
    case ReloadOperation::SeatMagazine:
        if (!magazineFeed || !magazineOut_) break;
        if (nothingToLoad) { // the same rounds go back in: no reload, no ammunition moves
            magazineOut_ = false;
            out.ack = Ack(r, ReloadAcknowledgement::Applied);
        } else {
            StartNative(r, n, now);
        }
        return;
    case ReloadOperation::OpenBreech:
        if (magazineFeed) break;
        breechOpen_ = true;
        out.ack = Ack(r, ReloadAcknowledgement::Applied);
        return;
    case ReloadOperation::InsertRound:
        if (magazineFeed || !breechOpen_) break;
        if (n.reserve <= 0) { out.ack = Ack(r, ReloadAcknowledgement::Rejected); return; } // nothing to load
        roundStaged_ = true;
        out.ack = Ack(r, ReloadAcknowledgement::Applied);
        return;
    case ReloadOperation::CloseBreech:
        if (magazineFeed || !breechOpen_) break;
        if (!roundStaged_ || nothingToLoad) {
            breechOpen_ = roundStaged_ = false;
            out.ack = Ack(r, ReloadAcknowledgement::Applied);
        } else {
            StartNative(r, n, now);
        }
        return;
    default:
        break;
    }
    out.ack = Ack(r, ReloadAcknowledgement::Rejected); // not an operation of this mechanism, or out of order
}

ReloadDriverOutput Bf4ReloadDriver::Update(const ReloadDriverInput& in) {
    ReloadDriverOutput out;
    if (!supported_) return out;
    const NativeFiringSample& n = in.native;
    if (!n.valid || n.identity != identity_) {
        if (identity_ != 0) EndWeapon();
        identity_ = n.valid ? n.identity : 0;
        if (!n.valid) return out; // no weapon observed: nothing to gate
    }
    // A round left the gun outside a reload: the shot owes a stroke on a pump/bolt gun.
    if (haveLast_ && !pending_ && n.loaded < lastLoaded_ && descriptor_.afterShot != WeaponAction::Automatic)
        owed_ = descriptor_.afterShot;
    if (in.transactionCancelled) pending_.reset(); // physical gates stay as the hands left them
    if (in.request && !pending_) Handle(*in.request, n, in.nowNs, out);
    else if (in.request) out.ack = Ack(*in.request, ReloadAcknowledgement::Rejected); // one native reload at a time

    if (pending_) {
        auto& p = *pending_;
        if (in.nowNs - p.pressedAt < kPressNs) out.pressReload = true;
        p.started = p.started || n.state != kNativeReadyState;
        if (n.reserve < p.reserve || n.loaded > p.loaded) { // the game moved ammunition
            out.ack = Ack(p.request, ReloadAcknowledgement::Applied);
            if (p.loaded == 0 && descriptor_.afterEmptyFeed != WeaponAction::Automatic) owed_ = descriptor_.afterEmptyFeed;
            magazineOut_ = breechOpen_ = roundStaged_ = false;
            pending_.reset();
        } else if (!p.started && in.nowNs - p.pressedAt > kStartNs) {
            // The game declined (sprinting, full, busy): the gun keeps the rounds it had.
            out.ack = Ack(p.request, ReloadAcknowledgement::Rejected);
            magazineOut_ = breechOpen_ = roundStaged_ = false;
            pending_.reset();
        }
    }
    if (in.cycleGestureCompleted && !pending_) owed_ = WeaponAction::Automatic;
    lastLoaded_ = n.loaded;
    haveLast_ = true;

    out.gate = pending_ ? ReloadGate::NativeReload
             : magazineOut_ ? ReloadGate::MagazineOut
             : breechOpen_ ? ReloadGate::BreechOpen
             : owed_ != WeaponAction::Automatic ? ReloadGate::OwedCycle
                                                : ReloadGate::None;
    out.blockFire = out.gate != ReloadGate::None;
    out.owedCycle = owed_;
    return out;
}

} // namespace fvr::bf4
