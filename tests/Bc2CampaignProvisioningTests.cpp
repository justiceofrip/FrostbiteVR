#include "Bc2CampaignProvisioning.h"
#include "Bc2CampaignProvisioningVariants.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
using namespace fvr::bc2::provisioning_candidate;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
struct Fixture {
    std::map<std::uint32_t,unsigned char> bytes;
    std::map<std::uint32_t,std::string> types;
    WeaponProvisioner service;Boundary b{};unsigned calls=0;bool allowOwner=true,assignOkay=true,wrongItem=false;
    std::function<void()> duringAssignment;
    static bool Read(void* p,std::uint32_t at,void* output,std::size_t n){
        auto& self=*static_cast<Fixture*>(p);auto* dest=static_cast<unsigned char*>(output);
        for(std::size_t k=0;k<n;++k){const auto found=self.bytes.find(at+std::uint32_t(k));if(found==self.bytes.end())return false;dest[k]=found->second;}return true;
    }
    static bool Type(void* p,std::uint32_t at,const char* type){return static_cast<Fixture*>(p)->types[at]==type;}
    static bool OwnerOkay(void* p,const Owner& o,std::int64_t){auto& f=*static_cast<Fixture*>(p);return f.allowOwner&&o==f.b.owner;}
    static bool Assign(void* p,std::uint32_t actor,std::uint32_t data,std::uint32_t slot,std::uint32_t& item){
        auto& f=*static_cast<Fixture*>(p);CHECK(actor==f.b.owner.actor&&slot==f.b.owner.slot);++f.calls;
        // Nested same hook cannot consume another call once submission started.
        CHECK(!f.service.AtSeatEntry(f.memory(),f.host(),f.b));
        if(f.duringAssignment)f.duringAssignment();
        item=0x82000+f.calls*0x100;f.put<std::uint32_t>(0x50000+slot*4,item);
        f.put(item+4,f.wrongItem?data+4:data);f.put(item+8,actor);return f.assignOkay;
    }
    Memory memory(){return {this,Read,Type};}Host host(){return {this,OwnerOkay,Assign};}
    template<class T>void put(std::uint32_t at,const T& v){const auto* p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t n=0;n<sizeof(T);++n)bytes[at+std::uint32_t(n)]=p[n];}
    void block(std::uint32_t at,const unsigned char* p,std::size_t size){for(std::size_t n=0;n<size;++n)bytes[at+std::uint32_t(n)]=p[n];}
    void text(std::uint32_t at,const char* value){block(at,reinterpret_cast<const unsigned char*>(value),std::strlen(value)+1);}
    void resource(std::uint32_t data,std::uint32_t asset,const char* name,const char* path){
        const auto firing=data+0x1000,primary=data+0x2000,nameAt=data+0x3000,pathAt=data+0x3100;
        put(data,std::uint32_t(0x145f098));put(data+0xc,nameAt);put(data+0x40,pathAt);put(data+0x98,firing);
        put(firing+0x40,primary);text(nameAt,name);text(pathAt,path);
        types[data]="SoldierWeaponData";types[firing]="WeaponFiringData";types[primary]="FiringFunctionData";
        std::array<unsigned char,0x8c> logic{};std::array<unsigned char,0x24> ammo{};
        block(primary+0xc,logic.data(),logic.size());block(primary+0x170,ammo.data(),ammo.size());
        put(primary+0xc+0x18,std::int32_t(1));put(primary+0xc+0x14,std::int32_t(1));
        put(primary+0xc+0x44,2.3f);put(primary+0xc+0x4d,std::uint8_t(1));put(primary+0x170+0x14,std::int32_t(5));
        if(asset){put(asset,std::uint32_t(0x1474554));put(asset+0x50,data);put(asset+0xc,pathAt);types[asset]="SoldierWeaponAsset";}
    }
    Fixture(){
        for(const auto& code:ProvisionCode)block(code.address,code.bytes.data(),code.bytes.size());
        b.owner={1,2,3,0x20000,0x30000,0x40000,0,0x80000,0x90000,{0x60000,0x61000,0x62000}};
        b.sequence=1;b.nowNs=1000000000;b.ownerObservedNs=b.nowNs;b.thread=11;b.returnAddress=0x697b44;b.seatDepth=1;
        std::copy_n(SeatEntryBytes,5,b.seatEntryBytes.begin());
        put(b.owner.actor,std::uint32_t(0x1424850));put(b.owner.actor+0x90,std::uint32_t(0x70000));put(b.owner.actor+0x94,std::uint32_t(0x70004));
        put(0x70000,std::uint32_t(0x71000));put(0x710e8,b.owner.player);put(b.owner.player+0xc3c,b.owner.actor);
        put(b.owner.player+0xc6c,b.owner.actor);put(b.owner.player+0xc38,std::uint32_t(0x72000));put(b.owner.actor+0x220,b.owner.player);
        put(b.owner.actor+0x2b8,std::uint32_t(0x50000));put(b.owner.actor+0x2bc,std::uint32_t(0x50008));
        put(b.owner.inventory+0x14c,b.owner.slot);put(0x50000,b.owner.item);put(b.owner.item+4,b.owner.data);put(b.owner.item+8,b.owner.actor);
        for(auto f:b.owner.firing){put(f+0x3c,std::uint32_t(2));put(f+0x44,std::uint32_t(2));}
        resource(0x90000,0,"XM8","Objects/Weapons/Handheld/US_ass_XM8/US_ass_XM8");
        resource(0xa0000,0xb0000,"M95_sp","Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95");
        service.ObserveServerUpdate(b.owner,b.thread,b.sequence,b.nowNs);
    }
    void tick(){++b.sequence;b.nowNs+=10000000;b.ownerObservedNs=b.nowNs;service.ObserveServerUpdate(b.owner,b.thread,b.sequence,b.nowNs);}
    Resource target(){const Selector s{"M95_sp","Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95",5,1,1,0,2.3f,true,false};
        const auto selected=SelectProvisioningResource(memory(),0xa0000,0xb0000,s);CHECK(selected);return *selected;}
    void prepare(){CHECK(service.Preflight(memory(),host(),b));tick();CHECK(service.Preflight(memory(),host(),b));CHECK(service.Arm(memory(),host(),b,target(),b.nowNs+1000000000));}
    void converge(std::uint32_t data){b.owner.item=service.Receipt().returnedItem;b.owner.data=data;tick();CHECK(service.ObserveConvergence(memory(),host(),b));}
};

