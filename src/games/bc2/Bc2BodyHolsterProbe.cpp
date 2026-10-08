#include "Bc2BodyHolsterProbe.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace fvr::bc2 {
namespace {
using namespace interaction;
bool Static(const math::Pose& p)noexcept{return std::hypot(p.position.x,p.position.y,p.position.z)<.0001f&&
    std::hypot(p.orientation.x,p.orientation.y,p.orientation.z)<.0001f&&std::abs(std::abs(p.orientation.w)-1.f)<.0001f;}
float Distance(const math::Vec3& a,const math::Vec3& b)noexcept{return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
math::Pose Step(const math::Pose& a,const math::Vec3& b)noexcept {math::Pose out;const auto d=Distance(a.position,b);const auto f=d>.025f?.025f/d:1.f;
    out.position={a.position.x+(b.x-a.position.x)*f,a.position.y+(b.y-a.position.y)*f,a.position.z+(b.z-a.position.z)*f};return out;}
constexpr math::Vec3 Neutral{.20f,-.25f,-.45f},FreeA{.12f,-.30f,-.40f},FreeB{.40f,-.05f,-.35f};
}
bool Bc2BodyHolsterProbe::Fresh(const BodyHolsterProbeSample& s,std::int64_t now)const noexcept {
    return s.nativeOwner==owner_&&s.hand.owner==physical_&&s.physicalGun==physicalGun_&&
        s.physicalGun.id&&s.physicalGun.generation==s.hand.owner.equipGeneration&&s.sampledNs>0&&s.sampledNs<=now&&s.trialStartNs>0&&
        s.trialDeadlineNs>now&&s.trialDeadlineNs-s.trialStartNs<=15000000000ll&&s.hand.observedNs>0&&s.hand.observedNs<=now&&
        s.hand.deadlineNs>now&&s.hand.deadlineNs-s.hand.observedNs<=150000000&&s.hand.focused&&s.hand.tracked[0]&&s.hand.tracked[1]&&
        s.input.generation==s.hand.sequence&&s.input.spaceGeneration==owner_.space&&s.nativeTick&&s.selectedSlot==slot_;
}
bool Bc2BodyHolsterProbe::Held(const BodyHolsterProbeSample& s,std::int64_t now)const noexcept {
    return Fresh(s,now)&&s.phase==BodyHolsterPhase::Held&&s.right&&s.right->token.kind==HandClaimKind::GunHold&&
        s.right->token.owner==physical_&&s.right->token.hand==InteractionHand::Right&&
        s.right->token.item==physicalGun_&&s.right->deadlineNs>now&&!s.left&&!s.outcome.freeRight&&
        !s.outcome.blockWeaponActions&&s.outcome.allowAutomaticGunHold;
}
bool Bc2BodyHolsterProbe::FireHeld(const BodyHolsterProbeSample& s,std::int64_t now)const noexcept {
    // Ordinary Fire cancels new inventory gestures. The existing exact claim,
    // not the permission to acquire an automatic new claim, must remain valid.
    auto copy=s;copy.outcome.allowAutomaticGunHold=true;
    return Held(copy,now)&&restoredClaim_&&s.right->token.id==restoredClaim_&&!s.suppression;
}
bool Bc2BodyHolsterProbe::FirePresentationUnavailable(const BodyHolsterProbeSample& s,std::int64_t now)const noexcept {
    if(!s.outcome.blockWeaponActions||s.outcome.allowAutomaticGunHold||s.outcome.inventoryEvaluation!=BodyInventoryEvaluation::NotEvaluated||
        s.outcome.visibility.enabled||s.outcome.freeRight||s.outcome.inventory.request||s.outcome.ordinaryDraw.command)return false;
    auto copy=s;copy.outcome.blockWeaponActions=false;copy.outcome.allowAutomaticGunHold=true;
    return FireHeld(copy,now); // Exact current owner/slot/claim/input remain mandatory.
}
void Bc2BodyHolsterProbe::DeferFire(std::int64_t now)noexcept {
    if(!fireUnavailable_){fireUnavailable_=true;fireUnavailableAt_=now;++fireUnavailableCount_;}
    if(!fireUnavailableFirst_)fireUnavailableFirst_=now;
    trigger_=0;
    // A interrupted pulse never creates a second Fire edge. End its actual
    // output now and resume only fresh neutral-release evidence afterwards.
    if(pulseStart_&&!pulseEnd_){pulseEnd_=now;PhaseTo(BodyHolsterFixturePhase::FireReleased,now);}
    if(now-fireUnavailableAt_>=200000000)Cancel(24,now);
}
void Bc2BodyHolsterProbe::ResumeFireEvidence(std::int64_t now)noexcept {if(fireUnavailable_&&now-fireUnavailableAt_>=200000000)Cancel(24,now);else fireUnavailable_=false;}
bool Bc2BodyHolsterProbe::Empty(const BodyHolsterProbeSample& s,std::int64_t now)const noexcept {
    return Fresh(s,now)&&s.phase==BodyHolsterPhase::Empty&&!s.left&&!s.right&&s.outcome.inventory.emptyHands&&s.outcome.freeRight&&
        BodyFreeRightEvidenceCurrent(*s.outcome.freeRight,now)&&s.visibility&&s.visibility->hidden&&s.visibility->verifiedCopyMask==3&&
        s.visibility->nativeOwner==owner_&&s.visibility->request==s.request&&s.visibility->deadlineNs>now&&
        s.suppression&&s.suppression->nativeTick==s.nativeTick&&s.suppression->request==s.request&&
        HolsterSuppressionCurrent(*s.suppression,*s.suppression);
}
void Bc2BodyHolsterProbe::PhaseTo(BodyHolsterFixturePhase p,std::int64_t now)noexcept {phase_=p;phaseAt_=now;Row(now,true);}
void Bc2BodyHolsterProbe::Cancel(unsigned why,std::int64_t now)noexcept {
    if(!enabled_||phase_==BodyHolsterFixturePhase::Done||Failed())return;failure_=why;squeeze_=trigger_=0;PhaseTo(BodyHolsterFixturePhase::Failed,now);
}
void Bc2BodyHolsterProbe::Prepare(InputFrame& input,const ReloadStateOwner& owner,std::string_view asset,
    std::shared_ptr<const BodyHolsterProbeSample> state,std::int64_t observed,std::int64_t deadline,std::int64_t now,
    const std::optional<Bc2AmmoReserveLease>& ammo)noexcept {
    if(!enabled_)return;
    if(FireRequested())input.hands[1].trigger=0;
    if(!first_){first_=phaseAt_=now;command_.position=Neutral;}
    if(now<=0||now<lastNow_||now-first_>=12500000000ll)Cancel(1,now);lastNow_=now;
    const bool source=ValidInput(input)&&input.focused&&input.headValid&&Static(input.referenceHead)&&Static(input.head)&&
        input.hands[0].gripTracked&&input.hands[1].gripTracked&&input.hands[1].aimTracked&&observed>0&&observed<=now&&
        deadline>now&&deadline-observed<=100000000;
    const bool table=BodyHolsterConfiguredDiagnostic(profile_);
    if(!ValidBodyHolsterDiagnosticProfile(profile_)||(!table&&!asset.empty()&&asset!=BodyHolsterDiagnosticAsset(profile_)))Cancel(2,now);
    if(phase_==BodyHolsterFixturePhase::Warmup){
        if(state&&source&&!asset.empty()&&(table||asset==BodyHolsterDiagnosticAsset(profile_))&&state->selectedSlot&&state->nativeOwner==owner&&state->phase==BodyHolsterPhase::Held){
            owner_=owner;physical_=state->hand.owner;physicalGun_=state->physicalGun;slot_=state->selectedSlot;
            const auto a=std::find_if(state->anchors.shoulders.begin(),state->anchors.shoulders.end(),[&](const auto& x){return x.slot==slot_->slot;});
            if(a==state->anchors.shoulders.end()||slot_->item.id!=owner.weapon)Cancel(3,now);
            else{anchor_=*a;actual_=state;if(Held(*state,now)){baselineClaim_=state->right->token.id;PhaseTo(BodyHolsterFixturePhase::Baseline,now);}}
        }
        if(phase_==BodyHolsterFixturePhase::Warmup&&now-first_>2500000000ll)Cancel(4,now);
    }else if(!Failed()&&phase_!=BodyHolsterFixturePhase::Done&&(!source||owner!=owner_))Cancel(5,now);
    bool ammoReady=true;
    if(profile_==BodyHolsterDiagnosticProfile::ExactConfiguredTableFire&&
       (phase_==BodyHolsterFixturePhase::Restored||phase_==BodyHolsterFixturePhase::FirePulse||phase_==BodyHolsterFixturePhase::FireReleased)){
        ammoReady=diagnosticFireAmmo_.Tick(ammo,owner,now,pulseStart_!=0);
        if(diagnosticFireAmmo_.Failed())Cancel(23,now);
    }
    if(FireRequested()&&(phase_==BodyHolsterFixturePhase::FirePulse||phase_==BodyHolsterFixturePhase::FireReleased)){
        // A missing original ammo lease permits no Fire, including a router's
        // duplicate cached action. Use the same bounded neutral-output path;
        // explicit bad ammo identity/count still fails above before this point.
        if(!ammoReady&&!Failed())DeferFire(now);
        if(!state)DeferFire(now);
        else if(!FireHeld(*state,now)){if(FirePresentationUnavailable(*state,now))DeferFire(now);else Cancel(19,now);}
        else if(ammoReady){ResumeFireEvidence(now);}
        if(!Failed()&&!fireUnavailable_&&phase_==BodyHolsterFixturePhase::FirePulse){
            if(pulseStart_&&input.generation>lastInput_&&now-pulseStart_>=80000000){pulseEnd_=now;trigger_=0;PhaseTo(BodyHolsterFixturePhase::FireReleased,now);}
            else if(!ammoReady)trigger_=0;
            else if(pulseStart_||input.generation>lastInput_){if(!pulseStart_)pulseStart_=now;trigger_=1;}
        }
    }
    if(lastInput_&&input.generation<=lastInput_){
        if(input.generation<lastInput_||observed!=originalObserved_||deadline!=originalDeadline_)Cancel(6,now);
        input.hands[1].grip=input.hands[1].aim=command_;input.hands[1].squeeze=Failed()?0:squeeze_;if(FireRequested())input.hands[1].trigger=Failed()?0:trigger_;return;
    }
    if(!source){input.hands[1].squeeze=0;return;}
    lastInput_=input.generation;originalObserved_=observed;originalDeadline_=deadline;
    const math::Vec3 shoulder{anchor_.center.x,anchor_.center.y,-anchor_.center.z};
    math::Vec3 target=Neutral;squeeze_=0;
    switch(phase_){
    case BodyHolsterFixturePhase::ReachHolster:case BodyHolsterFixturePhase::ReachDraw:target=shoulder;break;
    case BodyHolsterFixturePhase::PressHolster:case BodyHolsterFixturePhase::WaitEmpty:
    case BodyHolsterFixturePhase::PressDraw:case BodyHolsterFixturePhase::WaitDraw:target=shoulder;squeeze_=1;break;
    case BodyHolsterFixturePhase::Restored:{
        // Releasing a freshly drawn gun at the same shoulder is a legitimate
        // stow gesture. Keep the draw grip until both the outgoing command and
        // the actually consumed hand have cleared this exact current anchor.
        auto outgoing=input;outgoing.hands[1].grip=outgoing.hands[1].aim=command_;
        const auto commanded=BodyAnchorHandPose(outgoing,InteractionHand::Right);
        const auto consumed=state?BodyAnchorHandPose(state->input,InteractionHand::Right):std::nullopt;
        squeeze_=(!commanded||!consumed||BodyAnchorContains(anchor_,*commanded)||BodyAnchorContains(anchor_,*consumed))?1.f:0.f;
        break;}
    case BodyHolsterFixturePhase::FreeA:target=FreeA;break;
    case BodyHolsterFixturePhase::FreeB:target=FreeB;break;
    default:break;}
    command_=Step(command_,target);input.hands[1].grip=input.hands[1].aim=command_;input.hands[1].squeeze=squeeze_;
    if(FireRequested())input.hands[1].trigger=Failed()?0:trigger_;
    input.hands[1].touchActive=TouchComponents;input.hands[1].touched=0;
    if((phase_==BodyHolsterFixturePhase::ReachHolster||phase_==BodyHolsterFixturePhase::ReachDraw)&&Distance(command_.position,shoulder)<.001f){
        const auto body=BodyAnchorHandPose(input,InteractionHand::Right);
        if(!body||!BodyAnchorContains(anchor_,*body))Cancel(7,now);
        else if(now-phaseAt_>=300000000)PhaseTo(phase_==BodyHolsterFixturePhase::ReachHolster?BodyHolsterFixturePhase::PressHolster:BodyHolsterFixturePhase::PressDraw,now);
    }
}
void Bc2BodyHolsterProbe::Observe(std::shared_ptr<const BodyHolsterProbeSample> state,std::int64_t now)noexcept {
    if(!enabled_||Failed()||phase_==BodyHolsterFixturePhase::Done)return;
    if(now<lastNow_){Cancel(1,now);return;}lastNow_=now;if(state)actual_=state;
    if(phase_==BodyHolsterFixturePhase::Warmup)return;
    const bool fireStage=FireRequested()&&(phase_==BodyHolsterFixturePhase::Restored||phase_==BodyHolsterFixturePhase::FirePulse||phase_==BodyHolsterFixturePhase::FireReleased);
    if(!state){if(fireStage)DeferFire(now);else Cancel(8,now);return;}
    if(!Fresh(*state,now)){Cancel(8,now);return;}
    if(fireStage&&FirePresentationUnavailable(*state,now)){DeferFire(now);return;}
    if(fireStage)ResumeFireEvidence(now);
    if(state->phase==BodyHolsterPhase::Recovering||state->queuedTarget){Cancel(9,now);return;}
    switch(phase_){
    case BodyHolsterFixturePhase::Baseline:
        if(!Held(*state,now))Cancel(10,now);else if(now-phaseAt_>=2000000000)PhaseTo(BodyHolsterFixturePhase::ReachHolster,now);break;
    case BodyHolsterFixturePhase::PressHolster:
        if(state->phase==BodyHolsterPhase::HidePending)PhaseTo(BodyHolsterFixturePhase::WaitEmpty,now);
        else if(now-phaseAt_>1000000000)Cancel(11,now);break;
    case BodyHolsterFixturePhase::WaitEmpty:
        if(Empty(*state,now)){hiddenRequest_=state->request;emptyPairs_=state->pack.pairedCopies;PhaseTo(BodyHolsterFixturePhase::FreeA,now);}
        else if(now-phaseAt_>1500000000)Cancel(12,now);break;
    case BodyHolsterFixturePhase::FreeA:case BodyHolsterFixturePhase::FreeB:
        if(!Empty(*state,now)||state->request!=hiddenRequest_)Cancel(13,now);
        else if(now-phaseAt_>=1700000000&&state->pack.pairedCopies>emptyPairs_+2){
            if(phase_==BodyHolsterFixturePhase::FreeA){emptyPairs_=state->pack.pairedCopies;PhaseTo(BodyHolsterFixturePhase::FreeB,now);}
            else if(challenge_&&challenge_->committed)PhaseTo(BodyHolsterFixturePhase::ReachDraw,now);
            else if(now-phaseAt_>2400000000)Cancel(14,now);
        }break;
    case BodyHolsterFixturePhase::PressDraw:
        if(state->phase==BodyHolsterPhase::ShowPending){showRequest_=state->request;PhaseTo(BodyHolsterFixturePhase::WaitDraw,now);}
        else if(now-phaseAt_>1000000000)Cancel(15,now);break;
    case BodyHolsterFixturePhase::WaitDraw:
        if(Held(*state,now)&&state->right->token.id!=baselineClaim_&&state->visibility&&!state->visibility->hidden&&
            state->visibility->verifiedCopyMask==3&&state->visibility->nativeOwner==owner_&&
            state->visibility->request==showRequest_&&showRequest_!=hiddenRequest_&&state->visibility->deadlineNs>now){
            showRequest_=state->visibility->request;restoredClaim_=state->right->token.id;PhaseTo(BodyHolsterFixturePhase::Restored,now);}
        else if(now-phaseAt_>1500000000)Cancel(16,now);break;
    case BodyHolsterFixturePhase::Restored:
        if(!Held(*state,now))Cancel(17,now);else if(now-phaseAt_>=2000000000)
            PhaseTo(FireRequested()?BodyHolsterFixturePhase::FirePulse:BodyHolsterFixturePhase::Done,now);break;
    case BodyHolsterFixturePhase::FirePulse:
        if(!FireHeld(*state,now))Cancel(19,now);
        else if(state->input.hands[1].trigger>=.75f){
            if(!state->fireRequested||!state->fireCacheRead||state->fireCache!=1.f||!state->fireTickMs)Cancel(20,now);
            else{++fireCommits_;if(!fireFirstMs_)fireFirstMs_=state->fireTickMs;fireLastMs_=state->fireTickMs;if(fireCommits_==1)Row(now,true);}}
        break;
    case BodyHolsterFixturePhase::FireReleased:
        if(!FireHeld(*state,now))Cancel(19,now);
        else if(state->input.hands[1].trigger==0){
            if(state->fireRequested||!state->fireCacheRead||state->fireCache!=0.f)Cancel(21,now);
            else{++releaseCommits_;if(releaseCommits_==1)Row(now,true);if(now-phaseAt_>=500000000){
                if(!fireCommits_||!pulseStart_||pulseEnd_<=pulseStart_||pulseEnd_-pulseStart_>200000000)Cancel(22,now);
                else if(!Held(*state,now))Cancel(17,now);else PhaseTo(BodyHolsterFixturePhase::Done,now);}}}
        break;
    default:break;}
    Row(now);
}
bool Bc2BodyHolsterProbe::ChallengeWanted(const BodyHolsterProbeSample& state,std::int64_t now)const noexcept {
    return enabled_&&!challenge_&&phase_==BodyHolsterFixturePhase::FreeB&&now-phaseAt_>=400000000&&
        Empty(state,now)&&state.pack.pairedCopies>emptyPairs_+2;
}
void Bc2BodyHolsterProbe::RecordChallenge(const BodyHolsterChallengeRow& row,std::int64_t now)noexcept {
    if(!enabled_||challenge_)return;challenge_=row;if(!row.committed)Cancel(18,now);Row(now,true);
}
void Bc2BodyHolsterProbe::Row(std::int64_t now,bool force)noexcept {
    if(rowCount_==rows_.size()||(!force&&now-lastRow_<100000000))return;lastRow_=now;
    auto& r=rows_[rowCount_++];r.phase=unsigned(phase_);r.reason=failure_;r.now=now;r.grip=command_.position;r.squeeze=squeeze_;
    if(!actual_)return;const auto& s=*actual_;r.bodyPhase=unsigned(s.phase);r.input=s.hand.sequence;r.observed=s.hand.observedNs;r.deadline=s.hand.deadlineNs;
    r.tick=s.nativeTick;r.request=s.request;r.right=s.right?s.right->token.id:0;r.left=s.left?s.left->token.id:0;r.slot=s.selectedSlot?s.selectedSlot->slot:0;
    r.pairedFree=s.pack.pairedCopies;r.inventoryCommit=s.outcome.inventory.committedRequest;r.free=bool(s.outcome.freeRight);r.blocksActions=s.outcome.blockWeaponActions;r.allowsGunHold=s.outcome.allowAutomaticGunHold;
    r.trigger=trigger_;r.consumedTrigger=s.input.hands[1].trigger;r.fireRead=s.fireCacheRead;r.fireCache=s.fireCache;r.fireRequested=s.fireRequested;
    r.consumedGrip=s.input.hands[1].grip.position;r.consumedSqueeze=s.input.hands[1].squeeze;
    r.suppressed=s.suppression&&HolsterSuppressionCurrent(*s.suppression,*s.suppression);
    if(s.visibility){r.draw=s.visibility->drawSerial;r.receiptInput=s.visibility->inputSequence;r.hidden=s.visibility->hidden;}
}
void Bc2BodyHolsterProbe::Report(std::ostream& out)const {
    const auto precision=out.precision();out.precision(std::numeric_limits<float>::max_digits10);
    out<<"{\"requested\":"<<(enabled_?"true":"false")<<",\"diagnostic_profile\":"<<unsigned(profile_)<<",\"asset\":\""<<BodyHolsterDiagnosticAsset(profile_)<<"\",\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_
       <<",\"baseline_claim\":"<<baselineClaim_<<",\"hidden_request\":"<<hiddenRequest_<<",\"show_request\":"<<showRequest_
       <<",\"player\":"<<owner_.player<<",\"soldier\":"<<owner_.soldier<<",\"weak\":"<<owner_.weak<<",\"weapon\":"<<owner_.weapon
       <<",\"actor_generation\":"<<owner_.actorGeneration<<",\"native_equip_generation\":"<<owner_.equipGeneration<<",\"physical_equip_generation\":"<<physical_.equipGeneration<<",\"space\":"<<owner_.space
       <<",\"physical_gun\":"<<physicalGun_.id<<",\"physical_gun_generation\":"<<physicalGun_.generation
       <<",\"fire_unavailable\":{\"active\":"<<(fireUnavailable_?"true":"false")<<",\"count\":"<<fireUnavailableCount_<<",\"first_ns\":"<<fireUnavailableFirst_<<",\"max_wait_ns\":200000000}"
       <<",\"post_draw_fire\":{\"requested\":"<<(FireRequested()?"true":"false")<<",\"restored_claim\":"<<restoredClaim_
       <<",\"pulse_start_ns\":"<<pulseStart_<<",\"pulse_end_ns\":"<<pulseEnd_<<",\"first_commit_ms\":"<<fireFirstMs_<<",\"last_commit_ms\":"<<fireLastMs_
       <<",\"fire_cache_commits\":"<<fireCommits_<<",\"release_cache_commits\":"<<releaseCommits_<<",\"shot_verified\":false}"
       <<",\"production_input_accepted\":false,\"headset_verified\":false,\"gpu_visibility_verified\":false,\"rows\":[";
    for(unsigned n=0;n<rowCount_;++n){if(n)out<<',';const auto& r=rows_[n];out<<"{\"phase\":"<<r.phase<<",\"reason\":"<<r.reason<<",\"body_phase\":"<<r.bodyPhase
        <<",\"now_ns\":"<<r.now<<",\"input\":"<<r.input<<",\"observed_ns\":"<<r.observed<<",\"deadline_ns\":"<<r.deadline<<",\"native_tick\":"<<r.tick
        <<",\"request\":"<<r.request<<",\"right_claim\":"<<r.right<<",\"left_claim\":"<<r.left<<",\"draw_serial\":"<<r.draw<<",\"receipt_input\":"<<r.receiptInput
        <<",\"paired_free_copies\":"<<r.pairedFree<<",\"inventory_commit\":"<<r.inventoryCommit<<",\"slot\":"<<r.slot<<",\"grip\":["<<r.grip.x<<','<<r.grip.y<<','<<r.grip.z
        <<"],\"squeeze\":"<<r.squeeze<<",\"consumed_grip\":["<<r.consumedGrip.x<<','<<r.consumedGrip.y<<','<<r.consumedGrip.z<<"],\"consumed_squeeze\":"<<r.consumedSqueeze
        <<",\"trigger\":"<<r.trigger<<",\"consumed_trigger\":"<<r.consumedTrigger<<",\"fire_cache\":"<<r.fireCache
        <<",\"fire_cache_read\":"<<(r.fireRead?"true":"false")<<",\"fire_requested\":"<<(r.fireRequested?"true":"false")
        <<",\"blocks_actions\":"<<(r.blocksActions?"true":"false")<<",\"allows_gun_hold\":"<<(r.allowsGunHold?"true":"false")
        <<",\"hidden\":"<<(r.hidden?"true":"false")<<",\"suppressed\":"<<(r.suppressed?"true":"false")<<",\"free_right\":"<<(r.free?"true":"false")<<'}';}
    out<<"],\"challenge\":";if(!challenge_)out<<"null";else{const auto& c=*challenge_;out<<"{\"input\":"<<c.request.input.sequence<<",\"native_tick\":"<<c.request.nativeTick
        <<",\"request\":"<<c.request.request<<",\"cache\":"<<c.request.cache<<",\"staged\":"<<(c.staged?"true":"false")<<",\"committed\":"<<(c.committed?"true":"false")
        <<",\"restored\":"<<(c.restored?"true":"false")<<",\"unrelated_preserved\":"<<(c.unrelatedPreserved?"true":"false");
        const auto& o=c.request.owner;const auto& i=c.request.input;
        out<<",\"native_owner\":{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
           <<",\"actor_generation\":"<<o.actorGeneration<<",\"equip_generation\":"<<o.equipGeneration<<",\"space\":"<<o.space<<'}'
           <<",\"physical_owner\":{\"actor\":"<<i.owner.actor<<",\"actor_generation\":"<<i.owner.actorGeneration
           <<",\"equip_generation\":"<<i.owner.equipGeneration<<",\"space\":"<<i.owner.space<<'}'
           <<",\"input_observed_ns\":"<<i.observedNs<<",\"input_deadline_ns\":"<<i.deadlineNs<<",\"input_now_ns\":"<<i.nowNs
           <<",\"focused\":"<<(i.focused?"true":"false")<<",\"tracked\":["<<(i.tracked[0]?"true":"false")<<','<<(i.tracked[1]?"true":"false")<<']';
        const auto words=[&](const char* name,const auto& values){out<<",\""<<name<<"\":[";for(unsigned n=0;n<4;++n){if(n)out<<',';out<<values[n];}out<<']';};
        words("before",c.before);words("challenged",c.challenged);words("written",c.written);words("observed",c.observed);out<<'}';}
    if(profile_==BodyHolsterDiagnosticProfile::ExactConfiguredTableFire){out<<",\"fire_ammo_preflight\":";diagnosticFireAmmo_.Report(out);}
    out<<'}';out.precision(precision);
}
}
