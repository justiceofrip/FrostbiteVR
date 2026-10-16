#include "Bc2ResourceInventoryProbe.h"
#include <ostream>
#include <sstream>
#include <algorithm>
#include <cmath>
namespace fvr::bc2 {
using namespace interaction;
namespace {
math::Pose Approach(const math::Pose& from,const math::Pose& target)noexcept {
    auto out=target;const auto d=std::hypot(target.position.x-from.position.x,target.position.y-from.position.y,target.position.z-from.position.z);
    const float t=d>.015f?.015f/d:1;
    out.position={from.position.x+(target.position.x-from.position.x)*t,from.position.y+(target.position.y-from.position.y)*t,from.position.z+(target.position.z-from.position.z)*t};
    auto q=target.orientation;const auto a=from.orientation;float dot=a.x*q.x+a.y*q.y+a.z*q.z+a.w*q.w;
    if(dot<0){q={-q.x,-q.y,-q.z,-q.w};dot=-dot;}
    const float angle=2*std::acos(std::clamp(dot,0.f,1.f)),u=angle>.06f?.06f/angle:1;
    q={a.x+(q.x-a.x)*u,a.y+(q.y-a.y)*u,a.z+(q.z-a.z)*u,a.w+(q.w-a.w)*u};
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);out.orientation={q.x/n,q.y/n,q.z/n,q.w/n};return out;
}
}
bool Bc2ResourceInventoryProbe::EnablePump(const Bc2PumpCalibration& calibration,Bc2PumpControllerProbe::Scenario scenario,unsigned cycles)noexcept {
    if(first_||pump_||magazineOnly_||supportOnly_||phase_!=Phase::Setup||!PumpCalibrationValid(calibration))return false;
    if(unsigned(scenario)>unsigned(Bc2PumpControllerProbe::Scenario::HeldSupportNaturalReceiverGap))return false;
    if((cycles!=1&&cycles!=3&&cycles!=8)||(cycles!=1&&scenario!=Bc2PumpControllerProbe::Scenario::None&&
       scenario!=Bc2PumpControllerProbe::Scenario::HeldSupportNatural&&scenario!=Bc2PumpControllerProbe::Scenario::HeldSupportNaturalReceiverGap))return false;
    if(cycles==8&&!fullReturn_.UseCombinedWarmup())return false;
    calibration_=calibration;pumpScenario_=scenario;pumpCycles_=cycles;pump_.emplace(calibration,cycles,scenario);
    return true;
}
bool Bc2ResourceInventoryProbe::EnableTwoShells()noexcept {
    if(first_||!pump_||pumpCycles_!=3||shellRounds_!=1||replacementEnabled_||revisitEnabled_||
       (pumpScenario_!=Bc2PumpControllerProbe::Scenario::HeldSupportNatural&&pumpScenario_!=Bc2PumpControllerProbe::Scenario::HeldSupportNaturalReceiverGap))return false;
    shell_=Bc2PhysicalReloadProbe{true,2};shellRounds_=2;return true;
}
bool Bc2ResourceInventoryProbe::EnableShellFocusLoss()noexcept {
    if(first_||!pump_||pumpCycles_!=3||shellRounds_!=2||shellFocus_||replacementEnabled_||revisitEnabled_||
       pumpUnreachablePose_||pumpInspectionPose_||pumpScenario_!=Bc2PumpControllerProbe::Scenario::HeldSupportNatural)return false;
    shellFocus_.emplace();rifleSupportRequired_=false;return true;
}
void Bc2ResourceInventoryProbe::ObserveShellFocusLoss(const InputFrame& in,const ReloadStateOwner& owner,
    std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    if(!shellFocus_||phase_!=Phase::ShellFocusLoss)return;
    bool neutral=true;for(const auto& h:in.hands)neutral=neutral&&!h.held&&!h.touched&&!h.touchActive&&
        h.trigger==0&&h.squeeze==0&&h.stickX==0&&h.stickY==0;
    if(owner!=shellOwner_||!ValidInput(in)||in.focused||!in.headValid||
       !shellFocus_->Source(in.generation,in.spaceGeneration,observed,deadline,now,false,neutral))Fail(37,now);
}
bool Bc2ResourceInventoryProbe::EnableUnreachablePumpPose()noexcept {
    if(first_||!pump_||pumpCycles_!=3||shellRounds_!=2||pumpUnreachablePose_||replacementEnabled_||
       pumpInspectionPose_||revisitEnabled_||phase_!=Phase::Setup||
       pumpScenario_!=Bc2PumpControllerProbe::Scenario::HeldSupportNatural)return false;
    pumpUnreachablePose_=true;return true;
}
bool Bc2ResourceInventoryProbe::EnableActorReplacement(bool magazine)noexcept {
    if(first_||!pump_||pumpCycles_!=1||replacementEnabled_||pumpInspectionPose_||revisitEnabled_||phase_!=Phase::Setup)return false;
    replacementEnabled_=true;replacementMagazine_=magazine;
    if(magazine){firstMagazine_=Bc2MagazineResourceProbe{true,true,false,true};
        firstMagazine_.UseCombinedWarmup();rifleSupportRequired_=false;}
    return true;
}
bool Bc2ResourceInventoryProbe::UsePumpInspectionPose()noexcept {
    if(first_||!pump_||pumpInspectionPose_||phase_!=Phase::Setup||replacementEnabled_||revisitEnabled_||pumpCycles_!=1)return false;
    pumpInspectionPose_=true;return true;
}
bool Bc2ResourceInventoryProbe::EnablePumpRevisit()noexcept {
    if(first_||!pump_||pumpCycles_!=1||revisitEnabled_||replacementEnabled_||pumpInspectionPose_||
       pumpScenario_!=Bc2PumpControllerProbe::Scenario::None||phase_!=Phase::Setup)return false;
    revisitEnabled_=true;return true;
}
const ReloadStateOwner* Bc2ResourceInventoryProbe::ReplacementSourceOwner()const noexcept {
    if(replacementMagazineWitness_)return &replacementMagazineWitness_->identity.owner;
    if(replacementWitness_)return &replacementWitness_->native.owner;
    return nullptr;
}
bool Bc2ResourceInventoryProbe::WitnessMagazineReplacement(const ResourceInventoryObservation& o,std::int64_t now)noexcept {
    if(!replacementEnabled_||!replacementMagazine_||replacementResumed_||phase_!=Phase::FirstMagazine||
       !lastInput_||now<lastNow_||!weapon_cycle_detail::Window(observed_,deadline_,now))return false;
    if(replacementMagazineWitness_){
        const auto& old=*replacementMagazineWitness_;
        if(o.magazine.resource&&o.magazine.resource->identity==old.identity&&AmmoResourceViewFresh(*o.magazine.resource,now)){
            const auto& current=*o.magazine.resource;
            if(!current.wellEmpty||current.original!=old.original||current.snapshot.counts!=old.snapshot.counts||
               current.request!=old.request)Fail(27,now);
        }
        return true;
    }
    const auto witness=firstMagazine_.DetachedOriginal(o.magazine,now);
    if(!witness||witness->identity.owner!=initial_||witness->binding.context.resource!=original_||
       lastInput_->hands[0].squeeze<=.75f||lastInput_->hands[1].squeeze<=.75f)return false;
    const auto& n=witness->identity;
    if(n.serverPlayer<0x10000||n.serverSoldier<0x10000||n.serverItem<0x10000)return false;
    for(unsigned a=0;a<3;++a){if(n.firing[a]<0x10000)return false;
        for(unsigned b=0;b<a;++b)if(n.firing[a]==n.firing[b])return false;}
    replacementMagazineWitness_=witness;replacementWitnessAt_=now;replacementInput_=lastInput_->generation;
    replacementPendingEquip_=initial_.equipGeneration;
    replacementInputObserved_=observed_;replacementInputDeadline_=deadline_;return true;
}
bool Bc2ResourceInventoryProbe::WitnessReplacement(const std::optional<Bc2NativeCycleView>& view,std::int64_t now)noexcept {
    if(!replacementEnabled_||replacementMagazine_||replacementResumed_||phase_!=Phase::Pump)return false;
    if(replacementWitness_){
        if(view&&view->native==replacementWitness_->native&&
           (view->ready||view->phase==Bc2NativeCyclePhase::Complete||!view->blocksFire||
            view->cycle!=replacementWitness_->cycle||view->shot!=replacementWitness_->shot))Fail(24,now);
        return true;
    }
    if(!view||!lastInput_||now<lastNow_||!weapon_cycle_detail::Window(observed_,deadline_,now)||
       view->native.owner!=shellOwner_||!view->cycle||!view->shot||!view->blocksFire||view->ready||
       (view->phase!=Bc2NativeCyclePhase::ShotObserved&&view->phase!=Bc2NativeCyclePhase::Arming&&view->phase!=Bc2NativeCyclePhase::Held)||
       view->loaded!=shellLoaded_-1||view->reserve!=shellReserve_||view->capacity!=8)return false;
    const auto& n=view->native;
    if(n.serverPlayer<0x10000||n.serverSoldier<0x10000||n.serverItem<0x10000)return false;
    for(unsigned a=0;a<3;++a){if(n.firing[a]<0x10000)return false;
        for(unsigned b=0;b<a;++b)if(n.firing[a]==n.firing[b])return false;}
    replacementWitness_=view;replacementWitnessAt_=now;replacementInput_=lastInput_->generation;
    replacementPendingEquip_=shellOwner_.equipGeneration;
    replacementInputObserved_=observed_;replacementInputDeadline_=deadline_;return true;
}
bool Bc2ResourceInventoryProbe::ResumeReplacement(InputFrame& in,const ReloadStateOwner& owner,
    std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    const auto old=ReplacementSourceOwner();
    if(!replacementEnabled_||replacementResumed_||!old||
       phase_!=(replacementMagazine_?Phase::FirstMagazine:Phase::Pump)||CancelConsumer()||
       !NativeCycleClientActorReplaced(*old,owner)||
       owner.player<0x10000||owner.weapon<0x10000||!owner.equipGeneration||!owner.space||
       owner.space!=in.spaceGeneration||!lastInput_||in.generation<=lastInput_->generation||
       observed<observed_||now<=replacementWitnessAt_)return false;
    std::ostringstream prior;Report(prior);
    Bc2ResourceInventoryProbe resumed;
    resumed.EnablePump(calibration_,pumpScenario_);
    resumed.firstMagazine_=Bc2MagazineResourceProbe{true,true,false,true};
    resumed.replacementEnabled_=true;resumed.replacementResumed_=true;
    resumed.replacementMagazine_=replacementMagazine_;resumed.replacementMagazineWitness_=replacementMagazineWitness_;
    resumed.replacementWitness_=replacementWitness_;resumed.replacementWitnessAt_=replacementWitnessAt_;
    resumed.replacementInput_=replacementInput_;resumed.replacementInputObserved_=replacementInputObserved_;
    resumed.replacementPendingEquip_=replacementPendingEquip_;
    resumed.replacementInputDeadline_=replacementInputDeadline_;resumed.replacementAt_=now;resumed.replacementOwner_=owner;
    resumed.replacementPriorReport_=prior.str();
    // Only these input scripts restart. The overall deadline and input clock do
    // not restart, and this object cannot call/reset any gameplay consumer.
    resumed.first_=first_;resumed.lastNow_=now;resumed.phaseAt_=now;
    resumed.initial_=owner;resumed.observed_=observed;resumed.deadline_=deadline;
    for(auto& hand:in.hands){hand.squeeze=0;hand.trigger=0;}
    resumed.RecordInput(in,owner,observed,deadline,now);*this=std::move(resumed);return true;
}
void Bc2ResourceInventoryProbe::Move(Phase p,std::int64_t now)noexcept {
    phase_=p;phaseAt_=now;if(transitionCount_<transitions_.size())transitions_[transitionCount_++]={unsigned(p),failure_,now};
}
void Bc2ResourceInventoryProbe::Fail(unsigned reason,std::int64_t now)noexcept {
    if(Completed()||CancelConsumer())return;failure_=reason;Move(Phase::Failed,now);
}
bool Bc2ResourceInventoryProbe::Reserve(const ResourceInventoryObservation& o,const ReloadStateOwner& owner,std::int64_t now)const noexcept {
    return o.reserve&&o.reserve->verified&&o.reserve->sequence&&o.reserve->identity.owner==owner&&o.reserve->observedNs>0&&
        o.reserve->observedNs<=now&&o.reserve->deadlineNs>now&&o.reserve->deadlineNs-o.reserve->observedNs<=200000000&&
        o.reserve->allThreeIdle&&o.reserve->capacity==8&&o.reserve->loaded>=0&&o.reserve->loaded<=8&&o.reserve->reserve>0;
}
bool Bc2ResourceInventoryProbe::Held(const ResourceInventoryObservation& o,const ReloadStateOwner& owner,std::int64_t now)const noexcept {
    if(!o.body||!o.display||!BodyInventoryDisplayFresh(*o.display,now))return false;
    const auto& b=*o.body;return b.nativeOwner==owner&&b.phase==BodyHolsterPhase::Held&&b.selectedSlot&&
        b.selectedSlot->item.id==owner.weapon&&b.sampledNs>0&&b.sampledNs<=now&&now-b.sampledNs<=150000000&&
        b.right&&b.right->token.kind==HandClaimKind::GunHold&&b.right->token.owner==b.hand.owner&&
        b.right->token.item==b.physicalGun&&b.right->deadlineNs>now&&!b.outcome.blockWeaponActions&&!b.queuedTarget;
}
bool Bc2ResourceInventoryProbe::Original(const ResourceInventoryObservation& o,const ReloadStateOwner& owner,std::int64_t now)const noexcept {
    return owner.weapon==initial_.weapon&&o.magazine.resource&&AmmoResourceViewFresh(*o.magazine.resource,now)&&
        o.magazine.resource->binding.owner==owner&&o.magazine.resource->binding.context.resource==original_;
}
void Bc2ResourceInventoryProbe::PrepareEmptyMagazine(InputFrame& in,const ReloadStateOwner& owner,
    const ResourceInventoryObservation& o,std::int64_t now)noexcept {
    if(phase_!=Phase::EmptyFire&&phase_!=Phase::EmptyWait&&phase_!=Phase::EmptyDwell)return;
    in.hands[0].squeeze=0;in.hands[1].trigger=0;
    if(owner!=emptyIdentity_.owner){Fail(28,now);return;}
    if(now-phaseAt_>5000000000ll&&phase_!=Phase::EmptyDwell){Fail(29,now);return;}
    // Firing temporarily suspends the body interaction publication. Keep the
    // already-started, bounded input pulse through that gap; releasing and then
    // reasserting it would manufacture a second single-fire edge. New pulses
    // still require fresh ownership and ammunition evidence below.
    if(phase_==Phase::EmptyFire&&emptyPulseAt_&&now-emptyPulseAt_<100000000)in.hands[1].trigger=1;
    if(!Held(o,owner,now)||!Original(o,owner,now))return;
    const auto& v=*o.magazine.resource;const auto& b=v.binding;const auto& shot=v.snapshot;
    if(v.identity!=emptyIdentity_||b.owner!=emptyBinding_.owner||b.context!=emptyBinding_.context||
       b.inventory!=emptyBinding_.inventory||b.switching!=emptyBinding_.switching||
       b.data!=emptyBinding_.data||b.persistence!=emptyBinding_.persistence||v.request!=emptyRequest_||
       v.phase!=AmmunitionLedgerPhase::Ready||v.wellEmpty){Fail(30,now);return;}
    if(shot.sequence<emptySequence_||shot.observedNs<emptyObserved_){Fail(31,now);return;}
    const auto expected=emptyBefore_.loaded-int(emptyShots_);
    if(shot.counts.capacity!=emptyBefore_.capacity||shot.counts.reserve!=emptyBefore_.reserve){Fail(32,now);return;}
    if(shot.counts.loaded!=expected){
        if(shot.counts.loaded!=expected-1||emptyPulses_!=emptyShots_+1||
           shot.sequence<=emptySequence_||shot.observedNs<=emptyPulseAt_){Fail(33,now);return;}
        if(emptyShots_>=emptyShotRows_.size()){Fail(28,now);return;}
        emptyShotRows_[emptyShots_]={emptyPulses_,shot.sequence,shot.observedNs,emptyPulseAt_,shot.counts.loaded};
        ++emptyShots_;
    }
    emptySequence_=shot.sequence;emptyObserved_=shot.observedNs;
    if(phase_==Phase::EmptyFire){
        if(!emptyPulseAt_){
            if(emptyPulses_!=emptyShots_||shot.counts.loaded<=0){Fail(28,now);return;}
            ++emptyPulses_;emptyPulseAt_=now;
        }
        if(now-emptyPulseAt_<100000000)in.hands[1].trigger=1;
        else Move(Phase::EmptyWait,now);
    }
    if(phase_==Phase::EmptyWait&&emptyShots_==emptyPulses_&&
       now-emptyPulseAt_>=250000000&&shot.observedNs>phaseAt_){
        if(shot.counts.loaded==0){emptyAt_=now;Move(Phase::EmptyDwell,now);}
        else {emptyPulseAt_=0;Move(Phase::EmptyFire,now);}
    }
    // Stay empty longer than the enrolled pistol's native 3.75 s reload. This
    // sends no reload/cancel/ack; an automatic refill fails the exact counts.
    if(phase_==Phase::EmptyDwell&&now-emptyAt_>=5000000000ll&&shot.observedNs>=emptyAt_+5000000000ll){
        if(shot.counts.loaded!=0||emptyShots_!=unsigned(emptyBefore_.loaded)){Fail(28,now);return;}
        emptyVerifiedAt_=now;Move(Phase::FirstMagazine,now);
    }
}

