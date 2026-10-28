#include "Bc2SelectedKnifeNative.h"
#include "Bc2SelectedKnifeCode.h"
#include "Bc2ReloadFlow.h"
#include "Bc2KnifeSoundObservation.h"
#include "Bc2KnifeTimingObservation.h"
#include <atomic>
#include <mutex>
#if defined(_WIN32) && defined(_M_IX86) && defined(FVR_BC2_CHEST_KNIFE_DRAW_RETURN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
namespace fvr::bc2::selectedKnife {namespace {
using namespace interaction;
struct Row {
    std::uint64_t serial=0,input=0;unsigned cause=0,effects=0,attacker=0,handle=0,target=0;
    std::int64_t observedNs=0,deadlineNs=0,stagedNs=0,publishedNs=0,revokedNs=0,acquiredNs=0,attackedNs=0;
    InputDelivery inputDelivery{};
    unsigned searches=0,copies=0,damageCalls=0,failure=0,acquisitionFailure=0,acquisitionPriorFailure=0;
    bool acquisitionReturned=false,attackReturned=false,healthKnown=false;
    float healthBefore=0,healthAfter=0;
    math::Matrix4 nativeBefore{},blade{};
};
struct Action {
    SelectedMeleeIntent intent{};Native native{};std::optional<Geometry> geometry;
    float units=1;bool published=false,acquired=false,attackInFlight=false,ambiguousAttack=false;
    std::uint64_t admissionEpoch=0;
    std::optional<Target> target;Row row{};
};
struct Service {
    std::mutex gate;std::atomic<bool> enabled=false,installed=false;std::atomic<unsigned> active=0;
    std::atomic<std::uint64_t> admissionEpoch=1;
    ReloadStateMemory memory{};ReloadStateBinding state{};ReloadServerBinding server{};unsigned base=0;
    std::int64_t frequency=0;bool verified=false,cleanupFailed=false;unsigned installFailure=0;SelectedMeleeAttack policy;
    std::optional<Action> action;RecognitionGap recognitionGap;
    std::array<Row,32> rows{};unsigned count=0,dropped=0,prepared=0,rejected=0,readGaps=0,geometryGaps=0,unknown=0;
    std::array<unsigned,8> geometryFailures{};
    PreparationTelemetry preparation{};
    struct HookCounts {
        std::atomic<std::uint64_t> entries=0;
        std::atomic<std::uint64_t> rawLastBoundary=0;
        std::array<std::atomic<std::uint64_t>,unsigned(HookOutcome::Count)> outcomes{};
    } acquire,attack;
    std::int64_t nativeObservedNs=0,lastHookNs=0;
    std::array<unsigned,3> nativeCurrent{},nativeNext{};
    unsigned lastHook=0,lastOutcome=0,lastSelf=0,lastCaller=0;std::uint64_t lastSerial=0;
    unsigned completed=0;Row lastCompleted{};
    bool updateVerified=false;unsigned updateCaller=0;
    std::atomic<std::uint64_t> updateEntries=0,updateContention=0;
    std::optional<Native> observedNative;std::array<UpdateTelemetry,3> updates{};
    std::atomic<bool> soundObservation=false;
    std::atomic<std::uint64_t> soundEntries=0,soundContention=0,soundRejected=0;
    KnifeSoundJournal<> sounds;std::optional<Native> lastCompletedSoundNative;
    std::atomic<bool> timingObservation=false;unsigned stepCaller=0;
    std::array<KnifeTimingRow,4096> timingRows{};unsigned timingUsed=0;
    std::atomic<std::uint64_t> timingContention=0,timingRejected=0,timingFull=0,timingEndLost=0;
    std::array<std::uint64_t,unsigned(OwnerBoundary::Count)> ownerFailures{};
    std::array<OwnerFailure,4> ownerFailureRecords{};unsigned ownerFailureCount=0,ownerFailureDropped=0;
} service;
using AcquireFn=std::uintptr_t(__thiscall*)(void*);
using SearchFn=unsigned(__thiscall*)(void*,unsigned,const void*,unsigned,float);
using AttackFn=std::uintptr_t(__thiscall*)(void*,float,unsigned,void*);
using DamageFn=std::uintptr_t(__thiscall*)(void*,void*,void*);
AcquireFn originalAcquire=nullptr;SearchFn originalSearch=nullptr,originalRetry=nullptr;
AttackFn originalAttack=nullptr;DamageFn originalDamage=nullptr;
std::array<void*,5> hooks{};
struct Active {Active(){service.active.fetch_add(1,std::memory_order_acq_rel);}~Active(){service.active.fetch_sub(1,std::memory_order_acq_rel);}};
std::int64_t Now()noexcept {LARGE_INTEGER q{};if(!service.frequency||!QueryPerformanceCounter(&q))return 0;
    return q.QuadPart/service.frequency*1000000000ll+(q.QuadPart%service.frequency)*1000000000ll/service.frequency;}
bool Current(const Action& a)noexcept {return OwnerCurrent(service.memory,service.server,service.state,service.base,a.native);}
bool Retained(const Action& a)noexcept {return AcceptedOwnerRetained(service.memory,service.server,service.state,service.base,a.native);}
bool ObservedOwner(const Action& a,bool selected,OwnerBoundary boundary,bool gateHeld)noexcept {
    OwnerDiagnostic proof;
    const bool okay=selected?OwnerCurrent(service.memory,service.server,service.state,service.base,a.native,&proof):
        AcceptedOwnerRetained(service.memory,service.server,service.state,service.base,a.native,&proof);
    if(okay)return true;
    const OwnerFailure failure{boundary,a.intent.serial,Now(),proof};
    const auto record=[&]{++service.ownerFailures[unsigned(boundary)];
        if(service.ownerFailureCount<service.ownerFailureRecords.size())service.ownerFailureRecords[service.ownerFailureCount++]=failure;
        else ++service.ownerFailureDropped;};
    if(gateHeld)record();else {std::lock_guard lock(service.gate);record();}
    return false;
}
bool Same(const Action& a)noexcept {return service.action&&service.action->intent.serial==a.intent.serial&&
    service.action->intent.identity==a.intent.identity;}
void Count(Service::HookCounts& counts,HookOutcome outcome)noexcept {
    counts.outcomes[unsigned(outcome)].fetch_add(1,std::memory_order_relaxed);
}
void RawEntry(Service::HookCounts& counts,unsigned self,unsigned caller)noexcept {
    counts.rawLastBoundary.store((std::uint64_t(caller)<<32)|self,std::memory_order_relaxed);
    counts.entries.fetch_add(1,std::memory_order_relaxed);
}
// Called only while holding gate. Raw counters above do not need that lock.
void LastHook(unsigned hook,HookOutcome outcome,unsigned self,unsigned caller)noexcept {
    service.lastHook=hook;service.lastOutcome=unsigned(outcome);service.lastSelf=self;service.lastCaller=caller;
    service.lastSerial=service.action?service.action->intent.serial:0;service.lastHookNs=Now();
}
void Save(const Row& row)noexcept {if(service.count<service.rows.size())service.rows[service.count++]=row;else ++service.dropped;}
void Unknown(unsigned reason)noexcept {
    if(!service.action)return;auto& a=*service.action;
    if(service.policy.PendingState()!=SelectedMeleePending::Unknown){++service.unknown;a.row.failure=reason;Save(a.row);}
    service.policy.Resolve(a.intent.serial,a.intent.identity,SelectedMeleeOutcome::Unknown);
}
void RevokeAdmission()noexcept {
    if(service.action&&service.action->admissionEpoch==service.admissionEpoch.load(std::memory_order_acquire)){
        service.admissionEpoch.fetch_add(1,std::memory_order_acq_rel);
        service.action->row.revokedNs=Now();
        if(service.action->published&&!service.action->acquired)Unknown(10);
    }
}
struct Scope {Action action;Scope* previous=nullptr;};
thread_local Scope* acquiring=nullptr;thread_local Scope* attacking=nullptr;
thread_local KnifeSoundDispatchContext knifeSoundDispatch;
struct ScopeRestore {Scope*& slot;Scope* previous;~ScopeRestore(){slot=previous;}};
bool ShotOffsets(unsigned shot)noexcept {
    std::array<float,3> direction{},position{};detail::Reader r{service.memory};
    return r.Read(std::uint64_t(shot)+0x10,direction.data(),12)&&r.Read(std::uint64_t(shot)+0x20,position.data(),12)&&
        NeutralShotOffsets(direction,position);
}
unsigned Search(SearchFn original,void* self,unsigned soldier,const void* transform,unsigned shot,float scalar,unsigned caller,bool retry)noexcept {
    Active active;const auto passthrough=[&]{return original(self,soldier,transform,shot,scalar);};
    auto* scope=acquiring;
    // Retry's internal searches deliberately keep the native movement-adjusted
    // copy. They are not a second opportunity to substitute the original blade.
    if(!service.enabled.load(std::memory_order_acquire)||!scope||caller!=(retry?0x578573u:0x578552u))return passthrough();
    auto& a=scope->action;++a.row.searches;
    if(!a.geometry)return passthrough(); // Explicit trigger fallback uses stock aiming.
    const auto& v=a.native.links;const auto input=reinterpret_cast<unsigned>(transform);
    if(a.admissionEpoch!=service.admissionEpoch.load(std::memory_order_acquire)){
        a.row.failure=10;return passthrough();}
    if(reinterpret_cast<unsigned>(self)!=v.effects+0x130||
       soldier!=v.soldier||input!=v.effects+0x30||shot!=a.native.client.config.primaryFire+0xa0||
       !ObservedOwner(a,true,OwnerBoundary::SearchCurrent,false)||!ShotOffsets(shot)){a.row.failure=4;return passthrough();}
    alignas(16) std::array<std::byte,64> before{};detail::Reader r{service.memory};
    if(!r.Read(input,before.data(),64)){a.row.failure=5;return passthrough();}
    const auto mapped=AcquisitionCopy(before,a.geometry->nativeAcquisition,a.units);
    if(!mapped){a.row.failure=6;return passthrough();}
    alignas(16) auto copy=*mapped;std::memcpy(&a.row.nativeBefore,before.data(),64);
    a.row.blade=a.geometry->nativeAcquisition;++a.row.copies;
    // Only the caller argument changes. The original effects transform, shared
    // ShotConfig and native range/collision rules are never written or restored.
    if(a.admissionEpoch!=service.admissionEpoch.load(std::memory_order_acquire)){
        --a.row.copies;a.row.failure=10;return passthrough();}
    const auto result=original(self,soldier,copy.data(),shot,scalar);
    std::array<std::byte,64> after{};
    if(!r.Read(input,after.data(),64)||after!=before)a.row.failure=7;
    return result;
}
unsigned __fastcall SearchHook(void* self,void*,unsigned soldier,const void* transform,unsigned shot,float scalar) {
    return Search(originalSearch,self,soldier,transform,shot,scalar,reinterpret_cast<unsigned>(_ReturnAddress()),false);
}
unsigned __fastcall RetryHook(void* self,void*,unsigned soldier,const void* transform,unsigned shot,float scalar) {
    return Search(originalRetry,self,soldier,transform,shot,scalar,reinterpret_cast<unsigned>(_ReturnAddress()),true);
}
std::uintptr_t __fastcall AcquireHook(void* self,void*) {
    Active active;Scope local;bool admitted=false;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    KnifeSoundDispatchScope soundScope(knifeSoundDispatch,service.soundObservation.load(std::memory_order_relaxed));
    RawEntry(service.acquire,reinterpret_cast<unsigned>(self),caller);
    HookOutcome outcome=HookOutcome::Disabled;
    if(service.enabled.load(std::memory_order_acquire)&&IsAcquireCaller(caller)){
        std::lock_guard lock(service.gate);
        outcome=HookOutcome::NoAction;
        if(service.action){auto& a=*service.action;const auto now=Now();
            if(!a.published)outcome=HookOutcome::NotPublished;
            else if(a.acquired)outcome=HookOutcome::AlreadyAcquired;
            else if(reinterpret_cast<unsigned>(self)!=a.native.links.effects+0xd0)outcome=HookOutcome::Self;
            else if(service.policy.PendingState()!=SelectedMeleePending::Staged&&service.policy.PendingState()!=SelectedMeleePending::Unknown)outcome=HookOutcome::Pending;
            else if(now<a.intent.observedNs)outcome=HookOutcome::BeforeSource;
            else if(!ObservedOwner(a,false,OwnerBoundary::AcquireRetained,true))outcome=HookOutcome::Owner;
            else {
                a.acquired=true;a.row.acquiredNs=now;a.row.inputDelivery.Consumed(now);
                admitted=service.policy.PendingState()==SelectedMeleePending::Unknown||
                    service.policy.Resolve(a.intent.serial,a.intent.identity,SelectedMeleeOutcome::Submitted);
                outcome=admitted?HookOutcome::Admitted:HookOutcome::Policy;
                if(admitted){local.action=a;
                    // The input commit is the original admission. Exact native
                    // outcome observation survives interruption, but it cannot
                    // grant another blade mapping after its source was revoked.
                    if(a.admissionEpoch!=service.admissionEpoch.load(std::memory_order_acquire)||!ObservedOwner(a,true,OwnerBoundary::AcquireCurrent,true)){
                        local.action.geometry.reset();local.action.row.failure=10;
                    }
                }
            }
        }
        LastHook(1,outcome,reinterpret_cast<unsigned>(self),caller);
    }else if(service.enabled.load(std::memory_order_acquire))outcome=HookOutcome::Caller;
    Count(service.acquire,outcome);
    if(!admitted)return originalAcquire(self);
    local.previous=acquiring;acquiring=&local;ScopeRestore restore{acquiring,local.previous};
    soundScope.Bind({KnifeSoundDispatchPhase::Acquisition,local.action.intent.serial,local.action.row.input,
        local.action.native.client.owner.actorGeneration,local.action.native.client.owner.equipGeneration,
        local.action.native.client.owner.soldier,local.action.native.client.owner.weapon,local.action.native.links.effects});
    const auto result=originalAcquire(self);
    soundScope.Clear();
    local.action.target=ReadTarget(service.memory,local.action.native.links.effects);local.action.row.acquisitionReturned=true;
    if(local.action.target){local.action.row.handle=local.action.target->handle;local.action.row.target=local.action.target->soldier;}
    std::lock_guard lock(service.gate);
    if(Same(local.action)){
        service.action->row=local.action.row;service.action->target=local.action.target;
        // Preserve the first failing branch before Unknown(8) records the
        // aggregate outcome; never collapse an empty valid target into a gap.
        const unsigned failure=!local.action.target?1:
            !ObservedOwner(local.action,false,OwnerBoundary::AcquireReturned,true)?2:
            (local.action.geometry&&!local.action.row.copies)?3:local.action.row.failure?4:0;
        service.action->row.acquisitionFailure=failure;
        service.action->row.acquisitionPriorFailure=local.action.row.failure;
        if(failure)Unknown(8);
    }
    return result;
}
bool Health(const Target& target,float& health)noexcept {
    detail::Reader r{service.memory};unsigned pointee=0,again=0;
    return target.handle&&target.soldier&&r.Get(target.handle,pointee)&&pointee==target.soldier+4&&
        r.Eq(target.soldier,0x1424850)&&r.Eq(std::uint64_t(target.soldier)+0x2c,0x1424824)&&
        r.Get(std::uint64_t(target.soldier)+0x34,health)&&std::isfinite(health)&&
        r.Get(target.handle,again)&&again==pointee;
}
std::uintptr_t __fastcall DamageHook(void* self,void*,void* damage,void* player) {
    Active active;auto* scope=attacking;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    const bool matched=scope&&caller==0x57af15&&scope->action.target&&scope->action.target->soldier==reinterpret_cast<unsigned>(self)&&
        ReadTarget(service.memory,scope->action.native.links.effects)==scope->action.target&&Retained(scope->action);
    float before=0,after=0;const bool known=matched&&Health(*scope->action.target,before);
    const auto result=originalDamage(self,damage,player);
    if(matched){auto& row=scope->action.row;++row.damageCalls;
        if(known&&row.damageCalls==1&&Health(*scope->action.target,after)&&
           ReadTarget(service.memory,scope->action.native.links.effects)==scope->action.target){
            row.healthKnown=true;row.healthBefore=before;row.healthAfter=after;
        }else row.healthKnown=false;}
    return result;
}
std::uintptr_t __fastcall AttackHook(void* self,void*,float dt,unsigned primary,void* context) {
    Active active;Scope local;bool admitted=false;const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    KnifeSoundDispatchScope soundScope(knifeSoundDispatch,service.soundObservation.load(std::memory_order_relaxed));
    RawEntry(service.attack,reinterpret_cast<unsigned>(self),caller);
    HookOutcome outcome=HookOutcome::Disabled;
    if(service.enabled.load(std::memory_order_acquire)&&caller==0x6dd9e5){
        std::lock_guard lock(service.gate);
        outcome=HookOutcome::NoAction;
        if(service.action){auto& a=*service.action;
            if(!a.acquired)outcome=HookOutcome::NotAcquired;
            else if(!a.row.acquisitionReturned)outcome=HookOutcome::AcquireNotReturned;
            else if(reinterpret_cast<unsigned>(self)!=a.native.links.effects+0xd0)outcome=HookOutcome::Self;
            else if(!ObservedOwner(a,false,OwnerBoundary::AttackRetained,true))outcome=HookOutcome::Owner;
            else if(!a.target)outcome=HookOutcome::TargetUnreadable;
            else if(ReadTarget(service.memory,a.native.links.effects)!=a.target)outcome=HookOutcome::TargetChanged;
            else {
                if(a.attackInFlight){outcome=HookOutcome::Overlap;a.ambiguousAttack=true;Unknown(11);}
                else {a.attackInFlight=true;local.action=a;admitted=true;outcome=HookOutcome::Admitted;}
            }
        }
        LastHook(2,outcome,reinterpret_cast<unsigned>(self),caller);
    }else if(service.enabled.load(std::memory_order_acquire))outcome=HookOutcome::Caller;
    Count(service.attack,outcome);
    if(!admitted)return originalAttack(self,dt,primary,context);
    local.action.row.attackedNs=Now();local.previous=attacking;attacking=&local;ScopeRestore restore{attacking,local.previous};
    soundScope.Bind({KnifeSoundDispatchPhase::Attack,local.action.intent.serial,local.action.row.input,
        local.action.native.client.owner.actorGeneration,local.action.native.client.owner.equipGeneration,
        local.action.native.client.owner.soldier,local.action.native.client.owner.weapon,local.action.native.links.effects});
    const auto result=originalAttack(self,dt,primary,context);local.action.row.attackReturned=true;
    soundScope.Clear();
    std::lock_guard lock(service.gate);
    if(Same(local.action)){
        service.action->attackInFlight=false;
        if(service.action->ambiguousAttack){service.action->row=local.action.row;Unknown(11);return result;}
        // Completion is this exact native callback's return, not idle state,
        // timer expiry, input publication, sound, or a claimed hit.
        if(service.policy.Resolve(local.action.intent.serial,local.action.intent.identity,SelectedMeleeOutcome::Completed)){
            Save(local.action.row);++service.completed;service.lastCompleted=local.action.row;
            if(service.soundObservation.load(std::memory_order_relaxed)||service.timingObservation.load(std::memory_order_relaxed))
                service.lastCompletedSoundNative=local.action.native;
            service.action.reset();
        }else Unknown(9);
    }
    return result;
}
bool Remove()noexcept {
    bool okay=true;
    for(auto* hook:hooks)if(hook){const auto code=MH_DisableHook(hook);okay=(code==MH_OK||code==MH_ERROR_DISABLED)&&okay;}
    const auto began=GetTickCount64();while(service.active.load(std::memory_order_acquire)&&GetTickCount64()-began<3000)Sleep(1);
    if(service.active.load(std::memory_order_acquire))return false;
    if(!okay)return false;
    for(auto& hook:hooks)if(hook){if(MH_RemoveHook(hook)!=MH_OK)okay=false;else hook=nullptr;}
    return okay;
}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t base,const ReloadStateMemory& memory)noexcept {
    service.installFailure=1;
    if(service.installed.load(std::memory_order_acquire)||service.enabled.load()||service.cleanupFailed||
       base!=0x400000||!VerifyCode(memory,unsigned(base),pe.imageSize))return false;
    const auto state=DiscoverReloadState(bytes,pe);
    const auto server=DiscoverReloadServer(bytes,pe);
    LARGE_INTEGER frequency{};
    service.installFailure=2;
    if(!state||!server||!ValidateReloadStateLive(memory,*state,unsigned(base))||
       !ValidateReloadServerLive(memory,*server,unsigned(base))||!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return false;
    service.memory=memory;service.state=*state;service.server=*server;service.base=unsigned(base);service.frequency=frequency.QuadPart;
    // Independent read-only observation capability. Its absence does not alter
    // the existing selected-knife installation or attack admission.
    if(const auto flow=DiscoverReloadFlow(bytes,pe);flow&&ValidateReloadFlowLive(memory,*flow,unsigned(base))){
        service.updateVerified=true;service.updateCaller=unsigned(base)+flow->code[1].rva+0x2f;
        service.stepCaller=unsigned(base)+flow->code[0].rva+0x168;
    }
    const std::array<unsigned,5> addresses{0x578500,0x577790,0x577d40,0x57abd0,0x6c4c00};
    const std::array<void*,5> replacements{reinterpret_cast<void*>(&AcquireHook),reinterpret_cast<void*>(&SearchHook),
        reinterpret_cast<void*>(&RetryHook),reinterpret_cast<void*>(&AttackHook),reinterpret_cast<void*>(&DamageHook)};
    const std::array<void**,5> originals{reinterpret_cast<void**>(&originalAcquire),reinterpret_cast<void**>(&originalSearch),
        reinterpret_cast<void**>(&originalRetry),reinterpret_cast<void**>(&originalAttack),reinterpret_cast<void**>(&originalDamage)};
    for(unsigned n=0;n<hooks.size();++n){auto* at=reinterpret_cast<void*>(addresses[n]);
        if(MH_CreateHook(at,replacements[n],originals[n])!=MH_OK){service.installFailure=10+n;service.cleanupFailed=!Remove();return false;}hooks[n]=at;}
    for(unsigned n=0;n<hooks.size();++n)if(MH_EnableHook(hooks[n])!=MH_OK){service.installFailure=20+n;service.cleanupFailed=!Remove();return false;}
    // Explicit private opt-in only. No new hook, audio routing or gameplay write.
    wchar_t soundFlag[3]{};
    service.soundObservation.store(GetEnvironmentVariableW(L"FVR_BC2_PRIVATE_KNIFE_SOUND_OBSERVE",soundFlag,3)==1&&
        soundFlag[0]==L'1',std::memory_order_release);
    service.sounds={};service.lastCompletedSoundNative.reset();
    service.soundEntries=0;service.soundContention=0;service.soundRejected=0;
    service.timingObservation.store(TimingObservationRequested()&&service.updateVerified,std::memory_order_release);
    if(service.timingObservation.load()){for(auto& row:service.timingRows)row={};service.timingUsed=0;
        service.timingContention=0;service.timingRejected=0;service.timingFull=0;service.timingEndLost=0;}
    service.verified=true;service.installed.store(true,std::memory_order_release);service.installFailure=0;return true;
}
void Start()noexcept {if(service.installed.load(std::memory_order_acquire))service.enabled.store(true,std::memory_order_release);}
bool Stop()noexcept {service.enabled.store(false,std::memory_order_release);service.cleanupFailed=!Remove();
    if(service.cleanupFailed)return false;service.installed.store(false,std::memory_order_release);return true;}
Request Prepare(const SelectedKnifeRenderSource& source,const InputFrame& input,const HandInteraction& hands,
    const SelectedMeshesSnapshot* meshes,const std::optional<SelectedKnifeRenderReceipt>& rendered,
    const std::optional<math::Matrix4>& body,bool available,std::uint64_t inputEpoch)noexcept {
    if(!service.installed.load(std::memory_order_acquire))return {};
    std::lock_guard lock(service.gate);Request result{true,false,0};const auto now=Now();
    auto& diagnostic=service.preparation;++diagnostic.calls;auto& observation=diagnostic.last;observation={};
    observation.observedNs=now;observation.input=input.generation;observation.sourceInput=source.input.sequence;
    observation.inputEpoch=inputEpoch;observation.sourceObservedNs=source.input.observedNs;observation.sourceDeadlineNs=source.input.deadlineNs;
    observation.sourceGameplayOwned=source.gameplayOwned;observation.sourceFocused=source.input.focused;
    observation.sourceTracked=source.input.tracked[1];observation.sourceReleased=source.input.released[1];
    observation.focused=input.focused;observation.headValid=input.headValid;observation.gripTracked=input.hands[1].gripTracked;
    observation.active=input.hands[1].active;observation.menuOwner=unsigned(input.menu.owner);
    observation.triggerKnown=std::isfinite(input.hands[1].trigger);
    if(observation.triggerKnown)observation.trigger=input.hands[1].trigger;
    observation.actionsAvailable=available;if(available)++diagnostic.actionsAvailable;
    const auto finish=[&](PrepareGate gate){observation.gate=unsigned(gate);++diagnostic.gates[unsigned(gate)];return result;};
    const auto unavailable=[&](PrepareGate gate){RevokeAdmission();service.recognitionGap.Reset();service.policy.InvalidateMotion();return finish(gate);};
    // Preserve the original short-circuit order and record only its first failure.
    if(!service.enabled.load(std::memory_order_acquire))return unavailable(PrepareGate::Disabled);
    if(!selected_knife_detail::Source(source,now))return unavailable(PrepareGate::Source);
    if(!ValidInput(input))return unavailable(PrepareGate::Input);
    if(input.generation!=source.input.sequence)return unavailable(PrepareGate::Sequence);
    if(input.spaceGeneration!=source.input.owner.space)return unavailable(PrepareGate::Space);
    if(!GameplayOwnsInput(input.menu,now))return unavailable(PrepareGate::Menu);
    if((input.hands[1].active&Trigger)==0)return unavailable(PrepareGate::TriggerInactive);
    NativeReadDiagnostic readDiagnostic;
    const auto native=ReadNative(service.memory,service.server,service.state,service.base,source.native,&readDiagnostic);
    ++diagnostic.nativeReadFailures[unsigned(readDiagnostic.failure)];
    if(!native){diagnostic.readFailureNs=now;diagnostic.readFailureInput=input.generation;
        diagnostic.lastReadFailure=readDiagnostic.failure;diagnostic.lastReadScope=readDiagnostic.scope;
        diagnostic.lastReadBranch=readDiagnostic.branch;++diagnostic.branchFailures[unsigned(readDiagnostic.branch.failure)];}
    const auto hold=hands.Current(InteractionHand::Right);
    observation.nativeKnown=bool(native);observation.holdKnown=bool(hold);
    observation.nativeReady=native&&Ready(*native);if(observation.nativeReady)++diagnostic.nativeReady;
    if(hold){observation.holdId=hold->token.id;observation.holdItem=hold->token.item;observation.holdKind=unsigned(hold->token.kind);
        observation.holdInput=hold->inputSequence;observation.holdDeadlineNs=hold->deadlineNs;
        observation.holdOwnerMatches=hold->token.owner==source.input.owner;observation.holdSequenceMatches=hold->inputSequence==source.input.sequence;
        observation.holdFresh=hold->deadlineNs>now;observation.holdEvidence=hands.HasRecordedEvidence(source.input,hold->token.hand);}
    if(!hold){++service.readGaps;return unavailable(PrepareGate::HoldMissing);}
    SelectedMeleeSample sample;sample.identity={source.input.owner,hold->token.item,
        {source.native.config.weaponData,source.native.owner.equipGeneration},inputEpoch};
    sample.motion.source=source.input;sample.motion.source.nowNs=now;sample.motion.hold=hold->token;
    sample.motion.attackAvailable=available&&observation.nativeReady;sample.selected=true;sample.trigger=input.hands[1].trigger;
    const auto recordGap=[&](RecognitionGapResult outcome,std::uint64_t anchor,std::int64_t anchorNs){
        ++diagnostic.gapOutcomes[unsigned(outcome)];diagnostic.lastGap=outcome;diagnostic.gapNs=now;
        diagnostic.gapInput=input.generation;diagnostic.gapAnchorInput=anchor;diagnostic.gapAnchorNs=anchorNs;
    };
    if(!native){
        ++service.readGaps;
        const auto anchor=service.recognitionGap.AnchorSequence();const auto anchorNs=service.recognitionGap.AnchorObservedNs();
        const auto gap=service.recognitionGap.Skip(readDiagnostic,sample,hands,available&&!service.action,service.policy.PendingState());
        recordGap(gap,anchor,anchorNs);
        if(gap==RecognitionGapResult::Held||gap==RecognitionGapResult::Repeated)return finish(PrepareGate::RecognitionHeld);
        return unavailable(PrepareGate::NativeRead);
    }
    service.observedNative=*native;
    service.nativeObservedNs=now;
    for(unsigned n=0;n<2;++n){service.nativeCurrent[n]=native->client.branches[n].currentState;service.nativeNext[n]=native->client.branches[n].nextState;}
    service.nativeCurrent[2]=native->server.currentState;service.nativeNext[2]=native->server.nextState;
    if(service.action&&(!PublishedSourceContinues(service.action->intent,source.input,inputEpoch)||
       hold->token!=service.action->intent.hold||!available))RevokeAdmission();
    std::optional<Geometry> geometry;
    unsigned geometryFailure=!rendered?1:!body?2:0;
    if(rendered&&body){const RigMemory memory{service.memory.context,service.memory.read};
        const auto rig=ReadFirstPersonRigIdentity(memory,source.native.owner.soldier,source.native.owner.weak);
        geometryFailure=!rig?3:!SelectedKnifeRenderCurrent(*rendered,source,meshes,*rig,rendered->binding.rigFingerprint,now)?4:
            !ShotOffsets(source.native.config.primaryFire+0xa0)?5:0;
        if(!geometryFailure){geometry=MapGeometry(input,*rendered,*body);if(!geometry)geometryFailure=6;}}
    if(geometry){std::array<std::byte,64> original{};detail::Reader r{service.memory};
        if(!r.Read(std::uint64_t(native->links.effects)+0x30,original.data(),64)||
           !AcquisitionCopy(original,geometry->nativeAcquisition,input.worldUnitsPerMeter)){geometry.reset();geometryFailure=7;}}
    sample.geometryCurrent=bool(geometry);
    observation.geometryCurrent=bool(geometry);observation.geometryFailure=geometryFailure;
    if(geometry)++diagnostic.geometryReady;
    if(geometry){sample.motion.tip=geometry->motion.tip;sample.motion.direction=geometry->motion.direction;}
    else {++service.geometryGaps;++service.geometryFailures[geometryFailure];}
    if(service.recognitionGap.Active()){
        const auto anchor=service.recognitionGap.AnchorSequence();const auto anchorNs=service.recognitionGap.AnchorObservedNs();
        const auto gap=service.recognitionGap.BeforeVerified(*native,sample,hands,available&&!service.action,service.policy.PendingState());
        recordGap(gap,anchor,anchorNs);
        if(gap==RecognitionGapResult::Rejected)service.policy.InvalidateMotion();
    }
    const auto decision=service.policy.Update(sample,hands,&diagnostic.recognition);
    service.recognitionGap.Remember(*native,sample,hands,decision,diagnostic.recognition);
    ++diagnostic.policyCalls;diagnostic.recognitionNs=now;diagnostic.recognitionInput=input.generation;
    diagnostic.lastPolicyReason=unsigned(decision.reason);++diagnostic.policyReasons[unsigned(decision.reason)];
    if(diagnostic.recognition.motionUpdated){
        diagnostic.lastMotion=diagnostic.recognition.motion;diagnostic.motionNs=now;diagnostic.motionInput=input.generation;
        ++diagnostic.strokeDecisions[unsigned(diagnostic.lastMotion.decision)];}
    if(!decision.intent){
        if(decision.reason==SelectedMeleeReason::Invalid||decision.reason==SelectedMeleeReason::Order)
            return unavailable(PrepareGate::PolicyNoIntent);
        if(service.action){auto& a=*service.action;
            InputDeliveryAuthority authority{available,source.gameplayOwned,true,
                a.admissionEpoch==service.admissionEpoch.load(std::memory_order_acquire)};
            // Every original input/hold/menu gate above still applies. Current
            // ownership is checked again, but predicted firing readiness is
            // not a new admission for the same already-published intent.
            if(a.row.inputDelivery.CanRepeat(a.intent,service.policy.PendingState(),decision.reason,source.input,
                hold->token,inputEpoch,authority,now)){
                if(!Current(a))return unavailable(PrepareGate::CurrentOwner);
                if(Now()>=a.intent.deadlineNs)return finish(PrepareGate::IntentExpired);
                result.fire=true;result.serial=a.intent.serial;return finish(PrepareGate::IntentRetained);
            }
        }
        return finish(PrepareGate::PolicyNoIntent);
    }
    Action action;action.intent=*decision.intent;action.native=*native;action.units=input.worldUnitsPerMeter;
    action.admissionEpoch=service.admissionEpoch.load(std::memory_order_acquire);
    // Keep the proven action37 trigger fallback on its stock aiming path. Only
    // a physical blade stroke requests acquisition argument substitution.
    if(action.intent.cause==SelectedMeleeCause::Stroke)action.geometry=geometry;
    if(action.intent.cause==SelectedMeleeCause::Stroke&&!geometry){
        service.policy.Resolve(action.intent.serial,action.intent.identity,SelectedMeleeOutcome::RejectedNoEffect);++service.rejected;return finish(PrepareGate::StrokeGeometry);}
    const bool current=Current(action);
    if(!current||Now()>=action.intent.deadlineNs){
        service.policy.Resolve(action.intent.serial,action.intent.identity,SelectedMeleeOutcome::RejectedNoEffect);++service.rejected;
        return finish(current?PrepareGate::IntentExpired:PrepareGate::CurrentOwner);}
    action.row.serial=action.intent.serial;action.row.input=action.intent.sourceSequence;action.row.cause=unsigned(action.intent.cause);
    action.row.observedNs=action.intent.observedNs;action.row.deadlineNs=action.intent.deadlineNs;
    action.row.effects=action.native.links.effects;action.row.attacker=action.native.links.soldier;action.row.stagedNs=now;
    result.fire=true;result.serial=action.intent.serial;service.action=std::move(action);++service.prepared;return finish(PrepareGate::IntentPrepared);
}
void Published(const Request& request,bool applied)noexcept {
    if(!request.fire||!request.serial)return;std::lock_guard lock(service.gate);
    if(!service.action||service.action->intent.serial!=request.serial)return;
    if(applied){service.action->published=true;service.action->row.inputDelivery.Published(Now());
        service.action->row.publishedNs=service.action->row.inputDelivery.firstPublishedNs;
        if(Now()>=service.action->intent.deadlineNs||!Current(*service.action))RevokeAdmission();}
    // A failed later Gather cannot prove that an earlier committed level had
    // no effect. Retain its unknown outcome and revoke further publications.
    else if(service.action->published)RevokeAdmission();
    else{auto& a=*service.action;service.policy.Resolve(a.intent.serial,a.intent.identity,SelectedMeleeOutcome::RejectedNoEffect);
        service.action.reset();++service.rejected;}
}
void SourceAdvanced(const HandInteractionSample& source,std::uint64_t inputEpoch)noexcept {
    if(!service.installed.load(std::memory_order_acquire))return;std::lock_guard lock(service.gate);
    if(service.action&&!PublishedSourceContinues(service.action->intent,source,inputEpoch))RevokeAdmission();
    if(!service.recognitionGap.SourceContinues(source,inputEpoch)){service.recognitionGap.Reset();service.policy.InvalidateMotion();}
}
Request Missing()noexcept {
    if(!service.installed.load(std::memory_order_acquire))return {};std::lock_guard lock(service.gate);
    ++service.preparation.missingCalls;service.preparation.missingNs=Now();
    ++service.preparation.missingPending[unsigned(service.policy.PendingState())];
    RevokeAdmission();service.recognitionGap.Reset();service.policy.InvalidateMotion();return {true,false,0};
}
void ObserveOwner(const ReloadStateOwner& owner)noexcept {
    if(!service.installed.load(std::memory_order_acquire)||!owner.soldier||!owner.weak||!owner.actorGeneration)return;std::lock_guard lock(service.gate);
    if(!service.recognitionGap.NativeOwnerContinues(owner)){service.recognitionGap.Reset();service.policy.InvalidateMotion();}
    if(service.action){const auto& prior=service.action->native.client.owner;
        if(owner.soldier!=prior.soldier||owner.weak!=prior.weak||owner.player!=prior.player){
            auto& a=*service.action;service.policy.Resolve(a.intent.serial,a.intent.identity,SelectedMeleeOutcome::OwnerRetired);
            a.row.failure=2;Save(a.row);service.action.reset();service.policy.InvalidateMotion();}}
}
void ObserveUpdate(const OriginalFiringUpdate& call)noexcept {
    service.updateEntries.fetch_add(1,std::memory_order_relaxed);
    if(!service.enabled.load(std::memory_order_acquire))return;
    std::unique_lock lock(service.gate,std::try_to_lock);
    if(!lock.owns_lock()){service.updateContention.fetch_add(1,std::memory_order_relaxed);return;}
    if(!service.updateVerified||!service.observedNative)return;
    const auto& native=service.action?service.action->native:*service.observedNative;
    unsigned branch=3;
    for(unsigned n=0;n<2;++n)if(call.firing==native.client.branches[n].address)branch=n;
    if(call.firing==native.links.firing)branch=2;
    if(branch==3)return;
    auto& t=service.updates[branch];++t.calls;t.firing=call.firing;t.caller=call.caller;t.lastNs=Now();
    t.serial=service.action?service.action->intent.serial:0;t.listenerKnown=false;
    if(!AcceptedOwnerRetained(service.memory,service.server,service.state,service.base,native)){++t.ownerRejected;return;}
    ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
    std::array<std::byte,0x30> context{};detail::Reader r{service.memory};
    if(call.caller!=service.updateCaller||call.context<low||(call.context&3)||
       std::uint64_t(call.context)+context.size()>high||!r.Read(call.context,context.data(),context.size())){++t.contextRejected;return;}
    const auto decoded=DecodeReloadUpdateContext(context);if(!decoded){++t.contextRejected;return;}
    std::array<std::byte,0xb0> state{};
    if(!r.Read(call.firing,state.data(),state.size())||
       !r.Eq(call.firing,service.base+service.state.firingVtableRva)||
       !r.Eq(std::uint64_t(call.firing)+8,native.client.config.firingData)||
       !r.Eq(std::uint64_t(call.firing)+12,native.client.config.ammoAddress)){++t.stateRejected;return;}
    std::memcpy(&t.current,state.data()+0x3c,4);std::memcpy(&t.next,state.data()+0x44,4);
    if(t.current>15||t.next>15){++t.stateRejected;return;}
    t.inputFlags=decoded->inputFlags;t.contextFlags=0;
    for(unsigned n=0;n<5;++n)if(decoded->flags24Through28[n])t.contextFlags|=1u<<n;
    if(decoded->fireRequested){++t.fireOne;if(!t.firstFireNs)t.firstFireNs=t.lastNs;t.lastFireNs=t.lastNs;
        t.lastFireSerial=t.serial;t.lastFireContextFlags=t.contextFlags;t.lastFireCurrent=t.current;t.lastFireNext=t.next;
        t.lastFireListenerKnown=false;t.lastFireListenerCount=t.lastFireExpectedListenerMatches=0;
        if(service.action&&service.action->published)++t.fireForPublishedIntent;
    }else ++t.fireZero;
    if(decoded->flags24Through28[0])++t.notificationsOne;else ++t.notificationsZero;
    unsigned begin=0,end=0;std::memcpy(&begin,state.data()+0x18,4);std::memcpy(&end,state.data()+0x1c,4);
    std::array<unsigned,64> listeners{};t.listenerCount=0;t.expectedListenerMatches=0;
    if(end<begin||(end-begin)%4||end-begin>sizeof(listeners)||(end!=begin&&begin<0x10000))return;
    t.listenerCount=(end-begin)/4;
    if(end!=begin&&!r.Read(begin,listeners.data(),end-begin))return;
    if(!r.Eq(std::uint64_t(call.firing)+0x18,begin)||!r.Eq(std::uint64_t(call.firing)+0x1c,end))return;
    if(branch==2)for(unsigned n=0;n<t.listenerCount;++n)if(listeners[n]==native.links.effects+0xd0)++t.expectedListenerMatches;
    t.listenerKnown=true;
    if(decoded->fireRequested){t.lastFireListenerKnown=true;t.lastFireListenerCount=t.listenerCount;
        t.lastFireExpectedListenerMatches=t.expectedListenerMatches;
        if(branch==2&&t.expectedListenerMatches==1){++t.fireWithExpectedListener;
            // This observer runs before the original Update, whose verified
            // notification path invokes acquisition. Stop publishing the level
            // now; only the original callbacks can settle the pending intent.
            if(decoded->flags24Through28[0]&&service.action&&service.action->published)
                service.action->row.inputDelivery.Consumed(t.lastNs);
        }
    }
}
bool SoundObservationEnabled()noexcept {
    return service.soundObservation.load(std::memory_order_relaxed)&&service.enabled.load(std::memory_order_acquire);
}
void ObserveSound(bool start,unsigned receiver,const std::array<unsigned char,16>& guid,unsigned unused)noexcept {
    if(!SoundObservationEnabled())return;
    const auto kind=MatchKnifeSound(guid);if(kind==KnifeSoundKind::Unknown)return;
    Active active;++service.soundEntries;
    std::unique_lock lock(service.gate,std::try_to_lock);
    if(!lock.owns_lock()){++service.soundContention;return;}
    if(!service.enabled.load(std::memory_order_acquire)){++service.soundRejected;return;}
    const Native* source=service.action?&service.action->native:
        service.lastCompletedSoundNative?&*service.lastCompletedSoundNative:
        service.observedNative?&*service.observedNative:nullptr;
    if(!source){++service.soundRejected;return;}
    detail::Reader r{service.memory};unsigned owner=0;unsigned char firstPerson=0,flags=0;
    const bool readable=r.Read(std::uint64_t(receiver)+4,&owner,4)&&
        r.Read(std::uint64_t(receiver)+0x50,&firstPerson,1)&&r.Read(std::uint64_t(owner)+0x475,&flags,1);
    if(!KnifeSoundLocalBoundary(readable,owner,source->client.owner.soldier,firstPerson,flags,true)||
       !AcceptedOwnerRetained(service.memory,service.server,service.state,service.base,*source)){
        ++service.soundRejected;return;}
    KnifeSoundObservation row;row.observedNs=Now();row.receiver=receiver;row.owner=owner;
    row.weapon=source->client.owner.weapon;row.thread=GetCurrentThreadId();row.unused=unused;
    row.kind=kind;row.start=start;row.ownerVerified=true;
    row.originalScope=knifeSoundDispatch;
    row.originalScopeOwnerMatched=KnifeSoundDispatchOwnerMatches(row.originalScope,owner,
        source->client.owner.weapon,source->links.effects,source->client.owner.actorGeneration,
        source->client.owner.equipGeneration);
    const auto add=[&](const Row& action,const Native& native,bool complete){
        // Equip/reference-space changes do not grant a newer action ownership
        // of an older sound. Retain at most two exact-lifetime temporal candidates.
        if(native.client.owner!=source->client.owner||native.client.config!=source->client.config||
           native.links.effects!=source->links.effects||native.links.firing!=source->links.firing)return;
        const KnifeSoundCandidate candidate{action.serial,native.client.owner.actorGeneration,
            native.client.owner.equipGeneration,action.stagedNs,action.publishedNs,action.acquiredNs,
            action.attackedNs,action.damageCalls,complete};
        (void)AddKnifeSoundCandidate(row,candidate);
    };
    if(service.action)add(service.action->row,service.action->native,false);
    if(service.lastCompletedSoundNative)add(service.lastCompleted,*service.lastCompletedSoundNative,true);
    (void)service.sounds.Observe(row);
}
bool TimingObservationRequested()noexcept {
    wchar_t flag[3]{};return GetEnvironmentVariableW(L"FVR_BC2_PRIVATE_KNIFE_TIMING_OBSERVE",flag,3)==1&&flag[0]==L'1';
}
bool TimingObservationEnabled()noexcept {
    return service.timingObservation.load(std::memory_order_relaxed)&&service.enabled.load(std::memory_order_acquire);
}
namespace {
bool TimingState(const KnifeTimingRow& row,const Native& native,bool after,KnifeTimingState& out)noexcept {
    detail::Reader r{service.memory};std::array<std::byte,0xb0> state{};
    if(!r.Read(row.firing,state.data(),state.size())||!r.Eq(row.firing,service.base+service.state.firingVtableRva)||
       !r.Eq(std::uint64_t(row.firing)+8,native.client.config.firingData)||
       !r.Eq(std::uint64_t(row.firing)+12,native.client.config.ammoAddress)||
       !r.Read(std::uint64_t(native.client.config.primaryFire)+0xd0,&out.windupBits,4))return false;
    std::memcpy(&out.current,state.data()+0x3c,4);std::memcpy(&out.next,state.data()+0x44,4);
    std::memcpy(&out.timerBits,state.data()+0x50,4);if(out.current>15||out.next>15)return false;
    if(row.kind==KnifeTimingCall::Update||row.kind==KnifeTimingCall::Step){
        ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);
        if(row.pointer<low||(row.pointer&3)||std::uint64_t(row.pointer)+sizeof(out.context)>high||
           !r.Read(row.pointer,out.context.data(),sizeof(out.context)))return false;
        out.contextKnown=true;
    }else if(row.kind==KnifeTimingCall::Restore||after){
        if(!r.Read(row.pointer,out.snapshot.data(),sizeof(out.snapshot)))return false;
        out.snapshotKnown=true;
    }
    return true;
}
const Native* TimingNative(std::uint64_t serial)noexcept {
    if(service.action&&service.action->intent.serial==serial)return &service.action->native;
    if(service.lastCompletedSoundNative&&service.lastCompleted.serial==serial)return &*service.lastCompletedSoundNative;
    return nullptr;
}
}
std::uint64_t BeginTimingObservation(KnifeTimingCall kind,unsigned firing,unsigned pointer,unsigned caller,
    unsigned argument,float delta,std::uint64_t parentUpdate)noexcept {
    Active entry;
    if(!TimingObservationEnabled())return 0;
    std::unique_lock lock(service.gate,std::try_to_lock);
    if(!lock.owns_lock()){++service.timingContention;return 0;}
    if(!TimingObservationEnabled())return 0;
    // The committed-input/publication race cannot borrow a prior completed
    // intent while a new staged action exists but has no publication receipt.
    if(service.action&&!service.action->published)return 0;
    const Row* action=service.action?&service.action->row:
        service.lastCompletedSoundNative?&service.lastCompleted:nullptr;
    if(!action||action->publishedNs<=0)return 0;
    const auto now=Now();if(now<action->publishedNs||now-action->publishedNs>2000000000)return 0;
    const auto* native=TimingNative(action->serial);if(!native)return 0;
    const std::array<unsigned,3> cohort{native->client.branches[0].address,native->client.branches[1].address,native->links.firing};
    unsigned branch=3;for(unsigned n=0;n<3;++n)if(firing==cohort[n])branch=n;
    if(branch==3)return 0;
    if((kind==KnifeTimingCall::Update&&caller!=service.updateCaller)||
       (kind==KnifeTimingCall::Step&&(caller!=service.stepCaller||!parentUpdate))||
       caller<service.base||std::uint64_t(caller)>=std::uint64_t(service.base)+service.state.imageSize||
       !AcceptedOwnerRetained(service.memory,service.server,service.state,service.base,*native)){
        ++service.timingRejected;return 0;}
    if(service.timingUsed==service.timingRows.size()){++service.timingFull;return 0;}
    KnifeTimingRow row;row.invocation=std::uint64_t(service.timingUsed)+1;row.parentUpdate=parentUpdate;
    row.serial=action->serial;row.input=action->input;row.publishedNs=action->publishedNs;
    row.beginNs=now;row.kind=kind;row.firing=firing;row.pointer=pointer;row.caller=caller;row.argument=argument;
    std::memcpy(&row.deltaBits,&delta,4);row.thread=GetCurrentThreadId();row.branch=branch;row.cohort=cohort;
    const auto& owner=native->client.owner;row.actorGeneration=owner.actorGeneration;row.equipGeneration=owner.equipGeneration;
    row.soldier=owner.soldier;row.weak=owner.weak;row.weapon=owner.weapon;row.effects=native->links.effects;
    row.sourceObservedNs=native->client.observedNs;row.sourceDeadlineNs=action->deadlineNs;
    if(!TimingState(row,*native,false,row.before)||
       !AcceptedOwnerRetained(service.memory,service.server,service.state,service.base,*native)){
        ++service.timingRejected;return 0;}
    if(kind==KnifeTimingCall::Step){
        if(parentUpdate>service.timingUsed){++service.timingRejected;return 0;}
        const auto& parent=service.timingRows[std::size_t(parentUpdate-1)];
        if(parent.kind!=KnifeTimingCall::Update||parent.returned||parent.firing!=firing||parent.serial!=row.serial||
           parent.thread!=row.thread){++service.timingRejected;return 0;}
    }
    service.timingRows[service.timingUsed++]=row;service.active.fetch_add(1,std::memory_order_acq_rel);return row.invocation;
}
void EndTimingObservation(std::uint64_t token,unsigned returnPointer,bool returnPointerKnown)noexcept {
    if(!token)return;
    struct Exit{~Exit(){service.active.fetch_sub(1,std::memory_order_acq_rel);}} exit;
    std::unique_lock lock(service.gate,std::try_to_lock);
    if(!lock.owns_lock()){++service.timingEndLost;return;}
    if(token>service.timingUsed){++service.timingEndLost;return;}
    auto& row=service.timingRows[std::size_t(token-1)];
    if(row.returned||row.thread!=GetCurrentThreadId()){++service.timingEndLost;return;}
    row.returned=true;row.endNs=Now();row.returnPointer=returnPointer;row.returnPointerKnown=returnPointerKnown;
    const auto* native=TimingNative(row.serial);if(!native)return;
    const auto& owner=native->client.owner;
    if(row.soldier!=owner.soldier||row.weak!=owner.weak||row.weapon!=owner.weapon||
       row.actorGeneration!=owner.actorGeneration||row.equipGeneration!=owner.equipGeneration||
       row.effects!=native->links.effects||row.cohort!=std::array<unsigned,3>{native->client.branches[0].address,
           native->client.branches[1].address,native->links.firing})return;
    row.ownerRetained=AcceptedOwnerRetained(service.memory,service.server,service.state,service.base,*native)&&
        TimingState(row,*native,true,row.after)&&AcceptedOwnerRetained(service.memory,service.server,service.state,service.base,*native);
}
Telemetry ReadTelemetry()noexcept {
    Telemetry out;out.available=true;out.installed=service.installed.load(std::memory_order_acquire);
    out.enabled=service.enabled.load(std::memory_order_acquire);
    const auto counters=[](const Service::HookCounts& from,HookTelemetry& to){
        to.entries=from.entries.load(std::memory_order_relaxed);
        const auto boundary=from.rawLastBoundary.load(std::memory_order_relaxed);
        to.rawLastCaller=unsigned(boundary>>32);to.rawLastSelf=unsigned(boundary);
        for(unsigned n=0;n<to.outcomes.size();++n)to.outcomes[n]=from.outcomes[n].load(std::memory_order_relaxed);
    };
    counters(service.acquire,out.acquire);counters(service.attack,out.attack);
    out.updateEntries=service.updateEntries.load(std::memory_order_relaxed);out.updateContention=service.updateContention.load(std::memory_order_relaxed);
    std::unique_lock lock(service.gate,std::try_to_lock);if(!lock.owns_lock())return out;
    out.stateKnown=true;out.sampleNs=Now();out.prepared=service.prepared;out.rejected=service.rejected;
    out.readGaps=service.readGaps;out.geometryGaps=service.geometryGaps;out.unknown=service.unknown;
    out.pending=unsigned(service.policy.PendingState());out.currentEpoch=service.admissionEpoch.load(std::memory_order_acquire);
    out.nativeObservedNs=service.nativeObservedNs;out.nativeCurrent=service.nativeCurrent;out.nativeNext=service.nativeNext;
    out.lastHook=service.lastHook;out.lastOutcome=service.lastOutcome;out.lastSelf=service.lastSelf;out.lastCaller=service.lastCaller;
    out.lastSerial=service.lastSerial;out.lastHookNs=service.lastHookNs;
    out.completed=service.completed;out.lastCompletedSerial=service.lastCompleted.serial;
    out.lastCompletedTarget=service.lastCompleted.target;out.lastCompletedDamageCalls=service.lastCompleted.damageCalls;
    out.lastCompletedHealthKnown=service.lastCompleted.healthKnown;
    out.lastCompletedHealthBefore=service.lastCompleted.healthBefore;out.lastCompletedHealthAfter=service.lastCompleted.healthAfter;
    out.lastAcquisitionFailure=service.lastCompleted.acquisitionFailure;
    out.lastAcquisitionPriorFailure=service.lastCompleted.acquisitionPriorFailure;
    out.updateVerified=service.updateVerified;out.updates=service.updates;
    out.ownerFailures=service.ownerFailures;out.ownerFailureRecords=service.ownerFailureRecords;
    out.ownerFailureCount=service.ownerFailureCount;out.ownerFailureDropped=service.ownerFailureDropped;
    out.geometryFailures=service.geometryFailures;out.preparation=service.preparation;
    if(service.action){const auto& a=*service.action;const auto& r=a.row;
        out.serial=a.intent.serial;out.input=a.intent.sourceSequence;out.admissionEpoch=a.admissionEpoch;
        out.published=a.published;out.acquired=a.acquired;out.geometry=bool(a.geometry);
        out.effects=a.native.links.effects;out.serverFiring=a.native.links.firing;out.attacker=a.native.links.soldier;
        out.target=r.target;out.searches=r.searches;out.copies=r.copies;out.damageCalls=r.damageCalls;out.failure=r.failure;
        out.acquisitionFailure=r.acquisitionFailure;out.acquisitionPriorFailure=r.acquisitionPriorFailure;
        out.observedNs=r.observedNs;out.deadlineNs=r.deadlineNs;out.publishedNs=r.publishedNs;out.revokedNs=r.revokedNs;
        out.acquiredNs=r.acquiredNs;out.attackedNs=r.attackedNs;
        out.inputDelivery=r.inputDelivery;
        out.acquisitionReturned=r.acquisitionReturned;out.attackReturned=r.attackReturned;
    }
    return out;
}
void Report(std::ostream& out) {
    const auto telemetry=ReadTelemetry();
    std::lock_guard lock(service.gate);const auto precision=out.precision();out.precision(9);
    out<<"{\"installed\":"<<service.installed.load(std::memory_order_acquire)<<",\"verified\":"<<service.verified
       <<",\"install_failure\":"<<service.installFailure<<",\"cleanup_failed\":"<<service.cleanupFailed
       <<",\"prepared\":"<<service.prepared<<",\"rejected\":"<<service.rejected
       <<",\"read_gaps\":"<<service.readGaps<<",\"geometry_gaps\":"<<service.geometryGaps<<",\"unknown\":"<<service.unknown
       <<",\"pending\":"<<unsigned(service.policy.PendingState())<<",\"dropped\":"<<service.dropped<<",\"geometry_failures\":[";
    for(unsigned n=0;n<service.geometryFailures.size();++n){if(n)out<<',';out<<service.geometryFailures[n];}out<<"],\"records\":[";
    const auto matrix=[&](const math::Matrix4& m){out<<'[';for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c){if(r||c)out<<',';out<<m.values[r][c];}out<<']';};
    for(unsigned n=0;n<service.count;++n){if(n)out<<',';const auto& r=service.rows[n];
        out<<"{\"serial\":"<<r.serial<<",\"input\":"<<r.input<<",\"cause\":"<<r.cause<<",\"effects\":"<<r.effects
           <<",\"attacker\":"<<r.attacker<<",\"weak_handle\":"<<r.handle<<",\"target\":"<<r.target
           <<",\"observed_ns\":"<<r.observedNs<<",\"deadline_ns\":"<<r.deadlineNs
           <<",\"staged_ns\":"<<r.stagedNs<<",\"published_ns\":"<<r.publishedNs<<",\"revoked_ns\":"<<r.revokedNs
           <<",\"acquired_ns\":"<<r.acquiredNs<<",\"attacked_ns\":"<<r.attackedNs
           <<",\"input_publications\":"<<r.inputDelivery.publications
           <<",\"last_input_publication_ns\":"<<r.inputDelivery.lastPublishedNs<<",\"input_consumed_ns\":"<<r.inputDelivery.consumedNs
           <<",\"searches\":"<<r.searches<<",\"copies\":"<<r.copies<<",\"failure\":"<<r.failure
           <<",\"acquisition_failure\":"<<r.acquisitionFailure<<",\"acquisition_prior_failure\":"<<r.acquisitionPriorFailure
           <<",\"acquisition_returned\":"<<r.acquisitionReturned<<",\"attack_returned\":"<<r.attackReturned
           <<",\"damage_calls\":"<<r.damageCalls<<",\"health_known\":"<<r.healthKnown
           <<",\"health_before\":"<<r.healthBefore<<",\"health_after\":"<<r.healthAfter<<",\"native_before\":";
        matrix(r.nativeBefore);out<<",\"blade\":";matrix(r.blade);out<<'}';}
    out<<"],\"sound_observation\":{\"enabled\":"<<service.soundObservation.load()
       <<",\"native_timing_modified\":false,\"sounds_suppressed\":0,\"correlation_is_hit_proof\":false"
       <<",\"dispatch_context_is_causality_proof\":false,\"observation_is_audibility_proof\":false"
       <<",\"entries\":"<<service.soundEntries.load()<<",\"contention\":"<<service.soundContention.load()
       <<",\"rejected\":"<<service.soundRejected.load()<<",\"total\":"<<service.sounds.Total()<<",\"rows\":[";
    bool soundComma=false;for(const auto& row:service.sounds.Rows()){if(!row.ordinal)continue;
        if(soundComma)out<<',';soundComma=true;
        out<<"{\"ordinal\":"<<row.ordinal<<",\"observed_ns\":"<<row.observedNs<<",\"receiver\":"<<row.receiver
           <<",\"owner\":"<<row.owner<<",\"weapon\":"<<row.weapon<<",\"thread\":"<<row.thread
           <<",\"unused\":"<<row.unused<<",\"kind\":"<<unsigned(row.kind)<<",\"start\":"<<row.start
           <<",\"owner_verified\":"<<row.ownerVerified<<",\"original_scope\":{\"phase\":"<<unsigned(row.originalScope.phase)
           <<",\"serial\":"<<row.originalScope.serial<<",\"input\":"<<row.originalScope.input
           <<",\"actor_generation\":"<<row.originalScope.actorGeneration<<",\"item_generation\":"<<row.originalScope.itemGeneration
           <<",\"owner\":"<<row.originalScope.owner<<",\"weapon\":"<<row.originalScope.weapon<<",\"effects\":"<<row.originalScope.effects
           <<",\"owner_matched\":"<<row.originalScopeOwnerMatched<<"},\"candidate_count\":"<<row.candidateCount<<",\"candidates\":[";
        for(unsigned n=0;n<row.candidateCount;++n){if(n)out<<',';const auto& c=row.candidates[n];
            out<<"{\"serial\":"<<c.serial<<",\"actor_generation\":"<<c.actorGeneration<<",\"item_generation\":"<<c.itemGeneration
               <<",\"staged_ns\":"<<c.stagedNs<<",\"published_ns\":"<<c.publishedNs<<",\"acquired_ns\":"<<c.acquiredNs
               <<",\"attacked_ns\":"<<c.attackedNs<<",\"completed\":"<<c.completed<<",\"observed_damage_calls\":"<<c.damageCalls<<'}';}
        out<<"]}";
    }
    out<<"]},\"timing_observation\":{\"enabled\":"<<service.timingObservation.load()
       <<",\"native_timing_modified\":false,\"maximum_rows\":4096,\"maximum_intent_age_ns\":2000000000"
       <<",\"contention\":"<<service.timingContention.load()<<",\"rejected\":"<<service.timingRejected.load()
       <<",\"capacity_drops\":"<<service.timingFull.load()<<",\"end_drops\":"<<service.timingEndLost.load()<<",\"rows\":[";
    const auto timingState=[&](const KnifeTimingState& s){
        out<<"{\"current\":"<<s.current<<",\"next\":"<<s.next<<",\"timer_bits\":"<<s.timerBits
           <<",\"windup_bits\":"<<s.windupBits<<",\"context_known\":"<<s.contextKnown
           <<",\"snapshot_known\":"<<s.snapshotKnown<<",\"context\":[";
        for(unsigned n=0;n<s.context.size();++n){if(n)out<<',';out<<s.context[n];}out<<"],\"snapshot\":[";
        for(unsigned n=0;n<s.snapshot.size();++n){if(n)out<<',';out<<s.snapshot[n];}out<<"]}";
    };
    for(unsigned n=0;n<service.timingUsed;++n){if(n)out<<',';const auto& row=service.timingRows[n];
        out<<"{\"invocation\":"<<row.invocation<<",\"parent_update\":"<<row.parentUpdate<<",\"serial\":"<<row.serial
           <<",\"input\":"<<row.input<<",\"actor_generation\":"<<row.actorGeneration<<",\"equip_generation\":"<<row.equipGeneration
           <<",\"begin_ns\":"<<row.beginNs<<",\"end_ns\":"<<row.endNs<<",\"published_ns\":"<<row.publishedNs
           <<",\"source_observed_ns\":"<<row.sourceObservedNs<<",\"source_deadline_ns\":"<<row.sourceDeadlineNs
           <<",\"kind\":"<<unsigned(row.kind)<<",\"firing\":"<<row.firing<<",\"pointer\":"<<row.pointer
           <<",\"caller\":"<<row.caller<<",\"argument\":"<<row.argument<<",\"delta_bits\":"<<row.deltaBits
           <<",\"thread\":"<<row.thread<<",\"branch\":"<<row.branch<<",\"soldier\":"<<row.soldier
           <<",\"weak\":"<<row.weak<<",\"weapon\":"<<row.weapon<<",\"effects\":"<<row.effects<<",\"cohort\":["
           <<row.cohort[0]<<','<<row.cohort[1]<<','<<row.cohort[2]<<"],\"returned\":"<<row.returned
           <<",\"owner_retained\":"<<row.ownerRetained<<",\"return_pointer_known\":"<<row.returnPointerKnown
           <<",\"return_pointer\":"<<row.returnPointer<<",\"before\":";timingState(row.before);
        out<<",\"after\":";timingState(row.after);out<<'}';
    }
    out<<"]},\"telemetry\":";WriteTelemetry(out,telemetry);out<<'}';out.precision(precision);
}
}
#else
namespace fvr::bc2::selectedKnife {
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,const ReloadStateMemory&)noexcept{return false;}
void Start()noexcept{}bool Stop()noexcept{return true;}
Request Prepare(const SelectedKnifeRenderSource&,const interaction::InputFrame&,const interaction::HandInteraction&,
    const SelectedMeshesSnapshot*,const std::optional<SelectedKnifeRenderReceipt>&,const std::optional<math::Matrix4>&,bool,std::uint64_t)noexcept{return {};}
