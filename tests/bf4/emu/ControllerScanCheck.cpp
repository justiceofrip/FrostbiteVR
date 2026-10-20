// BF4's controller scan through the real dinput8 of this machine: game-controller enumerations
// pass through, the stand-in pad comes first when none of them is an XInput pad, BF4's
// IsXInputDevice (a stand-in function here) is answered without WMI, Steam's virtual pads count
// as XInput pads, and removal puts everything back.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include "../../../src/games/bf4/Bf4ControllerScan.h"

#include <MinHook.h>

#include <cstring>
#include <vector>

namespace {
using fvr::bf4::ControllerScan;

std::vector<DIDEVICEINSTANCEW> g_seen;
BOOL CALLBACK Collect(LPCDIDEVICEINSTANCEW d, LPVOID) {
    if (d) g_seen.push_back(*d);
    return DIENUM_CONTINUE;
}

// Plays BF4's IsXInputDevice: rcx = product GUID, result in al. Long enough to hook.
volatile long g_bodyRuns = 0;
__declspec(noinline) bool FakeBf4Check(const GUID* product) {
    InterlockedIncrement(&g_bodyRuns);
    volatile unsigned salt = 0;
    for (int i = 0; i < 3; ++i) salt = salt + static_cast<unsigned>(i);
    return product && product->Data1 == 0x12345678u + salt - 3u;
}

bool IsStandIn(const DIDEVICEINSTANCEW& d) {
    return d.guidInstance.Data1 == ControllerScan::kStandInInstance && d.guidProduct.Data1 == ControllerScan::kStandInProduct;
}
} // namespace

