#pragma once
// Offline review candidate. No installer, game invocation, or feature enablement.
#if !defined(_MSC_VER) || !defined(_M_IX86)
#error This candidate requires Windows/MSVC x86 and /arch:SSE2 (no AVX).
#endif
#include <Windows.h>
#include "Bc2CommittedOrigin386.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fvr::bc2::projectile302 {
using Word = std::uint32_t;
struct Registers { Word edi, esi, ebp, pushadEsp, ebx, edx, ecx, eax, flags; };
struct Entry { Registers registers; Word returnAddress; Word args[11]; };
struct alignas(16) FxState { unsigned char bytes[512]; };
struct alignas(16) Frame {
    FxState incoming, returned;
    Registers result;
    Entry* entry;
    Word incomingLastError, returnedLastError;
    unsigned char reserved[16]; // Explicit tail reserve matches Thunk's 1088-byte allocation.
};
static_assert(sizeof(void*) == 4 && sizeof(Registers) == 36);
static_assert(offsetof(Entry, returnAddress) == 36 && offsetof(Entry, args) == 40);
static_assert(sizeof(Entry) == 84 && sizeof(Frame) == 1088);
static_assert(offsetof(Frame, returned) == 512 && offsetof(Frame, result) == 1024);
static_assert(offsetof(Frame, entry) == 1060);
static_assert(offsetof(Frame, incomingLastError) == 1064 && offsetof(Frame, returnedLastError) == 1068);
static_assert(offsetof(Frame, reserved) == 1072 && alignof(Frame) == 16);
static_assert(std::is_trivially_copyable_v<Frame>);

// Deliberately untyped machine entry. The ABI is ECX plus eleven raw words,
// RET 2c. No semantic C++ return type is inferred from the executable.
using MachineEntry = void(__cdecl*)();
extern MachineEntry Original; // Set before MH enable; never replaced/freed.
void __cdecl Thunk();
void __cdecl Forward(Frame* frame);

// Independent, disabled committed-origin capability. ObserverApi/Grant remain
// read-only. The host separately supplies an explicit origin enrollment/request.
struct OriginKey {
    // Host values name ONE whole-shot invocation and ONE caller-P lifetime.
    // They cannot be the observer's per-projectile/helper ordinal/invocation.
    // (scopeSerial, invocation) must increase lexicographically on this thread.
    std::uint64_t scopeSerial = 0, invocation = 0, primaryLifetime = 0;
    Word thread = 0;
    bool operator==(const OriginKey&) const = default;
};
struct OriginProof {
    std::uint64_t evidence = 0, activeExit = 0, referenceSpace = 0;
    std::int64_t originalDeadline = 0;
};
struct OriginAdmission {
    OriginKey key{};
    Word primary = 0;
    origin386::Bits absoluteNativeXyz{};
    Word positiveNativeBoundBits = 0; // Reviewed positive finite IEEE binary32.
    OriginProof proof{};
};
// Context owns complete immutable host evidence for this scope; numeric proof
// IDs are joins, never substitutes for full Owner/equipment/config comparison.
// Prepare/validate must pin same live thread/whole-shot/P lifetime, first target
// ordering, original caller/target ABI, receiver, all native identity/config/
// token/reference-space and original deadlines, plus separately enrolled active
// rendered exit and conversion proof. Validate repeats immediately before stores.
// Both are bounded, no-allocation/no-wait/read-only; no game calls or retention.
// Prepare returns ALREADY AUTHORIZED absolute native XYZ; no guessed conversion.
// Validation must also rule out concurrent writers during synchronous stack edit.
using PrepareOrigin = bool(__cdecl*)(void*, const OriginKey&, const Entry&, OriginAdmission&) noexcept;
using ValidateOrigin = bool(__cdecl*)(void*, const OriginKey&, const Entry&, const OriginAdmission&) noexcept;
struct OriginApi { void* context = nullptr; PrepareOrigin prepare = nullptr; ValidateOrigin validate = nullptr; };
struct OriginSession {
    OriginApi api{}; // Immutable before hook enable; retained until process exit.
    std::atomic<bool> enabled{false}; // Explicit host enrollment/request required.
    std::atomic<bool> closed{false};
    void Close() noexcept { closed.store(true, std::memory_order_release); enabled.store(false, std::memory_order_release); }
};
extern OriginSession Origin;
enum class OriginStatus : Word {
    AwaitingEntry, Disabled, Refused, Nested, StaleScope, ReadFailure,
    Committed, WriteRestored, WriteUnresolved, AdmissionFault
};
// Construct at whole-shot entry even if no authorized exit exists. Keep alive
// through ALL its projectiles. Host must call Close from its native __finally;
// a C++ destructor under /EHsc alone does NOT establish SEH cleanup. No helper-row
// condition, diagnostic window, or 64-row budget may control this lifetime.
// The host must preserve this lifecycle: constructing only when an exit
// becomes available would miss earlier entries and is explicitly unsupported.
// The first target latches P; attempts consume eligibility before any callback.
// This object cannot reset/copy/move/reopen. A repeated host invocation is stale.
class OriginScope final {
public:
    explicit OriginScope(const OriginKey&) noexcept;
    ~OriginScope() noexcept;
    void Close() noexcept; // Idempotent lifecycle only, no native restoration.
    OriginScope(const OriginScope&) = delete;
    OriginScope& operator=(const OriginScope&) = delete;
    OriginScope(OriginScope&&) = delete;
    OriginScope& operator=(OriginScope&&) = delete;
    const OriginKey& Key() const noexcept { return key_; }
    OriginStatus Status() const noexcept { return status_; }
    bool Attempted() const noexcept { return attempted_; }
    bool NativeStarted() const noexcept { return nativeStarted_; }
    Word Primary() const noexcept { return primary_; }
private:
    friend struct OriginAccess;
    const OriginKey key_;
    OriginScope* previous_ = nullptr;
    OriginStatus status_ = OriginStatus::AwaitingEntry;
    Word primary_ = 0, ownerThread_ = 0;
    bool attempted_ = false, ambiguous_ = false, nativeStarted_ = false, closed_ = false;
};

