#pragma once
#include "Bc2NativeCycleService.h"
#include "Bc2EquipmentIdentity.h"
#include <array>
#include <atomic>
#include <type_traits>

// Passive, value-only diagnostic. This is not a selection permit, a debt store,
// or proof that all native consumers/copies have retired. No native addresses
// recorded here may subsequently be dereferenced by this ledger.
namespace fvr::bc2::cycleSelectionObservation {
inline constexpr unsigned Capacity=256,OuterRva=0x4be090,OuterSize=875,
    InnerRva=0x4c05c0,InnerSize=82,InnerCallerRva=0x4be3af;
inline constexpr const char* ExecutableHash="3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258";
inline constexpr const char* OuterHash="734b785b00c97621a3b258f378a351e9ca579cdd5580075067c29f57b261ba0b";
inline constexpr const char* InnerHash="8e733747f4e91df9875a1e198fd6cd85f6614f56d919e830bf90a5f52f0f9aec";
enum class Kind:unsigned {Update,Outer,Inner,Attempted,Refused,Committed,Selected};
enum Reason:unsigned {None=0,Memory=1,Overlap=2,Abnormal=4,PairChanged=8,NotSwapped=16,
    Queue=32,Fallback=64,Parent=128,Slot=256,Clock=512,Missing=1024,View=2048,Value=4096};
enum class Result:unsigned {NotStarted,Attempted,Refused,PendingPublication,PendingConsumer,
    PendingNeutral,PendingSelected,Joined,Rejected};
struct Cycle {
    Bc2NativeCycleSlot slot{};ReloadHoldIdentity native{};
    std::uint64_t cycle=0,shot=0,guardEpoch=0;bool present=false;
    bool operator==(const Cycle&)const=default;
};
inline Cycle Copy(const std::optional<Bc2NativeCycleView>& v)noexcept {
    if(!v)return {};return {v->slot,v->native,v->cycle,v->shot,v->debt?v->debt->guardEpoch:0,true};
}
struct Owner {
    unsigned player=0,soldier=0,weak=0,controlled=0,entry=0,entrySlot=0,cache=0,pair=0;
    WeaponEquipmentIdentity equipment{};
    bool operator==(const Owner&)const=default;
};
inline bool SamePair(const Owner& a,const Owner& b)noexcept {
    return a.player&&a.pair&&a.cache&&a.player==b.player&&a.soldier==b.soldier&&a.weak==b.weak&&
        a.controlled==b.controlled&&a.entry==b.entry&&a.entrySlot==b.entrySlot&&a.cache==b.cache&&a.pair==b.pair;
}
inline bool Matches(const Owner& a,const ReloadStateOwner& b)noexcept {
    return a.player==b.player&&a.soldier==b.soldier&&a.weak==b.weak&&a.equipment.weapon==b.weapon;
}
struct Pair {unsigned current=0,previous=0;bool operator==(const Pair&)const=default;};
struct Selected {unsigned address=0,current=0,previous=0,playerIndex=0;bool operator==(const Selected&)const=default;};
struct Row {
    Kind kind{};std::uint64_t id=0,parent=0,update=0,issue=0,publication=0,currentId=0,previousId=0;
    std::uint64_t inputSequence=0;std::int64_t begin=0,end=0;
    unsigned thread=0,depth=0,self=0,caller=0,slot=0,actualR=0,queue=0;
    unsigned action=0,value=0,currentValue=0,previousValue=0,targetWeapon=0,targetSlot=0;
    std::array<unsigned,4> args{};unsigned result=0,reasons=0,childCount=0;
    std::array<unsigned,3> beginActive{},endActive{};
    std::array<std::uint64_t,3> beginRevision{},endRevision{};
    Owner before{},after{};Pair pairBefore{},pairAfter{};Selected selected{};
    ReloadStateOwner nativeOwner{};Cycle cycle{};
    bool returned=false,relevant=true,matchingSlot=true;
};
struct ThreadState {std::uint64_t update=0,outer=0;std::array<unsigned,3> depth{};};
struct Token {Row row{};ThreadState previous{};unsigned childCount=0;bool active=false;};
static_assert(std::is_trivially_destructible_v<Token>);
struct Summary {
    Result result=Result::NotStarted;unsigned reasons=0;std::uint64_t issue=0,generation=0,outer=0,inner=0,
        previousPublication=0,absentPublication=0,selected=0,selectedBank=0;
};
struct Snapshot {
    std::array<Row,Capacity> rows{};unsigned count=0,dropped=0;bool capped=false,closed=false,expired=false;
    std::array<unsigned,3> active{};std::array<std::uint64_t,3> revision{};
    std::int64_t activation=0,deadline=0;
};
// Deterministic join over the one retained issue. IDs refer to actual immutable
// publications; matching addresses or a newer generation alone never suffices.
inline Summary Analyze(const Snapshot& s)noexcept {
    Summary out{};const Row* issue=nullptr;const Row* commit=nullptr;const Row* publication=nullptr;bool refused=false;
    std::uint64_t lastPublication=0;
    for(unsigned n=0;n<s.count;++n){const auto& r=s.rows[n];
        if(r.kind==Kind::Attempted){if(issue){out.reasons|=Missing;}else issue=&r;}
        if(r.kind==Kind::Update&&r.relevant&&r.publication){
            if(r.publication<=lastPublication||r.currentId!=r.publication)out.reasons|=Missing;
            lastPublication=r.publication;
        }
        if(r.relevant)out.reasons|=r.reasons;
    }
    if(!issue){if(s.dropped||s.capped){out.result=Result::Rejected;out.reasons|=Missing;}return out;}
    out.issue=issue->issue;out.result=Result::Attempted;
    if(s.dropped||s.capped)out.reasons|=Missing;
    if(!issue->cycle.present||!issue->cycle.slot||issue->cycle.native.owner!=issue->nativeOwner||
       !Matches(issue->before,issue->nativeOwner))out.reasons|=View;
    for(unsigned n=0;n<s.count;++n){const auto& r=s.rows[n];if(r.issue!=out.issue)continue;
        if(r.kind==Kind::Refused){out.result=Result::Refused;refused=true;}
        if(r.kind==Kind::Committed){
            if(commit||r.update!=issue->update||r.before!=issue->before||r.cycle!=issue->cycle||
               r.action!=issue->action||r.value!=issue->value||r.inputSequence!=issue->inputSequence)out.reasons|=View;
            commit=&r;
        }
    }
    if(commit&&refused)out.reasons|=Missing;
    if(out.reasons){out.result=Result::Rejected;return out;}
    if(!commit||out.result==Result::Refused)return out;
    out.result=Result::PendingPublication;
    for(unsigned n=0;n<s.count;++n){const auto& r=s.rows[n];
        if(r.kind!=Kind::Update||r.issue!=out.issue||r.update!=commit->update)continue;
        if(publication||!r.returned||!r.publication||r.currentId!=r.publication||
           !SamePair(r.before,issue->before)||!SamePair(r.after,issue->before)||
           r.action!=issue->action||r.currentValue!=issue->value)out.reasons|=Value;
        publication=&r;
    }
    if(out.reasons){out.result=Result::Rejected;return out;}
    if(!publication)return out;
    out.generation=publication->publication;out.result=Result::PendingConsumer;
    const Row* selectedOuter=nullptr;
    for(unsigned n=0;n<s.count;++n){const auto& outer=s.rows[n];
        if(outer.kind!=Kind::Outer||!outer.relevant||outer.begin<publication->end||
           (outer.currentId!=out.generation&&outer.previousId!=out.generation))continue;
        const Row* inner=nullptr;unsigned children=0,matching=0;
        for(unsigned j=0;j<s.count;++j){const auto& r=s.rows[j];
            if(r.kind==Kind::Inner&&r.parent==outer.id){++children;
                if(!r.returned||r.end>outer.end||r.begin<outer.begin||r.self!=outer.self)out.reasons|=Missing;
                if(r.relevant&&r.matchingSlot&&r.slot==outer.before.entrySlot){inner=&r;++matching;}
            }}
        if(children!=outer.childCount||matching!=1||!inner||!outer.returned||!inner->returned||inner->end>outer.end||
           inner->begin<outer.begin||inner->actualR!=issue->before.pair||inner->self!=outer.self||
           inner->slot!=outer.before.entrySlot||!SamePair(outer.before,issue->before)||
           !SamePair(inner->before,outer.before)||inner->currentId!=outer.currentId||
           inner->previousId!=outer.previousId){out.reasons|=Missing;continue;}
        if(outer.selected.current!=issue->targetSlot)continue;
        selectedOuter=&outer;out.outer=outer.id;out.inner=inner->id;
    }
    if(out.reasons){out.result=Result::Rejected;return out;}
    if(!selectedOuter)return out;
    out.result=Result::PendingNeutral;
    const Row* previous=nullptr;const Row* absent=nullptr;
    for(unsigned n=0;n<s.count;++n){const auto& r=s.rows[n];
        if(r.kind!=Kind::Update||!r.relevant||r.publication<=out.generation||!SamePair(r.after,issue->before))continue;
        if(!previous){
            if(r.previousId==out.generation&&r.currentId!=out.generation&&r.currentValue==0&&r.previousValue==issue->value)previous=&r;
        }else if(r.publication>previous->publication&&r.previousId==previous->currentId&&
                 r.currentId!=out.generation&&r.previousId!=out.generation&&r.currentValue==0&&r.previousValue==0){absent=&r;break;}
    }
    if(!previous||!absent)return out;
    out.previousPublication=previous->publication;out.absentPublication=absent->publication;
    if(s.active[0]||s.active[1]||s.active[2])return out;
    out.result=Result::PendingSelected;
    // Every target family has a fresh native Resolve/selected-index result.
    // A rifle need not have a mechanical service in the cycle bank. The later
    // bank view is optional corroboration, joined only under its exact owner.
    if(absent->begin<=selectedOuter->end||!SamePair(absent->after,selectedOuter->after)||
       absent->after.equipment.weapon!=issue->targetWeapon||absent->selected!=selectedOuter->selected)return out;
    if(selectedOuter->after.equipment.weapon==issue->targetWeapon){
        if(absent->after.equipment!=selectedOuter->after.equipment)return out;
    }else if(selectedOuter->after.equipment!=issue->before.equipment)return out;
    out.selected=absent->id;out.result=Result::Joined;
    for(unsigned n=0;n<s.count;++n){const auto& r=s.rows[n];
        if(r.kind!=Kind::Selected||r.begin<=selectedOuter->end)continue;
        if(r.after!=absent->after||r.selected!=absent->selected||!r.cycle.present||
           !r.cycle.slot||r.nativeOwner!=r.cycle.native.owner||!Matches(r.after,r.nativeOwner))continue;
        // Reusing the origin bank slot is only possible under a new generation;
        // an unchanged slot must still name the entire original native identity.
        if(r.cycle.slot.index==issue->cycle.slot.index&&r.cycle.slot.generation<=issue->cycle.slot.generation&&
           r.cycle.native!=issue->cycle.native){out.reasons|=Slot;continue;}
        out.selectedBank=r.id;
    }
    if(out.reasons)out.result=Result::Rejected;
    return out;
}
class Journal {
    struct Stored {Row row{};std::atomic<bool> ready{false};};
    std::array<Stored,Capacity> rows_{};std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    std::array<std::atomic<unsigned>,3> active_{};std::array<std::atomic<std::uint64_t>,3> revision_{};
    std::atomic<std::uint64_t> serial_{0},issue_{0};std::atomic<unsigned> count_{0},dropped_{0};
    std::atomic<bool> started_{false},closed_{false},capped_{false},expired_{false};
    std::atomic<std::int64_t> activation_{0};std::int64_t duration_=0;
    std::uint64_t publication_=0;Row lastPublication_{};
public:
    bool Configure(std::int64_t frequency)noexcept {if(duration_||frequency<=0||frequency>INT64_MAX/20)return false;duration_=frequency*20;return true;}
    bool Start()noexcept {if(!duration_||closed_.load())return false;return !started_.exchange(true);}
    void Close()noexcept {closed_.store(true);}
    bool Capturing(std::int64_t now)noexcept {
        const auto begin=activation_.load();if(!begin||closed_.load())return false;
        if(now<begin||now-begin>=duration_){expired_.store(true);Close();return false;}return true;
    }
    std::uint64_t Issue()const noexcept{return issue_.load();}
    bool IssueRow(Row& out)const noexcept {
        const auto n=count_.load();for(unsigned i=0;i<n;++i){Row r{};if(Read(i,r)&&r.kind==Kind::Attempted){out=r;return true;}}return false;
    }
    bool HasOutcome(std::uint64_t issue)const noexcept {
        const auto n=count_.load();for(unsigned i=0;i<n;++i){Row r{};if(Read(i,r)&&r.issue==issue&&
            (r.kind==Kind::Refused||r.kind==Kind::Committed))return true;}return false;
    }
    std::array<unsigned,3> Active()const noexcept {return {active_[0].load(),active_[1].load(),active_[2].load()};}
    std::array<std::uint64_t,3> Revision()const noexcept {return {revision_[0].load(),revision_[1].load(),revision_[2].load()};}
    Token Begin(Kind kind,ThreadState& thread,std::int64_t now,unsigned tid,unsigned self,unsigned caller)noexcept {
        Token t{};if(!started_.load()||closed_.load())return t;
        const auto k=unsigned(kind);if(k>2)return t;
        // Sequentially consistent registration precedes every host native read.
        ++active_[k];++revision_[k];t.active=true;t.previous=thread;
        auto& r=t.row;r.kind=kind;r.id=++serial_;r.begin=now;r.thread=tid;r.self=self;r.caller=caller;
        r.update=thread.update;r.parent=kind==Kind::Inner?thread.outer:thread.update;
        r.depth=++thread.depth[k];if(kind==Kind::Update)r.update=thread.update=r.id;
        if(kind==Kind::Outer)thread.outer=r.id;
        r.beginActive=Active();r.beginRevision=Revision();return t;
    }
    bool Attempt(Row r,std::int64_t now)noexcept {
        if(!started_.load()||closed_.load()||now<=0||now>INT64_MAX-duration_||!r.cycle.present||!r.cycle.slot||
           r.cycle.native.owner!=r.nativeOwner||!Matches(r.before,r.nativeOwner))return false;
        std::uint64_t empty=0;r.id=++serial_;r.issue=r.id;r.kind=Kind::Attempted;r.begin=r.end=now;
        if(!issue_.compare_exchange_strong(empty,r.id))return false;
        activation_.store(now);
        if(Active()!=std::array<unsigned,3>{1,0,0})r.reasons|=Overlap;
        return Append(r);
    }
    bool Append(Row r)noexcept {
        if(gate_.test_and_set()){++dropped_;return false;}
        const auto release=[&](){gate_.clear();};const auto n=count_.load();
        if(n>=Capacity){capped_.store(true);++dropped_;Close();release();return false;}
        if(!r.id)r.id=++serial_;
        if(r.kind==Kind::Update&&r.relevant&&r.returned){
            if(!r.update||(lastPublication_.publication&&(r.update==lastPublication_.update||r.begin<=lastPublication_.end)))r.reasons|=Overlap;
            if(!SamePair(r.before,r.after))r.reasons|=PairChanged;
            if(!r.pairBefore.current||r.pairBefore.current==r.pairBefore.previous||
               r.pairAfter.current!=r.pairBefore.previous||r.pairAfter.previous!=r.pairBefore.current)r.reasons|=NotSwapped;
            if(lastPublication_.publication&&(!SamePair(lastPublication_.after,r.before)||lastPublication_.pairAfter!=r.pairBefore))r.reasons|=PairChanged;
            r.publication=r.currentId=++publication_;
            r.previousId=lastPublication_.publication?lastPublication_.currentId:0;
            lastPublication_=r;
        }
        rows_[n].row=r;rows_[n].ready.store(true,std::memory_order_release);count_.store(n+1);release();return true;
    }
    void PairIds(Row& r)noexcept {
        if(gate_.test_and_set()){++dropped_;r.reasons|=Overlap;return;}
        if(lastPublication_.publication&&SamePair(lastPublication_.after,r.before)&&lastPublication_.pairAfter==r.pairBefore){
            r.currentId=lastPublication_.currentId;r.previousId=lastPublication_.previousId;
        }else if(lastPublication_.publication)r.reasons|=PairChanged;
        gate_.clear();
    }
    bool Read(unsigned index,Row& out)const noexcept {
        if(index>=Capacity||!rows_[index].ready.load(std::memory_order_acquire))return false;
        out=rows_[index].row;return true;
    }
    Snapshot Capture()const noexcept {
        Snapshot s{};s.dropped=dropped_.load();s.capped=capped_.load();s.closed=closed_.load();s.expired=expired_.load();
        s.active=Active();s.revision=Revision();s.activation=activation_.load();s.deadline=s.activation?s.activation+duration_:0;
        const auto n=count_.load();for(unsigned i=0;i<n;++i)if(Read(i,s.rows[s.count]))++s.count;return s;
    }
    void End(Token& t,ThreadState& thread,std::int64_t now,bool normal)noexcept {
        if(!t.active)return;auto& r=t.row;const auto k=unsigned(r.kind);
        r.end=now;r.childCount=t.childCount;r.endActive=Active();r.endRevision=Revision();
        if(!normal||!r.returned)r.reasons|=Abnormal;
        if(now<=0||r.begin<=0||now<r.begin)r.reasons|=Clock;
        auto expected=r.beginRevision;if(r.kind==Kind::Outer)expected[2]+=t.childCount;
        if(r.depth!=1||expected!=r.endRevision||r.beginActive!=r.endActive)r.reasons|=Overlap;
        if(r.kind==Kind::Update&&(r.beginActive[1]||r.beginActive[2]))r.reasons|=Overlap;
        if(r.kind==Kind::Outer&&(r.beginActive[0]||r.beginActive[1]!=1||r.beginActive[2]))r.reasons|=Overlap;
        if(r.kind==Kind::Inner&&(r.beginActive[0]||r.beginActive[1]!=1||r.beginActive[2]!=1||!r.parent))r.reasons|=Overlap;
        if(activation_.load()&&r.end>=activation_.load()){
            if(r.end-activation_.load()>=duration_){r.reasons|=Clock;expired_.store(true);Close();}
            Append(r);
        }
        thread=t.previous;--active_[k];t.active=false;
        // Closing a complete diagnostic changes no native work or future input.
        // Process-retained callbacks/originals remain callable after this point.
        if(activation_.load()&&!closed_.load()&&Active()==std::array<unsigned,3>{}){
            const auto revision=Revision();const auto s=Capture();const auto result=Analyze(s);
            if((result.result==Result::Joined||result.result==Result::Refused)&&revision==Revision()&&Active()==std::array<unsigned,3>{})Close();
        }
    }
    bool Drained(bool disabled,unsigned callbacks)const noexcept{return closed_.load()&&disabled&&!callbacks&&Active()==std::array<unsigned,3>{};}
};
} // namespace fvr::bc2::cycleSelectionObservation
