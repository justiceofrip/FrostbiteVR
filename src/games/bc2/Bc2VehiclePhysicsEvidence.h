#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Private, read-only function-boundary evidence. No operation capability.
namespace fvr::bc2::vehiclePhysicsEvidence {
using Word=std::uint32_t;
enum class Kind:Word {Update,Filter,Simulation,Output,Consumer,Associated94};
// Keep Consumer=4 in existing traces; native gateway index4 is Associated94.
inline constexpr Kind BoundaryKind(Word index)noexcept {return index==4?Kind::Associated94:static_cast<Kind>(index);}
enum Reason:Word {Okay=0,Owner=1,ReadFailed=2,Identity=4,Torn=8,Caller=16,
    NoParent=32,Nested=64,Overlap=128,Unwind=256,OutputBody=512,Code=1024};
struct Proof {Word rva,size,args;const char* hash;};
inline constexpr std::array<Proof,5> Proofs{{
    {0x6e9e50,482,1,"d3b168e87a32a58401587b7d57c1a4e76c4ebde454739c175c8bf4172d7d64fc"},
    {0x6d9bd0,838,3,"0df0f38e949d72961a6142ffdc7bc999fbda144f14df958f2fa1d52f7acc68b2"},
    {0x6e9690,637,1,"e68f5bec3c40268bccf84558f59ec130af07e5ddb206f4820ac7a3b973dac1a5"},
    {0x6a7830,108,3,"93b426a6868e1f427d6afc50ce322ef6930ba7bceae2d9c609c21caefc6b7b44"},
    {0x69edb0,61,0,"b13cc97f7312dad81d033b52efe53019c6dd612d9c1938b71b5c7f224215efc0"}
}};
inline constexpr Word PhysicsVtableRva=0x1056550,FilterCallerRva=0x6e9f14,SimulationCallerRva=0x6ea02a;
inline constexpr Word AssociatedVtableRva=0x10548e0,Associated94CallerRva=0x6ea018;
// Unpatched, exact predicate dependencies. No independent native calls.
inline constexpr std::array<Proof,4> PredicateDependencies{{
    {0x8bcde0,38,1,"60a1f5d772a93344c50768fdaeb9c84bb7a0ed9d4af852b8d2f4969573047996"},
    {0x6a6c90,37,0,"1339327da0559973e5ac3574228af4c0f530b9130d60a8acc2d344d4be86e9a3"},
    {0x8bc060,24,0,"5cbbebbbbecd6fb4befe815466dbc95f2ac371306fecbd9e3e8cc31b47dbee65"},
    {0x8bf130,71,0,"33ede69dfd05068c789f31696dd333215b8bd337b2979784930575322b3b8f78"}
}};
inline constexpr Word TransformGetterRva=0x6a72e0;
inline constexpr std::array<unsigned char,12> TransformGetter{0x8b,0x81,0x8c,0,0,0,0x05,0xe0,0,0,0,0xc3};
struct Sample {
    Word physics=0,vtable=0,control=0,controlConfig=0,associated=0,associatedVtable=0,body=0,target94=0;
    Word state=0,gate7c=0,filterFlag=0,nested=0,nestedFlag=0,reasons=0;
    // Raw IEEE words only. +04..+38, config +00..+4c, body+e0..+11f.
    std::array<Word,14> controls{},controlsAgain{};
    std::array<Word,20> config{};
    std::array<Word,16> transform{},transformAgain{};
};
// Validate retains the strict consumer result for its own caller. Raw sampling
// depends only on its separately established native-current output, including
// both bookends; publication-only identity changes cannot erase the sample.
template<class View,class Validate,class Collect>
Sample ReadNativeOwnedSample(Validate&& validate,Collect&& collect)noexcept {
    Sample sample;View view{};bool current=false;
    (void)validate(view,current);
    if(!current){sample.reasons|=Owner;return sample;}
    collect(sample);
    current=false;(void)validate(view,current);
    if(!current)sample.reasons|=Owner;
    return sample;
}
// Read-only boundary snapshots, not the native instruction's atomic operands.
// Equal bookends exclude observed changes only; actual returnAL is separate.
struct PredicateSample {
    Word body=0,subobject=0,subobjectFlags=0,bodyType=0,alternate=0,selected=0,scalar=0,target7c=0;
    Word readMask=0,reasons=0;
    bool operator==(const PredicateSample&)const=default;
};
struct Record {
    Kind kind=Kind::Update;Word thread=0,receiver=0,caller=0,reasons=0;
    std::array<Word,3> arguments{};
    std::array<Word,4> output0{},output1{};
    Word outputBody=0,outputBodyAgain=0,outputReadMask=0,consumerReasons=0;
    std::uint64_t sequence=0,parentUpdate=0,parentSimulation=0,consumerInvocation=0;
    std::int64_t begin=0,end=0;Sample before{},after{};
    PredicateSample predicateBefore{},predicateAfter{};
    Word predicateReturnEax=0;
    bool predicateReturned=false,normal=false;
};
struct Slot {Record row{};std::atomic<bool> complete{false};};
template<std::size_t Capacity=4096> class Journal {
public:
    static constexpr std::size_t MaxRecords=Capacity;
    // One-shot state: 0 unarmed, 1 arming, 2 armed, 3 closed. Context is
    // initialized by the caller before PublishArm's release publication.
    bool ReserveArm()noexcept {unsigned expected=0;return state.compare_exchange_strong(expected,1);}
    void PublishArm(std::int64_t now,std::int64_t frequency)noexcept {
        begin=now;deadline=now+2*frequency;unsigned expected=1;
        if(state.compare_exchange_strong(expected,2,std::memory_order_release))published.store(true,std::memory_order_release);
    }
    bool InWindow(std::int64_t now)const noexcept {
        return state.load(std::memory_order_acquire)==2&&now>=begin&&now<=deadline;
    }
    void Close()noexcept {state.store(3,std::memory_order_release);}
    Slot* Claim(Kind kind,std::int64_t now)noexcept {
        if(!InWindow(now)){++outside;return nullptr;}
        if(kind==Kind::Update&&updates.fetch_add(1)>=512){++updateLimit;Close();return nullptr;}
        const auto index=claimed.fetch_add(1);
        if(index>=Capacity){++overflow;Close();return nullptr;}
        auto* slot=&rows[index];slot->row.kind=kind;slot->row.sequence=index+1;slot->row.begin=now;return slot;
    }
    std::array<Slot,Capacity> rows{};
    std::atomic<unsigned> state{0},claimed{0},updates{0},overflow{0},updateLimit{0};
    std::atomic<std::uint64_t> outside{0},contextDrops{0},foreign{0},unscoped{0},predicateOtherCaller{0};
    std::atomic<unsigned> overlap{0};std::int64_t begin=0,deadline=0;
    std::atomic<bool> published{false};
};
static_assert(std::is_trivially_copyable_v<Record> && sizeof(Journal<>)<=4*1024*1024);
inline bool Comparable(const Record& r)noexcept {
    return r.normal&&!r.reasons&&!r.before.reasons&&!r.after.reasons&&
        (r.kind!=Kind::Associated94||(r.predicateReturned&&!r.predicateBefore.reasons&&!r.predicateAfter.reasons))&&
        r.before.physics==r.after.physics&&r.before.control==r.after.control&&
        r.before.associated==r.after.associated&&r.before.body==r.after.body;
}
} // namespace fvr::bc2::vehiclePhysicsEvidence

