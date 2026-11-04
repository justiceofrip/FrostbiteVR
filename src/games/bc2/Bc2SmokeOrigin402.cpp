#include "Bc2SmokeOrigin402.h"
#include <intrin.h>
#include <algorithm>

namespace fvr::bc2::smoke402 {
Session Runtime;
Word EarlyResume=0,ResetResume=0,ResetTarget=0;
static const Word DefaultMxcsr=0x1f80;
// 36 bytes pushfd/pushad, then a 16-aligned 544-byte area: FXSAVE at0,
// saved-register pointer at512 and incoming LastError at516. No FP instruction
// precedes FXSAVE. Each allocation remains smaller than a guard page.
__declspec(naked) void __cdecl EarlyThunk(){
    __asm {
        pushfd
        pushad
        mov eax,esp
        sub esp,544
        and esp,-16
        mov [esp+512],eax
        mov edx,fs:[34h]
        mov [esp+516],edx
        fxsave [esp]
        fninit
        ldmxcsr DefaultMxcsr
        cld
        push eax
        call EarlyDispatch
        lea esp,[esp+4]
        fxrstor [esp]
        mov edx,[esp+516]
        mov fs:[34h],edx
        mov esp,[esp+512]
        popad
        popfd
        lea edx,[esp+74h]
        push edx
        jmp dword ptr [EarlyResume]
    }
}
__declspec(naked) void __cdecl ResetThunk(){
    __asm {
        pushfd
        pushad
        mov eax,esp
        sub esp,544
        and esp,-16
        mov [esp+512],eax
        mov edx,fs:[34h]
        mov [esp+516],edx
        fxsave [esp]
        fninit
        ldmxcsr DefaultMxcsr
        cld
        push eax
        call ResetDispatch
        lea esp,[esp+4]
        fxrstor [esp]
        mov edx,[esp+516]
        mov fs:[34h],edx
        mov esp,[esp+512]
        popad
        popfd
        // Exact CALL534460 semantics, including native return57a17b, while
        // preserving every incoming GPR/flag and the caller's real stack.
        push dword ptr [ResetResume]
        jmp dword ptr [ResetTarget]
    }
}
bool StackExtent(Word primary)noexcept {
    const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto low=reinterpret_cast<Word>(tib->StackLimit),high=reinterpret_cast<Word>(tib->StackBase);
    if(primary<0x10000||(primary&15)||primary<low||std::uint64_t(primary)+64>high)return false;
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(reinterpret_cast<void*>(primary),&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&PAGE_GUARD)||reinterpret_cast<std::uintptr_t>(region.BaseAddress)>primary||
       std::uint64_t(primary)+64>reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize)return false;
    const auto p=region.Protect&0xff;
    return p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY;
}
static bool ReadWord(Word at,Word& value)noexcept {
    __try {value=*reinterpret_cast<const volatile Word*>(at);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool WriteWord(Word at,Word value)noexcept {
    __try {*reinterpret_cast<volatile Word*>(at)=value;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct Access {
    static thread_local Scope* current;
    static thread_local std::uint64_t lastScope,lastInvocation;
    static thread_local unsigned depth;
    static thread_local bool poisoned;
    static void Poison(Scope& s)noexcept {s.ambiguous_=true;s.outcome_=Outcome::Poisoned;}
    static bool Live(const Scope& s)noexcept {
        const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
        return current==&s&&!poisoned&&!s.ambiguous_&&!s.closed_&&depth==1&&
            s.ownerThread_==GetCurrentThreadId()&&s.key_.thread==s.ownerThread_&&
            Runtime.enabled.load(std::memory_order_acquire)&&!Runtime.closed.load(std::memory_order_acquire)&&
            s.stackLow_==reinterpret_cast<Word>(tib->StackLimit)&&s.stackHigh_==reinterpret_cast<Word>(tib->StackBase)&&
            StackExtent(s.primary_);
    }
    static bool PrepareCommit(Scope& s,const Entry& e,Admission& a,origin386::Bits& before)noexcept {
        __try {
            const auto& api=Runtime.api;
            if(!Live(s)||!api.prepare||!api.validate||!api.prepare(api.context,s.key_,e,a)||!Live(s)||
               a.key!=s.key_||a.primary!=s.primary_||!a.proof.evidence||!a.proof.activeExit||
               !a.proof.referenceSpace||a.proof.originalDeadline<=0||
               !origin386::BoundedAbsolute(a.absoluteNativeXyz,a.positiveNativeBoundBits))return false;
            for(unsigned i=0;i<3;++i)if(!ReadWord(s.primary_+0x30+i*4,before[i]))return false;
            return api.validate(api.context,s.key_,e,a)&&Live(s);
        }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    static bool Commit(Scope& s,const Entry& e){
        Admission a{};origin386::Bits before{};
        if(!PrepareCommit(s,e,a,before))return false;
        const auto at=s.primary_+0x30;
        const auto result=origin386::Commit(before,a.absoluteNativeXyz,
            [at](unsigned i,Word v)noexcept{return WriteWord(at+i*4,v);},
            [at](unsigned i,Word& v)noexcept{return ReadWord(at+i*4,v);});
        if(result==origin386::WriteResult::Committed)return true;
        if(result==origin386::WriteResult::Restored)return false;
        // Only failed synchronous PRE-NATIVE stores may be restored. Native
        // effects/physical writes are never inside this transaction or caught.
        RaiseException(0xe0402001u,EXCEPTION_NONCONTINUABLE,0,nullptr);
        __fastfail(FAST_FAIL_INVALID_ARG);
    }
    static void Early(const Registers& r){
        auto* s=current;if(!s)return;
        if(depth!=1||s->earlySeen_||s->physicalSeen_){Poison(*s);return;}
        s->earlySeen_=true;s->outcome_=Outcome::Refused;
        const auto p=std::uint64_t(r.pushadEsp)+4+0x74;
        if(p>UINT32_MAX)return;
        s->primary_=Word(p);
        const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
        s->stackLow_=reinterpret_cast<Word>(tib->StackLimit);s->stackHigh_=reinterpret_cast<Word>(tib->StackBase);
        Entry e{Boundary::EarlyEffect,r,s->primary_,nullptr};
        if(Commit(*s,e)){s->earlyCommitted_=true;s->outcome_=Outcome::EarlyCommitted;++Runtime.earlyCommitted;}
    }
    static void Reset(const Registers& r)noexcept {
        auto* s=current;if(!s||!s->earlySeen_)return;
        if(depth!=1||s->resetSeen_||s->physicalSeen_){Poison(*s);return;}
        s->resetSeen_=true; // duplicate markers can never reopen eligibility
        const auto p=std::uint64_t(r.pushadEsp)+4+0x60;
        if(p!=s->primary_||!s->earlyCommitted_||!Live(*s)){Poison(*s);return;}
        const Entry e{Boundary::CameraReset,r,s->primary_,nullptr};
        bool valid=false;
        __try {valid=Runtime.api.reset&&Runtime.api.reset(Runtime.api.context,s->key_,e)&&Live(*s);}
        __except(EXCEPTION_EXECUTE_HANDLER){valid=false;}
        if(!valid){Poison(*s);return;}
        s->outcome_=Outcome::ResetObserved;
    }
    static bool Physical(const projectile302::Entry& e){
        auto* s=current;if(!s)return false;
        // Dynamic effect code may itself call7155b0. Only the independently
        // verified physical caller return participates in this selection epoch.
        // The host additionally repeats the exact helper/receiver/all11 args.
        if(e.returnAddress!=0x57aa0d)return s->earlySeen_;
        if(!s->earlySeen_){s->physicalSeen_=true;return false;}
        if(depth!=1||projectile302::Depth!=1){Poison(*s);return true;}
        if(s->physicalSeen_)return true;
        s->physicalSeen_=true;
        if(e.args[1]!=s->primary_){Poison(*s);return true;}
        if(!s->earlyCommitted_)return true; // early refusal forbids later-only admission
        if(!s->resetSeen_)return true; // native may have mutated P: preserve it
        if(s->outcome_!=Outcome::ResetObserved||!Live(*s)){s->outcome_=Outcome::Partial;return true;}
        const Entry entry{Boundary::Physical,e.registers,e.args[1],&e};
        if(Commit(*s,entry)){s->outcome_=Outcome::PhysicalCommitted;++Runtime.physicalCommitted;}
        else s->outcome_=Outcome::Partial;
        return true;
    }
};
thread_local Scope* Access::current=nullptr;
thread_local std::uint64_t Access::lastScope=0,Access::lastInvocation=0;
thread_local unsigned Access::depth=0;
thread_local bool Access::poisoned=false;
Scope::Scope(const Key& key)noexcept:key_(key),previous_(Access::current),ownerThread_(GetCurrentThreadId()){
    const bool fresh=key.scopeSerial>Access::lastScope||(key.scopeSerial==Access::lastScope&&key.invocation>Access::lastInvocation);
    if(fresh&&key.scopeSerial&&key.invocation){Access::lastScope=key.scopeSerial;Access::lastInvocation=key.invocation;}
    if(Access::poisoned||!fresh||!key.scopeSerial||!key.invocation||!key.primaryLifetime||key.thread!=ownerThread_)Access::Poison(*this);
    if(previous_||Access::depth){Access::Poison(*this);if(previous_)Access::Poison(*previous_);}
    Access::current=this;
}
void Scope::Close()noexcept {
    if(closed_)return;closed_=true;
    if(ownerThread_!=GetCurrentThreadId()||Access::current!=this){Access::poisoned=true;Access::current=nullptr;return;}
    if(earlyCommitted_&&(!physicalSeen_||outcome_==Outcome::Partial||outcome_==Outcome::Poisoned))++Runtime.partial;
    if(outcome_==Outcome::Poisoned)++Runtime.poisoned;
    else if(earlySeen_&&!earlyCommitted_)++Runtime.refused;
    Access::current=previous_;
}
void __cdecl EarlyDispatch(const Registers* r){
    InterlockedIncrement(&Runtime.active);++Access::depth;
    __try {if(r)Access::Early(*r);}
    __finally {--Access::depth;InterlockedDecrement(&Runtime.active);}
}
void __cdecl ResetDispatch(const Registers* r)noexcept {
    InterlockedIncrement(&Runtime.active);++Access::depth;
    __try {if(r)Access::Reset(*r);}
    __finally {--Access::depth;InterlockedDecrement(&Runtime.active);}
}
bool BeforePhysical(const projectile302::Entry& e){
    bool handled=false;InterlockedIncrement(&Runtime.active);++Access::depth;
    __try {handled=Access::Physical(e);}
    __finally {--Access::depth;InterlockedDecrement(&Runtime.active);}
    return handled;
}
bool SuppressOrdinaryEntry()noexcept {return Access::current&&Access::current->CrossedEarly();}
bool VerifySeams(std::span<const std::byte> ef,std::span<const std::byte> rf,
                 std::span<const std::byte> el,std::span<const std::byte> rl)noexcept {
    const auto exact=[](std::span<const std::byte> s,const auto& b)noexcept{
        return s.size()==b.size()&&std::equal(s.begin(),s.end(),b.begin(),[](auto a,auto v){return std::to_integer<unsigned char>(a)==v;});};
    return exact(ef,EarlyBytes)&&exact(el,EarlyBytes)&&exact(rf,ResetBytes)&&exact(rl,ResetBytes);
}
} // namespace fvr::bc2::smoke402
