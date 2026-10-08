// Included by the existing BC2 inventory fixture: actual InputFrame conversion,
// native reader, hand arbiter, coordinator and paired hide/show acknowledgements.
int ShoulderEarlyGrabAndReleaseStow(){
 using namespace interaction;
 for(unsigned mirrored=0;mirrored<2;++mirrored){
  HolsterRun h;BodyAnchorConfig anchors;
  for(auto& controller:h.r.s.input.hands)controller.active|=Trigger;
  if(mirrored)for(auto& shoulder:anchors.shoulders)shoulder.center.x=-shoulder.center.x;
  h.r.adapter=bc2::Bc2BodyInventory(true,anchors);
  const auto point=[&](math::Vec3 p){h.r.s.input.hands[1].grip.position={p.x,1.7f+p.y,-p.z};};
  const auto own=anchors.shoulders[0].center;const math::Vec3 outside{0,-.4f,.4f};
  point(own);h.Step();CHECK(h.Empty());
  point(outside);h.Step();h.Step(1);CHECK(!h.out.select&&!h.out.inventory.request&&h.out.freeRight);
  h.Step(.55f);CHECK(!h.out.select); // analog grip remains held through hysteresis
  point(own);h.r.s.input.head.orientation={0,std::sin(.5f),0,std::cos(.5f)};
  h.Step(.55f);CHECK(h.out.select&&h.out.inventory.request&&h.out.blockWeaponActions&&!h.r.hands.Current(InteractionHand::Right));
  h.Step(.55f);h.Step(.55f);CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions);
  CHECK(h.r.hands.Current(InteractionHand::Right)->token.item==h.r.s.gun);
  // Keep gripping through normal carry, far beyond the approach window.
  point(outside);for(unsigned n=0;n<65;++n)h.Step(1);
  point(own);h.Step(.55f);CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.inventory.request);
  h.Step(.55f);h.Step(0);CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&h.out.inventory.request);
  CHECK(h.out.inventory.request->operation==BodyInventoryOperation::Holster&&h.out.blockWeaponActions);
  CHECK(h.r.hands.Current(InteractionHand::Right)); // never release native gun before exact hide acknowledgement
  h.Step();h.Step();CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight&&!h.r.hands.Current(InteractionHand::Right));
  CHECK(h.out.freeRight->receipt.verifiedCopyMask==3);
  std::ostringstream report;h.r.adapter.Report(report);CHECK(report.str().find("\"grip_release\":512")!=std::string::npos);
 }
 std::puts("Shoulder gestures: both sides, early empty grab, grip hysteresis, release stow and real acknowledgements passed");return 0;
}
int ShoulderMissKeepsUsableGun(){
 using namespace interaction;
 for(unsigned mode=0;mode<4;++mode){HolsterRun h;
  for(auto& controller:h.r.s.input.hands)controller.active|=Trigger;
  h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};h.Step(1);for(unsigned n=0;n<65;++n)h.Step(1);
  const auto held=h.r.hands.Current(InteractionHand::Right);CHECK(held);
  if(mode!=0){h.r.s.input.hands[1].grip.position={-.2f,1.58f,.18f};h.Step(1);}
  if(mode==1)h.r.s.input.hands[1].grip.position={0,1.3f,-.4f}; // current shoulder contact is required at release
  if(mode==2)h.r.s.input.hands[1].active&=~Squeeze; // missing component is not a release
  if(mode==3){h.r.s.input.hands[1].active|=Trigger;h.r.s.input.hands[1].trigger=.5f;} // no holster on a trigger-held release
  h.Step(0);CHECK(!h.out.inventory.request&&h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions);
  CHECK(h.r.hands.Current(InteractionHand::Right)->token==held->token);
  // A missed physical stow has not consumed or hidden the still usable gun.
  CHECK(!h.out.visibility.enabled&&!h.out.freeRight);
 }
 std::puts("Shoulder misses: lower-body release, left-zone exit, absent squeeze and held trigger preserve held gun");return 0;
}
int ShoulderReleaseExactContinuity(){
 using namespace interaction;
 for(unsigned failure=0;failure<13;++failure){BodyGripRelease gesture;
  HandInteractionSample in{{1,2,3,4},10,1000000000,1100000000,1000000000,true,{true,true}};
  HandInteractionKey weapon{5,3};std::uint64_t revision=1,claim=7;
  std::optional<BodySlotAssignment> contact=BodySlotAssignment{1,{5,9}};
  CHECK(!gesture.Update(in,weapon,revision,claim,true,false,true,contact));
  ++in.sequence;in.observedNs+=10000000;in.nowNs=in.observedNs;in.deadlineNs=in.observedNs+100000000;
  if(failure==1)++in.owner.actorGeneration;
  if(failure==2){++in.owner.equipGeneration;++weapon.generation;}
  if(failure==3)++in.owner.space;if(failure==4)++revision;if(failure==5)++claim;
  if(failure==6)in.tracked[1]=false;if(failure==7)in.focused=false;
  if(failure==8){in.nowNs=1100000000;in.observedNs=in.nowNs;in.deadlineNs=in.nowNs+100000000;}
  if(failure==9)++contact->item.generation;if(failure==10)contact.reset();
  if(failure==11)--in.sequence;
  const auto result=gesture.Update(in,weapon,revision,claim,false,true,failure!=12,contact);
  CHECK(bool(result)==(failure==0));CHECK(!gesture.Update(in,weapon,revision,claim,false,true,true,contact));
 }
 for(unsigned failure=0;failure<5;++failure){BodyGripRelease gesture;
  HandInteractionSample in{{1,2,3,4},10,1000000000,1100000000,1000000000,true,{true,true}};
  const HandInteractionKey weapon{5,3};const BodySlotAssignment contact{1,{5,9}};
  const auto step=[&](){++in.sequence;in.observedNs+=10000000;in.nowNs=in.observedNs;in.deadlineNs=in.nowNs+100000000;};
  if(failure){CHECK(!gesture.Update(in,weapon,1,7,true,false,failure!=4,contact));step();}
  if(failure==1){in.tracked[1]=false;CHECK(!gesture.Update(in,weapon,1,7,false,false,true,contact));in.tracked[1]=true;step();}
  if(failure==2)++in.owner.space;if(failure==3)gesture.Reset();
  if(failure==4){--in.sequence;CHECK(!gesture.Update(in,weapon,1,7,true,false,true,contact));++in.sequence;}
  CHECK(!gesture.Update(in,weapon,1,7,false,false,true,contact));step();
  CHECK(!gesture.Update(in,weapon,1,7,false,true,true,contact));
 }
 std::puts("Shoulder release continuity: 13 identity/contact cases and 5 unarmed middle-band/reset/duplicate cases passed");return 0;
}
int ShoulderFailedHideRecoversHeld(){
 using namespace interaction;
 for(unsigned failure=0;failure<2;++failure){HolsterRun h;
  for(auto& controller:h.r.s.input.hands)controller.active|=Trigger;
  h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};h.Step(1);for(unsigned n=0;n<65;++n)h.Step(.55f);
  h.r.s.input.hands[1].grip.position={-.2f,1.58f,.18f};h.Step(.55f);h.Step(0);
  CHECK(h.out.inventory.request&&h.out.phase==bc2::BodyHolsterPhase::HidePending);
  const auto token=h.r.hands.Current(InteractionHand::Right)->token;
  bool cancelled=false;
  for(unsigned n=0;n<152;++n){
   if(failure==0)h.allowReadback=false;else h.out.visibility={};
   h.Step();cancelled|=h.out.inventory.cancelledRequest!=0;
   CHECK(!h.out.freeRight&&h.r.hands.Current(InteractionHand::Right)->token==token);
   CHECK(!h.out.inventory.committedRequest&&h.out.blockWeaponActions);
  }
  CHECK(cancelled);h.allowReadback=true;
  h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};
  for(unsigned n=0;n<5;++n)h.Step();
  CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions);
  CHECK(!h.out.freeRight&&!h.out.visibility.enabled&&h.r.hands.Current(InteractionHand::Right)->token==token);
 }
 std::puts("Shoulder failed hide: missing suppression or stereo acknowledgement times out, retains gun and restores usable held state");return 0;
}
int ShoulderEmptyApproachInputBreaks(){
 using namespace interaction;
 for(unsigned failure=0;failure<8;++failure){HolsterRun h;CHECK(h.Empty());
  h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};h.Step();h.Step(1);
  CHECK(!h.out.inventory.request&&!h.out.select&&!h.r.hands.Current(InteractionHand::Right));
  h.r.s.input.hands[1].grip.position={-.2f,1.58f,.18f};
  if(failure==0){h.r.s.input.hands[1].gripTracked=false;h.r.s.hand.tracked[1]=false;}
  if(failure==1){h.r.s.input.focused=false;h.r.s.hand.focused=false;}
  if(failure==2)h.r.s.input.hands[1].active&=~Squeeze;
  if(failure==3){++h.r.s.input.spaceGeneration;++h.r.s.owner.space;++h.r.s.hand.owner.space;}
  if(failure==4)++h.r.s.hand.owner.actorGeneration;
  if(failure==5){++h.r.s.hand.owner.equipGeneration;++h.r.s.gun.generation;}
  if(failure==6){h.r.s.hand.observedNs+=151000000;h.r.s.hand.nowNs=h.r.s.hand.observedNs;
    h.r.s.input.predictedNs=h.r.s.hand.nowNs;}
  if(failure==7){h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};for(unsigned n=0;n<61;++n)h.Step(1);
    h.r.s.input.hands[1].grip.position={-.2f,1.58f,.18f};}
  h.Step(.55f);CHECK(!h.out.inventory.request&&!h.out.select&&!h.r.hands.Current(InteractionHand::Right));
  const auto recoveryVisibility=h.out.visibility;
  h.Step(.55f);CHECK(!h.out.inventory.request&&!h.out.select);
  // The existing coordinator may restore the gun after an equipment lifetime
  // change, but only from its fresh show acknowledgement, never the old grab.
  if(const auto gun=h.r.hands.Current(InteractionHand::Right)){
   CHECK(recoveryVisibility.enabled&&!recoveryVisibility.hide);
   CHECK(gun->token.owner==h.r.s.hand.owner&&gun->token.item==h.r.s.gun);
  }
 }
 std::puts("Shoulder empty approach: 8 actual input, tracking, owner, deadline and timeout breaks cannot synthesize draw requests");return 0;
}
