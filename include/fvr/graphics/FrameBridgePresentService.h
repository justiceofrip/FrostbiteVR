#pragma once
#include <atomic>
#include <cstdint>

namespace fvr::graphics {
struct FrameBridgePresentIdentity {
    std::uintptr_t renderer=0,rendererVtable=0,device=0,context=0,swapchain=0;
    std::uint32_t thread=0;
    bool operator==(const FrameBridgePresentIdentity&)const noexcept=default;
    bool Valid()const noexcept{return renderer&&rendererVtable&&device&&context&&swapchain&&thread;}
};
struct FrameBridgePresentBoundary {
    FrameBridgePresentIdentity current{};
    std::uintptr_t presentedSwapchain=0;
    bool enabled=false,lifetimeHeld=false,bridgeOwnerCurrent=false,connected=false;
    bool fatal=false,test=false,drawingNativeView=false,nativeStateRestored=false;
};
inline bool AdmitsFrameBridgePresent(const FrameBridgePresentIdentity& expected,
                                    const FrameBridgePresentBoundary& boundary)noexcept {
    return expected.Valid()&&boundary.current==expected&&
        boundary.presentedSwapchain==expected.swapchain&&boundary.enabled&&
        boundary.lifetimeHeld&&boundary.bridgeOwnerCurrent&&boundary.connected&&
        !boundary.fatal&&!boundary.test&&!boundary.drawingNativeView&&boundary.nativeStateRestored;
}

// The scope spans the original Present as well as its preceding callbacks.
// Only Owned() admits any added graphics work at a Present callback. Foreign
// and nested callbacks must call the original Present without that work.
// Each outer boundary has one
// before opportunity and one after an exactly successful original Present.
// Every opportunity needs fresh ownership/restoration evidence. Only existing
// work is pumped; neither phase admits a new frame.
struct FrameBridgePresentOmissionSnapshot {
    std::uint64_t foreignThread=0,reentrant=0;
    bool firstAvailable=false,firstForeignThread=false;
    std::uint32_t actualThread=0,expectedThread=0;
    std::uintptr_t presentedSwapchain=0,expectedSwapchain=0;
};
class FrameBridgePresentService {
public:
    // Safe while callbacks are active: a single winner publishes immutable
    // first-offender fields with release/acquire; counters remain atomic.
    FrameBridgePresentOmissionSnapshot Omissions()const noexcept {
        FrameBridgePresentOmissionSnapshot out;
        out.foreignThread=foreignThread_.load(std::memory_order_relaxed);
        out.reentrant=reentrant_.load(std::memory_order_relaxed);
        if(firstState_.load(std::memory_order_acquire)==2){
            out.firstAvailable=true;out.firstForeignThread=first_.firstForeignThread;
            out.actualThread=first_.actualThread;out.expectedThread=first_.expectedThread;
            out.presentedSwapchain=first_.presentedSwapchain;out.expectedSwapchain=first_.expectedSwapchain;
        }
        return out;
    }
    class Scope {
    public:
        Scope(FrameBridgePresentService& owner,bool ownerThread)noexcept {
            if(ownerThread&&!owner.active_.test_and_set(std::memory_order_acquire))owner_=&owner;
        }
        Scope(FrameBridgePresentService& owner,std::uint32_t actualThread,
              std::uint32_t expectedThread,std::uintptr_t presentedSwapchain,
              std::uintptr_t expectedSwapchain)noexcept
            :Scope(owner,actualThread!=0&&actualThread==expectedThread){
            if(!Owned())owner.RecordOmission(actualThread==0||actualThread!=expectedThread,
                actualThread,expectedThread,presentedSwapchain,expectedSwapchain);
        }
        ~Scope(){if(owner_)owner_->active_.clear(std::memory_order_release);}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
        bool Owned()const noexcept{return owner_!=nullptr;}
        template<class Pump> bool Run(const FrameBridgePresentIdentity& expected,
                                     const FrameBridgePresentBoundary& boundary,Pump&& pump)noexcept {
            static_assert(noexcept(pump()),"Present service must not throw");
            if(beforeServiced_||afterReached_||!Admitted(expected,boundary))return false;
            beforeServiced_=true;pump();return true;
        }
        // HRESULT S_OK is exactly zero; other success statuses (including
        // occlusion), errors and repeated completion reports cannot service.
        template<class Pump> bool RunAfterPresent(std::int32_t result,
                                     const FrameBridgePresentIdentity& expected,
                                     const FrameBridgePresentBoundary& boundary,Pump&& pump)noexcept {
            static_assert(noexcept(pump()),"Present service must not throw");
            if(afterReached_)return false;
            afterReached_=true;
            if(result!=0||!Admitted(expected,boundary))return false;
            pump();return true;
        }
    private:
        bool Admitted(const FrameBridgePresentIdentity& expected,
                      const FrameBridgePresentBoundary& boundary)const noexcept {
            return owner_&&AdmitsFrameBridgePresent(expected,boundary);
        }
        FrameBridgePresentService* owner_=nullptr;
        bool beforeServiced_=false,afterReached_=false;
    };
private:
    void RecordOmission(bool foreignThread,std::uint32_t actualThread,
                        std::uint32_t expectedThread,std::uintptr_t presentedSwapchain,
                        std::uintptr_t expectedSwapchain)noexcept {
        (foreignThread?foreignThread_:reentrant_).fetch_add(1,std::memory_order_relaxed);
        unsigned empty=0;
        if(firstState_.compare_exchange_strong(empty,1,std::memory_order_acquire)){
            first_.firstForeignThread=foreignThread;first_.actualThread=actualThread;
            first_.expectedThread=expectedThread;first_.presentedSwapchain=presentedSwapchain;
            first_.expectedSwapchain=expectedSwapchain;
            firstState_.store(2,std::memory_order_release);
        }
    }
    std::atomic_flag active_=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> foreignThread_=0,reentrant_=0;
    std::atomic<unsigned> firstState_=0;
    FrameBridgePresentOmissionSnapshot first_{};
};
}
