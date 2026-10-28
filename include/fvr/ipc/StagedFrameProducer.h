#pragma once
#include "fvr/ipc/FrameChannel.h"
#include "fvr/runtime/FrameProvider.h"
#include <atomic>
#include <span>
namespace fvr::ipc {
// Local identity, never a wire pointer. The engine adapter supplies generations
// for world ownership and the graphics device, plus its native frame serial.
struct NativeFrameKey {
    std::uint64_t owner=0,frameId=0,deviceEpoch=0;
    bool operator==(const NativeFrameKey&)const=default;
};
struct FrameLease {
    std::uint64_t channelGeneration=0,requestId=0;
    NativeFrameKey native{};
    runtime::PresentationRequirements requirements{};
    runtime::TrackingFrame tracking{};
    std::uint32_t sourceSlot=0,sourceSlotCount=1;
};
enum class StagedFrameObservation {Published,Reaped,Acknowledged};
using StagedFrameObserver=void(*)(void*,const FrameLease&,const graphics::PairTicket&,StagedFrameObservation)noexcept;
// One request can span native visibility and graphics callbacks on different
// threads. No native calls, simulation updates or GPU work occur in TryBegin.
// Runtime operations use try-locks only; contention drops/defer work.
class StagedFrameProducer {
public:
    // Published/Reaped run on graphics. Opt-in Acknowledged runs in TryBegin
    // on the admission thread BEFORE a successor is taken. That event permits
    // timing metadata only, never GPU work or feedback. All events hold gate_.
    // The observer must be bounded, nonthrowing, non-reentrant and outlive us.
    explicit StagedFrameProducer(runtime::IFrameProvider& feedback,StagedFrameObserver observer=nullptr,void* context=nullptr)
        :feedback_(feedback),observer_(observer),observerContext_(context){}
    ~StagedFrameProducer(){CloseGraphics();}
    StagedFrameProducer(const StagedFrameProducer&)=delete;
    StagedFrameProducer& operator=(const StagedFrameProducer&)=delete;
    // Setup/teardown only with both callback threads quiescent, on graphics thread.
    bool Connect(const std::wstring&,bool twoSourceSlots=false,bool thirdSource=false)noexcept;
    unsigned SourceSlotCount()const noexcept{return channel_.SourceSlotCount();}
    bool ExportOverlapEnabled()const noexcept{return channel_.ExportOverlapEnabled();}
    void CloseGraphics()noexcept;
    // Setup only, with callbacks quiescent. Default is the original ordering.
    bool SetDeferredGraphicsRetirement(bool enabled)noexcept;
    // Once taken, a frame cannot be retried, even after cancellation/reconnect.
    bool TryBegin(const NativeFrameKey&,FrameLease&,bool allowDeferred=true)noexcept;
    // Graphics owner only, before any successor resource mutation. Drain old
    // feedback even for an invalid/cancelled current lease, then validate it.
    // Busy means gate contention; Invalid means no matching Rendering lease.
    // Neither permits new GPU work. Retirement is drained before validation.
    ChannelResult PrepareGraphicsResult(const FrameLease&)noexcept;
    bool PrepareGraphics(const FrameLease& lease)noexcept {return PrepareGraphicsResult(lease)==ChannelResult::Ok;}
    // Graphics owner only, AFTER exact native restoration and all shared capture
    // reads (including the mirror) are queued on that same immediate context.
    // This releases only camera admission, never source ownership/publication.
    // Busy retains the caller's pending proof; retry only while still Rendering.
    ChannelResult MarkCaptureReleased(const FrameLease&)noexcept;
    ChannelResult ReadInput(interaction::InputFrame& out,std::int64_t* deadlineQpc=nullptr)noexcept {
        if(deadlineQpc)*deadlineQpc=0;out={};Lock lock(gate_);return lock?channel_.ReadInput(out,deadlineQpc):ChannelResult::Busy;
    }
    bool PublishFeedback(const interaction::FeedbackEvent& e)noexcept {Lock lock(gate_);return lock&&channel_.PublishFeedback(e);}
    // Ok transfers ticket ownership. On Busy/Invalid caller still owns the pair
    // and must retry or reclaim it. Submit ONLY after exact native restoration.
    ChannelResult Submit(const FrameLease&,const graphics::TextureDescriptor&,const graphics::PairTicket&,const graphics::SceneDepthFrame* depth=nullptr)noexcept;
    ChannelResult Cancel(const FrameLease&)noexcept;
    ChannelResult SetBodyProps(const FrameLease&,unsigned eye,const graphics::BodyPropEye&)noexcept;
    // One lock commits both stereo metadata eyes, or neither on contention.
    ChannelResult SetBodyPropPair(const FrameLease&,const std::array<graphics::BodyPropEye,2>&)noexcept;
    // Graphics thread only: publish and return GPU ownership feedback exactly once.
    void PumpGraphics()noexcept;
    // Explicit engine-authorized world rebuild, on the graphics owner while
    // native admission is paused. Both local and wire source ownership must be
    // fully retired. Unclaimed host requests keep their original deadlines.
    // Match expectedOld by owner/deviceEpoch, not its historical frame serial.
    // Truly never-bound/drained state is a no-op; first admission binds normally.
    // Never reconnect, clear a fault/closing, or reset channel/lease generations.
    ChannelResult TransitionWorldOwner(const NativeFrameKey& expectedOld,const NativeFrameKey& replacement)noexcept;
private:
    friend struct StagedFrameProducerTestAccess;
    enum class Step{Idle,Rendering,Skipping,Publishing,Waiting,Retiring};
    struct Lock {
        std::atomic_flag& flag;bool held;
        explicit Lock(std::atomic_flag& f):flag(f),held(!f.test_and_set(std::memory_order_acquire)){}
        ~Lock(){if(held)flag.clear(std::memory_order_release);}
        explicit operator bool()const{return held;}
    };
    bool Matches(const FrameLease&)const noexcept;
    // Caller holds gate_, or both callback threads are quiescent at teardown.
    void DrainRetiredGraphics()noexcept;
    FrameChannel channel_;runtime::IFrameProvider& feedback_;
    StagedFrameObserver observer_=nullptr;void* observerContext_=nullptr;
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    Step step_=Step::Idle;std::uint64_t generation_=0;
    bool deferredGraphicsRetirement_=false;
    struct Retirement {
        FrameLease lease{};graphics::PairTicket ticket{};Outcome outcome=Outcome::Pending;
    } retired_;
    NativeFrameKey lastNative_{};
    graphics::BodyPropFrame bodyProps_{};
    graphics::SceneDepthFrame sceneDepth_{};
    FrameLease lease_{};graphics::TextureDescriptor descriptor_{};graphics::PairTicket ticket_{};
    struct PipelineState {
        Step step=Step::Idle;FrameLease lease{};graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};
        graphics::BodyPropFrame bodyProps{};graphics::SceneDepthFrame sceneDepth{};PipelineRetirement retirement{};
        bool ownsSource=false,callbackComplete=false,captureReleased=false;
    };
    std::array<PipelineState,3> pipelineSlots_{};bool pipelineFaulted_=false;
    NativeFrameKey pipelineOwner_{};runtime::PresentationRequirements pipelineRequirements_{};
    std::span<PipelineState> PipelineSlots()noexcept{return {pipelineSlots_.data(),channel_.SourceSlotCount()};}
    bool PipelineMatches(const FrameLease&)const noexcept;
    bool TryBeginPipeline(const NativeFrameKey&,FrameLease&)noexcept;
    void DrainPipelineGraphics()noexcept;
    void PumpPipelineGraphics()noexcept;
    void ClosePipelineGraphics()noexcept;
    void FailPipeline()noexcept;
};
}
