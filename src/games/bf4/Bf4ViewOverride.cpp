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
        if (!fresh) self->stats_.poseRun = 0; // this frame shows the stock view
    }
    Matrix4f game;
    std::memcpy(game.m.data(), views, sizeof(game.m));
    const bool rigid = IsRigidTransform(game);
    if (rigid) { // the game's own aim, read before anything is written
        const LinearTransform camera = ReadViewTransform(views + kViewCopyOffset);
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->stats_.gameBack = camera.forward;
        ++self->stats_.gameSamples;
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
    WriteViewTransform(views, compose(ReadViewTransform(views)));
    WriteViewTransform(views + kViewCopyOffset, compose(ReadViewTransform(views + kViewCopyOffset)));
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
}

} // namespace fvr::bf4
