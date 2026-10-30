#pragma once
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/AmmunitionLedger.h"
namespace fvr::interaction {
// An adapter constructs this only from the completed ledger command and three
// independent own-Update witnesses. Counts alone never authorize retained debt.
struct WeaponCycleAmmoRebaseProvenance {
    HandInteractionOwner owner{};HandInteractionKey item{},mechanism{};
    std::uint64_t cycle=0,shot=0,pause=0,previousGuardEpoch=0,guardEpoch=0,drainRevision=0;
    AmmunitionReceipt receipt{};
    std::array<std::uint64_t,3> ownUpdates{};
    bool operator==(const WeaponCycleAmmoRebaseProvenance&)const=default;
};
struct WeaponCycleDebt {
    HandInteractionOwner owner{};
    HandInteractionKey item{},mechanism{};
    std::uint64_t cycle=0,shot=0,guardEpoch=0;
    int loaded=0,reserve=0,capacity=0;
    std::optional<WeaponCycleAmmoRebaseProvenance> ammunitionRebase;
    bool operator==(const WeaponCycleDebt&)const=default;
};
// Three independently observed, input-guarded native Updates. These may still
// be cycling: they grant physical manipulation, never native-ready or ammunition.
struct WeaponCycleGuardedAdvance {
    std::uint64_t guardEpoch=0;
    std::array<std::uint64_t,3> updates{};
    int loaded=0,reserve=0,capacity=0;
    bool operator==(const WeaponCycleGuardedAdvance&)const=default;
};
struct WeaponCycleRecoveryGrant {
    WeaponCycleDebt debt{};
    HandInteractionSample neutral{};
    std::uint64_t nonce=0;
    bool operator==(const WeaponCycleRecoveryGrant& other)const noexcept {
        const auto& a=neutral;const auto& b=other.neutral;
        return debt==other.debt&&nonce==other.nonce&&a.owner==b.owner&&a.sequence==b.sequence&&
            a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&a.nowNs==b.nowNs&&
            a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
    }
};
// Distinct authority for an unfinished physical debt whose native copies
// have independently converged to idle. No native-held flag or conversion.
struct WeaponCycleIdleDebtLease {
    HandInteractionOwner owner{};HandInteractionKey item{},mechanism{};
    std::uint64_t cycle=0,shot=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    WeaponCycleRecoveryGrant grant{};
    std::array<std::uint64_t,3> first{},second{};
    bool operator==(const WeaponCycleIdleDebtLease&)const=default;
};
}
