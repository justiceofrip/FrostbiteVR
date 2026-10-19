#pragma once
// Read-only census of BF4's runtime weapon-firing objects (docs/BF4-MANUAL-RELOAD.md 2.1),
// to answer from one live session: which objects belong to the player's weapon, whether
// single player keeps a client and a server copy, and whether the static layout holds.
//
// Portable half: the structural rules over raw bytes (unit-tested). The Windows half
// copies heap memory with ReadProcessMemory on a background thread and feeds these.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace fvr::bf4 {

inline constexpr std::size_t kFiringObjectBytes = 0x210;  // through the +0x208/+0x209 flags
inline constexpr std::size_t kFiringDataBytes = 0x250;    // FiringFunctionData through UnlimitedAmmoForAI
inline constexpr std::uint64_t kAmmoConfigInFiringData = 0x220;

struct FiringObjectFacts {
    std::uint64_t weaponFiringData = 0; // +0x128
    std::uint64_t ammoConfig = 0;       // +0x130, = FiringFunctionData + 0x220
    std::int32_t state = 0, nextState = 0, reloadStep = 0, loaded = 0, reserve = 0, capacityOverride = 0;
    std::int32_t fireMode = 0;
    float reloadMultiplier = 0;
    std::uint8_t flags208 = 0, flags209 = 0;
    bool UnlimitedAmmo() const { return (flags208 & 0x30) != 0; }
};

struct FiringDataFacts {
    std::int32_t reloadType = 0, fireLogic = 0, capacity = 0, magazines = 0;
    float reloadTime = 0, threshold = 0;
    bool unlimitedForAi = false;
};

namespace census_detail {
template <typename T>
T At(const std::uint8_t* p, std::size_t off) {
    T v;
    std::memcpy(&v, p + off, sizeof(T));
    return v;
}
inline bool UserPointer(std::uint64_t p) { return p >= 0x10000 && p < 0x7FFFFFFF0000ull && (p & 7) == 0; }
} // namespace census_detail

// Local checks only (no dereference), cheap enough to run on every 16-byte slot.
inline std::optional<FiringObjectFacts> ParseFiringObject(const std::uint8_t* o /* kFiringObjectBytes */) {
    using census_detail::At;
    FiringObjectFacts f;
    f.weaponFiringData = At<std::uint64_t>(o, 0x128);
    f.ammoConfig = At<std::uint64_t>(o, 0x130);
    if (!census_detail::UserPointer(f.weaponFiringData) || !census_detail::UserPointer(f.ammoConfig) ||
        f.ammoConfig < kAmmoConfigInFiringData)
        return std::nullopt;
    f.state = At<std::int32_t>(o, 0x148);
    f.nextState = At<std::int32_t>(o, 0x150);
    f.reloadMultiplier = At<float>(o, 0x17C);
    f.reloadStep = At<std::int32_t>(o, 0x188);
    f.loaded = At<std::int32_t>(o, 0x1A0);
    f.reserve = At<std::int32_t>(o, 0x1A4);
    f.fireMode = At<std::int32_t>(o, 0x1BC);
    f.capacityOverride = At<std::int32_t>(o, 0x1C0);
    f.flags208 = o[0x208];
    f.flags209 = o[0x209];
    if (f.state < 0 || f.state > 15 || f.nextState < 0 || f.nextState > 15) return std::nullopt;
    if (!std::isfinite(f.reloadMultiplier) || f.reloadMultiplier < 0.05f || f.reloadMultiplier > 10.0f) return std::nullopt;
    if (f.reloadStep < -1 || f.reloadStep > 1000 || f.loaded < 0 || f.loaded > 1000 || f.reserve < 0 || f.reserve > 100000)
        return std::nullopt;
    if (f.fireMode < 0 || f.fireMode > 8 || f.capacityOverride < -1 || f.capacityOverride > 1000) return std::nullopt;
    return f;
}

