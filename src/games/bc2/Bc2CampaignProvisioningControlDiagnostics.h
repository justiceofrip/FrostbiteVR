#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fvr::bc2::campaignProvisioning {
enum class ControlRefusalReason:std::uint32_t {None,MutexUnavailable,ActiveCallbacks};
// Captured at this Control acquisition, not a later inspection of the gate.
// MutexUnavailable means callback counts are UNKNOWN: their zero values must
// not be interpreted as an idle gate. No unlocked protected state is read.
// ActiveCallbacks owns the recursive mutex, so all these callbacks are on the
// acquiring thread. That thread is the sample's already-captured seat thread.
struct ControlAcquisition {
    ControlRefusalReason reason=ControlRefusalReason::None;
    std::uint32_t callbacks=0,updateDepth=0,gatherDepth=0;
};
struct ControlRefusalSample {
    // Existing seat Boundary timestamp, captured before source lookup/control;
    // NOT a new clock sample or an exact acquisition timestamp.
    std::int64_t boundaryNs=0;
    std::uint64_t seatSequence=0;
    std::uint32_t thread=0,caller=0,seat=0,nativeFiringDepth=0,seatDepth=0;
    ControlAcquisition acquisition{};
    std::uint32_t reserved=0;
};
struct ControlRefusalDetails {
    // Index 0: MutexUnavailable; index 1: ActiveCallbacks. First sample per
    // cause, not a callback trace. Saturated 31 is a lower bound.
    std::array<std::uint32_t,2> counts{};
    std::uint32_t saturatedMask=0,reserved=0;
    std::array<ControlRefusalSample,2> first{};
};
static_assert(std::is_trivially_copyable_v<ControlRefusalDetails>);
static_assert(sizeof(ControlAcquisition)==16&&sizeof(ControlRefusalSample)==56);
static_assert(offsetof(ControlRefusalSample,acquisition)==36&&sizeof(ControlRefusalDetails)==128);

// All access is serialized by the EXISTING provisioning runtime gate. Service
// alone admits the token/window and freezes this data with its terminal receipt.
// No atomic retries, allocation, lock, native reads or scheduling authority.
class ControlRefusalDiagnostics {
public:
    static constexpr std::uint32_t Limit=31;
    void Reset()noexcept{details_={};}
    void Record(const ControlRefusalSample& sample)noexcept{
        const auto reason=std::uint32_t(sample.acquisition.reason);
        if(reason<1||reason>2)return;
        const auto index=reason-1;
        auto& count=details_.counts[index];
        if(!count)details_.first[index]=sample;
        if(count<Limit)++count;
        if(count==Limit)details_.saturatedMask|=1u<<index;
    }
    const ControlRefusalDetails& Read()const noexcept{return details_;}
private:
    ControlRefusalDetails details_{};
};
}
