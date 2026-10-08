#include "Bc2BodyHolster.h"
#include "Bc2VisibilityDescriptorData.h"
namespace fvr::bc2 {
using namespace interaction;
namespace {
BodyInventoryOwner Owner(const ReloadStateOwner& o)noexcept{return {(std::uint64_t(o.weak)<<32)|o.soldier,o.actorGeneration,o.space};}
bool Fresh(const BodyHolsterSample& s)noexcept {
    const auto& i=s.hand;const auto& n=s.nativeOwner;const auto& b=s.body.inventory;
    return n.player>=0x10000&&n.soldier>=0x10000&&n.weak>=0x10000&&n.weapon>=0x10000&&n.equipGeneration&&
        b.owner==Owner(n)&&i.owner.actor==b.owner.actor&&i.owner.actorGeneration==b.owner.generation&&i.owner.space==b.owner.space&&
        i.owner.equipGeneration&&i.sequence&&i.observedNs>0&&i.observedNs<=i.nowNs&&i.deadlineNs>i.nowNs&&
        i.deadlineNs-i.observedNs<=150000000&&i.focused&&i.tracked[0]&&i.tracked[1]&&s.body.focused&&s.body.handTracked&&
        s.body.nowNs==i.nowNs&&s.nativeTick&&s.cache>=0x10000&&s.gun.id&&s.gun.generation==i.owner.equipGeneration&&
        s.selected&&s.selected->owner==n&&s.selected->sequence&&s.selected->observedNs>0&&s.selected->observedNs<=i.nowNs&&
        s.selected->deadlineNs>i.nowNs&&s.selected->deadlineNs-s.selected->observedNs<=200000000;
}
bool Ordinary(const BodyHolsterSample& s)noexcept {
    if(!s.ordinary)return false;const auto& v=*s.ordinary;
    return v.owner==s.nativeOwner&&v.generation&&v.generation<=s.hand.sequence&&v.observedNs>0&&v.observedNs<=s.hand.nowNs&&
        v.deadlineNs>s.hand.nowNs&&v.deadlineNs-v.observedNs<=150000000;
}
bool Receipt(const WeaponVisibilityReceipt& r,const WeaponVisibilityRequest& request,std::int64_t now)noexcept {
    return r.evidence&&r.verifiedCopyMask==3&&r.drawSerial&&r.request==request.request&&r.hidden==request.hide&&
        r.nativeOwner==request.nativeOwner&&r.rig==r.evidence->rig&&r.rig.soldier==request.nativeOwner.soldier&&r.rig.weak==request.nativeOwner.weak&&
        r.inputSequence==r.evidence->inputSequence&&r.physicalEquipGeneration==request.input.owner.equipGeneration&&
        r.physicalEquipGeneration==r.evidence->physicalEquipGeneration&&r.observedNs>=r.evidence->observedNs&&r.observedNs<=now&&
        r.observedNs<r.deadlineNs&&r.deadlineNs<=r.evidence->deadlineNs&&r.deadlineNs>now&&WeaponVisibilityCurrent(*r.evidence,request,now);
}
bool OrdinaryXm8CanShow(const BodyHolsterSample& s)noexcept {
    // Show-only retirement for the ordinary rifle selected from empty SPAS.
    // The renderer still independently validates exact paths, rig/palette and
    // both-eye copy receipts. This does not authorize hiding the XM8.
    if(!s.selected)return false;const auto& n=s.selected->weaponName;
    const auto end=std::find(n.begin(),n.end(),'\0');
    return end!=n.end()&&std::string_view(n.data(),std::size_t(end-n.begin()))=="XM8_sp_s"&&
        FindSelectedMesh(*s.selected,s.nativeOwner,SelectedMeshKind::Xm8,s.hand.nowNs)&&
        FindSelectedMesh(*s.selected,s.nativeOwner,SelectedMeshKind::Acog4x,s.hand.nowNs);
}
bool Gun(const HandClaim& r,const BodyHolsterSample& s)noexcept {
    return r.token.kind==HandClaimKind::GunHold&&r.token.hand==InteractionHand::Right&&r.token.owner==s.hand.owner&&
        r.token.item==s.gun&&r.deadlineNs>s.hand.nowNs;
}
}
bool Bc2BodyHolster::ProfileAccepted(const BodyHolsterSample& s)const noexcept {
    if(!s.selected||!DiagnosticFresh(s.hand.nowNs))return false;const auto& n=s.selected->weaponName;
    if(diagnosticDeadline_&&BodyHolsterConfiguredDiagnostic(diagnosticProfile_))
        return ResolveVisibilityDescriptor(*s.selected,s.nativeOwner,VisibilityDescriptors,s.hand.nowNs,true)!=nullptr;
    const auto end=std::find(n.begin(),n.end(),'\0');if(end==n.end())return false;
    const std::string_view name(n.data(),std::size_t(end-n.begin()));
    const auto mask=diagnosticDeadline_?BodyHolsterDiagnosticMask(diagnosticProfile_):capabilities_.acceptedProfileMask;
    return ((mask&1)&&name=="SPAS12_sp"&&FindSelectedMesh(*s.selected,s.nativeOwner,SelectedMeshKind::Spas12,s.hand.nowNs))||
        ((mask&2)&&name=="XM8_sp_s"&&FindSelectedMesh(*s.selected,s.nativeOwner,SelectedMeshKind::Xm8,s.hand.nowNs)&&
            FindSelectedMesh(*s.selected,s.nativeOwner,SelectedMeshKind::Acog4x,s.hand.nowNs))||
        ResolveVisibilityDescriptor(*s.selected,s.nativeOwner,VisibilityDescriptors,s.hand.nowNs,false)!=nullptr;
}
bool Bc2BodyHolster::AdmitDiagnostic(std::int64_t now,std::int64_t deadline,BodyHolsterDiagnosticProfile profile)noexcept {
    if(Enabled()||nextRender_||!ValidBodyHolsterDiagnosticProfile(profile)||now<=0||deadline<=now||deadline-now>15000000000ll)return false;
    diagnosticStart_=now;diagnosticDeadline_=deadline;diagnosticProfile_=profile;return true;
}
void Bc2BodyHolster::RememberEmpty(const BodyHolsterSample& s)noexcept {
    inputGapOwner_.reset();
    emptyCarried_=s.carried;
    emptyIntent_=emptyCarried_&&emptyCarried_->inventory>=0x10000&&emptyCarried_->switching>=0x10000&&
        emptyCarried_->count&&emptyCarried_->count<=emptyCarried_->items.size();
}
bool Bc2BodyHolster::ObserveNativeInputGap(const ReloadStateOwner& before,const ReloadStateOwner& after)noexcept {
    if(!emptyIntent_||pending_||!render_||!before.equipGeneration||before.equipGeneration==UINT64_MAX||
        (phase_!=BodyHolsterPhase::Empty&&phase_!=BodyHolsterPhase::Recovering&&!(phase_==BodyHolsterPhase::HidePending&&rebindEmpty_)))return false;
    const auto expected=inputGapOwner_.value_or(renderOwner_);
    auto next=before;++next.equipGeneration;
    if(before!=expected||after!=next)return false;
    inputGapOwner_=after;Invalidate(true);return true;
}
bool Bc2BodyHolster::CanRebindEmpty(const BodyHolsterSample& s)const noexcept {
    if(!emptyIntent_||pending_||!render_||!s.carried||s.carried!=emptyCarried_)return false;
    const bool changedSpace=s.nativeOwner.space!=renderOwner_.space&&s.hand.owner.space!=physicalOwner_.space;
    const bool resumeRebind=phase_==BodyHolsterPhase::Recovering;
    if(!changedSpace&&!resumeRebind)return false;
    const auto expectedNative=inputGapOwner_.value_or(renderOwner_);
    auto native=s.nativeOwner;native.space=expectedNative.space;
    auto physical=s.hand.owner;physical.space=physicalOwner_.space;
    if(native!=expectedNative||physical!=physicalOwner_)return false;
    // Native data/persistence/category/slot and container identity remain exact;
    // same-pointer replacement is not conflated with observation generations.
    return s.body.inventory.selected&&s.body.inventory.selected->id==renderOwner_.weapon;
}
bool Bc2BodyHolster::Begin(const BodyHolsterSample& s,bool hide)noexcept {
    if(nextRender_==UINT64_MAX)return false;render_=++nextRender_;renderOwner_=s.nativeOwner;physicalOwner_=s.hand.owner;revision_=s.body.inventory.revision;
    renderCarried_=s.carried;ordinaryRecovery_.reset();
    inputGapOwner_.reset();if(!hide)emptyIntent_=false;rebindEmpty_=false;phase_=hide?BodyHolsterPhase::HidePending:BodyHolsterPhase::ShowPending;return true;
}
bool Bc2BodyHolster::Replacement(const BodyHolsterSample& s)const noexcept {
    if(!render_||phase_==BodyHolsterPhase::Held||!renderCarried_||!s.carried||
       renderOwner_.player!=s.nativeOwner.player||renderOwner_.soldier!=s.nativeOwner.soldier||renderOwner_.weak!=s.nativeOwner.weak||
       renderOwner_.actorGeneration!=s.nativeOwner.actorGeneration||renderOwner_.space!=s.nativeOwner.space||
       s.nativeOwner.equipGeneration<=renderOwner_.equipGeneration||s.hand.owner.equipGeneration<=physicalOwner_.equipGeneration||
       !renderCarried_->count||renderCarried_->count>renderCarried_->items.size()||
       s.carried->inventory<0x10000||s.carried->switching<0x10000||!s.carried->count||s.carried->count>s.carried->items.size())return false;
    const auto old=std::find_if(renderCarried_->items.begin(),renderCarried_->items.begin()+renderCarried_->count,
        [&](const auto& item){return item.weapon==renderOwner_.weapon;});
    if(old==renderCarried_->items.begin()+renderCarried_->count||!BodyLongGunCategory(old->category))return false;
    const BodyNativeItem* current=nullptr;
    for(unsigned n=0;n<s.carried->count;++n){const auto& item=s.carried->items[n];
        if(item.slot!=n||item.category>20)return false;
        if(!item.weapon)continue;
        if(item.weapon<0x10000||item.data<0x10000)return false;
        for(unsigned k=0;k<n;++k)if(s.carried->items[k].weapon==item.weapon)return false;
        if(item.weapon==s.nativeOwner.weapon)current=&item;
    }
    if(!current||!BodyLongGunCategory(current->category)||current->data!=s.equipment.data||
       current->persistence!=s.equipment.persistence||current->weapon!=s.equipment.weapon)return false;
    // Native ground exchange can replace the configuration on the same object,
    // swap its object pointer, or rebuild related attachment/container entries.
    // Those changes revoke old slot/presentation authority; they must not make
    // ordinary rendering depend forever on the OLD inventory staying identical.
    // This is only an escape from a stale hide, never admission to hide a new
    // profile. Tick still requires the exact NEW ordinary stereo pair, current
    // suppression and released triggers before acquiring its NEW GunHold.
    return current->weapon!=old->weapon||*current!=*old;
}
bool Bc2BodyHolster::AcceptsProfile(const ReloadStateOwner& owner,std::shared_ptr<const SelectedMeshesSnapshot> selected,std::int64_t now)const noexcept {
    BodyHolsterSample s;s.nativeOwner=owner;s.selected=std::move(selected);s.hand.nowNs=now;return Enabled()&&ProfileAccepted(s);
}
std::optional<HolsterSuppressionRequest> Bc2BodyHolster::Demand(const BodyHolsterSample& s)const noexcept {
    if(!Enabled()||!DiagnosticFresh(s.hand.nowNs)||phase_==BodyHolsterPhase::Held||!render_||!s.nativeTick||s.cache<0x10000)return {};
    return HolsterSuppressionRequest{s.nativeOwner,s.hand,render_,s.nativeTick,s.cache};
}
BodyHolsterResult Bc2BodyHolster::Tick(BodyInventory& policy,const BodyHolsterSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    BodyHolsterResult out;
    if(!Enabled()){out.inventoryEvaluation=BodyInventoryEvaluation::Disabled;return out;} // Normal shoulder/chest path remains untouched.
    if(!DiagnosticFresh(s.hand.nowNs)){out.inventoryEvaluation=BodyInventoryEvaluation::DiagnosticExpired;Invalidate();out.phase=phase_;out.blockWeaponActions=BlocksActions();out.allowAutomaticGunHold=!BlocksActions();return out;}
    // An unsupported ordinary gun stays on the existing shoulder/cycling path.
    // Once hiding started, unsupported replacement must instead retire through
    // recovery; it cannot inherit an accepted profile or an old show receipt.
    if(phase_==BodyHolsterPhase::Held&&Fresh(s)&&!ProfileAccepted(s)){out.inventoryEvaluation=BodyInventoryEvaluation::UnsupportedProfile;return out;}
    auto body=s.body;body.acknowledgement.reset();body.inventory.presentation=BodyPresentation::Unknown;body.inventory.fireSuppressed=false;
    const auto finish=[&](){out.phase=phase_;out.blockWeaponActions=phase_!=BodyHolsterPhase::Held||out.inventory.blockFire;
        out.allowAutomaticGunHold=phase_==BodyHolsterPhase::Held&&Fresh(s)&&!s.cancel;Record(s,out,hands);return out;};
    const auto recover=[&](bool trackingGap=false){if(!trackingGap){emptyIntent_=false;rebindEmpty_=false;inputGapOwner_.reset();}pending_.reset();phase_=BodyHolsterPhase::Recovering;body.intent={};};
    const bool accepted=ProfileAccepted(s);
    const bool showOnly=phase_!=BodyHolsterPhase::Held&&!accepted&&OrdinaryXm8CanShow(s);
    const bool replacement=!accepted&&!showOnly&&Fresh(s)&&Replacement(s);
    if(!accepted&&!showOnly&&Fresh(s)&&phase_!=BodyHolsterPhase::Held)ObserveReplacement(s,replacement);
    if(replacement&&!s.cancel&&!s.reloadBusy){
        // The old private hide is already revoked. Keep safety suppression on
        // the NEW exact native owner until its ordinary palette is observed.
        // No unsupported hide/show binding or synthetic Draw acknowledgement.
        OrdinaryEquipmentRequest candidate{render_,s.nativeOwner,s.equipment,s.hand,s.gun};
        if(!ordinaryRecovery_||!SameOrdinaryEquipment(*ordinaryRecovery_,candidate)||ordinaryRecoveryCarried_!=s.carried){
            if(nextRender_==UINT64_MAX){recover();body.focused=false;out.inventory=policy.Update(body);out.inventoryEvaluation=BodyInventoryEvaluation::Evaluated;return finish();}
            render_=++nextRender_;candidate.request=render_;
            ordinaryRecovery_=candidate;ordinaryRecoveryCarried_=s.carried;pending_.reset();emptyIntent_=rebindEmpty_=false;inputGapOwner_.reset();
        }
        phase_=BodyHolsterPhase::Recovering;body.intent={};
        out.ordinaryRecovery=candidate;
        const auto demand=Demand(s);
        const bool proven=OrdinaryEquipmentFresh(candidate,s.hand.nowNs)&&s.ordinaryPair&&
            OrdinaryEquipmentPairCurrent(*s.ordinaryPair,candidate,s.hand.nowNs)&&Ordinary(s)&&
            demand&&s.suppression&&HolsterSuppressionCurrent(*s.suppression,*demand)&&s.triggerNeutral&&
            s.body.inventory.selected&&s.body.inventory.selected->id==s.nativeOwner.weapon;
        if(!proven)body.focused=false;
        else {body.inventory.presentation=BodyPresentation::WeaponVisible;body.inventory.fireSuppressed=true;}
        out.inventory=policy.Update(body);out.inventoryEvaluation=BodyInventoryEvaluation::Evaluated;
        if(proven&&!out.inventory.blockFire){
            hands.Update(s.hand);const auto right=hands.Current(InteractionHand::Right);
            bool acquired=right&&Gun(*right,s);
            if(!right&&!hands.Current(InteractionHand::Left)&&intent<UINT64_MAX)
                acquired=hands.Acquire(s.hand,{s.hand.owner,InteractionHand::Right,HandClaimKind::GunHold,s.gun,
                    {{1,s.hand.owner.equipGeneration},s.hand.sequence,s.hand.deadlineNs,true},++intent,0}).accepted;
            if(acquired&&!hands.Current(InteractionHand::Left)){
                phase_=BodyHolsterPhase::Held;ordinaryRecovery_.reset();out.ordinaryRecovery.reset();out.ordinaryRetired=true;
                renderCarried_=s.carried;renderOwner_=s.nativeOwner;physicalOwner_=s.hand.owner;revision_=s.body.inventory.revision;
                ++ordinaryRetirements_;
            }
        }
        return finish();
    }
    ordinaryRecovery_.reset();
    if(!Fresh(s)||(!accepted&&!showOnly)||s.cancel||s.reloadBusy){
        if(phase_!=BodyHolsterPhase::Held)recover(!Fresh(s)&&!s.cancel&&!s.reloadBusy);body.focused=false;out.inventory=policy.Update(body);out.inventoryEvaluation=BodyInventoryEvaluation::Evaluated;return finish();
    }
    if(showOnly){
        body.intent={}; // Never admit a new hide/draw gesture on this capability.
        if(phase_!=BodyHolsterPhase::ShowPending&&phase_!=BodyHolsterPhase::Recovering)recover();
    }
    hands.Update(s.hand);
    if(accepted&&CanRebindEmpty(s)){
        // Recenter is not a draw gesture. Stop first, then obtain a NEW paired
        // hide receipt in the new space. No old free-hand proof is republished.
        if(!Begin(s,true)){recover();out.inventory=policy.Update(body);out.inventoryEvaluation=BodyInventoryEvaluation::Evaluated;return finish();}
        rebindEmpty_=true;body.intent={};
    }else if(phase_!=BodyHolsterPhase::Held&&revision_!=s.body.inventory.revision)recover();
    const bool changed=render_&&(renderOwner_!=s.nativeOwner||physicalOwner_!=s.hand.owner);
    const bool requestedSelection=phase_==BodyHolsterPhase::ShowPending&&pending_&&pending_->operation==BodyInventoryOperation::Draw&&
        s.body.inventory.owner==pending_->owner&&s.body.inventory.selected==std::optional(pending_->item)&&pending_->item.id==s.nativeOwner.weapon;
    if(changed&&phase_!=BodyHolsterPhase::Held){
        if(!requestedSelection)recover();
        if(!Begin(s,false)){recover();out.inventory=policy.Update(body);out.inventoryEvaluation=BodyInventoryEvaluation::Evaluated;return finish();}
    }else if(phase_==BodyHolsterPhase::Recovering){
        if(!Begin(s,false)){out.inventory=policy.Update(body);out.inventoryEvaluation=BodyInventoryEvaluation::Evaluated;return finish();}
    }
    // No current selected-item alias guess. Launcher/base family mapping must
    // be explicitly added using the existing verified family resolver later.
    const bool selected=s.body.inventory.selected&&s.body.inventory.selected->id==s.nativeOwner.weapon;
    bool suppressed=false,visible=false;
    if(phase_!=BodyHolsterPhase::Held){
        const auto demand=Demand(s);suppressed=demand&&s.suppression&&HolsterSuppressionCurrent(*s.suppression,*demand);
        const bool hide=phase_==BodyHolsterPhase::HidePending||phase_==BodyHolsterPhase::Empty;
        // Never begin/renew hiding without this gather's committed suppression.
        if(!hide||suppressed)out.visibility={true,hide,render_,s.nativeOwner,s.hand,s.selected,diagnosticDeadline_};
        visible=s.visibility&&Receipt(*s.visibility,out.visibility,s.hand.nowNs);
        if(phase_==BodyHolsterPhase::Empty&&(!suppressed||!visible)){
            // Missing current presentation is lost authority, not a Draw. Keep
            // only an already committed intent with exact current identity.
            // The new request cannot use this gather's previous suppression or
            // the old paired receipt; no free-hand evidence is emitted here.
            ++emptyReceiptGaps_;recover(true);
            if(CanRebindEmpty(s)&&Begin(s,true)){
                ++emptyReceiptRehides_;rebindEmpty_=true;out.visibility={};
            }else{
                ++emptyReceiptRejected_;recover();Begin(s,false);
                out.visibility={true,false,render_,s.nativeOwner,s.hand,s.selected,diagnosticDeadline_};
            }
            suppressed=false;visible=false;
        }
    }
    if(phase_==BodyHolsterPhase::Held){
        if(selected&&Ordinary(s))body.inventory.presentation=BodyPresentation::WeaponVisible;
    }else if(phase_==BodyHolsterPhase::HidePending&&pending_&&suppressed&&visible&&selected){
        const auto right=hands.Current(InteractionHand::Right);
        if(!hands.Current(InteractionHand::Left)&&right&&Gun(*right,s)&&hands.Release(s.hand,right->token).accepted){
            body.inventory.presentation=BodyPresentation::EmptyHands;body.inventory.fireSuppressed=true;
            body.acknowledgement=BodyInventoryAcknowledgement{pending_->id,pending_->revision,pending_->owner,pending_->operation,pending_->item,true};
        }
    }else if(phase_==BodyHolsterPhase::HidePending&&rebindEmpty_&&suppressed&&visible&&selected&&
        !hands.Current(InteractionHand::Left)&&!hands.Current(InteractionHand::Right)){
        body.inventory.presentation=BodyPresentation::EmptyHands;body.inventory.fireSuppressed=true;
        phase_=BodyHolsterPhase::Empty;rebindEmpty_=false;
        // Policy adopts current presentation. There is no new user request to
        // acknowledge and no claim to release or reacquire across the rebase.
    }else if(phase_==BodyHolsterPhase::Empty&&suppressed&&visible&&selected){
        body.inventory.presentation=BodyPresentation::EmptyHands;body.inventory.fireSuppressed=true;
    }else if(phase_==BodyHolsterPhase::ShowPending&&suppressed&&visible&&selected&&
        (!pending_||pending_->item==*s.body.inventory.selected)){
        // New native equip has a new physical hand owner; reacquire only AFTER
        // the fresh show receipt and through the real shared arbiter.
        auto right=hands.Current(InteractionHand::Right);bool acquired=right&&Gun(*right,s);
        if(!right&&!hands.Current(InteractionHand::Left)&&intent<UINT64_MAX){
            acquired=hands.Acquire(s.hand,{s.hand.owner,InteractionHand::Right,HandClaimKind::GunHold,s.gun,
                {{1,s.hand.owner.equipGeneration},s.hand.sequence,s.hand.deadlineNs,true},++intent,0}).accepted;
        }
        if(acquired){body.inventory.presentation=BodyPresentation::WeaponVisible;
            if(pending_)body.acknowledgement=BodyInventoryAcknowledgement{pending_->id,pending_->revision,pending_->owner,pending_->operation,pending_->item,true};
            else phase_=BodyHolsterPhase::Held;}
    }
    // Refuse a new gesture while another subsystem owns the left hand, and do
    // not steal a non-GunHold right token (including ammo/other body gestures).
    if(body.intent.operation!=BodyInventoryOperation::None){const auto right=hands.Current(InteractionHand::Right);
        if(s.reloadBusy||hands.Current(InteractionHand::Left)||
            (body.intent.operation==BodyInventoryOperation::Holster&&(!right||!Gun(*right,s)))||
            (body.intent.operation==BodyInventoryOperation::Draw&&right))body.intent={};
    }
    out.inventory=policy.Update(body);out.inventoryEvaluation=BodyInventoryEvaluation::Evaluated;
    if(out.inventory.cancelledRequest){recover();if(Begin(s,false))out.visibility={true,false,render_,s.nativeOwner,s.hand,s.selected,diagnosticDeadline_};}
    if(out.inventory.committedRequest&&pending_){
        phase_=pending_->operation==BodyInventoryOperation::Holster?BodyHolsterPhase::Empty:BodyHolsterPhase::Held;
        if(phase_==BodyHolsterPhase::Empty)RememberEmpty(s);pending_.reset();
    }
    if(out.inventory.request){pending_=out.inventory.request;
        if(!Begin(s,pending_->operation==BodyInventoryOperation::Holster)){recover();}
        else if(pending_->operation==BodyInventoryOperation::Draw)out.select=pending_->item;
        // First suppression is on the following native gather. Do not emit a
        // hide request early using an unrelated/previous transaction receipt.
        out.visibility={};
    }
    if(phase_==BodyHolsterPhase::Empty&&suppressed&&visible&&!hands.Current(InteractionHand::Right))
        out.freeRight=BodyFreeRightEvidence{out.visibility,*s.visibility,*s.suppression,s.hand,diagnosticDeadline_};
    if(phase_==BodyHolsterPhase::Held)out.visibility={};
    return finish();
}
void Bc2BodyHolster::ObserveReplacement(const BodyHolsterSample& s,bool eligible)noexcept {
    if(!renderCarried_||!s.carried||s.nativeOwner.equipGeneration<=renderOwner_.equipGeneration)return;
    if(exchangeCount_){const auto& previous=exchanges_[exchangeCount_-1];
        if(previous.before==renderOwner_&&previous.after==s.nativeOwner&&previous.oldCarried==*renderCarried_&&
           previous.currentCarried==*s.carried&&previous.eligible==eligible)return;}
    if(exchangeCount_==exchanges_.size()){++exchangeDropped_;return;}
    exchanges_[exchangeCount_++]={renderOwner_,s.nativeOwner,*renderCarried_,*s.carried,s.hand.sequence,eligible};
}
void Bc2BodyHolster::Record(const BodyHolsterSample& s,const BodyHolsterResult& out,const HandInteraction& hands)noexcept {
    const unsigned gates=(Fresh(s)?1u:0u)|(ProfileAccepted(s)?2u:0u)|(s.cancel?4u:0u)|
        (s.reloadBusy?8u:0u)|(Ordinary(s)?16u:0u)|(bool(out.ordinaryRecovery)?32u:0u);
    if(transitionCount_){const auto& previous=transitions_[(transitionNext_+transitions_.size()-1)%transitions_.size()];
        if(previous.phase==out.phase&&previous.owner==s.nativeOwner&&previous.block==out.blockWeaponActions&&
            previous.gates==gates&&previous.physicalItem==s.gun.id&&previous.physicalEquip==s.hand.owner.equipGeneration&&
            !out.inventory.committedRequest)return;}
    auto& row=transitions_[transitionNext_];row={};transitionNext_=(transitionNext_+1)%unsigned(transitions_.size());
    if(transitionCount_<transitions_.size())++transitionCount_;else ++transitionDropped_;
    row.phase=out.phase;row.owner=s.nativeOwner;row.gates=gates;row.physicalItem=s.gun.id;row.physicalEquip=s.hand.owner.equipGeneration;
    row.input=s.hand.sequence;row.request=render_;row.commit=out.inventory.committedRequest;
    row.observed=s.hand.observedNs;row.deadline=s.hand.deadlineNs;row.now=s.hand.nowNs;
    row.block=out.blockWeaponActions;row.free=bool(out.freeRight);
    if(const auto right=hands.Current(InteractionHand::Right);right&&Gun(*right,s))row.right=right->token.id;
    if(s.visibility){const auto& v=*s.visibility;row.receiptRequest=v.request;row.receiptInput=v.inputSequence;row.draw=v.drawSerial;row.copyMask=v.verifiedCopyMask;row.hidden=v.hidden;
        const WeaponVisibilityRequest request{true,v.hidden,render_,s.nativeOwner,s.hand,s.selected,diagnosticDeadline_};
        row.receiptCurrent=Fresh(s)&&Receipt(v,request,s.hand.nowNs);}
    const HolsterSuppressionRequest demand{s.nativeOwner,s.hand,render_,s.nativeTick,s.cache};
    row.suppressionCurrent=s.suppression&&HolsterSuppressionCurrent(*s.suppression,demand);
}
void Bc2BodyHolster::Report(std::ostream& out)const {
    const auto carried=[&](const BodyCarriedIdentity& c){
        out<<"{\"inventory\":"<<c.inventory<<",\"switching\":"<<c.switching<<",\"count\":"<<c.count<<",\"items\":[";
        for(unsigned n=0;n<std::min<unsigned>(c.count,unsigned(c.items.size()));++n){if(n)out<<',';const auto& item=c.items[n];
            out<<"{\"slot\":"<<item.slot<<",\"weapon\":"<<item.weapon<<",\"data\":"<<item.data
               <<",\"persistence\":"<<item.persistence<<",\"category\":"<<item.category<<'}';}out<<"]}";
    };
    out<<"{\"final_phase\":"<<unsigned(phase_)<<",\"blocks_actions\":"<<(BlocksActions()?"true":"false")
       <<",\"empty_receipt_gaps\":"<<emptyReceiptGaps_<<",\"empty_receipt_rehides\":"<<emptyReceiptRehides_<<",\"empty_receipt_rejected\":"<<emptyReceiptRejected_
       <<",\"ordinary_replacements_retired\":"<<ordinaryRetirements_
       <<",\"exchanges_dropped\":"<<exchangeDropped_<<",\"exchanges\":[";
    for(unsigned n=0;n<exchangeCount_;++n){if(n)out<<',';const auto& e=exchanges_[n];
        out<<"{\"input\":"<<e.input<<",\"eligible\":"<<(e.eligible?"true":"false")
           <<",\"before_weapon\":"<<e.before.weapon<<",\"after_weapon\":"<<e.after.weapon
           <<",\"before_equip\":"<<e.before.equipGeneration<<",\"after_equip\":"<<e.after.equipGeneration<<",\"old\":";
        carried(e.oldCarried);out<<",\"current\":";carried(e.currentCarried);out<<'}';}
    out<<"],\"dropped\":"<<transitionDropped_<<",\"rows\":[";
    for(unsigned n=0;n<transitionCount_;++n){if(n)out<<',';const auto& e=transitions_[(transitionCount_==transitions_.size()?transitionNext_+n:n)%transitions_.size()];
        out<<"{\"phase\":"<<unsigned(e.phase)<<",\"weapon\":"<<e.owner.weapon<<",\"native_equip\":"<<e.owner.equipGeneration
           <<",\"actor\":"<<e.owner.actorGeneration<<",\"space\":"<<e.owner.space<<",\"input\":"<<e.input<<",\"request\":"<<e.request
           <<",\"gate_flags\":"<<e.gates<<",\"physical_item\":"<<e.physicalItem<<",\"physical_equip\":"<<e.physicalEquip
           <<",\"commit\":"<<e.commit<<",\"right_claim\":"<<e.right<<",\"observed_ns\":"<<e.observed<<",\"deadline_ns\":"<<e.deadline<<",\"now_ns\":"<<e.now
           <<",\"blocked\":"<<(e.block?"true":"false")<<",\"free_right\":"<<(e.free?"true":"false")
           <<",\"receipt_request\":"<<e.receiptRequest<<",\"receipt_input\":"<<e.receiptInput<<",\"draw_serial\":"<<e.draw<<",\"copy_mask\":"<<e.copyMask
           <<",\"hidden\":"<<(e.hidden?"true":"false")<<",\"receipt_current\":"<<(e.receiptCurrent?"true":"false")
           <<",\"suppression_current\":"<<(e.suppressionCurrent?"true":"false")<<'}';}
    out<<"]}";
}
bool BodyFreeRightCurrent(const BodyFreeRightEvidence& p,const BodyHolsterSample& s,const HandInteraction& hands)noexcept {
    if(!BodyFreeRightEvidenceCurrent(p,s.hand.nowNs)||!Fresh(s)||hands.Current(InteractionHand::Right)||p.input.owner!=s.hand.owner||p.input.sequence!=s.hand.sequence||
        p.input.observedNs!=s.hand.observedNs||p.input.deadlineNs!=s.hand.deadlineNs||!p.visibility.hide)return false;
    const WeaponVisibilityRequest current{true,true,p.visibility.request,s.nativeOwner,s.hand,s.selected,p.authorizationDeadlineNs};
    const HolsterSuppressionRequest demand{s.nativeOwner,s.hand,p.visibility.request,s.nativeTick,s.cache};
    return Receipt(p.receipt,current,s.hand.nowNs)&&HolsterSuppressionCurrent(p.suppression,demand);
}
bool BodyFreeRightEvidenceCurrent(const BodyFreeRightEvidence& p,std::int64_t now)noexcept {
    if(p.authorizationDeadlineNs&&(p.authorizationDeadlineNs<=p.input.observedNs||now>=p.authorizationDeadlineNs))return false;
    if(!p.visibility.enabled||!p.visibility.hide||p.input.owner!=p.visibility.input.owner||p.input.sequence!=p.visibility.input.sequence||
        p.input.observedNs!=p.visibility.input.observedNs||p.input.deadlineNs!=p.visibility.input.deadlineNs)return false;
    auto demand=static_cast<const HolsterSuppressionRequest&>(p.suppression);demand.input=p.input;demand.input.nowNs=now;
    if(demand.request!=p.visibility.request||demand.owner!=p.visibility.nativeOwner)return false;
    auto current=p.visibility;current.input.nowNs=now;
    return Receipt(p.receipt,current,now)&&HolsterSuppressionCurrent(p.suppression,demand);
}
std::optional<math::Matrix4> HolsterRightTarget(const BodyFreeRightEvidence& p,std::int64_t now,const Bc2HandBinding& binding,
    const math::Matrix4& rawGrip,const math::Matrix4& calibratedWrist)noexcept {
    if(!binding.rightHand||!BodyFreeRightEvidenceCurrent(p,now)||!InverseAnimatedTransform(rawGrip)||!InverseAnimatedTransform(calibratedWrist))return {};
    auto wrist=Multiply(binding.wristToGrip,rawGrip);wrist.values[3]=calibratedWrist.values[3];return wrist;
}
bool BodyFreeRightRenderCurrent(const BodyFreeRightEvidence& source,const BodyFreeRightEvidence* current,
    const WeaponVisibilityRequest* visibility,std::int64_t ns)noexcept {
    return current&&visibility&&visibility->enabled&&visibility->hide&&source.receipt.evidence&&
        WeaponVisibilityCurrent(*source.receipt.evidence,*visibility,ns)&&
        BodyFreeRightEvidenceCurrent(source,ns)&&BodyFreeRightEvidenceCurrent(*current,ns)&&
        current->visibility.request==source.visibility.request&&current->visibility.nativeOwner==source.visibility.nativeOwner&&
        current->input.owner==source.input.owner&&current->input.sequence>=source.input.sequence&&
        current->input.observedNs>=source.input.observedNs&&current->suppression.nativeTick>=source.suppression.nativeTick;
}
std::optional<HandPose> PoseHolsteredRight(const BodyFreeRightEvidence& p,std::int64_t now,const Bc2HandBinding& binding,
    std::span<const std::int32_t> parents,std::span<const math::Matrix4> world,const math::Matrix4& wrist,const ControllerState& controller) {
    if(!binding.rightHand||!BodyFreeRightEvidenceCurrent(p,now)||!InverseAnimatedTransform(wrist))return {};
    return GenerateHandPose(parents,binding.referenceWorld,world,binding.pose,wrist,
        ApplyHandTouch(RightHandTargets(HandPoseRole::Free,controller.squeeze,controller.trigger),controller));
}
std::optional<HandPose> PoseHolsteredRight(const BodyFreeRightEvidence& proof,const BodyHolsterSample& s,const HandInteraction& hands,
    const Bc2HandBinding& binding,std::span<const std::int32_t> parents,std::span<const math::Matrix4> world,
    const math::Matrix4& rawGrip,const math::Matrix4& calibratedWrist,const ControllerState& controller) {
    if(!binding.rightHand||!BodyFreeRightCurrent(proof,s,hands)||!InverseAnimatedTransform(rawGrip)||!InverseAnimatedTransform(calibratedWrist))return {};
    auto wrist=Multiply(binding.wristToGrip,rawGrip);wrist.values[3]=calibratedWrist.values[3];
    return GenerateHandPose(parents,binding.referenceWorld,world,binding.pose,wrist,
        ApplyHandTouch(RightHandTargets(HandPoseRole::Free,controller.squeeze,controller.trigger),controller));
}
}
