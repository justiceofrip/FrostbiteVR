#pragma once
#include "fvr/interaction/HandInteraction.h"
#include <cmath>

namespace fvr::interaction {
// Metres in one fixed tracking-space epoch. Its origin is arbitrary; the
// adapter validates current physical reach and measured equipment geometry.
// Head turns and actor locomotion must not manufacture a hand stroke.
struct MeleePoint {
    float x=0,y=0,z=0;
    bool operator==(const MeleePoint&)const=default;
};
struct MeleeStrokeConfig {
    // Comfort thresholds, not inferred blade dimensions or native hit range.
    float minimumSpeed=.65f,minimumTravel=.06f,restSpeed=.18f;
    float maximumSpeed=8.f,maximumStep=.30f,minimumAlignment=.65f;
    std::int64_t settleNs=120000000,rearmNs=180000000;
    std::int64_t maximumSampleGapNs=75000000,maximumStrokeNs=350000000;
};
struct MeleeStrokeSample {
    HandInteractionSample source{};
    HandClaimToken hold{};
    MeleePoint tip{},direction{0,0,1};
    // Exact native adapter readiness, including deployment, pending attack,
    // menu and scene gates. Gesture recognition cannot supply this authority.
    bool attackAvailable=false;
};
struct MeleeStrokeIntent {
    std::uint64_t serial=0;
    HandClaimToken hold{};
    std::uint64_t sourceSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    MeleePoint start{},end{},direction{};
};
enum class MeleeStrokePhase:unsigned {Unavailable,Settling,Ready,Travelling,Spent};
enum class MeleeStrokeDecision:unsigned {None,Invalid,Initial,Order,Duplicate,DuplicateChanged,
    SampleGap,StepLimit,SpeedLimit,Settling,Ready,Slow,Alignment,StrokeLimit,Direction,Travel,Intent,SerialLimit};
// Optional observation of the existing decision. No extra pose/native reads.
struct MeleeStrokeObservation {
    MeleeStrokeDecision decision=MeleeStrokeDecision::None;
    MeleeStrokePhase before=MeleeStrokePhase::Unavailable,after=MeleeStrokePhase::Unavailable;
    std::uint64_t previousSequence=0;
    std::int64_t previousObservedNs=0,elapsedNs=0,quietNs=0,strokeNs=0;
    float distance=0,speed=0,alignment=0,travel=0;
    bool metrics=false;
    unsigned invalid=0; // First existing validity predicate, in native evaluation order.
};
struct MeleeStrokeResult {
    MeleeStrokePhase phase=MeleeStrokePhase::Unavailable;
    std::optional<MeleeStrokeIntent> intent;
};

// Gesture recognition only. Reuses the actual shared GunHold; never claims a
// second hand or owns native attack completion. The adapter retains any emitted
// transaction independently and supplies attackAvailable=false until it settles.
class MeleeStroke {
public:
    explicit MeleeStroke(MeleeStrokeConfig config={}):config_(config){}
    MeleeStrokeResult Update(const MeleeStrokeSample& s,const HandInteraction& hands,MeleeStrokeObservation* observation=nullptr)noexcept {
        if(observation){*observation={};observation->before=phase_;observation->quietNs=quietNs_;observation->strokeNs=strokeNs_;
            if(previous_){observation->previousSequence=previous_->source.sequence;observation->previousObservedNs=previous_->source.observedNs;}}
        const auto finish=[&](MeleeStrokeDecision decision){
            if(observation){observation->decision=decision;observation->after=phase_;}
            return MeleeStrokeResult{phase_,{}};};
        if(!Valid(config_)){if(observation)observation->invalid=1;Invalidate();return finish(MeleeStrokeDecision::Invalid);}
        if(!Valid(s,hands,observation?&observation->invalid:nullptr)){Invalidate();return finish(MeleeStrokeDecision::Invalid);}
        const auto now=s.source.observedNs;
        if(!previous_||previous_->hold!=s.hold){Start(s);return finish(MeleeStrokeDecision::Initial);}
        if(s.source.sequence<previous_->source.sequence||now<previous_->source.observedNs||s.source.nowNs<lastNow_){
            Invalidate();return finish(MeleeStrokeDecision::Order);
        }
        lastNow_=s.source.nowNs;
        if(s.source.sequence==previous_->source.sequence){
            if(s.source.observedNs!=previous_->source.observedNs||s.source.deadlineNs!=previous_->source.deadlineNs||
               s.tip!=previous_->tip||s.direction!=previous_->direction||s.attackAvailable!=previous_->attackAvailable)
                Invalidate();
            return finish(phase_==MeleeStrokePhase::Unavailable?MeleeStrokeDecision::DuplicateChanged:MeleeStrokeDecision::Duplicate); // No progress.
        }
        const auto old=*previous_;previous_=s;
        const auto elapsed=now-old.source.observedNs;
        const auto step=Sub(s.tip,old.tip);const auto distance=Length(step);
        const auto seconds=static_cast<float>(elapsed)*1.e-9f;
        const auto speed=elapsed>0?distance/seconds:config_.maximumSpeed+1.f;
        if(observation){observation->metrics=true;observation->elapsedNs=elapsed;observation->distance=distance;observation->speed=speed;
            observation->alignment=distance>0?Dot(step,s.direction)/distance:0;}
        if(elapsed<=0||elapsed>config_.maximumSampleGapNs||distance>config_.maximumStep||
           !std::isfinite(speed)||speed>config_.maximumSpeed){Start(s);return finish(
            elapsed<=0||elapsed>config_.maximumSampleGapNs?MeleeStrokeDecision::SampleGap:
            distance>config_.maximumStep?MeleeStrokeDecision::StepLimit:MeleeStrokeDecision::SpeedLimit);}
        if(phase_==MeleeStrokePhase::Settling||phase_==MeleeStrokePhase::Spent){
            if(speed>config_.restSpeed)quietNs_=now;
            const auto dwell=phase_==MeleeStrokePhase::Spent?config_.rearmNs:config_.settleNs;
            if(now-quietNs_>=dwell)phase_=MeleeStrokePhase::Ready;
            return finish(phase_==MeleeStrokePhase::Ready?MeleeStrokeDecision::Ready:MeleeStrokeDecision::Settling);
        }
        if(phase_==MeleeStrokePhase::Ready){
            if(speed<config_.minimumSpeed)return finish(MeleeStrokeDecision::Slow);
            if(Dot(step,s.direction)<distance*config_.minimumAlignment){Start(s);return finish(MeleeStrokeDecision::Alignment);}
            start_=old.tip;axis_=s.direction;strokeNs_=old.source.observedNs;
            phase_=MeleeStrokePhase::Travelling;
        }
        if(phase_==MeleeStrokePhase::Travelling){
            // Minimum speed starts a deliberate stroke. Requiring it again at
            // every packet rejects an ordinary jab that decelerates near the
            // target, even when its forward travel is sufficient. Once started,
            // retain travel until reversal/rotation, interruption or timeout.
            if(now-strokeNs_>config_.maximumStrokeNs||
               Dot(axis_,s.direction)<config_.minimumAlignment||
               Dot(step,axis_)<distance*config_.minimumAlignment){
                const auto reason=now-strokeNs_>config_.maximumStrokeNs?MeleeStrokeDecision::StrokeLimit:
                    MeleeStrokeDecision::Direction;
                Start(s);return finish(reason);}
            const auto displacement=Sub(s.tip,start_);
            if(observation)observation->travel=Dot(displacement,axis_);
            if(Dot(displacement,axis_)>=config_.minimumTravel){
                if(serial_==UINT64_MAX){Invalidate();return finish(MeleeStrokeDecision::SerialLimit);}
                phase_=MeleeStrokePhase::Spent;quietNs_=now;
                (void)finish(MeleeStrokeDecision::Intent);
                return {phase_,MeleeStrokeIntent{++serial_,s.hold,s.source.sequence,now,s.source.deadlineNs,start_,s.tip,axis_}};
            }
        }
        return finish(MeleeStrokeDecision::Travel);
    }
    // Clears motion only. The adapter must not interpret this as cancellation
    // or completion of a native attack that has already been submitted.
    void Invalidate()noexcept{previous_.reset();phase_=MeleeStrokePhase::Unavailable;quietNs_=strokeNs_=0;}
    MeleeStrokePhase Phase()const noexcept{return phase_;}
    bool ReadyFor(const HandInteractionSample& source)const noexcept {
        return phase_==MeleeStrokePhase::Ready&&previous_&&previous_->source.owner==source.owner&&
            previous_->source.sequence==source.sequence&&previous_->source.observedNs==source.observedNs&&
            previous_->source.deadlineNs==source.deadlineNs&&source.nowNs>=source.observedNs&&source.deadlineNs>source.nowNs;
    }
private:
    static MeleePoint Sub(MeleePoint a,MeleePoint b)noexcept{return {a.x-b.x,a.y-b.y,a.z-b.z};}
    static float Dot(MeleePoint a,MeleePoint b)noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
    static float Length(MeleePoint a)noexcept{return std::hypot(a.x,a.y,a.z);}
    static bool Finite(MeleePoint a)noexcept{return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
    static bool Valid(const MeleeStrokeConfig& c)noexcept {
        return std::isfinite(c.minimumSpeed)&&std::isfinite(c.minimumTravel)&&std::isfinite(c.restSpeed)&&
            std::isfinite(c.maximumSpeed)&&std::isfinite(c.maximumStep)&&std::isfinite(c.minimumAlignment)&&
            c.restSpeed>=0&&c.minimumSpeed>c.restSpeed&&c.maximumSpeed>c.minimumSpeed&&c.maximumSpeed<=20&&
            c.minimumTravel>.005f&&c.minimumTravel<.5f&&c.maximumStep>0&&c.maximumStep<=.5f&&
            c.minimumAlignment>0&&c.minimumAlignment<=1&&c.settleNs>0&&c.settleNs<=2000000000&&
            c.rearmNs>0&&c.rearmNs<=2000000000&&c.maximumSampleGapNs>0&&c.maximumSampleGapNs<=150000000&&
            c.maximumStrokeNs>0&&c.maximumStrokeNs<=2000000000;
    }
    static bool Valid(const MeleeStrokeSample& s,const HandInteraction& hands,unsigned* invalid=nullptr)noexcept {
        const auto fail=[&](unsigned reason){if(invalid)*invalid=reason;return false;};
        if(!s.attackAvailable)return fail(2);
        if(s.hold.kind!=HandClaimKind::GunHold)return fail(3);
        if(s.hold.owner!=s.source.owner)return fail(4);
        if(!s.source.sequence)return fail(5);
        if(!s.source.focused)return fail(6);
        if(s.source.observedNs<0||s.source.nowNs<s.source.observedNs)return fail(7);
        if(s.source.deadlineNs<=s.source.nowNs||s.source.deadlineNs-s.source.observedNs>150000000)return fail(8);
        // An absolute radius would reject valid hands after room-scale walking
        // or a translated tracking origin. Delta/time/speed bounds below remain
        // independent of that origin; physical reach belongs to the adapter.
        if(!Finite(s.tip))return fail(9);
        if(!Finite(s.direction)||std::abs(Length(s.direction)-1.f)>.001f)return fail(10);
        if(!hands.HasRecordedEvidence(s.source,s.hold.hand))return fail(11);
        const auto current=hands.Current(s.hold.hand);
        if(!current)return fail(12);
        if(current->token!=s.hold)return fail(13);
        if(current->deadlineNs<=s.source.nowNs)return fail(14);
        if(current->inputSequence!=s.source.sequence)return fail(15);
        return true;
    }
    void Start(const MeleeStrokeSample& s)noexcept {
        previous_=s;lastNow_=s.source.nowNs;quietNs_=s.source.observedNs;strokeNs_=0;
        phase_=MeleeStrokePhase::Settling;
    }
    MeleeStrokeConfig config_{};std::optional<MeleeStrokeSample> previous_;
    MeleeStrokePhase phase_=MeleeStrokePhase::Unavailable;
    std::uint64_t serial_=0;std::int64_t quietNs_=0,strokeNs_=0,lastNow_=0;
    MeleePoint start_{},axis_{};
};
}