void Published(const Request&,bool)noexcept{}void SourceAdvanced(const interaction::HandInteractionSample&,std::uint64_t)noexcept{}
Request Missing()noexcept{return {};}void ObserveOwner(const ReloadStateOwner&)noexcept{}
Telemetry ReadTelemetry()noexcept{return {};}
void ObserveUpdate(const OriginalFiringUpdate&)noexcept{}
bool SoundObservationEnabled()noexcept{return false;}
void ObserveSound(bool,unsigned,const std::array<unsigned char,16>&,unsigned)noexcept{}
bool TimingObservationRequested()noexcept{return false;}
bool TimingObservationEnabled()noexcept{return false;}
std::uint64_t BeginTimingObservation(KnifeTimingCall,unsigned,unsigned,unsigned,unsigned,float,std::uint64_t)noexcept{return 0;}
void EndTimingObservation(std::uint64_t,unsigned,bool)noexcept{}
void Report(std::ostream& out){out<<"{\"available\":false}";}
}
#endif

namespace fvr::bc2::selectedKnife {
void WriteTelemetry(std::ostream& out,const Telemetry& t) {
    out<<"{\"available\":"<<t.available<<",\"installed\":"<<t.installed<<",\"enabled\":"<<t.enabled<<",\"state_known\":"<<t.stateKnown;
    const auto hook=[&](const char* name,const HookTelemetry& h){out<<",\""<<name<<"\":{\"entries\":"<<h.entries
        <<",\"raw_last_caller\":"<<h.rawLastCaller<<",\"raw_last_self\":"<<h.rawLastSelf;
        for(unsigned n=0;n<h.outcomes.size();++n)out<<",\""<<HookOutcomeNames[n]<<"\":"<<h.outcomes[n];out<<'}';};
    hook("acquire",t.acquire);hook("attack",t.attack);
    out<<",\"update_entries\":"<<t.updateEntries<<",\"update_contention\":"<<t.updateContention;
    if(t.stateKnown){
        out<<",\"geometry_failures\":[";
        for(unsigned n=0;n<t.geometryFailures.size();++n){if(n)out<<',';out<<t.geometryFailures[n];}out<<']';
        const auto& p=t.preparation;const auto& last=p.last;const auto& recognition=p.recognition;const auto& motion=p.lastMotion;
        const auto counts=[&](const char* name,const auto& names,const auto& values){
            out<<",\""<<name<<"\":{";for(unsigned n=0;n<values.size();++n){if(n)out<<',';out<<'\"'<<names[n]<<"\":"<<values[n];}out<<'}';};
        // Invalid input/motion is diagnosable without emitting non-JSON NaN/Inf.
        const auto scalar=[&](float value){if(std::isfinite(value))out<<value;else out<<"null";};
        out<<",\"preparation\":{\"calls\":"<<p.calls<<",\"missing_calls\":"<<p.missingCalls<<",\"missing_ns\":"<<p.missingNs
           <<",\"missing_pending\":["<<p.missingPending[0]<<','<<p.missingPending[1]<<','<<p.missingPending[2]<<','<<p.missingPending[3]<<']'
           <<",\"policy_calls\":"<<p.policyCalls<<",\"actions_available\":"<<p.actionsAvailable
           <<",\"native_ready\":"<<p.nativeReady<<",\"geometry_ready\":"<<p.geometryReady;
        counts("gates",PrepareGateNames,p.gates);counts("policy_reasons",PolicyReasonNames,p.policyReasons);
        counts("stroke_decisions",StrokeDecisionNames,p.strokeDecisions);
        out<<",\"last\":{\"observed_ns\":"<<last.observedNs<<",\"input\":"<<last.input<<",\"source_input\":"<<last.sourceInput
           <<",\"input_epoch\":"<<last.inputEpoch<<",\"source_observed_ns\":"<<last.sourceObservedNs<<",\"source_deadline_ns\":"<<last.sourceDeadlineNs
           <<",\"gate\":"<<last.gate<<",\"active\":"<<last.active<<",\"menu_owner\":"<<last.menuOwner
           <<",\"trigger_known\":"<<last.triggerKnown<<",\"trigger\":";scalar(last.trigger);
        out<<",\"source_gameplay_owned\":"<<last.sourceGameplayOwned<<",\"source_focused\":"<<last.sourceFocused
           <<",\"source_tracked\":"<<last.sourceTracked<<",\"source_released\":"<<last.sourceReleased
           <<",\"focused\":"<<last.focused<<",\"head_valid\":"<<last.headValid<<",\"grip_tracked\":"<<last.gripTracked
           <<",\"actions_available\":"<<last.actionsAvailable<<",\"native_known\":"<<last.nativeKnown<<",\"native_ready\":"<<last.nativeReady
           <<",\"geometry_current\":"<<last.geometryCurrent<<",\"geometry_failure\":"<<last.geometryFailure
           <<",\"hold_known\":"<<last.holdKnown<<",\"hold_id\":"<<last.holdId<<",\"hold_kind\":"<<last.holdKind
           <<",\"hold_item\":"<<last.holdItem.id<<",\"hold_item_generation\":"<<last.holdItem.generation
           <<",\"hold_input\":"<<last.holdInput<<",\"hold_deadline_ns\":"<<last.holdDeadlineNs
           <<",\"hold_owner_matches\":"<<last.holdOwnerMatches<<",\"hold_sequence_matches\":"<<last.holdSequenceMatches
           <<",\"hold_fresh\":"<<last.holdFresh<<",\"hold_evidence\":"<<last.holdEvidence<<'}';
        // Last recognition carries its own source/time; a later early Prepare
        // rejection or Missing call cannot make these retained metrics current.
        out<<",\"recognition\":{\"observed_ns\":"<<p.recognitionNs<<",\"input\":"<<p.recognitionInput<<",\"policy_reason\":"<<p.lastPolicyReason
           <<",\"motion_updated\":"<<recognition.motionUpdated<<",\"armed_before\":"<<recognition.armedBefore<<",\"armed_after\":"<<recognition.armedAfter
           <<",\"trigger_down_before\":"<<recognition.triggerDownBefore<<",\"trigger_down_after\":"<<recognition.triggerDownAfter
           <<",\"pending_before\":"<<unsigned(recognition.pendingBefore)<<",\"pending_after\":"<<unsigned(recognition.pendingAfter)<<'}'
           <<",\"last_motion\":{\"observed_ns\":"<<p.motionNs<<",\"input\":"<<p.motionInput
           <<",\"stroke_decision\":"<<unsigned(motion.decision)<<",\"stroke_before\":"<<unsigned(motion.before)<<",\"stroke_after\":"<<unsigned(motion.after)
           <<",\"previous_input\":"<<motion.previousSequence<<",\"previous_observed_ns\":"<<motion.previousObservedNs
           <<",\"elapsed_ns\":"<<motion.elapsedNs<<",\"quiet_ns\":"<<motion.quietNs<<",\"stroke_ns\":"<<motion.strokeNs
           <<",\"metrics\":"<<motion.metrics<<",\"invalid\":"<<motion.invalid<<",\"distance\":";scalar(motion.distance);
        out<<",\"speed\":";scalar(motion.speed);out<<",\"alignment\":";scalar(motion.alignment);out<<",\"travel\":";scalar(motion.travel);
        out<<"},\"native_read\":{\"failure_ns\":"<<p.readFailureNs<<",\"input\":"<<p.readFailureInput
           <<",\"reason\":"<<unsigned(p.lastReadFailure)<<",\"scope\":\""<<p.lastReadScope.stage
           <<"\",\"scope_address\":"<<p.lastReadScope.address<<",\"scope_expected\":"<<p.lastReadScope.expected
           <<",\"scope_observed\":"<<p.lastReadScope.observed<<",\"scope_read_failed\":"<<p.lastReadScope.readFailed
           <<",\"scope_type_failed\":"<<p.lastReadScope.typeFailed<<",\"branch_reason\":"<<unsigned(p.lastReadBranch.failure)
           <<",\"firing\":"<<p.lastReadBranch.firing<<",\"difference_offset\":"<<p.lastReadBranch.differenceOffset
           <<",\"before\":"<<p.lastReadBranch.before<<",\"after\":"<<p.lastReadBranch.after
           <<",\"ready_pair\":"<<p.lastReadBranch.readyPair<<",\"reasons\":[";
        for(unsigned n=0;n<p.nativeReadFailures.size();++n){if(n)out<<',';out<<p.nativeReadFailures[n];}
        out<<"],\"branch_reasons\":[";
        for(unsigned n=0;n<p.branchFailures.size();++n){if(n)out<<',';out<<p.branchFailures[n];}
        out<<"]},\"recognition_gap\":{\"observed_ns\":"<<p.gapNs<<",\"input\":"<<p.gapInput
           <<",\"anchor_input\":"<<p.gapAnchorInput<<",\"anchor_observed_ns\":"<<p.gapAnchorNs
           <<",\"result\":"<<unsigned(p.lastGap)<<",\"outcomes\":[";
        for(unsigned n=0;n<p.gapOutcomes.size();++n){if(n)out<<',';out<<p.gapOutcomes[n];}
        out<<"]}}";
        const auto differences=[&](const ReloadServerLinkDifference& d){
            out<<"{\"count\":"<<d.count<<",\"first\":[";
            for(unsigned n=0;n<d.count&&n<d.first.size();++n){if(n)out<<',';const auto& c=d.first[n];
                out<<"{\"field\":\""<<c.field<<"\",\"index\":"<<c.index<<",\"expected\":"<<c.expected<<",\"observed\":"<<c.observed<<'}';}
            out<<"]}";};
        out<<",\"owner_failures\":{";
        for(unsigned n=0;n<t.ownerFailures.size();++n){if(n)out<<',';out<<'\"'<<OwnerBoundaryNames[n]<<"\":"<<t.ownerFailures[n];}
        out<<"},\"owner_failure_dropped\":"<<t.ownerFailureDropped<<",\"owner_failure_records\":[";
        for(unsigned n=0;n<t.ownerFailureCount&&n<t.ownerFailureRecords.size();++n){if(n)out<<',';
            const auto& f=t.ownerFailureRecords[n];const auto& d=f.proof;const auto& s=d.scope;
            out<<"{\"boundary\":\""<<OwnerBoundaryNames[unsigned(f.boundary)]<<"\",\"serial\":"<<f.serial<<",\"observed_ns\":"<<f.observedNs
               <<",\"phase\":"<<unsigned(d.phase)<<",\"stage\":\""<<s.stage<<"\",\"address\":"<<s.address<<",\"bytes\":"<<s.bytes
               <<",\"expected\":"<<s.expected<<",\"observed\":"<<s.observed<<",\"read_failed\":"<<s.readFailed<<",\"type_failed\":"<<s.typeFailed
               <<",\"before_known\":"<<d.beforeKnown<<",\"after_known\":"<<d.afterKnown
               <<",\"effects_source_before_after\":["<<d.sourceEffects<<','<<d.beforeEffects<<','<<d.afterEffects<<']'
               <<",\"player_flags_source_before_after\":["<<d.sourcePlayerFlags<<','<<d.beforePlayerFlags<<','<<d.afterPlayerFlags<<']'
               <<",\"soldier_flags_source_before_after\":["<<d.sourceSoldierFlags<<','<<d.beforeSoldierFlags<<','<<d.afterSoldierFlags<<']'
               <<",\"inventory_source_before_after\":["<<d.sourceInventory<<','<<d.beforeInventory<<','<<d.afterInventory<<']'
               <<",\"later_inventory248\":"<<d.inventory248<<",\"later_inventory24c\":"<<d.inventory24c<<",\"later_inventory_read_mask\":"<<d.inventoryReadMask
               <<",\"bracket_difference\":";differences(d.bracketDifference);
            out<<",\"source_difference\":";differences(d.sourceDifference);
            out<<",\"normalized_difference\":";differences(d.normalizedDifference);out<<'}';
        }out<<']';
        out<<",\"sample_ns\":"<<t.sampleNs<<",\"prepared\":"<<t.prepared<<",\"rejected\":"<<t.rejected
           <<",\"read_gaps\":"<<t.readGaps<<",\"geometry_gaps\":"<<t.geometryGaps<<",\"unknown\":"<<t.unknown<<",\"pending\":"<<t.pending
           <<",\"native_observed_ns\":"<<t.nativeObservedNs<<",\"native_current\":["<<t.nativeCurrent[0]<<','<<t.nativeCurrent[1]<<','<<t.nativeCurrent[2]
           <<"],\"native_next\":["<<t.nativeNext[0]<<','<<t.nativeNext[1]<<','<<t.nativeNext[2]<<']'
           <<",\"serial\":"<<t.serial<<",\"input\":"<<t.input<<",\"published\":"<<t.published<<",\"acquired\":"<<t.acquired<<",\"geometry\":"<<t.geometry
           <<",\"admission_epoch\":"<<t.admissionEpoch<<",\"current_epoch\":"<<t.currentEpoch
           <<",\"effects\":"<<t.effects<<",\"server_firing\":"<<t.serverFiring<<",\"attacker\":"<<t.attacker<<",\"target\":"<<t.target
           <<",\"searches\":"<<t.searches<<",\"copies\":"<<t.copies<<",\"damage_calls\":"<<t.damageCalls<<",\"failure\":"<<t.failure
           <<",\"acquisition_failure\":"<<t.acquisitionFailure<<",\"acquisition_prior_failure\":"<<t.acquisitionPriorFailure
           <<",\"observed_ns\":"<<t.observedNs<<",\"deadline_ns\":"<<t.deadlineNs<<",\"published_ns\":"<<t.publishedNs<<",\"revoked_ns\":"<<t.revokedNs
           <<",\"acquired_ns\":"<<t.acquiredNs<<",\"attacked_ns\":"<<t.attackedNs
           <<",\"input_publications\":"<<t.inputDelivery.publications
           <<",\"last_input_publication_ns\":"<<t.inputDelivery.lastPublishedNs<<",\"input_consumed_ns\":"<<t.inputDelivery.consumedNs
           <<",\"acquisition_returned\":"<<t.acquisitionReturned<<",\"attack_returned\":"<<t.attackReturned
           <<",\"last_hook\":"<<t.lastHook<<",\"last_outcome\":"<<t.lastOutcome<<",\"last_self\":"<<t.lastSelf<<",\"last_caller\":"<<t.lastCaller
           <<",\"last_serial\":"<<t.lastSerial<<",\"last_hook_ns\":"<<t.lastHookNs
           <<",\"completed\":"<<t.completed<<",\"last_completed_serial\":"<<t.lastCompletedSerial
           <<",\"last_completed_target\":"<<t.lastCompletedTarget<<",\"last_completed_damage_calls\":"<<t.lastCompletedDamageCalls
           <<",\"last_acquisition_failure\":"<<t.lastAcquisitionFailure<<",\"last_acquisition_prior_failure\":"<<t.lastAcquisitionPriorFailure
           <<",\"last_completed_health_known\":"<<t.lastCompletedHealthKnown
           <<",\"last_completed_health_before\":"<<t.lastCompletedHealthBefore<<",\"last_completed_health_after\":"<<t.lastCompletedHealthAfter;
        out<<",\"update_verified\":"<<t.updateVerified<<",\"updates\":[";
        for(unsigned n=0;n<t.updates.size();++n){if(n)out<<',';const auto& u=t.updates[n];
            out<<"{\"calls\":"<<u.calls<<",\"owner_rejected\":"<<u.ownerRejected<<",\"context_rejected\":"<<u.contextRejected<<",\"state_rejected\":"<<u.stateRejected
               <<",\"fire_zero\":"<<u.fireZero<<",\"fire_one\":"<<u.fireOne<<",\"notifications_zero\":"<<u.notificationsZero<<",\"notifications_one\":"<<u.notificationsOne
               <<",\"serial\":"<<u.serial<<",\"last_ns\":"<<u.lastNs<<",\"first_fire_ns\":"<<u.firstFireNs<<",\"last_fire_ns\":"<<u.lastFireNs
               <<",\"last_fire_serial\":"<<u.lastFireSerial<<",\"fire_for_published_intent\":"<<u.fireForPublishedIntent
               <<",\"last_fire_context_flags\":"<<u.lastFireContextFlags<<",\"last_fire_current\":"<<u.lastFireCurrent<<",\"last_fire_next\":"<<u.lastFireNext
               <<",\"last_fire_listener_known\":"<<u.lastFireListenerKnown<<",\"last_fire_listener_count\":"<<u.lastFireListenerCount
               <<",\"last_fire_expected_listener_matches\":"<<u.lastFireExpectedListenerMatches<<",\"fire_with_expected_listener\":"<<u.fireWithExpectedListener
               <<",\"firing\":"<<u.firing<<",\"caller\":"<<u.caller<<",\"input_flags\":"<<u.inputFlags<<",\"context_flags\":"<<u.contextFlags
               <<",\"current\":"<<u.current<<",\"next\":"<<u.next<<",\"listener_known\":"<<u.listenerKnown
               <<",\"listener_count\":"<<u.listenerCount<<",\"expected_listener_matches\":"<<u.expectedListenerMatches<<'}';
        }out<<']';
    }
    out<<'}';
}
}
