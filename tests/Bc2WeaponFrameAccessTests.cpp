#include "Bc2WeaponFrameAccess.h"
#include "Test.h"
using namespace fvr::bc2;
struct Frame {
 unsigned soldier=0x10000,weak=0x20000,weapon=0x30000;
 std::uint64_t owner=1,space=2,equipmentGeneration=3,generation=100;
 bool valid=true,leftTracked=true,weaponActionsBlocked=true;
};
int main(){
 Frame guard,pose;--pose.generation;
 const auto eligible=[&](WeaponFrameUse use){return WeaponFrameAdmitted(guard,pose,0x10000,0x20000,0x30000,use);};
 // A pending transfer hides firing only. Contact remains usable by the actual
 // support/claim consumer while both source and current left hand are tracked.
 CHECK(!eligible(WeaponFrameUse::Firing));CHECK(eligible(WeaponFrameUse::Support));
 CHECK(eligible(WeaponFrameUse::BodyObservation));
 for(unsigned fault=0;fault<10;++fault){guard={};pose={};
  if(fault==0)guard.valid=false;if(fault==1)guard.leftTracked=false;
  if(fault==2)pose.leftTracked=false;if(fault==3)++pose.soldier;
  if(fault==4)++pose.weak;if(fault==5)++pose.weapon;if(fault==6)++pose.owner;
  if(fault==7)++pose.space;if(fault==8)++pose.equipmentGeneration;if(fault==9)++pose.generation;
  CHECK(!eligible(WeaponFrameUse::Support));
 }
 guard={};pose={};guard.weaponActionsBlocked=false;
 CHECK(eligible(WeaponFrameUse::Firing)&&eligible(WeaponFrameUse::Support));
 return 0;
}
