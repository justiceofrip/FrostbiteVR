#include "Bf4FrameworkGlue.h"
#include "Bf4SessionInfo.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#ifdef FVR_BF4_FRAMEWORK_GLUE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include "Bf4EyeResampler.h"
#include "Bf4WorldView.h"
#include "fvr/graphics/D3D11FrameBridge.h"
#include "fvr/graphics/D3D11MenuProducer.h"
#include "fvr/ipc/MenuChannel.h"

#include <memory>
#include <mutex>
#include <string>
#endif

namespace fvr::bf4 {

namespace {

class CountingFrameSink final : public IFrameSink {
public:
    bool Publish(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D*, const FrameMeta&) override { return true; }
    void ReleaseSwapchainResources() override {}
};

class EmptyControllerSource final : public IControllerSource {
public:
    bool Poll(ControllerSnapshot& out) override { out = {}; return false; }
};

// Emulated-test input: sweeps the pointer to a target and clicks once. Enabled only by
// FVR_BF4_SCRIPTED_POINTER=u,v (set by Bf4EmuHost, never by the launcher).
class ScriptedPointerSource final : public IControllerSource {
public:
    ScriptedPointerSource(float u, float v) : u_(u), v_(v) {}
    bool Poll(ControllerSnapshot& out) override {
        ++tick_;
        out = {};
        out.valid = true;
        out.timeSeconds = tick_ / 90.0;
        const float k = std::fmin(1.0f, tick_ / 40.0f);
        out.pointer.active = true;
        out.pointer.u = 0.1f + (u_ - 0.1f) * k;
        out.pointer.v = 0.1f + (v_ - 0.1f) * k;
        out.pointer.down = tick_ >= 60 && tick_ < 70;
        return true;
    }
private:
    float u_, v_;
    int tick_ = 0;
};

#ifdef FVR_BF4_FRAMEWORK_GLUE
// Headset link: everything between the adapter and the XR host, on the two lanes the host
// already speaks (same host and transports as BC2).
//  - Theater: the whole game screen (or the mod menu) on the menu lane (MenuChannel +
//    D3D11MenuProducer), shown as one quad with the host's laser pointer; the host's
//    MenuControl comes back as the game pointer.
//  - World (head-tracked view in campaign levels): the menu lane says Gameplay, so the host
//    asks for eye pairs on the frame lane (D3D11FrameBridge). Each request (a FrameLease) is
//    taken at the start of a Present; its head pose goes to the camera tagged with a pose
//    id; once the view submits show the presented frame carries it (Bf4WorldView.h), the
//    two eye crops of that frame go back labelled with the lease's own tracking. No pair is
//    ever labelled with a pose it was not rendered with: a lease that cannot be met is
//    cancelled, and with no pair for 2.5 s the screen returns to the theater panel.
// The frame lane also carries the controller samples (one producer per channel, so they
// arrive through the bridge). The host writes its channel token to <log_dir>\xr-channel.txt;
// the link connects whenever that file appears (the host may start after the game).
class TheaterLink {
public:
    explicit TheaterLink(std::string tokenFile) : tokenFile_(std::move(tokenFile)) {}

    void Publish(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer, const FrameMeta& meta) {
        std::lock_guard lock(mutex_);
        if (!Connect()) return;
        EnsureBridge(device);
        const ULONGLONG now = GetTickCount64();
        RefreshUntracked(now);
        const bool worldAllowed = (meta.world || untracked_) && BridgeUsable();
        bool pair = false;
        if (leaseHeld_) {
            WorldCaptureInput in;
            in.leaseHeld = true;
            in.leasePoseId = leasePoseId_;
            in.appliedPoseId = meta.poseId;
            in.appliedRun = meta.poseRun;
            in.settle = meta.worldSettle;
            in.leaseAgeMs = now - leaseTakenMs_;
            in.untracked = untracked_;
            in.stableFrames = meta.stereoStableFrames;
            const WorldStep step = worldAllowed ? DecideWorldStep(in) : WorldStep::Cancel;
            if (step == WorldStep::Capture) {
                pair = CapturePair(context, backbuffer, meta);
                if (!pair) ++captureFailures_;
                else if (meta.stereoSplit) ++stereoPairs_;
            }
            if (step == WorldStep::Cancel || (step == WorldStep::Capture && !pair)) CancelLease();
        }
        world_ = gate_.Update(worldAllowed, pair, now);
        if (!world_ && leaseHeld_) CancelLease();
        if (bridge_) bridge_->PumpGraphics();
        PublishState(backbuffer, meta.width, meta.height, world_);
    }

