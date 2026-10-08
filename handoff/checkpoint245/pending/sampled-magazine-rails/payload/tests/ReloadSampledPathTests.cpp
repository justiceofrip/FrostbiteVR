#include "fvr/interaction/ReloadInsertion.h"
#include "Test.h"
#include <cstdio>
#include <cstring>
#include <limits>
using namespace fvr;using namespace fvr::interaction;using namespace reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
math::Matrix4 Pose(float x=0,float y=0,float z=0,float angle=0){auto m=Identity();
 m.values[0][0]=m.values[1][1]=std::cos(angle);m.values[0][1]=std::sin(angle);m.values[1][0]=-std::sin(angle);
 m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;}
bool Near(const math::Matrix4& a,const math::Matrix4& b,float eps=1e-5f){
 for(unsigned n=0;n<4;++n)for(unsigned k=0;k<4;++k)if(std::abs(a.values[n][k]-b.values[n][k])>eps)return false;return true;}
ReloadInsertionProfile Profile(){ReloadInsertionProfile p;
 p.id=123;p.revision=4;p.family=ReloadInsertionFamily::Magazine;p.approach=ReloadInsertionApproach::RailContact;
 p.itemFromHand=Pose(.01f,.02f,-.03f,.4f);p.itemFromInsertion=Pose();p.weaponFromEntry=Pose();
 p.captureDistanceMeters=.015f;p.releaseDistanceMeters=.04f;p.postCaptureTravelMeters=.02f;
 p.captureAngleRadians=.3f;p.releaseAngleRadians=.6f;p.seatToleranceMeters=.001f;p.maxStepMeters=.03f;p.maxStepRadians=.4f;
 p.alignmentNs=40*Ms;p.seatDwellNs=20*Ms;p.maxSampleGapNs=100*Ms;p.maxGuidedNs=2000*Ms;
 p.pathCount=6;
 for(unsigned n=0;n<6;++n){const float v=float(5-n)/5;
  p.path[n].weaponFromItem=Pose(.04f*v*v,-.1f*v,.2f,.3f*v);
  if(n)p.path[n].arcMeters=p.path[n-1].arcMeters+Distance(p.path[n-1].weaponFromItem,p.path[n].weaponFromItem);}
 p.travelMeters=p.path[5].arcMeters;return p;
}
struct Fixture {
 ReloadInsertionProfile p;ReloadInsertion policy;ReloadInsertionSample s{};
 explicit Fixture(ReloadInsertionProfile profile=Profile()):p(profile),policy(p){
  s.identity={{1,2,3,4},{5,6},{7,8},9};s.profile={p.id,p.revision};s.nowNs=1000*Ms;
  s.focused=s.itemTracked=s.weaponTracked=s.held=s.eligible=true;
  s.itemClaim.token={10,s.identity.owner,InteractionHand::Left,HandClaimKind::AmmoObject,s.identity.item,{30,1},0};
  s.weaponClaim.token={11,s.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,s.identity.weapon,{31,1},0};
 }
 ReloadInsertionResult SendPose(const math::Matrix4& item,std::int64_t dt=10*Ms){
  ++s.sequence;s.geometrySequence=s.sequence;s.nowNs+=dt;s.observedNs=s.nowNs;s.deadlineNs=s.nowNs+p.maxSampleGapNs;
  s.itemClaim.inputSequence=s.weaponClaim.inputSequence=s.sequence;s.itemClaim.deadlineNs=s.weaponClaim.deadlineNs=s.deadlineNs;
  s.weaponFromHand=Multiply(p.itemFromHand,item);return policy.Update(s);
 }
 ReloadInsertionResult Send(double along,std::int64_t dt=10*Ms){return SendPose(InsertionItemPose(p,along),dt);}
};
int ShapeAndLegacy(){auto p=Profile();CHECK(ValidateReloadInsertionProfile(p));
 CHECK(Near(InsertionItemPose(p,0),p.path[0].weaponFromItem)&&Near(InsertionItemPose(p,p.travelMeters),p.path[5].weaponFromItem));
 for(unsigned bad=0;bad<11;++bad){auto q=p;
  if(bad==0)q.pathCount=1;if(bad==1)q.pathCount=33;if(bad==2)q.path[2]=q.path[1];
  if(bad==3)q.path[0].arcMeters=.001f;if(bad==4)q.path[3].arcMeters+=.001f;
  if(bad==5)q.path[2].weaponFromItem.values[2][2]=-1;if(bad==6)q.path[2].weaponFromItem.values[3][0]=std::numeric_limits<float>::quiet_NaN();
  if(bad==7)q.travelMeters+=.01f;if(bad==8)q.family=ReloadInsertionFamily::SingleShell;
  if(bad==9)q.orientation=ReloadInsertionOrientation::AxialSymmetry;
  if(bad==10){q.path[2].weaponFromItem=q.path[0].weaponFromItem;float arc=0;for(unsigned n=1;n<q.pathCount;++n){arc+=Distance(q.path[n-1].weaponFromItem,q.path[n].weaponFromItem);q.path[n].arcMeters=arc;}q.travelMeters=arc;}
  CHECK(!ValidateReloadInsertionProfile(q));
 }
 p.pathCount=0;p.itemFromInsertion=Pose(.02f,-.03f,.04f,.25f);p.weaponFromEntry=Pose(-.1f,.2f,.3f,-.4f);
 CHECK(ValidateReloadInsertionProfile(p));
 for(float along:{-.1f,0.f,.03f,p.travelMeters,.2f}){
  const auto legacy=Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,along),p.weaponFromEntry));
  const auto result=InsertionItemPose(p,along);CHECK(std::memcmp(&result,&legacy,sizeof(result))==0);
 }return 0;
}
int RotationAndProjection(){auto p=Profile();
 const auto a=Pose(0,0,0,3.13f),b=Pose(.2f,0,0,-3.13f);const auto mid=BlendRigid(a,b,.5);
 CHECK(Rigid(mid)&&Angle(mid,a)<.012f&&Angle(mid,b)<.012f&&std::abs(mid.values[3][0]-.1f)<1e-6f);
 CHECK(Near(BlendRigid(a,b,0),a)&&Near(BlendRigid(a,b,1),b));
 for(double along:{-.02,0.,.007,.04,.07,double(p.travelMeters),double(p.travelMeters)+.02}){
  const auto pose=InsertionItemPose(p,along);const auto projected=ProjectInsertionItem(pose,p);
  CHECK(projected&&std::abs(projected->along-along)<2e-6&&projected->distance<1e-6&&projected->angle<.001f&&Rigid(pose));
 }
 // The midpoint between different legs of a tight bend has no unique path
 // coordinate. A deterministic index tie cannot become gesture progress.
 p.pathCount=3;p.path[0].weaponFromItem=Pose(0,.1f);p.path[1].weaponFromItem=Pose(.1f,.1f);p.path[2].weaponFromItem=Pose(.1f,0);
 p.path[0].arcMeters=0;p.path[1].arcMeters=.1f;p.path[2].arcMeters=.2f;p.travelMeters=.2f;
 CHECK(ValidateReloadInsertionProfile(p));CHECK(!ProjectInsertionItem(Pose(.075f,.075f),p));
 Fixture corner(p);CHECK(corner.Send(.079).captured);
 // The raw chord is below the 3cm pose-step limit, but skipping around this
 // bend travels 4.2cm on the authored path. Arc continuity must reject it.
 CHECK(Distance(InsertionItemPose(p,.079),InsertionItemPose(p,.121))<p.maxStepMeters);
 CHECK(corner.Send(.121).reason==ReloadInsertionReason::PoseJump);
 p.path[0].weaponFromItem=Pose(.1f,0);p.path[2].weaponFromItem=Pose();
 p.path[2].arcMeters=.1f+std::sqrt(.02f);p.travelMeters=p.path[2].arcMeters;
 CHECK(!ValidateReloadInsertionProfile(p)); // inward/outward reversal of distance to seat
 return 0;
}
int MeasuredMotionAndSeat(){Fixture f;auto r=f.Send(0);CHECK(r.captured&&r.phase==ReloadInsertionPhase::Guided&&r.alignment==0);
 CHECK(Near(*r.rawItem,*r.guidedItem)&&Near(Multiply(f.p.itemFromHand,*r.guidedItem),*r.guidedHand));
 for(unsigned n=0;n<10;++n){r=f.Send(0);CHECK(!r.seat&&r.progress==0);}
 for(unsigned n=1;n<=6;++n){r=f.Send(double(f.p.travelMeters)*n/6);CHECK(!r.cancelled);}
 CHECK(!r.seat);r=f.Send(f.p.travelMeters);CHECK(!r.seat);r=f.Send(f.p.travelMeters);CHECK(r.seat&&r.progress==1);
 CHECK(Near(*r.guidedItem,f.p.path[f.p.pathCount-1].weaponFromItem)&&r.seat->operation==ReloadOperation::SeatMagazine);
 const auto id=r.seat->id;for(unsigned n=0;n<3;++n)CHECK(!f.Send(f.p.travelMeters).seat);
 f.s.held=false;++f.s.nowNs;r=f.policy.Update(f.s);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Released&&id==1);return 0;
}
int ReversalDuplicateAndOwnership(){Fixture f;f.Send(0);f.Send(.025);auto r=f.Send(.05);CHECK(r.phase==ReloadInsertionPhase::Guided);
 const auto previous=r.progress;r=f.Send(.03);CHECK(r.progress<previous&&!r.cancelled);
 const auto raw=*r.rawItem;f.s.weaponFromHand=Multiply(f.p.itemFromHand,f.p.path[5].weaponFromItem);f.s.nowNs+=20*Ms;
 r=f.policy.Update(f.s);CHECK(!r.seat&&Near(*r.rawItem,raw));
 r=f.Send(.08);CHECK(r.reason==ReloadInsertionReason::PoseJump&&!r.seat);
 Fixture expired;expired.Send(0);expired.s.nowNs=expired.s.deadlineNs;r=expired.policy.Update(expired.s);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::StaleInput);
 Fixture owner;owner.Send(0);++owner.s.weaponClaim.token.id;++owner.s.nowNs;r=owner.policy.Update(owner.s);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::IdentityChanged);
 Fixture skew;auto item=InsertionItemPose(skew.p,0);item.values[3][2]+=.03f;CHECK(!skew.SendPose(item).captured);
 return 0;
}
int OriginalWithdrawalAndReturn(){Fixture f;f.s.itemSource=ReloadInsertionItemSource::RetainedMagazine;
 f.s.itemClaim.token.kind=HandClaimKind::Mechanism;f.s.itemClaim.token.item=f.s.identity.weapon;f.s.itemClaim.token.prerequisiteClaim=f.s.weaponClaim.token.id;
 auto r=f.Send(f.p.travelMeters);CHECK(!r.captured);
 for(unsigned n=1;n<=6;++n){r=f.Send(double(f.p.travelMeters)*(6-n)/6);CHECK(!r.captured&&!r.seat);}
 r=f.Send(.025);CHECK(r.captured&&!r.seat);
 for(unsigned n=1;n<=5;++n)r=f.Send(.025+(f.p.travelMeters-.025)*n/5);
 CHECK(!r.seat);f.Send(f.p.travelMeters);r=f.Send(f.p.travelMeters);CHECK(r.seat&&Near(*r.guidedItem,f.p.path[5].weaponFromItem));return 0;
}
}
int main(){if(ShapeAndLegacy()||RotationAndProjection()||MeasuredMotionAndSeat()||ReversalDuplicateAndOwnership()||OriginalWithdrawalAndReturn())return 1;
 std::puts("Sampled magazine path: shape/adversarial, exact legacy poses, shortest rotations, projection, capture/seat, custody/freshness and original return passed.");return 0;}
