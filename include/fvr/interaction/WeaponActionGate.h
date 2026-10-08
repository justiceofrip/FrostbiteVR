#pragma once
#include "fvr/interaction/WeaponMechanism.h"

namespace fvr::interaction {
// Semantic evidence must be supplied by an admitted engine adapter. In
// particular, a loaded-round count does not establish chamber knowledge.
// One persistent instance belongs to one physical item lifetime. This policy
// never changes ammunition and contributes a veto to the normal firing gate.
struct WeaponActionEvidence {
    HandInteractionOwner owner{};
    HandInteractionKey item{};
    std::uint64_t descriptor=0,revision=0,sequence=0,shot=0,feed=0;
    std::int64_t observedNs=0,deadlineNs=0;
    ChamberKnowledge chamber=ChamberKnowledge::Unknown;
    // Meaningful only for a new independently acknowledged feed event.
    ChamberKnowledge chamberBeforeFeed=ChamberKnowledge::Unknown;
    bool nativeReady=false,actionClosed=false;
    bool operator==(const WeaponActionEvidence&)const=default;
};
enum class WeaponActionGatePhase : unsigned { Unbound, Ready, Required, Cycling, Submitted, Faulted };
// NativeAfterShot only tracks the owed manual action. Exact native ready/closed
// evidence hands firing back to the engine's existing rules without asserting
// a separate chamber, changing ammunition, or adding empty-feed charging.
enum class WeaponActionReadiness : unsigned { SemanticChamber, NativeAfterShot };
struct WeaponActionRetirement {
    WeaponCycleLease cycle{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // An exact native cancellation/retirement receipt, never a lost hand claim.
    bool retired=false,unchangedAmmunition=false;
};
class WeaponActionGate {
public:
    WeaponActionGate()=default;
    WeaponActionGate(const WeaponActionGate&)=delete;
    WeaponActionGate& operator=(const WeaponActionGate&)=delete;

    // Initial enrollment needs an independently established idle baseline.
    // Enrolling an already owed cycle needs adapter reconciliation first.
    bool Bind(const WeaponMechanismDescriptor& descriptor,const WeaponActionEvidence& evidence,
        std::int64_t now,WeaponActionReadiness readiness=WeaponActionReadiness::SemanticChamber)noexcept {
        if(phase_!=WeaponActionGatePhase::Unbound||!ValidWeaponMechanismDescriptor(descriptor)||
           readiness>WeaponActionReadiness::NativeAfterShot||
           (readiness==WeaponActionReadiness::NativeAfterShot&&descriptor.afterShot!=WeaponAction::Pump&&descriptor.afterShot!=WeaponAction::Bolt)||
           !Valid(evidence,now)||evidence.descriptor!=descriptor.id||evidence.revision!=descriptor.revision||
           (readiness==WeaponActionReadiness::SemanticChamber&&evidence.chamber==ChamberKnowledge::Unknown)||
           !evidence.nativeReady||!evidence.actionClosed)return false;
        descriptor_=descriptor;readiness_=readiness;latest_=evidence;evidenceValid_=true;phase_=WeaponActionGatePhase::Ready;return true;
    }

