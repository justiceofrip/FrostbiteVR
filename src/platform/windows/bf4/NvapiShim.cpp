// nvapi64.dll shim: BF4's 3D Vision path, emulated. Copied into the game folder only for
// VR sessions with stereo enabled, removed afterwards (like the dxgi proxy).
//
//   * stereo functions (Initialize, Stereo_*) are answered by NvapiStereoEmu
//   * every other function ID is forwarded to the real System32 nvapi64.dll when one
//     exists (NVIDIA GPUs); without one, the system/GPU queries BF4 makes before any
//     stereo call are answered by NvapiCoreEmu (one GPU, 3D Vision-era driver), and the
//     rest get nullptr = "not available" (NVAPI_NO_IMPLEMENTATION to the game)
//   * without a FrostbiteVR session (or with stereo off) it is a pure pass-through
//
// IDs/enums come from NVIDIA's MIT-licensed public SDK (see stereo/NvapiIds.h).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../../games/bf4/Bf4SessionInfo.h"
#include "../../../games/bf4/stereo/NvapiCoreEmu.h"
#include "../../../games/bf4/stereo/NvapiStereoEmu.h"

#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace {
using fvr::bf4::stereo::Eye;
using fvr::bf4::stereo::NvapiCoreEmu;
using fvr::bf4::stereo::NvapiStereoEmu;
namespace id = fvr::bf4::nvapi::id;

using QueryFn = void*(__cdecl*)(unsigned int);
using RealInitFn = int(__cdecl*)();
using EyeCallback = void(__cdecl*)(int eye, void* context);

std::once_flag g_once;
NvapiStereoEmu* g_emu = nullptr; // lives for the process
QueryFn g_realQuery = nullptr;
bool g_emulate = false;

void Init() {
    std::call_once(g_once, [] {
        wchar_t sys[MAX_PATH]{};
        GetSystemDirectoryW(sys, MAX_PATH);
        if (HMODULE real = LoadLibraryW((std::wstring(sys) + L"\\nvapi64.dll").c_str()))
            g_realQuery = reinterpret_cast<QueryFn>(GetProcAddress(real, "nvapi_QueryInterface"));

        NvapiStereoEmu::Config config;
        if (HANDLE section = OpenFileMappingW(FILE_MAP_READ, FALSE, fvr::bf4::kSessionSectionName)) {
            if (const void* view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, fvr::bf4::kSessionSectionBytes)) {
                const std::string text(static_cast<const char*>(view), fvr::bf4::kSessionSectionBytes);
                UnmapViewOfFile(view);
                if (const auto info = fvr::bf4::ParseSessionInfo(text)) {
                    g_emulate = info->stereo != fvr::bf4::StereoSetting::Off;
                    config.separationPercent = info->stereoSeparation;
                }
            }
            CloseHandle(section);
        }
        g_emu = new NvapiStereoEmu(config);
    });
}

NvapiStereoEmu& Emu() { return *g_emu; }
const NvapiCoreEmu g_core; // stateless answers; used only without a real driver

// ---- C ABI thunks (NVAPI_INTERFACE is __cdecl; enums/handles pass as int/pointer) --------
int __cdecl Initialize() {
    if (g_realQuery)
        if (auto real = reinterpret_cast<RealInitFn>(g_realQuery(id::Initialize))) real(); // keep NVIDIA features alive
    return Emu().Initialize();
}
int __cdecl Unload() { return fvr::bf4::nvapi::kOk; }
int __cdecl Enable() { return Emu().Enable(); }
int __cdecl Disable() { return Emu().Disable(); }
int __cdecl IsEnabled(unsigned char* out) { return Emu().IsEnabled(out); }
int __cdecl CreateHandle(void* device, void** handle) { return Emu().CreateHandle(device, handle); }
int __cdecl DestroyHandle(void* handle) { return Emu().DestroyHandle(handle); }
int __cdecl Activate(void* handle) { return Emu().Activate(handle); }
int __cdecl Deactivate(void* handle) { return Emu().Deactivate(handle); }
int __cdecl IsActivated(void* handle, unsigned char* out) { return Emu().IsActivated(handle, out); }
int __cdecl GetSeparation(void* handle, float* out) { return Emu().GetSeparation(handle, out); }
int __cdecl SetSeparation(void* handle, float value) { return Emu().SetSeparation(handle, value); }
int __cdecl GetConvergence(void* handle, float* out) { return Emu().GetConvergence(handle, out); }
int __cdecl SetConvergence(void* handle, float value) { return Emu().SetConvergence(handle, value); }
int __cdecl SetActiveEye(void* handle, int eye) { return Emu().SetActiveEye(handle, eye); }
int __cdecl SetDriverMode(int mode) { return Emu().SetDriverMode(mode); }
int __cdecl GetEyeSeparation(void* handle, float* out) { return Emu().GetEyeSeparation(handle, out); }
int __cdecl IsWindowedModeSupported(unsigned char* out) { return Emu().IsWindowedModeSupported(out); }
int __cdecl SetSurfaceCreationMode(void* handle, int mode) { return Emu().SetSurfaceCreationMode(handle, mode); }
int __cdecl GetSurfaceCreationMode(void* handle, int* out) { return Emu().GetSurfaceCreationMode(handle, out); }

