#include "Bf4FirstPersonView.h"

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

Mat4 Load(const std::uint8_t* block, std::uint32_t offset) {
    Mat4 m;
    std::memcpy(m.data(), block + offset, sizeof(m));
    return m;
}
void Store(std::uint8_t* block, std::uint32_t offset, const Mat4& m) { std::memcpy(block + offset, m.data(), sizeof(m)); }
} // namespace

Mat4 Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    return r;
}

Mat4 Identity4() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Mat4 FromPose(const math::Pose& pose) {
    const math::Vec3 x = math::Rotate(pose.orientation, {1, 0, 0}), y = math::Rotate(pose.orientation, {0, 1, 0}),
                     z = math::Rotate(pose.orientation, {0, 0, 1});
    return {x.x, y.x, z.x, pose.position.x, x.y, y.y, z.y, pose.position.y, x.z, y.z, z.z, pose.position.z, 0, 0, 0, 1};
}

Mat4 InverseRigid(const Mat4& m) {
    Mat4 r = Identity4();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i) r[i * 4 + 3] = -(r[i * 4 + 0] * m[3] + r[i * 4 + 1] * m[7] + r[i * 4 + 2] * m[11]);
    return r;
}

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
    if (!device || FAILED(device->CreateBuffer(&d, nullptr, &ours_))) return false;
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
    char line[480];
    std::snprintf(line, sizeof(line),
                  "first_person_view installed=%d binds=%llu matched=%llu replaced=%llu unreadable=%llu ky=%.4f fp_cam=(%.2f %.2f %.2f) hand=%llu/%llu/%llu by_render=%llu by_game=%llu diff=%.4f passes=%llu weapon=%llu (%.3f %.3f %.3f) zoom_held=%llu shift=%.3f",
                  static_cast<int>(installed_), static_cast<unsigned long long>(s.binds),
                  static_cast<unsigned long long>(s.matched), static_cast<unsigned long long>(s.replaced),
                  static_cast<unsigned long long>(s.unreadable), s.lastKy, s.cameraPos[0], s.cameraPos[1], s.cameraPos[2],
                  static_cast<unsigned long long>(s.handMatched), static_cast<unsigned long long>(s.handNearest),
                  static_cast<unsigned long long>(s.handMissing), static_cast<unsigned long long>(s.byRender),
                  static_cast<unsigned long long>(s.byGame), s.nearestDiff, static_cast<unsigned long long>(s.passes),
                  static_cast<unsigned long long>(s.weaponFound), s.weapon[0], s.weapon[1], s.weapon[2],
                  static_cast<unsigned long long>(s.zoomHeld), s.weaponShift);
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
    std::lock_guard<std::mutex> lock(mutex_);
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
            SafeCopy(w.data(), static_cast<const std::uint8_t*>(source.mapped) + offset, 64) && WeaponPart(w, passCamera_)) {
            pendingWorld_ = w;
            havePending_ = true;
        }
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
    if (!SafeCopy(block.data(), static_cast<const std::uint8_t*>(source.mapped) + offset, block.size())) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.unreadable;
        return;
    }
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
        // Zoomed: BF4 moved the receiver from its hip pose; move it back onto the hand.
        if (frame.holdThroughZoom && weaponValid_ && restValid_ && frame.gameFovY < hipFov_ - 0.005f) {
            const Mat4 back = Mul(weaponRest_, InverseRigid(weapon_));
            move = Mul(move, back);
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.zoomHeld;
            stats_.weaponShift = std::sqrt(back[3] * back[3] + back[7] * back[7] + back[11] * back[11]);
        }
    }
    Rewrite(block.data(), frame.renderKx, frame.renderKy, move);
    D3D11_MAPPED_SUBRESOURCE m{};
    const bool uploaded = SUCCEEDED(context->Map(ours_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)) && m.pData;
    if (uploaded) {
        std::memset(m.pData, 0, kOurBytes);
        std::memcpy(m.pData, block.data(), block.size());
        context->Unmap(ours_.Get(), 0);
        buffers[i] = ours_.Get(); // the bind proceeds with our buffer in this slot only
        if (first) first[i] = 0;
        if (count && count[i] * 16 > kOurBytes) count[i] = kOurBytes / 16;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.matched;
    if (uploaded) ++stats_.replaced;
    stats_.lastKy = ky;
    std::memcpy(stats_.cameraPos, camera, sizeof(camera));
}

} // namespace fvr::bf4
