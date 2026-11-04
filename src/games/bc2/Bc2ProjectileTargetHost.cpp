#include "Bc2ProjectileTargetHost.h"
#include <bcrypt.h>
#include <bit>
#include <algorithm>
#include <climits>

namespace fvr::bc2::projectile307 {
std::optional<ServerIdentity> ReadServerIdentity(const ServerIdentityMemory& api,const ServerIdentityBinding& b,
    const Owner& owner,unsigned effects)noexcept {
    const auto word=[&](unsigned base,unsigned offset,unsigned& out)noexcept {
        const auto at=std::uint64_t(base)+offset;
        return api.memory.read&&base>=0x10000&&at+4<=UINT32_MAX&&
            api.memory.read(api.memory.context,unsigned(at),&out,4);
    };
    const auto type=[&](unsigned object,const char* name)noexcept {
        return api.type&&api.type(api.memory.context,object,name);
    };
    if(!owner.foot||!owner.weapon||!type(effects,"ServerWeaponFiringEffects"))return {};
    unsigned client=0,data=0,player=0,vt=0,clientData=0,getter=0;
    if(!word(owner.weapon,0x38,client)||!word(effects,0x10,data)||!word(effects,0x128,player)||!word(player,0,vt)||
       !type(client,"ClientWeaponFiringEffects")||!type(data,"WeaponFiringData")||!word(client,0x10,clientData)||
       data!=clientData||vt<b.image||std::uint64_t(vt)+0x34>std::uint64_t(b.image)+b.imageSize||
       !word(vt,0x30,getter)||getter!=b.controlledGetter)return {};
    unsigned soldier=0,callback=0,serverData=0,clientSoldierData=0,callbackData=0,controlled=0;
    if(!word(player,0xc3c,soldier)||!word(effects,0xd4,callback)||!type(soldier,"ServerSoldierEntity")||
       !word(soldier,0xc,serverData)||!word(owner.soldier,0xc,clientSoldierData)||serverData!=clientSoldierData||
       !word(callback,4,callbackData)||callbackData!=data||!word(player,0xc6c,controlled)||controlled!=soldier||
       !MatchServerPlayer(api.memory,b.context,b.managerVtable,owner.player,player))return {};
    return ServerIdentity{player,soldier};
}
bool Lifecycle::Install(bool explicitRequest,bool retainedHost,bool signatures,bool helper,std::int64_t ticks,
    projectile302::Session& observer,projectile302::MachineEntry& original,void*& slot,void* target,
    const projectile302::ObserverApi& api,const HookApi& hookApi)noexcept {
    const bool dependencies=!original&&!slot&&target&&api.read&&api.validate&&hookApi.create&&hookApi.disable&&
        !observer.enabled.load(std::memory_order_acquire);
    if(!BeginInstall(explicitRequest,retainedHost,signatures,helper&&dependencies,ticks))return false;
    void* trampoline=nullptr;
    if(!hookApi.create(hookApi.context,target,projectile302::Thunk,&trampoline))return false;
    // MinHook's successful create supplies the trampoline before global enable.
    // Retain successful creation even if an invalid backend omits that pointer:
    // never enable/reinstall it or claim an uncreated hook safe to reclaim.
    hooks=hookApi;session=&observer;entry=target;created=true;
    original=reinterpret_cast<projectile302::MachineEntry>(trampoline);observer.api=api;slot=target;
    if(!original){Close();return false;}return true;
}
static bool Digest(std::span<const std::byte> bytes,std::string_view expected)noexcept {
    if(bytes.empty()||bytes.size()>ULONG_MAX||expected.size()!=64)return false;
    std::array<unsigned char,32> digest{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
        reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),ULONG(bytes.size()),
        digest.data(),ULONG(digest.size()))<0)return false;
    constexpr char hex[]="0123456789abcdef";
    for(unsigned n=0;n<digest.size();++n)
        if(hex[digest[n]>>4]!=expected[n*2]||hex[digest[n]&15]!=expected[n*2+1])return false;
    return true;
}
bool VerifySignatures(std::span<const std::byte> executable,std::span<const std::byte> targetFile,
    std::span<const std::byte> setterFile,std::span<const std::byte> targetLive,std::span<const std::byte> setterLive)noexcept {
    return targetFile.size()==TargetSize&&targetLive.size()==TargetSize&&setterFile.size()==SetterSize&&setterLive.size()==SetterSize&&
        Digest(executable,ExecutableSha)&&Digest(targetFile,TargetSha)&&Digest(setterFile,SetterSha)&&
        std::equal(targetFile.begin(),targetFile.end(),targetLive.begin())&&
        std::equal(setterFile.begin(),setterFile.end(),setterLive.begin());
}
void Report(std::ostream& out,const Lifecycle& life,projectile302::Session& session) {
    using namespace projectile302;
    struct Final {Grant grant;Sample before,after;Outcome outcome;bool claimed,originalReturned,complete;};
    std::array<Final,64> rows{};unsigned offered=0,completed=0;
    const bool retired=life.Retired();
    const bool snapshot=retired&&life.closed.load(std::memory_order_acquire)&&!session.enabled.load(std::memory_order_acquire)&&
        !InterlockedCompareExchange(&session.active,0,0)&&TryAcquireSRWLockExclusive(&session.gate);
    if(snapshot){
        offered=(std::min)(session.offered,unsigned(rows.size()));
        for(unsigned n=0;n<offered;++n){const auto& r=session.rows[n];
            // Copy all metadata under gate, only complete row values after acquire.
            if(!r.complete.load(std::memory_order_acquire))continue;
            rows[completed++]={r.grant,r.before,r.after,r.outcome,r.claimed,r.originalReturned,true};
        }
        ReleaseSRWLockExclusive(&session.gate);
    }
    out<<"{\"schema\":1,\"compiled\":true,\"read_only\":true,\"requested\":"<<life.requested
       <<",\"attempted\":"<<life.attempted<<",\"created\":"<<life.created<<",\"closed\":"<<life.closed.load()
       <<",\"disabled\":"<<life.disabled<<",\"registered_callbacks_drained\":"<<life.drained
       <<",\"trampoline_retained_until_process_exit\":true,\"hook_removed\":false,\"snapshot_available\":"<<snapshot
       <<",\"window_deadline_qpc\":"<<life.deadline.load()<<",\"window_duration_qpc\":"<<life.duration
       <<",\"window_started\":"<<(life.deadline.load()!=0)<<",\"capacity\":64,\"offered\":"<<offered
       <<",\"complete_rows\":"<<completed<<",\"incomplete_rows_omitted\":"<<(offered-completed)
       <<",\"busy\":"<<session.busy.load()<<",\"full\":"<<session.full.load()<<",\"no_grant\":"<<session.noGrant.load()
       <<",\"nested\":"<<session.nested.load()<<",\"disabled_calls\":"<<session.disabled.load()
       <<",\"scope\":\"whole_7155b0_including_nested_native_calls\",\"unchanged_means\":\"selected_raw_xyz_bits_equal\""
       <<",\"setter_skip_observed\":false,\"native_launch_acceptance\":false,\"impact_observed\":false"
       <<",\"producer_diagnostics\":"<<session.api.producerDiagnostics
       <<",\"factory_return_observed\":false,\"primary_nonretention_proven\":false"
       <<",\"sample_status_order\":[\"Unattempted\",\"Complete\",\"InvalidArguments\",\"ReadFailure\",\"IdentityChanged\",\"Torn\"]"
       <<",\"outcome_order\":[\"Offered\",\"RejectedIdentity\",\"ObserverFault\",\"EntryReadFailure\",\"ReturnReadFailure\",\"StorageIdentityChanged\",\"Torn\",\"NativeException\",\"ReturnedChanged\",\"ReturnedUnchanged\"],\"records\":[";
    const auto xyz=[&](const std::array<Word,3>& bits,bool read){
        out<<"{\"read\":"<<read<<",\"bits\":["<<bits[0]<<','<<bits[1]<<','<<bits[2]<<"],\"finite_xyz\":";
        const std::array<float,3> values{std::bit_cast<float>(bits[0]),std::bit_cast<float>(bits[1]),std::bit_cast<float>(bits[2])};
        if(!read||!std::isfinite(values[0])||!std::isfinite(values[1])||!std::isfinite(values[2]))out<<"null";
        else out<<'['<<values[0]<<','<<values[1]<<','<<values[2]<<']';out<<'}';
    };
    const auto sample=[&](const Sample& s){out<<"{\"status\":"<<unsigned(s.status)<<",\"receiver\":"<<s.identity.receiver
        <<",\"vtable\":"<<s.identity.vtable<<",\"target\":"<<s.identity.target<<",\"backing\":"<<s.identity.backing
        <<",\"selected\":"<<s.identity.selected<<",\"index\":"<<unsigned(s.identity.index)<<",\"indexed\":"<<s.identity.indexed
        <<",\"flags_read\":"<<s.flagsRead<<",\"flags\":"<<s.flags<<",\"source_address\":"<<s.source<<",\"source\":";
        xyz(s.sourceBits,s.sourceRead);out<<",\"selected_xyz\":";xyz(s.selectedBits,s.selectedRead);
        out<<",\"receiver_40_44_48\":";xyz(s.velocityCacheBits,s.velocityRead);
        const auto& p=s.producer;
        out<<",\"producer\":{\"status\":"<<unsigned(p.status)<<",\"embedded_address\":"<<p.embedded
           <<",\"words_read\":"<<p.wordsRead<<",\"words\":["<<p.words[0]<<','<<p.words[1]<<','<<p.words[2]<<','<<p.words[3]
           <<"],\"slot4_target_read\":"<<p.targetRead<<",\"slot4_target\":"<<p.slot4Target<<"}}";};
    for(unsigned n=0;n<completed;++n){if(n)out<<',';const auto& r=rows[n];const auto& g=r.grant;
        out<<"{\"scope_serial\":"<<g.scopeSerial<<",\"invocation\":"<<g.invocation<<",\"ordinal\":"<<g.ordinal
           <<",\"helper_index\":"<<g.helperIndex<<",\"thread\":"<<g.thread<<",\"token\":"<<g.token
           <<",\"receiver\":"<<g.receiver<<",\"vtable\":"<<g.vtable<<",\"target\":"<<g.target
           <<",\"return_address\":"<<g.returnAddress<<",\"source_address\":"<<g.source
           <<",\"stack_low\":"<<g.stackLow<<",\"stack_high\":"<<g.stackHigh<<",\"outcome\":"<<unsigned(r.outcome)
           <<",\"original_returned\":"<<r.originalReturned<<",\"before\":";sample(r.before);out<<",\"after\":";sample(r.after);out<<'}';
    }
    out<<"]}";
}
} // namespace fvr::bc2::projectile307
