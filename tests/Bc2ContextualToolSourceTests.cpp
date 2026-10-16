#include "Bc2ContextualToolSource.h"
#include <cstdio>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(false)
namespace {
struct Fixture {
    ReloadStateOwner owner{0x10000,0x11000,0x12000,0x13000,1,2,3};
    InputFrame in{};HandInteractionSample s{};ContextualToolToggle toggle;
    Fixture(){
        in.generation=1;in.spaceGeneration=3;in.predictedNs=1000000000;in.focused=in.headValid=true;
        in.menu={InputMenuOwner::Gameplay,1,1,1100000000};
        for(auto& h:in.hands){h.active=Trigger|Squeeze|Stick|StickClick;h.gripTracked=h.aimTracked=true;}
        in.hands[1].squeeze=1;
        s={{(std::uint64_t(0x12000)<<32)|0x11000,1,6,3},1,1000000000,1100000000,1000000000,true,{true,true},{false,false}};
    }
    void Step(){++s.sequence;s.observedNs+=10000000;s.deadlineNs=s.observedNs+100000000;s.nowNs=s.observedNs;
        in.generation=s.sequence;in.predictedNs=s.observedNs;in.menu.sequence=s.sequence;in.menu.deadlineNs=s.deadlineNs;}
    ContextualToolToggleResult Tick(bool active=false){return toggle.Update(in,s,{owner.weapon,4},active);}
    void Arm(){Tick();Step();Tick();Step();}
    ContextualToolAttachment Attachment(){
        ContextualToolAttachment a{owner,s,{owner.weapon,0x14000,0},42};
        for(unsigned n=0;n<4;++n)a.nativeWristInWeapon.values[n][n]=1;
        return a;
    }
};
int DeliberateToggle(){
    Fixture f;f.Arm();f.in.hands[1].held=StickClick;const auto draw=f.Tick();
    CHECK(draw.kind==ContextualToolToggleKind::Draw&&draw.intent.inputSequence==f.s.sequence&&draw.intent.deadlineNs==f.s.deadlineNs);
    CHECK(f.Tick().kind==ContextualToolToggleKind::None);f.Step();CHECK(f.Tick().kind==ContextualToolToggleKind::None);
    f.Step();f.in.hands[1].held=0;CHECK(f.Tick(true).kind==ContextualToolToggleKind::None);
    f.Step();CHECK(f.Tick(true).kind==ContextualToolToggleKind::None);
    f.Step();f.in.hands[1].held=StickClick;CHECK(f.Tick(true).kind==ContextualToolToggleKind::None); // still gripping
    f.Step();f.in.hands[1].held=0;f.in.hands[1].squeeze=0;f.s.released[1]=true;f.Tick(true);
    f.Step();f.in.hands[1].held=StickClick;CHECK(f.Tick(true).kind==ContextualToolToggleKind::Return);
    return 0;
}
int DoesNotStealRecenterOrOtherActions(){
    for(unsigned n=0;n<5;++n){Fixture f;f.Arm();f.in.hands[1].held=StickClick;
        if(n==0)f.in.hands[0].held=StickClick;
        if(n==1)f.in.hands[0].trigger=1;
        if(n==2)f.in.hands[1].trigger=1;
        if(n==3)f.in.hands[1].stickY=.8f;
        if(n==4)f.in.hands[1].squeeze=.2f;
        CHECK(f.Tick().kind==ContextualToolToggleKind::None);
    }return 0;
}
int GapRequiresNewNeutral(){
    for(unsigned n=0;n<4;++n){Fixture f;f.Arm();
        if(n==0)f.toggle.Missing();
        if(n==1){++f.s.owner.space;++f.in.spaceGeneration;}
        if(n==2){f.s.observedNs=f.s.deadlineNs;f.s.nowNs=f.s.observedNs;f.s.deadlineNs+=100000000;f.in.menu.deadlineNs=f.s.deadlineNs;}
        if(n==3)f.in.menu.owner=InputMenuOwner::Menu;
        f.in.hands[1].held=StickClick;CHECK(f.Tick().kind==ContextualToolToggleKind::None);
        f.in.menu.owner=InputMenuOwner::Gameplay;f.Step();CHECK(f.Tick().kind==ContextualToolToggleKind::None);
        f.Step();f.in.hands[1].held=0;f.Tick();f.Step();f.in.hands[1].held=StickClick;
        CHECK(f.Tick().kind==ContextualToolToggleKind::Draw);
    }return 0;
}
int AttachmentCannotOutliveOrPrecedeSelection(){
    Fixture f;auto a=f.Attachment();CHECK(ContextualToolAttachmentCurrent(a,f.owner,f.s,1,f.s.nowNs));
    CHECK(!ContextualToolAttachmentCurrent(a,f.owner,f.s,2,f.s.nowNs));
    for(unsigned n=0;n<7;++n){auto bad=a;
        if(n==0)++bad.owner.weapon;if(n==1)++bad.input.owner.space;
        if(n==2)bad.input.deadlineNs=f.s.nowNs;if(n==3)bad.input.released[1]=true;
        if(n==4)bad.rigFingerprint=0;if(n==5)bad.nativeWristInWeapon={};if(n==6)++bad.equipment.weapon;
        CHECK(!ContextualToolAttachmentCurrent(bad,f.owner,f.s,1,f.s.nowNs));
    }
    f.Step();CHECK(ContextualToolAttachmentCurrent(a,f.owner,f.s,1,f.s.nowNs));
    CHECK(!ContextualToolAttachmentCurrent(a,f.owner,f.s,1,a.input.deadlineNs));return 0;
}
int AimRequiresNativeReadbackAndOriginalSource(){
    Fixture f;const auto valid=ObserveContextualToolAim(f.owner,f.in,f.s,7,0x16000,.3f,.4f,.3f,.4f,.3f,.4f);
    CHECK(valid&&ContextualToolAimCurrent(*valid,f.owner,f.s,7,f.s.nowNs));
    CHECK(!ObserveContextualToolAim(f.owner,f.in,f.s,7,0x16000,.3f,.4f,.31f,.4f,.31f,.4f));
    CHECK(!ObserveContextualToolAim(f.owner,f.in,f.s,7,0x16000,.3f,1.55f,.3f,1.48f,.3f,1.48f)); // real native clamp
    CHECK(!ObserveContextualToolAim(f.owner,f.in,f.s,7,0x16000,.3f,.4f,.3f,.4f,.3f,.41f));
    CHECK(!ContextualToolAimCurrent(*valid,f.owner,f.s,8,f.s.nowNs));
    CHECK(!ContextualToolAimCurrent(*valid,f.owner,f.s,7,f.s.deadlineNs));
    auto altered=f.s;++altered.deadlineNs;CHECK(!ContextualToolAimCurrent(*valid,f.owner,altered,7,f.s.nowNs));
    f.Step();CHECK(!ContextualToolAimCurrent(*valid,f.owner,f.s,7,f.s.nowNs));
    f.in.menu.owner=InputMenuOwner::Menu;CHECK(!ObserveContextualToolAim(f.owner,f.in,f.s,7,0x16000,.3f,.4f,.3f,.4f,.3f,.4f));return 0;
}
}
int main(){CHECK(DeliberateToggle()==0);CHECK(DoesNotStealRecenterOrOtherActions()==0);CHECK(GapRequiresNewNeutral()==0);
    CHECK(AttachmentCannotOutliveOrPrecedeSelection()==0);CHECK(AimRequiresNativeReadbackAndOriginalSource()==0);
    std::puts("Five contextual tool source groups PASS: deliberate binding, recenter isolation, gap neutral, settled attachment lifetime and native aim readback.");}
