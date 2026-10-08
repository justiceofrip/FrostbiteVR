#include "Test.h"
#include <Windows.h>
#include <MinHook.h>
#include <cstdint>
#include "../include/fvr/graphics/D3D11PresentationPacing.h"
#include "../src/games/bc2/NativeViewAbi.h"
using ContextCamera=void(__thiscall*)(void*,void*,void*);
ContextCamera originalContextCamera;
using ProjectionContext=void*(__thiscall*)(void*,const void*);ProjectionContext originalProjectionContext;
using World=void(__thiscall*)(void*,void*);
using Prepare=void*(__thiscall*)(void*,void*,void*);
using Draw=void(__thiscall*)(void*,void*,void*,void*);
using Visibility=void(__thiscall*)(void*,void*,void*,void*,void*,void*,void*);
using Update=void(__thiscall*)(void*,void*,unsigned,float,void*,void*,void*);
Update originalUpdate;Visibility originalVisibility;
World originalWorld;Prepare originalPrepare;Draw originalDraw;
struct Data {unsigned world=0,prepare=0,draw=0,visibility=0,update=0,frame=0;float dt=0;void* request=nullptr;void* view=nullptr;void* prepared=nullptr;};
unsigned hooks=0;
__declspec(noinline) void* __fastcall FakeProjectionContext(void* self,void*,const void* projection){static_cast<Data*>(self)->view=const_cast<void*>(projection);return self;}
void* __fastcall HookProjectionContext(void* self,void*,const void* projection){return originalProjectionContext(self,projection);}
__declspec(noinline) void __fastcall FakeContextCamera(void* self,void*,void* primary,void* secondary){auto& d=*static_cast<Data*>(self);d.view=primary;d.prepared=secondary;}
void __fastcall HookContextCamera(void* self,void*,void* primary,void* secondary){originalContextCamera(self,primary,secondary);}

