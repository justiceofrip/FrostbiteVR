#include "Bc2VehiclePhysicsEvidence.h"
#include "Bc2VehicleConsumerEvidence.h"
#include <cstring>
#include <iostream>
#if defined(FVR_BC2_VEHICLE_PHYSICS_GATEWAYS) && defined(_MSC_VER) && defined(_M_IX86)
#include <Windows.h>
#include <MinHook.h>
#include <latch>
#include <thread>
#endif
namespace p=fvr::bc2::vehiclePhysicsEvidence;
static unsigned failures=0;
static void Check(bool okay,const char* what){if(!okay){++failures;std::cerr<<what<<'\n';}}
static void NativeOwnerChecks(){
    // The validator contract is native-current plus an independent strict
    // publication result. Exercise the production sample wrapper itself.
    namespace c=fvr::bc2::vehicleConsumerEvidence;
    using View=c::View;
    const c::Identity expected{1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    for(unsigned mismatch:{0u,1u,2u,3u}){
        unsigned validations=0,reads=0;
        const auto validate=[&](View& view,bool& current){
            current=true;view.owner=expected;view.generationsCurrent=mismatch!=0;
            const auto changed=100+(++validations);
            if(mismatch==1)view.owner.inputEpoch=changed;
            if(mismatch==2)view.owner.movementEpoch=changed;
            if(mismatch==3)view.owner.space=changed;
            const auto strict=view.generationsCurrent&&view.owner==expected?c::None:c::Owner;
            Check(strict==c::Owner,"strict consumer still refuses stale/different publication");
            return strict;
        };
        const auto sample=p::ReadNativeOwnedSample<View>(validate,[&](p::Sample& s){
            ++reads;s.physics=0x14930000;s.control=0x14931000;s.body=0x14932000;
            s.controls[7]=0x3f000000;s.transform[12]=0x41200000;
        });
        Check(validations==2&&reads==1&&!sample.reasons&&sample.controls[7]==0x3f000000&&sample.transform[12]==0x41200000,
            "current native owner retains raw controls/body across stale or changing publication");
    }
    bool current=false;unsigned reads=0;
    const auto validate=[&](View&,bool& nativeCurrent){nativeCurrent=current;return p::Owner;};
    auto sample=p::ReadNativeOwnedSample<View>(validate,[&](p::Sample&){++reads;});
    Check(!reads&&sample.reasons==p::Owner,"native owner refusal prevents raw reads");
    current=true;
    sample=p::ReadNativeOwnedSample<View>(validate,[&](p::Sample& s){++reads;s.physics=0x14930000;current=false;});
    Check(reads==1&&sample.physics==0x14930000&&sample.reasons==p::Owner,"native owner loss after read preserves raw data with refusal");
}
static void JournalChecks(){
    p::Journal<8> j;Check(!j.Claim(p::Kind::Update,100),"unarmed claim refused");
    Check(j.ReserveArm()&&!j.ReserveArm(),"one arm only");j.PublishArm(100,1000);
    Check(j.InWindow(100)&&j.InWindow(2100)&&!j.InWindow(2101),"exact two-second inclusive bound");
    const p::Kind kinds[]{p::Kind::Consumer,p::Kind::Update,p::Kind::Filter,p::Kind::Simulation,p::Kind::Output};
    for(auto kind:kinds){auto* s=j.Claim(kind,101);Check(s!=nullptr,"assembled execution chain retained");if(!s)continue;
        auto& r=s->row;r.normal=true;r.end=102;r.before.physics=r.after.physics=1000;
        r.before.control=r.after.control=2000;r.before.associated=r.after.associated=3000;r.before.body=r.after.body=4000;
        r.parentUpdate=2;r.parentSimulation=4;s->complete.store(true,std::memory_order_release);}
    Check(j.rows[4].row.parentSimulation==4&&j.rows[4].row.parentUpdate==2,"actual boundary parent IDs retained");
    auto r=j.rows[2].row;r.before.controls[7]=0x3f000000;r.after.controls[7]=0;
    Check(p::Comparable(r),"filter input-to-zero output remains a valid boundary observation");
    for(auto reason:{p::Owner,p::Torn,p::Code,p::Overlap,p::Unwind,p::Identity}){
        auto refused=r;refused.reasons=reason;Check(!p::Comparable(refused),"each material boundary refusal survives");}
    for(unsigned n=0;n<3;++n)Check(j.Claim(p::Kind::Output,102)!=nullptr,"remaining frozen capacity");
    Check(!j.Claim(p::Kind::Output,102)&&j.overflow==1,"overflow closes without overwrite");
    Check(j.rows[0].row.sequence==1&&j.rows[0].row.kind==p::Kind::Consumer,"first positive/consumer anchor survives overflow");
    Check(!j.ReserveArm(),"closed window cannot rearm");
    p::Journal<2> stopped;Check(stopped.ReserveArm(),"arm reservation");stopped.Close();stopped.PublishArm(100,1000);
    Check(!stopped.InWindow(100)&&!stopped.published,"stop racing arm never republishes");
}

#if defined(FVR_BC2_VEHICLE_PHYSICS_GATEWAYS) && defined(_MSC_VER) && defined(_M_IX86)
namespace n=p::native;
static void GateChecks(){
    // Pause the actual arm transition after both prechecks, then stop. This
    // exercises CaptureEnabled, not only the Journal's separate arm state.
    n::CaptureEnabled=0;p::Journal<2> journal;std::atomic<bool> started{true},opened{false};
    journal.ReserveArm();journal.PublishArm(100,1000);
    std::latch checked(1),resume(1);
    std::thread arm([&]{
        const bool eligible=journal.InWindow(100)&&started.load(std::memory_order_acquire);
        checked.count_down();resume.wait();
        if(eligible)opened.store(n::TryOpenCapture());
    });
    checked.wait();started.store(false,std::memory_order_release);n::CloseCapture();journal.Close();
    resume.count_down();arm.join();
    Check(!opened.load()&&n::CaptureEnabled==2&&!journal.InWindow(100),"stop between arm precheck/publication leaves terminal fast gate closed");
    Check(!n::TryOpenCapture()&&n::CaptureEnabled==2,"closed gate cannot rearm");
    n::CaptureEnabled=0;Check(n::TryOpenCapture(),"normal arm opens once");n::CloseCapture();
    Check(n::CaptureEnabled==2&&!n::TryOpenCapture(),"stop after arm is terminal too");

    // Deterministic A-enter/B-enter/A-exit/B-exit: B cannot steal A's gate,
    // and B's eventual exit cannot resurrect A or clear a later owner C.
    constexpr unsigned a=11,b=22,c=33;n::OutputThread=0;
    auto as=n::EnterOutputScope(a);auto bs=n::EnterOutputScope(b);
    Check(as==n::OutputScope::Owned&&bs==n::OutputScope::Refused&&n::OutputThread==a,"overlap refuses B while A keeps output admission");
    n::LeaveOutputScope(a,as);Check(!n::OutputThread,"A exit empties the gate despite B overlap");
    n::LeaveOutputScope(b,bs);Check(!n::OutputThread,"B exit cannot restore finished A");
    as=n::EnterOutputScope(a);bs=n::EnterOutputScope(b);n::LeaveOutputScope(a,as);
    auto cs=n::EnterOutputScope(c);n::LeaveOutputScope(b,bs);
    Check(cs==n::OutputScope::Owned&&n::OutputThread==c,"refused B cannot clear later owner C");n::LeaveOutputScope(c,cs);
    as=n::EnterOutputScope(a);auto nested=n::EnterOutputScope(a);n::LeaveOutputScope(a,nested);
    Check(nested==n::OutputScope::Borrowed&&n::OutputThread==a,"nested same-thread scope preserves outer ownership");
    n::LeaveOutputScope(a,as);Check(!n::OutputThread,"outer exit drains borrowed scope gate");
    as=n::EnterOutputScope(a);bs=n::EnterOutputScope(b);n::LeaveOutputScope(b,bs);
    Check(n::OutputThread==a,"reverse overlap exit also preserves A");n::LeaveOutputScope(a,as);
}

static n::FxState seenFx{},returnFx{};
static n::Entry seenEntry{};
static unsigned nativeCalls=0,observerCalls=0,observerActive=0,seenError=0,seenKind=0;
static constexpr unsigned NativeError=0x14930001,ReturnEax=0xa1493001;
static const unsigned defaultMxcsr=0x1f80;
static void ResetFp(){__asm {fninit} __asm {ldmxcsr defaultMxcsr} __asm {cld}}
static void MakeFx(n::FxState& f,unsigned seed){
    ResetFp();auto* destination=&f;
    __asm {fld1} __asm {fldpi} __asm {mov eax,destination} __asm {fxsave [eax]}
    ResetFp();const std::uint16_t cw=seed?0x0b7f:0x077f;const unsigned csr=seed?0x5fa0:0x3f81;
    std::memcpy(f.bytes,&cw,2);std::memcpy(f.bytes+24,&csr,4);
    for(unsigned i=0;i<128;++i)f.bytes[160+i]=static_cast<unsigned char>(i^seed);
}
static bool SameFx(const n::FxState& a,const n::FxState& b){
    if(std::memcmp(a.bytes,b.bytes,5)||std::memcmp(a.bytes+6,b.bytes+6,22))return false;
    for(unsigned i=0;i<8;++i)if(std::memcmp(a.bytes+32+i*16,b.bytes+32+i*16,10))return false;
    return !std::memcmp(a.bytes+160,b.bytes+160,128);
}
static void DirtyObserver(){__asm {fninit} __asm {fldz} __asm {pxor xmm0,xmm0} __asm {pxor xmm7,xmm7} SetLastError(0xbad1493);}
// The first instructions are real MinHook fixtures. Each original captures its
// actual incoming machine state, then returns a different floating environment.
#define NATIVE_FIXTURE(Name,Words,Pop,Kind) \
__declspec(naked) static void __cdecl Name(){ \
    __asm {pushfd} __asm {pushad} __asm {fxsave seenFx} \
    __asm {mov eax,fs:[34h]} __asm {mov seenError,eax} __asm {mov seenKind,Kind} \
    __asm {mov esi,esp} __asm {mov edi,offset seenEntry} __asm {mov ecx,Words} \
    __asm {cld} __asm {rep movsd} __asm {inc nativeCalls} \
    __asm {popad} __asm {popfd} __asm {fxrstor returnFx} \
    __asm {mov dword ptr fs:[34h],14930001h} __asm {mov eax,0a1493001h} __asm {ret Pop} \
}
NATIVE_FIXTURE(NativeUpdate,11,4,0)
NATIVE_FIXTURE(NativeFilter,13,12,1)
NATIVE_FIXTURE(NativeSimulation,11,4,2)
NATIVE_FIXTURE(NativeOutput,13,12,3)
#undef NATIVE_FIXTURE
static void __cdecl FixtureDispatch(n::Frame* frame){
    ++observerCalls;++observerActive;bool normal=false;
    __try {
        DirtyObserver();n::Forwards[frame->kind](frame);normal=true;
        Check(seenKind==frame->kind,"exact native boundary forwards to its own original");
        Check(SameFx(seenFx,frame->incoming),"observer cannot change incoming x87/SSE state");
        Check(SameFx(frame->returned,returnFx),"native returned x87/SSE state captured before observer");
        Check(seenError==frame->incomingLastError&&frame->returnedLastError==NativeError,"LastError preserved both ways");
        Check(frame->result.eax==ReturnEax,"native EAX survives observer");
        const auto& a=frame->entry->registers;const auto& b=frame->result;
        Check(a.edi==b.edi&&a.esi==b.esi&&a.ebp==b.ebp&&a.ebx==b.ebx&&a.edx==b.edx&&a.ecx==b.ecx&&
            ((a.flags^b.flags)&0xed5)==0,"native remaining GPRs and application flags survive observer");
        Check(seenEntry.registers.ecx==frame->entry->registers.ecx,"exact ECX receiver");
        for(unsigned k=0;k<p::Proofs[frame->kind].args;++k)Check(seenEntry.args[k]==frame->entry->args[k],"exact raw stack argument bits");
        DirtyObserver();
    }__finally {--observerActive;}
}
static void __fastcall ThrowUpdate(void*,void*,unsigned){RaiseException(0xe1493001,0,0,nullptr);}
static bool CatchForward(n::Frame* frame){
    __try {FixtureDispatch(frame);return false;}
    __except(GetExceptionCode()==0xe1493001?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
static bool CatchThunk(){
    using One=unsigned(__thiscall*)(void*,unsigned);
    __try {reinterpret_cast<One>(n::UpdateThunk)(reinterpret_cast<void*>(0x14930000),0);return false;}
    __except(GetExceptionCode()==0xe1493001?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
static void AbiChecks(){
    MakeFx(returnFx,0x59);n::Observe=FixtureDispatch;
    const n::MachineEntry functions[]{NativeUpdate,NativeFilter,NativeSimulation,NativeOutput};
    // Exercise the same assembled forwarders used by the gameplay dispatcher,
    // including raw non-float bit patterns and a nondefault incoming FP state.
    for(unsigned k=0;k<4;++k){
        *n::Originals[k]=functions[k];n::Entry entry{};entry.registers={0x1111,0x2222,0x3333,0,0x4444,0x5555,0x14930000,0x7777,0x246};
        entry.args[0]=0x80000000;entry.args[1]=0x7fc01234;entry.args[2]=0x3f800000;
        n::Frame frame{};frame.entry=&entry;frame.kind=k;frame.incomingLastError=0x14935555;MakeFx(frame.incoming,0);
        const auto before=nativeCalls;FixtureDispatch(&frame);Check(nativeCalls==before+1&&!observerActive,"one original, drained normal path");ResetFp();
        *n::Originals[k]=nullptr;
    }
    // Literal per-boundary markers above prevent identical-code folding.
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<a;++b)
        Check(functions[a]!=functions[b],"four distinct native fixture entry points");
    Check(MH_Initialize()==MH_OK,"MinHook fixture initialize");
    for(unsigned k=0;k<4;++k)Check(MH_CreateHook(functions[k],n::Thunks[k],reinterpret_cast<void**>(n::Originals[k]))==MH_OK,"create actual boundary adapter");
    Check(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable fixture adapters");
    n::CaptureEnabled=1;n::AdmittedPhysics=0x14930000;n::AdmittedControl=0x14930000;n::OutputThread=GetCurrentThreadId();
    using One=unsigned(__thiscall*)(void*,unsigned);using Three=unsigned(__thiscall*)(void*,unsigned,unsigned,unsigned);
    for(unsigned k=0;k<4;++k){
        const auto before=nativeCalls,observed=observerCalls;
        n::FxState userFx;auto* saved=&userFx;
        if(p::Proofs[k].args==1){One volatile call=reinterpret_cast<One>(functions[k]);Check(call(reinterpret_cast<void*>(0x14930000),0x80000000)==ReturnEax,"one-word thunk return/stack");}
        else {Three volatile call=reinterpret_cast<Three>(functions[k]);Check(call(reinterpret_cast<void*>(0x14930000),0x80000000,0x7fc01234,0x3f800000)==ReturnEax,"three-word thunk return/stack");}
        __asm {mov eax,saved} __asm {fxsave [eax]}
        Check(SameFx(userFx,returnFx),"native returned FP state reaches actual caller after observer dirties FP");
        Check(nativeCalls==before+1&&observerCalls==observed+1&&!observerActive,"real hook performs exactly one original");ResetFp();
    }
    // Unadmitted and closed paths must bypass the entire observer/FX gateway.
    One volatile update=reinterpret_cast<One>(functions[0]);auto observed=observerCalls;
    Check(update(reinterpret_cast<void*>(0x14939999),123)==ReturnEax&&observerCalls==observed,"foreign receiver cheap passthrough");ResetFp();
    n::CloseCapture();Check(update(reinterpret_cast<void*>(0x14930000),123)==ReturnEax&&observerCalls==observed,"closed gate cheap passthrough");ResetFp();
    Check(MH_DisableHook(MH_ALL_HOOKS)==MH_OK,"disable fixture hooks");
    for(auto function:functions)Check(MH_RemoveHook(function)==MH_OK,"fixture only removes drained trampolines");
    Check(MH_Uninitialize()==MH_OK,"MinHook fixture retire");
    n::Entry entry{};entry.registers.flags=0x202;n::Frame frame{};frame.entry=&entry;frame.kind=0;MakeFx(frame.incoming,0);
    n::UpdateOriginal=reinterpret_cast<n::MachineEntry>(ThrowUpdate);
    Check(CatchForward(&frame)&&!observerActive,"SEH propagates and finally drains observer");ResetFp();
    n::CaptureEnabled=1;
    Check(CatchThunk()&&!observerActive,"SEH traverses actual entry/forward adapters with finally cleanup");ResetFp();
}
#endif
int main(){NativeOwnerChecks();JournalChecks();
#if defined(FVR_BC2_VEHICLE_PHYSICS_GATEWAYS) && defined(_MSC_VER) && defined(_M_IX86)
    GateChecks();AbiChecks();
#endif
    return failures?1:0;
}