// The FiringFunctionData the object's ammo pointer implies, read at ammoConfig - 0x220.
inline std::optional<FiringDataFacts> ParseFiringData(const std::uint8_t* d /* kFiringDataBytes */) {
    using census_detail::At;
    FiringDataFacts f;
    f.reloadTime = At<float>(d, 0x1DC);
    f.threshold = At<float>(d, 0x1EC);
    f.reloadType = At<std::int32_t>(d, 0x204);
    f.fireLogic = At<std::int32_t>(d, 0x208);
    f.capacity = At<std::int32_t>(d, 0x220);
    f.magazines = At<std::int32_t>(d, 0x224);
    f.unlimitedForAi = d[0x249] != 0;
    if (f.reloadType < 0 || f.reloadType > 2 || f.fireLogic < 0 || f.fireLogic > 5) return std::nullopt;
    if (f.capacity < 1 || f.capacity > 1000 || f.magazines < -1 || f.magazines > 1000) return std::nullopt;
    if (!std::isfinite(f.reloadTime) || f.reloadTime < 0 || f.reloadTime > 30 || !std::isfinite(f.threshold) ||
        f.threshold < 0 || f.threshold > 1)
        return std::nullopt;
    return f;
}

// Ownership (static, docs/BF4-MANUAL-RELOAD.md 2.3): single player keeps a client and a
// server firing object per gun. Client weapons derive from an abstract base (vtable
// +0x1D46760, never seen on a live object) and hold their firing object at +0x8:
//   class A, vtable +0x1D467A0, constructor +0x968F90 creates the firing object, update
//            +0x9739F0 (slot 2; uses the client camera manager);
//   class B, vtable +0x1D467E0, constructor +0x9691C0 is handed one, update +0x973C90.
// The server weapon (vtable +0x1D64680, constructor +0xA18C60, update +0xA26220, slot 41)
// holds its firing object at +0xA0. A live object starts with its most-derived vtable.
inline constexpr std::uint64_t kClientWeaponVtableRva = 0x1D467A0;
inline constexpr std::uint64_t kClientWeaponBVtableRva = 0x1D467E0;
inline constexpr std::uint64_t kServerWeaponVtableRva = 0x1D64680;
inline constexpr std::uint64_t kClientWeaponFiringOffset = 0x8;
inline constexpr std::uint64_t kServerWeaponFiringOffset = 0xA0;

enum class FiringHolder : std::uint8_t { Unknown, ClientWeapon, ClientWeaponB, ServerWeapon };

// One client-side weapon entity owns both client classes (constructor +0x968D00, vtable
// +0x1D46540): class B at +0xA0, class A at +0xA8, and the active one at +0x98.
inline constexpr std::uint64_t kClientWeaponParentVtableRva = 0x1D46540;
inline constexpr std::uint64_t kParentHoldsBOffset = 0xA0;
inline constexpr std::uint64_t kParentHoldsAOffset = 0xA8;
inline constexpr std::uint64_t kParentActiveOffset = 0x98;

// The client weapon entity that holds `holder` (class A or B) at a pointer found at `at`:
// `qwordAt(at - offset)` must be the parent's vtable for that class's slot. 0 otherwise.
template <typename ReadQword>
std::uint64_t ClientWeaponParent(FiringHolder holder, std::uint64_t at, std::uint64_t imageBase, ReadQword qwordAt) {
    const std::uint64_t offset = holder == FiringHolder::ClientWeapon    ? kParentHoldsAOffset
                               : holder == FiringHolder::ClientWeaponB ? kParentHoldsBOffset
                                                                        : 0;
    if (!offset || at < offset) return 0;
    return qwordAt(at - offset) == imageBase + kClientWeaponParentVtableRva ? at - offset : 0;
}

inline const char* FiringHolderName(FiringHolder h) {
    switch (h) {
    case FiringHolder::ClientWeapon: return "client weapon A";
    case FiringHolder::ClientWeaponB: return "client weapon B";
    case FiringHolder::ServerWeapon: return "server weapon";
    default: return "unclassified holder";
    }
}

// `atClientSlot` = the qword 0x8 before the pointer, `atServerSlot` = the qword 0xA0 before.
inline FiringHolder ClassifyFiringHolder(std::uint64_t atClientSlot, std::uint64_t atServerSlot, std::uint64_t imageBase) {
    if (atClientSlot == imageBase + kClientWeaponVtableRva) return FiringHolder::ClientWeapon;
    if (atClientSlot == imageBase + kClientWeaponBVtableRva) return FiringHolder::ClientWeaponB;
    if (atServerSlot == imageBase + kServerWeaponVtableRva) return FiringHolder::ServerWeapon;
    return FiringHolder::Unknown;
}

} // namespace fvr::bf4
