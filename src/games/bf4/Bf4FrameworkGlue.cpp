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
#include "stereo/SideBySide.h"
#include "fvr/graphics/D3D11FrameBridge.h"
#include "fvr/graphics/D3D11MenuProducer.h"
#include "fvr/ipc/MenuChannel.h"

#include <deque>
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
// the link connects whenever that file appears (the host may start after the game). A host
// started with --frame-channels 2 also writes xr-channel-2.txt first: a second bridge then
// takes the next request while the current one renders (FIFO, oldest pose first).
class TheaterLink {
public:
    explicit TheaterLink(std::string tokenFile) : tokenFile_(std::move(tokenFile)) {
        // The host's second frame channel (--frame-channels 2): <stem>-2<ext>.
        const auto dot = tokenFile_.find_last_of('.');
        const auto slash = tokenFile_.find_last_of("\\/");
        secondTokenFile_ = dot != std::string::npos && (slash == std::string::npos || dot > slash)
                               ? tokenFile_.substr(0, dot) + "-2" + tokenFile_.substr(dot)
                               : tokenFile_ + "-2";
    }

    void Publish(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer, const FrameMeta& meta) {
        std::lock_guard lock(mutex_);
        if (!Connect()) return;
        EnsureBridges(device);
        const ULONGLONG now = GetTickCount64();
        RefreshUntracked(now);
        const bool worldAllowed = (meta.world || untracked_) && BridgeUsable(0);
        bool pair = false;
        if (!queue_.empty()) {
            const Lease& front = queue_.front();
            WorldCaptureInput in;
            in.leaseHeld = true;
            in.leasePoseId = front.poseId;
            in.appliedPoseId = meta.poseId;
            in.appliedRun = meta.poseRun;
            in.settle = meta.worldSettle;
            in.leaseAgeMs = now - front.takenMs;
            in.leaseMaxMs = LeaseMaxMs();
            in.untracked = untracked_;
            in.stableFrames = meta.stereoStableFrames;
            const WorldStep step = worldAllowed ? DecideWorldStep(in) : WorldStep::Cancel;
            if (step == WorldStep::Capture) {
                pair = CapturePair(context, backbuffer, meta);
                if (!pair) ++captureFailures_;
                else if (meta.stereoSplit) ++stereoPairs_;
                if (pair && !snapshotDir_.empty()) {
                    WriteSnapshot(context, backbuffer);
                    snapshotDir_.clear();
                }
            }
            if (step == WorldStep::Cancel && !worldAllowed) CancelLeases();
            else if (step == WorldStep::Cancel || (step == WorldStep::Capture && !pair)) CancelFront();
        }
        world_ = gate_.Update(worldAllowed, pair, now);
        if (!world_) CancelLeases();
        PumpBridges();
        PublishState(backbuffer, meta.width, meta.height, world_);
    }

    // An adapter-owned image (the mod menu): always the theater lane.
    void PublishPanel(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height) {
        std::lock_guard lock(mutex_);
        if (!Connect()) return;
        world_ = gate_.Update(false, false, GetTickCount64());
        CancelLeases();
        PumpBridges();
        PublishState(texture, width, height, false);
    }

