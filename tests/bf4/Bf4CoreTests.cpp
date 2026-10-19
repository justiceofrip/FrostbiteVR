// Emulated, platform-neutral regression suite for the BF4 adapter core.
// One executable on purpose (keeps the build tree small). No game, GPU, headset or
// Windows APIs. Each case covers a distinct behaviour; no duplicate permutations.
#include "fvr/game/Capability.h"
#include "fvr/interaction/ReloadPolicy.h"
#include "fvr/ui/ModMenuPanel.h"
#include "fvr/ui/ModMenu.h"
#include "../../src/engines/frostbite/Fb3CameraMath.h"
#include "../../src/engines/frostbite/PatternScan.h"
#include "../../src/games/bf4/Bf4Bindings.h"
#include "../../src/games/bf4/Bf4Profile.h"
#include "../../src/games/bf4/Bf4ResearchControl.h"
#include "../../src/games/bf4/Bf4CameraProbe.h"
#include "../../src/games/bf4/Bf4WorldView.h"
#include "../../src/games/bf4/Bf4EngineStereo.h"
#include "../../src/games/bf4/Bf4PointerInjector.h"
#include "../../src/games/bf4/Bf4AdapterCore.h"
#include "../../src/games/bf4/Bf4ViewOverride.h"

#include <map>
#include "../../src/games/bf4/Bf4SessionGuard.h"
#include "../../src/games/bf4/Bf4SessionInfo.h"
#include "../../src/games/bf4/Bf4SessionSignals.h"

#include "TestHarness.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

using fvrtest::Near;

TEST_CASE(PatternScanning) {
    using namespace fvr;
    using namespace fvr::frostbite;
    CHECK(!ParsePattern("").has_value());
    CHECK(!ParsePattern("?? ??").has_value());
    CHECK(!ParsePattern("4G").has_value());
    const auto p = ParsePattern("48 8B 05 ? ? ? ? 48 85 C0");
    CHECK(p && p->size() == 10);

    // mov rax, [rip+0x100]; test rax, rax  at offset 4 of a code blob mapped at 0x140001000
    std::vector<std::uint8_t> code = {0x90, 0x90, 0x90, 0x90,
                                      0x48, 0x8B, 0x05, 0x00, 0x01, 0x00, 0x00, 0x48, 0x85, 0xC0,
                                      0xCC, 0xCC};
    auto r = FindUnique(code, *p);
    CHECK(r.status == ScanStatus::Ok && r.offset == 4);
    const auto target = ResolveRipRelative(code, 0x140001000ull, r.offset, 3, 7);
    CHECK(target && *target == 0x140001000ull + 4 + 7 + 0x100);

    auto twice = code;
    twice.insert(twice.end(), code.begin(), code.end());
    CHECK(FindUnique(twice, *p).status == ScanStatus::Ambiguous);
    CHECK(FindUnique(std::vector<std::uint8_t>(64, 0x90), *p).status == ScanStatus::NotFound);
}

TEST_CASE(BindingsGateCapabilities) {
    using namespace fvr;
    using namespace fvr::bf4;
    std::vector<std::uint8_t> code(256, 0xCC);

    // Default table over foreign code: the camera bindings are enrolled for build 179547 but
    // absent here, the rest are unenrolled -> only the swapchain-based M0 features qualify.
    BindingTable table(DefaultBindingSpecs());
    table.Resolve(code, 0x140001000ull);
    for (const auto& b : table.All())
        CHECK(b.status == (b.name == names::kGameRenderer || b.name == names::kRenderViewUpdate
                               ? frostbite::ScanStatus::NotFound
                               : frostbite::ScanStatus::Unenrolled));
    game::CapabilityRegistry caps;
    ApplyBindingsToCapabilities(table, caps, /*presentHookOk=*/true);
    CHECK(caps.Get(game::Feature::TheaterView).state == game::FeatureState::Candidate);
    CHECK(caps.Get(game::Feature::HeadTrackedCamera).state == game::FeatureState::Disabled);
    CHECK(caps.Get(game::Feature::HeadTrackedCamera).reason.find("GameRenderer.Instance not-found") != std::string::npos);
    CHECK(caps.Get(game::Feature::ManualReload).state == game::FeatureState::Disabled);

    // An enrolled spec with a failing validator must not become usable.
    code[10] = 0x48; code[11] = 0x8B; code[12] = 0x0D; code[13] = 0x10; code[14] = 0; code[15] = 0; code[16] = 0;
    const BindingSpec enrolled[] = {{names::kInputConceptCache, "48 8B 0D ?? ?? ?? ??", BindingKind::RipRelativeGlobal, 0, 3, 7, "test"}};
    BindingTable t2(enrolled);
    t2.Resolve(code, 0x140001000ull);
    CHECK(t2.IsOk(names::kInputConceptCache));
    t2.Resolve(code, 0x140001000ull, [](std::string_view) -> BindingValidator { return [](const ResolvedBinding&) { return false; }; });
    CHECK(t2.Find(names::kInputConceptCache)->status == frostbite::ScanStatus::ValidationFailed);
}

