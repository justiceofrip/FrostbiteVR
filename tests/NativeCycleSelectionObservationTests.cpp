#include "Bc2NativeCycleSelectionObservation.h"
#include "Test.h"
#include <memory>
#include <thread>
using namespace fvr::bc2;
namespace so=fvr::bc2::cycleSelectionObservation;
namespace {
struct Fixture {
    std::unique_ptr<so::Journal> journal=std::make_unique<so::Journal>();so::ThreadState thread{};
    so::Owner old{0x10000,0x20000,0x30000,0x20000,0x40000,0,0x50000,0x60000,{0x70000,0x71000,11,{}}};
    so::Owner next=old;so::Pair first{0x60000,0x600d0},second{0x600d0,0x60000};
    ReloadStateOwner owner{0x10000,0x20000,0x30000,0x70000,2,3,4};
    so::Cycle origin{{2,9},{owner,{0x72000,0x73000,0x74000},0x75000,0x76000,0x77000},5,6,7,true};
    std::uint64_t issue=0;
    Fixture(){next.equipment={0x80000,0x81000,12,{}};journal->Configure(1000);journal->Start();}
    void Issue(bool committed=true,bool refused=false){
        auto u=journal->Begin(so::Kind::Update,thread,10,1,old.player,0);
        so::Row r{};r.before=old;r.cycle=origin;r.nativeOwner=owner;r.update=u.row.id;r.inputSequence=40;
        r.action=7;r.value=0x3f800000;r.targetWeapon=next.equipment.weapon;r.targetSlot=1;
        journal->Attempt(r,11);issue=journal->Issue();journal->IssueRow(r);
        if(committed||refused){r.kind=refused?so::Kind::Refused:so::Kind::Committed;r.id=0;r.begin=r.end=12;journal->Append(r);}
        u.row.issue=issue;u.row.action=7;u.row.before=u.row.after=old;u.row.pairBefore=first;u.row.pairAfter=second;
        u.row.currentValue=committed?0x3f800000:0;u.row.returned=true;journal->End(u,thread,13,true);
    }
    void Consumer(bool child=true,bool childNormal=true,unsigned otherSlots=0){
        auto o=journal->Begin(so::Kind::Outer,thread,14,1,old.controlled,0);
        o.row.before=old;o.row.after=next;o.row.pairBefore=o.row.pairAfter=second;journal->PairIds(o.row);
        if(child){auto i=journal->Begin(so::Kind::Inner,thread,15,1,old.controlled,0x400000+so::InnerCallerRva);++o.childCount;
            i.row.before=old;i.row.after=next;i.row.pairBefore=i.row.pairAfter=second;i.row.actualR=old.pair;i.row.slot=old.entrySlot;
            journal->PairIds(i.row);i.row.returned=childNormal;journal->End(i,thread,16,childNormal);}
        for(unsigned n=0;n<otherSlots;++n){
            auto i=journal->Begin(so::Kind::Inner,thread,16,1,old.controlled,0x400000+so::InnerCallerRva);++o.childCount;
            i.row.before=old;i.row.after=next;i.row.pairBefore=i.row.pairAfter=second;i.row.actualR=0xa0000+n*0x1000;
            i.row.slot=n+1;i.row.matchingSlot=false;i.row.returned=true;journal->End(i,thread,16,true);
        }
        o.row.returned=true;o.row.selected={0x90000,1,0,1};journal->End(o,thread,17,true);
    }
    void Neutral(bool selected=true){
        auto u=journal->Begin(so::Kind::Update,thread,18,1,old.player,0);
        if(selected){so::Row r{};r.kind=so::Kind::Selected;r.begin=r.end=19;r.after=next;r.selected={0x90000,1,0,1};
            r.nativeOwner=owner;r.nativeOwner.weapon=next.equipment.weapon;++r.nativeOwner.equipGeneration;
            r.cycle=origin;r.cycle.native.owner=r.nativeOwner;r.cycle.slot={3,1};r.cycle.cycle=1;r.cycle.shot=0;r.cycle.guardEpoch=0;
            journal->Append(r);}
        u.row.before=u.row.after=next;u.row.pairBefore=second;u.row.pairAfter=first;u.row.action=7;
        u.row.selected={0x90000,1,0,1};
        u.row.previousValue=0x3f800000;u.row.returned=true;journal->End(u,thread,20,true);
        auto v=journal->Begin(so::Kind::Update,thread,21,1,old.player,0);
        v.row.before=v.row.after=next;v.row.pairBefore=first;v.row.pairAfter=second;v.row.action=7;v.row.returned=true;
        v.row.selected={0x90000,1,0,1};
        journal->End(v,thread,22,true);
    }
    std::unique_ptr<so::Snapshot> Complete(){Issue();Consumer();Neutral();return std::make_unique<so::Snapshot>(journal->Capture());}
};
so::Row* Find(so::Snapshot& s,so::Kind kind,unsigned which=0){for(unsigned n=0;n<s.count;++n)if(s.rows[n].kind==kind){if(!which--)return &s.rows[n];}return nullptr;}
int CompleteJoin(){
    Fixture f;auto s=f.Complete();const auto result=so::Analyze(*s);
    CHECK(result.result==so::Result::Joined&&result.issue==f.issue&&result.generation==1);
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
    Fixture f;f.Issue(false,true);auto s=std::make_unique<so::Snapshot>(f.journal->Capture());
    CHECK(so::Analyze(*s).result==so::Result::Refused&&s->closed);
    CHECK(!Find(*s,so::Kind::Committed));
    Fixture g;g.Issue(false,false);auto a=std::make_unique<so::Snapshot>(g.journal->Capture());
    CHECK(so::Analyze(*a).result==so::Result::Attempted);
    Fixture h;h.Issue();h.Consumer(false);h.Neutral();auto b=std::make_unique<so::Snapshot>(h.journal->Capture());
    CHECK(so::Analyze(*b).result==so::Result::Rejected);
    Fixture i;i.Issue();i.Consumer(true,false);i.Neutral();auto c=std::make_unique<so::Snapshot>(i.journal->Capture());
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
        case 2:r->after.equipment.persistence+=1;break;case 3:r->selected.current=0;break;case 4:r->begin=16;break;}
        CHECK(so::Analyze(*s).result==so::Result::Joined&&!so::Analyze(*s).selectedBank);
    }
    Fixture plain;plain.Issue();plain.Consumer();plain.Neutral(false);
    auto a=std::make_unique<so::Snapshot>(plain.journal->Capture());
    CHECK(so::Analyze(*a).result==so::Result::Joined&&!so::Analyze(*a).selectedBank);
    Fixture stale;auto b=stale.Complete();Find(*b,so::Kind::Update,2)->selected.current=0;
    CHECK(so::Analyze(*b).result==so::Result::PendingSelected);
    Fixture replaced;auto c=replaced.Complete();Find(*c,so::Kind::Update,2)->after.equipment.persistence+=1;
    CHECK(so::Analyze(*c).result==so::Result::PendingSelected);
    Fixture delayed;auto d=delayed.Complete();Find(*d,so::Kind::Outer)->after=delayed.old;
    CHECK(so::Analyze(*d).result==so::Result::Joined);
    return 0;
}
int Publications(){
    Fixture f;f.Issue();f.Consumer();
    auto u=f.journal->Begin(so::Kind::Update,f.thread,18,1,f.old.player,0);
    u.row.before=u.row.after=f.next;u.row.pairBefore=u.row.pairAfter=f.second;u.row.returned=true;
    f.journal->End(u,f.thread,20,true);auto s=std::make_unique<so::Snapshot>(f.journal->Capture());
    CHECK(so::Analyze(*s).result==so::Result::Rejected&&(so::Analyze(*s).reasons&so::NotSwapped));
    Fixture g;g.Issue();auto v=g.journal->Begin(so::Kind::Update,g.thread,18,1,g.old.player,0);
    v.row.before=v.row.after=g.next;v.row.before.pair+=0x1000;v.row.after.pair=v.row.before.pair;
    v.row.pairBefore=g.second;v.row.pairAfter=g.first;v.row.returned=true;g.journal->End(v,g.thread,20,true);
    auto a=std::make_unique<so::Snapshot>(g.journal->Capture());CHECK(so::Analyze(*a).result==so::Result::Rejected);
    Fixture duplicate;auto b=duplicate.Complete();Find(*b,so::Kind::Update,1)->publication=1;
    CHECK(so::Analyze(*b).result==so::Result::Rejected);return 0;
}
int MultiSlotAndForeign(){
    Fixture f;f.Issue();f.Consumer(true,true,2);f.Neutral(false);
    auto s=std::make_unique<so::Snapshot>(f.journal->Capture());CHECK(so::Analyze(*s).result==so::Result::Joined);
    CHECK(Find(*s,so::Kind::Outer)->childCount==3);
    Fixture g;g.Issue();
    auto foreign=g.journal->Begin(so::Kind::Outer,g.thread,13,2,0xb0000,0);
    foreign.row.relevant=false;foreign.row.returned=true;g.journal->End(foreign,g.thread,13,true);
    g.Consumer();g.Neutral(false);auto a=std::make_unique<so::Snapshot>(g.journal->Capture());
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
    auto s=std::make_unique<so::Snapshot>(f.journal->Capture());CHECK(so::Analyze(*s).result==so::Result::Rejected);
    CHECK(so::Analyze(*s).reasons&so::Overlap);
    Fixture g;g.Issue();for(unsigned n=0;n<so::Capacity;++n){so::Row r{};r.kind=so::Kind::Selected;g.journal->Append(r);}
    auto a=std::make_unique<so::Snapshot>(g.journal->Capture());CHECK(a->capped&&a->dropped&&a->closed&&so::Analyze(*a).result==so::Result::Rejected);
    Fixture h;h.Issue();CHECK(!h.journal->Capturing(20011));auto b=std::make_unique<so::Snapshot>(h.journal->Capture());CHECK(b->expired&&b->closed);
    return 0;
}
int Eligibility(){
    Fixture f;auto u=f.journal->Begin(so::Kind::Update,f.thread,10,1,f.old.player,0);so::Row r{};
    r.before=f.old;r.nativeOwner=f.owner;r.cycle=f.origin;r.cycle.slot={};CHECK(!f.journal->Attempt(r,11));
    r.cycle=f.origin;r.cycle.native.owner.weapon+=4;CHECK(!f.journal->Attempt(r,11));
    CHECK(!f.journal->Issue()&&f.journal->Capture().activation==0);
    r.cycle=f.origin;r.update=u.row.id;CHECK(f.journal->Attempt(r,11));CHECK(!f.journal->Attempt(r,12));
    u.row.returned=true;f.journal->End(u,f.thread,13,true);return 0;
}
}
int main(){if(CompleteJoin()||ExactGenerations()||RefusedAndMissing()||OwnerPairAndSlot()||SelectedPending()||
    Publications()||MultiSlotAndForeign()||OverlapAndCap()||Eligibility())return 1;return 0;}
