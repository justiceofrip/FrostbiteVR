#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

#include "Bf4XInputSink.h"

#include <MinHook.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace fvr::bf4 {

namespace {
using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
GetStateFn g_getState = nullptr;
GetCapabilitiesFn g_getCapabilities = nullptr;

std::mutex g_lock;
std::atomic<bool> g_active{false};
XINPUT_GAMEPAD g_pad{};
DWORD g_packet = 0;

static_assert(interaction::pad::A == XINPUT_GAMEPAD_A && interaction::pad::Start == XINPUT_GAMEPAD_START &&
              interaction::pad::RightShoulder == XINPUT_GAMEPAD_RIGHT_SHOULDER, "pad layout matches XInput");

DWORD WINAPI HookGetState(DWORD user, XINPUT_STATE* state) {
    if (user == 0 && state && g_active.load(std::memory_order_acquire)) {
        std::lock_guard lock(g_lock);
        state->dwPacketNumber = g_packet;
        state->Gamepad = g_pad;
        return ERROR_SUCCESS;
    }
    return g_getState(user, state);
}

DWORD WINAPI HookGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps) {
    if (user == 0 && caps && g_active.load(std::memory_order_acquire)) {
        *caps = {};
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Gamepad.wButtons = 0xF3FF; // every standard button
        caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 0xFF;
        caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = static_cast<SHORT>(0xFFC0);
        return ERROR_SUCCESS;
    }
    return g_getCapabilities(user, flags, caps);
}

bool Hook(HMODULE module, const char* name, LPVOID detour, LPVOID* original) {
    const auto target = reinterpret_cast<LPVOID>(GetProcAddress(module, name));
    return target && MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK;
}
} // namespace

bool XInputSink::Install() {
    // bf4.exe imports XINPUT9_1_0 statically; load it for hosts that do not (emulator).
    HMODULE xinput = GetModuleHandleW(L"xinput9_1_0.dll");
    if (!xinput) xinput = LoadLibraryW(L"xinput9_1_0.dll");
    if (!xinput) return false;
    return Hook(xinput, "XInputGetState", reinterpret_cast<LPVOID>(&HookGetState), reinterpret_cast<LPVOID*>(&g_getState)) &&
           Hook(xinput, "XInputGetCapabilities", reinterpret_cast<LPVOID>(&HookGetCapabilities),
                reinterpret_cast<LPVOID*>(&g_getCapabilities));
}

void XInputSink::Apply(const interaction::PadState& pad, bool active) {
    std::lock_guard lock(g_lock);
    XINPUT_GAMEPAD next{};
    next.wButtons = pad.buttons;
    next.bLeftTrigger = pad.leftTrigger;
    next.bRightTrigger = pad.rightTrigger;
    next.sThumbLX = pad.thumbLX;
    next.sThumbLY = pad.thumbLY;
    next.sThumbRX = pad.thumbRX;
    next.sThumbRY = pad.thumbRY;
    if (std::memcmp(&next, &g_pad, sizeof(next)) != 0) ++g_packet; // XInput: changes bump the packet
    g_pad = next;
    g_active.store(active, std::memory_order_release);
}

void XInputSink::Deactivate() {
    std::lock_guard lock(g_lock);
    g_pad = {};
    ++g_packet;
    g_active.store(false, std::memory_order_release);
}

} // namespace fvr::bf4
