#include "Bc2PlayerGrenade.h"
#include "fvr/interaction/InputMenuOwnership.h"
#include <limits>
namespace fvr::bc2::player_grenade { namespace {
using namespace private_grenade;
void Count(unsigned& n)noexcept{if(n<std::numeric_limits<unsigned>::max())++n;}
bool SameLifetime(const ReloadStateOwner& a,const ReloadStateOwner& b)noexcept {
    return a.player==b.player&&a.soldier==b.soldier&&a.weak==b.weak&&a.actorGeneration==b.actorGeneration;
}
void Menu(Sample& s)noexcept {
    const auto& m=s.input.menu;
    s.admission.menuOwnershipKnown=interaction::ValidInputMenuOwnership(m)&&m.owner!=interaction::InputMenuOwner::Unknown&&
        s.nowNs>0&&m.deadlineNs>s.nowNs&&m.deadlineNs-s.nowNs<=OneGrenade::MaxSourceLifetimeNs;
    s.admission.menuClear=interaction::GameplayOwnsInput(m,s.nowNs);
}
bool Fresh(const Sample& s)noexcept {
    return s.identity.Valid()&&interaction::ValidInput(s.input)&&s.identity.space==s.input.spaceGeneration&&
        s.input.focused&&s.input.headValid&&s.input.hands[0].gripTracked&&s.input.hands[1].gripTracked&&s.input.hands[1].aimTracked&&
        s.observedNs>0&&s.nowNs>=s.observedNs&&s.deadlineNs>s.nowNs&&s.deadlineNs>s.observedNs&&
        s.deadlineNs-s.observedNs<=OneGrenade::MaxSourceLifetimeNs&&s.semantic.active&&s.semantic.owner==s.identity.actor;
}
bool Neutral(const Sample& s)noexcept {
    if(!Fresh(s)||!s.admission.All()||s.semantic.held||s.semantic.pressed||s.semantic.released||
       s.semantic.forward||s.semantic.strafe||s.semantic.turnDegrees)return false;
    constexpr auto components=interaction::Stick|interaction::Trigger|interaction::Squeeze|interaction::Primary|interaction::Secondary|interaction::StickClick;
    for(const auto& h:s.input.hands)if(h.held||(h.active&components)!=components||h.trigger>.1f||h.squeeze>.35f||std::hypot(h.stickX,h.stickY)>.18f)return false;
    return true;
}
}
void Controller::Lifetime(const ReloadStateOwner& owner)noexcept {
    if(!owner.player||!owner.soldier||!owner.weak||!owner.actorGeneration)return;
    if(lifetime_.player&&!SameLifetime(lifetime_,owner)){
        gesture_={};staged_={};baseline_.reset();body_.reset();gun_={};phase_=Phase::Gesture;fault_=Fault::None;neutralAfter_=lastSequence_=0;neutralObserved_=lastObserved_=0;gap_=false;
        lastReturnInput_=lastNeutralInput_=0;lastReturnObservedNs_=lastNeutralObservedNs_=0;lastReturnedOwner_={};
    }
    lifetime_=owner; // A tracking-space change alone cannot clear quarantine.
}
private_grenade::Sample Controller::Sample(const Native& n,const Facts& f)const noexcept {
    auto sample=private_grenade::StockSample(n,f);Menu(sample);return sample;
}
bool Controller::Plan(const Native& n)const noexcept {
    return baseline_&&baseline_->owner==n.owner&&baseline_->carried==n.carried&&
        private_grenade::StockItemSame(baseline_->original,n.original)&&private_grenade::StockItemSame(baseline_->target,n.target)&&baseline_->targetFlags==n.targetFlags;
}
void Controller::CancelGesture(std::uint64_t sequence)noexcept {
    gesture_={};staged_={};baseline_.reset();body_.reset();gun_={};phase_=Phase::AwaitNeutral;neutralAfter_=sequence;neutralObserved_=lastObserved_;
}
void Controller::Quarantine(Fault why)noexcept {if(fault_==Fault::None)fault_=why;phase_=Phase::Quarantined;}
std::optional<Request> Controller::Prepare(const std::optional<Native>& n,const Facts& f)noexcept {
    if(n)Lifetime(n->owner);
    if(phase_==Phase::Quarantined)return {};
    if(!n){Missing(f.source.nowNs);return {};}
    auto sample=Sample(*n,f);
    if(Fresh(sample)&&sample.observedNs>=lastObserved_){lastSequence_=sample.input.generation;lastObserved_=sample.observedNs;}
    if(phase_==Phase::AwaitReturn)return {};
    if(phase_==Phase::AwaitNeutral){
        // Publisher reconnection may restart packet numbers, but cannot make
        // an old original source timestamp into a fresh neutral observation.
        if(sample.input.generation==neutralAfter_||sample.observedNs<=neutralObserved_||!Neutral(sample))return {};
        if(lastReturnInput_){lastNeutralInput_=sample.input.generation;lastNeutralObservedNs_=sample.observedNs;}
        phase_=Phase::Gesture;gesture_={};neutralAfter_=0;
    }
    const auto before=gesture_.Current();const auto request=gesture_.Observe(sample);
    if(gesture_.Current()==private_grenade::Phase::Rejected){CancelGesture(sample.input.generation);return {};}
    if(gesture_.Current()==private_grenade::Phase::Pressed&&before!=private_grenade::Phase::Pressed){
        baseline_=*n;body_=f.display;gun_=f.right->token;Count(attempts_);
    }
    if(request&&!Plan(*n)){CancelGesture(sample.input.generation);return {};}
    return request;
}
bool Controller::CanStage(const Request& request,const std::optional<Native>& n,const Facts& f)noexcept {
    if(phase_!=Phase::Gesture||!n||!Plan(*n)){CancelGesture(f.source.input.generation);return false;}
    const auto sample=Sample(*n,f);
    if(!gesture_.CanStage(request,sample)){CancelGesture(sample.input.generation);return false;}
    staged_=sample;return true;
}
void Controller::Commit(const Request& request,const Facts& f,std::optional<bool> bit)noexcept {
    if(phase_!=Phase::Gesture||gesture_.Current()!=private_grenade::Phase::Issued)return;
    if(!bit){Quarantine(Fault::CommitUnknown);return;}
    auto current=staged_;current.nowNs=f.source.nowNs;current.input.menu=f.source.input.menu;
    current.admission.ownerCurrent=f.source.admission.ownerCurrent;Menu(current);
    if(*bit)Count(edges_);
    const bool accepted=gesture_.Committed(request,current,*bit);
    if(!accepted){if(*bit)Quarantine(Fault::CommitValidation);else CancelGesture(current.input.generation);return;}
    phase_=Phase::AwaitReturn;gap_=false;currentNoGaps_=true;
}
void Controller::Missing(std::int64_t)noexcept {
    if(phase_==Phase::AwaitReturn){if(!gap_)Count(gaps_);gap_=true;currentNoGaps_=false;return;}
    if(phase_==Phase::Gesture)CancelGesture(lastSequence_);
}
void Controller::Observe(const std::optional<Native>& n,const Facts& f)noexcept {
    if(n)Lifetime(n->owner);
    if(phase_!=Phase::AwaitReturn)return;
    if(!n){Missing(f.source.nowNs);return;}
    gap_=false;const auto& b=*baseline_;const auto& v=*n;
    if(!SameLifetime(b.owner,v.owner)||b.carried!=v.carried||!private_grenade::StockItemSame(b.original,v.original)||
       !private_grenade::StockItemSame(b.target,v.target)||b.targetFlags!=v.targetFlags){Quarantine(Fault::NativeIdentity);return;}
    if(v.owner.weapon!=b.original.client.target&&v.owner.weapon!=b.target.client.target){Quarantine(Fault::ForeignSelection);return;}
    for(unsigned i=0;i<3;++i){
        const auto& old=i<2?b.original.client.branches[i]:b.original.server;
        const auto& cur=i<2?v.original.client.branches[i]:v.original.server;
        if(old.loaded!=cur.loaded||old.reserve!=cur.reserve){Quarantine(Fault::OriginalAmmo);return;}
        const auto& oldGrenade=i<2?b.target.client.branches[i]:b.target.server;
        const auto& grenade=i<2?v.target.client.branches[i]:v.target.server;
        const auto before=std::int64_t(oldGrenade.loaded)+oldGrenade.reserve,after=std::int64_t(grenade.loaded)+grenade.reserve;
        if(grenade.loaded<0||grenade.reserve<0||after<before-1||after>before){Quarantine(Fault::TargetAmmo);return;}
        if(v.selectors[i].current!=b.originalSlot&&v.selectors[i].current!=b.targetSlot){Quarantine(Fault::ForeignSelection);return;}
    }
    if(f.custodyObserved&&f.display&&body_&&(f.display->revision!=body_->revision||f.display->carried!=body_->carried||
       f.display->count!=body_->count||f.display->slots!=body_->slots)){Quarantine(Fault::BodyIdentity);return;}
    // A read/tracking gap may cover the entire stock selection interval. The
    // final selector history, conserved ammo and renewed custody jointly prove
    // return; intermediate observation is not renewed action authority.
    const bool returned=v.owner.weapon==b.original.client.target&&v.selectors[0].current==b.originalSlot&&v.selectors[1].current==b.originalSlot&&v.selectors[2].current==b.originalSlot&&
        v.selectors[0].previous==b.targetSlot&&v.selectors[1].previous==b.targetSlot;
    const auto total=std::int64_t(v.target.client.branches[0].loaded)+v.target.client.branches[0].reserve;
    const auto before=std::int64_t(b.target.client.branches[0].loaded)+b.target.client.branches[0].reserve;
    const bool consumed=private_grenade::StockAmmoEqual(v.target)&&total==before-1;
    if(!returned||!consumed||!private_grenade::StockReady(v.original)||!private_grenade::StockAmmoEqual(v.original)||!f.custodyObserved)return;
    const auto sample=Sample(v,f);
    // A known conflicting holder/item is real loss. Absent publications during
    // the ordinary return transition merely defer acknowledgement.
    if(Fresh(sample)&&f.display&&f.display->selectedOwner==v.owner&&
       (f.display->physicalSelected!=body_->physicalSelected||
        (f.right&&f.right->inputSequence==sample.input.generation&&f.right->deadlineNs>sample.nowNs&&
         (f.right->token.kind!=interaction::HandClaimKind::GunHold||f.right->token.item.id!=gun_.item.id)))){
        Quarantine(Fault::Custody);return;
    }
    const bool support=!f.left||(f.right&&f.left->token.kind==interaction::HandClaimKind::WeaponSupport&&
        f.left->token.hand==interaction::InteractionHand::Left&&f.left->token.owner==f.right->token.owner&&
        f.left->token.item==f.right->token.item&&f.left->token.prerequisiteClaim==f.right->token.id&&
        f.left->inputSequence==sample.input.generation&&f.left->deadlineNs>sample.nowNs);
    const bool held=Fresh(sample)&&f.display&&body_&&f.display->selectedOwner==v.owner&&f.display->physicalSelected==body_->physicalSelected&&
        f.visible&&f.holsterPhase==1&&f.right&&support&&f.hand.focused&&f.hand.tracked[1]&&
        f.right->token.kind==interaction::HandClaimKind::GunHold&&f.right->token.id!=gun_.id&&
        f.right->token.owner.equipGeneration!=gun_.owner.equipGeneration&&f.right->token.owner.actor==gun_.owner.actor&&
        f.right->token.owner.actorGeneration==gun_.owner.actorGeneration&&f.right->token.owner.space==v.owner.space&&
        f.right->token.owner==f.hand.owner&&f.right->token.item==f.handItem&&f.right->token.item.id==gun_.item.id&&
        f.right->inputSequence==sample.input.generation&&f.right->deadlineNs>sample.nowNs;
    // Return can be observed before its ordinary visible/body acknowledgement.
    // Wait with no new authority; a transient missing publication is not loss.
    if(!held)return;
    lastReturnInput_=sample.input.generation;lastReturnObservedNs_=sample.observedNs;lastReturnedOwner_=v.owner;
    lastNeutralInput_=0;lastNeutralObservedNs_=0;
    lastReturnNoGaps_=currentNoGaps_;Count(completed_);lastObserved_=sample.observedNs;CancelGesture(sample.input.generation);
}
std::optional<TwoGrenadeNativeWitness> Controller::SecondDeathReadiness(const Native& n,const Facts& f)const noexcept {
    // This is a read-only fixture receipt, not admission or a rearm command.
    if(phase_!=Phase::Gesture||fault_!=Fault::None||attempts_!=1||edges_!=1||completed_!=1||
       !SameLifetime(lifetime_,n.owner)||n.owner!=lastReturnedOwner_||!lastReturnInput_||lastNeutralInput_<=lastReturnInput_||
       lastNeutralObservedNs_<=lastReturnObservedNs_)return {};
    const auto s=Sample(n,f);
    if(!Neutral(s)||s.input.generation<lastNeutralInput_||s.observedNs<lastNeutralObservedNs_)return {};
    TwoGrenadeNativeWitness w;w.observedNs=s.observedNs;w.deadlineNs=s.deadlineNs;w.input=s.input.generation;
    w.player=n.owner.player;w.soldier=n.owner.soldier;w.weak=n.owner.weak;w.actorGeneration=n.owner.actorGeneration;
    w.space=n.owner.space;w.weapon=n.owner.weapon;w.attempts=attempts_;w.edges=edges_;w.completed=completed_;
    w.returnInput=lastReturnInput_;w.returnObservedNs=lastReturnObservedNs_;
    w.neutralInput=lastNeutralInput_;w.neutralObservedNs=lastNeutralObservedNs_;
    w.gaps=gaps_;w.returnNoGaps=lastReturnNoGaps_;w.phase=unsigned(phase_);w.fault=unsigned(fault_);return w;
}
void Controller::Report(std::ostream& out)const {
    out<<"{\"phase\":"<<unsigned(phase_)<<",\"fault\":"<<unsigned(fault_)<<",\"attempts\":"<<attempts_<<",\"native_edges\":"<<edges_
       <<",\"completed\":"<<completed_<<",\"observation_gaps\":"<<gaps_<<",\"last_return_no_read_gaps\":"
       <<(completed_?(lastReturnNoGaps_?"true":"false"):"null")
       <<",\"last_return_input\":"<<lastReturnInput_<<",\"last_return_observed_ns\":"<<lastReturnObservedNs_
       <<",\"last_neutral_input\":"<<lastNeutralInput_<<",\"last_neutral_observed_ns\":"<<lastNeutralObservedNs_<<'}';
}
}
