#pragma once
#include "fvr/ipc/RemoteFrameProvider.h"
#include <array>
#include <chrono>
namespace fvr::ipc {
// Two independent frame channels presented to the host loop as one provider, so a game
// that needs several native frames per pair (BF4: the pose must reach two view submits
// before a presented frame is known to carry it) already holds the next request when it
// captures the current one. Each channel keeps the single-slot protocol, its own textures
// and its own ticket session; nothing about a pair crosses channels.
//  - At most one channel begins a request per XR frame, and a channel begins only once the
//    other one's request is kStagger of a typical completion old: the queued request then
//    reaches the producer shortly before it finishes the current pair, so its head pose is
//    not much older than a fresh one would be.
//  - If both complete in one frame, the older pair is returned now and the newer one on
//    the next call (never discarded: a discard makes the producer rebuild its textures).
//  - Feedback, body props and scene depth are offered to both channels; each accepts only
//    the ticket it delivered (FrameChannel compares the whole ticket).
//  - Controller input goes to channel 0 (the producer reads it there).
class PipelinedFrameProvider final:public runtime::IFrameProvider {
public:
    PipelinedFrameProvider(RemoteFrameProvider& first,RemoteFrameProvider& second)noexcept:channels_{&first,&second}{}
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    bool TryGetCompletedPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override;
    bool ReadBodyProps(const graphics::PairTicket& t,graphics::BodyPropFrame& p)noexcept override {
        return channels_[0]->ReadBodyProps(t,p)||channels_[1]->ReadBodyProps(t,p);
    }
    bool ReadSceneDepth(const graphics::TextureDescriptor& d,const graphics::PairTicket& t,graphics::SceneDepthFrame& p)noexcept override {
        return channels_[0]->ReadSceneDepth(d,t,p)||channels_[1]->ReadSceneDepth(d,t,p);
    }
    void UpdateInput(const interaction::InputFrame& f)noexcept override{channels_[0]->UpdateInput(f);}
    runtime::InputPublicationResult UpdateInputObserved(const interaction::InputFrame& f)noexcept override {
        return channels_[0]->UpdateInputObserved(f);
    }
    bool TakeFeedback(interaction::FeedbackEvent& e)noexcept override {
        return channels_[0]->TakeFeedback(e)||channels_[1]->TakeFeedback(e);
    }
    void Suspend()noexcept override;
    void PairConsumed(const graphics::PairTicket& t,bool consumed)noexcept override {
        channels_[0]->PairConsumed(t,consumed);channels_[1]->PairConsumed(t,consumed);
    }
    std::uint64_t Delivered(unsigned channel)const noexcept{return channel<2?delivered_[channel]:0;}
    std::uint64_t HeldPairs()const noexcept{return heldPairs_;}
    double TypicalLatencyUs()const noexcept{return typicalUs_;}
    static constexpr double kStagger=0.7;
private:
    struct Held {bool valid=false;runtime::TrackingFrame tracking{};graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};};
    using Clock=std::chrono::steady_clock;
    bool MayBegin(unsigned channel,Clock::time_point now)const noexcept;
    void Learn(unsigned channel,Clock::time_point now)noexcept;
    std::array<RemoteFrameProvider*,2> channels_;
    std::array<std::uint64_t,2> delivered_{};
    double typicalUs_=0;             // smoothed request-to-completion time
    Held held_{};unsigned heldChannel_=0;std::uint64_t heldPairs_=0;
};
}
