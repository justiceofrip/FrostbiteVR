#include "Test.h"
#include "fvr/interaction/GripAttachment.h"
#include <limits>
using namespace fvr;using namespace interaction;
int main(){
 math::Matrix4 rest{};for(unsigned n=0;n<4;++n)rest.values[n][n]=1;rest.values[3][2]=.35f;
 GripAttachment g;GripAttachmentOwner owner{1,2,3,4,5};
 CHECK(!g.Update(owner,0,rest));
 CHECK(g.Update(owner,1,rest)->values==rest.values);
 auto animated=rest;animated.values[3][1]=.8f;animated.values[0]={0,1,0,0};animated.values[1]={-1,0,0,0};
 CHECK(g.Update(owner,1,animated)->values==rest.values); // Reload and pump move authored hand.
 animated.values[0][0]=std::numeric_limits<float>::quiet_NaN();
 CHECK(g.Update(owner,1,animated)->values==rest.values); // Latched contact has its own provenance.
 CHECK(!g.Update(owner,0,animated));CHECK(!g.Update(owner,2,animated));
 CHECK(g.Update(owner,2,rest)->values==rest.values);
 auto other=rest;other.values[3][0]=.1f;
 CHECK(g.Update(owner,3,other)->values==other.values);
 for(unsigned i=0;i<5;++i){
  auto changed=owner;
  if(i==0)++changed.actor;if(i==1)++changed.generation;if(i==2)++changed.item;
  if(i==3)++changed.skeleton;if(i==4)++changed.space;
  CHECK(g.Update(changed,3,rest)->values==rest.values);
  CHECK(g.Update(owner,3,other)->values==other.values);
 }
 owner.item=0;CHECK(!g.Update(owner,3,rest));
 return 0;
}