bool CheckControllerScan(const char*& why) {
    // Pure parts: the device-path test and the enumeration classes.
    std::uint32_t product = 0;
    if (!ControllerScan::XInputName(L"\\\\?\\HID#VID_045E&PID_028E&IG_00#7&2a1b&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}", &product) ||
        product != 0x028E045Eu)
        return why = "XInput device path", false;
    if (!ControllerScan::XInputName(L"\\\\?\\hid#vid_045e&pid_02ff&ig_00#8&1&0&0000", &product) || product != 0x02FF045Eu)
        return why = "lower-case XInput device path", false;
    if (ControllerScan::XInputName(L"\\\\?\\HID#VID_046D&PID_C52B&MI_01#7&1&0&0000", &product) ||
        ControllerScan::XInputName(L"\\\\?\\HID#VID_04ZZ&PID_028E&IG_00#", &product))
        return why = "non-XInput or malformed path accepted", false;
    if (!ControllerScan::Filters(DI8DEVCLASS_GAMECTRL) || !ControllerScan::Filters(DI8DEVTYPE_GAMEPAD) ||
        ControllerScan::Filters(DI8DEVCLASS_ALL) || ControllerScan::Filters(DI8DEVCLASS_KEYBOARD))
        return why = "device classes", false;
    std::vector<std::uint8_t> code(0x200, 0xCC);
    const std::uint8_t next[] = {0x44, 0x8B, 0xC6, 0xBA, 0x10, 0x27, 0x00, 0x00, 0xFF, 0x50, 0x20};
    std::memcpy(code.data() + 0x1A7, next, sizeof(next));
    if (!ControllerScan::LooksLikeWmiCheck(code.data()) || ControllerScan::LooksLikeWmiCheck(code.data() + 1))
        return why = "WMI routine check", false;

    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    if (!ControllerScan::Install(reinterpret_cast<void*>(&FakeBf4Check)) || !ControllerScan::GetStats().codeHook)
        return why = "install", false;
    HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
    using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto create = dinput ? reinterpret_cast<CreateFn>(GetProcAddress(dinput, "DirectInput8Create")) : nullptr;
    constexpr GUID kIidW{0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    IDirectInput8W* di = nullptr;
    if (!create || FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, kIidW, reinterpret_cast<LPVOID*>(&di), nullptr)) ||
        !di) {
        ControllerScan::Remove(true);
        return why = "DirectInput8Create", false;
    }
    bool ok = true;
    const auto before = ControllerScan::GetStats();
    g_seen.clear();
    if (di->EnumDevices(DI8DEVCLASS_GAMECTRL, &Collect, nullptr, DIEDFL_ATTACHEDONLY) != DI_OK) ok = (why = "enumeration failed", false);
    // What this machine has decides whether the stand-in comes: it does when no XInput pad is attached.
    bool realPad = false;
    int standIns = 0;
    for (const auto& d : g_seen) {
        if (IsStandIn(d)) ++standIns;
        else if (ControllerScan::IsXInputProduct(d.guidProduct.Data1)) realPad = true;
    }
    const auto after = ControllerScan::GetStats();
    if (ok && (after.enumerations != before.enumerations + 1 || after.devices != before.devices + (g_seen.size() - standIns)))
        ok = (why = "enumeration not counted", false);
    if (ok && standIns != (realPad ? 0 : 1)) ok = (why = realPad ? "stand-in offered next to a real XInput pad" : "no stand-in pad", false);
    if (ok && !realPad && !IsStandIn(g_seen.front())) ok = (why = "stand-in not ahead of DirectInput's devices", false);
    if (ok && !realPad && (g_seen.front().dwDevType & 0xFF) != DI8DEVTYPE_GAMEPAD) ok = (why = "stand-in is not a game pad", false);
    // Again: answered from the last list at once; a worker enumerates behind it and finds the
    // same devices (nothing posted).
    if (ok) {
        const auto c0 = ControllerScan::GetStats();
        const std::vector<DIDEVICEINSTANCEW> first = g_seen;
        g_seen.clear();
        if (di->EnumDevices(DI8DEVCLASS_GAMECTRL, &Collect, nullptr, DIEDFL_ATTACHEDONLY) != DI_OK)
            ok = (why = "cached enumeration failed", false);
        const auto c1 = ControllerScan::GetStats();
        if (ok && (c1.cached != c0.cached + 1 || g_seen.size() != first.size()))
            ok = (why = "second enumeration not answered from the last list", false);
        for (int i = 0; ok && i < 300 && ControllerScan::GetStats().refreshes == c0.refreshes; ++i) Sleep(10);
        const auto c2 = ControllerScan::GetStats();
        if (ok && (c2.refreshes == c0.refreshes || c2.refreshChanges != c0.refreshChanges))
            ok = (why = "the worker did not enumerate again, or saw a change", false);
    }
    // BF4's check: answered without running its body; the stand-in is an XInput device.
    if (ok) {
        const long runs = g_bodyRuns;
        const GUID standIn{ControllerScan::kStandInProduct, 0, 0, {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'}};
        const GUID other{0x0BAD0BADu, 0, 0, {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'}};
        const GUID steam{ControllerScan::kSteamVirtualPad, 0, 0, {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'}};
        const auto s0 = ControllerScan::GetStats();
        if (!FakeBf4Check(&standIn) || FakeBf4Check(&other) || g_bodyRuns != runs) ok = (why = "check not answered by the scan", false);
        if (ok && !FakeBf4Check(&steam)) ok = (why = "Steam's virtual pad not an XInput pad", false);
        ControllerScan::SetSteamPads(ControllerScan::SteamPads::AsDirectInput);
        if (ok && FakeBf4Check(&steam)) ok = (why = "Steam's virtual pad still XInput as a DirectInput pad", false);
        ControllerScan::SetSteamPads(ControllerScan::SteamPads::AsXInput);
        const auto s1 = ControllerScan::GetStats();
        if (ok && (s1.checks != s0.checks + 4 || s1.accepted != s0.accepted + 2)) ok = (why = "checks not counted", false);
    }
    // Off: enumerations and checks are BF4's own again.
    if (ok) {
        ControllerScan::SetEnabled(false);
        g_seen.clear();
        di->EnumDevices(DI8DEVCLASS_GAMECTRL, &Collect, nullptr, DIEDFL_ATTACHEDONLY);
        for (const auto& d : g_seen)
            if (IsStandIn(d)) ok = (why = "stand-in offered while off", false);
        const long runs = g_bodyRuns;
        const GUID other{0x0BAD0BADu, 0, 0, {0}};
        FakeBf4Check(&other);
        if (ok && g_bodyRuns != runs + 1) ok = (why = "check answered while off", false);
        ControllerScan::SetEnabled(true);
    }
    // Keyboards are never touched.
    if (ok) {
        const auto k0 = ControllerScan::GetStats();
        di->EnumDevices(DI8DEVCLASS_KEYBOARD, [](LPCDIDEVICEINSTANCEW, LPVOID) -> BOOL { return DIENUM_CONTINUE; }, nullptr,
                        DIEDFL_ATTACHEDONLY);
        if (ControllerScan::GetStats().enumerations != k0.enumerations) ok = (why = "keyboard enumeration relayed", false);
    }
    di->Release();
    // Removed: the vtable entry and the code are as they were.
    ControllerScan::Remove(true);
    if (ok) {
        IDirectInput8W* again = nullptr;
        create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, kIidW, reinterpret_cast<LPVOID*>(&again), nullptr);
        if (again) {
            const auto removed = ControllerScan::GetStats();
            g_seen.clear();
            again->EnumDevices(DI8DEVCLASS_GAMECTRL, &Collect, nullptr, DIEDFL_ATTACHEDONLY);
            if (ControllerScan::GetStats().enumerations != removed.enumerations) ok = (why = "still relaying after removal", false);
            for (const auto& d : g_seen)
                if (IsStandIn(d)) ok = (why = "stand-in after removal", false);
            again->Release();
        }
        const long runs = g_bodyRuns;
        const GUID other{0x0BAD0BADu, 0, 0, {0}};
        FakeBf4Check(&other);
        if (ok && g_bodyRuns != runs + 1) ok = (why = "code hook left in place", false);
    }
    return ok;
}
