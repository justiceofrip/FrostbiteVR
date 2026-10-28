#include "fvr/ipc/StagedFrameProducer.h"
namespace fvr::ipc {
bool StagedFrameProducer::Connect(const std::wstring& token,bool twoSourceSlots,bool thirdSource)noexcept {
    CloseGraphics();if(generation_==UINT64_MAX)return false;
    ++generation_;return channel_.ConnectProducer(token,twoSourceSlots,thirdSource);
}
void StagedFrameProducer::CloseGraphics()noexcept {
    if(channel_.SourceSlotCount()>1){ClosePipelineGraphics();return;}
    // Caller has stopped both hooks; no callback may race destruction/connection.
    DrainRetiredGraphics();
    channel_.Close();
    if(step_==Step::Publishing||step_==Step::Waiting)feedback_.PairConsumed(ticket_,false);
    step_=Step::Idle;lease_={};ticket_={};descriptor_={};sceneDepth_={};
}
bool StagedFrameProducer::SetDeferredGraphicsRetirement(bool enabled)noexcept {
    Lock lock(gate_);if(!lock||channel_.SourceSlotCount()>1||step_!=Step::Idle||retired_.outcome!=Outcome::Pending)return false;
    deferredGraphicsRetirement_=enabled;return true;
}
bool StagedFrameProducer::Matches(const FrameLease& lease)const noexcept {
    return lease.channelGeneration==generation_&&lease.requestId&&lease.requestId==lease_.requestId&&lease.native==lease_.native;
}
bool StagedFrameProducer::TryBegin(const NativeFrameKey& native,FrameLease& out,bool allowDeferred)noexcept {
    if(!native.owner||!native.frameId||!native.deviceEpoch)return false;
    Lock lock(gate_);if(!lock)return false;
    if(native.owner==lastNative_.owner&&native.deviceEpoch==lastNative_.deviceEpoch&&native.frameId<=lastNative_.frameId)return false;
    if(channel_.SourceSlotCount()>1)return TryBeginPipeline(native,out);
    if(step_==Step::Waiting&&deferredGraphicsRetirement_&&allowDeferred&&retired_.outcome==Outcome::Pending){
        const auto outcome=channel_.PollOutcome();if(outcome==Outcome::Pending)return false;
        // The IPC slot is terminal, but GPU resource retirement is still owed
        // on its original graphics owner. Preserve the old identity/outcome
        // independently of the successor, without invoking PairConsumed here.
        retired_={lease_,ticket_,outcome};
        if(observer_)observer_(observerContext_,retired_.lease,retired_.ticket,StagedFrameObservation::Acknowledged);
        step_=Step::Idle;lease_={};ticket_={};descriptor_={};sceneDepth_={};
    }
    if(step_!=Step::Idle)return false;
    FrameRequest request{};if(channel_.TryTake(request)!=ChannelResult::Ok)return false;
    FrameLease next{};next.channelGeneration=generation_;next.requestId=request.requestId;next.native=native;
    if(!Decode(request,next.requirements,next.tracking)){step_=Step::Skipping;return false;}
    sceneDepth_={};bodyProps_={};bodyProps_.frameId=native.frameId;bodyProps_.spaceGeneration=next.tracking.spaceGeneration;bodyProps_.trackingGeneration=next.tracking.generation;
    lease_=next;lastNative_=native;step_=Step::Rendering;out=next;return true;
}
void StagedFrameProducer::DrainRetiredGraphics()noexcept {
    if(retired_.outcome==Outcome::Pending)return;
    const auto old=retired_;retired_={};
    // Reaped retains its original meaning: observed on the graphics owner
    // immediately before actual GPU feedback, not at logical IPC admission.
    if(observer_)observer_(observerContext_,old.lease,old.ticket,StagedFrameObservation::Reaped);
    feedback_.PairConsumed(old.ticket,old.outcome==Outcome::Consumed);
}
ChannelResult StagedFrameProducer::PrepareGraphicsResult(const FrameLease& lease)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(channel_.SourceSlotCount()>1){DrainPipelineGraphics();return !pipelineFaulted_&&PipelineMatches(lease)&&
        pipelineSlots_[lease.sourceSlot].step==Step::Rendering?ChannelResult::Ok:ChannelResult::Invalid;}
    DrainRetiredGraphics();
    return step_==Step::Rendering&&Matches(lease)?ChannelResult::Ok:ChannelResult::Invalid;
}
ChannelResult StagedFrameProducer::SetBodyProps(const FrameLease& lease,unsigned eye,const graphics::BodyPropEye& props)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(channel_.SourceSlotCount()>1){if(!PipelineMatches(lease)||pipelineSlots_[lease.sourceSlot].step!=Step::Rendering||pipelineSlots_[lease.sourceSlot].captureReleased||eye>1||props.count>graphics::MaxBodyProps)return ChannelResult::Invalid;
        pipelineSlots_[lease.sourceSlot].bodyProps.eyes[eye]=props;return ChannelResult::Ok;}
    if(step_!=Step::Rendering||!Matches(lease)||eye>1||props.count>graphics::MaxBodyProps)return ChannelResult::Invalid;
    bodyProps_.eyes[eye]=props;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::SetBodyPropPair(const FrameLease& lease,const std::array<graphics::BodyPropEye,2>& eyes)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(channel_.SourceSlotCount()>1){if(!PipelineMatches(lease)||pipelineSlots_[lease.sourceSlot].step!=Step::Rendering||pipelineSlots_[lease.sourceSlot].captureReleased||eyes[0].count>graphics::MaxBodyProps||eyes[1].count>graphics::MaxBodyProps)return ChannelResult::Invalid;
        pipelineSlots_[lease.sourceSlot].bodyProps.eyes=eyes;return ChannelResult::Ok;}
    if(step_!=Step::Rendering||!Matches(lease)||eyes[0].count>graphics::MaxBodyProps||eyes[1].count>graphics::MaxBodyProps)
        return ChannelResult::Invalid;
    bodyProps_.eyes=eyes;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::Submit(const FrameLease& lease,const graphics::TextureDescriptor& descriptor,const graphics::PairTicket& ticket,const graphics::SceneDepthFrame* depth)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(channel_.SourceSlotCount()>1){
        if(pipelineFaulted_||!PipelineMatches(lease))return ChannelResult::Invalid;auto& slot=pipelineSlots_[lease.sourceSlot];
        if(slot.step!=Step::Rendering||slot.ownsSource||ticket.frameId!=slot.lease.native.frameId||descriptor.sharing!=graphics::TextureSharing::LegacyFenced||
            !runtime::PairMatchesFrame(slot.lease.requirements,slot.lease.tracking,descriptor,ticket)||
            (depth&&(depth->samples!=1||depth->quality||!graphics::SceneDepthMatches(*depth,descriptor,ticket))))return ChannelResult::Invalid;
        slot.descriptor=descriptor;slot.ticket=ticket;slot.sceneDepth=depth?*depth:graphics::SceneDepthFrame{};
        slot.ownsSource=true;slot.step=Step::Publishing;return ChannelResult::Ok;
    }
    if(retired_.outcome!=Outcome::Pending)return ChannelResult::Busy;
    if(step_!=Step::Rendering||!Matches(lease)||ticket.frameId!=lease_.native.frameId||
       !runtime::PairMatchesFrame(lease_.requirements,lease_.tracking,descriptor,ticket)||
       (depth&&!graphics::SceneDepthMatches(*depth,descriptor,ticket)))return ChannelResult::Invalid;
    sceneDepth_=depth?*depth:graphics::SceneDepthFrame{};descriptor_=descriptor;ticket_=ticket;step_=Step::Publishing;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::Cancel(const FrameLease& lease)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(channel_.SourceSlotCount()>1){if(!PipelineMatches(lease)||pipelineSlots_[lease.sourceSlot].step!=Step::Rendering)return ChannelResult::Invalid;
        pipelineSlots_[lease.sourceSlot].step=Step::Skipping;return ChannelResult::Ok;}
    if(step_!=Step::Rendering||!Matches(lease))return ChannelResult::Invalid;
    step_=Step::Skipping;return ChannelResult::Ok;
}
void StagedFrameProducer::PumpGraphics()noexcept {
    Lock lock(gate_);if(!lock)return;
    if(channel_.SourceSlotCount()>1){PumpPipelineGraphics();return;}
    DrainRetiredGraphics();
    if(step_==Step::Skipping){if(channel_.Skip()!=ChannelResult::Busy){step_=Step::Idle;lease_={};}return;}
    if(step_==Step::Publishing){const auto result=channel_.Publish(descriptor_,ticket_,&bodyProps_,&sceneDepth_);
        if(result==ChannelResult::Busy)return;
        if(result==ChannelResult::Ok){step_=Step::Waiting;
            if(observer_)observer_(observerContext_,lease_,ticket_,StagedFrameObservation::Published);}
        else{feedback_.PairConsumed(ticket_,false);step_=Step::Idle;lease_={};return;}
    }
    if(step_==Step::Waiting){const auto outcome=channel_.PollOutcome();if(outcome==Outcome::Pending)return;
        if(observer_)observer_(observerContext_,lease_,ticket_,StagedFrameObservation::Reaped);
        feedback_.PairConsumed(ticket_,outcome==Outcome::Consumed);step_=Step::Idle;lease_={};
    }
}
namespace {
PipelineSlotKey PipelineKey(const FrameLease& lease)noexcept{return {lease.requestId,lease.sourceSlot,0};}
}
void StagedFrameProducer::FailPipeline()noexcept {
    pipelineFaulted_=true;(void)channel_.StopPipelineAdmission();
}
ChannelResult StagedFrameProducer::TransitionWorldOwner(const NativeFrameKey& expectedOld,const NativeFrameKey& replacement)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(channel_.SourceSlotCount()<2)return ChannelResult::Invalid;
    if(pipelineFaulted_)return ChannelResult::Closed;
    const bool cold=!pipelineOwner_.owner&&!pipelineOwner_.frameId&&!pipelineOwner_.deviceEpoch&&
        !lastNative_.owner&&!lastNative_.frameId&&!lastNative_.deviceEpoch;
    if(!expectedOld.owner||!expectedOld.deviceEpoch||!replacement.owner||!replacement.frameId||
       replacement.owner==expectedOld.owner||replacement.deviceEpoch!=expectedOld.deviceEpoch||
       (!cold&&(pipelineOwner_.owner!=expectedOld.owner||pipelineOwner_.deviceEpoch!=expectedOld.deviceEpoch)))return ChannelResult::Invalid;
    for(const auto& slot:PipelineSlots()){
        if(slot.step!=Step::Idle||slot.lease.requestId||slot.ownsSource||slot.callbackComplete||
           slot.captureReleased||slot.retirement.key.request)return ChannelResult::Busy;
    }
    const auto drained=channel_.CheckPipelineSourcesDrained();
    if(drained!=ChannelResult::Ok)return drained;
    // Before any admission there are no requirements to preserve or rebind.
    // A verified empty transition is a no-op; the first request binds normally.
    if(cold)return ChannelResult::Ok;
    // A verified incarnation may reuse a historical owner address, including
    // after an intermediate owner that rendered no frame. Reset only native
    // serial history; unchanged request/channel generations reject old leases.
    pipelineOwner_=replacement;lastNative_={};return ChannelResult::Ok;
}
bool StagedFrameProducer::PipelineMatches(const FrameLease& lease)const noexcept {
    if(lease.sourceSlotCount!=channel_.SourceSlotCount()||lease.sourceSlot>=channel_.SourceSlotCount()||lease.channelGeneration!=generation_||!lease.requestId)return false;
    const auto& current=pipelineSlots_[lease.sourceSlot].lease;
    return current.channelGeneration==lease.channelGeneration&&current.requestId==lease.requestId&&current.native==lease.native&&
        current.sourceSlot==lease.sourceSlot&&current.sourceSlotCount==channel_.SourceSlotCount();
}
ChannelResult StagedFrameProducer::MarkCaptureReleased(const FrameLease& lease)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(pipelineFaulted_||!channel_.ExportOverlapEnabled()||!PipelineMatches(lease))return ChannelResult::Invalid;
    auto& slot=pipelineSlots_[lease.sourceSlot];
    if(slot.step!=Step::Rendering||slot.ownsSource)return ChannelResult::Invalid;
    if(slot.captureReleased)return ChannelResult::Ok;
    const auto result=channel_.MarkCaptureReleased(PipelineKey(lease));
    if(result==ChannelResult::Ok)slot.captureReleased=true;
    return result;
}
bool StagedFrameProducer::TryBeginPipeline(const NativeFrameKey& native,FrameLease& out)noexcept {
    if(pipelineFaulted_)return false;
    if(pipelineOwner_.owner&&(pipelineOwner_.owner!=native.owner||pipelineOwner_.deviceEpoch!=native.deviceEpoch)){
        FailPipeline();return false;
    }
    for(const auto& slot:PipelineSlots()){
        if(slot.step==Step::Skipping)return false;
        if((slot.step==Step::Rendering||slot.step==Step::Publishing)&&
           (!channel_.ExportOverlapEnabled()||!slot.captureReleased))return false;
    }
    FrameRequest request{};PipelineSlotKey key{};const auto result=channel_.TryTakePipeline(request,key);
    if(result!=ChannelResult::Ok)return false;
    if(key.slot>=channel_.SourceSlotCount()||pipelineSlots_[key.slot].step!=Step::Idle){FailPipeline();return false;}
    auto& slot=pipelineSlots_[key.slot];FrameLease next{};next.channelGeneration=generation_;next.requestId=key.request;
    next.native=native;next.sourceSlot=key.slot;next.sourceSlotCount=channel_.SourceSlotCount();slot.lease=next;
    if(!Decode(request,next.requirements,next.tracking)){slot.step=Step::Skipping;return false;}
    if(pipelineOwner_.owner){const auto& a=pipelineRequirements_;const auto& b=next.requirements;
        if(a.width!=b.width||a.height!=b.height||a.format!=b.format||a.adapterLow!=b.adapterLow||a.adapterHigh!=b.adapterHigh){
            slot.step=Step::Skipping;FailPipeline();return false;
        }
    }else{pipelineOwner_=native;pipelineRequirements_=next.requirements;}
    slot.lease=next;slot.bodyProps.frameId=native.frameId;slot.bodyProps.spaceGeneration=next.tracking.spaceGeneration;
    slot.bodyProps.trackingGeneration=next.tracking.generation;slot.step=Step::Rendering;lastNative_=native;out=next;return true;
}
void StagedFrameProducer::DrainPipelineGraphics()noexcept {
    for(auto& slot:PipelineSlots()){
        if(slot.step==Step::Skipping){const auto result=channel_.SkipPipeline(PipelineKey(slot.lease));
            if(result==ChannelResult::Busy)continue;
            if(result!=ChannelResult::Ok){FailPipeline();continue;}slot.step=Step::Waiting;}
        if(slot.step==Step::Waiting){
            const auto result=channel_.ClaimPipelineRetirement(PipelineKey(slot.lease),slot.retirement);
            if(result==ChannelResult::Busy)continue;
            if(result!=ChannelResult::Ok){FailPipeline();continue;}
            if(slot.retirement.hasSource!=slot.ownsSource||
                (slot.ownsSource&&std::memcmp(&slot.retirement.ticket,&slot.ticket,sizeof(slot.ticket)))){FailPipeline();continue;}
            slot.step=Step::Retiring;
        }
        if(slot.step!=Step::Retiring)continue;
        if(!slot.callbackComplete){
            if(slot.retirement.hasSource){
                if(observer_)observer_(observerContext_,slot.lease,slot.ticket,StagedFrameObservation::Reaped);
                feedback_.PairConsumed(slot.ticket,slot.retirement.outcome==Outcome::Consumed);
            }
            // The IPC commit can contend after this callback. Preserve this bit
            // locally so the next Pump NEVER calls graphics feedback twice.
            slot.callbackComplete=true;slot.ownsSource=false;
        }
        const auto committed=channel_.FinishPipelineRetirement(slot.retirement);
        if(committed==ChannelResult::Ok)slot={};
        else if(committed!=ChannelResult::Busy)FailPipeline();
    }
}
void StagedFrameProducer::PumpPipelineGraphics()noexcept {
    if(pipelineFaulted_)(void)channel_.StopPipelineAdmission();
    DrainPipelineGraphics();
    for(auto& slot:PipelineSlots())if(slot.step==Step::Publishing){
        const auto* depth=graphics::SceneDepthAbsent(slot.sceneDepth)?nullptr:&slot.sceneDepth;
        const auto result=channel_.PublishPipeline(PipelineKey(slot.lease),slot.descriptor,slot.ticket,&slot.bodyProps,depth);
        if(result==ChannelResult::Busy)continue;
        if(result==ChannelResult::Ok||result==ChannelResult::Timeout){
            // Both results transfer source ownership to the exact wire slot;
            // expired/cancelled publication is claimed/retired negatively below.
            slot.step=Step::Waiting;
            if(result==ChannelResult::Ok&&observer_)observer_(observerContext_,slot.lease,slot.ticket,StagedFrameObservation::Published);
        }else{
            // No wire adoption: this staged owner still owes the sole negative
            // callback, and later no-source retirement must not repeat it.
            if(slot.ownsSource)feedback_.PairConsumed(slot.ticket,false);
            slot.ownsSource=false;slot.step=Step::Skipping;FailPipeline();
        }
    }
    DrainPipelineGraphics();
}
void StagedFrameProducer::ClosePipelineGraphics()noexcept {
    // Both callback threads quiescent, graphics owner held. Drain proven ACKs
    // once, then retire every unresolved adopted source without claiming ready.
    DrainPipelineGraphics(); // Never publish newly staged work during close.
    for(auto& slot:PipelineSlots()){if(slot.ownsSource&&!slot.callbackComplete)feedback_.PairConsumed(slot.ticket,false);slot={};}
    channel_.Close();pipelineFaulted_=false;pipelineOwner_={};pipelineRequirements_={};
}

}