#include "Bc2CampaignProvisioningAdmission.h"
#include "Bc2HolsterInput.h"
namespace cp=fvr::bc2::campaignProvisioning;
struct CampaignFixture:Fixture {
    cp::Service campaign;fvr::bc2::ReloadServerSnapshot source{};
    cp::ProcessIdentity process{};unsigned excluded=0,retired=0,resumed=0;
    bool settled=true,pendingExclusion=false,retireOkay=true,resumeOkay=true;
    // A completed ledger remains unchanged: replacement owns a new resource
    // generation. These host fixtures exercise the REAL service/policy types.
    CampaignFixture(){
        process.pid=42;process.creation=55;process.session=1;process.image[0]=1;process.executable[0]=u'G';
        source.client.owner={0x21000,0x31000,0x32000,0x81000,2,4,3};
        Sync();
        const auto* variant=FindProvisionVariant(0);CHECK(variant);
        for(unsigned n=0;n<variant->count;++n){const auto& word=variant->words[n];
            block(0xa2000+word.offset,reinterpret_cast<const unsigned char*>(&word.expected),word.bytes);}
    }
    cp::LifetimeHost life(){return {this,
        [](void* p,const fvr::bc2::ReloadServerSnapshot&,std::uint64_t)noexcept{
            auto& f=*static_cast<CampaignFixture*>(p);if(!f.settled)return cp::LifetimeHost::Exclusion::Refused;
            ++f.excluded;return f.pendingExclusion?cp::LifetimeHost::Exclusion::Pending:cp::LifetimeHost::Exclusion::Ready;},
        [](void* p,const fvr::bc2::ReloadServerSnapshot&,unsigned item,unsigned data,std::uint64_t,std::int64_t)noexcept{
            auto& f=*static_cast<CampaignFixture*>(p);CHECK(f.calls&&item==f.campaign.Read().native.returnedItem);
            CHECK(data==0xa0000||data==0x90000);++f.retired;return f.retireOkay;},
        [](void* p,const fvr::bc2::ReloadServerSnapshot&,std::uint64_t)noexcept{
            auto& f=*static_cast<CampaignFixture*>(p);++f.resumed;return f.resumeOkay;}};}
    void Sync(){
        source.client.sequence=b.sequence;source.client.observedNs=b.ownerObservedNs;
        source.client.config.weaponData=b.owner.data;
        source.client.branches[0].address=b.owner.firing[0];source.client.branches[1].address=b.owner.firing[1];
        source.links.player=b.owner.player;source.links.soldier=b.owner.actor;source.links.inventory=b.owner.inventory;
        source.links.slot=b.owner.slot;source.links.item=b.owner.item;source.links.firing=b.owner.firing[2];
    }
    void Configure(){CHECK(campaign.Configure(process,memory(),host(),life(),true));}
    cp::Request Request(cp::Operation op=cp::Operation::Equip,unsigned nonce=1){
        cp::Request r{};r.bytes=sizeof(r);r.operation=op;r.process=process;r.nonce=nonce;r.job=77;
        r.sourceSequence=source.client.sequence;r.issuedNs=b.nowNs;r.deadlineNs=b.nowNs+2000000000ll;
        r.sourceObservedNs=b.ownerObservedNs;r.source=b.owner;r.client=source.client.owner;
        if(op==cp::Operation::Equip){r.targetAsset=0xb0000;r.targetData=0xa0000;r.variant=0;}return r;
    }
    void Begin(){Configure();CHECK(campaign.Submit(Request(),b.nowNs));}
    void Step(){tick();Sync();campaign.ObserveServer(source,b.owner.firing[2],b.thread,b.sequence,b.nowNs);campaign.SeatEntry(source,b);}
    void Dispatch(){for(unsigned n=0;n<8;++n)Step();CHECK(calls==1);}
    void Converge(unsigned data){b.owner.item=campaign.Read().native.returnedItem;b.owner.data=data;
        source.client.owner.weapon=b.owner.item+0x1000;++source.client.owner.equipGeneration;Step();}
};
#include "Bc2CampaignProvisioningInputGate.h"
#include <atomic>
#include <latch>
#include <thread>
struct InputGateFixture {
    cp::InputGate gate;
    fvr::bc2::ReloadStateOwner owner{0x21000,0x31000,0x32000,0x81000,2,4,3};
    std::array<std::byte,fvr::bc2::InputBytes> cache{};
    static constexpr std::uint64_t nonce=9;
    static constexpr std::uint32_t cacheAddress=0x40000;
    static constexpr std::int64_t now=1000000000,deadline=1100000000;
    bool ownerOkay=true;
    InputGateFixture(){cp::InputGate::Control control(gate);CHECK(control.Begin(nonce,owner,cacheAddress));}
    fvr::bc2::HolsterSuppressionRequest Request(const cp::InputGate::Gather& gather){
        fvr::bc2::HolsterSuppressionRequest request;
        request.owner=owner;request.request=gather.Nonce();request.nativeTick=gather.NativeTick();request.cache=cacheAddress;
        request.input.owner={(std::uint64_t(owner.weak)<<32)|owner.soldier,2,7,3};
        request.input.sequence=1;request.input.observedNs=now;request.input.deadlineNs=deadline;request.input.nowNs=now;
        request.input.focused=true;request.input.tracked[1]=true;return request;
    }
    std::optional<fvr::bc2::HolsterSuppressionReceipt> Suppress(const cp::InputGate::Gather& gather){
        // Emulate original Gather writing actual action fields, then execute
        // the production suppression/readback implementation, not a fake proof.
        const std::uint32_t actions=~0u;
        std::memcpy(cache.data()+0x98,&actions,4);std::memcpy(cache.data()+0x9c,&actions,4);
        fvr::bc2::HolsterInputOverride patch;
        if(!patch.Apply(cache,Request(gather),{this,[](void* p,const fvr::bc2::HolsterSuppressionRequest& request)noexcept{
            const auto& f=*static_cast<InputGateFixture*>(p);
            return f.ownerOkay&&request.owner==f.owner&&request.cache==cacheAddress;
        }}))return {};
        return patch.Commit();
    }
    void Success(){cp::InputGate::Gather gather(gate);const auto receipt=Suppress(gather);CHECK(receipt&&gather.Commit(*receipt));}
    bool Neutral(std::int64_t at=now){cp::InputGate::Control control(gate);return control.Neutral(owner,nonce,at);}
};
unsigned TestInputGate(){unsigned groups=0;
    {InputGateFixture f;std::latch entered(1),continueUpdate(1);
        std::thread update([&]{
            cp::InputGate::Update input(f.gate);CHECK(input.Excluded());entered.count_down();continueUpdate.wait();
            // Resume attempted while this Update was paused before Gather.
            CHECK(input.Excluded());cp::InputGate::Gather gather(f.gate);
            const auto receipt=f.Suppress(gather);CHECK(receipt&&gather.Commit(*receipt));
            cp::InputGate::Control nested(f.gate);CHECK(!nested&&!nested.Resume(f.nonce));
        });
        entered.wait();{
            cp::InputGate::Control resume(f.gate);CHECK(!resume&&!resume.Resume(f.nonce));
        }
        continueUpdate.count_down();update.join();CHECK(f.Neutral());
        {cp::InputGate::Control resume(f.gate);CHECK(resume.Resume(f.nonce));}
        {cp::InputGate::Update next(f.gate);CHECK(!next.Excluded());}
        CHECK(!f.Neutral());++groups;
    }
    {InputGateFixture f;f.Success();CHECK(f.Neutral());std::latch gathering(1),finishGather(1);
        std::thread callback([&]{
            cp::InputGate::Gather gather(f.gate);gathering.count_down();finishGather.wait();
            f.ownerOkay=false;CHECK(!f.Suppress(gather)); // Later original Gather succeeds, suppression fails.
        });
        gathering.wait();{
            cp::InputGate::Control dispatch(f.gate);CHECK(!dispatch&&!dispatch.Neutral(f.owner,f.nonce,f.now));
        }
        finishGather.count_down();callback.join();CHECK(!f.Neutral());
        std::uint32_t actions=0;std::memcpy(&actions,f.cache.data()+0x9c,4);CHECK(actions==~0u);++groups;
    }
    {InputGateFixture f;f.Success();CHECK(f.Neutral());
        {cp::InputGate::Gather unmatched(f.gate); // Original without a usable local Update/cache scope.
            cp::InputGate::Control reentrant(f.gate);CHECK(!reentrant&&!reentrant.Neutral(f.owner,f.nonce,f.now));}
        CHECK(!f.Neutral());
        {cp::InputGate::Gather gather(f.gate);const auto receipt=f.Suppress(gather);CHECK(receipt);
            auto wrong=*receipt;++wrong.nativeTick;CHECK(!gather.Commit(wrong));
            wrong=*receipt;++wrong.cache;CHECK(!gather.Commit(wrong));
            wrong=*receipt;++wrong.owner.equipGeneration;CHECK(!gather.Commit(wrong));
            CHECK(gather.Commit(*receipt));CHECK(receipt->input.deadlineNs==f.deadline);
        }
        CHECK(f.Neutral(f.deadline-1));CHECK(!f.Neutral(f.deadline));CHECK(!f.Neutral(f.deadline+1));++groups;
    }
    {cp::InputGate gate;fvr::bc2::ReloadStateOwner owner{};
        {cp::InputGate::Update ordinary(gate);CHECK(!ordinary.Excluded());
            cp::InputGate::Control control(gate);CHECK(!control&&!control.Begin(1,owner,0x40000));
            CHECK(!ordinary.Excluded());}
        {cp::InputGate::Control control(gate);CHECK(control.Begin(1,owner,0x40000));}
        {cp::InputGate::Update excluded(gate);CHECK(excluded.Excluded());}++groups;
    }
    {InputGateFixture f;fvr::bc2::HolsterSuppressionReceipt old;
        {cp::InputGate::Gather first(f.gate);const auto receipt=f.Suppress(first);CHECK(receipt);old=*receipt;CHECK(first.Commit(old));}
        CHECK(f.Neutral());
        {cp::InputGate::Gather later(f.gate);CHECK(!later.Commit(old));}
        CHECK(!f.Neutral());
        {cp::InputGate::Gather outer(f.gate);const auto receipt=f.Suppress(outer);CHECK(receipt);
            {cp::InputGate::Gather inner(f.gate);const auto nested=f.Suppress(inner);CHECK(nested&&inner.Commit(*nested));}
            CHECK(!outer.Commit(*receipt)); // Outer completion invalidates any nested proof.
        }
        CHECK(!f.Neutral());++groups;
    }
    {InputGateFixture f;f.Success();std::thread callback;std::latch attempting(1);
        std::atomic<bool> originalStarted=false,dispatchComplete=false;
        {cp::InputGate::Control dispatch(f.gate);CHECK(dispatch.Neutral(f.owner,f.nonce,f.now));
            callback=std::thread([&]{attempting.count_down();cp::InputGate::Gather gather(f.gate);
                originalStarted.store(true);CHECK(dispatchComplete.load());
                const auto receipt=f.Suppress(gather);CHECK(receipt&&gather.Commit(*receipt));});
            attempting.wait();CHECK(!originalStarted.load());
            // Production keeps this same control lock through native assignment
            // and item retirement, so the admitted proof cannot be overwritten.
            dispatchComplete.store(true);
        }
        callback.join();CHECK(originalStarted.load());CHECK(f.Neutral());++groups;
    }
    return groups;
}

