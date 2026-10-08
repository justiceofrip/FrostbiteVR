#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace fvr::bc2 {
enum class MagazineAmmoMoveKind : unsigned {Remove,Return};
struct MagazineAmmoMove {
    MagazineAmmoMoveKind kind{};
    std::int32_t loaded=0,reserve=0,capacity=0,rounds=0,delta=0,expectedLoaded=0;
    bool operator==(const MagazineAmmoMove&)const=default;
};
// BC2's inspected secondary firing interface accepts a signed loaded-round
// delta, but does NOT prevent underflow, integer overflow or infinite capacity.
// Admit only whole-magazine removal or return into an empty well. This value
// plan grants no native invocation, owner, thread or replication authority.
inline std::optional<MagazineAmmoMove> PlanMagazineAmmoMove(MagazineAmmoMoveKind kind,
    std::int32_t loaded,std::int32_t reserve,std::int32_t capacity,std::int32_t rounds)noexcept {
    if(capacity<=0||capacity>1000000||loaded<0||loaded>capacity||reserve<0||reserve>1000000||
       rounds<=0||rounds>capacity)return {};
    if(kind==MagazineAmmoMoveKind::Remove){
        if(loaded!=rounds)return {};
        return MagazineAmmoMove{kind,loaded,reserve,capacity,rounds,-rounds,0};
    }
    if(kind!=MagazineAmmoMoveKind::Return||loaded!=0)return {};
    return MagazineAmmoMove{kind,loaded,reserve,capacity,rounds,rounds,rounds};
}

using MagazineAmmoMoveBytes=std::array<std::byte,0xb0>;
// Check the complete firing object surrounding an already executed operation.
// In particular reserve, state/timers, flags and both vtables must be unchanged.
// A match is a local postcondition, NOT a server receipt or client convergence.
inline bool MagazineAmmoMoveMatched(const MagazineAmmoMove& plan,
    const MagazineAmmoMoveBytes& before,const MagazineAmmoMoveBytes& after)noexcept {
    const auto checked=PlanMagazineAmmoMove(plan.kind,plan.loaded,plan.reserve,plan.capacity,plan.rounds);
    if(!checked||*checked!=plan)return false;
    const auto word=[](const MagazineAmmoMoveBytes& raw,std::size_t at){
        std::int32_t value=0;std::memcpy(&value,raw.data()+at,4);return value;
    };
    if(word(before,0x7c)!=plan.loaded||word(before,0x80)!=plan.reserve||
       word(after,0x7c)!=plan.expectedLoaded)return false;
    for(std::size_t n=0;n<before.size();++n)
        if((n<0x7c||n>=0x80)&&before[n]!=after[n])return false;
    return true;
}
}