// Only the gameplay TU and the x86 ABI fixture instantiate these gateways.
// Five function entries, no interior patches. Adapted from projectile302's
// existing full-machine-state gateway; copied args have no pointer identity.
#if defined(FVR_BC2_VEHICLE_PHYSICS_GATEWAYS) && defined(_MSC_VER) && defined(_M_IX86)
#include <intrin.h>
namespace fvr::bc2::vehiclePhysicsEvidence::native {
struct Registers {Word edi,esi,ebp,pushadEsp,ebx,edx,ecx,eax,flags;};
struct Entry {Registers registers;Word caller,args[3];};
struct alignas(16) FxState {unsigned char bytes[512];};
struct alignas(16) Frame {
    FxState incoming,returned;Registers result;Entry* entry;
    Word incomingLastError,returnedLastError,kind;unsigned char reserved[12];
};
static_assert(sizeof(Frame)==1088&&offsetof(Frame,entry)==1060&&offsetof(Frame,kind)==1072);
static_assert(offsetof(Frame,returned)==512&&offsetof(Frame,result)==1024);
static_assert(offsetof(Entry,caller)==36&&offsetof(Entry,args)==40);
using MachineEntry=void(__cdecl*)();
using DispatchFn=void(__cdecl*)(Frame*);
inline DispatchFn Observe=nullptr;
inline MachineEntry UpdateOriginal=nullptr,FilterOriginal=nullptr,SimulationOriginal=nullptr,OutputOriginal=nullptr,Associated94Original=nullptr;
inline const Word DefaultMxcsr=0x1f80;
// Direct aligned x86 entry loads; all live transitions use interlocked ops.
// Capture is one-shot: 0 pending, 1 open, 2 terminal closed. A late arm can
// never overwrite CloseCapture, even after it passed its journal/start checks.
inline volatile Word CaptureEnabled=0,AdmittedPhysics=0,AdmittedControl=0,AdmittedAssociated=0,OutputThread=0;
inline bool TryOpenCapture()noexcept {
    return _InterlockedCompareExchange(reinterpret_cast<volatile long*>(&CaptureEnabled),1,0)==0;
}
inline void CloseCapture()noexcept {
    _InterlockedExchange(reinterpret_cast<volatile long*>(&CaptureEnabled),2);
}
enum class OutputScope {Refused,Borrowed,Owned};
inline OutputScope EnterOutputScope(Word thread)noexcept {
    if(!thread)return OutputScope::Refused;
    const auto previous=Word(_InterlockedCompareExchange(reinterpret_cast<volatile long*>(&OutputThread),long(thread),0));
    if(!previous)return OutputScope::Owned;
    return previous==thread?OutputScope::Borrowed:OutputScope::Refused;
}
inline void LeaveOutputScope(Word thread,OutputScope scope)noexcept {
    // Only the outer owner clears its own gate. Nested same-thread scopes
    // borrow it; other threads are explicitly refused and never restore it.
    if(scope==OutputScope::Owned)
        _InterlockedCompareExchange(reinterpret_cast<volatile long*>(&OutputThread),0,long(thread));
}
void __cdecl Dispatch(Frame* frame){Observe(frame);}

__declspec(naked) void __cdecl UpdateThunk(){
    __asm {
        pushfd
        cmp dword ptr [CaptureEnabled],1
        jne passthrough
        cmp ecx,dword ptr [AdmittedPhysics]
        jne passthrough
        popfd
        pushfd
        pushad
        mov eax,esp
        sub esp,1088
        and esp,-16
        mov [esp+1060],eax
        mov eax,fs:[34h]
        mov [esp+1064],eax
        mov dword ptr [esp+1072],0
        fxsave [esp]
        fninit
        ldmxcsr DefaultMxcsr
        cld
        mov eax,esp
        push eax
        call Dispatch
        lea esp,[esp+4]
        fxrstor [esp+512]
        mov eax,[esp+1068]
        mov fs:[34h],eax
        mov eax,[esp+1060]
        mov edi,eax
        lea esi,[esp+1024]
        mov ecx,9
        cld
        rep movsd
        mov esp,eax
        popad
        popfd
        ret 4
    passthrough:
        popfd
        jmp dword ptr [UpdateOriginal]
    }
}
__declspec(naked) void __cdecl UpdateForward(Frame*){
    __asm {
        pushfd
        pushad
        mov edx,esp
        mov eax,[edx+40]
        sub esp,544
        and esp,-16
        fxsave [esp]
        mov [esp+512],edx
        mov [esp+516],eax
        mov edx,[eax+1064]
        mov fs:[34h],edx
        fxrstor [eax]
        mov edx,[eax+1060]
        push dword ptr [edx+40]
        push dword ptr [edx+32]
        popfd
        mov edi,[edx]
        mov esi,[edx+4]
        mov ebp,[edx+8]
        mov ebx,[edx+16]
        mov ecx,[edx+24]
        mov eax,[edx+28]
        mov edx,[edx+20]
        call dword ptr [UpdateOriginal]
        pushfd
        pushad
        mov eax,[esp+552]
        fxsave [eax+512]
        mov edx,fs:[34h]
        mov [eax+1068],edx
        lea edi,[eax+1024]
        mov esi,esp
        mov ecx,9
        cld
        rep movsd
        lea esp,[esp+36]
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl FilterThunk(){
    __asm {
        pushfd
        cmp dword ptr [CaptureEnabled],1
        jne passthrough
        cmp ecx,dword ptr [AdmittedControl]
        jne passthrough
        popfd
        pushfd
        pushad
        mov eax,esp
        sub esp,1088
        and esp,-16
        mov [esp+1060],eax
        mov eax,fs:[34h]
        mov [esp+1064],eax
        mov dword ptr [esp+1072],1
        fxsave [esp]
        fninit
        ldmxcsr DefaultMxcsr
        cld
        mov eax,esp
        push eax
        call Dispatch
        lea esp,[esp+4]
        fxrstor [esp+512]
        mov eax,[esp+1068]
        mov fs:[34h],eax
        mov eax,[esp+1060]
        mov edi,eax
        lea esi,[esp+1024]
        mov ecx,9
        cld
        rep movsd
        mov esp,eax
        popad
        popfd
        ret 12
    passthrough:
        popfd
        jmp dword ptr [FilterOriginal]
    }
}
__declspec(naked) void __cdecl FilterForward(Frame*){
    __asm {
        pushfd
        pushad
        mov edx,esp
        mov eax,[edx+40]
        sub esp,544
        and esp,-16
        fxsave [esp]
        mov [esp+512],edx
        mov [esp+516],eax
        mov edx,[eax+1064]
        mov fs:[34h],edx
        fxrstor [eax]
        mov edx,[eax+1060]
        push dword ptr [edx+48]
        push dword ptr [edx+44]
        push dword ptr [edx+40]
        push dword ptr [edx+32]
        popfd
        mov edi,[edx]
        mov esi,[edx+4]
        mov ebp,[edx+8]
        mov ebx,[edx+16]
        mov ecx,[edx+24]
        mov eax,[edx+28]
        mov edx,[edx+20]
        call dword ptr [FilterOriginal]
        pushfd
        pushad
        mov eax,[esp+552]
        fxsave [eax+512]
        mov edx,fs:[34h]
        mov [eax+1068],edx
        lea edi,[eax+1024]
        mov esi,esp
        mov ecx,9
        cld
        rep movsd
        lea esp,[esp+36]
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl SimulationThunk(){
    __asm {
        pushfd
        cmp dword ptr [CaptureEnabled],1
        jne passthrough
        cmp ecx,dword ptr [AdmittedPhysics]
        jne passthrough
        popfd
        pushfd
        pushad
        mov eax,esp
        sub esp,1088
        and esp,-16
        mov [esp+1060],eax
        mov eax,fs:[34h]
        mov [esp+1064],eax
        mov dword ptr [esp+1072],2
        fxsave [esp]
        fninit
        ldmxcsr DefaultMxcsr
        cld
        mov eax,esp
        push eax
        call Dispatch
        lea esp,[esp+4]
        fxrstor [esp+512]
        mov eax,[esp+1068]
        mov fs:[34h],eax
        mov eax,[esp+1060]
        mov edi,eax
        lea esi,[esp+1024]
        mov ecx,9
        cld
        rep movsd
        mov esp,eax
        popad
        popfd
        ret 4
    passthrough:
        popfd
        jmp dword ptr [SimulationOriginal]
    }
}
__declspec(naked) void __cdecl SimulationForward(Frame*){
    __asm {
        pushfd
        pushad
        mov edx,esp
        mov eax,[edx+40]
        sub esp,544
        and esp,-16
        fxsave [esp]
        mov [esp+512],edx
        mov [esp+516],eax
        mov edx,[eax+1064]
        mov fs:[34h],edx
        fxrstor [eax]
        mov edx,[eax+1060]
        push dword ptr [edx+40]
        push dword ptr [edx+32]
        popfd
        mov edi,[edx]
        mov esi,[edx+4]
        mov ebp,[edx+8]
        mov ebx,[edx+16]
        mov ecx,[edx+24]
        mov eax,[edx+28]
        mov edx,[edx+20]
        call dword ptr [SimulationOriginal]
        pushfd
        pushad
        mov eax,[esp+552]
        fxsave [eax+512]
        mov edx,fs:[34h]
        mov [eax+1068],edx
        lea edi,[eax+1024]
        mov esi,esp
        mov ecx,9
        cld
        rep movsd
        lea esp,[esp+36]
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl OutputThunk(){
    __asm {
        pushfd
        cmp dword ptr [CaptureEnabled],1
        jne passthrough
        push eax
        mov eax,fs:[24h]
        cmp eax,dword ptr [OutputThread]
        pop eax
        jne passthrough
        popfd
        pushfd
        pushad
        mov eax,esp
        sub esp,1088
        and esp,-16
        mov [esp+1060],eax
        mov eax,fs:[34h]
        mov [esp+1064],eax
        mov dword ptr [esp+1072],3
        fxsave [esp]
        fninit
        ldmxcsr DefaultMxcsr
        cld
        mov eax,esp
        push eax
        call Dispatch
        lea esp,[esp+4]
        fxrstor [esp+512]
        mov eax,[esp+1068]
        mov fs:[34h],eax
        mov eax,[esp+1060]
        mov edi,eax
        lea esi,[esp+1024]
        mov ecx,9
        cld
        rep movsd
        mov esp,eax
        popad
        popfd
        ret 12
    passthrough:
        popfd
        jmp dword ptr [OutputOriginal]
    }
}
__declspec(naked) void __cdecl OutputForward(Frame*){
    __asm {
        pushfd
        pushad
        mov edx,esp
        mov eax,[edx+40]
        sub esp,544
        and esp,-16
        fxsave [esp]
        mov [esp+512],edx
        mov [esp+516],eax
        mov edx,[eax+1064]
        mov fs:[34h],edx
        fxrstor [eax]
        mov edx,[eax+1060]
        push dword ptr [edx+48]
        push dword ptr [edx+44]
        push dword ptr [edx+40]
        push dword ptr [edx+32]
        popfd
        mov edi,[edx]
        mov esi,[edx+4]
        mov ebp,[edx+8]
        mov ebx,[edx+16]
        mov ecx,[edx+24]
        mov eax,[edx+28]
        mov edx,[edx+20]
        call dword ptr [OutputOriginal]
        pushfd
        pushad
        mov eax,[esp+552]
        fxsave [eax+512]
        mov edx,fs:[34h]
        mov [eax+1068],edx
        lea edi,[eax+1024]
        mov esi,esp
        mov ecx,9
        cld
        rep movsd
        lea esp,[esp+36]
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        ret
    }
}
__declspec(naked) void __cdecl Associated94Thunk(){
    __asm {
        pushfd
        cmp dword ptr [CaptureEnabled],1
        jne passthrough
        cmp ecx,dword ptr [AdmittedAssociated]
        jne passthrough
        popfd
        pushfd
        pushad
        mov eax,esp
        sub esp,1088
        and esp,-16
        mov [esp+1060],eax
        mov eax,fs:[34h]
        mov [esp+1064],eax
        mov dword ptr [esp+1072],4
        fxsave [esp]
        fninit
        ldmxcsr DefaultMxcsr
        cld
        mov eax,esp
        push eax
        call Dispatch
        lea esp,[esp+4]
        fxrstor [esp+512]
        mov eax,[esp+1068]
        mov fs:[34h],eax
        mov eax,[esp+1060]
        mov edi,eax
        lea esi,[esp+1024]
        mov ecx,9
        cld
        rep movsd
        mov esp,eax
        popad
        popfd
        ret
    passthrough:
        popfd
        jmp dword ptr [Associated94Original]
    }
}
__declspec(naked) void __cdecl Associated94Forward(Frame*){
    __asm {
        pushfd
        pushad
        mov edx,esp
        mov eax,[edx+40]
        sub esp,544
        and esp,-16
        fxsave [esp]
        mov [esp+512],edx
        mov [esp+516],eax
        mov edx,[eax+1064]
        mov fs:[34h],edx
        fxrstor [eax]
        mov edx,[eax+1060]
        push dword ptr [edx+32]
        popfd
        mov edi,[edx]
        mov esi,[edx+4]
        mov ebp,[edx+8]
        mov ebx,[edx+16]
        mov ecx,[edx+24]
        mov eax,[edx+28]
        mov edx,[edx+20]
        call dword ptr [Associated94Original]
        pushfd
        pushad
        mov eax,[esp+552]
        fxsave [eax+512]
        mov edx,fs:[34h]
        mov [eax+1068],edx
        lea edi,[eax+1024]
        mov esi,esp
        mov ecx,9
        cld
        rep movsd
        lea esp,[esp+36]
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        ret
    }
}

inline const std::array<MachineEntry,5> Thunks{UpdateThunk,FilterThunk,SimulationThunk,OutputThunk,Associated94Thunk};
inline const std::array<void(__cdecl*)(Frame*),5> Forwards{UpdateForward,FilterForward,SimulationForward,OutputForward,Associated94Forward};
inline const std::array<MachineEntry*,5> Originals{&UpdateOriginal,&FilterOriginal,&SimulationOriginal,&OutputOriginal,&Associated94Original};
} // namespace fvr::bc2::vehiclePhysicsEvidence::native
#endif
