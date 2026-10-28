#pragma once
#include "SceneRecoveryProbe.h"
#include "fvr/ipc/FrameChannel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string_view>

namespace fvr::probe {
constexpr bool TriggerInputSmokeOption(std::wstring_view option)noexcept {
    return option==L"--trigger-input-smoke"||option==L"--selected-knife-trigger-smoke"||
        option==L"--selected-knife-motion-smoke"||option==L"--selected-knife-motion-rotation"||option==L"--selected-knife-approach"||
        option==L"--selected-knife-approach-crouch"||option==L"--selected-knife-approach-turn"||
        option==L"--selected-knife-approach-resume"||option==L"--selected-knife-approach-prepared"||
        option==L"--selected-knife-review-images"||
        option==L"--no-image-files"||option==L"--pairs"||option==L"--static-pose"||
        option==L"--async"||option==L"--request-lifetime"||option==L"--capture-poses";
}
constexpr bool TriggerInputSmokeArguments(bool selected,bool staticPose,bool async,
    unsigned pairs,bool optionsOnly,bool motion=false,bool noImages=false,bool approach=false,bool reviewImages=false)noexcept {
    if(approach)return selected&&staticPose&&async&&pairs==320&&optionsOnly&&(noImages!=reviewImages);
    return !reviewImages&&selected&&staticPose&&async&&pairs>=8&&pairs<=240&&optionsOnly&&(motion?pairs==240:!noImages);
}
// A publication fixture, not a native attack/damage authority. The clock never
// extends the one pulse if a slow publisher misses it. No draw/grip gestures.
class TriggerInputSmokeSequence {
public:
    static constexpr std::uint64_t DurationMs=5000,PressMs=400,ReleaseMs=480,NeutralTailMs=500;
    enum class Receipt:unsigned {None,Neutral,Press,Release,NeutralTail,Motion};
    enum class Failure:unsigned {None,Clock,Identity,Tracking,UnexpectedInput,Publication,Menu,Approach};
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
    static constexpr std::uint64_t RotationBeginMs=500,RotationEndMs=1500;
    bool MotionRotation(math::Quaternion q)noexcept {
        const float length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
        if(!motion_||attempts_||rotation_||!std::isfinite(length)||length<.999f||length>1.001f)return false;
        q={q.x/length,q.y/length,q.z/length,q.w/length};
        if(q.w<.70710678f||q.w>1)return false; // Canonical quaternion, at most90degrees.
        rotation_=true;rotationTarget_=q;return true;
    }
    bool HasMotionRotation()const noexcept{return rotation_;}
    math::Quaternion MotionOrientation(std::uint64_t ms)const noexcept {
        if(!rotation_||ms<=RotationBeginMs)return {};
        if(ms>=RotationEndMs)return rotationTarget_;
        const float halfAngle=std::acos(rotationTarget_.w),sine=std::sin(halfAngle);
        if(sine<.000001f)return rotationTarget_;
        const float fraction=float(ms-RotationBeginMs)/float(RotationEndMs-RotationBeginMs);
        const float scale=std::sin(halfAngle*fraction)/sine;
        return {rotationTarget_.x*scale,rotationTarget_.y*scale,rotationTarget_.z*scale,std::cos(halfAngle*fraction)};
    }
    void ReportRotation(std::ostream& out)const {
        if(!rotation_)return;
        const auto precision=out.precision();out.precision(9);
        out<<",\"motion_rotation_xyzw\":["<<rotationTarget_.x<<','<<rotationTarget_.y<<','<<rotationTarget_.z<<','<<rotationTarget_.w
            <<"],\"rotation_window_ms\":["<<RotationBeginMs<<','<<RotationEndMs<<"],\"rotation_neutral_first\":true";
        out.precision(precision);
    }
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
            if(!Failed()&&neutralInput_&&tracking.focused&&tracking.headValid&&tracking.spaceGeneration==space_){
                result.hands[1].grip.position=MotionPosition(ms);
                if(rotation_)result.hands[1].grip.orientation=MotionOrientation(ms);
            }
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
            if(rotation_){
                const auto q=MotionOrientation(ms);
                const auto matches=[&](math::Quaternion actual){return std::isfinite(actual.x)&&std::isfinite(actual.y)&&
                    std::isfinite(actual.z)&&std::isfinite(actual.w)&&std::abs(q.x-actual.x)<.00001f&&
                    std::abs(q.y-actual.y)<.00001f&&std::abs(q.z-actual.z)<.00001f&&std::abs(q.w-actual.w)<.00001f;};
                const auto& aim=input.hands[1].aim;
                if((!neutralInput_&&ms>RotationBeginMs)||!matches(input.hands[1].grip.orientation)||!matches(aim.orientation)||
                   std::hypot(expected.x-aim.position.x,expected.y-aim.position.y,expected.z-aim.position.z)>.00001f)
                    Fail(Failure::UnexpectedInput);
            }
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
    void Report(std::ostream& out,std::uint64_t elapsed,bool durationEnded,Failure additional=Failure::None)const {
        durationEnded=durationEnded&&additional==Failure::None;
        const auto reportedFailure=Failed()?failure_:additional;
        if(motion_){out<<"{\"schema\":\"fvr.selected_knife_motion_smoke.v1\",\"duration_ms\":"<<Duration()
            <<",\"elapsed_ms\":"<<elapsed<<",\"publication_complete\":"<<(Complete(elapsed,durationEnded)?"true":"false")
            <<",\"stroke_windows_ms\":[[2000,2120],[5200,5320]],\"travel_metres\":0.12,\"axis_xr\":["<<direction_.x<<','<<direction_.y<<','<<direction_.z<<']';
            ReportRotation(out);out<<",\"attempts\":"<<attempts_<<",\"published\":"<<published_<<",\"missed\":"<<missed_<<",\"busy\":"<<busy_
            <<",\"stroke_samples\":["<<motionSamples_[0]<<','<<motionSamples_[1]<<"],\"delivered_stroke_travel\":["<<motionTravel_[0]<<','<<motionTravel_[1]<<']'
            <<",\"max_stroke_gap_ns\":"<<motionGapNs_<<",\"neutral_input\":"<<neutralInput_<<",\"neutral_tail_input\":"<<tailInput_
            <<",\"failure\":"<<unsigned(reportedFailure)<<",\"native_input_observed\":false,\"miss_verified\":false,\"damage_verified\":false,\"headset_tested\":false}";return;}
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
    bool motion_=false,rotation_=false;math::Quaternion rotationTarget_{};math::Vec3 direction_{};unsigned motionPhase_=0;
    std::array<unsigned,2> motionSamples_{};std::array<float,2> motionStartTravel_{},motionTravel_{};
    std::array<std::int64_t,2> motionLastNs_{};std::int64_t motionGapNs_=0,tailNs_=0;
    std::uint64_t neutralMs_=0,tailMs_=0;
};
// Optional receiver-only locomotion prefix. The unchanged attack sequence starts
// only after published movement release and a neutral interval. This is input
// evidence; neither elapsed time nor completed publication proves displacement,
// native attack acceptance, a hit, or damage.
class TriggerInputSmoke:public TriggerInputSmokeSequence {
    using Sequence=TriggerInputSmokeSequence;
public:
    static constexpr std::uint64_t ApproachNeutralMs=500,MaximumApproachMs=10000;
    static constexpr std::uint64_t TurnCycleMs=500,TurnPulseMs=150;
    bool Approach(std::uint64_t ms,float strafe,float forward)noexcept {
        if(approachMs_||attempts_||!ms||ms>MaximumApproachMs||!std::isfinite(strafe)||!std::isfinite(forward)||
           forward<0||std::hypot(strafe,forward)>.5f||(strafe==0&&forward==0))return false;
        approachMs_=ms;strafe_=strafe;forward_=forward;return true;
    }
    bool IsApproach()const noexcept{return approachMs_!=0;}
    bool ApproachCrouch()noexcept {
        if(!IsApproach()||attempts_||crouch_)return false;
        crouch_=true;return true;
    }
    bool CrouchDuringApproach()const noexcept{return crouch_;}
    static constexpr std::uint64_t CrouchClickMs=80;
    // Two bounded left Y clicks: release the first as movement begins and
    // the second after movement ends. Neither window is extended or retried.
    bool CrouchClickHeld(std::uint64_t recipeMs)const noexcept {
        return crouch_&&((recipeMs>=MovementStartMs()-CrouchClickMs&&recipeMs<MovementStartMs())||
            (recipeMs>=MovementStartMs()+approachMs_&&recipeMs<MovementReleaseMs()));
    }
    std::uint64_t MovementReleaseMs()const noexcept {
        return MovementStartMs()+approachMs_+(crouch_?CrouchClickMs:0);
    }
    bool ApproachTurn(int snaps)noexcept {
        if(!IsApproach()||attempts_||turnSnaps_||resumeEnabled_||prepared_||!snaps||snaps<-6||snaps>6)return false;
        turnSnaps_=snaps;return true;
    }
    static constexpr std::uint64_t MaximumPauseMs=2500,RearmNeutralMs=500;
    bool ApproachResume()noexcept {
        if(!IsApproach()||attempts_||turnSnaps_||resumeEnabled_||prepared_)return false;
        resumeEnabled_=true;return true;
    }
    static constexpr std::uint64_t PreparedMovementStartMs=2000;
    bool ApproachPrepared()noexcept {
        if(!IsApproach()||!IsMotion()||!HasMotionRotation()||attempts_||turnSnaps_||resumeEnabled_||prepared_)return false;
        prepared_=true;return true;
    }
    bool PreparedApproach()const noexcept{return prepared_;}
    std::uint64_t SequenceOffsetMs()const noexcept{return prepared_?PreparedMovementStartMs:0;}
    std::uint64_t MotionSequenceMs(std::uint64_t wall)const noexcept {
        const auto ms=RecipeMs(wall);
        if(prepared_&&ms<MovementStartMs())return ms;
        return SequenceOffsetMs()+(ms>=AttackStartMs()?ms-AttackStartMs():0);
    }
    void ReportPrepared(std::ostream& out,std::uint64_t wall)const {
        if(!prepared_)return;
        out<<",\"approach_prepared\":true,\"preparation_windows_ms\":[[0,500],[500,1500],[1500,2000]]"
            <<",\"motion_sequence_at_attack_start_ms\":"<<SequenceOffsetMs()<<",\"motion_sequence_elapsed_ms\":"<<MotionSequenceMs(wall)
            <<",\"scheduled_stroke_windows_ms\":[["<<AttackStartMs()<<','<<AttackStartMs()+120<<"],["
            <<AttackStartMs()+3200<<','<<AttackStartMs()+3320<<"]],\"preparation_samples\":["
            <<preparedSamples_[0]<<','<<preparedSamples_[1]<<','<<preparedSamples_[2]
            <<"],\"preparation_max_gap_ns\":"<<preparedGapNs_;
    }
    bool ResumeEnabled()const noexcept{return resumeEnabled_;}
    bool Paused()const noexcept{return paused_;}
    std::uint64_t RecipeMs(std::uint64_t wall)const noexcept {
        return paused_?pauseRecipeMs_:wall>=pauseDurationMs_?wall-pauseDurationMs_:0;
    }
    std::uint64_t MaximumWallDurationMs()const noexcept{return RecipeDurationMs()+(resumeEnabled_?MaximumPauseMs:0);}
    bool DurationEnded(std::uint64_t wall)const noexcept {
        return !paused_&&RecipeMs(wall)>=RecipeDurationMs();
    }
    void ReportResume(std::ostream& out,std::uint64_t wall)const {
        if(!resumeEnabled_)return;
        out<<",\"approach_resume\":true,\"phase_windows_use_recipe_ms\":true,\"wall_elapsed_ms\":"<<wall
            <<",\"recipe_elapsed_ms\":"<<RecipeMs(wall)
            <<",\"pause_active\":"<<(paused_?"true":"false")<<",\"pause_count\":"<<pauseCount_
            <<",\"maximum_pause_ms\":"<<MaximumPauseMs<<",\"maximum_wall_duration_ms\":"<<MaximumWallDurationMs()
            <<",\"pause_wall_start_ms\":"<<pauseStartMs_<<",\"pause_wall_end_ms\":"<<pauseEndMs_
            <<",\"pause_duration_ms\":"<<(paused_&&wall>=pauseStartMs_?wall-pauseStartMs_:pauseDurationMs_)
            <<",\"pause_start_ns\":"<<pauseStartNs_<<",\"pause_end_ns\":"<<pauseEndNs_
            <<",\"rearm_first_ns\":"<<rearmFirstNs_<<",\"rearm_last_ns\":"<<rearmLastNs_
            <<",\"rearm_samples\":"<<rearmSamples_<<",\"rearm_resets\":"<<rearmResets_
            <<",\"menu_read_result\":"<<unsigned(menuRead_)
            <<",\"max_movement_segment_gap_ns\":"<<movementSegmentGapNs_;
    }
    int TurnSnaps()const noexcept{return turnSnaps_;}
    unsigned TurnCount()const noexcept{return unsigned(turnSnaps_<0?-turnSnaps_:turnSnaps_);}
    std::uint64_t MovementStartMs()const noexcept{return prepared_?PreparedMovementStartMs:ApproachNeutralMs+TurnCount()*TurnCycleMs;}
    void ReportTurn(std::ostream& out)const {
        if(!turnSnaps_)return;
        out<<",\"approach_turn_snaps\":"<<turnSnaps_<<",\"turn_window_ms\":[500,"<<MovementStartMs()
            <<"],\"turn_cycle_ms\":"<<TurnCycleMs<<",\"turn_pulse_ms\":"<<TurnPulseMs
            <<",\"movement_start_ms\":"<<MovementStartMs();
    }
    bool EnableReviewImages()noexcept {
        if(!IsApproach()||attempts_||reviewImages_)return false;
        reviewImages_=true;return true;
    }
    bool ReviewImagesRequested()const noexcept{return reviewImages_;}
    unsigned ReviewPairs()const noexcept{return reviewPairs_;}
    std::uint64_t ReviewFinalMs()const noexcept{return AttackStartMs()+(IsMotion()?5320:ReleaseMs)-SequenceOffsetMs()+500;}
    // A completed pair must originate after an actually delivered release/end
    // packet. Wall-clock arrival alone cannot admit an old queued stereo pair.
    unsigned ReviewPair(std::uint64_t sourceInput,std::uint64_t space)noexcept {
        if(!reviewImages_||Failed()||reviewPairs_>=2||space!=space_||!sourceInput||
           sourceInput>lastInput_||sourceInput<=lastReviewInput_)return 0;
        const auto threshold=reviewPairs_?reviewFinalInput_:(crouch_?crouchReleaseInput_[1]:releaseInput_);
        if(!threshold||sourceInput<threshold)return 0;
        lastReviewInput_=sourceInput;return ++reviewPairs_;
    }
    void ReportReview(std::ostream& out)const {
        if(reviewImages_)out<<",\"review_images\":true,\"review_max_pairs\":2,\"review_final_after_ms\":"<<ReviewFinalMs();
    }
    bool JournalEveryInput()const noexcept{return IsApproach()||HasMotionRotation();}
    std::uint64_t ApproachMs()const noexcept{return approachMs_;}
    float Strafe()const noexcept{return strafe_;}
    float Forward()const noexcept{return forward_;}
    std::uint64_t AttackStartMs()const noexcept{return IsApproach()?MovementReleaseMs()+ApproachNeutralMs:0;}
    std::uint64_t RecipeDurationMs()const noexcept{return AttackStartMs()+Sequence::Duration()-SequenceOffsetMs();}
    std::uint64_t Duration()const noexcept{return RecipeDurationMs()+pauseDurationMs_;}
    bool ContinueInput(std::uint64_t ms)const noexcept {
        return (!resumeEnabled_||!Failed())&&ms<MaximumWallDurationMs()&&!DurationEnded(ms);
    }
    unsigned CurrentPhase(std::uint64_t ms)const noexcept {
        if(paused_)return 200;
        ms=RecipeMs(ms);
        if(IsApproach()&&ms<AttackStartMs()){
            if(prepared_&&ms<MovementStartMs())return ms<500?100:ms<1500?110:111;
            if(ms<ApproachNeutralMs)return 100;
            if(TurnWindow(ms))return 103+unsigned((ms-ApproachNeutralMs)/TurnCycleMs)*2+(Turning(ms)?0:1);
            return Moving(ms)?101:102;
        }
        return Sequence::CurrentPhase(ms-AttackStartMs()+SequenceOffsetMs());
    }
    bool PairRequestDue(std::uint64_t ms,unsigned consumed,unsigned target)const noexcept {
        return IsApproach()?consumed<target&&RecipeMs(ms)>=std::uint64_t(consumed)*(RecipeDurationMs()-500)/target:
            Sequence::PairRequestDue(ms,consumed,target);
    }
    bool Failed()const noexcept{return failure_!=Failure::None||Sequence::Failed();}
    Failure Reason()const noexcept{return failure_!=Failure::None?failure_:Sequence::Reason();}
    interaction::InputFrame Input(const runtime::TrackingFrame& t,std::uint64_t sequence,std::uint64_t ms)const noexcept {
        ms=RecipeMs(ms);
        if(!IsApproach()){
            auto input=Sequence::Input(t,sequence,ms);
            if(HasMotionRotation()&&Failed())input.hands[1].gripTracked=input.hands[1].aimTracked=false;
            return input;
        }
        auto input=NeutralSceneInput(t,sequence);
        if(Failed())return input;
        if(paused_)return input;
        if(ms<AttackStartMs()){
            if(prepared_)input=Sequence::Input(t,sequence,(std::min)(ms,PreparedMovementStartMs));
            if(Turning(ms)&&neutralInput_&&menuEpoch_&&t.focused&&t.headValid&&t.spaceGeneration==space_)
                input.hands[1].stickX=turnSnaps_<0?-.9f:.9f;
            if(Moving(ms)&&neutralInput_&&menuEpoch_&&t.focused&&t.headValid&&t.spaceGeneration==space_){
                input.hands[0].stickX=strafe_;input.hands[0].stickY=forward_;
            }
            if(CrouchClickHeld(ms)&&neutralInput_&&menuEpoch_&&t.focused&&t.headValid&&t.spaceGeneration==space_)
                input.hands[0].held=interaction::Secondary;
            return input;
        }
        if(prepared_)input=Sequence::Input(t,sequence,PreparedMovementStartMs);
        if(!releaseInput_||(crouch_&&!crouchReleaseInput_[1])||
           ms<(std::max)(releaseMs_,crouchReleaseMs_)+ApproachNeutralMs)return input;
        return Sequence::Input(t,sequence,ms-AttackStartMs()+SequenceOffsetMs());
    }
    // Called after the ordinary real-menu decorator, before journaling and IPC.
    // Default loss is terminal. The opt-in straight approach may pause once
    // during initial neutral or movement; it never extends menu authority.
    void GateMenu(interaction::InputFrame& input,std::uint64_t wall,std::int64_t now,
        ipc::ChannelResult read=ipc::ChannelResult::Ok)noexcept {
        if(!IsApproach()&&!HasMotionRotation())return;
        const auto ms=RecipeMs(wall);menuRead_=read;
        if(resumeEnabled_&&(wall<lastWallMs_||now<=0))Fail(Failure::Clock);
        if(resumeEnabled_&&(read==ipc::ChannelResult::Closed||read==ipc::ChannelResult::Invalid))Fail(Failure::Menu);
        if(!Tracked(input))Fail(Failure::Tracking);
        if(!input.trackingBasisGeneration||(basis_&&input.trackingBasisGeneration!=basis_)||
           (space_&&input.spaceGeneration!=space_))Fail(Failure::Identity);
        if(!basis_&&input.trackingBasisGeneration)basis_=input.trackingBasisGeneration;
        if(!space_)space_=input.spaceGeneration;
        const bool gameplay=interaction::GameplayOwnsInput(input.menu,now);
        if(gameplay){
            if(menuEpoch_&&input.menu.epoch!=menuEpoch_)Fail(Failure::Menu);
            else if(!menuEpoch_){menuEpoch_=input.menu.epoch;basis_=input.trackingBasisGeneration;space_=input.spaceGeneration;}
            lastMenuDeadlineNs_=input.menu.deadlineNs;
        }else if(menuEpoch_||ms>=ApproachNeutralMs){
            const bool expired=(read==ipc::ChannelResult::Timeout||read==ipc::ChannelResult::Busy)&&
                input.menu.owner==interaction::InputMenuOwner::Unknown&&lastMenuDeadlineNs_>0&&now>=lastMenuDeadlineNs_;
            // A startup hitch may expire the first lease before any action.
            // Use the same bounded neutral rearm; never restart a begun click.
            const bool startup=ms<ApproachNeutralMs&&neutralInput_&&!crouchPressInput_[0];
            if(resumeEnabled_&&!Failed()&&expired&&(paused_||(!pauseCount_&&
               (startup||(Moving(ms)&&movementSamples_&&!releaseInput_))))){
                if(!paused_){paused_=true;++pauseCount_;pauseStartMs_=wall;pauseRecipeMs_=ms;pauseStartNs_=now;}
            }else Fail(Failure::Menu);
        }
        if(paused_&&(wall-pauseStartMs_>=MaximumPauseMs||now-pauseStartNs_>=std::int64_t(MaximumPauseMs)*1000000))Fail(Failure::Menu);
        if(IsApproach()&&ms>=MovementStartMs()&&!TurnsCovered())Fail(Failure::Approach);
        if(prepared_&&ms>=MovementStartMs()){
            if(!PreparationCovered())Fail(Failure::Approach);
            if(ms<AttackStartMs()&&!PreparedPose(input))Fail(Failure::UnexpectedInput);
        }
        if(IsApproach()&&ms>=AttackStartMs()&&(!releaseInput_||!MovementCovered()))Fail(Failure::Approach);
        if(Failed()||!gameplay||paused_)for(auto& hand:input.hands){hand.held=hand.touched=hand.touchActive=0;
            hand.stickX=hand.stickY=hand.trigger=hand.squeeze=0;}
        // Cancelling this synthetic recipe also suspends its posture output.
        // Withholding authority is not a fabricated native menu transition.
        if(Failed())input.menu={};
        // A cancelled rotated recipe must not snap a still-tracked wrist back
        // to identity and accidentally create another physical gesture.
        if(Failed()&&HasMotionRotation())input.hands[1].gripTracked=input.hands[1].aimTracked=false;
    }
    Receipt Published(const interaction::InputFrame& input,std::uint64_t wall,std::int64_t before,std::int64_t after,
        bool delivered,bool busy)noexcept {
        ++attempts_;
        const auto ms=RecipeMs(wall);
        if(!IsApproach()&&!HasMotionRotation())return Sequence::Published(input,ms,before,after,delivered,busy);
        if(before<=0||after<before||(lastNs_&&before<lastNs_)||(attempts_>1&&ms<lastMs_)||
           wall<lastWallMs_||!ContinueInput(wall))Fail(Failure::Clock);
        if(!input.generation||(lastInput_&&input.generation<=lastInput_)||!input.spaceGeneration||
           (space_&&input.spaceGeneration!=space_)||!input.trackingBasisGeneration||input.trackingBasisGeneration!=basis_)Fail(Failure::Identity);
        if(!Tracked(input))Fail(Failure::Tracking);
        const bool gameplay=interaction::GameplayOwnsInput(input.menu,before)&&input.menu.deadlineNs>after;
        if(menuEpoch_&&(!gameplay||input.menu.epoch!=menuEpoch_)&&!paused_)Fail(Failure::Menu);
        lastInput_=input.generation;lastMs_=ms;lastWallMs_=wall;lastNs_=after;
        if(paused_)return PublishedPause(input,wall,after,delivered,busy,gameplay);
        if(!IsApproach())return Failed()?Receipt::None:Sequence::Published(input,ms,before,after,delivered,busy);
        if(!delivered){if(!busy)Fail(Failure::Publication);return Receipt::None;}
        if(Failed())return Receipt::None;
        auto checked=input;
        if(crouch_){
            const bool click=CrouchClickHeld(ms);
            if(input.hands[0].held!=(click?unsigned(interaction::Secondary):0u))Fail(Failure::UnexpectedInput);
            checked.hands[0].held=0;
            const unsigned index=ms<MovementStartMs()?0:1;
            if(click&&!crouchPressInput_[index])crouchPressInput_[index]=input.generation;
            for(unsigned n=0;n<2;++n)if(ms>=(n?MovementReleaseMs():MovementStartMs())&&!crouchReleaseInput_[n]){
                if(!crouchPressInput_[n]||click||ms>(n?MovementReleaseMs():MovementStartMs())+100)
                    Fail(Failure::Approach);
                else {
                    crouchReleaseInput_[n]=input.generation;
                    if(n==1){crouchReleaseMs_=ms;crouchReleaseNs_=after;}
                }
            }
            if(Failed())return Receipt::None;
        }
        if(prepared_&&ms<MovementStartMs()){
            if(!NeutralSceneActions(checked))Fail(Failure::UnexpectedInput);
            const unsigned phase=ms<500?0:ms<1500?1:2;
            if(!preparedSamples_[phase])preparedFirstMs_[phase]=ms;
            preparedLastMs_[phase]=ms;++preparedSamples_[phase];
            if(preparedLastNs_)preparedGapNs_=(std::max)(preparedGapNs_,after-preparedLastNs_);
            preparedLastNs_=after;
            if(ms<ApproachNeutralMs&&!neutralInput_)neutralInput_=input.generation;
            return Failed()?Receipt::None:Sequence::Published(checked,ms,before,after,delivered,busy);
        }
        if(prepared_&&ms<AttackStartMs()){
            if(!PreparationCovered())Fail(Failure::Approach);
            if(!PreparedPose(input))Fail(Failure::UnexpectedInput);
            if(!movementSamples_&&after-preparedLastNs_>100000000)Fail(Failure::Approach);
            if(Failed())return Receipt::None;
        }
        if(turnSnaps_&&ms<=MovementStartMs()){
            if(lastPrefixDeliveredNs_&&before-lastPrefixDeliveredNs_>100000000){Fail(Failure::Approach);return Receipt::None;}
            lastPrefixDeliveredNs_=after;
        }
        if(reviewImages_&&!reviewFinalInput_&&ms>=ReviewFinalMs())reviewFinalInput_=input.generation;
        if(ms>=AttackStartMs()){
            if(!MovementCovered()||!releaseInput_){Fail(Failure::Approach);return Receipt::None;}
            if(before-(std::max)(releaseNs_,crouchReleaseNs_)<std::int64_t(ApproachNeutralMs)*1000000)return Receipt::None;
            return Sequence::Published(input,ms-AttackStartMs()+SequenceOffsetMs(),before,after,delivered,busy);
        }
        auto neutral=checked;
        if(TurnWindow(ms)&&neutralInput_&&gameplay){
            const unsigned index=unsigned((ms-ApproachNeutralMs)/TurnCycleMs);
            if(Turning(ms)){
                if(input.hands[1].stickX!=(turnSnaps_<0?-.9f:.9f))Fail(Failure::UnexpectedInput);
                neutral.hands[1].stickX=0;
                if(!turnSamples_[index])turnFirstMs_[index]=ms;
                if(turnLastNs_[index])turnGapNs_=(std::max)(turnGapNs_,after-turnLastNs_[index]);
                turnLastNs_[index]=after;turnLastMs_[index]=ms;++turnSamples_[index];
            }else if(!turnReleaseInput_[index]){
                turnReleaseInput_[index]=input.generation;turnReleaseMs_[index]=ms;
                if(turnLastNs_[index])turnGapNs_=(std::max)(turnGapNs_,after-turnLastNs_[index]);
            }
        }else if(Moving(ms)&&neutralInput_&&gameplay){
            if(!TurnsCovered())Fail(Failure::Approach);
            if(input.hands[0].stickX!=strafe_||input.hands[0].stickY!=forward_)Fail(Failure::UnexpectedInput);
            neutral.hands[0].stickX=neutral.hands[0].stickY=0;
            if(!movementSamples_)firstMoveMs_=ms;
            if(lastMoveNs_)movementGapNs_=(std::max)(movementGapNs_,after-lastMoveNs_);
            if(movementSegmentLastNs_)movementSegmentGapNs_=(std::max)(movementSegmentGapNs_,after-movementSegmentLastNs_);
            movementSegmentLastNs_=after;
            lastMoveNs_=after;lastMoveMs_=ms;++movementSamples_;
        }else if(ms<ApproachNeutralMs&&gameplay&&!neutralInput_)neutralInput_=input.generation;
        // Stop the movement clock on the first delivered neutral axes. The
        // intentional uncrouch click has its own release proof and neutral tail.
        else if(ms>=MovementStartMs()+approachMs_&&!releaseInput_&&NeutralSceneActions(neutral)){
            releaseInput_=input.generation;releaseMs_=ms;releaseNs_=after;
            if(lastMoveNs_)movementGapNs_=(std::max)(movementGapNs_,after-lastMoveNs_);
            if(movementSegmentLastNs_)movementSegmentGapNs_=(std::max)(movementSegmentGapNs_,after-movementSegmentLastNs_);
        }
        if(!NeutralSceneActions(neutral))Fail(Failure::UnexpectedInput);
        return Receipt::None;
    }
    bool Complete(std::uint64_t elapsed,bool ended)const noexcept {
        elapsed=RecipeMs(elapsed);
        if(!IsApproach())return (!HasMotionRotation()||(!Failed()&&menuEpoch_&&basis_))&&Sequence::Complete(elapsed,ended);
        return !Failed()&&!paused_&&TurnsCovered()&&(!prepared_||PreparationCovered())&&MovementCovered()&&releaseInput_&&
            (!crouch_||crouchReleaseInput_[1])&&elapsed>=AttackStartMs()&&
            Sequence::Complete(elapsed-AttackStartMs()+SequenceOffsetMs(),ended);
    }
    void Report(std::ostream& out,std::uint64_t elapsed,bool ended)const {
        if(!IsApproach()){Sequence::Report(out,elapsed,ended,HasMotionRotation()?(menuEpoch_?Reason():Failure::Menu):Failure::None);return;}
        out<<"{\"schema\":\"fvr.selected_knife_approach_smoke.v1\",\"publication_complete\":"<<(Complete(elapsed,ended)?"true":"false")
            <<",\"duration_ms\":"<<Duration()<<",\"elapsed_ms\":"<<elapsed<<",\"movement_window_ms\":["<<MovementStartMs()<<','<<MovementStartMs()+approachMs_
            <<"],\"left_stick\":["<<strafe_<<','<<forward_<<"],\"attack_start_ms\":"<<AttackStartMs()
            <<",\"movement_samples\":"<<movementSamples_<<",\"first_move_ms\":"<<firstMoveMs_<<",\"last_move_ms\":"<<lastMoveMs_
            <<",\"max_movement_gap_ns\":"<<movementGapNs_<<",\"movement_release_input\":"<<releaseInput_<<",\"movement_release_ms\":"<<releaseMs_
            <<",\"menu_epoch\":"<<menuEpoch_<<",\"failure\":"<<unsigned(Reason());
        if(crouch_)out<<",\"approach_crouch\":true,\"crouch_binding\":\"left_y_click_release\""
            <<",\"crouch_press_inputs\":["<<crouchPressInput_[0]<<','<<crouchPressInput_[1]<<']'
            <<",\"crouch_release_inputs\":["<<crouchReleaseInput_[0]<<','<<crouchReleaseInput_[1]<<']'
            <<",\"crouch_release_ms\":"<<crouchReleaseMs_<<",\"crouch_release_ns\":"<<crouchReleaseNs_;
        ReportTurn(out);
        ReportReview(out);
        ReportResume(out,elapsed);
        ReportPrepared(out,elapsed);
        out<<",\"native_movement_verified\":false,\"native_input_observed\":false,\"damage_verified\":false,\"headset_tested\":false,\"attack\":";
        const auto recipe=RecipeMs(elapsed);
        Sequence::Report(out,prepared_?MotionSequenceMs(elapsed):recipe>=AttackStartMs()?recipe-AttackStartMs():0,ended&&!Failed());out<<'}';
    }
private:
    bool PreparationCovered()const noexcept {
        constexpr std::array<std::uint64_t,3> starts{0,500,1500},ends{500,1500,2000};
        for(unsigned n=0;n<3;++n)if(preparedSamples_[n]<2||preparedFirstMs_[n]>starts[n]+75||
            preparedLastMs_[n]+75<ends[n])return false;
        return preparedGapNs_<=75000000;
    }
    bool PreparedPose(const interaction::InputFrame& input)const noexcept {
        const auto position=MotionPosition(PreparedMovementStartMs);
        const auto rotation=MotionOrientation(PreparedMovementStartMs);
        const auto matches=[&](const math::Pose& p){return std::hypot(p.position.x-position.x,p.position.y-position.y,p.position.z-position.z)<.00001f&&
            std::abs(p.orientation.x-rotation.x)<.00001f&&std::abs(p.orientation.y-rotation.y)<.00001f&&
            std::abs(p.orientation.z-rotation.z)<.00001f&&std::abs(p.orientation.w-rotation.w)<.00001f;};
        return matches(input.hands[1].grip)&&matches(input.hands[1].aim);
    }
    Receipt PublishedPause(const interaction::InputFrame& input,std::uint64_t wall,std::int64_t after,
        bool delivered,bool busy,bool gameplay)noexcept {
        const auto neutral=NeutralSceneInput({},1);
        const auto poseMatches=[](const math::Pose& a,const math::Pose& b){return a.position.x==b.position.x&&
            a.position.y==b.position.y&&a.position.z==b.position.z&&a.orientation.x==b.orientation.x&&
            a.orientation.y==b.orientation.y&&a.orientation.z==b.orientation.z&&a.orientation.w==b.orientation.w;};
        if(!NeutralSceneActions(input))Fail(Failure::UnexpectedInput);
        for(unsigned n=0;n<2;++n)if(!poseMatches(input.hands[n].grip,neutral.hands[n].grip)||
            !poseMatches(input.hands[n].aim,neutral.hands[n].aim))Fail(Failure::UnexpectedInput);
        if(after-pauseStartNs_>=std::int64_t(MaximumPauseMs)*1000000)Fail(Failure::Menu);
        if(!delivered&&!busy)Fail(Failure::Publication);
        if(delivered&&!pauseNeutralInput_){
            pauseNeutralInput_=input.generation;
            if(movementSegmentLastNs_)movementSegmentGapNs_=(std::max)(movementSegmentGapNs_,after-movementSegmentLastNs_);
            if(movementSegmentGapNs_>100000000)Fail(Failure::Approach);
            movementSegmentLastNs_=0;
        }
        if(Failed())return Receipt::None;
        if(!delivered||!gameplay||input.menu.epoch!=menuEpoch_){
            if(rearmFirstNs_)++rearmResets_;
            rearmFirstNs_=rearmLastNs_=0;rearmSamples_=0;return Receipt::None;
        }
        if(rearmLastNs_&&after-rearmLastNs_>100000000){
            ++rearmResets_;rearmFirstNs_=rearmLastNs_=0;rearmSamples_=0;
        }
        if(!rearmFirstNs_)rearmFirstNs_=after;
        rearmLastNs_=after;++rearmSamples_;
        if(after-rearmFirstNs_>=std::int64_t(RearmNeutralMs)*1000000){
            paused_=false;pauseEndMs_=wall;pauseEndNs_=after;pauseDurationMs_=wall-pauseStartMs_;
            // Resumed movement must follow this delivered neutral sample.
            // Startup still has its remaining neutral prefix before movement.
            movementSegmentLastNs_=movementSamples_?after:0;
        }
        return Receipt::None;
    }
    bool TurnWindow(std::uint64_t ms)const noexcept{return turnSnaps_&&ms>=ApproachNeutralMs&&ms<MovementStartMs();}
    bool Turning(std::uint64_t ms)const noexcept{return TurnWindow(ms)&&(ms-ApproachNeutralMs)%TurnCycleMs<TurnPulseMs;}
    bool TurnsCovered()const noexcept {
        for(unsigned n=0;n<TurnCount();++n){const auto begin=ApproachNeutralMs+n*TurnCycleMs;
            if(turnSamples_[n]<2||turnFirstMs_[n]>begin+75||turnLastMs_[n]+75<begin+TurnPulseMs||
               !turnReleaseInput_[n]||turnReleaseMs_[n]>begin+TurnPulseMs+100)return false;
        }
        return turnGapNs_<=100000000;
    }
    bool Moving(std::uint64_t ms)const noexcept{return ms>=MovementStartMs()&&ms<MovementStartMs()+approachMs_;}
    bool MovementCovered()const noexcept{return movementSamples_&&firstMoveMs_<=MovementStartMs()+100&&
        lastMoveMs_+100>=MovementStartMs()+approachMs_&&releaseMs_<=MovementStartMs()+approachMs_+100&&
        (resumeEnabled_?movementSegmentGapNs_:movementGapNs_)<=100000000;}
    static bool Tracked(const interaction::InputFrame& input)noexcept {
        if(!input.focused||!input.headValid)return false;
        for(const auto& hand:input.hands)if(!hand.gripTracked||!hand.aimTracked||(hand.active&interaction::Components)!=interaction::Components)return false;
        return true;
    }
    void Fail(Failure failure)noexcept{if(failure_==Failure::None)failure_=failure;}
    Failure failure_=Failure::None;float strafe_=0,forward_=0;bool crouch_=false;
    std::array<std::uint64_t,2> crouchPressInput_{},crouchReleaseInput_{};
    std::uint64_t crouchReleaseMs_=0;std::int64_t crouchReleaseNs_=0;
    int turnSnaps_=0;std::array<unsigned,6> turnSamples_{};
    bool reviewImages_=false;unsigned reviewPairs_=0;std::uint64_t reviewFinalInput_=0,lastReviewInput_=0;
    std::array<std::uint64_t,6> turnFirstMs_{},turnLastMs_{},turnReleaseInput_{},turnReleaseMs_{};
    std::array<std::int64_t,6> turnLastNs_{};std::int64_t turnGapNs_=0,lastPrefixDeliveredNs_=0;
    std::uint64_t approachMs_=0,attempts_=0,neutralInput_=0,menuEpoch_=0,basis_=0,space_=0,lastInput_=0,lastMs_=0;
    std::uint64_t firstMoveMs_=0,lastMoveMs_=0,releaseInput_=0,releaseMs_=0;unsigned movementSamples_=0;
    std::int64_t lastNs_=0,lastMoveNs_=0,movementGapNs_=0,releaseNs_=0;
    bool resumeEnabled_=false,paused_=false;unsigned pauseCount_=0,rearmSamples_=0,rearmResets_=0;
    std::uint64_t pauseStartMs_=0,pauseEndMs_=0,pauseRecipeMs_=0,pauseDurationMs_=0,lastWallMs_=0,pauseNeutralInput_=0;
    std::int64_t pauseStartNs_=0,pauseEndNs_=0,lastMenuDeadlineNs_=0,rearmFirstNs_=0,rearmLastNs_=0;
    std::int64_t movementSegmentLastNs_=0,movementSegmentGapNs_=0;
    ipc::ChannelResult menuRead_=ipc::ChannelResult::Ok;
    bool prepared_=false;std::array<unsigned,3> preparedSamples_{};
    std::array<std::uint64_t,3> preparedFirstMs_{},preparedLastMs_{};
    std::int64_t preparedLastNs_=0,preparedGapNs_=0;
};
} // namespace fvr::probe
