int SupportWithoutMagazineEnrollment(){
 // All ordinary inventory/hand/support consumers stay real. Only the native
 // fixture is synthetic; explicitly remove every magazine publication.
 for(unsigned fault=0;fault<4;++fault){
  auto f=std::make_unique<Loop>();f->singleFireMagazine=true;CHECK(f->driver.EnableSupportOnly());
  CHECK(!f->driver.EnableMagazineOnly()&&!f->driver.EnablePump(f->calibration)&&!f->driver.RequireRifleSupport());
  unsigned heldTicks=0;bool faultApplied=false;
  f->beforeDriver=[&](Loop& loop){
   loop.observed.magazine={};loop.observed.magazineRaw={};loop.observed.reserve.reset();
   loop.observed.supportVerified=loop.Rifle()&&loop.magRaw.valid&&fault!=1;
   loop.observed.supportRaw=loop.magRaw;
   loop.observed.supportTarget=Multiply(Pose(-.2f,-.25f,.45f),Pose(5,2,3));
   const auto left=loop.hands.Current(InteractionHand::Left);
   if(loop.driver.State()==Bc2ResourceInventoryProbe::Phase::RifleSupport&&left&&left->token.kind==HandClaimKind::WeaponSupport){
    ++heldTicks;
    if(heldTicks==20&&fault>=2){
     faultApplied=true;
     if(fault==2)loop.hands.Release(loop.hand,left->token);
     else {++loop.native.owner.equipGeneration;loop.memory.owner=loop.native.owner;}
    }
   }
  };
  const bool complete=f->Run(false);
  if(complete!=(fault==0))std::cerr<<"Support-only fault="<<fault<<" evidence="<<f->Report()<<'\n';
  CHECK(complete==(fault==0));
  CHECK(!f->calls&&!f->rifleShots&&!f->shots&&!f->shellSubmits&&!f->selectionCalls);
  CHECK(f->rifle==AmmunitionCounts(22,191,30));
  if(!fault){
   CHECK(heldTicks>=75);
   CHECK(f->Report().find("\"support_only\":true")!=std::string::npos);
   CHECK(f->Report().find("\"rifle_support_released\":true")!=std::string::npos);
  }else CHECK(f->driver.CancelConsumer());
  if(fault>=2)CHECK(faultApplied);
 }
 auto started=std::make_unique<Loop>();started->Tick();CHECK(!started->driver.EnableSupportOnly());
 Bc2ResourceInventoryProbe mixed;CHECK(mixed.EnableMagazineOnly()&&!mixed.EnableSupportOnly());
 std::cout<<"Support-only draw/hold/release without magazine enrollment; missing contact, lost claim and changed weapon reject\n";
 return 0;
}
