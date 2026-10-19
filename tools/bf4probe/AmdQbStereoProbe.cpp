// Read-only driver check: does this machine's AMD driver still offer the HD3D quad-buffer
// stereo extension that Frostbite 2/3 used for "Stereoscopic 3D" on Radeon (BF3 shipped it
// in its Video options)? Also reports Windows' own DXGI 1.2 stereo. No game, no hooks.
//
// Interface declarations follow AMD's Quad Buffer Stereo SDK headers (AmdDxExtApi.h,
// AmdDxExtIface.h, AmdDxExtQbStereoApi.h); the vtable order must match them exactly.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <cstdio>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace amd {
class IAmdDxExtInterface {
public:
    virtual unsigned int AddRef() = 0;
    virtual unsigned int Release() = 0;
protected:
    IAmdDxExtInterface() = default;
    virtual ~IAmdDxExtInterface() = 0;
};
inline IAmdDxExtInterface::~IAmdDxExtInterface() = default;

struct AmdDxExtVersion { unsigned int majorVersion, minorVersion; };

class IAmdDxExt : public IAmdDxExtInterface {
public:
    virtual HRESULT GetVersion(AmdDxExtVersion* pExtVer) = 0;
    virtual IAmdDxExtInterface* GetExtInterface(unsigned int iface) = 0;
    virtual HRESULT IaSetPrimitiveTopology(unsigned int topology) = 0;
    virtual HRESULT IaGetPrimitiveTopology(int* pExtTopology) = 0;
    virtual HRESULT SetSingleSampleRead(void* pResource, BOOL singleSample) = 0;
    virtual HRESULT SetSingleSampleRead11(ID3D11Resource* pResource, BOOL singleSample) = 0;
protected:
    ~IAmdDxExt() override = 0;
};
inline IAmdDxExt::~IAmdDxExt() = default;

constexpr unsigned int AmdDxExtQuadBufferStereoID = 2;

class IAmdDxExtQuadBufferStereo : public IAmdDxExtInterface {
public:
    virtual HRESULT EnableQuadBufferStereo(BOOL enable) = 0;
    virtual HRESULT GetDisplayModeList(DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes, DXGI_MODE_DESC* pDesc) = 0;
    virtual UINT GetLineOffset(IDXGISwapChain* pSwapChain) = 0;
};

using PFNAmdDxExtCreate11 = HRESULT(__cdecl*)(ID3D11Device* pDevice, IAmdDxExt** ppExt);
} // namespace amd

namespace {
template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

UINT CountModes(IDXGIOutput* out, DXGI_FORMAT fmt, UINT flags) {
    UINT n = 0;
    return SUCCEEDED(out->GetDisplayModeList(fmt, flags, &n, nullptr)) ? n : 0;
}
} // namespace

int main() {
    IDXGIFactory2* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) {
        std::printf("dxgi: no IDXGIFactory2\n");
        return 2;
    }
    std::printf("dxgi.windowed_stereo_enabled=%d (Windows stereo display mode toggle)\n",
                factory->IsWindowedStereoEnabled() ? 1 : 0);

    // Pick the AMD adapter with the most dedicated VRAM (the discrete card, not the iGPU).
    IDXGIAdapter1* best = nullptr;
    DXGI_ADAPTER_DESC1 bestDesc{};
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* a = nullptr;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        std::printf("adapter[%u] vendor=0x%04X vram=%lluMB %ls\n", i, d.VendorId,
                    static_cast<unsigned long long>(d.DedicatedVideoMemory >> 20), d.Description);
        if (d.VendorId == 0x1002 && (!best || d.DedicatedVideoMemory > bestDesc.DedicatedVideoMemory)) {
            SafeRelease(best);
            best = a;
            bestDesc = d;
        } else {
            a->Release();
        }
    }
    if (!best) {
        std::printf("verdict=no-amd-adapter\n");
        SafeRelease(factory);
        return 3;
    }
    std::printf("using: %ls\n", bestDesc.Description);

    // DXGI 1.2 stereo display modes on the adapter's outputs (headset mirrors count too).
    for (UINT o = 0;; ++o) {
        IDXGIOutput* out = nullptr;
        if (best->EnumOutputs(o, &out) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_OUTPUT_DESC od{};
        out->GetDesc(&od);
        std::printf("output[%u] %ls modes=%u dxgi_stereo_modes=%u\n", o, od.DeviceName,
                    CountModes(out, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
                    CountModes(out, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_STEREO));
        out->Release();
    }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(best, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, 1,
                                   D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) {
        std::printf("verdict=d3d11-device-failed hr=0x%08lX\n", hr);
        SafeRelease(best);
        SafeRelease(factory);
        return 4;
    }

    // Games of that era used the already-loaded driver module, then fell back to loading it.
    HMODULE ext = GetModuleHandleW(L"atidxx64.dll");
    const bool wasLoaded = ext != nullptr;
    if (!ext) ext = LoadLibraryW(L"atidxx64.dll");
    wchar_t path[MAX_PATH] = L"";
    if (ext) GetModuleFileNameW(ext, path, MAX_PATH);
    std::printf("atidxx64 loaded_by_driver=%d path=%ls\n", wasLoaded ? 1 : 0, ext ? path : L"(none)");

    const char* verdict = "no-extension-dll";
    if (ext) {
        auto create = reinterpret_cast<amd::PFNAmdDxExtCreate11>(GetProcAddress(ext, "AmdDxExtCreate11"));
        amd::IAmdDxExt* dxExt = nullptr;
        hr = create ? create(device, &dxExt) : E_NOINTERFACE;
        std::printf("AmdDxExtCreate11 export=%d hr=0x%08lX ext=%p\n", create ? 1 : 0, hr, static_cast<void*>(dxExt));
        verdict = "no-dx-extension";
        if (SUCCEEDED(hr) && dxExt) {
            amd::AmdDxExtVersion v{};
            hr = dxExt->GetVersion(&v);
            std::printf("ext.version hr=0x%08lX %u.%u\n", hr, v.majorVersion, v.minorVersion);
            auto* qb = static_cast<amd::IAmdDxExtQuadBufferStereo*>(dxExt->GetExtInterface(amd::AmdDxExtQuadBufferStereoID));
            std::printf("quad_buffer_stereo_interface=%p\n", static_cast<void*>(qb));
            verdict = "no-quad-buffer-interface";
            if (qb) {
                hr = qb->EnableQuadBufferStereo(TRUE);
                std::printf("EnableQuadBufferStereo(TRUE) hr=0x%08lX\n", hr);
                UINT n = 0;
                const HRESULT mh = qb->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, nullptr);
                std::printf("qb.GetDisplayModeList hr=0x%08lX stereo_modes=%u\n", mh, n);
                std::vector<DXGI_MODE_DESC> modes(n);
                if (n && SUCCEEDED(qb->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, modes.data())))
                    for (UINT i = 0; i < n && i < 8; ++i)
                        std::printf("  mode %ux%u @ %u/%u\n", modes[i].Width, modes[i].Height,
                                    modes[i].RefreshRate.Numerator, modes[i].RefreshRate.Denominator);
                verdict = SUCCEEDED(hr) ? (n ? "quad-buffer-available" : "quad-buffer-enabled-no-stereo-modes")
                                        : "quad-buffer-enable-refused";
                qb->EnableQuadBufferStereo(FALSE);
                qb->Release();
            }
            dxExt->Release();
        }
    }
    std::printf("verdict=%s\n", verdict);
    SafeRelease(context);
    SafeRelease(device);
    SafeRelease(best);
    SafeRelease(factory);
    return 0;
}
