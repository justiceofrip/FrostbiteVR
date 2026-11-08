#include "Bc2CampaignProvisioningSource.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>
namespace cp=fvr::bc2::campaignProvisioning;
namespace cmd=cp::command;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
struct Fixture {
    std::int64_t now=1000000000,readCost=0,waitCost=5000000;
    unsigned reads=0,waits=0;
    cp::Request source{},out{};
    std::function<bool(Fixture&,cp::Request&)> onRead;
    Fixture(){
        source.bytes=sizeof(source);source.process.pid=42;source.process.creation=12;source.process.session=34;
        source.process.image[0]=1;source.process.executable[0]=u'G';
        source.client={1,2,3,4,5,6,7};source.source={34,5,7,8,9,10,0,11,12,{13,14,15}};
        source.sourceSequence=100;source.sourceObservedNs=now-150000000;
        out.job=999;
    }
    cmd::SourceResult acquire(std::uint64_t pid=42){
        return cmd::WaitForFreshSource(pid,out,[&](cp::Request& r){
            ++reads;r=source;const bool okay=!onRead||onRead(*this,r);now+=readCost;return okay;
        },[&]{return now;},[&](unsigned ms){CHECK(ms==5);++waits;now+=waitCost;});
    }
};
int main(){
    unsigned cases=0;
    {Fixture f;f.source.sourceObservedNs=f.now;CHECK(f.acquire()==cmd::SourceResult::Fresh);
     CHECK(f.reads==1&&!f.waits&&f.out.sourceSequence==100&&f.out.sourceObservedNs==1000000000);++cases;}
    {Fixture f;f.source.sourceObservedNs=f.now-cmd::SourceAgeNs;CHECK(f.acquire()==cmd::SourceResult::Fresh);++cases;}
    {Fixture f;f.onRead=[](Fixture& s,cp::Request& r){if(s.reads==22){++r.sourceSequence;r.sourceObservedNs=s.now-1000000;}return true;};
     CHECK(f.acquire()==cmd::SourceResult::Fresh);CHECK(f.waits==21&&f.out.sourceSequence==101);
     CHECK(f.out.sourceObservedNs==1104000000&&f.out.process==f.source.process&&f.out.client==f.source.client&&f.out.source==f.source.source);
     CHECK(!f.out.issuedNs&&!f.out.deadlineNs&&!f.out.job&&!f.out.nonce);++cases;}
    {Fixture f;CHECK(f.acquire()==cmd::SourceResult::TimedOut);CHECK(f.now==1500000000&&f.reads==100&&f.waits==100&&f.out.job==999);++cases;}
    {Fixture f;f.waitCost=0;CHECK(f.acquire()==cmd::SourceResult::TimedOut);CHECK(f.reads==cmd::SourceReadLimit&&f.waits==100);++cases;}
    {Fixture f;f.onRead=[](Fixture&,cp::Request&){return false;};CHECK(f.acquire()==cmd::SourceResult::Invalid);CHECK(f.reads==1&&!f.waits);++cases;}
    {Fixture f;f.onRead=[](Fixture& s,cp::Request& r){if(s.reads==2)return false;++r.sourceSequence;r.sourceObservedNs=s.now-150000000;return true;};
     CHECK(f.acquire()==cmd::SourceResult::Invalid);CHECK(f.reads==2&&f.waits==1&&f.out.job==999);++cases;}
    {Fixture f;f.source.sourceObservedNs=f.now+1;CHECK(f.acquire()==cmd::SourceResult::Invalid);CHECK(!f.waits);++cases;}
    {Fixture f;CHECK(f.acquire(43)==cmd::SourceResult::Invalid);CHECK(!f.waits);++cases;}
    {Fixture f;CHECK(f.acquire(0x10000002aull)==cmd::SourceResult::Invalid);CHECK(!f.waits);++cases;}
    // Every process identity and every client/server owner member is pinned,
    // including the full hash/path arrays and fields beyond pointer identity.
    const std::vector<std::function<void(cp::Request&)>> replace={
        [](auto& r){++r.process.pid;},[](auto& r){++r.process.reserved;},
        [](auto& r){++r.process.creation;},[](auto& r){++r.process.session;++r.source.session;},
        [](auto& r){++r.process.image[31];},[](auto& r){++r.process.executable[400];},
        [](auto& r){++r.client.player;},[](auto& r){++r.client.soldier;},
        [](auto& r){++r.client.weak;},[](auto& r){++r.client.weapon;},
        [](auto& r){++r.client.actorGeneration;++r.source.actorGeneration;},
        [](auto& r){++r.client.equipGeneration;},[](auto& r){++r.client.space;++r.source.space;},
        [](auto& r){++r.source.session;},[](auto& r){++r.source.actorGeneration;},
        [](auto& r){++r.source.space;},[](auto& r){++r.source.player;},
        [](auto& r){++r.source.actor;},[](auto& r){++r.source.inventory;},
        [](auto& r){++r.source.slot;},[](auto& r){++r.source.item;},[](auto& r){++r.source.data;},
        [](auto& r){++r.source.firing[0];},[](auto& r){++r.source.firing[1];},[](auto& r){++r.source.firing[2];}
    };
    for(const auto& change:replace){Fixture f;f.onRead=[&](Fixture& s,cp::Request& r){
        if(s.reads==2){change(r);++r.sourceSequence;r.sourceObservedNs=s.now;}return true;};
        const auto result=f.acquire();CHECK(result==cmd::SourceResult::Changed||result==cmd::SourceResult::Invalid);
        CHECK(f.reads==2&&f.waits==1&&f.out.job==999);++cases;}
    const std::vector<std::function<void(cp::Request&)>> invalid={
        [](auto& r){r.magic=0;},[](auto& r){++r.version;},[](auto& r){--r.bytes;},
        [](auto& r){r.reserved=1;},[](auto& r){r.sourceSequence=0;},[](auto& r){r.sourceObservedNs=0;},
        [](auto& r){r.process.creation=0;},[](auto& r){r.process.session=0;},
        [](auto& r){r.process.image={};},[](auto& r){r.process.executable[0]=0;},
        [](auto& r){r.process.executable.back()=1;},[](auto& r){r.client.weapon=0;},
        [](auto& r){r.source.firing[2]=0;},[](auto& r){r.operation=cp::Operation::Equip;},
        [](auto& r){r.job=1;},[](auto& r){r.nonce=1;},
        [](auto& r){r.issuedNs=1;},[](auto& r){r.deadlineNs=1;}
    };
    for(const auto& change:invalid){Fixture f;change(f.source);CHECK(f.acquire()==cmd::SourceResult::Invalid);CHECK(!f.waits);++cases;}
    for(unsigned mode=0;mode<4;++mode){Fixture f;f.onRead=[&](Fixture& s,cp::Request& r){if(s.reads==2){
        if(mode==0){++r.sourceSequence;} // sequence-only refresh
        if(mode==1){r.sourceObservedNs=s.now;} // timestamp-only refresh
        if(mode==2){--r.sourceSequence;r.sourceObservedNs=s.now;}
        if(mode==3){++r.sourceSequence;--r.sourceObservedNs;}
        }return true;};CHECK(f.acquire()==cmd::SourceResult::Invalid);CHECK(f.reads==2&&f.out.job==999);++cases;}
    {Fixture f;f.readCost=cmd::SourceWaitNs;f.source.sourceObservedNs=f.now;CHECK(f.acquire()==cmd::SourceResult::TimedOut);CHECK(f.reads==1&&!f.waits);++cases;}
    {Fixture f;f.waitCost=cmd::SourceWaitNs+1;CHECK(f.acquire()==cmd::SourceResult::TimedOut);CHECK(f.reads==1&&f.waits==1);++cases;}
    {Fixture f;f.waitCost=-1;CHECK(f.acquire()==cmd::SourceResult::Invalid);CHECK(f.reads==1&&f.waits==1);++cases;}
    {Fixture f;f.readCost=-1;CHECK(f.acquire()==cmd::SourceResult::Invalid);CHECK(f.reads==1&&!f.waits);++cases;}
    {Fixture f;f.source.sourceObservedNs=f.now;CHECK(f.acquire()==cmd::SourceResult::Fresh);
     const auto observed=f.out.sourceObservedNs;const auto sequence=f.out.sourceSequence;
     CHECK(cmd::ArmRequest(f.out,f.now));const auto deadline=f.out.deadlineNs;
     CHECK(deadline==f.now+2000000000&&!cmd::ArmRequest(f.out,f.now+1));
     CHECK(cmd::CanPublishRequest(f.out,f.now+cmd::SourceAgeNs));
     CHECK(!cmd::CanPublishRequest(f.out,f.now+cmd::SourceAgeNs+1));
     CHECK(!cmd::CanPublishRequest(f.out,f.now-1)&&!cmd::CanPublishRequest(f.out,deadline));
     CHECK(f.out.deadlineNs==deadline&&f.out.sourceObservedNs==observed&&f.out.sourceSequence==sequence);++cases;}
    {Fixture f;f.source.sourceObservedNs=f.now;CHECK(f.acquire()==cmd::SourceResult::Fresh);
     CHECK(!cmd::ArmRequest(f.out,f.now+cmd::SourceAgeNs+1));CHECK(!f.out.issuedNs&&!f.out.deadlineNs);++cases;}
    std::printf("PASS %u provisioning source cases (deterministic only)\n",cases);
}
