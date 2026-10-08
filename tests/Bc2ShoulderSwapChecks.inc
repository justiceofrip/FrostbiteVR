// Existing real native inventory/arbiter/coordinator fixture. No synthetic
// squeeze edge is supplied on shoulder entry: it stays in the .55 middle band.
int ShoulderEarlyDirectSwap(){using namespace interaction;
 for(unsigned mirrored=0;mirrored<2;++mirrored){HolsterRun h;BodyAnchorConfig anchors;
  for(auto& controller:h.r.s.input.hands)controller.active|=Trigger;
  if(mirrored)for(auto& anchor:anchors.shoulders)anchor.center.x=-anchor.center.x;
  h.r.adapter=bc2::Bc2BodyInventory(true,anchors);
  h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};h.Step();h.Step();h.Step(1);
  CHECK(!h.out.ordinaryDraw.command&&!h.out.inventory.request);
  h.Step(.55f);const auto other=anchors.shoulders[1].center;
  h.r.s.input.hands[1].grip.position={other.x,1.7f+other.y,-other.z};
  const auto oldGun=h.r.hands.Current(InteractionHand::Right);CHECK(oldGun);
  h.Step(.55f);CHECK(h.out.ordinaryDraw.command&&h.out.ordinaryDraw.command->targetWeapon==Fixture::b);
  CHECK(h.out.ordinaryDraw.pending&&h.out.blockWeaponActions&&!h.out.visibility.enabled&&!h.out.freeRight);
  CHECK(h.r.s.input.hands[1].squeeze==.55f&&h.r.hands.Current(InteractionHand::Right)->token.item==oldGun->token.item);
  const auto source=h.r.s.input;h.Tick();CHECK(!h.out.ordinaryDraw.command&&h.out.ordinaryDraw.pending);
  CHECK(h.r.s.input.generation==source.generation&&h.r.s.input.hands[1].squeeze==source.hands[1].squeeze);
  h.Step(.55f);CHECK(h.out.ordinaryDraw.pending&&!h.out.ordinaryDraw.committed);
  // Only the actual selected item change completes the ordinary native draw.
  h.r.f.Select(1);h.r.s.owner=h.r.f.owner;++h.r.s.hand.owner.equipGeneration;
  h.r.s.gun={Fixture::b,h.r.s.hand.owner.equipGeneration};h.r.Advance(.55f);h.r.Gun();h.Tick();
  CHECK(h.out.ordinaryDraw.committed&&!h.out.ordinaryDraw.pending&&!h.out.blockWeaponActions);
  CHECK(h.r.hands.Current(InteractionHand::Right)->token.item==h.r.s.gun);
 }
 std::puts("Shoulder direct swap: mirrored early grab, unchanged .55 input, single native command and actual selected-item acknowledgement passed");return 0;
}
int ShoulderDirectSwapFailures(){using namespace interaction;
 for(unsigned failure=0;failure<8;++failure){HolsterRun h;
  for(auto& controller:h.r.s.input.hands)controller.active|=Trigger;
  h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};h.Step();h.Step(1);CHECK(!h.out.ordinaryDraw.command);
  if(failure==0)for(unsigned n=0;n<61;++n)h.Step(.55f);
  if(failure==1){h.r.s.reloadBusy=true;h.Step(.55f);h.r.s.reloadBusy=false;}
  if(failure==2){h.r.s.input.hands[1].gripTracked=false;h.r.s.hand.tracked[1]=false;h.Step(.55f);
   h.r.s.input.hands[1].gripTracked=true;h.r.s.hand.tracked[1]=true;h.r.Advance(.55f);h.r.Gun();h.Tick();}
  if(failure==3){++h.r.s.input.spaceGeneration;++h.r.s.owner.space;++h.r.s.hand.owner.space;h.r.Advance(.55f);h.r.Gun();h.Tick();}
  if(failure==4){h.r.s.input.hands[1].trigger=.5f;}
  if(failure==5)h.r.f.denyMap=true;
  if(failure==6)h.r.f.Word(Fixture::bd+0x84,4); // lifetime/slot revision changes during reach
  h.r.s.input.hands[1].grip.position={.2f,1.58f,.18f};h.r.Advance(.55f);
  if(failure==7){CHECK(h.r.hands.Acquire(h.r.s.hand,{h.r.s.hand.owner,InteractionHand::Left,HandClaimKind::AmmoObject,
     {999,1},{{998,1},h.r.s.hand.sequence,h.r.s.hand.deadlineNs,true},++h.r.intent,0}).accepted);}
  h.Tick();CHECK(!h.out.ordinaryDraw.command&&!h.out.inventory.request&&!h.out.select&&!h.out.freeRight);
 }
 std::puts("Shoulder direct swap: eight expired, busy, tracking, recenter, trigger, native read, inventory and hand-owner failures reject");return 0;
}
int ShoulderSwapRereadAndTimeout(){using namespace interaction;
 for(unsigned failure=0;failure<3;++failure){HolsterRun h;
  for(auto& controller:h.r.s.input.hands)controller.active|=Trigger;
  h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};h.Step();h.Step(1);
  h.r.s.input.hands[1].grip.position={.2f,1.58f,.18f};h.r.Advance(.55f);
  struct Reread {HolsterRun* h;bc2::WeaponModeMemory original;unsigned failure,starts=0;
   bc2::WeaponModeMemory Memory(){return {this,[](void* p,unsigned at,void* out,std::size_t n){auto& r=*static_cast<Reread*>(p);
    if(at==Fixture::player+0xc54&&++r.starts==3){
     if(r.failure==0)r.h->r.f.Word(Fixture::bd+0x84,4);
     if(r.failure==1){r.h->r.f.Word(Fixture::array+4,Fixture::b+128);r.h->r.f.Word(Fixture::b+128+4,Fixture::bd);}
     if(r.failure==2)r.h->r.Gun(); // new token for same held item is still a new claim
    }
    return r.original.read(r.original.context,at,out,n);
   },[](void* p,unsigned at,const char* type){auto& r=*static_cast<Reread*>(p);return r.original.type(r.original.context,at,type);}};}
  } reread{&h,h.r.f.Memory(),failure};
  const auto sample=h.Sample();h.out=h.r.adapter.TickHolster(reread.Memory(),h.r.s,sample,h.holster,h.r.hands,h.r.intent);
  CHECK(reread.starts==4&&!h.out.ordinaryDraw.command&&!h.out.inventory.request&&!h.out.select);
  CHECK(h.r.hands.Current(InteractionHand::Right)->token.item==h.r.s.gun&&!h.out.freeRight);
 }
 {HolsterRun h;for(auto& controller:h.r.s.input.hands)controller.active|=Trigger;
  h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};h.Step();h.Step(1);
  h.r.s.input.hands[1].grip.position={.2f,1.58f,.18f};h.Step(.55f);CHECK(h.out.ordinaryDraw.command);
  bool cancelled=false;for(unsigned n=0;n<155;++n){h.Step(.55f);cancelled|=h.out.ordinaryDraw.cancelled!=0;CHECK(!h.out.ordinaryDraw.command);}
  CHECK(cancelled&&!h.out.ordinaryDraw.pending&&!h.out.blockWeaponActions&&!h.out.freeRight);
  CHECK(h.r.hands.Current(InteractionHand::Right)->token.item==h.r.s.gun);
 }
 std::puts("Shoulder direct swap: coherent reread rejects changed slot/item/claim; failed native selection preserves usable held gun");return 0;
}
