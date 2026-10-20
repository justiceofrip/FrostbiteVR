#include "Bf4Diagnostics.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace fvr::bf4 {

namespace {
constexpr int kMaxThreads = 192, kMaxFrames = 20;
constexpr ULONGLONG kStallMs = 4000;

std::atomic<ULONGLONG> g_lastPresent{0};
std::atomic<bool> g_faultArmed{false};
std::atomic<int> g_faultsLogged{0};
HANDLE g_faultFile = INVALID_HANDLE_VALUE;
std::function<void(const std::string&)> g_sink;

struct ThreadStack {
    DWORD id = 0;
    int frames = 0;
    DWORD64 rip[kMaxFrames] = {};
};
ThreadStack g_stacks[kMaxThreads]; // filled while threads are suspended: no allocation there

// Unwinds a context with the x64 unwind data. No allocation; guarded against bad stacks.
int Unwind(CONTEXT ctx, DWORD64* out, int max) {
    int n = 0;
    __try {
        if (!ctx.Rip && ctx.Rsp && max > 0) { // a call or return to null: the caller is on top of the stack
            out[n++] = 0;
            ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
        }
        while (n < max && ctx.Rip) {
            out[n++] = ctx.Rip;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
            if (!fn) { // leaf: the return address is on top of the stack
                ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
            } else {
                void* handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

void Describe(DWORD64 address, char* out, std::size_t size) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH)) {
        const char* name = path;
        for (const char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') name = p + 1;
        std::snprintf(out, size, "%s+0x%llx", name, static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    } else {
        std::snprintf(out, size, "0x%llx", static_cast<unsigned long long>(address));
    }
}

void DumpThreads() {
    // Thread list first (allocates), then each thread is suspended only to read its stack.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    DWORD ids[kMaxThreads];
    int count = 0;
    THREADENTRY32 te{sizeof(te)};
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok && count < kMaxThreads; ok = Thread32Next(snap, &te))
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) ids[count++] = te.th32ThreadID;
    CloseHandle(snap);
    int captured = 0;
    for (int i = 0; i < count; ++i) {
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ids[i]);
        if (!t) continue;
        if (SuspendThread(t) != static_cast<DWORD>(-1)) {
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_FULL;
            ThreadStack& s = g_stacks[captured];
            s.id = ids[i];
            s.frames = GetThreadContext(t, &ctx) ? Unwind(ctx, s.rip, kMaxFrames) : 0;
            ResumeThread(t);
            ++captured;
        }
        CloseHandle(t);
    }
    for (int i = 0; i < captured; ++i) {
        std::string line = "  thread " + std::to_string(g_stacks[i].id) + ":";
        for (int f = 0; f < g_stacks[i].frames; ++f) {
            char where[160];
            Describe(g_stacks[i].rip[f], where, sizeof(where));
            line += " ";
            line += where;
        }
        g_sink(line);
    }
}

// Who has the foreground, and the state of this process's own top-level windows: a stall
// with the game in the background (another window took focus) reads differently from one
// with the game in front.
BOOL CALLBACK OwnWindow(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
    auto* text = reinterpret_cast<std::string*>(out);
    char cls[64] = {};
    GetClassNameA(hwnd, cls, sizeof(cls));
    *text += std::string(" own=") + cls + (IsIconic(hwnd) ? "(minimized)" : "");
    return TRUE;
}

std::string DescribeForeground() {
    std::string text = "foreground=";
    const HWND fg = GetForegroundWindow();
    if (!fg) {
        text += "none";
    } else {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        char cls[64] = {}, image[MAX_PATH] = "?";
        GetClassNameA(fg, cls, sizeof(cls));
        if (pid == GetCurrentProcessId()) {
            std::snprintf(image, sizeof(image), "this game");
        } else if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            char path[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameA(p, 0, path, &size)) {
                const char* name = path;
                for (const char* c = path; *c; ++c)
                    if (*c == '\\' || *c == '/') name = c + 1;
                std::snprintf(image, sizeof(image), "%s", name);
            }
            CloseHandle(p);
        }
        text += std::string(image) + " class=" + cls;
    }
    EnumWindows(OwnWindow, reinterpret_cast<LPARAM>(&text));
    return text;
}

DWORD WINAPI Watchdog(LPVOID) {
    bool reported = false;
    int dumps = 0;
    for (;;) {
        Sleep(500);
        const ULONGLONG last = g_lastPresent.load();
        if (!last) continue;
        const ULONGLONG stalled = GetTickCount64() - last;
        if (stalled < kStallMs) {
            if (reported) g_sink("diagnostics: frames are being presented again; " + DescribeForeground());
            reported = false;
            continue;
        }
        if (reported || dumps >= 3) continue;
        reported = true;
        ++dumps;
        g_sink("diagnostics: no frame presented for " + std::to_string(stalled) + " ms; " + DescribeForeground() +
               "; thread stacks follow");
        DumpThreads();
    }
}

