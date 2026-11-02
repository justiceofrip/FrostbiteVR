#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Separate production protocol; never accept a private experiment payload.
namespace fvr::bc2::graphics_creation {
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kRecordCapacity = 128;
constexpr GUID kPayloadGuid = {0x4a6b0297, 0x8d17, 0x4fb1, {0xa9, 0xf3, 0x48, 0xec, 0x66, 0x10, 0x02, 0x71}};
constexpr std::uint32_t kSingleThreadedFlag = 0x1;
constexpr std::uint32_t ForwardFlags(std::uint32_t flags) noexcept {
    return flags & ~kSingleThreadedFlag;
}
enum CollectionReason : LONG { Collecting = 0, Stopped = 1, OwnerLost = 2, Full = 3, NotRequested = 4 };
enum AttachError : LONG {
    InitialStateRefused = -9701, MissingD3dRefused = -9702,
    CreateExportRefused = -9703, SwapExportRefused = -9704,
    SystemDirectoryRefused = -9705, ModuleFilenameRefused = -9706,
    SystemPathRefused = -9707, ImageRefused = -9708,
    PinRefused = -9709, RestoreWithoutError = -9710
};
#pragma pack(push, 8)
struct Config {
    std::uint32_t bytes, version, collectEvidence, ownerPid;
    std::uint64_t ownerCreated;
    std::uint32_t helperTimeoutMs, reserved;
    std::uint8_t token[16]; // CSPRNG bytes supplied by the original session supervisor.
};
struct Ticket {
    std::uint32_t bytes, version, targetPid, ownerPid;
    std::uint64_t targetCreated, ownerCreated;
    std::uint8_t token[16];
};
struct Payload {
    Config config;
    Ticket ticket;
    std::uint64_t ownerHandle; // Target-local query/synchronize HANDLE, not a pointer.
    wchar_t mappingName[96];
};
struct Record {
    volatile LONG published; // 0 unused/reserved, 1 entered, 2 complete and immutable.
    std::uint32_t kind, sequence, pid, tid, exportId, depth, proof;
    std::uint64_t processCreated, callId, parentCallId, module, entry;
    std::uint64_t device, context, swapchain, armedSequence;
    std::uint32_t originalFlags, forwardedFlags, returnedFlags;
    std::int32_t hresult;
    std::uint64_t qpc;
};
struct Shared {
    Payload identity; // Written completely before the single ResumeThread.
    volatile LONG armed, collectionReason, collectionStopped, reservedRecords;
    volatile LONG overflow, inFlight, nextCall, attachError;
    std::uint64_t module, createEntry, swapEntry, armedQpc;
    Record records[kRecordCapacity];
};
#pragma pack(pop)
static_assert(sizeof(wchar_t) == 2 && sizeof(LONG) == 4, "Windows ABI required");
static_assert(sizeof(Config) == 48 && sizeof(Ticket) == 48, "Managed ABI");
static_assert(offsetof(Config, ownerCreated) == 16 && offsetof(Config, token) == 32, "Managed ABI");
static_assert(offsetof(Ticket, targetCreated) == 16 && offsetof(Ticket, token) == 32, "Managed ABI");
static_assert(sizeof(Payload) == 296 && sizeof(Record) == 128, "Cross-bitness ABI");
static_assert(offsetof(Record, processCreated) == 32 && offsetof(Record, originalFlags) == 104, "Managed ABI");
static_assert(offsetof(Shared, records) == 360 && sizeof(Shared) == 16744, "Cross-bitness ABI");

inline std::uint64_t ProcessCreation(HANDLE process) noexcept {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
inline bool HasToken(const std::uint8_t (&token)[16]) noexcept {
    for (auto byte : token) if (byte) return true;
    return false;
}
inline bool SameTicket(const Ticket& left, const Ticket& right) noexcept {
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}
inline std::uint64_t Qpc() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return std::uint64_t(value.QuadPart);
}
} // namespace fvr::bc2::graphics_creation
