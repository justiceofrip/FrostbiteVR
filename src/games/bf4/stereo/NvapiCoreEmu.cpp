#include "NvapiCoreEmu.h"

#include <cstdio>
#include <cstring>

namespace fvr::bf4::stereo {

using namespace nvapi;

namespace {
void CopyShort(char* out, const char* text) {
    std::snprintf(out, kShortString, "%s", text);
}

std::uint32_t StructSize(std::uint32_t version) { return version & 0xFFFFu; }
std::uint32_t StructVer(std::uint32_t version) { return version >> 16; }

// NV_GET_CURRENT_SLI_STATE_V1 is 7 NvU32 (28 bytes); V2 adds numVRSLIGpus (32 bytes).
constexpr std::uint32_t kSliV1Size = 7 * 4, kSliV2Size = 8 * 4;
// NV_DISPLAY_DRIVER_VERSION: version, drvVersion, bldChangeListNum, two short strings.
constexpr std::uint32_t kDriverVersionSize = 3 * 4 + 2 * kShortString;
} // namespace

bool NvapiCoreEmu::Implements(std::uint32_t f) noexcept {
    switch (f) {
    case id::GetInterfaceVersionString: case id::GetErrorMessage: case id::SYS_GetDriverAndBranchVersion:
    case id::EnumPhysicalGPUs: case id::EnumLogicalGPUs: case id::GetPhysicalGPUsFromLogicalGPU:
    case id::GetLogicalGPUFromPhysicalGPU: case id::D3D_GetCurrentSLIState: case id::DISP_GetGDIPrimaryDisplayId:
    case id::EnumNvidiaDisplayHandle: case id::GetDisplayDriverVersion: case id::GPU_GetFullName:
    case id::GPU_GetPCIIdentifiers: case id::GPU_GetPhysicalFrameBufferSize: case id::GPU_GetGpuCoreCount:
    case id::GPU_GetBusType:
        return true;
    default:
        return false;
    }
}

int NvapiCoreEmu::GetInterfaceVersionString(char* out) const {
    if (!out) return kInvalidArgument;
    CopyShort(out, "NVidia Complete Version 1.10");
    return kOk;
}

int NvapiCoreEmu::GetErrorMessage(int status, char* out) const {
    if (!out) return kInvalidArgument;
    switch (status) {
    case kOk: CopyShort(out, "NVAPI_OK"); break;
    case kError: CopyShort(out, "NVAPI_ERROR"); break;
    case kNoImplementation: CopyShort(out, "NVAPI_NO_IMPLEMENTATION"); break;
    case kInvalidArgument: CopyShort(out, "NVAPI_INVALID_ARGUMENT"); break;
    case kEndEnumeration: CopyShort(out, "NVAPI_END_ENUMERATION"); break;
    case kInvalidHandle: CopyShort(out, "NVAPI_INVALID_HANDLE"); break;
    case kIncompatibleStructVersion: CopyShort(out, "NVAPI_INCOMPATIBLE_STRUCT_VERSION"); break;
    default: std::snprintf(out, kShortString, "NVAPI_ERROR %d", status); break;
    }
    return kOk;
}

int NvapiCoreEmu::GetDriverAndBranchVersion(std::uint32_t* version, char* branch) const {
    if (!version || !branch) return kInvalidArgument;
    *version = config_.driverVersion;
    CopyShort(branch, config_.branch);
    return kOk;
}

int NvapiCoreEmu::EnumPhysicalGpus(void** handles, std::uint32_t* count) const {
    if (!handles || !count) return kInvalidArgument;
    handles[0] = PhysicalGpu();
    *count = 1;
    return kOk;
}

int NvapiCoreEmu::EnumLogicalGpus(void** handles, std::uint32_t* count) const {
    if (!handles || !count) return kInvalidArgument;
    handles[0] = LogicalGpu();
    *count = 1;
    return kOk;
}

int NvapiCoreEmu::GetPhysicalGpusFromLogicalGpu(void* logical, void** handles, std::uint32_t* count) const {
    if (!handles || !count) return kInvalidArgument;
    if (logical != LogicalGpu()) return kInvalidHandle;
    handles[0] = PhysicalGpu();
    *count = 1;
    return kOk;
}

int NvapiCoreEmu::GetLogicalGpuFromPhysicalGpu(void* physical, void** logical) const {
    if (!logical) return kInvalidArgument;
    if (physical != PhysicalGpu()) return kInvalidHandle;
    *logical = LogicalGpu();
    return kOk;
}

int NvapiCoreEmu::GetCurrentSliState(void* device, void* state) const {
    if (!device || !state) return kInvalidArgument;
    std::uint32_t version = 0;
    std::memcpy(&version, state, 4);
    const std::uint32_t size = StructSize(version);
    if ((size != kSliV1Size && size != kSliV2Size) || StructVer(version) < 1 || StructVer(version) > 2)
        return kIncompatibleStructVersion;
    // Single GPU: one AFR group, always index 0. numVRSLIGpus (V2) = 0.
    const std::uint32_t fields[8] = {version, 1, 1, 0, 0, 0, 0, 0};
    std::memcpy(state, fields, size);
    return kOk;
}

int NvapiCoreEmu::GetGdiPrimaryDisplayId(std::uint32_t* displayId) const {
    if (!displayId) return kInvalidArgument;
    *displayId = config_.primaryDisplayId;
    return kOk;
}

int NvapiCoreEmu::EnumNvidiaDisplayHandle(std::uint32_t index, void** display) const {
    if (!display) return kInvalidArgument;
    if (index != 0) return kEndEnumeration;
    *display = Display();
    return kOk;
}

int NvapiCoreEmu::GetDisplayDriverVersion(void* display, void* out) const {
    if (!out) return kInvalidArgument;
    if (display && display != Display()) return kInvalidHandle; // NVAPI_DEFAULT_HANDLE (0) allowed
    std::uint32_t version = 0;
    std::memcpy(&version, out, 4);
    if (StructSize(version) != kDriverVersionSize || StructVer(version) != 1) return kIncompatibleStructVersion;
    auto* p = static_cast<unsigned char*>(out);
    std::memset(p + 4, 0, kDriverVersionSize - 4);
    std::memcpy(p + 4, &config_.driverVersion, 4);
    CopyShort(reinterpret_cast<char*>(p + 12), config_.branch);
    CopyShort(reinterpret_cast<char*>(p + 12 + kShortString), config_.gpuName);
    return kOk;
}

int NvapiCoreEmu::GetFullName(void* gpu, char* out) const {
    if (!out) return kInvalidArgument;
    if (gpu != PhysicalGpu()) return kInvalidHandle;
    CopyShort(out, config_.gpuName);
    return kOk;
}

int NvapiCoreEmu::GetPciIdentifiers(void* gpu, std::uint32_t* deviceId, std::uint32_t* subSystemId,
                                    std::uint32_t* revisionId, std::uint32_t* extDeviceId) const {
    if (!deviceId || !subSystemId || !revisionId || !extDeviceId) return kInvalidArgument;
    if (gpu != PhysicalGpu()) return kInvalidHandle;
    *deviceId = (config_.pciDevice << 16) | 0x10DEu; // device:vendor, as NVAPI reports it
    *subSystemId = 0;
    *revisionId = 0xA1;
    *extDeviceId = config_.pciDevice;
    return kOk;
}

int NvapiCoreEmu::GetPhysicalFrameBufferSize(void* gpu, std::uint32_t* kib) const {
    if (!kib) return kInvalidArgument;
    if (gpu != PhysicalGpu()) return kInvalidHandle;
    *kib = config_.frameBufferKiB;
    return kOk;
}

int NvapiCoreEmu::GetGpuCoreCount(void* gpu, std::uint32_t* count) const {
    if (!count) return kInvalidArgument;
    if (gpu != PhysicalGpu()) return kInvalidHandle;
    *count = config_.coreCount;
    return kOk;
}

int NvapiCoreEmu::GetBusType(void* gpu, int* busType) const {
    if (!busType) return kInvalidArgument;
    if (gpu != PhysicalGpu()) return kInvalidHandle;
    *busType = 3; // NVAPI_GPU_BUS_TYPE_PCI_EXPRESS
    return kOk;
}

} // namespace fvr::bf4::stereo