TEST_CASE(ReloadModeAndRouter) {
    using namespace fvr;
    using namespace fvr::interaction;
    game::CapabilityRegistry caps;
    auto res = ResolveReloadMode(ReloadMode::Manual, caps, true);
    CHECK(res.requested == ReloadMode::Manual && res.effective == ReloadMode::ButtonPress);
    CHECK(res.note.find("Manual reload isn't available yet") != std::string::npos);
    CHECK(ParseReloadMode("manual") == ReloadMode::Manual && !ParseReloadMode("nope"));

    ButtonReloadRouter router(0.25);
    ReloadFrameInput in;
    in.timeSeconds = 1.0; in.reloadButtonDown = false;
    CHECK(router.Update(in) == ReloadCommand::None);
    in.timeSeconds = 1.01; in.reloadButtonDown = true;
    CHECK(router.Update(in) == ReloadCommand::RequestNativeReload);
    in.timeSeconds = 1.02; // held: no repeat
    CHECK(router.Update(in) == ReloadCommand::None);
    in.timeSeconds = 1.05; in.reloadButtonDown = false; router.Update(in);
    in.timeSeconds = 1.10; in.reloadButtonDown = true; // chatter inside debounce window
    CHECK(router.Update(in) == ReloadCommand::None);
    // Press begun in a menu must be released before it can reload in gameplay.
    in.timeSeconds = 2.0; in.inMenu = true; router.Update(in);
    in.timeSeconds = 2.1; in.inMenu = false;
    CHECK(router.Update(in) == ReloadCommand::None);
    in.timeSeconds = 2.2; in.reloadButtonDown = false; router.Update(in);
    in.timeSeconds = 2.3; in.reloadButtonDown = true;
    CHECK(router.Update(in) == ReloadCommand::RequestNativeReload);
}

TEST_CASE(ModMenuPanelClicksNeedAFreshPress) {
    using namespace fvr;
    using namespace fvr::ui;
    ModMenuModel menu("FrostbiteVR");
    menu.Add({"reload_mode", "Reload", MenuItemKind::Choice, {"Button press", "Manual"}, 0, true, ""});
    menu.Add({"snap_turn", "Snap turn", MenuItemKind::Toggle, {}, 1, true, ""});
    menu.Add({"about", "Campaign only", MenuItemKind::Label, {}, 0, false, ""});
    int changes = 0;
    menu.OnChange([&](const MenuItem&) { ++changes; });
    VrPanel panel(1280, 720);
    LayoutModMenu(menu, panel);
    CHECK(panel.Widgets().size() == 4 && panel.Find(1) && panel.Find(1)->text == L"Reload: Button press");
    const Widget* row0 = panel.Find(1);
    const float u = (row0->rect.x + row0->rect.w * 0.5f) / 1280.0f, v = (row0->rect.y + row0->rect.h * 0.5f) / 720.0f;
    // Trigger already held when the pointer arrives (e.g. the menu just opened): no click.
    CHECK(!PointModMenu(menu, panel, {true, true, u, v}));
    CHECK(!PointModMenu(menu, panel, {true, false, u, v}) && changes == 0);
    // Fresh press + release on the same row: one activation.
    CHECK(!PointModMenu(menu, panel, {true, true, u, v}));
    CHECK(PointModMenu(menu, panel, {true, false, u, v}) && menu.Find("reload_mode")->value == 1);
    LayoutModMenu(menu, panel);
    CHECK(panel.Find(1)->text == L"Reload: Manual" && changes == 1);
    // Press on one row, release on another: nothing.
    const Widget* row1 = panel.Find(2);
    const float v1 = (row1->rect.y + row1->rect.h * 0.5f) / 720.0f;
    PointModMenu(menu, panel, {true, true, u, v});
    CHECK(!PointModMenu(menu, panel, {true, false, u, v1}) && changes == 1);
    // Labels never fire.
    const Widget* label = panel.Find(3);
    const float v2 = (label->rect.y + label->rect.h * 0.5f) / 720.0f;
    PointModMenu(menu, panel, {true, true, u, v2});
    CHECK(!PointModMenu(menu, panel, {true, false, u, v2}) && changes == 1);
}
TEST_CASE(SessionGuardFailsClosed) {
    using namespace fvr;
    using namespace fvr::bf4;
    SessionGuard g;
    CHECK(g.Mode() == SessionMode::Unknown && g.PresentationAllowed() && !g.GameplayAllowed());
    g.Observe({true, "", false, false, false});
    CHECK(g.Mode() == SessionMode::FrontEnd && !g.GameplayAllowed());
    g.Observe({true, "Levels/FrontEnd/FrontEnd", true, false, false}); // the menus' own level
    CHECK(g.Mode() == SessionMode::FrontEnd && !g.Locked());
    g.Observe({true, "Levels/SP_Prologue/SP_Prologue", false, false, false});
    CHECK(g.Mode() == SessionMode::Unknown && !g.GameplayAllowed()); // server signal unbound
    g.Observe({true, "Levels/SP_Prologue/SP_Prologue", true, false, false});
    CHECK(g.GameplayAllowed());
    g.Observe({true, "Levels/MP_Siege/MP_Siege", true, false, false});
    CHECK(g.Locked() && !g.PresentationAllowed());
    g.Observe({true, "Levels/SP_Prologue/SP_Prologue", true, false, false});
    CHECK(g.Locked()); // sticky

    SessionGuard pb;
    pb.Observe({false, "", false, false, true});
    CHECK(pb.Locked() && pb.Reason().find("PunkBuster") != std::string::npos);
}

