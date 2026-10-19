#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Bf4MenuBridge.h"

#include <MinHook.h>

#include <atomic>

namespace fvr::bf4 {

namespace {
std::atomic<bool> g_active{false};
std::atomic<bool> g_buttonDown{false};
std::atomic<LONG> g_screenX{0}, g_screenY{0};

using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);
GetCursorPosFn g_getCursorPos = nullptr;
SetCursorPosFn g_setCursorPos = nullptr;
GetAsyncKeyStateFn g_getAsyncKeyState = nullptr;

BOOL WINAPI HookGetCursorPos(LPPOINT p) {
    if (g_active.load(std::memory_order_relaxed) && p) {
        p->x = g_screenX.load(std::memory_order_relaxed);
        p->y = g_screenY.load(std::memory_order_relaxed);
        return TRUE;
    }
    return g_getCursorPos(p);
}

BOOL WINAPI HookSetCursorPos(int x, int y) {
    // Menus sometimes re-centre the cursor; while the laser owns it, swallow that.
    if (g_active.load(std::memory_order_relaxed)) return TRUE;
    return g_setCursorPos(x, y);
}

SHORT WINAPI HookGetAsyncKeyState(int key) {
    if (key == VK_LBUTTON && g_active.load(std::memory_order_relaxed))
        return g_buttonDown.load(std::memory_order_relaxed) ? static_cast<SHORT>(0x8000) : static_cast<SHORT>(0);
    return g_getAsyncKeyState(key);
}

LPARAM PackClient(int x, int y) { return MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y)); }

bool HookUser32(const char* name, LPVOID detour, LPVOID* original) {
    if (MH_CreateHookApi(L"user32", name, detour, original) != MH_OK) return false;
    return MH_EnableHook(reinterpret_cast<LPVOID>(GetProcAddress(GetModuleHandleW(L"user32"), name))) == MH_OK;
}
} // namespace

bool Win32PointerSink::Install() {
    return HookUser32("GetCursorPos", reinterpret_cast<LPVOID>(&HookGetCursorPos), reinterpret_cast<LPVOID*>(&g_getCursorPos)) &&
           HookUser32("SetCursorPos", reinterpret_cast<LPVOID>(&HookSetCursorPos), reinterpret_cast<LPVOID*>(&g_setCursorPos)) &&
           HookUser32("GetAsyncKeyState", reinterpret_cast<LPVOID>(&HookGetAsyncKeyState),
                      reinterpret_cast<LPVOID*>(&g_getAsyncKeyState));
}

void Win32PointerSink::Uninstall() {
    g_active = false;
    g_buttonDown = false;
}

void Win32PointerSink::Apply(void* window, std::span<const PointerEvent> events) {
    HWND hwnd = static_cast<HWND>(window);
    if (!hwnd) return;
    for (const auto& e : events) {
        switch (e.type) {
        case PointerEvent::Type::Move: {
            POINT screen{e.x, e.y};
            ClientToScreen(hwnd, &screen);
            g_screenX = screen.x;
            g_screenY = screen.y;
            g_active = true;
            PostMessageW(hwnd, WM_MOUSEMOVE, g_buttonDown ? MK_LBUTTON : 0, PackClient(e.x, e.y));
            break;
        }
        case PointerEvent::Type::ButtonDown:
            g_buttonDown = true;
            PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, PackClient(e.x, e.y));
            break;
        case PointerEvent::Type::ButtonUp:
            g_buttonDown = false;
            PostMessageW(hwnd, WM_LBUTTONUP, 0, PackClient(e.x, e.y));
            break;
        case PointerEvent::Type::Deactivate:
            g_active = false;
            break;
        }
    }
}

} // namespace fvr::bf4
