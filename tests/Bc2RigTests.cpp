#include "Test.h"
#include "Bc2Rig.h"
#include <cstring>
#include <limits>
using namespace fvr;
struct Memory {
 std::array<std::byte,0x10000> data{};
 static bool Read(void* p,std::uint32_t at,void* out,std::size_t n){auto& m=*static_cast<Memory*>(p);if(at<0x10000||at-0x10000>m.data.size()||n>m.data.size()-(at-0x10000))return false;std::memcpy(out,m.data.data()+at-0x10000,n);return true;}
 void Put(unsigned at,unsigned value){std::memcpy(data.data()+at-0x10000,&value,4);}
 void Init(){
  Put(0x11000,0x10004);data[0x475]=std::byte{1};Put(0x103b4,0x12000);Put(0x103b8,0x13000);Put(0x13004,0x10000);
  Put(0x12050,0x14000);Put(0x12054,0x15000);Put(0x12938,7);Put(0x14010,8);Put(0x15028,0x14000);Put(0x1501c,8);
  Put(0x15018,0x16000);Put(0x16000,8);Put(0x16004,0x18000);Put(0x14018,0x17000);Put(0x1401c,0x17020);
  Put(0x1400c,0x17100);Put(0x15010,0x17100);Put(0x16008,0x1a000);Put(0x1231c,0x1b000);Put(0x17104,8);Put(0x17108,0x17200);
  const char* names[]={"root","LeftArm","LeftForeArm","LeftHand","RightArm","RightForeArm","RightHand","jntWpn_1"};
  const int parents[]={-1,0,1,2,0,4,5,0};
  for(unsigned n=0;n<8;++n){Put(0x17000+n*4,0x19000+n*64);Put(0x17200+n*0x98,0x19000+n*64);Put(0x17204+n*0x98,unsigned(parents[n]));
   std::memcpy(data.data()+0x9000+n*64,names[n],std::strlen(names[n])+1);
   math::Matrix4 m{};for(unsigned a=0;a<4;++a)m.values[a][a]=1;std::memcpy(data.data()+0x7200+n*0x98+0x4c,&m,64);m.values[3][0]=float(n);m.values[3][2]=2;m.values[3][3]=0;std::memcpy(data.data()+0x8000+n*64,&m,64);std::memcpy(data.data()+0xa000+n*64,&m,64);m.values[3][1]=.125f;std::memcpy(data.data()+0xb000+n*64,&m,64);
  }
 }
};
int main(){Memory memory;memory.Init();bc2::RigMemory reader{&memory,Memory::Read};const auto original=memory.data;
 auto rig=bc2::ReadFirstPersonRig(reader,0x10000,0x11000);CHECK(rig&&rig->left.wrist==3&&rig->right.wrist==6&&rig->weaponBone==7);CHECK(rig->world[3].values[3][2]==-2);CHECK(memory.data==original);
 // Owner, both count sources, backlink, bone-name correspondence, parent cycles,
 // duplicate names, missing role and malformed affine poses must be rejected.
 for(auto edit:{std::pair{0x11000u,0x10008u},std::pair{0x15028u,0x14004u},std::pair{0x16000u,9u},std::pair{0x14010u,1025u},std::pair{0x17104u,9u},std::pair{0x17200u,0x19040u},std::pair{0x17204u,0u},std::pair{0x12938u,20u}}){memory.data=original;memory.Put(edit.first,edit.second);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));}
 memory.data=original;memory.data[0x9040]=std::byte{'X'};CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=original;memory.Put(0x18000,0x7fc00000);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=original;memory.Put(0x17200+2*0x98,0x19040);memory.Put(0x17008,0x19040);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=original;memory.data[0x299c]=std::byte{1};rig=bc2::ReadFirstPersonRig(reader,0x10000,0x11000);CHECK(rig&&rig->identity.nativeIk&&rig->identity.evaluatedMatrices==0x1b000&&Near(rig->evaluatedWorld[3].values[3][1],.125f));
 // Skin palette edits preserve padding and native animation; malformed or
 // duplicate writes never produce a partial usable plan.
 auto moved=rig->evaluatedWorld[3];moved.values[3][0]+=.25f;
 std::array<interaction::BoneWrite,1> writes{interaction::BoneWrite{3,moved}};
 // Non-float padding is legal and must be copied byte-for-byte.
 for(unsigned row=0;row<4;++row){const unsigned tag=0x7fc00042u+row;std::memcpy(rig->nativeEvaluated[3].data()+row*16+12,&tag,4);}
 const auto before=memory.data;auto plan=bc2::BuildRigPosePlan(*rig,writes);CHECK(plan&&plan->identity==rig->identity&&plan->edits.size()==1&&plan->edits[0].address==0x1b0c0&&memory.data==before);
 for(unsigned row=0;row<4;++row)CHECK(!std::memcmp(plan->edits[0].before.data()+row*16+12,plan->edits[0].after.data()+row*16+12,4));
 float x=0;std::memcpy(&x,plan->edits[0].after.data()+48,4);CHECK(Near(x,3.25f));
 const std::array<interaction::BoneWrite,2> duplicates{writes[0],writes[0]};CHECK(!bc2::BuildRigPosePlan(*rig,duplicates));writes[0].index=8;CHECK(!bc2::BuildRigPosePlan(*rig,writes));
 memory.data=original;memory.Put(0x1a038,0);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 // Observed hidden SPAS leaf is preserved without weakening arm/root checks.
 memory.data=original;
 for(auto at:{0x14010u,0x1501cu,0x16000u,0x17104u})memory.Put(at,9);
 memory.Put(0x1401c,0x17024);memory.Put(0x17020,0x19200);
 memory.Put(0x17200+8*0x98,0x19200);memory.Put(0x17204+8*0x98,7);
 std::memcpy(memory.data.data()+0x9200,"jntWpn_7",9);
 math::Matrix4 bind{};for(unsigned a=0;a<4;++a)bind.values[a][a]=1;
 std::memcpy(memory.data.data()+0x7200+8*0x98+0x4c,&bind,64);
 auto hidden=bind;hidden.values[0][0]=hidden.values[1][1]=hidden.values[2][2]=.0001f;
 for(unsigned offset:{0x8000u,0xa000u,0xb000u})std::memcpy(memory.data.data()+offset+8*64,&hidden,64);
 auto hiddenRig=bc2::ReadFirstPersonRig(reader,0x10000,0x11000);
 CHECK(hiddenRig&&hiddenRig->nativeHiddenLeaves==std::vector<std::uint32_t>{8});
 std::array<interaction::BoneWrite,1> hiddenWrite{interaction::BoneWrite{8,bind}};
 CHECK(!bc2::BuildRigPosePlan(*hiddenRig,hiddenWrite));
 const auto hiddenMemory=memory.data;
 memory.Put(0x17204+8*0x98,6);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=hiddenMemory;memory.data[0x9200]=std::byte{'X'};CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=hiddenMemory;memory.Put(0x18000+8*64,0x7fc00000);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=original;memory.data[0x475]=std::byte{0};CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));return 0;
}