TEST_CASE(SessionSignalsFromTheLiveReadings) {
    using namespace fvr::bf4;
    // The sequence the first live sessions logged: loading, menus with a noisy flag, campaign.
    SessionSignalFilter f;
    SessionGuard g;
    auto step = [&](bool readable, const char* level, int mp, std::uint64_t t) { g.Observe(f.Update(readable, level, mp, t)); };
    step(false, "", -1, 0);
    CHECK(g.Mode() == SessionMode::Unknown && !g.Locked());
    step(true, "", 1, 100); // loading: flag reads 1, never a lock
    step(true, "Levels/FrontEnd/FrontEnd", 1, 200);
    step(true, "Levels/FrontEnd/FrontEnd", 1, 5000); // menus: a long 1 is still noise
    CHECK(g.Mode() == SessionMode::FrontEnd && !g.Locked());
    step(true, "Levels/SP/SP_Prologue/SP_Prologue", -1, 6000); // in level, flag unreadable
    CHECK(g.Mode() == SessionMode::Unknown && !g.GameplayAllowed());
    step(true, "Levels/SP/SP_Prologue/SP_Prologue", 0, 6100);
    CHECK(g.GameplayAllowed());
    step(true, "Levels/SP/SP_Prologue/SP_Prologue", -1, 6200); // unreadable keeps the level's 0
    CHECK(g.GameplayAllowed());
    step(true, "Levels/SP/SP_Prologue/SP_Prologue", 1, 6300); // a short blip of 1 is ignored
    step(true, "Levels/SP/SP_Prologue/SP_Prologue", 0, 6400);
    CHECK(g.GameplayAllowed() && !g.Locked());
    step(true, "Levels/FrontEnd/FrontEnd", 1, 7000); // back to the menus
    CHECK(g.Mode() == SessionMode::FrontEnd && !g.GameplayAllowed());
    // A multiplayer map locks by its name at once; a held 1 in a level locks after kConfirmMs.
    SessionSignalFilter mpf;
    SessionGuard mpg;
    mpg.Observe(mpf.Update(true, "Levels/MP/MP_Siege/MP_Siege", 0, 0));
    CHECK(mpg.Locked());
    SessionSignalFilter held;
    SessionGuard hg;
    hg.Observe(held.Update(true, "Levels/SP/SP_Prologue/SP_Prologue", 1, 1000));
    CHECK(!hg.Locked());
    hg.Observe(held.Update(true, "Levels/SP/SP_Prologue/SP_Prologue", 1, 1000 + SessionSignalFilter::kConfirmMs));
    CHECK(hg.Locked());
}

TEST_CASE(CameraConventions) {
    using namespace fvr;
    using namespace fvr::frostbite;
    math::Pose identity;
    auto cam = XrPoseToFrostbite(identity);
    CHECK(Near(cam.forward.z, 1.0f) && Near(cam.right.x, 1.0f) && Near(cam.up.y, 1.0f));

    // Turning the head 90 degrees left (CCW about +Y in XR) must look down FB -X.
    math::Pose left;
    left.orientation = math::FromAxisAngle({0, 1, 0}, 3.14159265f * 0.5f);
    left.position = {0.1f, 1.7f, -0.2f};
    cam = XrPoseToFrostbite(left);
    CHECK(Near(cam.forward.x, -1.0f) && Near(cam.forward.z, 0.0f));
    CHECK(Near(cam.trans.z, 0.2f) && Near(cam.trans.y, 1.7f));
    // Basis stays consistent after the mirror: right x up == forward (component form).
    const auto f = math::Cross(cam.right, cam.up);
    CHECK(Near(f.x, cam.forward.x) && Near(f.z, cam.forward.z));

    // View matrix takes the camera position to the origin and forward to +Z.
    const auto view = ViewMatrixFromCamera(cam);
    const auto o = TransformPoint(view, cam.trans);
    CHECK(Near(o.x, 0) && Near(o.y, 0) && Near(o.z, 0));
    const auto ahead = TransformPoint(view, cam.trans + cam.forward * 3.0f);
    CHECK(Near(ahead.z, 3.0f));

    const FovAngles fov{-0.9f, 0.7f, 0.8f, -0.85f}; // asymmetric, like a real HMD eye
    const auto proj = ProjectionOffAxisLh(fov, 0.05f, 1000.0f, false);
    const auto nearP = TransformPoint(proj, {0, 0, 0.05f});
    const auto farP = TransformPoint(proj, {0, 0, 1000.0f});
    CHECK(Near(nearP.z, 0.0f) && Near(farP.z, 1.0f, 1e-3f));
    // Right frustum edge at depth 1 maps to NDC x = +1.
    const auto edge = TransformPoint(proj, {std::tan(0.7f), 0, 1.0f});
    CHECK(Near(edge.x, 1.0f, 1e-3f));
    const auto rev = ProjectionOffAxisLh(fov, 0.05f, 1000.0f, true);
    CHECK(Near(TransformPoint(rev, {0, 0, 0.05f}).z, 1.0f, 1e-3f));
}

