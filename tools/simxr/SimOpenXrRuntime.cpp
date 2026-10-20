// Developer-only simulated OpenXR runtime: a headset stand-in for live game sessions.
//
// The XR host loads it through the developer "xr_loader" setting (it is never registered
// as the system runtime). Unlike the test fixtures it has no scripted end: it paces frames
// at 90 Hz on the QPC clock until the host or the control file asks it to stop.
//
// Control directory: %FVR_SIMXR_DIR% (default: the host's working directory). The file
// simxr.txt there is re-read whenever it changes; each line sets part of the state:
//   head <yaw> <pitch> <roll> <x> <y> <z>      degrees (yaw to the right, pitch up), metres
//   left|right <x> <y> <z> <yaw> <pitch> <roll> controller grip and aim pose (stage space)
//   left|right input <trigger> <squeeze> <stickX> <stickY> <a> <b> <stickClick> <menu>
//   sway <0|1>                                 gentle head motion (default on)
//   snap <id> <name>                           once per new id: writes <name>-eyes.bmp (the
//                                              projection layer's eyes side by side) and
//                                              <name>-quad<n>.bmp for each quad layer
//   stop <0|1>                                 ends the session (as does a file "simxr-stop")
//   sig <A|B> <id>                             once per new id: the next eye pair's signature
//                                              (luminance of three rows of the left eye)
//   flip <yawA> <yawB> | flip off              head yaw alternates every frame (no sway); each
//                                              new pair is then classified by its signature
//                                              against its pose label: the pixels must show the
//                                              pose they are labelled with (else the world swims)
// simxr-ack.txt there reports the frame counts about once a second and each snapshot.
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr_platform.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr float kPi = 3.14159265f;
constexpr XrTime kPeriodNs = 11111111; // 90 Hz
constexpr float kIpd = 0.064f;
constexpr std::uint32_t kEyeWidth = 1440, kEyeHeight = 1584;

// ---- clock -------------------------------------------------------------------------
std::int64_t QpcNs() {
    LARGE_INTEGER q{}, f{};
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return (q.QuadPart / f.QuadPart) * 1000000000 + (q.QuadPart % f.QuadPart) * 1000000000 / f.QuadPart;
}

// ---- pose math (OpenXR: right-handed, +Y up, -Z forward) ----------------------------
XrQuaternionf Mul(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v) {
    const XrQuaternionf p{v.x, v.y, v.z, 0};
    const XrQuaternionf c{-q.x, -q.y, -q.z, q.w};
    const XrQuaternionf r = Mul(Mul(q, p), c);
    return {r.x, r.y, r.z};
}
XrQuaternionf Axis(float x, float y, float z, float radians) {
    const float s = std::sin(radians * 0.5f);
    return {x * s, y * s, z * s, std::cos(radians * 0.5f)};
}
// Yaw to the right, pitch up, roll clockwise (degrees).
XrQuaternionf Orientation(float yaw, float pitch, float roll) {
    const float d = kPi / 180.0f;
    return Mul(Mul(Axis(0, 1, 0, -yaw * d), Axis(1, 0, 0, pitch * d)), Axis(0, 0, 1, -roll * d));
}
XrPosef Compose(const XrPosef& parent, const XrPosef& child) {
    XrPosef out{};
    out.orientation = Mul(parent.orientation, child.orientation);
    const XrVector3f p = Rotate(parent.orientation, child.position);
    out.position = {parent.position.x + p.x, parent.position.y + p.y, parent.position.z + p.z};
    return out;
}
XrPosef Inverse(const XrPosef& pose) {
    XrPosef out{};
    out.orientation = {-pose.orientation.x, -pose.orientation.y, -pose.orientation.z, pose.orientation.w};
    const XrVector3f p = Rotate(out.orientation, pose.position);
    out.position = {-p.x, -p.y, -p.z};
    return out;
}

// ---- simulated state ------------------------------------------------------------------
struct Hand {
    float pose[6]; // x y z yaw pitch roll
    float trigger = 0, squeeze = 0, stickX = 0, stickY = 0;
    bool a = false, b = false, stickClick = false, menu = false;
};
struct Control {
    float head[6] = {0, 0, 0, 0, 1.7f, 0}; // yaw pitch roll x y z
    Hand hands[2] = {{{-0.25f, 1.0f, -0.1f, 0, -60, 0}}, {{0.15f, 1.4f, -0.4f, 0, 0, 0}}};
    bool sway = true, stop = false;
    std::string snapId, snapName;
    std::string sigId, sigSlot;
    bool flip = false;
    float flipYaw[2] = {0, 0};
};
constexpr int kSigBins = 64, kSigRows = 3;
using Signature = std::array<float, kSigBins * kSigRows>;
Signature g_sig[2]{};
bool g_haveSig[2]{};
std::string g_doneSig;
bool g_sigPending = false;
Signature g_lastStrip{};
bool g_haveLastStrip = false;
std::uint64_t g_flipPairs = 0, g_flipMatch = 0, g_flipMismatch = 0, g_flipAmbiguous = 0;

