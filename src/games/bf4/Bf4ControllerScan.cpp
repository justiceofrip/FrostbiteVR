#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dbt.h>
#include <dinput.h>

#include "Bf4ControllerScan.h"

#include <MinHook.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <vector>

namespace fvr::bf4 {

namespace {
using EnumW = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8W*, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD);
using EnumA = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
// BF4's IsXInputDevice (+0x94A090): rcx = &DIDEVICEINSTANCE::guidProduct, result in al (its two
// callers, +0x946892 and +0x9466F5, test al).
using CheckFn = bool (*)(const GUID*);
constexpr unsigned kEnumDevicesSlot = 4; // IUnknown (3), CreateDevice, EnumDevices
// IID_IDirectInput8W / A (dinput.h), defined here so no dinput8.lib or dxguid.lib is needed.
constexpr GUID kIidW{0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID kIidA{0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};

EnumW g_enumW = nullptr;
EnumA g_enumA = nullptr;
CheckFn g_bf4Check = nullptr;
void* g_checkTarget = nullptr;
std::atomic<bool> g_enabled{true}, g_codeHook{false}, g_requested{false};
std::atomic<int> g_steamPads{0}; // ControllerScan::SteamPads
std::atomic<std::uint64_t> g_enumerations{0}, g_devices{0}, g_xinput{0}, g_standIns{0}, g_checks{0}, g_accepted{0};
std::mutex g_timeLock;
float g_lastMs = 0, g_maxMs = 0, g_checkMaxMs = 0;
bool g_hookedW = false, g_hookedA = false;
const char* g_status = "not installed";
std::atomic<ControllerScan::LogFn> g_log{nullptr};
std::atomic<int> g_logLines{0};
constexpr int kLogLines = 64;

void Log(const char* format, ...) {
    const ControllerScan::LogFn log = g_log.load(std::memory_order_acquire);
    if (!log || g_logLines.fetch_add(1, std::memory_order_relaxed) >= kLogLines) return;
    char line[320];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log(line);
}

template <class Instance>
void LogDevice(const char* what, const Instance& d, bool xinput) {
    char name[64];
    std::size_t i = 0;
    for (; d.tszProductName[i] && i + 1 < sizeof(name); ++i)
        name[i] = static_cast<char>(d.tszProductName[i] >= 32 && d.tszProductName[i] < 127 ? d.tszProductName[i] : '?');
    name[i] = 0;
    Log("controller scan: %s product=%08lx type=%08lx instance=%08lx xinput=%d \"%s\"", what, d.guidProduct.Data1, d.dwDevType,
        d.guidInstance.Data1, static_cast<int>(xinput), name);
}

float MillisecondsSince(const LARGE_INTEGER& start) {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return static_cast<float>(1000.0 * double(now.QuadPart - start.QuadPart) / double(freq.QuadPart));
}

const wchar_t* FindNoCase(const wchar_t* s, const wchar_t* token) {
    const std::size_t n = std::wcslen(token);
    for (; *s; ++s) {
        std::size_t i = 0;
        while (i < n && s[i] && std::towupper(s[i]) == token[i]) ++i;
        if (i == n) return s;
    }
    return nullptr;
}

bool Hex4(const wchar_t* p, std::uint32_t* value) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const wchar_t c = static_cast<wchar_t>(std::towupper(p[i]));
        if (c >= L'0' && c <= L'9') v = v * 16 + static_cast<std::uint32_t>(c - L'0');
        else if (c >= L'A' && c <= L'F') v = v * 16 + static_cast<std::uint32_t>(c - L'A' + 10);
        else return false;
    }
    *value = v;
    return true;
}

// BF4's routine, answered without WMI. The stand-in is accepted by its product id.
bool CheckHook(const GUID* product) {
    if (!g_codeHook.load(std::memory_order_acquire) || !g_enabled.load(std::memory_order_acquire) || !product)
        return g_bf4Check(product);
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    const bool yes = product->Data1 == ControllerScan::kStandInProduct ||
                     (product->Data1 == ControllerScan::kSteamVirtualPad ? g_steamPads.load(std::memory_order_relaxed) == 0
                                                                          : ControllerScan::IsXInputProduct(product->Data1));
    const float ms = MillisecondsSince(start);
    Log("controller scan: check product=%08lx -> %d (%.2f ms)", product->Data1, static_cast<int>(yes), ms);
    g_checks.fetch_add(1, std::memory_order_relaxed);
    if (yes) g_accepted.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_timeLock);
    if (ms > g_checkMaxMs) g_checkMaxMs = ms;
    return yes;
}

