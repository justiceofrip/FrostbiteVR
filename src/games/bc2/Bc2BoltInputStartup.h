#pragma once
#include "Bc2BodyHolsterObservation.h"
#include <algorithm>
#include <cmath>
#include <ostream>
namespace fvr::bc2 {
// Input-only shoulder draw, using the same neutral/reach/press/clear sequence
// as InventoryReloadProbe. A selected native gun is not an already-drawn claim.
class Bc2BoltInputStartup {
public:
    enum class Phase:unsigned {Warmup,Reach,Press,Clear,Ready,Failed};
    bool Prepare(interaction::InputFrame& in,const ReloadStateOwner& owner,
        const std::shared_ptr<const BodyHolsterProbeSample>& s,std::int64_t now)noexcept {
        using namespace interaction;
        if(!first_){first_=at_=now;owner_=owner;command_=in.hands[1].grip;}
        if(now<lastNow_||owner!=owner_||now-first_>=6000000000ll)Fail(1);lastNow_=now;
        const bool fresh=s&&Current(*s,owner,now);
        if(fresh){lastInput_=s->hand.sequence;lastTick_=s->nativeTick;
            latest_={phase_,s->phase,s->hand.sequence,s->nativeTick,s->request,s->right?s->right->token.id:0,
                s->hand.observedNs,s->hand.deadlineNs,now,bool(s->visibility),s->visibility?s->visibility->hidden:false};}
        if(phase_==Phase::Warmup&&now-first_>=2000000000ll&&fresh){
            slot_=s->selectedSlot;
            const auto anchor=std::find_if(s->anchors.shoulders.begin(),s->anchors.shoulders.end(),[&](const auto& a){return a.slot==slot_->slot;});
            if(anchor==s->anchors.shoulders.end())Fail(2);
            else if(Held(*s,owner,now)){claim_=s->right->token.id;alreadyDrawn_=true;To(Phase::Clear,now);}
            else if(Empty(*s,owner,now)){anchor_=*anchor;hiddenRequest_=s->request;To(Phase::Reach,now);}
        }
        if(fresh&&slot_&&s->selectedSlot!=slot_)Fail(3);
        if(phase_==Phase::Press&&fresh&&s->phase==BodyHolsterPhase::ShowPending)showRequest_=s->request;
        if(phase_==Phase::Press&&fresh&&Held(*s,owner,now)&&showRequest_&&showRequest_!=hiddenRequest_&&
           s->visibility&&!s->visibility->hidden&&s->visibility->verifiedCopyMask==3&&
           s->visibility->nativeOwner==owner&&s->visibility->request==showRequest_&&s->visibility->deadlineNs>now){
            claim_=s->right->token.id;To(Phase::Clear,now);
        }
        constexpr math::Vec3 neutral{.15f,-.10f,-.20f};
        math::Vec3 target=neutral;const bool shoulder=phase_==Phase::Reach||phase_==Phase::Press;
        if(shoulder)target={anchor_.center.x,anchor_.center.y,-anchor_.center.z};
        const float distance=Distance(command_.position,target),t=distance>.025f?.025f/distance:1.f;
        for(unsigned n=0;n<3;++n){float* p=n==0?&command_.position.x:n==1?&command_.position.y:&command_.position.z;
            const float v=n==0?target.x:n==1?target.y:target.z;*p+=t*(v-*p);}
        command_.orientation={};
        for(auto& h:in.hands){h.squeeze=h.trigger=0;}
        in.hands[0].grip.position={-.2f,-.25f,-.45f};in.hands[0].aim=in.hands[0].grip;
        in.hands[1].grip=in.hands[1].aim=command_;
        // A natively visible unsupported item stays on ordinary input. A real
        // tracked grip may establish the ordinary gun claim; its actual fresh
        // receipt is still required by Held before this diagnostic can arm.
        const bool heldGrip=phase_==Phase::Warmup&&fresh&&s->phase==BodyHolsterPhase::Held&&!s->outcome.freeRight;
        in.hands[1].squeeze=heldGrip||phase_==Phase::Press||phase_==Phase::Clear||phase_==Phase::Ready?1.f:0.f;
        if(phase_==Phase::Reach&&Distance(command_.position,target)<.001f&&now-at_>=300000000){
            const auto body=BodyAnchorHandPose(in,InteractionHand::Right);
            if(!body||!BodyAnchorContains(anchor_,*body))Fail(4);else To(Phase::Press,now);
        }
        if(phase_==Phase::Clear&&fresh&&Held(*s,owner,now)&&s->right->token.id==claim_&&
           Distance(command_.position,neutral)<.001f&&Distance(s->input.hands[1].grip.position,neutral)<.01f&&now-at_>=350000000)
            To(Phase::Ready,now);
        if(Failed())in.hands[1].squeeze=0;
        return phase_==Phase::Ready;
    }
    bool Failed()const noexcept{return phase_==Phase::Failed;}
    bool Ready()const noexcept{return phase_==Phase::Ready;}
    void Report(std::ostream& out)const {
        out<<"{\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"already_drawn\":"<<(alreadyDrawn_?"true":"false")
           <<",\"weapon\":"<<owner_.weapon<<",\"slot\":"<<(slot_?slot_->slot:0)<<",\"hidden_request\":"<<hiddenRequest_
           <<",\"show_request\":"<<showRequest_<<",\"gun_claim\":"<<claim_<<",\"source_input\":"<<lastInput_<<",\"native_tick\":"<<lastTick_<<",\"event_drops\":"<<drops_<<",\"events\":[";
        for(unsigned n=0;n<count_;++n){if(n)out<<',';const auto& r=rows_[n];out<<"{\"phase\":"<<unsigned(r.phase)
            <<",\"body_phase\":"<<unsigned(r.body)<<",\"input_sequence\":"<<r.input<<",\"native_tick\":"<<r.tick
            <<",\"request\":"<<r.request<<",\"claim\":"<<r.claim<<",\"observed_ns\":"<<r.observed<<",\"deadline_ns\":"<<r.deadline
            <<",\"now_ns\":"<<r.now<<",\"visibility_receipt\":"<<(r.visibility?"true":"false")<<",\"hidden\":"<<(r.hidden?"true":"false")<<'}';}
        out<<"]}";
    }
    static bool Current(const BodyHolsterProbeSample& s,const ReloadStateOwner& owner,std::int64_t now)noexcept {
        return s.nativeOwner==owner&&s.sampledNs>0&&s.sampledNs<=now&&now-s.sampledNs<=150000000&&
            s.hand.observedNs>0&&s.hand.observedNs<=now&&s.hand.deadlineNs>now&&s.hand.deadlineNs-s.hand.observedNs<=150000000&&
            s.hand.focused&&s.hand.tracked[0]&&s.hand.tracked[1]&&s.input.generation==s.hand.sequence&&s.input.spaceGeneration==owner.space&&
            s.hand.owner.actor==((std::uint64_t(owner.weak)<<32)|owner.soldier)&&s.hand.owner.actorGeneration==owner.actorGeneration&&
            s.hand.owner.space==owner.space&&s.physicalGun.id==owner.weapon&&s.physicalGun.generation==s.hand.owner.equipGeneration&&
            s.nativeTick&&s.selectedSlot&&s.selectedSlot->item.id==owner.weapon&&!s.queuedTarget;
    }
    static bool Held(const BodyHolsterProbeSample& s,const ReloadStateOwner& o,std::int64_t now)noexcept {
        using namespace interaction;
        const auto& visible=s.ordinaryVisible;
        const bool originalVisible=visible&&visible->owner==o&&visible->generation&&visible->generation<=s.hand.sequence&&
            visible->observedNs>0&&visible->observedNs<=now&&visible->deadlineNs>now&&visible->deadlineNs-visible->observedNs<=150000000;
        return originalVisible&&Current(s,o,now)&&s.phase==BodyHolsterPhase::Held&&s.right&&!s.left&&
            s.right->token.kind==HandClaimKind::GunHold&&s.right->token.hand==InteractionHand::Right&&
            s.right->token.owner==s.hand.owner&&s.right->token.item==s.physicalGun&&s.right->deadlineNs>now&&
            s.right->inputSequence==s.hand.sequence&&!s.outcome.freeRight&&!s.outcome.blockWeaponActions&&
            s.outcome.allowAutomaticGunHold&&(!s.visibility||!s.visibility->hidden);
    }
private:
    static float Distance(math::Vec3 a,math::Vec3 b)noexcept{return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
    static bool Empty(const BodyHolsterProbeSample& s,const ReloadStateOwner& o,std::int64_t now)noexcept {
        return Current(s,o,now)&&s.phase==BodyHolsterPhase::Empty&&!s.left&&!s.right&&s.outcome.inventory.emptyHands&&
            s.outcome.freeRight&&BodyFreeRightEvidenceCurrent(*s.outcome.freeRight,now)&&s.visibility&&s.visibility->hidden&&
            s.visibility->verifiedCopyMask==3&&s.visibility->nativeOwner==o&&s.visibility->request==s.request&&s.visibility->deadlineNs>now&&
            s.suppression&&s.suppression->nativeTick==s.nativeTick&&s.suppression->request==s.request&&
            HolsterSuppressionCurrent(*s.suppression,*s.suppression);
    }
    void To(Phase p,std::int64_t now)noexcept{phase_=p;at_=now;latest_.phase=p;latest_.now=now;
        if(count_<rows_.size())rows_[count_++]=latest_;else ++drops_;}
    void Fail(unsigned why)noexcept{failure_=why;phase_=Phase::Failed;}
    struct Row {Phase phase;BodyHolsterPhase body;std::uint64_t input,tick,request,claim;std::int64_t observed,deadline,now;bool visibility,hidden;};
    Row latest_{};std::array<Row,16> rows_{};unsigned count_=0,drops_=0;
    Phase phase_=Phase::Warmup;unsigned failure_=0;bool alreadyDrawn_=false;
    std::int64_t first_=0,at_=0,lastNow_=0;ReloadStateOwner owner_{};math::Pose command_{};
    std::optional<interaction::BodySlotAssignment> slot_;interaction::BodyAnchor anchor_{};
    std::uint64_t hiddenRequest_=0,showRequest_=0,claim_=0,lastInput_=0,lastTick_=0;
};
}
