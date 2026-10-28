#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace fvr::bc2 {
struct ResolutionLease {
    std::uint32_t gamePid=0,ownerPid=0;
    std::uint64_t gameCreation=0,ownerCreation=0;
    std::array<std::uint32_t,4> token{};
    bool operator==(const ResolutionLease&)const=default;
};
constexpr bool ValidResolutionLease(const ResolutionLease& lease)noexcept {
    return lease.gamePid&&lease.ownerPid&&lease.gamePid!=lease.ownerPid&&lease.gameCreation&&lease.ownerCreation&&
        (lease.token[0]||lease.token[1]||lease.token[2]||lease.token[3]);
}
// Fixed x86 bootstrap contract; the caller keeps this allocation alive until
// the exported worker returns. No callback retains caller memory. Configure
// is one-shot; release is idempotent for the exact same process/session lease.
struct ResolutionRequest {
    std::uint32_t magic=0x52565246,version=2,width=0,height=0;
    ResolutionLease lease{};
    std::uint32_t passed=0,error=0,applied=0,rollbackAttempted=0,rollbackConfirmed=0;
    std::uint32_t beforeWidth=0,beforeHeight=0,afterWidth=0,afterHeight=0;
    // These two cleanup fields describe ONLY the temporary resize update hook.
    std::uint32_t updates=0,uiThread=0,hookRestored=0,callbacksDrained=0;
    // The dispatcher guard deliberately remains active after configure returns.
    // Other native Save/fullscreen paths are not intercepted by this contract.
    std::uint32_t guardInstalled=0,guardActive=0,guardDisabled=0,guardDrained=0,guardReleased=0,guardOwnerLost=0;
    std::uint32_t guardBlockedEvents=0,guardPassedEvents=0,guardOwnerMismatches=0,guardCleanupPending=0,guardNeverInstalled=0;
    // 0 none, 1 explicit release, 2 owner process ended, 3 setup failed,
    // 4 native owner changed. First reason wins.
    std::uint32_t guardReleaseReason=0;
};
static_assert(sizeof(ResolutionLease)==40&&offsetof(ResolutionRequest,lease)==16);
static_assert(offsetof(ResolutionRequest,passed)==56&&sizeof(ResolutionRequest)==160);
}