std::wstring g_dir;
Control g_control;
FILETIME g_controlStamp{};
std::int64_t g_lastPollNs = 0, g_startNs = 0, g_nextFrameNs = 0;
std::string g_doneSnap;
bool g_firstParse = true; // the file's one-shot lines (snap, sig) at start are history
bool g_snapPending = false;
unsigned g_snapWait = 0;

ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
struct Chain {
    std::vector<ComPtr<ID3D11Texture2D>> textures;
    std::int64_t format = 0;
    int acquired = -1, released = -1;
    unsigned next = 0;
};
std::map<std::uintptr_t, Chain> g_chains;
std::uintptr_t g_nextChain = 0x1000;

std::map<std::string, XrPath> g_paths;
std::map<XrPath, std::string> g_pathNames;
std::map<std::uintptr_t, std::string> g_actions;                         // action -> name
std::map<std::uintptr_t, std::pair<std::string, std::string>> g_spaces; // space -> (action, subaction path)
std::uintptr_t g_nextHandle = 0x2000;
std::deque<XrSessionState> g_events;
bool g_running = false, g_exitRequested = false;
std::uint64_t g_frames = 0, g_projection = 0, g_quads = 0, g_haptics = 0, g_snaps = 0;
std::uint32_t g_lastEyeW = 0, g_lastEyeH = 0;
std::int64_t g_lastAckNs = 0;

std::wstring ControlPath(const wchar_t* name) { return g_dir + L"\\" + name; }

void WriteAck(const std::string& extra = {}) {
    std::ofstream ack(ControlPath(L"simxr-ack.txt"), std::ios::trunc);
    ack << "frames=" << g_frames << " projection_layers=" << g_projection << " quad_layers=" << g_quads
        << " haptics=" << g_haptics << " eye=" << g_lastEyeW << "x" << g_lastEyeH << " snaps=" << g_snaps
        << " running=" << (g_running ? 1 : 0) << "\n";
    ack << "flip=" << (g_control.flip ? 1 : 0) << " sigA=" << g_haveSig[0] << " sigB=" << g_haveSig[1]
        << " pairs=" << g_flipPairs << " match=" << g_flipMatch << " mismatch=" << g_flipMismatch
        << " ambiguous=" << g_flipAmbiguous << "\n";
    if (!extra.empty()) ack << extra << "\n";
}

void ParseControl(const std::string& text) {
    Control c = g_control;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream in(line);
        std::string key;
        if (!(in >> key) || key[0] == '#') continue;
        if (key == "head") {
            for (float& v : c.head) in >> v;
        } else if (key == "left" || key == "right") {
            Hand& h = c.hands[key == "right" ? 1 : 0];
            std::string next;
            std::streampos at = in.tellg();
            if (in >> next && next == "input") {
                int a = 0, b = 0, click = 0, menu = 0;
                in >> h.trigger >> h.squeeze >> h.stickX >> h.stickY >> a >> b >> click >> menu;
                h.a = a != 0;
                h.b = b != 0;
                h.stickClick = click != 0;
                h.menu = menu != 0;
            } else {
                in.clear();
                in.seekg(at);
                for (float& v : h.pose) in >> v;
            }
        } else if (key == "sway") {
            int on = 1;
            in >> on;
            c.sway = on != 0;
        } else if (key == "snap") {
            in >> c.snapId >> c.snapName;
        } else if (key == "sig") {
            in >> c.sigSlot >> c.sigId;
        } else if (key == "flip") {
            std::string a;
            in >> a;
            c.flip = a != "off" && !a.empty();
            if (c.flip) {
                c.flipYaw[0] = std::strtof(a.c_str(), nullptr);
                in >> c.flipYaw[1];
            }
        } else if (key == "stop") {
            int on = 0;
            in >> on;
            c.stop = on != 0;
        }
    }
    auto clamp = [](float& v, float lo, float hi) { v = std::isfinite(v) ? (v < lo ? lo : v > hi ? hi : v) : 0.0f; };
    for (Hand& h : c.hands) {
        clamp(h.trigger, 0, 1);
        clamp(h.squeeze, 0, 1);
        clamp(h.stickX, -1, 1);
        clamp(h.stickY, -1, 1);
    }
    g_control = c;
    if (g_firstParse) { // one-shot requests already in the file belong to an earlier session
        g_firstParse = false;
        g_doneSnap = c.snapId;
        g_doneSig = c.sigId;
    }
    if (!c.sigId.empty() && c.sigId != g_doneSig) g_sigPending = true;
    if (!c.snapId.empty() && c.snapId != g_doneSnap) {
        g_snapPending = true;
        g_snapWait = 0;
    }
}

void PollControl() {
    const std::int64_t now = QpcNs();
    if (now - g_lastPollNs < 100000000) return;
    g_lastPollNs = now;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (GetFileAttributesExW(ControlPath(L"simxr.txt").c_str(), GetFileExInfoStandard, &attributes) &&
        CompareFileTime(&attributes.ftLastWriteTime, &g_controlStamp) != 0) {
        g_controlStamp = attributes.ftLastWriteTime;
        std::ifstream file(ControlPath(L"simxr.txt"));
        std::stringstream text;
        text << file.rdbuf();
        ParseControl(text.str());
    }
    const bool stopFile = GetFileAttributesW(ControlPath(L"simxr-stop").c_str()) != INVALID_FILE_ATTRIBUTES;
    if ((g_control.stop || stopFile) && g_running && !g_exitRequested) {
        g_exitRequested = true;
        g_events.push_back(XR_SESSION_STATE_STOPPING);
    }
    if (now - g_lastAckNs > 1000000000) {
        g_lastAckNs = now;
        WriteAck();
    }
}