void WriteRaw(const char* text) {
    if (g_faultFile == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(g_faultFile, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

LONG CALLBACK FaultLogger(EXCEPTION_POINTERS* e) {
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    const bool fault = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
                       code == EXCEPTION_PRIV_INSTRUCTION || code == 0xC0000374 /* heap corruption */ ||
                       code == EXCEPTION_INT_DIVIDE_BY_ZERO;
    if (!fault || !g_faultArmed.load() || g_faultsLogged.fetch_add(1) >= 24) return EXCEPTION_CONTINUE_SEARCH;
    char line[2048];
    int at = std::snprintf(line, sizeof(line), "fault 0x%08lx thread %lu addr=0x%llx info=0x%llx,0x%llx stack:",
                           static_cast<unsigned long>(code), static_cast<unsigned long>(GetCurrentThreadId()),
                           static_cast<unsigned long long>(e->ContextRecord->Rip),
                           static_cast<unsigned long long>(e->ExceptionRecord->NumberParameters > 0 ? e->ExceptionRecord->ExceptionInformation[0] : 0),
                           static_cast<unsigned long long>(e->ExceptionRecord->NumberParameters > 1 ? e->ExceptionRecord->ExceptionInformation[1] : 0));
    DWORD64 frames[16];
    const int n = Unwind(*e->ContextRecord, frames, 16);
    for (int i = 0; i < n && at > 0 && at < static_cast<int>(sizeof(line)) - 64; ++i) {
        char where[160];
        Describe(frames[i], where, sizeof(where));
        at += std::snprintf(line + at, sizeof(line) - at, " %s", where);
    }
    const CONTEXT& c = *e->ContextRecord;
    if (at > 0 && at < static_cast<int>(sizeof(line)))
        std::snprintf(line + at, sizeof(line) - at,
                      " | rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx rbp=%llx rsp=%llx r12=%llx r13=%llx r14=%llx r15=%llx",
                      static_cast<unsigned long long>(c.Rax), static_cast<unsigned long long>(c.Rbx),
                      static_cast<unsigned long long>(c.Rcx), static_cast<unsigned long long>(c.Rdx),
                      static_cast<unsigned long long>(c.Rsi), static_cast<unsigned long long>(c.Rdi),
                      static_cast<unsigned long long>(c.Rbp), static_cast<unsigned long long>(c.Rsp),
                      static_cast<unsigned long long>(c.R12), static_cast<unsigned long long>(c.R13),
                      static_cast<unsigned long long>(c.R14), static_cast<unsigned long long>(c.R15));
    WriteRaw(line);
    WriteRaw("\r\n");
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

namespace {
HHOOK g_deviceHook = nullptr;
LRESULT CALLBACK DeviceChangeHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && lParam) {
        const auto* m = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (m->message == WM_DEVICECHANGE && g_sink) {
            static std::uint64_t count = 0;
            if (++count <= 200) // a flood (a hub re-plugged) is still told, then quiet
                g_sink("device change: event 0x" + [&] {
                    char hex[16];
                    std::snprintf(hex, sizeof(hex), "%04llx", static_cast<unsigned long long>(m->wParam));
                    return std::string(hex);
                }() + " (#" + std::to_string(count) + "; BF4 re-enumerates its controllers on these)");
        }
    }
    return CallNextHookEx(g_deviceHook, code, wParam, lParam);
}
} // namespace

void Diagnostics::WatchDeviceChanges(void* window) {
    if (g_deviceHook || !window) return;
    const DWORD thread = GetWindowThreadProcessId(static_cast<HWND>(window), nullptr);
    if (thread) g_deviceHook = SetWindowsHookExW(WH_CALLWNDPROC, &DeviceChangeHook, nullptr, thread);
}

void Diagnostics::Start(std::function<void(const std::string&)> sink, const std::string& faultFile) {
    if (g_sink) return;
    g_sink = std::move(sink);
    g_faultFile = CreateFileA(faultFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    AddVectoredExceptionHandler(1, &FaultLogger);
    if (HANDLE t = CreateThread(nullptr, 0, &Watchdog, nullptr, 0, nullptr)) CloseHandle(t);
}

void Diagnostics::FramePresented() { g_lastPresent.store(GetTickCount64(), std::memory_order_relaxed); }

void Diagnostics::ArmFaultLogging(bool armed) { g_faultArmed.store(armed); }

std::string Diagnostics::DescribeForeground() { return fvr::bf4::DescribeForeground(); }

std::string Diagnostics::CurrentStack() {
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    DWORD64 rip[40] = {};
    const int frames = Unwind(ctx, rip, 40);
    std::string line;
    for (int f = 0; f < frames; ++f) {
        char where[160];
        Describe(rip[f], where, sizeof(where));
        line += " ";
        line += where;
    }
    return line;
}

} // namespace fvr::bf4
