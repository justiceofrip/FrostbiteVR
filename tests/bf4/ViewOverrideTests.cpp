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
