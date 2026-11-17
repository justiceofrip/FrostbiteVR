#include "Bc2NativeCycleBank.h"
#include "Bc2ReloadConfigDescriptor.h"
#include "Test.h"
#include <cstring>
#include <memory>
#include <type_traits>
using namespace fvr::bc2;
namespace {
// The runtime bank has static storage. Fixtures must own it outside their
// stack frames too, and neither path may introduce whole-bank copies.
static_assert(!std::is_copy_constructible_v<Bc2NativeCycleBank> &&
              !std::is_copy_assignable_v<Bc2NativeCycleBank> &&
              !std::is_move_constructible_v<Bc2NativeCycleBank> &&
              !std::is_move_assignable_v<Bc2NativeCycleBank>);
Bc2NativeCycleSelection Source(unsigned resource,std::uint64_t sequence=1){
    Bc2NativeCycleSelection s;s.mode=Bc2NativeCycleMode::Pump;s.nowNs=1000000000+sequence;
    auto& r=s.reserve;r.sequence=sequence;r.observedNs=s.nowNs;r.deadlineNs=s.nowNs+50000000;
    r.loaded=8;r.reserve=24;r.capacity=8;r.verified=r.allThreeIdle=true;
    r.identity={{0x10000,0x11000,0x12000,0x13000+resource*0x10000,11,12+resource,13},
        {0x14000+resource*0x10000,0x15000+resource*0x10000,0x16000+resource*0x10000},
        0x17000,0x18000,0x19000+resource*0x10000};
    auto& c=s.config;const auto& d=SpasReloadDescriptor;const auto& v=d.values;
    std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());
    std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
    c.weaponData=0x20000;c.firingData=0x21000;c.primaryFire=0x22000;c.ammoAddress=0x22170;
    c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;
    c.reloadInputAction=v.reloadInputAction;c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;
    c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;
    c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;return s;
}
Bc2NativeCycleActorDrain Drain(const Bc2NativeCycleBank& bank,const Bc2NativeCycleSelection& source){
    const auto v=bank.View(source.nowNs);
    return {v.native,v.cycle,v.shot,source.reserve.sequence+1,source.nowNs,source.reserve.deadlineNs,true};
}
int IdleResourcesReuseOnlyDrainedServices(){
    const auto storage=std::make_unique<Bc2NativeCycleBank>();auto& bank=*storage;
    for(unsigned n=0;n<24;++n){const auto source=Source(n,n+1);const auto previous=bank.Route(source.nowNs);
        CHECK(bank.SelectObserved(source,Drain(bank,source)));CHECK(bank.Owned()==1);
        CHECK(bank.View(source.nowNs).native==source.reserve.identity);
        CHECK(!bank.Find(previous.slot));bool applied=false;
        CHECK(!bank.Apply(previous,source.nowNs,[&](auto&){applied=true;return true;}));CHECK(!applied);
        const auto route=bank.Route(source.nowNs);
        CHECK(bank.Apply(route,source.nowNs,[&](auto&){applied=true;return true;}));CHECK(applied);
    }
    return 0;
}
int EveryIdentityComponentAndConfigurationRemainBound(){
    for(unsigned field=0;field<21;++field){const auto storage=std::make_unique<Bc2NativeCycleBank>();auto& bank=*storage;const auto original=Source(0);
        CHECK(bank.SelectObserved(original,Drain(bank,original)));const auto route=bank.Route(original.nowNs);
        auto bad=Source(0,2);auto& n=bad.reserve.identity;
        switch(field){
        case 0:++n.owner.player;break;case 1:++n.owner.soldier;break;case 2:++n.owner.weak;break;
        case 3:++n.owner.weapon;break;case 4:++n.owner.actorGeneration;break;case 5:++n.owner.space;break;
        case 6:++n.firing[0];break;case 7:++n.firing[1];break;case 8:++n.firing[2];break;
        case 9:++n.serverPlayer;break;case 10:++n.serverSoldier;break;case 11:++n.serverItem;break;
        case 12:++bad.config.weaponData;break;case 13:++bad.config.firingData;break;
        case 14:++bad.config.primaryFire;++bad.config.ammoAddress;break;case 15:bad.config.assetName[0]='x';break;
        case 16:bad.config.assetPath[0]='x';break;case 17:++bad.config.boltDelay;break;
        case 18:bad.mode=Bc2NativeCycleMode::Bolt;break;case 19:bad.reserve.allThreeIdle=false;break;
        case 20:bad.reserve.deadlineNs=bad.nowNs;break;
        }
        CHECK(!bank.SelectObserved(bad,Drain(bank,bad)));CHECK(bank.Route(original.nowNs)==route);
    }
    return 0;
}
int DrainAndGenerationFailuresCannotMutateSelection(){
    for(unsigned field=0;field<7;++field){const auto storage=std::make_unique<Bc2NativeCycleBank>();auto& bank=*storage;auto source=Source(0);
        CHECK(bank.SelectObserved(source,Drain(bank,source)));source=Source(1,2);auto drain=Drain(bank,source);
        switch(field){case 0:drain.callbacksDrained=false;break;case 1:drain.callbackRevision=0;break;
        case 2:++drain.expected.firing[0];break;case 3:++drain.cycle;break;case 4:++drain.shot;break;
        case 5:drain.deadlineNs=source.nowNs;break;case 6:++drain.deadlineNs;break;}
        const auto route=bank.Route(source.nowNs);CHECK(!bank.SelectObserved(source,drain));CHECK(bank.Route(source.nowNs)==route);
    }
    for(auto generation:{std::uint64_t{0},UINT64_MAX}){const auto storage=std::make_unique<Bc2NativeCycleBank>(generation);auto& bank=*storage;const auto source=Source(0);
        const auto route=bank.Route(source.nowNs);CHECK(!bank.SelectObserved(source,Drain(bank,source)));
        CHECK(bank.Route(source.nowNs)==route&&bank.Owned()==0);}
    const auto storage=std::make_unique<Bc2NativeCycleBank>(UINT64_MAX-1);auto& last=*storage;
    auto source=Source(0);CHECK(last.SelectObserved(source,Drain(last,source)));
    CHECK(last.SelectedSlot().generation==UINT64_MAX);source=Source(1,2);
    CHECK(!last.SelectObserved(source,Drain(last,source)));CHECK(last.Owned()==1);return 0;
}
}
int main(){CHECK(!IdleResourcesReuseOnlyDrainedServices());CHECK(!EveryIdentityComponentAndConfigurationRemainBound());
    CHECK(!DrainAndGenerationFailuresCannotMutateSelection());return 0;}
