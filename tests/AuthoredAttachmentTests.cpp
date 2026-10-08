#include "Test.h"
#include "fvr/interaction/TrackedRig.h"
#include "CapturedAekEquip.h"
#include <cstdio>
using namespace fvr;using namespace interaction;
math::Matrix4 At(float x,float y,float z){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
bool Close(const math::Matrix4&a,const math::Matrix4&b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],.0003f))return false;return true;}
struct Fixture {
 TrackedRig rig;TrackedRigOwner owner{1013716752,2,128241728,0xa7f2,9};InputFrame f{};
 std::array<ArmAnchor,2> anchors{ArmAnchor{{-.2f,1.5f,0},{-.1f,-.2f,.1f}},ArmAnchor{{.2f,1.5f,0},{.1f,-.2f,.1f}}};
 Fixture(){f.generation=f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=Components;h.grip.position={.2f,-.3f,-.4f};}f.hands[0].grip.position.x=-.2f;}
 auto Update(const CapturedEquipPose&p){f.predictedNs=1000000000+p.offsetNs;++f.generation;return rig.Update(owner,f,At(0,1.6f,0),At(-.2f,1.3f,.4f),p.right,p.weapon,anchors);}
};
int main(){unsigned groups=0;
 {Fixture x;std::optional<TrackedRigPose> out;
  for(const auto& p:CapturedAekEquip)out=x.Update(p);
  CHECK(out);const auto& final=CapturedAekEquip[6];const auto expected=Multiply(final.weapon,*InverseAnimatedTransform(final.right));
  const auto got=Multiply(out->weapon,*InverseAnimatedTransform(out->right));
  float error=0;for(unsigned n=0;n<3;++n)error+=std::pow(got.values[3][n]-expected.values[3][n],2.f);
  std::printf("Captured AEK attachment translation error: %.6f m\n",std::sqrt(error));
  CHECK(Close(got,expected));CHECK(!out->weaponAttachmentPending);++groups;
 }
 {Fixture x;for(unsigned i=0;i<6;++i){const auto out=x.Update(CapturedAekEquip[i]);CHECK(out&&out->weaponAttachmentPending);}
  const auto ready=x.Update(CapturedAekEquip[6]);CHECK(ready&&!ready->weaponAttachmentPending);
  auto recoil=CapturedAekEquip[0];recoil.offsetNs=900000000;const auto same=x.Update(recoil);CHECK(same&&!same->weaponAttachmentPending&&Close(same->weapon,ready->weapon));
  ++x.f.spaceGeneration;recoil.offsetNs+=10000000;const auto recentered=x.Update(recoil);CHECK(recentered&&!recentered->weaponAttachmentPending&&Close(recentered->weapon,ready->weapon));++groups;
 }
 {Fixture x;const auto first=x.Update(CapturedAekEquip[0]);CHECK(first);++x.owner.equipmentGeneration;
  const auto fresh=x.Update(CapturedAekEquip[6]);CHECK(fresh&&fresh->weaponAttachmentPending&&!fresh->calibrated);
  auto later=CapturedAekEquip[6];later.offsetNs+=700000000;const auto settled=x.Update(later);CHECK(settled&&!settled->weaponAttachmentPending);++groups;
 }
 {Fixture x;CHECK(x.Update(CapturedAekEquip[0]));x.f.hands[1].gripTracked=false;
  const auto missing=x.Update(CapturedAekEquip[6]);CHECK(missing&&!missing->tracked[1]&&missing->weaponAttachmentPending);
  x.f.hands[1].gripTracked=true;auto p=CapturedAekEquip[6];p.offsetNs+=10000000;CHECK(x.Update(p)->weaponAttachmentPending);
  p.offsetNs+=160000000;CHECK(!x.Update(p)->weaponAttachmentPending);++groups;
 }
 std::printf("Authored attachment: %u groups passed\n",groups);return 0;
}
