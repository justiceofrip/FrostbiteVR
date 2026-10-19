// Head-tracked main view (Bf4ViewOverride): the math, and the vtable-copy mechanism driven
// in-process through a fake renderer object laid out like bf4.exe's (locator + 23 slots,
// submit in slot 5, view block 0 at the start of the views buffer).
#include "TestHarness.h"

#include "../../src/games/bf4/Bf4ViewOverride.h"

#include <cmath>
#include <cstring>
#include <thread>

using namespace fvr;
using namespace fvr::bf4;
using frostbite::LinearTransform;
using fvrtest::Near;

namespace {
LinearTransform Yawed(float degrees, math::Vec3 at) {
    const float r = degrees * 3.14159265f / 180.0f;
    LinearTransform t;
    t.right = {std::cos(r), 0, -std::sin(r)};
    t.forward = {std::sin(r), 0, std::cos(r)};
    t.trans = at;
    return t;
}
LinearTransform Pitched(const LinearTransform& base, float degrees) { // nose up
    const float r = degrees * 3.14159265f / 180.0f;
    LinearTransform t = base;
    t.up = base.up * std::cos(r) - base.forward * std::sin(r);
    t.forward = base.forward * std::cos(r) + base.up * std::sin(r);
    return t;
}
bool Same(math::Vec3 a, math::Vec3 b) { return Near(a.x, b.x, 1e-4f) && Near(a.y, b.y, 1e-4f) && Near(a.z, b.z, 1e-4f); }

// Fake native side.
struct FakeSubmitLog {
    int calls = 0;
    LinearTransform seen, seenCopy;
    std::uint32_t seenDirty = 0;
    bool scribble = false; // the "game" rewrites the block during the call
};
FakeSubmitLog g_log;
void FakeSubmit(void*, float, float, std::uint32_t count, std::uint8_t* views, void*, void*) {
    ++g_log.calls;
    if (!count || !views) return;
    g_log.seen = ReadViewTransform(views);
    g_log.seenCopy = ReadViewTransform(views + kViewCopyOffset);
    std::memcpy(&g_log.seenDirty, views + kViewDirtyOffset, 4);
    if (g_log.scribble) WriteViewTransform(views, Yawed(-45, {9, 9, 9}));
}
void OtherSlot(void*) {}

struct FakeRenderer {
    void* table[1 + kRendererVtableSlots]; // [0] = RTTI locator, then the slots
    void** vptr;                           // the object's first field, as in bf4.exe
    FakeRenderer() {
        table[0] = reinterpret_cast<void*>(0x1234);
        for (std::size_t i = 1; i <= kRendererVtableSlots; ++i) table[i] = reinterpret_cast<void*>(&OtherSlot);
        table[1 + kRendererSubmitSlot] = reinterpret_cast<void*>(&FakeSubmit);
        vptr = table + 1;
    }
    void* Object() { return &vptr; }
    void SubmitThroughVtable(std::uint8_t* views) { // what bf4.exe's +0x8288DE call does
        void** vt = vptr;
        reinterpret_cast<RendererSubmitFn>(vt[kRendererSubmitSlot])(Object(), 0.016f, 1.0f, 1, views, nullptr, nullptr);
    }
};

struct alignas(16) Views {
    std::uint8_t bytes[0x1020] = {};
    explicit Views(const LinearTransform& game) {
        WriteViewTransform(bytes, game);
        WriteViewTransform(bytes + kViewCopyOffset, game);
        const float w = 7.0f; // padding lane: must survive
        std::memcpy(bytes + 12, &w, 4);
        const std::uint32_t dirty = 0x1;
        std::memcpy(bytes + kViewDirtyOffset, &dirty, 4);
    }
};
} // namespace

