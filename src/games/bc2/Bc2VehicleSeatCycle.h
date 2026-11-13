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
struct Attempt {
    VehicleSeatIdentity owner{};
    std::uint64_t input=0,space=0,menu=0;
    unsigned gather=0,player=0,weak=0,active=0,held=0,leftHeld=0,permissions=0;
    unsigned flagsWritten=0,flagsObserved=0;
    std::int64_t sampledNs=0,deadlineNs=0;
    Gate first=Gate::None;
    bool verified=false,fresh=false,bindingChecked=false,requested=false,committed=false,readback=false;
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
               <<",\"readback\":"<<r.readback<<'}';
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
        {0x3ed7a0,0x89,0x10b7df8652942f7cull}, {0x286417,0x61,0xf75b00e146fcbb13ull},
        {0x131db0,0xdc,0x78a835d793179026ull}, {0x21c400,0x39,0x104a86ecb86d420cull},
        {0x212970,0x86,0x2bd05e3ed2446007ull}, {0x1787cb,0x46,0x2ee064212d0f7c79ull},
        {0x164870,0xeb,0x259e70bd8cbd87b6ull}, {0x2a75d0,0xd2,0xa56e7830e80c9e37ull},
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
    bool operator==(const Binding&) const = default;
};
inline std::optional<Binding> ReadBinding(const VehicleRouteMemory& m,
        const VehicleRouteSnapshot& seat, bool verified, Gate* first=nullptr) noexcept {
    if (!verified || !m.read || seat.count>seat.routes.size() || !seat.identity.entry ||
        !seat.player || !seat.identity.router ||
        std::find(seat.routes.begin(),seat.routes.begin()+seat.count,VehicleRoute{17,42}) ==
            seat.routes.begin()+seat.count) return {};
    bool ok=true;
    const auto read=[&](std::uint64_t at,void* dst,unsigned size) {
        if (at<0x10000 || at+size>UINT32_MAX || !m.read(m.context,unsigned(at),dst,size)) ok=false;
    };
    const auto word=[&](unsigned base,unsigned offset=0) {
        unsigned value{}; read(std::uint64_t(base)+offset,&value,4); return value;
    };
    const auto ptr=[](unsigned address){return address>=0x10000 && !(address&3u);};
    if (word(seat.player,0xc74)!=seat.identity.cache || word(seat.identity.entry,0x198)!=seat.identity.router) {
        Admit(false,ok?Gate::Owner:Gate::Binding,first);return {};
    }
    if (!(word(seat.player,0x160)&(1u<<(17-11)))) {
        Admit(false,ok?Gate::Permission:Gate::Binding,first);return {};
    }
    // The device tests each alternative's concept veto before polling it. Accept
    // the global allow flag or an empty suppression list; leave busier modes native.
    unsigned char unrestricted{};
    read(0x1570e9c,&unrestricted,1);
    if (!unrestricted && word(0x1570ee0)!=word(0x1570ee4)) {
        Admit(false,ok?Gate::Permission:Gate::Binding,first);return {};
    }
    Binding b{};b.player=seat.player;b.router=seat.identity.router;
    const unsigned buckets=word(b.router,0x14), count=word(b.router,0x18);
    if (!ptr(buckets) || !count || count>256) return {};
    const unsigned sentinel=word(buckets,count*4);
    unsigned node=word(buckets,(17%count)*4);
    for (unsigned n=0; n<50 && ptr(node) && node!=sentinel && word(node)!=17; ++n)
        node=word(node,8);
    if (!ptr(node) || node==sentinel || word(node)!=17 || word(node,4)!=42) return {};
    b.device=word(b.router,4); b.map=word(b.router,8);
    if (!ptr(b.device) || !ptr(b.map) || word(b.device)!=0x14196f8 || word(b.map)!=0x141ca1c) return {};
    b.keyboard=word(b.device,8);
    b.table=word(b.map,4); b.tableEnd=word(b.map,8);
    if (!ptr(b.keyboard) || !ptr(b.table) || b.tableEnd<b.table ||
        b.tableEnd-b.table!=76u*16 || word(b.device,0x20+4*42)!=0) return {};
    const unsigned conceptRow=b.table+42*16;
    b.actions=word(conceptRow); b.actionsEnd=word(conceptRow,4);
    if (!ptr(b.actions) || b.actionsEnd<=b.actions || b.actionsEnd-b.actions>16*8 ||
        (b.actionsEnd-b.actions)%8) return {};
    for (unsigned i=b.actions;i<b.actionsEnd;i+=8) {
        const unsigned key=word(i);
        if (!ptr(key) || word(i,4)!=42 || word(key)!=0x14656f8) continue;
        unsigned char flags[2]{};
        read(std::uint64_t(key)+12,flags,2);
        const auto scanCode=word(key,16);
        if (ok && !flags[0] && !flags[1] && scanCode==59) {
            b.row=i; b.key=key; return b;
        }
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
        if (!eligible || now<=0 || deadline<=now || deadline-now>100000000 || (held && !binding.key) || !ValidSource(input)) {
            Admit(false,!eligible?Gate::Input:now<=0||deadline<=now||deadline-now>100000000?Gate::Deadline:
                held&&!binding.key?Gate::Binding:Gate::Controller,first);
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
