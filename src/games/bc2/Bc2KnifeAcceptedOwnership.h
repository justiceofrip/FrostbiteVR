#pragma once
#include "KnifeNativeReader.h"
namespace fvr::bc2::private_knife {
enum class AcceptedKnifeOwnership {Same,Unknown,Foreign};
// Existing accepted-operation authority only. Current selectors, input leases,
// phase timers and transient prediction state are deliberately absent. Unknown
// retains only automatic-action suppression; it cannot admit any new action.
inline AcceptedKnifeOwnership ReadAcceptedKnifeOwnership(const ReloadStateMemory& memory,
    const Anchor& a,unsigned firingVtable)noexcept {
    const auto& o=a.original.client.owner;const auto& v=a.original.links;
    if(!memory.read||!firingVtable||a.knifeSlot>=v.clientItems.size())return AcceptedKnifeOwnership::Unknown;
    struct Join {std::uint64_t address;unsigned expected;};
    std::array<Join,40> joins{};unsigned count=0;
    const auto add=[&](std::uint64_t at,unsigned expected){joins[count++]={at,expected};};
    add(std::uint64_t(o.player)+0xc54,o.weak);add(o.weak,o.soldier+4);
    add(std::uint64_t(o.player)+0xc68,o.soldier);add(std::uint64_t(o.soldier)+0x220,o.player);
    add(std::uint64_t(o.soldier)+((a.original.client.soldierFlags&1)?0x24c:0x248),v.clientInventory);
    add(std::uint64_t(o.soldier)+0x260,v.clientBegin);add(std::uint64_t(o.soldier)+0x264,v.clientEnd);
    add(std::uint64_t(v.player)+0xc3c,v.soldier);add(std::uint64_t(v.player)+0xc6c,v.soldier);
    add(std::uint64_t(v.soldier)+0x220,v.player);add(std::uint64_t(v.soldier)+0x2b4,v.inventory);
    add(std::uint64_t(v.soldier)+0x2b8,v.begin);add(std::uint64_t(v.soldier)+0x2bc,v.end);
    add(std::uint64_t(v.clientBegin)+4*a.knifeSlot,a.knifeWeapon);
    add(std::uint64_t(v.begin)+4*a.knifeSlot,v.items[a.knifeSlot]);
    add(std::uint64_t(a.knifeWeapon)+4,a.knifeData);add(std::uint64_t(v.items[a.knifeSlot])+4,a.knifeData);
    add(std::uint64_t(a.knifeData)+0x98,a.knifeFiringData);
    add(std::uint64_t(a.knifeWeapon)+0x3c,a.knifeFiring[0]);add(std::uint64_t(a.knifeWeapon)+0x40,a.knifeFiring[1]);
    add(std::uint64_t(v.items[a.knifeSlot])+0x10,a.knifeFiring[2]);
    for(auto firing:a.knifeFiring){add(firing,firingVtable);add(std::uint64_t(firing)+8,a.knifeFiringData);}
    std::array<unsigned,40> before{},after{};std::uint8_t flags=0,flagsAgain=0;
    const detail::Reader reader{memory};
    if(!reader.Get(std::uint64_t(o.soldier)+0x114,flags))return AcceptedKnifeOwnership::Unknown;
    for(unsigned n=0;n<count;++n)if(!reader.Get(joins[n].address,before[n]))return AcceptedKnifeOwnership::Unknown;
    for(unsigned n=0;n<count;++n)if(!reader.Get(joins[n].address,after[n]))return AcceptedKnifeOwnership::Unknown;
    if(!reader.Get(std::uint64_t(o.soldier)+0x114,flagsAgain)||before!=after||flags!=flagsAgain)
        return AcceptedKnifeOwnership::Unknown;
    if((flags&~0x18u)!=(a.original.client.soldierFlags&~0x18u))return AcceptedKnifeOwnership::Foreign;
    for(unsigned n=0;n<count;++n)if(before[n]!=joins[n].expected)return AcceptedKnifeOwnership::Foreign;
    return AcceptedKnifeOwnership::Same;
}
}
