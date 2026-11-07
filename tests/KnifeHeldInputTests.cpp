#include "Test.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeInput.h"
#include "Bc2KnifeAttackGate.h"
#include "Bc2KnifeStockTriggerSource.h"
#include "Bc2ChestKnifeStockHold.h"
#include "Bc2KnifeStockMotionSource.h"
#include "NativeProbeConfig.h"
#include <cstdio>
using namespace fvr::interaction;
using namespace fvr::bc2;
#include "KnifeCustodyFixture.inc"

int MovingHoldAndQuietRearm(){
    Fixture f;CHECK(f.Held());const auto token=*f.knife.Hold();
    f.s.controlsNeutral=false;f.s.holdInputCurrent=true;
    // Actual shared claim outlives twenty original 100ms leases by renewing
    // only from each NEW source. The old global quiet gate fails first renewal.
    for(unsigned n=0;n<100;++n){f.s.tip.z+=.015f;CHECK(!f.Step(20*ms));
        const auto held=f.hands.Current(InteractionHand::Right);
        CHECK(held&&held->token==token&&held->inputSequence==f.s.source.sequence);
        CHECK(held->deadlineNs==f.s.source.deadlineNs&&ChestKnifePoseCustody(f.knife,f.hands,f.s.source));
        CHECK(f.knife.Phase()==KnifePhase::Held&&!f.knife.RecognitionReady(f.s.source));}
    f.s.controlsNeutral=true;CHECK(!f.Step());CHECK(!f.knife.RecognitionReady(f.s.source));
    CHECK(f.Rest());CHECK(f.knife.RecognitionReady(f.s.source));
    const auto attack=f.Stab();CHECK(attack&&attack->kind==KnifeCommandKind::Attack);
    CHECK(f.knife.CanCommit(*attack,f.s,f.hands));
    f.s.controlsNeutral=false;CHECK(!f.knife.CanCommit(*attack,f.s,f.hands));
    f.s.controlsNeutral=true;f.knife.Committed(*attack,KnifeCommit::Accepted);
    // Native windup/recovery can span movement. No additional attack is staged.
    f.s.controlsNeutral=false;
    for(unsigned n=0;n<65;++n){CHECK(!f.Step(20*ms));const auto held=f.hands.Current(InteractionHand::Right);
        CHECK(held&&held->token==token&&held->inputSequence==f.s.source.sequence);
        CHECK(f.knife.Phase()==KnifePhase::Attacking);}
    CHECK(f.knife.Observe(f.Receipt(*attack)));CHECK(f.knife.Phase()==KnifePhase::Held);
    f.Grip(false);CHECK(!f.Step());CHECK(f.knife.NeedsNativeCleanup());
    f.s.controlsNeutral=true;const auto returned=f.Step();CHECK(returned&&returned->kind==KnifeCommandKind::Return);
    CHECK(f.knife.CanCommit(*returned,f.s,f.hands));f.knife.Committed(*returned,KnifeCommit::Accepted);
    CHECK(f.knife.Observe(f.Receipt(*returned)));CHECK(f.knife.Phase()==KnifePhase::NeedNeutral&&!f.knife.Hold());
    return 0;
}
int DrawStillNeedsNeutral(){
    Fixture f;f.s.holdInputCurrent=true;f.s.controlsNeutral=false;
    CHECK(!f.Step());f.Grip(true);CHECK(!f.Step());CHECK(!f.knife.Hold());
    f.s.controlsNeutral=true;CHECK(!f.Step());CHECK(!f.knife.Hold());
    f.Grip(false);CHECK(!f.Step());f.Grip(true);const auto draw=f.Step();
    CHECK(draw&&draw->kind==KnifeCommandKind::Draw&&f.knife.CanCommit(*draw,f.s,f.hands));return 0;
}
int MissingAuthorityNeverBecomesHeldInput(){
    for(unsigned n=0;n<7;++n){Fixture f;CHECK(f.Held());const auto token=*f.knife.Hold();
        f.s.controlsNeutral=false;f.s.holdInputCurrent=true;
        if(n==0)f.s.source.focused=false;
        if(n==1)f.s.source.tracked[1]=false;
        if(n==2)f.s.menu.owner=InputMenuOwner::Menu;
        if(n==3)f.s.holdInputCurrent=false;
        if(n==4)f.s.onFoot=false;
        if(n==5)f.s.fireSuppressed=false;
        if(n==6)++f.s.source.owner.space;
        CHECK(!f.Step(110*ms));CHECK(!f.hands.Current(InteractionHand::Right));
        f.s.controlsNeutral=true;f.s.holdInputCurrent=true;f.s.source.focused=true;f.s.source.tracked[1]=true;
        f.s.menu.owner=InputMenuOwner::Gameplay;f.s.onFoot=f.s.fireSuppressed=true;
        CHECK(!f.Step());CHECK(!f.hands.Current(InteractionHand::Right));
        CHECK(!f.knife.Hold()||f.knife.Hold()->id==token.id); // No held reacquisition.
    }
    Fixture changed;CHECK(changed.Held());const auto token=*changed.knife.Hold();
    ++changed.s.source.owner.actorGeneration;changed.s.controlsNeutral=false;changed.s.holdInputCurrent=true;
    CHECK(!changed.Step());CHECK(!changed.hands.Current(InteractionHand::Right));
    CHECK(!changed.knife.Hold()||changed.knife.Hold()->id!=token.id);return 0;
}
int RealRawInputSeparatesHoldAndIntent(){
    InputFrame in;in.generation=1;in.spaceGeneration=1;in.predictedNs=1000000000;
    in.focused=in.headValid=true;
    for(auto& hand:in.hands){hand.gripTracked=hand.aimTracked=true;
        hand.active=Stick|Trigger|Squeeze|Primary|Secondary|StickClick;}
    in.hands[1].squeeze=1;
    CHECK(ChestKnifeControlInputCurrent(in)&&ChestKnifeControlsNeutral(in));
    for(unsigned n=0;n<5;++n){auto moving=in;
        if(n==0)moving.hands[0].stickY=.8f;
        if(n==1)moving.hands[1].stickX=.8f;
        if(n==2)moving.hands[0].held=Secondary;
        if(n==3)moving.hands[0].held=StickClick;
        if(n==4)moving.hands[0].trigger=.8f;
        CHECK(ChestKnifeControlInputCurrent(moving)&&!ChestKnifeControlsNeutral(moving));}
    auto invalid=in;invalid.hands[1].active&=~Squeeze;invalid.hands[1].squeeze=0;
    CHECK(!ChestKnifeControlInputCurrent(invalid));invalid=in;invalid.hands[0].gripTracked=false;
    CHECK(!ChestKnifeControlInputCurrent(invalid));invalid=in;invalid.hands[1].aimTracked=false;
    CHECK(!ChestKnifeControlInputCurrent(invalid));invalid=in;invalid.focused=false;
    CHECK(!ChestKnifeControlInputCurrent(invalid));return 0;
}
int FocusGapReturnsBeforeFreshChestDraw(){
    for(unsigned variant=0;variant<2;++variant){const bool attacking=variant!=0;
        Fixture f;CHECK(f.Held());const auto oldHold=*f.knife.Hold();
        std::optional<KnifeCommand> attack;
        if(attacking){CHECK(f.Rest());attack=f.Stab();CHECK(attack&&f.knife.CanCommit(*attack,f.s,f.hands));
            f.knife.Committed(*attack,KnifeCommit::Accepted);}
        const auto beforeGap=f.s.source;
        f.s.source.focused=false;CHECK(!f.Step(5000*ms));CHECK(!f.hands.Current(InteractionHand::Right));
        // Returning with the same real item selected does not resurrect the
        // expired token or auto-draw just because the grip is still down.
        f.s.source.focused=true;f.s.holdInputCurrent=true;++f.s.menu.epoch;
        CHECK(!f.Step());CHECK(!f.hands.Current(InteractionHand::Right));
        if(attack){CHECK(f.knife.Phase()==KnifePhase::Attacking);
            CHECK(f.knife.Observe(f.Receipt(*attack)));} // Actual settled native receipt required.
        CHECK(f.knife.NeedsNativeCleanup());
        f.Grip(false);const auto returned=f.Step();CHECK(returned&&returned->kind==KnifeCommandKind::Return);
        CHECK(returned->source.sequence>beforeGap.sequence&&returned->source.observedNs>beforeGap.deadlineNs&&
            f.knife.CanCommit(*returned,f.s,f.hands));
        auto foreign=f.s;++foreign.knife.generation;CHECK(!f.knife.CanCommit(*returned,foreign,f.hands));
        f.knife.Committed(*returned,KnifeCommit::Accepted);CHECK(f.knife.Phase()==KnifePhase::Returning);
        // Elapsed time cannot replace original-selection/new-hide receipt.
        CHECK(!f.Step(500*ms)&&f.knife.Phase()==KnifePhase::Returning);
        auto incomplete=f.Receipt(*returned);incomplete.originalSelectedHidden=false;
        CHECK(!f.knife.Observe(incomplete));CHECK(f.knife.Observe(f.Receipt(*returned)));
        f.s.emptyHands=true;f.s.knifeSelected=f.s.trackedKnifeOwned=false;
        CHECK(!f.Step());CHECK(!f.Step());CHECK(!f.knife.Hold());
        f.Grip(true);const auto draw=f.Step();CHECK(draw&&draw->kind==KnifeCommandKind::Draw);
        CHECK(draw->serial>returned->serial&&f.knife.Hold()&&f.knife.Hold()->id!=oldHold.id);
    }return 0;
}
int DuplicateCannotWidenOrReacquire(){
    Fixture f;CHECK(f.Held());f.s.controlsNeutral=false;f.s.holdInputCurrent=true;CHECK(!f.Step());
    const auto token=*f.knife.Hold();auto shorter=f.s.heldContact;shorter.deadlineNs-=30*ms;
    CHECK(f.hands.Renew(f.s.source,token,shorter).accepted);
    CHECK(!f.knife.Prepare(f.s,f.hands));
    CHECK(f.hands.Current(InteractionHand::Right)->deadlineNs==shorter.deadlineNs);
    f.s.source.nowNs=shorter.deadlineNs;f.hands.Update(f.s.source);
    CHECK(!f.hands.Current(InteractionHand::Right)&&!f.knife.Prepare(f.s,f.hands));
    CHECK(!f.hands.Current(InteractionHand::Right));return 0;
}
int WalkingStrokeUsesSameNativeBoundaryPredicate(){
    Fixture f;CHECK(f.Held());
    InputFrame input;input.generation=1;input.spaceGeneration=1;input.predictedNs=1000000000;
    input.focused=input.headValid=true;
    for(auto& h:input.hands){h.active=Stick|Trigger|Squeeze|Primary|Secondary|StickClick;h.gripTracked=h.aimTracked=true;}
    input.hands[1].squeeze=1;input.hands[0].stickY=.8f;input.hands[1].stickX=.8f;
    input.hands[0].held=Secondary|StickClick;input.hands[1].held=Primary;
    CHECK(!ChestKnifeControlsNeutral(input)&&ChestKnifeActionInputCurrent(input));
    f.s.controlsNeutral=false;f.s.holdInputCurrent=true;f.s.actionInputCurrent=ChestKnifeActionInputCurrent(input);
    CHECK(f.Rest());const auto attack=f.Stab();CHECK(attack&&attack->kind==KnifeCommandKind::Attack);
    CHECK(f.knife.CanCommit(*attack,f.s,f.hands)&&KnifeCommandInputCurrent(*attack,f.s));
    for(unsigned n=0;n<6;++n){auto bad=input;
        if(n==0)bad.hands[0].held|=Primary; // Use
        if(n==1)bad.hands[1].held|=Secondary; // Reload
        if(n==2)bad.hands[1].held|=StickClick; // Recenter/another gesture
        if(n==3)bad.hands[1].trigger=1;
        if(n==4)bad.hands[0].squeeze=1;
        if(n==5)bad.focused=false;
        auto sample=f.s;sample.actionInputCurrent=ChestKnifeActionInputCurrent(bad);
        CHECK(!sample.actionInputCurrent&&!f.knife.CanCommit(*attack,sample,f.hands)&&!KnifeCommandInputCurrent(*attack,sample));}
    f.knife.Committed(*attack,KnifeCommit::Accepted);
    for(unsigned n=0;n<65;++n)CHECK(!f.Step(20*ms));
    CHECK(f.knife.Observe(f.Receipt(*attack)));f.Grip(false);const auto returned=f.Step();
    CHECK(returned&&returned->kind==KnifeCommandKind::Return&&f.knife.CanCommit(*returned,f.s,f.hands));
    CHECK(KnifeCommandInputCurrent(*returned,f.s));return 0;
}
int IdleGunNeedsANewEmptyHandNeutral(){
    Fixture f;
    CHECK(!f.Step()&&f.knife.Phase()==KnifePhase::Stowed);
    // The first Held tick still takes the full path and loses suppression.
    f.s.fireSuppressed=false;f.s.emptyHands=false;
    CHECK(!f.Step()&&f.knife.Phase()==KnifePhase::NeedNeutral);
    // Subsequent Held ticks publish only the original source/menu.
    KnifeSample heldGun;heldGun.source=f.s.source;heldGun.menu=f.s.menu;
    for(unsigned tick=0;tick<60;++tick){
        ++heldGun.source.sequence;heldGun.source.observedNs+=10*ms;
        heldGun.source.nowNs=heldGun.source.observedNs;
        heldGun.menu.deadlineNs=heldGun.source.deadlineNs=heldGun.source.nowNs+100*ms;
        ++heldGun.menu.sequence;
        if(tick==20)++heldGun.source.owner.actorGeneration;
        if(tick==40)++heldGun.source.owner.space;
        f.hands.Update(heldGun.source);
        CHECK(!f.knife.Prepare(heldGun,f.hands));
        CHECK(f.knife.Phase()==KnifePhase::NeedNeutral&&!f.knife.Hold());
    }
    f.s.source=heldGun.source;f.s.menu=heldGun.menu;
    f.s.emptyHands=f.s.fireSuppressed=true;
    f.Grip(true);CHECK(!f.Step()&&f.knife.Phase()==KnifePhase::NeedNeutral);
    f.Grip(false);CHECK(!f.Step()&&f.knife.Phase()==KnifePhase::Stowed);
    f.Grip(true);const auto draw=f.Step();
    CHECK(draw&&draw->kind==KnifeCommandKind::Draw&&f.knife.CanCommit(*draw,f.s,f.hands));
    return 0;
}
int StockTriggerRevocationCases(){
    using private_knife::StockKnifeSourceRevocation;
    // Each loss keeps the original packet identity. The failed-lock seam
    // deliberately leaves the native locked sample unchanged after Advance.
    for(unsigned loss=0;loss<18;++loss){Fixture f;CHECK(f.Held());f.s.nativeAttackReady=false;
        f.s.stockTriggerReady=f.s.stockTriggerInputCurrent=true;CHECK(!f.Step()&&!f.Step());
        f.s.controlsNeutral=f.s.actionInputCurrent=false;f.s.holdInputCurrent=true;f.s.stockTrigger=1;
        const auto attack=f.Step();CHECK(attack&&attack->stockTriggerHold);
        f.s.source.nowNs+=2*ms;CHECK(!f.knife.Prepare(f.s,f.hands));
        StockKnifeSourceRevocation source;CHECK(!source.Advance(f.s));const auto stamp=source.Stamp();
        private_knife::OriginalKnifeSource original;CHECK(original.Advance(f.s.source));
        private_knife::KnifeAttackGate pending,accepted;CHECK(pending.Stage(*attack,f.s.source.nowNs));
        CHECK(accepted.Stage(*attack,f.s.source.nowNs)&&accepted.Consume(1,f.s.source.nowNs,source.Current(stamp)));
        const auto lockedSample=f.s;auto lost=f.s;bool returnWanted=false;
        if(loss==0)lost.stockTriggerReady=false;
        if(loss==1)lost.stockTriggerInputCurrent=false;
        if(loss==2)lost.trackedKnifeOwned=false;
        if(loss==3)lost.knifeSelected=false;
        if(loss==4)lost.fireSuppressed=false;
        if(loss==5)lost.onFoot=false;
        if(loss==6)lost.mechanicsSettled=false;
        if(loss==7)lost.holdInputCurrent=false;
        if(loss==8)lost.stockTrigger=0;
        if(loss==9)lost.stockTrigger=.9f;
        if(loss==10)lost.source.nowNs--;
        if(loss==11)lost.heldContact.eligible=false;
        if(loss==12)lost.heldContact.deadlineNs=lost.source.nowNs;
        if(loss==13)lost.menu.owner=InputMenuOwner::Menu;
        if(loss==14)++lost.menu.epoch;
        if(loss==15)returnWanted=true;
        if(loss==16)lost.stockTrigger=std::numeric_limits<float>::quiet_NaN();
        if(loss==17)lost.gripHeld[1]=false;
        CHECK(!original.Advance(lost.source)); // The draw/return epoch is unchanged.
        CHECK(source.Advance(lost,returnWanted)&&!source.Current(stamp));
        CHECK(KnifeCommandInputCurrent(*attack,lockedSample)); // Native sample can still look valid.
        CHECK(!pending.Consume(1,lockedSample.source.nowNs,source.Current(stamp)));
        source.Advance(lockedSample);CHECK(!source.Current(stamp)); // 1 -> 0 -> 1 cannot revive.
        CHECK(!pending.Consume(2,lockedSample.source.nowNs,source.Current(stamp)));
        CHECK(accepted.ResolveBeforeSource(attack->source.sequence,attack->source.observedNs,lockedSample.source.nowNs)==KnifeCommit::Accepted);
        CHECK(accepted.Consume(2,attack->source.deadlineNs+1,source.Current(stamp))); // Existing sibling survives.
        CHECK(!accepted.Consume(1,attack->source.deadlineNs+1,source.Current(stamp)));
        // Native retirement follows its existing new-source/expiry path.
        CHECK(pending.ResolveBeforeSource(attack->source.sequence+1,attack->source.observedNs+1,lockedSample.source.nowNs)==KnifeCommit::RejectedNoEffect);
        if(loss!=15){
            CHECK(!f.knife.Prepare(lost,f.hands));CHECK(!f.knife.CanCommit(*attack,lockedSample,f.hands));
            CHECK(!f.knife.Prepare(lockedSample,f.hands));CHECK(!f.knife.CanCommit(*attack,lockedSample,f.hands));
        }
    }
    // An unchanged duplicate and advancing processing time preserve the stage.
    {Fixture f;CHECK(f.Held());f.s.nativeAttackReady=false;
        f.s.stockTriggerReady=f.s.stockTriggerInputCurrent=true;CHECK(!f.Step()&&!f.Step());f.s.stockTrigger=1;
        const auto attack=f.Step();CHECK(attack);StockKnifeSourceRevocation source;CHECK(!source.Advance(f.s));
        const auto stamp=source.Stamp();++f.s.source.nowNs;
        CHECK(!source.Advance(f.s)&&source.Current(stamp));
        CHECK(!f.knife.Prepare(f.s,f.hands)&&!f.knife.Prepare(f.s,f.hands));CHECK(f.knife.CanCommit(*attack,f.s,f.hands));
        // A local rejected stage retains return-first custody, then a fresh
        // draw and neutral packet admit a new edge, never the rejected serial.
        const auto serial=attack->serial;f.s.stockTrigger=.9f;CHECK(!f.knife.Prepare(f.s,f.hands));
        f.s.stockTrigger=1;CHECK(!f.knife.Prepare(f.s,f.hands));CHECK(f.knife.NeedsNativeCleanup());
        f.s.stockTrigger=0;f.Grip(false);const auto returned=f.Step();CHECK(returned&&returned->kind==KnifeCommandKind::Return);
        CHECK(f.knife.CanCommit(*returned,f.s,f.hands));f.knife.Committed(*returned,KnifeCommit::Accepted);CHECK(f.knife.Observe(f.Receipt(*returned)));
        f.s.emptyHands=true;f.s.knifeSelected=f.s.trackedKnifeOwned=false;CHECK(f.Held());f.s.nativeAttackReady=false;
        CHECK(!f.Step()&&!f.Step());f.s.stockTrigger=1;const auto fresh=f.Step();CHECK(fresh&&fresh->stockTriggerHold&&fresh->serial>serial);
        source.Advance(f.s);const auto freshStamp=source.Stamp();private_knife::KnifeAttackGate gate;
        CHECK(gate.Stage(*fresh,f.s.source.nowNs)&&gate.Consume(1,f.s.source.nowNs,source.Current(freshStamp)));
    }
    std::puts("Stock revocation: same-packet authority/scalar loss, failed-lock seam, no restoration replay, fresh neutral recovery, accepted sibling survival.");
    return 0;
}

