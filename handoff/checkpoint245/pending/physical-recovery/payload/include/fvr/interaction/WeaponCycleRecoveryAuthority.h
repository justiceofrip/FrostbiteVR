#pragma once
#include "fvr/interaction/HandInteraction.h"
namespace fvr::interaction {
struct WeaponCycleDebt {
    HandInteractionOwner owner{};
    HandInteractionKey item{},mechanism{};
    std::uint64_t cycle=0,shot=0,guardEpoch=0;
    int loaded=0,reserve=0,capacity=0;
    bool operator==(const WeaponCycleDebt&)const=default;
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
