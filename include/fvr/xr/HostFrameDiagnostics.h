#pragma once
#include "fvr/graphics/SharedTextureProtocol.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

namespace fvr::xr {
enum class HostStage : unsigned {WaitFrame,BeginFrame,LocateViews,LocateHead,Controls,Menu,Input,
    Feedback,Provider,SharedOpen,DepthCopy,ColorCopy,Props,SwapchainAcquire,SwapchainWait,Present,
    SwapchainRelease,EndFrame,ColorPoll,Count};
inline constexpr std::array<const char*,unsigned(HostStage::Count)> HostStageNames{
    "wait_frame","begin_frame","locate_views","locate_head","controls","menu","input_publication",
    "feedback","provider","shared_open","depth_copy","color_copy","props","swapchain_acquire",
    "swapchain_wait","present","swapchain_release","end_frame","color_poll"};
static_assert(unsigned(HostStage::Count)<=32);
// Inclusive upper limits; the final bucket has no upper limit. Each sample is
// one frame's total time in a stage, including all its invocations that frame.
inline constexpr std::array<std::uint64_t,14> HostStageHistogramUpperNs{
    10000,50000,100000,250000,500000,1000000,2000000,4000000,
    8000000,16000000,20000000,33000000,50000000,100000000};
inline bool AddHostDiagnosticSaturated(std::uint64_t& total,std::uint64_t value)noexcept {
    constexpr auto maximum=~std::uint64_t{0};
    if(value>maximum-total){total=maximum;return true;}
    total+=value;return false;
}
struct HostStageAggregate {
    std::uint64_t frames=0,totalNs=0;
    std::array<std::uint64_t,HostStageHistogramUpperNs.size()+1> histogram{};
    bool saturated=false;
    void Observe(std::uint64_t elapsed,bool sourceSaturated)noexcept {
        saturated|=sourceSaturated;
        saturated|=AddHostDiagnosticSaturated(frames,1);
        saturated|=AddHostDiagnosticSaturated(totalNs,elapsed);
        const auto bucket=std::size_t(std::lower_bound(HostStageHistogramUpperNs.begin(),HostStageHistogramUpperNs.end(),elapsed)-HostStageHistogramUpperNs.begin());
        saturated|=AddHostDiagnosticSaturated(histogram[bucket],1);
    }
};
enum HostGapReason : std::uint32_t {HostMenu=1,HostShouldRender=2,HostFocus=4,HostPresence=8,
    HostHead=16,HostViews=32,HostTracking=64,HostSpace=128,HostNoProvider=256,HostNoNewPair=512,
    HostRetainedAge=1024,HostRetainedSpace=2048,HostRetainedFuture=4096,HostRejectedPair=8192,
    HostNoRetained=16384,HostEndFailed=32768};
struct HostCopyObservation {
    // Copy phase/result are diagnostic facts, never authority to acknowledge a
    // shared ticket. Result is TransferResult's value; error preserves HRESULT bits.
    bool copySubmitted=false,copyPending=false,copyCompleted=false,copyRetired=false,copyCancelled=false;
    std::uint64_t copySequence=0,copyPendingAgeNs=0;
    // Exact host observations, not GPU execution timestamps. Completion may
    // precede this frame's input publication when serviced before xrWaitFrame.
    std::int64_t copySubmittedNs=0,copyCompletedNs=0;
    bool copyPreWaitPolled=false,copyPreWaitCompleted=false;
    bool copyPostInputPolled=false,copyPostInputCompleted=false;
    // Composed means both XR images were released after queued rendering;
    // these timestamps are CPU observations, not GPU target completion.
    bool copyComposed=false,copyAfterEndPolled=false,copyAfterEndCompleted=false;
    std::int64_t copyComposedNs=0;
    // Owned diagnostic copy of the delivered ticket, never the previously
    // presented image's identity. Sequence alone can repeat after a reset.
    graphics::PairTicket copyTicket{};
    void SetCopyTicket(const graphics::PairTicket& ticket)noexcept {copyTicket=ticket;copySequence=ticket.sequence;}
    std::uint32_t copyResult=0,copyError=0;
};
struct HostFrameObservation : HostCopyObservation {
    std::uint64_t frame=0,wallMs=0,space=0,retainedGeneration=0,retainedSpace=0;
    std::int64_t startedNs=0,endedNs=0,predictedNs=0,retainedPredictedNs=0,retainedAgeNs=0,gapBeforeNs=0;
    std::uint64_t inputGeneration=0,lastInputSuccessGeneration=0;
    std::uint64_t priorInputSuccessGeneration=0;
    std::int64_t inputStartedNs=0,inputReturnedNs=0,lastInputSuccessReturnedNs=0;
    std::int64_t priorInputSuccessReturnedNs=0;
    std::uint32_t inputResult=0,reasons=0,layers=0,menuLayers=0;
    std::int32_t endResult=0;
    bool shouldRender=false,focused=false,present=false,viewsValid=false,headValid=false,trackingValid=false;
    bool menu=false,provider=false,providerQueried=false,newPair=false,pairRejected=false;
    bool retained=false,spaceChanged=false,worldLayer=false,endAttempted=false,endSucceeded=false;
    bool inputAttempted=false,inputKnown=false,inputSucceeded=false;
    bool inputFocused=false,inputHeadValid=false,inputHandsTracked=false;
    // One host frame may complete an already-composed old ticket before Wait
    // or after current input/cancellation, before its one provider admission,
    // and then submit one successor. Preserve both observations independently.
    HostCopyObservation priorCopy{};
    bool hasPriorCopy=false,copyObservationOverflow=false;
    bool PreserveCompletedCopy()noexcept {
        if(!copyTicket.sequence||!copyCompleted||copyPending||!copyComposed)return false;
        if(hasPriorCopy){copyObservationOverflow=true;return false;}
        priorCopy=static_cast<const HostCopyObservation&>(*this);hasPriorCopy=true;
        static_cast<HostCopyObservation&>(*this)=HostCopyObservation{};return true;
    }
    std::array<std::uint64_t,unsigned(HostStage::Count)> stageNs{};
    // Not serialized per frame. Positive direct samples also imply observation;
    // the mask distinguishes a valid zero-duration timer from a skipped stage.
    std::uint32_t stageObservedMask=0,stageSaturatedMask=0;
};
inline std::uint32_t HostReasons(const HostFrameObservation& f)noexcept {
    std::uint32_t reasons=0;
    if(!f.endSucceeded)reasons|=HostEndFailed;
    if(f.worldLayer)return reasons;
    if(f.menu)reasons|=HostMenu;
    if(!f.shouldRender)reasons|=HostShouldRender;
    if(!f.focused)reasons|=HostFocus;
    if(!f.present)reasons|=HostPresence;
    if(f.shouldRender){if(!f.headValid)reasons|=HostHead;if(!f.viewsValid)reasons|=HostViews;
        if(f.headValid&&f.viewsValid&&!f.trackingValid)reasons|=HostTracking;}
    if(f.spaceChanged)reasons|=HostSpace;
    if(!f.provider)reasons|=HostNoProvider;
    if(f.providerQueried&&!f.newPair)reasons|=HostNoNewPair;
    if(f.pairRejected)reasons|=HostRejectedPair;
    if(!f.retained)reasons|=HostNoRetained;
    else {if(f.retainedSpace!=f.space)reasons|=HostRetainedSpace;
        if(f.retainedAgeNs<0)reasons|=HostRetainedFuture;
        if(f.retainedAgeNs>250000000)reasons|=HostRetainedAge;}
    return reasons;
}
// Diagnostics only. All times are host QPC nanoseconds except XR predicted
// times/retained age and wall_ms. Neither input nor render leases are extended.
struct HostFrameDiagnostics {
    static constexpr unsigned Capacity=256;
    static constexpr std::uint64_t SlowStageNs=20000000,SlowFrameNs=50000000;
    std::array<HostFrameObservation,Capacity> rows{};
    std::array<std::uint64_t,unsigned(HostStage::Count)> maxStageNs{};
    std::array<HostStageAggregate,unsigned(HostStage::Count)> stageAggregates{};
    std::array<std::uint64_t,16> noWorldReasonFrames{};
    std::uint64_t frames=0,noWorldFrames=0,zeroLayerFrames=0,menuOnlyFrames=0,failedFrames=0;
    std::uint64_t inputAttempts=0,inputSuccesses=0,inputFailures=0,inputUnknown=0;
    std::uint64_t copySubmittedFrames=0,copyPendingFrames=0,copyCompletedFrames=0,copyRetiredFrames=0,copyCancelledFrames=0;
    std::uint64_t maxCopyPendingAgeNs=0,copyPreWaitPollFrames=0,copyPreWaitCompletedFrames=0;
    std::uint64_t copyCompletedObservations=0,copyDualTicketFrames=0,copyObservationOverflowFrames=0;
    std::uint64_t copyAfterEndPollFrames=0,copyAfterEndCompletedFrames=0;
    std::uint64_t copyPostInputPollFrames=0,copyPostInputCompletedFrames=0;
    std::uint32_t lastCopyResult=0,lastCopyError=0;
    std::uint64_t maxInputSuccessGapNs=0;
    // Preserve the first observed success-to-success publication gap even if
    // later slow/runtime-loss frames overwrite the diagnostic ring. Historical
    // evidence only: no input/pose deadline or native authority is changed.
    static constexpr std::uint64_t FirstInputGapNs=100000000;
    std::uint64_t firstInputSuccessGapNs=0;
    HostFrameObservation firstInputSuccessGap{};
    // Keep the first rejected observation even after its ring row is overwritten.
    // This is historical evidence only; it cannot alter copy/ACK ownership.
    HostFrameObservation firstRejectedFrame{};
    std::uint64_t cleanupEndAttempts=0,cleanupZeroLayerFrames=0;
    std::int64_t lastCleanupStartedNs=0,lastCleanupReturnedNs=0;
    std::int32_t lastCleanupResult=0;
    std::uint64_t lastInputSuccessGeneration=0,maxFrameNs=0,maxGapBeforeNs=0,recorded=0,dropped=0;
    std::int64_t lastInputSuccessReturnedNs=0,lastFrameEndedNs=0;
    unsigned count=0,next=0;
    std::uint64_t previousSpace=0;
    std::uint32_t previousReasons=0,previousLayers=0,previousInputResult=0;
    std::uint32_t previousCopyPhases=0,previousCopyResult=0,previousCopyError=0;
    std::uint64_t previousCopySequence=0;
    graphics::PairTicket previousCopyTicket{};
    bool previousWorld=false,previousInputKnown=false,previousInputSucceeded=false;
    void CleanupEnd(std::int64_t start,std::int64_t returned,std::int32_t result,bool succeeded)noexcept {
        ++cleanupEndAttempts;if(succeeded)++cleanupZeroLayerFrames;
        lastCleanupStartedNs=start;lastCleanupReturnedNs=returned;lastCleanupResult=result;
    }
    void Input(std::uint64_t generation,std::int64_t returnedNs,bool known,bool success)noexcept {
        ++inputAttempts;
        if(!known){++inputUnknown;return;}
        if(!success){++inputFailures;return;}
        if(lastInputSuccessReturnedNs>0&&returnedNs>=lastInputSuccessReturnedNs)
            maxInputSuccessGapNs=(std::max)(maxInputSuccessGapNs,std::uint64_t(returnedNs-lastInputSuccessReturnedNs));
        ++inputSuccesses;lastInputSuccessGeneration=generation;lastInputSuccessReturnedNs=returnedNs;
    }
    void Observe(HostFrameObservation f)noexcept {
        f.spaceChanged|=previousSpace&&previousSpace!=f.space;
        if(lastFrameEndedNs>0&&f.startedNs>=lastFrameEndedNs)f.gapBeforeNs=f.startedNs-lastFrameEndedNs;
        f.lastInputSuccessGeneration=lastInputSuccessGeneration;f.lastInputSuccessReturnedNs=lastInputSuccessReturnedNs;
        f.reasons=HostReasons(f);
        const std::uint32_t copyPhases=std::uint32_t(f.copySubmitted)|(std::uint32_t(f.copyPending)<<1)|
            (std::uint32_t(f.copyCompleted)<<2)|(std::uint32_t(f.copyRetired)<<3)|(std::uint32_t(f.copyCancelled)<<4)|
            (std::uint32_t(f.copyPreWaitPolled)<<5)|(std::uint32_t(f.copyPreWaitCompleted)<<6)|
            (std::uint32_t(f.copyComposed)<<7)|(std::uint32_t(f.copyAfterEndPolled)<<8)|
            (std::uint32_t(f.copyAfterEndCompleted)<<9)|(std::uint32_t(f.copyPostInputPolled)<<10)|
            (std::uint32_t(f.copyPostInputCompleted)<<11);
        const bool copyObserved=copyPhases||f.copySequence;
        const auto& prior=f.priorCopy;
        if(f.copySubmitted||(f.hasPriorCopy&&prior.copySubmitted))++copySubmittedFrames;
        if(f.copyPending||(f.hasPriorCopy&&prior.copyPending))++copyPendingFrames;
        if(f.copyCompleted||(f.hasPriorCopy&&prior.copyCompleted))++copyCompletedFrames;
        if(f.copyRetired||(f.hasPriorCopy&&prior.copyRetired))++copyRetiredFrames;
        if(f.copyCancelled||(f.hasPriorCopy&&prior.copyCancelled))++copyCancelledFrames;
        if(f.copyPreWaitPolled||(f.hasPriorCopy&&prior.copyPreWaitPolled))++copyPreWaitPollFrames;
        if(f.copyPreWaitCompleted||(f.hasPriorCopy&&prior.copyPreWaitCompleted))++copyPreWaitCompletedFrames;
        if(f.copyAfterEndPolled||(f.hasPriorCopy&&prior.copyAfterEndPolled))++copyAfterEndPollFrames;
        if(f.copyAfterEndCompleted||(f.hasPriorCopy&&prior.copyAfterEndCompleted))++copyAfterEndCompletedFrames;
        if(f.copyPostInputPolled||(f.hasPriorCopy&&prior.copyPostInputPolled))++copyPostInputPollFrames;
        if(f.copyPostInputCompleted||(f.hasPriorCopy&&prior.copyPostInputCompleted))++copyPostInputCompletedFrames;
        copyCompletedObservations+=unsigned(f.copyCompleted)+unsigned(f.hasPriorCopy&&prior.copyCompleted);
        if(f.hasPriorCopy&&copyObserved)++copyDualTicketFrames;
        if(f.copyObservationOverflow)++copyObservationOverflowFrames;
        if(f.hasPriorCopy){maxCopyPendingAgeNs=(std::max)(maxCopyPendingAgeNs,prior.copyPendingAgeNs);
            lastCopyResult=prior.copyResult;lastCopyError=prior.copyError;}
        if(copyObserved){maxCopyPendingAgeNs=(std::max)(maxCopyPendingAgeNs,f.copyPendingAgeNs);
            lastCopyResult=f.copyResult;lastCopyError=f.copyError;}
        // Pending age grows each tick; only phase/identity/result changes merit
        // a row. Aggregates still cover every frame, including an unchanged wait.
        const bool ticketChanged=f.copyTicket.resourceEpoch!=previousCopyTicket.resourceEpoch||
            f.copyTicket.sequence!=previousCopyTicket.sequence||f.copyTicket.frameId!=previousCopyTicket.frameId||
            f.copyTicket.trackingGeneration!=previousCopyTicket.trackingGeneration||
            f.copyTicket.spaceGeneration!=previousCopyTicket.spaceGeneration||
            f.copyTicket.predictedNs!=previousCopyTicket.predictedNs||f.copyTicket.session!=previousCopyTicket.session;
        const bool copyChanged=f.hasPriorCopy||f.copyObservationOverflow||copyPhases!=previousCopyPhases||f.copySequence!=previousCopySequence||ticketChanged||
            (copyObserved&&(f.copyResult!=previousCopyResult||f.copyError!=previousCopyError));
        const auto elapsed=f.endedNs>=f.startedNs?std::uint64_t(f.endedNs-f.startedNs):0;
        maxFrameNs=(std::max)(maxFrameNs,elapsed);maxGapBeforeNs=(std::max)(maxGapBeforeNs,std::uint64_t(f.gapBeforeNs));
        bool slow=elapsed>=SlowFrameNs||std::uint64_t(f.gapBeforeNs)>=SlowFrameNs;
        for(unsigned i=0;i<f.stageNs.size();++i){maxStageNs[i]=(std::max)(maxStageNs[i],f.stageNs[i]);
            if(f.stageNs[i]||(f.stageObservedMask&(1u<<i)))
                stageAggregates[i].Observe(f.stageNs[i],(f.stageSaturatedMask&(1u<<i))!=0);
            if(f.stageNs[i]>=SlowStageNs)slow=true;else f.stageNs[i]=0;}
        if(!firstInputSuccessGapNs&&f.inputAttempted&&f.inputKnown&&f.inputSucceeded&&
           f.priorInputSuccessGeneration&&f.priorInputSuccessReturnedNs>0&&
           f.inputGeneration==f.lastInputSuccessGeneration&&f.inputReturnedNs==f.lastInputSuccessReturnedNs&&
           f.inputReturnedNs>=f.priorInputSuccessReturnedNs){
            const auto gap=std::uint64_t(f.inputReturnedNs-f.priorInputSuccessReturnedNs);
            if(gap>=FirstInputGapNs){firstInputSuccessGapNs=gap;firstInputSuccessGap=f;}
        }
        if(f.pairRejected&&!firstRejectedFrame.pairRejected)firstRejectedFrame=f;
        if(f.endSucceeded){if(!f.worldLayer){++noWorldFrames;for(unsigned i=0;i<16;++i)if(f.reasons&(1u<<i))++noWorldReasonFrames[i];}
            if(!f.layers)++zeroLayerFrames;if(!f.worldLayer&&f.menuLayers)++menuOnlyFrames;}
        else ++failedFrames;
        const bool changed=!frames||f.reasons!=previousReasons||f.layers!=previousLayers||f.worldLayer!=previousWorld||
            f.inputKnown!=previousInputKnown||f.inputSucceeded!=previousInputSucceeded||f.inputResult!=previousInputResult||copyChanged;
        if(changed||slow||!f.endSucceeded){rows[next]=f;next=(next+1)%Capacity;++recorded;
            if(count<Capacity)++count;else ++dropped;}
        ++frames;previousSpace=f.space;previousReasons=f.reasons;previousLayers=f.layers;previousWorld=f.worldLayer;
        previousInputKnown=f.inputKnown;previousInputSucceeded=f.inputSucceeded;lastFrameEndedNs=f.endedNs;
        previousInputResult=f.inputResult;
        previousCopyPhases=copyPhases;previousCopySequence=f.copySequence;
        previousCopyTicket=f.copyTicket;
        if(copyObserved){previousCopyResult=f.copyResult;previousCopyError=f.copyError;}
    }
    const HostFrameObservation& Row(unsigned i)const noexcept{return rows[(count==Capacity?next+i:i)%Capacity];}
    void Write(std::ostream& out)const {
        out<<"{\"schema\":1,\"capacity\":"<<Capacity<<",\"slow_stage_ns\":"<<SlowStageNs<<",\"slow_frame_ns\":"<<SlowFrameNs
           <<",\"clock\":\"host_qpc_ns; predicted/retained age are xr_time\",\"duration_kind\":\"host_elapsed_not_gpu_execution\""
           <<",\"frames\":"<<frames<<",\"no_world_frames\":"<<noWorldFrames<<",\"zero_layer_frames\":"<<(zeroLayerFrames+cleanupZeroLayerFrames)
           <<",\"normal_zero_layer_frames\":"<<zeroLayerFrames<<",\"cleanup_end_attempts\":"<<cleanupEndAttempts
           <<",\"cleanup_zero_layer_frames\":"<<cleanupZeroLayerFrames<<",\"last_cleanup_started_ns\":"<<lastCleanupStartedNs
           <<",\"last_cleanup_returned_ns\":"<<lastCleanupReturnedNs<<",\"last_cleanup_result\":"<<lastCleanupResult
           <<",\"menu_only_frames\":"<<menuOnlyFrames<<",\"failed_frames\":"<<failedFrames<<",\"recorded\":"<<recorded<<",\"dropped\":"<<dropped
           <<",\"max_frame_ns\":"<<maxFrameNs<<",\"max_gap_before_ns\":"<<maxGapBeforeNs<<",\"input_attempts\":"<<inputAttempts
           <<",\"input_successes\":"<<inputSuccesses<<",\"input_failures\":"<<inputFailures<<",\"input_unknown\":"<<inputUnknown
           <<",\"max_input_success_gap_ns\":"<<maxInputSuccessGapNs
           <<",\"copy_submitted_frames\":"<<copySubmittedFrames<<",\"copy_pending_frames\":"<<copyPendingFrames
           <<",\"copy_completed_frames\":"<<copyCompletedFrames<<",\"copy_retired_frames\":"<<copyRetiredFrames
           <<",\"copy_cancelled_frames\":"<<copyCancelledFrames<<",\"max_copy_pending_age_ns\":"<<maxCopyPendingAgeNs
           <<",\"copy_pre_wait_poll_frames\":"<<copyPreWaitPollFrames<<",\"copy_pre_wait_completed_frames\":"<<copyPreWaitCompletedFrames
           <<",\"copy_post_input_poll_frames\":"<<copyPostInputPollFrames<<",\"copy_post_input_completed_frames\":"<<copyPostInputCompletedFrames
           <<",\"copy_completed_observations\":"<<copyCompletedObservations
           <<",\"copy_dual_ticket_frames\":"<<copyDualTicketFrames<<",\"copy_observation_overflow_frames\":"<<copyObservationOverflowFrames
           <<",\"copy_after_end_poll_frames\":"<<copyAfterEndPollFrames<<",\"copy_after_end_completed_frames\":"<<copyAfterEndCompletedFrames
           <<",\"last_copy_result\":"<<lastCopyResult<<",\"last_copy_error\":"<<lastCopyError
           <<",\"last_input_success_generation\":"<<lastInputSuccessGeneration<<",\"last_input_success_returned_ns\":"<<lastInputSuccessReturnedNs
           <<",\"reason_bits\":{\"menu\":1,\"should_render_false\":2,\"unfocused\":4,\"absent\":8,\"head\":16,\"views\":32,\"tracking_invalid\":64,\"space_change\":128,\"no_provider\":256,\"no_new_pair\":512,\"retained_age\":1024,\"retained_space\":2048,\"retained_future\":4096,\"pair_rejected\":8192,\"no_retained_pair\":16384,\"end_not_successful\":32768}"
           <<",\"no_world_reason_frames\":[";
        for(unsigned i=0;i<16;++i){if(i)out<<',';out<<noWorldReasonFrames[i];}
        out<<"],\"stage_names\":[";for(unsigned i=0;i<HostStageNames.size();++i){if(i)out<<',';out<<'\"'<<HostStageNames[i]<<'\"';}
        out<<"],\"max_stage_ns\":[";for(unsigned i=0;i<maxStageNs.size();++i){if(i)out<<',';out<<maxStageNs[i];}
        out<<"],\"stage_aggregate_kind\":\"per_observed_frame_elapsed_sum\",\"stage_histogram_upper_ns\":[";
        for(unsigned i=0;i<HostStageHistogramUpperNs.size();++i){if(i)out<<',';out<<HostStageHistogramUpperNs[i];}
        out<<"],\"stage_histogram_last_bucket\":\"above_last_upper_limit\",\"stage_aggregates\":[";
        for(unsigned i=0;i<stageAggregates.size();++i){if(i)out<<',';const auto& aggregate=stageAggregates[i];
            out<<"{\"frames\":"<<aggregate.frames<<",\"total_ns\":"<<aggregate.totalNs
               <<",\"saturated\":"<<(aggregate.saturated?"true":"false")<<",\"histogram\":[";
            for(unsigned bucket=0;bucket<aggregate.histogram.size();++bucket){if(bucket)out<<',';out<<aggregate.histogram[bucket];}
            out<<"]}";}
        out<<"],\"rows\":[";
        for(unsigned i=0;i<count;++i){if(i)out<<',';WriteFrame(out,Row(i));}
        out<<"],\"first_rejected_frame\":";
        if(firstRejectedFrame.pairRejected)WriteFrame(out,firstRejectedFrame);else out<<"null";
        out<<",\"first_input_success_gap_ns\":"<<firstInputSuccessGapNs
           <<",\"first_input_success_gap\":";
        if(firstInputSuccessGapNs)WriteFrame(out,firstInputSuccessGap);else out<<"null";
        out<<'}';
    }
    static void WriteCopy(std::ostream& out,const HostCopyObservation& f){
            out<<"\"copy_submitted\":"<<f.copySubmitted<<",\"copy_pending\":"<<f.copyPending
               <<",\"copy_completed\":"<<f.copyCompleted<<",\"copy_retired\":"<<f.copyRetired<<",\"copy_cancelled\":"<<f.copyCancelled
               <<",\"copy_sequence\":"<<f.copySequence<<",\"copy_ticket\":";
            if(f.copyTicket.sequence){const auto& t=f.copyTicket;
                out<<"{\"resource_epoch\":"<<t.resourceEpoch<<",\"sequence\":"<<t.sequence
                   <<",\"frame\":"<<t.frameId<<",\"tracking\":"<<t.trackingGeneration<<",\"space\":"<<t.spaceGeneration
                   <<",\"predicted_ns\":"<<t.predictedNs<<",\"session\":[";
                for(unsigned n=0;n<t.session.size();++n){if(n)out<<',';out<<unsigned(t.session[n]);}
                out<<"]}";
            }else out<<"null";
            out<<",\"copy_submitted_ns\":"<<f.copySubmittedNs<<",\"copy_completed_ns\":"<<f.copyCompletedNs
               <<",\"copy_pre_wait_polled\":"<<f.copyPreWaitPolled<<",\"copy_pre_wait_completed\":"<<f.copyPreWaitCompleted
               <<",\"copy_post_input_polled\":"<<f.copyPostInputPolled<<",\"copy_post_input_completed\":"<<f.copyPostInputCompleted
               <<",\"copy_pending_age_ns\":"<<f.copyPendingAgeNs
               <<",\"copy_result\":"<<f.copyResult<<",\"copy_error\":"<<f.copyError
               <<",\"copy_composed\":"<<f.copyComposed<<",\"copy_composed_ns\":"<<f.copyComposedNs
               <<",\"copy_after_end_polled\":"<<f.copyAfterEndPolled<<",\"copy_after_end_completed\":"<<f.copyAfterEndCompleted;
    }
    static void WriteFrame(std::ostream& out,const HostFrameObservation& f){
            out<<"{\"frame\":"<<f.frame<<",\"wall_ms\":"<<f.wallMs<<",\"started_ns\":"<<f.startedNs<<",\"ended_ns\":"<<f.endedNs
               <<",\"gap_before_ns\":"<<f.gapBeforeNs<<",\"predicted_ns\":"<<f.predictedNs<<",\"space\":"<<f.space<<",\"reasons\":"<<f.reasons
               <<",\"world_layer\":"<<f.worldLayer<<",\"layer_count\":"<<f.layers<<",\"menu_layers\":"<<f.menuLayers
               <<",\"end_attempted\":"<<f.endAttempted<<",\"end_succeeded\":"<<f.endSucceeded<<",\"end_result\":"<<f.endResult
               <<",\"should_render\":"<<f.shouldRender<<",\"focused\":"<<f.focused<<",\"present\":"<<f.present
               <<",\"head_valid\":"<<f.headValid<<",\"views_valid\":"<<f.viewsValid<<",\"tracking_valid\":"<<f.trackingValid
               <<",\"menu\":"<<f.menu<<",\"provider_queried\":"<<f.providerQueried<<",\"new_pair\":"<<f.newPair<<",\"pair_rejected\":"<<f.pairRejected
               <<",\"retained\":"<<f.retained<<",\"retained_generation\":"<<f.retainedGeneration<<",\"retained_space\":"<<f.retainedSpace
               <<",\"retained_predicted_ns\":"<<f.retainedPredictedNs<<",\"retained_age_ns\":"<<f.retainedAgeNs
               <<",\"input_generation\":"<<f.inputGeneration<<",\"input_started_ns\":"<<f.inputStartedNs<<",\"input_returned_ns\":"<<f.inputReturnedNs
               <<",\"input_attempted\":"<<f.inputAttempted<<",\"input_known\":"<<f.inputKnown<<",\"input_succeeded\":"<<f.inputSucceeded
               <<",\"input_focused\":"<<f.inputFocused<<",\"input_head_valid\":"<<f.inputHeadValid<<",\"input_hands_tracked\":"<<f.inputHandsTracked
               <<",\"input_result\":"<<f.inputResult<<",\"last_input_success_generation\":"<<f.lastInputSuccessGeneration
               <<",\"prior_input_success_generation\":"<<f.priorInputSuccessGeneration<<",\"prior_input_success_returned_ns\":"<<f.priorInputSuccessReturnedNs
               <<",\"last_input_success_returned_ns\":"<<f.lastInputSuccessReturnedNs;
            out<<',';WriteCopy(out,f);
            out<<",\"prior_copy\":";
            if(f.hasPriorCopy){out<<'{';WriteCopy(out,f.priorCopy);out<<'}';}else out<<"null";
            out<<",\"copy_observation_overflow\":"<<f.copyObservationOverflow<<",\"slow_stage_ns\":[";
            for(unsigned n=0;n<f.stageNs.size();++n){if(n)out<<',';out<<f.stageNs[n];}out<<"]}";
    }
};
class HostStageTimer {
public:
    using Clock=std::int64_t(*)()noexcept;
    HostStageTimer(HostFrameObservation& f,HostStage s,Clock c)noexcept:frame_(f),stage_(s),clock_(c),start_(c()){}
    ~HostStageTimer(){const auto end=clock_();if(start_>0&&end>=start_){const auto index=unsigned(stage_);
        frame_.stageObservedMask|=1u<<index;
        if(AddHostDiagnosticSaturated(frame_.stageNs[index],std::uint64_t(end-start_)))frame_.stageSaturatedMask|=1u<<index;}}
private:HostFrameObservation& frame_;HostStage stage_;Clock clock_;std::int64_t start_;
};
}
