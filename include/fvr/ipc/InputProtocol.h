#pragma once
#include "fvr/ipc/FrameProtocol.h"
#include "fvr/interaction/ControllerInput.h"
namespace fvr::ipc {
struct ControllerWire {
    std::uint32_t flags=0,active=0,held=0,reserved=0; // v2: touchActive low8, touched next8.
    PoseWire grip{},aim{};
    float stickX=0,stickY=0,trigger=0,squeeze=0;
};
struct alignas(8) InputPacket {
    std::uint32_t magic=0x31494646,version=2,bytes=288,flags=0;
    std::uint64_t generation=0,spaceGeneration=0;
    std::int64_t predictedNs=0,deadlineQpc=0;
    float worldUnitsPerMeter=1;std::uint32_t reserved=0;
    PoseWire referenceHead{},head{};
    std::array<ControllerWire,2> hands{};
};
static_assert(sizeof(ControllerWire)==88&&sizeof(InputPacket)==288&&offsetof(InputPacket,hands)==112);
static_assert(std::is_trivially_copyable_v<InputPacket>);
bool EncodeInput(const interaction::InputFrame&,std::int64_t deadline,InputPacket&) noexcept;
bool DecodeInput(const InputPacket&,interaction::InputFrame&) noexcept;
}