void Bc2ResourceInventoryProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,
    const ResourceInventoryObservation& o,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    // A receiver gap provides no controller input. The diagnostic waits; real
    // ordinary consumers and native own Updates still observe that absence.
    if(pump_&&pump_->AwaitingReceiver()&&(!ValidInput(in)||!in.focused||!in.headValid||observed<=0||deadline<=now)){
        if(!pump_->ObserveMissingReceiver(now))Fail(36,now);return;
    }
    if(ObserveOriginInput(in,owner,o,observed,deadline,now))return;
    if(!first_)first_=phaseAt_=now;
    if(now<=0||now<lastNow_||now-first_>=(pump_?58000000000ll:55000000000ll)||
       (began_&&now-began_>=(pump_?50000000000ll:35000000000ll)))Fail(1,now);lastNow_=now;
    if(!ValidInput(in)||!in.focused||!in.headValid||!in.hands[0].gripTracked||!in.hands[1].gripTracked||
        observed<=0||observed>now||deadline<=now||deadline-observed>100000000)Fail(2,now);
    if(replacementEnabled_&&lastInput_&&observed<observed_)Fail(4,now);
    if(initial_.player&&(owner.player!=initial_.player||owner.soldier!=initial_.soldier||owner.weak!=initial_.weak||
        owner.actorGeneration!=initial_.actorGeneration||owner.space!=initial_.space)){
        if(!CancelConsumer()&&ResumeReplacement(in,owner,observed,deadline,now))return;
        Fail(3,now);
    }
    if(lastInput_&&in.generation<lastInput_->generation)Fail(4,now);
    const bool awaitingReplacement=replacementEnabled_&&!replacementResumed_&&ReplacementSourceOwner()&&
        phase_==(replacementMagazine_?Phase::FirstMagazine:Phase::Pump);
    if(awaitingReplacement&&!CancelConsumer()){
        auto sameActorWeapon=*ReplacementSourceOwner();sameActorWeapon.equipGeneration=owner.equipGeneration;
        if(owner!=sameActorWeapon||owner.equipGeneration<replacementPendingEquip_)Fail(18,now);
    }
    if(shellFocus_&&(phase_==Phase::ShellFocusLoss||phase_==Phase::Done)){
        // This finite episode ends after fresh neutral return, not a resumed
        // support-return claim. The actual focus path owns all cancellation.
        bool neutral=true;for(const auto& h:in.hands)neutral=neutral&&!h.held&&!h.touched&&!h.touchActive&&
            h.trigger==0&&h.squeeze==0&&h.stickX==0&&h.stickY==0;
        if(owner!=shellOwner_||!shellFocus_->Source(in.generation,in.spaceGeneration,observed,deadline,now,true,neutral))Fail(37,now);
        observed_=observed;deadline_=deadline;RecordInput(in,owner,observed,deadline,now);return;
    }
    if(lastInput_&&in.generation==lastInput_->generation){
        if(observed!=observed_||deadline!=deadline_)Fail(4,now);
        if(awaitingReplacement&&!CancelConsumer())replacementPendingEquip_=owner.equipGeneration;
        const bool neutral=CancelConsumer()||(awaitingReplacement&&!replacementMagazine_);
        for(unsigned n=0;n<2;++n){in.hands[n].grip=lastInput_->hands[n].grip;in.hands[n].aim=lastInput_->hands[n].aim;
            in.hands[n].squeeze=neutral?0:lastInput_->hands[n].squeeze;
            in.hands[n].gripTracked=lastInput_->hands[n].gripTracked;in.hands[n].aimTracked=lastInput_->hands[n].aimTracked;}
        in.hands[1].trigger=neutral?0:lastInput_->hands[1].trigger;if(neutral)in.hands[0].trigger=0;return;
    }
    observed_=observed;deadline_=deadline;
    if(supportOnly_){PrepareSupportOnly(in,owner,asset,o,observed,deadline,now);RecordInput(in,owner,observed,deadline,now);return;}
    if(!CancelConsumer()&&awaitingReplacement){
        // A checkpoint can invalidate the old selected-equip publication before
        // replacing its actor. This grants only neutral waiting, never a new
        // episode or native/physical authority; the original witness stays exact.
        replacementPendingEquip_=owner.equipGeneration;
        if(replacementMagazine_)WitnessMagazineReplacement(o,now);else WitnessReplacement(o.pumpNative,now);
        // Retain only the diagnostic controller grip/pose on the removed mag.
        // Menu focus and native/physical lifetime policies retain authority.
        for(unsigned n=0;n<2;++n){auto& hand=in.hands[n];hand.trigger=0;
            hand.grip=lastInput_->hands[n].grip;hand.aim=lastInput_->hands[n].aim;
            hand.squeeze=replacementMagazine_&&!CancelConsumer()?lastInput_->hands[n].squeeze:0;}
        RecordInput(in,owner,observed,deadline,now);return;
    }
    if(((phase_>=Phase::Fire&&phase_<=Phase::ShellSettle)||phase_==Phase::Pump||phase_==Phase::ShellSupport)&&owner!=shellOwner_)Fail(18,now);
    if(phase_==Phase::Setup&&replacementResumed_&&asset==SpasReloadAsset&&o.body&&o.display&&
       o.body->nativeOwner==owner&&o.display->selectedOwner==owner&&BodyInventoryDisplayFresh(*o.display,now)&&
       o.body->sampledNs>0&&o.body->sampledNs<=now&&now-o.body->sampledNs<=150000000&&
       o.body->selectedSlot&&o.body->selectedSlot->item.id==owner.weapon){
        // A checkpoint can leave SPAS selected but stowed. Resource publication
        // belongs to the selected rifle, so first draw its real shoulder slot.
        // The display chooses a gesture target; it never authorizes a select.
        std::optional<BodySlotAssignment> target;unsigned candidates=0;
        const auto& anchors=o.body->anchors;
        for(unsigned n=0;n<o.display->count;++n){const auto& slot=o.display->slots[n];
            if(slot.assignment==o.body->selectedSlot||
               (slot.assignment.slot!=anchors.shoulders[0].slot&&slot.assignment.slot!=anchors.shoulders[1].slot))continue;
            ++candidates;
            // Verified BC2 WeaponClassEnum wcAssault/wcSmg. Later actual
            // selected-weapon resource evidence must prove reload enrollment.
            if(slot.native.category==0||slot.native.category==2)target=slot.assignment;
        }
        if(candidates!=1||!target||!inventory_.PreferOtherInitialShoulder(*target))Fail(26,now);
        else {replacementShoulderTarget_=target;Move(Phase::InitialDraw,now);}
    }
    if(phase_==Phase::Setup&&now-first_>=5000000000ll&&o.magazine.resource&&AmmoResourceViewFresh(*o.magazine.resource,now)&&
        o.magazine.resource->binding.owner==owner&&o.display&&BodyInventoryDisplayFresh(*o.display,now)&&o.body){
        initial_=owner;original_=o.magazine.resource->binding.context.resource;Move(Phase::InitialDraw,now);
    }
    in.hands[1].trigger=0;
    if(phase_!=Phase::Setup&&!CancelConsumer()&&!Completed()){
        inventory_.Prepare(in,owner,asset,o.body,o.display,o.magazineRaw,{},observed,deadline,now);
        if(inventory_.CancelConsumer())Fail(5,now);
    }
    if(phase_==Phase::InitialDraw&&replacementShoulderTarget_&&!replacementShoulderBound_&&
       inventory_.State()==Bc2InventoryReloadProbe::Phase::InitialHeld&&Held(o,owner,now)){
        if(owner.weapon!=replacementShoulderTarget_->item.id||o.body->selectedSlot!=replacementShoulderTarget_||asset==SpasReloadAsset)Fail(26,now);
        else if(o.magazine.resource&&AmmoResourceViewFresh(*o.magazine.resource,now)&&o.magazine.resource->binding.owner==owner){
            initial_=owner;original_=o.magazine.resource->binding.context.resource;replacementShoulderBound_=true;
        }
    }
    if(phase_==Phase::InitialDraw&&inventory_.State()==Bc2InventoryReloadProbe::Phase::InitialHeld&&Held(o,owner,now)&&Original(o,owner,now)){
        began_=now;supportCommand_=in.hands[0].grip;
        if(pumpCycles_==8||(replacementEnabled_&&!replacementResumed_&&!replacementMagazine_)){
            // Begin the scene/empty-tube check with its new pump coverage;
            // magazine return after holstering still checks the rifle adapter.
            afterFirst_=o.magazine.resource->snapshot.counts;
            if(inventory_.ResumeInitial(now))Move(Phase::OtherDraw,now);else Fail(11,now);
        }else if(replacementResumed_&&o.magazine.resource->snapshot.counts.loaded<=0)Fail(25,now);
        else if(emptyMagazineFire_){
            const auto& v=*o.magazine.resource;const auto& counts=v.snapshot.counts;
            if(v.phase!=AmmunitionLedgerPhase::Ready||v.wellEmpty||counts.loaded<=0||counts.loaded>int(emptyShotRows_.size())||counts.reserve<=0)Fail(28,now);
            else {emptyBinding_=v.binding;emptyIdentity_=v.identity;emptyBefore_=counts;
                emptyRequest_=v.request;emptySequence_=v.snapshot.sequence;emptyObserved_=v.snapshot.observedNs;
                Move(Phase::EmptyFire,now);}
        }
        else Move(rifleSupportRequired_?Phase::RifleSupport:Phase::FirstMagazine,now);
    }
    if(phase_==Phase::RifleSupport)PrepareRifleSupport(in,owner,o,now);
    if(phase_==Phase::RifleSupportRelease){in.hands[0].grip=in.hands[0].aim=supportCommand_;in.hands[0].squeeze=0;}
    if((phase_==Phase::RifleSupport||phase_==Phase::RifleSupportRelease)&&now-phaseAt_>5000000000ll)Fail(23,now);
    PrepareEmptyMagazine(in,owner,o,now);
    if(phase_==Phase::FirstMagazine){
        if(owner.weapon!=initial_.weapon||(o.magazine.resource&&o.magazine.resource->binding.context.resource!=original_))Fail(6,now);
        else firstMagazine_.Prepare(in,owner,asset,o.magazineRaw,o.magazine,observed,deadline,now);
    }
    if(phase_==Phase::OtherDraw&&inventory_.State()==Bc2InventoryReloadProbe::Phase::OtherHeld&&Held(o,owner,now)){
        if(owner.weapon==initial_.weapon||asset!=SpasReloadAsset)Fail(7,now);
        else if(Reserve(o,owner,now)&&o.reserve->loaded>=int(pumpCycles_==8?8:pumpCycles_==3?3:2)&&
                o.reserve->capacity-o.reserve->loaded+int(pump_?pumpCycles_:1)>=int(shellRounds_)&&
                o.reserve->reserve>=int(shellRounds_)&&!o.shellBlocks&&!o.shell.pending){
            shellOwner_=owner;shellLoaded_=o.reserve->loaded;shellReserve_=o.reserve->reserve;shotSequence_=o.reserve->sequence;
            if(!shell_.Episode(o.body->anchors))Fail(8,now);else Move(pump_?Phase::Pump:Phase::Fire,now);
        }
    }
    if(pump_&&(phase_==Phase::Pump||phase_==Phase::Shell||phase_==Phase::ShellSupport||phase_==Phase::ShellSettle||
              phase_==Phase::RevisitPump||phase_==Phase::RevisitSettle)){
        // Match the actual successful standalone pump controller pose. The
        // inventory script's raised rifle pose puts the SPAS closed fore-end
        // outside the retained arm reach in combined236. Move real input;
        // neither extend IK reach nor replace measured contact geometry.
        auto desired=in.hands[1].grip;
        const bool returning=phase_==Phase::ShellSettle||phase_==Phase::RevisitSettle;
        if(!returning)desired.position={0,-.40f,0};
        // Deliberately request a farther fore-end during the finite Pump phase.
        // Fresh renderer contact still drives the real left-hand rail gesture;
        // only the ordinary arm solver decides whether that hand can reach.
        // Shell returns through this existing smoother, with no claim changes.
        if(pumpUnreachablePose_&&phase_==Phase::Pump)desired.position={0,-.22f,-.40f};
        if(pumpInspectionPose_&&phase_!=Phase::ShellSettle){
            desired.position={0,-.22f,-.40f};
            desired.orientation={0,.7071067812f,0,.7071067812f};
        }
        if(!pumpRightCommand_)pumpRightCommand_=in.hands[1].grip;
        *pumpRightCommand_=Approach(*pumpRightCommand_,desired);
        const auto& a=pumpRightCommand_->position;const auto& b=desired.position;
        pumpRightReturned_=returning&&std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<.001f;
        in.hands[1].grip=in.hands[1].aim=*pumpRightCommand_;
    }
    if(phase_==Phase::Pump){
        pump_->Prepare(in,owner,asset,o.pumpRaw,o.pumpNative,observed,deadline,now);
        if(pump_->Failed())Fail(19,now);
        else if(pump_->Completed()&&Reserve(o,owner,now)&&o.reserve->sequence>shotSequence_){
            if(o.reserve->loaded!=shellLoaded_-int(pumpCycles_)||o.reserve->reserve!=shellReserve_)Fail(9,now);
            else {if(pump_->ReceiverGap()&&originResumed_)originStroke_=true;
                shotObserved_=o.reserve->sequence;Move(Phase::Shell,now);}
        }
    }
    if(phase_==Phase::Fire){if(now-phaseAt_<100000000)in.hands[1].trigger=1;else Move(Phase::WaitShot,now);}
    if(phase_==Phase::WaitShot&&Reserve(o,owner,now)&&o.reserve->sequence>shotSequence_&&o.reserve->observedNs>phaseAt_){
        if(o.reserve->loaded==shellLoaded_-1&&o.reserve->reserve==shellReserve_){shotObserved_=o.reserve->sequence;Move(Phase::Shell,now);}
        else if(o.reserve->loaded!=shellLoaded_||o.reserve->reserve!=shellReserve_)Fail(9,now);
    }
    if(phase_==Phase::Shell)shell_.Prepare(in,owner,asset,o.shellRaw,o.shell,observed,deadline,now);
    if(phase_==Phase::ShellSupport)PrepareShellSupport(in,owner,o.shellRaw,now);
    if(phase_==Phase::ShellSettle&&(!pump_||pumpRightReturned_)&&Reserve(o,owner,now)&&!o.shellBlocks&&!o.shell.pending&&!o.shell.held){
        if(o.reserve->loaded!=shellLoaded_-int(pump_?pumpCycles_:1)+int(shellRounds_)||o.reserve->reserve!=shellReserve_-int(shellRounds_))Fail(10,now);
        else if(replacementResumed_)Move(Phase::Done,now);
        else if(inventory_.ResumeOther(now))Move(Phase::OriginalDraw,now);else Fail(11,now);
    }
    if(phase_==Phase::OriginalDraw&&inventory_.Completed()&&Held(o,owner,now)&&Original(o,owner,now)){
        if(o.magazine.resource->snapshot.counts!=afterFirst_||afterFirst_.loaded!=afterFirst_.capacity)Fail(12,now);
        else Move(Phase::FullReturn,now);
    }
    if(phase_==Phase::FullReturn){if(owner.weapon!=initial_.weapon||(o.magazine.resource&&o.magazine.resource->binding.context.resource!=original_))Fail(6,now);
        else fullReturn_.Prepare(in,owner,asset,o.magazineRaw,o.magazine,observed,deadline,now);}
    if(phase_==Phase::RifleFire){in.hands[0].squeeze=0;if(now-phaseAt_<100000000)in.hands[1].trigger=1;else Move(Phase::WaitRifleShot,now);}
    if((pump_||magazinePostFire_)&&(phase_==Phase::RifleFire||phase_==Phase::WaitRifleShot||phase_==Phase::Dwell)){
        // A bounded automatic-fire pulse can discharge twice (actual F2000 245).
        // Keep exact ownership/ammunition boundaries; a count drop alone is not
        // independent native shot proof. The recording auditor still owns that.
        if(owner!=rifleShotIdentity_.owner)Fail(21,now);
        if(!CancelConsumer()&&o.magazine.resource){
            const auto& v=*o.magazine.resource;const auto& b=v.binding;const auto& shot=v.snapshot;
            if(v.identity!=rifleShotIdentity_||b.owner!=rifleShotBinding_.owner||b.context!=rifleShotBinding_.context||
               b.inventory!=rifleShotBinding_.inventory||b.switching!=rifleShotBinding_.switching||
               b.data!=rifleShotBinding_.data||b.persistence!=rifleShotBinding_.persistence)Fail(21,now);
            else if(AmmoResourceViewFresh(v,now)){
                if(v.phase!=AmmunitionLedgerPhase::Ready||v.wellEmpty||v.request!=rifleShotRequest_||
                   v.requestState!=AmmoResourceRequestState::Completed)Fail(21,now);
                else if(shot.sequence>std::max(rifleShotBaseline_,rifleShotSequence_)&&
                        shot.observedNs>rifleFireAt_&&shot.observedNs>=rifleShotObserved_){
                    const auto count=afterFirst_.loaded-shot.counts.loaded;
                    if(shot.counts.reserve!=afterFirst_.reserve||shot.counts.capacity!=afterFirst_.capacity||
                       count<int(rifleShotCount_)||count<0||count>(magazinePostFire_?1:2))Fail(21,now);
                    else if(count){
                        // A delayed coherent second-shot publication may arrive
                        // after release. Require another stable dwell afterwards.
                        if(phase_==Phase::Dwell&&unsigned(count)>rifleShotCount_)phaseAt_=now;
                        rifleShotCount_=unsigned(count);rifleShotSequence_=shot.sequence;rifleShotObserved_=shot.observedNs;
                    }
                }
            }
        }
        if(phase_==Phase::WaitRifleShot&&rifleShotCount_)Move(Phase::Dwell,now);
    }
    if((phase_==Phase::WaitShot||phase_==Phase::ShellSettle||phase_==Phase::ShellSupport||phase_==Phase::WaitRifleShot)&&now-phaseAt_>5000000000ll)Fail(13,now);
    auto finalCounts=afterFirst_;if(pump_||magazinePostFire_)finalCounts.loaded-=int(rifleShotCount_);
    if(phase_==Phase::Dwell&&now-phaseAt_>=400000000&&Held(o,owner,now)&&Original(o,owner,now)&&
        !o.magazine.result.blocksWeaponActions&&!o.shellBlocks&&o.magazine.resource->snapshot.counts==finalCounts&&
        (!(pump_||magazinePostFire_)||(rifleShotCount_&&o.magazine.resource->snapshot.sequence>=rifleShotSequence_&&
                  o.magazine.resource->snapshot.observedNs>=rifleShotObserved_))){
        if(!revisitEnabled_)Move(Phase::Done,now);
        else if(inventory_.RevisitOther(now))Move(Phase::RevisitDraw,now);
        else Fail(32,now);
    }
    if(phase_==Phase::RevisitDraw&&inventory_.State()==Bc2InventoryReloadProbe::Phase::OtherHeld&&Held(o,owner,now)){
        if(asset!=SpasReloadAsset||owner.weapon!=shellOwner_.weapon)Fail(32,now);
        else if(Reserve(o,owner,now)&&!o.shellBlocks&&!o.shell.pending&&!o.shell.held){
            if(o.reserve->loaded!=shellLoaded_||o.reserve->reserve!=shellReserve_-1)Fail(32,now);
            else {revisitOwner_=owner;revisitLoaded_=o.reserve->loaded;revisitReserve_=o.reserve->reserve;
                revisitShotSequence_=o.reserve->sequence;
                revisitPump_.emplace(calibration_,1,Bc2PumpControllerProbe::Scenario::HeldSupportNatural);
                Move(Phase::RevisitPump,now);}
        }
    }
    if(phase_==Phase::RevisitPump){
        if(owner!=revisitOwner_||!revisitPump_)Fail(32,now);
        else {
            revisitPump_->Prepare(in,owner,asset,o.pumpRaw,o.pumpNative,observed,deadline,now);
            if(revisitPump_->Failed())Fail(33,now);
            else if(revisitPump_->Completed()&&Reserve(o,owner,now)&&o.reserve->sequence>revisitShotSequence_){
                if(o.reserve->loaded!=revisitLoaded_-1||o.reserve->reserve!=revisitReserve_)Fail(34,now);
                else Move(Phase::RevisitSettle,now);
            }
        }
    }
    if(phase_==Phase::RevisitSettle){
        if(owner!=revisitOwner_)Fail(32,now);
        else if(pumpRightReturned_&&Reserve(o,owner,now)&&!o.shellBlocks){
            if(o.reserve->loaded!=revisitLoaded_-1||o.reserve->reserve!=revisitReserve_)Fail(34,now);
            else if(inventory_.ResumeOther(now))Move(Phase::RevisitRifle,now);else Fail(32,now);
        }
    }
    if(phase_==Phase::RevisitRifle&&inventory_.Completed()&&Held(o,owner,now)&&Original(o,owner,now)){
        if(o.magazine.resource->snapshot.counts!=finalCounts||o.magazine.result.blocksWeaponActions)Fail(34,now);
        else {revisitCompleted_=true;Move(Phase::Done,now);}
    }
    if((phase_==Phase::RevisitDraw||phase_==Phase::RevisitSettle||phase_==Phase::RevisitRifle)&&now-phaseAt_>5000000000ll)Fail(35,now);
    if(CancelConsumer()||Completed()){in.hands[0].squeeze=0;in.hands[1].trigger=0;}
    RecordInput(in,owner,observed,deadline,now);
}
bool Bc2ResourceInventoryProbe::ObserveOriginInput(InputFrame& in,const ReloadStateOwner& owner,
    const ResourceInventoryObservation& o,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    if(phase_!=Phase::OriginObservation){
        auto same=shellOwner_;same.space=owner.space;
        if(phase_!=Phase::Pump||!pump_||!lastInput_||!o.pumpNative||owner!=same||
           shellOwner_.space==UINT64_MAX||owner.space!=shellOwner_.space+1||
           pump_->Current()!=Bc2PumpControllerProbe::Phase::InterruptLeft||
           o.pumpNative->phase!=Bc2NativeCyclePhase::Converging||!o.pumpNative->blocksFire||
           o.pumpNative->native.owner!=shellOwner_||!o.pumpNative->cycle||!o.pumpNative->shot)return false;
        // Stop the scripted stroke, not the real consumer. This focused
        // diagnostic changes no native state and cannot mark the combined
        // sequence completed. Normal input must cause any native handoff.
        originBefore_=o.pumpNative;originAfter_=owner;Move(Phase::OriginObservation,now);
    }
    if(!ValidInput(in)||!in.focused||!in.headValid||!in.hands[0].gripTracked||!in.hands[1].gripTracked||
       owner!=originAfter_||!lastInput_||in.generation<lastInput_->generation||
       observed<=0||observed>now||deadline<=now||deadline-observed>100000000||now<phaseAt_){Fail(23,now);return false;}
    // Observe the actual incoming neutral before the fixture changes any
    // controller field; the later dwell handoff is not a new neutral boundary.
    if(pump_->ReceiverGap()&&!pump_->ObserveReceiverReturnNeutral(in,owner,observed,deadline,now)){Fail(37,now);return false;}
    for(unsigned h=0;h<2;++h){in.hands[h].grip=lastInput_->hands[h].grip;in.hands[h].aim=lastInput_->hands[h].aim;
        in.hands[h].trigger=0;in.hands[h].held=0;in.hands[h].stickX=in.hands[h].stickY=0;in.hands[h].squeeze=h&&(!pump_->ReceiverGap()||now-phaseAt_>=200000000)?1.f:0.f;}
    if(o.pumpNative){const auto& v=*o.pumpNative;const auto& old=*originBefore_;
        auto expected=old.native;expected.owner=originAfter_;
        if(v.trackingRebase&&v.trackingRebase->previous==old.native.owner&&v.trackingRebase->current==originAfter_&&
           v.native==expected&&v.cycle==old.cycle&&v.shot==old.shot&&v.loaded==old.loaded&&v.reserve==old.reserve&&
           v.capacity==old.capacity&&v.blocksFire&&v.phase==Bc2NativeCyclePhase::Converging){
            if(!originVerified_)originVerifiedNs_=now;originVerified_=true;
        }
    }
    if(!originVerified_&&now-phaseAt_>5000000000ll){Fail(24,now);return false;}
    if(originVerified_&&pump_&&o.pumpNative&&(!pump_->ReceiverGap()||now-phaseAt_>=200000000)){
        if(!originResumed_){
            if(!pump_->FollowTrackingRebase(in,owner,*o.pumpNative,now)){Fail(27,now);return false;}
            originResumed_=true;
            if(pump_->ReceiverGap()){
                const auto& rebase=*o.pumpNative->trackingRebase;
                if(!inventory_.FollowReceiverSpace(rebase.previous,rebase.current,now)){Fail(36,now);return false;}
                // Resume this same bounded input episode only after the real
                // native debt has changed space. Counts/receipts are retained.
                initial_.space=owner.space;shellOwner_=owner;history_={};
                Move(Phase::Pump,now);lastInput_.reset();observed_=observed;deadline_=deadline;lastNow_=now;
                return true;
            }
        }
        if(!originStroke_){
            if(pumpRightCommand_)in.hands[1].grip=in.hands[1].aim=*pumpRightCommand_;
            pump_->Prepare(in,owner,SpasReloadAsset,o.pumpRaw,o.pumpNative,observed,deadline,now);
            if(pump_->Failed()){Fail(19,now);return false;}
            originStroke_=pump_->Completed();
        }
    }
    lastInput_=in;observed_=observed;deadline_=deadline;lastNow_=now;
    RecordInput(in,owner,observed,deadline,now);return true;
}
void Bc2ResourceInventoryProbe::RecordInput(const InputFrame& in,const ReloadStateOwner& owner,
    std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    InputRow row{in.generation,now,unsigned(phase_),owner.weapon,in.hands[1].trigger,in.hands[0].squeeze,in.hands[1].squeeze};
    // Keep every effective action/owner/phase change in every phase. Periodic
    // samples cover the bounded active episode; idle setup needs only 1 Hz and
    // terminal dwell needs no repeated neutral rows. Overflow remains explicit.
    const bool changed=!inputCount_||row.phase!=previous_.phase||row.weapon!=previous_.weapon||
        row.trigger!=previous_.trigger||row.left!=previous_.left||row.right!=previous_.right;
    const bool periodic=!Completed()&&!CancelConsumer()&&now-previous_.now>=(began_?100000000:1000000000);
    if(changed||periodic){
        if(inputCount_<inputs_.size())inputs_[inputCount_++]=row;else ++inputDropped_;previous_=row;}
    lastInput_=in;
    if(pump_||rifleSupportRequired_){history_[historyAt_]=Source{in,owner,observed,deadline};historyAt_=(historyAt_+1)%history_.size();}
}
void Bc2ResourceInventoryProbe::Observe(const ResourceInventoryObservation& o,const MagazinePackCounters& packs,std::int64_t now)noexcept {
    if(CancelConsumer()||Completed())return;
    if(phase_==Phase::FirstMagazine){
        if(replacementMagazineWitness_&&!replacementResumed_){WitnessMagazineReplacement(o,now);return;}
        firstMagazine_.Observe(o.magazine,packs,now);
        if(WitnessMagazineReplacement(o,now))return;
        if(firstMagazine_.Completed()){
            if(!o.magazine.resource||o.magazine.result.blocksWeaponActions)Fail(14,now);
            else {afterFirst_=o.magazine.resource->snapshot.counts;
                if(magazinePostFire_){
                    const auto& v=*o.magazine.resource;
                    if(!AmmoResourceViewFresh(v,now)||v.phase!=AmmunitionLedgerPhase::Ready||v.wellEmpty||v.snapshot.counts.loaded<=0)Fail(21,now);
                    else {rifleShotBinding_=v.binding;rifleShotIdentity_=v.identity;rifleShotRequest_=v.request;
                        rifleShotBaseline_=v.snapshot.sequence;rifleFireAt_=now;Move(Phase::RifleFire,now);}
                }
                else if(magazineOnly_)Move(Phase::Dwell,now);
                else if(inventory_.ResumeInitial(now))Move(Phase::OtherDraw,now);else Fail(11,now);}}
        else if(firstMagazine_.CancelConsumer())Fail(15,now);
    }
    if(phase_==Phase::Shell){shell_.Observe(o.shell,now);if(shell_.Completed()){
        shellCompletedAt_=now;if(lastInput_)supportCommand_=lastInput_->hands[0].grip;
        if(shellFocus_){
            if(!o.shell.reserve||o.shell.pending||o.shell.held||o.shell.completed!=2||o.shell.submitted!=2||
               o.shell.reserve->loaded!=7||o.shell.reserve->reserve!=22||
               !shellFocus_->Arm(o.shell.cycle,shellOwner_.space,7,22,o.shell.reserve->capacity,now))Fail(37,now);
            else Move(Phase::ShellFocusLoss,now);
        }else Move(pump_?Phase::ShellSupport:Phase::ShellSettle,now);
    }else if(shell_.Failed())Fail(16,now);}
    if(shellFocus_&&phase_==Phase::ShellFocusLoss){
        if(now-phaseAt_>6000000000ll)Fail(37,now);
        else if(Reserve(o,shellOwner_,now)){
            if(!shellFocus_->Counts(o.reserve->loaded,o.reserve->reserve,
                !o.shellBlocks&&!o.shell.pending&&!o.shell.held&&o.shell.completed==2&&o.shell.submitted==2,now))Fail(37,now);
            else if(shellFocus_->Completed())Move(Phase::Done,now);
        }
    }
    if(phase_==Phase::FullReturn){fullReturn_.Observe(o.magazine,packs,now);if(fullReturn_.Completed()){
        if(pump_){if(!o.magazine.resource||o.magazine.result.blocksWeaponActions||
                       !AmmoResourceViewFresh(*o.magazine.resource,now)||o.magazine.resource->snapshot.counts!=afterFirst_||
                       o.magazine.resource->phase!=AmmunitionLedgerPhase::Ready||o.magazine.resource->wellEmpty||
                       o.magazine.resource->requestState!=AmmoResourceRequestState::Completed)Fail(21,now);
            else {const auto& v=*o.magazine.resource;rifleShotBinding_=v.binding;rifleShotIdentity_=v.identity;
                rifleShotRequest_=v.request;rifleShotBaseline_=v.snapshot.sequence;rifleFireAt_=now;Move(Phase::RifleFire,now);}}
        else Move(Phase::Dwell,now);
    }else if(fullReturn_.CancelConsumer())Fail(17,now);}
}
void Bc2ResourceInventoryProbe::PrepareSupportOnly(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,
    const ResourceInventoryObservation& o,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    // This diagnostic emits controller poses/grip edges only. It never calls a
    // reload consumer or uses an ammunition receipt to authorize a grip.
    for(auto& h:in.hands){h.trigger=0;h.held=0;h.stickX=h.stickY=0;}
    if(CancelConsumer()){for(auto& h:in.hands)h.squeeze=0;return;}
    if(Completed()){
        if(lastInput_)for(unsigned h=0;h<2;++h){in.hands[h].grip=lastInput_->hands[h].grip;in.hands[h].aim=lastInput_->hands[h].aim;}
        in.hands[0].squeeze=0;in.hands[1].squeeze=1;return;
    }
    if(phase_==Phase::Setup&&now-first_>=5000000000ll&&o.body&&o.display&&
       o.body->nativeOwner==owner&&o.display->selectedOwner==owner&&BodyInventoryDisplayFresh(*o.display,now)){
        initial_=owner;Move(Phase::InitialDraw,now);
    }
    if(phase_!=Phase::Setup){
        inventory_.Prepare(in,owner,asset,o.body,o.display,{}, {},observed,deadline,now);
        if(inventory_.CancelConsumer()){Fail(5,now);return;}
    }
    if(phase_==Phase::InitialDraw&&inventory_.State()==Bc2InventoryReloadProbe::Phase::InitialHeld&&Held(o,owner,now)){
        initial_=owner;began_=now;supportCommand_=in.hands[0].grip;Move(Phase::RifleSupport,now);
    }
    if(phase_==Phase::RifleSupport||phase_==Phase::RifleSupportRelease){
        if(owner!=initial_||!Held(o,owner,now)){Fail(23,now);in.hands[0].squeeze=0;return;}
        if(phase_==Phase::RifleSupport)PrepareRifleSupport(in,owner,o,now);
        else {in.hands[0].grip=in.hands[0].aim=supportCommand_;in.hands[0].squeeze=0;}
        if(now-phaseAt_>5000000000ll){Fail(23,now);in.hands[0].squeeze=0;}
    }
}
void Bc2ResourceInventoryProbe::PrepareRifleSupport(InputFrame& in,const ReloadStateOwner& owner,const ResourceInventoryObservation& o,std::int64_t now)noexcept {
    // Once ordinary support owns the hand, hold the acquired controller pose.
    // Chasing the corrected weapon/contact would manufacture a release or move
    // the diagnostic hand along with the animation being tested.
    if(rifleSupportClaim_){
        in.hands[0].grip=in.hands[0].aim=supportCommand_;
        in.hands[0].squeeze=1;in.hands[1].trigger=0;return;
    }
    const auto& raw=o.supportRaw;const Source* source=nullptr;
    if(o.supportVerified&&raw.valid&&raw.owner==owner&&raw.inputEvidence.deadlineNs>now)
        for(const auto& h:history_)if(h&&h->owner==owner&&h->input.generation==raw.inputEvidence.sequence&&
            h->observed==raw.inputEvidence.observedNs&&h->deadline==raw.inputEvidence.deadlineNs){source=&*h;break;}
    if(source&&raw.inputEvidence.sequence>lastSupportRaw_){
        const auto controller=PhysicalReloadProbeController(raw,source->input,o.supportTarget);
        if(!controller){Fail(23,now);return;}
        supportCommand_=Approach(supportCommand_,*controller);lastSupportRaw_=raw.inputEvidence.sequence;
        if(reload_insertion_detail::Distance(raw.rawLeftWristWorldMeters,o.supportTarget)<.01f){
            if(!supportAlignedAt_)supportAlignedAt_=now;
        }else supportAlignedAt_=0;
    }
    in.hands[0].grip=in.hands[0].aim=supportCommand_;
    in.hands[0].squeeze=supportAlignedAt_&&now-supportAlignedAt_>=100000000?1.f:0.f;
    in.hands[1].trigger=0;
}
void Bc2ResourceInventoryProbe::PrepareShellSupport(InputFrame& in,const ReloadStateOwner& owner,const ReloadRawContact& raw,std::int64_t now)noexcept {
    const Source* source=nullptr;
    if(raw.valid&&raw.owner==owner&&raw.rigFingerprint==calibration_.rigFingerprint&&raw.inputEvidence.deadlineNs>now)
        for(const auto& h:history_)if(h&&h->owner==owner&&h->input.generation==raw.inputEvidence.sequence&&
            h->observed==raw.inputEvidence.observedNs&&h->deadline==raw.inputEvidence.deadlineNs){source=&*h;break;}
    if(source&&raw.inputEvidence.sequence>lastSupportRaw_){
        const auto desired=Multiply(calibration_.closedWrist,raw.weaponWorldMeters);
        const auto controller=PhysicalReloadProbeController(raw,source->input,desired);
        if(!controller){Fail(20,now);return;}
        supportCommand_=Approach(supportCommand_,*controller);lastSupportRaw_=raw.inputEvidence.sequence;
        if(reload_insertion_detail::Distance(raw.rawLeftWristWorldMeters,desired)<.003f&&reload_insertion_detail::Angle(raw.rawLeftWristWorldMeters,desired)<.05f){
            if(!supportAlignedAt_)supportAlignedAt_=now;
        }else supportAlignedAt_=0;
    }
    in.hands[0].grip=in.hands[0].aim=supportCommand_;in.hands[0].squeeze=1;in.hands[1].trigger=0;
    if(shellSupportToken_&&supportAlignedAt_&&now-supportAlignedAt_>=100000000)Move(Phase::ShellSettle,now);
}
void Bc2ResourceInventoryProbe::ObservePump(const Bc2PhysicalPumpResult& physical,const std::optional<Bc2NativeCycleView>& native,Bc2PumpPackCounters packs,std::int64_t now)noexcept {
    if(WitnessReplacement(native,now))return;
    if(pump_&&(phase_==Phase::Pump||(phase_==Phase::OriginObservation&&originResumed_))){
        pump_->Observe(physical,native,packs,now);if(pump_->Failed())Fail(19,now);
        if(pump_->ReceiverGap()&&originResumed_&&pump_->Completed())originStroke_=true;
    }
    if(revisitPump_&&phase_==Phase::RevisitPump){revisitPump_->Observe(physical,native,packs,now);if(revisitPump_->Failed())Fail(33,now);}
}
void Bc2ResourceInventoryProbe::ObserveSupport(const Bc2PumpTracking& tracking,const SupportGripResult& supported,
    const HandInteractionSample& current,const HandInteractionSample& original,const std::optional<HandClaim>& left,
    const std::optional<HandClaim>& gun,bool shellReturned,std::int64_t now)noexcept {
    if(replacementEnabled_&&!replacementResumed_&&replacementWitness_)return;
    if(phase_==Phase::RifleSupport&&supported.holding&&supported.token&&left&&gun&&
       left->token.kind==HandClaimKind::WeaponSupport&&left->token.owner==current.owner&&gun->token.owner==current.owner&&
       gun->token.kind==HandClaimKind::GunHold&&left->token.item==gun->token.item&&
       left->token.prerequisiteClaim==gun->token.id&&left->inputSequence==original.sequence&&
       gun->inputSequence==current.sequence&&left->deadlineNs>now&&gun->deadlineNs>now&&
       current.owner==original.owner&&weapon_cycle_detail::Window(original.observedNs,original.deadlineNs,now)&&
       current.focused&&current.tracked[0]&&current.tracked[1]&&!current.released[0]&&!original.released[0]){
        if(rifleSupportClaim_&&(left->token.id!=rifleSupportClaim_||original.sequence<rifleSupportSequence_||
           now<rifleSupportLast_||now-rifleSupportLast_>150000000)) {Fail(23,now);return;}
        if(!rifleSupportClaim_){rifleSupportClaim_=left->token.id;rifleSupportBegan_=now;}
        // Repeated callbacks for one controller packet are not a longer hold.
        if(original.sequence>rifleSupportSequence_){
            if(rifleSupportLast_)rifleSupportMaxGap_=std::max(rifleSupportMaxGap_,now-rifleSupportLast_);
            rifleSupportSequence_=original.sequence;rifleSupportLast_=now;++rifleSupportSamples_;
            if(now-rifleSupportBegan_>=750000000&&rifleSupportSamples_>=3)Move(Phase::RifleSupportRelease,now);
        }
    }else if(phase_==Phase::RifleSupport&&rifleSupportClaim_){
        // Never accept reacquisition after a dropped claim as a continuous hold.
        Fail(23,now);return;
    }else if(phase_==Phase::RifleSupportRelease&&rifleSupportClaim_&&!supported.holding&&!left&&current.released[0]&&
             current.sequence>rifleSupportSequence_){
        rifleSupportReleased_=true;supportAlignedAt_=0;lastSupportRaw_=0;Move(supportOnly_?Phase::Done:Phase::FirstMagazine,now);
    }
    if(pump_&&(phase_==Phase::Pump||(phase_==Phase::OriginObservation&&originResumed_))){pump_->ObserveHeldSupport(supported,current,left,gun,now);
        pump_->ObserveSupport(tracking,supported,now);if(pump_->Failed())Fail(19,now);}
    if(revisitPump_&&phase_==Phase::RevisitPump){revisitPump_->ObserveHeldSupport(supported,current,left,gun,now);
        revisitPump_->ObserveSupport(tracking,supported,now);if(revisitPump_->Failed())Fail(33,now);}
    if(!pump_||phase_!=Phase::ShellSupport||!shellReturned||shellSupportToken_)return;
    if(!supported.holding||!supported.engaged||!supported.token||!left||!gun||now<shellCompletedAt_||
       left->token.kind!=HandClaimKind::WeaponSupport||left->token.hand!=InteractionHand::Left||
       gun->token.kind!=HandClaimKind::GunHold||gun->token.hand!=InteractionHand::Right||left->token.owner!=current.owner||
       gun->token.owner!=current.owner||left->token.item!=gun->token.item||left->token.item.id!=shellOwner_.weapon||
       left->token.prerequisiteClaim!=gun->token.id||left->inputSequence!=original.sequence||left->deadlineNs>original.deadlineNs||left->deadlineNs<=now||
       gun->inputSequence!=current.sequence||gun->deadlineNs<=now||!current.focused||!current.tracked[0]||!current.tracked[1]||
       current.owner!=original.owner||current.released[0]||current.released[1]||original.released[0]||original.released[1]||
       !weapon_cycle_detail::Window(original.observedNs,original.deadlineNs,now)||original.sequence>current.sequence||
       supported.input.generation!=current.sequence||supported.input.hands[0].squeeze<=.35f){Fail(22,now);return;}
    shellSupportToken_=supported.token;shellSupportClaim_=left->token.id;shellSupportSource_=original.sequence;
    shellSupportObserved_=now;shellSupportDeadline_=left->deadlineNs;
}
void Bc2ResourceInventoryProbe::Report(std::ostream& out)const {
    out<<"{\"input_only\":true,\"persistent_consumers\":true,\"headset_verified\":false,\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_
       <<",\"magazine_postfire\":"<<(magazinePostFire_?"true":"false")
       <<",\"pump_inspection_pose\":"<<(pumpInspectionPose_?"true":"false")
       <<",\"pump_unreachable_hand_probe\":"<<(pumpUnreachablePose_?"true":"false")
       <<",\"support_only\":"<<(supportOnly_?"true":"false")
       <<",\"magazine_only\":"<<(magazineOnly_?"true":"false")
       <<",\"rifle_support_required\":"<<(rifleSupportRequired_?"true":"false")<<",\"rifle_support_claim\":"<<rifleSupportClaim_
       <<",\"rifle_support_source\":"<<rifleSupportSequence_<<",\"rifle_support_released\":"<<(rifleSupportReleased_?"true":"false")
       <<",\"rifle_support_began_ns\":"<<rifleSupportBegan_<<",\"rifle_support_last_ns\":"<<rifleSupportLast_
       <<",\"rifle_support_samples\":"<<rifleSupportSamples_<<",\"rifle_support_max_gap_ns\":"<<rifleSupportMaxGap_
       <<",\"manual_pump\":"<<(pump_?"true":"false")<<",\"requested_shells\":"<<shellRounds_<<",\"active_budget_ns\":"<<(pump_?50000000000ll:35000000000ll)
       <<",\"completed\":"<<(Completed()?"true":"false")<<",\"began_ns\":"<<began_<<",\"shot_sequence\":"<<shotObserved_<<",\"shell_loaded_before\":"<<shellLoaded_<<",\"shell_reserve_before\":"<<shellReserve_
       <<",\"shell_support\":{\"returned\":"<<(shellSupportToken_?"true":"false")<<",\"token\":"<<shellSupportToken_
       <<",\"claim\":"<<shellSupportClaim_<<",\"source_sequence\":"<<shellSupportSource_<<",\"observed_ns\":"<<shellSupportObserved_
       <<",\"deadline_ns\":"<<shellSupportDeadline_<<",\"native_completed_ns\":"<<shellCompletedAt_<<'}'
       <<",\"rifle_shot_count\":"<<rifleShotCount_<<",\"rifle_fire_ns\":"<<rifleFireAt_<<",\"rifle_shot_sequence\":"<<rifleShotSequence_<<",\"rifle_shot_observed_ns\":"<<rifleShotObserved_<<",\"transitions\":[";
    for(unsigned n=0;n<transitionCount_;++n){if(n)out<<',';const auto& r=transitions_[n];out<<"{\"phase\":"<<r.phase<<",\"failure\":"<<r.failure<<",\"now_ns\":"<<r.now<<'}';}
    out<<"],\"input_logging_version\":2,\"active_sample_ns\":100000000,\"setup_sample_ns\":1000000000,\"terminal_sample_ns\":0,\"input_rows_dropped\":"<<inputDropped_<<",\"effective_input\":[";
    for(unsigned n=0;n<inputCount_;++n){if(n)out<<',';const auto& r=inputs_[n];out<<"{\"generation\":"<<r.generation<<",\"now_ns\":"<<r.now<<",\"phase\":"<<r.phase<<",\"weapon\":"<<r.weapon<<",\"trigger\":"<<r.trigger<<",\"left_squeeze\":"<<r.left<<",\"right_squeeze\":"<<r.right<<'}';}
    out<<"],\"shell_focus_loss\":";if(shellFocus_)shellFocus_->Report(out);else out<<"{\"enabled\":false}";
    out<<",\"origin_observation\":{\"started\":"<<(originBefore_?"true":"false")<<",\"verified\":"<<(originVerified_?"true":"false")
       <<",\"old_space\":"<<(originBefore_?originBefore_->native.owner.space:0)<<",\"new_space\":"<<originAfter_.space
       <<",\"cycle\":"<<(originBefore_?originBefore_->cycle:0)<<",\"shot\":"<<(originBefore_?originBefore_->shot:0)
       <<",\"loaded\":"<<(originBefore_?originBefore_->loaded:-1)<<",\"reserve\":"<<(originBefore_?originBefore_->reserve:-1)
       <<",\"verified_ns\":"<<originVerifiedNs_<<",\"full_stroke_verified\":"<<(originStroke_?"true":"false")<<",\"native_authority\":false}"
       <<",\"actor_replacement\":{\"enabled\":"<<(replacementEnabled_?"true":"false")
       <<",\"witnessed\":"<<(ActorReplacementWitnessed()?"true":"false")<<",\"resumed\":"<<(replacementResumed_?"true":"false")
       <<",\"pre_restart_scope\":\""<<(replacementMagazine_?"initial_rifle_original_magazine_removed_held":"initial_rifle_draw_then_unfinished_spas_shot")
       <<"\",\"post_restart_scope\":\"one_original_magazine_return_then_pump_shell_and_support\""
       <<",\"skips_rifle_support\":"<<(replacementEnabled_?"true":"false")<<",\"skips_pre_restart_magazines\":"<<(replacementEnabled_&&!replacementMagazine_?"true":"false")
       <<",\"skips_post_restart_chest_and_final_rifle\":"<<(replacementEnabled_?"true":"false")
       <<",\"overall_first_ns\":"<<first_<<",\"witnessed_ns\":"<<replacementWitnessAt_<<",\"resumed_ns\":"<<replacementAt_
       <<",\"pending_equip_generation\":"<<replacementPendingEquip_
       <<",\"shoulder_bootstrap\":"<<(replacementShoulderTarget_?"true":"false")<<",\"shoulder_bound\":"<<(replacementShoulderBound_?"true":"false")
       <<",\"shoulder_target_weapon\":"<<(replacementShoulderTarget_?replacementShoulderTarget_->item.id:0)
       <<",\"input_generation\":"<<replacementInput_<<",\"input_observed_ns\":"<<replacementInputObserved_<<",\"input_deadline_ns\":"<<replacementInputDeadline_;
    const auto owner=[&](const ReloadStateOwner& o){out<<"{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
        <<",\"actor_generation\":"<<o.actorGeneration<<",\"equip_generation\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}';};
    out<<",\"replacement_owner\":";owner(replacementOwner_);out<<",\"unfinished_native\":";
    if(replacementWitness_){const auto& v=*replacementWitness_;out<<"{\"owner\":";owner(v.native.owner);
        out<<",\"server_player\":"<<v.native.serverPlayer<<",\"server_soldier\":"<<v.native.serverSoldier<<",\"server_item\":"<<v.native.serverItem
           <<",\"firing\":["<<v.native.firing[0]<<','<<v.native.firing[1]<<','<<v.native.firing[2]<<"],\"phase\":"<<unsigned(v.phase)
           <<",\"failure\":"<<unsigned(v.failure)<<",\"cycle\":"<<v.cycle<<",\"shot\":"<<v.shot<<",\"blocks_fire\":"<<(v.blocksFire?"true":"false")
           <<",\"loaded\":"<<v.loaded<<",\"reserve\":"<<v.reserve<<",\"capacity\":"<<v.capacity<<'}';
    }else out<<"null";
    out<<",\"unfinished_magazine\":";
    if(replacementMagazineWitness_){const auto& v=*replacementMagazineWitness_;const auto& r=*v.receipt;
        out<<"{\"owner\":";owner(v.identity.owner);
        out<<",\"server_player\":"<<v.identity.serverPlayer<<",\"server_soldier\":"<<v.identity.serverSoldier<<",\"server_item\":"<<v.identity.serverItem
           <<",\"firing\":["<<v.identity.firing[0]<<','<<v.identity.firing[1]<<','<<v.identity.firing[2]<<"]"
           <<",\"request\":"<<v.request<<",\"remove_command\":"<<r.command.id<<",\"authority_invocation\":"<<r.authorityInvocation
           <<",\"original_id\":"<<v.original->id<<",\"original_rounds\":"<<v.original->rounds
           <<",\"loaded_before\":"<<r.before.loaded<<",\"loaded_after\":"<<r.after.loaded<<",\"reserve\":"<<r.after.reserve<<",\"capacity\":"<<r.after.capacity
           <<",\"requested_ns\":"<<r.command.requestedNs<<",\"completed_ns\":"<<r.completedNs
           <<",\"snapshot_ns\":"<<v.snapshot.observedNs<<",\"snapshot_deadline_ns\":"<<v.snapshot.deadlineNs
           <<",\"well_empty\":true,\"original_held\":true,\"native_authority\":false}";
    }else out<<"null";
    out<<",\"prior_report\":"<<(replacementPriorReport_.empty()?"null":replacementPriorReport_)<<"},";
    out<<"\"empty_fire\":{\"enabled\":"<<(emptyMagazineFire_?"true":"false")<<",\"pulses\":"<<emptyPulses_
       <<",\"shots\":"<<emptyShots_<<",\"loaded_before\":"<<emptyBefore_.loaded<<",\"reserve_before\":"<<emptyBefore_.reserve
       <<",\"capacity\":"<<emptyBefore_.capacity<<",\"empty_ns\":"<<emptyAt_<<",\"verified_ns\":"<<emptyVerifiedAt_<<",\"rows\":[";
    for(unsigned n=0;n<emptyShots_;++n){if(n)out<<',';const auto& row=emptyShotRows_[n];
        out<<"{\"pulse\":"<<row.pulse<<",\"sequence\":"<<row.sequence<<",\"observed_ns\":"<<row.observed<<",\"input_ns\":"<<row.input<<",\"loaded\":"<<row.loaded<<'}';}
    out<<"]},\"inventory\":";
    inventory_.Report(out);out<<",\"first_magazine\":";firstMagazine_.Report(out);out<<",\"shell\":";shell_.Report(out);out<<",\"full_return\":";fullReturn_.Report(out);
    if(pump_){out<<",\"physical_pump\":";pump_->Report(out);}
    out<<",\"pump_revisit\":{\"enabled\":"<<(revisitEnabled_?"true":"false")
       <<",\"completed\":"<<(revisitCompleted_?"true":"false")<<",\"loaded_before\":"<<revisitLoaded_
       <<",\"reserve_before\":"<<revisitReserve_<<",\"physical_pump\":";
    if(revisitPump_)revisitPump_->Report(out);else out<<"null";out<<"}}";
}
}
