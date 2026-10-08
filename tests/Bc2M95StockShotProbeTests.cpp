#include "Bc2M95StockShotProbe.h"
#include "Bc2M95StockShotConfig.h"
#include <cstdlib>
#include <cstdio>
#include <map>
using namespace fvr;using namespace fvr::bc2;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
struct Fixture {
 Bc2M95StockShotProbe probe;interaction::ControllerActions actions;interaction::InputFrame input{};
 ReloadStateOwner owner{1,2,3,4,5,6,7};Bc2AmmoReserveLease lease{};std::int64_t now=1000000000;
 unsigned pressed=0,held=0;float trigger=0;bool exact=true,safe=true,missing=false;
 Fixture(){input.generation=1;input.spaceGeneration=7;input.predictedNs=now;input.focused=input.headValid=true;
  for(auto& h:input.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
  lease.identity.owner=owner;lease.identity.firing={0x10000,0x20000,0x30000};lease.identity.serverPlayer=11;lease.identity.serverSoldier=12;lease.identity.serverItem=13;
  lease.sequence=1;lease.loaded=5;lease.reserve=25;lease.capacity=5;lease.verified=lease.allThreeIdle=true;
 }
 void tick(bool advance=true){now+=10000000;if(advance)++input.generation;input.predictedNs=now;
  ++lease.sequence;lease.observedNs=now;lease.deadlineNs=now+100000000;
  trigger=probe.Tick(input,owner,safe,exact,missing?std::nullopt:std::optional{lease},now);
  auto mapped=input;for(auto& h:mapped.hands){h.held=0;h.squeeze=h.stickX=h.stickY=h.trigger=0;}
  mapped.hands[1].trigger=trigger;auto out=actions.Update(mapped,{owner.soldier,owner.equipGeneration,true,true},now);
  if(trigger<.75f){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}
  CHECK(!(out.held&~interaction::Fire)&&!(out.pressed&~interaction::Fire));
  if(out.pressed&interaction::Fire)++pressed;if(out.held&interaction::Fire)++held;
 }
 void arm(){for(unsigned n=0;n<210&&probe.Current()==Bc2M95StockShotProbe::Phase::Preflight;++n)tick();CHECK(trigger==1&&pressed==1);}
};
int main(){unsigned groups=0;
 {Fixture f;f.arm();f.lease.loaded=4;f.lease.allThreeIdle=false;
  for(unsigned n=0;n<20;++n)f.tick();CHECK(f.trigger==0&&f.pressed==1&&f.held<=10);
  f.lease.allThreeIdle=true;for(unsigned n=0;n<310;++n)f.tick();CHECK(f.probe.Completed()&&f.pressed==1);++groups;}
 {Fixture f;f.arm();for(unsigned n=0;n<30;++n)f.tick(false);CHECK(f.trigger==0&&f.pressed==1&&f.held<=10);++groups;}
 {Fixture f;f.exact=false;for(unsigned n=0;n<250;++n)f.tick();CHECK(f.trigger==0&&f.pressed==0);++groups;}
 {Fixture f;f.arm();f.owner.equipGeneration++;f.tick();CHECK(f.probe.Failed()&&f.trigger==0);++groups;}
 {Fixture f;f.arm();f.input.hands[0].held=interaction::Primary;f.tick();CHECK(f.probe.Failed()&&f.trigger==0);++groups;}
 {Fixture f;f.arm();f.lease.reserve++;f.tick();CHECK(f.probe.Failed()&&f.trigger==0);++groups;}
 {Fixture f;f.arm();f.lease.loaded=3;f.tick();CHECK(f.probe.Failed()&&f.trigger==0);++groups;}
 {Fixture f;f.arm();f.missing=true;for(unsigned n=0;n<22;++n)f.tick();CHECK(f.probe.Failed()&&f.pressed==1);++groups;}
 {Fixture f;f.arm();for(unsigned n=0;n<1200;++n)f.tick();CHECK(f.probe.Failed()&&f.pressed==1);++groups;}
 {CHECK(ValidM95StockShotSession(1,0x197809,15000,0,0,0,0));
  for(unsigned bit=0;bit<32;++bit)CHECK(!ValidM95StockShotSession(1,0x197809^(1u<<bit),15000,0,0,0,0));
  CHECK(!ValidM95StockShotSession(1,0x197809,30000,0,0,0,0));CHECK(!ValidM95StockShotSession(1,0x197809,15000,0,1,0,0));++groups;}
 {Fixture f;f.arm();f.input.generation=1;f.tick(false);CHECK(f.probe.Reason()==Bc2M95StockShotProbe::Failure::Sequence);++groups;}
 {ReloadObservedConfig c;c.weaponData=0x10000;c.firingData=0x20000;c.primaryFire=0x30000;c.ammoAddress=c.primaryFire+0x170;
  strcpy_s(c.assetName.data(),c.assetName.size(),"M95_sp");strcpy_s(c.assetPath.data(),c.assetPath.size(),"Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95");
  c.fireLogicType=c.reloadType=1;c.fireInputAction=8;c.reloadInputAction=29;c.baseCapacity=c.numberOfMagazines=5;
  c.reloadTime=6.9f;c.reloadThreshold=.67f;c.boltTime=2.3f;c.holdBoltUntilFireRelease=true;
  CHECK(M95StockShotConfig(c));c.holdBoltUntilZoomRelease=true;CHECK(!M95StockShotConfig(c));c.holdBoltUntilZoomRelease=false;
  c.baseCapacity=10;CHECK(!M95StockShotConfig(c));c.baseCapacity=5;c.boltTime=1.8f;CHECK(!M95StockShotConfig(c));c.boltTime=2.3f;
  strcpy_s(c.assetName.data(),c.assetName.size(),"M95_sp_s");CHECK(!M95StockShotConfig(c));++groups;}
 {Fixture f;f.arm();f.missing=true;f.tick();CHECK(f.trigger==0);f.missing=false;f.lease.loaded=4;
  for(unsigned n=0;n<30;++n){f.tick();CHECK(f.trigger==0);}CHECK(f.pressed==1);++groups;}
 std::printf("PASS %u M95 stock-shot groups; real ControllerActions, no native calls\n",groups);
}
