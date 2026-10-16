#include "Test.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeInput.h"
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
int main(){CHECK(!WalkingStrokeUsesSameNativeBoundaryPredicate());CHECK(!MovingHoldAndQuietRearm());CHECK(!DrawStillNeedsNeutral());
    CHECK(!MissingAuthorityNeverBecomesHeldInput());CHECK(!RealRawInputSeparatesHoldAndIntent());
    CHECK(!FocusGapReturnsBeforeFreshChestDraw());CHECK(!DuplicateCannotWidenOrReacquire());
    std::puts("Seven knife held-input groups PASS: real custody/quiet intent, input negatives, focus-gap return then deliberate redraw, immutable duplicate lease.");}
