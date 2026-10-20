#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>

#include "Bf4XInputSink.h"

#include <MinHook.h>

#include <atomic>
#include <cstdio>
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
std::atomic<std::uint64_t> g_polls{0}, g_virtualPolls{0}, g_idlePolls{0};
std::atomic<std::uint64_t> g_statesByUser[4]{}, g_capsByUser[4]{}; // developer evidence: which users BF4 asks for

static_assert(interaction::pad::A == XINPUT_GAMEPAD_A && interaction::pad::Start == XINPUT_GAMEPAD_START &&
              interaction::pad::RightShoulder == XINPUT_GAMEPAD_RIGHT_SHOULDER, "pad layout matches XInput");

// User 0 stays connected while the adapter runs: BF4 checks a pad's connection when it makes
// the device (its controller scan, Bf4ControllerScan.h), possibly before the VR pad is active,
// so with no real pad there user 0 answers a neutral pad while idle.
DWORD WINAPI HookGetState(DWORD user, XINPUT_STATE* state) {
    if (user < 4) g_statesByUser[user].fetch_add(1, std::memory_order_relaxed);
    if (user == 0) g_polls.fetch_add(1, std::memory_order_relaxed);
    if (user == 0 && state && g_active.load(std::memory_order_acquire)) {
        g_virtualPolls.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lock(g_lock);
        state->dwPacketNumber = g_packet;
        state->Gamepad = g_pad;
        return ERROR_SUCCESS;
    }
    const DWORD real = g_getState(user, state);
    if (user == 0 && state && real == ERROR_DEVICE_NOT_CONNECTED) {
        g_idlePolls.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard lock(g_lock);
        state->dwPacketNumber = g_packet;
        state->Gamepad = {};
        return ERROR_SUCCESS;
    }
    return real;
}

void Capabilities(XINPUT_CAPABILITIES* caps) {
    *caps = {};
    caps->Type = XINPUT_DEVTYPE_GAMEPAD;
    caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    caps->Gamepad.wButtons = 0xF3FF; // every standard button
    caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 0xFF;
    caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = static_cast<SHORT>(0xFFC0);
}

DWORD WINAPI HookGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps) {
    if (user < 4) g_capsByUser[user].fetch_add(1, std::memory_order_relaxed);
    if (user == 0 && caps && !g_active.load(std::memory_order_acquire)) {
        const DWORD real = g_getCapabilities(user, flags, caps);
        if (real != ERROR_DEVICE_NOT_CONNECTED) return real;
        Capabilities(caps);
        return ERROR_SUCCESS;
    }
    if (user == 0 && caps) {
        Capabilities(caps);
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

std::string XInputSink::Describe() {
    char line[256];
    auto n = [](const std::atomic<std::uint64_t>& v) { return static_cast<unsigned long long>(v.load()); };
    std::snprintf(line, sizeof(line),
                  "virtual_pad active=%d user0_polls=%llu virtual=%llu idle=%llu states_by_user=%llu/%llu/%llu/%llu caps_by_user=%llu/%llu/%llu/%llu",
                  static_cast<int>(g_active.load()), n(g_polls), n(g_virtualPolls), n(g_idlePolls), n(g_statesByUser[0]),
                  n(g_statesByUser[1]), n(g_statesByUser[2]), n(g_statesByUser[3]), n(g_capsByUser[0]), n(g_capsByUser[1]),
                  n(g_capsByUser[2]), n(g_capsByUser[3]));
    return line;
}

void XInputSink::Deactivate() {
    std::lock_guard lock(g_lock);
    g_pad = {};
    ++g_packet;
    g_active.store(false, std::memory_order_release);
}

} // namespace fvr::bf4