// Core/system answers (no real driver). Pointer-sized handles; NvU32 = unsigned int.
int __cdecl InterfaceVersion(char* out) { return g_core.GetInterfaceVersionString(out); }
int __cdecl ErrorMessage(int status, char* out) { return g_core.GetErrorMessage(status, out); }
int __cdecl DriverAndBranch(unsigned int* v, char* branch) { return g_core.GetDriverAndBranchVersion(v, branch); }
int __cdecl EnumPhysical(void** h, unsigned int* n) { return g_core.EnumPhysicalGpus(h, n); }
int __cdecl EnumLogical(void** h, unsigned int* n) { return g_core.EnumLogicalGpus(h, n); }
int __cdecl PhysicalFromLogical(void* l, void** h, unsigned int* n) { return g_core.GetPhysicalGpusFromLogicalGpu(l, h, n); }
int __cdecl LogicalFromPhysical(void* p, void** l) { return g_core.GetLogicalGpuFromPhysicalGpu(p, l); }
int __cdecl SliState(void* device, void* state) { return g_core.GetCurrentSliState(device, state); }
int __cdecl PrimaryDisplay(unsigned int* id) { return g_core.GetGdiPrimaryDisplayId(id); }
int __cdecl EnumDisplay(unsigned int i, void** d) { return g_core.EnumNvidiaDisplayHandle(i, d); }
int __cdecl DisplayDriverVersion(void* d, void* v) { return g_core.GetDisplayDriverVersion(d, v); }
int __cdecl FullName(void* g, char* out) { return g_core.GetFullName(g, out); }
int __cdecl PciIds(void* g, unsigned int* a, unsigned int* b, unsigned int* c, unsigned int* d) {
    return g_core.GetPciIdentifiers(g, a, b, c, d);
}
int __cdecl FrameBufferSize(void* g, unsigned int* kib) { return g_core.GetPhysicalFrameBufferSize(g, kib); }
int __cdecl CoreCount(void* g, unsigned int* n) { return g_core.GetGpuCoreCount(g, n); }
int __cdecl BusType(void* g, int* t) { return g_core.GetBusType(g, t); }

void* LookupCore(unsigned int f) {
    switch (f) {
    case id::GetInterfaceVersionString: return reinterpret_cast<void*>(&InterfaceVersion);
    case id::GetErrorMessage: return reinterpret_cast<void*>(&ErrorMessage);
    case id::SYS_GetDriverAndBranchVersion: return reinterpret_cast<void*>(&DriverAndBranch);
    case id::EnumPhysicalGPUs: return reinterpret_cast<void*>(&EnumPhysical);
    case id::EnumLogicalGPUs: return reinterpret_cast<void*>(&EnumLogical);
    case id::GetPhysicalGPUsFromLogicalGPU: return reinterpret_cast<void*>(&PhysicalFromLogical);
    case id::GetLogicalGPUFromPhysicalGPU: return reinterpret_cast<void*>(&LogicalFromPhysical);
    case id::D3D_GetCurrentSLIState: return reinterpret_cast<void*>(&SliState);
    case id::DISP_GetGDIPrimaryDisplayId: return reinterpret_cast<void*>(&PrimaryDisplay);
    case id::EnumNvidiaDisplayHandle: return reinterpret_cast<void*>(&EnumDisplay);
    case id::GetDisplayDriverVersion: return reinterpret_cast<void*>(&DisplayDriverVersion);
    case id::GPU_GetFullName: return reinterpret_cast<void*>(&FullName);
    case id::GPU_GetPCIIdentifiers: return reinterpret_cast<void*>(&PciIds);
    case id::GPU_GetPhysicalFrameBufferSize: return reinterpret_cast<void*>(&FrameBufferSize);
    case id::GPU_GetGpuCoreCount: return reinterpret_cast<void*>(&CoreCount);
    case id::GPU_GetBusType: return reinterpret_cast<void*>(&BusType);
    default: return nullptr;
    }
}

