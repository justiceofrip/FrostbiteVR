#include "fvr/ipc/PipelinedFrameProvider.h"
namespace fvr::ipc {
bool PipelinedFrameProvider::MayBegin(unsigned channel,Clock::time_point now)const noexcept {
    const auto& other=*channels_[channel^1u];
    if(!other.Pending())return true;
    const double ageUs=std::chrono::duration<double,std::micro>(now-other.PendingStarted()).count();
    return ageUs>=kStagger*typicalUs_;
}
void PipelinedFrameProvider::Learn(unsigned channel,Clock::time_point now)noexcept {
    const double us=std::chrono::duration<double,std::micro>(now-channels_[channel]->PendingStarted()).count();
    if(!(us>0&&us<250000))return;
    typicalUs_=typicalUs_>0?typicalUs_+0.2*(us-typicalUs_):us;
}
bool PipelinedFrameProvider::TryGetCompletedPair(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& current,
    runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& ticket)noexcept {
    const auto now=Clock::now();
    std::array<Held,2> fresh{};
    // The older outstanding request is polled first, so a channel that completes
    // this frame is seen before the other one decides whether it may begin.
    std::array<unsigned,2> order{0u,1u};
    if(channels_[0]->Pending()&&channels_[1]->Pending()&&channels_[1]->PendingStarted()<channels_[0]->PendingStarted())order={1u,0u};
    bool begun=false;
    for(const unsigned c:order){
        if(held_.valid&&heldChannel_==c)continue; // delivered, waiting for its turn; cannot begin anyway
        auto& channel=*channels_[c];
        const bool pending=channel.Pending();
        const bool allow=!pending&&!begun&&MayBegin(c,now);
        auto& f=fresh[c];
        f.valid=channel.TryGetCompletedPair(r,current,f.tracking,f.descriptor,f.ticket,allow);
        if(f.valid)Learn(c,now);
        else if(!pending&&channel.Pending())begun=true;
    }
    const auto deliver=[&](const Held& pair,unsigned channel){
        rendered=pair.tracking;d=pair.descriptor;ticket=pair.ticket;++delivered_[channel];return true;
    };
    if(held_.valid){
        // A held pair is always older than one the other channel completes now: its own
        // channel cannot begin again until the held pair is delivered and fed back.
        const Held previous=held_;const unsigned previousChannel=heldChannel_;held_.valid=false;
        for(unsigned c=0;c<2;++c)if(fresh[c].valid){held_=fresh[c];heldChannel_=c;++heldPairs_;}
        return deliver(previous,previousChannel);
    }
    if(fresh[0].valid&&fresh[1].valid){
        const unsigned older=fresh[1].tracking.predictedNs<fresh[0].tracking.predictedNs?1u:0u;
        held_=fresh[older^1u];heldChannel_=older^1u;++heldPairs_;
        return deliver(fresh[older],older);
    }
    for(unsigned c=0;c<2;++c)if(fresh[c].valid)return deliver(fresh[c],c);
    return false;
}
void PipelinedFrameProvider::Suspend()noexcept {
    // A held pair was delivered by its channel: hand it back before cancelling, or that
    // channel would stay busy with a pair nobody will present.
    if(held_.valid){channels_[heldChannel_]->PairConsumed(held_.ticket,false);held_.valid=false;}
    channels_[0]->Suspend();channels_[1]->Suspend();
}
}