TEST_CASE(ExecutableIdentity) {
    using namespace fvr;
    using namespace fvr::bf4;
    std::vector<std::uint8_t> h(512, 0);
    h[0] = 'M'; h[1] = 'Z';
    h[0x3C] = 0x80;
    h[0x80] = 'P'; h[0x81] = 'E';
    auto setMachine = [&](std::uint16_t m, std::uint16_t magic) {
        std::memcpy(&h[0x84], &m, 2);
        std::memcpy(&h[0x84 + 20], &magic, 2);
    };
    setMachine(0x8664, 0x20B);
    CHECK(IdentifyBuild(h, "abc").verdict == BuildVerdict::UnknownX64);
    setMachine(0x014C, 0x10B);
    CHECK(IdentifyBuild(h, "abc").verdict == BuildVerdict::WrongArch);
    CHECK(IdentifyBuild(std::vector<std::uint8_t>(64, 0), "").verdict == BuildVerdict::NotPe);
}

TEST_CASE(SessionInfoParsing) {
    using namespace fvr;
    using namespace fvr::bf4;
    const char text[] = "# FrostbiteVR session\nprotocol=1\nadapter=G:\\FrostbiteVR\\versions\\0.1.0\\bin\\FrostbiteVR.Bf4.dll\r\n"
                        "reload_mode=manual\ndeveloper=1\nlauncher_pid=4242\nfuture_key=ignored\n\0\0garbage";
    std::string err;
    const auto info = ParseSessionInfo(std::string_view(text, sizeof(text) - 1), &err);
    CHECK(info.has_value());
    if (info) {
        CHECK(info->reloadMode == interaction::ReloadMode::Manual);
        CHECK(info->developerFeatures && info->launcherPid == 4242);
        CHECK(info->adapterPath.ends_with("FrostbiteVR.Bf4.dll"));
    }
    CHECK(!ParseSessionInfo("protocol=2\nadapter=x\n", &err) && err == "protocol mismatch");
}

TEST_CASE(ResearchCommandsRunOnceAndRejectUnsafeInput) {
    using namespace fvr::bf4;
    using fvrtest::Near;
    const char* text = "# research\n1 snap menu_01\n2 click 0.25 0.5\n3 key enter\n4 click 1.5 0.2\n5 snap ../x\n"
                       "6 dumpimage\n7 key launchmissiles\n";
    auto all = ParseResearchCommands(text, 0);
    CHECK(all.size() == 7);
    CHECK(all[0].kind == ResearchCommand::Kind::Snap && all[0].arg == "menu_01");
    CHECK(all[1].kind == ResearchCommand::Kind::Click && Near(all[1].u, 0.25f) && Near(all[1].v, 0.5f));
    CHECK(all[2].kind == ResearchCommand::Kind::Key && ResearchKeyCode("enter") == 0x0D);
    CHECK(all[3].kind == ResearchCommand::Kind::Unknown);   // out-of-range coordinate
    CHECK(all[4].kind == ResearchCommand::Kind::Unknown);   // a snap name never becomes a path
    CHECK(all[5].kind == ResearchCommand::Kind::DumpImage && all[6].kind == ResearchCommand::Kind::Unknown);
    CHECK(ParseResearchCommands(text, 5).size() == 2);      // each id runs once
    CHECK(ResearchKeyCode("f5") == 0x74 && ResearchKeyCode("q") == 'Q' && ResearchKeyCode("f13") == 0);
    // A click hovers first, releases before the laser lets go of the cursor.
    const auto seq = PointerSequence(0.5f, 0.5f, 1920, 1080, true);
    CHECK(seq.size() == 4 && seq[0].event.type == PointerEvent::Type::Move && seq[0].event.x == 960 && seq[0].event.y == 540);
    CHECK(seq[1].event.type == PointerEvent::Type::ButtonDown && seq[1].frameDelay > 0);
    CHECK(seq[2].event.type == PointerEvent::Type::ButtonUp && seq[3].event.type == PointerEvent::Type::Deactivate);
}
TEST_CASE(CameraProbeRulesAcceptOnlyRigidCameras) {
    using namespace fvr::bf4;
    Matrix4f identity;
    identity.m = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 12.5f, 1.7f, -40.0f, 1};
    CHECK(IsRigidTransform(identity));
    Matrix4f yawed = identity; // 30 degrees about Y
    yawed.m[0] = 0.8660254f; yawed.m[2] = -0.5f; yawed.m[8] = 0.5f; yawed.m[10] = 0.8660254f;
    CHECK(IsRigidTransform(yawed));
    Matrix4f scaled = identity;
    scaled.m[0] = 2.0f; // not a camera transform
    CHECK(!IsRigidTransform(scaled));
    Matrix4f padded = yawed; // LinearTransform padding lanes carry no meaning
    padded.m[3] = 0.25f; padded.m[15] = 0.0f;
    CHECK(IsRigidTransform(padded));
    Matrix4f projection; // 16:9 perspective: scaled rows, not a camera transform
    projection.m = {0.79f, 0, 0, 0, 0, 1.40f, 0, 0, 0, 0, 1.0001f, 1, 0, 0, -0.1f, 0};
    CHECK(!IsRigidTransform(projection));
    ViewSample s;
    s.fovY = 0.79f; s.nearPlane = 0.06f; s.farPlane = 20000.0f; s.aspect = 1.7777f; // the live reading
    CHECK(PlausibleProjection(s));
    s.farPlane = 0.05f;
    CHECK(!PlausibleProjection(s));
    const std::uint8_t getter70[] = {0x48, 0x8B, 0x41, 0x70, 0xC3}, notGetter[] = {0x48, 0x8B, 0x41, 0x70, 0x90};
    CHECK(FieldGetterOffset(getter70) == 0x70 && FieldGetterOffset(notGetter) == -1);
}