int StockTriggerCases(){
    InputFrame input;input.generation=1;input.spaceGeneration=1;input.predictedNs=1000000000;
    input.focused=input.headValid=true;
    for(auto& h:input.hands){h.active=Stick|Trigger|Squeeze|Primary|Secondary|StickClick;h.gripTracked=h.aimTracked=true;}
    input.hands[1].squeeze=input.hands[1].trigger=1;
    CHECK(ChestKnifeStockTriggerInputCurrent(input)&&!ChestKnifeActionInputCurrent(input));
    for(unsigned conflict=0;conflict<5;++conflict){auto bad=input;
        if(conflict==0)bad.hands[0].held=Primary;if(conflict==1)bad.hands[1].held=Secondary;
        if(conflict==2)bad.hands[0].trigger=1;if(conflict==3)bad.hands[0].squeeze=1;
        if(conflict==4)bad.focused=false;
        CHECK(!ChestKnifeStockTriggerInputCurrent(bad));}
    // Mode2 remains attack-disabled unless the independent stock capability is
    // explicitly supplied. No stroke is invented for the trigger cause.
    Fixture f;CHECK(f.Held());f.s.nativeAttackReady=false;
    f.s.stockTriggerReady=f.s.stockTriggerInputCurrent=true;
    CHECK(!f.Step()&&!f.Step());
    f.s.controlsNeutral=f.s.actionInputCurrent=false;f.s.holdInputCurrent=true;f.s.stockTrigger=1;
    const auto attack=f.Step();CHECK(attack&&attack->kind==KnifeCommandKind::Attack&&attack->stockTriggerHold&&!attack->stroke);
    CHECK(f.knife.CanCommit(*attack,f.s,f.hands));
    for(unsigned n=0;n<7;++n){auto bad=f.s;
        if(n==0)bad.stockTriggerReady=false;if(n==1)bad.stockTriggerInputCurrent=false;
        if(n==2)++bad.source.owner.equipGeneration;if(n==3)bad.trackedKnifeOwned=false;
        if(n==4)bad.source.nowNs=bad.source.deadlineNs;if(n==5)bad.stockTrigger=0;if(n==6)bad.fireSuppressed=false;
        CHECK(!f.knife.CanCommit(*attack,bad,f.hands));
        if(n==0||n==1||n==5)CHECK(!KnifeCommandInputCurrent(*attack,bad));}
    private_knife::KnifeAttackGate gate;CHECK(gate.Stage(*attack,f.s.source.nowNs));
    CHECK(!gate.Consume(0,f.s.source.nowNs,true)&&!gate.Consume(1,f.s.source.nowNs,false));
    CHECK(gate.Consume(1,f.s.source.nowNs,true)&&!gate.Consume(1,f.s.source.nowNs,true));
    CHECK(gate.ResolveBeforeSource(attack->source.sequence+1,attack->source.observedNs+1,f.s.source.nowNs)==KnifeCommit::Accepted);
    CHECK(gate.Consume(2,f.s.source.deadlineNs+1,false)); // Existing sibling only.
    CHECK(!gate.Complete(attack->serial,true,true)); // Input alone is no completion.
    for(unsigned n=0;n<3;++n)gate.Observe(n,2,5,6);
    CHECK(!gate.Complete(attack->serial,true,false)&&gate.Complete(attack->serial,true,true));
    f.knife.Committed(*attack,KnifeCommit::Accepted);
    for(unsigned n=0;n<20;++n)CHECK(!f.Step());
    CHECK(f.knife.Observe(f.Receipt(*attack)));
    for(unsigned n=0;n<5;++n)CHECK(!f.Step()); // Held trigger never repeats.
    f.s.stockTrigger=0;CHECK(!f.Step());f.s.stockTrigger=1;CHECK(f.Step());
    for(unsigned loss=0;loss<7;++loss){Fixture x;CHECK(x.Held());x.s.nativeAttackReady=false;
        x.s.stockTriggerReady=x.s.stockTriggerInputCurrent=true;CHECK(!x.Step()&&!x.Step());
        if(loss==0)x.s.stockTriggerReady=false;if(loss==1)x.s.stockTriggerInputCurrent=false;
        if(loss==2)x.s.source.focused=false;if(loss==3)++x.s.menu.epoch;
        if(loss==4)x.s.mechanicsSettled=false;if(loss==5)x.s.trackedKnifeOwned=false;
        if(loss==6){x.s.stockTrigger=1;CHECK(!x.knife.Prepare(x.s,x.hands));} // Mutated duplicate.
        else CHECK(!x.Step());
        x.s.stockTriggerReady=x.s.stockTriggerInputCurrent=x.s.source.focused=x.s.mechanicsSettled=x.s.trackedKnifeOwned=true;
        x.s.stockTrigger=1;CHECK(!x.Step()); // No edge survives authority loss.
    }
    {Fixture x;CHECK(x.Held());x.s.nativeAttackReady=false;x.s.stockTriggerInputCurrent=true;
        CHECK(!x.Step()&&!x.Step());x.s.stockTrigger=1;CHECK(!x.Step());} // Default-off.
    {Fixture x;CHECK(x.Held());x.s.nativeAttackReady=false;x.s.stockTriggerReady=x.s.stockTriggerInputCurrent=true;
        x.s.stockTrigger=1;CHECK(!x.Step()&&!x.Step());x.s.stockTrigger=0;CHECK(!x.Step());
        CHECK(!x.knife.Prepare(x.s,x.hands));x.s.stockTrigger=1;CHECK(x.Step());} // Immutable repeat retains neutral.
    {auto bad=*attack;bad.stockTriggerHold->item.id++;private_knife::KnifeAttackGate g;
        CHECK(!g.Stage(bad,bad.source.nowNs));bad=*attack;bad.stroke=MeleeStrokeIntent{};
        CHECK(!g.Stage(bad,bad.source.nowNs));}
    std::puts("Stock trigger groups: explicit cause, owner/source/conflicts, neutral edge, no held replay, unchanged native copy/completion gate.");
    return 0;
}