void SetName(char (&dst)[MAX_PATH], const char* name) { std::snprintf(dst, MAX_PATH, "%s", name); }
void SetName(wchar_t (&dst)[MAX_PATH], const char* name) {
    std::size_t i = 0;
    for (; name[i] && i + 1 < MAX_PATH; ++i) dst[i] = static_cast<wchar_t>(name[i]);
    dst[i] = 0;
}

// A wired Xbox 360 controller as DirectInput reports one (product GUID {VIDPID-0000-0000-0000-
// "PIDVID"}, HID game pad); BF4 reads only the product's Data1 and keeps the instance GUID.
template <class Instance>
Instance StandIn() {
    Instance d{};
    d.dwSize = sizeof(Instance);
    d.guidInstance = {ControllerScan::kStandInInstance, 0x4246, 0x5652, {'V', 'R', 'P', 'A', 'D', 0, 0, 1}};
    d.guidProduct = {ControllerScan::kStandInProduct, 0, 0, {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'}};
    d.dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID;
    SetName(d.tszInstanceName, "FrostbiteVR virtual pad");
    SetName(d.tszProductName, "Controller (FrostbiteVR virtual pad)");
    d.wUsagePage = 1; // generic desktop
    d.wUsage = 5;     // game pad
    return d;
}

template <class Instance>
bool IsStandIn(const Instance& d) {
    return d.guidInstance.Data1 == ControllerScan::kStandInInstance && d.guidProduct.Data1 == ControllerScan::kStandInProduct;
}

// Is this device an XInput pad as far as BF4's check will say (Steam's virtual pads per the
// developer mode, the stand-in, raw-input names)?
template <class Instance>
bool AnswersXInput(const Instance& d) {
    if (d.guidProduct.Data1 == ControllerScan::kSteamVirtualPad) return g_steamPads.load(std::memory_order_relaxed) == 0;
    return IsStandIn(d) || ControllerScan::IsXInputProduct(d.guidProduct.Data1);
}

template <class Instance>
BOOL CALLBACK CollectDevice(const Instance* device, LPVOID context) {
    if (!device) return DIENUM_CONTINUE;
    if (device->guidProduct.Data1 == ControllerScan::kSteamVirtualPad && g_steamPads.load(std::memory_order_relaxed) == 2)
        return DIENUM_CONTINUE; // developer experiment: as on a PC without them
    static_cast<std::vector<Instance>*>(context)->push_back(*device);
    return DIENUM_CONTINUE;
}

// The game's enumeration: DirectInput's devices (or the entry before ours) collected, then
// handed to the game in their order, the stand-in first when none of them is an XInput pad
// and BF4's check knows it. First, because BF4 polls only one pad: the stand-in after eight
// DirectInput pads was made an XInput device but never polled (live 2026-10-20).
template <class Instance, class Callback, class Previous, class Self>
HRESULT Relayed(Previous previous, Self* self, DWORD type, Callback callback, LPVOID ref, DWORD flags) {
    g_enumerations.fetch_add(1, std::memory_order_relaxed);
    Log("controller scan: enumeration type=%lu flags=%lu", type, flags);
    std::vector<Instance> found;
    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    const HRESULT hr = previous(self, type, &CollectDevice<Instance>, &found, flags);
    const float ms = MillisecondsSince(start);
    bool sawXInput = false;
    for (const auto& d : found) {
        const bool xinput = AnswersXInput(d);
        sawXInput = sawXInput || xinput;
        LogDevice("device", d, xinput);
    }
    g_devices.fetch_add(found.size(), std::memory_order_relaxed);
    if (sawXInput) g_xinput.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_timeLock);
        g_lastMs = ms;
        if (ms > g_maxMs) g_maxMs = ms;
    }
    if (FAILED(hr)) return hr;
    const bool padClass = type == DI8DEVCLASS_GAMECTRL || type == DI8DEVTYPE_GAMEPAD;
    if (padClass && !(flags & DIEDFL_FORCEFEEDBACK) && g_codeHook.load(std::memory_order_acquire) && !sawXInput) {
        const Instance standIn = StandIn<Instance>();
        g_standIns.fetch_add(1, std::memory_order_relaxed);
        LogDevice("stand-in", standIn, true);
        if (callback(&standIn, ref) == DIENUM_STOP) return hr;
    }
    for (const auto& d : found)
        if (callback(&d, ref) == DIENUM_STOP) break;
    return hr;
}

