#pragma once
#include "fvr/interaction/MeleeStroke.h"
#include <array>
#include <limits>
#include <ostream>
namespace fvr::interaction {
enum class KnifeRecognitionGate:unsigned {None,Unknown,Admission,Duplicate,SourceOrder,Phase,
    ReturnWanted,HeldProof,ClaimTransfer,ClaimLost,ClaimAcquire,ClaimForeign,ClaimRenew,Motion,NativeGate};
enum class KnifeMotionIdentityGate:unsigned {None,NativeView,ExistingHold,RightClaim,NativeHold,MotionDisabled,
    Mechanics,NativeCleanup,TrackedKnife,HoldInput,ActionInput,Suppression,Menu,Grip,Focus,LeftTracking,
    RightTracking,Release,SourceTime,SourceDeadline,Evidence,InputSequence,Space,TrackingBasis,NativeLifetime,
    BodyOperation,SelectedOwner};
enum class KnifeMotionCalibrationReason:unsigned {Continuous,Initial,IdentityMissing,IdentityChanged,
    MeasurementInvalid,MeasurementChanged,MeasurementMissing,DuplicateSuspended};
enum class KnifeMotionProjectionReason:unsigned {None,Phase,Calibration,Geometry,Ready,Suspended};
enum class KnifeMotionMechanicsGate:unsigned {None,OtherMechanics,NativeHealth,BodyOperation,KnifeSentinel,Readiness};
struct KnifeRecognitionCoordinator {
    std::uint64_t bodyOperation=0,nativeLifetime=0,menuEpoch=0,renderInput=0;
    std::int64_t renderObservedNs=0,renderDeadlineNs=0;
    unsigned trackingBasis=0,nativeInspection=0,readinessReason=0,beforePrepare=0;
    KnifeMotionIdentityGate identityGate=KnifeMotionIdentityGate::NativeView;
    KnifeMotionCalibrationReason calibration=KnifeMotionCalibrationReason::IdentityMissing;
    KnifeMotionProjectionReason projection=KnifeMotionProjectionReason::None;
    KnifeMotionMechanicsGate mechanicsGate=KnifeMotionMechanicsGate::None;
    bool measuredRender=false,retainedContact=false,calibrationContinuous=false,gatherMissing=false;
    bool trackingReady=false,attackReady=false,duplicateSuspended=false;
    bool operator==(const KnifeRecognitionCoordinator&)const=default;
};
struct KnifeRecognitionRecord {
    HandInteractionSample source{};HandInteractionKey knife{};
    MeleePoint tip{},direction{};MeleeStrokeObservation motion{};
    KnifeRecognitionGate gate=KnifeRecognitionGate::None;
    std::uint64_t claim=0;
    unsigned phaseBefore=0,phaseAfter=0,prerequisites=0,command=0;
    bool recognizerReady=false;
    KnifeRecognitionCoordinator coordinator{};
};
// Gather-owned, opt-in observation only. No allocation, locks, clock reads or
// native reads in Record. Report follows the existing stopped coordinator report.
class KnifeRecognitionDiagnostic {
public:
    static constexpr unsigned Capacity=128;
    void Enable()noexcept{permanent_=enabled_=everEnabled_=true;}
    // Called only by gather, using the existing explicit snapshot window.
    // Request threads never mutate this ledger or attach it to policy.
    void Window(bool requested)noexcept{enabled_=permanent_||requested;everEnabled_|=enabled_;sampled_|=requested;}
    bool Enabled()const noexcept{return enabled_;}
    void Coordinator(const KnifeRecognitionCoordinator& value)noexcept{coordinator_=value;}
    void Record(KnifeRecognitionRecord row)noexcept {
        if(!enabled_||!((row.phaseBefore>=2&&row.phaseBefore<=8)||(row.phaseAfter>=2&&row.phaseAfter<=8)))return;
        row.coordinator=coordinator_;
        if(row.gate==KnifeRecognitionGate::Duplicate){
            if(permanent_)return;
            // Unchanged gather callbacks on one original packet add no sample.
            // A coordinator reset/readiness loss on that packet is retained.
            if(previous_&&previous_->source.owner==row.source.owner&&previous_->source.sequence==row.source.sequence&&
               previous_->source.observedNs==row.source.observedNs&&previous_->source.deadlineNs==row.source.deadlineNs&&
               previous_->coordinator==row.coordinator&&previous_->motion.after==row.motion.after&&
               previous_->prerequisites==row.prerequisites)return;
        }
        const bool moving=row.motion.metrics&&row.motion.distance>0&&
            (row.motion.after==MeleeStrokePhase::Travelling||row.motion.decision==MeleeStrokeDecision::Intent);
        if(previous_&&Same(*previous_,row)&&
           (permanent_?!moving||row.source.sequence==previous_->source.sequence:row.source.sequence==previous_->source.sequence))return;
        previous_=row;if(seen_!=UINT64_MAX)++seen_;
        if(kept_<rows_.size())rows_[(first_+kept_++)%Capacity]=row;
        else {if(dropped_!=UINT64_MAX)++dropped_;
            if(!permanent_){rows_[first_]=row;first_=(first_+1)%Capacity;}}
    }
    void NativeGate(const HandInteractionSample& source,unsigned phase,HandInteractionKey knife={},std::uint64_t claim=0)noexcept {
        KnifeRecognitionRecord row;row.source=source;row.knife=knife;row.claim=claim;row.phaseBefore=row.phaseAfter=phase;
        row.gate=KnifeRecognitionGate::NativeGate;Record(row);
    }
    void Report(std::ostream& out)const {
        out<<"{\"enabled\":"<<everEnabled_<<",\"active\":"<<enabled_<<",\"snapshot_window\":"<<sampled_
           <<",\"observation_only\":true,\"action_authority\":false,\"capacity\":"<<Capacity<<",\"seen\":"<<seen_
           <<",\"kept\":"<<kept_<<",\"dropped\":"<<dropped_
           <<",\"identity_gate_names\":[\"none\",\"native_view\",\"existing_hold\",\"right_claim\",\"native_hold\",\"motion_disabled\",\"mechanics\",\"native_cleanup\",\"tracked_knife\",\"hold_input\",\"action_input\",\"suppression\",\"menu\",\"grip\",\"focus\",\"left_tracking\",\"right_tracking\",\"release\",\"source_time\",\"source_deadline\",\"evidence\",\"input_sequence\",\"space\",\"tracking_basis\",\"native_lifetime\",\"body_operation\",\"selected_owner\"]"
           <<",\"calibration_reason_names\":[\"continuous\",\"initial\",\"identity_missing\",\"identity_changed\",\"measurement_invalid\",\"measurement_changed\",\"measurement_missing\",\"duplicate_suspended\"]"
           <<",\"mechanics_gate_names\":[\"none\",\"other_mechanics\",\"native_health\",\"body_operation\",\"knife_sentinel\",\"readiness\"]"
           <<",\"projection_reason_names\":[\"none\",\"phase\",\"calibration\",\"geometry\",\"ready\",\"suspended\"],\"rows\":[";
        for(unsigned n=0;n<kept_;++n){if(n)out<<',';const auto& r=rows_[(first_+n)%Capacity];const auto& s=r.source;const auto& m=r.motion;
            out<<"{\"input\":"<<s.sequence<<",\"observed_ns\":"<<s.observedNs<<",\"deadline_ns\":"<<s.deadlineNs
               <<",\"now_ns\":"<<s.nowNs<<",\"actor\":"<<s.owner.actor<<",\"actor_generation\":"<<s.owner.actorGeneration
               <<",\"equip_generation\":"<<s.owner.equipGeneration<<",\"space\":"<<s.owner.space
               <<",\"knife\":"<<r.knife.id<<",\"knife_generation\":"<<r.knife.generation<<",\"claim\":"<<r.claim
               <<",\"gate\":"<<unsigned(r.gate)<<",\"prerequisites\":"<<r.prerequisites
               <<",\"phase_before\":"<<r.phaseBefore<<",\"phase_after\":"<<r.phaseAfter
               <<",\"motion_before\":"<<unsigned(m.before)<<",\"motion_after\":"<<unsigned(m.after)
               <<",\"decision\":"<<unsigned(m.decision)<<",\"invalid\":"<<m.invalid<<",\"recognizer_ready\":"<<r.recognizerReady<<",\"command\":"<<r.command
               <<",\"previous_input\":"<<m.previousSequence<<",\"previous_observed_ns\":"<<m.previousObservedNs
               <<",\"elapsed_ns\":"<<m.elapsedNs<<",\"quiet_ns\":"<<m.quietNs<<",\"stroke_ns\":"<<m.strokeNs
               <<",\"metrics\":"<<m.metrics<<",\"distance\":";Number(out,m.distance);out<<",\"speed\":";Number(out,m.speed);
            out<<",\"alignment\":";Number(out,m.alignment);out<<",\"travel\":";Number(out,m.travel);
            out<<",\"tip\":[";Number(out,r.tip.x);out<<',';Number(out,r.tip.y);out<<',';Number(out,r.tip.z);
            out<<"],\"direction\":[";Number(out,r.direction.x);out<<',';Number(out,r.direction.y);out<<',';Number(out,r.direction.z);
            const auto& c=r.coordinator;
            out<<"],\"coordinator\":{\"body_operation\":"<<c.bodyOperation<<",\"native_lifetime\":"<<c.nativeLifetime
               <<",\"menu_epoch\":"<<c.menuEpoch<<",\"tracking_basis\":"<<c.trackingBasis
               <<",\"native_inspection\":"<<c.nativeInspection<<",\"readiness_reason\":"<<c.readinessReason
               <<",\"before_prepare\":"<<c.beforePrepare<<",\"duplicate_suspended\":"<<c.duplicateSuspended
               <<",\"mechanics_gate\":"<<unsigned(c.mechanicsGate)
               <<",\"identity_gate\":"<<unsigned(c.identityGate)<<",\"calibration\":"<<unsigned(c.calibration)
               <<",\"projection\":"<<unsigned(c.projection)<<",\"calibration_continuous\":"<<c.calibrationContinuous
               <<",\"measured_render\":"<<c.measuredRender<<",\"retained_contact\":"<<c.retainedContact
               <<",\"render_input\":"<<c.renderInput<<",\"render_observed_ns\":"<<c.renderObservedNs
               <<",\"render_deadline_ns\":"<<c.renderDeadlineNs<<",\"gather_missing\":"<<c.gatherMissing
               <<",\"tracking_ready\":"<<c.trackingReady<<",\"attack_ready\":"<<c.attackReady<<"}}";
        }out<<"]}";
    }
    unsigned Kept()const noexcept{return kept_;}
    std::uint64_t Seen()const noexcept{return seen_;}
    std::uint64_t Dropped()const noexcept{return dropped_;}
private:
    static bool Same(const KnifeRecognitionRecord& a,const KnifeRecognitionRecord& b)noexcept {
        return a.source.owner==b.source.owner&&a.knife==b.knife&&a.claim==b.claim&&a.gate==b.gate&&
            a.phaseBefore==b.phaseBefore&&a.phaseAfter==b.phaseAfter&&a.prerequisites==b.prerequisites&&
            a.motion.before==b.motion.before&&a.motion.after==b.motion.after&&a.motion.decision==b.motion.decision&&a.motion.invalid==b.motion.invalid&&
            a.command==b.command&&a.recognizerReady==b.recognizerReady&&a.coordinator==b.coordinator;
    }
    static void Number(std::ostream& out,float value){if(std::isfinite(value))out<<value;else out<<"null";}
    std::array<KnifeRecognitionRecord,Capacity> rows_{};std::optional<KnifeRecognitionRecord> previous_;
    KnifeRecognitionCoordinator coordinator_{};
    std::uint64_t seen_=0,dropped_=0;unsigned kept_=0,first_=0;
    bool enabled_=false,permanent_=false,everEnabled_=false,sampled_=false;
};
static_assert(sizeof(KnifeRecognitionDiagnostic)<65536);
}
