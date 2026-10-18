#include "Bc2SelectedKnifeNative.h"
#include "Bc2SelectedKnifeCode.h"
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
    std::int64_t observedNs=0,deadlineNs=0,stagedNs=0,acquiredNs=0,attackedNs=0;
    unsigned searches=0,copies=0,damageCalls=0,failure=0;
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
    std::optional<Action> action;
    std::array<Row,32> rows{};unsigned count=0,dropped=0,prepared=0,rejected=0,readGaps=0,geometryGaps=0,unknown=0;
    std::array<unsigned,8> geometryFailures{};
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
bool Same(const Action& a)noexcept {return service.action&&service.action->intent.serial==a.intent.serial&&
    service.action->intent.identity==a.intent.identity;}
void Save(const Row& row)noexcept {if(service.count<service.rows.size())service.rows[service.count++]=row;else ++service.dropped;}
void Unknown(unsigned reason)noexcept {
    if(!service.action)return;auto& a=*service.action;
    if(service.policy.PendingState()!=SelectedMeleePending::Unknown){++service.unknown;a.row.failure=reason;Save(a.row);}
    service.policy.Resolve(a.intent.serial,a.intent.identity,SelectedMeleeOutcome::Unknown);
}
void RevokeAdmission()noexcept {
    if(service.action&&service.action->admissionEpoch==service.admissionEpoch.load(std::memory_order_acquire)){
        service.admissionEpoch.fetch_add(1,std::memory_order_acq_rel);
        if(service.action->published&&!service.action->acquired)Unknown(10);
    }
}
struct Scope {Action action;Scope* previous=nullptr;};
thread_local Scope* acquiring=nullptr;thread_local Scope* attacking=nullptr;
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
       !Current(a)||!ShotOffsets(shot)){a.row.failure=4;return passthrough();}
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
    if(service.enabled.load(std::memory_order_acquire)&&caller==0x6dd770){
        std::lock_guard lock(service.gate);
        if(service.action){auto& a=*service.action;const auto now=Now();
            if(a.published&&!a.acquired&&reinterpret_cast<unsigned>(self)==a.native.links.effects+0xd0&&
               (service.policy.PendingState()==SelectedMeleePending::Staged||service.policy.PendingState()==SelectedMeleePending::Unknown)&&
               now>=a.intent.observedNs&&Retained(a)){
                a.acquired=true;a.row.acquiredNs=now;
                admitted=service.policy.PendingState()==SelectedMeleePending::Unknown||
                    service.policy.Resolve(a.intent.serial,a.intent.identity,SelectedMeleeOutcome::Submitted);
                if(admitted){local.action=a;
                    // The input commit is the original admission. Exact native
                    // outcome observation survives interruption, but it cannot
                    // grant another blade mapping after its source was revoked.
                    if(a.admissionEpoch!=service.admissionEpoch.load(std::memory_order_acquire)||!Current(a)){
                        local.action.geometry.reset();local.action.row.failure=10;
                    }
                }
            }
        }
    }
    if(!admitted)return originalAcquire(self);
    local.previous=acquiring;acquiring=&local;ScopeRestore restore{acquiring,local.previous};
    const auto result=originalAcquire(self);
    local.action.target=ReadTarget(service.memory,local.action.native.links.effects);local.action.row.acquisitionReturned=true;
    if(local.action.target){local.action.row.handle=local.action.target->handle;local.action.row.target=local.action.target->soldier;}
    std::lock_guard lock(service.gate);
    if(Same(local.action)){
        service.action->row=local.action.row;service.action->target=local.action.target;
        if(!local.action.target||!Retained(local.action)||(local.action.geometry&&!local.action.row.copies)||local.action.row.failure)Unknown(8);
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
    if(service.enabled.load(std::memory_order_acquire)&&caller==0x6dd9e5){
        std::lock_guard lock(service.gate);
        if(service.action){auto& a=*service.action;
            if(a.acquired&&a.row.acquisitionReturned&&reinterpret_cast<unsigned>(self)==a.native.links.effects+0xd0&&
               Retained(a)&&a.target&&ReadTarget(service.memory,a.native.links.effects)==a.target){
                if(a.attackInFlight){a.ambiguousAttack=true;Unknown(11);}
                else {a.attackInFlight=true;local.action=a;admitted=true;}
            }
        }
    }
    if(!admitted)return originalAttack(self,dt,primary,context);
    local.action.row.attackedNs=Now();local.previous=attacking;attacking=&local;ScopeRestore restore{attacking,local.previous};
    const auto result=originalAttack(self,dt,primary,context);local.action.row.attackReturned=true;
    std::lock_guard lock(service.gate);
    if(Same(local.action)){
        service.action->attackInFlight=false;
        if(service.action->ambiguousAttack){service.action->row=local.action.row;Unknown(11);return result;}
        // Completion is this exact native callback's return, not idle state,
        // timer expiry, input publication, sound, or a claimed hit.
        if(service.policy.Resolve(local.action.intent.serial,local.action.intent.identity,SelectedMeleeOutcome::Completed)){
            Save(local.action.row);service.action.reset();
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
    const std::array<unsigned,5> addresses{0x578500,0x577790,0x577d40,0x57abd0,0x6c4c00};
    const std::array<void*,5> replacements{reinterpret_cast<void*>(&AcquireHook),reinterpret_cast<void*>(&SearchHook),
        reinterpret_cast<void*>(&RetryHook),reinterpret_cast<void*>(&AttackHook),reinterpret_cast<void*>(&DamageHook)};
    const std::array<void**,5> originals{reinterpret_cast<void**>(&originalAcquire),reinterpret_cast<void**>(&originalSearch),
        reinterpret_cast<void**>(&originalRetry),reinterpret_cast<void**>(&originalAttack),reinterpret_cast<void**>(&originalDamage)};
    for(unsigned n=0;n<hooks.size();++n){auto* at=reinterpret_cast<void*>(addresses[n]);
        if(MH_CreateHook(at,replacements[n],originals[n])!=MH_OK){service.installFailure=10+n;service.cleanupFailed=!Remove();return false;}hooks[n]=at;}
    for(unsigned n=0;n<hooks.size();++n)if(MH_EnableHook(hooks[n])!=MH_OK){service.installFailure=20+n;service.cleanupFailed=!Remove();return false;}
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
    if(!service.enabled.load(std::memory_order_acquire)||!selected_knife_detail::Source(source,now)||
       !ValidInput(input)||input.generation!=source.input.sequence||input.spaceGeneration!=source.input.owner.space||
       !GameplayOwnsInput(input.menu,now)||(input.hands[1].active&Trigger)==0){RevokeAdmission();service.policy.InvalidateMotion();return result;}
    const auto native=ReadNative(service.memory,service.server,service.state,service.base,source.native);
    const auto hold=hands.Current(InteractionHand::Right);
    if(!native||!hold){++service.readGaps;RevokeAdmission();service.policy.InvalidateMotion();return result;}
    if(service.action&&(!PublishedSourceContinues(service.action->intent,source.input,inputEpoch)||
       hold->token!=service.action->intent.hold||!available))RevokeAdmission();
    SelectedMeleeSample sample;sample.identity={source.input.owner,hold->token.item,
        {source.native.config.weaponData,source.native.owner.equipGeneration},inputEpoch};
    sample.motion.source=source.input;sample.motion.source.nowNs=now;sample.motion.hold=hold->token;
    sample.motion.attackAvailable=available&&Ready(*native);sample.selected=true;sample.trigger=input.hands[1].trigger;
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
    if(geometry){sample.motion.tip=geometry->motion.tip;sample.motion.direction=geometry->motion.direction;}
    else {++service.geometryGaps;++service.geometryFailures[geometryFailure];}
    const auto decision=service.policy.Update(sample,hands);
    if(!decision.intent)return result;
    Action action;action.intent=*decision.intent;action.native=*native;action.units=input.worldUnitsPerMeter;
    action.admissionEpoch=service.admissionEpoch.load(std::memory_order_acquire);
    // Keep the proven action37 trigger fallback on its stock aiming path. Only
    // a physical blade stroke requests acquisition argument substitution.
    if(action.intent.cause==SelectedMeleeCause::Stroke)action.geometry=geometry;
    if(action.intent.cause==SelectedMeleeCause::Stroke&&!geometry){
        service.policy.Resolve(action.intent.serial,action.intent.identity,SelectedMeleeOutcome::RejectedNoEffect);++service.rejected;return result;}
    if(!Current(action)||Now()>=action.intent.deadlineNs){
        service.policy.Resolve(action.intent.serial,action.intent.identity,SelectedMeleeOutcome::RejectedNoEffect);++service.rejected;return result;}
    action.row.serial=action.intent.serial;action.row.input=action.intent.sourceSequence;action.row.cause=unsigned(action.intent.cause);
    action.row.observedNs=action.intent.observedNs;action.row.deadlineNs=action.intent.deadlineNs;
    action.row.effects=action.native.links.effects;action.row.attacker=action.native.links.soldier;action.row.stagedNs=now;
    result.fire=true;result.serial=action.intent.serial;service.action=std::move(action);++service.prepared;return result;
}
void Published(const Request& request,bool applied)noexcept {
    if(!request.fire||!request.serial)return;std::lock_guard lock(service.gate);
    if(!service.action||service.action->intent.serial!=request.serial)return;
    if(applied){service.action->published=true;
        if(Now()>=service.action->intent.deadlineNs||!Current(*service.action))RevokeAdmission();}
    else{auto& a=*service.action;service.policy.Resolve(a.intent.serial,a.intent.identity,SelectedMeleeOutcome::RejectedNoEffect);
        service.action.reset();++service.rejected;}
}
void SourceAdvanced(const HandInteractionSample& source,std::uint64_t inputEpoch)noexcept {
    if(!service.installed.load(std::memory_order_acquire))return;std::lock_guard lock(service.gate);
    if(service.action&&!PublishedSourceContinues(service.action->intent,source,inputEpoch))RevokeAdmission();
}
Request Missing()noexcept {
    if(!service.installed.load(std::memory_order_acquire))return {};std::lock_guard lock(service.gate);
    RevokeAdmission();service.policy.InvalidateMotion();return {true,false,0};
}
void ObserveOwner(const ReloadStateOwner& owner)noexcept {
    if(!service.installed.load(std::memory_order_acquire)||!owner.soldier||!owner.weak||!owner.actorGeneration)return;std::lock_guard lock(service.gate);
    if(service.action){const auto& prior=service.action->native.client.owner;
        if(owner.soldier!=prior.soldier||owner.weak!=prior.weak||owner.player!=prior.player){
            auto& a=*service.action;service.policy.Resolve(a.intent.serial,a.intent.identity,SelectedMeleeOutcome::OwnerRetired);
            a.row.failure=2;Save(a.row);service.action.reset();service.policy.InvalidateMotion();}}
}
void Report(std::ostream& out) {
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
           <<",\"staged_ns\":"<<r.stagedNs<<",\"acquired_ns\":"<<r.acquiredNs<<",\"attacked_ns\":"<<r.attackedNs
           <<",\"searches\":"<<r.searches<<",\"copies\":"<<r.copies<<",\"failure\":"<<r.failure
           <<",\"acquisition_returned\":"<<r.acquisitionReturned<<",\"attack_returned\":"<<r.attackReturned
           <<",\"damage_calls\":"<<r.damageCalls<<",\"health_known\":"<<r.healthKnown
           <<",\"health_before\":"<<r.healthBefore<<",\"health_after\":"<<r.healthAfter<<",\"native_before\":";
        matrix(r.nativeBefore);out<<",\"blade\":";matrix(r.blade);out<<'}';}
    out<<"]}";out.precision(precision);
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
void Report(std::ostream& out){out<<"{\"available\":false}";}
}
#endif
