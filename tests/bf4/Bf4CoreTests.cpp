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
#include "../../src/games/bf4/Bf4SessionGuard.h"
#include "../../src/games/bf4/Bf4SessionInfo.h"

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
    s.fovY = 55.0f; s.nearPlane = 0.1f; s.farPlane = 1000.0f; s.aspect = 1.7777f;
    CHECK(PlausibleProjection(s));
    s.farPlane = 0.05f;
    CHECK(!PlausibleProjection(s));
    const std::uint8_t getter70[] = {0x48, 0x8B, 0x41, 0x70, 0xC3}, notGetter[] = {0x48, 0x8B, 0x41, 0x70, 0x90};
    CHECK(FieldGetterOffset(getter70) == 0x70 && FieldGetterOffset(notGetter) == -1);
}
