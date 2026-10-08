#define main ExistingBodyInventoryMain
#include "Bc2BodyInventoryTests.cpp"
#undef main
#include "Bc2BodyCarriedPair.h"
#include <iostream>
#include <thread>
using namespace fvr::bc2;
namespace {
auto Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
auto Configs(const BodyInventoryDisplay& d){std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> out{};
    for(unsigned i=0;i<d.count;++i){const auto& slot=d.slots[i];if(slot.native.weapon==d.selectedOwner.weapon)continue;
        auto c=std::make_shared<CarriedMeshesSnapshot>();c->weapon=slot.native.weapon;c->nativeSlot=slot.native.slot;c->persistence=slot.native.persistence;
        auto& s=c->configured;s.owner=d.selectedOwner;s.sequence=d.sequence;s.observedNs=d.observedNs;s.deadlineNs=d.deadlineNs;
        s.weaponData=slot.native.data;s.inventory=d.carried.inventory;s.selectedSlot=0;strcpy_s(s.weaponName.data(),128,"SPAS12_sp");
        s.stateCount=1;s.soleConfiguredArray=0x40000;s.states[0].array=s.soleConfiguredArray;s.states[0].count=1;
        strcpy_s(s.states[0].meshes[0].assetPath.data(),512,"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh");out[i]=c;}
    return out;}
struct FixturePair {
    Run r;BodyCarriedRenderBatch a,b;ipc::FrameLease lease{};
    FixturePair(){r.Tick();const auto first=r.adapter.Display(r.s.hand.nowNs);if(!first)std::terminate();
        a=*BuildBodyCarriedBatch(std::make_shared<const BodyInventoryDisplay>(*first),Configs(*first),r.s.input,Identity(),{},r.s.hand.nowNs);
        r.Advance();r.s.input.head.position.x+=.2f;r.Tick();const auto second=r.adapter.Display(r.s.hand.nowNs);if(!second)std::terminate();
        b=*BuildBodyCarriedBatch(std::make_shared<const BodyInventoryDisplay>(*second),Configs(*second),r.s.input,Identity(),{},r.s.hand.nowNs);
        lease.channelGeneration=1;lease.requestId=2;lease.native={3,4,5};lease.tracking.generation=79;lease.tracking.spaceGeneration=first->physicalOwner.space;
    }
    auto Now()const{return r.s.hand.nowNs;}
};
int ActualPairAndDistinctCounters(){FixturePair f;BodyCarriedPair pair;const auto key=CarriedPairKey(f.lease);
    CHECK(f.a.inventory->sequence!=key.tracking);const auto left=pair.Read(key,0,f.a,f.Now());CHECK(left);
    const auto right=pair.Read(key,1,f.b,f.Now());CHECK(right&&right==left);CHECK(right->original.inventory->sequence==f.a.inventory->sequence);
    CHECK(Near(f.b.instances[0].world.values[3][0]-f.a.instances[0].world.values[3][0],.2f));
    graphics::BodyPropFrame frame;frame.frameId=f.lease.native.frameId;frame.spaceGeneration=key.space;frame.trackingGeneration=key.tracking;
    graphics::PairTicket ticket;ticket.frameId=frame.frameId;ticket.spaceGeneration=key.space;ticket.trackingGeneration=key.tracking;
    for(auto& eye:frame.eyes){eye.view=Identity();eye.projection=*math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,1000);}
    CHECK(AppendBodyCarriedPair(*left,key,f.a,f.Now(),frame.eyes[0]));CHECK(AppendBodyCarriedPair(*right,key,f.b,f.Now(),frame.eyes[1]));
    CHECK(graphics::BodyPropFrameMatches(frame,ticket));const auto& l=frame.eyes[0].instances[0];const auto& r=frame.eyes[1].instances[0];
    CHECK(graphics::SameBodyPropSource(l,r));CHECK(std::memcmp(&l.world,&r.world,sizeof(l.world))==0);
    CHECK(l.deadlineNs<=f.a.instances[0].deadlineNs&&r.deadlineNs<=f.a.instances[0].deadlineNs);return 0;}
