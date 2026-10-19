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

#include "fvr/graphics/D3D11MenuProducer.h"
#include "fvr/ipc/FrameChannel.h"
#include "fvr/ipc/MenuChannel.h"

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
// Theater link: the whole game screen travels to the XR host on the existing menu lane
// (MenuChannel + D3D11MenuProducer, the BC2 menu transport), shown there as one quad
// with the host's laser pointer; the host's MenuControl comes back as the game pointer.
// The host writes its channel token to <log_dir>\xr-channel.txt; the link connects
// whenever that file appears (the host may start after the game).
class TheaterLink {
public:
    explicit TheaterLink(std::string tokenFile) : tokenFile_(std::move(tokenFile)) {}

    void Publish(ID3D11Texture2D* backbuffer, std::uint32_t width, std::uint32_t height) {
        std::lock_guard lock(mutex_);
        if (!Connect()) return;
        ipc::MenuState s{};
        s.sequence = ++sequence_;
        s.epoch = epoch_;
        s.mode = ipc::MenuMode::Menu; // theater: the game screen is the panel
        // The host's pointer is verified for a 16:9 surface labelled 1280x720 and returns
        // normalised u,v; the adapter maps those to client pixels itself. Other aspects
        // stay visible there but unpointable, matching the host's evidence rule.
        const bool wide169 = height && std::uint64_t(width) * 9 == std::uint64_t(height) * 16;
        s.logicalWidth = wide169 ? 1280 : width;
        s.logicalHeight = wide169 ? 720 : height;
        s.inputReady = 1;
        if (channel_.PublishState(s) == ipc::ChannelResult::Ok) ++statesPublished_;
        producer_.Pump(backbuffer, channel_);
    }

    std::string Describe() {
        std::lock_guard lock(mutex_);
        char line[200];
        std::snprintf(line, sizeof(line), "theater_link connected=%d epoch=%llu states=%llu captured=%llu published=%llu failed=%llu discarded=%llu\n",
                      channel_.Connected() ? 1 : 0, static_cast<unsigned long long>(epoch_),
                      static_cast<unsigned long long>(statesPublished_), static_cast<unsigned long long>(producer_.Captured()),
                      static_cast<unsigned long long>(producer_.Published()), static_cast<unsigned long long>(producer_.Failed()),
                      static_cast<unsigned long long>(producer_.Discarded()));
        return line;
    }

    void Release() {
        std::lock_guard lock(mutex_);
        producer_.Reset(); // swapchain buffers are about to change
    }

    bool Poll(ControllerSnapshot& out) {
        std::lock_guard lock(mutex_);
        out = {};
        if (!Connect()) return false;
        // Controller sample for the virtual gamepad (host publishes one per XR frame); a
        // stale or invalid sample leaves `controls` unfocused, i.e. a neutral pad.
        interaction::InputFrame f{};
        if (input_.ReadInput(f) == ipc::ChannelResult::Ok && interaction::ValidInput(f)) out.controls = f;
        ipc::MenuControl c{};
        if (channel_.ReadControl(c) != ipc::ChannelResult::Ok) return out.controls.focused;
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
    bool Connect() {
        if (channel_.Connected()) return true;
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
        if (token.empty()) return false;
        channel_.Close();
        input_.Close();
        producer_.Reset();
        const std::wstring wide(token.begin(), token.end());
        if (!channel_.ConnectProducer(wide)) return false;
        // Same token: the frame channel carries the controller samples. The adapter never
        // takes frame requests (the host stops asking while the theater panel is up).
        input_.ConnectProducer(wide);
        ++epoch_; // a new host connection starts a new pointer epoch
        return true;
    }

    std::mutex mutex_;
    std::string tokenFile_;
    ipc::MenuChannel channel_;
    ipc::FrameChannel input_;
    graphics::D3D11MenuProducer producer_;
    std::uint64_t sequence_ = 0, epoch_ = 0, lastToggle_ = 0, statesPublished_ = 0;
    ULONGLONG lastAttempt_ = 0;
};

TheaterLink* g_link = nullptr; // one per adapter process; lives until exit

TheaterLink& Link(const SessionInfo& session) {
    if (!g_link) g_link = new TheaterLink(session.logDir + "\\xr-channel.txt");
    return *g_link;
}

class TheaterFrameSink final : public IFrameSink {
public:
    explicit TheaterFrameSink(TheaterLink& link) : link_(link) {}
    bool Publish(ID3D11Device*, ID3D11DeviceContext*, ID3D11Texture2D* backbuffer, const FrameMeta& meta) override {
        if (meta.eye > 0) return true; // the theater quad is mono: left eye / mono frame only
        link_.Publish(backbuffer, meta.width, meta.height);
        return true;
    }
    void ReleaseSwapchainResources() override { link_.Release(); }
    bool PublishTexture(ID3D11Texture2D* texture, std::uint32_t width, std::uint32_t height) override {
        link_.Publish(texture, width, height);
        return true;
    }
    std::string Describe() const override { return link_.Describe(); }

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
