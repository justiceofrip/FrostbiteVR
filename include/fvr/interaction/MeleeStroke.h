#pragma once
#include "fvr/interaction/HandInteraction.h"
#include <cmath>

namespace fvr::interaction {
// Body-relative metres in a fixed reference-space epoch. The adapter derives
// the tip and blade direction from measured equipment geometry. Head turns and
// actor locomotion must not manufacture a hand stroke.
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
    MeleeStrokeResult Update(const MeleeStrokeSample& s,const HandInteraction& hands)noexcept {
        const auto finish=[&](){return MeleeStrokeResult{phase_,{}};};
        if(!Valid(config_)||!Valid(s,hands)){Invalidate();return finish();}
        const auto now=s.source.observedNs;
        if(!previous_||previous_->hold!=s.hold){Start(s);return finish();}
        if(s.source.sequence<previous_->source.sequence||now<previous_->source.observedNs||s.source.nowNs<lastNow_){
            Invalidate();return finish();
        }
        lastNow_=s.source.nowNs;
        if(s.source.sequence==previous_->source.sequence){
            if(s.source.observedNs!=previous_->source.observedNs||s.source.deadlineNs!=previous_->source.deadlineNs||
               s.tip!=previous_->tip||s.direction!=previous_->direction||s.attackAvailable!=previous_->attackAvailable)
                Invalidate();
            return finish(); // Repeated rendering polls never advance a stroke.
        }
        const auto old=*previous_;previous_=s;
        const auto elapsed=now-old.source.observedNs;
        const auto step=Sub(s.tip,old.tip);const auto distance=Length(step);
        const auto seconds=static_cast<float>(elapsed)*1.e-9f;
        const auto speed=elapsed>0?distance/seconds:config_.maximumSpeed+1.f;
        if(elapsed<=0||elapsed>config_.maximumSampleGapNs||distance>config_.maximumStep||
           !std::isfinite(speed)||speed>config_.maximumSpeed){Start(s);return finish();}
        if(phase_==MeleeStrokePhase::Settling||phase_==MeleeStrokePhase::Spent){
            if(speed>config_.restSpeed)quietNs_=now;
            const auto dwell=phase_==MeleeStrokePhase::Spent?config_.rearmNs:config_.settleNs;
            if(now-quietNs_>=dwell)phase_=MeleeStrokePhase::Ready;
            return finish();
        }
        if(phase_==MeleeStrokePhase::Ready){
            if(speed<config_.minimumSpeed)return finish();
            if(Dot(step,s.direction)<distance*config_.minimumAlignment){Start(s);return finish();}
            start_=old.tip;axis_=s.direction;strokeNs_=old.source.observedNs;
            phase_=MeleeStrokePhase::Travelling;
        }
        if(phase_==MeleeStrokePhase::Travelling){
            if(now-strokeNs_>config_.maximumStrokeNs||speed<config_.minimumSpeed||
               Dot(axis_,s.direction)<config_.minimumAlignment||
               Dot(step,axis_)<distance*config_.minimumAlignment){Start(s);return finish();}
            const auto displacement=Sub(s.tip,start_);
            if(Dot(displacement,axis_)>=config_.minimumTravel){
                if(serial_==UINT64_MAX){Invalidate();return finish();}
                phase_=MeleeStrokePhase::Spent;quietNs_=now;
                return {phase_,MeleeStrokeIntent{++serial_,s.hold,s.source.sequence,now,s.source.deadlineNs,start_,s.tip,axis_}};
            }
        }
        return finish();
    }
    // Clears motion only. The adapter must not interpret this as cancellation
    // or completion of a native attack that has already been submitted.
    void Invalidate()noexcept{previous_.reset();phase_=MeleeStrokePhase::Unavailable;quietNs_=strokeNs_=0;}
    MeleeStrokePhase Phase()const noexcept{return phase_;}
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
    static bool Valid(const MeleeStrokeSample& s,const HandInteraction& hands)noexcept {
        if(!s.attackAvailable||s.hold.kind!=HandClaimKind::GunHold||s.hold.owner!=s.source.owner||
           !s.source.sequence||!s.source.focused||s.source.observedNs<0||s.source.nowNs<s.source.observedNs||
           s.source.deadlineNs<=s.source.nowNs||s.source.deadlineNs-s.source.observedNs>150000000||
           !Finite(s.tip)||Length(s.tip)>3.f||!Finite(s.direction)||std::abs(Length(s.direction)-1.f)>.001f||
           !hands.HasRecordedEvidence(s.source,s.hold.hand))return false;
        const auto current=hands.Current(s.hold.hand);
        return current&&current->token==s.hold&&current->deadlineNs>s.source.nowNs&&
            current->inputSequence==s.source.sequence;
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
