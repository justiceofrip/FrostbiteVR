#include "Bf4FirstPersonView.h"

#include <intrin.h>
#include <smmintrin.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fvr::bf4 {

namespace {
using L = ViewConstantsLayout;
constexpr UINT kViewSlot = 2;             // viewConstants (every BF4 shader binds it at b2)
constexpr std::uint32_t kOurBytes = 4096; // our constant buffer: 256 constants, enough for any count BF4 binds

// BF4's ring buffer is read after the game unmapped it (the driver keeps it mapped): guard
// against an address the driver retired.
bool SafeCopy(void* dst, const void* src, std::size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Reading BF4's upload ring (write-combined memory: plain loads are uncached and slow):
// SSE4.1 streaming loads fetch a line at a time. 16-byte aligned source and size only.
bool HasStreamingLoads() {
    int info[4]{};
    __cpuid(info, 1);
    return (info[2] & (1 << 19)) != 0; // SSE4.1
}

bool StreamCopy(void* dst, const void* src, std::size_t n) {
    if ((reinterpret_cast<std::uintptr_t>(src) & 15) || (n & 15)) return SafeCopy(dst, src, n);
    __try {
        auto* s = static_cast<__m128i*>(const_cast<void*>(src));
        auto* d = static_cast<__m128i*>(dst);
        std::size_t i = 0;
        for (; i + 4 <= n / 16; i += 4) {
            const __m128i a = _mm_stream_load_si128(s + i), b = _mm_stream_load_si128(s + i + 1),
                          c = _mm_stream_load_si128(s + i + 2), e = _mm_stream_load_si128(s + i + 3);
            _mm_storeu_si128(d + i, a);
            _mm_storeu_si128(d + i + 1, b);
            _mm_storeu_si128(d + i + 2, c);
            _mm_storeu_si128(d + i + 3, e);
        }
        for (; i < n / 16; ++i) _mm_storeu_si128(d + i, _mm_stream_load_si128(s + i));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Every read of BF4's ring: streaming loads where the CPU has them (live, session 68: a 2.9 KB
// palette in 3.1 us against 61.9 us with plain loads; the arms read ~10 per frame).
bool RingCopy(void* dst, const void* src, std::size_t n) {
    static const bool streaming = HasStreamingLoads();
    return streaming ? StreamCopy(dst, src, n) : SafeCopy(dst, src, n);
}

double MicrosecondsSince(LARGE_INTEGER start) {
    static const double perUs = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1e6;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - start.QuadPart) / perUs;
}

Mat4 Load(const std::uint8_t* block, std::uint32_t offset) {
    Mat4 m;
    std::memcpy(m.data(), block + offset, sizeof(m));
    return m;
}
void Store(std::uint8_t* block, std::uint32_t offset, const Mat4& m) { std::memcpy(block + offset, m.data(), sizeof(m)); }
} // namespace

math::Pose GunGripAtRest() {
    // Live (prologue rifle, hip, 2026-10-20): the weapon's root bone sits at (0.066, -0.022,
    // -0.254) in the first-person camera's space with its bore along (-0.029, 0.019, -0.999)
    // (1.6 deg left, 1.1 deg up); the magazine bone 9.5 cm below the root. The hand holds the
    // pistol grip ~8 cm below and ~6.5 cm behind the root, along the bore.
    math::Pose p;
    p.position = {0.066f, -0.100f, -0.190f};
    const math::Vec3 bore = math::Normalize({-0.0286f, 0.0186f, -0.9994f});
    const math::Vec3 axis{bore.y, -bore.x, 0}; // (0, 0, -1) x bore
    p.orientation = math::FromAxisAngle(axis, std::acos(-bore.z));
    return p;
}

Mat4 GripInRightHandBind() {
    return {-0.54443f, 0.44305f, 0.71225f, -0.45786f, 0.82528f, 0.43479f, 0.36038f, 1.12034f,
            -0.15001f, 0.78401f, -0.60235f, 0.26732f, 0, 0, 0, 1};
}

const FirstPersonView::Frame::Hand* FirstPersonView::HandFor(const std::uint8_t* block, const Frame& frame, int& how,
                                                             bool& game, float& diff) {
    const Mat4 v = Load(block, L::kViewMatrix);
    const Frame::Hand* best = nullptr;
    float bestDiff = 1e9f;
    game = false;
    auto differ = [&](const float* rotation) {
        float d = 0;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) d = std::fmax(d, std::fabs(v[r * 4 + c] - rotation[r * 3 + c]));
        return d;
    };
    for (const auto& h : frame.hands) {
        if (!h.valid) continue;
        const float byRender = differ(h.rotation), byGame = differ(h.gameRotation);
        if (std::fmin(byRender, byGame) < bestDiff) {
            bestDiff = std::fmin(byRender, byGame);
            best = &h;
            game = byGame < byRender;
        }
    }
    diff = best ? bestDiff : 0.0f;
    how = !best ? 2 : bestDiff < 2e-3f ? 0 : bestDiff < 0.05f ? 1 : 2;
    return how == 2 ? nullptr : best;
}

bool FirstPersonView::IsFirstPersonBlock(const std::uint8_t* block, float gameFovY, float renderKy) {
    if (!(gameFovY > 0.1f && gameFovY < 3.0f) || !(renderKy > 0)) return false;
    const Mat4 p = Load(block, L::kProjMatrix), v = Load(block, L::kViewMatrix);
    for (float x : p)
        if (!std::isfinite(x)) return false;
    // Perspective (w = -z) with no shear: the shape of BF4's main and first-person projections.
    if (p[12] != 0 || p[13] != 0 || p[14] != -1.0f || p[15] != 0 || p[1] != 0 || p[4] != 0 || !(p[0] > 0) || !(p[5] > 0))
        return false;
    // A rigid view (unit rotation rows).
    for (int r = 0; r < 3; ++r) {
        const float n = v[r * 4] * v[r * 4] + v[r * 4 + 1] * v[r * 4 + 1] + v[r * 4 + 2] * v[r * 4 + 2];
        if (std::fabs(n - 1.0f) > 1e-3f) return false;
    }
    const float gameKy = 1.0f / std::tan(gameFovY * 0.5f);
    return std::fabs(p[5] - gameKy) < 1e-3f * gameKy && std::fabs(p[5] - renderKy) > 0.05f * renderKy;
}

namespace {
// A rigid perspective view: the shape every BF4 main or first-person viewConstants has.
bool PerspectiveRigid(const Mat4& p, const Mat4& v) {
    for (float x : p)
        if (!std::isfinite(x)) return false;
    if (p[12] != 0 || p[13] != 0 || p[14] != -1.0f || p[15] != 0 || p[1] != 0 || p[4] != 0 || !(p[0] > 0) || !(p[5] > 0))
        return false;
    for (int r = 0; r < 3; ++r) {
        const float n = v[r * 4] * v[r * 4] + v[r * 4 + 1] * v[r * 4 + 1] + v[r * 4 + 2] * v[r * 4 + 2];
        if (std::fabs(n - 1.0f) > 1e-3f) return false;
    }
    return true;
}
} // namespace

const FirstPersonView::Frame::Hand* FirstPersonView::FirstPersonHand(const std::uint8_t* block, const Frame& frame,
                                                                     bool* exact, bool* shaped) {
    const Mat4 p = Load(block, L::kProjMatrix), v = Load(block, L::kViewMatrix);
    if (shaped) *shaped = false;
    if (exact) *exact = false;
    if (!PerspectiveRigid(p, v) || !(frame.renderKy > 0) || std::fabs(p[5] - frame.renderKy) <= 0.05f * frame.renderKy)
        return nullptr;
    if (shaped) *shaped = true;
    const Frame::Hand* best = nullptr;
    float bestDiff = kShakeTolerance;
    for (const auto& h : frame.hands) {
        if (!h.valid) continue;
        float d = 0;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) d = std::fmax(d, std::fabs(v[r * 4 + c] - h.gameRotation[r * 3 + c]));
        if (d < bestDiff) {
            bestDiff = d;
            best = &h;
        }
    }
    if (exact) *exact = best && bestDiff < 2e-3f;
    return best;
}