TEST_CASE(HeadTrackedViewKeepsGameYawAndHeadsetPitch) {
    const LinearTransform game = Pitched(Yawed(90, {10, 2, -5}), 30); // looking east, 30 deg up
    LinearTransform identity;
    const auto level = HeadTrackedView(game, identity, false);
    CHECK(Same(level.forward, {1, 0, 0}) && Same(level.up, {0, 1, 0}) && Same(level.right, {0, 0, -1}));
    CHECK(Same(level.trans, {10, 2, -5}));
    const auto full = HeadTrackedView(game, identity, true); // keep the game's pitch
    CHECK(Same(full.forward, game.forward) && Same(full.up, game.up));
    // Headset 20 deg up and 0.1 m to the right: pitch comes from the head only.
    LinearTransform head = Pitched(LinearTransform{}, 20);
    head.trans = {0.1f, 0, 0};
    const auto tracked = HeadTrackedView(game, head, false);
    CHECK(Near(tracked.forward.y, std::sin(20 * 3.14159265f / 180), 1e-4f));
    CHECK(Same(tracked.trans, {10, 2, -5.1f})); // right of an east-facing camera is -Z
    // Straight down: heading still comes from the right row.
    const auto down = HeadTrackedView(Pitched(Yawed(90, {}), -90), identity, false);
    CHECK(Same(down.forward, {1, 0, 0}));
}

TEST_CASE(ViewOverrideAppliesRestoresAndDetachesExactly) {
    g_log = {};
    FakeRenderer r;
    void** const originalVptr = r.vptr;
    const auto submit = reinterpret_cast<std::uintptr_t>(&FakeSubmit);
    const LinearTransform game = Yawed(0, {1, 2, 3});
    ViewOverride vo;
    CHECK(vo.Install(r.Object(), submit + 1) == ViewOverride::InstallResult::UnexpectedSubmit);
    CHECK(r.vptr == originalVptr);
    CHECK(vo.Install(r.Object(), submit) == ViewOverride::InstallResult::Ok);
    CHECK(r.vptr != originalVptr && r.vptr[-1] == originalVptr[-1]); // RTTI locator kept
    bool othersSame = true;
    for (std::size_t i = 0; i < kRendererVtableSlots; ++i)
        if (i != kRendererSubmitSlot) othersSame = othersSame && r.vptr[i] == originalVptr[i];
    CHECK(othersSame);

    Views v(game);
    Views pristine(game);
    r.SubmitThroughVtable(v.bytes); // no pose yet: the stock view goes through untouched
    CHECK(g_log.calls == 1 && Same(g_log.seen.forward, game.forward));
    CHECK(std::memcmp(v.bytes, pristine.bytes, sizeof(v.bytes)) == 0);

    vo.SetLocal(Yawed(30, {}), false);
    r.SubmitThroughVtable(v.bytes);
    CHECK(Same(g_log.seen.forward, Yawed(30, {}).forward) && Same(g_log.seenCopy.forward, Yawed(30, {}).forward));
    CHECK((g_log.seenDirty & kViewDirtyBits) == kViewDirtyBits);
    CHECK(std::memcmp(v.bytes, pristine.bytes, sizeof(v.bytes)) == 0); // exact restore, w lane and dirty too
    auto s = vo.GetStats();
    CHECK(s.submits == 2 && s.applied == 1 && s.restored == 1 && s.foreign == 0);

    g_log.scribble = true; // the game wrote its own value during the call: keep the game's
    r.SubmitThroughVtable(v.bytes);
    g_log.scribble = false;
    CHECK(Same(ReadViewTransform(v.bytes).trans, {9, 9, 9}) && vo.GetStats().foreign == 1);

    Views garbage(game); // block 0 is not a camera transform (wrong layout): left alone
    const float two = 2.0f;
    std::memcpy(garbage.bytes, &two, 4);
    Views garbageCopy = garbage;
    r.SubmitThroughVtable(garbage.bytes);
    CHECK(std::memcmp(garbage.bytes, garbageCopy.bytes, sizeof(garbage.bytes)) == 0 && vo.GetStats().notRigid == 1);

    Views fresh(game);
    std::this_thread::sleep_for(ViewOverride::kMaxAge + std::chrono::milliseconds(30));
    r.SubmitThroughVtable(fresh.bytes); // producer stopped: the pose expires, stock view
    CHECK(Same(g_log.seen.forward, game.forward) && vo.GetStats().stale == 1);

    void** const ours = r.vptr;
    FakeRenderer other;
    CHECK(vo.Remove(r.Object()) == ViewOverride::RemoveResult::Restored && r.vptr == originalVptr);
    CHECK(vo.Remove(r.Object()) == ViewOverride::RemoveResult::NotInstalled);
    // A call still in flight through the stale copy only forwards.
    const int before = g_log.calls;
    Views late(game);
    reinterpret_cast<RendererSubmitFn>(ours[kRendererSubmitSlot])(r.Object(), 0, 0, 1, late.bytes, nullptr, nullptr);
    CHECK(g_log.calls == before + 1 && Same(g_log.seen.forward, game.forward));

    // Replaced or freed objects are never written.
    CHECK(vo.Install(r.Object(), submit) == ViewOverride::InstallResult::Ok);
    CHECK(vo.Remove(other.Object()) == ViewOverride::RemoveResult::ObjectGone && r.vptr != originalVptr);
    r.vptr = originalVptr;
    CHECK(vo.Install(r.Object(), submit) == ViewOverride::InstallResult::Ok);
    void* foreignTable[1 + kRendererVtableSlots] = {};
    r.vptr = foreignTable + 1; // another hook took the object over
    CHECK(vo.Remove(r.Object()) == ViewOverride::RemoveResult::ForeignVtable && r.vptr == foreignTable + 1);
}

