#include "Bf4ViewOverride.h"

#include "Bf4CameraProbe.h"

#include <cmath>
#include <cstring>

namespace fvr::bf4 {

using frostbite::LinearTransform;
using math::Vec3;

namespace {
// The replacement slot outlives any instance (the copied vtable is never freed), so the
// original and the active instance live in statics the replacement can always reach.
std::atomic<RendererSubmitFn> g_original{nullptr};
std::atomic<ViewOverride*> g_active{nullptr};

Vec3 ReadRow(const std::uint8_t* row) {
    float v[3];
    std::memcpy(v, row, sizeof(v));
    return {v[0], v[1], v[2]};
}
void WriteRow(std::uint8_t* row, Vec3 v) {
    const float f[3] = {v.x, v.y, v.z};
    std::memcpy(row, f, sizeof(f)); // bytes 12..15 (the w lane) stay as the game left them
}
float ReadFloat(const std::uint8_t* at) {
    float f;
    std::memcpy(&f, at, sizeof(f));
    return f;
}
bool PlausibleFov(float f) { return f > 0.05f && f < 3.1f; }
} // namespace

LinearTransform ReadViewTransform(const std::uint8_t* rows) {
    return {ReadRow(rows), ReadRow(rows + 16), ReadRow(rows + 32), ReadRow(rows + 48)};
}

void WriteViewTransform(std::uint8_t* rows, const LinearTransform& t) {
    WriteRow(rows, t.right);
    WriteRow(rows + 16, t.up);
    WriteRow(rows + 32, t.forward);
    WriteRow(rows + 48, t.trans);
}

namespace {
CameraAxes AxesOf(const LinearTransform& t) {
    CameraAxes a;
    const math::Vec3 rows[3] = {t.right, t.up, t.forward};
    for (int i = 0; i < 3; ++i) {
        a.row[i][0] = rows[i].x;
        a.row[i][1] = rows[i].y;
        a.row[i][2] = rows[i].z;
    }
    return a;
}
void SetAxes(LinearTransform& t, const CameraAxes& a) {
    t.right = {a.row[0][0], a.row[0][1], a.row[0][2]};
    t.up = {a.row[1][0], a.row[1][1], a.row[1][2]};
    t.forward = {a.row[2][0], a.row[2][1], a.row[2][2]};
}
} // namespace
LinearTransform HeadTrackedView(const LinearTransform& game, const LinearTransform& local, bool keepGamePitch) {
    LinearTransform parent = game;
    if (!keepGamePitch) {
        // Yaw only, +Y up (Frostbite is left-handed: right = up x forward).
        const Vec3 up{0, 1, 0};
        Vec3 forward{game.forward.x, 0, game.forward.z};
        if (math::Length(forward) > 1e-4f)
            forward = math::Normalize(forward);
        else // looking straight up/down: the right row still carries the heading
            forward = math::Cross(math::Normalize(Vec3{game.right.x, 0, game.right.z}), up);
        parent.right = math::Cross(up, forward);
        parent.up = up;
        parent.forward = forward;
    }
    return frostbite::Compose(parent, local);
}

bool AimInView(const math::Vec3& aimBack, const LinearTransform& view, float& tanX, float& tanY) {
    const math::Vec3 f{-aimBack.x, -aimBack.y, -aimBack.z};
    auto dot = [](const math::Vec3& a, const math::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const float depth = -dot(f, view.forward); // the view looks along -forward
    if (!(depth > 0.1f)) return false;
    tanX = dot(f, view.right) / depth;
    tanY = dot(f, view.up) / depth;
    return std::isfinite(tanX) && std::isfinite(tanY);
}

LinearTransform HeadTrackedViewAtYaw(const LinearTransform& game, const LinearTransform& local, float yaw) {
    LinearTransform parent = game;
    const float s = std::sin(yaw), c = std::cos(yaw);
    parent.forward = {-s, 0, c}; // +Z row: behind a camera looking along (s, 0, -c)
    parent.up = {0, 1, 0};
    parent.right = math::Cross(parent.up, parent.forward);
    return frostbite::Compose(parent, local);
}

ViewOverride::~ViewOverride() {
    ViewOverride* expected = this;
    g_active.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

ViewOverride::InstallResult ViewOverride::Install(void* renderer, std::uintptr_t expectedSubmit) {
    if (object_) return InstallResult::AlreadyInstalled;
    if (!renderer) return InstallResult::NoObject;
    void** vtable = *static_cast<void***>(renderer);
    if (!vtable || reinterpret_cast<std::uintptr_t>(vtable[kRendererSubmitSlot]) != expectedSubmit)
        return InstallResult::UnexpectedSubmit;
    // Locator (RTTI) entry before the slots, then the slots with ours in place of slot 5.
    copy_ = new void*[kRendererVtableSlots + 1];
    std::memcpy(copy_, vtable - 1, sizeof(void*) * (kRendererVtableSlots + 1));
    original_ = reinterpret_cast<RendererSubmitFn>(vtable[kRendererSubmitSlot]);
    copy_[1 + kRendererSubmitSlot] = reinterpret_cast<void*>(&ViewOverride::Submit);
    originalVtable_ = vtable;
    object_ = renderer;
    g_original.store(original_, std::memory_order_release);
    g_active.store(this, std::memory_order_release); // publishes object_ to the replacement
    void** ours = copy_ + 1;
    std::memcpy(renderer, &ours, sizeof(ours)); // the one native write; aligned pointer store
    return InstallResult::Ok;
}

ViewOverride::RemoveResult ViewOverride::Remove(void* currentRenderer) {
    if (!object_) return RemoveResult::NotInstalled;
    RemoveResult result = RemoveResult::ObjectGone;
    if (currentRenderer == object_) {
        void** now = nullptr;
        std::memcpy(&now, object_, sizeof(now));
        if (now == copy_ + 1) {
            std::memcpy(object_, &originalVtable_, sizeof(originalVtable_));
            result = RemoveResult::Restored;
        } else {
            result = RemoveResult::ForeignVtable; // someone else owns it now: leave it
        }
    }
    g_active.store(nullptr, std::memory_order_release); // a call through a stale copy just forwards
    object_ = nullptr;
    originalVtable_ = nullptr;
    copy_ = nullptr; // intentionally not freed
    Clear();
    return result;
}

void ViewOverride::SetLocal(const LinearTransform& local, bool keepGamePitch, std::uint64_t poseId, float fovY,
                            float parentYaw) {
    std::lock_guard<std::mutex> lock(mutex_);
    local_ = local;
    keepGamePitch_ = keepGamePitch;
    poseId_ = poseId;
    parentYaw_ = parentYaw;
    fovY_ = PlausibleFov(fovY) ? fovY : 0.0f;
    havePose_ = true;
    poseTime_ = std::chrono::steady_clock::now();
}

void ViewOverride::SetHand(const math::Pose& handInHead, bool valid, const math::Pose& bodyInHead, bool bodyValid) {
    std::lock_guard<std::mutex> lock(mutex_);
    hand_ = handInHead;
    handValid_ = valid;
    body_ = bodyInHead;
    bodyValid_ = bodyValid;
}

std::array<ViewOverride::HandRecord, ViewOverride::kHandRecords> ViewOverride::RecentHands() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hands_;
}

void ViewOverride::SetGunAim(const GunAimSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    gunAim_ = settings;
}

void ViewOverride::RequestBlockDump() {
    std::lock_guard<std::mutex> lock(mutex_);
    dumpRequested_ = true;
    dumpReady_ = false;
}

bool ViewOverride::TakeBlockDump(std::array<std::uint8_t, kViewBlockBytes>& out, std::uint32_t& viewCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!dumpReady_) return false;
    out = dump_;
    viewCount = dumpViewCount_;
    dumpReady_ = false;
    return true;
}

void ViewOverride::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    havePose_ = false;
}

