#pragma once
#include "fvr/ipc/FrameProtocol.h"
#include "fvr/graphics/BodyPropFrame.h"
#include "fvr/ipc/InputProtocol.h"
#include "fvr/interaction/Feedback.h"
#include <memory>
#include <string>
namespace fvr::ipc {
enum class ChannelResult {Ok,Busy,Invalid,Timeout,Closed};
// One host thread and one producer thread, in independent x64/x86 processes.
// A fresh random token identifies each session. No game pointers cross this API.
class FrameChannel {
public:
    FrameChannel();~FrameChannel();
    FrameChannel(const FrameChannel&)=delete;FrameChannel& operator=(const FrameChannel&)=delete;
    bool CreateHost() noexcept;
    bool ConnectProducer(const std::wstring& token) noexcept;
    const std::wstring& Token()const noexcept;
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
    ChannelResult PollRequest(graphics::TextureDescriptor&,graphics::PairTicket&) noexcept;
    ChannelResult CancelRequest() noexcept;
    ChannelResult Feedback(const graphics::PairTicket&,bool consumed) noexcept;
    // Host can drain deferred GPU feedback without requesting another frame.
    ChannelResult FlushFeedback() noexcept;
    // Producer: call at the verified render boundary, on the graphics thread.
    ChannelResult TryTake(FrameRequest&) noexcept;
    ChannelResult Publish(const graphics::TextureDescriptor&,const graphics::PairTicket&,
        const graphics::BodyPropFrame* props=nullptr) noexcept;
    bool ReadBodyProps(const graphics::PairTicket&,graphics::BodyPropFrame&)noexcept;
    ChannelResult Skip() noexcept;
    Outcome PollOutcome() noexcept;
private:struct State;std::unique_ptr<State> state_;std::wstring token_;
};
}