__declspec(noinline) void __fastcall FakeVisibility(void* self,void*,void* req,void* view,void* params,void* extra,void* out1,void* out2){auto& d=*static_cast<Data*>(self);++d.visibility;d.request=req;d.view=view;d.prepared=params;*static_cast<void**>(out1)=params;*static_cast<void**>(out2)=extra;}
void __fastcall HookVisibility(void* self,void*,void* req,void* view,void* params,void* extra,void* out1,void* out2){++hooks;originalVisibility(self,req,view,params,extra,out1,out2);}
__declspec(noinline) void __fastcall FakeWorld(void* self,void*,void* req){auto& d=*static_cast<Data*>(self);++d.world;d.request=req;}
__declspec(noinline) void* __fastcall FakePrepare(void* self,void*,void* req,void* view){auto& d=*static_cast<Data*>(self);++d.prepare;d.request=req;d.view=view;return view;}
__declspec(noinline) void __fastcall FakeDraw(void* self,void*,void* req,void* view,void* prepared){auto& d=*static_cast<Data*>(self);++d.draw;d.request=req;d.view=view;d.prepared=prepared;}
void __fastcall HookWorld(void* self,void*,void* req){++hooks;originalWorld(self,req);}
void* __fastcall HookPrepare(void* self,void*,void* req,void* view){++hooks;return originalPrepare(self,req,view);}
void __fastcall HookDraw(void* self,void*,void* req,void* view,void* prepared){++hooks;originalDraw(self,req,view,prepared);}
struct NativeViewFake {unsigned refs=0;bool active=true;const char* name=nullptr;};
__declspec(noinline) void* __fastcall FakeFactory(void* self,void*,const fvr::bc2::NativeStringRange* name){auto* v=static_cast<NativeViewFake*>(self);v->name=name->begin;return name->end>name->begin?self:nullptr;}
__declspec(noinline) unsigned __fastcall FakeAdd(void* self,void*){return ++static_cast<NativeViewFake*>(self)->refs;}
__declspec(noinline) unsigned __fastcall FakeRelease(void* self,void*){return --static_cast<NativeViewFake*>(self)->refs;}
__declspec(noinline) void __fastcall FakeSetActive(void* self,void*,bool active){static_cast<NativeViewFake*>(self)->active=active;}
__declspec(noinline) void __fastcall FakeUpdate(void* self,void*,void* req,unsigned frame,float dt,void* extra,void* out1,void* out2){auto& d=*static_cast<Data*>(self);++d.update;d.request=req;d.frame=frame;d.dt=dt;*static_cast<void**>(out1)=req;*static_cast<void**>(out2)=extra;}
void __fastcall HookUpdate(void* self,void*,void* req,unsigned frame,float dt,void* extra,void* out1,void* out2){++hooks;originalUpdate(self,req,frame,dt,extra,out1,out2);}
struct PresentCall {UINT interval=0,flags=0;unsigned calls=0;};
fvr::graphics::SwapChainPresentFn originalPresent=nullptr;
IDXGISwapChain* pacingTarget=nullptr;bool pacingActive=false;
__declspec(noinline) HRESULT STDMETHODCALLTYPE FakePresent(IDXGISwapChain* chain,UINT interval,UINT flags){
    auto& d=*reinterpret_cast<PresentCall*>(chain);d.interval=interval;d.flags=flags;++d.calls;return DXGI_ERROR_DEVICE_REMOVED;
}
HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* chain,UINT interval,UINT flags){
    return fvr::graphics::PresentDesktopForVr(originalPresent,chain,interval,flags,pacingActive&&chain==pacingTarget);
}
using GameplayUpdate=void(__thiscall*)(void*,float);GameplayUpdate originalGameplay;
__declspec(noinline) void __fastcall FakeGameplay(void* self,void*,float dt){auto& d=*static_cast<Data*>(self);d.dt=dt;++d.frame;}
void __fastcall HookGameplay(void* self,void*,float dt){originalGameplay(self,dt);}
using AnimationUpdate=std::uintptr_t(__thiscall*)(void*,float,float,bool);AnimationUpdate originalAnimation;
using AnimationEvaluate=std::uintptr_t(__thiscall*)(void*,float,bool);AnimationEvaluate originalEvaluate;
using AnimationPost=std::uintptr_t(__thiscall*)(void*,float);AnimationPost originalPost;
struct AnimationData {unsigned calls=0;float dt=0,alpha=0;bool full=false;};
__declspec(noinline) std::uintptr_t __fastcall FakeAnimation(void* self,void*,float dt,float alpha,bool full){auto& d=*static_cast<AnimationData*>(self);++d.calls;d.dt=dt;d.alpha=alpha;d.full=full;return reinterpret_cast<std::uintptr_t>(self)+17;}
std::uintptr_t __fastcall HookAnimation(void* self,void*,float dt,float alpha,bool full){return originalAnimation(self,dt,alpha,full);}
__declspec(noinline) std::uintptr_t __fastcall FakeEvaluate(void* self,void*,float dt,bool full){auto& d=*static_cast<AnimationData*>(self);++d.calls;d.dt=dt;d.full=full;return 0xabc001u+d.calls;}
std::uintptr_t __fastcall HookEvaluate(void* self,void*,float dt,bool full){return originalEvaluate(self,dt,full);}
__declspec(noinline) std::uintptr_t __fastcall FakePost(void* self,void*,float alpha){auto& d=*static_cast<AnimationData*>(self);++d.calls;d.alpha=alpha;return 0x123400u+d.calls;}
std::uintptr_t __fastcall HookPost(void* self,void*,float alpha){return originalPost(self,alpha);}
using PalettePack=std::uintptr_t(__cdecl*)(const float*,float*,unsigned);PalettePack originalPalettePack;unsigned paletteCalls=0;
__declspec(noinline) std::uintptr_t __cdecl FakePalettePack(const float* source,float* destination,unsigned count){++paletteCalls;for(unsigned n=0;n<count;++n)for(unsigned col=0;col<3;++col)for(unsigned row=0;row<4;++row)destination[n*12+col*4+row]=source[n*16+row*4+col];return reinterpret_cast<std::uintptr_t>(source)+(count*16+8)*sizeof(float);}
std::uintptr_t __cdecl HookPalettePack(const float* source,float* destination,unsigned count){return originalPalettePack(source,destination,count);}
using ClientShoot=std::uintptr_t(__thiscall*)(void*,void*,float,unsigned,void*);ClientShoot originalClientShoot;
struct ShotData {unsigned calls=0,primary=0;float dt=0;void* config=nullptr;void* context=nullptr;};
__declspec(noinline) std::uintptr_t __fastcall FakeClientShoot(void* self,void*,void* config,float dt,unsigned primary,void* context){auto& d=*static_cast<ShotData*>(self);++d.calls;d.config=config;d.dt=dt;d.primary=primary;d.context=context;return reinterpret_cast<std::uintptr_t>(config)^primary;}
std::uintptr_t __fastcall HookClientShoot(void* self,void*,void* config,float dt,unsigned primary,void* context){return originalClientShoot(self,config,dt,primary,context);}
using Compose=std::uintptr_t(__thiscall*)(void*,void*,const void*);Compose originalCompose;
unsigned composeCalls=0;
__declspec(noinline) std::uintptr_t __fastcall FakeCompose(void* self,void*,void* output,const void* rhs){++composeCalls;const auto* a=static_cast<const float*>(self);const auto* b=static_cast<const float*>(rhs);auto* out=static_cast<float*>(output);for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)out[r*4+c]=a[r*4+c]+b[r*4+c];return reinterpret_cast<std::uintptr_t>(output);}
std::uintptr_t __fastcall HookCompose(void* self,void*,void* output,const void* rhs){return originalCompose(self,output,rhs);}
int main(){
    volatile LONG borrowed=12;CHECK(fvr::bc2::RestoreBorrowedViewPointer(&borrowed,12,7)&&borrowed==7);
    borrowed=13;CHECK(!fvr::bc2::RestoreBorrowedViewPointer(&borrowed,12,7)&&borrowed==13);

    const char label[]="owned";const fvr::bc2::NativeStringRange range{label,label+5,label+6};NativeViewFake object{};
    fvr::bc2::CreateViewFn volatile factory=reinterpret_cast<fvr::bc2::CreateViewFn>(FakeFactory);
    fvr::bc2::ViewRefFn volatile add=reinterpret_cast<fvr::bc2::ViewRefFn>(FakeAdd),release=reinterpret_cast<fvr::bc2::ViewRefFn>(FakeRelease);
    fvr::bc2::SetViewActiveFn volatile active=reinterpret_cast<fvr::bc2::SetViewActiveFn>(FakeSetActive);
    for(unsigned i=0;i<256;++i){CHECK(factory(&object,&range)==&object&&object.name==label);CHECK(add(&object)==1);active(&object,false);CHECK(!object.active);active(&object,true);CHECK(object.active);CHECK(release(&object)==0);}

    CHECK(MH_Initialize()==MH_OK);
    CHECK(MH_CreateHook(FakeWorld,HookWorld,reinterpret_cast<void**>(&originalWorld))==MH_OK);
    CHECK(MH_CreateHook(FakePrepare,HookPrepare,reinterpret_cast<void**>(&originalPrepare))==MH_OK);
    CHECK(MH_CreateHook(FakeDraw,HookDraw,reinterpret_cast<void**>(&originalDraw))==MH_OK);
    CHECK(MH_CreateHook(FakeVisibility,HookVisibility,reinterpret_cast<void**>(&originalVisibility))==MH_OK);
    CHECK(MH_CreateHook(FakeUpdate,HookUpdate,reinterpret_cast<void**>(&originalUpdate))==MH_OK);
    CHECK(MH_CreateHook(FakeContextCamera,HookContextCamera,reinterpret_cast<void**>(&originalContextCamera))==MH_OK);
    CHECK(MH_CreateHook(FakeProjectionContext,HookProjectionContext,reinterpret_cast<void**>(&originalProjectionContext))==MH_OK);
    CHECK(MH_CreateHook(FakePresent,HookPresent,reinterpret_cast<void**>(&originalPresent))==MH_OK);
    CHECK(MH_CreateHook(FakeGameplay,HookGameplay,reinterpret_cast<void**>(&originalGameplay))==MH_OK);
    CHECK(MH_CreateHook(FakeAnimation,HookAnimation,reinterpret_cast<void**>(&originalAnimation))==MH_OK);
    CHECK(MH_CreateHook(FakeEvaluate,HookEvaluate,reinterpret_cast<void**>(&originalEvaluate))==MH_OK);
    CHECK(MH_CreateHook(FakePost,HookPost,reinterpret_cast<void**>(&originalPost))==MH_OK);
    CHECK(MH_CreateHook(FakePalettePack,HookPalettePack,reinterpret_cast<void**>(&originalPalettePack))==MH_OK);
    CHECK(MH_CreateHook(FakeClientShoot,HookClientShoot,reinterpret_cast<void**>(&originalClientShoot))==MH_OK);
    CHECK(MH_CreateHook(FakeCompose,HookCompose,reinterpret_cast<void**>(&originalCompose))==MH_OK);
    CHECK(MH_EnableHook(MH_ALL_HOOKS)==MH_OK);
    Compose volatile compose=reinterpret_cast<Compose>(FakeCompose);float ca[16],cb[16],co[16];for(unsigned i=0;i<16;++i){ca[i]=float(i);cb[i]=float(i)*2;co[i]=-77;}
    for(unsigned i=0;i<1024;++i){CHECK(compose(ca,co,cb)==reinterpret_cast<std::uintptr_t>(co));CHECK(composeCalls==i+1);for(unsigned n=0;n<16;++n){CHECK(ca[n]==float(n)&&cb[n]==float(n)*2);CHECK(co[n]==(n%4==3?-77.f:float(n)*3));}}

    ClientShoot volatile shot=reinterpret_cast<ClientShoot>(FakeClientShoot);ShotData shotData{};int shotConfig=0,shotContext=0;
    for(unsigned i=0;i<1024;++i){const float dt=.01f+float(i)*.00001f;CHECK(shot(&shotData,&shotConfig,dt,i,&shotContext)==(reinterpret_cast<std::uintptr_t>(&shotConfig)^i));CHECK(shotData.calls==i+1&&shotData.config==&shotConfig&&shotData.context==&shotContext&&shotData.primary==i&&shotData.dt==dt);}
    PalettePack volatile pack=reinterpret_cast<PalettePack>(FakePalettePack);float source[32],destination[26];for(unsigned i=0;i<32;++i)source[i]=float(i)+.25f;
    for(unsigned attempt=0;attempt<256;++attempt){for(auto& f:destination)f=-1234;
        CHECK(pack(source,destination+1,2)==reinterpret_cast<std::uintptr_t>(source)+40*sizeof(float));CHECK(paletteCalls==attempt+1&&destination[0]==-1234&&destination[25]==-1234);
        for(unsigned n=0;n<2;++n)for(unsigned col=0;col<3;++col)for(unsigned row=0;row<4;++row)CHECK(destination[1+n*12+col*4+row]==source[n*16+row*4+col]);
    }

    AnimationUpdate volatile animation=reinterpret_cast<AnimationUpdate>(FakeAnimation);
    AnimationEvaluate volatile evaluate=reinterpret_cast<AnimationEvaluate>(FakeEvaluate);
    AnimationPost volatile post=reinterpret_cast<AnimationPost>(FakePost);AnimationData animated{};
    for(unsigned i=0;i<1024;++i){const float dt=.012f+float(i)*.00001f,alpha=.5f+float(i)*.000001f;const bool full=(i%2)==0;
        CHECK(animation(&animated,dt,alpha,full)==reinterpret_cast<std::uintptr_t>(&animated)+17);CHECK(animated.calls==i*3+1&&animated.dt==dt&&animated.alpha==alpha&&animated.full==full);
        CHECK(evaluate(&animated,dt+1,!full)==0xabc001u+i*3+2);CHECK(animated.calls==i*3+2&&animated.dt==dt+1&&animated.full==!full);
        CHECK(post(&animated,alpha+1)==0x123400u+i*3+3);CHECK(animated.calls==i*3+3&&animated.alpha==alpha+1);
    }

    GameplayUpdate volatile gameplay=reinterpret_cast<GameplayUpdate>(FakeGameplay);Data player{};
    for(unsigned i=0;i<256;++i){gameplay(&player,.016f+float(i)*.000001f);CHECK(player.frame==i+1&&player.dt==.016f+float(i)*.000001f);}
    PresentCall desktop{},other{};pacingTarget=reinterpret_cast<IDXGISwapChain*>(&desktop);
    auto* otherChain=reinterpret_cast<IDXGISwapChain*>(&other);
    fvr::graphics::SwapChainPresentFn volatile present=FakePresent;
    for(unsigned i=0;i<256;++i){
        pacingActive=true;CHECK(present(pacingTarget,1,DXGI_PRESENT_DO_NOT_WAIT)==DXGI_ERROR_DEVICE_REMOVED);
        CHECK(desktop.interval==0&&desktop.flags==DXGI_PRESENT_DO_NOT_WAIT);
        CHECK(present(pacingTarget,1,DXGI_PRESENT_TEST)==DXGI_ERROR_DEVICE_REMOVED);CHECK(desktop.interval==1&&desktop.flags==DXGI_PRESENT_TEST);
        CHECK(present(otherChain,1,0)==DXGI_ERROR_DEVICE_REMOVED);CHECK(other.interval==1);
        pacingActive=false;CHECK(present(pacingTarget,1,0)==DXGI_ERROR_DEVICE_REMOVED);CHECK(desktop.interval==1);
        pacingActive=true;CHECK(present(pacingTarget,5,0)==DXGI_ERROR_DEVICE_REMOVED);CHECK(desktop.interval==5);
    }
    World volatile world=reinterpret_cast<World>(FakeWorld);Prepare volatile prepare=reinterpret_cast<Prepare>(FakePrepare);Draw volatile draw=reinterpret_cast<Draw>(FakeDraw);
    Visibility volatile visibility=reinterpret_cast<Visibility>(FakeVisibility);
    Update volatile update=reinterpret_cast<Update>(FakeUpdate);
    Data d{};int request=1,view=2,prepared=3;
    ProjectionContext volatile projectionContext=reinterpret_cast<ProjectionContext>(FakeProjectionContext);
    for(unsigned i=0;i<256;++i){CHECK(projectionContext(&d,&request)==&d&&d.view==&request);}
    ContextCamera volatile contextCamera=reinterpret_cast<ContextCamera>(FakeContextCamera);
    for(unsigned i=0;i<256;++i){contextCamera(&d,&view,&prepared);CHECK(d.view==&view&&d.prepared==&prepared);}

    for(unsigned i=0;i<256;++i){world(&d,&request);CHECK(prepare(&d,&request,&view)==&view);draw(&d,&request,&view,&prepared);void* first=nullptr;void* second=nullptr;visibility(&d,&request,&view,&prepared,&request,&first,&second);CHECK(first==&prepared&&second==&request);update(&d,&request,0x12340000+i,.012345f,&view,&first,&second);CHECK(first==&request&&second==&view&&d.frame==0x12340000+i&&d.dt==.012345f);}
    CHECK(d.update==256&&d.visibility==256&&d.world==256&&d.prepare==256&&d.draw==256&&hooks==1280&&d.request==&request&&d.view==&view&&d.prepared==&prepared);
    CHECK(MH_DisableHook(MH_ALL_HOOKS)==MH_OK);world(&d,&request);CHECK(d.world==257&&hooks==1280);
    CHECK(MH_Uninitialize()==MH_OK);return 0;
}
