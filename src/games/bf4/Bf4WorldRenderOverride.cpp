#include "Bf4WorldRenderOverride.h"

#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fvr::bf4 {

namespace {
std::atomic<WorldRenderFn> g_original{nullptr};
std::atomic<WorldDrawFn> g_originalDraw{nullptr};
std::atomic<WorldRenderOverride*> g_active{nullptr};

double NowUs() {
#if defined(_WIN32)
    static const double perUs = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1e6;
    }();
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / perUs;
#else
    return 0.0;
#endif
}
} // namespace

WorldRenderOverride::~WorldRenderOverride() {
    WorldRenderOverride* expected = this;
    g_active.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

WorldRenderOverride::InstallResult WorldRenderOverride::Install(void* object, std::uintptr_t expectedSlot, Hooks* hooks,
                                                              std::uintptr_t expectedDraw) {
    if (object_) return InstallResult::AlreadyInstalled;
    if (!object || !hooks || sizeof(void*) != 8) return InstallResult::NoObject; // the analysed layout is x64's
    void** vtable = *static_cast<void***>(object);
    if (!vtable || reinterpret_cast<std::uintptr_t>(vtable[kWorldRenderSlot]) != expectedSlot)
        return InstallResult::UnexpectedSlot;
    if (expectedDraw && reinterpret_cast<std::uintptr_t>(vtable[kWorldDrawSlot]) != expectedDraw)
        return InstallResult::UnexpectedSlot;
    copy_ = new void*[kWorldRendererVtableSlots + 1];
    std::memcpy(copy_, vtable - 1, sizeof(void*) * (kWorldRendererVtableSlots + 1));
    copy_[1 + kWorldRenderSlot] = reinterpret_cast<void*>(&WorldRenderOverride::Call);
    if (expectedDraw) copy_[1 + kWorldDrawSlot] = reinterpret_cast<void*>(&WorldRenderOverride::Draw);
    g_originalDraw.store(reinterpret_cast<WorldDrawFn>(vtable[kWorldDrawSlot]), std::memory_order_release);
    originalVtable_ = vtable;
    object_ = object;
    hooks_ = hooks;
    g_original.store(reinterpret_cast<WorldRenderFn>(vtable[kWorldRenderSlot]), std::memory_order_release);
    g_active.store(this, std::memory_order_release);
    void** ours = copy_ + 1;
    std::memcpy(object, &ours, sizeof(ours)); // the one native write; aligned pointer store
    return InstallResult::Ok;
}

bool WorldRenderOverride::Remove() {
    if (!object_) return false;
    void** now = nullptr;
    std::memcpy(&now, object_, sizeof(now));
    const bool ours = now == copy_ + 1;
    if (ours) std::memcpy(object_, &originalVtable_, sizeof(originalVtable_));
    g_active.store(nullptr, std::memory_order_release); // a call through a stale copy just forwards
    object_ = nullptr;
    originalVtable_ = nullptr;
    copy_ = nullptr; // intentionally not freed
    hooks_ = nullptr;
    return ours;
}

WorldRenderOverride::Stats WorldRenderOverride::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void WorldRenderOverride::Call(void* self, void* args, void* a3, void* a4) {
    const WorldRenderFn original = g_original.load(std::memory_order_acquire);
    WorldRenderOverride* o = g_active.load(std::memory_order_acquire);
    if (!o || self != o->object_ || !o->hooks_ || !args) {
        original(self, args, a3, a4);
        if (o) {
            std::lock_guard<std::mutex> lock(o->mutex_);
            ++o->stats_.calls;
            ++o->stats_.foreign;
        }
        return;
    }
    std::uint32_t views = 0;
    std::memcpy(&views, static_cast<const std::uint8_t*>(args) + kWorldRenderArgsViewCount, sizeof(views));
    const int runs = views == 1 ? (o->hooks_->Runs(views) == 2 ? 2 : 1) : 1; // only BF4's own one view is doubled
    double us[2] = {0, 0};
    std::uint64_t waits = 0;
    for (int i = 0; i < runs; ++i) {
        if (i > 0) { // the previous run's jobs finish before this run rebuilds the view state
            if (const JobWaitFn wait = o->wait_.load(std::memory_order_acquire)) {
                if (a3) wait(a3);
                if (a4) wait(a4);
                ++waits;
            }
        }
        o->hooks_->BeforeRun(i, runs);
        const double t0 = NowUs();
        original(self, args, a3, a4);
        us[i] = NowUs() - t0;
        o->hooks_->AfterRun(i, runs);
    }
    std::lock_guard<std::mutex> lock(o->mutex_);
    ++o->stats_.calls;
    if (runs == 2) ++o->stats_.doubled;
    o->stats_.waits += waits;
    o->stats_.runUs[0] = us[0];
    o->stats_.runUs[1] = us[1];
}

void WorldRenderOverride::Draw(void* self, void* context, void* a3, void* a4) {
    const WorldDrawFn original = g_originalDraw.load(std::memory_order_acquire);
    WorldRenderOverride* o = g_active.load(std::memory_order_acquire);
    if (!o || self != o->object_ || !o->hooks_) {
        original(self, context, a3, a4);
        return;
    }
    const int runs = o->hooks_->Draws() == 2 ? 2 : 1;
    double us[2] = {0, 0};
    for (int i = 0; i < runs; ++i) {
        o->hooks_->BeforeDraw(i, runs);
        const double t0 = NowUs();
        original(self, context, a3, a4);
        us[i] = NowUs() - t0;
        o->hooks_->AfterDraw(i, runs);
    }
    std::lock_guard<std::mutex> lock(o->mutex_);
    ++o->stats_.drawCalls;
    if (runs == 2) ++o->stats_.drawsDoubled;
    o->stats_.drawUs[0] = us[0];
    o->stats_.drawUs[1] = us[1];
}

} // namespace fvr::bf4
