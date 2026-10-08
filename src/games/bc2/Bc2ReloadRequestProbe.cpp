#include "Bc2ReloadRequestProbe.h"
#include <cmath>
#include <limits>
namespace fvr::bc2 {
namespace {
void IdentityJson(std::ostream& out,const ReloadHoldIdentity& id){
    const auto& o=id.owner;out<<"{\"owner\":["<<o.player<<','<<o.soldier<<','<<o.weak<<','<<o.weapon<<','<<o.actorGeneration<<','<<o.equipGeneration<<','<<o.space
        <<"],\"server\":["<<id.serverPlayer<<','<<id.serverSoldier<<','<<id.serverItem<<"],\"firing\":["<<id.firing[0]<<','<<id.firing[1]<<','<<id.firing[2]<<"]}";
}
}
void Bc2ReloadRequestProbe::Record(Event event,unsigned reason,std::int64_t now,const std::optional<ReloadRoundLease>& lease)noexcept {
    if(count_==rows_.size()){++dropped_;return;}
    if(api_.clock)now=api_.clock(); // Only observation time; never renew source/deadline.
    rows_[count_++]={event,reason,now,source_,deadline_,sequence_,cycle_,request_,serverInvocation_,lease,api_.snapshot?api_.snapshot():std::nullopt};
    sampled_=now;
}
void Bc2ReloadRequestProbe::Fail(unsigned reason,std::int64_t now)noexcept {
    if(stopped_)return;reason_=reason;stopped_=true;if(api_.cancel)api_.cancel();stoppedAt_=api_.clock?api_.clock():now;Record(Failed,reason,now);
}
void Bc2ReloadRequestProbe::Stop(std::int64_t now)noexcept {
    if(!enabled_||stopped_)return;stopped_=true;if(api_.cancel)api_.cancel();stoppedAt_=api_.clock?api_.clock():now;Record(Cancelled,0,now);
}
void Bc2ReloadRequestProbe::PollRetirement(std::int64_t now)noexcept {
    if(!started_||!stopped_||postRetirementReserve_||retirementReason_||(!api_.retire&&!api_.reserve))return;
    if(!api_.retire||!api_.reserve){retirementReason_=1;return;}
    if(now-retirementPoll_<25000000)return;retirementPoll_=now;
    if(retirement_&&now>=retirement_->deadlineNs){++retirementExpired_;retirement_.reset();}
    if(!retirement_){
        ++retirementAttempts_;const auto receipt=api_.retire(identity_,cycle_);
        if(api_.clock)now=api_.clock();
        if(!receipt)return;
        if(!receipt->verified||receipt->identity!=identity_||receipt->cycle!=cycle_||!receipt->event||
           receipt->observedNs<stoppedAt_||receipt->observedNs>now||receipt->deadlineNs<=receipt->observedNs||
           receipt->deadlineNs-receipt->observedNs>200000000){retirementReason_=2;return;}
        if(receipt->deadlineNs<=now){++retirementExpired_;return;}
        retirement_=receipt;retirementChecked_=now;
    }
    ++reserveAttempts_;const auto reserve=api_.reserve();if(api_.clock)now=api_.clock();
    if(!reserve)return;
    // This is a later authoritative read, never an acknowledgement or rollback.
    if(!reserve->verified||reserve->identity!=identity_||!reserve->sequence||reserve->observedNs<retirementChecked_||
       reserve->observedNs>now||reserve->deadlineNs<=now||reserve->deadlineNs-reserve->observedNs>200000000||
       now>=retirement_->deadlineNs||reserve->capacity<=0||reserve->loaded<0||reserve->loaded>reserve->capacity||reserve->reserve<0)return;
    postRetirementReserve_=reserve;reserveChecked_=now;
}
void Bc2ReloadRequestProbe::Tick(const interaction::InputFrame& input,const interaction::HandInteractionOwner& physical,
    std::int64_t source,std::int64_t deadline,std::int64_t now)noexcept {
    if(!enabled_)return;
    if(!api_.identity||!api_.start||!api_.keep||!api_.lease||!api_.submit||!api_.ack||!api_.cancel){Fail(1,now);return;}
    if(now<=0||now<last_){Fail(2,now);return;}
    if(!first_)first_=now;last_=now;
    if(now-first_>=30000000000ll){Stop(now);return;}
    if(stopped_){PollRetirement(now);if(now-sampled_>=250000000)Record(Sample,0,now);return;}
    const bool valid=interaction::ValidInput(input)&&input.focused&&input.headValid&&input.hands[0].gripTracked&&
        input.hands[1].gripTracked&&input.hands[1].aimTracked&&input.generation&&source>0&&source<=now&&deadline>now&&
        deadline>source&&deadline-source<=150000000&&input.spaceGeneration==physical.space&&physical.actor&&physical.actorGeneration&&physical.equipGeneration;
    if(!valid){if(started_)Fail(3,now);return;}
    if(input.generation<sequence_||source<source_||(input.generation==sequence_&&(source!=source_||deadline!=deadline_))){Fail(4,now);return;}
    const float marker=input.hands[0].trigger;
    if(marker!=0&&marker!=.25f&&marker!=1.f){Fail(5,now);return;}
    sequence_=input.generation;source_=source;deadline_=deadline;
    if(!started_){
        if(marker==0)return;
        if(marker!=.25f){Fail(6,now);return;}
        const auto identity=api_.identity();if(!identity){Fail(7,now);return;}
        const auto& owner=identity->owner;
        if(physical.actor!=((std::uint64_t(owner.weak)<<32)|owner.soldier)||physical.actorGeneration!=owner.actorGeneration||physical.space!=owner.space){Fail(8,now);return;}
        identity_=*identity;physical_=physical;cycle_=sequence_;
        if(!api_.start({identity_,cycle_,sequence_,source_,deadline_,true})){Fail(9,now);return;}
        started_=true;kept_=now;Record(Started,0,now);
    }else if(physical!=physical_){Fail(10,now);return;}
    if(marker==0){
        if(!acknowledged_||!reheld_){Fail(11,now);return;}
        Stop(now);return;
    }
    if(now-kept_>=25000000){
        if(!api_.keep({identity_,cycle_,sequence_,source_,deadline_,true})){Fail(12,now);return;}
        kept_=now;
    }
    const bool seat=marker==1.f&&marker_!=1.f;
    marker_=marker;
    if(now-polled_<10000000&&!seat)return;polled_=now;
    const auto lease=api_.lease(identity_,cycle_);
    if(lease&&lease->allThreeHeld&&!heldAt_){heldAt_=now;loaded_=lease->loaded;reserve_=lease->reserve;Record(Held,0,now,lease);}
    if(seat){
        // This is an explicit receiver request, not inferred shell geometry.
        if(submitted_||!lease||!lease->nativeBindingVerified||!lease->allThreeHeld||!heldAt_||now-first_<21000000000ll){Fail(13,now);return;}
        request_=sequence_;
        const interaction::HandInteractionKey item{0x4656525350454354ull,cycle_};
        const interaction::HandClaimToken claim{request_,physical_,interaction::InteractionHand::Left,interaction::HandClaimKind::AmmoObject,item,{0x46565253454154ull,cycle_},0};
        const auto& owner=identity_.owner;
        const interaction::ManualReloadRequest request{request_,{owner.soldier,owner.actorGeneration,owner.weapon,owner.equipGeneration,owner.space},interaction::ReloadOperation::InsertRound,0,0};
        if(!api_.submit({request,*lease,{item,claim,request_,request_,cycle_}})){Fail(14,now);return;}
        submitted_=true;Record(Submitted,0,now,lease);
    }
    if(submitted_&&!acknowledged_){
        const auto ack=api_.ack(identity_,cycle_);
        if(api_.clock)now=api_.clock();
        if(ack){const auto& a=ack->acknowledgement;const auto& owner=identity_.owner;
            if(!ack->verified||ack->observedNs>now||ack->deadlineNs<=now||a.identity!=identity_||a.cycle!=cycle_||a.semantic.request!=request_||!a.serverInvocation||
                a.semantic.owner!=interaction::ManualReloadOwner{owner.soldier,owner.actorGeneration,owner.weapon,owner.equipGeneration,owner.space}||
                a.semantic.operation!=interaction::ReloadOperation::InsertRound||a.semantic.status!=interaction::ReloadAcknowledgement::Applied){Fail(15,now);return;}
            acknowledged_=true;serverInvocation_=a.serverInvocation;ackAt_=now;Record(Acknowledged,0,now,lease);
        }
    }
    if(acknowledged_&&lease&&lease->allThreeHeld){
        if(lease->loaded!=loaded_+1||lease->reserve!=reserve_-1){Fail(16,now);return;}
        if(!reholdSince_)reholdSince_=now;
        if(!reheld_&&now-reholdSince_>=350000000){reheld_=true;Record(Reheld,0,now,lease);}
    }else if(acknowledged_)reholdSince_=0;
    if(now-sampled_>=250000000)Record(Sample,0,now,lease);
}
void Bc2ReloadRequestProbe::Report(std::ostream& out)const {
    out<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"scripted_reservation\":true,\"physical_bridge_integrated\":false,\"headset_verified\":false"
       <<",\"started\":"<<(started_?"true":"false")<<",\"submitted\":"<<(submitted_?"true":"false")<<",\"acknowledged\":"<<(acknowledged_?"true":"false")
       <<",\"scripted_item_id\":"<<0x4656525350454354ull<<",\"scripted_item_generation\":"<<cycle_<<",\"scripted_claim_id\":"<<request_
       <<",\"reheld_350ms\":"<<(reheld_?"true":"false")<<",\"stopped\":"<<(stopped_?"true":"false")<<",\"reason\":"<<reason_<<",\"dropped\":"<<dropped_<<",\"journal\":[";
    for(unsigned i=0;i<count_;++i){const auto& r=rows_[i];if(i)out<<',';
        out<<"{\"event\":"<<r.event<<",\"reason\":"<<r.reason<<",\"now_ns\":"<<r.now<<",\"source_ns\":"<<r.source<<",\"deadline_ns\":"<<r.deadline
           <<",\"sequence\":"<<r.sequence<<",\"cycle\":"<<r.cycle<<",\"request\":"<<r.request<<",\"server_invocation\":"<<r.serverInvocation;
        if(r.lease)out<<",\"lease\":{\"sequence\":"<<r.lease->sequence<<",\"observed_ns\":"<<r.lease->observedNs<<",\"deadline_ns\":"<<r.lease->deadlineNs<<",\"held\":"<<(r.lease->allThreeHeld?"true":"false")<<",\"loaded\":"<<r.lease->loaded<<",\"reserve\":"<<r.lease->reserve<<'}';
        if(r.native){const auto& n=*r.native;const auto& id=n.identity;
            out<<",\"native\":{\"observed_ns\":"<<n.observedNs<<",\"cycle\":"<<n.cycle<<",\"phase\":"<<n.phase<<",\"failure\":"<<n.failure
                <<",\"pending_request\":"<<n.pendingRequest<<",\"unresolved\":"<<(n.unresolved?"true":"false")<<",\"active_callbacks\":"<<n.activeCallbacks<<",\"revision\":"<<n.revision
                <<",\"patch_failures\":"<<n.patchFailures<<",\"restore_failures\":"<<n.restoreFailures<<",\"owner\":["<<id.owner.player<<','<<id.owner.soldier<<','<<id.owner.weak<<','<<id.owner.weapon<<','<<id.owner.actorGeneration<<','<<id.owner.equipGeneration<<','<<id.owner.space
                <<"],\"server\":["<<id.serverPlayer<<','<<id.serverSoldier<<','<<id.serverItem<<"],\"branches\":[";
            for(unsigned b=0;b<3;++b){if(b)out<<',';const auto& s=n.branches[b];out<<"{\"firing\":"<<s.address<<",\"state\":"<<s.currentState<<",\"next\":"<<s.nextState<<",\"timer\":"<<s.phaseTimer<<",\"loaded\":"<<s.loaded<<",\"reserve\":"<<s.reserve<<",\"applied\":"<<n.applied[b]<<",\"restored\":"<<n.restored[b]<<",\"transfers\":"<<n.transfers[b]<<",\"last_transfer\":"<<n.lastTransfer[b]<<'}';}
            out<<"]}";
        }out<<'}';
    }out<<"],\"retirement\":{\"enabled\":"<<((api_.retire||api_.reserve)?"true":"false")
        <<",\"verified\":"<<(postRetirementReserve_?"true":"false")<<",\"reason\":"<<retirementReason_
        <<",\"stopped_ns\":"<<stoppedAt_<<",\"attempts\":"<<retirementAttempts_<<",\"reserve_attempts\":"<<reserveAttempts_<<",\"expired\":"<<retirementExpired_<<",\"expected_identity\":";
    IdentityJson(out,identity_);
    if(retirement_){const auto& r=*retirement_;out<<",\"receipt\":{\"identity\":";IdentityJson(out,r.identity);
        out<<",\"cycle\":"<<r.cycle<<",\"event\":"<<r.event<<",\"observed_ns\":"<<r.observedNs<<",\"deadline_ns\":"<<r.deadlineNs<<",\"checked_ns\":"<<retirementChecked_<<",\"verified\":"<<(r.verified?"true":"false")<<'}';}
    if(postRetirementReserve_){const auto& r=*postRetirementReserve_;out<<",\"reserve\":{\"identity\":";IdentityJson(out,r.identity);
        out<<",\"sequence\":"<<r.sequence<<",\"observed_ns\":"<<r.observedNs<<",\"deadline_ns\":"<<r.deadlineNs<<",\"checked_ns\":"<<reserveChecked_
            <<",\"loaded\":"<<r.loaded<<",\"count\":"<<r.reserve<<",\"capacity\":"<<r.capacity<<",\"verified\":"<<(r.verified?"true":"false")<<'}';}
    out<<"}}";
}
}
