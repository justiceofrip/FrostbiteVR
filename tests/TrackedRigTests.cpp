#include "Test.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/ComfortCamera.h"
#include "fvr/interaction/AimFrame.h"
#include <cmath>
#include <limits>
using namespace fvr;using namespace interaction;
math::Matrix4 At(float x,float y,float z){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;}
bool Close(const math::Matrix4& a,const math::Matrix4& b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(std::abs(a.values[r][c]-b.values[r][c])>.0002f)return false;return true;}
math::Vec3 Point(const math::Matrix4& m){return {m.values[3][0],m.values[3][1],m.values[3][2]};}
math::Vec3 Transform(math::Vec3 v,const math::Matrix4& m,float w=1){return {
 v.x*m.values[0][0]+v.y*m.values[1][0]+v.z*m.values[2][0]+w*m.values[3][0],
 v.x*m.values[0][1]+v.y*m.values[1][1]+v.z*m.values[2][1]+w*m.values[3][1],
 v.x*m.values[0][2]+v.y*m.values[1][2]+v.z*m.values[2][2]+w*m.values[3][2]};}
bool Close(math::Vec3 a,math::Vec3 b){return Near(a.x,b.x,.0002f)&&Near(a.y,b.y,.0002f)&&Near(a.z,b.z,.0002f);}
math::Quaternion Product(math::Quaternion a,math::Quaternion b){return {
 a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
 a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
int PhysicalHeadingRegression(){
 constexpr float radians=.017453292519943295f;
 const auto yaw=[](float angle){return math::Quaternion{0,std::sin(angle*.5f),0,std::cos(angle*.5f)};};
 InputFrame f{};f.generation=f.spaceGeneration=f.predictedNs=1;f.focused=f.headValid=true;
 for(auto& hand:f.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;}
 f.hands[0].grip.position={-.2f,-.3f,-.4f};f.hands[1].grip.position={.2f,-.3f,-.4f};
 const auto initial=f;const auto body=At(5,1.6f,10),left=At(4.8f,1.3f,10.4f),right=At(5.2f,1.3f,10.4f),gun=At(5.2f,1.3f,10.7f);
 const std::array<ArmAnchor,2> anchors{ArmAnchor{{4.8f,1.5f,10},{-.1f,-.2f,.1f}},ArmAnchor{{5.2f,1.5f,10},{.1f,-.2f,.1f}}};
 TrackedRig rig;const TrackedRigOwner owner{41,1,17,19};
 const auto start=rig.Update(owner,f,body,left,right,gun,anchors,body,body);CHECK(start);
 const std::array<std::int32_t,7> parents{-1,0,1,2,0,4,5};
 const std::array<math::Matrix4,7> native{body,At(4.8f,1.5f,10),At(4.7f,1.25f,10.2f),left,At(5.2f,1.5f,10),At(5.3f,1.25f,10.2f),right};
 const ArmJoints lj{1,2,3},rj{4,5,6};
 const auto initialSolved=SolveTrackedArms(parents,native,lj,rj,{left,anchors[0].poleDirection,anchors[0].shoulder},{right,anchors[1].poleDirection,anchors[1].shoulder});
 CHECK(initialSolved&&!initialSolved->reachClamped[0]&&!initialSolved->reachClamped[1]);
 auto initialPalette=native;for(const auto& w:initialSolved->writes)initialPalette[w.index]=w.transform;
 unsigned oldClamped=0;
 // A rigid physical turn carries anatomy and controllers together. Formerly
 // wrists turned while shoulders stayed behind (28.3 cm at 90 degrees here).
 for(int degrees=-180;degrees<=180;degrees+=15){
  f=initial;const float angle=degrees*radians;f.head.orientation=yaw(angle);
  const auto local=*MakeComfortCamera(At(0,0,0),float(-degrees));
  const auto expectedBody=Multiply(local,body);
  const auto worldDelta=Multiply(*InverseRigid(body),expectedBody);
  for(unsigned side=0;side<2;++side){auto pos=initial.hands[side].grip.position;pos.z=-pos.z;pos=Transform(pos,local);pos.z=-pos.z;
   f.hands[side].grip.position=pos;f.hands[side].grip.orientation=yaw(angle);f.hands[side].aim=f.hands[side].grip;}
  const auto torso=PhysicalTorsoFrame(f,body);CHECK(torso&&Close(*torso,expectedBody));
  const auto targets=rig.Update(owner,f,body,left,right,gun,anchors,*torso,body);CHECK(targets);
  CHECK(Close(targets->left,Multiply(start->left,worldDelta))&&Close(targets->right,Multiply(start->right,worldDelta)));
  CHECK(Close(targets->weapon,Multiply(start->weapon,worldDelta)));
  for(unsigned side=0;side<2;++side){CHECK(Close(targets->arms[side].shoulder,Transform(anchors[side].shoulder,worldDelta)));
   CHECK(Close(targets->arms[side].poleDirection,Transform(anchors[side].poleDirection,worldDelta,0)));}
  CHECK(targets->torso&&Close(*targets->torso,expectedBody));
  auto source=native;for(auto& m:source)m=Multiply(m,worldDelta);
  const auto solved=SolveTrackedArms(parents,source,lj,rj,{targets->left,targets->arms[0].poleDirection,targets->arms[0].shoulder},{targets->right,targets->arms[1].poleDirection,targets->arms[1].shoulder});
  CHECK(solved&&!solved->reachClamped[0]&&!solved->reachClamped[1]);
  const auto stranded=SolveTrackedArms(parents,source,lj,rj,{targets->left,anchors[0].poleDirection,anchors[0].shoulder},{targets->right,anchors[1].poleDirection,anchors[1].shoulder});CHECK(stranded);
  if(stranded->reachClamped[0]||stranded->reachClamped[1])++oldClamped;
  auto palette=source;for(const auto& w:solved->writes){palette[w.index]=w.transform;CHECK(Close(w.transform,Multiply(initialPalette[w.index],worldDelta)));}
  // Synthetic sleeve rings blended across upper/lower and lower/wrist joints
  // exercise skin palettes, not only joint origins. This is covariance evidence,
  // not a claim to reproduce BC2's unrecorded sleeve weights or native seam.
  for(auto pair:{std::array<unsigned,2>{1,2},std::array<unsigned,2>{2,3},std::array<unsigned,2>{4,5},std::array<unsigned,2>{5,6}})for(unsigned n=0;n<12;++n){
   const float phase=n*6.28318530718f/12;auto vertex=Point(native[pair[1]]);vertex.x+=.035f*std::cos(phase);vertex.z+=.035f*std::sin(phase);
   const auto blend=[&](const auto& bones){const auto a=Transform(vertex,Multiply(*InverseRigid(native[pair[0]]),bones[pair[0]]));const auto b=Transform(vertex,Multiply(*InverseRigid(native[pair[1]]),bones[pair[1]]));return math::Vec3{(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f};};
   CHECK(Close(blend(palette),Transform(blend(initialPalette),worldDelta)));
  }
  // Virtual snap belongs in the body base exactly once, including when physical
  // yaw and roomscale translation coexist. Grip/weapon calibration is unchanged.
  f.head.position={.13f,-.08f,.09f};
  const auto withoutSnap=PhysicalTorsoFrame(f,body);CHECK(withoutSnap);
  const auto withoutTargets=rig.Update(owner,f,body,left,right,gun,anchors,*withoutSnap,body);CHECK(withoutTargets);
  for(float snap:{-90.f,-30.f,30.f,90.f}){
   const auto snapped=*MakeComfortCamera(body,snap),delta=Multiply(*InverseRigid(body),snapped);
   const auto anatomy=PhysicalTorsoFrame(f,snapped);CHECK(anatomy&&Close(*anatomy,Multiply(*withoutSnap,delta)));
   const auto after=rig.Update(owner,f,snapped,left,right,gun,anchors,*anatomy,body);CHECK(after);
   CHECK(Close(after->left,Multiply(withoutTargets->left,delta))&&Close(after->right,Multiply(withoutTargets->right,delta)));
   CHECK(Close(after->weapon,Multiply(withoutTargets->weapon,delta)));
   for(unsigned side=0;side<2;++side)CHECK(Close(after->arms[side].shoulder,Transform(withoutTargets->arms[side].shoulder,delta)));
  }
 }
 CHECK(oldClamped>0);
 // Looking with the head must not rotate stationary controller/gun targets a
 // second time. Only anatomy changes, and no calibration is invalidated.
 f=initial;const auto before=rig.Update(owner,f,body,left,right,gun,anchors,body,body);CHECK(before);
 f.head.orientation=yaw(1.2f);const auto looked=PhysicalTorsoFrame(f,body);CHECK(looked);
 const auto after=rig.Update(owner,f,body,left,right,gun,anchors,*looked,body);CHECK(after&&!after->calibrated);
 CHECK(Close(before->left,after->left)&&Close(before->right,after->right)&&Close(before->weapon,after->weapon));
 // Pitch/roll alone leave upright heading intact, including vertical pitch;
 // neither head tilt nor its translation is applied to the shoulder basis.
 for(float heading:{-170.f,-90.f,0.f,90.f,170.f})for(float tilt:{-90.f,-45.f,0.f,45.f,90.f})for(bool roll:{false,true}){
  f=initial;const float half=tilt*radians*.5f;
  const math::Quaternion q=roll?math::Quaternion{0,0,std::sin(half),std::cos(half)}:math::Quaternion{std::sin(half),0,0,std::cos(half)};
  f.head.orientation=Product(yaw(heading*radians),q);
  const auto anatomy=PhysicalTorsoFrame(f,body);CHECK(anatomy&&Close(*anatomy,*MakeComfortCamera(body,-heading)));
 }
 // Recenter changes the XR reference and virtual base together. It must preserve
 // world anatomy, including when looking down at recenter time.
 f=initial;f.head.position={.4f,-.2f,.1f};f.head.orientation=Product(yaw(.9f),math::Quaternion{std::sin(.3f),0,0,std::cos(.3f)});
 const auto prior=PhysicalTorsoFrame(f,body);CHECK(prior);
 f.referenceHead=*UprightReference(f.head);++f.spaceGeneration;
 const auto recentered=PhysicalTorsoFrame(f,*prior);CHECK(recentered&&Close(*recentered,*prior));
 // Equivalent quaternion signs produce the same heading. Invalid/lost tracking
 // returns no anatomy rather than retaining another actor/space's orientation.
 for(float* v:{&f.head.orientation.x,&f.head.orientation.y,&f.head.orientation.z,&f.head.orientation.w})*v=-*v;
 const auto signedPose=PhysicalTorsoFrame(f,*prior);CHECK(signedPose&&Close(*signedPose,*prior));
 f.headValid=false;CHECK(!PhysicalTorsoFrame(f,body));f.headValid=true;
 f.head.orientation.x=std::numeric_limits<float>::quiet_NaN();CHECK(!PhysicalTorsoFrame(f,body));
 f=initial;f.focused=false;CHECK(!PhysicalTorsoFrame(f,body));
 f=initial;f.head.orientation={1,0,0,0};const auto upsideDown=PhysicalTorsoFrame(f,body);CHECK(upsideDown&&Close(*upsideDown,body));
 return 0;
}
int main(){
 CHECK(PhysicalHeadingRegression()==0);
 TrackedRig rig;TrackedRigOwner owner{1,2,3,4};InputFrame input{};input.generation=1;input.spaceGeneration=2;input.predictedNs=100;input.focused=input.headValid=true;
 for(auto& h:input.hands){h.gripTracked=h.aimTracked=true;h.active=Components;h.grip.position={.2f,-.3f,-.4f};}
 input.hands[0].grip.position.x=-.2f;
 std::array<ArmAnchor,2> anchors{ArmAnchor{{4.8f,1.5f,10},{-.1f,-.2f,.1f}},ArmAnchor{{5.2f,1.5f,10},{.1f,-.2f,.1f}}};
 auto body=At(5,1.6f,10),left=At(4.8f,1.3f,10.4f),right=At(5.2f,1.3f,10.4f),weapon=At(5.2f,1.3f,10.7f);
 auto p=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p&&p->calibrated&&Close(p->left,left)&&Close(p->right,right)&&Close(p->weapon,weapon));
 input.head.orientation={0,.70710678f,0,.70710678f};input.head.position={.4f,.2f,0};p=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p&&!p->calibrated&&Close(p->right,right));
 input.hands[1].grip.position.x+=.25f;p=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p&&Near(p->right.values[3][0],5.45f)&&Near(p->weapon.values[3][0],5.45f)&&Close(p->left,left));
 // Changing native shoulder/pole positions during weapon aiming must not move
 // the body anchors or the independent left target.
 const auto initialAnchors=p->arms;
 anchors[0].shoulder.x+=.4f;anchors[1].shoulder.z-=.3f;anchors[0].poleDirection={.2f,.1f,-.1f};
 auto anchored=rig.Update(owner,input,body,left,right,weapon,anchors);
 CHECK(anchored&&Near(anchored->arms[0].shoulder.x,initialAnchors[0].shoulder.x)&&Near(anchored->arms[0].poleDirection.x,initialAnchors[0].poleDirection.x)&&Close(anchored->left,left));
 // Native arm yaw changes must not double-rotate a controller target.
 // The gun keeps its calibrated attachment through native recoil and pump motion.
 auto changedRight=*MakeComfortCamera(right,30);auto changedWeapon=Multiply(Multiply(weapon,*InverseRigid(right)),changedRight);
 auto q=rig.Update(owner,input,body,left,changedRight,changedWeapon,anchors);CHECK(q&&Close(q->right,p->right)&&Close(q->weapon,p->weapon));
 input.hands[1].grip.orientation={0,0,.70710678f,.70710678f};p=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p&&std::abs(p->right.values[0][1])>.99f);
 CHECK(Near(p->right.values[3][0],5.45f)&&Near(p->right.values[3][1],1.3f)&&Near(p->right.values[3][2],10.4f));
 auto turned=*MakeComfortCamera(body,90);p=rig.Update(owner,input,turned,left,right,weapon,anchors);CHECK(p);
 const auto worldDelta=Multiply(*InverseRigid(body),turned);auto p0=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p0&&Close(p->right,Multiply(p0->right,worldDelta)));
 input.focused=false;CHECK(!rig.Update(owner,input,body,left,right,weapon,anchors));input.focused=true;input.hands[1].gripTracked=false;
 auto partial=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(partial&&partial->tracked[0]&&!partial->tracked[1]&&Close(partial->weapon,weapon));
 input.hands[1].gripTracked=true;
 input.hands[1].grip.position.x=10;partial=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(partial&&partial->tracked[0]&&!partial->tracked[1]);input.hands[1].grip.position.x=.4f;
 ++input.spaceGeneration;p=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p&&p->calibrated&&Near(p->right.values[3][0],5.4f)&&Near(p->arms[0].shoulder.x,anchors[0].shoulder.x));
 ++owner.generation;p=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p&&p->calibrated);const auto equippedPose=*p;
 ++owner.equipped;p=rig.Update(owner,input,body,At(40,30,20),At(41,30,20),At(41,30,20.3f),anchors);
 CHECK(p&&!p->calibrated&&Close(p->left,equippedPose.left)&&Close(p->right,equippedPose.right));
 // Tracking loss/recovery is per hand and never recalibrates a previously seen
 // grip. Invalid untracked payloads cannot leak into matrix math.
 const auto beforeLoss=*p;input.hands[0].gripTracked=false;
 const auto savedLeft=input.hands[0].grip;
 input.hands[0].grip.position.x=std::numeric_limits<float>::quiet_NaN();
 input.hands[1].grip.position.x+=.1f;
 p=rig.Update(owner,input,body,left,right,weapon,anchors);
 CHECK(p&&!p->tracked[0]&&p->tracked[1]&&!p->calibrated&&Near(p->right.values[3][0],beforeLoss.right.values[3][0]+.1f));
 input.hands[0].grip=savedLeft;input.hands[0].gripTracked=true;
 p=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p&&!p->calibrated&&Close(p->left,beforeLoss.left));
 input.hands[1].gripTracked=false;p=rig.Update(owner,input,body,left,right,weapon,anchors);
 CHECK(p&&p->tracked[0]&&!p->tracked[1]&&Close(p->left,beforeLoss.left)&&Close(p->weapon,weapon));
 input.hands[0].gripTracked=false;CHECK(!rig.Update(owner,input,body,left,right,weapon,anchors));
 // A new actor can start with one controller and calibrate the other later.
 ++owner.generation;input.hands[0].gripTracked=true;
 p=rig.Update(owner,input,body,left,right,weapon,anchors);CHECK(p&&p->calibratedHands[0]&&!p->calibratedHands[1]);
 input.hands[1].gripTracked=true;p=rig.Update(owner,input,body,left,right,weapon,anchors);
 CHECK(p&&!p->calibratedHands[0]&&p->calibratedHands[1]&&Near(p->right.values[3][0],5.5f));
 // Native reload/weapon sway cannot move a stationary player's hand base.
 TrackedBodyFrame stable;input.hands[0].gripTracked=input.hands[1].gripTracked=true;
 auto authored=At(5,1.6f,10);math::Vec3 actor{4.9f,0,10};
 auto fixed=stable.Update(owner,input,authored,actor);CHECK(fixed&&Close(*fixed,authored));
 auto sway=At(5.18f,1.55f,9.8f);fixed=stable.Update(owner,input,sway,actor);
 CHECK(fixed&&Near(fixed->values[3][0],5)&&Near(fixed->values[3][2],10)&&Near(fixed->values[3][1],1.55f));
 ++owner.equipped;fixed=stable.Update(owner,input,sway,actor);CHECK(fixed&&Near(fixed->values[3][0],5));
 actor.x+=1;fixed=stable.Update(owner,input,sway,actor);CHECK(fixed&&Near(fixed->values[3][0],6));
 fixed=stable.Update(owner,input,sway,actor,{.4f,0,0});CHECK(fixed&&Near(fixed->values[3][0],5.6f));
 auto yawBody=*MakeComfortCamera(sway,90);fixed=stable.Update(owner,input,yawBody,actor);
 CHECK(fixed&&Near(fixed->values[3][0],actor.x)&&Near(fixed->values[3][2],actor.z-.1f));
 fixed=stable.Update(owner,input,yawBody,actor,{.4f,0,0});CHECK(fixed&&Near(fixed->values[3][2],actor.z+.3f));
 ++input.spaceGeneration;fixed=stable.Update(owner,input,sway,actor);CHECK(fixed&&Close(*fixed,sway));
 auto invalid=actor;invalid.x=std::numeric_limits<float>::quiet_NaN();CHECK(!stable.Update(owner,input,sway,invalid));
 // Physical torso translation moves shoulders AND wrists together. Controller
 // travel remains in tracking space; collider lag must not create
 // false arm extension and clamp an otherwise unchanged physical arm pose.
 TrackedRig walking;TrackedRigOwner walkOwner{100,1,2,3};InputFrame walkInput=input;
 walkInput.hands[0].grip.position={-.2f,-.3f,-.4f};walkInput.hands[1].grip.position={.2f,-.3f,-.4f};
 auto walkBody=At(5,1.6f,10);std::array<ArmAnchor,2> walkAnchors{ArmAnchor{{4.8f,1.5f,10},{-.1f,-.2f,.1f}},ArmAnchor{{5.2f,1.5f,10},{.1f,-.2f,.1f}}};
 auto initial=walking.Update(walkOwner,walkInput,walkBody,left,right,weapon,walkAnchors,walkBody);CHECK(initial);
 auto actualBody=walkBody;actualBody.values[3][0]+=.4f;
 for(auto& h:walkInput.hands)h.grip.position.x+=.4f;
 auto walked=walking.Update(walkOwner,walkInput,walkBody,left,right,weapon,walkAnchors,actualBody);CHECK(walked&&!walked->calibrated);
 for(unsigned side=0;side<2;++side){CHECK(Near(walked->arms[side].shoulder.x-initial->arms[side].shoulder.x,.4f));}
 CHECK(Near(walked->left.values[3][0]-initial->left.values[3][0],.4f));CHECK(Near(walked->right.values[3][0]-initial->right.values[3][0],.4f));
 // A physical crouch likewise carries anatomy and both controllers vertically.
 actualBody.values[3][1]-=.5f;for(auto& h:walkInput.hands)h.grip.position.y-=.5f;
 auto crouched=walking.Update(walkOwner,walkInput,walkBody,left,right,weapon,walkAnchors,actualBody);CHECK(crouched);
 CHECK(Near(crouched->arms[0].shoulder.y-initial->arms[0].shoulder.y,-.5f));CHECK(Near(crouched->left.values[3][1]-initial->left.values[3][1],-.5f));
 walkInput.head.position={.4f,-.5f,0};walkInput.head.orientation={0,.70710678f,0,.70710678f};
 auto torso=PhysicalTorsoFrame(walkInput,walkBody);CHECK(torso&&Close(*torso,*MakeComfortCamera(actualBody,-90)));
 auto turnedTorso=PhysicalTorsoFrame(walkInput,*MakeComfortCamera(walkBody,90));CHECK(turnedTorso&&Near(turnedTorso->values[3][2],9.6f)&&Near(turnedTorso->values[3][1],1.1f));
 walkInput.headValid=false;CHECK(!PhysicalTorsoFrame(walkInput,walkBody));

 const auto restFrame=BindAnatomyFrame({0,1.7f,.09f},{.217f,1.508f,.097f},{-.217f,1.508f,.097f});
 CHECK(restFrame&&Near(restFrame->values[0][0],-1)&&Near(restFrame->values[2][2],-1));
 const auto restInverse=InverseRigid(*restFrame);
 const auto restLeft=Multiply(At(.217f,1.508f,.097f),*restInverse);
 CHECK(Near(restLeft.values[3][0],-.217f)&&Near(restLeft.values[3][1],-.192f)&&Near(restLeft.values[3][2],-.007f));
 CHECK(!BindAnatomyFrame({0,1.7f,0},{0,1.5f,0},{0,1.5f,0}));
 // Startup pose cannot offset tracked wrists. Recoil/pump cannot slide the gun
 // away from a stationary grip. Child animation is tested by subtree retarget.
 TrackedRig absolute;InputFrame absoluteInput{};absoluteInput.generation=1;absoluteInput.spaceGeneration=1;
 absoluteInput.predictedNs=1;absoluteInput.focused=absoluteInput.headValid=true;
 for(auto& h:absoluteInput.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
 absoluteInput.hands[0].grip.position={-.25f,-.45f,-.4f};absoluteInput.hands[1].grip.position={.25f,-.45f,-.4f};
 auto first=absolute.Update({101,1,2,3},absoluteInput,At(5,1.7f,10),At(3,1,9),At(7,1,9),At(7,1,9.3f),anchors,At(5,1.7f,10),At(5,1.45f,10));
 CHECK(first&&Near(first->left.values[3][0],4.75f)&&Near(first->right.values[3][0],5.25f)&&Near(first->right.values[3][1],1.25f)&&Near(first->right.values[3][2],10.4f));
 absoluteInput.predictedNs+=700000000;
 first=absolute.Update({101,1,2,3},absoluteInput,At(5,1.7f,10),At(3,1,9),At(7,1,9),At(7,1,9.3f),anchors,At(5,1.7f,10),At(5,1.45f,10));
 CHECK(first&&!first->weaponAttachmentPending);
 auto pump=absolute.Update({101,1,2,3},absoluteInput,At(5,1.7f,10),At(3,1,9),At(7,1,9.2f),At(7.1f,1.1f,9.8f),anchors,At(5,1.7f,10),At(5.2f,1.45f,10));
 CHECK(pump&&Close(pump->weapon,first->weapon)&&pump->torso&&first->torso&&Close(*pump->torso,*first->torso));
 // Walking beyond the original calibration position is valid when the hands
 // remain near the head; it must not drop an arm after 1.5 metres of roomscale.
 absoluteInput.head.position.x=2;for(auto& h:absoluteInput.hands)h.grip.position.x+=2;
 auto walk=absolute.Update({101,1,2,3},absoluteInput,At(5,1.7f,10),At(3,1,9),At(7,1,9.2f),At(7.1f,1.1f,9.8f),anchors,At(7,1.7f,10),At(5.2f,1.45f,10));
 CHECK(walk&&walk->tracked[0]&&walk->tracked[1]&&Near(walk->right.values[3][0],7.25f));
 // A recentered tracking origin leaves the same local physical hand positions.
 absoluteInput.referenceHead.position.x=2;++absoluteInput.spaceGeneration;
 auto centered=absolute.Update({101,1,2,3},absoluteInput,At(7,1.7f,10),At(3,1,9),At(7,1,9.2f),At(7.1f,1.1f,9.8f),anchors);
 CHECK(centered&&Near(centered->right.values[3][0],walk->right.values[3][0])&&Near(centered->left.values[3][0],walk->left.values[3][0]));
 // New actor/reference with only the off-hand available must not reuse an
 // old torso or cache a weapon attachment before its gun hand is available.
 TrackedRig delayed;InputFrame delayedInput=absoluteInput;delayedInput.head={};delayedInput.referenceHead={};
 for(auto& h:delayedInput.hands)h.grip.position={.2f,-.3f,-.4f};
 TrackedRigOwner delayedOwner{200,1,2,3};
 CHECK(delayed.Update(delayedOwner,delayedInput,body,left,right,weapon,anchors,body,At(5,1.4f,10)));
 ++delayedOwner.actor;delayedInput.hands[1].gripTracked=false;
 CHECK(delayed.Update(delayedOwner,delayedInput,body,left,right,weapon,anchors,body));
 delayedInput.hands[1].gripTracked=true;
 const auto later=delayed.Update(delayedOwner,delayedInput,body,left,right,At(5.2f,1.3f,10.9f),anchors,body,At(5,1.1f,10));
 CHECK(later&&later->torso&&Near(later->torso->values[3][1],1.1f)&&Near(later->weapon.values[3][2]-later->right.values[3][2],.5f));

 // Absolute aim cannot inherit an arbitrary initial animated wrist rotation.
 // Equipping publishes a new identity before the new weapon's pose arrives.
 TrackedRig equip;TrackedRigOwner equipOwner{501,1,20,4};InputFrame ei{};
 ei.generation=ei.spaceGeneration=1;ei.focused=ei.headValid=true;ei.predictedNs=1000000000;
 for(auto& h:ei.hands){h.gripTracked=h.aimTracked=true;h.active=Components;h.grip.position={.2f,-.3f,-.4f};}
 ei.hands[0].grip.position.x=-.2f;
 const auto gunAim=*MakeComfortCamera(At(0,0,0),25);
 auto er=*MakeComfortCamera(At(.2f,1.3f,.4f),-35),eg=Multiply(At(0,.14f,.26f),er);
 auto ep=equip.Update(equipOwner,ei,At(0,1.6f,0),left,er,eg,anchors,{},{},gunAim);
 CHECK(ep&&ep->weaponAttachmentPending);
 for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)CHECK(Near(ep->weapon.values[r][c],gunAim.values[r][c]));
 CHECK(Near(ep->right.values[3][0],.2f)&&Near(ep->right.values[3][1],1.3f));
 ei.predictedNs+=700000000;ep=equip.Update(equipOwner,ei,At(0,1.6f,0),left,er,eg,anchors,{},{},gunAim);
 CHECK(ep&&!ep->weaponAttachmentPending);
 ++equipOwner.equipped;ei.predictedNs+=10000000;
 CHECK(equip.Update(equipOwner,ei,At(0,1.6f,0),left,er,eg,anchors,{},{},gunAim)->weaponAttachmentPending);
 // The old pose persists 200 ms; the replacement has a 7 cm different grip.
 ei.predictedNs+=200000000;eg=Multiply(At(0,.07f,.21f),er);
 CHECK(equip.Update(equipOwner,ei,At(0,1.6f,0),left,er,eg,anchors,{},{},gunAim)->weaponAttachmentPending);
 ei.predictedNs+=500000000;ep=equip.Update(equipOwner,ei,At(0,1.6f,0),left,er,eg,anchors,{},{},gunAim);
 CHECK(ep&&!ep->weaponAttachmentPending);
 CHECK(Close(Multiply(ep->weapon,*InverseAnimatedTransform(ep->right)),Multiply(eg,*InverseAnimatedTransform(er))));
 const auto settled=*ep;
 // Recoil and recenter must not relearn a transient hand/weapon relation.
 ++ei.spaceGeneration;ei.predictedNs+=100000000;eg=Multiply(At(.1f,.3f,.5f),er);
 ep=equip.Update(equipOwner,ei,At(0,1.6f,0),left,er,eg,anchors,{},{},gunAim);
 CHECK(ep&&!ep->weaponAttachmentPending&&Close(ep->weapon,settled.weapon)&&Close(ep->right,settled.right));
 // The aim basis is absolute relative to the reference, including roll/body yaw.
 ei.hands[1].aim.orientation={0,0,.70710678f,.70710678f};
 auto aimFrame=TrackedAimFrame(ei,At(4,2,8));CHECK(aimFrame&&Near(aimFrame->values[0][1],1)&&Near(aimFrame->values[2][2],1));
 CHECK(Near(aimFrame->values[3][0],0)&&Near(aimFrame->values[3][1],0));
 ei.hands[1].aimTracked=false;CHECK(!TrackedAimFrame(ei,At(0,0,0)));

 // Compare the full rendered aim basis to the independently calculated native
 // controller yaw/pitch over compound pitch, yaw, roll and body heading.
 ei.hands[1].aimTracked=true;
 for(const auto orientation:{math::Quaternion{.2f,.3f,.1f,.9f},math::Quaternion{-.4f,.2f,-.3f,.8f},math::Quaternion{0,.70710678f,0,.70710678f}}){
  ei.hands[1].aim.orientation=orientation;
  const auto angles=ControllerAim(ei.referenceHead,ei.hands[1].aim);CHECK(angles);
  const auto aimBody=*MakeComfortCamera(At(4,2,8),63);
  const auto frame=TrackedAimFrame(ei,aimBody);CHECK(frame);
  const float yaw=angles->yaw+63*.017453292519943295f;
  CHECK(Near(frame->values[2][0],std::sin(yaw)*std::cos(angles->pitch)));
  CHECK(Near(frame->values[2][1],std::sin(angles->pitch)));
  CHECK(Near(frame->values[2][2],std::cos(yaw)*std::cos(angles->pitch)));
 }
 return 0;
}