namespace {
struct Peek { // reads what the "game" saw during the call
    static void Submit(void*, float, float, std::uint32_t, std::uint8_t* views, void*, void*) {
        std::memcpy(&g_peekFov, views + kViewFovOffset, 4);
        std::memcpy(&g_peekHorizontal, views + kViewHorizontalFovOffset, 4);
        std::memcpy(&g_peekDefault, views + kViewDefaultFovOffset, 4);
        std::memcpy(&g_peekDirty, views + kViewDirtyOffset, 4);
    }
    static inline float g_peekFov = 0, g_peekHorizontal = 0, g_peekDefault = 0;
    static inline std::uint32_t g_peekDirty = 0;
};
} // namespace

TEST_CASE(ViewOverrideFovAndPoseRunsForTheWorldLane) {
    g_log = {};
    FakeRenderer r;
    const auto submit = reinterpret_cast<std::uintptr_t>(&FakeSubmit);
    const LinearTransform game = Yawed(0, {1, 2, 3});
    ViewOverride vo;
    CHECK(vo.Install(r.Object(), submit) == ViewOverride::InstallResult::Ok);
    Views v(game);
    const float gameFov = 0.96f, aspect = 16.0f / 9.0f, horizontal = 1.5f;
    std::memcpy(v.bytes + kViewFovOffset, &gameFov, 4);
    std::memcpy(v.bytes + kViewAspectOffset, &aspect, 4);
    std::memcpy(v.bytes + kViewHorizontalFovOffset, &horizontal, 4);
    const float defaultFov = 0.96f; // the game's baseline FOV (weapon scale reference)
    std::memcpy(v.bytes + kViewDefaultFovOffset, &defaultFov, 4);
    Views pristine = v;

    float seenFov = 0, seenHorizontal = 0;
    std::uint32_t seenDirty = 0;
    vo.SetLocal(LinearTransform{}, false, /*poseId=*/7, /*fovY=*/1.9f);
    CHECK(vo.Remove(r.Object()) == ViewOverride::RemoveResult::Restored);
    r.table[1 + kRendererSubmitSlot] = reinterpret_cast<void*>(&Peek::Submit);
    CHECK(vo.Install(r.Object(), reinterpret_cast<std::uintptr_t>(&Peek::Submit)) == ViewOverride::InstallResult::Ok);
    vo.SetLocal(LinearTransform{}, false, 7, 1.9f);
    r.SubmitThroughVtable(v.bytes);
    seenFov = Peek::g_peekFov;
    seenHorizontal = Peek::g_peekHorizontal;
    seenDirty = Peek::g_peekDirty;
    CHECK(Near(seenFov, 1.9f, 1e-6f));
    CHECK(Near(seenHorizontal, 2.0f * std::atan(std::tan(0.95f) * aspect), 1e-5f)); // as the game's setter computes it
    CHECK((seenDirty & kViewFovDirtyBits) == kViewFovDirtyBits);
    CHECK(Near(Peek::g_peekDefault, 1.9f, 1e-6f)); // weapon scale tan(fov/2)/tan(default/2) becomes 1
    CHECK(std::memcmp(v.bytes, pristine.bytes, sizeof(v.bytes)) == 0); // every byte back
    auto s = vo.GetStats();
    CHECK(s.fovApplied == 1 && s.poseId == 7 && s.poseRun == 1 && Near(s.gameFovY, gameFov, 1e-6f) &&
          Near(s.renderedFovY, 1.9f, 1e-6f));
    r.SubmitThroughVtable(v.bytes);
    CHECK(vo.GetStats().poseRun == 2);
    vo.SetLocal(LinearTransform{}, false, 8, 0); // next lease, game FOV kept
    r.SubmitThroughVtable(v.bytes);
    s = vo.GetStats();
    CHECK(s.poseId == 8 && s.poseRun == 1 && s.fovApplied == 2 && Near(Peek::g_peekFov, gameFov, 1e-6f) &&
          Near(s.renderedFovY, gameFov, 1e-6f));
    Views garbage(game); // not a camera: the run breaks, so no frame is labelled with pose 8
    const float two = 2.0f;
    std::memcpy(garbage.bytes, &two, 4);
    r.SubmitThroughVtable(garbage.bytes);
    CHECK(vo.GetStats().poseRun == 0);
    CHECK(vo.Remove(r.Object()) == ViewOverride::RemoveResult::Restored);
}

