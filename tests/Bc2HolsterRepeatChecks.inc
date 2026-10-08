// Actual Bc2BodyInventory reader -> shared coordinator -> suppression/claims.
// Models the recorded Empty SPAS -> already-carried AEK in the other slot,
// including Gameplay's neutral rearm invalidation after native selection.
namespace {
void SelectOtherCarried(HolsterRun& h){
 h.r.f.Select(1);h.r.s.owner=h.r.f.owner;++h.r.s.hand.owner.equipGeneration;
 h.r.s.gun={Fixture::b,h.r.s.hand.owner.equipGeneration};h.r.hands.Reset();
 h.holster.Invalidate();h.r.adapter.Cancel();h.out.visibility={};
 for(auto& hand:h.r.s.input.hands){hand.active=interaction::Components;hand.gripTracked=hand.aimTracked=true;}
 h.r.s.input.hands[1].grip.position={.2f,1.2f,-.3f};
}
void OtherCarriedTick(HolsterRun& h,bool pair=true,bool neutral=true,unsigned bad=0){
 const auto prior=h.out.ordinaryRecovery;
 h.r.Advance();h.r.s.input.hands[1].trigger=neutral?0.f:1.f;
 auto s=h.Sample();s.visibility.reset();
 auto mesh=std::make_shared<bc2::SelectedMeshesSnapshot>(*s.selected);
 std::fill(mesh->weaponName.begin(),mesh->weaponName.end(),'\0');std::memcpy(mesh->weaponName.data(),"AEK971_sp",10);
 mesh->states[0].count=0;mesh->weaponData=Fixture::bd;s.selected=mesh;
 s.equipment={Fixture::b,Fixture::bd,0,{}};std::memcpy(s.equipment.asset.data(),"AEK971_sp",10);
 if(pair&&prior){bc2::RigIdentity rig{};rig.soldier=Fixture::soldier;rig.weak=Fixture::weak;rig.pose=0x42000;rig.count=147;
  s.ordinaryPair=bc2::OrdinaryEquipmentPair{*prior,rig,100+h.nativeTick,s.hand.nowNs,prior->input.deadlineNs,3};}
 if(const auto demand=h.holster.Demand(s)){bc2::HolsterInputOverride patch;
  if(patch.Apply(h.cache,*demand,{nullptr,[](void*,const bc2::HolsterSuppressionRequest&)noexcept{return true;}}))s.suppression=patch.Commit();}
 if(bad==1)s.suppression.reset();
 if(bad==2&&s.ordinaryPair)s.ordinaryPair->copyMask=1;
 if(bad==3&&s.ordinaryPair)s.ordinaryPair->deadlineNs=s.hand.nowNs;
 if(bad==4&&s.ordinaryPair)++s.ordinaryPair->source.equipment.data;
 if(bad==5)h.r.s.visible.reset();
 if(bad==6)s.equipment.data+=4;
 if(bad==7)s.equipment.persistence+=4;
 if(bad==8)h.r.s.reloadBusy=true;
 if(bad==9)h.r.s.cancel=true;
 h.out=h.r.adapter.TickHolster(h.r.f.Memory(),h.r.s,s,h.holster,h.r.hands,h.r.intent);
}
}
int ActualCarriedCrossDrawRetirement(){
 for(unsigned repeats=0;repeats<2;++repeats){HolsterRun h;CHECK(h.Empty());
  for(unsigned round=0;round<3;++round){
   if(round){
    // Ordinary return to accepted selected SPAS; same item array, new owner.
    h.r.f.Select(0);h.r.s.owner=h.r.f.owner;++h.r.s.hand.owner.equipGeneration;
    h.r.s.gun={Fixture::a,h.r.s.hand.owner.equipGeneration};h.r.hands.Reset();h.r.adapter.Cancel();
    h.r.Advance();h.r.Gun();h.Tick();h.Step();
    h.r.s.input.hands[1].grip.position={-.20f,1.58f,.18f};CHECK(h.Empty());
   }
   const auto old=*h.out.freeRight;const auto before=h.r.f.bytes;
   // Empty draw requests an existing assigned different item, never a new item.
   h.Step();h.r.s.input.hands[1].grip.position.x=.20f;h.Step(1);
   CHECK(h.out.select&&h.out.select->id==Fixture::b);
   SelectOtherCarried(h);
   if(repeats) {h.r.s.cancel=true;OtherCarriedTick(h,false);h.r.s.cancel=false;}
   OtherCarriedTick(h,false);CHECK(h.out.ordinaryRecovery&&h.out.blockWeaponActions);
   CHECK(!h.r.hands.Current(interaction::InteractionHand::Right)&&!h.out.visibility.enabled&&!h.out.select);
   OtherCarriedTick(h,true,false);CHECK(h.out.blockWeaponActions&&!h.out.ordinaryRetired);
   OtherCarriedTick(h,true);CHECK(h.out.ordinaryRetired&&!h.out.blockWeaponActions);
   CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&h.out.inventory.slots.size()==2);
   const auto gun=h.r.hands.Current(interaction::InteractionHand::Right);CHECK(gun&&gun->token.item==h.r.s.gun);
   CHECK(!h.out.freeRight&&!h.out.visibility.enabled&&!h.out.inventory.committedRequest);
   CHECK(!bc2::BodyFreeRightEvidenceCurrent(old,h.r.s.hand.nowNs)||old.visibility.nativeOwner!=h.r.s.owner);
   // The only fixture-native mutation was Select's actual slot observation.
   auto expected=before;unsigned selected=1;std::memcpy(expected.data()+Fixture::inventory+0x14c-0x10000,&selected,4);
   CHECK(h.r.f.bytes==expected);
  }
 }
 std::puts("Repeated carried cross-draw: six actual reader/suppression/ordinary-pair recoveries passed");return 0;
}
int CarriedCrossDrawIdentityFailures(){
 for(unsigned bad=0;bad<9;++bad){HolsterRun h;CHECK(h.Empty());SelectOtherCarried(h);
  if(bad==0)h.r.f.Word(Fixture::ad+0x64,7); // Other carried identity changed.
  if(bad==1)h.r.f.Word(Fixture::bd+0x64,7); // Selected contents changed.
  if(bad==2){++h.r.s.owner.actorGeneration;++h.r.s.hand.owner.actorGeneration;}
  if(bad==3){++h.r.s.owner.space;++h.r.s.hand.owner.space;h.r.s.input.spaceGeneration=h.r.s.owner.space;}
  if(bad==4)--h.r.s.owner.equipGeneration;
  if(bad==5)--h.r.s.hand.owner.equipGeneration;
  if(bad==6)h.r.f.Word(Fixture::bd+0x84,5); // Underbarrel remains ordinary stock mode.
  if(bad==7)h.r.f.denyMap=true;
  if(bad==8){h.r.f.categoryReads=0;h.r.f.mutateCategory=true;}
  OtherCarriedTick(h,false);
  if(bad==0){
   // Coherently changed non-selected carried data invalidates old slots; it is
   // not authority to keep the NEW selected ordinary native gun unusable.
   CHECK(h.out.ordinaryRecovery&&!h.out.ordinaryRetired&&h.out.blockWeaponActions);
   CHECK(!h.r.hands.Current(interaction::InteractionHand::Right));
   OtherCarriedTick(h,true,false);CHECK(!h.out.ordinaryRetired&&h.out.blockWeaponActions);
   OtherCarriedTick(h,true);CHECK(h.out.ordinaryRetired&&!h.out.blockWeaponActions);
   CHECK(h.r.hands.Current(interaction::InteractionHand::Right));continue;
  }
  CHECK(!h.out.ordinaryRecovery&&!h.out.ordinaryRetired&&h.out.blockWeaponActions);
  CHECK(!h.r.hands.Current(interaction::InteractionHand::Right)&&!h.out.visibility.enabled);
 }
 return 0;
}
int CarriedCrossDrawAuthorityFailures(){
 for(unsigned bad=1;bad<=9;++bad){HolsterRun h;CHECK(h.Empty());SelectOtherCarried(h);
  OtherCarriedTick(h,false);CHECK(h.out.ordinaryRecovery);
  OtherCarriedTick(h,true,true,bad);CHECK(!h.out.ordinaryRetired&&h.out.blockWeaponActions);
  CHECK(!h.r.hands.Current(interaction::InteractionHand::Right)&&!h.out.visibility.enabled&&!h.out.freeRight);
 }
 return 0;
}
int HolsterTransitionJournalKeepsLatest(){
 HolsterRun h;CHECK(h.Empty());
 for(unsigned n=0;n<150;++n){h.r.s.reloadBusy=n%2==0;h.Step();}
 std::ostringstream report;h.holster.Report(report);const auto text=report.str();
 CHECK(text.find("\"input\":"+std::to_string(h.r.s.input.generation)+",")!=std::string::npos);
 CHECK(text.find("\"gate_flags\":")!=std::string::npos);
 CHECK(text.find("\"dropped\":0,")==std::string::npos);
 CHECK(text.find("\"physical_item\":"+std::to_string(h.r.s.gun.id)+",")!=std::string::npos);
 return 0;
}