// Head pose in stage space at an XrTime (the QPC clock in ns).
XrPosef HeadAt(XrTime time) {
    const float t = float(double(time - g_startNs) * 1e-9);
    float yaw = g_control.head[0], pitch = g_control.head[1];
    float y = g_control.head[4];
    if (g_control.flip) {
        yaw = g_control.flipYaw[((time - g_startNs) / kPeriodNs) & 1];
    } else if (g_control.sway) {
        yaw += 5.0f * std::sin(2 * kPi * t / 8.0f);
        pitch += 2.0f * std::sin(2 * kPi * t / 5.3f);
        y += 0.01f * std::sin(2 * kPi * t / 3.1f);
    }
    XrPosef pose{};
    pose.orientation = Orientation(yaw, pitch, g_control.head[2]);
    pose.position = {g_control.head[3], y, g_control.head[5]};
    return pose;
}
XrPosef EyeAt(XrTime time, unsigned eye) {
    XrPosef offset{{0, 0, 0, 1}, {eye ? kIpd * 0.5f : -kIpd * 0.5f, 0, 0}};
    return Compose(HeadAt(time), offset);
}
XrPosef HandAt(unsigned hand) {
    const float* p = g_control.hands[hand].pose;
    XrPosef pose{};
    pose.orientation = Orientation(p[3], p[4], p[5]);
    pose.position = {p[0], p[1], p[2]};
    return pose;
}
// Spaces are stage-relative; LOCAL starts at the default head height.
XrPosef InBase(const XrPosef& stagePose, XrSpace base, XrTime time) {
    const auto type = XrReferenceSpaceType(reinterpret_cast<std::uintptr_t>(base) - 20);
    if (type == XR_REFERENCE_SPACE_TYPE_LOCAL) {
        XrPosef p = stagePose;
        p.position.y -= 1.7f;
        return p;
    }
    if (type == XR_REFERENCE_SPACE_TYPE_VIEW) return Compose(Inverse(HeadAt(time)), stagePose);
    return stagePose;
}

// ---- snapshots -------------------------------------------------------------------------
bool ReadRect(const Chain& chain, const XrSwapchainSubImage& sub, std::vector<std::uint8_t>& bgra, std::uint32_t& w,
              std::uint32_t& h) {
    if (chain.released < 0 || chain.released >= int(chain.textures.size())) return false;
    ID3D11Texture2D* texture = chain.textures[chain.released].Get();
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                      d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
    const bool bgraFormat = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                            d.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS;
    if (!rgba && !bgraFormat) return false;
    const UINT slice = sub.imageArrayIndex < d.ArraySize ? sub.imageArrayIndex : 0;
    const UINT mips = d.MipLevels;
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    d.MipLevels = 1;
    d.ArraySize = 1;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(g_device->CreateTexture2D(&d, nullptr, &staging))) return false;
    g_context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture, D3D11CalcSubresource(0, slice, mips), nullptr);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    const int x0 = sub.imageRect.offset.x, y0 = sub.imageRect.offset.y;
    w = std::uint32_t(sub.imageRect.extent.width);
    h = std::uint32_t(sub.imageRect.extent.height);
    if (x0 < 0 || y0 < 0 || x0 + w > d.Width || y0 + h > d.Height || !w || !h) {
        g_context->Unmap(staging.Get(), 0);
        return false;
    }
    bgra.resize(std::size_t(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(m.pData) + std::size_t(y0 + y) * m.RowPitch + std::size_t(x0) * 4;
        std::uint8_t* out = &bgra[std::size_t(y) * w * 4];
        std::memcpy(out, row, std::size_t(w) * 4);
        if (rgba)
            for (std::uint32_t x = 0; x < w; ++x) std::swap(out[x * 4], out[x * 4 + 2]);
        for (std::uint32_t x = 0; x < w; ++x) out[x * 4 + 3] = 255;
    }
    g_context->Unmap(staging.Get(), 0);
    return true;
}

bool WriteBmp(const std::wstring& path, const std::vector<std::uint8_t>& bgra, std::uint32_t w, std::uint32_t h) {
    std::FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    const std::uint32_t size = w * h * 4;
    std::uint8_t header[54] = {'B', 'M'};
    auto put32 = [&](int at, std::uint32_t value) { std::memcpy(header + at, &value, 4); };
    put32(2, 54 + size);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, static_cast<std::uint32_t>(-static_cast<std::int32_t>(h)));
    header[26] = 1;
    header[28] = 32;
    put32(34, size);
    std::fwrite(header, 1, sizeof(header), f);
    std::fwrite(bgra.data(), 1, bgra.size(), f);
    return std::fclose(f) == 0;
}

