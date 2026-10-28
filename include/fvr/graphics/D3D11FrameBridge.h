#pragma once
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11LegacyRelay.h"
#include "fvr/graphics/D3D11SceneDepthPair.h"
#include "fvr/graphics/D3D11DesktopMirror.h"
#include "fvr/graphics/FrameBridgeDiagnostics.h"
#include "fvr/graphics/D3D11RenderQueueLimit.h"
#include "fvr/ipc/StagedFrameProducer.h"
#include <wrl/client.h>
#include <atomic>
#include <ostream>
namespace fvr::graphics {
enum class FrameSharingPreference {Automatic,LegacyRelay,LegacyIpc};
enum class SourceDimensionPolicy {Fatal,DiscardPair};
// Per-call observation only. Accepted means the existing path accepted the
// restored pair, not that IPC publication or GPU completion has occurred.
enum class RestoredPublicationResult {NotAttempted,Accepted,PrepareBusy,PrepareInvalid,
    WrongGraphicsOwner,CaptureStateRejected,ExportRejected};
inline const char* RestoredPublicationResultName(RestoredPublicationResult result)noexcept {
    switch(result){
    case RestoredPublicationResult::NotAttempted:return "not_attempted";
    case RestoredPublicationResult::Accepted:return "accepted";
    case RestoredPublicationResult::PrepareBusy:return "prepare_busy";
    case RestoredPublicationResult::PrepareInvalid:return "prepare_invalid";
    case RestoredPublicationResult::WrongGraphicsOwner:return "wrong_graphics_owner";
    case RestoredPublicationResult::CaptureStateRejected:return "capture_state_rejected";
    case RestoredPublicationResult::ExportRejected:return "export_rejected";
    }return "unknown";
}
// Local identity of one restored pair, including the request's original pose
// generations. This is not a wire ticket and never identifies a queued successor.
struct RestoredPairLease {
    std::uint64_t channelGeneration=0,requestId=0;
    ipc::NativeFrameKey native{};
    std::uint64_t trackingGeneration=0,spaceGeneration=0;
    std::int64_t predictedNs=0;
    bool operator==(const RestoredPairLease&)const noexcept=default;
};
struct PipelineGraphicsSnapshot {
    static constexpr unsigned Capacity=3;
    struct Publication {
        bool present=false;FrameBridgeTimingKey key{};PairTicket ticket{};
        TextureDescriptor color{};SceneDepthFrame depth{};std::int64_t observedQpc=0;
    };
    struct Slot {
        std::uint64_t generations=0,eyeCopies=0,depthCopies=0,exports=0,submissions=0,consumed=0,discarded=0,cancellations=0;
        Publication lastPublication{};
    };
    std::array<Slot,Capacity> slots{};
    unsigned sourceSlots=2;
    unsigned LogicalTextureCount()const noexcept{return 3+6*sourceSlots;}
    std::uint64_t LogicalByteLimit()const noexcept{return (sourceSlots==3?768ull:512ull)*1024*1024;}
    bool configured=false,available=false,allocationAdmitted=false,budgetAvailable=false,exportOverlap=false;
    std::uint64_t logicalReservationBytes=0,budgetBytes=0,usageBytes=0,budgetChecks=0,graphicsDeferrals=0,rejectedFeedback=0;
    std::uint64_t captureReleaseAttempts=0,captureReleases=0,captureReleaseBusy=0,captureReleaseDeclined=0;
    void Write(std::ostream& out)const {
        out<<"{\"schema\":\""<<(sourceSlots==3?"fvr.native-three-source-slots.v1":"fvr.native-two-source-slots.v1")
           <<"\",\"configured\":"<<(configured?"true":"false")
           <<",\"available\":"<<(available?"true":"false")<<",\"allocation_admitted\":"<<(allocationAdmitted?"true":"false")
           <<",\"source_slots\":"<<sourceSlots<<",\"logical_texture_count\":"<<LogicalTextureCount()
           <<",\"logical_byte_limit\":"<<LogicalByteLimit()<<",\"logical_reservation_bytes\":"<<logicalReservationBytes
           <<",\"physical_allocation_measured\":false,\"local_budget_available\":"<<(budgetAvailable?"true":"false")
           <<",\"local_budget_bytes\":"<<budgetBytes<<",\"local_usage_before_allocation_bytes\":"<<usageBytes<<",\"local_budget_checks\":"<<budgetChecks
           <<",\"required_budget_headroom_bytes\":67108864,\"graphics_deferrals\":"<<graphicsDeferrals
           <<",\"rejected_feedback\":"<<rejectedFeedback<<",\"export_overlap\":"<<(exportOverlap?"true":"false")
           <<",\"capture_release_attempts\":"<<captureReleaseAttempts<<",\"capture_releases\":"<<captureReleases
           <<",\"capture_release_busy\":"<<captureReleaseBusy<<",\"capture_release_declined\":"<<captureReleaseDeclined<<",\"slots\":[";
        for(unsigned i=0;i<sourceSlots&&i<slots.size();++i){const auto& s=slots[i];if(i)out<<',';
            out<<"{\"slot\":"<<i<<",\"generations\":"<<s.generations<<",\"eye_copies\":"<<s.eyeCopies
               <<",\"depth_copies\":"<<s.depthCopies<<",\"exports\":"<<s.exports<<",\"submissions\":"<<s.submissions
               <<",\"consumed\":"<<s.consumed<<",\"discarded\":"<<s.discarded<<",\"cancellations\":"<<s.cancellations
               <<",\"last_publication\":";
            const auto& p=s.lastPublication;if(!p.present){out<<"null}";continue;}
            const auto& k=p.key;const auto& t=p.ticket;const auto ids=LegacyResourceIds(p.color);
            out<<"{\"observed_qpc\":"<<p.observedQpc<<",\"channel\":"<<k.channel<<",\"request\":"<<k.request
               <<",\"owner\":"<<k.owner<<",\"device\":"<<k.device<<",\"frame\":"<<k.frame<<",\"tracking\":"<<k.tracking
               <<",\"space\":"<<k.space<<",\"predicted_ns\":"<<k.predictedNs
               <<",\"ticket\":{\"resource_epoch\":"<<t.resourceEpoch<<",\"sequence\":"<<t.sequence<<",\"frame\":"<<t.frameId
               <<",\"tracking\":"<<t.trackingGeneration<<",\"space\":"<<t.spaceGeneration<<",\"predicted_ns\":"<<t.predictedNs<<",\"session\":[";
            for(unsigned n=0;n<t.session.size();++n){if(n)out<<',';out<<unsigned(t.session[n]);}
            out<<"]},\"width\":"<<p.color.width<<",\"height\":"<<p.color.height<<",\"color_format\":"<<p.color.format
               <<",\"adapter_low\":"<<p.color.adapterLow<<",\"adapter_high\":"<<p.color.adapterHigh
               <<",\"depth_present\":"<<(!SceneDepthAbsent(p.depth)?"true":"false")
               <<",\"color_resources\":["<<ids[0]<<','<<ids[1]<<"],\"depth_samples\":"<<p.depth.samples
               <<",\"depth_quality\":"<<p.depth.quality<<",\"depth_format\":"<<p.depth.format
               <<",\"depth_resource_generation\":"<<p.depth.resourceGeneration<<",\"depth_resources\":[";
            for(unsigned eye=0;eye<2;++eye)for(unsigned stage=0;stage<2;++stage){if(eye||stage)out<<',';out<<p.depth.resources[eye][stage];}
            out<<"],\"scope\":\"last_successful_ipc_publication_only\"}}";}
        out<<"]}";
    }
};

// Reusable native-renderer transport: no game layout, hooks, or XR SDK types.
// All GPU/setup calls belong to the original immediate-context thread.
class D3D11FrameBridge final:public runtime::IFrameProvider {
public:
    D3D11FrameBridge():channel_(*this,ObservePublication,this){}
    ~D3D11FrameBridge(){CloseGraphics();}
    bool ConnectGraphics(ID3D11Device*,const std::wstring&,FrameSharingPreference preference=FrameSharingPreference::Automatic)noexcept;
    bool TryBegin(const ipc::NativeFrameKey&,ipc::FrameLease&)noexcept;
    ipc::ChannelResult ReadInput(interaction::InputFrame& out,std::int64_t* deadlineQpc=nullptr)noexcept{return channel_.ReadInput(out,deadlineQpc);}
    bool PublishFeedback(const interaction::FeedbackEvent& event)noexcept{return channel_.PublishFeedback(event);}
    // DiscardPair only tolerates width/height changes. False still requires the
    // caller to restore native state and cancel this unsubmitted lease.
    bool CaptureEye(unsigned,ID3D11Texture2D*,const ipc::FrameLease&,SourceDimensionPolicy=SourceDimensionPolicy::Fatal)noexcept;
    // Optional local admission reason; the existing prepare call still owns
    // validation. Only PrepareBusy permits deferral before the required copy.
    bool BeginDepthCapture(const ipc::FrameLease&,bool reversedDepth,RestoredPublicationResult* observation=nullptr)noexcept;
    bool CaptureDepth(unsigned eye,unsigned stage,ID3D11Texture2D*,const ipc::FrameLease&)noexcept;
    ipc::ChannelResult SetBodyProps(const ipc::FrameLease& lease,unsigned eye,const BodyPropEye& props)noexcept {
        return channel_.SetBodyProps(lease,eye,props);
    }
    ipc::ChannelResult SetBodyPropPair(const ipc::FrameLease& lease,const std::array<BodyPropEye,2>& props)noexcept {
        return channel_.SetBodyPropPair(lease,props);
    }
    bool PublishRestored(const ipc::FrameLease&,RestoredPublicationResult* observation=nullptr)noexcept;
    bool PresentDesktop(IDXGISwapChain* chain,bool active,bool menuKnown,bool menuVisible,bool test)noexcept {
        return desktopMirror_.Present(chain,active,menuKnown,menuVisible,test);
    }
    const D3D11DesktopMirror& DesktopMirror()const noexcept{return desktopMirror_;}
    void Cancel(const ipc::FrameLease&)noexcept;
    void PumpGraphics()noexcept;
    // Explicit verified scene replacement, on the original graphics owner.
    // Busy preserves all old ownership and may be retried by that transition;
    // Invalid/Closed never reconnects or clears a fault. The old key's frame is
    // not an identity component. Device epoch and presentation requirements
    // remain unchanged; retired source sets get fresh epochs before frame 1.
    ipc::ChannelResult TransitionWorldOwnerGraphics(const ipc::NativeFrameKey& expectedOld,
        const ipc::NativeFrameKey& replacement)noexcept;
    // Original graphics owner only, inside the caller's callback lifetime.
    // Read-only, no locks/COM calls/IPC promotion. Mode 3 only. False after
    // consumption OR discard/cancel/failure/close, even if a successor is queued.
    bool TryGetRestoredPairLease(RestoredPairLease& out)const noexcept;
    bool SameRestoredPairPending(const RestoredPairLease&)const noexcept;
    void CloseGraphics()noexcept;
    std::uint64_t Published()const noexcept{return published_.load();}
    std::uint64_t Consumed()const noexcept{return consumed_.load();}
    std::uint64_t Discarded()const noexcept{return discarded_.load();}
    unsigned FailureStage()const noexcept{return failureStage_.load();}
    long FailureCode()const noexcept{return failureCode_.load();}
    std::uint64_t CapturedEyes()const noexcept{return capturedEyes_.load();}
    long Compatibility(unsigned index)const noexcept{return compatibility_[index].load();}
    unsigned DeviceFlags()const noexcept{return deviceFlags_.load();}
    unsigned FeatureLevel()const noexcept{return featureLevel_.load();}
    unsigned FailureOperation()const noexcept{return failureOperation_.load();}
    unsigned SharingMode()const noexcept{return sharingMode_.load();}
    bool Fatal()const noexcept{const auto stage=failureStage_.load();return (stage>=2&&stage<=5)||stage>=10;}
    bool Connected()const noexcept{return connected_.load(std::memory_order_acquire);}
    // Atomic identity query only; safe for callbacks that do not own graphics.
    bool OnGraphicsThread()const noexcept{return GetCurrentThreadId()==graphicsThread_.load(std::memory_order_acquire);}
    // Read only on the original graphics owner thread, under the caller's
    // callback lifetime. No COM calls or ownership transfer are performed.
    bool MatchesGraphicsOwner(ID3D11Device* device,ID3D11DeviceContext* context)const noexcept {
        return device&&context&&device_.Get()==device&&context_.Get()==context;
    }
    bool TimingSnapshot(FrameBridgeTimingSnapshot& out)const noexcept {
        if(pipelineEnabled_){out={};return false;}return timing_.Snapshot(out);
    }
    bool TwoSourceSlotsEnabled()const noexcept{return pipelineEnabled_;}
    unsigned SourceSlotCount()const noexcept override{return pipelineSourceCount_;}
    bool SlotTimingSnapshot(unsigned slot,FrameBridgeTimingSnapshot& out)const noexcept {
        if(!pipelineEnabled_||slot>=pipelineSourceCount_){out={};return false;}return pipelineTiming_[slot].Snapshot(out);
    }
    PipelineGraphicsSnapshot PipelineSnapshot(bool callbacksDrained)const noexcept;
    const RenderQueueSnapshot& RenderQueueSnapshotValue()const noexcept{return renderQueue_.Snapshot();}
    bool EarlyFrameAdmissionEnabled()const noexcept{return earlyFrameAdmission_;}
    std::uint64_t EarlyAcknowledgements()const noexcept{return earlyAcknowledgements_.load();}
    std::uint64_t EarlyGraphicsDeferrals()const noexcept{return earlyGraphicsDeferrals_.load();}
    std::uint64_t PreparedPublications()const noexcept{return preparedPublications_.load();}
    std::uint64_t PreparedGraphicsReuses()const noexcept{return preparedGraphicsReuses_.load();}
private:
    friend struct D3D11FrameBridgeTestAccess;
    enum class PipelineStep {Idle,Capturing,Exporting,Submitting,Published,Cancelling};
    struct PipelineSource {
        D3D11PairProducer color;D3D11SceneDepthProducer depth;
        ipc::FrameLease lease{},cancelLease{};PairTicket ticket{};SceneDepthFrame depthFrame{};
        PipelineStep state=PipelineStep::Idle;unsigned colorMask=0,depthMask=0;
        bool depthInitialized=false,depthAllocationAdmitted=false,depthActive=false,reversed=false,cancelPending=false;
        bool restorationCommitted=false,captureReleased=false;
        std::uint64_t generations=0,eyeCopies=0,depthCopies=0,exports=0,submissions=0,consumed=0,discarded=0,cancellations=0;
    };
    static constexpr std::uint64_t PipelineBudgetHeadroom=64ull*1024*1024;
    static constexpr unsigned PipelineInvalidSlot=UINT32_MAX;
    std::array<PipelineSource,PipelineGraphicsSnapshot::Capacity> pipeline_;
    std::array<FrameBridgeDiagnostics,PipelineGraphicsSnapshot::Capacity> pipelineTiming_;
    PipelineGraphicsSnapshot pipelineEvidence_{};
    bool pipelineEnabled_=false,pipelineAllocated_=false;
    unsigned pipelineSourceCount_=1,pipelineCaptureSlot_=PipelineInvalidSlot;
    bool PipelineOwner()const noexcept;
    static bool PipelineMatches(const ipc::FrameLease&,const ipc::FrameLease&)noexcept;
    bool CheckPipelineBudget(const runtime::PresentationRequirements&,std::uint64_t,bool initial)noexcept;
    bool InitializePipeline(const ipc::FrameLease&)noexcept;
    bool PreparePipeline(const ipc::FrameLease&,RestoredPublicationResult* =nullptr)noexcept;
    bool BeginPipelineDepth(const ipc::FrameLease&,bool,RestoredPublicationResult*)noexcept;
    bool CapturePipelineDepth(unsigned,unsigned,ID3D11Texture2D*,const ipc::FrameLease&)noexcept;
    bool CapturePipelineEye(unsigned,ID3D11Texture2D*,const ipc::FrameLease&)noexcept;
    bool PublishPipelineRestored(const ipc::FrameLease&,RestoredPublicationResult*)noexcept;
    static void ClearPipelineLease(PipelineSource&)noexcept;
    void CancelPipeline(const ipc::FrameLease&)noexcept;
    void PumpPipelineSlot(unsigned)noexcept;
    void TryReleasePipelineCapture(unsigned)noexcept;
    void PumpPipeline()noexcept;
    void PipelineConsumed(const PairTicket&,bool)noexcept;

