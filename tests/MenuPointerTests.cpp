#include "Test.h"
#include "fvr/interaction/MenuPointer.h"
using namespace fvr;
int ChordTests(){
 interaction::MenuPointer p;interaction::InputFrame i;i.generation=i.spaceGeneration=1;i.predictedNs=1000000000;
 i.focused=i.headValid=true;i.hands[0].gripTracked=i.hands[1].aimTracked=true;
 constexpr auto face=std::uint32_t(interaction::Primary|interaction::Secondary);
 i.hands[0].active=face|interaction::StickClick;i.hands[1].active=interaction::Trigger;
 interaction::MenuPointerFrame out;auto step=[&](std::int64_t ns=100000000){++i.generation;i.predictedNs+=ns;
  if(i.menu.owner==interaction::InputMenuOwner::Gameplay){++i.menu.sequence;i.menu.deadlineNs=i.predictedNs+100000000;}
  out=p.Update(i,nullptr,0,0);};
 step();CHECK(out.control.toggle==0&&!out.suppressLeftFaceButtons);
 // Individual existing buttons remain available. The simultaneous chord owns
 // both buttons from its first frame, long before its half-second activation.
 i.hands[0].held=interaction::Primary;step();CHECK(!out.suppressLeftFaceButtons);
 i.hands[0].held=face;step();CHECK(out.suppressLeftFaceButtons&&out.control.toggle==0);
 auto filtered=i;filtered.hands[0].held|=interaction::StickClick;filtered.hands[1].trigger=.9f;
 out.FilterGameplayInput(filtered);CHECK(filtered.hands[0].held==interaction::StickClick);
 CHECK(filtered.hands[0].active==i.hands[0].active&&filtered.hands[1].trigger==.9f&&filtered.hands[0].gripTracked);
 for(int n=0;n<4;++n)step();CHECK(out.control.toggle==0);step();CHECK(out.control.toggle==1);
 for(int n=0;n<12;++n)step();CHECK(out.control.toggle==1&&out.suppressLeftFaceButtons);
 // A partial release cannot leak Crouch/Interact or re-arm the menu chord.
 i.hands[0].held=interaction::Secondary;step();filtered=i;out.FilterGameplayInput(filtered);CHECK(filtered.hands[0].held==0);
 i.hands[0].held=face;for(int n=0;n<8;++n)step();CHECK(out.control.toggle==1&&out.suppressLeftFaceButtons);
 i.hands[0].held=0;step();CHECK(!out.suppressLeftFaceButtons);
 i.hands[0].held=face;for(int n=0;n<6;++n)step();CHECK(out.control.toggle==2);
 // Loss of focus/space or a stalled clock requires complete neutral again.
 for(int fault=0;fault<4;++fault){
  i.hands[0].held=0;step();i.hands[0].held=face;step();step();
  if(fault==0){i.focused=false;step();i.focused=true;step();}
  if(fault==1){++i.spaceGeneration;step();}
  if(fault==2)step(200000000);
  if(fault==3)step(-100000000);
  for(int n=0;n<8;++n)step();CHECK(out.control.toggle==2&&out.suppressLeftFaceButtons);
 }
 i.hands[0].held=0;step();i.hands[0].held=face;step();
 for(int n=0;n<10;++n)step(0);CHECK(out.control.toggle==2);
 for(int n=0;n<5;++n)step();CHECK(out.control.toggle==3);
 // Lost button availability cannot count as releasing the owned chord.
 i.hands[0].active=interaction::Primary;i.hands[0].held=interaction::Primary;step();CHECK(out.suppressLeftFaceButtons);
 i.hands[0].active=face;i.hands[0].held=face;for(int n=0;n<7;++n)step();CHECK(out.control.toggle==3);
 i.hands[0].held=0;step();CHECK(!out.suppressLeftFaceButtons);
 // The actual Menu button stays usable independently of this fallback.
 i.hands[0].active|=interaction::MenuClick;step();i.hands[0].held=interaction::MenuClick;step();CHECK(out.control.toggle==4&&!out.suppressLeftFaceButtons);
 // Verify filtered simultaneous/partial chord samples against real gameplay
 // policy, followed by Y toggle-crouch and held left-stick sprint after release.
 interaction::ControllerActions actions;interaction::InputOwner owner{55,1,true,true};
 i.hands[0].active|=interaction::StickClick;i.hands[1].active|=interaction::StickClick;
 i.menu={interaction::InputMenuOwner::Gameplay,1,1,i.predictedNs+100000000};
 i.hands[0].held=0;step();filtered=i;actions.Update(filtered,owner,filtered.predictedNs);
 step();filtered=i;actions.Update(filtered,owner,filtered.predictedNs);
 i.hands[0].held=face;step();filtered=i;out.FilterGameplayInput(filtered);
 auto gameplay=actions.Update(filtered,owner,filtered.predictedNs);CHECK(!(gameplay.held&(interaction::Use|interaction::Crouch|interaction::Sprint))&&!(gameplay.pressed&(interaction::Use|interaction::Crouch|interaction::Sprint)));
 i.hands[0].held=interaction::Secondary;step();filtered=i;out.FilterGameplayInput(filtered);gameplay=actions.Update(filtered,owner,filtered.predictedNs);
 CHECK(!(gameplay.held&(interaction::Use|interaction::Crouch|interaction::Sprint))&&!(gameplay.pressed&(interaction::Use|interaction::Crouch|interaction::Sprint)));
 i.hands[0].held=0;step();actions.Update(i,owner,i.predictedNs);
 i.hands[0].held=interaction::Secondary;step();filtered=i;out.FilterGameplayInput(filtered);gameplay=actions.Update(filtered,owner,filtered.predictedNs);
 CHECK(!(gameplay.held&(interaction::Crouch|interaction::Sprint)));
 i.hands[0].held=0;step();gameplay=actions.Update(i,owner,i.predictedNs);CHECK(gameplay.held==interaction::Crouch&&gameplay.pressed==interaction::Crouch);
 i.hands[0].held=interaction::Secondary;step();gameplay=actions.Update(i,owner,i.predictedNs);CHECK(gameplay.held==interaction::Crouch&&!gameplay.pressed);
 i.hands[0].held=0;step();gameplay=actions.Update(i,owner,i.predictedNs);CHECK(!gameplay.held&&gameplay.released==interaction::Crouch);
 i.hands[0].held=interaction::StickClick;step();gameplay=actions.Update(i,owner,i.predictedNs);CHECK(gameplay.held==interaction::Sprint&&gameplay.pressed==interaction::Sprint);
 step();gameplay=actions.Update(i,owner,i.predictedNs);CHECK(gameplay.held==interaction::Sprint&&!gameplay.pressed);
 i.hands[0].held=0;step();gameplay=actions.Update(i,owner,i.predictedNs);CHECK(!gameplay.held&&gameplay.released==interaction::Sprint);
 return 0;
}
int PassiveTests(){
 interaction::MenuPointer pointer;interaction::InputFrame input;
 input.generation=input.spaceGeneration=1;input.predictedNs=1000000000;
 input.focused=input.headValid=true;input.head.position.y=1.6f;
 input.hands[0].active=interaction::MenuClick|interaction::Primary|interaction::Secondary;
 input.hands[1].active=interaction::Trigger|interaction::Secondary;
 input.hands[1].aimTracked=true;input.hands[1].aim.position={0,1.6f,0};
 ipc::MenuState state{1,1,1,ipc::MenuMode::Menu,1280,720,1};
 auto step=[&]{++input.generation;input.predictedNs+=100000000;return pointer.Update(input,&state,1920,1080);};
 CHECK(step().pointing);
 state.mode=ipc::MenuMode::Passive;state.inputReady=0; // Same epoch must still retire interactive arming.
 input.hands[0].held=interaction::MenuClick|interaction::Primary|interaction::Secondary;
 input.hands[1].held=interaction::Secondary;input.hands[1].trigger=.8f;
 for(unsigned n=0;n<8;++n){const auto frame=step();
  CHECK(frame.visible&&!frame.pointing&&frame.control.menuEpoch==state.epoch);
  CHECK(frame.control.flags==ipc::MenuFocused&&!frame.control.toggle&&!frame.control.cancel);
 }
 state.mode=ipc::MenuMode::Menu;state.inputReady=1;
 auto frame=step();CHECK(frame.visible&&frame.pointing&&!(frame.control.flags&ipc::MenuDown));
 CHECK(!frame.control.toggle&&!frame.control.cancel);
 // Leaving passive with buttons still held does not dispatch deferred actions.
 for(unsigned n=0;n<8;++n){frame=step();CHECK(!frame.control.toggle&&!frame.control.cancel&&!(frame.control.flags&ipc::MenuDown));}
 input.hands[0].held=input.hands[1].held=0;input.hands[1].trigger=0;step();
 input.hands[0].held=interaction::MenuClick;input.hands[1].held=interaction::Secondary;input.hands[1].trigger=.8f;
 frame=step();CHECK(frame.control.toggle==1&&frame.control.cancel==1&&(frame.control.flags&ipc::MenuDown));
 state.mode=ipc::MenuMode::Passive;state.inputReady=0;input.hands[1].aimTracked=false;
 frame=step();CHECK(frame.visible&&!frame.pointing&&frame.control.toggle==1&&frame.control.cancel==1);
 input.focused=false;frame=step();CHECK(!frame.visible&&frame.control.flags==0);
 input.focused=true;++input.spaceGeneration;frame=step();CHECK(frame.visible&&!frame.pointing&&frame.control.toggle==1&&frame.control.cancel==1);
 return 0;
}
int main(){
 CHECK(ChordTests()==0);
 CHECK(PassiveTests()==0);
 interaction::MenuPointer p;interaction::InputFrame i;i.generation=i.spaceGeneration=1;i.predictedNs=1000000000;i.focused=i.headValid=true;i.head.position.y=1.6f;
 i.hands[0].active=interaction::MenuClick;i.hands[1].active=interaction::Trigger|interaction::Secondary;i.hands[1].aimTracked=true;i.hands[1].aim.position={0,1.6f,0};
 ipc::MenuState menu{1,1,1,ipc::MenuMode::Menu,1280,720,1};
 // The host describes the complete captured raster at every aspect. Native
 // mapping/rejection is independent of presentation; no engine letterboxing
 // or logical-canvas coordinates are guessed in the XR pointer.
 for(const auto size:{std::array{1024u,768u},std::array{1280u,800u},std::array{2560u,1080u}}){
  interaction::MenuPointer aspect;auto input=i;
  auto frame=aspect.Update(input,&menu,size[0],size[1]);
  CHECK(frame.visible&&frame.pointing&&std::abs(frame.control.u-.5f)<.00001f&&std::abs(frame.control.v-.5f)<.00001f);
  CHECK(std::abs(frame.height/frame.width-float(size[1])/float(size[0]))<.00001f);
  input.hands[1].aimTracked=false;frame=aspect.Update(input,&menu,size[0],size[1]);
  CHECK(frame.visible&&!frame.pointing&&!(frame.control.flags&ipc::MenuDown));
 }
 auto r=p.Update(i,&menu,1920,1080);CHECK(r.visible&&r.pointing);CHECK(std::abs(r.control.u-.5f)<.00001f&&std::abs(r.control.v-.5f)<.00001f);
 CHECK(!(r.control.flags&ipc::MenuDown));++i.generation;i.predictedNs+=11000000;i.hands[1].trigger=.8f;r=p.Update(i,&menu,1920,1080);CHECK(r.control.flags&ipc::MenuDown);
 // Tracking loss releases and requires neutral, even if the trigger stays held.
 i.hands[1].aimTracked=false;r=p.Update(i,&menu,1920,1080);CHECK(!(r.control.flags&ipc::MenuDown));i.hands[1].aimTracked=true;r=p.Update(i,&menu,1920,1080);CHECK(!(r.control.flags&ipc::MenuDown));
 i.hands[1].trigger=0;p.Update(i,&menu,1920,1080);i.hands[1].trigger=.8f;r=p.Update(i,&menu,1920,1080);CHECK(r.control.flags&ipc::MenuDown);
 ++menu.epoch;r=p.Update(i,&menu,1920,1080);CHECK(!(r.control.flags&ipc::MenuDown));
 i.hands[0].held=interaction::MenuClick;r=p.Update(i,nullptr,0,0);CHECK(r.control.toggle==1&&!r.visible);r=p.Update(i,nullptr,0,0);CHECK(r.control.toggle==1);
 i.focused=false;r=p.Update(i,&menu,1920,1080);CHECK(r.control.flags==0&&!r.visible);i.focused=true;r=p.Update(i,&menu,1920,1080);CHECK(r.control.toggle==1);
 i.hands[0].held=0;p.Update(i,&menu,1920,1080);i.hands[0].held=interaction::MenuClick;r=p.Update(i,&menu,1920,1080);CHECK(r.control.toggle==2);
 ++i.spaceGeneration;r=p.Update(i,&menu,1920,1080);CHECK(r.control.toggle==2&&!(r.control.flags&ipc::MenuDown));
 menu.mode=ipc::MenuMode::Gameplay;r=p.Update(i,&menu,1920,1080);CHECK(!r.visible&&!(r.control.flags&ipc::MenuPoint));return 0;
}
