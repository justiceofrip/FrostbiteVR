#ifndef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
#define FVR_BC2_CHEST_KNIFE_DRAW_RETURN
#define FVR_OUTER_CASE_LOCAL_DRAW_RETURN
#endif
#include <Windows.h>
#include "Bc2SelectedKnifeHookUnwind.h"
#include "Bc2ChestKnifeAttackBridge.h"
#include "fvr/interaction/SelectedMeleeAttack.h"
#include <cstdio>
#include <cstdlib>

namespace outer433 {
using namespace fvr::bc2::selectedKnife;
namespace chest=fvr::bc2::chestKnifeNative;
static_assert(!noexcept(chest::BridgeAcquire(nullptr,0,nullptr)));
static_assert(!noexcept(chest::BridgeSearch(nullptr,0,nullptr,0,0,0,false,nullptr)));
static_assert(!noexcept(chest::BridgeAttack(nullptr,0,0,nullptr,0,nullptr)));
static_assert(!noexcept(chest::BridgeDamage(nullptr,nullptr,nullptr,0,nullptr)));
#include "Bc2ChestKnifeCallbackScopes.inc"
#define REQUIRE_KNIFE_CASE(x) do {if(!(x)){std::fprintf(stderr,"failed at line %d\n",__LINE__);std::exit(1);}} while(0)
constexpr DWORD fault=0xe0420433;
struct CppFault {unsigned cookie;};
enum class Exit {Normal,ParentFault,ChildCaught,ChildEscapes};
enum class Route {SelectedAcquire,SelectedAttack,Chest,Passthrough};
__declspec(noinline) void Raise(bool cpp) {
    if(cpp)throw CppFault{433};
    RaiseException(fault,0,0,nullptr);
}
template<class Body> __declspec(noinline) bool CatchSeh(const Body& body) {
    __try {body();}
    __except(GetExceptionCode()==fault?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
template<class Body> bool Catch(bool cpp,const Body& body) {
    if(!cpp)return CatchSeh(body);
    try {body();}catch(const CppFault& e){REQUIRE_KNIFE_CASE(e.cookie==433);return true;}
    return false;
}
struct Scope {bool childFault=false;};
thread_local Scope* acquiring=nullptr;
thread_local Scope* attacking=nullptr;
thread_local int* chestAcquire=nullptr;
thread_local int* chestAttack=nullptr;
thread_local KnifeSoundDispatchContext sound;
std::atomic<unsigned> active=0,chestActive=0;
std::atomic<bool> observeSound=true;
struct Isolation {
    int* acquire=chestAcquire;
    int* attack=chestAttack;
    Isolation(){chestAcquire=chestAttack=nullptr;}
    ~Isolation(){chestAcquire=acquire;chestAttack=attack;}
};
constexpr KnifeSoundDispatchContext SavedSound{KnifeSoundDispatchPhase::Attack,1,2,3,4,5,6,7};
constexpr KnifeSoundDispatchContext InnerSound{KnifeSoundDispatchPhase::Acquisition,11,12,13,14,15,16,17};
void Exercise(Route route,Exit path,bool cpp,bool soundEnabled) {
    REQUIRE_KNIFE_CASE(!active.load()&&!chestActive.load()&&!acquiring&&!attacking&&!chestAcquire&&!chestAttack);
    Scope savedAcquire,savedAttack,local;
    int savedChestAcquire=0,savedChestAttack=0,localChest=0;
    acquiring=&savedAcquire;attacking=&savedAttack;
    chestAcquire=&savedChestAcquire;chestAttack=&savedChestAttack;
    sound=SavedSound;observeSound=soundEnabled;
    const bool selected=route==Route::SelectedAcquire||route==Route::SelectedAttack;
    const bool drawsAcquire=route==Route::SelectedAcquire;
    bool originalReturned=false;
    unsigned calls=0,childCalls=0,aborts=0,chestAborts=0,receipts=0;
    std::uintptr_t result=0;
    const bool escaped=Catch(cpp,[&]{
        result=RunSelectedKnifeParent<Isolation>(active,acquiring,attacking,sound,observeSound,[&](auto& boundary){
            REQUIRE_KNIFE_CASE(active==1&&!acquiring&&!attacking&&!chestAcquire&&!chestAttack);
            REQUIRE_KNIFE_CASE(sound==(soundEnabled?KnifeSoundDispatchContext{}:SavedSound));
            if(selected){(drawsAcquire?acquiring:attacking)=&local;boundary.Bind(InnerSound);}
            const auto original=[&]{
                ++calls;
                REQUIRE_KNIFE_CASE(active==1);
                const auto parentSound=sound;
                auto* parentAcquire=acquiring;auto* parentAttack=attacking;auto* parentChest=chestAcquire;
                const auto child=[&]{
                    const auto value=RunSelectedKnifeChild(active,selected?&local:nullptr,[&]{
                        ++childCalls;REQUIRE_KNIFE_CASE(active==2&&acquiring==parentAcquire&&attacking==parentAttack&&sound==parentSound);
                        if(path==Exit::ChildCaught||path==Exit::ChildEscapes)Raise(cpp);
                        return 0xfe11u;
                    });
                    REQUIRE_KNIFE_CASE(value==0xfe11u);
                };
                // An independent nested parent masks every outer slot even
                // on passthrough, then restores those exact values on unwind.
                unsigned nestedCalls=0;
                REQUIRE_KNIFE_CASE(Catch(cpp,[&]{RunSelectedKnifeParent<Isolation>(active,acquiring,attacking,sound,observeSound,[&](auto&){
                    ++nestedCalls;REQUIRE_KNIFE_CASE(active==2&&!acquiring&&!attacking&&!chestAcquire&&!chestAttack);
                    REQUIRE_KNIFE_CASE(sound==(soundEnabled?KnifeSoundDispatchContext{}:SavedSound));
                    Raise(cpp);return 0u;
                });}));
                REQUIRE_KNIFE_CASE(nestedCalls==1&&active==1&&acquiring==parentAcquire&&attacking==parentAttack&&
                    chestAcquire==parentChest&&!chestAttack&&sound==parentSound&&!local.childFault);
                if(path==Exit::ChildCaught)REQUIRE_KNIFE_CASE(Catch(cpp,child));else child();
                REQUIRE_KNIFE_CASE(active==1&&acquiring==parentAcquire&&attacking==parentAttack&&sound==parentSound);
                if(path==Exit::ParentFault)Raise(cpp);
                return std::uintptr_t(0xbeef);
            };
            if(route==Route::Chest){
                std::uintptr_t value=0;
                CountChestKnifeCallback(chestActive,[&]{
                    value=ForwardChestKnifeOriginal(&chestAcquire,&localChest,original,[&]{++chestAborts;});
                });
                return value;
            }
            if(!selected)return original();
            const auto value=ForwardAdmittedSelectedKnife(originalReturned,local.childFault,original,[&]{++aborts;});
            if(!local.childFault)++receipts;
            return value;
        });
    });
    const bool failedParent=path==Exit::ParentFault||path==Exit::ChildEscapes;
    REQUIRE_KNIFE_CASE(calls==1&&childCalls==1&&escaped==failedParent&&result==(failedParent?0u:0xbeefu));
    REQUIRE_KNIFE_CASE(originalReturned==(selected&&!failedParent)&&receipts==unsigned(selected&&path==Exit::Normal));
    REQUIRE_KNIFE_CASE(aborts==unsigned(selected&&path!=Exit::Normal)&&chestAborts==unsigned(route==Route::Chest&&failedParent));
    REQUIRE_KNIFE_CASE(!active.load()&&!chestActive.load()&&acquiring==&savedAcquire&&attacking==&savedAttack&&
        chestAcquire==&savedChestAcquire&&chestAttack==&savedChestAttack&&sound==SavedSound);
    acquiring=attacking=nullptr;chestAcquire=chestAttack=nullptr;
}
void CheckPolicy(bool priorUnknown) {
    using namespace fvr::interaction;
    HandInteraction hands;SelectedMeleeAttack policy;SelectedMeleeSample sample{};
    sample.motion.source={{1,2,3,4},1,0,100000000,0,true,{true,true},{false,false}};
    sample.identity={sample.motion.source.owner,{42,1},{53,1},1};
    sample.selected=sample.motion.attackAvailable=true;
    const auto step=[&](float trigger){
        auto& source=sample.motion.source;++source.sequence;
        source.observedNs+=10000000;source.nowNs=source.observedNs;source.deadlineNs=source.nowNs+100000000;
        hands.Update(source);HandContactProof proof{{11,1},source.sequence,source.deadlineNs,true};
        auto held=hands.Current(InteractionHand::Right);
        const auto claim=held?hands.Renew(source,held->token,proof):hands.Acquire(source,
            {source.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.identity.item,proof,1,0});
        REQUIRE_KNIFE_CASE(claim.claim);sample.motion.hold=claim.claim->token;sample.trigger=trigger;
        return policy.Update(sample,hands).intent;
    };
    REQUIRE_KNIFE_CASE(!step(0)&&!step(0));const auto intent=step(1);REQUIRE_KNIFE_CASE(intent);
    using Outcome=SelectedMeleeOutcome;
    REQUIRE_KNIFE_CASE(!policy.Resolve(intent->serial,intent->identity,Outcome::CallbackAborted));
    REQUIRE_KNIFE_CASE(policy.Resolve(intent->serial,intent->identity,Outcome::Submitted));
    if(priorUnknown)REQUIRE_KNIFE_CASE(policy.Resolve(intent->serial,intent->identity,Outcome::Unknown));
    auto foreign=intent->identity;++foreign.inputEpoch;
    REQUIRE_KNIFE_CASE(!policy.Resolve(intent->serial+1,intent->identity,Outcome::CallbackAborted));
    REQUIRE_KNIFE_CASE(!policy.Resolve(intent->serial,foreign,Outcome::CallbackAborted)&&policy.Pending());
    REQUIRE_KNIFE_CASE(policy.Resolve(intent->serial,intent->identity,Outcome::CallbackAborted)&&!policy.Pending());
    REQUIRE_KNIFE_CASE(!policy.Resolve(intent->serial,intent->identity,Outcome::Completed)&&!step(1)&&!step(0));
    const auto next=step(1);REQUIRE_KNIFE_CASE(next&&next->serial>intent->serial);
    REQUIRE_KNIFE_CASE(!policy.Resolve(intent->serial,intent->identity,Outcome::CallbackAborted)&&policy.Pending()->serial==next->serial);
}
int Run() {
    for(auto route:{Route::SelectedAcquire,Route::SelectedAttack,Route::Chest,Route::Passthrough})
        for(auto path:{Exit::Normal,Exit::ParentFault,Exit::ChildCaught,Exit::ChildEscapes})
            for(bool cpp:{false,true})for(bool soundEnabled:{false,true})Exercise(route,path,cpp,soundEnabled);
    CheckPolicy(false);CheckPolicy(true);
    return 0;
}
#undef REQUIRE_KNIFE_CASE
}
#ifdef FVR_OUTER_CASE_LOCAL_DRAW_RETURN
#undef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
#undef FVR_OUTER_CASE_LOCAL_DRAW_RETURN
#endif
