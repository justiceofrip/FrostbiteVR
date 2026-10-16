#include "TwoGrenadeFadeProbe.h"
#include "fvr/ipc/FrameChannel.h"
#include <cstdlib>
#include <iostream>
using namespace fvr;using namespace fvr::probe;
void Check(bool b,const char* text){if(!b){std::cerr<<text<<'\n';std::exit(1);}}
std::uint64_t Bits(float x){return std::bit_cast<std::uint32_t>(x);}
bc2::TwoGrenadeHealthWitness Health(std::uint64_t at,float current=100){return {1,99,1234,at,20,1,10,11,12,13,14,Bits(100),Bits(100),Bits(current),Bits(current),Bits(100)};}
bc2::TwoGrenadeNativeWitness Native(std::uint64_t at){return {1,99,1234,at*1000000,at*1000000+100000000,90,10,11,12,4,0x200000001ull,16,1,1,1,70,(at-400)*1000000,80,(at-200)*1000000,1,0,0,0,at};}
void First(TwoGrenadeFadeGesture& g){auto h=Health(2000);Check(g.Held(2000,2000000000,1000,true,99,1234,&h,nullptr),"initial admitted");g.ObservePublished(true,true,true,true,unsigned(ipc::ChannelResult::Ok));h=Health(2650);Check(!g.Held(2650,2650000000,1000,true,99,1234,&h,nullptr),"initial released");g.ObservePublished(false,false,true,true,unsigned(ipc::ChannelResult::Ok));}
void FirstHealthWindow402(){
 // Actual401 schedule: row27 atT+813; missed28..32 aroundT+1000;
 // coherent33 atT+1016, publication finished byT+1031.
 constexpr std::uint64_t start=497490265;
 for(const bool gapPublished:{false,true}){
  TwoGrenadeFadeGesture g;auto h=Health(start+813);h.index=27;
  if(gapPublished){h.tickMs=start+1000;h.index=28;h.valid=0;}
  Check(!g.Held(start+1000,std::int64_t((start+1000)*1000000),start,true,99,1234,&h,nullptr)&&
        !g.Aborted()&&!g.FirstAdmitted()&&g.Presses()==0,"stale or explicit missed slot keeps first admission undecided");
  h=Health(start+1016);h.index=32;h.valid=0;
  Check(!g.Held(start+1016,std::int64_t((start+1016)*1000000),start,true,99,1234,&h,nullptr)&&
        !g.Aborted()&&!g.FirstAdmitted(),"invalid catch-up row never starts hold");
  h=Health(start+1016);h.index=33;
  Check(g.Held(start+1031,std::int64_t((start+1031)*1000000),start,true,99,1234,&h,nullptr)&&
        g.FirstAdmitted()&&!g.Aborted(),"fresh original health within same first window admits exactly once");
  Check(h.tickMs==start+1016&&h.index==33,"original health witness remains unchanged");
  g.ObservePublished(true,true,true,true,unsigned(ipc::ChannelResult::Ok));h=Health(start+1650);
  Check(!g.Held(start+1650,std::int64_t((start+1650)*1000000),start,true,99,1234,&h,nullptr),"release stays at original absolute1650ms");
  g.ObservePublished(false,false,true,true,unsigned(ipc::ChannelResult::Ok));Check(g.Presses()==1&&g.Releases()==1,"no replay or additional edge for missed health slots");
 }
 {TwoGrenadeFadeGesture g;auto h=Health(start+1000);h.valid=0;
  Check(!g.Held(start+1000,std::int64_t((start+1000)*1000000),start,true,99,1234,&h,nullptr)&&!g.Aborted(),"invalid first slot waits without input");
  h.tickMs=start+1100;
  Check(!g.Held(start+1100,std::int64_t((start+1100)*1000000),start,true,99,1234,&h,nullptr)&&!g.Aborted(),"missing health at last admissible tick gives no authority");
  h=Health(start+1101);
  Check(!g.Held(start+1101,std::int64_t((start+1101)*1000000),start,true,99,1234,&h,nullptr)&&g.Aborted()&&!g.FirstAdmitted(),"fresh health after unchanged1100ms bound fails permanently");
  h=Health(start+1110);
  Check(!g.Held(start+1110,std::int64_t((start+1110)*1000000),start,true,99,1234,&h,nullptr)&&g.Presses()==0,"later recovery cannot retry expired first admission");
 }
 {TwoGrenadeFadeGesture g;
  Check(!g.Held(start+1000,std::int64_t((start+1000)*1000000),start,true,99,1234,nullptr,nullptr)&&!g.Aborted(),"unreadable witness waits within unchanged interval");
  auto h=Health(start+1100);
  Check(g.Held(start+1100,std::int64_t((start+1100)*1000000),start,true,99,1234,&h,nullptr),"existing inclusive1100ms boundary preserved");
 }
}