enum class Boundary : Word { Entry, Return };
enum class SampleStatus : Word {
    Unattempted, Complete, InvalidArguments, ReadFailure, IdentityChanged, Torn
};
struct Selection {
    Word receiver = 0, vtable = 0, target = 0, backing = 0, selected = 0;
    unsigned char index = 0;
    bool indexed = false;
    bool operator==(const Selection&) const = default;
};
// Passive whole-7155b0 boundary values, not a factory call/return observation.
// 699120 uses ECX = outer receiver + 0x2c (embedded object), not *(receiver+0x2c).
// Numeric addresses are diagnostic values only; none authorize a later read.
struct ProducerSample {
    SampleStatus status = SampleStatus::Unattempted;
    Word embedded = 0, slot4Target = 0;
    std::array<Word, 4> words{}; // [outer+0x2c, outer+0x3c); words[0] is its vtable.
    bool wordsRead = false, targetRead = false;
};
struct Sample {
    SampleStatus status = SampleStatus::Unattempted;
    Selection identity{};
    Word source = 0;
    std::uint16_t flags = 0;
    std::array<Word, 3> sourceBits{}, selectedBits{}, velocityCacheBits{};
    bool flagsRead = false, sourceRead = false, selectedRead = false, velocityRead = false;
    ProducerSample producer{}; // Independent status; never changes Sample::status/outcome.
};

// Grant is a COPY from the latest accepted helper row in one live server scope.
// It grants no ownership or native writes. All borrowed addresses expire when
// that scope exits; validation must reject an expired scope before any read.
struct Grant {
    std::uint64_t scopeSerial = 0, invocation = 0;
    Word ordinal = 0, helperIndex = 0, thread = 0, token = 0;
    Word receiver = 0, vtable = 0, target = 0, returnAddress = 0, source = 0;
    Word stackLow = 0, stackHigh = 0;
};
using Read = bool(__cdecl*)(void*, Word, void*, std::size_t) noexcept;
// Must check enabled optional window/deadline, current same-thread ShotScope,
// invocation/ordinal/tokenValid/token, full Owner/equipment/nativeConfig equality,
// effects/config, descriptor identity and SAME helper row. No hash substitutes.
// Must not call native game functions, allocate, wait, write, or retain pointers.
// Return false on any missing/changed evidence. This is the integration seam.
using Validate = bool(__cdecl*)(void*, const Grant&, Boundary) noexcept;
struct ObserverApi {
    void* context = nullptr; Read read = nullptr; Validate validate = nullptr;
    bool producerDiagnostics = false; // Immutable at install; existing optional window only.
};

Sample CopyBoundary(const ObserverApi&, const Grant&, const Sample* before = nullptr) noexcept;
ProducerSample CopyProducerBoundary(const ObserverApi&, const Grant&, Boundary,
    const ProducerSample* before = nullptr) noexcept;
enum class Outcome : Word {
    Offered, RejectedIdentity, ObserverFault, EntryReadFailure, ReturnReadFailure,
    StorageIdentityChanged, Torn, NativeException, ReturnedChanged, ReturnedUnchanged, OriginCommitFailure
};
struct Record {
    Grant grant{};
    Sample before{}, after{};
    Outcome outcome = Outcome::Offered;
    bool claimed = false, originalReturned = false;
    // "Unchanged" means identical selected XYZ bits across WHOLE 7155b0 only.
    // It does not mean the target was a no-op or that 610d10 took its skip path.
    std::atomic<bool> complete{false};
};
struct Session {
    ObserverApi api{}; // Configure once before enable; retain until process exit.
    std::atomic<bool> enabled{false};
    SRWLOCK gate = SRWLOCK_INIT;
    std::array<Record, 64> rows{};
    Word offered = 0; // Access under gate; tests alone use known single-thread access.
    std::atomic<Word> busy{0}, full{0}, noGrant{0}, nested{0}, disabled{0};
    volatile LONG active = 0; // Counts Dispatch, not pre-registration thunk IPs.
    // Called synchronously by accepted helper publication, at most 64 times.
    bool Offer(const Grant&) noexcept;
    // Never reset/reopen this Session or clear Original in the live process.
    void Close() noexcept { enabled.store(false, std::memory_order_release); }
};
extern Session Observer;
extern thread_local unsigned Depth;
void __cdecl Dispatch(Frame* frame); // Native exceptions must propagate.

// Startup precondition only, before creating any hook or calling Thunk.
bool FloatingStateSupported() noexcept;
} // namespace fvr::bc2::projectile302
