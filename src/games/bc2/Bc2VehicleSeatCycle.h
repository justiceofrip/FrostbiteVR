#pragma once
#include "Bc2VehicleCommit.h"
#include "Bc2VehicleRoutes.h"
#include "fvr/interaction/ControllerInput.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <ostream>
#include <utility>

namespace fvr::bc2::seatCycle {
constexpr unsigned InputBit = 1u << 17;
constexpr unsigned ConflictingBits = (1u << 16) | (0xffu << 18);

// Observation only; None means no refusal was observed, never a native seat change.
enum class Gate {None,Verification,Routes,Capability,Freshness,Controller,Owner,Cache,
    Playing,Rearm,Exit,Input,Menu,Conflict,Plan,Binding,Permission,BindingChanged,
    Deadline,Replay,Continuity,Request,Apply,Readback};
inline const char* GateName(Gate gate) noexcept {
    constexpr const char* names[]{"none","verification","routes","capability","freshness","controller","owner","cache",
        "playing","rearm","exit","input","menu","conflict","plan","binding","permission","binding_changed",
        "deadline","replay","continuity","request","apply","readback"};
    return names[unsigned(gate)];
}
inline bool Admit(bool accepted, Gate gate, Gate* first) noexcept {
    if (!accepted && first && *first==Gate::None) *first=gate;
    return accepted;
}
enum class BindingField {PlayerCache,EntryRouter,Permission,Unrestricted,SuppressionBegin,SuppressionEnd,
    Buckets,BucketCount,Sentinel,InitialNode,NodeAction,NodeConcept,NodeNext,Device,Map,DeviceVtable,
    MapVtable,Keyboard,Table,TableEnd,Override,Actions,ActionsEnd,Count};
struct BindingAlternative {
    unsigned row=0,readMask=0,successMask=0,actual=0,expected=0;
    // key, row concept, key vtable, flags +12/+13, scan +16; absent bits mean not read.
    std::array<unsigned,6> values{};
    const char* refusal="none";
};
struct BindingObservation {
    // Only existing reads are observed. A successful first proof may be replaced
    // by the existing second proof; the first failed proof is never overwritten.
    unsigned calls=0,call=0,routeCount=0,reads=0,traversed=0,terminalNode=0,alternativeCount=0;
    unsigned actual=0,expected=0,failedReadSize=0,failedReadIndex=0,failedAlternativeRow=0;
    std::uint64_t failedReadAddress=0;
    unsigned readMask=0,successMask=0;
    std::array<unsigned,unsigned(BindingField::Count)> values{};
    std::array<BindingAlternative,16> alternatives{};
    const char* refusal="none";
    bool verified=false,reader=false,routeChecked=false,routePresent=false,returned=false,failedReadCalled=false,unboundConcept=false;
    BindingObservation* Begin(bool proof,bool hasReader,unsigned routes) noexcept {
        const auto next=++calls;
        if(call && !returned)return nullptr;
        *this={};calls=call=next;verified=proof;reader=hasReader;routeCount=routes;
        return this;
    }
    bool Check(bool accepted,const char* predicate,unsigned value=0,unsigned wanted=0) noexcept {
        if(!accepted && !std::strcmp(refusal,"none")){refusal=predicate;actual=value;expected=wanted;}
        return accepted;
    }
    void Report(std::ostream& out)const {
        out<<"{\"calls\":"<<calls<<",\"captured_call\":"<<call<<",\"returned\":"<<returned
           <<",\"first_failed_predicate\":\""<<refusal<<"\",\"actual\":"<<actual<<",\"expected\":"<<expected
           <<",\"failed_alternative_row\":"<<failedAlternativeRow
           <<",\"verified\":"<<verified<<",\"reader_present\":"<<reader<<",\"route_count\":"<<routeCount
           <<",\"route_checked\":"<<routeChecked<<",\"route_17_42_present\":"<<routePresent
           <<",\"unbound_concept_route\":"<<unboundConcept
           <<",\"reads\":"<<reads<<",\"first_failed_read\":{\"present\":"<<(failedReadIndex!=0)
           <<",\"index\":"<<failedReadIndex<<",\"address\":"<<failedReadAddress<<",\"size\":"<<failedReadSize
           <<",\"called\":"<<failedReadCalled<<",\"success\":0},\"traversed\":"<<traversed<<",\"terminal_node\":"<<terminalNode;
        constexpr const char* names[]{"player_c74","entry_198","player_160","global_1570e9c","global_1570ee0","global_1570ee4",
            "router_buckets","router_count","sentinel","initial_node","node_action","node_concept","node_next","device","map",
            "device_vtable","map_vtable","keyboard","table","table_end","concept_42_override","actions","actions_end"};
        for(unsigned i=0;i<values.size();++i)
            out<<",\""<<names[i]<<"\":{\"read\":"<<((readMask>>i)&1)<<",\"success\":"<<((successMask>>i)&1)<<",\"value\":"<<values[i]<<'}';
        out<<",\"alternatives\":[";
        constexpr const char* alternativeNames[]{"key","row_concept","key_vtable","flags12","flags13","scan16"};
        for(unsigned i=0;i<alternativeCount;++i){const auto& a=alternatives[i];if(i)out<<',';
            out<<"{\"row\":"<<a.row<<",\"first_failed_predicate\":\""<<a.refusal<<"\",\"actual\":"<<a.actual<<",\"expected\":"<<a.expected
               <<",\"read_mask\":"<<a.readMask<<",\"success_mask\":"<<a.successMask;
            for(unsigned j=0;j<a.values.size();++j)out<<",\""<<alternativeNames[j]<<"\":"<<a.values[j];
            out<<'}';
        }out<<"]}";
    }
};
struct Attempt {
    VehicleSeatIdentity owner{};
    std::uint64_t input=0,space=0,menu=0;
    unsigned gather=0,player=0,weak=0,active=0,held=0,leftHeld=0,permissions=0;
    unsigned flagsWritten=0,flagsObserved=0;
    std::int64_t sampledNs=0,deadlineNs=0;
    Gate first=Gate::None;
    bool verified=false,fresh=false,bindingChecked=false,requested=false,committed=false,readback=false;
    BindingObservation binding{};
};
class AttemptTrace {
    // One verified input-thread producer; published rows never change. Neutral
    // gathers and repeated holds consume no capacity, even after a refused A.
    VehicleSeatIdentity owner_{};std::uint64_t space_=0;bool held_=false;
    std::array<Attempt,16> rows_{};
    std::atomic<unsigned> count_=0,dropped_=0;
public:
    std::optional<Attempt> Observe(bool enabled,const VehicleSeatIdentity& owner,
        const interaction::InputFrame& input) noexcept {
        if (!enabled) return {};
        const bool held=(input.hands[1].held&interaction::Primary)!=0;
        const bool start=held && (!held_ || owner!=owner_ || input.spaceGeneration!=space_);
        held_=held;owner_=owner;space_=input.spaceGeneration;
        if (!start || !input.generation) return {};
        if (count_.load(std::memory_order_relaxed)==rows_.size()) {++dropped_;return {};}
        Attempt row;row.owner=owner;row.input=input.generation;row.space=input.spaceGeneration;row.menu=input.menu.epoch;
        row.active=input.hands[1].active;row.held=input.hands[1].held;row.leftHeld=input.hands[0].held;
        return row;
    }
    void Publish(const Attempt& row) noexcept {
        const auto n=count_.load(std::memory_order_relaxed);
        if (n<rows_.size()) {rows_[n]=row;count_.store(n+1,std::memory_order_release);}
    }
    unsigned Count()const noexcept {return count_.load(std::memory_order_acquire);}
    unsigned Dropped()const noexcept {return dropped_.load();}
    const Attempt& Row(unsigned n)const noexcept {return rows_[n];}
    void Report(std::ostream& out)const {
        out<<"{\"capacity\":"<<rows_.size()<<",\"dropped_holds\":"<<Dropped()
           <<",\"sampling\":\"first gather of each observed A hold; committed/readback describe the gather cache, not a seat change\",\"rows\":[";
        for(unsigned n=0,count=Count();n<count;++n){const auto& r=rows_[n];if(n)out<<',';
            out<<"{\"first_refusal\":\""<<GateName(r.first)<<"\",\"verified\":"<<r.verified
               <<",\"input\":"<<r.input<<",\"gather\":"<<r.gather<<",\"space\":"<<r.space<<",\"menu_epoch\":"<<r.menu
               <<",\"player\":"<<r.player<<",\"soldier\":"<<r.owner.actor<<",\"weak\":"<<r.weak
               <<",\"actor_generation\":"<<r.owner.actorGeneration<<",\"seat_generation\":"<<r.owner.seatGeneration
               <<",\"entity\":"<<r.owner.controlled<<",\"entry\":"<<r.owner.entry<<",\"router\":"<<r.owner.router<<",\"cache\":"<<r.owner.cache
               <<",\"active\":"<<r.active<<",\"held\":"<<r.held<<",\"left_held\":"<<r.leftHeld
               <<",\"sampled_ns\":"<<r.sampledNs<<",\"deadline_ns\":"<<r.deadlineNs<<",\"fresh\":"<<r.fresh
               <<",\"permissions\":"<<r.permissions<<",\"binding_checked\":"<<r.bindingChecked<<",\"requested\":"<<r.requested
               <<",\"cache_committed\":"<<r.committed<<",\"flags_written\":"<<r.flagsWritten<<",\"flags_observed\":"<<r.flagsObserved
               <<",\"readback\":"<<r.readback<<",\"binding\":";r.binding.Report(out);out<<'}';
        }out<<"]}";
    }
};

// Optional grant for the reviewed loaded x86 image. The absolute operands and
// vtable addresses below deliberately restrict this grant to its preferred base.
template<class Executable>
bool Verify(const VehicleRouteMemory& m, unsigned image, Executable executable) noexcept {
    if (image != 0x400000 || !m.read) return false;
    struct Proof { unsigned rva, size; std::uint64_t digest; };
    constexpr Proof code[]{
        {0x41ad55,0x58,0xf88712cf6c3cb069ull}, // gather and subsequent restriction call
        {0x22eee0,0x230,0x93aaa59e588b53fdull}, {0x22f120,0x58,0xf776d360cf2be993ull},
        {0x21c760,0x26,0x92bc9137324ee0d3ull}, {0x3c3320,0x2b,0x5795b87d7a80ab5dull},
        {0x180850,0xa,0x4ac1db82880f349bull}, {0x197070,0x19,0xbca108ab4f057a9full},
        {0x190570,0x24e,0xa37330173f7030c2ull}, {0x3cb530,0x1f,0xc7fcac042d462cc9ull},
        {0x3c33a0,0x34,0x9434ddc5b7a6a4a1ull}, {0x180690,0x46,0xc476d2942d992e49ull},
        {0x3ed7a0,0x89,0x10b7df8652942f7cull}, // post-gather player permission mask
        {0x286417,0x61,0xf75b00e146fcbb13ull},
        {0x131db0,0xdc,0x78a835d793179026ull}, {0x21c400,0x39,0x104a86ecb86d420cull},
        {0x212970,0x86,0x2bd05e3ed2446007ull}, // native semantic cache read
        {0x1787cb,0x46,0x2ee064212d0f7c79ull}, // ChangeEntry17 rising-edge dispatch
        {0x164870,0xeb,0x259e70bd8cbd87b6ull}, // native next-entry search
        {0x2a75d0,0xd2,0xa56e7830e80c9e37ull}, // native vehicle/entry eligibility
        {0x297cf0,0x156,0x43fb36e686ec68aaull}, {0x2979a0,0x20,0x83b9627b2db2fc11ull},
        {0x1610b0,0x85,0x57119ef2959a3e51ull}, {0xfb2e80,6,0x1c320354c69d55ebull}
    };
    for (const auto& p : code) {
        std::array<unsigned char,640> bytes{};
        if (p.size > bytes.size() || !executable(p.rva,p.size) ||
            !m.read(m.context,image+p.rva,bytes.data(),p.size)) return false;
        std::uint64_t digest = 14695981039346656037ull;
        for (unsigned i=0; i<p.size; ++i) digest=(digest^bytes[i])*1099511628211ull;
        if (digest != p.digest) return false;
    }
    for (const auto [address,value] : {std::pair{0x14196f8u+0x20,0x597070u},
             {0x14196f8u+0x68,0x580690u},{0x141ca1cu+4,0x61c760u},
             {0x14656f8u+8,0x13b2e80u},{0x1424000u+0xf8,0x6979a0u},
             {0x1424000u+0x104,0x6a75d0u},{0x1c27df0u+16,17u},
             {0x1c2f530u+16,42u},{0x1c30358u+16,59u}}) {
        unsigned word{};
        if (!m.read(m.context,address,&word,sizeof(word)) || word!=value) return false;
    }
    return true;
}

struct Binding {
    unsigned player=0, router=0, device=0, map=0, table=0, tableEnd=0;
    unsigned actions=0, actionsEnd=0, row=0, key=0, keyboard=0;
    bool unboundConcept=false;
    bool CanRequest() const noexcept { return key!=0 || unboundConcept; }
    bool operator==(const Binding&) const = default;
};
inline std::optional<Binding> ReadBinding(const VehicleRouteMemory& m,
        const VehicleRouteSnapshot& seat, bool verified, Gate* first=nullptr,
        BindingObservation* observation=nullptr) noexcept {
    auto* d=observation?observation->Begin(verified,m.read!=nullptr,seat.count):nullptr;
    const auto check=[&](bool accepted,const char* predicate,unsigned actual=0,unsigned expected=0) {
        return d?d->Check(accepted,predicate,actual,expected):accepted;
    };
    const auto equal=[&](unsigned actual,unsigned expected,const char* predicate) {
        return check(actual==expected,predicate,actual,expected);
    };
    if (!check(verified,"verified",verified,1) || !check(m.read!=nullptr,"reader_present",m.read!=nullptr,1) ||
        !check(seat.count<=seat.routes.size(),"route_count",seat.count,unsigned(seat.routes.size())) ||
        !check(seat.identity.entry!=0,"entry_present",seat.identity.entry) ||
        !check(seat.player!=0,"player_present",seat.player) || !check(seat.identity.router!=0,"router_present",seat.identity.router)) return {};
    const bool routePresent=std::find(seat.routes.begin(),seat.routes.begin()+seat.count,VehicleRoute{17,42}) !=
        seat.routes.begin()+seat.count;
    if(d){d->routeChecked=true;d->routePresent=routePresent;}
    if(!check(routePresent,"route_17_42",routePresent,1))return {};
    bool ok=true,lastReadSuccess=false;
    const auto read=[&](std::uint64_t at,void* dst,unsigned size) {
        const bool called=at>=0x10000 && at+size<=UINT32_MAX;
        lastReadSuccess=called && m.read(m.context,unsigned(at),dst,size);
        if(!lastReadSuccess)ok=false;
        if(d){++d->reads;if(!lastReadSuccess){
            d->Check(false,called?"memory_read":"read_address");
            if(!d->failedReadIndex){d->failedReadIndex=d->reads;d->failedReadAddress=at;
                d->failedReadSize=size;d->failedReadCalled=called;}
        }}
    };
    const auto save=[&](BindingField field,unsigned value) {
        if(d && field!=BindingField::Count){const auto index=unsigned(field),bit=1u<<index;
            d->readMask|=bit;d->successMask=(d->successMask&~bit)|(lastReadSuccess?bit:0);d->values[index]=value;}
        return value;
    };
    const auto word=[&](unsigned base,unsigned offset=0,BindingField field=BindingField::Count) {
        unsigned value{};read(std::uint64_t(base)+offset,&value,4);return save(field,value);
    };
    const auto ptr=[](unsigned address){return address>=0x10000 && !(address&3u);};
    if (!equal(word(seat.player,0xc74,BindingField::PlayerCache),seat.identity.cache,"player_cache") ||
        !equal(word(seat.identity.entry,0x198,BindingField::EntryRouter),seat.identity.router,"entry_router")) {
        Admit(false,ok?Gate::Owner:Gate::Binding,first);return {};
    }
    const unsigned permission=word(seat.player,0x160,BindingField::Permission);
    if (!check((permission&(1u<<(17-11)))!=0,"player_permission",permission,1u<<(17-11))) {
        Admit(false,ok?Gate::Permission:Gate::Binding,first);return {};
    }
    // The device tests each alternative's concept veto before polling it. Accept
    // the global allow flag or an empty suppression list; leave busier modes native.
    unsigned char unrestricted{};
    read(0x1570e9c,&unrestricted,1);
    save(BindingField::Unrestricted,unrestricted);
    if (!unrestricted && word(0x1570ee0,0,BindingField::SuppressionBegin)!=word(0x1570ee4,0,BindingField::SuppressionEnd)) {
        if(d)d->Check(false,"concept_suppression",d->values[unsigned(BindingField::SuppressionBegin)],d->values[unsigned(BindingField::SuppressionEnd)]);
        Admit(false,ok?Gate::Permission:Gate::Binding,first);return {};
    }
    Binding b{};b.player=seat.player;b.router=seat.identity.router;
    const unsigned buckets=word(b.router,0x14,BindingField::Buckets), count=word(b.router,0x18,BindingField::BucketCount);
    if (!check(ptr(buckets),"bucket_pointer",buckets) || !check(count!=0,"bucket_count_nonzero",count) ||
        !check(count<=256,"bucket_count_limit",count,256)) return {};
    const unsigned sentinel=word(buckets,count*4,BindingField::Sentinel);
    unsigned node=word(buckets,(17%count)*4,BindingField::InitialNode);
    if(d)d->terminalNode=node;
    for (unsigned n=0; n<50 && ptr(node) && node!=sentinel && word(node,0,BindingField::NodeAction)!=17; ++n) {
        node=word(node,8,BindingField::NodeNext);
        if(d){++d->traversed;d->terminalNode=node;}
    }
    if (!check(ptr(node),"node_pointer",node) || !check(node!=sentinel,"node_sentinel",node,sentinel) ||
        !equal(word(node,0,BindingField::NodeAction),17,"node_action") || !equal(word(node,4,BindingField::NodeConcept),42,"node_concept")) return {};
    b.device=word(b.router,4,BindingField::Device); b.map=word(b.router,8,BindingField::Map);
    if (!check(ptr(b.device),"device_pointer",b.device) || !check(ptr(b.map),"map_pointer",b.map) ||
        !equal(word(b.device,0,BindingField::DeviceVtable),0x14196f8,"device_vtable") ||
        !equal(word(b.map,0,BindingField::MapVtable),0x141ca1c,"map_vtable")) return {};
    b.keyboard=word(b.device,8,BindingField::Keyboard);
    b.table=word(b.map,4,BindingField::Table); b.tableEnd=word(b.map,8,BindingField::TableEnd);
    if (!check(ptr(b.keyboard),"keyboard_pointer",b.keyboard) || !check(ptr(b.table),"table_pointer",b.table) ||
        !check(b.tableEnd>=b.table,"table_end_order",b.tableEnd,b.table) ||
        !equal(b.tableEnd-b.table,76u*16,"table_span") || !equal(word(b.device,0x20+4*42,BindingField::Override),0,"concept_override")) return {};
    const unsigned conceptRow=b.table+42*16;
    b.actions=word(conceptRow,0,BindingField::Actions); b.actionsEnd=word(conceptRow,4,BindingField::ActionsEnd);
    // The fingerprinted 62eee0 consumes this keyboard map before GatherHook's
    // semantic request. An empty concept row supplies no stock button; it is
    // not a permission veto. After gather, 7ed7a0 applies player permissions,
    // then 5787ff/61c400 consume ChangeEntry17's cache edge and 564870 performs
    // the native next-entry search with 6a75d0/697cf0 eligibility. None of those
    // consumers polls a keyboard binding. Admit only the proved empty vector;
    // retain owner, route, device/map, permission, suppression and override checks.
    if (!b.actions && !b.actionsEnd && ok) {
        b.unboundConcept=true;
        if(d){d->unboundConcept=true;d->returned=true;}
        return b;
    }
    if (!check(ptr(b.actions),"actions_pointer",b.actions) || !check(b.actionsEnd>b.actions,"actions_end_order",b.actionsEnd,b.actions) ||
        !check(b.actionsEnd-b.actions<=16*8,"actions_span_limit",b.actionsEnd-b.actions,16*8) ||
        !equal((b.actionsEnd-b.actions)%8,0,"actions_stride")) return {};
    for (unsigned i=b.actions;i<b.actionsEnd;i+=8) {
        auto* a=d?&d->alternatives[d->alternativeCount++]:nullptr;
        if(a)a->row=i;
        const auto alternativeWord=[&](unsigned base,unsigned offset,unsigned field) {
            const auto value=word(base,offset);
            if(a){a->values[field]=value;a->readMask|=1u<<field;if(lastReadSuccess)a->successMask|=1u<<field;}
            return value;
        };
        const auto alternativeCheck=[&](bool accepted,const char* predicate,unsigned field=6,unsigned wanted=0) {
            if(a && !accepted && !std::strcmp(a->refusal,"none")){
                a->refusal=predicate;a->actual=field<a->values.size()?a->values[field]:0;a->expected=wanted;
            }
            return accepted;
        };
        const unsigned key=alternativeWord(i,0,0);
        if (!alternativeCheck(ptr(key),"key_pointer",0) || !alternativeCheck(alternativeWord(i,4,1)==42,"row_concept",1,42) ||
            !alternativeCheck(alternativeWord(key,0,2)==0x14656f8,"key_vtable",2,0x14656f8)) continue;
        unsigned char flags[2]{};
        read(std::uint64_t(key)+12,flags,2);
        if(a){a->readMask|=24;if(lastReadSuccess)a->successMask|=24;a->values[3]=flags[0];a->values[4]=flags[1];}
        const auto scanCode=alternativeWord(key,16,5);
        if (alternativeCheck(ok,"prior_read_failure") && alternativeCheck(!flags[0],"flags12",3) &&
            alternativeCheck(!flags[1],"flags13",4) && alternativeCheck(scanCode==59,"scan16",5,59)) {
            if(d)d->returned=true;
            b.row=i; b.key=key; return b;
        }
    }
    // Rejected alternatives are not a binding refusal if a later one succeeds.
    // Once all fail, retain the first rejection unless an earlier read failed.
    if(d && d->alternativeCount && !std::strcmp(d->refusal,"none")){
        const auto& a=d->alternatives[0];d->Check(false,a.refusal,a.actual,a.expected);d->failedAlternativeRow=a.row;
    }
    return {};
}

inline bool Request(VehicleInputPlan& plan, unsigned flags) noexcept {
    if (plan.status!=VehicleInputPlanStatus::Ready || plan.count>plan.edits.size() ||
        (flags&(InputBit|ConflictingBits))) return false;
    for (unsigned n=0;n<plan.count;++n) {
        auto& e=plan.edits[n];
        if (e.offset!=0x98) continue;
        if (e.before!=flags || (e.after&ConflictingBits)) return false;
        e.after |= InputBit; plan.ownedFlags |= InputBit;
        return true;
    }
    if (plan.count==plan.edits.size()) return false;
    plan.edits[plan.count++]={0x98,flags,flags|InputBit};plan.ownedFlags=InputBit;
    return true;
}

// Stock consumes a held command as a rising edge. Repeated XR packets may
// retain that hold. A denied/gapped source must observe a new release first.
class Button {
public:
    template<class VerifyBinding>
    Binding CheckBinding(const interaction::InputFrame& input, bool eligible, VerifyBinding verify, Gate* first=nullptr) const noexcept {
        if (!Admit(armed_,Gate::Rearm,first) || !Admit(eligible,Gate::Input,first) ||
            !Admit(ValidSource(input) && input.hands[1].held==interaction::Primary,Gate::Controller,first))
            return {};
        return verify();
    }

