#pragma once
#include "fvr/ipc/FrameChannel.h"
#include "fvr/runtime/FrameProvider.h"
#include <chrono>
namespace fvr::ipc {
struct AsyncDeliveryStats {
    std::uint64_t requested=0,completed=0,timeouts=0;
    std::uint64_t totalLatencyUs=0,maxLatencyUs=0,completedAfter50Ms=0;
};
class RemoteFrameProvider final:public runtime::IFrameProvider {
public:
    bool Create()noexcept{pending_=cancelling_=false;stats_={};return channel_.CreateHost();}
    const std::wstring& Token()const noexcept{return channel_.Token();}
    bool Connected()noexcept{return channel_.Connected();}
    void Close()noexcept{channel_.Close();pending_=cancelling_=false;}
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
    void UpdateInput(const interaction::InputFrame& input)noexcept override{channel_.PublishInput(input);}
    bool TakeFeedback(interaction::FeedbackEvent& event)noexcept override{return channel_.TakeFeedback(event);}
    void Suspend()noexcept override;
    void PairConsumed(const graphics::PairTicket& t,bool consumed)noexcept override{channel_.Feedback(t,consumed);}
private:FrameChannel channel_;unsigned budgetMs_=8,requestLifetimeMs_=150;
    AsyncDeliveryStats stats_{};std::chrono::steady_clock::time_point pendingStarted_{};
    bool pending_=false,cancelling_=false;
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
