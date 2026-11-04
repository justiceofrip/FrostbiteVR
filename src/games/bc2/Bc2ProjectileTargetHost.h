#pragma once
#include "Bc2ProjectileTarget302.h"
#include "Bc2MuzzleAdmission.h"
#include "Bc2PlayerPair.h"
#include <span>
#include <string_view>
#include <ostream>

namespace fvr::bc2::projectile307 {
// Complete value copy of gameplay::Owner, including its observation-only role.
// Existing gameplay Owner::operator== intentionally excludes role; this join does not.
struct Owner {
    unsigned manager=0,player=0,weak=0,soldier=0,controlled=0,entry=0,router=0,cache=0,weapon=0,aim=0;
    WeaponEquipmentIdentity equipment{};bool foot=false;unsigned role=0;
    bool operator==(const Owner&)const=default;
};
struct Receiver {
    unsigned receiver=0,vtable=0,target=0,descriptor=0,descriptorVtable=0;
    bool operator==(const Receiver&)const=default;
};
struct Join {
    projectile302::Grant grant{};
    Owner owner{};NativeMuzzleConfig config{};Receiver receiver{};
    unsigned effects=0,configuration=0,serverPlayer=0,serverSoldier=0;
};
struct Current {
    std::uint64_t scopeSerial=0,invocation=0;
    unsigned ordinal=0,helperIndex=UINT32_MAX,thread=0,token=0;
    unsigned effects=0,configuration=0,serverPlayer=0,serverSoldier=0;
    bool tokenValid=false,helperAccepted=false,helperReturned=false;
    Owner scopeOwner{},resolvedOwner{};
    NativeMuzzleConfig scopeConfig{},resolvedConfig{};
    Receiver receiver{};
};
struct ServerIdentity {unsigned player=0,soldier=0;};
struct ServerIdentityMemory {
    PlayerPairMemory memory{};
    bool (*type)(void*,unsigned,const char*)noexcept=nullptr;
};
struct ServerIdentityBinding {
    unsigned image=0,imageSize=0,controlledGetter=0,context=0,managerVtable=0;
};
// The exact ServerShootHook relationships, freshly read at each sample bookend.
// Existing profile addresses only; MatchServerPlayer retains its indexed join.
std::optional<ServerIdentity> ReadServerIdentity(const ServerIdentityMemory&,const ServerIdentityBinding&,
    const Owner&,unsigned effects)noexcept;
inline bool SameGrant(const projectile302::Grant& a,const projectile302::Grant& b)noexcept {
    return a.scopeSerial==b.scopeSerial&&a.invocation==b.invocation&&a.ordinal==b.ordinal&&
        a.helperIndex==b.helperIndex&&a.thread==b.thread&&a.token==b.token&&a.receiver==b.receiver&&
        a.vtable==b.vtable&&a.target==b.target&&a.returnAddress==b.returnAddress&&a.source==b.source&&
        a.stackLow==b.stackLow&&a.stackHigh==b.stackHigh;
}
// Used by the real callback after resolving the current TLS scope and native copies.
inline bool Matches(const Join& saved,const projectile302::Grant& grant,const Current& c)noexcept {
    return SameGrant(saved.grant,grant)&&grant.scopeSerial&&grant.invocation&&grant.ordinal&&
        c.scopeSerial==grant.scopeSerial&&c.invocation==grant.invocation&&c.ordinal==grant.ordinal&&
        c.helperIndex==grant.helperIndex&&c.thread==grant.thread&&c.tokenValid&&c.token==grant.token&&
        c.helperAccepted&&!c.helperReturned&&c.effects==saved.effects&&c.configuration==saved.configuration&&
        c.serverPlayer==saved.serverPlayer&&c.serverSoldier==saved.serverSoldier&&
        c.scopeOwner==saved.owner&&c.resolvedOwner==saved.owner&&
        c.scopeConfig==saved.config&&c.resolvedConfig==saved.config&&c.receiver==saved.receiver&&
        c.receiver.receiver==grant.receiver&&c.receiver.vtable==grant.vtable&&c.receiver.target==grant.target&&
        c.receiver.descriptor==saved.config.projectileData&&saved.effects==saved.config.effects&&
        saved.configuration==saved.config.config;
}

struct HookApi {
    void* context=nullptr;
    bool (*create)(void*,void*,projectile302::MachineEntry,void**)noexcept=nullptr;
    bool (*disable)(void*,void*)noexcept=nullptr;
};
struct Lifecycle {
    bool requested=false,attempted=false,created=false,disabled=false,drained=false;
    bool observerRequested=false,originRequested=false;
    projectile302::OriginSession* origin=nullptr;
    bool AttachOrigin(projectile302::OriginSession& value,const projectile302::OriginApi& api)noexcept {
        if(!created||started.load()||closed.load()||origin||value.closed.load()||value.enabled.load()||!api.prepare||!api.validate)return false;
        value.api=api;origin=&value;originRequested=true;return true;
    }
    std::atomic<bool> closed{false},started{false};
    std::atomic<std::int64_t> deadline{0};
    std::int64_t duration=0; // Immutable before global hook enable; QPC ticks.
    // Published once by Install, retained through every stop/failure path.
    // No remove/free/reset/reopen operation exists in this seam.
    HookApi hooks{};projectile302::Session* session=nullptr;void* entry=nullptr;
    bool Install(bool explicitRequest,bool retainedHost,bool signatures,bool helper,std::int64_t ticks,
        projectile302::Session&,projectile302::MachineEntry&,void*& slot,void* target,
        const projectile302::ObserverApi&,const HookApi&)noexcept;
    // Single installer thread. A failed create, close or start cannot be retried.
    bool BeginInstall(bool explicitRequest,bool retainedHost,bool signatures,bool helper,std::int64_t ticks)noexcept {
        if(attempted||closed.load()||!explicitRequest)return false;
        requested=true;attempted=true;
        if(!retainedHost||!signatures||!helper||ticks<=0)return false;
        duration=ticks;return true;
    }
    bool Start()noexcept {
        bool expected=false;
        if(!created||!session||closed.load(std::memory_order_acquire)||!started.compare_exchange_strong(expected,true))return false;
        session->enabled.store(observerRequested,std::memory_order_release);
        if(origin)origin->enabled.store(originRequested,std::memory_order_release);return true;
    }
    // Anchor once to the first eligible accepted helper; never renew per row/frame.
    bool Window(std::int64_t now,bool firstAcceptedHelper=false)noexcept {
        if(closed.load(std::memory_order_acquire)||!started.load(std::memory_order_acquire)||now<=0)return false;
        auto end=deadline.load(std::memory_order_acquire);
        if(!end&&firstAcceptedHelper&&duration>0&&now<=INT64_MAX-duration){
            auto zero=std::int64_t{0};const auto proposed=now+duration;
            if(deadline.compare_exchange_strong(zero,proposed,std::memory_order_acq_rel))end=proposed;else end=zero;
        }
        return end>0&&now>=end-duration&&now<end;
    }
    void Close()noexcept {
        closed.store(true,std::memory_order_release);if(origin)origin->Close();if(session)session->Close();
    }
    bool Disable()noexcept {
        Close();if(!created)return true;
        disabled=hooks.disable&&hooks.disable(hooks.context,entry);return disabled;
    }
    bool Active()const noexcept {return session&&InterlockedCompareExchange(&session->active,0,0)!=0;}
    bool PollDrain()noexcept {drained=!Active();return drained;}
    bool Retired()const noexcept {return !created||(closed.load(std::memory_order_acquire)&&disabled&&drained);}
};

inline constexpr unsigned TargetRva=0x3155b0,TargetSize=339,SetterRva=0x210d10,SetterSize=188;
inline constexpr std::string_view ExecutableSha="3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258";
inline constexpr std::string_view TargetSha="5fe442f160311174d7588a5d49e80c01e5df11f02e1c6d6e13d06c4bcc244913";
inline constexpr std::string_view SetterSha="04434e07469c10f9d6677a6ad826e23b034d37fb39951db76ee0351147cd9590";
bool VerifySignatures(std::span<const std::byte> executable,std::span<const std::byte> targetFile,
    std::span<const std::byte> setterFile,std::span<const std::byte> targetLive,std::span<const std::byte> setterLive)noexcept;
// Final value-only snapshot; callbacks never serialize or allocate.
void Report(std::ostream&,const Lifecycle&,projectile302::Session&);
} // namespace fvr::bc2::projectile307