    std::string Describe() {
        std::lock_guard lock(mutex_);
        std::uint64_t published = 0, consumed = 0, discarded = 0;
        unsigned connected = 0, failureStage = 0;
        unsigned long failureCode = 0;
        for (const auto& b : bridges_) {
            if (!b) continue;
            if (b->Connected()) ++connected;
            published += b->Published();
            consumed += b->Consumed();
            discarded += b->Discarded();
            if (!failureStage && b->FailureStage()) {
                failureStage = b->FailureStage();
                failureCode = static_cast<unsigned long>(b->FailureCode());
            }
        }
        char line[640];
        std::snprintf(line, sizeof(line),
                      "theater_link connected=%d epoch=%llu states=%llu captured=%llu published=%llu failed=%llu discarded=%llu "
                      "lane=%s bridges=%u leases=%llu queued=%llu pairs=%llu stereo_pairs=%llu reticle_pairs=%llu marked_pairs=%llu depth_pairs=%llu cancels=%llu capture_failures=%llu "
                      "fallbacks=%llu bridge_published=%llu consumed=%llu host_discarded=%llu bridge_failure=%u/0x%08lx "
                      "resampler=%u/0x%08lx%s\n",
                      !token_.empty() && !closed_ ? 1 : 0, static_cast<unsigned long long>(epoch_),
                      static_cast<unsigned long long>(statesPublished_), static_cast<unsigned long long>(producer_.Captured()),
                      static_cast<unsigned long long>(producer_.Published()), static_cast<unsigned long long>(producer_.Failed()),
                      static_cast<unsigned long long>(producer_.Discarded()), world_ ? "world" : "theater", connected,
                      static_cast<unsigned long long>(leases_), static_cast<unsigned long long>(queuedLeases_),
                      static_cast<unsigned long long>(pairs_), static_cast<unsigned long long>(stereoPairs_),
                      static_cast<unsigned long long>(reticles_), static_cast<unsigned long long>(markedPairs_),
                      static_cast<unsigned long long>(depthPairs_),
                      static_cast<unsigned long long>(cancels_), static_cast<unsigned long long>(captureFailures_),
                      static_cast<unsigned long long>(gate_.Fallbacks()), static_cast<unsigned long long>(published),
                      static_cast<unsigned long long>(consumed), static_cast<unsigned long long>(discarded), failureStage,
                      failureCode, resampler_.LastStage(), static_cast<unsigned long>(resampler_.LastError()),
                      untracked_ ? " untracked" : "");
        return line;
    }

    bool WorldLane() {
        std::lock_guard lock(mutex_);
        return world_;
    }

    void RequestSnapshot(const std::string& dir) {
        std::lock_guard lock(mutex_);
        snapshotDir_ = dir;
    }

    bool CurrentWorld(WorldRequest& out) {
        std::lock_guard lock(mutex_);
        out = {};
        FillWorld(out);
        return out.valid;
    }

    bool PublishFeedback(const interaction::FeedbackEvent& event) {
        std::lock_guard lock(mutex_);
        return BridgeUsable(0) && bridges_[0]->PublishFeedback(event);
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
        PumpBridges();
        if (bridges_[0]) {
            // Controller sample for the virtual gamepad (host publishes one per XR frame, on its
            // first frame channel); a stale or invalid sample leaves `controls` unfocused, i.e.
            // a neutral pad.
            interaction::InputFrame f{};
            if (bridges_[0]->ReadInput(f) == ipc::ChannelResult::Ok && interaction::ValidInput(f)) {
                out.controls = f;
                if (f.headValid) out.head = f.head;
            }
        }
        // World lane: take the host's eye-pair requests at the start of the frame. With two
        // frame channels the next request is already queued while the current one renders;
        // the oldest one's pose goes to the camera (requests are served in host order).
        if (world_) TakeLeases();
        FillWorld(out.world);
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
    static constexpr unsigned kBridges = 2;

    struct Lease {
        ipc::FrameLease lease{};
        unsigned bridge = 0;
        std::uint64_t poseId = 0;
        ULONGLONG takenMs = 0;
    };

    // Bf4EmuHost only: FVR_BF4_WORLD_UNTRACKED=1 sends frames on the world lane without a
    // camera (the emulator has no renderer), to test transport and crops end to end. Read
    // from the process environment block, so the emulator can switch it per phase.
    void RefreshUntracked(ULONGLONG now) {
        if (now - untrackedCheckedMs_ < 250) return;
        untrackedCheckedMs_ = now;
        char v[4] = {};
        untracked_ = GetEnvironmentVariableA("FVR_BF4_WORLD_UNTRACKED", v, sizeof(v)) == 1 && v[0] == '1';
    }

    // The oldest queued lease as the camera's request.
    void FillWorld(WorldRequest& w) const {
        if (queue_.empty()) return;
        const Lease& front = queue_.front();
        w.valid = true;
        w.poseId = front.poseId;
        w.head = front.lease.tracking.head;
        w.fov[0] = front.lease.tracking.fov[0];
        w.fov[1] = front.lease.tracking.fov[1];
        const auto& e = front.lease.tracking.eyes;
        const float dx = e[1].position.x - e[0].position.x, dy = e[1].position.y - e[0].position.y,
                    dz = e[1].position.z - e[0].position.z;
        w.halfEyeDistance = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz) * front.lease.tracking.worldUnitsPerMeter;
    }