void PublicationDelivery402(){
 constexpr std::uint64_t start=1000;
 const auto ok=unsigned(ipc::ChannelResult::Ok),busy=unsigned(ipc::ChannelResult::Busy);
 {TwoGrenadeFadeGesture g;auto h=Health(2000);
  Check(g.Held(2000,2000000000,start,true,99,1234,&h,nullptr),"first scheduled hold");
  g.ObservePublished(true,true,true,false,busy);
  Check(!g.Aborted()&&g.Presses()==0&&g.Releases()==0,"Busy before first press is nondelivery only");
  h=Health(2031);Check(g.Held(2031,2031000000,start,true,99,1234,&h,nullptr),"same original hold survives Busy");
  g.ObservePublished(true,true,true,true,ok);
  Check(g.Presses()==1&&g.Releases()==0,"only actual successful first press counts");
  g.ObservePublished(true,true,true,false,busy);
  h=Health(2062);Check(g.Held(2062,2062000000,start,true,99,1234,&h,nullptr),"mid-hold Busy does not manufacture neutral");
  g.ObservePublished(true,true,true,true,ok);
  Check(g.Presses()==1&&g.Releases()==0&&!g.Aborted(),"successful held continuation adds no press");
  h=Health(2650);Check(!g.Held(2650,2650000000,start,true,99,1234,&h,nullptr),"Busy never extends absolute first release");
  g.ObservePublished(false,false,true,false,busy);
  Check(g.Presses()==1&&g.Releases()==0&&!g.Aborted(),"Busy at release delivers no release");
  h=Health(2670);Check(!g.Held(2670,2670000000,start,true,99,1234,&h,nullptr),"next loop remains neutral without re-press");
  g.ObservePublished(false,false,true,true,ok);
  Check(g.Presses()==1&&g.Releases()==1&&!g.Aborted(),"actual neutral alone records release");
  h=Health(6000,29);auto n=Native(6000);
  Check(g.Held(6000,6000000000,start,true,99,1234,&h,&n),"existing second evidence gate");
  const auto decision=g.SecondAt(),deadline=g.SecondNative().deadlineNs;
  g.ObservePublished(true,true,true,false,busy);
  h=Health(6020,29);Check(g.Held(6020,6020000000,start,true,99,1234,&h,nullptr),"second Busy keeps original hold");
  g.ObservePublished(true,true,true,true,ok);
  g.ObservePublished(true,true,true,false,busy);
  Check(g.SecondAt()==decision&&g.SecondNative().deadlineNs==deadline,"publication outcome never restamps original readiness");
  h=Health(6650,29);Check(!g.Held(6650,6650000000,start,true,99,1234,&h,nullptr),"second release still absolute decision plus650");
  g.ObservePublished(false,false,true,false,busy);
  Check(g.Presses()==2&&g.Releases()==1&&!g.Aborted(),"second busy release remains undelivered");
  g.ObservePublished(false,false,true,true,ok);
  h=Health(6670,29);n=Native(6670);
  Check(!g.Held(6670,6670000000,start,true,99,1234,&h,&n)&&g.Presses()==2&&g.Releases()==2,"no third hold or manufactured edge");
 }
 {TwoGrenadeFadeGesture g;auto h=Health(2000);Check(g.Held(2000,2000000000,start,true,99,1234,&h,nullptr),"undelivered hold begins");
  g.ObservePublished(true,true,true,false,busy);
  h=Health(2650);Check(!g.Held(2650,2650000000,start,true,99,1234,&h,nullptr),"missed entire hold cannot restart at deadline");
  g.ObservePublished(false,false,true,true,ok);
  Check(g.Presses()==0&&g.Releases()==0&&!g.Aborted(),"neutral after undelivered hold invents no transition or acceptance");
 }
 for(unsigned fault=0;fault<5;++fault){TwoGrenadeFadeGesture g;First(g);
  const bool known=fault!=0,success=fault==4;
  const unsigned code=fault==0||fault==4?busy:unsigned(fault==1?ipc::ChannelResult::Invalid:fault==2?ipc::ChannelResult::Closed:ipc::ChannelResult::Timeout);
  g.ObservePublished(true,true,known,success,code);
  Check(g.Aborted(),"unknown hard failure and inconsistent success remain rejected");
 }
 {TwoGrenadeFadeGesture g;First(g);g.ObservePublished(true,false,true,false,busy);
  Check(g.Aborted(),"independent menu filtering still cancels even with Busy nondelivery");
 }
}