    bool Update(const VehicleSeatIdentity& seat, const Binding& binding,
                const interaction::InputFrame& input, unsigned gather, std::int64_t now,
                std::int64_t deadline, bool eligible, Gate* first=nullptr) noexcept {
        const auto& right=input.hands[1];
        const bool held=(right.held&interaction::Primary)!=0;
        if (owner_==seat && space_==input.spaceGeneration && input.generation<sequence_) {
            Admit(false,Gate::Replay,first);
            Reset(); return false; // A replayed neutral packet cannot rearm a refused hold.
        }
        const bool continuous=valid_ && owner_==seat && (!held_ || !held || binding_==binding) &&
            space_==input.spaceGeneration && menu_==input.menu.epoch &&
            gather==gather_+1 && input.generation>=sequence_ && now>=lastNow_ && now<deadline_;
        if (!eligible || now<=0 || deadline<=now || deadline-now>100000000 || (held && !binding.CanRequest()) || !ValidSource(input)) {
            Admit(false,!eligible?Gate::Input:now<=0||deadline<=now||deadline-now>100000000?Gate::Deadline:
                held&&!binding.CanRequest()?Gate::Binding:Gate::Controller,first);
            Reset(); owner_=seat;space_=input.spaceGeneration;sequence_=input.generation; return false;
        }
        if (!continuous) {if(held)Admit(false,owner_!=seat?Gate::Owner:Gate::Continuity,first);armed_=false;held_=false;}
        if (input.generation!=sequence_) {
            if (!held) {armed_=true;held_=false;}
            else held_=armed_;
        } else if (!held && held_) {held_=false;armed_=false;}
        owner_=seat;binding_=binding;space_=input.spaceGeneration;menu_=input.menu.epoch;
        gather_=gather;sequence_=input.generation;lastNow_=now;deadline_=deadline;valid_=true;
        if(held&&!held_)Admit(false,Gate::Rearm,first);
        return held_;
    }

    bool Apply(VehicleInputOverride& patch, std::span<std::byte> cache, VehicleInputPlan& plan, unsigned flags, Gate* first=nullptr) noexcept {
        if (held_ && !Request(plan,flags)) {Admit(false,Gate::Request,first);Reset();}
        if (patch.Apply(cache,plan)) return true;
        Admit(false,Gate::Apply,first);
        Reset();
        return false;
    }
    void Reset() noexcept { valid_=false; armed_=false; held_=false; }
private:
    static bool ValidSource(const interaction::InputFrame& input) noexcept {
        const auto& right=input.hands[1];
        // Simple-controller select also maps to Primary, but has no stick click.
        constexpr unsigned required=interaction::Primary|interaction::StickClick;
        return input.focused && input.headValid && right.gripTracked && (right.active&required)==required &&
            !(right.held&~unsigned(interaction::Primary)) && !input.hands[0].held;
    }
    VehicleSeatIdentity owner_{}; Binding binding_{};
    std::uint64_t space_=0,menu_=0,sequence_=0; unsigned gather_=0;
    std::int64_t lastNow_=0,deadline_=0;
    bool valid_=false,armed_=false,held_=false;
};
}