void FirstPersonView::Rewrite(std::uint8_t* block, float renderKx, float renderKy, const Mat4& m) {
    const Mat4 view = Mul(m, Load(block, L::kViewMatrix));
    Mat4 proj = Load(block, L::kProjMatrix);
    proj[0] = renderKx;
    proj[5] = renderKy;
    Mat4 rotation = view; // camera-relative: the view's rotation only
    rotation[3] = rotation[7] = rotation[11] = 0;
    Store(block, L::kViewMatrix, view);
    Store(block, L::kProjMatrix, proj);
    Store(block, L::kViewProjMatrix, Mul(proj, view));
    Store(block, L::kCrViewProjMatrix, Mul(proj, rotation));
    float k[2] = {renderKx, renderKy};
    std::memcpy(block + L::kProjectionK, k, sizeof(k));
    // Camera position: the point the view maps to the origin, -R^T t.
    float camera[3];
    for (int j = 0; j < 3; ++j)
        camera[j] = -(view[0 * 4 + j] * view[3] + view[1 * 4 + j] * view[7] + view[2 * 4 + j] * view[11]);
    std::memcpy(block + L::kCameraPos, camera, sizeof(camera));
}

bool FirstPersonView::IsMainViewBlock(const std::uint8_t* block, float renderKy) {
    const Mat4 p = Load(block, L::kProjMatrix), v = Load(block, L::kViewMatrix);
    return renderKy > 0 && PerspectiveRigid(p, v) && std::fabs(p[5] - renderKy) < 1e-3f * renderKy;
}