    // An adapter-owned image (the mod menu): always the theater lane.
    void PublishPanel(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height) {
        std::lock_guard lock(mutex_);
        if (!Connect()) return;
        world_ = gate_.Update(false, false, GetTickCount64());
        if (leaseHeld_) CancelLease();
        if (bridge_) bridge_->PumpGraphics();
        PublishState(texture, width, height, false);
    }

    std::string Describe() {
        std::lock_guard lock(mutex_);
        char line[512];
        std::snprintf(line, sizeof(line),
                      "theater_link connected=%d epoch=%llu states=%llu captured=%llu published=%llu failed=%llu discarded=%llu "
                      "lane=%s bridge=%d leases=%llu pairs=%llu stereo_pairs=%llu cancels=%llu capture_failures=%llu fallbacks=%llu "
                      "bridge_published=%llu consumed=%llu host_discarded=%llu bridge_failure=%u/0x%08lx resampler=%u/0x%08lx%s\n",
                      !token_.empty() && !closed_ ? 1 : 0, static_cast<unsigned long long>(epoch_),
                      static_cast<unsigned long long>(statesPublished_), static_cast<unsigned long long>(producer_.Captured()),
                      static_cast<unsigned long long>(producer_.Published()), static_cast<unsigned long long>(producer_.Failed()),
                      static_cast<unsigned long long>(producer_.Discarded()), world_ ? "world" : "theater",
                      bridge_ && bridge_->Connected() ? 1 : 0, static_cast<unsigned long long>(leases_),
                      static_cast<unsigned long long>(pairs_), static_cast<unsigned long long>(stereoPairs_),
                      static_cast<unsigned long long>(cancels_),
                      static_cast<unsigned long long>(captureFailures_), static_cast<unsigned long long>(gate_.Fallbacks()),
                      static_cast<unsigned long long>(bridge_ ? bridge_->Published() : 0),
                      static_cast<unsigned long long>(bridge_ ? bridge_->Consumed() : 0),
                      static_cast<unsigned long long>(bridge_ ? bridge_->Discarded() : 0), bridge_ ? bridge_->FailureStage() : 0u,
                      static_cast<unsigned long>(bridge_ ? bridge_->FailureCode() : 0), resampler_.LastStage(),
                      static_cast<unsigned long>(resampler_.LastError()), untracked_ ? " untracked" : "");
        return line;
    }

    bool WorldLane() {
        std::lock_guard lock(mutex_);
        return world_;
    }

    void Release() {
        std::lock_guard lock(mutex_);
        producer_.Reset(); // swapchain buffers are about to change
        resampler_.Reset();
    }

    bool Poll(ControllerSnapshot& out) {
        std::lock_guard lock(mutex_);
        out = {};
        if (!Connect()) return false;
        if (bridge_) {
            bridge_->PumpGraphics();
            // Controller sample for the virtual gamepad (host publishes one per XR frame); a
            // stale or invalid sample leaves `controls` unfocused, i.e. a neutral pad.
            interaction::InputFrame f{};
            if (bridge_->ReadInput(f) == ipc::ChannelResult::Ok && interaction::ValidInput(f)) {
                out.controls = f;
                if (f.headValid) out.head = f.head;
            }
            // World lane: take the host's current eye-pair request at the start of the frame,
            // so this frame's camera update already carries its pose.
            if (world_ && !leaseHeld_ && BridgeUsable() &&
                bridge_->TryBegin({kLeaseOwner, ++frameSerial_, deviceEpoch_}, lease_)) {
                leaseHeld_ = true;
                leasePoseId_ = ++nextPoseId_;
                leaseTakenMs_ = GetTickCount64();
                ++leases_;
            }
        }
        if (leaseHeld_) {
            out.world.valid = true;
            out.world.poseId = leasePoseId_;
            out.world.head = lease_.tracking.head;
            out.world.fov[0] = lease_.tracking.fov[0];
            out.world.fov[1] = lease_.tracking.fov[1];
            const auto& e = lease_.tracking.eyes;
            const float dx = e[1].position.x - e[0].position.x, dy = e[1].position.y - e[0].position.y,
                        dz = e[1].position.z - e[0].position.z;
            out.world.halfEyeDistance = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz) * lease_.tracking.worldUnitsPerMeter;
        }
        ipc::MenuControl c{};
        const auto r = channel_.ReadControl(c);
        if (r == ipc::ChannelResult::Closed) closed_ = true;
        if (r != ipc::ChannelResult::Ok) return out.controls.focused || out.world.valid;
        out.valid = true;
        out.pointer.active = (c.flags & ipc::MenuPoint) && c.menuEpoch == epoch_;
        out.pointer.u = c.u;
        out.pointer.v = c.v;
        out.pointer.down = out.pointer.active && (c.flags & ipc::MenuDown);
        out.menuButton = c.toggle != lastToggle_;
        lastToggle_ = c.toggle;
        return true;
    }

private:
    static constexpr std::uint64_t kLeaseOwner = 0xB4F0000000000001ULL;