void* Lookup(unsigned int f) {
    switch (f) {
    case id::Initialize: return reinterpret_cast<void*>(&Initialize);
    case id::Unload: return reinterpret_cast<void*>(&Unload);
    case id::Stereo_Enable: return reinterpret_cast<void*>(&Enable);
    case id::Stereo_Disable: return reinterpret_cast<void*>(&Disable);
    case id::Stereo_IsEnabled: return reinterpret_cast<void*>(&IsEnabled);
    case id::Stereo_CreateHandleFromIUnknown: return reinterpret_cast<void*>(&CreateHandle);
    case id::Stereo_DestroyHandle: return reinterpret_cast<void*>(&DestroyHandle);
    case id::Stereo_Activate: return reinterpret_cast<void*>(&Activate);
    case id::Stereo_Deactivate: return reinterpret_cast<void*>(&Deactivate);
    case id::Stereo_IsActivated: return reinterpret_cast<void*>(&IsActivated);
    case id::Stereo_GetSeparation: return reinterpret_cast<void*>(&GetSeparation);
    case id::Stereo_SetSeparation: return reinterpret_cast<void*>(&SetSeparation);
    case id::Stereo_GetConvergence: return reinterpret_cast<void*>(&GetConvergence);
    case id::Stereo_SetConvergence: return reinterpret_cast<void*>(&SetConvergence);
    case id::Stereo_SetActiveEye: return reinterpret_cast<void*>(&SetActiveEye);
    case id::Stereo_SetDriverMode: return reinterpret_cast<void*>(&SetDriverMode);
    case id::Stereo_GetEyeSeparation: return reinterpret_cast<void*>(&GetEyeSeparation);
    case id::Stereo_IsWindowedModeSupported: return reinterpret_cast<void*>(&IsWindowedModeSupported);
    case id::Stereo_SetSurfaceCreationMode: return reinterpret_cast<void*>(&SetSurfaceCreationMode);
    case id::Stereo_GetSurfaceCreationMode: return reinterpret_cast<void*>(&GetSurfaceCreationMode);
    default: return nullptr;
    }
}
} // namespace

extern "C" __declspec(dllexport) void* __cdecl nvapi_QueryInterface(unsigned int functionId) {
    Init();
    if (!g_emulate) return g_realQuery ? g_realQuery(functionId) : nullptr;
    // The caller is the game's NVAPI wrapper for this ID; record where it lives.
    const auto exe = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const std::uint64_t callerRva = ret > exe && ret - exe < 0x40000000u ? ret - exe : 0;
    if (void* mine = Lookup(functionId)) {
        Emu().NoteQuery(functionId, true, callerRva);
        return mine;
    }
    void* answer = g_realQuery ? g_realQuery(functionId) : LookupCore(functionId);
    Emu().NoteQuery(functionId, answer != nullptr, callerRva);
    return answer;
}

// ---- FrostbiteVR side (the adapter finds these with GetProcAddress) ------------------------
extern "C" __declspec(dllexport) int __cdecl FvrNvapiShim_Bind(EyeCallback callback, void* context) {
    Init();
    Emu().SetEyeCallback([callback, context](Eye eye) { if (callback) callback(static_cast<int>(eye), context); });
    return 1; // shim ABI version
}

extern "C" __declspec(dllexport) void __cdecl FvrNvapiShim_SetSuppressed(int suppressed) {
    Init();
    Emu().SetSuppressed(suppressed != 0);
}

extern "C" __declspec(dllexport) int __cdecl FvrNvapiShim_State(int* handleCreated, int* directMode,
                                                                 unsigned long long* eyeSwitches) {
    Init();
    if (handleCreated) *handleCreated = Emu().HandleCount() > 0;
    if (directMode) *directMode = Emu().DirectMode();
    if (eyeSwitches) *eyeSwitches = Emu().EyeSwitches();
    return g_emulate ? 1 : 0;
}

extern "C" __declspec(dllexport) int __cdecl FvrNvapiShim_Report(char* buffer, int size) {
    Init();
    if (!buffer || size <= 0) return 0;
    std::string r = Emu().Report();
    r += g_realQuery ? "real nvapi64.dll: present (non-stereo calls forwarded)\n" : "real nvapi64.dll: absent\n";
    const std::size_t n = std::min<std::size_t>(r.size(), static_cast<std::size_t>(size - 1));
    std::memcpy(buffer, r.data(), n);
    buffer[n] = '\0';
    return static_cast<int>(n);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
