#include "Bc2ContextualToolSource.h"
#include <cmath>
namespace fvr::bc2 {namespace {
using namespace interaction;
bool Lease(const HandInteractionSample& s,std::int64_t now)noexcept {
    return s.owner.actor&&s.owner.actorGeneration&&s.owner.space&&s.sequence&&s.observedNs>0&&
        s.observedNs<=now&&s.deadlineNs>now&&s.deadlineNs-s.observedNs<=100000000&&
        s.focused&&s.tracked[0]&&s.tracked[1];
}
bool SamePacket(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&
        a.deadlineNs==b.deadlineNs&&a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
}
bool ContextualToolSourceCurrent(const InputFrame& in,const HandInteractionSample& s,std::int64_t now)noexcept {
    if(!Lease(s,now)||!ValidInput(in)||!in.focused||!in.headValid||in.generation!=s.sequence||
       in.spaceGeneration!=s.owner.space||!GameplayOwnsInput(in.menu,now))return false;
    for(const auto& h:in.hands)if(!h.gripTracked||(h.active&(Trigger|Squeeze))!=(Trigger|Squeeze))return false;
    return true;
}
bool ContextualToolQuiet(const InputFrame& in,bool allowRightClick)noexcept {
    for(unsigned h=0;h<2;++h)if(in.hands[h].trigger>.1f||
        (in.hands[h].held&~(allowRightClick&&h==1?unsigned(StickClick):0u))||
        std::hypot(in.hands[h].stickX,in.hands[h].stickY)>.1f)return false;
    return true;
}
bool ContextualToolAttachmentCurrent(const ContextualToolAttachment& a,const ReloadStateOwner& owner,
    const HandInteractionSample& s,std::uint64_t selectedSequence,std::int64_t now)noexcept {
    return s.owner.actor==((std::uint64_t(owner.weak)<<32)|owner.soldier)&&s.owner.actorGeneration==owner.actorGeneration&&
        s.owner.space==owner.space&&a.owner==owner&&owner.weapon&&a.equipment.weapon==owner.weapon&&a.equipment.data&&a.rigFingerprint&&
        a.input.owner==s.owner&&Lease(a.input,now)&&Lease(s,now)&&!a.input.released[1]&&!s.released[1]&&
        a.input.sequence>=selectedSequence&&a.input.sequence<=s.sequence&&a.input.observedNs<=s.observedNs&&
        bool(InverseAnimatedTransform(a.nativeWristInWeapon));
}
std::optional<ContextualToolAim> ObserveContextualToolAim(const ReloadStateOwner& owner,
    const InputFrame& in,const HandInteractionSample& s,std::uint64_t operation,unsigned aimer,
    float wantedYaw,float wantedPitch,float yaw,float pitch,float copiedYaw,float copiedPitch)noexcept {
    constexpr float tau=6.283185307179586f;
    if(!operation||!aimer||!owner.weapon||owner.actorGeneration!=s.owner.actorGeneration||owner.space!=s.owner.space||
       s.owner.actor!=((std::uint64_t(owner.weak)<<32)|owner.soldier)||!ContextualToolSourceCurrent(in,s,s.nowNs)||
       !std::isfinite(wantedYaw)||!std::isfinite(wantedPitch)||!std::isfinite(yaw)||!std::isfinite(pitch)||
       yaw!=copiedYaw||pitch!=copiedPitch||std::abs(std::remainder(yaw-wantedYaw,tau))>0.0001f||
       std::abs(pitch-wantedPitch)>0.0001f)return {};
    return ContextualToolAim{owner,s,operation,aimer,yaw,pitch};
}
bool ContextualToolAimCurrent(const ContextualToolAim& a,const ReloadStateOwner& owner,
    const HandInteractionSample& s,std::uint64_t operation,std::int64_t now)noexcept {
    return operation&&a.operation==operation&&a.owner==owner&&a.aimer&&SamePacket(a.input,s)&&Lease(s,now)&&
        std::isfinite(a.yaw)&&std::isfinite(a.pitch);
}
ContextualToolToggleResult ContextualToolToggle::Update(const InputFrame& in,const HandInteractionSample& s,
    HandInteractionKey item,bool active)noexcept {
    ContextualToolToggleResult out;
    if(!ContextualToolSourceCurrent(in,s,s.nowNs)||!item.id||!item.generation||
       !(in.hands[0].active&StickClick)||!(in.hands[1].active&StickClick)) {Missing();return out;}
    const bool boundary=owner_!=s.owner||item_!=item||active_!=active||s.sequence<sequence_||s.observedNs<observed_||
        (deadline_&&s.observedNs>=deadline_);
    if(boundary){owner_=s.owner;item_=item;active_=active;armed_=false;sequence_=s.sequence;observed_=s.observedNs;deadline_=s.deadlineNs;return out;}
    if(s.sequence==sequence_)return out;
    sequence_=s.sequence;observed_=s.observedNs;deadline_=s.deadlineNs;
    if(ContextualToolQuiet(in)){armed_=true;return out;}
    const bool click=ContextualToolQuiet(in,true)&&(in.hands[1].held&StickClick);
    const bool edge=armed_&&click;armed_=false;
    if(!edge)return out;
    if(!active&&!s.released[1]&&in.hands[1].squeeze>=.65f)out.kind=ContextualToolToggleKind::Draw;
    if(active&&s.released[1]&&in.hands[1].squeeze<=.35f)out.kind=ContextualToolToggleKind::Return;
    if(out.kind!=ContextualToolToggleKind::None)out.intent={{0x424332544f47474cull,item.generation},s.sequence,s.deadlineNs,true};
    return out;
}
}
