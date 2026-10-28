#pragma once
#include <array>
#include <cstdint>
namespace fvr::runtime {
// Local delivered-layout metadata, not a replacement for the full PairTicket.
// channelSession identifies the negotiated IPC lifetime, not legacy GPU handles.
struct SourceSlotIdentity {
    std::array<std::uint8_t,16> channelSession{};
    std::uint64_t requestId=0;
    std::uint32_t slot=UINT32_MAX,slotCount=0;
    // Exact delivered snapshot: false is canonical absence, never a failed read.
    // This is local metadata; the mapped v8/v9 layouts are unchanged. Slot 2
    // is a valid physical source only in the explicitly selected v10 path.
    bool depthPresent=false;
};
}