Mat4 FirstPersonView::EyeShift(int eye, float halfEye) {
    // The camera moves to x = -half (left) or +half (right) of the head's camera space; what the
    // head saw at p the eye sees at p - (x, 0, 0).
    Mat4 m = Identity4();
    m[3] = eye == 0 ? halfEye : -halfEye;
    return m;
}

Mat4 FirstPersonView::BodyInHead(const float rows[9]) {
    const math::Vec3 r0{rows[0], rows[1], rows[2]}, r1{rows[3], rows[4], rows[5]}, r2{rows[6], rows[7], rows[8]};
    const math::Vec3 up{0, 1, 0};
    // The body's back: the head's, level. Looking far down or up the head's up axis carries the
    // heading, so both are blended (r2 tilts up as r1 tilts forward when looking down).
    const math::Vec3 r2h = r2 - up * math::Dot(r2, up), r1h = r1 - up * math::Dot(r1, up);
    const math::Vec3 back = math::Normalize(r2h - r1h * (r2.y >= 0 ? 1.0f : -1.0f));
    if (math::Length(back) < 0.5f) return Identity4();
    const math::Vec3 right = math::Normalize(math::Cross(up, back));
    const math::Vec3 axes[3] = {right, up, back};
    const math::Vec3 head[3] = {r0, r1, r2};
    Mat4 m = Identity4();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[i * 4 + j] = math::Dot(head[i], axes[j]);
    return m;
}

namespace {
float ChainDistance(const ArmChain& a, const ArmChain& b) {
    return std::fmax(math::Length(a.shoulder - b.shoulder),
                     std::fmax(math::Length(a.elbow - b.elbow), math::Length(a.wrist - b.wrist)));
}
} // namespace

void FirstPersonView::RewriteArms(ID3D11DeviceContext* context, const void* mapped, std::uint32_t size,
                                  ID3D11Buffer** buffers, UINT* first, UINT* count) {
    const std::uint32_t offset = first ? first[0] * 16 : 0;
    const std::uint32_t bytes = count ? count[0] * 16 : (size > offset ? size - offset : 0);
    if (bytes < kArmMinPaletteBytes || bytes > kOurBytes || offset + bytes > size) return;
    static const bool streaming = HasStreamingLoads();
    LARGE_INTEGER t0{};
    QueryPerformanceCounter(&t0);
    alignas(16) std::uint8_t data[kOurBytes];
    if (!RingCopy(data, static_cast<const std::uint8_t*>(mapped) + offset, bytes)) return;
    const double readUs = MicrosecondsSince(t0);
    LARGE_INTEGER t1{};
    QueryPerformanceCounter(&t1);
    float* palette = reinterpret_cast<float*>(data);
    const int bones = std::min(static_cast<int>(bytes / (kArmBoneFloats * sizeof(float))), kArmMaxBones);
    // This palette's own arms vote; the most voted (ties: nearest the previous pass's) lead.
    ArmChain own[2];
    FindArmChains(palette, bones, armBind_, own);
    // The player's arms sit by the first-person camera; the stale copies some weapon palettes
    // carry lie metres away (live, session 64: one of them led the pass's first arms draw).
    const math::Vec3 camera{passCamera_[0], passCamera_[1], passCamera_[2]};
    for (ArmChain& chain : own)
        if (chain.found && math::Length(chain.shoulder - camera) > kArmNearCamera) chain.found = false;
    bool changedRef = false;
    for (int side = 0; side < 2; ++side) {
        if (!own[side].found) continue;
        ArmCandidate* hit = nullptr;
        for (int k = 0; k < armCandidates_[side] && !hit; ++k)
            if (ChainDistance(armVotes_[side][k].chain, own[side]) < kArmChainTolerance) hit = &armVotes_[side][k];
        if (!hit && armCandidates_[side] < kArmCandidates) {
            hit = &armVotes_[side][armCandidates_[side]++];
            hit->chain = own[side];
            hit->votes = 0;
        }
        if (hit) ++hit->votes;
        const ArmCandidate* best = nullptr;
        for (int k = 0; k < armCandidates_[side]; ++k) {
            const ArmCandidate& c = armVotes_[side][k];
            if (!best || c.votes > best->votes ||
                (c.votes == best->votes && armPrev_[side].found &&
                 ChainDistance(c.chain, armPrev_[side]) < ChainDistance(best->chain, armPrev_[side])))
                best = &c;
        }
        if (best && (!armRef_[side].found || ChainDistance(best->chain, armRef_[side]) >= kArmChainTolerance)) {
            if (armRef_[side].found) {
                std::lock_guard<std::mutex> lock(mutex_);
                ++stats_.armSwitches;
            }
            armRef_[side] = best->chain;
            changedRef = true;
        }
    }
    if (changedRef) armSolved_ = false;
    // The grip from BF4's right hand: every palette's right hand bone is a candidate (EndPass
    // takes one of the pass's right arm).
    if (own[1].found && anchorCandidateCount_ < kAnchorCandidates) {
        const int hand = FindHandBone(palette, bones, armBind_, own[1], 1);
        if (hand >= 0) {
            const float* h = palette + hand * kArmBoneFloats;
            const Mat4 m = Mul(passView_, Mat4{h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11], 0, 0, 0, 1});
            if (AllFinite(m)) anchorCandidates_[anchorCandidateCount_++] = {m, own[1]};
        }
    }
    if (!armPass_) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.armReadUs += readUs;
        return;
    }
    if (!armSolved_ && (armRef_[0].found || armRef_[1].found)) {
        ArmSolveInput in;
        in.chains[0] = armRef_[0];
        in.chains[1] = armRef_[1];
        in.headFromPass = armHeadFromPass_;
        in.body = armBody_;
        in.shoulderFollow = kArmShoulderFollow;
        in.clavicleShare = kArmClavicleShare;
        in.hasWristTarget[0] = armLeftBlend_ > 0;
        in.wristTarget[0] = armLeftTarget_;
        in.wristBlend[0] = armLeftBlend_;
        in.handTurn[0] = armLeftTurn_;
        armCorr_ = SolveArms(in);
        armSolved_ = true;
    }
    int changed = 0;
    if (armSolved_) {
        BoneClass classes[kArmMaxBones];
        ClassifyBones(palette, bones, armBind_, armRef_, kArmClassTolerance, classes, kArmFingerReach);
        changed = ApplyArmCorrections(palette, bones, classes, armHeadFromPass_, armCorr_);
    }
    const double workUs = MicrosecondsSince(t1);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.armPalettes;
        stats_.armReadUs += readUs;
        stats_.armWorkUs += workUs;
        stats_.armStreamingReads = streaming;
    }
    if (!changed) return;
    LARGE_INTEGER t2{};
    QueryPerformanceCounter(&t2);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(oursPalette_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)) || !m.pData) return;
    std::memcpy(m.pData, data, bytes);
    if (bytes < kOurBytes) std::memset(static_cast<std::uint8_t*>(m.pData) + bytes, 0, kOurBytes - bytes);
    context->Unmap(oursPalette_.Get(), 0);
    buffers[0] = oursPalette_.Get(); // the bind proceeds with our palette in this slot only
    if (first) first[0] = 0;
    const double uploadUs = MicrosecondsSince(t2);
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.armUploadUs += uploadUs;
    ++stats_.armRewritten;
    stats_.armBones += static_cast<std::uint64_t>(changed);
    stats_.armSlide[0] = armCorr_.slide[0];
    stats_.armSlide[1] = armCorr_.slide[1];
}

