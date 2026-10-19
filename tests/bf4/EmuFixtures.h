#pragma once
// Shared emulated-BF4 fixtures: synthetic .text image, fake platform (Win32/D3D11/MinHook
// stand-in, including a fake backbuffer and the emulated nvapi64.dll), session helpers.
#include "../../src/games/bf4/Bf4AdapterCore.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fvr::bf4::emu {


// ---- emulated executable ---------------------------------------------------------------
struct FakeImage {
    std::vector<std::uint8_t> code = std::vector<std::uint8_t>(4096, 0xCC);
    std::vector<std::string> patterns; // storage for BindingSpec string_views
    std::vector<BindingSpec> specs;

    // Plants "48 8B 05 <disp32> 0F 1F <id>" per binding: a RIP-relative load + unique tag.
    explicit FakeImage(std::initializer_list<std::string_view> enrolled) {
        patterns.reserve(16);
        std::size_t at = 64;
        std::uint8_t id = 1;
        for (const auto& spec : DefaultBindingSpecs()) {
            BindingSpec s = spec;
            bool on = false;
            for (auto n : enrolled) on = on || n == spec.name;
            if (on) {
                const std::uint8_t bytes[] = {0x48, 0x8B, 0x05, 0x00, 0x02, 0x00, 0x00, 0x0F, 0x1F, id};
                std::memcpy(&code[at], bytes, sizeof(bytes));
                char text[64];
                std::snprintf(text, sizeof(text), "48 8B 05 ?? ?? ?? ?? 0F 1F %02X", id);
                patterns.emplace_back(text);
                s.pattern = patterns.back();
                s.kind = BindingKind::RipRelativeGlobal;
                s.adjust = 0;
                s.dispOffset = 3;
                s.instructionLength = 7;
                at += 32;
            } else {
                s.pattern = ""; // RE not done in this emulated image (real patterns never match it)
            }
            ++id;
            specs.push_back(s);
        }
    }
    BindingTable Resolve() const {
        BindingTable t(specs);
        t.Resolve(code, 0x140001000ull);
        return t;
    }
};

inline const std::initializer_list<std::string_view> kAllBindings = {
    names::kGameRenderer, names::kRenderViewUpdate, names::kStereoEyeLoop, names::kStereoActiveEye,
    names::kClientGameContext, names::kTypeInfoHead, names::kLevelName, names::kInputConceptCache,
    names::kUiCursorRead, names::kSoldierWeaponTransform};

// ---- emulated platform -----------------------------------------------------------------
struct FakePlatform final : IAdapterPlatform {
    bool punkBuster = false;
    int moduleScans = 0;
    std::string level;          // "" = front end
    bool levelKnown = false, remoteKnown = false, remote = false;
    std::vector<PointerEvent> pointerLog;
    std::vector<std::vector<CameraRequest>> cameraLog;
    std::vector<FrameMeta> frames;
    int reloads = 0, detaches = 0;
    std::vector<std::string> log;

    bool PunkBusterLoaded() override { ++moduleScans; return punkBuster; }
    SessionObservation ObserveGame() override {
        SessionObservation o;
        o.levelKnown = levelKnown;
        o.levelName = level;
        o.remoteKnown = remoteKnown;
        o.connectedToRemoteServer = remote;
        return o;
    }
    void ApplyPointer(std::span<const PointerEvent> e) override { pointerLog.insert(pointerLog.end(), e.begin(), e.end()); }
    bool PublishFrame(const FrameMeta& m) override { frames.push_back(m); return true; }
    bool ApplyCamera(std::span<const CameraRequest> r) override { cameraLog.emplace_back(r.begin(), r.end()); return true; }
    void RequestNativeReload() override { ++reloads; }
    void DetachAll(std::string_view) override { ++detaches; }
    void Log(std::string_view line) override { log.emplace_back(line); }

