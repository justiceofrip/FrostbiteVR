#include "Test.h"
#include "fvr/interaction/MenuPointer.h"
using namespace fvr;
int ChordTests(){
 interaction::MenuPointer p;interaction::InputFrame i;i.generation=i.spaceGeneration=1;i.predictedNs=1000000000;
 i.focused=i.headValid=true;i.hands[0].gripTracked=i.hands[1].aimTracked=true;
 constexpr auto face=std::uint32_t(interaction::Primary|interaction::Secondary);
 i.hands[0].active=face|interaction::StickClick;i.hands[1].active=interaction::Trigger;
 interaction::MenuPointerFrame out;auto step=[&](std::int64_t ns=100000000){++i.generation;i.predictedNs+=ns;out=p.Update(i,nullptr,0,0);};
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
 // policy, followed by the unaffected normal Crouch mapping after release.
 interaction::ControllerActions actions;interaction::InputOwner owner{55,1,true,true};
 i.hands[0].held=0;step();filtered=i;actions.Update(filtered,owner,filtered.predictedNs);
 step();filtered=i;actions.Update(filtered,owner,filtered.predictedNs);
 i.hands[0].held=face;step();filtered=i;out.FilterGameplayInput(filtered);
 auto gameplay=actions.Update(filtered,owner,filtered.predictedNs);CHECK(!(gameplay.held&(interaction::Use|interaction::Crouch))&&!(gameplay.pressed&(interaction::Use|interaction::Crouch)));
 i.hands[0].held=interaction::Secondary;step();filtered=i;out.FilterGameplayInput(filtered);gameplay=actions.Update(filtered,owner,filtered.predictedNs);
 CHECK(!(gameplay.held&(interaction::Use|interaction::Crouch))&&!(gameplay.pressed&(interaction::Use|interaction::Crouch)));
 i.hands[0].held=0;step();actions.Update(i,owner,i.predictedNs);
 i.hands[0].held=interaction::Secondary;step();filtered=i;out.FilterGameplayInput(filtered);gameplay=actions.Update(filtered,owner,filtered.predictedNs);CHECK(gameplay.held&interaction::Crouch);
 return 0;
}
int main(){
 CHECK(ChordTests()==0);
 interaction::MenuPointer p;interaction::InputFrame i;i.generation=i.spaceGeneration=1;i.predictedNs=1000000000;i.focused=i.headValid=true;i.head.position.y=1.6f;
 i.hands[0].active=interaction::MenuClick;i.hands[1].active=interaction::Trigger|interaction::Secondary;i.hands[1].aimTracked=true;i.hands[1].aim.position={0,1.6f,0};
 ipc::MenuState menu{1,1,1,ipc::MenuMode::Menu,1280,720,1};
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
