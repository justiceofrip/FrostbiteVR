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
#include <vector>

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

// One firing object the census accepted.
struct CensusObject {
    std::uint64_t address = 0, data = 0; // the object, its FiringFunctionData
    FiringObjectFacts o;
    FiringDataFacts d;
};

// The local player's weapons (live, session 126): each of the player's guns has three firing
// objects sharing its FiringFunctionData, two of them allocated exactly kClientPairStride apart
// (the locally predicted client copies); the other soldiers' guns have a single one. All of
// them follow the HUD. A weapon is the player's when its limited-ammo objects include such a
// pair, or number three or more (the grenades' client copies were not side by side); its
// objects are listed with the ones outside a pair (the server's) first.
inline constexpr std::uint64_t kClientPairStride = 0x220;
inline constexpr int kMaxWeaponObjects = 4;

struct PlayerWeapon {
    std::uint64_t data = 0;
    std::uint64_t objects[kMaxWeaponObjects]{};
    int count = 0;
    FiringObjectFacts o; // from the first listed object
    FiringDataFacts d;
};

inline std::vector<PlayerWeapon> PlayerWeapons(const std::vector<CensusObject>& found) {
    std::vector<PlayerWeapon> out;
    for (std::size_t i = 0; i < found.size(); ++i) {
        const CensusObject& e = found[i];
        if (e.o.UnlimitedAmmo()) continue;
        bool seen = false;
        for (const PlayerWeapon& w : out) seen |= w.data == e.data;
        if (seen) continue;
        std::vector<const CensusObject*> group;
        for (const CensusObject& f : found)
            if (f.data == e.data && !f.o.UnlimitedAmmo()) group.push_back(&f);
        bool pair = false;
        for (const CensusObject* a : group)
            for (const CensusObject* b : group) pair |= b->address == a->address + kClientPairStride;
        if (!pair && group.size() < 3) continue;
        PlayerWeapon w;
        w.data = e.data;
        // Outside the pair first (the server copy), then the pair.
        auto inPair = [&](const CensusObject* x) {
            for (const CensusObject* y : group)
                if (y->address == x->address + kClientPairStride || x->address == y->address + kClientPairStride) return true;
            return false;
        };
        for (int pass = 0; pass < 2; ++pass)
            for (const CensusObject* x : group)
                if (inPair(x) == (pass == 1) && w.count < kMaxWeaponObjects) w.objects[w.count++] = x->address;
        const CensusObject* first = group.front();
        for (const CensusObject* x : group)
            if (x->address == w.objects[0]) first = x;
        w.o = first->o;
        w.d = first->d;
        out.push_back(w);
    }
    return out;
}

// The feed and action a weapon's own firing data shows, before its weapon class is known (the
// class lives outside FiringFunctionData; until it is read, ClassifyReload keeps every gun on
// button reload). Belt-fed guns look like large magazines in the data: `largeMagazine` flags
// them for an authored Belt profile, nothing more.
enum class FiringFeed : std::uint8_t { Unknown, Magazine, Tube, Single, Throwable, Detonated };
struct WeaponFamily {
    FiringFeed feed = FiringFeed::Unknown;
    bool boltOrPump = false;     // fire logic 1: a bolt or pump stroke after each shot
    bool largeMagazine = false;  // 100 rounds or more: likely belt-fed
};
inline WeaponFamily FamilyFromData(const FiringDataFacts& d) {
    WeaponFamily f;
    if (d.fireLogic == 4) f.feed = FiringFeed::Throwable;
    else if (d.fireLogic == 5) f.feed = FiringFeed::Detonated;
    else if (d.reloadType == 0) f.feed = FiringFeed::Tube;
    else if ((d.reloadType == 1 || d.reloadType == 2) && d.capacity == 1) f.feed = FiringFeed::Single;
    else if ((d.reloadType == 1 || d.reloadType == 2) && d.capacity > 1) f.feed = FiringFeed::Magazine;
    f.boltOrPump = d.fireLogic == 1;
    f.largeMagazine = f.feed == FiringFeed::Magazine && d.capacity >= 100;
    return f;
}
inline const char* FiringFeedName(FiringFeed feed) {
    switch (feed) {
    case FiringFeed::Magazine: return "magazine";
    case FiringFeed::Tube: return "tube";
    case FiringFeed::Single: return "single-load";
    case FiringFeed::Throwable: return "throwable";
    case FiringFeed::Detonated: return "detonated";
    default: return "unknown";
    }
}

inline const char* FireLogicName(std::int32_t fireLogic) {
    switch (fireLogic) {
    case 0: return "single";
    case 1: return "bolt";
    case 2: return "auto";
    case 3: return "burst";
    case 4: return "hold-release";
    case 5: return "detonated";
    default: return "?";
    }
}

} // namespace fvr::bf4
