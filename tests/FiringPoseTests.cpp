#include "Test.h"
#include "fvr/interaction/TrackingMath.h"
#include "fvr/interaction/FiringPoseHistory.h"
#include <cmath>
#include <cstring>
#include <limits>
using namespace fvr;using namespace fvr::interaction;
math::Matrix4 Pose(float yaw,float x,float y,float z){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;
 m.values[0][0]=m.values[2][2]=std::cos(yaw);m.values[0][2]=-std::sin(yaw);m.values[2][0]=std::sin(yaw);m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;}
int main(){auto anchor=Pose(.4f,500,100,300),fire=Pose(.47f,500.1f,100.2f,299.9f),muzzle=Pose(1.2f,500.3f,99.8f,300.1f);
 const auto before=fire;auto shot=PlaceFireAtMuzzle(fire,muzzle,2);CHECK(shot&&!std::memcmp(&fire,&before,sizeof(fire)));
 CHECK(!std::memcmp(shot->values.data(),fire.values.data(),3*sizeof(fire.values[0])));
 CHECK(Near(shot->values[3][0],500.3f)&&Near(shot->values[3][1],99.8f)&&Near(shot->values[3][2],300.1f));
 // Idle, reload, recoil or equip animation can orient the attachment arbitrarily;
 // native aiming/recoil/spread remain byte-for-byte untouched by its orientation.
 for(float yaw:{-2.f,-.5f,0.f,2.5f}){muzzle=Pose(yaw,500.3f,99.8f,300.1f);shot=PlaceFireAtMuzzle(fire,muzzle,2);CHECK(shot&&!std::memcmp(shot->values.data(),fire.values.data(),3*sizeof(fire.values[0])));}
 // The final origin is exactly the attachment, with no second authored offset, with native direction.
 shot=PlaceFireAtMuzzle(anchor,muzzle,2);CHECK(shot);for(unsigned n=0;n<3;++n)CHECK(shot->values[3][n]==muzzle.values[3][n]);
 auto large=Pose(0,1000,1000,1000),offset=Pose(.2f,1000,1000,1060);CHECK(!PlaceFireAtMuzzle(offset,large,2));CHECK(PlaceFireAtMuzzle(offset,large,200));
 for(float invalid:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})CHECK(!PlaceFireAtMuzzle(fire,muzzle,invalid));
 for(unsigned which=0;which<2;++which){auto f=fire,m=muzzle;auto* bad=which==0?&f:&m;bad->values[1][1]=std::numeric_limits<float>::quiet_NaN();CHECK(!PlaceFireAtMuzzle(f,m,2));}
 FiringPoseHistory history;FiringPoseKey key{1,2,3,4,0};
 FiringPoseSample first{Pose(0,1,2,3),10,150},recoiled{Pose(.2f,1.1f,2.2f,3.3f),11,180};
 const auto a=history.Resolve(key,first,100),b=history.Resolve(key,recoiled,120);
 CHECK(a&&b&&b->generation==10&&b->deadline==150&&!std::memcmp(&a->muzzle,&b->muzzle,sizeof(a->muzzle)));
 CHECK(!history.Resolve(key,recoiled,150)); // no silent deadline refresh
 key.event=1;CHECK(history.Resolve(key,recoiled,151)->generation==11);
 CHECK(!history.Resolve(key,recoiled,140)); // reject backwards clock
 history.Reset();CHECK(history.Resolve(key,recoiled,120)->generation==11);
 for(unsigned change=0;change<4;++change){history.Reset();CHECK(history.Resolve(key,first,100));auto changed=key;
  if(change==0)++changed.actor;if(change==1)++changed.ownerGeneration;if(change==2)++changed.equipped;if(change==3)++changed.space;
  CHECK(history.Resolve(changed,recoiled,110)->generation==11);
 }
 history.Reset();for(unsigned n=0;n<100;++n){key.event=n;auto sample=first;sample.generation=n+1;CHECK(history.Resolve(key,sample,100)->generation==n+1);}
 auto invalid=first;invalid.muzzle.values[0][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!history.Resolve(key,invalid,110));
 CHECK(!history.Resolve({},first,100));CHECK(!history.Resolve(key,first,150));CHECK(!history.Resolve(key,first,0));
 return 0;
}
