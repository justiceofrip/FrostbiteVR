#pragma once
#include "Bc2FeedEquipmentAdapter.h"
#include "fvr/interaction/AmmoSupplyVisual.h"

namespace fvr::bc2 {
// Exact native reserve and enrolled pouch geometry. This is not a second belt
// store: the existing FeedAmmunition command owns the eventual reserve debit.
struct Bc2FeedBodyAmmo {
    FeedEquipmentNativeView native{};
    std::uint64_t registrationKey=0,lifetime=0;
    interaction::FeedSupplyKey supply{};
};
std::optional<interaction::AmmoSupplySource> Bc2FeedBodySource(const Bc2FeedBodyAmmo&,
    const interaction::HandInteractionSample&,std::int64_t now)noexcept;
bool Bc2FeedBodyRetained(const Bc2FeedBodyAmmo&,const Bc2FeedBodyAmmo&)noexcept;
interaction::AmmoSupplyConfig Bc2FeedSupplyConfig()noexcept;
bool Bc2FeedSupplyContact(const math::Matrix4&)noexcept;

struct Bc2FeedSupplyCustody {
    Bc2FeedBodyAmmo origin{};
    interaction::AmmoSupplySource source{};
    interaction::AmmoSupplyObject object{};
    Bc2FeedBodyAmmo context{};
    interaction::HandInteractionOwner handOwner{};
    // Pickup reserves representation, not native rounds. Release/tracking loss
    // relinquishes its hand only. An exact feed command then owns settlement.
    std::optional<interaction::FeedAmmunitionCommand> pending;
};
class Bc2FeedAmmoSupply {
public:
    Bc2FeedAmmoSupply()noexcept:pickup_(Bc2FeedSupplyConfig()){}
    bool Update(const Bc2FeedBodyAmmo&,const interaction::HandInteractionSample&,
        const std::optional<math::Matrix4>& bodyFromHand,bool grip,bool acquire,
        interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    void Suspend(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    bool Ready(const Bc2FeedBodyAmmo&,const interaction::HandInteractionSample&)const noexcept;
    bool Bind(const interaction::FeedAmmunitionCommand&,const Bc2FeedBodyAmmo&,
        const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    bool Complete(const interaction::FeedAmmunitionReceipt&)noexcept;
    bool Migrate(const interaction::FeedContextMigration&,const Bc2FeedBodyAmmo&,
        const interaction::HandInteractionSample&)noexcept;
    const std::optional<Bc2FeedSupplyCustody>& Custody()const noexcept{return custody_;}
    const std::optional<interaction::AmmoSupplyObject>& Held()const noexcept{return pickup_.Held();}
private:
    interaction::AmmoSupply pickup_;
    std::optional<Bc2FeedSupplyCustody> custody_;
};
}