    bool BridgeUsable(unsigned i) const { return bridges_[i] && bridges_[i]->Connected() && !bridges_[i]->Fatal(); }

    // A pipelined host gives its requests the longest lifetime (200 ms), since one waits
    // behind the other; a single channel keeps the 150 ms one.
    std::uint64_t LeaseMaxMs() const { return BridgeUsable(1) ? kWorldPipelinedLeaseMaxMs : kWorldLeaseMaxMs; }

    void PumpBridges() {
        for (auto& b : bridges_)
            if (b) b->PumpGraphics();
    }

    void TakeLeases() {
        Lease taken[kBridges];
        unsigned count = 0;
        for (unsigned i = 0; i < kBridges; ++i) {
            if (held_[i] || !BridgeUsable(i)) continue;
            Lease l;
            if (!bridges_[i]->TryBegin({kLeaseOwner, ++frameSerial_, deviceEpoch_}, l.lease)) continue;
            l.bridge = i;
            l.takenMs = GetTickCount64();
            taken[count++] = l;
        }
        // Two taken at once: serve the host's older request first.
        if (count == 2 && taken[1].lease.tracking.predictedNs < taken[0].lease.tracking.predictedNs) std::swap(taken[0], taken[1]);
        for (unsigned n = 0; n < count; ++n) {
            taken[n].poseId = ++nextPoseId_;
            held_[taken[n].bridge] = true;
            if (!queue_.empty()) ++queuedLeases_;
            queue_.push_back(taken[n]);
            ++leases_;
        }
    }

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
    // restored inside the submit call). Serves the oldest queued lease.
    bool CapturePair(ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer, const FrameMeta& meta) {
        if (!context || !backbuffer || !meta.width || !meta.height || queue_.empty()) return false;
        const Lease& front = queue_.front();
        auto& bridge = bridges_[front.bridge];
        if (!bridge) return false;
        const auto& req = front.lease.requirements;
        const auto& t = front.lease.tracking;
        const float frameAspect = float(meta.width) / float(meta.height);
        // The projection's own aspect: the frame's in mono, an eye half's in split stereo.
        const float aspect = meta.renderAspect > 0.2f && meta.renderAspect < 5.0f && !untracked_ ? meta.renderAspect : frameAspect;
        float fovY = meta.renderFovY;
        if (untracked_) fovY = RequiredVerticalFov(t.fov[0], t.fov[1], aspect);
        if (!(fovY > 0.05f && fovY < 3.1f)) return false;
        const float tanY = std::tan(fovY * 0.5f), tanX = tanY * aspect;
        EyeCrop crops[2];
        const math::FovTangents fov[2] = {t.fov[0], t.fov[1]};
        EyeCrops(fov, tanX, tanY, meta.stereoSplit && !untracked_, meta.stereoSeparation, crops);
        Reticle reticle;
        if (meta.reticle && meta.aimValid && !meta.stereoSplit && !untracked_)
            reticle = ReticleAt(meta.aimTanX, meta.aimTanY, tanX, tanY);
        EyeOverlay overlays[2];
        bool marked = false;
        for (int e = 0; e < 2; ++e) {
            overlays[e].spanX = t.fov[e].right - t.fov[e].left;
            overlays[e].spanY = t.fov[e].up - t.fov[e].down;
            overlays[e].fov = t.fov[e];
            overlays[e].vignette = untracked_ ? 0.0f : meta.vignette;
            overlays[e].hudInsets = meta.hudInsets && !untracked_ && !meta.stereoSplit;
            for (int m = 0; m < 2; ++m) {
                if (!meta.markers[m].show) continue;
                overlays[e].markers[m] = MarkerInEye(t.eyes[e], t.fov[e], meta.markers[m].position, meta.markers[m].kind);
                marked |= overlays[e].markers[m].show;
            }
        }
        DepthStereoInput depth;
        if (meta.depthStereo && meta.depth && !meta.stereoSplit && !untracked_) {
            depth.texture = meta.depth;
            depth.format = meta.depthFormat;
            depth.params = DepthStereoFor(meta.halfEyeDistance, tanX, meta.nearZ, meta.farZ, meta.depthClear);
        }
        if (!resampler_.Render(context, backbuffer, crops, req.width, req.height, req.format, reticle, overlays,
                               depth.params.enabled ? &depth : nullptr))
            return false;
        if (depth.params.enabled) ++depthPairs_;
        if (reticle.show) ++reticles_;
        if (marked) ++markedPairs_;
        if (!bridge->CaptureEye(0, resampler_.Eye(0), front.lease) || !bridge->CaptureEye(1, resampler_.Eye(1), front.lease) ||
            !bridge->PublishRestored(front.lease))
            return false;
        held_[front.bridge] = false;
        queue_.pop_front();
        ++pairs_;
        return true;
    }

