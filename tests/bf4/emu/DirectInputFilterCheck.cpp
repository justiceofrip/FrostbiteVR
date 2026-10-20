// BF4's DirectInput game-controller enumerations answered with no devices (no WMI scan), other
// classes passed on; through the real dinput8 of this machine.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include "../../../src/games/bf4/Bf4DirectInputFilter.h"

#include <MinHook.h>

namespace {
int g_seen = 0;
BOOL CALLBACK Count(LPCDIDEVICEINSTANCEW, LPVOID) {
    ++g_seen;
    return DIENUM_CONTINUE;
}
} // namespace

bool CheckDirectInputFilter(const char*& why) {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    if (!fvr::bf4::DirectInputFilter::Filters(DI8DEVCLASS_GAMECTRL) ||
        !fvr::bf4::DirectInputFilter::Filters(DI8DEVTYPE_GAMEPAD) || fvr::bf4::DirectInputFilter::Filters(DI8DEVCLASS_ALL) ||
        fvr::bf4::DirectInputFilter::Filters(DI8DEVCLASS_KEYBOARD))
        return why = "device classes", false;
    if (!fvr::bf4::DirectInputFilter::Install()) return why = "install", false;
    HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
    using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    const auto create = dinput ? reinterpret_cast<CreateFn>(GetProcAddress(dinput, "DirectInput8Create")) : nullptr;
    constexpr GUID kIidW{0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
    IDirectInput8W* di = nullptr;
    if (!create || FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, kIidW, reinterpret_cast<LPVOID*>(&di), nullptr)) ||
        !di)
        return why = "DirectInput8Create", false;
    bool ok = true;
    const auto before = fvr::bf4::DirectInputFilter::GetStats();
    fvr::bf4::DirectInputFilter::SetEnabled(true);
    g_seen = 0;
    if (di->EnumDevices(DI8DEVCLASS_GAMECTRL, &Count, nullptr, DIEDFL_ATTACHEDONLY) != DI_OK || g_seen != 0)
        ok = (why = "game controllers not answered empty", false);
    const auto after = fvr::bf4::DirectInputFilter::GetStats();
    if (ok && (after.calls != before.calls + 1 || after.skipped != before.skipped + 1))
        ok = (why = "enumeration not counted as answered", false);
    if (ok) { // other classes go to DirectInput (its answer is this machine's)
        di->EnumDevices(DI8DEVCLASS_KEYBOARD, &Count, nullptr, DIEDFL_ATTACHEDONLY);
        if (fvr::bf4::DirectInputFilter::GetStats().passed != after.passed + 1) ok = (why = "keyboards not passed on", false);
    }
    if (ok) { // off: game controllers are passed on too
        fvr::bf4::DirectInputFilter::SetEnabled(false);
        const auto off = fvr::bf4::DirectInputFilter::GetStats();
        di->EnumDevices(DI8DEVCLASS_GAMECTRL, &Count, nullptr, DIEDFL_ATTACHEDONLY);
        const auto offAfter = fvr::bf4::DirectInputFilter::GetStats();
        if (offAfter.skipped != off.skipped || offAfter.passed != off.passed + 1) ok = (why = "filter off still answered", false);
        fvr::bf4::DirectInputFilter::SetEnabled(true);
    }
    di->Release();
    // Removed: the entry is DirectInput's again (nothing answered any more).
    fvr::bf4::DirectInputFilter::Remove();
    if (ok) {
        IDirectInput8W* again = nullptr;
        create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, kIidW, reinterpret_cast<LPVOID*>(&again), nullptr);
        if (again) {
            const auto removed = fvr::bf4::DirectInputFilter::GetStats();
            again->EnumDevices(DI8DEVCLASS_GAMECTRL, &Count, nullptr, DIEDFL_ATTACHEDONLY);
            const auto later = fvr::bf4::DirectInputFilter::GetStats();
            if (later.calls != removed.calls) ok = (why = "still filtering after removal", false);
            again->Release();
        }
        fvr::bf4::DirectInputFilter::Install(); // as the adapter leaves it
    }
    return ok;
}
