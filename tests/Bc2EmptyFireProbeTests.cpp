#include "Bc2EmptyFireProbe.h"
#include "Bc2PumpDiagnostic.h"
#include <cstdio>
#define CHECK(x) do{if(!(x)){std::printf("line%d\n",__LINE__);return 1;}}while(0)
using namespace fvr::bc2;
struct Fixture {
 Bc2EmptyFireProbe p;Bc2AmmoReserveLease s;std::int64_t now=1000000000;
 Fixture(){s.identity.owner={0x10000,0x20000,0x30000,0x40000,1,1,1};s.identity.firing={0x50000,0x60000,0x70000};s.identity.serverPlayer=0x80000;s.identity.serverSoldier=0x90000;s.identity.serverItem=0xa0000;s.loaded=s.capacity=2;s.reserve=10;s.verified=s.allThreeIdle=true;s.reloadInputReady=false;}
 float Tick(std::int64_t advance=10000000,bool supported=true,bool safe=true){now+=advance;++s.sequence;s.observedNs=now;s.deadlineNs=now+100000000;return p.Tick(s.identity.owner,supported,safe,s,now);}
 void Arm(){Tick();Tick();}
};
int main(){
 {Fixture f;f.s.loaded=f.s.capacity=8;f.Arm();
  for(int remaining=7;remaining>=0;--remaining){
   f.s.loaded=remaining;f.s.allThreeIdle=false;f.Tick();
   const auto pulses=f.p.Pulses();
   for(int busy=0;busy<4;++busy)CHECK(f.Tick(350000000)==0);
   CHECK(f.p.Pulses()==pulses);
   f.s.allThreeIdle=true;
   if(remaining)CHECK(f.Tick()==1);
  }
  CHECK(f.Tick(Bc2EmptyFireProbe::ObserveNs)==0);
  CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Done);
  CHECK(f.p.Consumed()==8&&f.p.Pulses()==8);
 }
 {Fixture f;CHECK(f.Tick()==0);CHECK(f.Tick()==1);CHECK(f.p.Pulses()==1);CHECK(f.Tick(90000000)==0);--f.s.loaded;CHECK(f.Tick()==0);CHECK(f.Tick(260000000)==1);f.s.loaded=0;CHECK(f.Tick()==0);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Observe);CHECK(f.p.Consumed()==2);CHECK(f.Tick(Bc2EmptyFireProbe::ObserveNs)==0);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Done);}
 {Fixture f;f.Arm();++f.s.identity.owner.weapon;CHECK(f.Tick()==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Owner);}
 {Fixture f;f.Arm();CHECK(f.p.Tick(f.s.identity.owner,true,true,std::nullopt,f.now+1)==0);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Firing);CHECK(f.Tick(10000000)==1);}
 {Fixture f;f.Arm();CHECK(f.Tick(1,true,false)==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Input);}
 {Fixture f;CHECK(f.Tick(1,false)==0);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Preflight);CHECK(f.Tick(Bc2EmptyFireProbe::MaxNs)==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Timeout);}
 {Fixture f;f.Arm();f.s.loaded=0;f.Tick();f.s.loaded=2;CHECK(f.Tick()==0);CHECK(f.p.AutoReloadObserved());CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Counts);}
 {Fixture f;f.Arm();for(unsigned n=0;n<4;++n)f.Tick(350000000);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::ShotBound);CHECK(f.p.Pulses()==4);}
 {Fixture f;f.Tick();++f.s.reserve;CHECK(f.Tick()==0);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Preflight);CHECK(f.Tick()==1);}
 {Fixture f;f.Arm();CHECK(f.p.Tick(f.s.identity.owner,true,true,f.s,f.now-1)==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Clock);}
 {Fixture f;f.Arm();f.p.Cancel();CHECK(f.Tick()==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Input);}
 {Fixture f;f.s.loaded=0;CHECK(f.Tick()==0);CHECK(f.Tick()==0);CHECK(f.p.Pulses()==0);}
 {Fixture f;f.Arm();f.s.reserve=9;CHECK(f.Tick()==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Counts);}
 {Fixture f;f.Arm();const auto gap=f.now+1;CHECK(f.p.Tick(f.s.identity.owner,true,true,std::nullopt,gap)==0);CHECK(f.p.Tick(f.s.identity.owner,true,true,std::nullopt,gap+Bc2EmptyFireProbe::GapNs)==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Evidence);}
 {Fixture f;f.Arm();CHECK(f.p.Tick(f.s.identity.owner,true,true,std::nullopt,f.now+1)==0);++f.s.identity.serverItem;CHECK(f.Tick()==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Owner);}
 {Fixture f;f.Arm();CHECK(f.p.Tick(f.s.identity.owner,true,true,std::nullopt,f.now+1)==0);f.s.loaded=3;CHECK(f.Tick()==0);CHECK(f.p.Reason()==Bc2EmptyFireProbe::Failure::Counts);}
 {Fixture f;f.Arm();f.s.loaded=0;f.Tick();f.Tick(2000000000);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Observe);CHECK(f.p.Tick(f.s.identity.owner,true,true,std::nullopt,f.now+1)==0);f.Tick(10000000);f.Tick(2000000000);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Observe);f.Tick(1000000000);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Done);}
 {Fixture f;f.Arm();auto expired=f.s;expired.deadlineNs=f.now+1;CHECK(f.p.Tick(f.s.identity.owner,true,true,expired,f.now+2)==0);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Firing);CHECK(f.Tick(10000000)==1);}
 {Fixture f;f.Arm();CHECK(f.p.Tick(f.s.identity.owner,true,true,std::nullopt,f.now+1)==0);--f.s.loaded;CHECK(f.Tick()==1);CHECK(f.p.Current()==Bc2EmptyFireProbe::Phase::Firing);CHECK(f.p.Consumed()==1);}
 {Fixture f;f.s.allThreeIdle=false;CHECK(f.Tick()==0);CHECK(f.Tick()==0);CHECK(f.p.Pulses()==0);}
 {Fixture f;f.s.reloadInputReady=false;CHECK(f.s.loaded==f.s.capacity);CHECK(f.Tick()==0);CHECK(f.Tick()==1);}
 constexpr auto normal=9u|0x197800u|0x2000000u;
 CHECK(ValidPumpHoldDiagnostic(PumpHoldDiagnostic::SelectedManualEmptyFire,normal,30000));
 CHECK(!ValidPumpHoldDiagnostic(PumpHoldDiagnostic::SelectedManualEmptyFire,normal|0x1000000u,30000));
 CHECK(!ValidPumpHoldDiagnostic(PumpHoldDiagnostic::SelectedManualEmptyFire,normal|0x400000u,30000));
 CHECK(!ValidPumpHoldDiagnostic(PumpHoldDiagnostic::SelectedManualEmptyFire,normal,15000));
 CHECK(!ValidPumpHoldDiagnostic(PumpHoldDiagnostic(3),normal,30000));
 CHECK(ValidPumpHoldDiagnostic(PumpHoldDiagnostic::SpasOneShot,9u|0x197800u|0x400000u,15000));
 for(const auto option:{L"--empty-fire-neutral",L"--pairs",L"--static-pose",L"--async",L"--capture-poses",L"--request-lifetime"})CHECK(EmptyFireReceiverOption(option));
 for(const auto option:{L"--controls-fire",L"--controls-shot-probe",L"--shot-reload",L"--magazine-full-return-probe",L"--controls-use",L"--controls-weapon-cycle",L"--support-prepare-shots"})CHECK(!EmptyFireReceiverOption(option));
 std::puts("20 empty-fire scheduler groups + selector/parser isolation passed");return 0;
}
