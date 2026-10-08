#include "Bc2BodyAmmo.h"
#include "Bc2MagazineConsumerFixture.h"
using namespace magazine_consumer_fixture;
int main(){
 for(int loaded:{30,27,0}){Fixture f;f.policy->EnableBodyAmmo();f.reserve.loaded=loaded;f.reserve.allThreeIdle=true;f.reserve.reloadInputReady=loaded>0&&loaded<30;
  f.Send();CHECK(f.result.bodyAmmo);BodyAmmoTracking t{*f.result.bodyAmmo,f.result.tracking,{}};CHECK(BodyAmmoFresh(t,f.now));
  CHECK(!f.result.reloadHeld&&f.starts==0&&f.submits==0&&f.reserve.loaded==loaded&&f.reserve.reserve==83);
  auto owners=BindMagazineOwners(f.s.input.owner,f.s.weapon,f.reserve,0,f.now,f.s.family);CHECK(owners);
  if(loaded==30)CHECK(!MagazineSupply(*owners,f.reserve,f.s.trackingEpoch,f.now)); // display cannot grant refill supply
  CHECK(!BodyAmmoFresh(t,f.now+101*Ms));
  if(loaded!=27){f.Send(true,true);CHECK(f.starts==0&&f.submits==0&&!f.result.reloadHeld);}
 }
 for(unsigned bad=0;bad<6;++bad){Fixture f;f.policy->EnableBodyAmmo();f.reserve.loaded=30;f.reserve.allThreeIdle=true;f.reserve.reloadInputReady=false;
  if(bad==0)f.reserve.reserve=0;
  if(bad==1)f.source=false;
  if(bad==2)++f.s.nativeOwner.weapon;
  if(bad==3)f.reserve.verified=false;
  if(bad==4){f.reserve.allThreeIdle=false;}
  if(bad==5)f.api.reserve=[](void*p)noexcept->std::optional<Bc2AmmoReserveLease>{auto&f=*static_cast<Fixture*>(p);auto a=f.reserve;a.deadlineNs=f.now;return a;};
  // Fixture copied API into real consumer at construction; replace it explicitly for changed expiry reader.
  if(bad==5){f.policy.emplace(true,f.api);f.policy->EnableBodyAmmo();}
  f.Send();CHECK(!f.result.bodyAmmo&&f.starts==0&&f.submits==0);
 }
 std::cout<<"Chest reserve display: actual magazine consumer full/partial/zero publish; zero reserve/missing/owner/unknown/nonidle/expired reject; no refill/start authority\n";
}

