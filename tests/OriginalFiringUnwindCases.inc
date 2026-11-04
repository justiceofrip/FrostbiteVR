#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Bc2OriginalFiringUnwind.h"
#include "Bc2NativeCycleCallbacks.h"

namespace {
using namespace fvr::bc2;
constexpr DWORD firingFault=0xe0420430;
struct FiringExceptionFixture {
    unsigned active=0,entered=0,returned=0,aborted=0,returnedSerial=0,abortedSerial=0,originals=0;
    unsigned mode=0;bool nested=false,valid=true;
    NativeCycleCallbackState cycles;
    OriginalFiringObserver observer{this,
        [](void* p,const OriginalFiringUpdate&)noexcept{
            auto& f=*static_cast<FiringExceptionFixture*>(p);++f.active;
            OriginalFiringToken token;token.words[6]=++f.entered;return token;},
        [](void* p,const OriginalFiringUpdate&,const OriginalFiringToken& token)noexcept{
            auto& f=*static_cast<FiringExceptionFixture*>(p);--f.active;++f.returned;
            f.returnedSerial=unsigned(token.words[6]);},
        [](void*,const OriginalFiringUpdate&,OriginalFiringToken&)noexcept->std::optional<std::array<std::byte,0x30>>{
            return std::array<std::byte,0x30>{};},
        [](void* p,const OriginalFiringUpdate&,const OriginalFiringToken& token)noexcept{
            auto& f=*static_cast<FiringExceptionFixture*>(p);--f.active;++f.aborted;
            f.abortedSerial=unsigned(token.words[6]);}
    };
};
void TestFiringDispatch(FiringExceptionFixture&);
__declspec(noinline) DWORD EngineCatchesFiring(FiringExceptionFixture& f){
    DWORD caught=0;
    __try {TestFiringDispatch(f);}
    __except(GetExceptionCode()==firingFault?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=GetExceptionCode();}
    return caught;
}
void TestFiringDispatch(FiringExceptionFixture& f){
    OriginalFiringScope scope(f.observer,{100,200,300,0});
    NativeCycleCallbackScope cycle(&f.cycles,100,GetCurrentThreadId(),NativeCycleCallbackKind::Update);
    auto body=[&]{
        const auto input=scope.InputCopy();alignas(16) auto nativeInput=*input;
        scope.BindCopiedContext(reinterpret_cast<std::uintptr_t>(nativeInput.data()));
        ++f.originals;
        if(f.mode==3)throw 23;
        if(f.mode==1||f.nested){RaiseException(firingFault,0,0,nullptr);return;}
        if(f.mode==4){f.nested=true;TestFiringDispatch(f);}
        if(f.mode==2){
            f.nested=true;const auto caught=EngineCatchesFiring(f);f.nested=false;
            f.valid=f.valid&&caught==firingFault&&OriginalFiringScope::Current()==&scope&&
                scope.Copied()&&scope.NativeContext()==reinterpret_cast<std::uintptr_t>(nativeInput.data())&&
                f.active==1&&f.cycles.active.load()==1&&f.returnedSerial==0&&f.abortedSerial==2;
        }
    };
    auto abort=[&]()noexcept{cycle.Release();scope.Abort();};
    RunOriginalFiringUnwind(body,abort);
}
int OriginalFiringUnwindCases(){
    FiringExceptionFixture direct;direct.mode=1;
    CHECK(EngineCatchesFiring(direct)==firingFault&&direct.originals==1&&direct.active==0);
    CHECK(!OriginalFiringScope::Current()&&!direct.cycles.active.load());
    CHECK(direct.aborted==1&&direct.abortedSerial==1&&!direct.returned&&!direct.returnedSerial);
    direct.mode=0;CHECK(!EngineCatchesFiring(direct));
    CHECK(direct.originals==2&&direct.returned==1&&direct.returnedSerial==2&&direct.aborted==1&&!direct.active);
    FiringExceptionFixture nested;nested.mode=2;CHECK(!EngineCatchesFiring(nested));
    CHECK(nested.valid&&nested.originals==2&&nested.aborted==1&&nested.returned==1&&nested.returnedSerial==1);
    CHECK(!OriginalFiringScope::Current()&&!nested.active&&!nested.cycles.active.load());
    FiringExceptionFixture both;both.mode=4;
    CHECK(EngineCatchesFiring(both)==firingFault&&both.aborted==2&&!both.returned&&!both.active);
    CHECK(!OriginalFiringScope::Current()&&!both.cycles.active.load());
    FiringExceptionFixture cpp;cpp.mode=3;int caught=0;
    try {TestFiringDispatch(cpp);}catch(int e){caught=e;}
    CHECK(caught==23&&cpp.aborted==1&&!cpp.active&&!cpp.returned&&!cpp.cycles.active.load());
    CHECK(!OriginalFiringScope::Current());
    return 0;
}
}
