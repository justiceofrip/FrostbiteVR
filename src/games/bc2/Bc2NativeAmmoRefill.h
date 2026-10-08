#pragma once
#include "Bc2MagazineAmmoMove.h"
#include <algorithm>
#include <cmath>

namespace fvr::bc2 {
struct NativeAmmoRefill {
    std::int32_t loaded=0,reserve=0,capacity=0,reloadType=0,units=0,expectedLoaded=0,expectedReserve=0;
    bool operator==(const NativeAmmoRefill&)const=default;
};
// Transfer treats a rounded configured magazine count of -1 as infinite.
// Accept only exact nonnegative bounded products, avoiding that sentinel and
// the CRT's overflow/rounding edge cases. The multiplier is not a refill size.
inline bool NativeAmmoRefillFiniteReserve(int magazines,float multiplier)noexcept {
    if(magazines<0||magazines>1000000||!std::isfinite(multiplier)||multiplier<=0)return false;
    const double product=double(magazines)*double(multiplier);
    return product>=0&&product<=1000000&&std::trunc(product)==product;
}
// Value-only contract for the inspected native Transfer: reloadType 0 moves
// one round; type 1 fills remaining capacity. It does not check capacity on
// the one-round path, so reject a full weapon BEFORE authorizing a call.
// Finite ammo/config, owner, native thread and replication are adapter proofs.
inline std::optional<NativeAmmoRefill> PlanNativeAmmoRefill(int loaded,int reserve,int capacity,int reloadType)noexcept {
    if(capacity<=0||capacity>1000000||loaded<0||loaded>=capacity||reserve<=0||reserve>1000000||
       (reloadType!=0&&reloadType!=1))return {};
    const auto units=std::min(reserve,reloadType==0?1:capacity-loaded);
    return NativeAmmoRefill{loaded,reserve,capacity,reloadType,units,loaded+units,reserve-units};
}
inline bool NativeAmmoRefillMatched(const NativeAmmoRefill& plan,
    const MagazineAmmoMoveBytes& before,const MagazineAmmoMoveBytes& after)noexcept {
    const auto expected=PlanNativeAmmoRefill(plan.loaded,plan.reserve,plan.capacity,plan.reloadType);
    if(!expected||*expected!=plan)return false;
    const auto word=[](const auto& b,unsigned at){int v=0;std::memcpy(&v,b.data()+at,4);return v;};
    if(word(before,0x7c)!=plan.loaded||word(before,0x80)!=plan.reserve||
       word(after,0x7c)!=plan.expectedLoaded||word(after,0x80)!=plan.expectedReserve)return false;
    for(unsigned n=0;n<before.size();++n)if((n<0x7c||n>=0x84)&&before[n]!=after[n])return false;
    return true;
}
}
