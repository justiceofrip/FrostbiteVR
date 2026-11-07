#include "Bc2ChestKnifeCoordinator.h"
#include "Bc2KnifeMotionGeometry.h"
#include "Bc2SelectedKnifeMotion.h"
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
bool Bc2ChestKnifeCoordinator::TickIdleHeldGun(const BodyDrawSample& s,const Bc2BodyHolster& body,
    HandInteraction& hands,std::uint64_t& intent)noexcept {
#ifdef FVR_BC2_CHEST_KNIFE_PROBE
    return false; // The probe publishes a full witness for each gather.
#else
    if(body.Phase()!=BodyHolsterPhase::Held||body.TemporaryToolActive()||bodyOperation_||anchor_||
       nativeOwned_||policy_.Hold()||policy_.Phase()!=KnifePhase::NeedNeutral||intent==UINT64_MAX)return false;
    // Held has no suppression demand or free hand. Keep the actual source so
    // actor changes and recenters still reach policy before the next chest draw.
    KnifeSample idle;
    idle.source=s.hand;
    idle.menu=s.input.menu;
    if(!chestKnifeNative::TryIdleHeldGun(idle))return false;
    InvalidateLiveObservation();
    rendering_.reset();
    idle.claimIntent=++intent;
    policy_.Prepare(idle,hands);
    return true;
#endif
}
void Bc2ChestKnifeCoordinator::Missing(std::int64_t now,HandInteraction& hands)noexcept {
    InvalidateLiveObservation();
    rendering_.reset();PrepareChestKnifeMissing(now,hands,policy_,[&](const KnifeSample& sample,ChestKnifeCustody& policy)noexcept{
        const bool okay=chestKnifeNative::BeforePrepare(sample,policy);
        if(!okay)recognition_.NativeGate(sample.source,unsigned(policy.Phase()));return okay;});
}
void Bc2ChestKnifeCoordinator::Tick(const WeaponModeMemory& memory,const BodyDrawSample& s,BodyHolsterSample post,
    const BodyHolsterResult& bodyResult,const std::optional<ChestKnifeRenderReceipt>& rendered,bool mechanics,
    Bc2BodyInventory& inventory,Bc2BodyHolster& body,HandInteraction& hands,std::uint64_t& intent,const std::optional<math::Matrix4>& currentBody)noexcept {
    // A worker may not reuse the preceding gather while current gates change.
    liveObservation_.Begin();
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
    std::optional<selectedKnife::Geometry> attackGeometry;
    KnifeSample sample;sample.source=s.hand;sample.menu=s.input.menu;
    if(key)sample.knife={key->id,key->generation};
    sample.holdInputCurrent=ChestKnifeControlInputCurrent(s.input)&&
        s.interaction==BodyInteractionState::Available&&!s.cancel&&!s.reloadBusy;
    sample.controlsNeutral=sample.holdInputCurrent&&ChestKnifeControlsNeutral(s.input)&&
        s.input.hands[0].squeeze<=.35f&&!hands.Current(InteractionHand::Left);
    sample.actionInputCurrent=sample.holdInputCurrent&&ChestKnifeActionInputCurrent(s.input)&&
        !hands.Current(InteractionHand::Left);
    sample.stockTriggerInputCurrent=sample.holdInputCurrent&&ChestKnifeStockTriggerInputCurrent(s.input)&&
        !hands.Current(InteractionHand::Left);
    sample.stockTrigger=s.input.hands[1].trigger;
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
        sample.stockTriggerReady=native->stockTriggerEnabled&&!native->attackBridgeEnabled&&
            sample.trackedKnifeOwned&&sample.mechanicsSettled;
        if(sample.trackedKnifeOwned)sample.heldContact={{0x4243324b47524950ull,key->generation},s.hand.sequence,
            (std::min)(s.hand.deadlineNs,rendered->source.input.deadlineNs),true};
        // Raw original grip in one fixed tracking epoch, never rendered/head
        // motion. Native and shared hand admission are checked again at Stage.
        if(sample.trackedKnifeOwned&&s.input.hands[1].gripTracked)
        if(const auto motion=private_knife::KnifeMotionFromTrackedGrip(s.input,rendered->wristInGrip)){
            sample.tip=motion->tip;sample.direction=motion->direction;
            if(native->attackBridgeEnabled&&currentBody)
                attackGeometry=selectedKnife::MapTrackedGeometry(s.input,rendered->wristInGrip,*currentBody);
            sample.nativeAttackReady=native->motionEnabled&&sample.trackedKnifeOwned&&sample.mechanicsSettled&&
                (!native->attackBridgeEnabled||attackGeometry.has_value());
        }
    }else ++readGaps_;
    if(!chestKnifeNative::BeforePrepare(sample,policy_)){InvalidateLiveObservation();recognition_.NativeGate(sample.source,unsigned(policy_.Phase()));return;}
    std::optional<chestKnifeNative::ReturnCompletion> completedReturn;
    if(const auto receipt=chestKnifeNative::Observe(s.owner,body,bodyResult.freeRight,s.hand.nowNs,&completedReturn)){
        const bool accepted=policy_.Observe(*receipt);
        if(accepted&&receipt->kind==KnifeCommandKind::Draw){++draws_;drawReceipt_=*receipt;}
        // Record the exact native/policy join before originals reset. History
        // never feeds Prepare, rendering, suppression or session retirement.
        if(completedReturn) returnObservation_.Publish(*completedReturn,*receipt,accepted,
            originalGun_,drawCommand_,drawReceipt_,bodyOperation_,draws_,returns_+(accepted?1:0),s.hand.nowNs);
        if(accepted&&receipt->kind==KnifeCommandKind::Return){++returns_;nativeOwned_=false;anchor_.reset();bodyOperation_=0;}
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
            if(bodyOkay){anchor_=native->anchor;bodyOperation_=operation;
                originalGun_=inventory.ItemKey(native->anchor.original.client.owner.weapon);
                drawCommand_=*command;drawReceipt_.reset();}
        }
        if(!bodyOkay||!anchor_||!chestKnifeNative::Stage(*command,sample,hands,policy_,*anchor_,s.owner,bodyOperation_,
            attackGeometry?std::optional(attackGeometry->nativeAcquisition):std::nullopt,s.input.worldUnitsPerMeter,
            rendered?(std::min)(command->source.deadlineNs,rendered->source.input.deadlineNs):0)){
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
    // Only explicit live-snapshot requests copy these already evaluated facts.
    // No native inspection, policy transition, claim or source lease is added.
    if(!liveObservation_.Requested(s.hand.nowNs)){InvalidateLiveObservation();return;}
    const auto held=hands.Current(InteractionHand::Right);
    const auto original=native?inventory.ItemKey(native->anchor.original.client.owner.weapon):std::nullopt;
    if(!native||!rendered||!key||!original||!held||!post.suppression||
       native->snapshot.selectedOwner!=s.owner){InvalidateLiveObservation();return;}
    ChestKnifeLiveObservation observation;
    observation.owner=s.owner;observation.originalOwner=native->anchor.original.client.owner;
    observation.knife=*key;observation.originalGun=*original;observation.input=s.hand;observation.controls=s.input;
    observation.hold=*held;observation.rendered=*rendered;
    observation.bodyOperation=bodyOperation_;observation.draws=draws_;observation.returns=returns_;
    observation.nativeSequence=native->snapshot.sequence;observation.nativeObservedNs=native->snapshot.observedNs;
    observation.nativeTick=post.suppression->nativeTick;observation.suppressionRequest=post.suppression->request;
    observation.knifeSlot=native->anchor.knifeSlot;observation.selectedSlots=native->snapshot.selected.current;
    observation.phase=policy_.Phase();observation.nativeOwned=nativeOwned_;observation.nativeHealthy=native->healthy;
    observation.nativeObservationComplete=native->observationComplete;observation.stockTriggerEnabled=native->stockTriggerEnabled;
    observation.motionEnabled=native->motionEnabled;observation.attackBridgeEnabled=native->attackBridgeEnabled;
    observation.held=poseCustody&&policy_.Hold()&&held->token==*policy_.Hold()&&sample.trackedKnifeOwned&&rendering_.has_value();
    observation.stockTriggerReady=sample.stockTriggerReady;observation.stockTriggerArmed=policy_.StockTriggerArmedFor(sample,hands);
    observation.fireSuppressed=sample.fireSuppressed;observation.mechanicsSettled=sample.mechanicsSettled;
    liveObservation_.Publish(observation,s.hand.nowNs);
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