    // Counters identify actual adapter-issued events, not trigger edges. Gaps
    // or simultaneous shot/feed changes cannot be reconciled from a snapshot.
    bool Observe(const WeaponActionEvidence& evidence,std::int64_t now)noexcept {
        if(phase_==WeaponActionGatePhase::Unbound||phase_==WeaponActionGatePhase::Faulted||
           !Valid(evidence,now)||!SameItem(evidence)||evidence.sequence<latest_.sequence||
           evidence.observedNs<latest_.observedNs){evidenceValid_=false;return false;}
        if(evidence.sequence==latest_.sequence){
            if(evidence!=latest_)evidenceValid_=false;
            return evidenceValid_;
        }
        if(evidence.shot<latest_.shot||evidence.feed<latest_.feed){evidenceValid_=false;return false;}
        const bool shot=evidence.shot!=latest_.shot,feed=evidence.feed!=latest_.feed;
        if((shot&&feed)||(shot&&evidence.shot-latest_.shot!=1)||(feed&&evidence.feed-latest_.feed!=1)||
           ((shot||feed)&&phase_!=WeaponActionGatePhase::Ready)){
            phase_=WeaponActionGatePhase::Faulted;return false;
        }
        if(shot||feed){
            if(readiness_==WeaponActionReadiness::NativeAfterShot){
                requirement_={};
                if(shot){
                    requirement_.purpose=WeaponCyclePurpose::AfterShot;requirement_.action=descriptor_.afterShot;
                    requirement_.gestureFamily=descriptor_.afterShot==WeaponAction::Bolt?WeaponCycleFamily::Bolt:WeaponCycleFamily::Pump;
                }
            }else{
                const auto intent=shot?WeaponMechanismIntent::AfterShot:
                    evidence.chamberBeforeFeed==ChamberKnowledge::Occupied?WeaponMechanismIntent::TacticalFeed:WeaponMechanismIntent::EmptyFeed;
                const auto chamber=shot?evidence.chamber:evidence.chamberBeforeFeed;
                const auto selected=SelectWeaponMechanismPlan(descriptor_,intent,chamber);
                if(!selected.plan){phase_=WeaponActionGatePhase::Faulted;return false;}
                requirement_=*selected.plan;
            }
            phase_=requirement_.purpose==WeaponCyclePurpose::None?WeaponActionGatePhase::Ready:WeaponActionGatePhase::Required;
            eventObserved_=evidence.observedNs;
        }
        // Owner equip/space may change without resetting the physical item or
        // its debt. An old cycle still requires exact completion or retirement.
        latest_=evidence;evidenceValid_=true;return true;
    }

    bool BeginCycle(const WeaponCycleProfile& profile,const WeaponCycleLease& lease,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        if(phase_!=WeaponActionGatePhase::Required||!requirement_.gestureFamily||
           !Profile(profile)||profile.family!=*requirement_.gestureFamily||!Lease(lease,now)||
           !Fresh(now)||lease.owner!=latest_.owner||lease.item!=latest_.item||lease.cycle<=lastCycle_||
           lease.sequence<latest_.sequence||lease.sequence<=retiredSequence_||lease.observedNs<latest_.observedNs||
           lease.observedNs<eventObserved_||lease.observedNs<retiredObserved_||
           lease.shot!=(requirement_.purpose==WeaponCyclePurpose::AfterShot?latest_.shot:0)||
           now>INT64_MAX-profile.maximumCycleNs)return false;
        cycle_=lease;lastCycle_=lease.cycle;cycleDeadline_=now+profile.maximumCycleNs;
        phase_=WeaponActionGatePhase::Cycling;return true;
    }

    // Called once for the actual recognizer's completed physical sequence.
    // The request is immutable; newer input must not extend its admission.
    bool Submit(const WeaponCycleRelease& release,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        if(phase_!=WeaponActionGatePhase::Cycling||!cycle_||now>=cycleDeadline_||
           !Fresh(now)||latest_.owner!=cycle_->owner||!Same(release.cycle,*cycle_)||
           !Lease(release.cycle,now)||release.cycle.sequence<cycle_->sequence||
           release.cycle.observedNs<cycle_->observedNs||(release.cycle.sequence==cycle_->sequence&&release.cycle!=*cycle_)||!release.request||!release.inputSequence||
           release.observedNs<release.cycle.observedNs||!Window(release.observedNs,release.deadlineNs,now)||
           release.deadlineNs>release.cycle.deadlineNs)return false;
        release_=release;phase_=WeaponActionGatePhase::Submitted;return true;
    }