    static void ObservePublication(void*,const ipc::FrameLease&,const PairTicket&,ipc::StagedFrameObservation)noexcept;
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,TextureDescriptor&,PairTicket&)noexcept override{return false;}
    void PairConsumed(const PairTicket&,bool)noexcept override;
    bool PrepareGraphics(const ipc::FrameLease&,RestoredPublicationResult* observation=nullptr)noexcept;
    void CancelPrepared(const ipc::FrameLease&)noexcept;
    bool PreparedGraphicsMatches(const ipc::FrameLease&)const noexcept;
    // Graphics-owned proof for one local Rendering lease. First preparation
    // drains prior GPU ownership. Same-owner capture steps cannot be displaced
    // by admission/input, so they need no redundant gate. Submit still locks.
    ipc::FrameLease preparedGraphics_{};
    ID3D11Device* preparedDevice_=nullptr;
    ID3D11DeviceContext* preparedContext_=nullptr;
    bool preparedGraphicsValid_=false;
    std::atomic<std::uint64_t> preparedPublications_=0,preparedGraphicsReuses_=0;
    bool earlyFrameAdmission_=false,deferredCancellationPending_=false;
    ipc::FrameLease deferredCancellation_{};
    std::atomic<std::uint64_t> earlyAcknowledgements_=0,earlyGraphicsDeferrals_=0;
    D3D11LegacyRelay relay_;bool forceLegacy_=false,forceLegacyIpc_=false,waitingGameCopies_=false,waitingLegacyExport_=false;
    std::atomic<unsigned> sharingMode_=0;
    ipc::StagedFrameProducer channel_;D3D11PairProducer producer_;
    D3D11DesktopMirror desktopMirror_;
    D3D11SceneDepthProducer depthProducer_;bool depthInitialized_=false,depthActive_=false,depthReversed_=false;
    ipc::FrameLease depthLease_{};SceneDepthFrame pendingDepth_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> eyes_;
    runtime::PresentationRequirements allocated_{};std::uint64_t epoch_=0,capturedRequest_=0;unsigned mask_=0;
    ipc::NativeFrameKey allocatedNative_{};
    ipc::FrameLease pending_{};PairTicket ticket_{};bool submitting_=false,canceling_=false;
    // These pending fields are graphics-owned; TryBegin never writes them.
    bool restoredPairPending_=false;
    std::atomic<std::uint32_t> graphicsThread_=0;
    void DiagnoseSharing()noexcept;
    std::array<std::atomic<long>,9> compatibility_{};
    std::atomic<unsigned> deviceFlags_=0,featureLevel_=0,failureOperation_=0;
    std::atomic<unsigned> failureStage_=0;std::atomic<long> failureCode_=0;std::atomic<std::uint64_t> capturedEyes_=0;
    bool Fail(unsigned stage,HRESULT code,unsigned operation=0)noexcept{preparedGraphicsValid_=false;if(!failureStage_.load()){failureCode_.store(code);failureOperation_.store(operation);failureStage_.store(stage);}return false;}
    std::atomic<bool> connected_=false;
    std::atomic<std::uint64_t> published_=0,consumed_=0,discarded_=0;
    FrameBridgeDiagnostics timing_;
    D3D11RenderQueueLimit renderQueue_;
};
}
