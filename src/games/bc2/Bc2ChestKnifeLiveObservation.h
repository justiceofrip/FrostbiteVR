#pragma once
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeInput.h"
#include "Bc2ChestKnifeNative.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <ostream>
#include <sstream>
#include <locale>
#include <string>
#include <string_view>
namespace fvr::bc2 {
// One retained successful native Return plus its exact policy acceptance join.
// Historical clocks are verbatim. This survives Held/gather absence and expiry;
// it can never supply a current Empty proof, hold, action or retirement lease.
struct ChestKnifeReturnObservation {
    chestKnifeNative::ReturnCompletion native{};
    interaction::KnifeCommand drawCommand{};
    interaction::KnifeReceipt drawReceipt{},policyReceipt{};
    interaction::BodyItemKey originalGun{};
    std::uint64_t draws=0,returns=0;
    std::int64_t policyObservedNs=0;
    bool policyAccepted=false,policyMatched=false;
};
inline bool ChestKnifeReturnPolicyMatch(const ChestKnifeReturnObservation& v,std::uint64_t operation)noexcept {
    const auto& n=v.native;const auto& c=n.command;const auto& r=v.policyReceipt;const auto& d=v.drawCommand;
    const auto receiptMatches=[](const interaction::KnifeReceipt& a,const interaction::KnifeReceipt& b){
        return a.kind==b.kind&&a.serial==b.serial&&a.actor==b.actor&&a.actorGeneration==b.actorGeneration&&
            a.space==b.space&&a.knife==b.knife&&a.observedNs==b.observedNs&&a.nowNs==b.nowNs&&a.deadlineNs==b.deadlineNs&&
            a.coherent==b.coherent&&a.originalAmmoUnchanged==b.originalAmmoUnchanged&&a.knifeSentinelUnchanged==b.knifeSentinelUnchanged&&
            a.selectedKnife==b.selectedKnife&&a.attackObserved==b.attackObserved&&a.attackSettled==b.attackSettled&&
            a.drawDidNotAttack==b.drawDidNotAttack&&a.holdGatesOwned==b.holdGatesOwned&&
            a.originalSelectedHidden==b.originalSelectedHidden&&a.fireSuppressed==b.fireSuppressed&&a.attackQuiescentOnly==b.attackQuiescentOnly;
    };
    return v.policyAccepted&&n.bodyOperation&&n.bodyOperation==operation&&n.lifetime&&n.chooserMask==3&&
        c.kind==interaction::KnifeCommandKind::Return&&c.serial&&r.kind==c.kind&&r.serial==c.serial&&
        r.actor==c.source.owner.actor&&r.actorGeneration==c.source.owner.actorGeneration&&r.space==c.source.owner.space&&
        r.knife==c.knife&&receiptMatches(r,n.receipt)&&r.coherent&&r.originalAmmoUnchanged&&r.knifeSentinelUnchanged&&
        r.originalSelectedHidden&&r.fireSuppressed&&v.policyObservedNs==r.nowNs&&
        v.originalGun.id==n.anchor.original.client.owner.weapon&&v.originalGun.generation&&
        d.kind==interaction::KnifeCommandKind::Draw&&d.serial&&d.serial<c.serial&&d.knife==c.knife&&
        c.knife.id==n.anchor.knifeWeapon&&
        c.source.owner.actor==((std::uint64_t(n.anchor.original.client.owner.weak)<<32)|n.anchor.original.client.owner.soldier)&&
        c.source.owner.actorGeneration==n.anchor.original.client.owner.actorGeneration&&
        d.source.owner.actor==c.source.owner.actor&&d.source.owner.actorGeneration==c.source.owner.actorGeneration&&
        v.drawReceipt.kind==d.kind&&v.drawReceipt.serial==d.serial&&v.drawReceipt.knife==d.knife&&
        v.drawReceipt.actor==d.source.owner.actor&&v.drawReceipt.actorGeneration==d.source.owner.actorGeneration&&
        v.drawReceipt.space==d.source.owner.space&&v.drawReceipt.coherent&&v.drawReceipt.selectedKnife&&
        v.drawReceipt.drawDidNotAttack&&v.drawReceipt.holdGatesOwned&&v.draws&&v.returns;
}
class ChestKnifeReturnPublication {
public:
    void Publish(const chestKnifeNative::ReturnCompletion& native,const interaction::KnifeReceipt& receipt,bool accepted,
        const std::optional<interaction::BodyItemKey>& original,const std::optional<interaction::KnifeCommand>& draw,
        const std::optional<interaction::KnifeReceipt>& drawn,std::uint64_t operation,std::uint64_t draws,std::uint64_t returns,
        std::int64_t policyNow)noexcept {
        ChestKnifeReturnObservation value;value.native=native;value.policyReceipt=receipt;value.policyAccepted=accepted;
        if(original)value.originalGun=*original;if(draw)value.drawCommand=*draw;if(drawn)value.drawReceipt=*drawn;
        value.draws=draws;value.returns=returns;value.policyObservedNs=policyNow;
        value.policyMatched=ChestKnifeReturnPolicyMatch(value,operation);
        try{value_.store(std::make_shared<const ChestKnifeReturnObservation>(value),std::memory_order_release);}
        catch(...){value_.store({},std::memory_order_release);}
    }
    std::shared_ptr<const ChestKnifeReturnObservation> Read()const noexcept{return value_.load(std::memory_order_acquire);}
private:
    std::atomic<std::shared_ptr<const ChestKnifeReturnObservation>> value_{};
};
struct ChestKnifeReturnDiagnostic {
    std::shared_ptr<const ChestKnifeReturnObservation> completed;
    std::optional<chestKnifeNative::ReturnPendingObservation> pending;
};
inline void WriteChestKnifeReturnObservation(std::ostream& out,const ChestKnifeReturnDiagnostic& value) {
    const auto flag=[&](bool b){out<<(b?"true":"false");};
    const auto key=[&](const auto& k){out<<"{\"id\":"<<k.id<<",\"generation\":"<<k.generation<<'}';};
    const auto owner=[&](const ReloadStateOwner& o){out<<"{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak
        <<",\"weapon\":"<<o.weapon<<",\"actorGeneration\":"<<o.actorGeneration<<",\"equipGeneration\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}';};
    const auto input=[&](const interaction::HandInteractionSample& s){out<<"{\"owner\":{\"actor\":"<<s.owner.actor
        <<",\"actorGeneration\":"<<s.owner.actorGeneration<<",\"equipGeneration\":"<<s.owner.equipGeneration<<",\"space\":"<<s.owner.space
        <<"},\"sequence\":"<<s.sequence<<",\"observedNs\":"<<s.observedNs<<",\"deadlineNs\":"<<s.deadlineNs<<",\"nowNs\":"<<s.nowNs
        <<",\"focused\":";flag(s.focused);out<<",\"tracked\":[";flag(s.tracked[0]);out<<',';flag(s.tracked[1]);
        out<<"],\"released\":[";flag(s.released[0]);out<<',';flag(s.released[1]);out<<"]}";};
    const auto command=[&](const interaction::KnifeCommand& c){out<<"{\"kind\":"<<unsigned(c.kind)<<",\"serial\":"<<c.serial
        <<",\"knife\":";key(c.knife);out<<",\"hand\":"<<unsigned(c.hand)<<",\"returnToEmptyHands\":";flag(c.returnToEmptyHands);
        out<<",\"source\":";input(c.source);out<<'}';};
    const auto receipt=[&](const interaction::KnifeReceipt& r){out<<"{\"kind\":"<<unsigned(r.kind)<<",\"serial\":"<<r.serial
        <<",\"actor\":"<<r.actor<<",\"actorGeneration\":"<<r.actorGeneration<<",\"space\":"<<r.space<<",\"knife\":";key(r.knife);
        out<<",\"observedNs\":"<<r.observedNs<<",\"deadlineNs\":"<<r.deadlineNs<<",\"nowNs\":"<<r.nowNs<<",\"coherent\":";flag(r.coherent);
        out<<",\"originalAmmoUnchanged\":";flag(r.originalAmmoUnchanged);out<<",\"knifeSentinelUnchanged\":";flag(r.knifeSentinelUnchanged);
        out<<",\"selectedKnife\":";flag(r.selectedKnife);out<<",\"drawDidNotAttack\":";flag(r.drawDidNotAttack);out<<",\"holdGatesOwned\":";flag(r.holdGatesOwned);
        out<<",\"originalSelectedHidden\":";flag(r.originalSelectedHidden);out<<",\"fireSuppressed\":";flag(r.fireSuppressed);out<<'}';};
    const auto rig=[&](const RigIdentity& r){out<<"{\"soldier\":"<<r.soldier<<",\"weak\":"<<r.weak<<",\"animation\":"<<r.animation<<",\"skeleton\":"<<r.skeleton
        <<",\"pose\":"<<r.pose<<",\"worldHeader\":"<<r.worldHeader<<",\"worldMatrices\":"<<r.worldMatrices<<",\"skinMatrices\":"<<r.skinMatrices
        <<",\"evaluatedMatrices\":"<<r.evaluatedMatrices<<",\"count\":"<<r.count<<",\"nativeIk\":";flag(r.nativeIk);out<<'}';};
    const auto triple=[&](const auto& a){out<<'['<<a[0]<<','<<a[1]<<','<<a[2]<<']';};
    const auto states=[&](const auto& a,const auto& b){out<<'[';unsigned i=0;for(const auto* s:{&a[0],&b,&a[1]}){
        if(i++)out<<',';out<<"{\"address\":"<<s->address<<",\"current\":"<<s->currentState<<",\"next\":"<<s->nextState
            <<",\"previous\":"<<s->previousState<<",\"phaseTimer\":"<<s->phaseTimer<<",\"flagsA8\":"<<unsigned(s->flagsA8)
            <<",\"loaded\":"<<s->loaded<<",\"reserve\":"<<s->reserve<<'}';}out<<']';};
    out<<"{\"schema\":\"fvr.bc2.chest-knife-return.v1\",\"observationOnly\":true,\"actionAuthority\":false,\"freshEmptyRequired\":true,\"available\":";
    flag(bool(value.completed)||bool(value.pending));out<<",\"pending\":";
    if(value.pending){const auto& p=*value.pending;out<<"{\"historicalLastObserve\":true,\"command\":";command(p.command);
        out<<",\"originalOwner\":";owner(p.originalOwner);out<<",\"lifetime\":"<<p.lifetime<<",\"bodyOperation\":"<<p.bodyOperation
            <<",\"chooserMask\":"<<p.chooserMask<<",\"returnReady\":";flag(p.returnReady);
        out<<",\"lastObserveReason\":"<<unsigned(p.lastObserveReason)<<",\"lastObservedNs\":"<<p.lastObservedNs<<'}';}else out<<"null";
    out<<",\"completed\":";
    if(!value.completed){out<<"null}";return;}
    const auto& v=*value.completed;const auto& n=v.native;const auto& a=n.anchor;const auto& s=n.snapshot;const auto& f=n.freeRight;
    out<<"{\"historical\":true,\"nativeCompleted\":true,\"policyAccepted\":";flag(v.policyAccepted);
    out<<",\"policyMatched\":";flag(v.policyMatched);out<<",\"policyObservedNs\":"<<v.policyObservedNs<<",\"bodyOperation\":"<<n.bodyOperation
        <<",\"lifetime\":"<<n.lifetime<<",\"chooserMask\":"<<n.chooserMask<<",\"chooserAcceptedNs\":"<<n.chooserAcceptedNs
        <<",\"drawReceipts\":"<<v.draws<<",\"returnReceipts\":"<<v.returns<<",\"originalGun\":";key(v.originalGun);
    out<<",\"command\":";command(n.command);out<<",\"nativeReceipt\":";receipt(n.receipt);out<<",\"policyReceipt\":";receipt(v.policyReceipt);
    out<<",\"drawCommand\":";command(v.drawCommand);out<<",\"drawReceipt\":";receipt(v.drawReceipt);
    out<<",\"originalAnchor\":{\"owner\":";owner(a.original.client.owner);out<<",\"sequence\":"<<a.original.client.sequence<<",\"observedNs\":"<<a.original.client.observedNs
        <<",\"originalSlot\":"<<a.original.client.selectedSlot<<",\"clientInventory\":"<<a.original.links.clientInventory<<",\"serverInventory\":"<<a.original.links.inventory
        <<",\"serverPlayer\":"<<a.original.links.player<<",\"serverActor\":"<<a.original.links.soldier<<",\"serverItem\":"<<a.original.links.item
        <<",\"knifeWeapon\":"<<a.knifeWeapon<<",\"knifeSlot\":"<<a.knifeSlot<<",\"knifeData\":"<<a.knifeData<<",\"knifeFiringData\":"<<a.knifeFiringData
        <<",\"knifeFiring\":";triple(a.knifeFiring);out<<",\"originalCopies\":";states(a.original.client.branches,a.original.state);out<<'}';
    out<<",\"nativeCompletion\":{\"owner\":";owner(s.selectedOwner);out<<",\"sequence\":"<<s.sequence<<",\"observedNs\":"<<s.observedNs
        <<",\"inventory\":";triple(s.selected.inventory);out<<",\"selected\":";triple(s.selected.current);out<<",\"previous\":";triple(s.selected.previous);
    out<<",\"originalCopies\":";states(s.original.client.branches,s.original.server);out<<",\"knifeCopies\":";states(s.knife.client.branches,s.knife.server);out<<'}';
    out<<",\"freeRight\":{\"validatedAtCompletion\":true,\"visibilityOwner\":";owner(f.visibilityOwner);out<<",\"receiptOwner\":";owner(f.receiptOwner);
    out<<",\"visibilityInput\":";input(f.visibilityInput);out<<",\"input\":";input(f.input);
    out<<",\"visibilityRequest\":"<<f.visibilityRequest<<",\"receiptRequest\":"<<f.receiptRequest<<",\"receiptInputSequence\":"<<f.receiptInputSequence
        <<",\"physicalEquipGeneration\":"<<f.physicalEquipGeneration<<",\"drawSerial\":"<<f.drawSerial<<",\"verifiedCopyMask\":"<<f.verifiedCopyMask
        <<",\"observedNs\":"<<f.observedNs<<",\"deadlineNs\":"<<f.deadlineNs<<",\"authorizationDeadlineNs\":"<<f.authorizationDeadlineNs
        <<",\"visibilityAuthorizationDeadlineNs\":"<<f.visibilityAuthorizationDeadlineNs<<",\"visibilityEnabled\":";flag(f.visibilityEnabled);
    out<<",\"visibilityHide\":";flag(f.visibilityHide);out<<",\"hidden\":";flag(f.hidden);
    out<<",\"rig\":";rig(f.rig);out<<",\"plan\":{\"present\":";flag(f.planPresent);out<<",\"owner\":";owner(f.planOwner);
    out<<",\"rig\":";rig(f.planRig);out<<",\"request\":"<<f.planRequest<<",\"inputSequence\":"<<f.planInputSequence
        <<",\"physicalEquipGeneration\":"<<f.planPhysicalEquipGeneration<<",\"meshSequence\":"<<f.planMeshSequence
        <<",\"observedNs\":"<<f.planObservedNs<<",\"deadlineNs\":"<<f.planDeadlineNs<<",\"inputDeadlineNs\":"<<f.planInputDeadlineNs
        <<",\"hidden\":";flag(f.planHidden);out<<"},\"suppression\":{\"owner\":";owner(f.suppression.owner);out<<",\"input\":";input(f.suppression.input);
    out<<",\"request\":"<<f.suppression.request<<",\"nativeTick\":"<<f.suppression.nativeTick<<",\"cache\":"<<f.suppression.cache
        <<",\"equipmentDispatched\":";flag(f.suppression.equipmentDispatched);out<<"}}}}";
}

// On-demand, immutable gather observation. Identities are comparison values;
// this is neither an action capability nor a replacement native receipt.
struct ChestKnifeLiveObservation {
    ReloadStateOwner owner{},originalOwner{};
    interaction::BodyItemKey knife{},originalGun{};
    interaction::HandInteractionSample input{};
    interaction::InputFrame controls{};
    interaction::HandClaim hold{};
    ChestKnifeRenderReceipt rendered{};
    std::uint64_t continuity=0,bodyOperation=0,draws=0,returns=0,nativeSequence=0,nativeTick=0,suppressionRequest=0;
    std::int64_t nativeObservedNs=0;
    std::array<unsigned,3> selectedSlots{};
    unsigned knifeSlot=0;
    interaction::KnifePhase phase=interaction::KnifePhase::NeedNeutral;
    bool nativeOwned=false,nativeHealthy=false,nativeObservationComplete=false;
    bool stockTriggerEnabled=false,motionEnabled=false,attackBridgeEnabled=false;
    bool held=false,stockTriggerReady=false,stockTriggerArmed=false,fireSuppressed=false,mechanicsSettled=false;
};
inline std::int64_t ChestKnifeLiveDeadline(const ChestKnifeLiveObservation& v)noexcept {
    return (std::min)({v.input.deadlineNs,v.controls.menu.deadlineNs,v.hold.deadlineNs,v.rendered.source.input.deadlineNs});
}
inline std::string_view ChestKnifeLiveRejection(const ChestKnifeLiveObservation& v,std::int64_t now)noexcept {
    const auto& f=v.input;const auto& o=v.owner;const auto& original=v.originalOwner;
    if(!v.continuity||!v.bodyOperation||!v.draws||v.phase!=interaction::KnifePhase::Held||
       !v.held||!v.stockTriggerReady||!v.stockTriggerArmed||!v.fireSuppressed||!v.mechanicsSettled||
       !v.nativeOwned||!v.nativeHealthy||!v.nativeObservationComplete||!v.stockTriggerEnabled||v.motionEnabled||v.attackBridgeEnabled)
        return "held_stock_trigger_not_ready";
    if(!o.player||!o.soldier||!o.weak||!o.weapon||!o.actorGeneration||!o.equipGeneration||!o.space||
       original.player!=o.player||original.soldier!=o.soldier||original.weak!=o.weak||
       original.actorGeneration!=o.actorGeneration||original.space!=o.space||!original.equipGeneration||
       !original.weapon||original.weapon==o.weapon||v.originalGun.id!=original.weapon||!v.originalGun.generation||
       v.knife.id!=o.weapon||!v.knife.generation||f.owner.actor!=((std::uint64_t(o.weak)<<32)|o.soldier)||
       f.owner.actorGeneration!=o.actorGeneration||!f.owner.equipGeneration||f.owner.space!=o.space)
        return "native_or_physical_owner_mismatch";
    if(now<=0||!f.sequence||f.observedNs<=0||f.observedNs>now||f.nowNs<f.observedNs||f.nowNs>now||
       f.deadlineNs<=now||f.deadlineNs-f.observedNs>100000000||!f.focused||!f.tracked[0]||!f.tracked[1]||f.released[1]||
       ChestKnifeLiveDeadline(v)<=now)return "source_expired_or_invalid";
    if(!ChestKnifeControlsNeutral(v.controls)||!v.controls.trackingBasisGeneration||v.controls.generation!=f.sequence||v.controls.spaceGeneration!=f.owner.space||
       v.controls.hands[0].squeeze>.35f||v.controls.hands[1].squeeze<.65f||
       !interaction::GameplayOwnsInput(v.controls.menu,now))return "controls_or_menu_invalid";
    const auto& h=v.hold;const auto& t=h.token;
    if(!t.id||t.owner!=f.owner||t.hand!=interaction::InteractionHand::Right||t.kind!=interaction::HandClaimKind::GunHold||
       t.item!=interaction::HandInteractionKey{v.knife.id,v.knife.generation}||!t.contact.id||!t.contact.generation||
       t.prerequisiteClaim||h.inputSequence!=f.sequence||h.deadlineNs>f.deadlineNs)
        return "hold_not_joined";
    if(v.rendered.source.bodyOperation!=v.bodyOperation||v.rendered.source.knife!=v.knife||
       v.rendered.rigFingerprint!=ChestKnifeGripProfiles()[0].rigFingerprint||
       !ChestKnifeRenderCurrent(v.rendered.source,o,f,now)||
       (v.rendered.source.input.sequence==f.sequence&&
        (v.rendered.source.input.observedNs!=f.observedNs||v.rendered.source.input.deadlineNs!=f.deadlineNs)))
        return "render_not_joined";
    if(v.knifeSlot!=7||v.selectedSlots!=std::array<unsigned,3>{v.knifeSlot,v.knifeSlot,v.knifeSlot}||
       v.nativeSequence!=f.sequence||v.nativeObservedNs<f.observedNs||v.nativeObservedNs>f.nowNs||
       !v.nativeTick||!v.suppressionRequest)return "native_selection_or_suppression_not_joined";
    return {};
}
inline std::string_view ChestKnifeLiveJoinRejection(const ChestKnifeLiveObservation& v,const ReloadStateOwner& owner,
    const interaction::HandInteractionSample& input,const interaction::InputFrame& controls,std::int64_t now)noexcept {
    if(const auto reason=ChestKnifeLiveRejection(v,now);!reason.empty())return reason;
    const auto& f=v.input;
    if(v.owner!=owner||f.owner!=input.owner||!input.sequence||input.observedNs<=0||input.observedNs>now||
       input.nowNs<input.observedNs||input.nowNs>now||input.deadlineNs<=now||input.deadlineNs-input.observedNs>100000000||
       !input.focused||!input.tracked[0]||!input.tracked[1]||input.released[1]||
       !ChestKnifeControlsNeutral(controls)||controls.generation!=input.sequence||controls.spaceGeneration!=input.owner.space||
       controls.hands[0].squeeze>.35f||controls.hands[1].squeeze<.65f||controls.worldUnitsPerMeter!=v.controls.worldUnitsPerMeter||
       controls.trackingBasisGeneration!=v.controls.trackingBasisGeneration||controls.menu.epoch!=v.controls.menu.epoch||
       !interaction::GameplayOwnsInput(controls.menu,now)||
       (f.sequence==input.sequence&&(f.observedNs!=input.observedNs||f.deadlineNs!=input.deadlineNs))||
       (f.sequence<input.sequence&&f.observedNs>=input.observedNs)||(f.sequence>input.sequence&&f.observedNs<=input.observedNs))
        return "snapshot_owner_or_input_not_joined";
    return {};
}
inline bool ChestKnifeLiveContinues(const ChestKnifeLiveObservation& before,const ChestKnifeLiveObservation& after,std::int64_t now)noexcept {
    return ChestKnifeLiveRejection(before,now).empty()&&ChestKnifeLiveRejection(after,now).empty()&&
        before.continuity==after.continuity&&before.owner==after.owner&&before.originalOwner==after.originalOwner&&
        before.knife==after.knife&&before.originalGun==after.originalGun&&before.bodyOperation==after.bodyOperation&&
        before.hold.token==after.hold.token&&before.draws==after.draws&&before.returns==after.returns&&
        before.controls.trackingBasisGeneration==after.controls.trackingBasisGeneration&&
        before.controls.worldUnitsPerMeter==after.controls.worldUnitsPerMeter&&
        before.controls.menu.epoch==after.controls.menu.epoch&&before.input.sequence<=after.input.sequence&&
        (before.input.sequence==after.input.sequence?
            before.input.observedNs==after.input.observedNs&&before.input.deadlineNs==after.input.deadlineNs:
            before.input.observedNs<after.input.observedNs);
}
// One gather writer, explicit snapshot readers. Missing/rejected source clears
// authority immediately; recovery gets a new continuity identity. The request
// window enables copying only and cannot extend any source or claim deadline.
class ChestKnifeLivePublication {
public:
    void Request(std::int64_t now)noexcept {
        if(now>0&&now<=INT64_MAX-1000000000)until_.store(now+1000000000,std::memory_order_release);
    }
    bool Requested(std::int64_t now)const noexcept{return now>0&&now<until_.load(std::memory_order_acquire);}
    void Begin()noexcept{value_.store({},std::memory_order_release);}
    void Invalidate()noexcept{Begin();if(continuity_!=UINT64_MAX)++continuity_;}
    bool Publish(ChestKnifeLiveObservation value,std::int64_t now)noexcept {
        value.continuity=continuity_;
        if(!Requested(now)||continuity_==UINT64_MAX||!ChestKnifeLiveRejection(value,now).empty()){Invalidate();return false;}
        try{value_.store(std::make_shared<const ChestKnifeLiveObservation>(value),std::memory_order_release);return true;}
        catch(...){Invalidate();return false;}
    }
    std::shared_ptr<const ChestKnifeLiveObservation> Read(std::int64_t now)const noexcept {
        const auto value=value_.load(std::memory_order_acquire);
        if(!Requested(now)||!value||!ChestKnifeLiveRejection(*value,now).empty())return {};
        return value;
    }
private:
    std::atomic<std::int64_t> until_{0};
    std::atomic<std::shared_ptr<const ChestKnifeLiveObservation>> value_{};
    std::uint64_t continuity_=1; // Original gather writer only.
};
inline void WriteChestKnifeLiveObservation(std::ostream& out,const ChestKnifeLiveObservation* value,
    const ReloadStateOwner& owner,const interaction::HandInteractionSample& input,const interaction::InputFrame& controls,std::int64_t now) {
    const auto reason=value?ChestKnifeLiveJoinRejection(*value,owner,input,controls,now):std::string_view("observation_unavailable");
    out<<"{\"observationOnly\":true,\"available\":"<<(reason.empty()?"true":"false")<<",\"observedNs\":"<<now<<",\"reason\":";
    if(!reason.empty()){out<<'"'<<reason<<"\"}";return;}
    const auto& v=*value;const auto& f=v.input;const auto& t=v.hold.token;const auto& r=v.rendered.source;
    const auto nativeOwner=[&](const ReloadStateOwner& o){out<<"{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak
        <<",\"weapon\":"<<o.weapon<<",\"actorGeneration\":"<<o.actorGeneration<<",\"equipGeneration\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}';};
    const auto handOwner=[&](const interaction::HandInteractionOwner& o){out<<"{\"actor\":"<<o.actor<<",\"actorGeneration\":"<<o.actorGeneration
        <<",\"equipGeneration\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}';};
    const auto key=[&](const auto& k){out<<"{\"id\":"<<k.id<<",\"generation\":"<<k.generation<<'}';};
    const auto evidence=[&](const interaction::HandInteractionSample& e){out<<"{\"owner\":";handOwner(e.owner);
        out<<",\"sequence\":"<<e.sequence<<",\"observedNs\":"<<e.observedNs<<",\"deadlineNs\":"<<e.deadlineNs<<",\"nowNs\":"<<e.nowNs
            <<",\"focused\":"<<(e.focused?"true":"false")<<",\"tracked\":["<<(e.tracked[0]?"true":"false")<<','<<(e.tracked[1]?"true":"false")
            <<"],\"released\":["<<(e.released[0]?"true":"false")<<','<<(e.released[1]?"true":"false")<<"]}";};
    out<<"null,\"validUntilNs\":"<<ChestKnifeLiveDeadline(v)<<",\"continuity\":"<<v.continuity<<",\"owner\":";nativeOwner(v.owner);
    out<<",\"originalOwner\":";nativeOwner(v.originalOwner);out<<",\"knife\":";key(v.knife);out<<",\"originalGun\":";key(v.originalGun);
    out<<",\"bodyOperation\":"<<v.bodyOperation<<",\"drawReceipts\":"<<v.draws<<",\"returnReceipts\":"<<v.returns
        <<",\"phase\":"<<unsigned(v.phase)<<",\"held\":true,\"stockTriggerReady\":true,\"stockTriggerArmed\":true"
        <<",\"nativeOwned\":true,\"nativeHealthy\":true,\"nativeObservationComplete\":true,\"stockTriggerEnabled\":true"
        <<",\"motionEnabled\":false,\"attackBridgeEnabled\":false,\"fireSuppressed\":true,\"mechanicsSettled\":true,\"inputEvidence\":";evidence(f);
    out<<",\"menu\":{\"owner\":"<<unsigned(v.controls.menu.owner)<<",\"sequence\":"<<v.controls.menu.sequence<<",\"epoch\":"<<v.controls.menu.epoch
        <<",\"deadlineNs\":"<<v.controls.menu.deadlineNs<<"},\"trackingBasisGeneration\":"<<v.controls.trackingBasisGeneration<<",\"controls\":[";
    for(unsigned n=0;n<2;++n){if(n)out<<',';const auto& h=v.controls.hands[n];out<<"{\"held\":"<<h.held<<",\"squeeze\":"<<h.squeeze
        <<",\"trigger\":"<<h.trigger<<",\"stickX\":"<<h.stickX<<",\"stickY\":"<<h.stickY<<'}';}
    out<<"],\"hold\":{\"id\":"<<t.id<<",\"owner\":";handOwner(t.owner);out<<",\"hand\":"<<unsigned(t.hand)<<",\"kind\":"<<unsigned(t.kind)<<",\"item\":";key(t.item);
    out<<",\"contact\":";key(t.contact);out<<",\"prerequisiteClaim\":"<<t.prerequisiteClaim<<",\"inputSequence\":"<<v.hold.inputSequence<<",\"deadlineNs\":"<<v.hold.deadlineNs<<'}';
    out<<",\"render\":{\"owner\":";nativeOwner(r.owner);out<<",\"knife\":";key(r.knife);out<<",\"bodyOperation\":"<<r.bodyOperation
        <<",\"rigFingerprint\":"<<v.rendered.rigFingerprint<<",\"inputEvidence\":";evidence(r.input);out<<'}';
    out<<",\"nativeSequence\":"<<v.nativeSequence<<",\"nativeObservedNs\":"<<v.nativeObservedNs<<",\"knifeSlot\":"<<v.knifeSlot
        <<",\"selectedSlots\":[7,7,7],\"nativeTick\":"<<v.nativeTick<<",\"suppressionRequest\":"<<v.suppressionRequest<<'}';
}
// Serialize one optional section and recheck its retained observation afterward.
// A concurrent gather, revocation or expiry removes only this section's proof;
// it never rejects an otherwise valid mechanism snapshot or waits for recovery.
template<class Read,class Clock>
std::string ObserveChestKnifeLiveJson(Read read,Clock clock,const ReloadStateOwner& owner,
    const interaction::HandInteractionSample& input,const interaction::InputFrame& controls) {
    const auto value=read(clock());const auto observed=clock();
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(9);
    WriteChestKnifeLiveObservation(out,value.get(),owner,input,controls,observed);
    if(!value||!ChestKnifeLiveJoinRejection(*value,owner,input,controls,observed).empty())return out.str();
    const auto latest=read(clock());const auto completed=clock();
    if(latest&&ChestKnifeLiveContinues(*value,*latest,completed)&&
       ChestKnifeLiveJoinRejection(*value,owner,input,controls,completed).empty())return out.str();
    std::ostringstream unavailable;
    WriteChestKnifeLiveObservation(unavailable,nullptr,owner,input,controls,completed);return unavailable.str();
}

// The held body proof owns its native/physical identities, original controls,
// render receipt and deadlines. A missing firearm aim/configuration does not
// replace any of those facts. This is diagnostic-only and grants no action.
// At most two passive reads; a gather/revocation/expiry still drops the proof.
template<class Read,class Clock>
std::string ObserveOwnedChestKnifeLiveJson(Read read,Clock clock) {
    const auto value=read(clock());const auto observed=clock();
    if(!value)return "{\"observationOnly\":true,\"available\":false,\"reason\":\"observation_unavailable\"}";
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(9);
    WriteChestKnifeLiveObservation(out,value.get(),value->owner,value->input,value->controls,observed);
    if(!ChestKnifeLiveJoinRejection(*value,value->owner,value->input,value->controls,observed).empty())return out.str();
    const auto latest=read(clock());const auto completed=clock();
    if(latest&&ChestKnifeLiveContinues(*value,*latest,completed)&&
       ChestKnifeLiveJoinRejection(*value,value->owner,value->input,value->controls,completed).empty())return out.str();
    std::ostringstream unavailable;
    WriteChestKnifeLiveObservation(unavailable,nullptr,value->owner,value->input,value->controls,completed);return unavailable.str();
}
}
