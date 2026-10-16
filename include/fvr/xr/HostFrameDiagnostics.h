#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <ostream>

namespace fvr::xr {
enum class HostStage : unsigned {WaitFrame,BeginFrame,LocateViews,LocateHead,Controls,Menu,Input,
    Feedback,Provider,SharedOpen,DepthCopy,ColorCopy,Props,SwapchainAcquire,SwapchainWait,Present,
    SwapchainRelease,EndFrame,Count};
inline constexpr std::array<const char*,unsigned(HostStage::Count)> HostStageNames{
    "wait_frame","begin_frame","locate_views","locate_head","controls","menu","input_publication",
    "feedback","provider","shared_open","depth_copy","color_copy","props","swapchain_acquire",
    "swapchain_wait","present","swapchain_release","end_frame"};
enum HostGapReason : std::uint32_t {HostMenu=1,HostShouldRender=2,HostFocus=4,HostPresence=8,
    HostHead=16,HostViews=32,HostTracking=64,HostSpace=128,HostNoProvider=256,HostNoNewPair=512,
    HostRetainedAge=1024,HostRetainedSpace=2048,HostRetainedFuture=4096,HostRejectedPair=8192,
    HostNoRetained=16384,HostEndFailed=32768};
struct HostFrameObservation {
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
    std::array<std::uint64_t,unsigned(HostStage::Count)> stageNs{};
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
    std::array<std::uint64_t,16> noWorldReasonFrames{};
    std::uint64_t frames=0,noWorldFrames=0,zeroLayerFrames=0,menuOnlyFrames=0,failedFrames=0;
    std::uint64_t inputAttempts=0,inputSuccesses=0,inputFailures=0,inputUnknown=0;
    std::uint64_t maxInputSuccessGapNs=0;
    // Preserve the first observed success-to-success publication gap even if
    // later slow/runtime-loss frames overwrite the diagnostic ring. Historical
    // evidence only: no input/pose deadline or native authority is changed.
    static constexpr std::uint64_t FirstInputGapNs=100000000;
    std::uint64_t firstInputSuccessGapNs=0;
    HostFrameObservation firstInputSuccessGap{};
    std::uint64_t cleanupEndAttempts=0,cleanupZeroLayerFrames=0;
    std::int64_t lastCleanupStartedNs=0,lastCleanupReturnedNs=0;
    std::int32_t lastCleanupResult=0;
    std::uint64_t lastInputSuccessGeneration=0,maxFrameNs=0,maxGapBeforeNs=0,recorded=0,dropped=0;
    std::int64_t lastInputSuccessReturnedNs=0,lastFrameEndedNs=0;
    unsigned count=0,next=0;
    std::uint64_t previousSpace=0;
    std::uint32_t previousReasons=0,previousLayers=0,previousInputResult=0;
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
        const auto elapsed=f.endedNs>=f.startedNs?std::uint64_t(f.endedNs-f.startedNs):0;
        maxFrameNs=(std::max)(maxFrameNs,elapsed);maxGapBeforeNs=(std::max)(maxGapBeforeNs,std::uint64_t(f.gapBeforeNs));
        bool slow=elapsed>=SlowFrameNs||std::uint64_t(f.gapBeforeNs)>=SlowFrameNs;
        for(unsigned i=0;i<f.stageNs.size();++i){maxStageNs[i]=(std::max)(maxStageNs[i],f.stageNs[i]);
            if(f.stageNs[i]>=SlowStageNs)slow=true;else f.stageNs[i]=0;}
        if(!firstInputSuccessGapNs&&f.inputAttempted&&f.inputKnown&&f.inputSucceeded&&
           f.priorInputSuccessGeneration&&f.priorInputSuccessReturnedNs>0&&
           f.inputGeneration==f.lastInputSuccessGeneration&&f.inputReturnedNs==f.lastInputSuccessReturnedNs&&
           f.inputReturnedNs>=f.priorInputSuccessReturnedNs){
            const auto gap=std::uint64_t(f.inputReturnedNs-f.priorInputSuccessReturnedNs);
            if(gap>=FirstInputGapNs){firstInputSuccessGapNs=gap;firstInputSuccessGap=f;}
        }
        if(f.endSucceeded){if(!f.worldLayer){++noWorldFrames;for(unsigned i=0;i<16;++i)if(f.reasons&(1u<<i))++noWorldReasonFrames[i];}
            if(!f.layers)++zeroLayerFrames;if(!f.worldLayer&&f.menuLayers)++menuOnlyFrames;}
        else ++failedFrames;
        const bool changed=!frames||f.reasons!=previousReasons||f.layers!=previousLayers||f.worldLayer!=previousWorld||
            f.inputKnown!=previousInputKnown||f.inputSucceeded!=previousInputSucceeded||f.inputResult!=previousInputResult;
        if(changed||slow||!f.endSucceeded){rows[next]=f;next=(next+1)%Capacity;++recorded;
            if(count<Capacity)++count;else ++dropped;}
        ++frames;previousSpace=f.space;previousReasons=f.reasons;previousLayers=f.layers;previousWorld=f.worldLayer;
        previousInputKnown=f.inputKnown;previousInputSucceeded=f.inputSucceeded;lastFrameEndedNs=f.endedNs;
        previousInputResult=f.inputResult;
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
           <<",\"last_input_success_generation\":"<<lastInputSuccessGeneration<<",\"last_input_success_returned_ns\":"<<lastInputSuccessReturnedNs
           <<",\"reason_bits\":{\"menu\":1,\"should_render_false\":2,\"unfocused\":4,\"absent\":8,\"head\":16,\"views\":32,\"tracking_invalid\":64,\"space_change\":128,\"no_provider\":256,\"no_new_pair\":512,\"retained_age\":1024,\"retained_space\":2048,\"retained_future\":4096,\"pair_rejected\":8192,\"no_retained_pair\":16384,\"end_not_successful\":32768}"
           <<",\"no_world_reason_frames\":[";
        for(unsigned i=0;i<16;++i){if(i)out<<',';out<<noWorldReasonFrames[i];}
        out<<"],\"stage_names\":[";for(unsigned i=0;i<HostStageNames.size();++i){if(i)out<<',';out<<'\"'<<HostStageNames[i]<<'\"';}
        out<<"],\"max_stage_ns\":[";for(unsigned i=0;i<maxStageNs.size();++i){if(i)out<<',';out<<maxStageNs[i];}
        out<<"],\"rows\":[";
        for(unsigned i=0;i<count;++i){if(i)out<<',';WriteFrame(out,Row(i));}
        out<<"],\"first_input_success_gap_ns\":"<<firstInputSuccessGapNs
           <<",\"first_input_success_gap\":";
        if(firstInputSuccessGapNs)WriteFrame(out,firstInputSuccessGap);else out<<"null";
        out<<'}';
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
               <<",\"last_input_success_returned_ns\":"<<f.lastInputSuccessReturnedNs<<",\"slow_stage_ns\":[";
            for(unsigned n=0;n<f.stageNs.size();++n){if(n)out<<',';out<<f.stageNs[n];}out<<"]}";
    }
};
class HostStageTimer {
public:
    using Clock=std::int64_t(*)()noexcept;
    HostStageTimer(HostFrameObservation& f,HostStage s,Clock c)noexcept:frame_(f),stage_(s),clock_(c),start_(c()){}
    ~HostStageTimer(){const auto end=clock_();if(start_>0&&end>=start_)frame_.stageNs[unsigned(stage_)]+=std::uint64_t(end-start_);}
private:HostFrameObservation& frame_;HostStage stage_;Clock clock_;std::int64_t start_;
};
}
