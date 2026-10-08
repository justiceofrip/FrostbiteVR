#pragma once
#include "fvr/runtime/PresentationPolicy.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace fvr::ipc {
inline constexpr std::uint32_t FrameMagic=0x31514646; // FFQ1, distinct from GPU protocol
// Explicit wire poses: xyz metres, then quaternion xyzw. Never serialize C++ bools.
struct PoseWire {std::array<float,7> values{};};
struct alignas(8) FrameRequest {
    std::uint32_t magic=FrameMagic,version=2,bytes=248,flags=3;
    std::uint64_t requestId=0;
    std::int64_t deadlineQpc=0;
    std::uint32_t width=0,height=0,format=0,adapterLow=0;
    std::int32_t adapterHigh=0;
    std::uint32_t reserved0=0;
    std::uint64_t trackingGeneration=0,spaceGeneration=0;
    std::int64_t predictedNs=0;
    PoseWire referenceHead{},head{};
    std::array<PoseWire,2> eyes{};
    std::array<std::array<float,4>,2> fov{};
    float worldUnitsPerMeter=1;std::uint32_t reservedScale=0;
    std::array<std::uint64_t,2> reserved{};
};
static_assert(sizeof(FrameRequest)==248 && offsetof(FrameRequest,referenceHead)==80 && offsetof(FrameRequest,fov)==192);
static_assert(std::is_trivially_copyable_v<FrameRequest>);
bool Encode(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
    std::uint64_t requestId,std::int64_t deadlineQpc,FrameRequest&) noexcept;
bool Decode(const FrameRequest&,runtime::PresentationRequirements&,runtime::TrackingFrame&) noexcept;
enum class Phase:std::uint32_t {Idle,Requested,Rendering,Ready,Delivered,Complete,Cancelled,Closed};
enum class Outcome {Pending,Consumed,Discarded};
// Only access under the interprocess mutex; no unaligned x86 atomic assumptions.
struct alignas(8) FrameSlot {
    Phase phase=Phase::Idle;
    std::uint32_t consumed=0;
    FrameRequest request{};
    graphics::TextureDescriptor descriptor{};
    graphics::PairTicket ticket{};
    std::uint64_t reserved=0;
};
static_assert(sizeof(FrameSlot)==424 && offsetof(FrameSlot,ticket)==320);
static_assert(std::is_trivially_copyable_v<FrameSlot>);
bool Begin(FrameSlot&,const FrameRequest&,std::int64_t now) noexcept;
bool Take(FrameSlot&,std::int64_t now,FrameRequest&) noexcept;
bool Publish(FrameSlot&,std::uint64_t id,std::int64_t now,
    const graphics::TextureDescriptor&,const graphics::PairTicket&) noexcept;
bool Deliver(FrameSlot&,std::uint64_t id,std::int64_t now,
    graphics::TextureDescriptor&,graphics::PairTicket&) noexcept;
void Cancel(FrameSlot&,std::uint64_t id) noexcept;
void Skip(FrameSlot&,std::uint64_t id) noexcept;
bool Acknowledge(FrameSlot&,std::uint64_t id,bool consumed) noexcept;
Outcome Reap(FrameSlot&,std::uint64_t id,std::int64_t now) noexcept;
}
