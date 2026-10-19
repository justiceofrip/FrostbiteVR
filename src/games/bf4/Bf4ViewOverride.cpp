#include "Bf4ViewOverride.h"

#include "Bf4CameraProbe.h"

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

void ViewOverride::SetLocal(const LinearTransform& local, bool keepGamePitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    local_ = local;
    keepGamePitch_ = keepGamePitch;
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
    }
    Matrix4f game;
    std::memcpy(game.m.data(), views, sizeof(game.m));
    const bool rigid = IsRigidTransform(game);
    if (fresh && !rigid) {
        std::lock_guard<std::mutex> lock(self->mutex_);
        ++self->stats_.notRigid;
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
    WriteViewTransform(views, HeadTrackedView(ReadViewTransform(views), local, keepPitch));
    WriteViewTransform(views + kViewCopyOffset,
                       HeadTrackedView(ReadViewTransform(views + kViewCopyOffset), local, keepPitch));
    const std::uint32_t dirty = savedDirty | kViewDirtyBits; // as the native setter does
    std::memcpy(views + kViewDirtyOffset, &dirty, sizeof(dirty));
    std::uint8_t written[kViewTransformBytes];
    std::memcpy(written, views, sizeof(written));

    original(renderer, a, b, viewCount, views, sync, extra);

    bool restored = false;
    if (std::memcmp(views, written, sizeof(written)) == 0) {
        std::memcpy(views, saved, sizeof(saved));
        restored = true;
    }
    std::uint32_t nowDirty = 0;
    std::memcpy(&nowDirty, views + kViewDirtyOffset, sizeof(nowDirty));
    if (nowDirty == dirty) std::memcpy(views + kViewDirtyOffset, &savedDirty, sizeof(savedDirty));
    std::lock_guard<std::mutex> lock(self->mutex_);
    ++self->stats_.applied;
    ++(restored ? self->stats_.restored : self->stats_.foreign);
}

} // namespace fvr::bf4