    // ---- engine stereo (fake shim + fake backbuffer) ----
    stereo::NvapiStereoEmu* shim = nullptr;   // set when the "game" loaded nvapi64.dll
    unsigned vendor = 0x1002;                  // AMD by default, like the dev machine
    bool spoofed = false;
    std::string backbuffer;                    // what the "engine" last drew
    std::vector<std::pair<int, std::string>> captures;
    int snapshots = 0;
    bool suppressed = false;
    std::map<std::string, std::string> reports;

    StereoShimState StereoShim() override {
        StereoShimState s;
        s.bound = shim != nullptr;
        s.gpuVendorId = vendor;
        s.vendorSpoofed = spoofed;
        if (shim) {
            s.directMode = shim->DirectMode();
            s.eyeSwitches = shim->EyeSwitches();
            s.handleCreated = shim->HandleCount() > 0;
        }
        return s;
    }
    bool CaptureEye(int eye) override { captures.emplace_back(eye, backbuffer); return true; }
    bool WriteStereoSnapshot() override { ++snapshots; return true; }
    void SetStereoSuppressed(bool s) override { suppressed = s; if (shim) shim->SetSuppressed(s); }
    std::string NvapiReport() override { return shim ? shim->Report() : std::string("nvapi64.dll not loaded\n"); }
    void WriteReport(std::string_view name, std::string_view text) override { reports[std::string(name)] = std::string(text); }

    // ---- virtual gamepad ----
    interaction::PadState pad;
    bool padActive = false;
    int padUpdates = 0;
    void ApplyPad(const interaction::PadState& p, bool active) override { pad = p; padActive = active; ++padUpdates; }

    // ---- FrostbiteVR's own menu panel ----
    int panelsPublished = 0;
    std::uint64_t lastPanelRevision = 0;
    bool PublishPanel(const ui::VrPanel& panel) override {
        ++panelsPublished;
        lastPanelRevision = panel.Revision();
        return true;
    }

    // ---- split-screen route: a real pixel backbuffer analysed by the real code ----
    std::vector<std::uint8_t> pixels; // RGBA
    int pixelWidth = 0, pixelHeight = 0;
    int splitCaptures = 0;
    bool CaptureSplitPair() override { ++splitCaptures; return !pixels.empty(); }
    bool AnalyzeBackbufferSplit(stereo::SplitAnalysis& out) override {
        if (pixels.empty()) return false;
        out = stereo::AnalyzeSplitFrame({pixels.data(), pixelWidth, pixelHeight, pixelWidth * 4, true});
        return true;
    }

    void Campaign() { levelKnown = remoteKnown = true; remote = false; level = "Levels/SP_Tashgar/SP_Tashgar"; }
};

inline BuildIdentity X64() {
    BuildIdentity b;
    b.verdict = BuildVerdict::UnknownX64;
    b.label = "emulated";
    return b;
}

inline SessionInfo Session(bool developer, interaction::ReloadMode mode = interaction::ReloadMode::ButtonPress) {
    SessionInfo s;
    s.protocol = kSessionProtocol;
    s.adapterPath = "emu";
    s.developerFeatures = developer;
    s.reloadMode = mode;
    return s;
}

struct Clock {
    std::uint64_t ms = 1000;
    PlatformFrame Next() {
        ms += 11; // ~90 Hz
        PlatformFrame f;
        f.tickMs = ms;
        f.backbufferWidth = 1920;
        f.backbufferHeight = 1080;
        f.clientWidth = 1920;
        f.clientHeight = 1080;
        return f;
    }
};

inline ControllerSnapshot Input(double t) {
    ControllerSnapshot in;
    in.valid = true;
    in.timeSeconds = t;
    in.head.position = {0.0f, 1.7f, 0.0f};
    in.eyes[0].pose = in.head;
    in.eyes[0].pose.position.x = -0.032f;
    in.eyes[1].pose = in.head;
    in.eyes[1].pose.position.x = 0.032f;
    in.eyes[0].fov = {-0.95f, 0.75f, 0.85f, -0.9f};
    in.eyes[1].fov = {-0.75f, 0.95f, 0.85f, -0.9f};
    return in;
}


} // namespace fvr::bf4::emu
