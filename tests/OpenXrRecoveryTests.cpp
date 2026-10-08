#include "Test.h"
#include "fvr/xr/OpenXrHost.h"
#include "fvr/interaction/ControllerInput.h"
#include <Windows.h>
#include <iostream>
#include <cmath>
using namespace fvr;
struct Observe final:runtime::IFrameProvider {
 interaction::InputFrame latest{};unsigned invalid=0,released=0,focused=0,spaces=0,afterRecovery=0;
 std::uint64_t space=0;
 bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame& f,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override {
  if(f.spaceGeneration!=latest.spaceGeneration||f.referenceHead.position.x!=latest.referenceHead.position.x||f.referenceHead.orientation.y!=latest.referenceHead.orientation.y)++invalid;
  return false;
 }
 void PairConsumed(const graphics::PairTicket&,bool)noexcept override{}
 void UpdateInput(const interaction::InputFrame& f)noexcept override {
  latest=f;if(!interaction::ValidInput(f)){++invalid;return;}
  if(!f.focused){++released;for(const auto& h:f.hands)if(h.active||h.held||h.gripTracked||h.aimTracked)++invalid;return;}
  if(!f.headValid)return;
  ++focused;if(space!=f.spaceGeneration){if(space)++spaces;space=f.spaceGeneration;}
  const auto head=math::MakeRelativePose(f.referenceHead,f.head);
  if(!head||std::hypot(head->position.x,head->position.y,head->position.z)>.0001f)++invalid;
  for(unsigned side=0;side<2;++side)if(f.hands[side].gripTracked){
   const auto hand=math::MakeRelativePose(f.referenceHead,f.hands[side].grip);
   if(!hand||std::abs(hand->position.x-(side?.25f:-.25f))>.0001f||std::abs(hand->position.y+.45f)>.0001f||std::abs(hand->position.z+.4f)>.0001f)++invalid;
  }
  if(f.referenceHead.position.x>1)++afterRecovery;
 }
};
int wmain(int argc,wchar_t** argv){
 CHECK(argc==2);const auto path=std::filesystem::absolute(argv[1]);
 const auto dll=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(dll);
 const auto set=reinterpret_cast<void(*)(unsigned)>(GetProcAddress(dll,"FakeXrRecoveryScenario"));
 const auto errors=reinterpret_cast<unsigned(*)()>(GetProcAddress(dll,"FakeXrErrors"));CHECK(set&&errors);
 for(unsigned scenario=1;scenario<=6;++scenario){
  if(scenario==5)continue;
  set(scenario);Observe input;xr::HostOptions opt;opt.loaderPath=path;opt.probeOnly=false;opt.controllers=opt.roomscale=true;opt.seconds=8;opt.provider=&input;
  const auto r=xr::RunOpenXrHost(opt);
  std::cout<<"scenario="<<scenario<<" frames="<<r.endedFrames<<" automatic="<<r.automaticRecenters<<" total="<<r.recenters<<" spaces="<<input.spaces<<" invalid="<<input.invalid<<" released="<<input.released<<'\n';
  CHECK(r.okay&&r.endedFrames==260&&errors()==0&&input.invalid==0);
  CHECK(r.automaticRecenters==(scenario==3?0u:1u));
  CHECK(r.recenters==(scenario==3?1u:2u));CHECK(input.spaces==r.recenters);
  CHECK(input.focused>100&&input.released>0);
  if(scenario!=3)CHECK(input.afterRecovery>100);
  CHECK(r.userPresenceSupported==(scenario==6));if(scenario==6)CHECK(r.userPresenceEvents==3&&input.released>80);
 }
 set(0);FreeLibrary(dll);return 0;
}