int FirstPersonView::FrameEye() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shiftedThisFrame_ ? eye_ : -1;
}

void FirstPersonView::OnPresent() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shiftedThisFrame_) {
        ++stats_.shiftedFrames;
        eye_ ^= 1;
    }
    shiftedThisFrame_ = false;
}

bool FirstPersonView::Upload(ID3D11DeviceContext* context, const std::uint8_t* block, ID3D11Buffer** buffers, UINT i,
                             UINT* first, UINT* count) {
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(ours_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)) || !m.pData) return false;
    std::memset(m.pData, 0, kOurBytes);
    std::memcpy(m.pData, block, L::kSize);
    context->Unmap(ours_.Get(), 0);
    buffers[i] = ours_.Get(); // the bind proceeds with our buffer in this slot only
    if (first) first[i] = 0;
    if (count && count[i] * 16 > kOurBytes) count[i] = kOurBytes / 16;
    return true;
}

FirstPersonView::~FirstPersonView() { Remove(); }

bool FirstPersonView::Install(ID3D11DeviceContext* immediate) {
    if (installed_) return true;
    if (!immediate) return false;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    immediate->GetDevice(&device);
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = kOurBytes;
    d.Usage = D3D11_USAGE_DYNAMIC;
    d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (!device || FAILED(device->CreateBuffer(&d, nullptr, &ours_)) ||
        FAILED(device->CreateBuffer(&d, nullptr, &oursPalette_)))
        return false;
    immediate_ = immediate;
    if (!ContextHooks::Install(immediate) || !ContextHooks::Add(this)) return false;
    installed_ = true;
    return true;
}

void FirstPersonView::Remove() {
    if (installed_) ContextHooks::Remove(this);
    installed_ = false;
}

void FirstPersonView::SetFrame(const Frame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!frame.enabled) sources_.clear();
    frame_ = frame;
}