    // CPU readback of a texture (one-off evidence; stalls the GPU once).
    template <typename Use>
    static bool WithPixels(ID3D11DeviceContext* context, ID3D11Texture2D* texture, Use&& use) {
        D3D11_TEXTURE2D_DESC d{};
        texture->GetDesc(&d);
        if (d.SampleDesc.Count != 1) return false;
        const DXGI_FORMAT f = d.Format;
        const bool rgba = f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_TYPELESS;
        const bool bgra = f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_TYPELESS;
        if (!rgba && !bgra) return false;
        ID3D11Device* device = nullptr;
        texture->GetDevice(&device);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = 0;
        ID3D11Texture2D* staging = nullptr;
        bool ok = device && SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging));
        if (ok) {
            context->CopyResource(staging, texture);
            D3D11_MAPPED_SUBRESOURCE m{};
            ok = SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &m));
            if (ok) {
                use(stereo::ImageView{static_cast<const std::uint8_t*>(m.pData), static_cast<int>(d.Width),
                                      static_cast<int>(d.Height), static_cast<int>(m.RowPitch), rgba});
                context->Unmap(staging, 0);
            }
            staging->Release();
        }
        if (device) device->Release();
        return ok;
    }

    void WriteSnapshot(ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer) {
        WithPixels(context, backbuffer, [&](const stereo::ImageView& full) {
            stereo::WriteSideBySideBmp(snapshotDir_ + "\\vr-snapshot-frame.bmp", stereo::LeftHalf(full), stereo::RightHalf(full), 2);
        });
        ID3D11Texture2D* left = resampler_.Eye(0);
        ID3D11Texture2D* right = resampler_.Eye(1);
        if (!left || !right) return;
        WithPixels(context, left, [&](const stereo::ImageView& l) {
            WithPixels(context, right, [&](const stereo::ImageView& r) {
                stereo::WriteSideBySideBmp(snapshotDir_ + "\\vr-snapshot-eyes.bmp", l, r, 2);
            });
        });
    }

    // The oldest lease could not be met (too old, or its capture failed): hand it back. The
    // next queued one, if any, becomes the camera's pose at the next frame.
    void CancelFront() {
        if (queue_.empty()) return;
        const Lease& front = queue_.front();
        if (bridges_[front.bridge]) bridges_[front.bridge]->Cancel(front.lease);
        held_[front.bridge] = false;
        queue_.pop_front();
        ++cancels_;
    }

    void CancelLeases() {
        while (!queue_.empty()) CancelFront();
    }

    // Frame lane: the bridges need the game's device, so they connect on the graphics thread
    // at the first Present after the host tokens are known (both hooks run on that thread).
    void EnsureBridges(ID3D11Device* device) {
        if (!device) return;
        const std::string* tokens[kBridges] = {&token_, &token2_};
        for (unsigned i = 0; i < kBridges; ++i) {
            if (bridgeTokens_[i] == *tokens[i]) continue;
            CancelLeases(); // queue order spans both bridges
            if (bridges_[i]) bridges_[i]->CloseGraphics();
            bridges_[i].reset();
            bridgeTokens_[i] = *tokens[i];
            if (tokens[i]->empty()) continue;
            bridges_[i] = std::make_unique<graphics::D3D11FrameBridge>(); // failures are sticky per bridge
            const std::wstring wide(tokens[i]->begin(), tokens[i]->end());
            ++deviceEpoch_;
            if (!bridges_[i]->ConnectGraphics(device, wide)) bridges_[i].reset();
        }
    }

    static std::string ReadToken(const std::string& path) {
        std::string token;
        if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
            char buf[256];
            const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
            std::fclose(f);
            for (std::size_t i = 0; i < n; ++i)
                if (buf[i] > ' ' && buf[i] < 127) token += buf[i]; // tolerate UTF-16 or trailing newline
        }
        return token;
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
        const std::string token = ReadToken(tokenFile_);
        if (token.empty() || token == token_) return false; // nothing new to attach to
        channel_.Close();
        producer_.Reset();
        const std::wstring wide(token.begin(), token.end());
        if (!channel_.ConnectProducer(wide)) return false;
        token_ = token;
        // Written by the host before the main token file, so it is there now if it exists.
        token2_ = ReadToken(secondTokenFile_);
        closed_ = false;
        world_ = false;
        ++epoch_; // a new host connection starts a new pointer epoch
        return true;
    }

    std::mutex mutex_;
    std::string tokenFile_, secondTokenFile_;
    ipc::MenuChannel channel_;
    graphics::D3D11MenuProducer producer_;
    std::unique_ptr<graphics::D3D11FrameBridge> bridges_[kBridges];
    std::string bridgeTokens_[kBridges]; // the host channel each bridge is attached to
    bool held_[kBridges] = {};           // the bridge has a lease in the queue
    std::deque<Lease> queue_;            // oldest host request first
    EyeResampler resampler_;
    WorldModeGate gate_;
    bool world_ = false, untracked_ = false;
    std::uint64_t nextPoseId_ = 0, frameSerial_ = 0, deviceEpoch_ = 0;
    std::uint64_t reticles_ = 0;    // pairs drawn with the aim reticle
    std::uint64_t markedPairs_ = 0; // pairs drawn with a gesture marker
    std::uint64_t depthPairs_ = 0;  // pairs given depth stereo
    std::uint64_t leases_ = 0, queuedLeases_ = 0, pairs_ = 0, stereoPairs_ = 0, cancels_ = 0, captureFailures_ = 0;
    ULONGLONG untrackedCheckedMs_ = 0;
    std::string snapshotDir_;
    std::uint64_t sequence_ = 0, epoch_ = 0, lastToggle_ = 0, statesPublished_ = 0;
    ULONGLONG lastAttempt_ = 0;
    std::string token_;  // the host session we are attached to
    std::string token2_; // the host's second frame channel, empty with one channel
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
    void RequestSnapshot(const std::string& dir) override { link_.RequestSnapshot(dir); }
    bool Haptic(const interaction::FeedbackEvent& event) override { return link_.PublishFeedback(event); }
    bool CurrentWorld(WorldRequest& out) override { return link_.CurrentWorld(out); }

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
