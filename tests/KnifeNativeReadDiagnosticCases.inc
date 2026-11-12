#include <vector>
#include <utility>
namespace knife_read_diagnostic_cases {
namespace pk=fvr::bc2::private_knife;
using namespace fvr::bc2;
struct Memory {
    static constexpr unsigned base=0x400000,cp=0x10000,cs=0x12000,weak=0x13000,ci=0x14000,ca=0x15000,cw=0x16000,
        data=0x17000,fd=0x18000,manager=0x20000,players=0x21000,sp=0x22000,ss=0x24000,si=0x26000,sa=0x27000,
        sw=0x28000,effects=0x29000,callback=0x2c000,soldierData=0x2d000,inventoryData=0x2e000,
        knife=0x30000,knifeServer=0x31000,knifeData=0x32000,knifeFiringData=0x33000,firing=0x34000;
    std::vector<std::byte> bytes=std::vector<std::byte>(0x30000),image=std::vector<std::byte>(0x10000);
    std::vector<std::pair<unsigned,std::size_t>> reads;
    ReloadServerBinding binding{};ReloadStateBinding state{};pk::Anchor anchor{};
    unsigned denyRead=0,denyType=0,raceAt=0,raceCount=0,raceValue=0;
    std::byte* At(unsigned at){return at>=base?image.data()+at-base:bytes.data()+at-0x10000;}
    template<class T>void Put(unsigned at,T value){std::memcpy(At(at),&value,sizeof(value));}
    Memory(){
        binding.preferredBase=base;binding.imageSize=0x10000;binding.contextRva=0x100;binding.managerVtableRva=0x200;binding.controlledGetterRva=0x300;
        state.firingVtableRva=0x500;
        auto& c=anchor.original.client;c.owner={cp,cs,weak,cw,1,2,3};c.sequence=4;c.observedNs=100;c.selectedSlot=0;
        c.config.weaponData=data;c.config.firingData=fd;
        auto& v=anchor.original.links;v.manager=manager;v.players=players;v.capacity=64;v.player=sp;v.playerTable=base+0x400;
        v.soldier=ss;v.soldierTable=base+0x600;v.soldierData=soldierData;v.inventory=si;v.inventoryData=inventoryData;
        v.begin=sa;v.end=sa+8;v.item=sw;v.effects=effects;v.effectsTable=base+0x700;v.callback=callback;
        v.clientInventory=ci;v.clientBegin=ca;v.clientEnd=ca+8;v.clientItems[0]=cw;v.clientItems[1]=knife;v.items[0]=sw;v.items[1]=knifeServer;
        anchor.knifeWeapon=knife;anchor.knifeSlot=1;anchor.knifeData=knifeData;anchor.knifeFiringData=knifeFiringData;
        Put<std::uint8_t>(cp+0xccd,8);Put(cp+0xc54,weak);Put(weak,cs+4);Put(cs+0x220,cp);Put(cp+0xc68,cs);
        Put(cs+0x248,ci);Put(cs+0x260,ca);Put(cs+0x264,ca+8);Put(ca,cw);Put(ca+4,knife);Put(ci+0x14c,1u);Put(cw+4,data);
        Put(base+0x108,manager);Put(manager,base+0x200);Put(manager+4,64u);Put(manager+0x6c,players);Put(players,sp);
        Put(sp,base+0x400);Put(base+0x430,base+0x300);Put(sp+0xc3c,ss);Put(sp+0xc6c,ss);Put(ss,base+0x600);
        Put(ss+0xc,soldierData);Put(cs+0xc,soldierData);Put(ss+0x220,sp);Put(ss+0x2b4,si);Put(si+4,inventoryData);
        Put(ss+0x2b8,sa);Put(ss+0x2bc,sa+8);Put(sa,sw);Put(sa+4,knifeServer);Put(si+0x14c,1u);
        Put(sw+4,data);Put(sw+0xc,effects);Put(effects,base+0x700);Put(effects+0x128,sp);Put(effects+0xd4,callback);Put(callback+4,fd);
        Put(knife+4,knifeData);Put(knifeServer+4,knifeData);Put(knifeData+0x84,11u);Put(knifeData+0x98,knifeFiringData);
        Put(firing,base+0x500);Put(firing+8,knifeFiringData);Put(firing+0x3c,2u);Put(firing+0x44,2u);
    }
    ReloadStateMemory View(){return {this,[](void* context,unsigned at,void* out,std::size_t count){auto& f=*static_cast<Memory*>(context);
        f.reads.emplace_back(at,count);
        if(at==f.denyRead||!count||!((at>=0x10000&&std::uint64_t(at)+count<=0x40000)||(at>=base&&std::uint64_t(at)+count<=base+0x10000)))return false;
        if(at==f.raceAt&&++f.raceCount==2)f.Put(at,f.raceValue);
        std::memcpy(out,f.At(at),count);return true;
    },[](void* context,unsigned at,const char* name){auto& f=*static_cast<Memory*>(context);f.reads.emplace_back(at,0);
        return at!=f.denyType&&((at==ss&&!std::strcmp(name,"ServerSoldierEntity"))||(at==knifeData&&!std::strcmp(name,"SoldierWeaponData")));}};}
    bool Selection(pk::Selection& selection,pk::detail::SelectionDiagnostic* diagnostic=nullptr){const auto memory=View();
        return pk::detail::CurrentSelection(pk::detail::Reader{memory},binding,base,anchor,selection,diagnostic);}
    bool State(pk::detail::CallbackStateDiagnostic* diagnostic=nullptr){const auto memory=View();std::array<std::byte,0xb0> value{};
        return pk::detail::CallbackState(pk::detail::Reader{memory},state,base,&anchor,firing,value,diagnostic);}
};
int Cases(){
    Memory baseline;pk::Selection plain,observed;CHECK(baseline.Selection(plain));const auto order=baseline.reads;baseline.reads.clear();
    pk::detail::SelectionDiagnostic d;CHECK(baseline.Selection(observed,&d));CHECK(plain==observed&&baseline.reads==order&&!d.stage&&!d.actor.predicate);
    CHECK(d.before==d.after&&d.actualWeapon==Memory::knife);
    // Every actual read/type failure refuses in the same order with/without observation.
    for(const auto& [at,count]:order){Memory a,b;if(count){a.denyRead=b.denyRead=at;}else {a.denyType=b.denyType=at;}
        pk::Selection p,q;CHECK(!a.Selection(p));CHECK(!b.Selection(q,&d));CHECK(a.reads==b.reads&&p==q&&d.stage);
        if(!std::strcmp(d.stage,"slot_read")){CHECK(d.slotRead.address==at&&!d.slotRead.known);}
        else {CHECK(d.actor.predicate&&d.actor.lastRead.address==at&&!d.actor.lastRead.result);}
    }
    {Memory f;f.Put(Memory::cp+0xc54,Memory::weak+4);pk::Selection q;CHECK(!f.Selection(q,&d));
        CHECK(d.actor.lastRead.kind==pk::detail::ReadDiagnostic::Kind::Equal&&d.actor.lastRead.known);
        CHECK(d.actor.lastRead.value==Memory::weak+4&&d.actor.lastRead.expected==Memory::weak);}
    {Memory f;f.Put<std::uint8_t>(Memory::cp+0xccd,0);pk::Selection q;CHECK(!f.Selection(q,&d));
        CHECK(!std::strcmp(d.actor.predicate,"!(playerFlags&8)")&&d.actor.lastRead.value==0&&d.actor.lastRead.bytes==1);}
    {Memory f;f.Put(Memory::sa+4,Memory::knifeServer+4);pk::Selection q;CHECK(!f.Selection(q,&d));
        CHECK(!std::strcmp(d.actor.predicate,"servers!=v.items")&&d.actor.differenceIndex==1&&d.actor.actualItem==Memory::knifeServer+4&&d.actor.expectedItem==Memory::knifeServer);}
    {Memory f;f.Put(Memory::si+0x150,32u);pk::Selection q;CHECK(!f.Selection(q,&d));
        CHECK(!std::strcmp(d.actor.predicate,"out.previous[n]>=32")&&d.actor.partial.previous[1]==32&&d.actor.lastRead.address==Memory::si+0x150);}
    {Memory f;f.Put(Memory::si+0x14c,2u);pk::Selection q;CHECK(!f.Selection(q,&d));
        CHECK(d.actor.partial.current[1]==2&&d.actor.partial.current[0]==1);}
    {Memory f;f.raceAt=Memory::si+0x150;f.raceValue=1;pk::Selection q;CHECK(!f.Selection(q,&d));
        CHECK(!std::strcmp(d.stage,"selection_changed")&&d.before.previous[1]==0&&d.after.previous[1]==1);}
    {Memory f;f.raceAt=Memory::ss+0x220;f.raceValue=Memory::sp+4;pk::Selection q;CHECK(!f.Selection(q,&d));
        CHECK(!std::strcmp(d.stage,"actor_after")&&d.actor.lastRead.address==Memory::ss+0x220&&d.actor.lastRead.value==Memory::sp+4);}
    // Actual normal89 lost strict Held when one callback's two actor reads
    // differed only by soldier flags 3->19. Actor accepts the 0x18 context
    // bits already; its final selection bracket must use that same identity.
    for(unsigned before:{3u,11u,19u,27u})for(unsigned after:{3u,11u,19u,27u}){
        Memory a,b;
        for(auto* f:{&a,&b}){
            f->anchor.original.client.soldierFlags=std::uint8_t(before);
            f->Put<std::uint8_t>(Memory::cs+0x114,std::uint8_t(before));
            f->Put(Memory::cs+0x24c,Memory::ci);
            f->raceAt=Memory::cs+0x114;f->raceValue=after;
        }
        pk::Selection p,q;CHECK(a.Selection(p)&&b.Selection(q,&d));
        CHECK(a.reads==b.reads&&p==q&&p.soldierFlags==before);
        CHECK(!d.stage&&!d.actor.predicate&&d.before.soldierFlags==before&&d.after.soldierFlags==after);
        CHECK(pk::SameSelectionIdentity(d.before,d.after));
        CHECK((d.before==d.after)==(before==after)); // Raw observation stays exact.
        CHECK(a.raceCount==2&&b.raceCount==2); // No retry or extra actor read.
        for(unsigned branch=0;branch<3;++branch)for(unsigned field=0;field<3;++field){
            auto changed=d.after;
            if(field==0)++changed.inventory[branch];
            if(field==1)++changed.current[branch];
            if(field==2)++changed.previous[branch];
            CHECK(!pk::SameSelectionIdentity(d.before,changed));
        }
    }
    for(unsigned bit:{1u,2u,4u,32u,64u,128u}){
        Memory a,b;
        for(auto* f:{&a,&b}){
            f->anchor.original.client.soldierFlags=3;
            f->Put<std::uint8_t>(Memory::cs+0x114,3);
            f->Put(Memory::cs+0x24c,Memory::ci);
            f->raceAt=Memory::cs+0x114;f->raceValue=3u^0x18u^bit;
        }
        pk::Selection p,q;CHECK(!a.Selection(p)&&!b.Selection(q,&d));CHECK(a.reads==b.reads);
        CHECK(!std::strcmp(d.stage,"actor_after"));
        CHECK(!std::strcmp(d.actor.predicate,"(out.soldierFlags&~0x18u)!=(s.client.soldierFlags&~0x18u)"));
        pk::Selection original,changed;original.soldierFlags=3;changed.soldierFlags=std::uint8_t(3u^0x18u^bit);
        CHECK(!pk::SameSelectionIdentity(original,changed));
    }
    {Memory f;f.Put(Memory::ci+0x14c,64u);pk::Selection q;CHECK(!f.Selection(q,&d));CHECK(!std::strcmp(d.stage,"slot_range")&&d.slot==64);}
    {Memory f;pk::detail::CallbackStateDiagnostic state;CHECK(f.State());const auto reads=f.reads;f.reads.clear();CHECK(f.State(&state));
        CHECK(reads==f.reads&&!state.predicate&&state.read.known&&state.current==2);}
    const std::array<std::pair<unsigned,const char*>,4> fields{{{0,"vtable"},{8,"firing_data"},{0x3c,"current_state"},{0x44,"next_state"}}};
    for(const auto& [offset,name]:fields){Memory a,b;a.Put(Memory::firing+offset,16u);b.Put(Memory::firing+offset,16u);
        pk::detail::CallbackStateDiagnostic state;CHECK(!a.State()&&!b.State(&state));CHECK(a.reads==b.reads&&!std::strcmp(state.predicate,name)&&state.read.known);}
    {Memory f;f.denyRead=Memory::firing;pk::detail::CallbackStateDiagnostic state;CHECK(!f.State(&state));
        CHECK(!std::strcmp(state.predicate,"state_read")&&!state.read.known&&state.read.address==Memory::firing);}
    std::puts("Knife native read diagnostics: unchanged read order, every read/type refusal, exact identity/value faults, 3->19 context bracket, non-context/selection races and callback state boundaries passed.");
    return 0;
}
}
