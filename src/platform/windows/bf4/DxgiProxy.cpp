// dxgi.dll proxy for bf4.exe. Copied into the game folder by the launcher ONLY for the
// duration of a VR session and removed afterwards (see launcher/fvrlaunch/session.py).
//
// Every export except the three factory functions forwards to the real System32 dxgi.dll
// through a GLOBALROOT forwarder string (no code). The factory functions are owned here:
// the first call initialises FrostbiteVR synchronously - before the game has enumerated
// a single adapter - then forwards. That ordering is what lets the adapter install the
// optional GPU-vendor report (stereo probe on AMD/Intel) before BF4 decides on its GPU path.
//
// No session section => initialisation is a no-op and the proxy is a pure pass-through.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../../../games/bf4/Bf4SessionInfo.h"
#include "../../InitGate.h"
#include "DxgiProxyExports.generated.h"

#include <cstring>
#include <cwchar>
#include <string>

#pragma comment(linker, "/export:CreateDXGIFactory=FvrCreateDXGIFactory")
#pragma comment(linker, "/export:CreateDXGIFactory1=FvrCreateDXGIFactory1")
#pragma comment(linker, "/export:CreateDXGIFactory2=FvrCreateDXGIFactory2")

namespace {

fvr::platform::InitGate g_gate;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool HostProcessIsSupported() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"bf4.exe") == 0 || _wcsicmp(name, L"Bf4EmuHost.exe") == 0;
}

HMODULE RealDxgi() {
    static HMODULE real = [] {
        wchar_t sys[MAX_PATH]{};
        GetSystemDirectoryW(sys, MAX_PATH);
        return LoadLibraryW((std::wstring(sys) + L"\\dxgi.dll").c_str());
    }();
    return real;
}

void RunSessionInit() {
    if (!HostProcessIsSupported()) return;
    HANDLE section = OpenFileMappingW(FILE_MAP_READ, FALSE, fvr::bf4::kSessionSectionName);
    if (!section) return; // not a VR session: pure pass-through
    std::string text;
    if (const void* view = MapViewOfFile(section, FILE_MAP_READ, 0, 0, fvr::bf4::kSessionSectionBytes)) {
        text.assign(static_cast<const char*>(view), fvr::bf4::kSessionSectionBytes);
        UnmapViewOfFile(view);
    }
    CloseHandle(section);
    const auto info = fvr::bf4::ParseSessionInfo(text);
    if (!info) return;
    if (info->launcherPid) { // a stale section (launcher gone) is ignored
        HANDLE launcher = OpenProcess(SYNCHRONIZE, FALSE, info->launcherPid);
        if (!launcher) return;
        const bool alive = WaitForSingleObject(launcher, 0) == WAIT_TIMEOUT;
        CloseHandle(launcher);
        if (!alive) return;
    }
    HMODULE adapter = LoadLibraryExW(Widen(info->adapterPath).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!adapter) return;
    using StartFn = int(__cdecl*)(const char* sessionText);
    if (auto start = reinterpret_cast<StartFn>(GetProcAddress(adapter, "FvrBf4_Start"))) start(text.c_str());
}

// Re-entrant calls (the adapter's own D3D device creation during init) just forward.
void EnsureInit() {
    if (g_gate.Enter(GetCurrentThreadId()) == fvr::platform::InitGate::Decision::RunInit) {
        RunSessionInit();
        g_gate.Finish();
    }
}

template <typename Fn>
Fn Real(const char* name) {
    return reinterpret_cast<Fn>(GetProcAddress(RealDxgi(), name));
}

// Fallback for a host that never creates a factory itself (should not happen with BF4).
DWORD WINAPI FallbackInitThread(LPVOID) {
    Sleep(5000);
    EnsureInit();
    return 0;
}

} // namespace

extern "C" HRESULT WINAPI FvrCreateDXGIFactory(const GUID& riid, void** factory) {
    EnsureInit();
    static const auto real = Real<HRESULT(WINAPI*)(const GUID&, void**)>("CreateDXGIFactory");
    return real ? real(riid, factory) : E_FAIL;
}

extern "C" HRESULT WINAPI FvrCreateDXGIFactory1(const GUID& riid, void** factory) {
    EnsureInit();
    static const auto real = Real<HRESULT(WINAPI*)(const GUID&, void**)>("CreateDXGIFactory1");
    return real ? real(riid, factory) : E_FAIL;
}

extern "C" HRESULT WINAPI FvrCreateDXGIFactory2(UINT flags, const GUID& riid, void** factory) {
    EnsureInit();
    static const auto real = Real<HRESULT(WINAPI*)(UINT, const GUID&, void**)>("CreateDXGIFactory2");
    return real ? real(flags, riid, factory) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        if (HANDLE t = CreateThread(nullptr, 0, FallbackInitThread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
