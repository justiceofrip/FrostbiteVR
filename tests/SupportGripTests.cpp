#include "Test.h"
#include "fvr/interaction/SupportGrip.h"
#include <cstring>
#include <cmath>
#include <limits>
using namespace fvr;using namespace interaction;
int main(){
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
 CHECK(!send(1,true).holding);CHECK(send(1).engaged); // Genuine neutral survives temporary owner.
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
