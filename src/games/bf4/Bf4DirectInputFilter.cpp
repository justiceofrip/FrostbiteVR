#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include "Bf4DirectInputFilter.h"

#include <atomic>
#include <cstdio>

namespace fvr::bf4 {

namespace {
using EnumW = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8W*, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD);
using EnumA = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
constexpr unsigned kEnumDevicesSlot = 4; // IUnknown (3), CreateDevice, EnumDevices
// IID_IDirectInput8W / A (dinput.h), defined here so no dinput8.lib or dxguid.lib is needed.
constexpr GUID kIidW{0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
constexpr GUID kIidA{0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};

EnumW g_enumW = nullptr;
EnumA g_enumA = nullptr;
std::atomic<bool> g_enabled{true};
std::atomic<std::uint64_t> g_calls{0}, g_skipped{0}, g_passed{0};
bool g_hookedW = false, g_hookedA = false;
const char* g_status = "not installed";

bool Answer(DWORD type) {
    if (!DirectInputFilter::Filters(type)) {
        g_passed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (!g_enabled.load(std::memory_order_acquire)) {
        g_passed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    g_skipped.fetch_add(1, std::memory_order_relaxed);
    return true; // no game controllers: the enumeration ends without a callback
}

HRESULT STDMETHODCALLTYPE EnumHookW(IDirectInput8W* self, DWORD type, LPDIENUMDEVICESCALLBACKW cb, LPVOID ref, DWORD flags) {
    if (Answer(type)) return DI_OK;
    return g_enumW(self, type, cb, ref, flags);
}
HRESULT STDMETHODCALLTYPE EnumHookA(IDirectInput8A* self, DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags) {
    if (Answer(type)) return DI_OK;
    return g_enumA(self, type, cb, ref, flags);
}

// The class vtable entry (shared by every instance in the process, BF4's included) now points
// at our filter, which calls what it held before: dinput8's own code, or the Steam overlay's
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

bool DirectInputFilter::Filters(std::uint32_t devType) {
    // DI8DEVCLASS_GAMECTRL, or one of the game-controller device types (joystick .. supplemental).
    return devType == DI8DEVCLASS_GAMECTRL || (devType >= DI8DEVTYPE_JOYSTICK && devType <= DI8DEVTYPE_SUPPLEMENTAL);
}

bool DirectInputFilter::Install() {
    if (g_hookedW || g_hookedA) return true;
    HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
    if (!dinput) dinput = LoadLibraryW(L"dinput8.dll");
    const auto create = dinput ? reinterpret_cast<CreateFn>(GetProcAddress(dinput, "DirectInput8Create")) : nullptr;
    if (!create) {
        g_status = "no dinput8";
        return false;
    }
    g_hookedW = SwapSlot<IDirectInput8W>(create, kIidW, reinterpret_cast<void*>(&EnumHookW), &g_enumW, g_slotW);
    g_hookedA = SwapSlot<IDirectInput8A>(create, kIidA, reinterpret_cast<void*>(&EnumHookA), &g_enumA, g_slotA);
    g_status = g_hookedW && g_hookedA ? "installed" : g_hookedW || g_hookedA ? "partly installed" : "not installed";
    return g_hookedW || g_hookedA;
}

void DirectInputFilter::Remove() {
    for (Slot* slot : {&g_slotW, &g_slotA}) {
        if (slot->entry && *slot->entry == slot->ours) WriteEntry(slot->entry, slot->previous);
        *slot = {};
    }
    g_hookedW = g_hookedA = false;
    g_status = "removed";
}

void DirectInputFilter::SetEnabled(bool enabled) { g_enabled.store(enabled, std::memory_order_release); }

DirectInputFilter::Stats DirectInputFilter::GetStats() {
    return {g_calls.load(std::memory_order_relaxed), g_skipped.load(std::memory_order_relaxed),
            g_passed.load(std::memory_order_relaxed)};
}

std::string DirectInputFilter::Describe() {
    const Stats s = GetStats();
    char line[200];
    std::snprintf(line, sizeof(line), "directinput_filter %s enabled=%d game_controller_enumerations=%llu answered_empty=%llu passed=%llu",
                  g_status, static_cast<int>(g_enabled.load()), static_cast<unsigned long long>(s.calls),
                  static_cast<unsigned long long>(s.skipped), static_cast<unsigned long long>(s.passed));
    return line;
}

} // namespace fvr::bf4