std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }

void Snapshot(const XrFrameEndInfo* end) {
    std::string written;
    unsigned quadIndex = 0;
    bool any = false;
    for (std::uint32_t i = 0; i < end->layerCount; ++i) {
        const auto* layer = end->layers[i];
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            const auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
            if (p->viewCount != 2) continue;
            std::vector<std::uint8_t> eyes[2];
            std::uint32_t w[2]{}, h[2]{};
            bool ok = true;
            for (unsigned e = 0; e < 2 && ok; ++e) {
                const auto it = g_chains.find(reinterpret_cast<std::uintptr_t>(p->views[e].subImage.swapchain));
                ok = it != g_chains.end() && ReadRect(it->second, p->views[e].subImage, eyes[e], w[e], h[e]);
            }
            if (!ok) continue;
            const std::uint32_t W = w[0] + w[1], H = h[0] > h[1] ? h[0] : h[1];
            std::vector<std::uint8_t> both(std::size_t(W) * H * 4, 0);
            for (unsigned e = 0; e < 2; ++e)
                for (std::uint32_t y = 0; y < h[e]; ++y)
                    std::memcpy(&both[(std::size_t(y) * W + (e ? w[0] : 0)) * 4], &eyes[e][std::size_t(y) * w[e] * 4],
                                std::size_t(w[e]) * 4);
            const std::string name = g_control.snapName + "-eyes.bmp";
            if (WriteBmp(ControlPath(Wide(name).c_str()), both, W, H)) written += " " + name, any = true;
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            const auto* q = reinterpret_cast<const XrCompositionLayerQuad*>(layer);
            const auto it = g_chains.find(reinterpret_cast<std::uintptr_t>(q->subImage.swapchain));
            std::vector<std::uint8_t> pixels;
            std::uint32_t w = 0, h = 0;
            if (it == g_chains.end() || !ReadRect(it->second, q->subImage, pixels, w, h)) continue;
            const std::string name = g_control.snapName + "-quad" + std::to_string(quadIndex++) + ".bmp";
            char where[160];
            std::snprintf(where, sizeof(where), "(%.2f %.2f %.2f, %.2fx%.2f m)", q->pose.position.x, q->pose.position.y,
                          q->pose.position.z, q->size.width, q->size.height);
            if (WriteBmp(ControlPath(Wide(name).c_str()), pixels, w, h)) written += " " + name + where, any = true;
        }
    }
    if (any || ++g_snapWait > 90) {
        g_snapPending = false;
        g_doneSnap = g_control.snapId;
        ++g_snaps;
        WriteAck("snap " + g_control.snapId + (any ? written : std::string(" nothing submitted")));
    }
}