FirstPersonView::Stats FirstPersonView::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::string FirstPersonView::Describe() const {
    const Stats s = GetStats();
    char line[760];
    int n = std::snprintf(line, sizeof(line),
                  "first_person_view installed=%d binds=%llu matched=%llu replaced=%llu unreadable=%llu ky=%.4f fp_cam=(%.2f %.2f %.2f) hand=%llu/%llu/%llu by_render=%llu by_game=%llu diff=%.4f passes=%llu weapon=%llu (%.3f %.3f %.3f) zoom_held=%llu shift=%.3f eye_views=%llu eye_frames=%llu",
                  static_cast<int>(installed_), static_cast<unsigned long long>(s.binds),
                  static_cast<unsigned long long>(s.matched), static_cast<unsigned long long>(s.replaced),
                  static_cast<unsigned long long>(s.unreadable), s.lastKy, s.cameraPos[0], s.cameraPos[1], s.cameraPos[2],
                  static_cast<unsigned long long>(s.handMatched), static_cast<unsigned long long>(s.handNearest),
                  static_cast<unsigned long long>(s.handMissing), static_cast<unsigned long long>(s.byRender),
                  static_cast<unsigned long long>(s.byGame), s.nearestDiff, static_cast<unsigned long long>(s.passes),
                  static_cast<unsigned long long>(s.weaponFound), s.weapon[0], s.weapon[1], s.weapon[2],
                  static_cast<unsigned long long>(s.zoomHeld), s.weaponShift, static_cast<unsigned long long>(s.shiftedViews),
                  static_cast<unsigned long long>(s.shiftedFrames));
    if (n > 0 && n < static_cast<int>(sizeof(line)))
        std::snprintf(line + n, sizeof(line) - n,
                      " arms=%llu/%llu bones=%llu solved=%llu switches=%llu slide=(%.3f %.3f) arm_us_per_pass=read %.0f%s work %.0f upload %.0f anchor=%llu/%llu/jumps %llu grip_from_rest=(%.1f cm %.1f deg) kicks=%llu last=[%.1f/%.1f/%.0f %.1f/%.1f/%.0f %.1f/%.1f/%.0f %.1f/%.1f/%.0f]",
                      static_cast<unsigned long long>(s.armPalettes), static_cast<unsigned long long>(s.armRewritten),
                      static_cast<unsigned long long>(s.armBones), static_cast<unsigned long long>(s.armSolved),
                      static_cast<unsigned long long>(s.armSwitches),
                      s.armSlide[0], s.armSlide[1], s.armSolved ? s.armReadUs / s.armSolved : 0.0,
                      s.armStreamingReads ? " (streaming)" : "", s.armSolved ? s.armWorkUs / s.armSolved : 0.0,
                      s.armSolved ? s.armUploadUs / s.armSolved : 0.0, static_cast<unsigned long long>(s.anchorFound),
                      static_cast<unsigned long long>(s.anchorUsed), static_cast<unsigned long long>(s.anchorJumps),
                      s.anchorCm, s.anchorDeg,
                      static_cast<unsigned long long>(s.kicks), s.kickLog[0][0], s.kickLog[0][1], s.kickLog[0][2],
                      s.kickLog[1][0], s.kickLog[1][1], s.kickLog[1][2], s.kickLog[2][0], s.kickLog[2][1], s.kickLog[2][2],
                      s.kickLog[3][0], s.kickLog[3][1], s.kickLog[3][2]);
    return line;
}

void FirstPersonView::OnMap(ID3D11DeviceContext* context, ID3D11Resource* resource, D3D11_MAP, void* data) {
    if (context != immediate_ || !resource) return;
    D3D11_RESOURCE_DIMENSION dim{};
    resource->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return;
    D3D11_BUFFER_DESC d{};
    static_cast<ID3D11Buffer*>(resource)->GetDesc(&d);
    if (!(d.BindFlags & D3D11_BIND_CONSTANT_BUFFER)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!frame_.enabled) return;
    sources_[resource] = {data, d.ByteWidth};
}

