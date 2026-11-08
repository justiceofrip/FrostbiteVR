#include "Bc2OriginalFiringMonitor.h"
#include "Test.h"
#include <cstdio>
#if defined(_MSC_VER) && defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "Bc2OriginalFiringUnwind.h"
#endif
using namespace fvr::bc2;
namespace {
struct Counts {
    unsigned active=0,enters=0,leaves=0,aborts=0,copies=0;
    std::uint64_t leaveToken=0,abortToken=0;
    bool valid=true;
    static OriginalFiringToken Enter(void* p,const OriginalFiringUpdate&)noexcept {
        auto& c=*static_cast<Counts*>(p);++c.active;
        OriginalFiringToken token;token.words[0]=++c.enters;return token;
    }
    static void Leave(void* p,const OriginalFiringUpdate&,const OriginalFiringToken& token)noexcept {
        auto& c=*static_cast<Counts*>(p);c.valid=c.valid&&c.active>0;
        --c.active;++c.leaves;c.leaveToken=token.words[0];
    }
    static void Abort(void* p,const OriginalFiringUpdate&,const OriginalFiringToken& token)noexcept {
        auto& c=*static_cast<Counts*>(p);c.valid=c.valid&&c.active>0;
        --c.active;++c.aborts;c.abortToken=token.words[0];
    }
    static std::optional<std::array<std::byte,0x30>> Copy(void* p,const OriginalFiringUpdate&,OriginalFiringToken&)noexcept {
        ++static_cast<Counts*>(p)->copies;return std::array<std::byte,0x30>{};
    }
    OriginalFiringObserver Monitor(){return {this,Enter,Leave,nullptr,Abort};}
    OriginalFiringObserver Primary(){return {this,Enter,Leave,Copy,Abort};}
};
int ScopeCases(){
    Counts passive,primary;
    CHECK(ValidOriginalFiringMonitor(passive.Monitor()));
    CHECK(!ValidOriginalFiringMonitor(primary.Primary()));
    {auto invalid=passive.Monitor();invalid.context=nullptr;CHECK(!ValidOriginalFiringMonitor(invalid));}
    {auto invalid=passive.Monitor();invalid.enter=nullptr;CHECK(!ValidOriginalFiringMonitor(invalid));}
    {auto invalid=passive.Monitor();invalid.leave=nullptr;CHECK(!ValidOriginalFiringMonitor(invalid));}
    {auto invalid=passive.Monitor();invalid.abort=nullptr;CHECK(!ValidOriginalFiringMonitor(invalid));}
    {
        OriginalFiringMonitorScope invalid(primary.Primary(),{});
        CHECK(!primary.enters&&!OriginalFiringScope::Current());
    }
    {
        OriginalFiringMonitorScope monitor(passive.Monitor(),{1,2,3,4});
        CHECK(passive.active==1&&!OriginalFiringScope::Current());
        OriginalFiringScope scope(primary.Primary(),{1,2,3,4});
        CHECK(OriginalFiringScope::Current()==&scope&&scope.InputCopy()&&!scope.InputCopy());
        scope.BindCopiedContext(0x12000);
        {
            OriginalFiringMonitorScope nested(passive.Monitor(),{5,6,7,8});
            CHECK(OriginalFiringScope::Current()==&scope&&scope.NativeContext()==0x12000);
            nested.Abort();nested.Abort();
            CHECK(passive.active==1&&passive.aborts==1&&passive.abortToken==2);
        }
        CHECK(OriginalFiringScope::Current()==&scope&&primary.copies==1&&!passive.copies);
    }
    CHECK(!OriginalFiringScope::Current()&&!primary.active&&!passive.active);
    CHECK(primary.valid&&passive.valid&&primary.leaves==1&&passive.leaves==1&&passive.leaveToken==1);
    CHECK(!primary.aborts&&passive.aborts==1);
    return 0;
}
#if defined(_MSC_VER) && defined(_WIN32)
constexpr DWORD fault=0xe0420773;
struct DispatchFixture {
    Counts primary,monitor;
    unsigned originals=0,mode=0;bool nested=false,valid=true;
};
void Dispatch(DispatchFixture&);
__declspec(noinline) DWORD EngineCatch(DispatchFixture& f){
    DWORD caught=0;
    __try {Dispatch(f);}
    __except(GetExceptionCode()==fault?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=GetExceptionCode();}
    return caught;
}
void Dispatch(DispatchFixture& f){
    OriginalFiringMonitorScope monitor(f.monitor.Monitor(),{10,20,30,40});
    OriginalFiringScope primary(f.primary.Primary(),{10,20,30,40});
    auto body=[&]{
        auto copied=primary.InputCopy();alignas(16) auto input=*copied;
        primary.BindCopiedContext(reinterpret_cast<std::uintptr_t>(input.data()));
        ++f.originals;
        if(f.mode==3)throw 773;
        if(f.mode==1||f.nested){RaiseException(fault,0,0,nullptr);return;}
        if(f.mode==4){f.nested=true;Dispatch(f);}
        if(f.mode==2){
            f.nested=true;const auto caught=EngineCatch(f);f.nested=false;
            f.valid=f.valid&&caught==fault&&OriginalFiringScope::Current()==&primary&&primary.Copied()&&
                primary.NativeContext()==reinterpret_cast<std::uintptr_t>(input.data())&&
                f.primary.active==1&&f.monitor.active==1&&f.primary.abortToken==2&&f.monitor.abortToken==2;
        }
    };
    auto abort=[&]()noexcept{primary.Abort();monitor.Abort();};
    RunOriginalFiringUnwind(body,abort);
}
bool Drained(const DispatchFixture& f){
    return f.valid&&f.primary.valid&&f.monitor.valid&&!f.primary.active&&!f.monitor.active&&
        !OriginalFiringScope::Current()&&f.primary.enters==f.monitor.enters&&
        f.primary.leaves==f.monitor.leaves&&f.primary.aborts==f.monitor.aborts&&!f.monitor.copies;
}
int UnwindCases(){
    DispatchFixture normal;CHECK(!EngineCatch(normal));
    CHECK(Drained(normal)&&normal.originals==1&&normal.primary.leaves==1&&!normal.primary.aborts);
    DispatchFixture direct;direct.mode=1;CHECK(EngineCatch(direct)==fault);
    CHECK(Drained(direct)&&direct.originals==1&&!direct.primary.leaves&&direct.primary.aborts==1);
    direct.mode=0;CHECK(!EngineCatch(direct));
    CHECK(Drained(direct)&&direct.originals==2&&direct.primary.leaves==1&&direct.primary.aborts==1);
    DispatchFixture nested;nested.mode=2;CHECK(!EngineCatch(nested));
    CHECK(Drained(nested)&&nested.originals==2&&nested.primary.leaves==1&&nested.primary.aborts==1);
    CHECK(nested.primary.leaveToken==1&&nested.monitor.leaveToken==1);
    DispatchFixture both;both.mode=4;CHECK(EngineCatch(both)==fault);
    CHECK(Drained(both)&&both.originals==2&&!both.primary.leaves&&both.primary.aborts==2);
    DispatchFixture cpp;cpp.mode=3;int caught=0;
    try {Dispatch(cpp);}catch(int e){caught=e;}
    CHECK(caught==773&&Drained(cpp)&&cpp.originals==1&&!cpp.primary.leaves&&cpp.primary.aborts==1);
    return 0;
}
#endif
}
int main(){
    CHECK(!ScopeCases());
#if defined(_MSC_VER) && defined(_WIN32)
    CHECK(!UnwindCases());
#endif
    std::puts("Passive firing monitor ownership and unwind passed");
}