TEST_CASE(WorldLaneCropsDecisionsAndFallback) {
    using namespace fvr::bf4;
    // An eye whose FOV equals the render's covers the whole image; a narrower, asymmetric
    // eye covers the matching part; a wider eye is partly outside (filled black).
    const float tanY = 1.2f, tanX = tanY * 16.0f / 9.0f;
    const auto full = EyeCropRect({-tanX, tanX, tanY, -tanY}, tanX, tanY);
    CHECK(Near(full.u0, 0) && Near(full.u1, 1) && Near(full.v0, 0) && Near(full.v1, 1) && full.inside);
    const auto left = EyeCropRect({-1.0f, 0.8f, 1.0f, -1.2f}, tanX, tanY);
    CHECK(Near(left.u0, 0.5f - 1.0f / (2 * tanX)) && Near(left.u1, 0.5f + 0.8f / (2 * tanX)));
    CHECK(Near(left.v0, 0.5f - 1.0f / 2.4f) && Near(left.v1, 1.0f) && left.inside);
    CHECK(!EyeCropRect({-1.0f, 1.0f, 1.5f, -1.5f}, tanX, tanY).inside);
    const auto half = EyeCropRect({-tanX, tanX, tanY, -tanY}, tanX, tanY, 0.5f, 1.0f); // right half of a side-by-side frame
    CHECK(Near(half.u0, 0.5f) && Near(half.u1, 1.0f) && half.inside);
    // Required render FOV: the taller of the two eyes (16:9 is wide enough here), plus 3 %.
    const float fov = RequiredVerticalFov({-1.0f, 0.9f, 1.0f, -1.1f}, {-0.9f, 1.0f, 1.0f, -1.1f}, 16.0f / 9.0f);
    CHECK(Near(fov, 2.0f * std::atan(1.1f * 1.03f), 1e-5f));
    CHECK(Near(RequiredVerticalFov({-2.0f, 2.0f, 0.5f, -0.5f}, {-2.0f, 2.0f, 0.5f, -0.5f}, 2.0f), 2.0f * std::atan(1.03f), 1e-5f));

    // Capture only once enough submits carried this lease's pose; cancel stale leases.
    WorldCaptureInput in;
    CHECK(DecideWorldStep(in) == WorldStep::None);
    in.leaseHeld = true;
    in.leasePoseId = 9;
    in.appliedPoseId = 8; // still the previous pose
    in.appliedRun = 5;
    CHECK(DecideWorldStep(in) == WorldStep::Wait);
    in.appliedPoseId = 9;
    in.appliedRun = 1;
    CHECK(DecideWorldStep(in) == WorldStep::Wait); // settle 2: the presented frame may be the one before
    in.appliedRun = 2;
    CHECK(DecideWorldStep(in) == WorldStep::Capture);
    in.appliedRun = 0;
    in.leaseAgeMs = kWorldLeaseMaxMs + 1;
    CHECK(DecideWorldStep(in) == WorldStep::Cancel);
    in.untracked = true;
    CHECK(DecideWorldStep(in) == WorldStep::Capture);

    // No pair for 2.5 s: back to the theater panel for 5 s, then another try.
    WorldModeGate gate;
    CHECK(!gate.Update(false, false, 1000));
    CHECK(gate.Update(true, false, 1000));
    CHECK(gate.Update(true, true, 3000) && gate.Update(true, false, 5400));
    CHECK(!gate.Update(true, false, 5500) && gate.Fallbacks() == 1);
    CHECK(!gate.Update(true, false, 10000));
    CHECK(gate.Update(true, false, 10500) && gate.World());
    CHECK(!gate.Update(false, false, 10600) && !gate.World()); // a menu or loading screen leaves at once
}

