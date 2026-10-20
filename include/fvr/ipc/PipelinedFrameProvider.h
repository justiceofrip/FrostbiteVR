#pragma once
#include "fvr/ipc/RemoteFrameProvider.h"
#include <array>
#include <chrono>
#include <initializer_list>
namespace fvr::ipc {
// Two to four independent frame channels presented to the host loop as one provider, so a
// game that needs several native frames per pair (BF4: the pose must reach two view submits
// before a presented frame is known to carry it) already holds the next requests when it
// captures the current one. Each channel keeps the single-slot protocol, its own textures
// and its own ticket session; nothing about a pair crosses channels.
//  - At most one channel begins a request per XR frame, and a channel begins only once the
//    youngest outstanding request is kStagger / (channels - 1) of a typical completion old:
//    queued requests then reach the producer shortly before it finishes the current pair,
//    so their head poses are not much older than fresh ones would be.
//  - Pairs that complete together are delivered oldest first, one per call; the others are
//    held (never discarded: a discard makes the producer rebuild its textures). A channel
//    whose pair is held does not begin again until it is delivered and fed back.
//  - Feedback, body props and scene depth are offered to every channel; each accepts only
//    the ticket it delivered (FrameChannel compares the whole ticket).
//  - Controller input goes to channel 0 (the producer reads it there).
class PipelinedFrameProvider final:public runtime::IFrameProvider {
public:
    static constexpr unsigned kMaxChannels=4;
    PipelinedFrameProvider(RemoteFrameProvider& first,RemoteFrameProvider& second)noexcept:channels_{&first,&second},count_(2){}
    // Two to kMaxChannels channels, in order (channel 0 carries input).
    explicit PipelinedFrameProvider(std::initializer_list<RemoteFrameProvider*> channels)noexcept{
        for(auto* c:channels)if(c&&count_<kMaxChannels)channels_[count_++]=c;
    }
    unsigned Channels()const noexcept{return count_;}
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    bool TryGetCompletedPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override;
    bool ReadBodyProps(const graphics::PairTicket& t,graphics::BodyPropFrame& p)noexcept override {
        for(unsigned c=0;c<count_;++c)if(channels_[c]->ReadBodyProps(t,p))return true;
        return false;
    }
    bool ReadSceneDepth(const graphics::TextureDescriptor& d,const graphics::PairTicket& t,graphics::SceneDepthFrame& p)noexcept override {
        for(unsigned c=0;c<count_;++c)if(channels_[c]->ReadSceneDepth(d,t,p))return true;
        return false;
    }
    void UpdateInput(const interaction::InputFrame& f)noexcept override{if(count_)channels_[0]->UpdateInput(f);}
    runtime::InputPublicationResult UpdateInputObserved(const interaction::InputFrame& f)noexcept override {
        return count_?channels_[0]->UpdateInputObserved(f):runtime::InputPublicationResult{};
    }
    bool TakeFeedback(interaction::FeedbackEvent& e)noexcept override {
        for(unsigned c=0;c<count_;++c)if(channels_[c]->TakeFeedback(e))return true;
        return false;
    }
    void Suspend()noexcept override;
    void PairConsumed(const graphics::PairTicket& t,bool consumed)noexcept override {
        for(unsigned c=0;c<count_;++c)channels_[c]->PairConsumed(t,consumed);
    }
    std::uint64_t Delivered(unsigned channel)const noexcept{return channel<count_?delivered_[channel]:0;}
    std::uint64_t HeldPairs()const noexcept{return heldPairs_;}
    double TypicalLatencyUs()const noexcept{return typicalUs_;}
    static constexpr double kStagger=0.7;
private:
    struct Held {bool valid=false,fresh=false;unsigned channel=0;runtime::TrackingFrame tracking{};graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};};
    using Clock=std::chrono::steady_clock;
    bool MayBegin(unsigned channel,Clock::time_point now)const noexcept;
    void Learn(unsigned channel,Clock::time_point now)noexcept;
    bool IsHeld(unsigned channel)const noexcept;
    std::array<RemoteFrameProvider*,kMaxChannels> channels_{};
    unsigned count_=0;
    std::array<std::uint64_t,kMaxChannels> delivered_{};
    double typicalUs_=0;             // smoothed request-to-completion time
    std::array<Held,kMaxChannels> held_{}; // completed pairs waiting for their turn (oldest first)
    std::uint64_t heldPairs_=0;
};
}
