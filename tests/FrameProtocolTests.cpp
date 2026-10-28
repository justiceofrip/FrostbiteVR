#include "Test.h"
#include "fvr/ipc/FrameProtocol.h"
#include "fvr/ipc/TwoSlotFramePipeline.h"
#include <cstring>
#include <limits>
using namespace fvr;
// Included into the existing FrameProtocolTests candidate; no new target.
namespace {
bool PipelineSameTicket(const graphics::PairTicket& a,const graphics::PairTicket& b){return std::memcmp(&a,&b,sizeof(a))==0;}
bool PipelineRequestFixture(std::uint64_t id,std::int64_t deadline,ipc::FrameRequest& request){
    const runtime::PresentationRequirements requirements{100,120,29,19,4};
    runtime::TrackingFrame tracking{};tracking.generation=id+100;tracking.spaceGeneration=7;
    tracking.predictedNs=1000000000+std::int64_t(id)*11000000;tracking.focused=tracking.headValid=true;
    tracking.fov={math::FovTangents{-1,1,1,-1},math::FovTangents{-1,1,1,-1}};
    return ipc::Encode(requirements,tracking,id,deadline,request);
}
void PipelineSourceFixture(const ipc::FrameRequest& request,unsigned slot,graphics::TextureDescriptor& descriptor,
    graphics::PairTicket& ticket){
    descriptor={};descriptor.width=request.width;descriptor.height=request.height;descriptor.format=request.format;
    descriptor.adapterLow=request.adapterLow;descriptor.adapterHigh=request.adapterHigh;
    descriptor.resourceEpoch=slot+10;descriptor.sharing=graphics::TextureSharing::LegacyFenced;
    const std::array<std::uint64_t,2> handles{1000+std::uint64_t(slot)*2,1001+std::uint64_t(slot)*2};
    std::memcpy(descriptor.session.data(),handles.data(),sizeof(handles));
    ticket={};ticket.resourceEpoch=descriptor.resourceEpoch;ticket.session=descriptor.session;
    ticket.sequence=request.requestId;ticket.frameId=request.requestId+200;
    ticket.spaceGeneration=request.spaceGeneration;ticket.trackingGeneration=request.trackingGeneration;ticket.predictedNs=request.predictedNs;
}
int PipelineTwoSlotsOverlapAndRetire(){
    using namespace ipc;
    TwoSlotFramePipeline p{};FrameRequest a{},b{},c{},taken{};PipelineSlotKey ak{},bk{},ck{},key{};
    CHECK(PipelineRequestFixture(1,100,a)&&PipelineRequestFixture(2,120,b)&&PipelineRequestFixture(3,140,c));
    CHECK(!PipelineRequest(p,a,1,false,key));CHECK(PipelineEnable(p));CHECK(!PipelineEnable(p));
    CHECK(PipelineRequest(p,a,1,false,ak));CHECK(!PipelineRequest(p,b,2,false,key));
    CHECK(PipelineTake(p,2,key,taken)&&key==ak&&taken.deadlineQpc==100);
    graphics::TextureDescriptor ad{},bd{},cd{},out{};graphics::PairTicket at{},bt{},ct{},delivered{};
    PipelineSourceFixture(a,ak.slot,ad,at);
    CHECK(PipelinePublish(p,ak,3,ad,at)==PipelinePublishResult::Published);
    CHECK(PipelineDeliver(p,ak,4,out,delivered));CHECK(PipelineDelivered(p)==1);
    CHECK(PipelineRequest(p,b,5,true,bk)&&bk.slot!=ak.slot);
    PipelineSourceFixture(b,bk.slot,bd,bt);
    CHECK(PipelineTake(p,6,key,taken)&&key==bk&&taken.trackingGeneration==b.trackingGeneration);
    CHECK(!PipelineTake(p,7,key,taken)); // Never a second simultaneous native camera.
    CHECK(p.slots[ak.slot].frame.phase==Phase::Delivered&&PipelineSameTicket(p.slots[ak.slot].frame.ticket,at));
    CHECK(PipelinePublish(p,bk,8,bd,bt)==PipelinePublishResult::Published);
    CHECK(!PipelineDeliver(p,bk,9,out,delivered)); // No second host-owned lease.
    CHECK(!PipelineRequest(p,c,9,true,key));
    PipelineRetirement ar{},br{},cr{},duplicate{};
    CHECK(!PipelineClaimRetirement(p,ak,1000,ar)); // Delivered survives its deadline.
    auto wrong=at;++wrong.spaceGeneration;CHECK(!PipelineFeedback(p,wrong,true,false));
    CHECK(PipelineFeedback(p,at,true,false));CHECK(!PipelineFeedback(p,at,false,false));
    CHECK(PipelineDeliver(p,bk,10,out,delivered)&&PipelineSameTicket(delivered,bt));
    CHECK(!PipelineRequest(p,c,11,true,key)); // Logical ACK did not release source A.
    CHECK(PipelineClaimRetirement(p,ak,12,ar)&&ar.hasSource&&ar.outcome==Outcome::Consumed);
    CHECK(!PipelineClaimRetirement(p,ak,13,duplicate));
    CHECK(p.slots[ak.slot].frame.phase==Phase::Idle&&p.slots[ak.slot].nativeHeldRequest==ak.request);
    CHECK(!PipelineRequest(p,c,13,true,key)); // Idle still is NOT writable while callback is owed.
    auto forged=ar;forged.outcome=Outcome::Discarded;CHECK(!PipelineFinishRetirement(p,forged));
    forged=ar;++forged.ticket.resourceEpoch;CHECK(!PipelineFinishRetirement(p,forged));
    // This call models the exact graphics callback having completed; no GPU is claimed.
    CHECK(PipelineFinishRetirement(p,ar));CHECK(!PipelineFinishRetirement(p,ar));
    CHECK(PipelineRequest(p,c,14,true,ck)&&ck.slot==ak.slot&&ck.request!=ak.request);
    CHECK(PipelineTake(p,15,key,taken)&&key==ck);
    PipelineSourceFixture(c,ck.slot,cd,ct);
    CHECK(PipelinePublish(p,ak,16,ad,at)==PipelinePublishResult::Rejected); // Late old callback cannot mutate C.
    CHECK(!PipelineFinishRetirement(p,ar));
    PipelineClose(p);CHECK(!PipelineGraphicsDrained(p));
    CHECK(p.slots[bk.slot].frame.phase==Phase::Delivered&&p.slots[ck.slot].frame.phase==Phase::Cancelled);
    CHECK(!PipelineClaimRetirement(p,ck,17,cr)); // Restore/skip the camera before graphics retirement.
    CHECK(!PipelineTake(p,17,key,taken));CHECK(!PipelineRequest(p,c,17,false,key));
    CHECK(PipelineFeedback(p,bt,false,true));
    CHECK(PipelineClaimRetirement(p,bk,18,br)&&br.hasSource&&br.outcome==Outcome::Discarded);
    CHECK(PipelineFinishRetirement(p,br));CHECK(!PipelineGraphicsDrained(p));
    CHECK(PipelineSkip(p,ck));CHECK(PipelineClaimRetirement(p,ck,19,cr)&&!cr.hasSource&&cr.outcome==Outcome::Discarded);
    CHECK(PipelineFinishRetirement(p,cr)&&PipelineGraphicsDrained(p));
    CHECK(!PipelineEnable(p)); // Closed lifetime cannot be silently restarted.
    return 0;
}
int PipelineCancellationBeforeFeedback(){
    using namespace ipc;
    for(unsigned stage=0;stage<3;++stage){
        TwoSlotFramePipeline p{};CHECK(PipelineEnable(p));FrameRequest a{},b{},taken{};PipelineSlotKey ak{},bk{},key{};
        CHECK(PipelineRequestFixture(1,100,a)&&PipelineRequestFixture(2,100,b));
        CHECK(PipelineRequest(p,a,1,false,ak)&&PipelineTake(p,2,key,taken));
        graphics::TextureDescriptor ad{},bd{},out{};graphics::PairTicket at{},bt{},ticket{};
        PipelineSourceFixture(a,ak.slot,ad,at);
        CHECK(PipelinePublish(p,ak,3,ad,at)==PipelinePublishResult::Published&&PipelineDeliver(p,ak,4,out,ticket));
        CHECK(PipelineRequest(p,b,5,true,bk));PipelineSourceFixture(b,bk.slot,bd,bt);
        if(stage)CHECK(PipelineTake(p,6,key,taken));
        if(stage==2)CHECK(PipelinePublish(p,bk,7,bd,bt)==PipelinePublishResult::Published);
        // Models deferred cancellation and old ACK under one acquired mutex.
        CHECK(PipelineFeedback(p,at,true,true));CHECK(p.slots[ak.slot].frame.phase==Phase::Complete);
        CHECK(!PipelineTake(p,8,key,taken));CHECK(!PipelineDeliver(p,bk,8,out,ticket));
        PipelineRetirement ar{},br{};
        CHECK(PipelineClaimRetirement(p,ak,9,ar)&&ar.outcome==Outcome::Consumed);
        CHECK(PipelineFinishRetirement(p,ar));
        if(stage==0){CHECK(p.slots[bk.slot].frame.phase==Phase::Idle&&!p.slots[bk.slot].nativeHeldRequest);}
        else {
            if(stage==1){
                CHECK(p.slots[bk.slot].frame.phase==Phase::Cancelled);
                CHECK(PipelinePublish(p,bk,10,bd,bt)==PipelinePublishResult::RetirementRequired);
            }
            CHECK(PipelineClaimRetirement(p,bk,11,br)&&br.hasSource&&br.outcome==Outcome::Discarded);
            CHECK(PipelineFinishRetirement(p,br));CHECK(!PipelineFeedback(p,bt,true,false));
        }
        CHECK(PipelineGraphicsDrained(p));
    }
    return 0;
}
int PipelineOriginalDeadlineAndSourceIdentity(){
    using namespace ipc;
    TwoSlotFramePipeline p{};CHECK(PipelineEnable(p));FrameRequest a{},b{},taken{};PipelineSlotKey ak{},bk{},key{};
    CHECK(PipelineRequestFixture(1,100,a)&&PipelineRequestFixture(2,20,b));
    CHECK(PipelineRequest(p,a,1,false,ak)&&PipelineTake(p,2,key,taken));
    graphics::TextureDescriptor ad{},bd{},out{};graphics::PairTicket at{},bt{},ticket{};
    PipelineSourceFixture(a,ak.slot,ad,at);
    CHECK(PipelinePublish(p,ak,3,ad,at)==PipelinePublishResult::Published&&PipelineDeliver(p,ak,4,out,ticket));
    CHECK(PipelineRequest(p,b,5,true,bk)&&PipelineTake(p,6,key,taken));
    PipelineSourceFixture(b,bk.slot,bd,bt);
    auto named=bd;named.sharing=graphics::TextureSharing::NamedKeyed;
    CHECK(PipelinePublish(p,bk,7,named,bt)==PipelinePublishResult::Rejected);
    auto alias=bd;auto aliasTicket=bt;const auto held=graphics::LegacyResourceIds(ad);
    auto aliasHandles=graphics::LegacyResourceIds(alias);aliasHandles[0]=held[1];
    std::memcpy(alias.session.data(),aliasHandles.data(),sizeof(aliasHandles));aliasTicket.session=alias.session;
    CHECK(PipelinePublish(p,bk,7,alias,aliasTicket)==PipelinePublishResult::Rejected);
    CHECK(p.slots[bk.slot].hasSource==0&&p.slots[ak.slot].frame.phase==Phase::Delivered);
    CHECK(PipelinePublish(p,bk,8,bd,bt)==PipelinePublishResult::Published);
    PipelineRetirement br{},ar{};CHECK(!PipelineClaimRetirement(p,bk,19,br));
    CHECK(PipelineClaimRetirement(p,bk,20,br)&&br.outcome==Outcome::Discarded&&br.hasSource);
    CHECK(p.slots[ak.slot].frame.phase==Phase::Delivered&&PipelineSameTicket(p.slots[ak.slot].frame.ticket,at));
    CHECK(PipelineFinishRetirement(p,br));
    auto next=b;next.requestId=3;next.deadlineQpc=40;++next.trackingGeneration;
    CHECK(PipelineRequest(p,next,21,true,bk));CHECK(PipelineTake(p,22,key,taken));
    CHECK(taken.deadlineQpc==40&&taken.predictedNs==b.predictedNs); // No renewal or relabeling.
    PipelineSourceFixture(next,bk.slot,bd,bt);
    CHECK(PipelinePublish(p,bk,40,bd,bt)==PipelinePublishResult::RetirementRequired);
    CHECK(!PipelineDeliver(p,bk,40,out,ticket));CHECK(!PipelineGraphicsDrained(p));
    CHECK(PipelineClaimRetirement(p,bk,41,br)&&br.outcome==Outcome::Discarded&&PipelineSameTicket(br.ticket,bt));
    CHECK(PipelineFinishRetirement(p,br));
    PipelineClose(p);CHECK(!PipelineClaimRetirement(p,ak,1000,ar));
    CHECK(PipelineFeedback(p,at,false,true));CHECK(PipelineClaimRetirement(p,ak,1001,ar));
    CHECK(PipelineFinishRetirement(p,ar)&&PipelineGraphicsDrained(p));
    return 0;
}
int PipelineLookaheadBeforeDelivery(){
    using namespace ipc;
    for(unsigned stage=0;stage<3;++stage){
        TwoSlotFramePipeline p{};CHECK(PipelineEnable(p));FrameRequest a{},b{},c{},taken{};
        PipelineSlotKey ak{},bk{},key{};
        CHECK(PipelineRequestFixture(1,100,a)&&PipelineRequestFixture(2,120,b)&&PipelineRequestFixture(3,140,c));
        CHECK(PipelineLookaheadRequest(p,a,1,ak));
        CHECK(!PipelineRequest(p,b,2,false,key)); // Legacy policy is still narrower.
        graphics::TextureDescriptor ad{},bd{},out{};graphics::PairTicket at{},bt{},ticket{};
        PipelineSourceFixture(a,ak.slot,ad,at);
        if(stage){CHECK(PipelineLookaheadTake(p,2,key,taken)&&key==ak);}
        if(stage==2)CHECK(PipelinePublish(p,ak,3,ad,at)==PipelinePublishResult::Published);
        CHECK(PipelineLookaheadRequest(p,b,4,bk)&&bk.slot!=ak.slot);
        CHECK(!PipelineLookaheadRequest(p,c,5,key)); // No third texture set.
        if(!stage){CHECK(PipelineLookaheadTake(p,5,key,taken)&&key==ak&&taken.deadlineQpc==100);}
        if(stage<2){
            CHECK(!PipelineLookaheadTake(p,6,key,taken)); // One native camera.
            CHECK(PipelinePublish(p,ak,7,ad,at)==PipelinePublishResult::Published);
        }
        // A is still undelivered; B can start after A is genuinely published.
        CHECK(PipelineLookaheadTake(p,8,key,taken)&&key==bk&&taken.deadlineQpc==120&&
            taken.trackingGeneration==b.trackingGeneration&&taken.predictedNs==b.predictedNs);
        CHECK(p.slots[ak.slot].frame.phase==Phase::Ready&&PipelineSameTicket(p.slots[ak.slot].frame.ticket,at));
        PipelineSourceFixture(b,bk.slot,bd,bt);
        CHECK(PipelinePublish(p,bk,9,bd,bt)==PipelinePublishResult::Published);
        CHECK(!PipelineLookaheadDeliver(p,bk,10,out,ticket)); // Oldest first.
        CHECK(PipelineLookaheadDeliver(p,ak,10,out,ticket)&&PipelineSameTicket(ticket,at));
        CHECK(!PipelineLookaheadDeliver(p,bk,11,out,ticket)); // One host lease.
        auto wrong=at;++wrong.sequence;CHECK(!PipelineFeedback(p,wrong,true,false));
        CHECK(PipelineFeedback(p,at,true,false));
        CHECK(PipelineLookaheadDeliver(p,bk,12,out,ticket)&&PipelineSameTicket(ticket,bt));
        CHECK(!PipelineLookaheadRequest(p,c,13,key)); // ACK is not physical release.
        PipelineRetirement retired{};
        CHECK(PipelineClaimRetirement(p,ak,13,retired));
        CHECK(!PipelineLookaheadRequest(p,c,14,key));
        CHECK(PipelineFinishRetirement(p,retired));
        CHECK(PipelineLookaheadRequest(p,c,15,key)&&key.slot==ak.slot);
        PipelineClose(p);CHECK(!PipelineLookaheadTake(p,16,key,taken));
        CHECK(!PipelineLookaheadRequest(p,c,16,key));
    }
    return 0;
}
int PipelineLookaheadRequestOrderAfterReuse(){
    using namespace ipc;
    TwoSlotFramePipeline p{};CHECK(PipelineEnable(p));FrameRequest a{},b{},c{},taken{};PipelineSlotKey ak{},bk{},ck{},key{};
    CHECK(PipelineRequestFixture(1,100,a)&&PipelineRequestFixture(2,120,b)&&PipelineRequestFixture(3,140,c));
    CHECK(PipelineLookaheadRequest(p,a,1,ak)&&PipelineLookaheadRequest(p,b,2,bk));
    CHECK(ak.slot==0&&bk.slot==1&&PipelineLookaheadTake(p,3,key,taken)&&key==ak);
    graphics::TextureDescriptor ad{},bd{},out{};graphics::PairTicket at{},bt{},ticket{};
    PipelineSourceFixture(a,ak.slot,ad,at);
    CHECK(PipelinePublish(p,ak,4,ad,at)==PipelinePublishResult::Published&&PipelineLookaheadDeliver(p,ak,5,out,ticket));
    CHECK(PipelineFeedback(p,at,true,false));PipelineRetirement retired{};
    CHECK(PipelineClaimRetirement(p,ak,6,retired)&&PipelineFinishRetirement(p,retired));
    CHECK(PipelineLookaheadRequest(p,c,7,ck)&&ck.slot==0);
    CHECK(PipelineLookaheadTake(p,8,key,taken)&&key==bk&&taken.requestId==2); // Not lower slot0/C.
    CHECK(!PipelineLookaheadTake(p,9,key,taken));
    PipelineSourceFixture(b,bk.slot,bd,bt);
    CHECK(PipelinePublish(p,bk,10,bd,bt)==PipelinePublishResult::Published);
    CHECK(PipelineLookaheadTake(p,11,key,taken)&&key==ck&&taken.deadlineQpc==140);
    PipelineCancelSuccessors(p);
    CHECK(p.slots[bk.slot].frame.phase==Phase::Complete&&p.slots[ck.slot].frame.phase==Phase::Cancelled);
    CHECK(!PipelineLookaheadDeliver(p,bk,12,out,ticket)&&!PipelineLookaheadTake(p,12,key,taken));
    CHECK(PipelineSkip(p,ck));
    CHECK(PipelineClaimRetirement(p,bk,13,retired)&&retired.hasSource&&retired.outcome==Outcome::Discarded);
    CHECK(PipelineFinishRetirement(p,retired));
    CHECK(PipelineClaimRetirement(p,ck,14,retired)&&!retired.hasSource&&retired.outcome==Outcome::Discarded);
    CHECK(PipelineFinishRetirement(p,retired)&&PipelineGraphicsDrained(p));
    return 0;
}
int PipelineLookaheadOriginalDeadline(){
    using namespace ipc;
    TwoSlotFramePipeline p{};CHECK(PipelineEnable(p));FrameRequest a{},b{},taken{};PipelineSlotKey ak{},bk{},key{};
    CHECK(PipelineRequestFixture(1,10,a)&&PipelineRequestFixture(2,30,b));
    CHECK(PipelineLookaheadRequest(p,a,1,ak)&&PipelineLookaheadRequest(p,b,2,bk));
    CHECK(!PipelineLookaheadTake(p,10,key,taken)&&!key.request&&!taken.requestId);
    CHECK(p.slots[ak.slot].frame.phase==Phase::Idle&&!p.slots[ak.slot].nativeHeldRequest);
    CHECK(p.slots[bk.slot].frame.phase==Phase::Requested); // No same-call skip-to-younger.
    CHECK(PipelineLookaheadTake(p,11,key,taken)&&key==bk&&taken.deadlineQpc==30&&taken.predictedNs==b.predictedNs);
    graphics::TextureDescriptor bd{},out{};graphics::PairTicket bt{},ticket{};PipelineSourceFixture(b,bk.slot,bd,bt);
    CHECK(PipelinePublish(p,bk,30,bd,bt)==PipelinePublishResult::RetirementRequired);
    CHECK(!PipelineLookaheadDeliver(p,bk,31,out,ticket));
    PipelineRetirement retired{};CHECK(PipelineClaimRetirement(p,bk,31,retired)&&retired.outcome==Outcome::Discarded);
    CHECK(PipelineFinishRetirement(p,retired)&&PipelineGraphicsDrained(p));
    return 0;
}
int PipelineCaptureReleaseOverlap(){
    using namespace ipc;
    TwoSlotFramePipeline p{};PipelineCaptureReleases released{};CHECK(PipelineEnable(p));
    FrameRequest a{},b{},c{},taken{};PipelineSlotKey ak{},bk{},ck{},key{};
    CHECK(PipelineRequestFixture(1,100,a)&&PipelineRequestFixture(2,120,b)&&PipelineRequestFixture(3,140,c));
    CHECK(PipelineLookaheadRequest(p,a,1,ak)&&PipelineLookaheadRequest(p,b,1,bk));
    CHECK(!PipelineMarkCaptureReleased(p,released,ak)); // Requested is not restored.
    CHECK(PipelineLookaheadTake(p,2,key,taken,&released)&&key==ak);
    auto wrong=ak;++wrong.request;CHECK(!PipelineMarkCaptureReleased(p,released,wrong));
    wrong=ak;wrong.reserved=1;CHECK(!PipelineMarkCaptureReleased(p,released,wrong));
    CHECK(!PipelineLookaheadTake(p,3,key,taken,&released));
    CHECK(PipelineMarkCaptureReleased(p,released,ak)&&PipelineMarkCaptureReleased(p,released,ak));
    CHECK(!PipelineCaptureReleasesValid(p,released,false)); // Default wire rejects markers.
    CHECK(!PipelineLookaheadTake(p,3,key,taken)); // Default policy still waits for publication.
    CHECK(PipelineLookaheadTake(p,4,key,taken,&released)&&key==bk&&taken.deadlineQpc==120);
    CHECK(p.slots[ak.slot].frame.phase==Phase::Rendering&&p.slots[bk.slot].frame.phase==Phase::Rendering);
    CHECK(PipelineCaptureReleasesValid(p,released,true));
    auto corrupt=released;corrupt[ak.slot]=0;CHECK(!PipelineCaptureReleasesValid(p,corrupt,true));
    corrupt=released;++corrupt[ak.slot];CHECK(!PipelineCaptureReleasesValid(p,corrupt,true));
    CHECK(!PipelineLookaheadRequest(p,c,5,key)&&!PipelineLookaheadTake(p,5,key,taken,&released));
    graphics::TextureDescriptor ad{},bd{},out{};graphics::PairTicket at{},bt{},got{};
    PipelineSourceFixture(a,ak.slot,ad,at);PipelineSourceFixture(b,bk.slot,bd,bt);
    // B can finish first, but this is not permission to deliver it before A.
    CHECK(PipelinePublish(p,bk,6,bd,bt)==PipelinePublishResult::Published);
    CHECK(!PipelineLookaheadDeliver(p,bk,7,out,got)&&!PipelineMarkCaptureReleased(p,released,bk));
    CHECK(PipelinePublish(p,ak,8,ad,at)==PipelinePublishResult::Published);
    CHECK(!PipelineMarkCaptureReleased(p,released,ak)); // No initial mark after source adoption.
    CHECK(PipelineLookaheadDeliver(p,ak,9,out,got)&&PipelineSameTicket(got,at));
    CHECK(!PipelineLookaheadDeliver(p,bk,9,out,got)&&!PipelineLookaheadRequest(p,c,9,key));
    CHECK(PipelineFeedback(p,at,true,false));PipelineRetirement retired{};
    CHECK(PipelineClaimRetirement(p,ak,10,retired)&&released[ak.slot]==ak.request);
    auto altered=retired;++altered.ticket.frameId;
    CHECK(!PipelineFinishRetirement(p,altered,&released)&&released[ak.slot]==ak.request);
    CHECK(PipelineFinishRetirement(p,retired,&released)&&released[ak.slot]==0);
    CHECK(PipelineLookaheadRequest(p,c,11,ck)&&ck.slot==ak.slot);
    CHECK(!PipelineMarkCaptureReleased(p,released,ak)); // Reused physical slot cannot inherit A.
    CHECK(PipelineLookaheadTake(p,12,key,taken,&released)&&key==ck&&taken.predictedNs==c.predictedNs);
    CHECK(PipelineLookaheadDeliver(p,bk,13,out,got)&&PipelineSameTicket(got,bt));
    PipelineClose(p);CHECK(!PipelineMarkCaptureReleased(p,released,ck));
    CHECK(!PipelineLookaheadTake(p,14,key,taken,&released));
    return 0;
}
int PipelineCaptureReleaseCancellationAndExpiry(){
    using namespace ipc;
    for(bool expire:{false,true}){
        TwoSlotFramePipeline p{};PipelineCaptureReleases released{};CHECK(PipelineEnable(p));
        FrameRequest a{},b{},taken{};PipelineSlotKey ak{},bk{},key{};
        CHECK(PipelineRequestFixture(1,10,a)&&PipelineRequestFixture(2,30,b));
        CHECK(PipelineLookaheadRequest(p,a,1,ak)&&PipelineLookaheadRequest(p,b,1,bk));
        CHECK(PipelineLookaheadTake(p,2,key,taken,&released)&&PipelineMarkCaptureReleased(p,released,ak));
        CHECK(PipelineLookaheadTake(p,3,key,taken,&released)&&key==bk);
        if(expire){
            graphics::TextureDescriptor ad{};graphics::PairTicket at{};PipelineSourceFixture(a,ak.slot,ad,at);
            CHECK(PipelinePublish(p,ak,10,ad,at)==PipelinePublishResult::RetirementRequired);
        }else{
            PipelineCancelSuccessors(p);CHECK(PipelineCaptureReleasesValid(p,released,true));
            CHECK(!PipelineLookaheadTake(p,4,key,taken,&released)); // Cancelled blocks even when marked.
            CHECK(!PipelineMarkCaptureReleased(p,released,ak)&&!PipelineMarkCaptureReleased(p,released,bk));
            CHECK(PipelineSkip(p,ak));
        }
        CHECK(released[ak.slot]==ak.request);PipelineRetirement retired{};
        CHECK(PipelineClaimRetirement(p,ak,11,retired)&&retired.outcome==Outcome::Discarded);
        CHECK(PipelineFinishRetirement(p,retired,&released)&&released[ak.slot]==0);
        CHECK(PipelineSkip(p,bk)&&PipelineClaimRetirement(p,bk,12,retired));
        CHECK(PipelineFinishRetirement(p,retired,&released)&&PipelineGraphicsDrained(p));
        CHECK(released==PipelineCaptureReleases{});
    }
    return 0;
}
}