namespace {
// A render device laid out as on the analysed build: global -> device (vtable, settings at
// +0x80, outputs at +0x38, count at +0x30) -> settings (RenderDevice.* fields) and output 0.
struct FakeDeviceMemory final : fvr::runtime::IMemory {
    std::map<std::uint64_t, std::uint8_t> bytes;
    bool engineTenth = false; // the two-eye loop's extra 0.1 factor (seen under some condition)
    static constexpr std::uint64_t kGlobal = 0x1000, kDevice = 0x2000, kSettings = 0x3000, kOutputs = 0x4000;
    static constexpr std::uint64_t kVtable = 0x140001DD1870ULL;
    bool Read(std::uint64_t a, void* out, std::size_t n) override {
        auto* o = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            const auto it = bytes.find(a + i);
            if (it == bytes.end()) return false;
            o[i] = it->second;
        }
        return true;
    }
    bool Write(std::uint64_t a, const void* in, std::size_t n) override {
        const auto* p = static_cast<const std::uint8_t*>(in);
        for (std::size_t i = 0; i < n; ++i) {
            if (!bytes.count(a + i)) return false;
            bytes[a + i] = p[i];
        }
        return true;
    }
    template <typename T> void Put(std::uint64_t a, T v) {
        std::uint8_t b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T));
        for (std::size_t i = 0; i < sizeof(T); ++i) bytes[a + i] = b[i];
    }
    template <typename T> T Get(std::uint64_t a) {
        T v{};
        Read(a, &v, sizeof(T));
        return v;
    }
    FakeDeviceMemory() {
        for (std::uint64_t a = kSettings; a < kSettings + 0x90; ++a) bytes[a] = 0;
        for (std::uint64_t a = kOutputs; a < kOutputs + 0x138; ++a) bytes[a] = 0;
        Put(kGlobal, kDevice);
        Put(kDevice, kVtable);
        Put(kDevice + 0x30, std::uint32_t{1});
        Put(kDevice + 0x38, kOutputs);
        Put(kDevice + 0x80, kSettings);
        Put(kSettings + 0x28, std::uint32_t{1080});
        Put(kSettings + 0x2C, std::uint32_t{1920});
        Put(kSettings + 0x44, 1.0f);  // StereoDepth
        Put(kSettings + 0x48, 1.0f);  // StereoConvergenceScale
        Put(kSettings + 0x4C, 1.0f);  // StereoSeparationScale
    }
    // +0xC12FF0, then the two-eye loop's optional factor.
    void EngineFrame() {
        const bool split = Get<std::uint8_t>(kSettings + 0x7D) && Get<std::uint8_t>(kSettings + 0x7E);
        Put(kOutputs + 0x84, Get<float>(kSettings + 0x48) * 0.1f);
        float sep = Get<float>(kSettings + 0x44) * 0.01f * Get<float>(kSettings + 0x4C);
        if (engineTenth) sep *= 0.1f;
        Put(kOutputs + 0x88, sep);
        Put(kOutputs + 0x7C, std::uint32_t{split ? 1u : 0u});
        Put(kOutputs + 0x80, std::uint8_t{split ? std::uint8_t{1} : std::uint8_t{0}});
    }
};
} // namespace

TEST_CASE(EngineStereoDrivesSplitRenderAndRestoresExactly) {
    using namespace fvr::bf4;
    const EngineStereoLayout layout{FakeDeviceMemory::kGlobal, FakeDeviceMemory::kVtable};
    const float half = 0.032f, tanX = 2.1f;
    {
        FakeDeviceMemory mem;
        const auto before = mem.bytes;
        EngineStereo wrongClass(mem);
        CHECK(wrongClass.Enable({FakeDeviceMemory::kGlobal, FakeDeviceMemory::kVtable + 8}, half, tanX) ==
              EngineStereo::Status::Refused);
        CHECK(mem.bytes == before); // nothing written for an unexpected object
    }
    FakeDeviceMemory mem;
    const auto pristine = mem.bytes;
    {
        EngineStereo stereo(mem);
        CHECK(stereo.Enable(layout, half, tanX) == EngineStereo::Status::On);
        mem.EngineFrame();
        auto r = stereo.Read();
        CHECK(r.SplitActive() && Near(r.convergence, EngineStereo::kConvergenceMeters, 1e-4f));
        CHECK(Near(r.HalfEyeDistance(tanX), half, 1e-5f)); // the engine's eyes sit at the headset's
        for (int i = 0; i < 40; ++i) { stereo.Update(half, tanX); mem.EngineFrame(); }
        CHECK(stereo.Adjustments() == 0);

        // The two-eye loop scales separation by 0.1 under some condition: measured and corrected.
        mem.engineTenth = true;
        mem.EngineFrame();
        for (int i = 0; i < 40; ++i) { stereo.Update(half, tanX); mem.EngineFrame(); }
        CHECK(stereo.Adjustments() == 1 && Near(stereo.Read().HalfEyeDistance(tanX), half, 1e-5f));

        stereo.Disable();
        mem.EngineFrame();
        CHECK(!stereo.Read().SplitActive());
        // Every settings byte is back (outputs are the engine's own per-frame values).
        bool same = true;
        for (std::uint64_t a = FakeDeviceMemory::kSettings; a < FakeDeviceMemory::kSettings + 0x90; ++a)
            same = same && mem.bytes[a] == pristine.at(a);
        CHECK(same);
    }
    {
        FakeDeviceMemory game;
        EngineStereo stereo(game);
        CHECK(stereo.Enable(layout, half, tanX) == EngineStereo::Status::On);
        game.Put(FakeDeviceMemory::kSettings + 0x4C, 0.5f); // the game changed the scale itself
        stereo.Update(half, tanX);
        CHECK(stereo.State() == EngineStereo::Status::Foreign);
        CHECK(Near(game.Get<float>(FakeDeviceMemory::kSettings + 0x4C), 0.5f)); // the game's value stays
        CHECK(game.Get<std::uint8_t>(FakeDeviceMemory::kSettings + 0x7E) == 0); // ours went back
    }
    CHECK(Near(EngineStereo::NextSeparationScale(1.0f, 0.0032f, 0.032f), 10.0f, 1e-4f));
    CHECK(Near(EngineStereo::NextSeparationScale(1.0f, 1.0f, 1000.0f), 10.0f, 1e-4f)); // clamped step

    // Split crops: left eye from the left half shifted by +separation, right from the right half.
    const float tanY = 1.2f;
    const fvr::math::FovTangents fov[2] = {{-1.0f, 0.8f, 1.0f, -1.1f}, {-0.8f, 1.0f, 1.0f, -1.1f}};
    EyeCrop c[2];
    EyeCrops(fov, tanX, tanY, true, 0.01f, c);
    CHECK(Near(c[0].bu0, 0.0f) && Near(c[0].bu1, 0.5f) && Near(c[1].bu0, 0.5f) && Near(c[1].bu1, 1.0f));
    CHECK(Near(c[0].u0, 0.5f * (0.5f - 1.0f / (2 * tanX) - 0.005f), 1e-6f)); // right-handed view: left eye -sep
    CHECK(Near(c[1].u1, 0.5f + 0.5f * (0.5f + 1.0f / (2 * tanX) + 0.005f), 1e-6f));
    EyeCrops(fov, tanX, tanY, false, 0.01f, c);
    CHECK(Near(c[0].bu1, 1.0f) && Near(c[0].u0, 0.5f - 1.0f / (2 * tanX)));

    // No capture across a mono/stereo switch until the layout has been stable for settle + 2.
    WorldCaptureInput in;
    in.leaseHeld = true;
    in.leasePoseId = in.appliedPoseId = 3;
    in.appliedRun = 2;
    in.stableFrames = 3;
    CHECK(DecideWorldStep(in) == WorldStep::Wait);
    in.stableFrames = 4;
    CHECK(DecideWorldStep(in) == WorldStep::Capture);
}