int main(){
 PublicationDelivery402();
 FirstHealthWindow402();
 {bc2::TwoGrenadeLifetimeGuard g;Check(!g.Allows(0,2,3,4,5,0)&&!g.Pinned(),"invalid actor cannot pin");Check(g.Allows(1,2,3,4,5,0)&&g.Allows(1,2,3,4,5,1),"same actor allows first two edges");Check(!g.Allows(1,2,3,4,5,2)&&!g.Allows(1,2,3,4,5,3),"native cap forbids third even if receiver asks");}
 for(unsigned field=0;field<5;++field){bc2::TwoGrenadeLifetimeGuard g;Check(g.Allows(1,2,3,4,5,0),"initial owner");std::array<std::uint64_t,5> a{1,2,3,4,5};++a[field];Check(!g.Allows(a[0],a[1],a[2],a[3],a[4],1)&&g.Lost()&&!g.Allows(1,2,3,4,5,1),"actor or space turnover stays rejected");}
 {bc2::TwoGrenadeLifetimeGuard g;Check(g.Allows(1,2,3,4,5,1)&&!g.Allows(1,2,3,4,5,0)&&g.Lost(),"counter reset cannot rearm private fixture");}
 Check(TwoGrenadeFadeWindow(1000,12000,250)&&!TwoGrenadeFadeWindow(UINT64_MAX-11000,12000,250),"window bounds");
 Check(TwoGrenadeFadeRequest(999,1000,191)&&!TwoGrenadeFadeRequest(999,1000,192)&&TwoGrenadeFadeRequest(1000,1000,239)&&!TwoGrenadeFadeRequest(1000,1000,240),"reserved48 pairs");
 {TwoGrenadeFadeGesture g;First(g);auto h=Health(6600,29.9f);auto n=Native(6600);Check(g.Held(6600,6600000000,1000,true,99,1234,&h,&n),"fresh coherent damaged return admits second despite explicit earlier observation gap");g.ObservePublished(true,true,true,true,unsigned(ipc::ChannelResult::Ok));Check(g.SecondNative().gaps==1&&!g.SecondNative().returnNoGaps&&g.SecondAtNs()==6600000000ll,"gap and exact original QPC decision retained");h=Health(7249,29.9f);Check(g.Held(7249,7249000000,1000,true,99,1234,&h,nullptr),"hold persists after original witness expires");h=Health(7250,29.9f);Check(!g.Held(7250,7250000000,1000,true,99,1234,&h,nullptr),"second hold ends exactly650ms");g.ObservePublished(false,false,true,true,unsigned(ipc::ChannelResult::Ok));h=Health(7300,15);n=Native(7300);Check(!g.Held(7300,7300000000,1000,true,99,1234,&h,&n)&&g.Presses()==2&&g.Releases()==2,"no third gesture");}
 for(unsigned fault=0;fault<14;++fault){TwoGrenadeFadeGesture g;First(g);auto h=Health(6000,29);auto n=Native(6000);switch(fault){case 0:n.deadlineNs=6000000000;break;case 1:n.observedNs=6000000001;break;case 2:n.pid++;break;case 3:n.creation++;break;case 4:n.soldier++;break;case 5:n.returnInput=0;break;case 6:n.neutralInput=n.returnInput;break;case 7:n.neutralObservedNs=n.returnObservedNs;break;case 8:n.completed=2;break;case 9:n.edges=2;break;case 10:n.fault=1;break;case 11:n.phase=1;break;case 12:n.space++;break;case 13:n.attempts=2;break;}Check(!g.Held(6000,6000000000,1000,true,99,1234,&h,&n),"bad native readiness rejected");}
 for(unsigned fault=0;fault<9;++fault){TwoGrenadeFadeGesture g;First(g);auto h=Health(6000,29);auto n=Native(6000);switch(fault){case 0:h.tickMs=5899;break;case 1:h.tickMs=6001;break;case 2:h.valid=0;break;case 3:h.soldier++;break;case 4:h.client=Bits(0);h.server=h.client;break;case 5:h.client=Bits(100);h.server=h.client;break;case 6:h.client=0x7fc00000;break;case 7:h.client=Bits(29);h.server=Bits(30);break;case 8:h.client=Bits(101);h.server=h.client;break;}Check(!g.Held(6000,6000000000,1000,true,99,1234,&h,&n),"bad/zero/nondecreased health rejected");}
 {TwoGrenadeFadeGesture g;First(g);auto h=Health(6000,29);Check(!g.Held(6000,6000000000,1000,true,99,1234,&h,nullptr),"missing witness waits without action");h=Health(6501,29);auto n=Native(6501);Check(!g.Held(6501,6501000000,1000,true,99,1234,&h,&n)&&g.Aborted(),"damage wait expires permanently");}
 {TwoGrenadeFadeGesture g;First(g);auto h=Health(6000,29);auto n=Native(6000);h.soldier++;Check(!g.Held(6000,6000000000,1000,true,99,1234,&h,&n)&&g.Aborted(),"actor replacement sticky abort");h=Health(6001,29);n=Native(6001);Check(!g.Held(6001,6001000000,1000,true,99,1234,&h,&n),"recovery cannot erase owner loss");}
 {TwoGrenadeFadeGesture g;First(g);auto h=Health(6000,29);auto n=Native(6000);h.soldier++;h.valid=0;Check(!g.Held(6000,6000000000,1000,true,99,1234,&h,&n)&&g.Aborted(),"invalid replacement row cannot conceal changed owner");}
 {TwoGrenadeFadeGesture g;auto h=Health(2110);Check(!g.Held(2110,2110000000,1000,true,99,1234,&h,nullptr)&&g.Aborted(),"late initial gesture never retried");}
 {TwoGrenadeFadeGesture g;First(g);g.ObservePublished(true,false,true,true,unsigned(ipc::ChannelResult::Ok));auto h=Health(6000,29);auto n=Native(6000);Check(g.Aborted()&&!g.Held(6000,6000000000,1000,true,99,1234,&h,&n),"menu filtering permanently cancels recipe");}
 {TwoGrenadeFadeGesture g;First(g);auto h=Health(6000,0);auto n=Native(6000);Check(!g.Held(6000,6000000000,1000,true,99,1234,&h,&n)&&g.ZeroObserved(),"first throw zero requires no second");h=Health(6100,29);Check(!g.Held(6100,6100000000,1000,true,99,1234,&h,&n),"zero observation latch cannot rearm");}
 {TwoGrenadeFadeGesture g;auto h=Health(2000);Check(g.Held(2000,2000000000,1000,true,99,1234,&h,nullptr),"first hold begins");h=Health(2050,0);Check(!g.Held(2050,2050000000,1000,true,99,1234,&h,nullptr),"zero ends first hold");Check(!g.Held(2100,2100000000,1000,true,99,1234,nullptr,nullptr),"missing sample cannot restart hold after zero");h=Health(2150,20);Check(!g.Held(2150,2150000000,1000,true,99,1234,&h,nullptr),"recovered positive cannot restart hold after zero");}
 std::cout<<"TwoGrenadeFade focused policy checks passed\n";
}
