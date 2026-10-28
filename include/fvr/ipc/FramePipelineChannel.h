#pragma once
#include "fvr/ipc/FrameChannel.h"
namespace fvr::ipc {
// Explicit private v9(two-source)/v10(three-source) transport. No discovery or
// cross-version fallback; v10 additionally requires lookahead/export overlap.
class FramePipelineChannel final {
public:
    FramePipelineChannel();~FramePipelineChannel();
    bool CreateHost(bool lookahead=false,bool exportOverlap=false,bool thirdSource=false)noexcept;
    bool ConnectProducer(const std::wstring&,bool thirdSource=false)noexcept;
    const std::wstring& Token()const noexcept{return token_;}
    bool Connected()noexcept;
    bool ExportOverlapEnabled()const noexcept;
    unsigned SourceSlotCount()const noexcept;
    void Close()noexcept;
    ChannelResult BeginRequest(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,unsigned,bool queued)noexcept;
    ChannelResult SubmitLookaheadRequest(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,unsigned)noexcept;
    ChannelResult PollRequest(graphics::TextureDescriptor&,graphics::PairTicket&,FrameTimeoutSnapshot* timeout=nullptr)noexcept;
    ChannelResult CancelRequest()noexcept;
    ChannelResult Feedback(const graphics::PairTicket&,bool)noexcept;
    ChannelResult FlushFeedback()noexcept;
    ChannelResult TryTake(FrameRequest&,PipelineSlotKey&)noexcept;
    ChannelResult MarkCaptureReleased(const PipelineSlotKey&)noexcept;
    // Ok and Timeout both adopt the source. Timeout means retire it negatively;
    // ClaimRetirement then owns the sole graphics callback, never the caller.
    ChannelResult Publish(const PipelineSlotKey&,const graphics::TextureDescriptor&,const graphics::PairTicket&,
        const graphics::BodyPropFrame*,const graphics::SceneDepthFrame*)noexcept;
    ChannelResult Skip(const PipelineSlotKey&)noexcept;
    ChannelResult ClaimRetirement(const PipelineSlotKey&,PipelineRetirement&)noexcept;
    ChannelResult FinishRetirement(const PipelineRetirement&)noexcept;
    ChannelResult StopAdmission()noexcept;
    ChannelResult CheckSourcesDrained()noexcept;
    bool ReadBodyProps(const graphics::PairTicket&,graphics::BodyPropFrame&)noexcept;
    bool ReadSceneDepth(const graphics::TextureDescriptor&,const graphics::PairTicket&,graphics::SceneDepthFrame&)noexcept;
    bool ReadSourceSlot(const graphics::PairTicket&,runtime::SourceSlotIdentity&)noexcept;
    ChannelResult PublishInput(const interaction::InputFrame&)noexcept;
    ChannelResult ReadInput(interaction::InputFrame&,std::int64_t*)noexcept;
    bool PublishFeedback(const interaction::FeedbackEvent&)noexcept;
    bool TakeFeedback(interaction::FeedbackEvent&)noexcept;
private:
    struct State;std::unique_ptr<State> state_;std::wstring token_;
};
}
