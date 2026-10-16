#include "Bc2ChestKnifeCoordinator.h"
#include "Bc2KnifeMotionGeometry.h"
#include "Bc2ChestKnifeMissing.h"
#include "Bc2ChestKnifeInput.h"
#include <cmath>
namespace fvr::bc2 {namespace {
using namespace interaction;
bool Ready(const private_knife::Item& item)noexcept {
    for(const auto* state:{&item.client.branches[0],&item.client.branches[1],&item.server})
        if(state->currentState!=2||state->nextState!=2||!std::isfinite(state->phaseTimer)||state->phaseTimer<0||
           state->phaseTimer>1000000||(state->flagsA8&8))return false;
    return true;
}

}
Bc2ChestKnifeCoordinator::Bc2ChestKnifeCoordinator()noexcept {
#ifdef FVR_BC2_CHEST_KNIFE_PROBE
    recognition_.Enable();policy_.ObserveRecognition(&recognition_);
#endif
}
void Bc2ChestKnifeCoordinator::Missing(std::int64_t now,HandInteraction& hands)noexcept {
    rendering_.reset();PrepareChestKnifeMissing(now,hands,policy_,[&](const KnifeSample& sample,ChestKnifeCustody& policy)noexcept{
        const bool okay=chestKnifeNative::BeforePrepare(sample,policy);
        if(!okay)recognition_.NativeGate(sample.source,unsigned(policy.Phase()));return okay;});
}
void Bc2ChestKnifeCoordinator::Tick(const WeaponModeMemory& memory,const BodyDrawSample& s,BodyHolsterSample post,
    const BodyHolsterResult& bodyResult,const std::optional<ChestKnifeRenderReceipt>& rendered,bool mechanics,
    Bc2BodyInventory& inventory,Bc2BodyHolster& body,HandInteraction& hands,std::uint64_t& intent)noexcept {
    rendering_.reset();chestKnifeNative::ObserveLifetime(s.owner);chestKnifeNative::AcknowledgeNoEffectRehide(body);
    if(anchor_&&(anchor_->original.client.owner.player!=s.owner.player||anchor_->original.client.owner.soldier!=s.owner.soldier||
       anchor_->original.client.owner.weak!=s.owner.weak||anchor_->original.client.owner.actorGeneration!=s.owner.actorGeneration)){
        anchor_.reset();bodyOperation_=0;nativeOwned_=false;
    }
    if(bodyOperation_&&body.LastTemporaryToolReturn()==bodyOperation_&&!body.TemporaryToolActive()&&!nativeOwned_){anchor_.reset();bodyOperation_=0;}
    const auto display=inventory.Display(s.hand.nowNs);unsigned knife=anchor_?anchor_->knifeWeapon:0;
    if(!knife&&display)for(unsigned n=0;n<display->carried.count;++n)if(display->carried.items[n].category==11){
        if(knife){knife=0;break;}knife=display->carried.items[n].weapon;}
    const auto key=knife?inventory.ItemKey(knife):std::nullopt;
    const auto native=key?chestKnifeNative::Inspect(s.owner,knife,s.hand.sequence,s.hand.nowNs):std::nullopt;
    KnifeSample sample;sample.source=s.hand;sample.menu=s.input.menu;
    if(key)sample.knife={key->id,key->generation};
    sample.holdInputCurrent=ChestKnifeControlInputCurrent(s.input)&&
        s.interaction==BodyInteractionState::Available&&!s.cancel&&!s.reloadBusy;
    sample.controlsNeutral=sample.holdInputCurrent&&ChestKnifeControlsNeutral(s.input)&&
        s.input.hands[0].squeeze<=.35f&&!hands.Current(InteractionHand::Left);
    sample.actionInputCurrent=sample.holdInputCurrent&&ChestKnifeActionInputCurrent(s.input)&&
        !hands.Current(InteractionHand::Left);
    sample.onFoot=bool(native);sample.emptyHands=body.Phase()==BodyHolsterPhase::Empty&&bodyResult.freeRight.has_value();
    post.nativeOwner=s.owner;post.hand=s.hand;
    const auto demand=body.Demand(post);
    sample.fireSuppressed=demand&&post.suppression&&HolsterSuppressionCurrent(*post.suppression,*demand);
    sample.handSupported[1]=authored_grip_detail::Profile(ChestKnifeGripProfiles()[0]);
    for(unsigned h=0;h<2;++h)sample.gripHeld[h]=(s.input.hands[h].active&Squeeze)&&s.input.hands[h].squeeze>=.65f;
    if(const auto point=BodyAnchorHandPose(s.input,InteractionHand::Right))
        sample.chest[1]={{0x4243324b43485354ull,1},s.hand.sequence,s.hand.deadlineNs,BodyAnchorContains(private_knife::KnifeChest,*point)};
    if(native){
        nativeOwned_=native->owned;sample.knifeSelected=s.owner.weapon==knife;
        for(auto selected:native->snapshot.selected.current)sample.knifeSelected&=selected==native->anchor.knifeSlot;
        sample.mechanicsSettled=mechanics&&native->healthy&&(!nativeOwned_||body.TemporaryToolOperation()==bodyOperation_)&&
            private_knife::KnifeSentinel(native->snapshot)&&
            Ready(sample.knifeSelected?native->snapshot.knife:native->snapshot.original);
        sample.trackedKnifeOwned=sample.knifeSelected&&nativeOwned_&&bodyOperation_&&body.TemporaryToolOperation()==bodyOperation_&&rendered&&
            rendered->source.bodyOperation==bodyOperation_&&rendered->source.knife==*key&&
            rendered->rigFingerprint==ChestKnifeGripProfiles()[0].rigFingerprint&&
            ChestKnifeRenderCurrent(rendered->source,s.owner,s.hand,s.hand.nowNs);
        if(sample.trackedKnifeOwned)sample.heldContact={{0x4243324b47524950ull,key->generation},s.hand.sequence,
            (std::min)(s.hand.deadlineNs,rendered->source.input.deadlineNs),true};
        // Raw original grip in one fixed tracking epoch, never rendered/head
        // motion. Native and shared hand admission are checked again at Stage.
        if(sample.trackedKnifeOwned&&s.input.hands[1].gripTracked)
        if(const auto motion=private_knife::KnifeMotionFromGrip(s.input.hands[1].grip,rendered->wristInGrip)){
            sample.tip=motion->tip;sample.direction=motion->direction;
            sample.nativeAttackReady=native->motionEnabled&&sample.trackedKnifeOwned&&sample.mechanicsSettled;
        }
    }else ++readGaps_;
    if(!chestKnifeNative::BeforePrepare(sample,policy_)){recognition_.NativeGate(sample.source,unsigned(policy_.Phase()));return;}
    if(const auto receipt=chestKnifeNative::Observe(s.owner,body,bodyResult.freeRight,s.hand.nowNs)){
        if(policy_.Observe(*receipt)){
            if(receipt->kind==KnifeCommandKind::Draw)++draws_;
            if(receipt->kind==KnifeCommandKind::Return){++returns_;nativeOwned_=false;anchor_.reset();bodyOperation_=0;}
        }
    }
    if(const auto rehide=chestKnifeNative::ReturnNeedsRehide())body.RequestTemporaryToolRehide(*rehide);
    if(intent==UINT64_MAX){Missing(s.hand.nowNs,hands);return;}sample.claimIntent=++intent;
    const auto command=policy_.Prepare(sample,hands);
    if(command){
        bool bodyOkay=true;
        if(command->kind==KnifeCommandKind::Draw){
            const auto operation=body.NextTemporaryToolOperation();
            bodyOkay=operation&&native&&key&&bodyResult.freeRight&&policy_.Hold()&&
                inventory.BeginTemporaryTool(memory,s,post,body,*key,operation,*bodyResult.freeRight,*policy_.Hold(),hands);
            if(bodyOkay){anchor_=native->anchor;bodyOperation_=operation;}
        }
        if(!bodyOkay||!anchor_||!chestKnifeNative::Stage(*command,sample,hands,policy_,*anchor_,s.owner,bodyOperation_)){
            policy_.Committed(*command,KnifeCommit::RejectedNoEffect);++rejected_;
            // Stage(false) proves no first native chooser can execute. A body
            // interval already acquired still requires a NEW paired hide.
            if(command->kind==KnifeCommandKind::Draw&&bodyOkay)body.RequestTemporaryToolRehide(bodyOperation_);
        }
    }
    const bool poseCustody=ChestKnifePoseCustody(policy_,hands,s.hand);
    if(poseCustody&&native&&native->healthy&&nativeOwned_&&bodyOperation_&&body.TemporaryToolOperation()==bodyOperation_&&sample.knifeSelected&&
       sample.holdInputCurrent&&sample.fireSuppressed&&GameplayOwnsInput(sample.menu,s.hand.nowNs)&&
       sample.gripHeld[1]&&!s.hand.released[1]&&s.hand.focused&&s.hand.tracked[1]&&s.hand.deadlineNs>s.hand.nowNs)
        rendering_=ChestKnifeRenderSource{s.owner,s.hand,*key,bodyOperation_};
#ifdef FVR_BC2_CHEST_KNIFE_PROBE
    ChestKnifeProbeWitness witness;
    witness.observedNs=std::uint64_t(s.hand.observedNs);witness.deadlineNs=std::uint64_t(s.hand.deadlineNs);
    witness.input=s.hand.sequence;witness.space=s.hand.owner.space;
    witness.actor=s.hand.owner.actor;witness.actorGeneration=s.hand.owner.actorGeneration;witness.weapon=s.owner.weapon;
    witness.bodyOperation=body.TemporaryToolOperation();witness.draws=draws_;witness.returns=returns_;
    witness.phase=unsigned(policy_.Phase());witness.nativeOwned=nativeOwned_;
    witness.empty=sample.emptyHands&&!body.TemporaryToolActive()&&!policy_.Hold()&&!nativeOwned_&&
        sample.controlsNeutral&&sample.fireSuppressed&&GameplayOwnsInput(sample.menu,s.hand.nowNs)&&native&&native->healthy;
    witness.held=policy_.Hold()&&policy_.Hold()->kind==HandClaimKind::GunHold&&poseCustody&&sample.trackedKnifeOwned&&native&&native->healthy&&
        sample.gripHeld[1]&&sample.holdInputCurrent&&sample.fireSuppressed&&GameplayOwnsInput(sample.menu,s.hand.nowNs);
    witness.attackReady=witness.held&&KnifeActionInputCurrent(sample)&&sample.nativeAttackReady;
    witness.recognizerReady=witness.attackReady&&policy_.RecognitionReady(sample.source);
    if(witness.held&&rendered)witness.wristInGrip=rendered->wristInGrip;
    if(!witnessGate_.test_and_set(std::memory_order_acquire)){witness_=witness;witnessGate_.clear(std::memory_order_release);}
#endif
}
std::optional<ChestKnifeProbeWitness> Bc2ChestKnifeCoordinator::ProbeWitness(std::int64_t now)const noexcept {
    if(now<=0||witnessGate_.test_and_set(std::memory_order_acquire))return {};
    const auto copy=witness_;witnessGate_.clear(std::memory_order_release);
    if(copy.input&&copy.observedNs<=std::uint64_t(now)&&copy.deadlineNs>std::uint64_t(now))return copy;
    return {};
}
void Bc2ChestKnifeCoordinator::Report(std::ostream& out)const {
    out<<"{\"phase\":"<<unsigned(policy_.Phase())<<",\"draw_receipts\":"<<draws_<<",\"return_receipts\":"<<returns_
       <<",\"rejected_stages\":"<<rejected_<<",\"read_gaps\":"<<readGaps_<<",\"body_operation\":"<<bodyOperation_<<",\"recognition\":";recognition_.Report(out);out<<'}';
}
}
