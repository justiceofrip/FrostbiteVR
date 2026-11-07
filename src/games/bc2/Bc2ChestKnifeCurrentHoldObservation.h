#pragma once
#include "Bc2ChestKnifeLiveObservation.h"
namespace fvr::bc2 {
// A separately requested current state observation. Historical incompleteness
// remains visible. This type is never accepted by a game/action predicate.
struct ChestKnifeCurrentHoldObservation {
    ChestKnifeLiveObservation current{};
    interaction::KnifeCommand drawCommand{};
    interaction::KnifeReceipt drawReceipt{};
    interaction::BodyItemKey drawOriginalGun{};
    chestKnifeNative::CurrentHoldWorkObservation nativeWork{};
    bool drawOnlyIssued=false,needsNativeCleanup=true,leftEmpty=false,bodyOperationMatches=false;
    static constexpr bool actionAuthority=false;
};
inline bool ChestKnifeCurrentHoldDrawMatches(const ChestKnifeCurrentHoldObservation& v)noexcept {
    const auto& d=v.drawCommand;const auto& r=v.drawReceipt;const auto& c=v.current;const auto& s=d.source;
    return d.kind==interaction::KnifeCommandKind::Draw&&d.serial&&d.serial==v.nativeWork.drawSerial&&
        d.hand==interaction::InteractionHand::Right&&d.returnToEmptyHands&&!d.stroke&&!d.stockTriggerHold&&
        d.knife==interaction::HandInteractionKey{c.knife.id,c.knife.generation}&&v.drawOriginalGun==c.originalGun&&
        s.owner.actor==c.input.owner.actor&&s.owner.actorGeneration==c.input.owner.actorGeneration&&
        s.owner.space==c.input.owner.space&&s.owner.equipGeneration&&s.sequence&&s.sequence<=c.input.sequence&&
        s.observedNs>0&&s.nowNs>=s.observedNs&&s.nowNs<s.deadlineNs&&s.deadlineNs-s.observedNs<=100000000&&
        s.focused&&s.tracked[0]&&s.tracked[1]&&!s.released[1]&&
        r.kind==d.kind&&r.serial==d.serial&&r.actor==s.owner.actor&&r.actorGeneration==s.owner.actorGeneration&&
        r.space==s.owner.space&&r.knife==d.knife&&r.observedNs>=s.observedNs&&r.nowNs>=r.observedNs&&
        r.nowNs<=c.input.nowNs&&r.deadlineNs>r.nowNs&&r.deadlineNs-r.observedNs<=100000000&&
        r.coherent&&r.originalAmmoUnchanged&&r.knifeSentinelUnchanged&&r.selectedKnife&&r.drawDidNotAttack&&r.holdGatesOwned&&
        !r.attackObserved&&!r.attackSettled&&!r.attackQuiescentOnly;
}
inline std::string_view ChestKnifeCurrentHoldRejection(const ChestKnifeCurrentHoldObservation& v,std::int64_t now)noexcept {
    const auto& c=v.current;
    if(!c.continuity||!c.bodyOperation||!c.draws||c.phase!=interaction::KnifePhase::Held||!c.held||
       !c.fireSuppressed||!c.mechanicsSettled||!c.nativeOwned||!c.nativeHealthy||!c.stockTriggerEnabled||
       c.attackBridgeEnabled||v.needsNativeCleanup||!v.leftEmpty||!v.bodyOperationMatches)
        return "current_nonfiring_hold_not_ready";
    if(c.nativeInspection!=chestKnifeNative::InspectReason::Available||!c.readinessKnife||
       c.readiness.reason!=chestKnifeNative::ReadyReason::Ready)return "current_knife_readiness_unavailable";
    if(!v.drawOnlyIssued||!chestKnifeNative::CurrentHoldWorkIdle(v.nativeWork)||v.nativeWork.bodyOperation!=c.bodyOperation)
        return "draw_only_idle_not_joined";
    if(!ChestKnifeCurrentHoldDrawMatches(v))return "accepted_draw_not_joined";
    // Run every current join even when lifetime observationComplete is false.
    // Never substitute an all-true copy into the strict Held validator.
    return ChestKnifeCurrentJoins(c,now);
}
inline bool ChestKnifeCurrentHoldContinues(const ChestKnifeCurrentHoldObservation& before,
    const ChestKnifeCurrentHoldObservation& after,std::int64_t now)noexcept {
    const auto& a=before.drawCommand;const auto& b=after.drawCommand;
    return ChestKnifeCurrentHoldRejection(before,now).empty()&&ChestKnifeCurrentHoldRejection(after,now).empty()&&
        ChestKnifeSameCurrent(before.current,after.current)&&before.nativeWork.lifetime==after.nativeWork.lifetime&&
        before.nativeWork.drawSerial==after.nativeWork.drawSerial&&a.serial==b.serial&&a.source.owner==b.source.owner&&
        a.source.sequence==b.source.sequence&&a.source.observedNs==b.source.observedNs&&a.source.deadlineNs==b.source.deadlineNs&&
        before.drawReceipt.observedNs==after.drawReceipt.observedNs&&before.drawReceipt.nowNs==after.drawReceipt.nowNs&&
        before.drawReceipt.deadlineNs==after.drawReceipt.deadlineNs;
}
inline std::string_view ChestKnifeCurrentHoldJoinRejection(const ChestKnifeCurrentHoldObservation& v,const ReloadStateOwner& owner,
    const interaction::HandInteractionSample& input,const interaction::InputFrame& controls,std::int64_t now)noexcept {
    if(const auto reason=ChestKnifeCurrentHoldRejection(v,now);!reason.empty())return reason;
    return ChestKnifeCurrentPacketJoins(v.current,owner,input,controls,now);
}
struct ChestKnifeCurrentHoldFailure {
    std::string_view reason{};
    interaction::HandInteractionSample input{};
    std::uint64_t bodyOperation=0;
    std::int64_t observedNs=0;
    unsigned dropped=0;
};
inline void WriteChestKnifeCurrentHoldDiagnostic(std::ostream& out,const std::optional<ChestKnifeCurrentHoldFailure>& failure){
    out<<"{\"observationOnly\":true,\"actionAuthority\":false,\"historical\":true,\"available\":";
    if(!failure){out<<"false}";return;}
    const auto& f=*failure;const auto& s=f.input;const auto& o=s.owner;
    out<<"true,\"reason\":\""<<f.reason<<"\",\"observedNs\":"<<f.observedNs<<",\"bodyOperation\":"<<f.bodyOperation
        <<",\"dropped\":"<<f.dropped<<",\"input\":{\"owner\":{\"actor\":"<<o.actor<<",\"actorGeneration\":"<<o.actorGeneration
        <<",\"equipGeneration\":"<<o.equipGeneration<<",\"space\":"<<o.space<<"},\"sequence\":"<<s.sequence
        <<",\"observedNs\":"<<s.observedNs<<",\"nowNs\":"<<s.nowNs<<",\"deadlineNs\":"<<s.deadlineNs<<"}}";
}
class ChestKnifeCurrentHoldPublication {
public:
    void Request(std::int64_t now)noexcept {
        if(now>0&&now<=INT64_MAX-1000000000)until_.store(now+1000000000,std::memory_order_release);
    }
    bool Requested(std::int64_t now)const noexcept{return now>0&&now<until_.load(std::memory_order_acquire);}
    void Begin()noexcept{value_.store({},std::memory_order_release);}
    void Invalidate()noexcept{Begin();if(continuity_!=UINT64_MAX)++continuity_;}
    void Reject(const ChestKnifeLiveObservation& value,std::int64_t now,std::string_view reason)noexcept {
        if(Requested(now)){
            if(diagnosticGate_.test_and_set(std::memory_order_acquire))dropped_.fetch_add(1,std::memory_order_relaxed);
            else{failure_={reason,value.input,value.bodyOperation,now,0};diagnosticGate_.clear(std::memory_order_release);}
        }
        Invalidate();
    }
    std::optional<ChestKnifeCurrentHoldFailure> ReadDiagnostic()const noexcept {
        if(diagnosticGate_.test_and_set(std::memory_order_acquire))return {};
        auto value=failure_;diagnosticGate_.clear(std::memory_order_release);
        if(!value.observedNs)return {};value.dropped=dropped_.load(std::memory_order_relaxed);return value;
    }
    bool Publish(ChestKnifeCurrentHoldObservation value,std::int64_t now)noexcept {
        value.current.continuity=continuity_;
        if(!Requested(now)){Invalidate();return false;}
        const auto reason=ChestKnifeCurrentHoldRejection(value,now);
        if(continuity_==UINT64_MAX||!reason.empty()){
            Reject(value.current,now,continuity_==UINT64_MAX?"continuity_exhausted":reason);return false;
        }
        try{value_.store(std::make_shared<const ChestKnifeCurrentHoldObservation>(value),std::memory_order_release);return true;}
        catch(...){Reject(value.current,now,"publication_allocation");return false;}
    }
    std::shared_ptr<const ChestKnifeCurrentHoldObservation> Read(std::int64_t now)const noexcept {
        const auto value=value_.load(std::memory_order_acquire);
        if(!Requested(now)||!value||!ChestKnifeCurrentHoldRejection(*value,now).empty())return {};
        return value;
    }
private:
    std::atomic<std::int64_t> until_{0};
    std::atomic<std::shared_ptr<const ChestKnifeCurrentHoldObservation>> value_{};
    std::uint64_t continuity_=1;
    mutable std::atomic_flag diagnosticGate_=ATOMIC_FLAG_INIT;
    std::atomic<unsigned> dropped_=0;
    ChestKnifeCurrentHoldFailure failure_{};
};
inline void WriteChestKnifeCurrentHoldObservation(std::ostream& out,const ChestKnifeCurrentHoldObservation* value,std::int64_t now,
    std::string_view packetReason={}){
    const auto reason=!packetReason.empty()?packetReason:value?ChestKnifeCurrentHoldRejection(*value,now):std::string_view("observation_unavailable");
    out<<"{\"schema\":\"fvr.bc2.current-nonfiring-hold.v1\",\"observationOnly\":true,\"actionAuthority\":false"
        <<",\"scope\":\"current_nonfiring_hold\",\"available\":"<<(reason.empty()?"true":"false")<<",\"observedNs\":"<<now<<",\"reason\":";
    if(!reason.empty()){out<<'"'<<reason<<"\"}";return;}
    const auto& v=*value;const auto& c=v.current;const auto& d=v.drawCommand;const auto& r=v.drawReceipt;
    const auto flag=[&](bool b){out<<(b?"true":"false");};
    const auto key=[&](const auto& k){out<<"{\"id\":"<<k.id<<",\"generation\":"<<k.generation<<'}';};
    const auto owner=[&](const ReloadStateOwner& o){out<<"{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak
        <<",\"weapon\":"<<o.weapon<<",\"actorGeneration\":"<<o.actorGeneration<<",\"equipGeneration\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}';};
    const auto handOwner=[&](const interaction::HandInteractionOwner& o){out<<"{\"actor\":"<<o.actor<<",\"actorGeneration\":"<<o.actorGeneration
        <<",\"equipGeneration\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}';};
    const auto input=[&](const interaction::HandInteractionSample& s){out<<"{\"owner\":";handOwner(s.owner);
        out<<",\"sequence\":"<<s.sequence<<",\"observedNs\":"<<s.observedNs<<",\"nowNs\":"<<s.nowNs<<",\"deadlineNs\":"<<s.deadlineNs
            <<",\"focused\":";flag(s.focused);out<<",\"tracked\":[";flag(s.tracked[0]);out<<',';flag(s.tracked[1]);
        out<<"],\"released\":[";flag(s.released[0]);out<<',';flag(s.released[1]);out<<"]}";};
    out<<"null,\"validUntilNs\":"<<ChestKnifeLiveDeadline(c)<<",\"continuity\":"<<c.continuity<<",\"owner\":";owner(c.owner);
    out<<",\"originalOwner\":";owner(c.originalOwner);out<<",\"knife\":";key(c.knife);out<<",\"originalGun\":";key(c.originalGun);
    out<<",\"bodyOperation\":"<<c.bodyOperation<<",\"nativeLifetime\":"<<v.nativeWork.lifetime<<",\"drawReceipts\":"<<c.draws
        <<",\"returnReceipts\":"<<c.returns<<",\"phase\":"<<unsigned(c.phase)<<",\"held\":true,\"nativeOwned\":true,\"nativeHealthy\":true"
        <<",\"fireSuppressed\":true,\"mechanicsSettled\":true,\"nativeObservationComplete\":";flag(c.nativeObservationComplete);
    out<<",\"motionEnabled\":"<<(c.motionEnabled?"true":"false")<<",\"attackBridgeEnabled\":false"
        <<",\"firstObservationGap\":{\"reason\":"<<c.firstObservationGap/4<<",\"branch\":"<<(c.firstObservationGap?c.firstObservationGap%4:3)
        <<"},\"noAttackOrReturnSinceDraw\":true,\"nativeWorkIdle\":true,\"needsNativeCleanup\":false,\"leftEmpty\":true,\"inputEvidence\":";input(c.input);
    out<<",\"menu\":{\"owner\":"<<unsigned(c.controls.menu.owner)<<",\"sequence\":"<<c.controls.menu.sequence<<",\"epoch\":"<<c.controls.menu.epoch
        <<",\"deadlineNs\":"<<c.controls.menu.deadlineNs<<"},\"trackingBasisGeneration\":"<<c.controls.trackingBasisGeneration
        <<",\"worldUnitsPerMeter\":"<<c.controls.worldUnitsPerMeter<<",\"controls\":[";
    for(unsigned n=0;n<2;++n){if(n)out<<',';const auto& h=c.controls.hands[n];out<<"{\"held\":"<<h.held<<",\"squeeze\":"<<h.squeeze
        <<",\"trigger\":"<<h.trigger<<",\"stickX\":"<<h.stickX<<",\"stickY\":"<<h.stickY<<'}';}
    const auto& h=c.hold;const auto& t=h.token;out<<"],\"hold\":{\"id\":"<<t.id<<",\"owner\":";handOwner(t.owner);
    out<<",\"hand\":"<<unsigned(t.hand)<<",\"kind\":"<<unsigned(t.kind)<<",\"item\":";key(t.item);out<<",\"contact\":";key(t.contact);
    out<<",\"prerequisiteClaim\":"<<t.prerequisiteClaim<<",\"inputSequence\":"<<h.inputSequence<<",\"deadlineNs\":"<<h.deadlineNs<<'}';
    out<<",\"render\":{\"owner\":";owner(c.rendered.source.owner);out<<",\"knife\":";key(c.rendered.source.knife);
    out<<",\"bodyOperation\":"<<c.rendered.source.bodyOperation<<",\"rigFingerprint\":"<<c.rendered.rigFingerprint<<",\"inputEvidence\":";input(c.rendered.source.input);out<<'}';
    out<<",\"nativeSequence\":"<<c.nativeSequence<<",\"nativeObservedNs\":"<<c.nativeObservedNs<<",\"knifeSlot\":"<<c.knifeSlot
        <<",\"selectedSlots\":[7,7,7],\"nativeTick\":"<<c.nativeTick<<",\"suppressionRequest\":"<<c.suppressionRequest<<",\"readiness\":";
    chestKnifeNative::WriteReadyObservation(out,c.readiness);
    out<<",\"drawCommand\":{\"historical\":true,\"kind\":"<<unsigned(d.kind)<<",\"serial\":"<<d.serial<<",\"knife\":";key(d.knife);
    out<<",\"hand\":"<<unsigned(d.hand)<<",\"returnToEmptyHands\":true,\"source\":";input(d.source);out<<'}';
    out<<",\"drawReceipt\":{\"historical\":true,\"kind\":"<<unsigned(r.kind)<<",\"serial\":"<<r.serial<<",\"actor\":"<<r.actor
        <<",\"actorGeneration\":"<<r.actorGeneration<<",\"space\":"<<r.space<<",\"knife\":";key(r.knife);
    out<<",\"observedNs\":"<<r.observedNs<<",\"nowNs\":"<<r.nowNs<<",\"deadlineNs\":"<<r.deadlineNs
        <<",\"coherent\":true,\"originalAmmoUnchanged\":true,\"knifeSentinelUnchanged\":true,\"selectedKnife\":true"
        <<",\"drawDidNotAttack\":true,\"holdGatesOwned\":true,\"attackObserved\":false,\"attackSettled\":false,\"attackQuiescentOnly\":false}}";
}
// Only passive reads. A missing gather, new continuity or elapsed source lease
// invalidates this optional section; serializing never renews any source clock.
template<class Read,class Clock>
std::string ObserveChestKnifeCurrentHoldJson(Read read,Clock clock){
    const auto value=read(clock());const auto observed=clock();
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(9);
    WriteChestKnifeCurrentHoldObservation(out,value.get(),observed);
    if(!value||!ChestKnifeCurrentHoldRejection(*value,observed).empty())return out.str();
    const auto latest=read(clock());const auto completed=clock();
    if(latest&&ChestKnifeCurrentHoldContinues(*value,*latest,completed))return out.str();
    std::ostringstream unavailable;WriteChestKnifeCurrentHoldObservation(unavailable,nullptr,completed);return unavailable.str();
}
template<class Read,class Clock>
std::string ObserveChestKnifeCurrentHoldJson(Read read,Clock clock,const ReloadStateOwner& owner,
    const interaction::HandInteractionSample& input,const interaction::InputFrame& controls){
    const auto value=read(clock());const auto observed=clock();
    const auto reason=value?ChestKnifeCurrentHoldJoinRejection(*value,owner,input,controls,observed):std::string_view("observation_unavailable");
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(9);
    WriteChestKnifeCurrentHoldObservation(out,value.get(),observed,reason);
    if(!reason.empty())return out.str();
    const auto latest=read(clock());const auto completed=clock();
    if(latest&&ChestKnifeCurrentHoldContinues(*value,*latest,completed)&&
       ChestKnifeCurrentHoldJoinRejection(*value,owner,input,controls,completed).empty())return out.str();
    std::ostringstream unavailable;WriteChestKnifeCurrentHoldObservation(unavailable,nullptr,completed);return unavailable.str();
}
}
