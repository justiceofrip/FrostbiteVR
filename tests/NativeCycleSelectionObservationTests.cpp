#include "Bc2NativeCycleSelectionObservation.h"
#include "Bc2HolsterInput.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
using namespace fvr::bc2;
namespace so=fvr::bc2::cycleSelectionObservation;
namespace {
std::unique_ptr<so::Snapshot> Capture(const so::Journal& journal){
    auto snapshot=std::make_unique<so::Snapshot>();journal.Capture(*snapshot);return snapshot;
}
struct Fixture {
    std::unique_ptr<so::Journal> journal=std::make_unique<so::Journal>();so::ThreadState thread{};
    so::Owner old{0x10000,0x20000,0x30000,0x20000,0x40000,0,0x50000,0x60000,{0x70000,0x71000,11,{}}};
    so::Owner next=old;so::Pair first{0x60000,0x600d0},second{0x600d0,0x60000};
    ReloadStateOwner owner{0x10000,0x20000,0x30000,0x70000,2,3,4};
    so::Cycle origin{{2,9},{owner,{0x72000,0x73000,0x74000},0x75000,0x76000,0x77000},5,6,7,true};
    std::uint64_t issue=0;
    Fixture(){next.equipment={0x80000,0x81000,12,{}};journal->Configure(1000);journal->Start();}
    so::Selected TargetSelection()const {
        return {0x90000,0,0,0,{0x82000,1,0x83000,0x83008,next.equipment.weapon,next.equipment}};
    }
    void Adopt(const so::Cycle& c,const ReloadStateOwner& nativeOwner){
        origin=c;owner=nativeOwner;old.player=owner.player;old.soldier=old.controlled=owner.soldier;old.weak=owner.weak;
        old.equipment.weapon=owner.weapon;next=old;next.equipment={0x80000,0x81000,12,{}};
    }
    so::Row AttemptRow(std::uint64_t update)const {
        so::Row r{};r.before=old;r.pairBefore=first;r.cycle=origin;r.nativeOwner=owner;r.update=update;r.inputSequence=40;r.thread=1;
        r.action=7;r.value=0x3f800000;r.targetWeapon=next.equipment.weapon;r.targetSlot=1;
        r.command={r.action,r.targetWeapon,r.targetSlot,0x82000,12,0,0,false};
        r.origin={origin,old,first,owner,r.command,update,r.inputSequence,r.thread,10};return r;
    }
    void Issue(bool committed=true,bool refused=false,std::optional<so::Cycle> live={}){
        auto u=journal->Begin(so::Kind::Update,thread,10,1,old.player,0);
        auto r=AttemptRow(u.row.id);if(live)r.cycle=*live;
        journal->Attempt(r,11);issue=journal->Issue();journal->IssueRow(r);
        if(committed||refused){r.kind=refused?so::Kind::Refused:so::Kind::Committed;r.id=0;r.begin=r.end=12;
            r.commitLive=r.cycle;r.after=r.before;r.pairAfter=r.pairBefore;r.cacheValue=r.value;journal->Append(r);}
        u.row.issue=issue;u.row.action=7;u.row.before=u.row.after=old;u.row.pairBefore=first;u.row.pairAfter=second;
        u.row.currentValue=committed?0x3f800000:0;u.row.returned=true;journal->End(u,thread,13,true);
    }
    void Consumer(bool child=true,bool childNormal=true,unsigned otherSlots=0,unsigned queueBefore=0,unsigned queueAfter=0){
        auto o=journal->Begin(so::Kind::Outer,thread,14,1,old.controlled,0);
        o.row.queueBeforeRead=o.row.queueAfterRead=true;
        o.row.queue=queueBefore;o.row.queueAfter=queueAfter;
        o.row.before=old;o.row.after=next;o.row.pairBefore=o.row.pairAfter=second;journal->PairIds(o.row);
        if(child){auto i=journal->Begin(so::Kind::Inner,thread,15,1,old.controlled,0x400000+so::InnerCallerRva);++o.childCount;
            i.row.queueBeforeRead=i.row.queueAfterRead=true;
            i.row.before=old;i.row.after=next;i.row.pairBefore=i.row.pairAfter=second;i.row.actualR=old.pair;i.row.slot=old.entrySlot;
            journal->PairIds(i.row);i.row.returned=childNormal;journal->End(i,thread,16,childNormal);}
        for(unsigned n=0;n<otherSlots;++n){
            auto i=journal->Begin(so::Kind::Inner,thread,16,1,old.controlled,0x400000+so::InnerCallerRva);++o.childCount;
            i.row.queueBeforeRead=i.row.queueAfterRead=true;
            i.row.before=old;i.row.after=next;i.row.pairBefore=i.row.pairAfter=second;i.row.actualR=0xa0000+n*0x1000;
            i.row.slot=n+1;i.row.matchingSlot=false;i.row.returned=true;journal->End(i,thread,16,true);
        }
        o.row.returned=true;o.row.selected=TargetSelection();journal->End(o,thread,17,true);
    }
    void Neutral(bool selected=true){
        auto u=journal->Begin(so::Kind::Update,thread,18,1,old.player,0);
        if(selected){so::Row r{};r.kind=so::Kind::Selected;r.begin=r.end=19;r.after=next;r.selected=TargetSelection();
            r.nativeOwner=owner;r.nativeOwner.weapon=next.equipment.weapon;++r.nativeOwner.equipGeneration;
            r.cycle=origin;r.cycle.native.owner=r.nativeOwner;r.cycle.slot={3,1};r.cycle.cycle=1;r.cycle.shot=0;r.cycle.guardEpoch=0;
            journal->Append(r);}
        u.row.before=u.row.after=next;u.row.pairBefore=second;u.row.pairAfter=first;u.row.action=7;
        u.row.selected=TargetSelection();
        u.row.previousValue=0x3f800000;u.row.returned=true;journal->End(u,thread,20,true);
        auto v=journal->Begin(so::Kind::Update,thread,21,1,old.player,0);
        v.row.before=v.row.after=next;v.row.pairBefore=first;v.row.pairAfter=second;v.row.action=7;v.row.returned=true;
        v.row.selected=TargetSelection();
        journal->End(v,thread,22,true);
    }
    std::unique_ptr<so::Snapshot> Complete(){Issue();Consumer();Neutral();return Capture(*journal);}
};
so::Row* Find(so::Snapshot& s,so::Kind kind,unsigned which=0){for(unsigned n=0;n<s.count;++n)if(s.rows[n].kind==kind){if(!which--)return &s.rows[n];}return nullptr;}
int CompleteJoin(){
    Fixture f;auto s=f.Complete();const auto result=so::Analyze(*s);
    CHECK(result.result==so::Result::Joined&&result.observedChainComplete&&result.issue==f.issue&&result.generation==1);
    CHECK(result.outer&&result.inner&&result.selected&&result.selectedBank&&result.previousPublication==2&&result.absentPublication==3);
    CHECK(s->closed&&s->count==8&&!s->dropped&&!s->capped);
    CHECK(f.journal->Drained(true,0)&&!f.journal->Drained(false,0)&&!f.journal->Drained(true,1));
    CHECK(!f.journal->Start());auto t=f.journal->Begin(so::Kind::Outer,f.thread,24,1,1,1);CHECK(!t.active);
    return 0;
}
int ExactGenerations(){
    Fixture f;auto s=f.Complete();
    Find(*s,so::Kind::Outer)->currentId=2;Find(*s,so::Kind::Inner)->currentId=2;
    CHECK(so::Analyze(*s).result==so::Result::PendingConsumer); // g+1 is not g.
    Fixture g;auto a=g.Complete();Find(*a,so::Kind::Update,1)->previousId=0;
    CHECK(so::Analyze(*a).result==so::Result::PendingNeutral);
    Fixture h;auto b=h.Complete();Find(*b,so::Kind::Update,2)->previousId=1;
    CHECK(so::Analyze(*b).result==so::Result::PendingNeutral);
    Fixture i;auto c=i.Complete();c->active[2]=1;
    CHECK(so::Analyze(*c).result!=so::Result::Joined);
    return 0;
}
int RefusedAndMissing(){
    Fixture f;f.Issue(false,true);auto s=Capture(*f.journal);
    CHECK(so::Analyze(*s).result==so::Result::Refused&&s->closed);
    CHECK(!Find(*s,so::Kind::Committed));
    Fixture g;g.Issue(false,false);auto a=Capture(*g.journal);
    CHECK(so::Analyze(*a).result==so::Result::Attempted);
    Fixture h;h.Issue();h.Consumer(false);h.Neutral();auto b=Capture(*h.journal);
    CHECK(so::Analyze(*b).result==so::Result::Rejected);
    Fixture i;i.Issue();i.Consumer(true,false);i.Neutral();auto c=Capture(*i.journal);
    CHECK(so::Analyze(*c).result==so::Result::Rejected&&(so::Analyze(*c).reasons&so::Abnormal));return 0;
}
int OwnerPairAndSlot(){
    for(unsigned test=0;test<8;++test){Fixture f;auto s=f.Complete();
        switch(test){
        case 0:Find(*s,so::Kind::Inner)->actualR+=4;break;
        case 1:Find(*s,so::Kind::Inner)->before.player+=4;break;
        case 2:Find(*s,so::Kind::Inner)->slot+=1;break;
        case 3:Find(*s,so::Kind::Committed)->cycle.slot.generation+=1;break;
        case 4:Find(*s,so::Kind::Selected)->cycle.slot=f.origin.slot;break;
        case 5:Find(*s,so::Kind::Outer)->reasons|=so::Queue;break;
        case 6:Find(*s,so::Kind::Inner)->reasons|=so::Fallback;break;
        case 7:Find(*s,so::Kind::Update)->currentValue=0;break;
        }
        CHECK(so::Analyze(*s).result==so::Result::Rejected);
    }return 0;
}
int SelectedPending(){
    for(unsigned test=0;test<5;++test){Fixture f;auto s=f.Complete();auto* r=Find(*s,so::Kind::Selected);
        switch(test){case 0:r->cycle.present=false;break;case 1:r->cycle.native.owner.weapon+=4;break;
        case 2:r->after.equipment.persistence+=1;break;case 3:r->selected.current=1;break;case 4:r->begin=16;break;}
        CHECK(so::Analyze(*s).result==so::Result::Joined&&!so::Analyze(*s).selectedBank);
    }
    Fixture plain;plain.Issue();plain.Consumer();plain.Neutral(false);
    auto a=Capture(*plain.journal);
    CHECK(so::Analyze(*a).result==so::Result::Joined&&!so::Analyze(*a).selectedBank);
    Fixture stale;auto b=stale.Complete();Find(*b,so::Kind::Update,2)->selected.current=1;
    CHECK(so::Analyze(*b).result==so::Result::PendingSelected);
    Fixture replaced;auto c=replaced.Complete();Find(*c,so::Kind::Update,2)->after.equipment.persistence+=1;
    CHECK(so::Analyze(*c).result==so::Result::PendingSelected);
    Fixture delayed;auto d=delayed.Complete();Find(*d,so::Kind::Outer)->after=delayed.old;
    // An index alone cannot override a different freshly resolved weapon.
    CHECK(so::Analyze(*d).result==so::Result::PendingConsumer);
    return 0;
}
int Publications(){
    Fixture f;f.Issue();f.Consumer();
    auto u=f.journal->Begin(so::Kind::Update,f.thread,18,1,f.old.player,0);
    u.row.before=u.row.after=f.next;u.row.pairBefore=u.row.pairAfter=f.second;u.row.returned=true;
    f.journal->End(u,f.thread,20,true);auto s=Capture(*f.journal);
    CHECK(so::Analyze(*s).result==so::Result::Rejected&&(so::Analyze(*s).reasons&so::NotSwapped));
    Fixture g;g.Issue();auto v=g.journal->Begin(so::Kind::Update,g.thread,18,1,g.old.player,0);
    v.row.before=v.row.after=g.next;v.row.before.pair+=0x1000;v.row.after.pair=v.row.before.pair;
    v.row.pairBefore=g.second;v.row.pairAfter=g.first;v.row.returned=true;g.journal->End(v,g.thread,20,true);
    auto a=Capture(*g.journal);CHECK(so::Analyze(*a).result==so::Result::Rejected);
    Fixture duplicate;auto b=duplicate.Complete();Find(*b,so::Kind::Update,1)->publication=1;
    CHECK(so::Analyze(*b).result==so::Result::Rejected);return 0;
}
int MultiSlotAndForeign(){
    Fixture f;f.Issue();f.Consumer(true,true,2);f.Neutral(false);
    auto s=Capture(*f.journal);CHECK(so::Analyze(*s).result==so::Result::Joined);
    CHECK(Find(*s,so::Kind::Outer)->childCount==3);
    Fixture g;g.Issue();
    auto foreign=g.journal->Begin(so::Kind::Outer,g.thread,13,2,0xb0000,0);
    foreign.row.before=foreign.row.after=g.old;foreign.row.pairBefore=foreign.row.pairAfter=g.second;
    foreign.row.foreign=true;foreign.row.relevant=false;foreign.row.returned=true;g.journal->End(foreign,g.thread,13,true);
    g.Consumer();g.Neutral(false);auto a=Capture(*g.journal);
    CHECK(so::Analyze(*a).result==so::Result::Joined);
    // A second child claiming the local slot cannot masquerade as one reader.
    auto* child=Find(*s,so::Kind::Inner,1);child->matchingSlot=true;child->slot=0;child->actualR=f.old.pair;
    CHECK(so::Analyze(*s).result==so::Result::Rejected);return 0;
}
int OverlapAndCap(){
    Fixture f;f.Issue();
    auto o=f.journal->Begin(so::Kind::Outer,f.thread,14,1,f.old.controlled,0);
    so::ThreadState other{};auto concurrent=f.journal->Begin(so::Kind::Update,other,15,2,f.old.player,0);
    concurrent.row.returned=true;f.journal->End(concurrent,other,16,true);
    o.row.returned=true;f.journal->End(o,f.thread,17,true);
    auto s=Capture(*f.journal);CHECK(so::Analyze(*s).result==so::Result::Rejected);
    CHECK(so::Analyze(*s).reasons&so::Overlap);
    Fixture g;g.Issue();for(unsigned n=0;n<so::Capacity;++n){so::Row r{};r.kind=so::Kind::Selected;g.journal->Append(r);}
    auto a=Capture(*g.journal);CHECK(a->capped&&a->dropped&&a->closed&&so::Analyze(*a).result==so::Result::Rejected);
    Fixture h;h.Issue();CHECK(!h.journal->Capturing(20011));auto b=Capture(*h.journal);CHECK(b->expired&&b->closed);
    return 0;
}
int InventoryNamespace(){
    Fixture different;auto a=different.Complete();
    CHECK(Find(*a,so::Kind::Outer)->selected.current==0&&Find(*a,so::Kind::Attempted)->targetSlot==1);
    CHECK(so::Analyze(*a).result==so::Result::Joined);
    // Coincident entry index1 cannot qualify an inventory weapon at slot0.
    Fixture coincident;auto b=coincident.Complete();
    for(unsigned n=0;n<b->count;++n){b->rows[n].selected.current=1;b->rows[n].selected.inventory.index=0;}
    CHECK(so::Analyze(*b).result==so::Result::PendingConsumer&&!so::Analyze(*b).observedChainComplete);
    for(unsigned where=0;where<2;++where)for(unsigned fault=0;fault<10;++fault){Fixture f;auto s=f.Complete();
        auto& v=(where?Find(*s,so::Kind::Update,2):Find(*s,so::Kind::Outer))->selected.inventory;
        switch(fault){
        case 0:v.inventory+=4;break;case 1:v.index=0;break;case 2:v.weapon+=4;break;
        case 3:v.equipment.weapon+=4;break;case 4:v.equipment.data+=4;break;case 5:++v.equipment.persistence;break;
        case 6:v.end=v.begin;break;case 7:v.end=v.begin+129;break;case 8:v.index=2;break;case 9:v.begin=4;break;
        }
        CHECK(so::Analyze(*s).result==(where?so::Result::PendingSelected:so::Result::PendingConsumer));
        CHECK(!so::Analyze(*s).observedChainComplete);
    }
    return 0;
}
struct InventoryMemory {
    std::vector<std::byte> bytes=std::vector<std::byte>(0xa0000);unsigned failedAddress=0;
    void Word(unsigned at,unsigned value){std::memcpy(bytes.data()+at,&value,4);}
    explicit InventoryMemory(const so::Owner& owner){
        Word(owner.soldier+0x248,0x82000);Word(owner.soldier+0x24c,0x82000);Word(0x82000+0x14c,1);
        Word(owner.soldier+0x260,0x83000);Word(owner.soldier+0x264,0x83008);Word(0x83004,owner.equipment.weapon);
        Word(owner.equipment.weapon+4,owner.equipment.data);Word(owner.equipment.data+0x64,owner.equipment.persistence);
        Word(owner.equipment.data+0xc,0x84000);std::memcpy(bytes.data()+0x84000,owner.equipment.asset.data(),owner.equipment.asset.size());
    }
    WeaponModeMemory Memory(){return {this,[](void* context,unsigned at,void* dst,std::size_t n){
        auto& m=*static_cast<InventoryMemory*>(context);
        if(at==m.failedAddress||std::uint64_t(at)+n>m.bytes.size())return false;
        std::memcpy(dst,m.bytes.data()+at,n);return true;
    },[](void*,unsigned at,const char* type){return at==0x81000&&!std::strcmp(type,"SoldierWeaponData");}};}
};
int InventoryReads(){
    Fixture f;std::memcpy(f.next.equipment.asset.data(),"XM8_sp_s",9);
    for(unsigned path=0;path<2;++path){InventoryMemory m(f.next);m.bytes[f.next.soldier+0x114]=std::byte(path);
        so::InventorySelection read{};CHECK(so::ReadInventorySelection(m.Memory(),f.next,read));
        CHECK(read==f.TargetSelection().inventory);
    }
    for(unsigned fault=0;fault<13;++fault){InventoryMemory m(f.next);auto owner=f.next;so::InventorySelection out{};
        switch(fault){
        case 0:m.Word(0x8214c,2);break;case 1:m.Word(owner.soldier+0x264,0x83000);break;
        case 2:m.Word(owner.soldier+0x264,0x83084);break;case 3:m.Word(owner.soldier+0x264,0x83009);break;
        case 4:m.Word(0x83004,0x70000);break;case 5:m.Word(owner.soldier+0x248,UINT32_MAX-4);break;
        case 6:m.failedAddress=owner.soldier+0x114;break;case 7:m.failedAddress=0x8214c;break;
        case 8:m.failedAddress=0x83004;break;case 9:m.failedAddress=0x81064;break;
        case 10:++owner.equipment.persistence;break;case 11:owner.controlled+=4;break;
        case 12:m.Word(owner.soldier+0x260,4);break;
        }
        CHECK(!so::ReadInventorySelection(m.Memory(),owner,out));CHECK(out==so::InventorySelection{});
    }
    return 0;
}
int QueueCompletion(){
    for(unsigned phase=0;phase<3;++phase){Fixture f;f.Issue();f.Consumer(true,true,0,phase!=1?0xb0000:0,phase?0xb0000:0);
        auto partial=Capture(*f.journal);const auto pending=so::Analyze(*partial);
        CHECK(pending.result==so::Result::Rejected&&pending.reasons==so::Queue&&!pending.observedChainComplete&&!partial->closed);
        f.Neutral();auto s=Capture(*f.journal);const auto done=so::Analyze(*s);
        CHECK(done.result==so::Result::Rejected&&done.reasons==so::Queue&&done.observedChainComplete&&s->closed);
        CHECK(done.issue&&done.outer&&done.inner&&done.previousPublication==2&&done.absentPublication==3&&done.selected);
        const auto* outer=Find(*s,so::Kind::Outer);
        CHECK(outer->queueBeforeRead&&outer->queueAfterRead&&outer->queue==(phase!=1?0xb0000u:0u)&&outer->queueAfter==(phase?0xb0000u:0u));
        for(unsigned fault=0;fault<10;++fault){auto bad=std::make_unique<so::Snapshot>(*s);
            switch(fault){
            case 0:bad->dropped=1;break;case 1:bad->capped=true;break;case 2:bad->active[2]=1;break;
            case 3:Find(*bad,so::Kind::Committed)->kind=so::Kind::Selected;break;
            case 4:Find(*bad,so::Kind::Update,2)->previousId=1;break;
            case 5:Find(*bad,so::Kind::Inner)->reasons|=so::Overlap;break;
            case 6:Find(*bad,so::Kind::Outer)->queueAfterRead=false;break;
            case 7:bad->rows[bad->count++].relevant=false;break;
            case 8:Find(*bad,so::Kind::Outer)->selected.inventory.weapon+=4;break;
            case 9:bad->expired=true;break;
            }
            CHECK(so::Analyze(*bad).result==so::Result::Rejected&&!so::Analyze(*bad).observedChainComplete);
        }
    }
    return 0;
}
void Foreign(so::Token& token,const Fixture& f){
    token.row.before=token.row.after=f.old;token.row.pairBefore=token.row.pairAfter=f.second;
    token.row.foreign=true;token.row.relevant=false;token.row.returned=true;
}
int ForeignBounded(){
    Fixture f;f.Issue();constexpr unsigned flood=400;
    for(unsigned n=0;n<flood;++n){
        auto u=f.journal->Begin(so::Kind::Update,f.thread,13,2,0xb0000,0);Foreign(u,f);f.journal->End(u,f.thread,13,true);
        auto o=f.journal->Begin(so::Kind::Outer,f.thread,13,2,0xb0000,0);Foreign(o,f);
        auto i=f.journal->Begin(so::Kind::Inner,f.thread,13,2,0xb0000,0);Foreign(i,f);++o.childCount;
        f.journal->End(i,f.thread,13,true);f.journal->End(o,f.thread,13,true);
    }
    f.Consumer();f.Neutral(false);auto s=Capture(*f.journal);
    CHECK(so::Analyze(*s).result==so::Result::Joined&&s->closed&&s->count==7&&!s->capped&&!s->dropped);
    CHECK((s->foreignSuppressed==std::array<unsigned,3>{flood,flood,flood}));
    CHECK((s->completedCallbacks==std::array<unsigned,3>{flood+3,flood+1,flood+1}));
    CHECK((s->revision==std::array<std::uint64_t,3>{flood+3,flood+1,flood+1}));
    for(unsigned fault=0;fault<5;++fault){Fixture g;g.Issue();
        auto o=g.journal->Begin(so::Kind::Outer,g.thread,13,2,0xb0000,0);Foreign(o,g);
        switch(fault){case 0:o.row.foreign=false;break;case 1:o.row.after.player+=4;break;
        case 2:o.row.returned=false;break;case 3:o.row.reasons|=so::Memory;break;case 4:o.row.self=g.old.controlled;break;}
        g.journal->End(o,g.thread,13,true);g.Consumer();g.Neutral(false);auto bad=Capture(*g.journal);
        CHECK(bad->foreignSuppressed[1]==0&&bad->count==8&&Find(*bad,so::Kind::Outer)->reasons);
        CHECK(so::Analyze(*bad).result==so::Result::Rejected&&!so::Analyze(*bad).observedChainComplete);
    }
    Fixture crossed;crossed.Issue();so::ThreadState other{};
    auto local=crossed.journal->Begin(so::Kind::Outer,crossed.thread,14,1,crossed.old.controlled,0);
    auto foreign=crossed.journal->Begin(so::Kind::Outer,other,15,2,0xb0000,0);Foreign(foreign,crossed);
    crossed.journal->End(foreign,other,16,true);local.row.returned=true;crossed.journal->End(local,crossed.thread,17,true);
    auto overlap=Capture(*crossed.journal);
    CHECK(overlap->foreignSuppressed[1]==0&&(so::Analyze(*overlap).reasons&so::Overlap));
    CHECK(!so::Analyze(*overlap).observedChainComplete);
    return 0;
}
int Eligibility(){
    Fixture f;auto u=f.journal->Begin(so::Kind::Update,f.thread,10,1,f.old.player,0);auto r=f.AttemptRow(u.row.id);
    r.cycle.slot={};CHECK(!f.journal->Attempt(r,11));
    r.cycle=f.origin;r.cycle.native.owner.weapon+=4;CHECK(!f.journal->Attempt(r,11));
    CHECK(!f.journal->Issue()&&Capture(*f.journal)->activation==0);
    r.cycle=f.origin;CHECK(f.journal->Attempt(r,11));CHECK(!f.journal->Attempt(r,12));
    u.row.returned=true;f.journal->End(u,f.thread,13,true);return 0;
}
// Exercise the real fixed bank, Select/Cancel and callback-fed service. Native
// receipts below are deterministic fixtures, never evidence of game execution.
struct NativeFixture {
    std::unique_ptr<Bc2NativeCycleBank> bank=std::make_unique<Bc2NativeCycleBank>();
    Bc2NativeCycleSelection source{};Bc2NativeCycleControl control{};ReloadHoldInput input{};
    ReloadFlowRecord record{};Bc2NativeCycleDecision decision{};std::uint64_t event=0;
    std::int64_t now=1000000000;bool bolt=false;
    explicit NativeFixture(bool isBolt):bolt(isBolt){
        source.mode=bolt?Bc2NativeCycleMode::Bolt:Bc2NativeCycleMode::Pump;source.nowNs=now;
        auto& r=source.reserve;r.identity={{0x10000,0x11000,0x12000,0x13000,11,12,13},
            {0x14000,0x15000,0x16000},0x17000,0x18000,0x19000};
        r.sequence=1;r.observedNs=now;r.deadlineNs=now+50000000;r.loaded=r.capacity=bolt?5:8;r.reserve=bolt?45:24;
        r.verified=r.allThreeIdle=true;
        auto& c=source.config;
        const auto name=bolt?"M95_sp":"SPAS12_sp";
        const auto path=bolt?"Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95":"Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12";
        std::memcpy(c.assetName.data(),name,std::strlen(name)+1);std::memcpy(c.assetPath.data(),path,std::strlen(path)+1);
        c.weaponData=0x20000;c.firingData=0x21000;c.primaryFire=0x22000;c.ammoAddress=0x22170;
        c.fireLogicType=1;c.reloadType=bolt?1:0;c.fireInputAction=8;c.reloadInputAction=29;
        c.baseCapacity=c.numberOfMagazines=bolt?5:4;c.reloadDelay=bolt?0:.06f;c.reloadTime=bolt?6.9f:.72f;
        c.reloadThreshold=bolt?.67f:1;c.postReloadTime=bolt?0:1;c.boltDelay=bolt?0:.5f;c.boltTime=bolt?2.3f:0;c.holdBoltUntilFireRelease=bolt;
        input.identity=r.identity;input.config=c;input.verified=true;
        input.context.deltaSeconds=1.f/60;input.context.reloadTimeMultiplier=1;input.context.flags24Through28[0]=true;
        input.capacities={r.capacity,r.capacity,r.capacity};
        for(unsigned b=0;b<3;++b){auto& v=input.branches[b];v.address=r.identity.firing[b];v.wrapperOffset=b==0?0x3c:b==1?0x40:0x10;
            v.currentState=bolt?8:7;v.previousState=bolt?7:6;v.nextState=bolt?1:8;v.phaseTimer=bolt?2.2f:.2f;v.loaded=r.loaded-1;v.reserve=r.reserve;}
        const auto& o=r.identity.owner;control.nativeOwner=o;
        control.input.owner={(std::uint64_t(o.weak)<<32)|o.soldier,o.actorGeneration,99,o.space};
        control.item={o.weapon,99};control.mechanism={0x50554d50,99};control.permitted=true;
    }
    bool Select(){
        const auto before=bank->View(now);
        return bank->SelectObserved(source,{before.native,before.cycle,before.shot,1,now,now+50000000,true});
    }
    bool Advance(){
        now+=10000000;auto& s=control.input;++s.sequence;s.observedNs=s.nowNs=now;s.deadlineNs=now+100000000;
        s.focused=true;s.tracked={true,true};s.released={false,false};control.release.reset();return bank->Selected().Control(control);
    }
    template<class Bytes,class T>static void Put(Bytes& bytes,unsigned at,T value){std::memcpy(bytes.data()+at,&value,sizeof(value));}
    void Begin(unsigned branch){
        input.branch=branch;input.contextObservedNs=input.nowNs=++now;input.leaseDeadlineNs=now+100000000;
        decision=bank->Selected().Evaluate(input,++event);record={};record.id=event;
        auto& a=record.entry;a.kind=ReloadFlowEvent::Update;a.nativeInvocation=a.nativeUpdate=a.update=event;
        a.thread=7;a.depth=1;a.caller=0x6e90b0;a.context=0x24000;a.nowNs=now;a.contextCopied=true;
        const auto& v=input.branches[branch];auto& b=a.boundary;b.owner=input.identity.owner;b.snapshotSequence=20;
        b.firing=v.address;b.wrapperOffset=v.wrapperOffset;b.branch=std::uint8_t(branch);
        b.current=v.currentState;b.previous=v.previousState;b.next=v.nextState;b.timer=v.phaseTimer;b.loaded=v.loaded;b.reserve=v.reserve;
        if(branch==2){b.serverPlayer=input.identity.serverPlayer;b.serverSoldier=input.identity.serverSoldier;b.serverItem=input.identity.serverItem;}
        Put(a.copiedContext,0x18,input.context.deltaSeconds);Put(a.copiedContext,0x20,1.f);
        a.copiedContext[0x24]=std::byte{1};Put(a.copiedContext,0x2c,input.context.inputFlags);
        record.exit.thread=a.thread;record.exit.nowNs=++now;record.exit.boundary=b;
        record.exit.contextCopied=true;record.exit.copiedContext=a.copiedContext;record.finished=record.identityRetained=true;
    }
    void Finish(){
        if(decision.hold){record.exit.holdRequested=true;record.exit.hold={std::bit_cast<unsigned>(input.context.deltaSeconds),0,true,true,false};}
        bank->Selected().Finish(decision,record);
    }
    bool Hold(){
        if(!Advance())return false;
        for(unsigned b=0;b<3;++b){input.context.inputFlags=1;input.context.fireRequested=true;Begin(b);
            auto& entry=record.entry.boundary;entry.current=2;entry.previous=1;entry.next=2;entry.timer=0;entry.loaded=source.reserve.loaded;
            Finish();input.context.inputFlags=0;input.context.fireRequested=false;}
        for(unsigned pass=0;pass<2;++pass){if(!Advance())return false;for(unsigned b=0;b<3;++b){Begin(b);Finish();}}
        return bank->View(now).held.has_value();
    }
    void Commit(unsigned from,unsigned to){
        auto r=record;r.id=++event;auto& a=r.entry;a.kind=ReloadFlowEvent::Commit;a.depth=2;
        a.parent=a.nativeParent=a.update=a.nativeUpdate=record.id;a.nativeInvocation=r.id;a.nowNs=++now;
        a.boundary.current=from;a.boundary.previous=from-1;a.boundary.next=to;a.boundary.timer=0;
        r.exit.nowNs=++now;r.exit.boundary=a.boundary;r.exit.boundary->current=to;r.exit.boundary->previous=from;bank->Selected().Commit(r);
    }
    bool Ready(){
        const auto held=bank->View(now).held;if(!held)return false;
        control.input.nowNs=now;control.release=fvr::interaction::WeaponCycleRelease{*held,1,control.input.sequence,now,held->deadlineNs};
        if(!bank->Selected().Control(control)||!Advance())return false;
        for(unsigned b=0;b<3;++b){Begin(b);if(!bolt)Commit(7,8);Commit(8,1);Commit(1,2);
            record.exit.nowNs=++now;auto& after=*record.exit.boundary;after.current=2;after.previous=1;after.next=2;after.timer=0;Finish();
            auto& v=input.branches[b];v.currentState=2;v.previousState=1;v.nextState=2;v.phaseTimer=0;}
        return bank->View(now).ready.has_value();
    }
};
int RealIdleCancellation(){
    for(bool bolt:{false,true})for(bool cancel:{false,true}){
        NativeFixture native(bolt);CHECK(native.Select());const auto before=so::Copy(native.bank->View(native.now));
        CHECK(before.native==native.source.reserve.identity&&before.selectedMode==native.source.mode&&so::Idle(before));
        Fixture f;f.Adopt(before,native.source.reserve.identity.owner);
        if(cancel)native.bank->Selected().Cancel(Bc2NativeCycleFailure::Control);
        const auto live=so::Copy(native.bank->View(native.now));
        if(cancel){CHECK(live.native==ReloadHoldIdentity{}&&live.slot==before.slot);
            // Taking the origin after the real Cancel reproduces late admission failure.
            Fixture late;late.Adopt(live,f.owner);late.Issue(false,false);
            CHECK(!late.journal->Issue()&&!Capture(*late.journal)->activation);}
        f.Issue(true,false,live);CHECK(f.issue);f.Consumer();f.Neutral(false);
        const auto snapshot=Capture(*f.journal);CHECK(so::Analyze(*snapshot).result==so::Result::Joined);
        const auto* attempted=Find(*snapshot,so::Kind::Attempted);const auto* committed=Find(*snapshot,so::Kind::Committed);
        CHECK(attempted&&committed&&attempted->origin.cycle==before&&attempted->cycle==live&&committed->commitLive==live);
        CHECK(committed->origin==attempted->origin&&native.bank->View(native.now).native==live.native);
    }return 0;
}
int OriginAndIssueNegatives(){
    for(unsigned test=0;test<36;++test){NativeFixture native(test%2!=0);CHECK(native.Select());
        Fixture f;f.Adopt(so::Copy(native.bank->View(native.now)),native.source.reserve.identity.owner);
        native.bank->Selected().Cancel(Bc2NativeCycleFailure::Control);
        auto u=f.journal->Begin(so::Kind::Update,f.thread,10,1,f.old.player,0);auto r=f.AttemptRow(u.row.id);
        r.cycle=so::Copy(native.bank->View(native.now));
        switch(test){
        case 0:++r.update;break;case 1:++r.inputSequence;break;case 2:++r.thread;break;
        case 3:++r.command.inventory;break;case 4:++r.command.persistent;break;case 5:++r.command.primaryAction;break;
        case 6:++r.command.family;break;case 7:r.command.selectedSecondary=true;break;
        case 8:++r.command.targetWeapon;break;case 9:++r.command.targetSlot;break;case 10:++r.command.action;break;
        case 11:++r.before.equipment.persistence;break;case 12:++r.before.player;break;
        case 13:++r.pairBefore.current;break;case 14:++r.nativeOwner.space;break;case 15:++r.nativeOwner.equipGeneration;break;
        case 16:r.cycle={};break;case 17:++r.cycle.slot.index;break;case 18:++r.cycle.slot.generation;break;
        case 19:r.cycle.native.firing[1]=f.origin.native.firing[1];break;
        case 20:++r.cycle.cycle;break;case 21:++r.cycle.shot;break;case 22:++r.cycle.guardEpoch;break;
        case 23:r.cycle.ready=true;break;case 24:r.cycle.debt=true;break;case 25:r.cycle.release=true;break;
        case 26:r.cycle.blocked=true;break;case 27:r.cycle.parked=true;break;case 28:r.cycle.phase=Bc2NativeCyclePhase::Cancelled;break;
        case 29:++r.cycle.selectedOwner.weapon;break;case 30:r.cycle.selectedMode.reset();break;
        case 31:r.origin={};break;case 32:r.origin.cycle.slot.index=Bc2NativeCycleBank::Capacity;break;
        case 33:r.origin.qpc=12;break;case 34:r.cycle.held=true;break;case 35:r.cycle.retainedDebt=true;break;
        }
        CHECK(!f.journal->Attempt(r,11)&&!f.journal->Issue()&&!Capture(*f.journal)->activation);
        u.row.returned=true;f.journal->End(u,f.thread,13,true);
    }
    // Unsafe origins cannot borrow a clean later bank view either.
    for(unsigned test=0;test<9;++test){NativeFixture native(false);CHECK(native.Select());
        Fixture f;f.Adopt(so::Copy(native.bank->View(native.now)),native.source.reserve.identity.owner);
        native.bank->Selected().Cancel(Bc2NativeCycleFailure::Control);auto r=f.AttemptRow(1);r.cycle=so::Copy(native.bank->View(native.now));
        auto& c=r.origin.cycle;
        switch(test){case 0:c.debt=true;break;case 1:c.ready=true;break;case 2:c.release=true;break;case 3:c.blocked=true;break;
        case 4:c.parked=true;break;case 5:c.held=true;break;case 6:c.suspended=true;break;case 7:c.retainedDebt=true;break;
        case 8:c.phase=Bc2NativeCyclePhase::Cancelled;break;}
        CHECK(!f.journal->Attempt(r,11));
    }return 0;
}
int CommittedLiveAndOriginCannotDrift(){
    for(unsigned test=0;test<17;++test){NativeFixture native(test%2!=0);CHECK(native.Select());
        Fixture f;f.Adopt(so::Copy(native.bank->View(native.now)),native.source.reserve.identity.owner);
        native.bank->Selected().Cancel(Bc2NativeCycleFailure::Control);f.Issue(true,false,so::Copy(native.bank->View(native.now)));f.Consumer();f.Neutral(false);
        auto s=Capture(*f.journal);auto* r=Find(*s,so::Kind::Committed);CHECK(r);
        switch(test){case 0:++r->origin.cycle.shot;break;case 1:++r->origin.qpc;break;
        case 2:++r->cycle.slot.generation;break;case 3:r->commitLive.present=false;break;
        case 4:++r->commitLive.slot.generation;break;case 5:++r->commitLive.cycle;break;
        case 6:r->commitLive.native=r->origin.cycle.native;break;case 7:r->cacheValue=0;break;
        case 8:++r->after.equipment.persistence;break;case 9:++r->pairAfter.previous;break;
        case 10:++r->command.action;break;case 11:++r->inputSequence;break;case 12:++r->update;break;
        case 13:++r->thread;break;case 14:++r->nativeOwner.actorGeneration;break;
        case 15:r->begin=10;break;case 16:r->commitLive.release=true;break;}
        CHECK(so::Analyze(*s).result==so::Result::Rejected);
    }return 0;
}
int RealDebtKeepsOriginalBank(){
    const auto sameReady=[](const auto& a,const auto& b){
        return bool(a)==bool(b)&&(!a||(a->release==b->release&&a->sequence==b->sequence&&
            a->observedNs==b->observedNs&&a->deadlineNs==b->deadlineNs&&a->nativeReady==b->nativeReady&&
            a->unchangedAmmunition==b->unchangedAmmunition));};
    for(bool bolt:{false,true})for(unsigned stage=0;stage<3;++stage){NativeFixture native(bolt);
        CHECK(native.bank->EnableRecovery()&&native.Select()&&native.Hold());
        if(stage==1)CHECK(native.bank->Selected().BeginRecovery(native.now));
        if(stage==2)CHECK(native.Ready());
        const auto original=native.bank->View(native.now);const auto slot=native.bank->SelectedSlot();
        CHECK(original.blocksFire&&!NativeCycleSelectionAllowed(original,native.source.reserve.identity.owner));
        if(stage==1)CHECK(original.debt&&original.retainedDebtOrigin);
        if(stage==2)CHECK(original.ready&&original.releasePending);
        Fixture f;f.Adopt(so::Copy(original),original.native.owner);f.Issue(false,true);
        CHECK(so::Analyze(*Capture(*f.journal)).result==so::Result::Refused);
        const auto refused=native.bank->View(native.now);
        CHECK(refused.native==original.native&&refused.debt==original.debt&&sameReady(refused.ready,original.ready));
        native.bank->Selected().Cancel(Bc2NativeCycleFailure::Control);const auto after=native.bank->View(native.now);
        CHECK(native.bank->SelectedSlot()==slot&&native.bank->Find(slot)==&native.bank->Selected());
        CHECK(after.native==original.native&&after.cycle==original.cycle&&after.shot==original.shot&&after.blocksFire);
        CHECK(!NativeCycleSelectionAllowed(after,original.native.owner));
        CHECK(after.debt==original.debt&&sameReady(after.ready,original.ready));
        CHECK(!so::IssueLiveMatches(f.AttemptRow(1).origin,so::Copy(after))||so::Copy(after)==so::Copy(original));
        // Even an invented successful dispatch of unchanged owed state cannot join.
        Fixture forged;forged.Adopt(so::Copy(original),original.native.owner);forged.Issue();forged.Consumer();forged.Neutral(false);
        CHECK(so::Analyze(*Capture(*forged.journal)).result==so::Result::Rejected);
    }return 0;
}
int NoIssuedEdgeCannotCommit(){
    for(bool bolt:{false,true}){NativeFixture native(bolt);CHECK(native.Select());
        Fixture noEdge;noEdge.Adopt(so::Copy(native.bank->View(native.now)),native.source.reserve.identity.owner);
        const auto prepared=noEdge.AttemptRow(1).origin;CHECK(so::Valid(prepared));
        CHECK(!noEdge.journal->Issue()&&!Capture(*noEdge.journal)->activation);
        // Run the same real cache overrides as the dispatcher: a refused patch
        // or committed holster suppression cannot produce the issued edge.
        for(unsigned path=0;path<2;++path){Fixture f;f.Adopt(prepared.cycle,prepared.nativeOwner);
            native.bank->Selected().Cancel(Bc2NativeCycleFailure::Control);f.Issue(false,false,so::Copy(native.bank->View(native.now)));
            std::array<std::byte,InputBytes> cache{};InputOverride patch;
            fvr::interaction::ActionOutput action{};action.active=true;action.owner=f.owner.soldier;
            const auto bytes=path?std::span<std::byte>{cache}:std::span<std::byte>{cache}.first(InputBytes-1);
            const bool applied=patch.Apply(bytes,action,{},true);CHECK(applied==bool(path));
            if(applied){
                unsigned value=0;std::memcpy(&value,cache.data()+8+4*7,4);CHECK(value==0x3f800000);
                HolsterSuppressionRequest request{};request.owner=f.owner;request.request=request.nativeTick=1;request.cache=f.old.cache;
                auto& input=request.input;input.owner={(std::uint64_t(f.owner.weak)<<32)|f.owner.soldier,f.owner.actorGeneration,99,f.owner.space};
                input.sequence=40;input.observedNs=input.nowNs=1000000000;input.deadlineNs=1100000000;input.focused=true;input.tracked={true,true};
                HolsterInputOverride suppression;
                CHECK(suppression.Apply(cache,request,{nullptr,[](void*,const HolsterSuppressionRequest&)noexcept{return true;}}));
                const auto receipt=suppression.Commit();CHECK(receipt&&HolsterSuppressionCurrent(*receipt,request));patch.Commit();
                std::memcpy(&value,cache.data()+8+4*7,4);CHECK(value==0);
            }
            f.Consumer();f.Neutral(false);const auto s=Capture(*f.journal);
            CHECK(!Find(*s,so::Kind::Committed)&&so::Analyze(*s).result==so::Result::Attempted);}
    }return 0;
}
}
int main(){if(CompleteJoin()||ExactGenerations()||RefusedAndMissing()||OwnerPairAndSlot()||SelectedPending()||
    Publications()||MultiSlotAndForeign()||OverlapAndCap()||InventoryNamespace()||InventoryReads()||QueueCompletion()||ForeignBounded()||Eligibility()||RealIdleCancellation()||OriginAndIssueNegatives()||
    CommittedLiveAndOriginCannotDrift()||RealDebtKeepsOriginalBank()||NoIssuedEdgeCannotCommit())return 1;return 0;}
