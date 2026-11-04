#ifdef NDEBUG
#error "Smoke402 checks require assertions in Release"
#endif
#include "Bc2SmokeOrigin402.h"
#include "CandidateSmokeOrigin402.h"
#include <cassert>
#include <bit>
#include <cstring>
#include <iostream>
using namespace fvr;
using namespace fvr::bc2;
namespace smoke=smoke402;
namespace target=projectile302;
using smoke::EarlyThunk;
using smoke::ResetThunk;
namespace {
unsigned checks=0;
void Check(bool value){++checks;assert(value);}
struct Fixture {
    origin388::Request request{};
    muzzle_part_observation::Frame frame{};
    explicit Fixture(const smoke::Enrollment& e){
        const auto& p=*e.geometry.observedPart;auto& k=request.captured;
        k.owner.player=1;k.owner.soldier=2;k.owner.weak=3;k.owner.weapon=4;k.owner.foot=true;
        k.owner.equipment.weapon=4;k.owner.equipment.data=5;
        std::memcpy(k.owner.equipment.asset.data(),"40mmsmk",8);
        k.scopeSerial=1;k.invocation=2;k.callerLifetime=2;k.publicationSerial=3;k.thread=4;
        k.generation=10;k.ownerGeneration=11;k.equipmentGeneration=12;k.space=13;k.configurationSequence=9;
        k.tokenValid=true;k.sourceP=0x20000;k.callerReturn=0x57a10d;k.target=0x100000;
        k.effects=0x30000;k.firingData=0x40000;k.configuration=0x500a0;
        k.deadlineQpc=200;k.inputDeadlineNs=200;k.configurationDeadlineNs=190;
        request.current=k;request.capabilityEnabled=request.firstTargetEntry=true;request.nestedOrAmbiguous=false;
        request.enrollment=&e.geometry;auto& evidence=request.evidence;evidence.proof=k;
        auto& s=evidence.publication;s.equipment=k.owner.equipment;s.configuration=p.configuration;s.mesh=p.mesh;s.rigFingerprint=p.rigFingerprint;
        s.generation=k.generation;s.ownerGeneration=k.ownerGeneration;s.equipmentGeneration=k.equipmentGeneration;s.space=k.space;
        s.nowQpc=s.nowNs=100;s.deadlineQpc=k.deadlineQpc;s.configurationDeadlineNs=k.configurationDeadlineNs;s.configurationSequence=k.configurationSequence;
        s.unitsPerMetre=1;s.selectedRigVerified=s.configurationVerified=s.attachmentReady=s.ordinaryPublishedPalette=true;
        frame.profile=&p;frame.rig.soldier=k.owner.soldier;frame.rig.weak=k.owner.weak;frame.rig.count=147;
        frame.equipment=s.equipment;frame.generation=s.generation;frame.ownerGeneration=s.ownerGeneration;frame.equipmentGeneration=s.equipmentGeneration;
        frame.space=s.space;frame.deadlineQpc=s.deadlineQpc;frame.configurationDeadlineNs=s.configurationDeadlineNs;
        frame.configurationSequence=s.configurationSequence;frame.unitsPerMetre=1;frame.bone=p.expectedIndex;
        for(unsigned i=0;i<4;++i)frame.trackedPart.values[i][i]=1;
        evidence.rig=frame.rig;evidence.observedPart=&frame;evidence.activeState=0;evidence.firingAllowed=evidence.exitVisible=true;
        request.coordinate={k,origin388::Boundary::AbsoluteNativeWorldRh,1,{0,0,0}};
        auto& c=request.capturedConfig;c.effects=k.effects;c.firingData=k.firingData;c.function=0x50000;c.config=k.configuration;
        c.muzzleExplosion=0x60000;c.projectileData=0x70000;c.speed={0,0,50};c.direction={0,0,1};c.position=smoke::SmokeRoute.position;
        c.bulletsPerShell=c.bulletsPerShot=1;c.cameraFlags={1,1,0};request.currentConfig=c;
    }
};
struct Host {
    origin386::Bits desired{0x3e800000,0x3f000000,0x3f400000};
    bool rejectEarly=false,rejectPhysical=false,rejectValidate=false,rejectReset=false;
    unsigned prepares=0,validates=0,resets=0;
    static bool __cdecl Prepare(void* c,const smoke::Key& k,const smoke::Entry& e,smoke::Admission& a)noexcept{
        auto& h=*static_cast<Host*>(c);++h.prepares;
        if((e.boundary==smoke::Boundary::EarlyEffect&&h.rejectEarly)||(e.boundary==smoke::Boundary::Physical&&h.rejectPhysical))return false;
        a={k,e.primary,h.desired,0x7f7fffff,{1,2,3,100}};SetLastError(0xabcdef);return true;
    }
    static bool __cdecl Validate(void* c,const smoke::Key&,const smoke::Entry&,const smoke::Admission&)noexcept{
        auto& h=*static_cast<Host*>(c);++h.validates;return !h.rejectValidate;
    }
    static bool __cdecl Reset(void* c,const smoke::Key&,const smoke::Entry&)noexcept{
        auto& h=*static_cast<Host*>(c);++h.resets;return !h.rejectReset;
    }
    void Bind(){smoke::Runtime.api={this,Prepare,Validate,Reset};smoke::Runtime.enabled.store(true);}
};
std::uint64_t serial=0;
smoke::Key Next(){++serial;return {serial,serial,serial,GetCurrentThreadId()};}
target::Registers EarlyRegisters(unsigned p){target::Registers r{};r.pushadEsp=p-0x78;return r;}
target::Registers ResetRegisters(unsigned p){target::Registers r{};r.pushadEsp=p-0x64;return r;}
target::Entry Physical(unsigned p,unsigned caller=0x57aa0d){target::Entry e{};e.returnAddress=caller;e.args[1]=p;return e;}
bool PhysicalEntry(const target::Entry& e){target::Depth=1;const bool result=smoke::BeforePhysical(e);target::Depth=0;return result;}
void Early(unsigned p){const auto r=EarlyRegisters(p);smoke::EarlyDispatch(&r);}
void Reset(unsigned p){const auto r=ResetRegisters(p);smoke::ResetDispatch(&r);}
void ExceptionalClose(smoke::Scope* scope){
    __try {RaiseException(0xe0402042,0,0,nullptr);}
    __finally {scope->Close();}
}
bool CatchNativeException(smoke::Scope* scope){
    __try {ExceptionalClose(scope);return false;}
    __except(GetExceptionCode()==0xe0402042?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}

// Real x86 seam thunks, exercised on this test process's own stack. The fake
// native calls capture the machine state at their entry and run exactly once.
alignas(16) unsigned char seedFx[512]{},savedFx[512]{},capturedFx[512]{};
unsigned savedSp=0,inputSp=0,afterSp=0,seenP=0,seenReturn=0,seenError=0,seenWritten=0;
unsigned captured[9]{},effectCalls=0,resetCalls=0,mode=0;
__declspec(naked) void FakeEffect(){
    __asm {
        pushfd
        pushad
        fxsave capturedFx
        mov esi,esp
        lea edi,captured
        mov ecx,9
        cld
        rep movsd
        mov eax,fs:[34h]
        mov seenError,eax
        mov eax,[esp+36]
        mov seenReturn,eax
        mov eax,[esp+40]
        mov seenP,eax
        mov dword ptr [eax+30h],11223344h
        inc effectCalls
        popad
        popfd
        ret 18h
    }
}
__declspec(naked) void FakeReset(){
    __asm {
        pushfd
        pushad
        fxsave capturedFx
        mov esi,esp
        lea edi,captured
        mov ecx,9
        cld
        rep movsd
        mov eax,fs:[34h]
        mov seenError,eax
        mov eax,[esp+36]
        mov seenReturn,eax
        inc resetCalls
        popad
        popfd
        ret
    }
}
__declspec(naked) void MachineComplete(){
    __asm {
        mov afterSp,esp
        cmp mode,0
        jne doneWrite
        mov eax,seenP
        mov eax,[eax+30h]
        mov seenWritten,eax
    doneWrite:
        fxrstor savedFx
        mov esp,savedSp
        popad
        popfd
        ret
    }
}
__declspec(naked) void TestEarlyResume(){
    __asm {
        call eax
        jmp MachineComplete
    }
}
__declspec(naked) void TestResetResume(){__asm {jmp MachineComplete}}
__declspec(naked) void RunMachine(){
    __asm {
        pushfd
        pushad
        mov savedSp,esp
        fxsave savedFx
        sub esp,512
        and esp,-16
        cmp mode,0
        jne resetSetup
        sub esp,4
        mov inputSp,esp
        mov eax,offset FakeEffect
        jmp commonSetup
    resetSetup:
        mov inputSp,esp
        mov eax,12345678h
    commonSetup:
        fxrstor seedFx
        mov dword ptr fs:[34h],13579h
        mov ecx,11111111h
        mov edx,22222222h
        mov ebx,33333333h
        mov ebp,44444444h
        mov esi,55555555h
        mov edi,66666666h
        cmp mode,0
        jne doReset
        push 246h
        popfd
        jmp EarlyThunk
    doReset:
        push 246h
        popfd
        jmp ResetThunk
    }
}
void SeedMachine(){
    __asm {
        fxsave savedFx
        fninit
        fld1
        fxsave seedFx
        fxrstor savedFx
    }
    for(unsigned i=160;i<288;++i)seedFx[i]=static_cast<unsigned char>(i^0x5a);
}
void MachineChecks(){
    smoke::Runtime.enabled.store(false);SeedMachine();
    smoke::EarlyResume=reinterpret_cast<unsigned>(TestEarlyResume);
    smoke::ResetResume=reinterpret_cast<unsigned>(TestResetResume);smoke::ResetTarget=reinterpret_cast<unsigned>(FakeReset);
    for(mode=0;mode<2;++mode){RunMachine();
        Check(seenError==0x13579&&captured[0]==0x66666666&&captured[1]==0x55555555&&captured[2]==0x44444444);
        Check(captured[4]==0x33333333&&captured[6]==0x11111111&&(captured[8]&0x8d5)==(0x246&0x8d5));
        Check(std::memcmp(seedFx,capturedFx,32)==0&&std::memcmp(seedFx+32,capturedFx+32,8*16)==0&&
            std::memcmp(seedFx+160,capturedFx+160,8*16)==0);
        if(!mode){Check(effectCalls==1&&seenP==inputSp+0x74&&captured[5]==seenP&&afterSp==inputSp+0x14&&seenWritten==0x11223344);
            Check(captured[7]==reinterpret_cast<unsigned>(FakeEffect)&&seenReturn==reinterpret_cast<unsigned>(TestEarlyResume)+2);
        }else{Check(resetCalls==1&&captured[5]==0x22222222&&captured[7]==0x12345678&&afterSp==inputSp);
            Check(seenReturn==reinterpret_cast<unsigned>(TestResetResume));}
    }
}
}
int main(){
    Check(!smoke::Runtime.enabled.load()&&!smoke::Runtime.closed.load());
    Check(target::FloatingStateSupported());
    for(const auto& enrollment:smoke::Enrollments){
        Fixture fixture(enrollment);auto accepted=smoke::PrepareSmoke(fixture.request,enrollment);Check(bool(accepted));
        // Existing ordinary admission remains unchanged and rejects smoke.
        origin388::Once ordinary;Check(!origin388::PrepareOriginOnce(ordinary,fixture.request));
        auto bad=fixture.request;bad.currentConfig.muzzleExplosion=0;Check(!smoke::PrepareSmoke(bad,enrollment));
        bad=fixture.request;bad.evidence.publication.configuration="unreviewed";Check(!smoke::PrepareSmoke(bad,enrollment));
        bad=fixture.request;bad.evidence.publication.nowQpc=bad.captured.deadlineQpc;Check(!smoke::PrepareSmoke(bad,enrollment));
        bad=fixture.request;bad.coordinate.nativeInputXYZ={100,100,100};Check(!smoke::PrepareSmoke(bad,enrollment));
        for(unsigned scenario=0;scenario<8;++scenario){
            alignas(16) std::array<unsigned,16> p{};for(unsigned i=0;i<p.size();++i)p[i]=0x3f800000+i;
            const auto before=p;const auto at=reinterpret_cast<unsigned>(p.data());
            Host host;host.Bind();host.rejectEarly=scenario==2;smoke::Scope scope(Next());
            Early(at);
            if(scenario==2){Check(!scope.EarlyCommitted()&&p==before);Reset(at);Check(PhysicalEntry(Physical(at))&&p==before);continue;}
            Check(scope.EarlyCommitted()&&p[12]==host.desired[0]&&p[13]==host.desired[1]&&p[14]==host.desired[2]);
            for(unsigned i=0;i<16;++i)if(i<12||i>14)Check(p[i]==before[i]);
            if(scenario==6){p[12]=0x12345678;Check(CatchNativeException(&scope)&&p[12]==0x12345678);continue;}
            // An inner dynamic effect's7155b0 must not consume later physical.
            Check(PhysicalEntry(Physical(at,0x123456))&&host.prepares==1);
            p[12]=0x76543210; // legitimate native effect write, after early commit
            if(scenario==1){Check(PhysicalEntry(Physical(at))&&p[12]==0x76543210&&host.prepares==1);continue;}
            if(scenario==5){smoke::Scope nested(Next());Early(at);}
            if(scenario==7)smoke::Runtime.enabled.store(false);
            p[12]=0x3f800000;p[13]=0x40000000;p[14]=0x40400000; // native selection stores
            Reset(at);if(scenario==4)Reset(at);
            host.rejectPhysical=scenario==3;Check(PhysicalEntry(Physical(at)));
            if(scenario==0)Check(scope.Status()==smoke::Outcome::PhysicalCommitted&&p[12]==host.desired[0]&&host.prepares==2);
            else Check(p[12]==0x3f800000);
            p[12]=0x99887766;Check(PhysicalEntry(Physical(at))&&p[12]==0x99887766);scope.Close();Check(p[12]==0x99887766);
        }
    }
    MachineChecks();
    const auto eb=std::as_bytes(std::span(smoke::EarlyBytes)),rb=std::as_bytes(std::span(smoke::ResetBytes));
    Check(smoke::VerifySeams(eb,rb,eb,rb));auto changed=smoke::EarlyBytes;changed[4]=0x90;
    Check(!smoke::VerifySeams(eb,rb,std::as_bytes(std::span(changed)),rb));
    smoke::Runtime.Close();Check(!smoke::Runtime.enabled.load()&&smoke::Runtime.closed.load());
    std::cout<<checks<<" smoke402 checks;24 exact tuples, real stack stores and x86 thunk fixtures\n";
}