HRESULT STDMETHODCALLTYPE EnumHookW(IDirectInput8W* self, DWORD type, LPDIENUMDEVICESCALLBACKW callback, LPVOID ref, DWORD flags) {
    if (!callback || !ControllerScan::Filters(type) || !g_enabled.load(std::memory_order_acquire))
        return g_enumW(self, type, callback, ref, flags);
    return Relayed<DIDEVICEINSTANCEW>(g_enumW, self, type, callback, ref, flags);
}
HRESULT STDMETHODCALLTYPE EnumHookA(IDirectInput8A* self, DWORD type, LPDIENUMDEVICESCALLBACKA callback, LPVOID ref, DWORD flags) {
    if (!callback || !ControllerScan::Filters(type) || !g_enabled.load(std::memory_order_acquire))
        return g_enumA(self, type, callback, ref, flags);
    return Relayed<DIDEVICEINSTANCEA>(g_enumA, self, type, callback, ref, flags);
}

// The class vtable entry (shared by every instance in the process, BF4's included) now points
// at our relay, which calls what it held before: dinput8's own code, or the Steam overlay's
// replacement (live: the overlay swaps this entry itself). Put back on Remove only while it
// still holds ours.
struct Slot {
    void** entry = nullptr;
    void* previous = nullptr;
    void* ours = nullptr;
};
Slot g_slotW, g_slotA;

bool WriteEntry(void** entry, void* value) {
    DWORD old = 0;
    if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(entry, value);
    DWORD unused = 0;
    VirtualProtect(entry, sizeof(void*), old, &unused);
    FlushInstructionCache(GetCurrentProcess(), entry, sizeof(void*));
    return true;
}

template <class Iface, class Fn>
bool SwapSlot(CreateFn create, const GUID& iid, void* detour, Fn* previous, Slot& slot) {
    Iface* instance = nullptr;
    if (FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, iid, reinterpret_cast<LPVOID*>(&instance), nullptr)) ||
        !instance)
        return false;
    void** table = *reinterpret_cast<void***>(instance);
    instance->Release();
    void** entry = table + kEnumDevicesSlot;
    void* current = *entry;
    if (!current || current == detour) return current == detour;
    *previous = reinterpret_cast<Fn>(current);
    if (!WriteEntry(entry, detour)) return false;
    slot = {entry, current, detour};
    return true;
}
} // namespace

bool ControllerScan::Filters(std::uint32_t devType) {
    // DI8DEVCLASS_GAMECTRL, or one of the game-controller device types (joystick .. supplemental).
    return devType == DI8DEVCLASS_GAMECTRL || (devType >= DI8DEVTYPE_JOYSTICK && devType <= DI8DEVTYPE_SUPPLEMENTAL);
}

bool ControllerScan::XInputName(const wchar_t* name, std::uint32_t* product) {
    // The test BF4's WMI scan makes on each PnP device ID: "IG_" marks an XInput interface;
    // VID_xxxx and PID_xxxx give its product as MAKELONG(vid, pid).
    if (!name || !FindNoCase(name, L"IG_")) return false;
    const wchar_t* vid = FindNoCase(name, L"VID_");
    const wchar_t* pid = FindNoCase(name, L"PID_");
    std::uint32_t v = 0, p = 0;
    if (!vid || !pid || !Hex4(vid + 4, &v) || !Hex4(pid + 4, &p)) return false;
    if (product) *product = (p << 16) | v;
    return true;
}

bool ControllerScan::IsXInputProduct(std::uint32_t product) {
    std::vector<RAWINPUTDEVICELIST> list;
    UINT got = static_cast<UINT>(-1);
    for (int attempt = 0; attempt < 3 && got == static_cast<UINT>(-1); ++attempt) {
        UINT count = 0;
        if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0) return false;
        list.resize(count + 8); // devices can arrive between the two calls
        UINT room = static_cast<UINT>(list.size());
        got = GetRawInputDeviceList(list.data(), &room, sizeof(RAWINPUTDEVICELIST));
    }
    if (got == static_cast<UINT>(-1)) return false;
    for (UINT i = 0; i < got; ++i) {
        if (list[i].dwType != RIM_TYPEHID) continue;
        wchar_t name[512];
        UINT size = 512;
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, name, &size) == static_cast<UINT>(-1)) continue;
        name[511] = 0;
        std::uint32_t found = 0;
        if (XInputName(name, &found) && found == product) return true;
    }
    return false;
}

