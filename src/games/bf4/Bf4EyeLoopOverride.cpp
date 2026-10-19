#include "Bf4EyeLoopOverride.h"

#include <cstring>
#include <new>

namespace fvr::bf4 {

namespace {
std::atomic<EyeLoopFn> g_original{nullptr};
std::atomic<EyeLoopOverride*> g_active{nullptr};

std::uintptr_t Load(const void* object, std::size_t offset) {
    std::uintptr_t v = 0;
    std::memcpy(&v, static_cast<const std::uint8_t*>(object) + offset, sizeof(v));
    return v;
}
void Store(void* object, std::size_t offset, std::uintptr_t v) {
    std::memcpy(static_cast<std::uint8_t*>(object) + offset, &v, sizeof(v));
}
} // namespace

EyeLoopOverride::~EyeLoopOverride() {
    EyeLoopOverride* expected = this;
    g_active.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

EyeLoopOverride::InstallResult EyeLoopOverride::Install(void* object, std::uintptr_t expectedSlot) {
    if (object_) return InstallResult::AlreadyInstalled;
    if (!object || sizeof(void*) != 8) return InstallResult::NoObject; // the analysed layout is x64's
    void** vtable = *static_cast<void***>(object);
    if (!vtable || reinterpret_cast<std::uintptr_t>(vtable[kEyeLoopSlot]) != expectedSlot) return InstallResult::UnexpectedSlot;
    const auto self = reinterpret_cast<std::uintptr_t>(object);
    const std::uintptr_t storage = self + kEyeArrayInline;
    if (Load(object, kEyeArrayBegin) != storage || Load(object, kEyeArrayCapacity) != storage + kEyeEntryBytes)
        return InstallResult::NotInlineArray; // not the layout the analysis describes
    buffer_ = static_cast<std::uint8_t*>(::operator new(kEntries * kEyeEntryBytes, std::align_val_t{64}));
    copy_ = new void*[kEyeLoopVtableSlots + 1];
    std::memcpy(copy_, vtable - 1, sizeof(void*) * (kEyeLoopVtableSlots + 1));
    copy_[1 + kEyeLoopSlot] = reinterpret_cast<void*>(&EyeLoopOverride::Call);
    originalVtable_ = vtable;
    object_ = object;
    g_original.store(reinterpret_cast<EyeLoopFn>(vtable[kEyeLoopSlot]), std::memory_order_release);
    g_active.store(this, std::memory_order_release);
    void** ours = copy_ + 1;
    std::memcpy(object, &ours, sizeof(ours)); // the one native write; aligned pointer store
    return InstallResult::Ok;
}

bool EyeLoopOverride::Remove() {
    if (!object_) return false;
    void** now = nullptr;
    std::memcpy(&now, object_, sizeof(now));
    const bool ours = now == copy_ + 1;
    if (ours) std::memcpy(object_, &originalVtable_, sizeof(originalVtable_));
    g_active.store(nullptr, std::memory_order_release); // a call through a stale copy just forwards
    object_ = nullptr;
    originalVtable_ = nullptr;
    copy_ = nullptr;
    buffer_ = nullptr; // intentionally not freed
    return ours;
}

EyeLoopOverride::Stats EyeLoopOverride::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void EyeLoopOverride::Call(void* self, std::uint64_t a2, std::uint64_t a3, float a4, float a5, std::uint32_t a6, void* a7,
                           void* a8, std::uint64_t a9, std::uint64_t a10) {
    const EyeLoopFn original = g_original.load(std::memory_order_acquire);
    EyeLoopOverride* o = g_active.load(std::memory_order_acquire);
    if (!o || self != o->object_) {
        original(self, a2, a3, a4, a5, a6, a7, a8, a9, a10);
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(self);
    const std::uintptr_t begin = Load(self, kEyeArrayBegin), end = Load(self, kEyeArrayEnd),
                         capacity = Load(self, kEyeArrayCapacity);
    const bool inlineArray = begin == base + kEyeArrayInline && capacity == begin + kEyeEntryBytes;
    if (!inlineArray) { // someone else changed the array: leave it alone
        original(self, a2, a3, a4, a5, a6, a7, a8, a9, a10);
        std::lock_guard<std::mutex> lock(o->mutex_);
        ++o->stats_.calls;
        ++o->stats_.foreign;
        return;
    }
    // Temporary override: room for kEntries eyes for the length of this call.
    const auto storage = reinterpret_cast<std::uintptr_t>(o->buffer_);
    Store(self, kEyeArrayBegin, storage);
    Store(self, kEyeArrayEnd, storage);
    Store(self, kEyeArrayCapacity, storage + EyeLoopOverride::kEntries * kEyeEntryBytes);

    original(self, a2, a3, a4, a5, a6, a7, a8, a9, a10);

    const std::uintptr_t usedEnd = Load(self, kEyeArrayEnd);
    const bool stillOurs = Load(self, kEyeArrayBegin) == storage &&
                           Load(self, kEyeArrayCapacity) == storage + EyeLoopOverride::kEntries * kEyeEntryBytes;
    if (stillOurs) {
        Store(self, kEyeArrayBegin, begin);
        Store(self, kEyeArrayEnd, end);
        Store(self, kEyeArrayCapacity, capacity);
    }
    std::lock_guard<std::mutex> lock(o->mutex_);
    ++o->stats_.calls;
    ++o->stats_.widened;
    ++(stillOurs ? o->stats_.restored : o->stats_.foreign);
    if (usedEnd >= storage) {
        const std::uint64_t entries = (usedEnd - storage) / kEyeEntryBytes;
        if (entries > o->stats_.maxEntries) o->stats_.maxEntries = entries;
    }
}

} // namespace fvr::bf4
