#include "fvr/ipc/StagedFrameProducer.h"
namespace fvr::ipc {
bool StagedFrameProducer::Connect(const std::wstring& token)noexcept {
    CloseGraphics();if(generation_==UINT64_MAX)return false;
    ++generation_;return channel_.ConnectProducer(token);
}
void StagedFrameProducer::CloseGraphics()noexcept {
    // Caller has stopped both hooks; no callback may race destruction/connection.
    channel_.Close();
    if(step_==Step::Publishing||step_==Step::Waiting)feedback_.PairConsumed(ticket_,false);
    step_=Step::Idle;lease_={};ticket_={};descriptor_={};
}
bool StagedFrameProducer::Matches(const FrameLease& lease)const noexcept {
    return lease.channelGeneration==generation_&&lease.requestId&&lease.requestId==lease_.requestId&&lease.native==lease_.native;
}
bool StagedFrameProducer::TryBegin(const NativeFrameKey& native,FrameLease& out)noexcept {
    if(!native.owner||!native.frameId||!native.deviceEpoch)return false;
    Lock lock(gate_);if(!lock||step_!=Step::Idle)return false;
    if(native.owner==lastNative_.owner&&native.deviceEpoch==lastNative_.deviceEpoch&&native.frameId<=lastNative_.frameId)return false;
    FrameRequest request{};if(channel_.TryTake(request)!=ChannelResult::Ok)return false;
    FrameLease next{};next.channelGeneration=generation_;next.requestId=request.requestId;next.native=native;
    if(!Decode(request,next.requirements,next.tracking)){step_=Step::Skipping;return false;}
    bodyProps_={};bodyProps_.frameId=native.frameId;bodyProps_.spaceGeneration=next.tracking.spaceGeneration;bodyProps_.trackingGeneration=next.tracking.generation;
    lease_=next;lastNative_=native;step_=Step::Rendering;out=next;return true;
}
ChannelResult StagedFrameProducer::SetBodyProps(const FrameLease& lease,unsigned eye,const graphics::BodyPropEye& props)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(step_!=Step::Rendering||!Matches(lease)||eye>1||props.count>graphics::MaxBodyProps)return ChannelResult::Invalid;
    bodyProps_.eyes[eye]=props;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::SetBodyPropPair(const FrameLease& lease,const std::array<graphics::BodyPropEye,2>& eyes)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(step_!=Step::Rendering||!Matches(lease)||eyes[0].count>graphics::MaxBodyProps||eyes[1].count>graphics::MaxBodyProps)
        return ChannelResult::Invalid;
    bodyProps_.eyes=eyes;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::Submit(const FrameLease& lease,const graphics::TextureDescriptor& descriptor,const graphics::PairTicket& ticket)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(step_!=Step::Rendering||!Matches(lease)||ticket.frameId!=lease_.native.frameId||
       !runtime::PairMatchesFrame(lease_.requirements,lease_.tracking,descriptor,ticket))return ChannelResult::Invalid;
    descriptor_=descriptor;ticket_=ticket;step_=Step::Publishing;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::Cancel(const FrameLease& lease)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(step_!=Step::Rendering||!Matches(lease))return ChannelResult::Invalid;
    step_=Step::Skipping;return ChannelResult::Ok;
}
void StagedFrameProducer::PumpGraphics()noexcept {
    Lock lock(gate_);if(!lock)return;
    if(step_==Step::Skipping){if(channel_.Skip()!=ChannelResult::Busy){step_=Step::Idle;lease_={};}return;}
    if(step_==Step::Publishing){const auto result=channel_.Publish(descriptor_,ticket_,&bodyProps_);
        if(result==ChannelResult::Busy)return;
        if(result==ChannelResult::Ok)step_=Step::Waiting;
        else{feedback_.PairConsumed(ticket_,false);step_=Step::Idle;lease_={};return;}
    }
    if(step_==Step::Waiting){const auto outcome=channel_.PollOutcome();if(outcome==Outcome::Pending)return;
        feedback_.PairConsumed(ticket_,outcome==Outcome::Consumed);step_=Step::Idle;lease_={};
    }
}
}
