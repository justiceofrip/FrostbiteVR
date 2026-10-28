#pragma once
#include "fvr/ipc/FrameProtocol.h"
#include "fvr/ipc/TwoSlotFramePipeline.h"
#include "fvr/runtime/FrameSourceSlot.h"
#include "fvr/graphics/BodyPropFrame.h"
#include "fvr/graphics/SceneDepthFrame.h"
#include "fvr/ipc/InputProtocol.h"
#include "fvr/interaction/Feedback.h"
#include <memory>
#include <string>
namespace fvr::ipc {
enum class ChannelResult {Ok,Busy,Invalid,Timeout,Closed};
enum class FrameTimeoutReason {Unavailable,QueuedDeadline,RequestMissing,SlotReplaced,SlotDeadlineOrTerminal};
// Caller-local observation, never part of the mapped wire ABI. Copied only on
// Timeout, under PollRequest's existing mutex, before cancellation/pop. A phase
// describes the observed slot; a replaced slot need not belong to requestedKey.
struct FrameTimeoutSnapshot {
    bool available=false,clockValid=false,requestObserved=false,phaseValid=false;
    bool deadlineExpired=false,terminalPhase=false;
    std::uint32_t wireVersion=0;
    FrameTimeoutReason reason=FrameTimeoutReason::Unavailable;
    PipelineSlotKey requestedKey{};
    std::int64_t nowQpc=0,frequencyQpc=0,observedDeadlineQpc=0,queuedDeadlineQpc=0;
    std::uint64_t observedRequestId=0,queuedRequestId=0,nativeHeldRequest=0,captureReleasedRequest=0;
    Phase phase=Phase::Idle;
    bool hasSource=false,retiring=false;
};
static_assert(std::is_trivially_copyable_v<FrameTimeoutSnapshot>);
// One host thread and one producer thread, in independent x64/x86 processes.
// A fresh random token identifies each session. No game pointers cross this API.
class FramePipelineChannel;
class FrameChannel {
public:
    FrameChannel();~FrameChannel();
    FrameChannel(const FrameChannel&)=delete;FrameChannel& operator=(const FrameChannel&)=delete;
    bool CreateHost(bool twoSourceSlots=false,bool lookahead=false,bool exportOverlap=false,bool thirdSource=false) noexcept;
    bool ConnectProducer(const std::wstring& token,bool twoSourceSlots=false,bool thirdSource=false) noexcept;
    const std::wstring& Token()const noexcept;
    unsigned SourceSlotCount()const noexcept;
    bool ExportOverlapEnabled()const noexcept;
    // Graphics owner: exact camera restoration and capture reads have been
    // enqueued. This does not publish, complete, ACK, or release a source set.
    ChannelResult MarkCaptureReleased(const PipelineSlotKey&)noexcept;
    bool ReadSourceSlot(const graphics::PairTicket&,runtime::SourceSlotIdentity&)noexcept;
    ChannelResult TryTakePipeline(FrameRequest&,PipelineSlotKey&)noexcept;
    ChannelResult PublishPipeline(const PipelineSlotKey&,const graphics::TextureDescriptor&,const graphics::PairTicket&,
        const graphics::BodyPropFrame*,const graphics::SceneDepthFrame*)noexcept;
    ChannelResult SkipPipeline(const PipelineSlotKey&)noexcept;
    ChannelResult ClaimPipelineRetirement(const PipelineSlotKey&,PipelineRetirement&)noexcept;
    ChannelResult FinishPipelineRetirement(const PipelineRetirement&)noexcept;
    ChannelResult StopPipelineAdmission()noexcept;
    // Producer-only, nonblocking observation. Unclaimed request metadata may
    // remain; no old native/source/retirement ownership or closing is permitted.
    ChannelResult CheckPipelineSourcesDrained()noexcept;
    bool Connected() noexcept;
    // Latest controls are independent of an in-flight render request. Never wait.
    ChannelResult PublishInput(const interaction::InputFrame&) noexcept;
    ChannelResult ReadInput(interaction::InputFrame&,std::int64_t* deadlineQpc=nullptr) noexcept;
    bool PublishFeedback(const interaction::FeedbackEvent&)noexcept;
    bool TakeFeedback(interaction::FeedbackEvent&)noexcept;
    void Close() noexcept;
    // Host: bounded rendezvous for this exact predicted-time sample. Game methods
    // never wait for a CPU mutex. A busy slot drops the current host request.
    ChannelResult RequestPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        unsigned budgetMs,graphics::TextureDescriptor&,graphics::PairTicket&) noexcept;
    // Host: nonblocking split rendezvous. A pending request keeps its immutable
    // sample across XR frames. Lifetime is 1..200 ms, independent of the 50 ms
    // blocking wait cap. Poll never consumes a newer sample's images.
    ChannelResult BeginRequest(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,unsigned lifetimeMs) noexcept;
    // Host: queue ONE immutable next sample while the delivered pair remains
    // GPU-owned. The producer can take it only after reaping that pair's exact
    // acknowledgement. Queueing neither releases pixels nor renews a deadline.
    ChannelResult QueueRequest(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,unsigned lifetimeMs) noexcept;
    // Explicit negotiated v9 look-ahead only: append one immutable pose to the
    // two-entry undelivered FIFO. Never delivers, releases, replaces or renews.
    ChannelResult SubmitLookaheadRequest(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,unsigned lifetimeMs) noexcept;
    ChannelResult PollRequest(graphics::TextureDescriptor&,graphics::PairTicket&,FrameTimeoutSnapshot* timeout=nullptr) noexcept;
    ChannelResult CancelRequest() noexcept;
    ChannelResult Feedback(const graphics::PairTicket&,bool consumed) noexcept;
    // Host can drain deferred GPU feedback without requesting another frame.
    ChannelResult FlushFeedback() noexcept;
    // Producer: call at the verified render boundary, on the graphics thread.
    ChannelResult TryTake(FrameRequest&) noexcept;
    ChannelResult Publish(const graphics::TextureDescriptor&,const graphics::PairTicket&,
        const graphics::BodyPropFrame* props=nullptr,const graphics::SceneDepthFrame* depth=nullptr) noexcept;
    bool ReadBodyProps(const graphics::PairTicket&,graphics::BodyPropFrame&)noexcept;
    bool ReadSceneDepth(const graphics::TextureDescriptor&,const graphics::PairTicket&,graphics::SceneDepthFrame&)noexcept;
    ChannelResult Skip() noexcept;
    Outcome PollOutcome() noexcept;
private:struct State;std::unique_ptr<State> state_;std::unique_ptr<FramePipelineChannel> pipeline_;std::wstring token_;
};
}
