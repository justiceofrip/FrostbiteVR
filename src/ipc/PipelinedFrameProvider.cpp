#include "fvr/ipc/PipelinedFrameProvider.h"
#include <algorithm>
namespace fvr::ipc {
bool PipelinedFrameProvider::MayBegin(unsigned channel,Clock::time_point now)const noexcept {
    // The youngest outstanding request of the other channels sets the pace.
    bool any=false;Clock::time_point youngest{};
    for(unsigned c=0;c<count_;++c){
        if(c==channel||!channels_[c]->Pending())continue;
        const auto started=channels_[c]->PendingStarted();
        if(!any||started>youngest)youngest=started;
        any=true;
    }
    if(!any)return true;
    const double ageUs=std::chrono::duration<double,std::micro>(now-youngest).count();
    return ageUs>=kStagger*typicalUs_/double(count_>1?count_-1:1);
}
void PipelinedFrameProvider::Learn(unsigned channel,Clock::time_point now)noexcept {
    const double us=std::chrono::duration<double,std::micro>(now-channels_[channel]->PendingStarted()).count();
    if(!(us>0&&us<250000))return;
    typicalUs_=typicalUs_>0?typicalUs_+0.2*(us-typicalUs_):us;
}
bool PipelinedFrameProvider::IsHeld(unsigned channel)const noexcept {
    for(const auto& h:held_)if(h.valid&&h.channel==channel)return true;
    return false;
}
bool PipelinedFrameProvider::TryGetCompletedPair(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& current,
    runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& ticket)noexcept {
    if(count_<2)return false;
    const auto now=Clock::now();
    // Outstanding requests are polled oldest first, so a channel that completes this frame
    // is seen before another decides whether it may begin.
    std::array<unsigned,kMaxChannels> order{};
    for(unsigned c=0;c<count_;++c)order[c]=c;
    std::sort(order.begin(),order.begin()+count_,[&](unsigned a,unsigned b){
        const bool pa=channels_[a]->Pending(),pb=channels_[b]->Pending();
        if(pa!=pb)return pa; // pending ones first
        return pa&&channels_[a]->PendingStarted()<channels_[b]->PendingStarted();
    });
    bool begun=false;
    unsigned added=0;
    for(unsigned n=0;n<count_;++n){
        const unsigned c=order[n];
        if(IsHeld(c))continue; // delivered, waiting for its turn; cannot begin anyway
        auto& channel=*channels_[c];
        const bool pending=channel.Pending();
        const bool allow=!pending&&!begun&&MayBegin(c,now);
        Held f;
        f.valid=channel.TryGetCompletedPair(r,current,f.tracking,f.descriptor,f.ticket,allow);
        if(f.valid){
            Learn(c,now);
            f.channel=c;
            f.fresh=true;
            for(auto& h:held_)if(!h.valid){h=f;break;}
            ++added;
        }else if(!pending&&channel.Pending())begun=true;
    }
    // Deliver the oldest completed pair; the others wait (counted when they start waiting).
    int oldest=-1;
    for(unsigned i=0;i<kMaxChannels;++i)
        if(held_[i].valid&&(oldest<0||held_[i].tracking.predictedNs<held_[unsigned(oldest)].tracking.predictedNs))oldest=int(i);
    if(oldest<0)return false;
    const Held pair=held_[unsigned(oldest)];
    held_[unsigned(oldest)].valid=false;
    heldPairs_+=added-(pair.fresh?1u:0u);
    for(auto& h:held_)h.fresh=false;
    rendered=pair.tracking;d=pair.descriptor;ticket=pair.ticket;++delivered_[pair.channel];
    return true;
}
void PipelinedFrameProvider::Suspend()noexcept {
    // Held pairs were delivered by their channels: hand them back before cancelling, or those
    // channels would stay busy with pairs nobody will present.
    for(auto& h:held_)if(h.valid){channels_[h.channel]->PairConsumed(h.ticket,false);h.valid=false;}
    for(unsigned c=0;c<count_;++c)channels_[c]->Suspend();
}
}
