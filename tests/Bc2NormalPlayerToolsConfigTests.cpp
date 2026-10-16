#include "NativeProbeConfig.h"
#include "Test.h"
#include <cstddef>
#include <initializer_list>
#include <cstdio>
using namespace fvr::bc2;
namespace {
constexpr unsigned flags=9u|0x197800u|0x2000000u|0x10000000u|0x400u;
int DefaultsAndMatchedAbi(){
 NativeProbeConfig c;CHECK(sizeof(c)==1220&&c.bytes==1220&&c.normalPlayerKnife==0&&c.normalContextualTools==0);
 CHECK(offsetof(NativeProbeConfig,normalPlayerKnife)==1212&&offsetof(NativeProbeConfig,normalContextualTools)==1216);
 CHECK(ValidNormalPlayerTools(0,0,0,0,0,true,false,false));
 c.bytes=1212;CHECK(c.bytes!=sizeof(c));return 0;
}
int IndependentCompileAndRuntimeKnobs(){
 CHECK(ValidNormalPlayerTools(1,0,flags,123,3,false,true,false));
 CHECK(ValidNormalPlayerTools(0,1,flags,123,3,false,false,true));
 CHECK(ValidNormalPlayerTools(1,1,flags,123,3,false,true,true));
 CHECK(ValidNormalPlayerGrenades(1,flags,123,3,false,true));
 CHECK(!ValidNormalPlayerTools(1,0,flags,123,3,false,false,true));
 CHECK(!ValidNormalPlayerTools(0,1,flags,123,3,false,true,false));
 CHECK(!ValidNormalPlayerTools(2,0,flags,123,3,false,true,true));
 CHECK(!ValidNormalPlayerTools(0,2,flags,123,3,false,true,true));return 0;
}
int StrictRealHostAndNoFixture(){
 for(unsigned mask:{0x400u,0x800u,0x1000u,0x2000u,0x4000u,0x10000u,0x80000u,0x100000u,0x2000000u,0x10000000u})
  CHECK(!ValidNormalPlayerTools(1,1,flags&~mask,123,3,false,true,true));
 CHECK(!ValidNormalPlayerTools(1,1,flags,0,3,false,true,true));
 CHECK(!ValidNormalPlayerTools(1,1,flags,123,0,false,true,true));
 CHECK(!ValidNormalPlayerTools(1,1,flags,123,3,true,true,true));
 CHECK(!ValidNormalPlayerTools(1,1,flags|0x100u,123,3,false,true,true));
 CHECK(ValidNormalPlayerTools(1,1,flags|0x200u|0x200000u,123,3,false,true,true));return 0;
}
}
int main(){CHECK(!DefaultsAndMatchedAbi());CHECK(!IndependentCompileAndRuntimeKnobs());CHECK(!StrictRealHostAndNoFixture());
 std::puts("Three normal player tool configuration groups PASS.");}
