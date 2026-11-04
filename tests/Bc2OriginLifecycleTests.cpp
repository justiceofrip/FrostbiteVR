#ifdef NDEBUG
#error "Origin regression checks must remain active in Release builds"
#endif
// ROOT-owned compile/run. Real changed Lifecycle code, synthetic hook backend;
// no game, hook install, native memory access or origin-result qualification.
#include "Bc2ProjectileTargetHost.h"
#include <iostream>
#include <sstream>
using namespace fvr::bc2;
namespace target=projectile302;
namespace host=projectile307;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::cerr<<label<<'\n';}}
void __cdecl Original(){}
bool __cdecl Read(void*,unsigned,void*,std::size_t)noexcept{return false;}
bool __cdecl Validate(void*,const target::Grant&,target::Boundary)noexcept{return false;}
bool __cdecl Prepare(void*,const target::OriginKey&,const target::Entry&,target::OriginAdmission&)noexcept{return false;}
bool __cdecl Revalidate(void*,const target::OriginKey&,const target::Entry&,const target::OriginAdmission&)noexcept{return false;}
struct Backend {
    unsigned creates=0,disables=0;bool failDisable=false;
    static bool Create(void* p,void*,target::MachineEntry entry,void** original)noexcept{
        auto& b=*static_cast<Backend*>(p);++b.creates;
        if(entry!=target::Thunk)return false;*original=reinterpret_cast<void*>(Original);return true;
    }
    static bool Disable(void* p,void*)noexcept{auto& b=*static_cast<Backend*>(p);++b.disables;return !b.failDisable;}
};
struct Fixture {
    host::Lifecycle life;target::Session observer;target::OriginSession origin;Backend backend;
    target::MachineEntry original=nullptr;void* slot=nullptr;
    bool Install(){return life.Install(true,true,true,true,150,observer,original,slot,reinterpret_cast<void*>(0x7155b0),
        {nullptr,Read,Validate,false},{&backend,Backend::Create,Backend::Disable});}
    bool Attach(){return life.AttachOrigin(origin,{nullptr,Prepare,Revalidate});}
};
}
int main(){
    {
        Fixture f;Check(!f.life.Start()&&!f.origin.enabled.load()&&!f.observer.enabled.load(),"default disabled");
        Check(!f.Attach(),"cannot attach before verified hook install");
        Check(f.Install()&&f.Attach(),"independent origin attaches to verified retained hook");
        f.life.observerRequested=false;
        Check(f.life.Start()&&f.origin.enabled.load()&&!f.observer.enabled.load(),"origin-only Start keeps observer disabled");
        Check(!f.Attach()&&!f.life.Start(),"no reattach or restart");
        // A full diagnostic prefix and an expired diagnostic window have no
        // authority over the independent OriginSession.
        f.observer.offered=64;
        Check(f.life.Window(100,true)&&!f.life.Window(250)&&f.origin.enabled.load(),"expired observer clock leaves origin enabled");
        Check(f.origin.enabled.load()&&!f.observer.enabled.load(),"full 64 row observer leaves origin enabled");
        InterlockedIncrement(&f.observer.active);
        Check(f.life.Active()&&!f.life.PollDrain(),"shared thunk activity counted even without observer enablement");
        f.life.Close();Check(!f.origin.enabled.load()&&f.origin.closed.load()&&!f.observer.enabled.load(),"close stops both capabilities");
        f.backend.failDisable=true;
        Check(!f.life.Disable()&&!f.life.Retired(),"failed disable retains shared target");
        f.backend.failDisable=false;
        Check(f.life.Disable()&&!f.life.Retired(),"disabled target awaits active origin callback");
        InterlockedDecrement(&f.observer.active);
        Check(f.life.PollDrain()&&f.life.Retired()&&f.slot&&f.original,"drained target retains immutable original and slot");
        Check(!f.Attach()&&!f.life.Start(),"closed capability cannot reopen");
        std::ostringstream report;host::Report(report,f.life,f.observer);
        Check(report.str().find("\"read_only\":0")!=std::string::npos&&report.str().find("\"origin_requested\":1")!=std::string::npos,
            "origin-enabled report does not claim read-only target");
    }
    {
        Fixture f;Check(f.Install()&&f.life.Start()&&f.observer.enabled.load()&&!f.origin.enabled.load(),"passive-only installation stays passive");
        Check(!f.Attach(),"cannot enable origin after first native entries");
        f.life.Close();Check(f.life.Disable()&&f.life.PollDrain()&&f.life.Retired(),"passive shutdown retained");
    }
    {
        Fixture f;Check(f.Install()&&f.Attach()&&f.life.Start()&&f.observer.enabled.load()&&f.origin.enabled.load(),"independent opt-ins may coexist");
        f.life.Close();Check(f.life.Disable()&&f.life.PollDrain()&&f.life.Retired(),"combined shutdown retires both");
    }
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