int FrameAndOwnerChanges(){FixturePair f;const auto key=CarriedPairKey(f.lease);BodyCarriedPair pair;const auto original=pair.Read(key,0,f.a,f.Now());CHECK(original);
    for(unsigned n=0;n<7;++n){auto wrong=key;switch(n){case 0:++wrong.channel;break;case 1:++wrong.request;break;case 2:++wrong.owner;break;
        case 3:++wrong.frame;break;case 4:++wrong.device;break;case 5:++wrong.tracking;break;case 6:++wrong.space;break;}
        CHECK(!pair.Read(wrong,1,f.b,f.Now()));graphics::BodyPropEye eye;CHECK(!AppendBodyCarriedPair(*original,wrong,f.b,f.Now(),eye));CHECK(!eye.count);}
    auto replaced=f.b;auto inventory=std::make_shared<BodyInventoryDisplay>(*f.b.inventory);++inventory->cohort;replaced.inventory=inventory;
    replaced.instances[0].equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Carried,inventory->cohort,replaced.slotIndices[0]);
    CHECK(BodyCarriedBatchFresh(replaced,f.Now()));CHECK(!pair.Read(key,1,replaced,f.Now()));return 0;}
int OriginalExpiryAndNoNewLease(){FixturePair f;BodyCarriedPair pair;const auto key=CarriedPairKey(f.lease);CHECK(pair.Read(key,0,f.a,f.Now()));
    const auto expiry=f.a.instances[0].deadlineNs;CHECK(f.b.instances[0].deadlineNs>expiry);CHECK(!pair.Read(key,1,f.b,expiry));
    auto backwards=f.a;auto d=std::make_shared<BodyInventoryDisplay>(*f.a.inventory);--d->observedNs;backwards.inventory=d;
    CHECK(!pair.Read(key,1,backwards,f.Now()));return 0;}
int ResetAndConcurrentSeed(){FixturePair f;const auto key=CarriedPairKey(f.lease);BodyCarriedPair pair;
    CHECK(!pair.Read(key,1,f.a,f.Now()));std::shared_ptr<const BodyCarriedPairSource> a,b;
    std::thread first([&]{a=pair.Read(key,0,f.a,f.Now());}),second([&]{b=pair.Read(key,0,f.b,f.Now());});first.join();second.join();
    // If the newer source wins, an older candidate cannot validate it; every
    // successful callback nevertheless sees the same immutable source.
    CHECK(a||b);if(a&&b)CHECK(a==b);const auto current=pair.Read(key,1,f.b,f.Now());CHECK(current);
    CHECK(current==a||current==b);pair.Reset();CHECK(!pair.Read(key,1,f.b,f.Now()));
    auto next=key;++next.request;CHECK(pair.Read(next,0,f.b,f.Now()));CHECK(!pair.Read(key,1,f.b,f.Now()));return 0;}
int ActualInventoryReplacementAndRecenter(){for(unsigned variant=0;variant<3;++variant){FixturePair f;BodyCarriedPair pair;
    const auto key=CarriedPairKey(f.lease);CHECK(pair.Read(key,0,f.a,f.Now()));
    if(variant==0)f.r.f.Word(Fixture::bd+0x64,71);
    if(variant==1){++f.r.s.owner.space;++f.r.s.hand.owner.space;++f.r.s.input.spaceGeneration;}
    if(variant==2)f.r.s.input.focused=false;
    f.r.Advance();f.r.Tick();const auto display=f.r.adapter.Display(f.r.s.hand.nowNs);
    if(variant==2){CHECK(!display);continue;}
    CHECK(display);const auto batch=BuildBodyCarriedBatch(std::make_shared<const BodyInventoryDisplay>(*display),Configs(*display),
        f.r.s.input,Identity(),{},f.r.s.hand.nowNs);CHECK(batch);CHECK(!pair.Read(key,1,*batch,f.r.s.hand.nowNs));
    }return 0;}
}
int main(){for(auto fn:{ActualPairAndDistinctCounters,FrameAndOwnerChanges,OriginalExpiryAndNoNewLease,ResetAndConcurrentSeed,ActualInventoryReplacementAndRecenter})if(fn())return 1;
    std::cout<<"BodyCarriedPair: 5 actual batch/frame/comparator groups passed; no native/GPU run\n";}
