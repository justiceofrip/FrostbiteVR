#include "Bc2GraphicsCreationProtocol.h"
#include <d3d11.h>
#include <detours.h>
#include <strsafe.h>
#include <type_traits>
#if !defined(_M_IX86) && !defined(BC2_GRAPHICS_CREATION_TESTS)
#error Build the graphics creation bootstrap as x86
#endif
#if defined(_M_IX86) && !defined(BC2_GRAPHICS_CREATION_TESTS)
// Official Detours cross-bitness helper entry, required at ordinal 1.
#pragma comment(linker, "/export:DetourFinishHelperProcess=_DetourFinishHelperProcess@16,@1,NONAME")
#endif

namespace {
using namespace fvr::bc2::graphics_creation;
static_assert(kSingleThreadedFlag == UINT(D3D11_CREATE_DEVICE_SINGLETHREADED), "SDK flag contract");
decltype(&D3D11CreateDevice) realCreate = &D3D11CreateDevice;
decltype(&D3D11CreateDeviceAndSwapChain) realSwap = &D3D11CreateDeviceAndSwapChain;
Shared* shared;
Payload identity;
HANDLE mapping, owner;
std::uint64_t createEntry, swapEntry, d3dModule;
__declspec(thread) std::uint32_t depth;
__declspec(thread) std::uint64_t currentCall;

bool RefuseAttach(LONG reason) {
    InterlockedExchange(&shared->attachError, reason);
    return false;
}
void StopEvidence(LONG reason) {
    InterlockedCompareExchange(&shared->collectionReason, reason, Collecting);
}
Record* ReserveEvidence() {
    if (!identity.config.collectEvidence) { StopEvidence(NotRequested); return nullptr; }
    if (InterlockedCompareExchange(&shared->collectionStopped, 0, 0)) StopEvidence(Stopped);
    if (InterlockedCompareExchange(&shared->collectionReason, 0, 0)) return nullptr;
    if (WaitForSingleObject(owner, 0) != WAIT_TIMEOUT) { StopEvidence(OwnerLost); return nullptr; }
    LONG index = InterlockedCompareExchange(&shared->reservedRecords, 0, 0);
    for (;;) {
        if (index >= LONG(kRecordCapacity)) {
            InterlockedIncrement(&shared->overflow);
            StopEvidence(Full);
            return nullptr;
        }
        LONG previous = InterlockedCompareExchange(&shared->reservedRecords, index + 1, index);
        if (previous == index) return &shared->records[index];
        index = previous;
    }
}
struct CreationCall {
    Record* row;
    const UINT flags;
    const std::uint64_t previousCall;
    CreationCall(UINT original, std::uint32_t which)
        : row(ReserveEvidence()), flags(ForwardFlags(original)), previousCall(currentCall) {
        // Correction is unconditional after attach. Evidence admission cannot change it.
        InterlockedIncrement(&shared->inFlight);
        ++depth;
        currentCall = std::uint32_t(InterlockedIncrement(&shared->nextCall));
        if (!row) return;
        row->kind = 1; row->sequence = std::uint32_t(row - shared->records) + 1;
        row->pid = identity.ticket.targetPid; row->tid = GetCurrentThreadId();
        row->exportId = which; row->depth = depth;
        row->processCreated = identity.ticket.targetCreated;
        row->callId = currentCall; row->parentCallId = previousCall;
        row->module = d3dModule; row->entry = which == 1 ? createEntry : swapEntry;
        row->armedSequence = 1;
        row->originalFlags = original; row->forwardedFlags = flags; row->qpc = Qpc();
        InterlockedExchange(&row->published, 1);
    }
    void Finish(HRESULT result, ID3D11Device** device, ID3D11DeviceContext** context, IDXGISwapChain** swap) {
        if (!row) return;
        row->hresult = result;
        if (SUCCEEDED(result)) {
            if (device && *device) {
                row->device = reinterpret_cast<std::uintptr_t>(*device);
                row->returnedFlags = (*device)->GetCreationFlags();
                row->proof = 1; // Same callback thread; retain no COM reference.
            }
            if (context) row->context = reinterpret_cast<std::uintptr_t>(*context);
            if (swap) row->swapchain = reinterpret_cast<std::uintptr_t>(*swap);
        }
        InterlockedExchange(&row->published, 2);
    }
    ~CreationCall() {
        currentCall = previousCall;
        --depth;
        InterlockedDecrement(&shared->inFlight);
    }
};
HRESULT WINAPI HookCreate(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk, ID3D11Device** device,
    D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    CreationCall call(flags, 1);
    const HRESULT result = realCreate(adapter, driver, software, call.flags, levels, count, sdk, device, level, context);
    call.Finish(result, device, context, nullptr);
    return result;
}
HRESULT WINAPI HookSwap(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk, const DXGI_SWAP_CHAIN_DESC* description,
    IDXGISwapChain** swap, ID3D11Device** device, D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    CreationCall call(flags, 2);
    const HRESULT result = realSwap(adapter, driver, software, call.flags, levels, count, sdk,
        description, swap, device, level, context);
    call.Finish(result, device, context, swap);
    return result;
}
static_assert(std::is_same<decltype(&HookCreate), decltype(&D3D11CreateDevice)>::value, "SDK x86 ABI");
static_assert(std::is_same<decltype(&HookSwap), decltype(&D3D11CreateDeviceAndSwapChain)>::value, "SDK x86 ABI");
bool VerifiedExport(HMODULE module, FARPROC entry, const char* name) {
    MEMORY_BASIC_INFORMATION memory{};
    return entry && GetProcAddress(module, name) == entry &&
        VirtualQuery(reinterpret_cast<void*>(entry), &memory, sizeof(memory)) == sizeof(memory) &&
        memory.AllocationBase == module && memory.Type == MEM_IMAGE;
}
bool Attach(HMODULE self) {
    DWORD bytes = 0;
    auto payload = static_cast<Payload*>(DetourFindPayloadEx(kPayloadGuid, &bytes));
    if (!payload || bytes != sizeof(identity)) return false;
    identity = *payload;
    const Config& config = identity.config;
    const Ticket& ticket = identity.ticket;
    if (config.bytes != sizeof(config) || config.version != kVersion || config.collectEvidence > 1 ||
        config.reserved || !HasToken(config.token) || config.helperTimeoutMs < 1000 || config.helperTimeoutMs > 60000 ||
        ticket.bytes != sizeof(ticket) || ticket.version != kVersion || !config.ownerCreated ||
        config.ownerPid != ticket.ownerPid || config.ownerCreated != ticket.ownerCreated ||
        std::memcmp(config.token, ticket.token, sizeof(config.token)) || ticket.targetPid != GetCurrentProcessId() ||
        !ticket.targetCreated || ticket.targetCreated != ProcessCreation(GetCurrentProcess()) ||
        !identity.ownerHandle || identity.ownerHandle > UINT32_MAX ||
        !identity.mappingName[0] || identity.mappingName[95] != 0) return false;
    owner = reinterpret_cast<HANDLE>(std::uintptr_t(identity.ownerHandle));
    if (GetProcessId(owner) != config.ownerPid || ProcessCreation(owner) != config.ownerCreated ||
        WaitForSingleObject(owner, 0) != WAIT_TIMEOUT) return false;
    mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, identity.mappingName);
    if (!mapping) return false;
    shared = static_cast<Shared*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Shared)));
    if (!shared || std::memcmp(&shared->identity, &identity, sizeof(identity))) return false;
    if (shared->armed || shared->reservedRecords || shared->overflow || shared->inFlight || shared->nextCall ||
        shared->attachError || shared->module || shared->createEntry || shared->swapEntry || shared->armedQpc ||
        (shared->collectionStopped != 0 && shared->collectionStopped != 1) ||
        (shared->collectionReason != Collecting && shared->collectionReason != Stopped))
        return RefuseAttach(InitialStateRefused);
    if (!DetourRestoreAfterWith()) {
        const DWORD error = GetLastError();
        return RefuseAttach(error ? LONG(error) : RestoreWithoutError);
    }
    HMODULE module = GetModuleHandleW(L"d3d11.dll"); // Static import; never LoadLibrary in DllMain.
    if (!module) return RefuseAttach(MissingD3dRefused);
    realCreate = reinterpret_cast<decltype(realCreate)>(DetourCodeFromPointer(reinterpret_cast<PVOID>(realCreate), nullptr));
    realSwap = reinterpret_cast<decltype(realSwap)>(DetourCodeFromPointer(reinterpret_cast<PVOID>(realSwap), nullptr));
    if (!VerifiedExport(module, reinterpret_cast<FARPROC>(realCreate), "D3D11CreateDevice"))
        return RefuseAttach(CreateExportRefused);
    if (!VerifiedExport(module, reinterpret_cast<FARPROC>(realSwap), "D3D11CreateDeviceAndSwapChain"))
        return RefuseAttach(SwapExportRefused);
    wchar_t expected[MAX_PATH]{}, actual[MAX_PATH]{};
    // The exercised x86 loader reports SystemDirectory, including WOW64 redirection.
    UINT expectedLength = GetSystemDirectoryW(expected, ARRAYSIZE(expected));
    if (!expectedLength || expectedLength >= ARRAYSIZE(expected)) return RefuseAttach(SystemDirectoryRefused);
    DWORD actualLength = GetModuleFileNameW(module, actual, ARRAYSIZE(actual));
    if (!actualLength || actualLength >= ARRAYSIZE(actual)) return RefuseAttach(ModuleFilenameRefused);
    if (FAILED(StringCchCatW(expected, ARRAYSIZE(expected), L"\\d3d11.dll")) || _wcsicmp(expected, actual))
        return RefuseAttach(SystemPathRefused);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000)
        return RefuseAttach(ImageRefused);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return RefuseAttach(ImageRefused);
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(self), &pinned) ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(module), &pinned)) return RefuseAttach(PinRefused);
    d3dModule = reinterpret_cast<std::uintptr_t>(module);
    createEntry = reinterpret_cast<std::uintptr_t>(realCreate);
    swapEntry = reinterpret_cast<std::uintptr_t>(realSwap);
    LONG error = DetourTransactionBegin();
    if (error == NO_ERROR) {
        error = DetourUpdateThread(GetCurrentThread());
        if (error == NO_ERROR) error = DetourAttach(&realCreate, HookCreate);
        if (error == NO_ERROR) error = DetourAttach(&realSwap, HookSwap);
        if (error == NO_ERROR) error = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    if (error != NO_ERROR) return RefuseAttach(error);
    shared->module = d3dModule; shared->createEntry = createEntry; shared->swapEntry = swapEntry;
    shared->armedQpc = Qpc();
    InterlockedExchange(&shared->armed, 1);
    return true;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (DetourIsHelperProcess()) return TRUE;
        return Attach(self) ? TRUE : FALSE;
    }
    // Process-lifetime correction: never detach, unload, wait, or restore device flags.
    return TRUE;
}