    // Bf4EmuHost only: FVR_BF4_WORLD_UNTRACKED=1 sends frames on the world lane without a
    // camera (the emulator has no renderer), to test transport and crops end to end. Read
    // from the process environment block, so the emulator can switch it per phase.
    void RefreshUntracked(ULONGLONG now) {
        if (now - untrackedCheckedMs_ < 250) return;
        untrackedCheckedMs_ = now;
        char v[4] = {};
        untracked_ = GetEnvironmentVariableA("FVR_BF4_WORLD_UNTRACKED", v, sizeof(v)) == 1 && v[0] == '1';
    }

    bool BridgeUsable() const { return bridge_ && bridge_->Connected() && !bridge_->Fatal(); }

    void PublishState(ID3D11Texture2D* image, std::uint32_t width, std::uint32_t height, bool world) {
        ipc::MenuState s{};
        s.sequence = ++sequence_;
        s.epoch = epoch_;
        // Theater: the game screen (or mod menu) is the panel. World: no panel; the host
        // asks for eye pairs instead.
        s.mode = world ? ipc::MenuMode::Gameplay : ipc::MenuMode::Menu;
        // The host's pointer is verified for a 16:9 surface labelled 1280x720 and returns
        // normalised u,v; the adapter maps those to client pixels itself. Other aspects
        // stay visible there but unpointable, matching the host's evidence rule.
        const bool wide169 = height && std::uint64_t(width) * 9 == std::uint64_t(height) * 16;
        s.logicalWidth = wide169 ? 1280 : width;
        s.logicalHeight = wide169 ? 720 : height;
        s.inputReady = 1;
        const auto r = channel_.PublishState(s);
        if (r == ipc::ChannelResult::Ok) ++statesPublished_;
        else if (r == ipc::ChannelResult::Closed) { closed_ = true; return; }
        if (!world) producer_.Pump(image, channel_);
    }

    // Both eyes from this one presented frame: crop, resample to the host's exact eye size,
    // capture, publish (the bridge restores nothing native: the camera write was already
    // restored inside the submit call).
    bool CapturePair(ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer, const FrameMeta& meta) {
        if (!context || !backbuffer || !meta.width || !meta.height) return false;
        const auto& req = lease_.requirements;
        const auto& t = lease_.tracking;
        const float aspect = float(meta.width) / float(meta.height);
        float fovY = meta.renderFovY;
        if (untracked_) fovY = RequiredVerticalFov(t.fov[0], t.fov[1], aspect);
        if (!(fovY > 0.05f && fovY < 3.1f)) return false;
        const float tanY = std::tan(fovY * 0.5f), tanX = tanY * aspect;
        EyeCrop crops[2];
        const math::FovTangents fov[2] = {t.fov[0], t.fov[1]};
        EyeCrops(fov, tanX, tanY, meta.stereoSplit && !untracked_, meta.stereoSeparation, crops);
        if (!resampler_.Render(context, backbuffer, crops, req.width, req.height, req.format)) return false;
        if (!bridge_->CaptureEye(0, resampler_.Eye(0), lease_) || !bridge_->CaptureEye(1, resampler_.Eye(1), lease_) ||
            !bridge_->PublishRestored(lease_))
            return false;
        leaseHeld_ = false;
        ++pairs_;
        return true;
    }

    void CancelLease() {
        if (!leaseHeld_) return;
        if (bridge_) bridge_->Cancel(lease_);
        leaseHeld_ = false;
        ++cancels_;
    }

    // Frame lane: the bridge needs the game's device, so it connects on the graphics thread
    // at the first Present after the host token is known (both hooks run on that thread).
    void EnsureBridge(ID3D11Device* device) {
        if (!device || bridgeToken_ == token_) return;
        if (bridge_ && leaseHeld_) CancelLease();
        if (bridge_) bridge_->CloseGraphics();
        bridge_ = std::make_unique<graphics::D3D11FrameBridge>(); // failures are sticky per bridge
        const std::wstring wide(token_.begin(), token_.end());
        bridgeToken_ = token_;
        ++deviceEpoch_;
        if (!bridge_->ConnectGraphics(device, wide)) bridge_.reset();
    }

