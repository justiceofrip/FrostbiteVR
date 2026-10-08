#pragma once
#include "Bc2BodyHolster.h"
namespace fvr::bc2 {
struct BodyHolsterChallengeRow {
    HolsterSuppressionRequest request{};
    std::array<std::uint32_t,4> before{},challenged{},written{},observed{};
    bool staged=false,committed=false,restored=false,unrelatedPreserved=false;
};
// Diagnostic only, inside the current owned Gather callback. The challenge
// never reaches an engine call: the ordinary production suppression immediately
// follows Stage. Complete/RAII retires every fabricated field before return.
class BodyHolsterCacheChallenge {
public:
    ~BodyHolsterCacheChallenge(){Restore();}
    BodyHolsterCacheChallenge()=default;
    BodyHolsterCacheChallenge(const BodyHolsterCacheChallenge&)=delete;
    BodyHolsterCacheChallenge& operator=(const BodyHolsterCacheChallenge&)=delete;
    bool Stage(std::span<std::byte>,const HolsterSuppressionRequest&,HolsterInputOwner,
        const BodyFreeRightEvidence&,const interaction::HandInteraction&,std::int64_t trialDeadlineNs)noexcept;
    bool Complete(const std::optional<HolsterSuppressionReceipt>&)noexcept;
    bool Restore()noexcept;
    const BodyHolsterChallengeRow& Evidence()const noexcept{return row_;}
private:
    std::span<std::byte> cache_{};HolsterInputOwner owner_{};
    BodyHolsterChallengeRow row_{};bool active_=false;
};
}
