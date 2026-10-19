#pragma once
// NVAPI function IDs and enum values used by the stereo emulation, copied from NVIDIA's
// MIT-licensed public SDK (github.com/NVIDIA/nvapi: nvapi_interface.h, nvapi_lite_stereo.h).
// NVIDIA published these headers specifically to enable re-implementations of NVAPI.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fvr::bf4::nvapi {

namespace id {
inline constexpr std::uint32_t Initialize = 0x0150e828;
inline constexpr std::uint32_t Unload = 0xd22bdd7e;
inline constexpr std::uint32_t GetErrorMessage = 0x6c2d048c;
inline constexpr std::uint32_t Stereo_Enable = 0x239c4545;
inline constexpr std::uint32_t Stereo_Disable = 0x2ec50c2b;
inline constexpr std::uint32_t Stereo_IsEnabled = 0x348ff8e1;
inline constexpr std::uint32_t Stereo_GetStereoSupport = 0x296c434d;
inline constexpr std::uint32_t Stereo_CreateHandleFromIUnknown = 0xac7e37f4;
inline constexpr std::uint32_t Stereo_DestroyHandle = 0x3a153134;
inline constexpr std::uint32_t Stereo_Activate = 0xf6a1ad68;
inline constexpr std::uint32_t Stereo_Deactivate = 0x2d68de96;
inline constexpr std::uint32_t Stereo_IsActivated = 0x1fb0bc30;
inline constexpr std::uint32_t Stereo_GetSeparation = 0x451f2134;
inline constexpr std::uint32_t Stereo_SetSeparation = 0x5c069fa3;
inline constexpr std::uint32_t Stereo_GetConvergence = 0x4ab00934;
inline constexpr std::uint32_t Stereo_SetConvergence = 0x3dd6b54b;
inline constexpr std::uint32_t Stereo_GetFrustumAdjustMode = 0xe6839b43;
inline constexpr std::uint32_t Stereo_SetFrustumAdjustMode = 0x7be27fa2;
inline constexpr std::uint32_t Stereo_InitActivation = 0xc7177702;
inline constexpr std::uint32_t Stereo_Trigger_Activation = 0x0d6c6cd2;
inline constexpr std::uint32_t Stereo_ReverseStereoBlitControl = 0x3cd58f89;
inline constexpr std::uint32_t Stereo_SetNotificationMessage = 0x6b9b409e;
inline constexpr std::uint32_t Stereo_SetActiveEye = 0x96eea9f8;
inline constexpr std::uint32_t Stereo_SetDriverMode = 0x5e8f0bec;
inline constexpr std::uint32_t Stereo_GetEyeSeparation = 0xce653127;
inline constexpr std::uint32_t Stereo_IsWindowedModeSupported = 0x40c8ed5e;
inline constexpr std::uint32_t Stereo_SetSurfaceCreationMode = 0xf5dcfcba;
inline constexpr std::uint32_t Stereo_GetSurfaceCreationMode = 0x36f1c736;
// Core/system queries BF4 makes before any stereo call (seen on the real game, probe 1).
inline constexpr std::uint32_t GetInterfaceVersionString = 0x01053fa5;
inline constexpr std::uint32_t SYS_GetDriverAndBranchVersion = 0x2926aaad;
inline constexpr std::uint32_t EnumPhysicalGPUs = 0xe5ac921f;
inline constexpr std::uint32_t EnumLogicalGPUs = 0x48b3ea59;
inline constexpr std::uint32_t GetPhysicalGPUsFromLogicalGPU = 0xaea3fa32;
inline constexpr std::uint32_t GetLogicalGPUFromPhysicalGPU = 0xadd604d1;
inline constexpr std::uint32_t D3D_GetCurrentSLIState = 0x4b708b54;
inline constexpr std::uint32_t DISP_GetGDIPrimaryDisplayId = 0x1e9d8a31;
inline constexpr std::uint32_t EnumNvidiaDisplayHandle = 0x9abdd40d;
inline constexpr std::uint32_t GetDisplayDriverVersion = 0xf951a4d1;
inline constexpr std::uint32_t GPU_GetFullName = 0xceee8e9f;
inline constexpr std::uint32_t GPU_GetPCIIdentifiers = 0x2ddfb66e;
inline constexpr std::uint32_t GPU_GetPhysicalFrameBufferSize = 0x46fbeb03;
inline constexpr std::uint32_t GPU_GetGpuCoreCount = 0xc7026a87;
inline constexpr std::uint32_t GPU_GetBusType = 0x1bb18724;
} // namespace id

// NV_STEREO_ACTIVE_EYE
inline constexpr int kEyeRight = 1;
inline constexpr int kEyeLeft = 2;
inline constexpr int kEyeMono = 3;
// NV_STEREO_DRIVER_MODE
inline constexpr int kDriverModeAutomatic = 0;
inline constexpr int kDriverModeDirect = 2;
// NvAPI_Status values used here (nvapi_lite_common.h).
inline constexpr int kOk = 0;
inline constexpr int kError = -1;
inline constexpr int kNoImplementation = -3;
inline constexpr int kInvalidArgument = -5;
inline constexpr int kEndEnumeration = -7;
inline constexpr int kInvalidHandle = -8;
inline constexpr int kIncompatibleStructVersion = -9;
// MAKE_NVAPI_VERSION(type, ver) = sizeof(type) | ver << 16
constexpr std::uint32_t MakeVersion(std::uint32_t size, std::uint32_t ver) { return size | (ver << 16); }
inline constexpr std::size_t kShortString = 64; // NvAPI_ShortString
inline constexpr std::size_t kMaxGpus = 64;     // NVAPI_MAX_PHYSICAL_GPUS / NVAPI_MAX_LOGICAL_GPUS

// Name for logging; nullptr for IDs outside the stereo/core subset above.
const char* FunctionName(std::uint32_t functionId) noexcept;

} // namespace fvr::bf4::nvapi