int main(){
    CHECK(PipelineTwoSlotsOverlapAndRetire()==0);
    CHECK(PipelineCancellationBeforeFeedback()==0);
    CHECK(PipelineOriginalDeadlineAndSourceIdentity()==0);
    CHECK(PipelineLookaheadBeforeDelivery()==0);
    CHECK(PipelineLookaheadRequestOrderAfterReuse()==0);
    CHECK(PipelineLookaheadOriginalDeadline()==0);
    CHECK(PipelineCaptureReleaseOverlap()==0);
    CHECK(PipelineCaptureReleaseCancellationAndExpiry()==0);
    runtime::PresentationRequirements requirements{100,120,29,0x87654321,-3};runtime::TrackingFrame tracking{};
    tracking.generation=0x123456789abcdef0ULL;tracking.spaceGeneration=0x100000007ULL;tracking.predictedNs=10000000000;
    tracking.worldUnitsPerMeter=2.5f;
    tracking.focused=tracking.headValid=true;tracking.referenceHead.position.y=1.6f;tracking.head=tracking.referenceHead;tracking.head.position.x=.2f;
    tracking.eyes={tracking.head,tracking.head};tracking.eyes[0].position.x-=.032f;tracking.eyes[1].position.x+=.032f;
    tracking.fov={math::FovTangents{-1.2f,.9f,1.1f,-1.f},math::FovTangents{-.9f,1.2f,1.1f,-1.f}};
    ipc::FrameRequest request{};CHECK(ipc::Encode(requirements,tracking,0x100000001ULL,1000,request));
    runtime::PresentationRequirements decoded{};runtime::TrackingFrame sample{};CHECK(ipc::Decode(request,decoded,sample));
    CHECK(sample.worldUnitsPerMeter==2.5f);
    auto invalidScale=request;invalidScale.worldUnitsPerMeter=0;CHECK(!ipc::Decode(invalidScale,decoded,sample));
    invalidScale=request;invalidScale.reservedScale=1;CHECK(!ipc::Decode(invalidScale,decoded,sample));
    CHECK(decoded.adapterHigh==-3&&decoded.adapterLow==requirements.adapterLow&&sample.generation==tracking.generation&&sample.predictedNs==tracking.predictedNs);
    CHECK(sample.eyes[0].position.x==tracking.eyes[0].position.x&&sample.referenceHead.position.y==1.6f&&sample.fov[1].right==1.2f);
    auto corrupt=request;corrupt.bytes--;CHECK(!ipc::Decode(corrupt,decoded,sample));corrupt=request;corrupt.flags=1;CHECK(!ipc::Decode(corrupt,decoded,sample));
    corrupt=request;corrupt.referenceHead.values[6]=0;CHECK(!ipc::Decode(corrupt,decoded,sample));corrupt=request;corrupt.eyes[1].values[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!ipc::Decode(corrupt,decoded,sample));
    corrupt=request;corrupt.fov[0][1]=corrupt.fov[0][0];CHECK(!ipc::Decode(corrupt,decoded,sample));corrupt=request;corrupt.reserved[1]=1;CHECK(!ipc::Decode(corrupt,decoded,sample));
    tracking.focused=false;CHECK(!ipc::Encode(requirements,tracking,1,1000,corrupt));tracking.focused=true;
    graphics::TextureDescriptor d{};d.width=requirements.width;d.height=requirements.height;d.format=requirements.format;d.adapterLow=requirements.adapterLow;d.adapterHigh=requirements.adapterHigh;d.resourceEpoch=4;d.session[0]=7;
    graphics::PairTicket ticket{};ticket.session=d.session;ticket.resourceEpoch=d.resourceEpoch;ticket.sequence=1;ticket.frameId=0x100000009ULL;
    ticket.trackingGeneration=tracking.generation;ticket.spaceGeneration=tracking.spaceGeneration;ticket.predictedNs=tracking.predictedNs;
    ipc::FrameSlot slot{};ipc::FrameRequest taken{};graphics::TextureDescriptor got{};graphics::PairTicket pair{};
    CHECK(ipc::Begin(slot,request,1));CHECK(!ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));CHECK(!ipc::Take(slot,3,taken));
    auto badTicket=ticket;--badTicket.predictedNs;CHECK(!ipc::Publish(slot,request.requestId,5,d,badTicket));CHECK(!ipc::Publish(slot,1,5,d,ticket));
    CHECK(ipc::Publish(slot,request.requestId,5,d,ticket));CHECK(!ipc::Publish(slot,request.requestId,6,d,ticket));
    CHECK(ipc::Deliver(slot,request.requestId,6,got,pair));CHECK(!ipc::Deliver(slot,request.requestId,6,got,pair));
    // A timeout cannot reclaim a texture that the GPU consumer already owns.
    ipc::Cancel(slot,request.requestId);CHECK(ipc::Reap(slot,request.requestId,2000)==ipc::Outcome::Pending);
    CHECK(!ipc::Acknowledge(slot,2,true));CHECK(ipc::Acknowledge(slot,request.requestId,true));CHECK(ipc::Reap(slot,request.requestId,2000)==ipc::Outcome::Consumed);CHECK(slot.phase==ipc::Phase::Idle);
    // Timeout before take; timeout during render; timeout after publish.
    CHECK(ipc::Begin(slot,request,1));CHECK(!ipc::Take(slot,1000,taken));CHECK(slot.phase==ipc::Phase::Idle);
    CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));ipc::Cancel(slot,request.requestId);
    CHECK(slot.phase==ipc::Phase::Cancelled);CHECK(!ipc::Begin(slot,request,3));CHECK(!ipc::Publish(slot,request.requestId,4,d,ticket));CHECK(slot.phase==ipc::Phase::Idle);
    CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));CHECK(ipc::Publish(slot,request.requestId,3,d,ticket));
    CHECK(!ipc::Deliver(slot,request.requestId,1000,got,pair));CHECK(ipc::Reap(slot,request.requestId,1000)==ipc::Outcome::Discarded);
    CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));CHECK(ipc::Publish(slot,request.requestId,3,d,ticket));
    CHECK(ipc::Reap(slot,request.requestId,1000)==ipc::Outcome::Discarded);
    // Skip a frame without a GPU pair, then reject the previous frame's late write.
    CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));ipc::Skip(slot,request.requestId);
    auto next=request;++next.requestId;CHECK(ipc::Begin(slot,next,3));CHECK(!ipc::Publish(slot,request.requestId,4,d,ticket));CHECK(slot.phase==ipc::Phase::Requested);
    slot.phase=ipc::Phase::Closed;CHECK(!ipc::Begin(slot,request,1));CHECK(ipc::Reap(slot,next.requestId,2)==ipc::Outcome::Discarded);
    return 0;
}
