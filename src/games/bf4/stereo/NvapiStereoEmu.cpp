#include "NvapiStereoEmu.h"

#include <cstdio>

namespace fvr::bf4 {

namespace nvapi {
const char* FunctionName(std::uint32_t f) noexcept {
    switch (f) {
    case id::Initialize: return "NvAPI_Initialize";
    case id::Unload: return "NvAPI_Unload";
    case id::GetErrorMessage: return "NvAPI_GetErrorMessage";
    case id::Stereo_Enable: return "NvAPI_Stereo_Enable";
    case id::Stereo_Disable: return "NvAPI_Stereo_Disable";
    case id::Stereo_IsEnabled: return "NvAPI_Stereo_IsEnabled";
    case id::Stereo_GetStereoSupport: return "NvAPI_Stereo_GetStereoSupport";
    case id::Stereo_CreateHandleFromIUnknown: return "NvAPI_Stereo_CreateHandleFromIUnknown";
    case id::Stereo_DestroyHandle: return "NvAPI_Stereo_DestroyHandle";
    case id::Stereo_Activate: return "NvAPI_Stereo_Activate";
    case id::Stereo_Deactivate: return "NvAPI_Stereo_Deactivate";
    case id::Stereo_IsActivated: return "NvAPI_Stereo_IsActivated";
    case id::Stereo_GetSeparation: return "NvAPI_Stereo_GetSeparation";
    case id::Stereo_SetSeparation: return "NvAPI_Stereo_SetSeparation";
    case id::Stereo_GetConvergence: return "NvAPI_Stereo_GetConvergence";
    case id::Stereo_SetConvergence: return "NvAPI_Stereo_SetConvergence";
    case id::Stereo_GetFrustumAdjustMode: return "NvAPI_Stereo_GetFrustumAdjustMode";
    case id::Stereo_SetFrustumAdjustMode: return "NvAPI_Stereo_SetFrustumAdjustMode";
    case id::Stereo_InitActivation: return "NvAPI_Stereo_InitActivation";
    case id::Stereo_Trigger_Activation: return "NvAPI_Stereo_Trigger_Activation";
    case id::Stereo_ReverseStereoBlitControl: return "NvAPI_Stereo_ReverseStereoBlitControl";
    case id::Stereo_SetNotificationMessage: return "NvAPI_Stereo_SetNotificationMessage";
    case id::Stereo_SetActiveEye: return "NvAPI_Stereo_SetActiveEye";
    case id::Stereo_SetDriverMode: return "NvAPI_Stereo_SetDriverMode";
    case id::Stereo_GetEyeSeparation: return "NvAPI_Stereo_GetEyeSeparation";
    case id::Stereo_IsWindowedModeSupported: return "NvAPI_Stereo_IsWindowedModeSupported";
    case id::Stereo_SetSurfaceCreationMode: return "NvAPI_Stereo_SetSurfaceCreationMode";
    case id::Stereo_GetSurfaceCreationMode: return "NvAPI_Stereo_GetSurfaceCreationMode";
    case id::GetInterfaceVersionString: return "NvAPI_GetInterfaceVersionString";
    case id::SYS_GetDriverAndBranchVersion: return "NvAPI_SYS_GetDriverAndBranchVersion";
    case id::EnumPhysicalGPUs: return "NvAPI_EnumPhysicalGPUs";
    case id::EnumLogicalGPUs: return "NvAPI_EnumLogicalGPUs";
    case id::GetPhysicalGPUsFromLogicalGPU: return "NvAPI_GetPhysicalGPUsFromLogicalGPU";
    case id::GetLogicalGPUFromPhysicalGPU: return "NvAPI_GetLogicalGPUFromPhysicalGPU";
    case id::D3D_GetCurrentSLIState: return "NvAPI_D3D_GetCurrentSLIState";
    case id::DISP_GetGDIPrimaryDisplayId: return "NvAPI_DISP_GetGDIPrimaryDisplayId";
    case id::EnumNvidiaDisplayHandle: return "NvAPI_EnumNvidiaDisplayHandle";
    case id::GetDisplayDriverVersion: return "NvAPI_GetDisplayDriverVersion";
    case id::GPU_GetFullName: return "NvAPI_GPU_GetFullName";
    case id::GPU_GetPCIIdentifiers: return "NvAPI_GPU_GetPCIIdentifiers";
    case id::GPU_GetPhysicalFrameBufferSize: return "NvAPI_GPU_GetPhysicalFrameBufferSize";
    case id::GPU_GetGpuCoreCount: return "NvAPI_GPU_GetGpuCoreCount";
    case id::GPU_GetBusType: return "NvAPI_GPU_GetBusType";
    default: return nullptr;
    }
}
} // namespace nvapi

namespace stereo {

using namespace nvapi;

Eye EyeFromNvapi(int nvEye) noexcept {
    if (nvEye == kEyeLeft) return Eye::Left;
    if (nvEye == kEyeRight) return Eye::Right;
    return Eye::Mono;
}

bool NvapiStereoEmu::Implements(std::uint32_t f) noexcept {
    switch (f) {
    case id::Initialize: case id::Unload:
    case id::Stereo_Enable: case id::Stereo_Disable: case id::Stereo_IsEnabled:
    case id::Stereo_CreateHandleFromIUnknown: case id::Stereo_DestroyHandle:
    case id::Stereo_Activate: case id::Stereo_Deactivate: case id::Stereo_IsActivated:
    case id::Stereo_GetSeparation: case id::Stereo_SetSeparation:
    case id::Stereo_GetConvergence: case id::Stereo_SetConvergence:
    case id::Stereo_SetActiveEye: case id::Stereo_SetDriverMode: case id::Stereo_GetEyeSeparation:
    case id::Stereo_IsWindowedModeSupported:
    case id::Stereo_SetSurfaceCreationMode: case id::Stereo_GetSurfaceCreationMode:
        return true;
    default:
        return false; // ReverseStereoBlitControl etc.: logged, not faked until seen in use
    }
}

int NvapiStereoEmu::Initialize() {
    std::lock_guard lock(mutex_);
    initialized_ = true;
    separation_ = config_.separationPercent;
    convergence_ = config_.convergence;
    return kOk;
}

int NvapiStereoEmu::SetDriverMode(int mode) {
    std::lock_guard lock(mutex_);
    if (mode != kDriverModeAutomatic && mode != kDriverModeDirect) return kError;
    directMode_ = mode == kDriverModeDirect;
    return kOk;
}

int NvapiStereoEmu::Enable() { std::lock_guard lock(mutex_); enabled_ = true; return kOk; }
int NvapiStereoEmu::Disable() { std::lock_guard lock(mutex_); enabled_ = false; return kOk; }

int NvapiStereoEmu::IsEnabled(std::uint8_t* out) const {
    if (!out) return kError;
    std::lock_guard lock(mutex_);
    *out = enabled_ && !suppressed_ ? 1 : 0;
    return kOk;
}

bool NvapiStereoEmu::ValidHandle(void* handle) const { return handles_.count(handle) != 0; }

int NvapiStereoEmu::CreateHandle(void* device, void** outHandle) {
    if (!device || !outHandle) return kError;
    std::lock_guard lock(mutex_);
    void* handle = reinterpret_cast<void*>(nextHandle_);
    nextHandle_ += 0x10;
    handles_[handle] = device;
    *outHandle = handle;
    return kOk;
}

int NvapiStereoEmu::DestroyHandle(void* handle) {
    std::lock_guard lock(mutex_);
    return handles_.erase(handle) ? kOk : kError;
}

int NvapiStereoEmu::Activate(void* handle) {
    std::lock_guard lock(mutex_);
    if (!ValidHandle(handle) || !enabled_) return kError;
    activated_ = true;
    return kOk;
}

int NvapiStereoEmu::Deactivate(void* handle) {
    std::lock_guard lock(mutex_);
    if (!ValidHandle(handle)) return kError;
    activated_ = false;
    return kOk;
}

int NvapiStereoEmu::IsActivated(void* handle, std::uint8_t* out) const {
    if (!out) return kError;
    std::lock_guard lock(mutex_);
    if (!ValidHandle(handle)) return kError;
    // Emulated 3D Vision comes up activated (like a driver with stereo on by default),
    // unless FrostbiteVR suppressed it.
    *out = enabled_ && !suppressed_ ? 1 : 0;
    return kOk;
}

int NvapiStereoEmu::GetSeparation(void* handle, float* out) const {
    std::lock_guard lock(mutex_);
    if (!out || !ValidHandle(handle)) return kError;
    *out = suppressed_ ? 0.0f : separation_;
    return kOk;
}

int NvapiStereoEmu::SetSeparation(void* handle, float value) {
    std::lock_guard lock(mutex_);
    if (!ValidHandle(handle) || value < 0.0f || value > 100.0f) return kError;
    separation_ = value;
    return kOk;
}

int NvapiStereoEmu::GetConvergence(void* handle, float* out) const {
    std::lock_guard lock(mutex_);
    if (!out || !ValidHandle(handle)) return kError;
    *out = convergence_;
    return kOk;
}

int NvapiStereoEmu::SetConvergence(void* handle, float value) {
    std::lock_guard lock(mutex_);
    if (!ValidHandle(handle)) return kError;
    convergence_ = value;
    return kOk;
}

int NvapiStereoEmu::GetEyeSeparation(void* handle, float* out) const {
    std::lock_guard lock(mutex_);
    if (!out || !ValidHandle(handle)) return kError;
    *out = config_.eyeSeparation;
    return kOk;
}

int NvapiStereoEmu::IsWindowedModeSupported(std::uint8_t* out) const {
    if (!out) return kError;
    *out = 1; // BF4 may run windowed/borderless; refusing would hide the stereo path
    return kOk;
}

int NvapiStereoEmu::SetSurfaceCreationMode(void* handle, int mode) {
    std::lock_guard lock(mutex_);
    if (!ValidHandle(handle)) return kError;
    surfaceMode_ = mode;
    return kOk;
}

int NvapiStereoEmu::GetSurfaceCreationMode(void* handle, int* out) const {
    std::lock_guard lock(mutex_);
    if (!out || !ValidHandle(handle)) return kError;
    *out = surfaceMode_;
    return kOk;
}

int NvapiStereoEmu::SetActiveEye(void* handle, int nvEye) {
    std::function<void(Eye)> callback;
    Eye eye;
    {
        std::lock_guard lock(mutex_);
        if (!ValidHandle(handle)) return kError;
        if (nvEye != kEyeLeft && nvEye != kEyeRight && nvEye != kEyeMono) return kError;
        eye = EyeFromNvapi(nvEye);
        if (eye != activeEye_) ++eyeSwitches_;
        activeEye_ = eye;
        callback = onEye_;
    }
    if (callback && !suppressed_) callback(eye); // outside the lock: it copies textures
    return kOk;
}

void NvapiStereoEmu::SetEyeCallback(std::function<void(Eye)> callback) {
    std::lock_guard lock(mutex_);
    onEye_ = std::move(callback);
}

void NvapiStereoEmu::NoteQuery(std::uint32_t f, bool answered, std::uint64_t callerRva) {
    std::lock_guard lock(mutex_);
    auto& q = queries_[f];
    if (q.count++ == 0) q.callerRva = callerRva;
    q.answered = answered;
}

std::string NvapiStereoEmu::Report() const {
    std::lock_guard lock(mutex_);
    std::string s;
    char line[160];
    std::snprintf(line, sizeof(line), "nvapi initialized=%d direct_mode=%d activated=%d suppressed=%d handles=%zu\n",
                  initialized_, directMode_, activated_, suppressed_.load(), handles_.size());
    s += line;
    std::snprintf(line, sizeof(line), "engine separation=%.3f convergence=%.3f surface_mode=%d eye_switches=%llu\n",
                  separation_, convergence_, surfaceMode_, static_cast<unsigned long long>(eyeSwitches_));
    s += line;
    for (const auto& [f, q] : queries_) {
        const char* name = nvapi::FunctionName(f);
        std::snprintf(line, sizeof(line), "query 0x%08x %-42s x%llu %-8s caller=exe+0x%llx\n", f,
                      name ? name : "(not stereo/core)", static_cast<unsigned long long>(q.count),
                      q.answered ? "answered" : "null", static_cast<unsigned long long>(q.callerRva));
        s += line;
    }
    return s;
}

} // namespace stereo
} // namespace fvr::bf4
