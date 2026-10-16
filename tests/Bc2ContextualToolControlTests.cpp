#include "Bc2ContextualToolAdapter.h"
#include <cstdio>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(false)
namespace {
struct Fixture {
 ContextualToolObservation o{};InputFrame in{};HandInteractionSample s{};HandInteraction hands;
 HandClaimToken hold;ContextualToolControlGate gate;
 Fixture(){
  o.inventory.owner={0x10000,0x11000,0x12000,0x13000,1,2,3};o.inventory.count=5;o.inventory.selected=4;
  o.tool.item={0x13000,0x14000,0,12,4};o.inventory.items[4]=o.tool.item;
  in.generation=1;in.spaceGeneration=3;in.predictedNs=1000000000;in.focused=in.headValid=true;
  in.menu={InputMenuOwner::Gameplay,1,1,1100000000};
  for(auto& h:in.hands){h.active=Trigger|Squeeze;h.gripTracked=h.aimTracked=true;}
  in.hands[1].squeeze=1;
  s={{(std::uint64_t(0x12000)<<32)|0x11000,1,6,3},1,1000000000,1100000000,1000000000,true,{true,true},{false,false}};
  const auto claim=hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,
   {0x13000,8},{{0x15000,1},1,s.deadlineNs,true},1,0});hold=claim.claim->token;
 }
 void Step(float zoom,float fire){++s.sequence;s.observedNs+=10000000;s.deadlineNs=s.observedNs+100000000;s.nowNs=s.observedNs;
  in.generation=s.sequence;in.predictedNs=s.observedNs;in.menu.sequence=s.sequence;in.menu.deadlineNs=s.deadlineNs;
  in.hands[0].trigger=zoom;in.hands[1].trigger=fire;
  hands.Update(s);hands.Renew(s,hold,{hold.contact,s.sequence,s.deadlineNs,true});}
 ToolControlLevels Tick(){return gate.Update(o,in,s,hold,hands);}
 bool Quiet(){const auto x=Tick();return !x.zoom&&!x.fire;}
 void Arm(){Tick();Step(0,0);Tick();Step(1,1);}
};
int ExactFreshNeutral(){
 Fixture f;f.in.hands[0].trigger=f.in.hands[1].trigger=1;CHECK(f.Quiet());f.Step(1,1);CHECK(f.Quiet());
 f.Step(0,0);CHECK(f.Quiet());f.Step(1,1);auto result=f.Tick();CHECK(result.zoom&&result.fire);
 const auto claim=f.hands.Current(InteractionHand::Right);result=f.Tick();CHECK(result.zoom&&result.fire);
 CHECK(f.hands.Current(InteractionHand::Right)->deadlineNs==claim->deadlineNs);
 f.Step(0,1);result=f.Tick();CHECK(!result.zoom&&result.fire);f.Step(1,0);result=f.Tick();CHECK(result.zoom&&!result.fire);return 0;
}
int GapsAndDuplicates(){
 Fixture f;f.Arm();CHECK(f.Tick().fire);++f.s.deadlineNs;CHECK(f.Quiet()); // duplicate cannot renew lease
 f.Step(1,1);CHECK(f.Quiet());f.Step(0,0);CHECK(f.Quiet());f.Step(1,1);CHECK(f.Tick().fire);
 Fixture g;g.Arm();CHECK(g.Tick().fire);g.gate.Suspend();CHECK(g.Quiet());g.Step(1,1);CHECK(g.Quiet());
 g.Step(0,0);CHECK(g.Quiet());g.Step(1,1);CHECK(g.Tick().fire);
 Fixture changed;changed.Arm();CHECK(changed.Tick().fire);changed.in.hands[0].trigger=0;CHECK(changed.Quiet());return 0;
}
int FreshMenuAndOwnership(){
 for(unsigned variant=0;variant<10;++variant){Fixture f;f.Arm();CHECK(f.Tick().fire);
  if(variant==0)f.s.nowNs=f.s.deadlineNs;
  if(variant==1)f.in.menu.owner=InputMenuOwner::Menu;
  if(variant==2)f.in.menu.deadlineNs=f.s.nowNs;
  if(variant==3)f.o.inventory.owner.weapon+=4;
  if(variant==4)f.o.tool.item.category=10;
  if(variant==5)++f.o.inventory.owner.space;
  if(variant==6)f.hands.Release(f.s,f.hold);
  if(variant==7)f.hold.item.generation++;
  if(variant==8)f.s.released[1]=true;
  if(variant==9)f.in.hands[0].active=0;
  CHECK(f.Quiet());
 }return 0;
}
}
int main(){CHECK(ExactFreshNeutral()==0);CHECK(GapsAndDuplicates()==0);CHECK(FreshMenuAndOwnership()==0);
 std::puts("Three contextual-tool action boundary groups PASS; exact original neutral, both controls, custody/menu/space and duplicate expiry.");}
