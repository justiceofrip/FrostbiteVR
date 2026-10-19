#pragma once
// Emulated NVAPI core/system answers for a machine WITHOUT an NVIDIA driver (stereo probe
// with "report an NVIDIA GPU"). Probe 1 on the real game showed BF4 asking for driver
// version, GPU enumeration, SLI state and the primary display right after
// NvAPI_Initialize, and never reaching a stereo call while those returned "not available".
//
// One GTX 1080-class GPU on a 3D Vision-era driver (425.31, the last with 3D Vision).
// Platform-neutral; the nvapi64.dll shim forwards these IDs here only when no real
// nvapi64.dll is present. Struct layouts follow NVIDIA's public nvapi.h.
#include "NvapiIds.h"

#include <cstdint>

namespace fvr::bf4::stereo {

class NvapiCoreEmu {
public:
    struct Config {
        std::uint32_t driverVersion = 42531; // r425.31
        const char* branch = "r425_00";
        const char* gpuName = "GeForce GTX 1080";
        std::uint32_t pciDevice = 0x1B80;     // matches the DXGI vendor report
        std::uint32_t frameBufferKiB = 8u * 1024u * 1024u;
        std::uint32_t coreCount = 2560;
        std::uint32_t primaryDisplayId = 0x00010000;
    };

    NvapiCoreEmu() = default;
    explicit NvapiCoreEmu(Config config) : config_(config) {}

    static bool Implements(std::uint32_t functionId) noexcept;

    // Opaque handles the game passes back; never dereferenced by the game.
    static void* PhysicalGpu() noexcept { return reinterpret_cast<void*>(std::uintptr_t{0x6E560100}); }
    static void* LogicalGpu() noexcept { return reinterpret_cast<void*>(std::uintptr_t{0x6E560200}); }
    static void* Display() noexcept { return reinterpret_cast<void*>(std::uintptr_t{0x6E560300}); }

    int GetInterfaceVersionString(char* out) const;
    int GetErrorMessage(int status, char* out) const;
    int GetDriverAndBranchVersion(std::uint32_t* version, char* branch) const;
    int EnumPhysicalGpus(void** handles, std::uint32_t* count) const;
    int EnumLogicalGpus(void** handles, std::uint32_t* count) const;
    int GetPhysicalGpusFromLogicalGpu(void* logical, void** handles, std::uint32_t* count) const;
    int GetLogicalGpuFromPhysicalGpu(void* physical, void** logical) const;
    int GetCurrentSliState(void* device, void* state) const; // NV_GET_CURRENT_SLI_STATE V1/V2
    int GetGdiPrimaryDisplayId(std::uint32_t* displayId) const;
    int EnumNvidiaDisplayHandle(std::uint32_t index, void** display) const;
    int GetDisplayDriverVersion(void* display, void* version) const; // NV_DISPLAY_DRIVER_VERSION
    int GetFullName(void* gpu, char* out) const;
    int GetPciIdentifiers(void* gpu, std::uint32_t* deviceId, std::uint32_t* subSystemId,
                          std::uint32_t* revisionId, std::uint32_t* extDeviceId) const;
    int GetPhysicalFrameBufferSize(void* gpu, std::uint32_t* kib) const;
    int GetGpuCoreCount(void* gpu, std::uint32_t* count) const;
    int GetBusType(void* gpu, int* busType) const;

private:
    Config config_;
};

} // namespace fvr::bf4::stereo