// ---- pose-label check ------------------------------------------------------------------------
// Luminance of three rows of an eye image, 64 bins each (a cheap fingerprint of the view).
bool Strip(const Chain& chain, const XrSwapchainSubImage& sub, Signature& out) {
    if (chain.released < 0 || chain.released >= int(chain.textures.size())) return false;
    ID3D11Texture2D* texture = chain.textures[chain.released].Get();
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                      d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
    const UINT x0 = UINT(sub.imageRect.offset.x), y0 = UINT(sub.imageRect.offset.y);
    const UINT w = UINT(sub.imageRect.extent.width), h = UINT(sub.imageRect.extent.height);
    if (!w || !h || x0 + w > d.Width || y0 + h > d.Height) return false;
    static ComPtr<ID3D11Texture2D> staging;
    static D3D11_TEXTURE2D_DESC stagingDesc{};
    if (!staging || stagingDesc.Width != w || stagingDesc.Format != d.Format) {
        stagingDesc = {};
        stagingDesc.Width = w;
        stagingDesc.Height = kSigRows;
        stagingDesc.MipLevels = stagingDesc.ArraySize = 1;
        stagingDesc.Format = d.Format;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging.Reset();
        if (FAILED(g_device->CreateTexture2D(&stagingDesc, nullptr, &staging))) return false;
    }
    const float rows[kSigRows] = {0.35f, 0.5f, 0.65f};
    for (int r = 0; r < kSigRows; ++r) {
        const UINT y = y0 + UINT(rows[r] * float(h));
        const D3D11_BOX box{x0, y, 0, x0 + w, y + 1, 1};
        g_context->CopySubresourceRegion(staging.Get(), 0, 0, UINT(r), 0, texture, 0, &box);
    }
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(g_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    out.fill(0);
    for (int r = 0; r < kSigRows; ++r) {
        const auto* row = static_cast<const std::uint8_t*>(m.pData) + std::size_t(r) * m.RowPitch;
        for (UINT x = 0; x < w; ++x) {
            const std::uint8_t* p = row + x * 4;
            const float red = rgba ? p[0] : p[2], blue = rgba ? p[2] : p[0];
            out[std::size_t(r) * kSigBins + std::size_t(x) * kSigBins / w] += 0.2126f * red + 0.7152f * p[1] + 0.0722f * blue;
        }
    }
    g_context->Unmap(staging.Get(), 0);
    for (float& v : out) v /= float(w) / float(kSigBins);
    return true;
}

float YawRightDegrees(const XrQuaternionf& q) {
    const XrVector3f f = Rotate(q, {0, 0, -1});
    return std::atan2(f.x, -f.z) * 180.0f / kPi;
}
float AngleApart(float a, float b) {
    float d = std::fmod(std::fabs(a - b), 360.0f);
    return d > 180.0f ? 360.0f - d : d;
}

void Classify(const XrFrameEndInfo* end) {
    for (std::uint32_t i = 0; i < end->layerCount; ++i) {
        if (end->layers[i]->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) continue;
        const auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(end->layers[i]);
        if (p->viewCount != 2) continue;
        const auto it = g_chains.find(reinterpret_cast<std::uintptr_t>(p->views[0].subImage.swapchain));
        Signature s{};
        if (it == g_chains.end() || !Strip(it->second, p->views[0].subImage, s)) return;
        if (g_sigPending) {
            const int slot = g_control.sigSlot == "B" ? 1 : 0;
            g_sig[slot] = s;
            g_haveSig[slot] = true;
            g_doneSig = g_control.sigId;
            g_sigPending = false;
            WriteAck("sig " + g_control.sigSlot + " " + g_control.sigId);
        }
        const bool repeat = g_haveLastStrip && s == g_lastStrip; // the retained pair again
        g_lastStrip = s;
        g_haveLastStrip = true;
        if (!g_control.flip || !g_haveSig[0] || !g_haveSig[1] || repeat) return;
        const float yaw = YawRightDegrees(p->views[0].pose.orientation);
        const int label = AngleApart(yaw, g_control.flipYaw[0]) <= AngleApart(yaw, g_control.flipYaw[1]) ? 0 : 1;
        float d[2] = {0, 0};
        for (int k = 0; k < 2; ++k)
            for (std::size_t n = 0; n < s.size(); ++n) d[k] += std::fabs(s[n] - g_sig[k][n]);
        ++g_flipPairs;
        if (std::fabs(d[0] - d[1]) < 0.1f * std::fmax(d[0], d[1])) ++g_flipAmbiguous;
        else if ((d[0] < d[1] ? 0 : 1) == label) ++g_flipMatch;
        else ++g_flipMismatch;
        return;
    }
}
// ---- OpenXR entry points -------------------------------------------------------------------
XrInstance Instance() { return reinterpret_cast<XrInstance>(std::uintptr_t{1}); }
XrSession Session() { return reinterpret_cast<XrSession>(std::uintptr_t{2}); }

XrResult XRAPI_CALL EnumerateInstanceExtensionProperties(const char*, uint32_t capacity, uint32_t* count,
                                                         XrExtensionProperties* out) {
    *count = 1;
    if (capacity >= 1) {
        strcpy_s(out[0].extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
        out[0].extensionVersion = 1;
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL CreateInstance(const XrInstanceCreateInfo*, XrInstance* out) {
    wchar_t dir[MAX_PATH]{};
    const DWORD n = GetEnvironmentVariableW(L"FVR_SIMXR_DIR", dir, MAX_PATH);
    if (n && n < MAX_PATH) g_dir = dir;
    else {
        GetCurrentDirectoryW(MAX_PATH, dir);
        g_dir = dir;
    }
    g_startNs = QpcNs();
    g_control = Control{};
    g_controlStamp = {};
    g_firstParse = true;
    g_lastPollNs = 0;
    *out = Instance();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroyInstance(XrInstance) {
    WriteAck("instance destroyed");
    return XR_SUCCESS;
}
XrResult XRAPI_CALL GetInstanceProperties(XrInstance, XrInstanceProperties* p) {
    strcpy_s(p->runtimeName, "FVR SIMULATED HEADSET - developer, no headset");
    p->runtimeVersion = 1;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL GetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId* out) {
    *out = 1;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL GetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* p) {
    strcpy_s(p->systemName, "Simulated headset (90 Hz)");
    return XR_SUCCESS;
}
XrResult XRAPI_CALL GetD3D11GraphicsRequirementsKHR(XrInstance, XrSystemId, XrGraphicsRequirementsD3D11KHR* r) {
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumAdapters1(0, &adapter)) ||
        FAILED(adapter->GetDesc1(&desc)))
        return XR_ERROR_RUNTIME_FAILURE;
    r->adapterLuid = desc.AdapterLuid;
    r->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL EnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity,
                                                    uint32_t* count, XrViewConfigurationView* out) {
    *count = 2;
    if (capacity >= 2)
        for (unsigned i = 0; i < 2; ++i) {
            out[i].recommendedImageRectWidth = kEyeWidth;
            out[i].recommendedImageRectHeight = kEyeHeight;
            out[i].maxImageRectWidth = out[i].maxImageRectHeight = 4096;
            out[i].recommendedSwapchainSampleCount = out[i].maxSwapchainSampleCount = 1;
        }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL EnumerateEnvironmentBlendModes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity,
                                                   uint32_t* count, XrEnvironmentBlendMode* out) {
    *count = 1;
    if (capacity) out[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL CreateSession(XrInstance, const XrSessionCreateInfo* info, XrSession* out) {
    const auto* binding = static_cast<const XrGraphicsBindingD3D11KHR*>(info->next);
    if (!binding || binding->type != XR_TYPE_GRAPHICS_BINDING_D3D11_KHR || !binding->device) return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    g_device = binding->device;
    g_device->GetImmediateContext(&g_context);
    g_events.clear();
    g_events.push_back(XR_SESSION_STATE_IDLE);
    g_events.push_back(XR_SESSION_STATE_READY);
    g_running = g_exitRequested = false;
    *out = Session();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroySession(XrSession) {
    g_chains.clear();
    g_context.Reset();
    g_device.Reset();
    return XR_SUCCESS;
}
XrResult XRAPI_CALL BeginSession(XrSession, const XrSessionBeginInfo*) {
    g_running = true;
    g_nextFrameNs = 0;
    g_events.push_back(XR_SESSION_STATE_SYNCHRONIZED);
    g_events.push_back(XR_SESSION_STATE_VISIBLE);
    g_events.push_back(XR_SESSION_STATE_FOCUSED);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL EndSession(XrSession) {
    g_running = false;
    g_events.push_back(XR_SESSION_STATE_IDLE);
    g_events.push_back(XR_SESSION_STATE_EXITING);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL RequestExitSession(XrSession) {
    if (!g_exitRequested) {
        g_exitRequested = true;
        g_events.push_back(XR_SESSION_STATE_STOPPING);
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL PollEvent(XrInstance, XrEventDataBuffer* out) {
    PollControl();
    if (g_events.empty()) return XR_EVENT_UNAVAILABLE;
    XrEventDataSessionStateChanged e{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    e.session = Session();
    e.state = g_events.front();
    e.time = QpcNs();
    g_events.pop_front();
    std::memcpy(out, &e, sizeof(e));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL EnumerateReferenceSpaces(XrSession, uint32_t capacity, uint32_t* count, XrReferenceSpaceType* out) {
    *count = 3;
    if (capacity >= 3) {
        out[0] = XR_REFERENCE_SPACE_TYPE_VIEW;
        out[1] = XR_REFERENCE_SPACE_TYPE_LOCAL;
        out[2] = XR_REFERENCE_SPACE_TYPE_STAGE;
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL CreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo* info, XrSpace* out) {
    *out = reinterpret_cast<XrSpace>(std::uintptr_t(info->referenceSpaceType) + 20);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroySpace(XrSpace space) {
    g_spaces.erase(reinterpret_cast<std::uintptr_t>(space));
    return XR_SUCCESS;
}
constexpr XrSpaceLocationFlags kTracked = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                                          XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
XrResult XRAPI_CALL LocateSpace(XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out) {
    const auto key = reinterpret_cast<std::uintptr_t>(space);
    out->locationFlags = kTracked;
    const auto it = g_spaces.find(key);
    if (it != g_spaces.end()) {
        const unsigned hand = it->second.second == "/user/hand/right" ? 1 : 0;
        XrPosef pose = HandAt(hand);
        if (it->second.first == "aim_pose") pose = Compose(pose, XrPosef{{0, 0, 0, 1}, {0, 0, -0.05f}});
        out->pose = InBase(pose, base, time);
        return XR_SUCCESS;
    }
    const auto type = XrReferenceSpaceType(key - 20);
    if (type == XR_REFERENCE_SPACE_TYPE_VIEW) out->pose = InBase(HeadAt(time), base, time);
    else if (type == XR_REFERENCE_SPACE_TYPE_LOCAL) out->pose = InBase(XrPosef{{0, 0, 0, 1}, {0, 1.7f, 0}}, base, time);
    else out->pose = InBase(XrPosef{{0, 0, 0, 1}, {0, 0, 0}}, base, time);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL StringToPath(XrInstance, const char* text, XrPath* out) {
    auto [it, inserted] = g_paths.emplace(text, XrPath(g_paths.size() + 1));
    if (inserted) g_pathNames[it->second] = text;
    *out = it->second;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL CreateActionSet(XrInstance, const XrActionSetCreateInfo*, XrActionSet* out) {
    *out = reinterpret_cast<XrActionSet>(g_nextHandle++);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroyActionSet(XrActionSet) { return XR_SUCCESS; }
XrResult XRAPI_CALL CreateAction(XrActionSet, const XrActionCreateInfo* info, XrAction* out) {
    const auto key = g_nextHandle++;
    g_actions[key] = info->actionName;
    *out = reinterpret_cast<XrAction>(key);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL SuggestInteractionProfileBindings(XrInstance, const XrInteractionProfileSuggestedBinding*) { return XR_SUCCESS; }
XrResult XRAPI_CALL AttachSessionActionSets(XrSession, const XrSessionActionSetsAttachInfo*) { return XR_SUCCESS; }
XrResult XRAPI_CALL CreateActionSpace(XrSession, const XrActionSpaceCreateInfo* info, XrSpace* out) {
    const auto key = g_nextHandle++;
    g_spaces[key] = {g_actions[reinterpret_cast<std::uintptr_t>(info->action)], g_pathNames[info->subactionPath]};
    *out = reinterpret_cast<XrSpace>(key);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL SyncActions(XrSession, const XrActionsSyncInfo*) { return XR_SUCCESS; }

const Hand* HandFor(const XrActionStateGetInfo* info) {
    const auto it = g_pathNames.find(info->subactionPath);
    if (it == g_pathNames.end()) return nullptr;
    return &g_control.hands[it->second == "/user/hand/right" ? 1 : 0];
}
std::string ActionName(const XrActionStateGetInfo* info) { return g_actions[reinterpret_cast<std::uintptr_t>(info->action)]; }

XrResult XRAPI_CALL GetActionStatePose(XrSession, const XrActionStateGetInfo*, XrActionStatePose* out) {
    out->isActive = XR_TRUE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL GetActionStateFloat(XrSession, const XrActionStateGetInfo* info, XrActionStateFloat* out) {
    const Hand* h = HandFor(info);
    out->isActive = h ? XR_TRUE : XR_FALSE;
    const std::string name = ActionName(info);
    out->currentState = !h ? 0.0f : name == "trigger" ? h->trigger : name == "squeeze" ? h->squeeze : 0.0f;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL GetActionStateVector2f(XrSession, const XrActionStateGetInfo* info, XrActionStateVector2f* out) {
    const Hand* h = HandFor(info);
    out->isActive = h ? XR_TRUE : XR_FALSE;
    out->currentState = h ? XrVector2f{h->stickX, h->stickY} : XrVector2f{0, 0};
    return XR_SUCCESS;
}
XrResult XRAPI_CALL GetActionStateBoolean(XrSession, const XrActionStateGetInfo* info, XrActionStateBoolean* out) {
    const Hand* h = HandFor(info);
    out->isActive = h ? XR_TRUE : XR_FALSE;
    const std::string name = ActionName(info);
    bool v = false;
    if (h) {
        if (name == "primary") v = h->a;
        else if (name == "secondary") v = h->b;
        else if (name == "stick_click") v = h->stickClick;
        else if (name == "menu") v = h->menu;
        else if (name == "index_touch") v = h->trigger > 0.05f;
        else if (name == "thumb_touch") v = h->a || h->b || h->stickX != 0 || h->stickY != 0;
    }
    out->currentState = v ? XR_TRUE : XR_FALSE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL ApplyHapticFeedback(XrSession, const XrHapticActionInfo*, const XrHapticBaseHeader*) {
    ++g_haptics;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL StopHapticFeedback(XrSession, const XrHapticActionInfo*) { return XR_SUCCESS; }

XrResult XRAPI_CALL LocateViews(XrSession, const XrViewLocateInfo* info, XrViewState* state, uint32_t capacity, uint32_t* count,
                                XrView* out) {
    *count = 2;
    if (capacity < 2) return capacity ? XR_ERROR_SIZE_INSUFFICIENT : XR_SUCCESS;
    state->viewStateFlags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                            XR_VIEW_STATE_POSITION_TRACKED_BIT | XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
    // Quest 2-like per-eye fields of view (the outer side is wider).
    const float d = kPi / 180.0f;
    for (unsigned eye = 0; eye < 2; ++eye) {
        out[eye].pose = InBase(EyeAt(info->displayTime, eye), info->space, info->displayTime);
        out[eye].fov = eye ? XrFovf{-43 * d, 50 * d, 47 * d, -53 * d} : XrFovf{-50 * d, 43 * d, 47 * d, -53 * d};
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL EnumerateSwapchainFormats(XrSession, uint32_t capacity, uint32_t* count, int64_t* out) {
    const int64_t formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM,
                               DXGI_FORMAT_B8G8R8A8_UNORM};
    *count = 4;
    if (capacity >= 4) std::memcpy(out, formats, sizeof(formats));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL CreateSwapchain(XrSession, const XrSwapchainCreateInfo* info, XrSwapchain* out) {
    if (!g_device || info->width == 0 || info->height == 0 || info->width > 8192 || info->height > 8192) return XR_ERROR_VALIDATION_FAILURE;
    Chain chain;
    chain.format = info->format;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = info->width;
    d.Height = info->height;
    d.Format = DXGI_FORMAT(info->format);
    d.MipLevels = info->mipCount ? info->mipCount : 1;
    d.ArraySize = info->arraySize ? info->arraySize : 1;
    d.SampleDesc.Count = info->sampleCount ? info->sampleCount : 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 3; ++i) {
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(g_device->CreateTexture2D(&d, nullptr, &texture))) return XR_ERROR_RUNTIME_FAILURE;
        chain.textures.push_back(texture);
    }
    const auto key = g_nextChain++;
    g_chains[key] = std::move(chain);
    *out = reinterpret_cast<XrSwapchain>(key);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroySwapchain(XrSwapchain chain) {
    g_chains.erase(reinterpret_cast<std::uintptr_t>(chain));
    return XR_SUCCESS;
}
XrResult XRAPI_CALL EnumerateSwapchainImages(XrSwapchain handle, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* out) {
    const auto it = g_chains.find(reinterpret_cast<std::uintptr_t>(handle));
    if (it == g_chains.end()) return XR_ERROR_HANDLE_INVALID;
    *count = uint32_t(it->second.textures.size());
    if (capacity >= *count) {
        auto* images = reinterpret_cast<XrSwapchainImageD3D11KHR*>(out);
        for (uint32_t i = 0; i < *count; ++i) images[i].texture = it->second.textures[i].Get();
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL AcquireSwapchainImage(XrSwapchain handle, const XrSwapchainImageAcquireInfo*, uint32_t* out) {
    auto& c = g_chains[reinterpret_cast<std::uintptr_t>(handle)];
    if (c.textures.empty()) return XR_ERROR_HANDLE_INVALID;
    if (c.acquired >= 0) return XR_ERROR_CALL_ORDER_INVALID;
    c.acquired = int(c.next++ % c.textures.size());
    *out = uint32_t(c.acquired);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL WaitSwapchainImage(XrSwapchain, const XrSwapchainImageWaitInfo*) { return XR_SUCCESS; }
XrResult XRAPI_CALL ReleaseSwapchainImage(XrSwapchain handle, const XrSwapchainImageReleaseInfo*) {
    auto& c = g_chains[reinterpret_cast<std::uintptr_t>(handle)];
    if (c.acquired < 0) return XR_ERROR_CALL_ORDER_INVALID;
    c.released = c.acquired;
    c.acquired = -1;
    return XR_SUCCESS;
}

// Paces to 90 Hz with a high-resolution waitable timer (no busy wait beyond the last 0.5 ms).
void SleepUntil(std::int64_t targetNs) {
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    std::int64_t remaining = targetNs - QpcNs();
    if (remaining > 600000 && timer) {
        LARGE_INTEGER due{};
        due.QuadPart = -((remaining - 500000) / 100);
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, 100);
    } else if (remaining > 600000) {
        Sleep(DWORD((remaining - 500000) / 1000000));
    }
    while (QpcNs() < targetNs) YieldProcessor();
}
XrResult XRAPI_CALL WaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* out) {
    PollControl();
    const std::int64_t now = QpcNs();
    if (!g_nextFrameNs || now - g_nextFrameNs > 4 * kPeriodNs) g_nextFrameNs = now; // late: re-anchor, no catch-up burst
    g_nextFrameNs += kPeriodNs;
    SleepUntil(g_nextFrameNs);
    ++g_frames;
    out->predictedDisplayPeriod = kPeriodNs;
    out->predictedDisplayTime = g_nextFrameNs + 2 * kPeriodNs; // scan-out about two frames ahead
    out->shouldRender = g_running ? XR_TRUE : XR_FALSE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL BeginFrame(XrSession, const XrFrameBeginInfo*) { return XR_SUCCESS; }
XrResult XRAPI_CALL EndFrame(XrSession, const XrFrameEndInfo* end) {
    for (std::uint32_t i = 0; i < end->layerCount; ++i) {
        if (end->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            ++g_projection;
            const auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(end->layers[i]);
            if (p->viewCount) {
                g_lastEyeW = std::uint32_t(p->views[0].subImage.imageRect.extent.width);
                g_lastEyeH = std::uint32_t(p->views[0].subImage.imageRect.extent.height);
            }
        } else if (end->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
            ++g_quads;
        }
    }
    if (g_device && (g_sigPending || g_control.flip)) Classify(end);
    if (g_snapPending && g_device) Snapshot(end);
    return XR_SUCCESS;
}

} // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* out) {
#define API(n)                                                                                                                   \
    if (std::strcmp(name, "xr" #n) == 0) {                                                                                       \
        *out = reinterpret_cast<PFN_xrVoidFunction>(n);                                                                          \
        return XR_SUCCESS;                                                                                                       \
    }
    API(EnumerateInstanceExtensionProperties) API(CreateInstance) API(DestroyInstance) API(GetInstanceProperties) API(GetSystem)
    API(GetSystemProperties) API(GetD3D11GraphicsRequirementsKHR) API(EnumerateViewConfigurationViews)
    API(EnumerateEnvironmentBlendModes) API(CreateSession) API(DestroySession) API(PollEvent) API(BeginSession) API(EndSession)
    API(RequestExitSession) API(EnumerateReferenceSpaces) API(StringToPath) API(CreateActionSet) API(DestroyActionSet)
    API(CreateAction) API(SuggestInteractionProfileBindings) API(AttachSessionActionSets) API(CreateActionSpace) API(SyncActions)
    API(GetActionStatePose) API(GetActionStateFloat) API(GetActionStateVector2f) API(GetActionStateBoolean)
    API(ApplyHapticFeedback) API(StopHapticFeedback) API(CreateReferenceSpace) API(DestroySpace) API(LocateSpace)
    API(LocateViews) API(EnumerateSwapchainFormats) API(CreateSwapchain) API(DestroySwapchain) API(EnumerateSwapchainImages)
    API(AcquireSwapchainImage) API(WaitSwapchainImage) API(ReleaseSwapchainImage) API(WaitFrame) API(BeginFrame) API(EndFrame)
#undef API
    *out = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