    // Attached to a host session: stay attached until a call reports Closed (the host ended).
    // MenuChannel::Connected() also reads false while the shared lock is merely busy (a
    // stall on either side), and Close() ends a token's session for good, so a busy lock
    // must never trigger a teardown. Reattach only to a NEW token (a restarted host).
    bool Connect() {
        if (!token_.empty() && !closed_) return true;
        const ULONGLONG now = GetTickCount64();
        if (now - lastAttempt_ < 1000) return false;
        lastAttempt_ = now;
        std::string token;
        if (std::FILE* f = std::fopen(tokenFile_.c_str(), "rb")) {
            char buf[256];
            const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
            std::fclose(f);
            for (std::size_t i = 0; i < n; ++i)
                if (buf[i] > ' ' && buf[i] < 127) token += buf[i]; // tolerate UTF-16 or trailing newline
        }
        if (token.empty() || token == token_) return false; // nothing new to attach to
        channel_.Close();
        producer_.Reset();
        const std::wstring wide(token.begin(), token.end());
        if (!channel_.ConnectProducer(wide)) return false;
        token_ = token;
        closed_ = false;
        world_ = false;
        ++epoch_; // a new host connection starts a new pointer epoch
        return true;
    }

    std::mutex mutex_;
    std::string tokenFile_;
    ipc::MenuChannel channel_;
    graphics::D3D11MenuProducer producer_;
    std::unique_ptr<graphics::D3D11FrameBridge> bridge_;
    EyeResampler resampler_;
    WorldModeGate gate_;
    ipc::FrameLease lease_{};
    bool leaseHeld_ = false, world_ = false, untracked_ = false;
    std::uint64_t leasePoseId_ = 0, nextPoseId_ = 0, frameSerial_ = 0, deviceEpoch_ = 0;
    std::uint64_t leases_ = 0, pairs_ = 0, stereoPairs_ = 0, cancels_ = 0, captureFailures_ = 0;
    ULONGLONG leaseTakenMs_ = 0, untrackedCheckedMs_ = 0;
    std::uint64_t sequence_ = 0, epoch_ = 0, lastToggle_ = 0, statesPublished_ = 0;
    ULONGLONG lastAttempt_ = 0;
    std::string token_;       // the host session we are attached to
    std::string bridgeToken_; // the host session the frame lane is attached to
    bool closed_ = false;
};

TheaterLink* g_link = nullptr; // one per adapter process; lives until exit

TheaterLink& Link(const SessionInfo& session) {
    if (!g_link) g_link = new TheaterLink(session.logDir + "\\xr-channel.txt");
    return *g_link;
}

class TheaterFrameSink final : public IFrameSink {
public:
    explicit TheaterFrameSink(TheaterLink& link) : link_(link) {}
    bool Publish(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer,
                 const FrameMeta& meta) override {
        if (meta.eye > 0) return true; // the theater quad is mono: left eye / mono frame only
        link_.Publish(device, context, backbuffer, meta);
        return true;
    }
    void ReleaseSwapchainResources() override { link_.Release(); }
    bool PublishTexture(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height) override {
        link_.PublishPanel(texture, width, height);
        return true;
    }
    std::string Describe() const override { return link_.Describe(); }
    bool WorldLane() const override { return link_.WorldLane(); }

private:
    TheaterLink& link_;
};

class TheaterControllerSource final : public IControllerSource {
public:
    explicit TheaterControllerSource(TheaterLink& link) : link_(link) {}
    bool Poll(ControllerSnapshot& out) override { return link_.Poll(out); }

private:
    TheaterLink& link_;
};
#endif

} // namespace

std::unique_ptr<IFrameSink> CreateFrameSink(const SessionInfo& session) {
#ifdef FVR_BF4_FRAMEWORK_GLUE
    if (!session.logDir.empty()) return std::make_unique<TheaterFrameSink>(Link(session));
#else
    (void)session;
#endif
    return std::make_unique<CountingFrameSink>();
}

std::unique_ptr<IControllerSource> CreateControllerSource(const SessionInfo& session) {
    if (const char* scripted = std::getenv("FVR_BF4_SCRIPTED_POINTER")) {
        float u = 0.5f, v = 0.5f;
        if (std::sscanf(scripted, "%f,%f", &u, &v) == 2) return std::make_unique<ScriptedPointerSource>(u, v);
    }
#ifdef FVR_BF4_FRAMEWORK_GLUE
    if (!session.logDir.empty()) return std::make_unique<TheaterControllerSource>(Link(session));
#else
    (void)session;
#endif
    return std::make_unique<EmptyControllerSource>();
}

} // namespace fvr::bf4
