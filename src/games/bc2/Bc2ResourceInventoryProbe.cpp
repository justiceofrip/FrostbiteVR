#include "Bc2ResourceInventoryProbe.h"
#include <ostream>
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
bool Bc2ResourceInventoryProbe::EnablePump(const Bc2PumpCalibration& calibration)noexcept {
    if(first_||pump_||phase_!=Phase::Setup||!PumpCalibrationValid(calibration))return false;
    calibration_=calibration;pump_.emplace(calibration,1);return true;
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
void Bc2ResourceInventoryProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,
    const ResourceInventoryObservation& o,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    if(!first_)first_=phaseAt_=now;
    if(now<=0||now<lastNow_||now-first_>=(pump_?58000000000ll:55000000000ll)||
       (began_&&now-began_>=(pump_?50000000000ll:35000000000ll)))Fail(1,now);lastNow_=now;
    if(!ValidInput(in)||!in.focused||!in.headValid||!in.hands[0].gripTracked||!in.hands[1].gripTracked||
        observed<=0||observed>now||deadline<=now||deadline-observed>100000000)Fail(2,now);
    if(initial_.player&&(owner.player!=initial_.player||owner.soldier!=initial_.soldier||owner.weak!=initial_.weak||
        owner.actorGeneration!=initial_.actorGeneration||owner.space!=initial_.space))Fail(3,now);
    if(lastInput_&&in.generation<lastInput_->generation)Fail(4,now);
    if(lastInput_&&in.generation==lastInput_->generation){
        if(observed!=observed_||deadline!=deadline_)Fail(4,now);
        for(unsigned n=0;n<2;++n){in.hands[n].grip=lastInput_->hands[n].grip;in.hands[n].aim=lastInput_->hands[n].aim;
            in.hands[n].squeeze=CancelConsumer()?0:lastInput_->hands[n].squeeze;}
        in.hands[1].trigger=CancelConsumer()?0:lastInput_->hands[1].trigger;return;
    }
    observed_=observed;deadline_=deadline;
    if(((phase_>=Phase::Fire&&phase_<=Phase::ShellSettle)||phase_==Phase::Pump||phase_==Phase::ShellSupport)&&owner!=shellOwner_)Fail(18,now);
    if(phase_==Phase::Setup&&now-first_>=5000000000ll&&o.magazine.resource&&AmmoResourceViewFresh(*o.magazine.resource,now)&&
        o.magazine.resource->binding.owner==owner&&o.display&&BodyInventoryDisplayFresh(*o.display,now)&&o.body){
        initial_=owner;original_=o.magazine.resource->binding.context.resource;Move(Phase::InitialDraw,now);
    }
    in.hands[1].trigger=0;
    if(phase_!=Phase::Setup&&!CancelConsumer()&&!Completed()){
        inventory_.Prepare(in,owner,asset,o.body,o.display,o.magazineRaw,{},observed,deadline,now);
        if(inventory_.CancelConsumer())Fail(5,now);
    }
    if(phase_==Phase::InitialDraw&&inventory_.State()==Bc2InventoryReloadProbe::Phase::InitialHeld&&Held(o,owner,now)&&Original(o,owner,now)){
        began_=now;Move(Phase::FirstMagazine,now);
    }
    if(phase_==Phase::FirstMagazine){
        if(owner.weapon!=initial_.weapon||(o.magazine.resource&&o.magazine.resource->binding.context.resource!=original_))Fail(6,now);
        else firstMagazine_.Prepare(in,owner,asset,o.magazineRaw,o.magazine,observed,deadline,now);
    }
    if(phase_==Phase::OtherDraw&&inventory_.State()==Bc2InventoryReloadProbe::Phase::OtherHeld&&Held(o,owner,now)){
        if(owner.weapon==initial_.weapon||asset!=SpasReloadAsset)Fail(7,now);
        else if(Reserve(o,owner,now)&&o.reserve->loaded>=2&&!o.shellBlocks&&!o.shell.pending){
            shellOwner_=owner;shellLoaded_=o.reserve->loaded;shellReserve_=o.reserve->reserve;shotSequence_=o.reserve->sequence;
            if(!shell_.Episode(o.body->anchors))Fail(8,now);else Move(pump_?Phase::Pump:Phase::Fire,now);
        }
    }
    if(pump_&&(phase_==Phase::Pump||phase_==Phase::Shell||phase_==Phase::ShellSupport||phase_==Phase::ShellSettle)){
        // Match the actual successful standalone pump controller pose. The
        // inventory script's raised rifle pose puts the SPAS closed fore-end
        // outside the retained arm reach in combined236. Move real input;
        // neither extend IK reach nor replace measured contact geometry.
        auto desired=in.hands[1].grip;
        if(phase_!=Phase::ShellSettle)desired.position={0,-.40f,0};
        if(!pumpRightCommand_)pumpRightCommand_=in.hands[1].grip;
        *pumpRightCommand_=Approach(*pumpRightCommand_,desired);
        const auto& a=pumpRightCommand_->position;const auto& b=desired.position;
        pumpRightReturned_=phase_==Phase::ShellSettle&&std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<.001f;
        in.hands[1].grip=in.hands[1].aim=*pumpRightCommand_;
    }
    if(phase_==Phase::Pump){
        pump_->Prepare(in,owner,asset,o.pumpRaw,o.pumpNative,observed,deadline,now);
        if(pump_->Failed())Fail(19,now);
        else if(pump_->Completed()&&Reserve(o,owner,now)&&o.reserve->sequence>shotSequence_){
            if(o.reserve->loaded!=shellLoaded_-1||o.reserve->reserve!=shellReserve_)Fail(9,now);
            else {shotObserved_=o.reserve->sequence;Move(Phase::Shell,now);}
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
        if(o.reserve->loaded!=shellLoaded_||o.reserve->reserve!=shellReserve_-1)Fail(10,now);
        else if(inventory_.ResumeOther(now))Move(Phase::OriginalDraw,now);else Fail(11,now);
    }
    if(phase_==Phase::OriginalDraw&&inventory_.Completed()&&Held(o,owner,now)&&Original(o,owner,now)){
        if(o.magazine.resource->snapshot.counts!=afterFirst_||afterFirst_.loaded!=afterFirst_.capacity)Fail(12,now);
        else Move(Phase::FullReturn,now);
    }
    if(phase_==Phase::FullReturn){if(owner.weapon!=initial_.weapon||(o.magazine.resource&&o.magazine.resource->binding.context.resource!=original_))Fail(6,now);
        else fullReturn_.Prepare(in,owner,asset,o.magazineRaw,o.magazine,observed,deadline,now);}
    if(phase_==Phase::RifleFire){in.hands[0].squeeze=0;if(now-phaseAt_<100000000)in.hands[1].trigger=1;else Move(Phase::WaitRifleShot,now);}
    if(phase_==Phase::WaitRifleShot&&Original(o,owner,now)&&o.magazine.resource->snapshot.sequence>rifleShotBaseline_&&
       o.magazine.resource->snapshot.observedNs>rifleFireAt_){
        const auto& shot=o.magazine.resource->snapshot;auto expected=afterFirst_;--expected.loaded;
        if(shot.counts==expected){rifleShotSequence_=shot.sequence;rifleShotObserved_=shot.observedNs;Move(Phase::Dwell,now);}
        else if(shot.counts!=afterFirst_)Fail(21,now);
    }
    if((phase_==Phase::WaitShot||phase_==Phase::ShellSettle||phase_==Phase::ShellSupport||phase_==Phase::WaitRifleShot)&&now-phaseAt_>5000000000ll)Fail(13,now);
    auto finalCounts=afterFirst_;if(pump_&&rifleShotSequence_)--finalCounts.loaded;
    if(phase_==Phase::Dwell&&now-phaseAt_>=400000000&&Held(o,owner,now)&&Original(o,owner,now)&&
        !o.magazine.result.blocksWeaponActions&&!o.shellBlocks&&o.magazine.resource->snapshot.counts==finalCounts)Move(Phase::Done,now);
    if(CancelConsumer()||Completed()){in.hands[0].squeeze=0;in.hands[1].trigger=0;}
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
    if(pump_){history_[historyAt_]=Source{in,owner,observed,deadline};historyAt_=(historyAt_+1)%history_.size();}
}
void Bc2ResourceInventoryProbe::Observe(const ResourceInventoryObservation& o,const MagazinePackCounters& packs,std::int64_t now)noexcept {
    if(CancelConsumer()||Completed())return;
    if(phase_==Phase::FirstMagazine){firstMagazine_.Observe(o.magazine,packs,now);
        if(firstMagazine_.Completed()){
            if(!o.magazine.resource||o.magazine.result.blocksWeaponActions)Fail(14,now);
            else {afterFirst_=o.magazine.resource->snapshot.counts;if(inventory_.ResumeInitial(now))Move(Phase::OtherDraw,now);else Fail(11,now);}}
        else if(firstMagazine_.CancelConsumer())Fail(15,now);
    }
    if(phase_==Phase::Shell){shell_.Observe(o.shell,now);if(shell_.Completed()){
        shellCompletedAt_=now;if(lastInput_)supportCommand_=lastInput_->hands[0].grip;
        Move(pump_?Phase::ShellSupport:Phase::ShellSettle,now);
    }else if(shell_.Failed())Fail(16,now);}
    if(phase_==Phase::FullReturn){fullReturn_.Observe(o.magazine,packs,now);if(fullReturn_.Completed()){
        if(pump_){if(!o.magazine.resource||o.magazine.result.blocksWeaponActions)Fail(21,now);
            else {rifleShotBaseline_=o.magazine.resource->snapshot.sequence;rifleFireAt_=now;Move(Phase::RifleFire,now);}}
        else Move(Phase::Dwell,now);
    }else if(fullReturn_.CancelConsumer())Fail(17,now);}
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
    if(pump_&&phase_==Phase::Pump){pump_->Observe(physical,native,packs,now);if(pump_->Failed())Fail(19,now);}
}
void Bc2ResourceInventoryProbe::ObserveSupport(const Bc2PumpTracking& tracking,const SupportGripResult& supported,
    const HandInteractionSample& current,const HandInteractionSample& original,const std::optional<HandClaim>& left,
    const std::optional<HandClaim>& gun,bool shellReturned,std::int64_t now)noexcept {
    if(pump_&&phase_==Phase::Pump){pump_->ObserveSupport(tracking,supported,now);if(pump_->Failed())Fail(19,now);}
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
       <<",\"manual_pump\":"<<(pump_?"true":"false")<<",\"active_budget_ns\":"<<(pump_?50000000000ll:35000000000ll)
       <<",\"completed\":"<<(Completed()?"true":"false")<<",\"began_ns\":"<<began_<<",\"shot_sequence\":"<<shotObserved_<<",\"shell_loaded_before\":"<<shellLoaded_<<",\"shell_reserve_before\":"<<shellReserve_
       <<",\"shell_support\":{\"returned\":"<<(shellSupportToken_?"true":"false")<<",\"token\":"<<shellSupportToken_
       <<",\"claim\":"<<shellSupportClaim_<<",\"source_sequence\":"<<shellSupportSource_<<",\"observed_ns\":"<<shellSupportObserved_
       <<",\"deadline_ns\":"<<shellSupportDeadline_<<",\"native_completed_ns\":"<<shellCompletedAt_<<'}'
       <<",\"rifle_fire_ns\":"<<rifleFireAt_<<",\"rifle_shot_sequence\":"<<rifleShotSequence_<<",\"rifle_shot_observed_ns\":"<<rifleShotObserved_<<",\"transitions\":[";
    for(unsigned n=0;n<transitionCount_;++n){if(n)out<<',';const auto& r=transitions_[n];out<<"{\"phase\":"<<r.phase<<",\"failure\":"<<r.failure<<",\"now_ns\":"<<r.now<<'}';}
    out<<"],\"input_logging_version\":2,\"active_sample_ns\":100000000,\"setup_sample_ns\":1000000000,\"terminal_sample_ns\":0,\"input_rows_dropped\":"<<inputDropped_<<",\"effective_input\":[";
    for(unsigned n=0;n<inputCount_;++n){if(n)out<<',';const auto& r=inputs_[n];out<<"{\"generation\":"<<r.generation<<",\"now_ns\":"<<r.now<<",\"phase\":"<<r.phase<<",\"weapon\":"<<r.weapon<<",\"trigger\":"<<r.trigger<<",\"left_squeeze\":"<<r.left<<",\"right_squeeze\":"<<r.right<<'}';}
    out<<"],\"inventory\":";inventory_.Report(out);out<<",\"first_magazine\":";firstMagazine_.Report(out);out<<",\"shell\":";shell_.Report(out);out<<",\"full_return\":";fullReturn_.Report(out);
    if(pump_){out<<",\"physical_pump\":";pump_->Report(out);}out<<'}';
}
}
