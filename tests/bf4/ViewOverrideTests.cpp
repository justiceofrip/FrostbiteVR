// Head-tracked main view (Bf4ViewOverride): the math, and the vtable-copy mechanism driven
// in-process through a fake renderer object laid out like bf4.exe's (locator + 23 slots,
// submit in slot 5, view block 0 at the start of the views buffer).
#include "TestHarness.h"

#include "../../src/games/bf4/Bf4ViewOverride.h"

#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

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
    std::vector<math::Vec3> copyTrans; // every call's +0x40 position
    std::vector<int> events;           // second view: 0 render, 10 wait, 20+eye hook
};
FakeSubmitLog g_log;
void FakeSubmit(void*, float, float, std::uint32_t count, std::uint8_t* views, void*, void*) {
    ++g_log.calls;
    g_log.events.push_back(0);
    if (!count || !views) return;
    g_log.seen = ReadViewTransform(views);
    g_log.seenCopy = ReadViewTransform(views + kViewCopyOffset);
    g_log.copyTrans.push_back(g_log.seenCopy.trans);
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
    void SubmitThroughVtable(std::uint8_t* views, void* sync = nullptr) { // what bf4.exe's +0x8288DE call does
        void** vt = vptr;
        reinterpret_cast<RendererSubmitFn>(vt[kRendererSubmitSlot])(Object(), 0.016f, 1.0f, 1, views, sync, nullptr);
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
    CHECK(s.copiesDiffer == 0.0f); // the game submitted the same camera twice

    // Research dump: the block exactly as the game submitted it, taken once.
    std::array<std::uint8_t, kViewBlockBytes> dump{};
    std::uint32_t dumpCount = 0;
    CHECK(!vo.TakeBlockDump(dump, dumpCount));
    vo.RequestBlockDump();
    vo.SetLocal(Yawed(30, {}), false);
    r.SubmitThroughVtable(v.bytes);
    CHECK(vo.TakeBlockDump(dump, dumpCount) && dumpCount == 1);
    CHECK(std::memcmp(dump.data(), pristine.bytes, kViewBlockBytes) == 0); // not the override's pose
    CHECK(!vo.TakeBlockDump(dump, dumpCount));

    vo.SetCopyMask(2); // research: only the +0x40 copy gets the head pose
    vo.SetLocal(Yawed(30, {}), false);
    r.SubmitThroughVtable(v.bytes);
    CHECK(Same(g_log.seen.forward, game.forward) && Same(g_log.seenCopy.forward, Yawed(30, {}).forward));
    CHECK(std::memcmp(v.bytes, pristine.bytes, sizeof(v.bytes)) == 0);
    vo.SetCopyMask(3);

    // Gun follows aim: the camera turned to the game's aim at the head's position, latched per
    // pose; +0x00 keeps the game's own value; exact restore as always.
    ViewOverride::GunAimSettings gun;
    gun.enabled = true;
    gun.eye[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
    gun.eye[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
    gun.aspect = 16.0f / 9.0f;
    vo.SetGunAim(gun);
    LinearTransform headLocal = Yawed(8, {}); // a small turn: the render goes all the way to the aim
    headLocal.trans = {0.1f, 0, 0};
    vo.SetLocal(headLocal, false, /*poseId=*/5, /*fovY=*/1.6f);
    r.SubmitThroughVtable(v.bytes);
    CHECK(Same(g_log.seenCopy.forward, game.forward));           // rendered along the aim
    CHECK(Same(g_log.seenCopy.trans, {1.1f, 2, 3}));             // from the head's position
    CHECK(Same(g_log.seen.forward, game.forward) && Same(g_log.seen.trans, game.trans)); // +0x00 untouched
    CHECK(std::memcmp(v.bytes, pristine.bytes, sizeof(v.bytes)) == 0);
    auto gs = vo.GetStats();
    CHECK(gs.frameRotation.enabled && Near(gs.gunFraction, 1.0f, 1e-5f) && gs.poseId == 5);
    const FrameRotation latched = gs.frameRotation;
    r.SubmitThroughVtable(v.bytes); // same pose: the same turn relative to the head
    gs = vo.GetStats();
    bool sameTurn = true;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) sameTurn = sameTurn && gs.frameRotation.m[i][k] == latched.m[i][k];
    CHECK(sameTurn && gs.poseRun == 2);
    gun.enabled = false;
    vo.SetGunAim(gun);
    vo.SetLocal(Yawed(30, {}), false);

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

#include "../../src/games/bf4/Bf4WorldRenderOverride.h"

namespace {
// bf4.exe's world renderer slot 2 as seen from outside: renders the args' views.
struct FakeWorldLog {
    int calls = 0;
    void *args = nullptr, *a3 = nullptr, *a4 = nullptr;
    std::vector<int> order; // hook events (-1-index before, +1+index after) and renders (0)
};
FakeWorldLog g_world;
void FakeWorldRender(void*, void* args, void* a3, void* a4) {
    ++g_world.calls;
    g_world.args = args;
    g_world.a3 = a3;
    g_world.a4 = a4;
    g_world.order.push_back(0);
}
int g_worldDraws = 0;
void FakeWorldDraw(void*, void* context, void*, void*) {
    ++g_worldDraws;
    g_world.order.push_back(context ? 50 : 51);
}
struct FakeWorldObject {
    void* table[1 + kWorldRendererVtableSlots];
    std::uint8_t bytes[0x200] = {};
    FakeWorldObject() {
        table[0] = nullptr; // no RTTI, as in bf4.exe
        for (std::size_t i = 1; i <= kWorldRendererVtableSlots; ++i) table[i] = reinterpret_cast<void*>(&OtherEyeSlot);
        table[1 + kWorldRenderSlot] = reinterpret_cast<void*>(&FakeWorldRender);
        table[1 + kWorldDrawSlot] = reinterpret_cast<void*>(&FakeWorldDraw);
        void** vptr = table + 1;
        std::memcpy(bytes, &vptr, 8);
    }
    void Draw() {
        void** vt = nullptr;
        std::memcpy(&vt, bytes, 8);
        reinterpret_cast<WorldDrawFn>(vt[kWorldDrawSlot])(bytes, bytes + 32, nullptr, nullptr);
    }
    void Call(std::uint32_t views) {
        std::uint8_t args[0x60] = {};
        std::memcpy(args + kWorldRenderArgsViewCount, &views, 4);
        void** vt = nullptr;
        std::memcpy(&vt, bytes, 8);
        reinterpret_cast<WorldRenderFn>(vt[kWorldRenderSlot])(bytes, args, bytes + 8, bytes + 16);
    }
};
std::vector<void*> g_waited;
void FakeJobWait(void* handle) {
    g_waited.push_back(handle);
    g_world.order.push_back(9); // a wait between runs
}
struct TwiceHooks : WorldRenderOverride::Hooks {
    int runs = 2, draws = 1;
    int Runs(std::uint32_t) override { return runs; }
    int Draws() override { return draws; }
    void BeforeDraw(int index, int) override { g_world.order.push_back(-10 - index); }
    void AfterDraw(int index, int) override { g_world.order.push_back(10 + index); }
    void BeforeRun(int index, int) override { g_world.order.push_back(-1 - index); }
    void AfterRun(int index, int) override { g_world.order.push_back(1 + index); }
};
} // namespace

TEST_CASE(WorldRenderOverrideRunsTheRenderOncePerEyeAndRestoresExactly) {
    if (sizeof(void*) != 8) {
        WorldRenderOverride wo;
        TwiceHooks hooks;
        CHECK(wo.Install(&wo, 0, &hooks) == WorldRenderOverride::InstallResult::NoObject);
        return;
    }
    g_world = {};
    auto* obj = new FakeWorldObject;
    const auto slot = reinterpret_cast<std::uintptr_t>(&FakeWorldRender);
    TwiceHooks hooks;
    WorldRenderOverride wo;
    CHECK(wo.Install(obj->bytes, slot + 1, &hooks) == WorldRenderOverride::InstallResult::UnexpectedSlot);
    CHECK(wo.Install(obj->bytes, slot, &hooks) == WorldRenderOverride::InstallResult::Ok);
    obj->Call(1); // BF4's one view: rendered once per eye, the hooks around each run
    CHECK(g_world.calls == 2 && g_world.a3 == obj->bytes + 8 && g_world.a4 == obj->bytes + 16);
    CHECK((g_world.order == std::vector<int>{-1, 0, 1, -2, 0, 2}));
    // With the game's job wait: the first run's two job handles are waited on before the second.
    g_world.order.clear();
    g_waited.clear();
    wo.SetJobWait(&FakeJobWait);
    obj->Call(1);
    CHECK((g_world.order == std::vector<int>{-1, 0, 1, 9, 9, -2, 0, 2}));
    CHECK(g_waited.size() == 2 && g_waited[0] == obj->bytes + 8 && g_waited[1] == obj->bytes + 16);
    wo.SetJobWait(nullptr);
    g_world.calls = 2;
    // Two views (not BF4's PC shape) and runs = 1 render once.
    g_world.order.clear();
    obj->Call(2);
    hooks.runs = 1;
    obj->Call(1);
    CHECK(g_world.calls == 4 && (g_world.order == std::vector<int>{-1, 0, 1, -1, 0, 1})); // single runs: hooks told, nothing doubled
    const auto s = wo.GetStats();
    CHECK(s.calls == 4 && s.doubled == 2 && s.foreign == 0 && s.waits == 1);
    // Every other slot is BF4's; Remove puts the original table back exactly.
    void** vt = nullptr;
    std::memcpy(&vt, obj->bytes, 8);
    CHECK(vt != obj->table + 1 && vt[-1] == nullptr && vt[0] == obj->table[1] && vt[kWorldRendererVtableSlots - 1] == obj->table[kWorldRendererVtableSlots]);
    CHECK(wo.Remove());
    std::memcpy(&vt, obj->bytes, 8);
    CHECK(vt == obj->table + 1);
    delete obj;
}

namespace {
void* g_waitedSync = nullptr;
void FakeSyncWait(void* sync) {
    g_waitedSync = sync;
    g_log.events.push_back(10);
}
void FakeEyeHook(void* context, int eye) {
    ++*static_cast<int*>(context);
    g_log.events.push_back(20 + eye);
}
} // namespace

TEST_CASE(ViewOverrideSecondViewRendersEachEyeAfterTheFirstEyesJobs) {
    g_log = {};
    FakeRenderer r;
    ViewOverride vo;
    CHECK(vo.Install(r.Object(), reinterpret_cast<std::uintptr_t>(&FakeSubmit)) == ViewOverride::InstallResult::Ok);
    const LinearTransform game = Yawed(0, {1, 2, 3});
    Views v(game), pristine(game);
    int hookCalls = 0, syncObject = 0;
    vo.SetLocal(Yawed(0, {}), false);
    vo.SetSecondView(2, 0.032f, &FakeSyncWait, &FakeEyeHook, &hookCalls);
    r.SubmitThroughVtable(v.bytes, &syncObject);
    // Two renders, the game's job group waited on between them; the left eye first.
    CHECK(g_log.calls == 2 && g_waitedSync == &syncObject && hookCalls == 3);
    CHECK((g_log.events == std::vector<int>{20, 0, 10, 21, 0, 19}));
    CHECK(g_log.copyTrans.size() == 2 && Same(g_log.copyTrans[0], {1 - 0.032f, 2, 3}) && Same(g_log.copyTrans[1], {1 + 0.032f, 2, 3}));
    CHECK(std::memcmp(v.bytes, pristine.bytes, sizeof(v.bytes)) == 0); // exact restore after both
    CHECK(vo.GetStats().restored == 1 && vo.SecondViewRenders() == 2);
    // Mode 1: the same camera twice. Without a sync to wait on: one render, as the game does.
    g_log = {};
    vo.SetSecondView(1, 0.032f, &FakeSyncWait, nullptr, nullptr);
    r.SubmitThroughVtable(v.bytes, &syncObject);
    CHECK(g_log.calls == 2 && g_log.copyTrans.size() == 2 && Same(g_log.copyTrans[0], g_log.copyTrans[1]));
    g_log = {};
    r.SubmitThroughVtable(v.bytes, nullptr);
    CHECK(g_log.calls == 1);
    vo.SetSecondView(0, 0, nullptr, nullptr, nullptr);
    g_log = {};
    r.SubmitThroughVtable(v.bytes, &syncObject);
    CHECK(g_log.calls == 1 && std::memcmp(v.bytes, pristine.bytes, sizeof(v.bytes)) == 0);
    CHECK(vo.Remove(r.Object()) == ViewOverride::RemoveResult::Restored);
}

TEST_CASE(WorldRenderOverrideDrawsTheWorldOncePerEye) {
    if (sizeof(void*) != 8) return;
    g_world = {};
    g_worldDraws = 0;
    auto* obj = new FakeWorldObject;
    TwiceHooks hooks;
    hooks.runs = 1;
    WorldRenderOverride wo;
    const auto build = reinterpret_cast<std::uintptr_t>(&FakeWorldRender), draw = reinterpret_cast<std::uintptr_t>(&FakeWorldDraw);
    CHECK(wo.Install(obj->bytes, build, &hooks, draw + 1) == WorldRenderOverride::InstallResult::UnexpectedSlot);
    CHECK(wo.Install(obj->bytes, build, &hooks, draw) == WorldRenderOverride::InstallResult::Ok);
    obj->Draw(); // one draw as the game does
    CHECK(g_worldDraws == 1 && (g_world.order == std::vector<int>{-10, 50, 10}));
    g_world.order.clear();
    hooks.draws = 2; // once per eye, the hooks around each, the context passed on
    obj->Draw();
    CHECK(g_worldDraws == 3 && (g_world.order == std::vector<int>{-10, 50, 10, -11, 50, 11}));
    const auto s = wo.GetStats();
    CHECK(s.drawCalls == 2 && s.drawsDoubled == 1);
    CHECK(wo.Remove());
    delete obj;
}

TEST_CASE(WorldRenderOverrideForgetsAReplacedRendererWithoutWritingIt) {
    // A level change can replace BF4's world renderer: the old object is dropped without a write
    // (its memory may be another object's now), a draw through the old table only forwards, and
    // the new renderer can be taken over.
    if (sizeof(void*) != 8) return;
    g_world = {};
    g_worldDraws = 0;
    auto* oldObj = new FakeWorldObject;
    TwiceHooks hooks;
    hooks.runs = 1;
    hooks.draws = 2;
    WorldRenderOverride wo;
    const auto build = reinterpret_cast<std::uintptr_t>(&FakeWorldRender), draw = reinterpret_cast<std::uintptr_t>(&FakeWorldDraw);
    CHECK(wo.Install(oldObj->bytes, build, &hooks, draw) == WorldRenderOverride::InstallResult::Ok);
    void* ours = nullptr;
    std::memcpy(&ours, oldObj->bytes, 8);
    CHECK(ours == wo.OurVtable() && ours != oldObj->table + 1);
    std::uint8_t before[sizeof(oldObj->bytes)];
    std::memcpy(before, oldObj->bytes, sizeof(before));
    wo.Forget();
    CHECK(!wo.Installed() && wo.OurVtable() == nullptr);
    CHECK(std::memcmp(before, oldObj->bytes, sizeof(before)) == 0); // nothing written
    oldObj->Draw(); // a draw still in flight through the old table: forwarded once, no hooks
    CHECK(g_worldDraws == 1 && (g_world.order == std::vector<int>{50}));
    auto* newObj = new FakeWorldObject;
    g_world.order.clear();
    CHECK(wo.Install(newObj->bytes, build, &hooks, draw) == WorldRenderOverride::InstallResult::Ok);
    newObj->Draw();
    CHECK(g_worldDraws == 3 && (g_world.order == std::vector<int>{-10, 50, 10, -11, 50, 11}));
    CHECK(wo.Remove());
    delete newObj;
    delete oldObj;
}