ViewOverride::Stats ViewOverride::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void ViewOverride::Submit(void* renderer, float a, float b, std::uint32_t viewCount, std::uint8_t* views, void* sync,
                          void* extra) {
    const RendererSubmitFn original = g_original.load(std::memory_order_acquire);
    ViewOverride* self = g_active.load(std::memory_order_acquire);
    if (!self || renderer != self->object_ || viewCount == 0 || !views) {
        original(renderer, a, b, viewCount, views, sync, extra);
        return;
    }
    LinearTransform local;
    bool keepPitch = false, fresh = false;
    std::uint64_t poseId = 0;
    float fovY = 0, parentYaw = std::numeric_limits<float>::quiet_NaN();
    math::Pose hand{}, body{};
    bool handValid = false, bodyValid = false;
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        ++self->stats_.submits;
        fresh = self->havePose_ && std::chrono::steady_clock::now() - self->poseTime_ <= kMaxAge;
        if (self->havePose_ && !fresh) {
            ++self->stats_.stale;
            self->havePose_ = false; // expired: stock view until the next fresh pose
        }
        local = self->local_;
        keepPitch = self->keepGamePitch_;
        poseId = self->poseId_;
        fovY = self->fovY_;
        parentYaw = self->parentYaw_;
        hand = self->hand_;
        handValid = self->handValid_;
        body = self->body_;
        bodyValid = self->bodyValid_;
        if (!fresh) self->stats_.poseRun = 0; // this frame shows the stock view
        if (self->dumpRequested_) { // research: block 0 as the game submitted it
            std::memcpy(self->dump_.data(), views, kViewBlockBytes);
            self->dumpViewCount_ = viewCount;
            self->dumpRequested_ = false;
            self->dumpReady_ = true;
        }
    }
    Matrix4f game;
    std::memcpy(game.m.data(), views, sizeof(game.m));
    const bool rigid = IsRigidTransform(game);
    if (rigid) { // the game's own aim, read before anything is written
        const LinearTransform camera = ReadViewTransform(views + kViewCopyOffset);
        float nearZ = 0, farZ = 0;
        std::memcpy(&nearZ, views + kViewNearOffset, sizeof(nearZ));
        std::memcpy(&farZ, views + kViewFarOffset, sizeof(farZ));
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (nearZ > 0 && farZ > nearZ && std::isfinite(farZ)) {
            self->stats_.nearZ = nearZ;
            self->stats_.farZ = farZ;
        }
        self->stats_.gameBack = camera.forward;
        ++self->stats_.gameSamples;
        float differ = 0;
        for (std::size_t i = 0; i < 16; ++i) {
            if (i % 4 == 3) continue; // w lanes are padding
            float first = 0, second = 0;
            std::memcpy(&first, views + i * 4, 4);
            std::memcpy(&second, views + kViewCopyOffset + i * 4, 4);
            differ = std::fmax(differ, std::fabs(first - second));
        }
        self->stats_.copiesDiffer = differ;
    }
    if (fresh && !rigid) {
        std::lock_guard<std::mutex> lock(self->mutex_);
        ++self->stats_.notRigid;
        self->stats_.poseRun = 0;
    }
    if (!fresh || !rigid) { // never compose onto something that is not a camera
        original(renderer, a, b, viewCount, views, sync, extra);
        return;
    }
    // Temporary override of view block 0 (the main view), restored exactly afterwards.
    std::uint8_t saved[kViewTransformBytes];
    std::uint32_t savedDirty = 0;
    std::memcpy(saved, views, sizeof(saved));
    std::memcpy(&savedDirty, views + kViewDirtyOffset, sizeof(savedDirty));
    auto compose = [&](const LinearTransform& g) {
        return std::isfinite(parentYaw) ? HeadTrackedViewAtYaw(g, local, parentYaw) : HeadTrackedView(g, local, keepPitch);
    };
    const LinearTransform gameCamera = ReadViewTransform(views + kViewCopyOffset);
    const LinearTransform headCamera = compose(gameCamera);
    // Gun follows aim: the camera turned toward the game's aim at the head's position, the
    // turn latched per pose so every submit of a lease renders the same view of the world.
    GunAim gun;
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        if (self->gunAim_.enabled && poseId) {
            if (self->gunLatchPose_ != poseId) {
                const float blockAspect = ReadFloat(views + kViewAspectOffset);
                const float viewAspect = self->gunAim_.aspect > 0.2f ? self->gunAim_.aspect : blockAspect;
                self->gunLatched_ = GunAimRotation(AxesOf(headCamera), AxesOf(gameCamera), self->gunAim_.eye[0],
                                                   self->gunAim_.eye[1], viewAspect, self->gunAim_.budget);
                self->gunLatchPose_ = poseId;
            }
            gun = self->gunLatched_;
        }
    }
    LinearTransform renderCamera = headCamera;
    if (gun.rotation.enabled) {
        SetAxes(renderCamera, ApplyRotation(AxesOf(headCamera), gun.rotation));
        fovY = PlausibleFov(fovY) ? std::max(fovY, gun.fovY) : gun.fovY;
    }
    {
        // The hand of this pose, keyed by the rotation this frame renders with (the gun pass
        // carries that rotation in its own view constants).
        std::lock_guard<std::mutex> lock(self->mutex_);
        HandRecord& r = self->hands_[self->handNext_++ % kHandRecords];
        const math::Vec3 rows[3] = {renderCamera.right, renderCamera.up, renderCamera.forward};
        const math::Vec3 gameRows[3] = {gameCamera.right, gameCamera.up, gameCamera.forward};
        for (int i = 0; i < 3; ++i) {
            r.rotation[i * 3 + 0] = rows[i].x;
            r.rotation[i * 3 + 1] = rows[i].y;
            r.rotation[i * 3 + 2] = rows[i].z;
            r.gameRotation[i * 3 + 0] = gameRows[i].x;
            r.gameRotation[i * 3 + 1] = gameRows[i].y;
            r.gameRotation[i * 3 + 2] = gameRows[i].z;
        }
        r.hand = hand;
        r.valid = handValid;
        r.poseId = poseId;
        r.body = body;
        r.bodyValid = bodyValid;
    }
    const std::uint32_t mask = self->copyMask_.load();
    // +0x00 (the camera relative to the soldier) feeds BF4's weapon lag: with the gun on the
    // aim it keeps the game's own value, so head motion never swings the gun.
    bool handHeld = false;
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        handHeld = self->gunAim_.handHeld;
    }
    if ((mask & 1u) && !gun.rotation.enabled && !handHeld) WriteViewTransform(views, compose(ReadViewTransform(views)));
    if (mask & 2u) WriteViewTransform(views + kViewCopyOffset, renderCamera);
    {
        float tx = 0, ty = 0;
        const bool inView = AimInView(gameCamera.forward, renderCamera, tx, ty);
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->stats_.aimInView = inView;
        self->stats_.aimTanX = tx;
        self->stats_.aimTanY = ty;
    }
    const std::uint32_t dirty = savedDirty | kViewDirtyBits; // as the native setter does
    std::memcpy(views + kViewDirtyOffset, &dirty, sizeof(dirty));
    std::uint8_t written[kViewTransformBytes];
    std::memcpy(written, views, sizeof(written));
    // FOV: only onto a block whose FOV and aspect read as such, as the FOV setter writes it.
    const float gameFov = ReadFloat(views + kViewFovOffset), aspect = ReadFloat(views + kViewAspectOffset);
    const float savedHorizontal = ReadFloat(views + kViewHorizontalFovOffset);
    const float savedDefault = ReadFloat(views + kViewDefaultFovOffset);
    const bool fovWrite = fovY > 0 && PlausibleFov(gameFov) && aspect > 0.2f && aspect < 5.0f;
    const bool defaultWrite = fovWrite && PlausibleFov(savedDefault);
    float writtenHorizontal = 0;
    if (fovWrite) {
        writtenHorizontal = 2.0f * std::atan(std::tan(fovY * 0.5f) * aspect);
        std::memcpy(views + kViewFovOffset, &fovY, 4);
        std::memcpy(views + kViewHorizontalFovOffset, &writtenHorizontal, 4);
        if (defaultWrite) std::memcpy(views + kViewDefaultFovOffset, &fovY, 4);
        const std::uint32_t fovDirty = dirty | kViewFovDirtyBits;
        std::memcpy(views + kViewDirtyOffset, &fovDirty, 4);
    }
    std::uint32_t writtenDirty = 0;
    std::memcpy(&writtenDirty, views + kViewDirtyOffset, 4);

    original(renderer, a, b, viewCount, views, sync, extra);

    bool restored = false;
    if (std::memcmp(views, written, sizeof(written)) == 0) {
        std::memcpy(views, saved, sizeof(saved));
        restored = true;
    }
    if (fovWrite) { // each field back only if it still holds exactly what we wrote
        if (ReadFloat(views + kViewFovOffset) == fovY) std::memcpy(views + kViewFovOffset, &gameFov, 4);
        else restored = false;
        if (ReadFloat(views + kViewHorizontalFovOffset) == writtenHorizontal)
            std::memcpy(views + kViewHorizontalFovOffset, &savedHorizontal, 4);
        else restored = false;
        if (defaultWrite) {
            if (ReadFloat(views + kViewDefaultFovOffset) == fovY) std::memcpy(views + kViewDefaultFovOffset, &savedDefault, 4);
            else restored = false;
        }
    }
    std::uint32_t nowDirty = 0;
    std::memcpy(&nowDirty, views + kViewDirtyOffset, sizeof(nowDirty));
    if (nowDirty == writtenDirty) std::memcpy(views + kViewDirtyOffset, &savedDirty, sizeof(savedDirty));
    std::lock_guard<std::mutex> lock(self->mutex_);
    ++self->stats_.applied;
    ++(restored ? self->stats_.restored : self->stats_.foreign);
    if (fovWrite) ++self->stats_.fovApplied;
    if (PlausibleFov(gameFov)) self->stats_.gameFovY = gameFov;
    self->stats_.renderedFovY = fovWrite ? fovY : gameFov;
    if (poseId && poseId == self->stats_.poseId) ++self->stats_.poseRun;
    else self->stats_.poseRun = poseId ? 1 : 0;
    self->stats_.poseId = poseId;
    self->stats_.frameRotation = gun.rotation; // with the pose id: a capture reads both together
    self->stats_.gunFraction = gun.fraction;
    const math::Vec3 rows[3] = {renderCamera.right, renderCamera.up, renderCamera.forward};
    for (int i = 0; i < 3; ++i) {
        self->stats_.renderRotation[i * 3 + 0] = rows[i].x;
        self->stats_.renderRotation[i * 3 + 1] = rows[i].y;
        self->stats_.renderRotation[i * 3 + 2] = rows[i].z;
    }
}

} // namespace fvr::bf4
