#include "fvr/ipc/RemoteFrameProvider.h"
namespace fvr::ipc {
void RemoteFrameProvider::Suspend()noexcept {
    cancelling_=channel_.CancelRequest()==ChannelResult::Busy;
    if(!cancelling_){pending_=false;lookaheadPending_={};lookaheadCount_=0;lastIssued_={};}
}
namespace {
bool SameRequirements(const runtime::PresentationRequirements& a,const runtime::PresentationRequirements& b)noexcept {
    return a.width==b.width&&a.height==b.height&&a.format==b.format&&a.adapterLow==b.adapterLow&&a.adapterHigh==b.adapterHigh;
}
}
void RemoteFrameProvider::ServiceRequests(const runtime::PresentationRequirements& requirements,const runtime::TrackingFrame& current)noexcept {
    if(!lookahead_)return;
    ++stats_.lookaheadServices;
    const bool changed=(lookaheadCount_&&!SameRequirements(requirements,lookaheadPending_[0].requirements))||
        (lastIssued_.generation&&(current.spaceGeneration!=lastIssued_.spaceGeneration||
            current.predictedNs<lastIssued_.predictedNs||current.generation<lastIssued_.generation));
    if(cancelling_||changed||!current.focused||!current.headValid){
        // Cancel BOTH immutable pending samples before flushing an old GPU ACK.
        // Contention keeps that intent and those records until the wire agrees.
        Suspend();if(cancelling_||!current.focused||!current.headValid)return;
    }
    channel_.FlushFeedback();
    if(lookaheadCount_==lookaheadPending_.size()||
        (lastIssued_.generation&&(current.generation<=lastIssued_.generation||current.predictedNs<=lastIssued_.predictedNs)))return;
    const auto started=std::chrono::steady_clock::now();
    if(channel_.SubmitLookaheadRequest(requirements,current,requestLifetimeMs_)!=ChannelResult::Ok)return;
    auto& next=lookaheadPending_[lookaheadCount_++];
    next={requirements,current,started,++stats_.requested,requestLifetimeMs_};
    lastIssued_=current;++stats_.lookaheadRequests;
    if(lookaheadCount_>stats_.maxPendingRequests)stats_.maxPendingRequests=lookaheadCount_;
}
void RemoteFrameProvider::PopLookahead()noexcept {
    if(!lookaheadCount_)return;
    lookaheadPending_[0]=lookaheadPending_[1];lookaheadPending_[1]={};--lookaheadCount_;
}
bool RemoteFrameProvider::TryGetLookaheadPair(const runtime::PresentationRequirements& requirements,const runtime::TrackingFrame& current,
    runtime::TrackingFrame& rendered,graphics::TextureDescriptor& descriptor,graphics::PairTicket& ticket)noexcept {
    // Also supports direct provider callers. Repeated sampling of this same
    // generation cannot enqueue another request or renew either deadline.
    ServiceRequests(requirements,current);
    if(cancelling_||!current.focused||!current.headValid||!lookaheadCount_)return false;
    FrameTimeoutSnapshot timeout{};
    const auto result=channel_.PollRequest(descriptor,ticket,&timeout);
    if(result==ChannelResult::Busy)return false;
    if(result!=ChannelResult::Ok&&result!=ChannelResult::Timeout){Suspend();return false;}
    const auto completed=lookaheadPending_[0];PopLookahead();
    const auto observed=std::chrono::steady_clock::now();
    if(result==ChannelResult::Timeout){
        ++stats_.timeouts;
        AsyncRequestTimeout row{};row.observed=true;row.requestOrdinal=completed.ordinal;
        row.queuedAtSubmission=true;row.lifetimeMs=completed.lifetimeMs;
        row.requirements=completed.requirements;row.requestedTracking=completed.tracking;row.currentTracking=current;
        row.startedSteadyNs=std::chrono::duration_cast<std::chrono::nanoseconds>(completed.started.time_since_epoch()).count();
        row.observedSteadyNs=std::chrono::duration_cast<std::chrono::nanoseconds>(observed.time_since_epoch()).count();
        row.clockValid=row.startedSteadyNs>0&&row.observedSteadyNs>=row.startedSteadyNs;
        if(row.clockValid)row.ageNs=std::uint64_t(row.observedSteadyNs-row.startedSteadyNs);
        stats_.RetainTimeout(row,timeout);
        return false; // Never let the next pose label this expired head's pixels.
    }
    const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(observed-completed.started).count());
    ++stats_.completed;stats_.totalLatencyUs+=us;if(us>stats_.maxLatencyUs)stats_.maxLatencyUs=us;if(us>50000)++stats_.completedAfter50Ms;
    rendered=completed.tracking;
    // v10 alone refills the newly freed local FIFO entry with this frame's
    // original sample. It cannot replace/renew either pending pose or ACK the
    // delivered source. Timeout above intentionally does not perform this step.
    if(channel_.SourceSlotCount()==3)ServiceRequests(requirements,current);
    return true;
}
bool RemoteFrameProvider::TryGetCompletedPair(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& current,
    runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& ticket)noexcept {
    if(lookahead_)return TryGetLookaheadPair(r,current,rendered,d,ticket);
    const bool changed=r.width!=pendingRequirements_.width||r.height!=pendingRequirements_.height||r.format!=pendingRequirements_.format||
        r.adapterLow!=pendingRequirements_.adapterLow||r.adapterHigh!=pendingRequirements_.adapterHigh||
        current.spaceGeneration!=pendingTracking_.spaceGeneration||current.predictedNs<pendingTracking_.predictedNs;
    if(cancelling_||(pending_&&changed)||!current.focused||!current.headValid){Suspend();if(cancelling_||!current.focused||!current.headValid)return false;}
    // Cancellation clears a queued successor BEFORE a deferred old GPU ack
    // can make it runnable. Never flush first on a focus/space/size transition.
    channel_.FlushFeedback();
    if(pending_){FrameTimeoutSnapshot timeout{};const auto result=channel_.PollRequest(d,ticket,&timeout);
        if(result==ChannelResult::Busy)return false;
        pending_=false;
        if(result==ChannelResult::Timeout){
            ++stats_.timeouts;
            AsyncRequestTimeout row{};row.observed=true;row.requestOrdinal=stats_.requested;
            row.queuedAtSubmission=pendingQueued_;row.lifetimeMs=pendingLifetimeMs_;
            row.requirements=pendingRequirements_;row.requestedTracking=pendingTracking_;row.currentTracking=current;
            row.startedSteadyNs=std::chrono::duration_cast<std::chrono::nanoseconds>(pendingStarted_.time_since_epoch()).count();
            // Keep the existing clock-read count. Later non-lookahead timeouts
            // have no host observation timestamp; v9 channel QPC may still exist.
            if(!stats_.firstTimeout.observed){
                row.observedSteadyNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                row.clockValid=row.startedSteadyNs>0&&row.observedSteadyNs>=row.startedSteadyNs;
                if(row.clockValid)row.ageNs=std::uint64_t(row.observedSteadyNs-row.startedSteadyNs);
            }
            stats_.RetainTimeout(row,timeout);
        }
        if(result==ChannelResult::Ok){
            const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-pendingStarted_).count());
            ++stats_.completed;stats_.totalLatencyUs+=us;if(us>stats_.maxLatencyUs)stats_.maxLatencyUs=us;if(us>50000)++stats_.completedAfter50Ms;
            rendered=pendingTracking_;
            // Queue the next immutable pose without releasing the just-delivered
            // GPU pair. Its exact feedback + producer reap unlock the successor.
            // This removes the otherwise mandatory idle XR tick after delivery.
            const auto started=std::chrono::steady_clock::now();
            if(channel_.QueueRequest(r,current,requestLifetimeMs_)==ChannelResult::Ok){
                pendingRequirements_=r;pendingTracking_=current;pending_=true;pendingStarted_=started;
                pendingQueued_=true;pendingLifetimeMs_=requestLifetimeMs_;++stats_.requested;
            }
            return true;
        }
    }
    const auto started=std::chrono::steady_clock::now();
    if(channel_.BeginRequest(r,current,requestLifetimeMs_)==ChannelResult::Ok){
        pendingRequirements_=r;pendingTracking_=current;pending_=true;pendingStarted_=started;
        pendingQueued_=false;pendingLifetimeMs_=requestLifetimeMs_;++stats_.requested;
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