TEST_CASE(LaserClickHoldsItsPositionUnlessItIsADrag) {
    using namespace fvr::bf4;
    using T = PointerEvent::Type;
    PointerInjector p;
    std::vector<PointerEvent> out;
    GamePointer g;
    g.active = true;
    g.u = 0.50f;
    g.v = 0.50f;
    p.Update(g, 1920, 1080, out);
    CHECK(out.size() == 1 && out[0].type == T::Move && out[0].x == 960 && out[0].y == 540);
    g.down = true;
    p.Update(g, 1920, 1080, out);
    CHECK(out.size() == 1 && out[0].type == T::ButtonDown && out[0].x == 960);
    g.u = 0.51f; // hand jitter while the trigger is held: the cursor stays put
    p.Update(g, 1920, 1080, out);
    CHECK(out.empty());
    g.down = false;
    p.Update(g, 1920, 1080, out);
    CHECK(out.size() == 1 && out[0].type == T::ButtonUp && out[0].x == 960 && out[0].y == 540);
    // After the release the laser is followed again.
    p.Update(g, 1920, 1080, out);
    CHECK(out.size() == 1 && out[0].type == T::Move && out[0].x == 979);
    // A deliberate drag (more than 2 % of the screen) follows the laser.
    g.down = true;
    p.Update(g, 1920, 1080, out);
    g.u = 0.60f;
    p.Update(g, 1920, 1080, out);
    CHECK(out.size() == 1 && out[0].type == T::Move && out[0].x == 1152);
    g.down = false;
    p.Update(g, 1920, 1080, out);
    CHECK(out.size() == 1 && out[0].type == T::ButtonUp && out[0].x == 1152);
    CHECK(p.Clicks() == 2);
}

TEST_CASE(HeadPoseTurnsTheBf4CameraTheSameWay) {
    using namespace fvr;
    using namespace fvr::bf4;
    // BF4 camera rows: right, up, +Z (behind the camera); the view looks along -Z.
    frostbite::LinearTransform game; // facing world -Z
    game.right = {1, 0, 0};
    game.up = {0, 1, 0};
    game.forward = {0, 0, 1};
    auto look = [&](const math::Pose& head) {
        const auto cam = HeadTrackedView(game, AdapterCore::XrPoseToBf4Camera(head), false);
        return math::Vec3{-cam.forward.x, -cam.forward.y, -cam.forward.z}; // view direction
    };
    math::Pose left; // head turned 90 degrees left (OpenXR: +90 about +Y)
    left.orientation = math::FromAxisAngle({0, 1, 0}, 1.5707963f);
    const auto l = look(left);
    CHECK(Near(l.x, -1.0f) && Near(l.z, 0.0f)); // the camera looks left (-X) too
    math::Pose up; // head pitched 30 degrees up (OpenXR: +30 about +X)
    up.orientation = math::FromAxisAngle({1, 0, 0}, 0.5235988f);
    const auto u = look(up);
    CHECK(Near(u.y, 0.5f) && Near(u.z, -0.8660254f)); // and up
    math::Pose leaned; // head 10 cm right and 5 cm forward (OpenXR -Z)
    leaned.position = {0.1f, 0, -0.05f};
    const auto cam = HeadTrackedView(game, AdapterCore::XrPoseToBf4Camera(leaned), false);
    CHECK(Near(cam.trans.x, 0.1f) && Near(cam.trans.z, -0.05f)); // forward for a -Z facing camera
}

