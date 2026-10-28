#pragma once
#include "fvr/ipc/FrameProtocol.h"
#include <cstring>

namespace fvr::ipc {
// Candidate scheduling policy, not a mapped channel or a GPU owner. Every call
// requires the same channel mutex. Graphics methods additionally require the
// existing native graphics-owner guard. Neither requirement is replaced here.
// v8 remains the default path; FramePipelineChannel owns explicit v9/v10 adapters.
struct PipelineSlotKey {
    std::uint64_t request=0;
    std::uint32_t slot=UINT32_MAX,reserved=0;
    bool operator==(const PipelineSlotKey&)const=default;
};
struct PipelineSlot {
    FrameSlot frame{};
    // Survives logical Reap/Idle until the graphics callback has actually
    // released this exact source set. A free wire phase alone is insufficient.
    std::uint64_t nativeHeldRequest=0;
    std::uint32_t hasSource=0,retiring=0;
};
template<std::size_t N> struct BasicFramePipeline {
    static_assert(N==2||N==3);
    std::array<PipelineSlot,N> slots{};
    std::uint64_t lastRequest=0;
    std::uint32_t enabled=0,closing=0;
};
using TwoSlotFramePipeline=BasicFramePipeline<2>;
using ThreeSlotFramePipeline=BasicFramePipeline<3>;
static_assert(sizeof(ThreeSlotFramePipeline)==1336);
static_assert(sizeof(PipelineSlotKey)==16 && sizeof(PipelineSlot)==440);
static_assert(sizeof(TwoSlotFramePipeline)==896);
static_assert(std::is_trivially_copyable_v<TwoSlotFramePipeline>);
struct PipelineRetirement {
    PipelineSlotKey key{};
    graphics::TextureDescriptor descriptor{};
    graphics::PairTicket ticket{};
    Outcome outcome=Outcome::Pending;
    bool hasSource=false;
};
enum class PipelinePublishResult {Rejected,Published,RetirementRequired};

template<class P> inline bool PipelineKeyMatches(const P& p,const PipelineSlotKey& key)noexcept {
    return key.slot<p.slots.size()&&!key.reserved&&key.request&&p.slots[key.slot].nativeHeldRequest==key.request&&
        p.slots[key.slot].frame.request.requestId==key.request;
}
// Kept outside PipelineSlot so the existing wire slot layout is unchanged.
// A release is camera/capture-read ordering evidence, never a GPU fence or ACK.
template<std::size_t N> using BasicPipelineCaptureReleases=std::array<std::uint64_t,N>;
using PipelineCaptureReleases=BasicPipelineCaptureReleases<2>;
template<class P,class Releases> inline bool PipelineCaptureReleasesValid(const P& p,
    const Releases& released,bool enabled)noexcept {
    if(released.size()!=p.slots.size())return false;
    unsigned rendering=0,unreleased=0;
    for(unsigned index=0;index<p.slots.size();++index){const auto& s=p.slots[index];const auto mark=released[index];
        if(mark&&(!enabled||mark!=s.nativeHeldRequest||mark!=s.frame.request.requestId||
            s.frame.phase==Phase::Requested||s.frame.phase==Phase::Closed))return false;
        if(s.frame.phase==Phase::Rendering||s.frame.phase==Phase::Cancelled){++rendering;unreleased+=!mark;}
    }
    return rendering<=(enabled?p.slots.size():1u)&&unreleased<=1;
}
template<class P,class Releases> inline bool PipelineMarkCaptureReleased(P& p,Releases& released,
    const PipelineSlotKey& key)noexcept {
    if(!p.enabled||p.closing||!PipelineKeyMatches(p,key)||!PipelineCaptureReleasesValid(p,released,true))return false;
    const auto& s=p.slots[key.slot];
    if(s.frame.phase!=Phase::Rendering||s.hasSource||s.retiring||
        (released[key.slot]&&released[key.slot]!=key.request))return false;
    released[key.slot]=key.request;return true;
}
template<class P> inline bool PipelineEnable(P& p)noexcept {
    if(p.enabled||p.closing||p.lastRequest)return false;
    for(const auto& s:p.slots)if(s.frame.phase!=Phase::Idle||s.nativeHeldRequest||s.hasSource||s.retiring)return false;
    p.enabled=1;return true;
}
template<class P> inline unsigned PipelineDelivered(const P& p)noexcept {
    unsigned count=0;for(const auto& s:p.slots)count+=s.frame.phase==Phase::Delivered;return count;
}
// Host: exactly the existing BeginRequest or QueueRequest decision. Queueing
// adopts the immutable original pose/deadline; it cannot replace or renew it.
template<class P> inline bool PipelineRequest(P& p,const FrameRequest& request,std::int64_t now,
    bool behindDelivered,PipelineSlotKey& key)noexcept {
    key={};if(!p.enabled||p.closing||!request.requestId||request.requestId<=p.lastRequest||
        PipelineDelivered(p)!=(behindDelivered?1u:0u))return false;
    for(const auto& s:p.slots)if(s.frame.phase==Phase::Requested||s.frame.phase==Phase::Rendering||
        s.frame.phase==Phase::Ready||s.frame.phase==Phase::Cancelled)return false;
    for(unsigned index=0;index<p.slots.size();++index){auto& s=p.slots[index];
        if(s.frame.phase!=Phase::Idle||s.nativeHeldRequest||s.hasSource||s.retiring)continue;
        if(!Begin(s.frame,request,now))return false;
        p.lastRequest=request.requestId;key={request.requestId,index,0};return true;
    }
    return false;
}
// Admission thread: at most one camera render, even when another source is
// Delivered. This performs no resource mutation and no graphics feedback.
template<class P> inline bool PipelineTake(P& p,std::int64_t now,PipelineSlotKey& key,FrameRequest& request)noexcept {
    key={};request={};if(!p.enabled||p.closing)return false;
    for(const auto& s:p.slots)if(s.frame.phase==Phase::Rendering||s.frame.phase==Phase::Cancelled)return false;
    for(unsigned index=0;index<p.slots.size();++index){auto& s=p.slots[index];
        if(s.nativeHeldRequest||s.hasSource||s.retiring)continue;
        if(Take(s.frame,now,request)){s.nativeHeldRequest=request.requestId;key={request.requestId,index,0};return true;}
    }
    return false;
}
// Negotiated look-ahead capability ONLY. Unlike PipelineRequest, another
// Requested/Rendering/Ready source is allowed. A physically held slot is never
// reusable, even after a logical ACK; the adapter bounds its undelivered FIFO.
template<class P> inline bool PipelineLookaheadRequest(P& p,const FrameRequest& request,std::int64_t now,
    PipelineSlotKey& key)noexcept {
    key={};if(!p.enabled||p.closing||!request.requestId||request.requestId<=p.lastRequest||PipelineDelivered(p)>1)return false;
    // v10 still admits at most two live undelivered poses, even with three
    // physical sources. Cancelled/terminal ownership is never a new pose.
    if(p.slots.size()==3){unsigned pending=0;for(const auto& s:p.slots)
        pending+=s.frame.phase==Phase::Requested||s.frame.phase==Phase::Rendering||s.frame.phase==Phase::Ready;
        if(pending>=2)return false;}
    for(unsigned index=0;index<p.slots.size();++index){auto& s=p.slots[index];
        if(s.frame.phase!=Phase::Idle||s.nativeHeldRequest||s.hasSource||s.retiring)continue;
        if(!Begin(s.frame,request,now))return false;
        p.lastRequest=request.requestId;key={request.requestId,index,0};return true;
    }
    return false;
}
// Slot index is NOT request order after a slot has been retired and reused.
// Expiring the oldest request does not also take a later request in this call.
template<class P,class Releases=PipelineCaptureReleases> inline bool PipelineLookaheadTake(P& p,std::int64_t now,PipelineSlotKey& key,FrameRequest& request,
    const Releases* released=nullptr)noexcept {
    key={};request={};if(!p.enabled||p.closing)return false;
    if(released&&!PipelineCaptureReleasesValid(p,*released,true))return false;
    unsigned oldest=unsigned(p.slots.size());
    for(unsigned index=0;index<p.slots.size();++index){const auto& s=p.slots[index];
        if(s.frame.phase==Phase::Cancelled)return false;
        if(s.frame.phase==Phase::Rendering&&(!released||!s.nativeHeldRequest||
            (*released)[index]!=s.nativeHeldRequest))return false;
        if(s.frame.phase==Phase::Requested&&!s.nativeHeldRequest&&!s.hasSource&&!s.retiring&&
            (oldest==p.slots.size()||s.frame.request.requestId<p.slots[oldest].frame.request.requestId))oldest=index;
    }
    if(oldest==p.slots.size())return false;auto& s=p.slots[oldest];
    if(!Take(s.frame,now,request))return false;
    s.nativeHeldRequest=request.requestId;key={request.requestId,oldest,0};return true;
}
// Graphics owner, after exact native restoration and the real producer fence.
// Rejected leaves ownership with caller. Both other results adopt the source;
// cancellation/expiry requires the same exact negative graphics retirement.
template<class P> inline PipelinePublishResult PipelinePublish(P& p,const PipelineSlotKey& key,
    std::int64_t now,const graphics::TextureDescriptor& descriptor,const graphics::PairTicket& ticket)noexcept {
    if(!PipelineKeyMatches(p,key))return PipelinePublishResult::Rejected;
    auto& s=p.slots[key.slot];runtime::PresentationRequirements requirements{};runtime::TrackingFrame tracking{};
    if(s.retiring||s.hasSource||(s.frame.phase!=Phase::Rendering&&s.frame.phase!=Phase::Cancelled)||
        descriptor.sharing!=graphics::TextureSharing::LegacyFenced||!Decode(s.frame.request,requirements,tracking)||
        !runtime::PairMatchesFrame(requirements,tracking,descriptor,ticket))return PipelinePublishResult::Rejected;
    // Defensive publication validation; the bridge must select separate source
    // sets BEFORE any GPU writes, using the reserved slot key, not this check.
    const auto handles=graphics::LegacyResourceIds(descriptor);
    for(unsigned other=0;other<p.slots.size();++other)if(other!=key.slot&&p.slots[other].hasSource){
        const auto held=graphics::LegacyResourceIds(p.slots[other].frame.descriptor);
        for(auto handle:handles)for(auto old:held)if(handle==old)return PipelinePublishResult::Rejected;
    }
    const bool published=!p.closing&&Publish(s.frame,key.request,now,descriptor,ticket);
    // Keep the adopted exact ticket even when Publish rejected its old deadline.
    s.frame.descriptor=descriptor;s.frame.ticket=ticket;s.hasSource=1;
    if(!published){s.frame.phase=Phase::Complete;s.frame.consumed=0;return PipelinePublishResult::RetirementRequired;}
    return PipelinePublishResult::Published;
}
// Host: serial delivery is intentional. Ready B can exist while A is being
// copied, but no second host lease/admission is granted until A is acknowledged.
template<class P> inline bool PipelineDeliver(P& p,const PipelineSlotKey& key,std::int64_t now,
    graphics::TextureDescriptor& descriptor,graphics::PairTicket& ticket)noexcept {
    if(!p.enabled||p.closing||PipelineDelivered(p)||!PipelineKeyMatches(p,key))return false;
    auto& s=p.slots[key.slot];if(s.retiring||!s.hasSource)return false;
    return Deliver(s.frame,key.request,now,descriptor,ticket);
}
template<class P> inline bool PipelineLookaheadDeliver(P& p,const PipelineSlotKey& key,std::int64_t now,
    graphics::TextureDescriptor& descriptor,graphics::PairTicket& ticket)noexcept {
    // The adapter additionally retains terminal/missing oldest IDs until one
    // Timeout result is observed. This check prevents live wire reordering.
    for(const auto& s:p.slots)if(s.frame.request.requestId&&s.frame.request.requestId<key.request&&
        (s.frame.phase==Phase::Requested||s.frame.phase==Phase::Rendering||
         s.frame.phase==Phase::Ready||s.frame.phase==Phase::Cancelled))return false;
    return PipelineDeliver(p,key,now,descriptor,ticket);
}
// Host: all invalidated successors are cancelled under this SAME mutex before
// old feedback. Delivered pixels remain owned by the consumer until real ACK.
template<class P> inline void PipelineCancelSuccessors(P& p)noexcept {
    for(auto& s:p.slots)Cancel(s.frame,s.frame.request.requestId);
}
template<class P> inline bool PipelineFeedback(P& p,const graphics::PairTicket& ticket,bool consumed,
    bool cancelSuccessors)noexcept {
    if(cancelSuccessors)PipelineCancelSuccessors(p);
    for(auto& s:p.slots){
        if(s.frame.phase!=Phase::Delivered||!s.hasSource||s.retiring||
            std::memcmp(&s.frame.ticket,&ticket,sizeof(ticket)))continue;
        return Acknowledge(s.frame,s.frame.request.requestId,consumed);
    }
    return false;
}
// Graphics owner, after cancelling/restoring the active camera. A captured but
// unpublished producer set remains the bridge's responsibility, as today.
template<class P> inline bool PipelineSkip(P& p,const PipelineSlotKey& key)noexcept {
    if(!PipelineKeyMatches(p,key))return false;auto& s=p.slots[key.slot];
    if(s.hasSource||s.retiring||(s.frame.phase!=Phase::Rendering&&s.frame.phase!=Phase::Cancelled))return false;
    Skip(s.frame,key.request);return s.frame.phase==Phase::Idle;
}
// Graphics owner: claim exactly one terminal outcome. Do not invoke foreign
// graphics callbacks under the IPC mutex; keep this slot held across that gap.
template<class P> inline bool PipelineClaimRetirement(P& p,const PipelineSlotKey& key,std::int64_t now,
    PipelineRetirement& retirement)noexcept {
    retirement={};if(!PipelineKeyMatches(p,key))return false;auto& s=p.slots[key.slot];
    if(s.retiring||(s.frame.phase!=Phase::Idle&&s.frame.phase!=Phase::Ready&&s.frame.phase!=Phase::Complete))return false;
    const auto result=Reap(s.frame,key.request,now);if(result==Outcome::Pending)return false;
    s.retiring=1;retirement={key,s.frame.descriptor,s.frame.ticket,result,s.hasSource!=0};return true;
}
// Commit ONLY after the bridge's exact-slot Acknowledge(consumed) or full
// resource retirement has run successfully on the original graphics owner.
// On callback failure leave the slot held and fail the owning session closed.
template<class P,class Releases=PipelineCaptureReleases> inline bool PipelineFinishRetirement(P& p,const PipelineRetirement& retirement,
    Releases* released=nullptr)noexcept {
    if(!PipelineKeyMatches(p,retirement.key))return false;auto& s=p.slots[retirement.key.slot];
    if(!s.retiring||s.frame.phase!=Phase::Idle||retirement.outcome!=(s.frame.consumed==1?Outcome::Consumed:Outcome::Discarded)||
        retirement.hasSource!=(s.hasSource!=0)||
        std::memcmp(&s.frame.ticket,&retirement.ticket,sizeof(retirement.ticket))||
        std::memcmp(&s.frame.descriptor,&retirement.descriptor,sizeof(retirement.descriptor)))return false;
    s={};if(released)(*released)[retirement.key.slot]=0;return true;
}
template<class P> inline void PipelineClose(P& p)noexcept {
    p.closing=1;PipelineCancelSuccessors(p);
    // Delivered remains held. Normal host teardown must send exact negative
    // feedback after retiring its copy. Peer-loss proof belongs to the adapter;
    // this policy does not infer safe reuse from a timeout or a closed channel.
}
template<class P> inline bool PipelineGraphicsDrained(const P& p)noexcept {
    for(const auto& s:p.slots)if(s.nativeHeldRequest||s.hasSource||s.retiring||s.frame.phase!=Phase::Idle)return false;
    return true;
}
}
