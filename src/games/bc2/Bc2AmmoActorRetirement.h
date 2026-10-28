#pragma once
#include "Bc2AmmoReserve.h"
#include "fvr/interaction/AmmunitionLedger.h"

namespace fvr::bc2 {
// This is supplied only while the native invocation-entry gate excludes all
// other callbacks. A changed controller/weapon/space epoch alone is not proof
// that a native actor ended. No member authorizes a native ammunition write.
struct AmmoResourceActorDrain {
    ReloadHoldIdentity expected{};
    std::optional<interaction::AmmunitionCommand> pending;
    std::uint64_t callbackRevision=0,lifetimeEndEpoch=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool callbacksDrained=false;
};
struct AmmoResourceActorRetirement {
    std::uint64_t event=0,callbackRevision=0,lifetimeEndEpoch=0;
    std::int64_t observedNs=0;
    ReloadHoldIdentity previous{},replacement{};
    std::optional<interaction::AmmunitionCommand> pending;
    std::size_t retiredItems=0;
    bool nativeDispatched=false;
};
inline bool AmmoResourceSameNativeActor(const ReloadHoldIdentity& a,const ReloadHoldIdentity& b)noexcept {
    return a.owner.player==b.owner.player&&a.owner.soldier==b.owner.soldier&&a.owner.weak==b.owner.weak&&
        a.serverPlayer==b.serverPlayer&&a.serverSoldier==b.serverSoldier;
}
inline bool AmmoResourceActorReplacement(const ReloadHoldIdentity& a,const ReloadHoldIdentity& b)noexcept {
    return a.owner.soldier>=0x10000&&a.owner.weak>=0x10000&&a.owner.actorGeneration&&a.serverSoldier>=0x10000&&
        b.owner.soldier>=0x10000&&b.owner.weak>=0x10000&&b.owner.actorGeneration>a.owner.actorGeneration&&
        (b.owner.soldier!=a.owner.soldier||b.owner.weak!=a.owner.weak)&&
        b.serverSoldier>=0x10000&&b.serverSoldier!=a.serverSoldier;
}
inline bool AmmoResourceReplacementFresh(const Bc2AmmoReserveLease& r,std::int64_t now)noexcept {
    const auto& n=r.identity;const auto& o=n.owner;
    if(!r.verified||!r.allThreeIdle||!r.sequence||r.observedNs<=0||now<r.observedNs||now>=r.deadlineNs||
        r.deadlineNs-r.observedNs>200000000||r.loaded<0||r.reserve<0||r.reserve>1000000||
        r.capacity<=0||r.capacity>1000000||r.loaded>r.capacity||
        o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||o.weapon<0x10000||
        !o.actorGeneration||!o.equipGeneration||!o.space||n.serverPlayer<0x10000||n.serverSoldier<0x10000||n.serverItem<0x10000)return false;
    for(unsigned a=0;a<3;++a){if(n.firing[a]<0x10000)return false;
        for(unsigned b=0;b<a;++b)if(n.firing[a]==n.firing[b])return false;}
    return true;
}
inline bool AmmoResourceRetirementValid(const AmmoResourceActorRetirement& r)noexcept {
    return r.event&&r.callbackRevision&&r.lifetimeEndEpoch&&r.observedNs>0&&r.retiredItems&&
        AmmoResourceActorReplacement(r.previous,r.replacement);
}
}