unsigned TestAdmissionDiagnostics(){unsigned groups=0;
    {CampaignFixture f;f.Configure();
        // Exercise exhaustion through accepted requests, not a private counter
        // shortcut. Expiry has no native assignment or input ownership effect.
        for(unsigned nonce=1;nonce<=cp::AdmissionCounters::MaxScopes;++nonce){
            const auto request=f.Request(cp::Operation::Observe,nonce);
            CHECK(f.campaign.Submit(request,f.b.nowNs));
            CHECK(f.campaign.ReadAdmission().scope==nonce);
            f.campaign.Expire(request.deadlineNs);
            CHECK(f.campaign.ReadAdmission().status==unsigned(cp::Status::Expired));
            f.b.nowNs=request.deadlineNs;f.b.ownerObservedNs=f.b.nowNs;f.Sync();
        }
        const auto request=f.Request(cp::Operation::Observe,cp::AdmissionCounters::MaxScopes+1);
        CHECK(f.campaign.Submit(request,f.b.nowNs));const auto accepted=f.campaign.ReadAdmission();
        CHECK(!accepted.scope&&accepted.frozen&&!accepted.frozenNs&&!f.campaign.AdmissionToken());
        CHECK(accepted.status==unsigned(cp::Status::Observing)&&accepted.nonce==request.nonce);
        CHECK(accepted.process==request.process&&accepted.job==request.job&&accepted.acceptedNs==f.b.nowNs);
        CHECK(accepted.issuedNs==request.issuedNs&&accepted.deadlineNs==request.deadlineNs);
        f.campaign.RecordAdmission(cp::AdmissionCounters::MaxScopes,cp::AdmissionEvent::SeatHook,f.b.nowNs);
        f.campaign.Expire(request.deadlineNs);const auto terminal=f.campaign.ReadAdmission();
        CHECK(terminal.status==unsigned(cp::Status::Expired)&&terminal.frozenNs==request.deadlineNs);
        CHECK(!terminal.scope&&terminal.frozen&&!terminal.saturatedMask);
        for(const auto count:terminal.counts)CHECK(!count);
        f.campaign.Expire(request.deadlineNs+1);f.campaign.SeatEntry(f.source,f.b);
        const auto later=f.campaign.ReadAdmission();CHECK(std::memcmp(&terminal,&later,sizeof(terminal))==0);
        // A subsequent request still reaches Ready with exhausted diagnostics.
        f.b.nowNs=request.deadlineNs;f.b.ownerObservedNs=f.b.nowNs;f.Sync();
        const auto next=f.Request(cp::Operation::Observe,cp::AdmissionCounters::MaxScopes+2);
        CHECK(f.campaign.Submit(next,f.b.nowNs));
        CHECK(f.campaign.ReadAdmission().status==unsigned(cp::Status::Observing)&&!f.campaign.ReadAdmission().frozenNs);
        for(unsigned n=0;n<8;++n)f.Step();const auto ready=f.campaign.ReadAdmission();
        CHECK(f.campaign.Read().status==cp::Status::Ready&&ready.status==unsigned(cp::Status::Ready));
        CHECK(!ready.scope&&ready.frozen&&ready.frozenNs==f.b.nowNs&&ready.nonce==next.nonce);
        f.campaign.Stop();const auto stopped=f.campaign.ReadAdmission();
        CHECK(f.campaign.Read().status==cp::Status::Stopped&&std::memcmp(&ready,&stopped,sizeof(ready))==0);
        CHECK(!f.calls&&!f.excluded&&!f.retired&&!f.resumed&&!f.campaign.BlocksInput());++groups;
    }
    {cp::AdmissionCounters counters;CHECK(!counters.Token());
        counters.Begin(100,200);const auto token=counters.Token();CHECK(token);
        for(unsigned n=0;n<cp::AdmissionCounters::Count;++n){
            const auto event=cp::AdmissionEvent(n);
            counters.Record(token,event,99);counters.Record(token,event,200);counters.Record(token,event,201);
            CHECK(counters.Read().counts[n]==0);counters.Record(token,event,100);
        }
        for(const auto count:counters.Read().counts)CHECK(count==1);
        CHECK(counters.Freeze());const auto frozen=counters.Read();CHECK(!counters.Token());
        counters.Record(token,cp::AdmissionEvent::SeatHook,101);CHECK(counters.Read().counts==frozen.counts);
        CHECK(!counters.Freeze());counters.Begin(201,300);CHECK(counters.Token()!=token);
        counters.Record(token,cp::AdmissionEvent::ServiceEntered,250);
        for(const auto count:counters.Read().counts)CHECK(!count);++groups;
    }
    {cp::AdmissionCounters counters;counters.Begin(100,200);const auto token=counters.Token();
        std::array<std::thread,4> threads;
        for(auto& thread:threads)thread=std::thread([&]{for(unsigned i=0;i<1000;++i)
            counters.Record(token,cp::AdmissionEvent::RuntimeGateBusy,150);});
        for(auto& thread:threads)thread.join();counters.Freeze();const auto snapshot=counters.Read();
        CHECK(snapshot.counts[unsigned(cp::AdmissionEvent::RuntimeGateBusy)]==cp::AdmissionCounters::Limit);
        CHECK(snapshot.saturatedMask==(1u<<unsigned(cp::AdmissionEvent::RuntimeGateBusy)));
        CHECK(!snapshot.counts[unsigned(cp::AdmissionEvent::ServiceEntered)]);++groups;
    }
    {cp::AdmissionCounters counters;counters.Begin(100,200);const auto token=counters.Token();
        std::latch ready(1),release(1);std::thread old([&]{ready.count_down();release.wait();
            counters.Record(token,cp::AdmissionEvent::SoldierMismatch,150);});
        ready.wait();counters.Freeze();const auto before=counters.Read();
        counters.Begin(100,200);release.count_down();old.join();
        CHECK(counters.Read().scope!=before.scope);CHECK(!counters.Read().counts[unsigned(cp::AdmissionEvent::SoldierMismatch)]);
        for(unsigned n=2;n<cp::AdmissionCounters::MaxScopes;++n)counters.Begin(100,200);
        CHECK(counters.Token());counters.Begin(100,200);CHECK(!counters.Token()&&!counters.Read().scope);++groups;
    }
    {CampaignFixture f;f.Configure();f.b.ownerObservedNs-=82222100;f.Sync();
        const auto request=f.Request(cp::Operation::Observe);
        CHECK(f.campaign.Submit(request,f.b.nowNs));const auto token=f.campaign.AdmissionToken();
        f.campaign.ObserveServer(f.source,f.b.owner.firing[2],f.b.thread,f.b.sequence,f.b.nowNs);
        f.campaign.RecordAdmission(token,cp::AdmissionEvent::SeatHook,f.b.nowNs);
        f.campaign.RecordAdmission(token,cp::AdmissionEvent::RuntimeGateBusy,f.b.nowNs);
        f.campaign.Expire(request.deadlineNs+39103900);
        const auto before=cp::ToWire(f.campaign.Read());const auto diagnostic=f.campaign.ReadAdmission();
        CHECK(before.status==unsigned(cp::Status::Expired)&&!before.preflight&&!before.nativeCalls);
        CHECK(before.current==fvr::bc2::ReloadStateOwner{}&&before.boundary==0);
        CHECK(diagnostic.frozen&&diagnostic.status==before.status&&diagnostic.deadlineNs==request.deadlineNs);
        CHECK(diagnostic.nonce==request.nonce&&diagnostic.job==request.job&&diagnostic.process==request.process);
        CHECK(diagnostic.counts[unsigned(cp::AdmissionEvent::SeatHook)]==1);
        CHECK(!diagnostic.counts[unsigned(cp::AdmissionEvent::ServiceEntered)]);
        // Later valid-looking source + ServerAge must not rewrite the retained
        // actual79/09 zero-current Expired receipt or its diagnostic snapshot.
        f.b.nowNs=request.deadlineNs+28000000000ll;f.b.ownerObservedNs=f.b.nowNs;f.Sync();f.campaign.SeatEntry(f.source,f.b);
        const auto after=cp::ToWire(f.campaign.Read());CHECK(std::memcmp(&before,&after,sizeof(before))==0);
        const auto afterDiagnostic=f.campaign.ReadAdmission();
        CHECK(std::memcmp(&diagnostic,&afterDiagnostic,sizeof(diagnostic))==0);
        f.campaign.RecordAdmission(token,cp::AdmissionEvent::ServiceEntered,request.issuedNs+1);
        CHECK(f.campaign.ReadAdmission().counts==diagnostic.counts);
        CHECK(!f.campaign.Submit(request,f.b.nowNs));CHECK(f.campaign.ReadAdmission().nonce==request.nonce);
        CHECK(f.campaign.Submit(f.Request(cp::Operation::Observe,2),f.b.nowNs));
        CHECK(!f.campaign.ReadAdmission().frozen&&f.campaign.ReadAdmission().nonce==2);
        for(const auto count:f.campaign.ReadAdmission().counts)CHECK(!count);CHECK(!f.calls);++groups;
    }
    {CampaignFixture f;f.Configure();CHECK(f.campaign.Submit(f.Request(cp::Operation::Observe),f.b.nowNs));
        auto nested=f.b;nested.seatDepth=2;const auto before=cp::ToWire(f.campaign.Read());
        f.campaign.SeatEntry(f.source,nested);const auto after=cp::ToWire(f.campaign.Read());
        CHECK(std::memcmp(&before,&after,sizeof(before))==0);
        CHECK(f.campaign.ReadAdmission().counts[unsigned(cp::AdmissionEvent::NestedDepth)]==1);
        CHECK(!f.campaign.ReadAdmission().counts[unsigned(cp::AdmissionEvent::ServiceEntered)]);
        for(unsigned n=0;n<8;++n)f.Step();CHECK(f.campaign.Read().status==cp::Status::Ready);
        const auto complete=cp::ToWire(f.campaign.Read());const auto diagnostic=f.campaign.ReadAdmission();
        CHECK(diagnostic.frozen&&diagnostic.counts[unsigned(cp::AdmissionEvent::ServiceEntered)]==8);
        f.Step();const auto later=cp::ToWire(f.campaign.Read());CHECK(std::memcmp(&complete,&later,sizeof(complete))==0);
        CHECK(f.campaign.ReadAdmission().counts==diagnostic.counts&&!f.calls);++groups;
    }
    {CampaignFixture f;f.Begin();f.settled=false;for(unsigned n=0;n<8;++n)f.Step();
        CHECK(f.campaign.Read().status==cp::Status::Refused);const auto before=cp::ToWire(f.campaign.Read());
        f.Step();const auto after=cp::ToWire(f.campaign.Read());CHECK(std::memcmp(&before,&after,sizeof(before))==0);
        CHECK(f.campaign.ReadAdmission().frozen&&!f.calls);++groups;
    }
    {CampaignFixture f;f.Begin();f.Dispatch();f.Converge(0xa0000);
        const auto equipped=cp::ToWire(f.campaign.Read());const auto equipDiagnostic=f.campaign.ReadAdmission();
        CHECK(equipDiagnostic.frozen&&equipDiagnostic.status==unsigned(cp::Status::Equipped));
        f.Step();const auto later=cp::ToWire(f.campaign.Read());CHECK(std::memcmp(&equipped,&later,sizeof(later))==0);
        CHECK(f.campaign.Submit(f.Request(cp::Operation::Restore,2),f.b.nowNs));f.Step();f.Converge(0x90000);
        const auto restored=cp::ToWire(f.campaign.Read());const auto restoreDiagnostic=f.campaign.ReadAdmission();
        CHECK(restoreDiagnostic.frozen&&restoreDiagnostic.status==unsigned(cp::Status::Restored));
        CHECK(restoreDiagnostic.scope!=equipDiagnostic.scope&&restoreDiagnostic.nonce==2);
        f.Step();const auto last=cp::ToWire(f.campaign.Read());CHECK(std::memcmp(&restored,&last,sizeof(last))==0);
        CHECK(f.calls==2&&f.retired==2&&f.resumed==2);++groups;
    }
    return groups;
}

