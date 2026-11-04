#include "Bc2ChestKnifeAttackTransaction.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace chest429 {
#include "Bc2ChestKnifeCallbackScopes.inc"
namespace pk=fvr::bc2::private_knife;
using Phase=pk::ChestAttackPhase;
using Outcome=pk::ChestAttackOutcome;
constexpr DWORD nativeFault=0xe1122429;
#define REQUIRE_KNIFE_CASE(x) do {if(!(x)){std::fprintf(stderr,"failed at line %d\n",__LINE__);std::exit(1);}} while(0)
template<class Body> __declspec(noinline) bool CatchNative(const Body& body) {
    __try {body();}
    __except(GetExceptionCode()==nativeFault?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
    return false;
}
enum class Exit {Normal,ParentFault,ChildCaught,ChildEscapes};
thread_local int* slot=nullptr;
std::atomic<unsigned> active{0};
void Exercise(Phase phase,Exit path) {
    pk::ChestAttackIdentity key;
    key.serial=2;key.lifetime=3;key.operation=4;key.sourceEpoch=9;key.input=497;key.strokeSerial=2;key.holdId=16;
    key.originalOwner={0x10000,0x10200,0x10400,0x10600,7,8,11};
    key.inputOwner={0x1040000010200,7,12,11};key.knife={0x20000,6};
    key.selectedOwner=key.originalOwner;key.selectedOwner.weapon=unsigned(key.knife.id);
    key.observedNs=100;key.geometryDeadlineNs=110;key.deadlineNs=120;
    key.serverActor=1;key.serverItem=2;key.effects=3;key.firing[2]=4;key.config.primaryFire=5;key.config.fireInputAction=8;
    const pk::ChestAttackTarget target{0x50000,0x51000};
    fvr::math::Matrix4 blade{};for(unsigned i=0;i<4;++i)blade.values[i][i]=1;
    pk::ChestAttackTransaction tx;
    REQUIRE_KNIFE_CASE(tx.Stage(key,blade,101)&&tx.Consume(key,2)&&tx.BeginAcquire(key,true,true)&&tx.TakeGeometry(key,101,true));
    if(phase==Phase::Attacking){
        tx.GeometryReturned(key,false,true);
        REQUIRE_KNIFE_CASE(tx.EndAcquire(key,target)&&tx.BeginAttack(key,target,true,true));
    }
    auto foreign=key;++foreign.serial;tx.AbortCallback(foreign,phase);
    tx.AbortCallback(key,phase==Phase::Attacking?Phase::Acquiring:Phase::Attacking);
    REQUIRE_KNIFE_CASE(!tx.Pending()->failed&&tx.Phase()==phase);
    int parent=1,local=2;REQUIRE_KNIFE_CASE(!slot&&active.load()==0);slot=&parent;
    unsigned calls=0,childCalls=0;std::uintptr_t result=0;
    const auto child=[&]() noexcept {
        CountChestKnifeCallback(active,[&]{
            const auto value=ForwardChestKnifeOriginal<int>(nullptr,nullptr,[&]{
                ++childCalls;REQUIRE_KNIFE_CASE(slot==&local&&active.load()==2);
                if(path==Exit::ChildCaught||path==Exit::ChildEscapes)RaiseException(nativeFault,0,0,nullptr);
                return 0xa0u;
            },[&]{tx.Fail(key);});
            REQUIRE_KNIFE_CASE(value==0xa0u);
            if(phase==Phase::Acquiring)tx.GeometryReturned(key,false,true);else tx.Damage(key,true,100,75);
        });
    };
    const bool escaped=CatchNative([&]() noexcept {
        CountChestKnifeCallback(active,[&]{
            result=ForwardChestKnifeOriginal(&slot,&local,[&]{
                ++calls;REQUIRE_KNIFE_CASE(slot==&local&&active.load()==1);
                if(path==Exit::ChildCaught){
                    REQUIRE_KNIFE_CASE(CatchNative(child)&&slot==&local&&active.load()==1);
                    REQUIRE_KNIFE_CASE(tx.Phase()==phase&&tx.Pending()->failed);
                }else child();
                if(path==Exit::ParentFault)RaiseException(nativeFault,0,0,nullptr);
                return 0xbeefu;
            },[&]{REQUIRE_KNIFE_CASE(slot==&parent);tx.AbortCallback(key,phase);});
            if(phase==Phase::Acquiring)REQUIRE_KNIFE_CASE(tx.EndAcquire(key,target));else REQUIRE_KNIFE_CASE(tx.EndAttack(key));
        });
    });
    REQUIRE_KNIFE_CASE(slot==&parent&&active.load()==0&&calls==1&&childCalls==1);slot=nullptr;
    REQUIRE_KNIFE_CASE(escaped==(path==Exit::ParentFault||path==Exit::ChildEscapes)&&result==(escaped?0u:0xbeefu));
    REQUIRE_KNIFE_CASE((tx.Phase()==Phase::Aborted)==escaped);
    if(escaped)REQUIRE_KNIFE_CASE(!tx.BeginAcquire(key,true,true)&&!tx.BeginAttack(key,target,true,true));
    REQUIRE_KNIFE_CASE(!tx.Settle(key)&&tx.Consume(key,1));
    const auto r=tx.Settle(key);REQUIRE_KNIFE_CASE(r&&r->failed==(path!=Exit::Normal));
    REQUIRE_KNIFE_CASE(r->acquisitionReturned==(phase==Phase::Attacking||!escaped));
    REQUIRE_KNIFE_CASE(r->attackReturned==(phase==Phase::Attacking&&!escaped));
    const bool childReturned=path==Exit::Normal||path==Exit::ParentFault;
    REQUIRE_KNIFE_CASE(r->damageCalls==(phase==Phase::Attacking&&childReturned?1u:0u));
    REQUIRE_KNIFE_CASE(r->geometryForwardMask==(phase==Phase::Attacking||childReturned?1u:0u));
    REQUIRE_KNIFE_CASE(r->outcome==(path==Exit::Normal&&phase==Phase::Attacking?Outcome::Hit:Outcome::Unknown));
}
int Run() {
    for(auto phase:{Phase::Acquiring,Phase::Attacking})
        for(auto path:{Exit::Normal,Exit::ParentFault,Exit::ChildCaught,Exit::ChildEscapes})Exercise(phase,path);
    return 0;
}
#undef REQUIRE_KNIFE_CASE
}
