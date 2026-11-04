#pragma once
#include "Bc2KnifeSoundObservation.h"
#include <atomic>
#include <optional>

#if defined(_MSC_VER) && defined(_M_IX86)
namespace fvr::bc2::selectedKnife {
// The engine may catch a fault above any one of these callbacks. Keep this
// x86 SEH frame free of C++ destructors. Cleanup captures belong to its caller.
template<class Body,class Finish>
__declspec(noinline) std::uintptr_t RunSelectedKnifeFinally(const Body& body,const Finish& finish) {
    std::uintptr_t result=0;
    bool returned=false;
    __try {
        result=body();
        returned=true;
    } __finally {
        finish(returned);
    }
    return result;
}

template<class ChestIsolation,class Scope,class Body>
std::uintptr_t RunSelectedKnifeParent(std::atomic<unsigned>& active,Scope*& acquiring,Scope*& attacking,
    KnifeSoundDispatchContext& sound,const std::atomic<bool>& observeSound,const Body& body) {
    active.fetch_add(1,std::memory_order_acq_rel);
    Scope* previousAcquire=acquiring;
    Scope* previousAttack=attacking;
    std::optional<KnifeSoundDispatchScope> soundScope;
    std::optional<ChestIsolation> chestScope;
    return RunSelectedKnifeFinally([&]{
        acquiring=attacking=nullptr;
        auto& boundary=soundScope.emplace(sound,observeSound.load(std::memory_order_relaxed));
        chestScope.emplace();
        return body(boundary);
    },[&](bool) noexcept {
        // Explicit destruction here also covers SEH under /EHsc.
        chestScope.reset();
        soundScope.reset();
        acquiring=previousAcquire;
        attacking=previousAttack;
        active.fetch_sub(1,std::memory_order_acq_rel);
    });
}

template<class Scope,class Body>
std::uintptr_t RunSelectedKnifeChild(std::atomic<unsigned>& active,Scope* parent,const Body& body) {
    active.fetch_add(1,std::memory_order_acq_rel);
    return RunSelectedKnifeFinally(body,[&](bool returned) noexcept {
        if(!returned&&parent)parent->childFault=true;
        active.fetch_sub(1,std::memory_order_acq_rel);
    });
}

template<class Original,class Abort>
std::uintptr_t ForwardAdmittedSelectedKnife(bool& originalReturned,const bool& childFault,
    const Original& original,const Abort& abort) {
    return RunSelectedKnifeFinally(original,[&](bool returned) noexcept {
        originalReturned=returned;
        if(!returned||childFault)abort();
    });
}
}
#endif
