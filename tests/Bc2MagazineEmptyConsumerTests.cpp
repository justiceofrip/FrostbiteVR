#include "Bc2MagazineConsumerFixture.h"
#include "Bc2BodyAmmo.h"
using namespace magazine_consumer_fixture;
namespace {
bool InsertEmpty(Fixture& f){if(!f.Eject())return false;f.Send();f.s.bodyFromHand=Pose();
 const auto travel=f.profile->geometry->interaction.insertion.travelMeters;f.Send(true,false,-.1f);
 for(float p=-.08f;p<travel;p+=.02f){f.Send(true,false,p);if(f.submits)return true;}
 for(unsigned n=0;n<12;++n){f.Send(true,false,travel);if(f.submits)return true;}return false;}
bool ReturnEmpty(Fixture& f){const auto& c=f.profile->geometry->interaction;const auto t=c.insertion.travelMeters;
 f.Send();f.Send(true);if(f.starts!=1)return false;f.held=true;f.reserve.reloadInputReady=false;
 for(float d=.02f;d<c.pullMeters+.1f;d+=.02f)f.Send(true,false,t-d);
 for(float d=c.pullMeters+.1f;d>0;d-=.02f){f.Send(true,false,t-d);if(f.result.interaction.originalSeat)return true;}
 for(unsigned n=0;n<12;++n){f.Send(true,false,t);if(f.result.interaction.originalSeat)return true;}return false;}
int Replacement(const MagazineEquipmentProfile& profile){for(int available:{7,83}){
 Fixture f(true,profile,true);f.reserve.loaded=0;f.reserve.reserve=available;f.reserve.emptyReloadControlled=true;
 CHECK(InsertEmpty(f));CHECK(f.result.interaction.original&&f.result.interaction.original->rounds==0);
 CHECK(f.submits==1&&f.submitted&&f.submitted->reservedUnits==unsigned(std::min(30,available)));
 CHECK(f.reserve.loaded==0&&f.reserve.reserve==available); // Gestures never edit native ammo.
 f.Complete();f.Send();f.allowRetire=true;f.Send();f.Send();
 CHECK(f.policy->ProbeState(f.now).completed==1&&!f.policy->BlocksEquipment());
 CHECK(f.reserve.loaded==std::min(30,available)&&f.reserve.reserve==std::max(0,available-30));
 }return 0;}
int OriginalReturn(const MagazineEquipmentProfile& profile){Fixture f(true,profile,true);f.reserve.loaded=0;f.reserve.emptyReloadControlled=true;
 CHECK(ReturnEmpty(f));CHECK(f.result.interaction.original&&f.result.interaction.original->rounds==0&&f.submits==0);
 f.allowRetire=true;f.Send();CHECK(f.policy->BlocksEquipment());
 f.reserve.reloadInputReady=true;f.Send();CHECK(!f.policy->BlocksEquipment());
 CHECK(f.policy->ProbeState(f.now).originalReturns==1&&f.reserve.loaded==0&&f.reserve.reserve==83&&f.submits==0);return 0;}
int MissingProofAndUnavailableReserve(const MagazineEquipmentProfile& profile){for(unsigned bad=0;bad<9;++bad){Fixture f(true,profile,true);f.reserve.loaded=0;f.reserve.emptyReloadControlled=true;
 if(bad==0)f.reserve.emptyReloadControlled=false;if(bad==1)f.reserve.reserve=0;
 if(bad==2)f.reserve.reloadInputReady=false;if(bad==3)f.reserve.loaded=-1;if(bad==4)f.reserve.loaded=30;
 if(bad==5)f.s.input.focused=false;if(bad==6)++f.s.input.owner.space;
 if(bad==7)f.source=false;if(bad==8)f.familyFault=4;
 f.Send();f.Send(true);CHECK(f.starts==0&&f.submits==0&&!f.policy->BlocksEquipment());
 if(bad==0){CHECK(f.result.ordinaryReloadAllowed);f.reserve.emptyReloadControlled=true;f.Send();f.Send(true);CHECK(f.starts==1);}
 }return 0;}
int EmptyBodyAvailability(const MagazineEquipmentProfile& profile){Fixture f(true,profile,true);f.policy->EnableBodyAmmo(true);f.reserve.loaded=0;f.reserve.emptyReloadControlled=true;f.Send();
 CHECK(f.result.bodyAmmo&&f.result.bodyAmmo->source.objectUnits==30);
 BodyAmmoTracking body;body.visual=*f.result.bodyAmmo;body.magazine=f.result.tracking;CHECK(BodyAmmoFresh(body,f.now));
 f.Send(true);CHECK(f.starts==1&&!f.result.bodyAmmo);return 0;}
int CancellationKeepsCounts(const MagazineEquipmentProfile& profile){Fixture f(true,profile,true);f.reserve.loaded=0;f.reserve.emptyReloadControlled=true;CHECK(f.Eject());
 f.s.cancel=true;f.Send();CHECK(f.cancels==1&&f.policy->BlocksEquipment()&&f.submits==0);
 f.s.cancel=false;f.allowRetire=true;f.Send();f.Send();CHECK(!f.policy->BlocksEquipment());
 CHECK(f.reserve.loaded==0&&f.reserve.reserve==83&&!f.hands.Current(InteractionHand::Left));return 0;}
}
int main(){unsigned profiles=0;
 for(const auto& row:RegisteredMagazineNativeProfiles()){const auto p=FindMagazineEquipment(row.id);if(!p||!p->Ready())continue;
  if(Replacement(*p)||OriginalReturn(*p)||MissingProofAndUnavailableReserve(*p)||EmptyBodyAvailability(*p)||CancellationKeepsCounts(*p))return 1;++profiles;}
 CHECK(profiles>=1);
#ifdef FVR_EXPECT_GENERATED_MAGAZINE
 CHECK(profiles>=2); // Accepted XM8 plus at least one actually exercised optional geometry.
#endif
 std::cout<<"5 empty actual-consumer groups passed, profiles="<<profiles<<"\n";
}
