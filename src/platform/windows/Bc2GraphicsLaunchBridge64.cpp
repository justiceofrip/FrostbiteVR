#include "Bc2GraphicsCreationProtocol.h"
#include <detours.h>
#include <strsafe.h>
#include <type_traits>
#include <cwchar>
#if !defined(_M_X64)
#error Build the graphics launch bridge as x64
#endif
static_assert(sizeof(STARTUPINFOW) == 104 && sizeof(PROCESS_INFORMATION) == 24, "Managed SDK ABI");

namespace {
using namespace fvr::bc2::graphics_creation;
// Session.ps1 is a dedicated supervisor. One launch attempt per supervisor process.
volatile LONG used, callbacks, resumeState, timedOut;
Config config;
Ticket ticket;
Payload payload;
Shared* shared;
HANDLE mapping, target, helper, helperDone, watchdog;
wchar_t expectedImage[32768], helperImage[MAX_PATH];
std::uint64_t helperCreated;
DWORD primaryTid;

BOOL Fail(DWORD error) { SetLastError(error); return FALSE; }
bool Absolute(const wchar_t* path) {
    return path && path[0] && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
}
bool PathMatches(HANDLE process, const wchar_t* expected) {
    wchar_t actual[32768]{};
    DWORD size = ARRAYSIZE(actual);
    return QueryFullProcessImageNameW(process, 0, actual, &size) && _wcsicmp(actual, expected) == 0;
}
bool Owned(const Ticket* candidate) {
    return candidate && shared && SameTicket(*candidate, ticket) && GetCurrentProcessId() == ticket.ownerPid &&
        ProcessCreation(GetCurrentProcess()) == ticket.ownerCreated && target &&
        GetProcessId(target) == ticket.targetPid && ProcessCreation(target) == ticket.targetCreated;
}
void Close(HANDLE& handle) {
    if (handle) { CloseHandle(handle); handle = nullptr; }
}
void TerminateCaptured(HANDLE process, std::uint64_t created, DWORD error) {
    if (process && created && ProcessCreation(process) == created && WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
        TerminateProcess(process, error);
}
DWORD WINAPI WatchHelper(void*) {
    if (WaitForSingleObject(helperDone, config.helperTimeoutMs) != WAIT_OBJECT_0 && helper &&
        ProcessCreation(helper) == helperCreated && WaitForSingleObject(helper, 0) == WAIT_TIMEOUT) {
        InterlockedExchange(&timedOut, 1);
        TerminateProcess(helper, ERROR_TIMEOUT); // Exact captured handle, never rediscover by PID/name.
    }
    return 0;
}
// A watchdog that has not joined retains its handles until supervisor exit.
// Never close a handle that its outstanding WaitForSingleObject may still use.
bool FinishHelper() {
    if (helperDone) SetEvent(helperDone);
    if (watchdog && WaitForSingleObject(watchdog, 3000) != WAIT_OBJECT_0) return false;
    Close(watchdog); Close(helperDone);
    return true;
}
BOOL FailLaunch(DWORD error, PROCESS_INFORMATION* process, bool ownsOriginalHandles) {
    error = error ? error : ERROR_DLL_INIT_FAILED;
    TerminateCaptured(target, ticket.targetCreated, error);
    TerminateCaptured(helper, helperCreated, error);
    if (process) {
        // Detours already closes these on its own failure; never close stale values.
        if (ownsOriginalHandles) { Close(process->hThread); Close(process->hProcess); }
        *process = {};
    }
    if (FinishHelper()) Close(helper);
    Close(target);
    if (shared) { UnmapViewOfFile(shared); shared = nullptr; }
    Close(mapping);
    return Fail(error);
}
BOOL WINAPI CreateOwned(LPCWSTR app, LPWSTR command, LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes, BOOL inherit, DWORD flags, LPVOID environment,
    LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
    const LONG ordinal = InterlockedIncrement(&callbacks);
    if (!app || !process || !startup || inherit || !(flags & CREATE_SUSPENDED) || ordinal > 2)
        return Fail(ERROR_INVALID_PARAMETER);
    const bool isHelper = ordinal == 2;
    if (_wcsicmp(app, isHelper ? helperImage : expectedImage)) return Fail(ERROR_INVALID_NAME);
    STARTUPINFOW local = *startup;
    if (isHelper) { local.dwFlags |= STARTF_USESHOWWINDOW; local.wShowWindow = SW_HIDE; }
    if (!CreateProcessW(app, command, processAttributes, threadAttributes, FALSE, flags,
        environment, directory, &local, process)) return FALSE;
    HANDLE captured = nullptr;
    const std::uint64_t created = ProcessCreation(process->hProcess);
    if (!created || !PathMatches(process->hProcess, isHelper ? helperImage : expectedImage) ||
        !DuplicateHandle(GetCurrentProcess(), process->hProcess, GetCurrentProcess(), &captured,
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE, FALSE, 0)) {
        const DWORD error = GetLastError();
        TerminateProcess(process->hProcess, ERROR_INVALID_DATA);
        Close(process->hThread); Close(process->hProcess); *process = {};
        return Fail(error ? error : ERROR_INVALID_DATA);
    }
    if (isHelper) {
        helper = captured; helperCreated = created;
        watchdog = CreateThread(nullptr, 0, WatchHelper, nullptr, 0, nullptr);
        if (!watchdog) {
            const DWORD error = GetLastError();
            TerminateProcess(process->hProcess, error);
            Close(helper); Close(process->hThread); Close(process->hProcess); *process = {};
            return Fail(error);
        }
    } else {
        BOOL wow64 = FALSE;
        if (!IsWow64Process(process->hProcess, &wow64) || !wow64) {
            TerminateProcess(process->hProcess, ERROR_BAD_EXE_FORMAT);
            Close(captured); Close(process->hThread); Close(process->hProcess); *process = {};
            return Fail(ERROR_BAD_EXE_FORMAT);
        }
        target = captured; primaryTid = process->dwThreadId;
        ticket = {sizeof(Ticket), kVersion, process->dwProcessId, config.ownerPid, created, config.ownerCreated, {}};
        std::memcpy(ticket.token, config.token, sizeof(ticket.token));
    }
    return TRUE;
}
static_assert(std::is_same<decltype(&CreateOwned), PDETOUR_CREATE_PROCESS_ROUTINEW>::value, "Detours callback ABI");
} // namespace

// Keep SDK CreateProcessW order and ABI. Detours' W entry still takes an ANSI DLL path.
extern "C" __declspec(dllexport) BOOL WINAPI Bc2GraphicsCreateSuspendedW(
    LPCWSTR app, LPWSTR command, LPSECURITY_ATTRIBUTES processAttributes, LPSECURITY_ATTRIBUTES threadAttributes,
    BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory, LPSTARTUPINFOW startup,
    LPPROCESS_INFORMATION process, LPCSTR dll, const Config* request, Ticket* outTicket) {
    if (process) *process = {};
    if (outTicket) *outTicket = {};
    if (!request || request->bytes != sizeof(Config) || request->version != kVersion ||
        request->collectEvidence > 1 || request->reserved || !HasToken(request->token) ||
        request->ownerPid != GetCurrentProcessId() || !request->ownerCreated ||
        request->ownerCreated != ProcessCreation(GetCurrentProcess()) ||
        request->helperTimeoutMs < 1000 || request->helperTimeoutMs > 60000 ||
        !Absolute(app) || !Absolute(directory) || !command || !startup || startup->cb != sizeof(STARTUPINFOW) ||
        !process || !outTicket || !dll || processAttributes || threadAttributes || inherit || !(flags & CREATE_SUSPENDED) ||
        (flags & (EXTENDED_STARTUPINFO_PRESENT | DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS)) ||
        (startup->dwFlags & STARTF_USESTDHANDLES)) return Fail(ERROR_INVALID_PARAMETER);
    const size_t dllLength = strnlen_s(dll, MAX_PATH);
    if (dllLength < 7 || dllLength >= MAX_PATH || std::strcmp(dll + dllLength - 6, "64.dll"))
        return Fail(ERROR_INVALID_PARAMETER);
    wchar_t dllWide[MAX_PATH]{};
    for (size_t index = 0; index < dllLength; ++index) {
        if (static_cast<unsigned char>(dll[index]) > 127) return Fail(ERROR_NO_UNICODE_TRANSLATION);
        dllWide[index] = wchar_t(dll[index]);
    }
    if (!Absolute(dllWide)) return Fail(ERROR_INVALID_NAME);
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Bc2GraphicsCreateSuspendedW), &self)) return FALSE;
    wchar_t loaded[MAX_PATH]{};
    const DWORD loadedLength = GetModuleFileNameW(self, loaded, ARRAYSIZE(loaded));
    const wchar_t* filename = wcsrchr(dllWide, L'\\');
    if (!loadedLength || loadedLength >= ARRAYSIZE(loaded) || _wcsicmp(loaded, dllWide) || !filename ||
        std::wcscmp(filename + 1, L"Bc2GraphicsCreation64.dll")) return Fail(ERROR_INVALID_NAME);
    dllWide[dllLength - 6] = L'3'; dllWide[dllLength - 5] = L'2';
    const DWORD attributes = GetFileAttributesW(dllWide);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return Fail(ERROR_FILE_NOT_FOUND);
    if (InterlockedCompareExchange(&used, 1, 0)) return Fail(ERROR_ALREADY_EXISTS);
    config = *request;
    if (FAILED(StringCchCopyW(expectedImage, ARRAYSIZE(expectedImage), app))) return Fail(ERROR_FILENAME_EXCED_RANGE);
    const UINT helperLength = GetSystemWow64DirectoryW(helperImage, ARRAYSIZE(helperImage));
    if (!helperLength || helperLength >= ARRAYSIZE(helperImage) ||
        FAILED(StringCchCatW(helperImage, ARRAYSIZE(helperImage), L"\\rundll32.exe"))) return Fail(ERROR_INVALID_NAME);
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(self), &pinned)) return FALSE;
    payload.config = config;
    if (FAILED(StringCchPrintfW(payload.mappingName, ARRAYSIZE(payload.mappingName),
        L"Local\\BC2Graphics-%08x-%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
        config.ownerPid, config.token[0], config.token[1], config.token[2], config.token[3],
        config.token[4], config.token[5], config.token[6], config.token[7], config.token[8], config.token[9],
        config.token[10], config.token[11], config.token[12], config.token[13], config.token[14], config.token[15])))
        return Fail(ERROR_INVALID_NAME);
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), payload.mappingName);
    if (!mapping) return Fail(GetLastError());
    if (GetLastError() == ERROR_ALREADY_EXISTS) { Close(mapping); return Fail(ERROR_ALREADY_EXISTS); }
    shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Shared)));
    if (!shared) return FailLaunch(GetLastError(), process, false);
    ZeroMemory(shared, sizeof(*shared));
    helperDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!helperDone) return FailLaunch(GetLastError(), process, false);
    BOOL succeeded = DetourCreateProcessWithDllExW(app, command, processAttributes, threadAttributes, FALSE, flags,
        environment, directory, startup, process, dll, CreateOwned);
    const BOOL detoursSucceeded = succeeded;
    DWORD error = GetLastError();
    if (!FinishHelper() || InterlockedCompareExchange(&timedOut, 0, 0)) { succeeded = FALSE; error = ERROR_TIMEOUT; }
    if (!succeeded) return FailLaunch(error, process, detoursSucceeded != FALSE);
    if (helper && WaitForSingleObject(helper, 0) != WAIT_OBJECT_0)
        return FailLaunch(ERROR_DLL_INIT_FAILED, process, true);
    Close(helper);
    HANDLE remoteOwner = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), process->hProcess, &remoteOwner,
        PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0)) return FailLaunch(GetLastError(), process, true);
    payload.ticket = ticket;
    payload.ownerHandle = reinterpret_cast<std::uintptr_t>(remoteOwner);
    // The x86 bootstrap cannot represent a remote handle above UINT32_MAX.
    if (payload.ownerHandle > UINT32_MAX) return FailLaunch(ERROR_INVALID_HANDLE, process, true);
    shared->identity = payload;
    if (!DetourCopyPayloadToProcess(process->hProcess, kPayloadGuid, &payload, sizeof(payload)))
        return FailLaunch(GetLastError(), process, true);
    if (!PathMatches(process->hProcess, expectedImage) || GetProcessId(process->hProcess) != ticket.targetPid ||
        ProcessCreation(process->hProcess) != ticket.targetCreated || WaitForSingleObject(process->hProcess, 0) != WAIT_TIMEOUT)
        return FailLaunch(ERROR_INVALID_DATA, process, true);
    *outTicket = ticket;
    return TRUE; // Still suspended. Caller persists/rechecks its ownership receipt first.
}
extern "C" __declspec(dllexport) BOOL WINAPI Bc2GraphicsResume(const Ticket* candidate, HANDLE process, HANDLE thread) {
    if (!Owned(candidate) || !process || !thread || GetProcessId(process) != ticket.targetPid ||
        ProcessCreation(process) != ticket.targetCreated || !PathMatches(process, expectedImage) ||
        GetProcessIdOfThread(thread) != ticket.targetPid || GetThreadId(thread) != primaryTid ||
        WaitForSingleObject(process, 0) != WAIT_TIMEOUT) return Fail(ERROR_INVALID_DATA);
    if (InterlockedCompareExchange(&resumeState, 1, 0)) return Fail(ERROR_ALREADY_EXISTS);
    if (ResumeThread(thread) != 1) {
        InterlockedExchange(&shared->attachError, ERROR_INVALID_STATE);
        TerminateCaptured(target, ticket.targetCreated, ERROR_INVALID_STATE);
        return Fail(ERROR_INVALID_STATE);
    }
    return TRUE; // Caller closes its original thread handle, then observes armed or process exit.
}
extern "C" __declspec(dllexport) BOOL WINAPI Bc2GraphicsStopCollection(const Ticket* candidate) {
    if (!Owned(candidate)) return Fail(ERROR_INVALID_DATA);
    InterlockedExchange(&shared->collectionStopped, 1);
    InterlockedCompareExchange(&shared->collectionReason, Stopped, Collecting);
    return TRUE; // Correction and trampoline lifetime are unaffected.
}
extern "C" __declspec(dllexport) BOOL WINAPI Bc2GraphicsAbortSuspended(const Ticket* candidate) {
    if (!Owned(candidate)) return Fail(ERROR_INVALID_DATA);
    if (InterlockedCompareExchange(&resumeState, 2, 0)) return Fail(ERROR_INVALID_STATE);
    if (WaitForSingleObject(target, 0) == WAIT_OBJECT_0) return TRUE;
    return TerminateProcess(target, ERROR_CANCELLED);
}
extern "C" __declspec(dllexport) BOOL WINAPI Bc2GraphicsReadRecord(
    const Ticket* candidate, std::uint32_t index, Record* out, std::uint32_t bytes) {
    if (!Owned(candidate) || !out || bytes != sizeof(Record) || index >= kRecordCapacity)
        return Fail(ERROR_INVALID_PARAMETER);
    if (InterlockedCompareExchange(&shared->records[index].published, 0, 0) != 2) return Fail(ERROR_IO_INCOMPLETE);
    std::memcpy(out, &shared->records[index], sizeof(*out));
    return TRUE;
}
// Eight fixed-width LONGs: armed, collectionReason, collectionStopped, reservedRecords,
// overflow, inFlight, calls, attachError. Snapshot counters are independently atomic.
extern "C" __declspec(dllexport) BOOL WINAPI Bc2GraphicsReadStatus(
    const Ticket* candidate, LONG* out, std::uint32_t count) {
    if (!Owned(candidate) || !out || count != 8) return Fail(ERROR_INVALID_PARAMETER);
    volatile LONG* counters[] = {&shared->armed, &shared->collectionReason, &shared->collectionStopped,
        &shared->reservedRecords, &shared->overflow, &shared->inFlight, &shared->nextCall, &shared->attachError};
    for (unsigned index = 0; index != 8; ++index) out[index] = InterlockedCompareExchange(counters[index], 0, 0);
    return TRUE;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
