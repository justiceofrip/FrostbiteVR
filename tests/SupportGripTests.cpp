#include "Test.h"
#include "fvr/interaction/SupportGrip.h"
#include "fvr/interaction/MechanismAimContinuation.h"
#include "fvr/interaction/TrackedRig.h"
#include <cstring>
#include <cmath>
#include <limits>
using namespace fvr;using namespace interaction;
#include "SupportGripPrimaryHandChecks.inc"
namespace {
math::Matrix4 AimAt(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
bool AimClose(const math::Matrix4& a,const math::Matrix4& b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],.0001f))return false;return true;}
math::Quaternion AimProduct(math::Quaternion a,math::Quaternion b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
 a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
bool AimClose(math::Quaternion a,math::Quaternion b){const float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;return std::abs(std::abs(dot)-1)<.0001f;}
struct AimContinuationFixture {
 InputFrame raw{},guided{};HandInteractionSample safety{};HandInteraction hands;SupportGrip support;MechanismAimContinuation continuation;
 HandClaim gun{},left{};std::uint64_t intent=0;
 bool Next(){++raw.generation;raw.predictedNs+=11000000;safety.sequence=raw.generation;
  safety.observedNs+=11000000;safety.deadlineNs+=11000000;safety.nowNs=safety.observedNs;
  if(!hands.Update(safety).inputValid)return false;
  if(gun.token.id){const auto r=hands.Renew(safety,gun.token,{gun.token.contact,safety.sequence,safety.deadlineNs,true});if(!r.claim)return false;gun=*r.claim;}
  if(left.token.id){const auto r=hands.Renew(safety,left.token,{left.token.contact,safety.sequence,safety.deadlineNs,true});if(!r.claim)return false;left=*r.claim;}return true;
 }
 bool Begin(){raw.generation=1;raw.spaceGeneration=4;raw.predictedNs=1000000000;raw.focused=raw.headValid=true;
  for(auto& h:raw.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
  raw.hands[0].grip.position.z=-.4f;raw.hands[1].squeeze=1;
  // The two controller orientation channels need not have the same axes.
  raw.hands[1].aim.orientation={std::sin(.08f),0,0,std::cos(.08f)};
  safety={{1,2,3,4},1,1000000000,1100000000,1000000000,true,{true,true}};
  hands.Update(safety);auto acquired=hands.Acquire(safety,{safety.owner,InteractionHand::Right,HandClaimKind::GunHold,{9,3},{{1,3},1,safety.deadlineNs,true},++intent,0});
  if(!acquired.claim)return false;gun=*acquired.claim;
  support.Update({1,2,9},raw,{true,0,{}});raw.hands[0].squeeze=1;if(!Next())return false;
  auto result=support.Update({1,2,9},raw,{true,0,{}});if(!result.engaged)return false;
  acquired=hands.Acquire(safety,{safety.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,gun.token.item,{{2,3},safety.sequence,safety.deadlineNs,true},++intent,gun.token.id});
  if(!acquired.claim)return false;left=*acquired.claim;
  raw.hands[0].grip.position.x=.15f;if(!Next())return false;
  result=support.Update({1,2,9},raw,{true,0,{}});if(!result.holding||result.correctionRadians<.3f)return false;
  guided=result.input;return continuation.Capture(raw,guided,safety,left,gun);
 }
 bool Transfer(){const auto r=hands.Transfer(safety,left.token,{safety.owner,InteractionHand::Left,HandClaimKind::Mechanism,
  gun.token.item,{{22,3},safety.sequence,safety.deadlineNs,true},++intent,gun.token.id});if(!r.claim)return false;left=*r.claim;return true;}
};
int MechanismGuidanceComposition(){
 AimContinuationFixture f;CHECK(f.Begin());const auto original=f.guided;CHECK(f.Transfer());
 const auto continued=f.continuation.Apply(f.raw,f.safety,f.left,f.gun);CHECK(continued&&f.continuation.Active());
 auto repeatedSafety=f.safety;repeatedSafety.nowNs+=1000;
 const auto repeated=f.continuation.Apply(f.raw,repeatedSafety,f.left,f.gun);CHECK(repeated);
 CHECK(!std::memcmp(&*continued,&*repeated,sizeof(InputFrame)));f.safety.nowNs=repeatedSafety.nowNs;
 CHECK(AimClose(continued->hands[1].grip.orientation,original.hands[1].grip.orientation));
 CHECK(AimClose(continued->hands[1].aim.orientation,original.hands[1].aim.orientation));
 auto preserved=*continued;preserved.hands[1].grip.orientation=f.raw.hands[1].grip.orientation;
 preserved.hands[1].aim.orientation=f.raw.hands[1].aim.orientation;CHECK(!std::memcmp(&preserved,&f.raw,sizeof(preserved)));
 // Exercise the production tracked-rig attachment and weapon-local raw
 // contact mapping. The former busy-hand fallback drops guidance and moves
 // the contact despite completely stationary physical controllers.
 TrackedRig rig;const TrackedRigOwner owner{1,2,9,11,3};const auto body=AimAt();
 const std::array<ArmAnchor,2> arms{ArmAnchor{{-.2f,0,0},{0,-1,0}},ArmAnchor{{.2f,0,0},{0,-1,0}}};
 const auto place=[&](const InputFrame& input){return rig.Update(owner,input,body,AimAt(.15f,0,.4f),body,AimAt(0,0,.2f),arms,
  {},{},TrackedAimFrame(input,body),AuthoredGripAttachment{AimAt(0,0,-.2f)});};
 const auto before=place(original),bad=place(f.raw),after=place(*continued);CHECK(before&&bad&&after);
 CHECK(AimClose(before->weapon,after->weapon)&&AimClose(before->right,after->right));
 const auto rawLeft=*InverseRigid(*math::MakeLhViewFromOpenXRPose(f.raw.hands[0].grip));
 const auto beforeContact=Multiply(rawLeft,*InverseRigid(before->weapon));
 const auto badContact=Multiply(rawLeft,*InverseRigid(bad->weapon));
 const auto afterContact=Multiply(rawLeft,*InverseRigid(after->weapon));
 CHECK(AimClose(beforeContact,afterContact));
 CHECK(std::hypot(beforeContact.values[3][0]-badContact.values[3][0],beforeContact.values[3][1]-badContact.values[3][1],beforeContact.values[3][2]-badContact.values[3][2])>.04f);
 // Once transferred, renewed exact custody may outlive the old support
 // seed. Current controller rotation still drives the gun normally.
 const auto delta=AimProduct({0,std::sin(.14f),0,std::cos(.14f)},{std::sin(.09f),0,0,std::cos(.09f)});
 f.raw.hands[1].grip.orientation=AimProduct(delta,f.raw.hands[1].grip.orientation);
 f.raw.hands[1].aim.orientation=AimProduct(delta,f.raw.hands[1].aim.orientation);
 for(unsigned n=0;n<15;++n){CHECK(f.Next());const auto next=f.continuation.Apply(f.raw,f.safety,f.left,f.gun);CHECK(next);
  CHECK(AimClose(next->hands[1].grip.orientation,AimProduct(delta,original.hands[1].grip.orientation)));
  CHECK(AimClose(next->hands[1].aim.orientation,AimProduct(delta,original.hands[1].aim.orientation)));}
 // Sliding the independent left hand never re-steers or rewrites raw poses.
 f.raw.hands[0].grip.position.z+=.094f;CHECK(f.Next());const auto moved=f.continuation.Apply(f.raw,f.safety,f.left,f.gun);CHECK(moved);
 CHECK(moved->hands[0].grip.position.z==f.raw.hands[0].grip.position.z&&
  AimClose(moved->hands[1].aim.orientation,AimProduct(delta,original.hands[1].aim.orientation)));
 // Initial seed expiry cannot authorize a fresh later mechanism grab.
 {AimContinuationFixture e;CHECK(e.Begin());for(unsigned n=0;n<11;++n)CHECK(e.Next());CHECK(e.Transfer());CHECK(!e.continuation.Apply(e.raw,e.safety,e.left,e.gun));}
 // BC2's trigger-hand gun hold is latched. A neutral squeeze is valid while
 // that exact GunHold remains live and the adapter reports no real release.
 {AimContinuationFixture e;CHECK(e.Begin());e.raw.hands[1].squeeze=e.guided.hands[1].squeeze=0;
  CHECK(e.continuation.Capture(e.raw,e.guided,e.safety,e.left,e.gun));CHECK(e.Transfer());
  CHECK(e.continuation.Apply(e.raw,e.safety,e.left,e.gun));}
 for(unsigned badCase=0;badCase<13;++badCase){AimContinuationFixture e;CHECK(e.Begin()&&e.Transfer());
  CHECK(e.continuation.Apply(e.raw,e.safety,e.left,e.gun));CHECK(e.Next());
  if(badCase==0)e.safety.released[0]=true;if(badCase==1)e.safety.released[1]=true;
  if(badCase==2)e.safety.tracked[0]=false;if(badCase==3)e.raw.hands[1].aimTracked=false;
  if(badCase==4)++e.safety.owner.space;if(badCase==5)++e.gun.token.id;
  if(badCase==6)++e.left.token.id;if(badCase==7)++e.left.token.item.generation;
  if(badCase==8)e.left.deadlineNs=e.safety.nowNs;if(badCase==9)e.raw.focused=false;
  if(badCase==10)e.raw.hands[0].squeeze=0;if(badCase==11)++e.left.token.prerequisiteClaim;
  if(badCase==12)e.raw.worldUnitsPerMeter=2;
  CHECK(!e.continuation.Apply(e.raw,e.safety,e.left,e.gun)&&!e.continuation.Active());
  // Failure retires the seed too: restoring the same packet cannot resurrect it.
  e.safety.released={};e.safety.tracked={true,true};e.raw.focused=e.raw.hands[1].aimTracked=true;
  CHECK(!e.continuation.Apply(e.raw,e.safety,e.left,e.gun));
 }
 for(unsigned badCase=0;badCase<5;++badCase){AimContinuationFixture e;CHECK(e.Begin());auto guided=e.guided;
  if(badCase==0)guided.hands[0].grip.position.x+=.01f;if(badCase==1)++guided.generation;
  if(badCase==2)guided.hands[1].held|=Primary;if(badCase==3)e.left.token.kind=HandClaimKind::Mechanism;
  if(badCase==4)e.left.deadlineNs=e.safety.nowNs;
  CHECK(!e.continuation.Capture(e.raw,guided,e.safety,e.left,e.gun));
 }
 std::puts("Support->mechanism guidance preserves gun/contact placement under real hand transfer; authority negatives passed");return 0;
}
int MechanismReturnKeepsGuidance(){
 AimContinuationFixture f;CHECK(f.Begin()&&f.Transfer());
 const auto guided=f.continuation.Apply(f.raw,f.safety,f.left,f.gun);CHECK(guided);
 f.support.Update({1,2,9},f.raw,{true,0,{}},false,true);
 CHECK(f.hands.Release(f.safety,f.left.token).accepted);f.left={};
 const auto submitted=f.safety.nowNs;
 CHECK(f.Next());const auto returning=f.continuation.ApplyReturning(f.raw,f.safety,f.gun,submitted);CHECK(returning);
 CHECK(AimClose(returning->hands[1].grip.orientation,guided->hands[1].grip.orientation));
 CHECK(AimClose(returning->hands[1].aim.orientation,guided->hands[1].aim.orientation));
 // Guidance cannot create custody. A separate fresh physical support return
 // acquires the actual claim, then uses that guided frame to seed normal aim.
 CHECK(!f.hands.Current(InteractionHand::Left));
 const auto acquired=f.hands.Acquire(f.safety,{f.safety.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,
  f.gun.token.item,{{2,3},f.safety.sequence,f.safety.deadlineNs,true},++f.intent,f.gun.token.id});CHECK(acquired.claim);
 f.left=*acquired.claim;
 const auto adopted=f.support.AdoptHeld({1,2,9},*returning,{true,0,{}},f.safety,f.safety,f.left,f.gun);
 CHECK(adopted.holding&&adopted.engaged);
 CHECK(f.continuation.Capture(f.raw,adopted.input,f.safety,f.left,f.gun));
 CHECK(f.Next());const auto held=f.support.Update({1,2,9},f.raw,{true,0,{}});
 CHECK(held.holding&&AimClose(held.input.hands[1].grip.orientation,guided->hands[1].grip.orientation));
 CHECK(AimClose(held.input.hands[1].aim.orientation,guided->hands[1].aim.orientation));
 for(unsigned bad=0;bad<8;++bad){AimContinuationFixture e;CHECK(e.Begin()&&e.Transfer());
  CHECK(e.continuation.Apply(e.raw,e.safety,e.left,e.gun));auto at=e.safety.nowNs;
  CHECK(e.Next());
  if(bad==0)e.safety.released[0]=true;if(bad==1)e.safety.tracked[1]=false;
  if(bad==2)++e.gun.token.id;if(bad==3)e.raw.referenceHead.position.x+=.01f;
  if(bad==4)at=e.safety.nowNs+1;if(bad==5)at-=200000001;
  if(bad==6)e.gun.deadlineNs=e.safety.nowNs;
  if(bad==7){CHECK(e.continuation.ApplyReturning(e.raw,e.safety,e.gun,at));++at;}
  CHECK(!e.continuation.ApplyReturning(e.raw,e.safety,e.gun,at)&&!e.continuation.Active());
 }
 {AimContinuationFixture e;CHECK(e.Begin());CHECK(!e.continuation.ApplyReturning(e.raw,e.safety,e.gun,e.safety.nowNs));}
 std::puts("Completed mechanism retains bounded aim through fresh support adoption; no synthetic custody or native completion");return 0;
}
}
int main(){
 if(CloseHandSupportChecks()||MechanismGuidanceComposition()||MechanismReturnKeepsGuidance())return 1;
 InputFrame input{};input.generation=1;input.spaceGeneration=2;input.predictedNs=1000000000;
 input.focused=input.headValid=true;for(auto& h:input.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
 input.hands[0].grip.position.z=-.4f;SupportGripOwner owner{1,2,3};SupportGripContact contact{true,0,{}};SupportGrip grip;
 const auto tick=[&]{++input.generation;input.predictedNs+=11000000;return grip.Update(owner,input,contact);};
 CHECK(!grip.Update(owner,input,contact).holding);
 input.hands[0].squeeze=.9f;auto out=tick();CHECK(out.holding&&out.engaged&&Near(out.correctionRadians,0)&&out.token>0);const auto firstToken=out.token;
 input.hands[0].grip.position.x=.15f;out=tick();CHECK(out.holding&&!out.engaged&&out.correctionRadians>.3f&&out.token==firstToken);
 CHECK(!std::memcmp(&out.input.hands[0],&input.hands[0],sizeof(ControllerState)));
 CHECK(!std::memcmp(&out.input.hands[1].grip.position,&input.hands[1].grip.position,sizeof(math::Vec3)));
 CHECK(out.input.hands[1].aim.orientation.y<-.1f);
 const auto supported=out.input.hands[1].aim.orientation;
 out=grip.Update(owner,input,contact);CHECK(out.holding&&!out.engaged&&Near(out.input.hands[1].aim.orientation.y,supported.y));
 input.hands[0].squeeze=.2f;out=tick();CHECK(!out.holding&&out.released&&!std::memcmp(&out.input,&input,sizeof(input)));
 input.hands[0].squeeze=.9f;CHECK(tick().holding);contact.distanceMeters=.4f;out=tick();CHECK(out.released&&!out.holding&&out.token==0&&out.reason==SupportRelease::Distance);
 contact.distanceMeters=0;CHECK(!tick().holding);input.hands[0].squeeze=0;tick();input.hands[0].squeeze=1;CHECK(tick().holding);
 input.hands[0].gripTracked=false;input.hands[0].grip.position.x=std::numeric_limits<float>::quiet_NaN();out=tick();CHECK(out.released&&!out.holding);
 input.hands[0].gripTracked=true;input.hands[0].grip.position.x=0;CHECK(!tick().holding);
 input.hands[0].squeeze=0;tick();input.hands[0].squeeze=1;CHECK(tick().holding);
 ++owner.equipped;out=tick();CHECK(out.released&&!out.holding);CHECK(!tick().holding);
 input.hands[0].squeeze=0;tick();input.hands[0].squeeze=1;CHECK(tick().holding);
 out=grip.Update(owner,input,contact,true);CHECK(out.released&&!out.holding);CHECK(!tick().holding);
 input.hands[0].squeeze=0;tick();input.hands[0].squeeze=1;CHECK(tick().holding);
 ++input.spaceGeneration;CHECK(!tick().holding);
 input.hands[0].squeeze=0;tick();input.hands[0].squeeze=1;CHECK(tick().holding);
 input.predictedNs+=300000000;CHECK(!tick().holding);
 grip.Reset();input.hands[0].grip.position={-.4f,0,0};contact.wristOffsetMeters={.4f,0,-.4f};
 input.hands[0].squeeze=0;tick();input.hands[0].squeeze=1;out=tick();CHECK(out.holding&&Near(out.correctionRadians,0)&&out.token>firstToken);
 input.hands[0].grip.position.x+=.1f;out=tick();CHECK(out.holding&&out.correctionRadians>.2f);
 contact.distanceMeters=std::numeric_limits<float>::quiet_NaN();CHECK(!tick().holding);contact.distanceMeters=0;
 input.hands[0].squeeze=0;input.hands[0].active&=~Squeeze;CHECK(!tick().holding);

 // Same support in a recentered/yawed XR space gives the same relative result.
 InputFrame turned{};turned.generation=1;turned.spaceGeneration=1;turned.predictedNs=1000000000;turned.focused=turned.headValid=true;
 const float half=std::sqrt(.5f);turned.referenceHead.orientation=turned.head.orientation={0,half,0,half};
 for(auto& h:turned.hands){h.gripTracked=h.aimTracked=true;h.active=Components;h.grip.orientation=h.aim.orientation=turned.referenceHead.orientation;}
 turned.hands[0].grip.position={-.4f,0,0};SupportGrip yawed;SupportGripContact plain{true,0,{}};
 CHECK(!yawed.Update(owner,turned,plain).holding);
 turned.hands[0].squeeze=1;CHECK(!yawed.Update(owner,turned,plain).engaged); // same packet cannot create an edge
 ++turned.generation;turned.predictedNs+=11000000;CHECK(yawed.Update(owner,turned,plain).holding);
 turned.hands[0].grip.position.z=-.15f;++turned.generation;turned.predictedNs+=11000000;
 const auto yawedResult=yawed.Update(owner,turned,plain);
 CHECK(yawedResult.holding&&Near(yawedResult.correctionRadians,std::atan(.15f/.4f),.0001f));
 const auto relative=math::MakeRelativePose(turned.referenceHead,yawedResult.input.hands[1].aim);
 CHECK(relative&&Near(relative->orientation.y,supported.y,.0001f));
 turned.worldUnitsPerMeter=2;++turned.generation;CHECK(!yawed.Update(owner,turned,plain).holding);
 turned.hands[0].squeeze=0;++turned.generation;yawed.Update(owner,turned,plain);
 turned.hands[0].squeeze=.69f;++turned.generation;CHECK(!yawed.Update(owner,turned,plain).holding);
 turned.hands[0].squeeze=.71f;plain.distanceMeters=.17f;++turned.generation;CHECK(!yawed.Update(owner,turned,plain).holding);
 plain.distanceMeters=.1f;turned.hands[0].squeeze=0;++turned.generation;yawed.Update(owner,turned,plain);
 turned.hands[0].squeeze=1;++turned.generation;CHECK(yawed.Update(owner,turned,plain).holding);
 plain.distanceMeters=.27f;turned.hands[0].squeeze=.5f;++turned.generation;CHECK(yawed.Update(owner,turned,plain).holding);
 ++owner.generation;++turned.generation;CHECK(!yawed.Update(owner,turned,plain).holding);
 // A release observed while reload owns the hand must not require another
 // release after completion. Ownership blocks every acquisition while busy.
 SupportGrip recovering;InputFrame recover=input;recover.hands[0].active=Components;
 recover.hands[0].gripTracked=recover.hands[1].gripTracked=recover.hands[1].aimTracked=true;
 recover.hands[0].grip.position={0,0,-.4f};recover.hands[1].grip.position={};
 SupportGripContact available{true,0,{}};SupportGripOwner live{10,20,30};
 const auto send=[&](float squeeze,bool busy=false,bool hardCancel=false){++recover.generation;recover.predictedNs+=11000000;recover.hands[0].squeeze=squeeze;return recovering.Update(live,recover,available,hardCancel,busy);};
 CHECK(!send(0,true).holding);CHECK(send(1).engaged);
 CHECK(send(0,true).released);CHECK(!recovering.Update(live,recover,available,false,true).holding);
 CHECK(!send(1,true).holding);CHECK(!send(1).engaged); // Squeeze belongs to the magazine, not a queued support grab.
 CHECK(!send(1).holding); // Native completion cannot turn that squeeze into support.
 CHECK(!send(0).holding);CHECK(send(1).engaged); // Immediate explicit regrip still works.
 CHECK(send(0,true).released);CHECK(!send(.5f,true).holding);CHECK(!send(1).holding);
 CHECK(!send(0,true).holding);CHECK(send(1).engaged); // Neutral at handoff permits next squeeze.
 available.valid=false;CHECK(!send(0).holding);available.valid=true;CHECK(send(1).engaged);
 CHECK(send(0,false,true).released);CHECK(!send(1).holding); // Hard cancel cannot arm.
 CHECK(!send(0,true).holding);++recover.spaceGeneration;CHECK(!send(1).holding);
 CHECK(!send(0,true).holding);++live.equipped;CHECK(!send(1).holding);
 CHECK(!send(0,true).holding);recover.focused=false;CHECK(!send(0,true).holding);
 recover.focused=true;CHECK(!send(1).holding);
 CHECK(!send(0,true).holding);recover.hands[0].gripTracked=false;CHECK(!send(0,true).holding);
 recover.hands[0].gripTracked=true;CHECK(!send(1).holding);
 CHECK(!send(0,true).holding);recover.predictedNs+=300000000;CHECK(!send(1).holding);
 // A modified duplicate cannot supply new neutral proof.
 CHECK(!send(1,false,true).holding);CHECK(!send(1,true).holding);recover.hands[0].squeeze=0;
 CHECK(!recovering.Update(live,recover,available,false,true).holding);CHECK(!send(1).holding);
 // After genuine neutral, approach with squeeze held needs no second click.
 CHECK(!send(0).holding);available.distanceMeters=.3f;
 CHECK(!send(1).holding);available.distanceMeters=.1f;CHECK(send(1).engaged);
 // Losing an established grab still consumes it, rather than silently regrab.
 available.distanceMeters=.4f;CHECK(send(1).released);available.distanceMeters=.1f;CHECK(!send(1).holding);
 available.valid=false;CHECK(!send(0).holding);CHECK(!send(1).holding);
 available.valid=true;CHECK(send(1).engaged);
 CHECK(!send(0).holding);recover.hands[0].grip.position.z=-.05f;CHECK(!send(1).holding);
 recover.hands[0].grip.position.z=-.4f;CHECK(send(1).engaged);
 return 0;
}