#include "../../src/games/bf4/Bf4EyeLoopOverride.h"

namespace {
// bf4.exe's two-eye loop as it behaves: reset the array, append one entry per eye with no
// bounds check, consume it before returning.
struct FakeEyeLoopLog {
    int calls = 0, eyes = 2;
    std::uint64_t seenEntries = 0;
    float a4 = 0, a5 = 0;
    std::uint32_t a6 = 0;
    void *a7 = nullptr, *a8 = nullptr;
    std::uint64_t a3 = 0, a9 = 0, a10 = 0;
};
FakeEyeLoopLog g_eye;
std::uintptr_t LoadAt(void* o, std::size_t off) { std::uintptr_t v; std::memcpy(&v, static_cast<std::uint8_t*>(o) + off, 8); return v; }
void StoreAt(void* o, std::size_t off, std::uintptr_t v) { std::memcpy(static_cast<std::uint8_t*>(o) + off, &v, 8); }
void FakeEyeLoop(void* self, std::uint64_t, std::uint64_t a3, float a4, float a5, std::uint32_t a6, void* a7, void* a8,
                 std::uint64_t a9, std::uint64_t a10) {
    ++g_eye.calls;
    g_eye.a3 = a3; g_eye.a4 = a4; g_eye.a5 = a5; g_eye.a6 = a6; g_eye.a7 = a7; g_eye.a8 = a8; g_eye.a9 = a9; g_eye.a10 = a10;
    StoreAt(self, kEyeArrayEnd, LoadAt(self, kEyeArrayBegin));
    for (int e = 0; e < g_eye.eyes; ++e) {
        const std::uintptr_t at = LoadAt(self, kEyeArrayEnd);
        std::memset(reinterpret_cast<void*>(at), 0xE0 + e, kEyeEntryBytes);
        StoreAt(self, kEyeArrayEnd, at + kEyeEntryBytes);
    }
    g_eye.seenEntries = (LoadAt(self, kEyeArrayEnd) - LoadAt(self, kEyeArrayBegin)) / kEyeEntryBytes;
}
void OtherEyeSlot(void*) {}
struct FakeEyeObject {
    void* table[1 + kEyeLoopVtableSlots];
    std::uint8_t bytes[0x4860] = {};
    FakeEyeObject() {
        table[0] = reinterpret_cast<void*>(0x5678);
        for (std::size_t i = 1; i <= kEyeLoopVtableSlots; ++i) table[i] = reinterpret_cast<void*>(&OtherEyeSlot);
        table[1 + kEyeLoopSlot] = reinterpret_cast<void*>(&FakeEyeLoop);
        void** vptr = table + 1;
        std::memcpy(bytes, &vptr, 8);
        const auto inl = reinterpret_cast<std::uintptr_t>(bytes) + kEyeArrayInline;
        StoreAt(bytes, kEyeArrayBegin, inl);
        StoreAt(bytes, kEyeArrayEnd, inl + kEyeEntryBytes);
        StoreAt(bytes, kEyeArrayCapacity, inl + kEyeEntryBytes);
        std::memset(bytes + kEyeArrayInline + kEyeEntryBytes, 0x5A, sizeof(bytes) - kEyeArrayInline - kEyeEntryBytes);
    }
    void Call(std::uint32_t views) {
        void** vt = nullptr;
        std::memcpy(&vt, bytes, 8);
        reinterpret_cast<EyeLoopFn>(vt[kEyeLoopSlot])(bytes, 0, 3, 0.25f, 0.5f, views, bytes + 8, bytes + 16, 99, 100);
    }
};
} // namespace

