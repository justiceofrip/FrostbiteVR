#pragma once
#include "SceneRecoveryProbe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string_view>

namespace fvr::probe {
constexpr bool TriggerInputSmokeOption(std::wstring_view option)noexcept {
    return option==L"--trigger-input-smoke"||option==L"--selected-knife-trigger-smoke"||
        option==L"--selected-knife-motion-smoke"||option==L"--no-image-files"||option==L"--pairs"||option==L"--static-pose"||
        option==L"--async"||option==L"--request-lifetime"||option==L"--capture-poses";
}
constexpr bool TriggerInputSmokeArguments(bool selected,bool staticPose,bool async,
    unsigned pairs,bool optionsOnly,bool motion=false,bool noImages=false)noexcept {
    return selected&&staticPose&&async&&pairs>=8&&pairs<=240&&optionsOnly&&(motion?pairs==240:!noImages);
}
// A publication fixture, not a native attack/damage authority. The clock never
// extends the one pulse if a slow publisher misses it. No draw/grip gestures.
class TriggerInputSmoke {
public:
    static constexpr std::uint64_t DurationMs=5000,PressMs=400,ReleaseMs=480,NeutralTailMs=500;
    enum class Receipt:unsigned {None,Neutral,Press,Release,NeutralTail,Motion};
    enum class Failure:unsigned {None,Clock,Identity,Tracking,UnexpectedInput,Publication};
    static constexpr unsigned Phase(std::uint64_t ms)noexcept {
        return ms<PressMs?0:ms<ReleaseMs?1:ms<DurationMs?2:3;
    }
    static constexpr bool Continue(std::uint64_t ms)noexcept{return ms<DurationMs;}
    // LOCAL translation direction from the measured blade and actual wrist
    // calibration. This is receiver placement only, never native hit authority.
    bool Motion(math::Vec3 direction)noexcept {
        const float length=std::hypot(direction.x,direction.y,direction.z);
        if(attempts_||motion_||!std::isfinite(length)||length<.999f||length>1.001f)return false;
        direction_={direction.x/length,direction.y/length,direction.z/length};motion_=true;return true;
    }
    bool IsMotion()const noexcept{return motion_;}
    std::uint64_t Duration()const noexcept{return motion_?8000:DurationMs;}
    bool ContinueInput(std::uint64_t ms)const noexcept{return ms<Duration();}
    unsigned CurrentPhase(std::uint64_t ms)const noexcept {
        if(!motion_)return Phase(ms);
        return ms<2000?0:ms<2120?1:ms<3200?2:ms<4200?3:ms<5200?4:
            ms<5320?5:ms<6500?6:ms<7500?7:ms<8000?8:9;
    }
    bool PairRequestDue(std::uint64_t ms,unsigned consumed,unsigned target)const noexcept {
        return consumed<target&&(!motion_||ms>=std::uint64_t(consumed)*7500/target);
    }
    math::Vec3 MotionPosition(std::uint64_t ms)const noexcept {
        float travel=0;
        if(ms>=2000&&ms<2120)travel=.12f*float(ms-2000)/120;
        else if(ms>=2120&&ms<3200)travel=.12f;
        else if(ms>=3200&&ms<4200)travel=.12f*(1-float(ms-3200)/1000);
        else if(ms>=5200&&ms<5320)travel=.12f*float(ms-5200)/120;
        else if(ms>=5320&&ms<6500)travel=.12f;
        else if(ms>=6500&&ms<7500)travel=.12f*(1-float(ms-6500)/1000);
        return {.15f+direction_.x*travel,-.10f+direction_.y*travel,-.20f+direction_.z*travel};
    }
    interaction::InputFrame Input(const runtime::TrackingFrame& tracking,std::uint64_t sequence,
        std::uint64_t ms)const noexcept {
        auto result=NeutralSceneInput(tracking,sequence);
        if(motion_){
            if(!Failed()&&neutralInput_&&tracking.focused&&tracking.headValid&&tracking.spaceGeneration==space_)
                result.hands[1].grip.position=MotionPosition(ms);
            result.hands[1].aim=result.hands[1].grip;return result;
        }
        // A first packet arriving inside/after the pulse cannot manufacture an
        // armed trigger: a successfully published neutral packet must precede it.
        if(!Failed()&&neutralInput_&&tracking.focused&&tracking.headValid&&
           tracking.spaceGeneration==space_&&Phase(ms)==1)result.hands[1].trigger=1.f;
        return result;
    }
    Receipt Published(const interaction::InputFrame& input,std::uint64_t ms,
        std::int64_t before,std::int64_t after,bool delivered,bool busy)noexcept {
        ++attempts_;
        if(before<=0||after<before||(lastBefore_&&before<lastBefore_)||
           (attempts_>1&&ms<lastMs_)||!ContinueInput(ms))Fail(Failure::Clock);
        if(!input.generation||(lastInput_&&input.generation<=lastInput_)||!input.spaceGeneration||
           (space_&&input.spaceGeneration!=space_))Fail(Failure::Identity);
        if(!input.focused||!input.headValid)Fail(Failure::Tracking);
        for(const auto& hand:input.hands)if(!hand.gripTracked||!hand.aimTracked||
            (hand.active&interaction::Components)!=interaction::Components)Fail(Failure::Tracking);
        auto neutral=input;neutral.hands[1].trigger=0;
        const bool high=input.hands[1].trigger==1.f;
        if(!NeutralSceneActions(neutral)||(input.hands[1].trigger!=0.f&&!high)||
           (high&&(motion_||!neutralInput_||Phase(ms)!=1)))Fail(Failure::UnexpectedInput);
        if(motion_&&!Failed()){
            const auto expected=MotionPosition(ms);const auto& actual=input.hands[1].grip.position;
            if((!neutralInput_&&CurrentPhase(ms)!=0)||std::hypot(expected.x-actual.x,expected.y-actual.y,expected.z-actual.z)>.00001f)
                Fail(Failure::UnexpectedInput);
        }
        lastMs_=ms;lastBefore_=after;lastInput_=input.generation;
        if(!delivered){++missed_;if(busy)++busy_;else Fail(Failure::Publication);return Receipt::None;}
        ++published_;
        if(lastAfter_)maxGapNs_=(std::max)(maxGapNs_,after-lastAfter_);
        lastAfter_=after;
        if(Failed())return Receipt::None;
        space_=input.spaceGeneration;
        if(motion_)return PublishedMotion(input,ms,after);
        if(high){
            if(releaseInput_)Fail(Failure::UnexpectedInput);
            if(!pressInput_){pressInput_=input.generation;pressMs_=ms;pressBeforeNs_=before;pressAfterNs_=after;
                return Receipt::Press;}
        }else if(!pressInput_){
            if(!neutralInput_){neutralInput_=input.generation;return Receipt::Neutral;}
        }else{
            ++postReleaseSamples_;
            if(!releaseInput_){releaseInput_=input.generation;releaseMs_=ms;releaseBeforeNs_=before;releaseAfterNs_=after;
                return Receipt::Release;}
            if(!tailInput_&&ms>=releaseMs_+NeutralTailMs&&after-releaseAfterNs_>=std::int64_t(NeutralTailMs)*1000000){
                tailInput_=input.generation;return Receipt::NeutralTail;}
        }
        return Receipt::None;
    }
    bool Complete(std::uint64_t elapsed,bool durationEnded)const noexcept {
        if(motion_)return durationEnded&&elapsed>=Duration()&&!Failed()&&neutralInput_&&neutralMs_<=1800&&
            motionSamples_[0]>=4&&motionSamples_[1]>=4&&motionTravel_[0]>=.06f&&motionTravel_[1]>=.06f&&
            motionGapNs_<=75000000&&tailInput_&&tailMs_<=7550&&lastMs_>=7900&&lastAfter_-tailNs_>=400000000;
        return durationEnded&&elapsed>=DurationMs&&!Failed()&&neutralInput_&&pressInput_&&releaseInput_&&tailInput_&&postReleaseSamples_>=2;
    }
    bool Failed()const noexcept{return failure_!=Failure::None;}
    Failure Reason()const noexcept{return failure_;}
    void Report(std::ostream& out,std::uint64_t elapsed,bool durationEnded)const {
        if(motion_){out<<"{\"schema\":\"fvr.selected_knife_motion_smoke.v1\",\"duration_ms\":"<<Duration()
            <<",\"elapsed_ms\":"<<elapsed<<",\"publication_complete\":"<<(Complete(elapsed,durationEnded)?"true":"false")
            <<",\"stroke_windows_ms\":[[2000,2120],[5200,5320]],\"travel_metres\":0.12,\"axis_xr\":["<<direction_.x<<','<<direction_.y<<','<<direction_.z<<']'
            <<",\"attempts\":"<<attempts_<<",\"published\":"<<published_<<",\"missed\":"<<missed_<<",\"busy\":"<<busy_
            <<",\"stroke_samples\":["<<motionSamples_[0]<<','<<motionSamples_[1]<<"],\"delivered_stroke_travel\":["<<motionTravel_[0]<<','<<motionTravel_[1]<<']'
            <<",\"max_stroke_gap_ns\":"<<motionGapNs_<<",\"neutral_input\":"<<neutralInput_<<",\"neutral_tail_input\":"<<tailInput_
            <<",\"failure\":"<<unsigned(failure_)<<",\"native_input_observed\":false,\"miss_verified\":false,\"damage_verified\":false,\"headset_tested\":false}";return;}
        out<<"{\"schema\":\"fvr.trigger_input_smoke.v1\",\"duration_ms\":"<<DurationMs
           <<",\"elapsed_ms\":"<<elapsed<<",\"pulse_window_ms\":["<<PressMs<<','<<ReleaseMs
           <<"],\"minimum_post_release_observation_ms\":"<<NeutralTailMs
           <<",\"publication_complete\":"<<(Complete(elapsed,durationEnded)?"true":"false")
           <<",\"attempts\":"<<attempts_<<",\"published\":"<<published_<<",\"missed\":"<<missed_<<",\"busy\":"<<busy_
           <<",\"neutral_input\":"<<neutralInput_<<",\"press_input\":"<<pressInput_<<",\"release_input\":"<<releaseInput_
           <<",\"neutral_tail_input\":"<<tailInput_<<",\"post_release_samples\":"<<postReleaseSamples_
           <<",\"press_elapsed_ms\":"<<pressMs_<<",\"press_before_ns\":"<<pressBeforeNs_<<",\"press_after_ns\":"<<pressAfterNs_
           <<",\"release_elapsed_ms\":"<<releaseMs_<<",\"release_before_ns\":"<<releaseBeforeNs_<<",\"release_after_ns\":"<<releaseAfterNs_
           <<",\"max_successful_publication_gap_ns\":"<<maxGapNs_<<",\"failure\":"<<unsigned(failure_)
           <<",\"native_input_observed\":false,\"damage_verified\":false,\"headset_tested\":false}";
    }
private:
    Receipt PublishedMotion(const interaction::InputFrame& input,std::uint64_t ms,std::int64_t after)noexcept {
        const auto phase=CurrentPhase(ms);const bool changed=phase!=motionPhase_;motionPhase_=phase;
        if(!neutralInput_){neutralInput_=input.generation;neutralMs_=ms;return Receipt::Neutral;}
        if(phase==1||phase==5){const unsigned stroke=phase==1?0:1;const auto start=stroke?5200:2000;
            const float travel=.12f*float(ms-start)/120;
            if(!motionSamples_[stroke])motionStartTravel_[stroke]=travel;
            if(motionLastNs_[stroke])motionGapNs_=(std::max)(motionGapNs_,after-motionLastNs_[stroke]);
            motionLastNs_[stroke]=after;motionTravel_[stroke]=travel-motionStartTravel_[stroke];++motionSamples_[stroke];
            return Receipt::Motion;
        }
        if(phase==8&&!tailInput_){tailInput_=input.generation;tailMs_=ms;tailNs_=after;return Receipt::NeutralTail;}
        return changed?Receipt::Motion:Receipt::None;
    }
    void Fail(Failure reason)noexcept{if(!Failed())failure_=reason;}
    Failure failure_=Failure::None;
    std::uint64_t lastMs_=0,lastInput_=0,space_=0,neutralInput_=0,pressInput_=0,releaseInput_=0,tailInput_=0,pressMs_=0,releaseMs_=0;
    std::int64_t lastBefore_=0,lastAfter_=0,maxGapNs_=0,pressBeforeNs_=0,pressAfterNs_=0,releaseBeforeNs_=0,releaseAfterNs_=0;
    unsigned attempts_=0,published_=0,missed_=0,busy_=0,postReleaseSamples_=0;
    bool motion_=false;math::Vec3 direction_{};unsigned motionPhase_=0;
    std::array<unsigned,2> motionSamples_{};std::array<float,2> motionStartTravel_{},motionTravel_{};
    std::array<std::int64_t,2> motionLastNs_{};std::int64_t motionGapNs_=0,tailNs_=0;
    std::uint64_t neutralMs_=0,tailMs_=0;
};
} // namespace fvr::probe