    // The semantic mode also requires chamber knowledge. NativeAfterShot uses
    // exact native readiness as a return to ordinary engine firing rules.
    bool Complete(const WeaponCycleReady& ready,const WeaponActionEvidence& evidence,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        if(phase_!=WeaponActionGatePhase::Submitted||!release_||now>=cycleDeadline_||
           ready.release!=*release_||!ready.nativeReady||!ready.unchangedAmmunition||
           ready.sequence<=release_->cycle.sequence||ready.observedNs<release_->observedNs||
           !Window(ready.observedNs,ready.deadlineNs,now)||!Valid(evidence,now)||!SameItem(evidence)||
           evidence.owner!=release_->cycle.owner||evidence.sequence!=ready.sequence||
           evidence.observedNs!=ready.observedNs||evidence.deadlineNs!=ready.deadlineNs||
           evidence.sequence<latest_.sequence||evidence.observedNs<latest_.observedNs||
           evidence.shot!=latest_.shot||evidence.feed!=latest_.feed||
           (readiness_==WeaponActionReadiness::SemanticChamber&&evidence.chamber==ChamberKnowledge::Unknown)||
           !evidence.nativeReady||!evidence.actionClosed)return false;
        latest_=evidence;evidenceValid_=true;phase_=WeaponActionGatePhase::Ready;requirement_={};cycle_.reset();release_.reset();return true;
    }

    // Tracking/focus loss never calls this. Only exact adapter-observed native
    // retirement permits a new held cycle, and the original action debt remains.
    bool Retire(const WeaponActionRetirement& receipt,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        if((phase_!=WeaponActionGatePhase::Cycling&&phase_!=WeaponActionGatePhase::Submitted)||!cycle_||
           receipt.cycle!=*cycle_||!receipt.retired||!receipt.unchangedAmmunition||
           receipt.sequence<=cycle_->sequence||receipt.observedNs<cycle_->observedNs||
           (release_&&(receipt.sequence<=release_->cycle.sequence||receipt.observedNs<release_->observedNs))||
           !Window(receipt.observedNs,receipt.deadlineNs,now))return false;
        retiredSequence_=receipt.sequence;retiredObserved_=receipt.observedNs;
        phase_=WeaponActionGatePhase::Required;cycle_.reset();release_.reset();return true;
    }

    bool BlocksFire(const HandInteractionSample& source)const noexcept {
        using namespace weapon_cycle_detail;
        return phase_!=WeaponActionGatePhase::Ready||source.owner!=latest_.owner||!source.sequence||
            !source.focused||!source.tracked[0]||!source.tracked[1]||
            !Window(source.observedNs,source.deadlineNs,source.nowNs)||!Fresh(source.nowNs)||
            (readiness_==WeaponActionReadiness::SemanticChamber&&latest_.chamber!=ChamberKnowledge::Occupied)||
            !latest_.nativeReady||!latest_.actionClosed;
    }
    WeaponActionGatePhase Phase()const noexcept{return phase_;}
    const WeaponMechanismPlan& Requirement()const noexcept{return requirement_;}
private:
    static bool Valid(const WeaponActionEvidence& e,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        return Owner(e.owner)&&Key(e.item)&&e.descriptor&&e.revision&&e.sequence&&
            e.chamber<=ChamberKnowledge::Occupied&&e.chamberBeforeFeed<=ChamberKnowledge::Occupied&&Window(e.observedNs,e.deadlineNs,now);
    }
    bool SameItem(const WeaponActionEvidence& e)const noexcept {
        return e.owner.actor==latest_.owner.actor&&e.owner.actorGeneration==latest_.owner.actorGeneration&&
            e.item==latest_.item&&e.descriptor==descriptor_.id&&e.revision==descriptor_.revision;
    }
    bool Fresh(std::int64_t now)const noexcept{return evidenceValid_&&weapon_cycle_detail::Window(latest_.observedNs,latest_.deadlineNs,now);}
    WeaponMechanismDescriptor descriptor_{};
    WeaponActionEvidence latest_{};
    WeaponMechanismPlan requirement_{};
    WeaponActionGatePhase phase_=WeaponActionGatePhase::Unbound;
    WeaponActionReadiness readiness_=WeaponActionReadiness::SemanticChamber;
    std::optional<WeaponCycleLease> cycle_;
    std::optional<WeaponCycleRelease> release_;
    std::uint64_t lastCycle_=0,retiredSequence_=0;
    std::int64_t eventObserved_=0,cycleDeadline_=0,retiredObserved_=0;
    bool evidenceValid_=false;
};
}
