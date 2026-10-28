#pragma once
#include "fvr/ipc/FrameChannel.h"
#include "fvr/runtime/FrameProvider.h"
#include <chrono>
#include <array>
#include <ostream>
namespace fvr::ipc {
// Historical observation of ChannelResult::Timeout, not proof that its native
// deadline expired: a terminal or replaced slot can return the same result.
struct AsyncRequestTimeout {
    bool observed=false,clockValid=false,queuedAtSubmission=false;
    std::uint64_t requestOrdinal=0,ageNs=0; // Local ordinal, not an IPC request ID.
    std::int64_t startedSteadyNs=0,observedSteadyNs=0;
    unsigned lifetimeMs=0;
    runtime::PresentationRequirements requirements{};
    runtime::TrackingFrame requestedTracking{},currentTracking{};
    void Write(std::ostream& out)const {
        if(!observed){out<<"null";return;}
        out<<"{\"schema\":\"fvr.async-request-timeout.v1\",\"result\":\"channel_timeout\",\"cause\":\"unclassified\""
           <<",\"clock\":\"steady_clock_ns\",\"elapsed_semantics\":\"host_elapsed_since_request_submission_not_gpu_time\""
           <<",\"request_ordinal\":"<<requestOrdinal<<",\"request_ordinal_is_ipc_id\":false"
           <<",\"queued_at_submission\":"<<(queuedAtSubmission?"true":"false")<<",\"lifetime_ms\":"<<lifetimeMs
           <<",\"clock_valid\":"<<(clockValid?"true":"false")<<",\"started_steady_ns\":"<<startedSteadyNs
           <<",\"observed_steady_ns\":"<<observedSteadyNs<<",\"age_ns\":"<<ageNs
           <<",\"requirements\":{\"width\":"<<requirements.width<<",\"height\":"<<requirements.height
           <<",\"format\":"<<requirements.format<<",\"adapter_luid_low\":"<<requirements.adapterLow
           <<",\"adapter_luid_high\":"<<requirements.adapterHigh<<"},\"original_tracking\":";
        const auto tracking=[&](const runtime::TrackingFrame& f){
            out<<"{\"generation\":"<<f.generation<<",\"space\":"<<f.spaceGeneration<<",\"predicted_ns\":"<<f.predictedNs
               <<",\"focused\":"<<(f.focused?"true":"false")<<",\"head_valid\":"<<(f.headValid?"true":"false")<<'}';
        };
        tracking(requestedTracking);out<<",\"current_tracking\":";tracking(currentTracking);out<<'}';
    }
};
struct AsyncDeliveryStats {
    std::uint64_t requested=0,completed=0,timeouts=0;
    std::uint64_t totalLatencyUs=0,maxLatencyUs=0,completedAfter50Ms=0;
    std::uint64_t lookaheadServices=0,lookaheadRequests=0,maxPendingRequests=0;
    AsyncRequestTimeout firstTimeout{};
    struct TimeoutRecord {
        AsyncRequestTimeout request{};
        FrameTimeoutSnapshot channel{};
    };
    static constexpr unsigned TimeoutHistoryCapacity=16;
    std::array<TimeoutRecord,TimeoutHistoryCapacity> timeoutHistory{};
    unsigned timeoutHistoryCount=0;
    std::uint64_t timeoutHistoryOverflow=0;
    void RetainTimeout(const AsyncRequestTimeout& request,const FrameTimeoutSnapshot& channel)noexcept {
        if(!firstTimeout.observed)firstTimeout=request;
        if(timeoutHistoryCount<timeoutHistory.size())timeoutHistory[timeoutHistoryCount++]={request,channel};
        else ++timeoutHistoryOverflow;
    }
    void WriteTimeoutHistory(std::ostream& out)const {
        out<<"{\"schema\":\"fvr.async-timeout-history.v1\",\"capacity\":"<<TimeoutHistoryCapacity
           <<",\"retained\":"<<timeoutHistoryCount<<",\"overflow\":"<<timeoutHistoryOverflow<<",\"records\":[";
        for(unsigned i=0;i<timeoutHistoryCount;++i){
            if(i)out<<',';const auto& row=timeoutHistory[i];const auto& c=row.channel;
            out<<"{\"request\":";row.request.Write(out);
            out<<",\"channel\":{\"available\":"<<(c.available?"true":"false");
            if(!c.available){out<<",\"reason\":\"snapshot_unavailable\"}}";continue;}
            const char* reason="unavailable";
            switch(c.reason){
            case FrameTimeoutReason::QueuedDeadline:reason="queued_deadline_or_invalid_clock";break;
            case FrameTimeoutReason::RequestMissing:reason="request_missing";break;
            case FrameTimeoutReason::SlotReplaced:reason="slot_replaced";break;
            case FrameTimeoutReason::SlotDeadlineOrTerminal:reason="slot_deadline_or_terminal_or_invalid_clock";break;
            default:break;
            }
            out<<",\"wire_version\":"<<c.wireVersion<<",\"reason\":\""<<reason<<"\",\"sample_semantics\":\"under_poll_mutex_before_mutation\""
               <<",\"requested_id\":"<<c.requestedKey.request<<",\"requested_slot\":"<<c.requestedKey.slot
               <<",\"requested_key_reserved\":"<<c.requestedKey.reserved
               <<",\"clock_valid\":"<<(c.clockValid?"true":"false")<<",\"now_qpc\":"<<c.nowQpc<<",\"qpc_frequency\":"<<c.frequencyQpc
               <<",\"request_observed\":"<<(c.requestObserved?"true":"false")<<",\"observed_id\":"<<c.observedRequestId
               <<",\"observed_deadline_qpc\":"<<c.observedDeadlineQpc<<",\"deadline_expired\":"<<(c.deadlineExpired?"true":"false")
               <<",\"phase_valid\":"<<(c.phaseValid?"true":"false")<<",\"phase\":"<<std::uint32_t(c.phase)
               <<",\"terminal_phase\":"<<(c.terminalPhase?"true":"false")<<",\"native_held_request\":"<<c.nativeHeldRequest
               <<",\"has_source\":"<<(c.hasSource?"true":"false")<<",\"retiring\":"<<(c.retiring?"true":"false")
               <<",\"capture_released_request\":"<<c.captureReleasedRequest
               <<",\"queued_request_id\":"<<c.queuedRequestId<<",\"queued_deadline_qpc\":"<<c.queuedDeadlineQpc<<"}}";
        }
        out<<"]}";
    }
};
class RemoteFrameProvider final:public runtime::IFrameProvider {
public:
    bool Create(bool twoSourceSlots=false,bool lookahead=false,bool exportOverlap=false,bool thirdSource=false)noexcept {
        Close();stats_={};
        if(!channel_.CreateHost(twoSourceSlots,lookahead,exportOverlap,thirdSource))return false;
        lookahead_=lookahead;return true;
    }
    bool LookaheadEnabled()const noexcept{return lookahead_;}
    bool ExportOverlapEnabled()const noexcept{return channel_.ExportOverlapEnabled();}
    const std::wstring& Token()const noexcept{return channel_.Token();}
    unsigned SourceSlotCount()const noexcept override{return channel_.SourceSlotCount();}
    bool ReadSourceSlot(const graphics::PairTicket& t,runtime::SourceSlotIdentity& out)noexcept override{return channel_.ReadSourceSlot(t,out);}
    bool Connected()noexcept{return channel_.Connected();}
    void Close()noexcept{channel_.Close();pending_=cancelling_=lookahead_=false;lookaheadPending_={};lookaheadCount_=0;lastIssued_={};}
    ChannelResult FlushFeedback()noexcept{return channel_.FlushFeedback();}
    // Blocking wait budget only. Async rendering spans multiple native frames.
    bool SetBudget(unsigned ms)noexcept{if(!ms||ms>50)return false;budgetMs_=ms;return true;}
    // A bounded 150 ms lifetime permits render + GPU-fence handoff at 60 Hz.
    // Calls still never wait; original pose and 250 ms presentation age are unchanged.
    bool SetRequestLifetime(unsigned ms)noexcept{if(!ms||ms>200)return false;requestLifetimeMs_=ms;return true;}
    unsigned RequestLifetimeMs()const noexcept{return requestLifetimeMs_;}
    const AsyncDeliveryStats& Statistics()const noexcept{return stats_;}
    bool TryGetPair(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& t,
        graphics::TextureDescriptor& d,graphics::PairTicket& p)noexcept override {
        return channel_.RequestPair(r,t,budgetMs_,d,p)==ChannelResult::Ok;
    }
    bool TryGetCompletedPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override;
    bool ReadBodyProps(const graphics::PairTicket& t,graphics::BodyPropFrame& p)noexcept override{return channel_.ReadBodyProps(t,p);}
    bool ReadSceneDepth(const graphics::TextureDescriptor& d,const graphics::PairTicket& t,graphics::SceneDepthFrame& p)noexcept override{return channel_.ReadSceneDepth(d,t,p);}
    void UpdateInput(const interaction::InputFrame& input)noexcept override{channel_.PublishInput(input);}
    runtime::InputPublicationResult UpdateInputObserved(const interaction::InputFrame& input)noexcept override {
        const auto result=channel_.PublishInput(input);
        return {true,result==ChannelResult::Ok,std::uint32_t(result)};
    }
    bool TakeFeedback(interaction::FeedbackEvent& event)noexcept override{return channel_.TakeFeedback(event);}
    void Suspend()noexcept override;
    void ServiceFeedback()noexcept override{channel_.FlushFeedback();}
    void ServiceRequests(const runtime::PresentationRequirements&,const runtime::TrackingFrame&)noexcept override;
    void PairConsumed(const graphics::PairTicket& t,bool consumed)noexcept override{channel_.Feedback(t,consumed);}
private:FrameChannel channel_;unsigned budgetMs_=8,requestLifetimeMs_=150;
    struct PendingRequest {
        runtime::PresentationRequirements requirements{};
        runtime::TrackingFrame tracking{};
        std::chrono::steady_clock::time_point started{};
        std::uint64_t ordinal=0;
        unsigned lifetimeMs=0;
    };
    bool TryGetLookaheadPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept;
    void PopLookahead()noexcept;
    bool lookahead_=false;unsigned lookaheadCount_=0;
    std::array<PendingRequest,2> lookaheadPending_{};
    runtime::TrackingFrame lastIssued_{};
    AsyncDeliveryStats stats_{};std::chrono::steady_clock::time_point pendingStarted_{};
    bool pending_=false,cancelling_=false;
    bool pendingQueued_=false;unsigned pendingLifetimeMs_=0;
    runtime::PresentationRequirements pendingRequirements_{};
    runtime::TrackingFrame pendingTracking_{};
};
// The local provider must outlive this object. Pump only on its graphics thread,
// at a verified render boundary. Never advances native simulation by itself.
class RemoteFrameProducer {
public:
    explicit RemoteFrameProducer(runtime::IFrameProvider& provider):provider_(provider){}
    ~RemoteFrameProducer(){Close();}
    bool Connect(const std::wstring& token)noexcept{Close();return channel_.ConnectProducer(token);}
    bool Connected()noexcept{return channel_.Connected();}
    void Pump()noexcept;
    void Close()noexcept;
private:
    enum class Step{Idle,Skipping,Publishing,Waiting};
    FrameChannel channel_;runtime::IFrameProvider& provider_;Step step_=Step::Idle;
    graphics::TextureDescriptor descriptor_{};graphics::PairTicket ticket_{};
};
}
