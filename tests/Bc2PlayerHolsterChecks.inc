// One persistent fixture spans actual input policy, spatial gestures, native
// inventory reads, coordinator and hand claims. Paired renderer receipts are
// the existing explicit test-owned mocks; no live native authority is claimed.
int PlayerRepeatedHolsterRecovery(){
 using namespace interaction;
 HolsterRun h(bc2::BodyInventoryHolsterAcceptance,true);
 for(auto& hand:h.r.s.input.hands){hand.active=Components;hand.gripTracked=hand.aimTracked=true;}
 ControllerActions actions;bc2::BodyHolsterLifecycleLog journal;
 const auto point=[&](unsigned slot){h.r.s.input.hands[1].grip.position={slot==1?-.2f:.2f,1.58f,.18f};};
 const auto outside=[&]{h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};};
 for(unsigned round=0;round<3;++round){
  const auto own=h.r.adapter.AssignedSlot(h.r.s.owner.weapon);CHECK(own);
  point(own->slot);h.Step();CHECK(h.Empty());const auto old=*h.out.freeRight;
  outside();h.Step();
  // Real action loss and rearm: failed Apply leaves the cache untouched. Use
  // current production SuspendInteraction, preserving observed item lifetimes.
  for(unsigned n=0;n<5;++n){h.r.Advance();const auto action=actions.Update(h.r.s.input,{Fixture::soldier,h.r.s.owner.equipGeneration,true,true},h.r.s.input.predictedNs);h.Tick();CHECK(action.active||n<3);}
  h.r.s.input.focused=false;h.r.Advance();
  auto action=actions.Update(h.r.s.input,{Fixture::soldier,h.r.s.owner.equipGeneration,true,true},h.r.s.input.predictedNs);
  if(!action.active||action.owner!=Fixture::soldier){action={};action.owner=Fixture::soldier;}
  const auto cache=h.cache;bc2::InputOverride lost;
  CHECK(!lost.Apply(h.cache,action)&&h.cache==cache);
  journal.Invalidate(h.holster,bc2::BodyHolsterLifecycleReason::InputApplyRejected,false,h.r.s.hand,h.r.s.owner.equipGeneration);
  h.r.adapter.SuspendInteraction();h.out.visibility={};
  h.r.s.input.focused=true;unsigned accepts=0;
  for(unsigned n=0;n<10;++n){h.r.Advance();action=actions.Update(h.r.s.input,{Fixture::soldier,h.r.s.owner.equipGeneration,true,true},h.r.s.input.predictedNs);
   if(!action.active||action.owner!=Fixture::soldier){action={};action.owner=Fixture::soldier;}
   bc2::InputOverride patch;
   if(patch.Apply(h.cache,action)){patch.Commit();++accepts;h.Tick();CHECK(!h.out.select&&!h.out.inventory.request&&!h.out.ordinaryDraw.command);}
   else {journal.Invalidate(h.holster,bc2::BodyHolsterLifecycleReason::InputApplyRejected,false,h.r.s.hand,h.r.s.owner.equipGeneration);h.r.adapter.SuspendInteraction();h.out.visibility={};}
   CHECK(!h.r.hands.Current(InteractionHand::Right));
  }
  CHECK(accepts>=3&&h.holster.Phase()==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);
  CHECK(h.out.freeRight->visibility.request>old.visibility.request&&h.out.freeRight->receipt.verifiedCopyMask==3);
  outside();h.Step();h.Step(1);CHECK(!h.out.select&&h.out.freeRight);h.Step(.55f);CHECK(!h.out.select);
  point(own->slot);h.Step(.55f);CHECK(h.out.select==std::optional(own->item));h.Step(.55f);h.Step(.55f);
  CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(InteractionHand::Right));
  outside();for(unsigned n=0;n<65;++n)h.Step(1);
  point(own->slot);h.Step(.55f);h.Step(.55f);h.Step();
  CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::HidePending);h.Step();h.Step();CHECK(h.out.freeRight);
  // Explicit shoulder cross-draw, followed only by fixture-owned native slot
  // observation. The coordinator must consume fresh show receipts itself.
  const auto native=bc2::ReadBodyInventory(h.r.f.Memory(),h.r.s.owner,true);CHECK(native.snapshot);
  const unsigned otherSlot=native.snapshot->selected==0?1:0;
  const unsigned other=native.snapshot->items[otherSlot].weapon;
  const auto assigned=h.r.adapter.AssignedSlot(other);CHECK(assigned);h.Step();point(assigned->slot);h.Step(1);
  CHECK(h.out.select==std::optional(assigned->item));h.r.f.Select(otherSlot);h.r.f.owner.weapon=other;h.r.s.owner=h.r.f.owner;
  ++h.r.s.hand.owner.equipGeneration;h.r.s.gun={other,h.r.s.hand.owner.equipGeneration};h.scopedXm8=native.snapshot->items[otherSlot].category==0;
  h.Step(1);CHECK(!h.r.hands.Current(InteractionHand::Right));h.Step(1);
  CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(InteractionHand::Right)->token.item==h.r.s.gun);
  CHECK(!h.out.freeRight&&h.r.adapter.AssignedSlot(other)->item==assigned->item);
  if(round==0){
   // Fixture-observed pickup replaces only the non-selected rifle slot. Its
   // old item lifetime cannot be reused by the next explicit shoulder draw.
   const auto retired=h.r.adapter.AssignedSlot(Fixture::a);CHECK(retired);
   constexpr unsigned replacement=0x13400,data=0x14800,persistence=0x14980;
   h.r.f.Word(Fixture::array,replacement);h.r.f.Word(replacement+4,data);
   h.r.f.Word(data,Fixture::table);h.r.f.Word(data+0x64,persistence);h.r.f.Word(data+0x84,0);
   h.r.f.types[data]="SoldierWeaponData";outside();h.Step(1);
   CHECK(!h.out.select&&!h.out.inventory.request&&!h.out.ordinaryDraw.command);
   CHECK(!h.r.adapter.AssignedSlot(Fixture::a));const auto picked=h.r.adapter.AssignedSlot(replacement);
   CHECK(picked&&picked->item!=retired->item&&h.r.hands.Current(InteractionHand::Right)->token.item==h.r.s.gun);
  }
 }
 std::puts("Player holster sequence: three same-instance input interruptions, explicit draws/stows, native cross-draws and non-selected pickup lifetime passed; paired render receipts mocked");return 0;
}