namespace {
// A game whose aim turns by `gain` radians per mouse count, applied one frame later (the
// game reads input at the start of its next frame).
struct SimulatedGameAim {
    fvr::bf4::AimAngle aim{0.3f, 0.0f};
    float gainX = 0.00031f, gainY = -0.00022f;
    int pendingX = 0, pendingY = 0;
    std::uint64_t sample = 0;
    void Frame() {
        aim.yaw = fvr::bf4::WrapPi(aim.yaw + gainX * pendingX);
        aim.pitch += gainY * pendingY;
        pendingX = pendingY = 0;
        ++sample;
    }
};
} // namespace

TEST_CASE(ControllerAimSteersTheGameAimThroughMouseLook) {
    using namespace fvr;
    using namespace fvr::bf4;
    MotionAim aim;
    SimulatedGameAim game;
    MotionAimInput in;
    in.engage = true;
    in.aimTracked = true;
    in.reference.orientation = {0, 0, 0, 1};
    // Controller 40 degrees right and 15 degrees up of the reference.
    in.aim.orientation = math::FromAxisAngle({0, 1, 0}, -0.6981317f) * math::FromAxisAngle({1, 0, 0}, 0.2617994f);
    in.dt = 1.0f / 60;
    MotionAimOutput out;
    for (int i = 0; i < 240; ++i) {
        game.Frame();
        in.gameValid = true;
        in.game = game.aim;
        in.gameSample = game.sample;
        out = aim.Update(in);
        game.pendingX += out.mouseX;
        game.pendingY += out.mouseY;
    }
    // The body heading stayed where the game was looking at the start; the game's aim is
    // now the body heading plus the controller's direction.
    CHECK(out.engaged && Near(out.bodyYaw, 0.3f, 1e-4f));
    CHECK(Near(game.aim.yaw, 0.3f + 0.6981317f, 0.01f));
    CHECK(Near(game.aim.pitch, 0.2617994f, 0.01f));
    CHECK(Near(aim.GainX(), 0.00031f, 0.00008f) && aim.GainY() < 0); // learned, including the sign
    CHECK(Near(out.moveRotation, 0.6981317f, 0.01f)); // walking is turned back to the body
    // The right stick turns the body heading; the aim follows it.
    in.turn = 1.0f;
    for (int i = 0; i < 30; ++i) {
        game.Frame();
        in.game = game.aim;
        in.gameSample = game.sample;
        out = aim.Update(in);
        game.pendingX += out.mouseX;
        game.pendingY += out.mouseY;
    }
    CHECK(Near(out.bodyYaw, WrapPi(0.3f + 30 * MotionAim::kTurnRate / 60), 1e-3f));
    in.turn = 0;
    for (int i = 0; i < 120; ++i) {
        game.Frame();
        in.game = game.aim;
        in.gameSample = game.sample;
        out = aim.Update(in);
        game.pendingX += out.mouseX;
        game.pendingY += out.mouseY;
    }
    CHECK(Near(WrapPi(game.aim.yaw - out.bodyYaw), 0.6981317f, 0.01f));
    // Released outside campaign play: no mouse movement at all.
    in.engage = false;
    out = aim.Update(in);
    CHECK(!out.engaged && out.mouseX == 0 && out.mouseY == 0);
    // Body-heading view: yaw 90 degrees right looks down +X with the game camera's position.
    frostbite::LinearTransform gameCam;
    gameCam.trans = {5, 1, 2};
    const auto view = HeadTrackedViewAtYaw(gameCam, frostbite::LinearTransform{}, 1.5707963f);
    CHECK(Near(-view.forward.x, 1.0f) && Near(view.forward.z, 0.0f) && Near(view.trans.x, 5.0f));
    CHECK(Near(CameraAim(view.forward).yaw, 1.5707963f));
}

TEST_CASE(ControllerAimGoesQuietWhenTheGameIgnoresTheMouse) {
    using namespace fvr;
    using namespace fvr::bf4;
    MotionAim aim;
    MotionAimInput in;
    in.engage = in.aimTracked = in.gameValid = true;
    in.reference.orientation = {0, 0, 0, 1};
    in.aim.orientation = math::FromAxisAngle({0, 1, 0}, -1.0f); // far right: large errors
    in.dt = 1.0f / 60;
    int sent = 0, quietFrames = 0;
    for (int i = 1; i <= 200; ++i) { // a paused game: the aim never moves
        in.gameSample = static_cast<std::uint64_t>(i);
        const auto out = aim.Update(in);
        sent += std::abs(out.mouseX) + std::abs(out.mouseY);
        if (out.engaged && !out.mouseX && !out.mouseY) ++quietFrames;
    }
    CHECK(aim.Mutes() >= 1 && quietFrames >= MotionAim::kMuteFrames);
    CHECK(sent < 200 * MotionAim::kMaxCounts); // it did not keep pushing every frame
}