int main(){unsigned groups=TestInputGate()+TestAdmissionDiagnostics();
    {CampaignFixture f;f.Begin();
        f.duringAssignment=[&]{
            auto nested=f.b;nested.seatDepth=2;nested.nowNs+=3000000000ll;
            const auto before=f.campaign.Read().status;
            f.campaign.SeatEntry(f.source,nested);
            CHECK(f.campaign.Read().status==before);
        };
        f.Dispatch();CHECK(f.calls==1&&f.retired==1);
        f.Converge(0xa0000);CHECK(f.campaign.Read().status==cp::Status::Equipped);++groups;
    }
    {CampaignFixture f;f.Begin();f.pendingExclusion=true;for(unsigned n=0;n<8;++n)f.Step();
        CHECK(!f.calls&&f.campaign.BlocksInput()&&f.campaign.Read().status==cp::Status::Excluded);
        f.pendingExclusion=false;f.Step();CHECK(f.calls==1&&f.retired==1);++groups;}
    {using namespace fvr::bc2;using namespace fvr::interaction;
        const ReloadStateOwner owner{0x21000,0x31000,0x32000,0x81000,2,4,3};const std::int64_t now=1000000000;
        AmmoResourceView v;v.identity.owner=owner;v.binding.owner=owner;
        v.binding.context={{owner.soldier,owner.actorGeneration,owner.weapon,9},owner.equipGeneration,owner.space};
        v.binding.inventory=0x41000;v.binding.switching=0x42000;v.binding.data=0x90000;
        v.binding.observedNs=now;v.binding.deadlineNs=now+100000000;
        v.snapshot={v.binding.context,1,now,now+100000000,{5,25,5},true};
        v.phase=AmmunitionLedgerPhase::Ready;v.requestState=AmmoResourceRequestState::Completed;
        AmmunitionReceipt completed{};completed.command.context=v.binding.context;completed.authorityInvocation=37;
        completed.before={0,30,5};completed.after={5,25,5};completed.authorityVerified=completed.copiesVerified=true;
        v.receipt=completed;
        CHECK(cp::SettledItem(v,owner,now,{},false));CHECK(v.receipt==completed);
        Bc2NativeCycleView cycle;cycle.blocksFire=true;CHECK(!cp::SettledItem(v,owner,now,cycle,true));
        cycle.blocksFire=false;cycle.heldSuspended=true;CHECK(!cp::SettledItem(v,owner,now,cycle,true));
        v.originalState=MagazineResourceState::Held;CHECK(!cp::SettledItem(v,owner,now,{},false));
        v.originalState=MagazineResourceState::Returned;v.requestState=AmmoResourceRequestState::Uncertain;
        CHECK(!cp::SettledItem(v,owner,now,{},false));CHECK(v.receipt==completed);++groups;}
    {using namespace fvr::bc2;std::array<std::byte,InputBytes> cache{};
        std::uint32_t bits=~0u;std::memcpy(cache.data()+0x98,&bits,4);std::memcpy(cache.data()+0x9c,&bits,4);
        HolsterSuppressionRequest r;r.owner={0x21000,0x31000,0x32000,0x81000,2,4,3};
        r.input.owner={(std::uint64_t(r.owner.weak)<<32)|r.owner.soldier,2,7,3};
        r.input.sequence=1;r.input.observedNs=1000000000;r.input.deadlineNs=1100000000;r.input.nowNs=1200000000;
        r.input.focused=true;r.input.tracked[1]=true;r.request=9;r.nativeTick=1;r.cache=0x40000;
        HolsterInputOverride patch;CHECK(patch.Apply(cache,r,{nullptr,[](void*,const HolsterSuppressionRequest&)noexcept{return true;}}));
        const auto receipt=patch.Commit();CHECK(receipt);CHECK(!HolsterSuppressionCurrent(*receipt,r));
        std::uint32_t after=0;std::memcpy(&after,cache.data()+0x9c,4);CHECK((after&((1u<<1)|(1u<<4)|(1u<<5)|(1u<<6)))==0);
        CHECK(r.input.deadlineNs==1100000000);++groups;}
    {CampaignFixture f;CHECK(!f.campaign.Configure(f.process,f.memory(),f.host(),f.life()));
        CHECK(!f.campaign.Submit(f.Request(),f.b.nowNs));CHECK(!f.calls);++groups;}
    {CampaignFixture f;f.Begin();for(unsigned n=0;n<7;++n)f.Step();CHECK(!f.calls&&f.campaign.Read().preflight==7);
        f.Step();CHECK(f.calls==1&&f.retired==1&&f.campaign.BlocksInput());
        CHECK(f.campaign.Read().status==cp::Status::AwaitingReplication);f.Converge(0xa0000);
        CHECK(f.campaign.Read().status==cp::Status::Equipped&&!f.campaign.BlocksInput());
        CHECK(f.campaign.Submit(f.Request(cp::Operation::Restore,2),f.b.nowNs));f.Step();CHECK(f.calls==2&&f.retired==2);
        f.Converge(0x90000);CHECK(f.campaign.Read().status==cp::Status::Restored&&!f.campaign.BlocksInput());
        CHECK(!f.campaign.Submit(f.Request(cp::Operation::Restore,3),f.b.nowNs));++groups;}
    {CampaignFixture f;f.Begin();f.settled=false;for(unsigned n=0;n<8;++n)f.Step();
        CHECK(!f.calls&&!f.excluded&&!f.retired&&f.campaign.Read().status==cp::Status::Refused);++groups;}
    {CampaignFixture f;f.Begin();f.b.nativeFiringDepth=1;for(unsigned n=0;n<8;++n)f.Step();
        CHECK(!f.calls&&!f.campaign.Read().preflight);CHECK(f.campaign.Read().boundary==BoundaryFailure::NativeDepth);++groups;}
    {CampaignFixture f;f.Begin();f.assignOkay=false;f.Dispatch();CHECK(f.campaign.Read().status==cp::Status::Quarantined);
        CHECK(f.campaign.BlocksInput()&&!f.retired);for(unsigned n=0;n<20;++n)f.Step();CHECK(f.calls==1);
        CHECK(!f.campaign.Submit(f.Request(cp::Operation::Equip,2),f.b.nowNs));f.campaign.Stop();CHECK(f.campaign.BlocksInput());++groups;}
    {CampaignFixture f;f.Begin();f.Dispatch();const auto at=f.b.ownerObservedNs;
        f.b.owner.item=f.campaign.Read().native.returnedItem;f.b.owner.data=0xa0000;
        f.tick();f.b.ownerObservedNs=at;f.Sync();f.campaign.ObserveServer(f.source,f.b.owner.firing[2],f.b.thread,f.b.sequence,f.b.nowNs);
        f.campaign.SeatEntry(f.source,f.b);CHECK(f.campaign.BlocksInput());f.campaign.Expire(f.b.nowNs+3000000000ll);
        CHECK(f.campaign.Read().status==cp::Status::Quarantined&&f.calls==1);++groups;}
    {CampaignFixture f;f.Configure();auto r=f.Request();++r.process.creation;CHECK(!f.campaign.Submit(r,f.b.nowNs));
        r=f.Request();++r.process.image[0];CHECK(!f.campaign.Submit(r,f.b.nowNs));
        r=f.Request();r.process.executable[0]=u'C';CHECK(!f.campaign.Submit(r,f.b.nowNs));++groups;}
    {CampaignFixture f;f.Begin();++f.source.client.owner.equipGeneration;f.Step();CHECK(!f.calls&&f.campaign.Read().status==cp::Status::Refused);++groups;}
    {CampaignFixture f;f.Begin();f.bytes[0xa2000+12]^=1;for(unsigned n=0;n<8;++n)f.Step();
        CHECK(!f.calls&&f.campaign.Read().status==cp::Status::Refused);++groups;}
    {CampaignFixture f;f.Begin();f.retireOkay=false;f.Dispatch();CHECK(f.campaign.Read().status==cp::Status::Quarantined);
        CHECK(f.calls==1&&f.retired==1&&!f.resumed);++groups;}
    {CampaignFixture f;f.Begin();f.b.thread=12;f.Step();f.b.thread=11;for(unsigned n=0;n<8;++n)f.Step();
        CHECK(!f.calls&&f.campaign.Read().boundary==BoundaryFailure::ThreadConflict);++groups;}
    {CampaignFixture f;f.Begin();f.bytes[0x6bcf80]^=1;for(unsigned n=0;n<8;++n)f.Step();CHECK(!f.calls);++groups;}
    for(const bool restore:{false,true})for(const std::int64_t late:{0ll,1ll}){
        CampaignFixture f;f.Begin();f.Dispatch();
        std::int64_t deadline=3000000000ll;
        if(restore){
            f.Converge(0xa0000);const auto request=f.Request(cp::Operation::Restore,2);
            deadline=request.deadlineNs;CHECK(f.campaign.Submit(request,f.b.nowNs));f.Step();CHECK(f.calls==2);
        }
        const auto resumes=f.resumed;const auto calls=f.calls;
        // Step adds 10ms and supplies fresh, fully converged source/owner data.
        // No worker Expire call is made before this exact/late seat callback.
        f.b.nowNs=deadline+late-10000000;f.Converge(restore?0x90000:0xa0000);
        CHECK(f.campaign.Read().status==cp::Status::Quarantined&&f.campaign.BlocksInput());
        CHECK(f.resumed==resumes&&f.calls==calls);
        CHECK(!f.campaign.Read().native.clientServerConverged);++groups;
    }
    std::printf("PASS %u campaign provisioning groups; real service/policy, native assignment fixture; no native acceptance\n",groups);
}