TEST_CASE(EyeLoopOverrideGivesTheSecondEyeRoomAndRestoresExactly) {
    if (sizeof(void*) != 8) { // bf4.exe is x64; the array layout has 8-byte pointers
        EyeLoopOverride eo;
        CHECK(eo.Install(&eo, 0) == EyeLoopOverride::InstallResult::NoObject);
        return;
    }
    g_eye = {};
    auto* obj = new FakeEyeObject;
    const auto slot = reinterpret_cast<std::uintptr_t>(&FakeEyeLoop);
    std::uint8_t before[0x30];
    std::memcpy(before, obj->bytes + kEyeArrayBegin, sizeof(before));
    EyeLoopOverride eo;
    CHECK(eo.Install(obj->bytes, slot + 1) == EyeLoopOverride::InstallResult::UnexpectedSlot);
    CHECK(eo.Install(obj->bytes, slot) == EyeLoopOverride::InstallResult::Ok);
    obj->Call(1); // stereo: two eyes for one view
    CHECK(g_eye.calls == 1 && g_eye.seenEntries == 2);
    // Arguments reach the game's function unchanged.
    CHECK(g_eye.a3 == 3 && g_eye.a4 == 0.25f && g_eye.a5 == 0.5f && g_eye.a6 == 1 && g_eye.a7 == obj->bytes + 8 &&
          g_eye.a8 == obj->bytes + 16 && g_eye.a9 == 99 && g_eye.a10 == 100);
    // Nothing after the one-entry inline storage was touched; the array pointers are back.
    bool guard = true;
    for (std::size_t i = kEyeArrayInline + kEyeEntryBytes; i < sizeof(obj->bytes); ++i) guard = guard && obj->bytes[i] == 0x5A;
    CHECK(guard);
    CHECK(std::memcmp(before, obj->bytes + kEyeArrayBegin, sizeof(before)) == 0);
    auto s = eo.GetStats();
    CHECK(s.calls == 1 && s.widened == 1 && s.restored == 1 && s.foreign == 0 && s.maxEntries == 2);
    // An array someone else re-pointed is left alone (the call goes through untouched).
    std::uint8_t elsewhere[kEyeEntryBytes * 2];
    const auto saved = LoadAt(obj->bytes, kEyeArrayBegin);
    StoreAt(obj->bytes, kEyeArrayBegin, reinterpret_cast<std::uintptr_t>(elsewhere));
    StoreAt(obj->bytes, kEyeArrayCapacity, reinterpret_cast<std::uintptr_t>(elsewhere) + sizeof(elsewhere));
    obj->Call(1);
    CHECK(eo.GetStats().foreign == 1 && LoadAt(obj->bytes, kEyeArrayBegin) == reinterpret_cast<std::uintptr_t>(elsewhere));
    StoreAt(obj->bytes, kEyeArrayBegin, saved);
    StoreAt(obj->bytes, kEyeArrayCapacity, saved + kEyeEntryBytes);
    CHECK(eo.Remove());
    void** vt = nullptr;
    std::memcpy(&vt, obj->bytes, 8);
    CHECK(vt == obj->table + 1);
    delete obj;
}