int StockTriggerPassiveReadiness(){
    Fixture f;CHECK(f.Held());f.s.nativeAttackReady=false;f.s.stockTriggerReady=f.s.stockTriggerInputCurrent=true;
    CHECK(!f.knife.StockTriggerArmedFor(f.s,f.hands));CHECK(!f.Step());
    CHECK(!f.knife.StockTriggerArmedFor(f.s,f.hands)); // First valid neutral is a baseline, not an edge.
    CHECK(!f.knife.Prepare(f.s,f.hands));CHECK(!f.knife.StockTriggerArmedFor(f.s,f.hands));
    CHECK(!f.Step());CHECK(f.knife.StockTriggerArmedFor(f.s,f.hands));
    const auto original=f.s.source;const auto hold=*f.knife.Hold();
    for(unsigned n=0;n<4;++n)CHECK(f.knife.StockTriggerArmedFor(f.s,f.hands));
    CHECK(f.knife.Hold()==hold&&f.s.source.sequence==original.sequence&&f.s.source.deadlineNs==original.deadlineNs);
    CHECK(!f.knife.Prepare(f.s,f.hands));CHECK(f.knife.StockTriggerArmedFor(f.s,f.hands));
    for(unsigned fault=0;fault<17;++fault){auto bad=f.s;
        if(fault==0)++bad.source.sequence;if(fault==1)++bad.source.observedNs;if(fault==2)++bad.source.deadlineNs;
        if(fault==3)bad.source.nowNs=bad.source.deadlineNs;if(fault==4)++bad.source.owner.equipGeneration;
        if(fault==5)++bad.source.owner.space;if(fault==6)bad.source.focused=false;
        if(fault==7)bad.source.tracked[1]=false;if(fault==8)bad.gripHeld[1]=false;
        if(fault==9)bad.stockTrigger=1;if(fault==10)bad.stockTriggerReady=false;
        if(fault==11)bad.stockTriggerInputCurrent=false;if(fault==12)bad.fireSuppressed=false;
        if(fault==13)bad.trackedKnifeOwned=false;if(fault==14)bad.mechanicsSettled=false;
        if(fault==15)++bad.menu.epoch;if(fault==16)bad.heldContact.eligible=false;
        CHECK(!f.knife.StockTriggerArmedFor(bad,f.hands));
        CHECK(f.knife.StockTriggerArmedFor(f.s,f.hands)); // A read does not revoke or rearm policy.
    }
    f.s.stockTrigger=1;const auto attack=f.Step();CHECK(attack&&attack->stockTriggerHold&&!attack->stroke);
    CHECK(!f.knife.StockTriggerArmedFor(f.s,f.hands));CHECK(!f.Step());
    // An actual observed gate loss revokes arming; recovery cannot revive it.
    for(unsigned fault=0;fault<8;++fault){Fixture x;CHECK(x.Held());x.s.nativeAttackReady=false;
        x.s.stockTriggerReady=x.s.stockTriggerInputCurrent=true;CHECK(!x.Step()&&!x.Step());
        CHECK(x.knife.StockTriggerArmedFor(x.s,x.hands));
        if(fault==0)x.s.stockTriggerReady=false;if(fault==1)x.s.source.focused=false;
        if(fault==2)x.s.source.tracked[1]=false;if(fault==3)x.s.fireSuppressed=false;
        if(fault==4)x.s.trackedKnifeOwned=false;if(fault==5)x.s.mechanicsSettled=false;
        if(fault==6)x.s.stockTriggerInputCurrent=false;if(fault==7)x.s.menu.owner=InputMenuOwner::Menu;
        CHECK(!x.Step());CHECK(!x.knife.StockTriggerArmedFor(x.s,x.hands));
        x.s.stockTriggerReady=x.s.source.focused=x.s.source.tracked[1]=x.s.fireSuppressed=true;
        x.s.trackedKnifeOwned=x.s.mechanicsSettled=x.s.stockTriggerInputCurrent=true;x.s.menu.owner=InputMenuOwner::Gameplay;
        x.s.stockTrigger=1;CHECK(!x.Step());CHECK(!x.knife.StockTriggerArmedFor(x.s,x.hands));
    }
    std::puts("Passive stock readiness: fresh neutral arming, immutable observation, exact source/claim negatives and lost-gate cancellation.");
    return 0;
}
#include "KnifeStockHoldCases.inc"
#include "KnifeStockJabCases.inc"
int main(){CHECK(!StockJabCases());CHECK(!StockHoldPublicationGapCases());CHECK(!StockTriggerPassiveReadiness());CHECK(!StockTriggerRevocationCases());CHECK(!StockTriggerCases());CHECK(!IdleGunNeedsANewEmptyHandNeutral());CHECK(!WalkingStrokeUsesSameNativeBoundaryPredicate());CHECK(!MovingHoldAndQuietRearm());CHECK(!DrawStillNeedsNeutral());
    CHECK(!MissingAuthorityNeverBecomesHeldInput());CHECK(!RealRawInputSeparatesHoldAndIntent());
    CHECK(!FocusGapReturnsBeforeFreshChestDraw());CHECK(!DuplicateCannotWidenOrReacquire());
    std::puts("Eight knife held-input groups PASS: idle gun rearm, real custody/quiet intent, input negatives, focus-gap return then deliberate redraw, immutable duplicate lease.");}
