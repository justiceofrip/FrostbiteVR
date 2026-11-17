#pragma once
#include "Bc2AmmoReserve.h"
#include "Bc2MenuState.h"
#include "fvr/graphics/AmmoCounter.h"
#include <algorithm>
#include <optional>
namespace fvr::bc2 {
// Caller supplies current, independently observed gameplay/held ownership.
// This adapter does not read native memory, infer chamber state or mutate ammo.
struct AmmoCounterOwner {
    ReloadStateOwner owner{};
    std::int64_t observedNs=0,deadlineNs=0;
    bool alive=false,onFoot=false,held=false;
    std::uint64_t sceneOwnerEpoch=0,sceneEpoch=0,hudGeneration=0;
};
// Separate presentation lifetime: never increments a native equipment epoch
// and never changes a reserve, reload, hand claim or input decision.
class AmmoCounterHudLifetime {
public:
    void Bind(AmmoCounterOwner& current,const MenuPresentationSnapshot* scene,std::int64_t now)noexcept {
        current.sceneOwnerEpoch=current.sceneEpoch=current.hudGeneration=0;
        if(!scene||!MenuPresentationFresh(*scene,now)||scene->observation.state!=MenuPresentationState::Playing||
           !current.alive||!current.onFoot||!current.held||current.observedNs<=0||
           current.observedNs>now||current.deadlineNs<=now){bound_=false;return;}
        if(!bound_||lastOwner_!=current.owner||ownerEpoch_!=scene->ownerEpoch||sceneEpoch_!=scene->sceneEpoch)++generation_;
        bound_=true;lastOwner_=current.owner;ownerEpoch_=scene->ownerEpoch;sceneEpoch_=scene->sceneEpoch;
        current.sceneOwnerEpoch=ownerEpoch_;current.sceneEpoch=sceneEpoch_;current.hudGeneration=generation_;
        current.deadlineNs=(std::min)(current.deadlineNs,scene->deadlineNs);
    }
private:
    ReloadStateOwner lastOwner_{};std::uint64_t ownerEpoch_=0,sceneEpoch_=0,generation_=0;bool bound_=false;
};
inline graphics::AmmoCounterSample AmmoCounterHostSample(const std::optional<Bc2AmmoReserveLease>& ammo,
    const AmmoCounterOwner& current,std::int64_t now)noexcept {
    if(!current.alive||!current.onFoot||!current.held||
        current.observedNs<=0||current.observedNs>now||current.deadlineNs<=now||
        current.deadlineNs-current.observedNs>graphics::AmmoCounterOwnerMaxAgeNs)return {};
    const auto& o=current.owner;
    if(o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||o.weapon<0x10000||
        !o.actorGeneration||!o.equipGeneration||!o.space)return {};
    const graphics::AmmoCounterSample ownerOnly{0,current.observedNs,current.deadlineNs,
        o.actorGeneration,o.equipGeneration,o.space,0,0,0,graphics::AmmoCounterOwnerOnly};
    // Missing/rejected ammunition cannot authorize gameplay or become a fresh
    // count. It can still leave independently observed held ownership intact.
    if(!ammo||!ammo->verified||ammo->identity.owner!=o)return ownerOnly;
    const auto& a=*ammo;
    if(!a.sequence||a.observedNs<=0||a.observedNs>now||a.deadlineNs<=now||
        a.deadlineNs-a.observedNs>250000000)return ownerOnly;
    if(a.identity.serverPlayer<0x10000||a.identity.serverSoldier<0x10000||a.identity.serverItem<0x10000||
        a.identity.firing[0]<0x10000||a.identity.firing[1]<0x10000||a.identity.firing[2]<0x10000)return ownerOnly;
    const auto deadline=(std::min)({a.deadlineNs,current.deadlineNs,
        a.observedNs+(std::min)(a.deadlineNs-a.observedNs,graphics::AmmoCounterMaxAgeNs)});
    graphics::AmmoCounterSample result{a.sequence,a.observedNs,deadline,o.actorGeneration,o.equipGeneration,o.space,
                                      a.loaded,a.reserve,a.capacity};
    return graphics::AmmoCounterFresh(result,now)?result:ownerOnly;
}
inline graphics::AmmoCounterSample SceneAmmoCounterHostSample(const std::optional<Bc2AmmoReserveLease>& ammo,
    const AmmoCounterOwner& current,const MenuPresentationSnapshot* scene,std::int64_t now)noexcept {
    if(!scene||!MenuPresentationFresh(*scene,now)||scene->observation.state!=MenuPresentationState::Playing||
       !current.hudGeneration||current.sceneOwnerEpoch!=scene->ownerEpoch||current.sceneEpoch!=scene->sceneEpoch)return {};
    // Validate the EXACT native owner before translating only the HUD wire ID.
    auto sample=AmmoCounterHostSample(ammo,current,now);
    if(!graphics::AmmoCounterFresh(sample,now)&&!graphics::AmmoCounterOwnerOnlyFresh(sample,now))return {};
    sample.equipmentGeneration=current.hudGeneration;
    sample.deadlineNs=(std::min)(sample.deadlineNs,scene->deadlineNs);return sample;
}
// The reserve reader may perform fresh native reads. Capture validation time
// afterward: a timestamp taken by the caller before read() makes every new
// observation appear future-dated. Never change either source's original
// observation/deadline. The caller still verifies owner-publication stability.
template<class Read,class Clock>
graphics::AmmoCounterSample ReadAmmoCounterHostSample(const AmmoCounterOwner& current,
    Read&& read,Clock&& clock)noexcept {
    const auto ammo=read();
    const auto validatedNs=clock();
    return AmmoCounterHostSample(ammo,current,validatedNs);
}
} // namespace fvr::bc2