void FirstPersonView::OnUpdate(ID3D11DeviceContext* context, ID3D11Resource* resource, const D3D11_BOX*, const void*) {
    if (context != immediate_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.erase(resource); // its contents no longer come from a mapped address
}

bool FirstPersonView::WeaponPart(const Mat4& w, const float camera[3], float radius) {
    for (float x : w)
        if (!std::isfinite(x)) return false;
    if (std::fabs(w[12]) > 1e-4f || std::fabs(w[13]) > 1e-4f || std::fabs(w[14]) > 1e-4f || std::fabs(w[15] - 1.0f) > 1e-4f)
        return false;
    for (int r = 0; r < 3; ++r) {
        const float n = w[r * 4] * w[r * 4] + w[r * 4 + 1] * w[r * 4 + 1] + w[r * 4 + 2] * w[r * 4 + 2];
        if (std::fabs(n - 1.0f) > 1e-2f) return false;
    }
    const float dx = w[3] - camera[0], dy = w[7] - camera[1], dz = w[11] - camera[2];
    return dx * dx + dy * dy + dz * dz < radius * radius;
}

void FirstPersonView::EndPass() {
    inPass_ = false;
    // The right hand of the pass: a candidate of the pass's right arm (the most voted), the one
    // nearest the hand in use (live, sessions 78-80: weapon palettes' stale arm copies gave
    // hands 0.4-0.5 m away).
    {
        const AnchorCandidate* best = nullptr;
        float bestDistance = 1e9f;
        for (int k = 0; k < anchorCandidateCount_; ++k) {
            const AnchorCandidate& c = anchorCandidates_[k];
            if (armRef_[1].found && ChainDistance(c.arm, armRef_[1]) >= kArmChainTolerance) continue;
            const float d = anchorValid_ ? RigidDistance(c.hand, anchor_) : 0.0f;
            if (d < bestDistance) {
                bestDistance = d;
                best = &c;
            }
        }
        if (best) {
            anchorPass_ = best->hand;
            anchorPassValid_ = true;
        }
        anchorCandidateCount_ = 0;
    }
    const bool both = armPass_ && armSolved_ && armCorr_.solved[0] && armCorr_.solved[1];
    for (int side = 0; side < 2; ++side) {
        if (armPass_ && armRef_[side].found) armPrev_[side] = armRef_[side];
        armRef_[side] = {};
        armCandidates_[side] = 0;
    }
    armPass_ = armSolved_ = palettePass_ = false;
    // The right hand's grip: smoothed toward this pass's hand (time-based), or stale when missing.
    // A hand far from the smoothed one is not taken (live, session 78: 0.4-0.5 m jumps, a weapon
    // palette's stale arm copy leading half a split pass), unless such hands keep coming.
    if (anchorPassValid_ && anchorValid_ && RigidDistance(anchorPass_, anchor_) > kAnchorJump) {
        if (++anchorJumps_ <= kAnchorJumpPasses) {
            anchorPassValid_ = false;
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.anchorJumps;
        }
    } else if (anchorPassValid_) {
        anchorJumps_ = 0;
    }
    if (anchorPassValid_) {
        LARGE_INTEGER now{}, freq{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        const float dt = anchorTick_ ? std::min(0.1f, static_cast<float>(now.QuadPart - anchorTick_) /
                                                          static_cast<float>(freq.QuadPart))
                                     : 1.0f;
        anchorTick_ = now.QuadPart;
        // Recoil: the raw hand's step back along the gun (the grip's +z) since the last pass at
        // least kKickMinGap ago (live: the pass can be split in two within a frame, ~0 ms apart).
        const float kickDt = kickRawTick_ ? static_cast<float>(now.QuadPart - kickRawTick_) / static_cast<float>(freq.QuadPart) : 0.0f;
        const bool kickStep = anchorValid_ && kickDt >= kKickMinGap;
        if (kickStep && kickDt < 0.1f) {
            const Mat4 grip = Mul(anchorPass_, GripInRightHandBind());
            const math::Vec3 back = math::Normalize(TransformVector(grip, {0, 0, 1}));
            const math::Vec3 step{anchorPass_[3] - anchorLastRaw_[3], anchorPass_[7] - anchorLastRaw_[7],
                                  anchorPass_[11] - anchorLastRaw_[11]};
            const float backMm = math::Dot(step, back) * 1000.0f;
            const float sideMm = math::Length(step - back * math::Dot(step, back)) * 1000.0f;
            const float speed = math::Dot(step, back) / kickDt;
            const float sinceKick = kickTick_ ? static_cast<float>(now.QuadPart - kickTick_) / static_cast<float>(freq.QuadPart)
                                              : kKickGap;
            if (speed >= kKickSpeed && math::Length(step) <= kKickMaxStep && sinceKick >= kKickGap) {
                kickTick_ = now.QuadPart;
                std::lock_guard<std::mutex> lock(mutex_);
                const int slot = static_cast<int>(stats_.kicks % 4);
                stats_.kickLog[slot][0] = backMm;
                stats_.kickLog[slot][1] = sideMm;
                stats_.kickLog[slot][2] = kickDt * 1000.0f;
                ++stats_.kicks;
            }
        }
        if (kickStep || !kickRawTick_ || kickDt >= 0.1f) {
            anchorLastRaw_ = anchorPass_;
            kickRawTick_ = now.QuadPart;
        }
        anchor_ = anchorValid_ ? BlendRigid(anchor_, anchorPass_, 1.0f - std::exp(-dt / kAnchorSeconds)) : anchorPass_;
        anchorValid_ = true;
        anchorMisses_ = 0;
    } else if (anchorValid_ && ++anchorMisses_ > kAnchorStalePasses) {
        anchorValid_ = false;
    }
    const bool anchorFound = anchorPassValid_;
    anchorPassValid_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (both) ++stats_.armSolved;
    if (anchorFound) ++stats_.anchorFound;
    ++stats_.passes;
    if (!bestCount_) return;
    weapon_ = Mul(passView_, bestWorld_);
    weaponValid_ = true;
    ++stats_.weaponFound;
    stats_.weapon[0] = weapon_[3];
    stats_.weapon[1] = weapon_[7];
    stats_.weapon[2] = weapon_[11];
    // Hip: the game's widest FOV. Its pose there is the rest the hand holds; while zoomed (and
    // a moment after) that rest is kept.
    const float fov = frame_.gameFovY;
    hipFov_ = std::fmax(hipFov_, fov);
    if (fov >= hipFov_ - 0.005f) {
        if (++hipPasses_ >= 20 || !restValid_) {
            weaponRest_ = weapon_;
            restValid_ = true;
        }
    } else {
        hipPasses_ = 0;
    }
}

void FirstPersonView::OnDraw(ID3D11DeviceContext* context, const char*, UINT count, UINT) {
    if (context != immediate_ || !inPass_ || !havePending_) return;
    if (count > bestCount_) {
        bestCount_ = count;
        bestWorld_ = pendingWorld_;
    }
}

void FirstPersonView::OnBindConstants(ID3D11DeviceContext* context, ContextHooks::Stage stage, UINT startSlot, UINT numBuffers,
                                      ID3D11Buffer** buffers, UINT* first, UINT* count) {
    if (context != immediate_) return;
    // functionConstants (b0) inside the pass: a rigid mesh's world matrix, if it is one.
    if (stage == ContextHooks::Stage::Vertex && inPass_ && startSlot == 0 && buffers[0]) {
        havePending_ = false;
        Source source;
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = sources_.find(buffers[0]);
            if (it != sources_.end()) {
                source = it->second;
                known = true;
            }
        }
        const std::uint32_t offset = first ? first[0] * 16 : 0;
        Mat4 w{};
        if (known && source.mapped && offset + 64 <= source.size &&
            RingCopy(w.data(), static_cast<const std::uint8_t*>(source.mapped) + offset, 64) && WeaponPart(w, passCamera_)) {
            pendingWorld_ = w;
            havePending_ = true;
        }
        if (palettePass_ && known && source.mapped) RewriteArms(context, source.mapped, source.size, buffers, first, count);
    }
    if (kViewSlot < startSlot || kViewSlot >= startSlot + numBuffers) return;
    const UINT i = kViewSlot - startSlot;
    ID3D11Buffer* buffer = buffers[i];
    if (!buffer || buffer == ours_.Get()) return;
    Frame frame;
    Source source;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!frame_.enabled) return;
        frame = frame_;
        ++stats_.binds;
        const auto it = sources_.find(buffer);
        if (it == sources_.end()) return;
        source = it->second;
    }
    const std::uint32_t offset = first ? first[i] * 16 : 0;
    if (!source.mapped || offset + L::kSize > source.size) return;
    std::array<std::uint8_t, L::kSize> block{};
    if (!RingCopy(block.data(), static_cast<const std::uint8_t*>(source.mapped) + offset, block.size())) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.unreadable;
        return;
    }
    // Alternate eyes: a main view of this frame goes to this frame's eye (it ends a first-person
    // pass like any other view).
    const bool shift = frame.alternateEyes && frame.halfEye > 0;
    int eye = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        eye = eye_;
    }
    const Mat4 eyeMove = shift ? EyeShift(eye, frame.halfEye) : Identity4();
    if (shift && IsMainViewBlock(block.data(), frame.renderKy)) {
        if (stage == ContextHooks::Stage::Vertex && inPass_) EndPass();
        float kx = 0, ky = 0;
        std::memcpy(&kx, block.data() + L::kProjMatrix + 0 * 4, 4);
        std::memcpy(&ky, block.data() + L::kProjMatrix + 5 * 4, 4);
        Rewrite(block.data(), kx, ky, eyeMove);
        const bool uploaded = Upload(context, block.data(), buffers, i, first, count);
        std::lock_guard<std::mutex> lock(mutex_);
        if (uploaded) {
            ++stats_.shiftedViews;
            shiftedThisFrame_ = true;
        }
        return;
    }
    if (shift && !frame.handMode) return; // the first-person pass is left as BF4 draws it
    bool exactHand = false, shaped = false;
    const Frame::Hand* passHand = frame.handMode ? FirstPersonHand(block.data(), frame, &exactHand, &shaped) : nullptr;
    if (frame.handMode && shaped && !passHand) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.handMissing; // its shape, no recorded rotation near: BF4's own pass this time
    }
    if (frame.handMode ? !passHand : !IsFirstPersonBlock(block.data(), frame.gameFovY, frame.renderKy)) {
        if (stage == ContextHooks::Stage::Vertex && inPass_) EndPass();
        return;
    }
    float ky = 0, camera[3]{};
    std::memcpy(&ky, block.data() + L::kProjMatrix + 5 * 4, 4);
    std::memcpy(camera, block.data() + L::kCameraPos, sizeof(camera));
    if (stage == ContextHooks::Stage::Vertex && !inPass_) { // a first-person pass starts
        inPass_ = true;
        havePending_ = false;
        bestCount_ = 0;
        passView_ = Load(block.data(), L::kViewMatrix);
        std::memcpy(passCamera_, camera, sizeof(camera));
    }
    Mat4 move = frame.transform;
    if (frame.handMode) {
        const Frame::Hand* hand = passHand;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++(exactHand ? stats_.handMatched : stats_.handNearest);
            ++stats_.byGame;
        }
        move = Mul(FromPose(hand->hand), InverseRigid(FromPose(frame.gripAtRest)));
        palettePass_ = inPass_ && oursPalette_;
        if (frame.handAnchor && anchorValid_) {
            // The grip BF4's right hand holds now (smoothed), wherever BF4 holds the gun.
            const Mat4 grip = Mul(anchor_, GripInRightHandBind());
            move = Mul(FromPose(hand->hand), InverseRigid(grip));
            const Mat4 rest = FromPose(frame.gripAtRest);
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.anchorUsed;
            stats_.anchorCm = RigidDistance(grip, rest) * 100.0f;
            stats_.anchorDeg = RigidAngle(grip, rest) * 57.29578f;
        } else if (frame.holdThroughZoom && weaponValid_ && restValid_ && frame.gameFovY < hipFov_ - 0.005f) {
            // Zoomed: BF4 moved the receiver from its hip pose; move it back onto the hand.
            const Mat4 back = Mul(weaponRest_, InverseRigid(weapon_));
            move = Mul(move, back);
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.zoomHeld;
            stats_.weaponShift = std::sqrt(back[3] * back[3] + back[7] * back[7] + back[11] * back[11]);
        }
        // Arms from the body: the rig as drawn in the head's space (before any eye shift) and
        // where its torso goes.
        const bool armStart = frame.armIk && inPass_ && oursPalette_ && !armPass_;
        armPass_ = frame.armIk && inPass_ && oursPalette_;
        if (armPass_) {
            const Mat4 headFromPass = Mul(move, Load(block.data(), L::kViewMatrix));
            const Mat4 torso = hand->bodyValid ? FromPose(hand->body) : BodyInHead(hand->rotation);
            const Mat4 body = Mul(torso, InverseRigid(move));
            if (headFromPass != armHeadFromPass_ || body != armBody_) armSolved_ = false; // solve again
            armHeadFromPass_ = headFromPass;
            armBody_ = body;
            // The left hand: once per pass, toward its controller when free, back to the gun when not.
            if (armStart) {
                LARGE_INTEGER now{}, freq{};
                QueryPerformanceCounter(&now);
                QueryPerformanceFrequency(&freq);
                const float dt = armLastPassTick_ ? std::min(0.1f, static_cast<float>(now.QuadPart - armLastPassTick_) /
                                                                       static_cast<float>(freq.QuadPart))
                                                  : 0.0f;
                armLastPassTick_ = now.QuadPart;
                const float step = dt / kArmHandMoveSeconds;
                const bool free = hand->leftValid && hand->leftFree;
                armLeftBlend_ = std::clamp(armLeftBlend_ + (free ? step : -step), 0.0f, 1.0f);
                if (hand->leftValid) {
                    Mat4 controller = FromPose(hand->leftHand), gun = move;
                    controller[3] = controller[7] = controller[11] = 0; // rotations only
                    gun[3] = gun[7] = gun[11] = 0;
                    if (free) {
                        armLeftTarget_ = TransformPoint(FromPose(hand->leftHand), {0, 0, kArmWristBehindGrip});
                        armLeftTurn_ = Mul(controller, Mul(armLeftGrip_, InverseRigid(gun)));
                    } else {
                        armLeftGrip_ = Mul(InverseRigid(controller), gun); // on the gun: the relation to keep
                    }
                }
                armSolved_ = false;
            }
        }
    }
    if (shift) move = Mul(eyeMove, move); // the rig seen from this frame's eye
    Rewrite(block.data(), frame.renderKx, frame.renderKy, move);
    const bool uploaded = Upload(context, block.data(), buffers, i, first, count);
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.matched;
    if (uploaded) ++stats_.replaced;
    stats_.lastKy = ky;
    std::memcpy(stats_.cameraPos, camera, sizeof(camera));
}

} // namespace fvr::bf4
