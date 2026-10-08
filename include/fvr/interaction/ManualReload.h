#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace fvr::interaction {
// Semantic physical operations, not native input actions or ammo mutations.
enum class ReloadOperation : std::uint8_t {
    None,UnseatMagazine,SeatMagazine,InsertRound,CycleAction,OpenBreech,CloseBreech
};
struct ManualReloadStep {ReloadOperation operation=ReloadOperation::None;std::uint8_t repeats=1;};
struct ManualReloadConfig {
    static constexpr std::size_t MaxSteps=8;
    static constexpr unsigned MaxOperations=64;
    std::array<ManualReloadStep,MaxSteps> steps{};
    std::uint8_t stepCount=0;
    std::int64_t maxSampleGapNs=0,ackTimeoutNs=0,transactionTimeoutNs=0;
};
// Opaque adapter identity. Generations must change on respawn, equip/mode change
// or reference-space reset; no native pointer interpretation exists here.
struct ManualReloadOwner {
    std::uint64_t actor=0,actorGeneration=0,weapon=0,equipGeneration=0,space=0;
    bool operator==(const ManualReloadOwner&)const=default;
};
struct ManualReloadGesture {std::uint64_t id=0;ReloadOperation operation=ReloadOperation::None;};
enum class ReloadAcknowledgement : std::uint8_t {None,Applied,Rejected};
struct ManualReloadAck {
    std::uint64_t request=0;ManualReloadOwner owner{};
    ReloadOperation operation=ReloadOperation::None;
    ReloadAcknowledgement status=ReloadAcknowledgement::None;
};
struct ManualReloadSample {
    ManualReloadOwner owner{};
    std::uint64_t sequence=0;std::int64_t nowNs=0;
    // Verified means the entire immutable plan has native bindings, authoritative
    // completion observers, owned physical resources and a current profile.
    bool focused=false,tracked=false,bindingsVerified=false,cancel=false;
    // Neutral belongs to the physical gesture recognizer, not the gun hand.
    // This must be a fresh coherent input packet, never synthetic neutral while
    // waiting for geometry. A held magazine can therefore remain in the hand.
    bool neutral=false;
    ManualReloadGesture gesture{};ManualReloadAck acknowledgement{};
};
struct ManualReloadRequest {
    std::uint64_t id=0;ManualReloadOwner owner{};ReloadOperation operation=ReloadOperation::None;
    std::uint8_t step=0,repetition=0;
};
enum class ManualReloadPhase : std::uint8_t {Idle,Active,AwaitingAcknowledgement,Complete};
enum class ManualReloadCancel : std::uint8_t {
    None,InvalidConfig,InvalidSample,UnverifiedBinding,TrackingLost,IdentityChanged,
    ClockDiscontinuity,SequenceRollback,GestureRollback,StaleTracking,Explicit,
    UnexpectedOperation,NativeRejected,AcknowledgementTimeout,TransactionTimeout,TokenExhausted
};
struct ManualReloadResult {
    ManualReloadPhase phase=ManualReloadPhase::Idle;
    std::optional<ManualReloadRequest> request{};
    std::optional<ReloadOperation> expected{};
    std::uint64_t cancelledRequest=0;
    unsigned acceptedOperations=0;
    bool cancelled=false,completed=false,acknowledged=false;
    ManualReloadCancel reason=ManualReloadCancel::None;
};
// Pure command/ack coordinator, deliberately not enabled by any BC2 adapter.
// No geometry recognizer, ammo arithmetic, inventory grant, native action,
// animation edits or compensating rollback commands are implemented here.
// Requests are one-shot; each requires an exact authoritative acknowledgement.
class ManualReload {
public:
    explicit ManualReload(ManualReloadConfig config)noexcept:config_(config){}
    bool ValidConfig()const noexcept;
    ManualReloadResult Update(const ManualReloadSample& sample)noexcept;
    // Cancels local intent only. The adapter must reconcile already applied
    // native effects; resetting never sends an inverse operation or reuses IDs.
    ManualReloadResult Reset()noexcept;
    // Idle-only profile data change; preserve the global request watermark.
    bool Reconfigure(ManualReloadConfig config)noexcept;
private:
    static bool ValidOperation(ReloadOperation operation)noexcept {
        return operation>=ReloadOperation::UnseatMagazine&&operation<=ReloadOperation::CloseBreech;
    }
    static bool ValidOwner(const ManualReloadOwner& o)noexcept {
        return o.actor&&o.actorGeneration&&o.weapon&&o.equipGeneration&&o.space;
    }
    ManualReloadResult Snapshot()const noexcept;
    ManualReloadResult Cancel(ManualReloadCancel reason)noexcept;
    void Adopt(const ManualReloadSample& sample)noexcept;
    ManualReloadConfig config_{};ManualReloadOwner owner_{};
    ManualReloadPhase phase_=ManualReloadPhase::Idle;
    std::optional<ManualReloadRequest> pending_{};
    std::uint64_t sequence_=0,lastGesture_=0,nextRequest_=0;
    std::int64_t lastCall_=0,lastFresh_=0,startedAt_=0,requestedAt_=0;
    unsigned accepted_=0;std::uint8_t step_=0,repetition_=0;
    bool initialized_=false,armed_=false;
};
inline bool ManualReload::ValidConfig()const noexcept {
    if(!config_.stepCount||config_.stepCount>config_.steps.size()||config_.maxSampleGapNs<=0||
       config_.ackTimeoutNs<=0||config_.transactionTimeoutNs<config_.ackTimeoutNs)return false;
    unsigned total=0;
    for(std::size_t i=0;i<config_.stepCount;++i){
        const auto& s=config_.steps[i];
        if(!ValidOperation(s.operation)||!s.repeats||s.repeats>32)return false;
        total+=s.repeats;
    }
    return total<=ManualReloadConfig::MaxOperations;
}
inline ManualReloadResult ManualReload::Snapshot()const noexcept {
    ManualReloadResult result{};result.phase=phase_;result.acceptedOperations=accepted_;
    if(ValidConfig()&&phase_!=ManualReloadPhase::Complete)result.expected=config_.steps[step_].operation;
    return result;
}
inline ManualReloadResult ManualReload::Cancel(ManualReloadCancel reason)noexcept {
    const bool active=phase_==ManualReloadPhase::Active||phase_==ManualReloadPhase::AwaitingAcknowledgement;
    const auto cancelled=pending_?pending_->id:0;const auto accepted=accepted_;
    phase_=ManualReloadPhase::Idle;pending_.reset();armed_=false;
    step_=repetition_=0;accepted_=0;startedAt_=requestedAt_=0;
    auto result=Snapshot();result.cancelled=active;result.cancelledRequest=cancelled;
    result.acceptedOperations=accepted;result.reason=reason;return result;
}
inline void ManualReload::Adopt(const ManualReloadSample& sample)noexcept {
    owner_=sample.owner;sequence_=sample.sequence;lastGesture_=sample.gesture.id;
    lastCall_=lastFresh_=sample.nowNs;initialized_=true;
}
inline ManualReloadResult ManualReload::Reset()noexcept {
    auto result=Cancel(ManualReloadCancel::Explicit);initialized_=false;
    return result;
}
inline bool ManualReload::Reconfigure(ManualReloadConfig config)noexcept {
    if(phase_!=ManualReloadPhase::Idle||pending_||!ManualReload(config).ValidConfig())return false;
    Reset();config_=config;return true;
}
inline ManualReloadResult ManualReload::Update(const ManualReloadSample& sample)noexcept {
    if(!ValidConfig())return Cancel(ManualReloadCancel::InvalidConfig);
    if(!ValidOwner(sample.owner)||!sample.sequence||sample.nowNs<=0)
        return Cancel(ManualReloadCancel::InvalidSample);
    if(initialized_&&sample.owner!=owner_){
        auto result=Cancel(ManualReloadCancel::IdentityChanged);Adopt(sample);return result;
    }
    bool fresh=true;
    if(initialized_){
        if(sample.nowNs<lastCall_)return Cancel(ManualReloadCancel::ClockDiscontinuity);
        lastCall_=sample.nowNs;
        if(sample.sequence<sequence_)return Cancel(ManualReloadCancel::SequenceRollback);
        fresh=sample.sequence>sequence_;
        // Also reject a new packet after a gap: it cannot continue old intent.
        if(sample.nowNs-lastFresh_>config_.maxSampleGapNs){
            auto result=Cancel(ManualReloadCancel::StaleTracking);Adopt(sample);return result;
        }
        if(fresh){sequence_=sample.sequence;lastFresh_=sample.nowNs;}
    }else Adopt(sample);
    if(!sample.focused||!sample.tracked)return Cancel(ManualReloadCancel::TrackingLost);
    if(!sample.bindingsVerified)return Cancel(ManualReloadCancel::UnverifiedBinding);
    if(sample.cancel)return Cancel(ManualReloadCancel::Explicit);
    if((sample.gesture.id==0)!=(sample.gesture.operation==ReloadOperation::None)||
       (sample.gesture.id&&(!ValidOperation(sample.gesture.operation)||sample.neutral)))
        return Cancel(ManualReloadCancel::InvalidSample);
    bool event=false;
    if(fresh&&sample.gesture.id){
        if(sample.gesture.id<lastGesture_)return Cancel(ManualReloadCancel::GestureRollback);
        event=sample.gesture.id>lastGesture_;lastGesture_=sample.gesture.id;
    }
    if(phase_==ManualReloadPhase::Active||phase_==ManualReloadPhase::AwaitingAcknowledgement){
        if(sample.nowNs-startedAt_>config_.transactionTimeoutNs)return Cancel(ManualReloadCancel::TransactionTimeout);
    }
    if(pending_){
        if(sample.nowNs-requestedAt_>config_.ackTimeoutNs)return Cancel(ManualReloadCancel::AcknowledgementTimeout);
        const auto& ack=sample.acknowledgement;
        if(ack.request==pending_->id&&ack.owner==pending_->owner&&ack.operation==pending_->operation){
            if(ack.status==ReloadAcknowledgement::Rejected)return Cancel(ManualReloadCancel::NativeRejected);
            if(ack.status==ReloadAcknowledgement::Applied){
                pending_.reset();++accepted_;++repetition_;
                if(repetition_==config_.steps[step_].repeats){++step_;repetition_=0;}
                phase_=step_==config_.stepCount?ManualReloadPhase::Complete:ManualReloadPhase::Active;
                armed_=false;
                auto result=Snapshot();result.acknowledged=true;result.completed=phase_==ManualReloadPhase::Complete;
                return result; // This packet cannot also arm or request another step.
            }
        }
        return Snapshot(); // Consume new gesture IDs while pending; never queue them.
    }
    if(!fresh)return Snapshot();
    if(sample.neutral){
        if(phase_==ManualReloadPhase::Complete){phase_=ManualReloadPhase::Idle;step_=repetition_=0;accepted_=0;}
        armed_=true;return Snapshot();
    }
    if(!event||!armed_||phase_==ManualReloadPhase::Complete)return Snapshot();
    armed_=false;
    if(sample.gesture.operation!=config_.steps[step_].operation)return Cancel(ManualReloadCancel::UnexpectedOperation);
    if(nextRequest_==std::numeric_limits<std::uint64_t>::max())return Cancel(ManualReloadCancel::TokenExhausted);
    if(phase_==ManualReloadPhase::Idle){phase_=ManualReloadPhase::Active;startedAt_=sample.nowNs;}
    pending_=ManualReloadRequest{++nextRequest_,owner_,sample.gesture.operation,step_,repetition_};
    requestedAt_=sample.nowNs;phase_=ManualReloadPhase::AwaitingAcknowledgement;
    auto result=Snapshot();result.request=pending_;return result;
}
}
