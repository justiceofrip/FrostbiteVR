#include "fvr/ipc/RemoteFrameProvider.h"
namespace fvr::ipc {
void RemoteFrameProvider::Suspend()noexcept {
    cancelling_=channel_.CancelRequest()==ChannelResult::Busy;
    if(!cancelling_)pending_=false;
}
bool RemoteFrameProvider::TryGetCompletedPair(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& current,
    runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& ticket)noexcept {
    channel_.FlushFeedback();
    const bool changed=r.width!=pendingRequirements_.width||r.height!=pendingRequirements_.height||r.format!=pendingRequirements_.format||
        r.adapterLow!=pendingRequirements_.adapterLow||r.adapterHigh!=pendingRequirements_.adapterHigh||
        current.spaceGeneration!=pendingTracking_.spaceGeneration||current.predictedNs<pendingTracking_.predictedNs;
    if(cancelling_||(pending_&&changed)||!current.focused||!current.headValid){Suspend();if(cancelling_||!current.focused||!current.headValid)return false;}
    if(pending_){const auto result=channel_.PollRequest(d,ticket);
        if(result==ChannelResult::Busy)return false;
        pending_=false;
        if(result==ChannelResult::Timeout)++stats_.timeouts;
        if(result==ChannelResult::Ok){
            const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-pendingStarted_).count());
            ++stats_.completed;stats_.totalLatencyUs+=us;if(us>stats_.maxLatencyUs)stats_.maxLatencyUs=us;if(us>50000)++stats_.completedAfter50Ms;
            rendered=pendingTracking_;return true;
        }
    }
    const auto started=std::chrono::steady_clock::now();
    if(channel_.BeginRequest(r,current,requestLifetimeMs_)==ChannelResult::Ok){
        pendingRequirements_=r;pendingTracking_=current;pending_=true;pendingStarted_=started;++stats_.requested;
    }
    return false;
}
void RemoteFrameProducer::Close()noexcept {
    channel_.Close();if(step_==Step::Publishing||step_==Step::Waiting)provider_.PairConsumed(ticket_,false);step_=Step::Idle;
}
void RemoteFrameProducer::Pump()noexcept {
    if(step_==Step::Waiting){const auto outcome=channel_.PollOutcome();if(outcome==Outcome::Pending)return;
        provider_.PairConsumed(ticket_,outcome==Outcome::Consumed);step_=Step::Idle;}
    if(step_==Step::Skipping){if(channel_.Skip()==ChannelResult::Busy)return;step_=Step::Idle;}
    if(step_==Step::Idle){
        FrameRequest request{};if(channel_.TryTake(request)!=ChannelResult::Ok)return;
        runtime::PresentationRequirements requirements{};runtime::TrackingFrame tracking{};
        if(!Decode(request,requirements,tracking)||!provider_.TryGetPair(requirements,tracking,descriptor_,ticket_)){
            step_=Step::Skipping;if(channel_.Skip()!=ChannelResult::Busy)step_=Step::Idle;return;}
        step_=Step::Publishing;
    }
    if(step_==Step::Publishing){const auto result=channel_.Publish(descriptor_,ticket_);if(result==ChannelResult::Busy)return;
        if(result==ChannelResult::Ok)step_=Step::Waiting;
        else{provider_.PairConsumed(ticket_,false);step_=Step::Idle;}
    }
}
}