bool ControllerScan::LooksLikeWmiCheck(const void* fn) {
    static constexpr std::uint8_t kNext[] = {0x44, 0x8B, 0xC6, 0xBA, 0x10, 0x27, 0x00, 0x00, 0xFF, 0x50, 0x20};
    if (!fn) return false;
    __try {
        return std::memcmp(static_cast<const std::uint8_t*>(fn) + 0x1A7, kNext, sizeof(kNext)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ControllerScan::Install(void* isXInputDevice) {
    if (isXInputDevice && !g_checkTarget) {
        if (MH_CreateHook(isXInputDevice, reinterpret_cast<LPVOID>(&CheckHook), reinterpret_cast<LPVOID*>(&g_bf4Check)) == MH_OK) {
            if (MH_EnableHook(isXInputDevice) == MH_OK) {
                g_checkTarget = isXInputDevice;
                g_codeHook.store(true, std::memory_order_release);
            } else {
                MH_RemoveHook(isXInputDevice);
            }
        }
    }
    if (!g_hookedW && !g_hookedA) {
        HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
        if (!dinput) dinput = LoadLibraryW(L"dinput8.dll");
        const auto create = dinput ? reinterpret_cast<CreateFn>(GetProcAddress(dinput, "DirectInput8Create")) : nullptr;
        if (create) {
            g_hookedW = SwapSlot<IDirectInput8W>(create, kIidW, reinterpret_cast<void*>(&EnumHookW), &g_enumW, g_slotW);
            g_hookedA = SwapSlot<IDirectInput8A>(create, kIidA, reinterpret_cast<void*>(&EnumHookA), &g_enumA, g_slotA);
        }
    }
    g_status = g_hookedW && g_hookedA ? "installed" : g_hookedW || g_hookedA ? "partly installed" : "not installed";
    return g_hookedW || g_hookedA;
}

void ControllerScan::Remove(bool unhookCode) {
    for (Slot* slot : {&g_slotW, &g_slotA}) {
        if (slot->entry && *slot->entry == slot->ours) WriteEntry(slot->entry, slot->previous);
        *slot = {};
    }
    g_hookedW = g_hookedA = false;
    g_codeHook.store(false, std::memory_order_release); // the code hook passes through until it is removed
    if (unhookCode && g_checkTarget) {
        MH_DisableHook(g_checkTarget);
        MH_RemoveHook(g_checkTarget);
        g_checkTarget = nullptr;
    }
    g_status = "removed";
}

void ControllerScan::SetSteamPads(SteamPads mode) { g_steamPads.store(static_cast<int>(mode), std::memory_order_release); }

void ControllerScan::SetLog(LogFn log) { g_log.store(log, std::memory_order_release); }

void ControllerScan::SetEnabled(bool enabled) { g_enabled.store(enabled, std::memory_order_release); }

bool ControllerScan::RequestEnumeration(void* window) {
    if (!window || g_requested.load() || g_enumerations.load() > 0 || !g_codeHook.load() || !g_enabled.load()) return false;
    g_requested.store(true);
    return PostMessageW(static_cast<HWND>(window), WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0) != 0;
}

ControllerScan::Stats ControllerScan::GetStats() {
    Stats s;
    s.enumerations = g_enumerations.load(std::memory_order_relaxed);
    s.devices = g_devices.load(std::memory_order_relaxed);
    s.xinputDevices = g_xinput.load(std::memory_order_relaxed);
    s.standIns = g_standIns.load(std::memory_order_relaxed);
    s.checks = g_checks.load(std::memory_order_relaxed);
    s.accepted = g_accepted.load(std::memory_order_relaxed);
    s.codeHook = g_codeHook.load(std::memory_order_relaxed);
    s.requested = g_requested.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_timeLock);
    s.lastMs = g_lastMs;
    s.maxMs = g_maxMs;
    s.checkMaxMs = g_checkMaxMs;
    return s;
}

std::string ControllerScan::Describe() {
    const Stats s = GetStats();
    char line[320];
    std::snprintf(line, sizeof(line),
                  "controller_scan %s code_hook=%d enabled=%d enumerations=%llu devices=%llu with_xinput_pad=%llu stand_ins=%llu checks=%llu/%llu check_max_ms=%.2f enum_ms=%.1f max=%.1f requested=%d",
                  g_status, static_cast<int>(s.codeHook), static_cast<int>(g_enabled.load()),
                  static_cast<unsigned long long>(s.enumerations), static_cast<unsigned long long>(s.devices),
                  static_cast<unsigned long long>(s.xinputDevices), static_cast<unsigned long long>(s.standIns),
                  static_cast<unsigned long long>(s.accepted), static_cast<unsigned long long>(s.checks), s.checkMaxMs,
                  s.lastMs, s.maxMs, static_cast<int>(s.requested));
    return line;
}

} // namespace fvr::bf4
