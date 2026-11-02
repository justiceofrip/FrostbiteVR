// Exercise the actual wrappers with SDK-typed originals. This switch only permits
// x64 compilation and omits the helper export; it changes no correction behavior.
#define BC2_GRAPHICS_CREATION_TESTS
#include "../src/platform/windows/Bc2GraphicsCreationBootstrap32.cpp"
#include "Test.h"

namespace {
struct ExpectedArguments {
    IDXGIAdapter* adapter = reinterpret_cast<IDXGIAdapter*>(0x1234);
    D3D_DRIVER_TYPE driver = D3D_DRIVER_TYPE_UNKNOWN;
    HMODULE software = reinterpret_cast<HMODULE>(0x2345);
    UINT flags = 0, count = 2, sdk = 0x12345678;
    const D3D_FEATURE_LEVEL* levels = nullptr;
    ID3D11Device** device = nullptr;
    D3D_FEATURE_LEVEL* level = nullptr;
    ID3D11DeviceContext** context = nullptr;
    const DXGI_SWAP_CHAIN_DESC* description = nullptr;
    IDXGISwapChain** swap = nullptr;
} expected;
volatile LONG mockCalls, argumentMismatch;
HRESULT mockResult = S_OK;
ID3D11Device* mockDevice;
ID3D11DeviceContext* mockContext;
IDXGISwapChain* mockSwap;
bool nestedCreate, stopInside, loseOwnerInside;

void CheckArguments(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk, ID3D11Device** device,
    D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    InterlockedIncrement(&mockCalls);
    if (adapter != expected.adapter || driver != expected.driver || software != expected.software ||
        flags != expected.flags || levels != expected.levels || count != expected.count || sdk != expected.sdk ||
        device != expected.device || level != expected.level || context != expected.context)
        InterlockedExchange(&argumentMismatch, 1);
}
HRESULT WINAPI MockCreate(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk, ID3D11Device** device,
    D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    CheckArguments(adapter, driver, software, flags, levels, count, sdk, device, level, context);
    if (device) *device = mockDevice;
    if (context) *context = mockContext;
    if (level) *level = D3D_FEATURE_LEVEL_11_0;
    if (stopInside) {
        InterlockedExchange(&shared->collectionStopped, 1);
        StopEvidence(Stopped);
    }
    if (loseOwnerInside) SetEvent(owner);
    return mockResult;
}
HRESULT WINAPI MockSwap(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk, const DXGI_SWAP_CHAIN_DESC* description,
    IDXGISwapChain** swap, ID3D11Device** device, D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    CheckArguments(adapter, driver, software, flags, levels, count, sdk, device, level, context);
    if (description != expected.description || swap != expected.swap) InterlockedExchange(&argumentMismatch, 1);
    if (swap) *swap = mockSwap;
    if (nestedCreate) return HookCreate(adapter, driver, software, flags, levels, count, sdk, device, level, context);
    if (device) *device = mockDevice;
    if (context) *context = mockContext;
    if (level) *level = D3D_FEATURE_LEVEL_11_0;
    return mockResult;
}
struct Fixture {
    Shared state{};
    D3D_FEATURE_LEVEL levels[2]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL resultLevel{};
    ID3D11Device* resultDevice = nullptr;
    ID3D11DeviceContext* resultContext = nullptr;
    IDXGISwapChain* resultSwap = nullptr;
    DXGI_SWAP_CHAIN_DESC description{};
    explicit Fixture(bool collect = true) {
        shared = &state;
        identity = {};
        identity.config.collectEvidence = collect ? 1 : 0;
        identity.ticket.targetPid = GetCurrentProcessId();
        identity.ticket.targetCreated = ProcessCreation(GetCurrentProcess());
        owner = CreateEventW(nullptr, TRUE, FALSE, nullptr); // Unsignaled models a live captured supervisor.
        realCreate = MockCreate; realSwap = MockSwap;
        depth = 0; currentCall = 0;
        d3dModule = 0x10000; createEntry = 0x10100; swapEntry = 0x10200;
        mockCalls = argumentMismatch = 0;
        mockResult = S_OK;
        mockDevice = nullptr;
        mockContext = reinterpret_cast<ID3D11DeviceContext*>(0x3456);
        mockSwap = reinterpret_cast<IDXGISwapChain*>(0x4567);
        nestedCreate = stopInside = loseOwnerInside = false;
        expected = {};
        expected.levels = levels;
        expected.device = &resultDevice; expected.level = &resultLevel; expected.context = &resultContext;
        expected.description = &description; expected.swap = &resultSwap;
    }
    ~Fixture() { CloseHandle(owner); owner = nullptr; shared = nullptr; }
    HRESULT Create(UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED) {
        return HookCreate(expected.adapter, expected.driver, expected.software, flags, expected.levels,
            expected.count, expected.sdk, expected.device, expected.level, expected.context);
    }
    HRESULT Swap(UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED) {
        return HookSwap(expected.adapter, expected.driver, expected.software, flags, expected.levels,
            expected.count, expected.sdk, expected.description, expected.swap, expected.device, expected.level, expected.context);
    }
};
int FlagAndArgumentForwarding() {
    Fixture fixture(false);
    CHECK(owner != nullptr);
    for (unsigned bit = 0; bit != 32; ++bit) {
        const UINT flags = (UINT(1) << bit) | D3D11_CREATE_DEVICE_SINGLETHREADED;
        expected.flags = flags & ~UINT(D3D11_CREATE_DEVICE_SINGLETHREADED);
        CHECK(fixture.Create(flags) == S_OK);
        CHECK(fixture.Swap(flags) == S_OK);
        CHECK(argumentMismatch == 0);
    }
    expected.flags = 0xfffffffeu;
    CHECK(fixture.Create(0xffffffffu) == S_OK);
    expected.flags = 0;
    CHECK(fixture.Swap(0) == S_OK);
    CHECK(mockCalls == 66 && argumentMismatch == 0);
    CHECK(fixture.resultDevice == nullptr && fixture.resultContext == mockContext && fixture.resultSwap == mockSwap);
    CHECK(fixture.resultLevel == D3D_FEATURE_LEVEL_11_0);
    CHECK(fixture.state.reservedRecords == 0 && fixture.state.collectionReason == NotRequested);
    CHECK(fixture.state.nextCall == 66 && fixture.state.inFlight == 0 && depth == 0 && currentCall == 0);
    return 0;
}
int SuccessAndFailureAreForwardedOnce() {
    Fixture fixture;
    CHECK(owner != nullptr);
    mockResult = S_FALSE; // Success is not normalized to S_OK.
    CHECK(fixture.Swap() == S_FALSE && mockCalls == 1);
    CHECK(fixture.state.records[0].published == 2 && fixture.state.records[0].hresult == S_FALSE);
    CHECK(fixture.state.records[0].context == reinterpret_cast<std::uintptr_t>(mockContext));
    CHECK(fixture.state.records[0].swapchain == reinterpret_cast<std::uintptr_t>(mockSwap));
    mockResult = HRESULT(0x887a0004u);
    mockDevice = reinterpret_cast<ID3D11Device*>(0x1); // Must never be dereferenced after failed creation.
    CHECK(fixture.Create(1) == mockResult && mockCalls == 2);
    CHECK(fixture.resultDevice == mockDevice && fixture.resultContext == mockContext);
    CHECK(fixture.state.records[1].published == 2 && fixture.state.records[1].hresult == mockResult);
    CHECK(fixture.state.records[1].proof == 0 && fixture.state.records[1].device == 0);
    CHECK(fixture.Swap(1) == mockResult && mockCalls == 3);
    CHECK(fixture.resultSwap == mockSwap && fixture.state.records[2].proof == 0);
    CHECK(argumentMismatch == 0 && fixture.state.inFlight == 0);
    expected.device = nullptr; expected.level = nullptr; expected.context = nullptr; expected.swap = nullptr;
    expected.levels = nullptr; expected.count = 0; expected.description = nullptr;
    mockResult = S_OK;
    CHECK(fixture.Create() == S_OK && fixture.Swap() == S_OK && mockCalls == 5);
    CHECK(argumentMismatch == 0);
    return 0;
}
int NestedExportsPreserveLineage() {
    Fixture fixture;
    CHECK(owner != nullptr);
    nestedCreate = true;
    CHECK(fixture.Swap() == S_OK && mockCalls == 2 && argumentMismatch == 0);
    const auto& outer = fixture.state.records[0];
    const auto& inner = fixture.state.records[1];
    CHECK(outer.published == 2 && inner.published == 2);
    CHECK(outer.exportId == 2 && outer.depth == 1 && outer.callId == 1 && outer.parentCallId == 0);
    CHECK(inner.exportId == 1 && inner.depth == 2 && inner.callId == 2 && inner.parentCallId == 1);
    CHECK(outer.originalFlags == 1 && outer.forwardedFlags == 0 && inner.originalFlags == 0 && inner.forwardedFlags == 0);
    CHECK(outer.entry == swapEntry && inner.entry == createEntry);
    CHECK(fixture.state.inFlight == 0 && depth == 0 && currentCall == 0);
    CHECK(fixture.Create() == S_OK && fixture.state.records[2].parentCallId == 0);
    return 0;
}
int EvidenceOverflowPreservesLaterCorrection() {
    Fixture fixture;
    CHECK(owner != nullptr);
    for (unsigned index = 0; index != kRecordCapacity; ++index) CHECK(fixture.Create() == S_OK);
    const Record first = fixture.state.records[0];
    const Record last = fixture.state.records[kRecordCapacity - 1];
    CHECK(fixture.Swap() == S_OK && fixture.Create() == S_OK);
    CHECK(mockCalls == LONG(kRecordCapacity + 2) && argumentMismatch == 0);
    CHECK(fixture.state.reservedRecords == LONG(kRecordCapacity) && fixture.state.overflow == 1);
    CHECK(fixture.state.collectionReason == Full && fixture.state.inFlight == 0);
    CHECK(std::memcmp(&first, &fixture.state.records[0], sizeof(first)) == 0);
    CHECK(std::memcmp(&last, &fixture.state.records[kRecordCapacity - 1], sizeof(last)) == 0);
    return 0;
}
int StopAndOwnerExitPreserveLaterCorrection() {
    {
        Fixture fixture;
        CHECK(owner != nullptr);
        stopInside = true; // An already entered record may complete across a stop.
        CHECK(fixture.Create() == S_OK);
        CHECK(fixture.state.records[0].published == 2 && fixture.state.reservedRecords == 1);
        CHECK(fixture.Swap() == S_OK && mockCalls == 2 && argumentMismatch == 0);
        CHECK(fixture.state.reservedRecords == 1 && fixture.state.collectionReason == Stopped);
    }
    {
        Fixture fixture;
        CHECK(owner != nullptr);
        loseOwnerInside = true;
        CHECK(fixture.Create() == S_OK);
        CHECK(fixture.Swap() == S_OK && mockCalls == 2 && argumentMismatch == 0);
        CHECK(fixture.state.records[0].published == 2 && fixture.state.reservedRecords == 1);
        CHECK(fixture.state.collectionReason == OwnerLost && fixture.state.inFlight == 0);
    }
    return 0;
}
DWORD WINAPI CreateMany(void*) {
    for (unsigned index = 0; index != 32; ++index)
        HookCreate(expected.adapter, expected.driver, expected.software, 1, expected.levels,
            expected.count, expected.sdk, nullptr, nullptr, nullptr);
    return 0;
}
int ConcurrentEvidenceRemainsBounded() {
    Fixture fixture;
    CHECK(owner != nullptr);
    expected.device = nullptr; expected.level = nullptr; expected.context = nullptr;
    HANDLE threads[8]{};
    unsigned started = 0;
    for (; started != ARRAYSIZE(threads); ++started) {
        threads[started] = CreateThread(nullptr, 0, CreateMany, nullptr, 0, nullptr);
        if (!threads[started]) break;
    }
    // Join all successfully created workers even if the machine refused another.
    DWORD wait = started ? WaitForMultipleObjects(started, threads, TRUE, 10000) : WAIT_FAILED;
    if (wait != WAIT_OBJECT_0) {
        // Do not destroy the shared fixture while an original-call mock is active.
        for (unsigned index = 0; index != started; ++index) WaitForSingleObject(threads[index], INFINITE);
    }
    for (unsigned index = 0; index != started; ++index) CloseHandle(threads[index]);
    CHECK(started == ARRAYSIZE(threads) && wait == WAIT_OBJECT_0);
    CHECK(mockCalls == 256 && argumentMismatch == 0);
    CHECK(fixture.state.reservedRecords == 128 && fixture.state.overflow >= 1 && fixture.state.collectionReason == Full);
    CHECK(fixture.state.inFlight == 0 && fixture.state.nextCall == 256);
    bool seen[257]{};
    for (const auto& row : fixture.state.records) {
        CHECK(row.published == 2 && row.depth == 1 && row.parentCallId == 0);
        CHECK(row.callId > 0 && row.callId <= 256 && !seen[row.callId]);
        seen[row.callId] = true;
        CHECK(row.forwardedFlags == 0 && row.originalFlags == 1);
    }
    return 0;
}
} // namespace

int main() {
    CHECK(FlagAndArgumentForwarding() == 0);
    CHECK(SuccessAndFailureAreForwardedOnce() == 0);
    CHECK(NestedExportsPreserveLineage() == 0);
    CHECK(EvidenceOverflowPreservesLaterCorrection() == 0);
    CHECK(StopAndOwnerExitPreserveLaterCorrection() == 0);
    CHECK(ConcurrentEvidenceRemainsBounded() == 0);
    return 0;
}
