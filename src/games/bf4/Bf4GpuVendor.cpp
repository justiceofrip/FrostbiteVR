#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>

#include "Bf4GpuVendor.h"

#include <MinHook.h>

#include <cwchar>
#include <string>

namespace fvr::bf4 {
namespace {
constexpr unsigned kNvidia = 0x10DE;
constexpr unsigned kMicrosoftBasicRender = 0x1414;
constexpr unsigned kSpoofDevice = 0x1B80; // GeForce GTX 1080: a 3D Vision-era card

using GetDescFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter*, DXGI_ADAPTER_DESC*);
using GetDesc1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGIAdapter1*, DXGI_ADAPTER_DESC1*);
GetDescFn g_getDesc = nullptr;
GetDesc1Fn g_getDesc1 = nullptr;

template <typename Desc>
void Spoof(Desc* d) {
    if (!d || d->VendorId == kMicrosoftBasicRender) return; // leave WARP alone
    d->VendorId = kNvidia;
    d->DeviceId = kSpoofDevice;
    wcsncpy(d->Description, L"NVIDIA GeForce GTX 1080", 127);
    d->Description[127] = L'\0';
}

HRESULT STDMETHODCALLTYPE HookGetDesc(IDXGIAdapter* a, DXGI_ADAPTER_DESC* d) {
    const HRESULT hr = g_getDesc(a, d);
    if (SUCCEEDED(hr)) Spoof(d);
    return hr;
}

HRESULT STDMETHODCALLTYPE HookGetDesc1(IDXGIAdapter1* a, DXGI_ADAPTER_DESC1* d) {
    const HRESULT hr = g_getDesc1(a, d);
    if (SUCCEEDED(hr)) Spoof(d);
    return hr;
}
} // namespace

GpuVendorResult QueryGpuVendor(bool installNvidiaSpoof) {
    GpuVendorResult result;
    // Talk to the real System32 dxgi directly: the proxy's own factory export is re-entrant
    // during init, but there is no reason to go through it here.
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    HMODULE dxgi = LoadLibraryW((std::wstring(sys) + L"\\dxgi.dll").c_str());
    if (!dxgi) return result;
    using CreateFn = HRESULT(WINAPI*)(const GUID&, void**);
    auto create = reinterpret_cast<CreateFn>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    IDXGIFactory1* factory = nullptr;
    if (!create || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return result;
    IDXGIAdapter1* adapter = nullptr;
    if (SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) result.realVendorId = desc.VendorId;
        if (installNvidiaSpoof && result.realVendorId != kNvidia) {
            void** vtable = *reinterpret_cast<void***>(adapter);
            // IDXGIAdapter::GetDesc = slot 8, IDXGIAdapter1::GetDesc1 = slot 10.
            result.spoofInstalled =
                MH_CreateHook(vtable[8], reinterpret_cast<LPVOID>(&HookGetDesc), reinterpret_cast<LPVOID*>(&g_getDesc)) == MH_OK &&
                MH_CreateHook(vtable[10], reinterpret_cast<LPVOID>(&HookGetDesc1), reinterpret_cast<LPVOID*>(&g_getDesc1)) == MH_OK &&
                MH_EnableHook(vtable[8]) == MH_OK && MH_EnableHook(vtable[10]) == MH_OK;
        }
        adapter->Release();
    }
    factory->Release();
    return result;
}

} // namespace fvr::bf4
