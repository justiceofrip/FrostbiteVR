#include "Bc2ContextualToolAdapter.h"
#include "Bc2ContextualToolInteraction.h"
#include "Bc2BodyInteractionInput.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include <cstdio>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
struct Fixture {
 ContextualToolObservation o{};BodyDrawSample s{};HandInteraction hands;HandClaimToken hold{};
 ContextualToolControlGate gate;bool held=true,revoked=false;std::uint64_t operation=17,bodyOperation=17;
 std::array<std::byte,InputBytes> cache{};
 Fixture(){
  o.inventory.owner={0x10000,0x11000,0x12000,0x13000,1,2,3};o.inventory.count=5;o.inventory.selected=4;
  o.tool.item={0x13000,0x14000,0,12,4};o.inventory.items[4]=o.tool.item;s.owner=o.inventory.owner;
  s.input.generation=1;s.input.spaceGeneration=3;s.input.predictedNs=1000000000;s.input.focused=s.input.headValid=true;
  s.input.menu={InputMenuOwner::Gameplay,1,1,1100000000};
  for(auto& h:s.input.hands){h.active=Trigger|Squeeze;h.gripTracked=h.aimTracked=true;}s.input.hands[1].squeeze=1;
  s.hand={{(std::uint64_t(0x12000)<<32)|0x11000,1,6,3},1,1000000000,1100000000,1000000000,true,{true,true},{false,false}};
  hold=hands.Acquire(s.hand,{s.hand.owner,InteractionHand::Right,HandClaimKind::GunHold,{0x13000,8},
    {{0x15000,1},1,s.hand.deadlineNs,true},1,0}).claim->token;
 }
 void Step(float fire){++s.hand.sequence;s.hand.observedNs+=10000000;s.hand.nowNs=s.hand.observedNs;s.hand.deadlineNs=s.hand.observedNs+100000000;
  s.input.generation=s.hand.sequence;s.input.predictedNs=s.hand.observedNs;s.input.menu.sequence=s.hand.sequence;s.input.menu.deadlineNs=s.hand.deadlineNs;
  s.input.hands[1].trigger=fire;hands.Update(s.hand);hands.Renew(s.hand,hold,{hold.contact,s.hand.sequence,s.hand.deadlineNs,true});}
 bool Admitted(){return ContextualToolInteractionCurrent(s,o,held,revoked,operation,bodyOperation,hold,hands);}
 bool Tick(unsigned actions){
  SetBodyInteractionInput(s,true,true,true,actions);
  if(!Admitted()){gate.Suspend();return false;}
  const auto levels=gate.Update(o,s.input,s.hand,hold,hands);
  ToolInputRequest r;r.scope={s.owner,s.hand,11,13,0x17000};r.operation=operation;r.serial=s.hand.sequence;
  r.from=r.target=o.tool.item;r.custody=hold;r.kind=ToolInputKind::Controls;r.fire=levels.fire;r.zoom=levels.zoom;
  ContextualToolInputOverride w;const ToolInputValidation v{this,[](void* p,const ToolInputRequest&)noexcept{return static_cast<Fixture*>(p)->Admitted();}};
  return w.Apply(cache,r,v)&&w.Commit().has_value()&&levels.fire;
 }
 void Arm(){Tick(0);Step(0);Tick(0);Step(1);}
 unsigned FireWord(){unsigned word=0;std::memcpy(&word,cache.data()+8+4*8,4);return word;}
};
int RealSuspensionToControlWrite(){
 Fixture f;f.Arm();CHECK(f.Tick(Fire));CHECK(f.s.interaction==BodyInteractionState::Suspended&&f.s.cancelOnlyForFire);
 CHECK(std::bit_cast<float>(f.FireWord())==1.f); // Old adapter exits before this actual scoped writer.
 CHECK(f.hands.Current(InteractionHand::Right)->token==f.hold);
 f.Step(0);CHECK(!f.Tick(0));f.Step(1);CHECK(f.Tick(Fire));return 0;
}
int OrdinaryWeaponsUseMenuAndHolsterRemainBlocked(){
 for(unsigned n=0;n<15;++n){Fixture f;f.Arm();unsigned actions=Fire;
  if(n==0)f.o.inventory.selected=0; // Ordinary weapon
  if(n==1){f.o.tool.item.category=0;f.o.inventory.items[4]=f.o.tool.item;}
  if(n==2)actions|=Use;
  if(n==3)actions|=NextWeapon;
  if(n==4)actions|=PreviousWeapon;
  if(n==5)f.s.input.menu.owner=InputMenuOwner::Menu;
  if(n==6)f.s.input.menu.owner=InputMenuOwner::Pending;
  if(n==7)f.s.input.menu.deadlineNs=f.s.hand.nowNs;
  if(n==8)f.held=false; // Selecting/AwaitNeutral/equip/return cannot use this exception
  if(n==9)f.bodyOperation++;
  if(n==10)f.revoked=true;
  if(n==11)f.hold.id++;
  if(n==12)f.s.reloadBusy=true;
  if(n==13)f.s.cancel=true;
  if(n==14)f.s.hand.nowNs=f.s.hand.deadlineNs;
  CHECK(!f.Tick(actions));CHECK(f.FireWord()==0);
 }return 0;
}
int CauseAndCurrentClaimAreBothNecessary(){
 for(unsigned n=0;n<8;++n){Fixture f;f.Arm();SetBodyInteractionInput(f.s,n!=0,n!=1,n!=2,Fire);
  if(n==3)f.s.cancelOnlyForFire=false;
  if(n==4)f.hands.Release(f.s.hand,f.hold);
  if(n==5)f.s.hand.released[1]=true;
  if(n==6)f.s.input.hands[1].squeeze=.2f;
  if(n==7)f.s.hand.tracked[1]=false;
  CHECK(!f.Admitted());}
 BodyDrawSample s;SetBodyInteractionInput(s,true,true,true,Crouch|Sprint|Jump);
 CHECK(s.interaction==BodyInteractionState::Available&&!s.cancelOnlyForFire);
 SetBodyInteractionInput(s,true,true,true,Fire|Crouch);
 CHECK(s.interaction==BodyInteractionState::Suspended&&s.cancelOnlyForFire);return 0;
}
}
int main(){CHECK(!RealSuspensionToControlWrite());CHECK(!OrdinaryWeaponsUseMenuAndHolsterRemainBlocked());
 CHECK(!CauseAndCurrentClaimAreBothNecessary());std::puts("Three real contextual suspension/control/writer groups PASS.");}
