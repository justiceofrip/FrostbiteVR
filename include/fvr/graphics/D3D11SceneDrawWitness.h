#pragma once
#include "fvr/graphics/SceneDrawWitness.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <span>
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
namespace fvr::graphics {
// Private, opt-in read-only observation. No pipeline binds, native writes,
// Flush, query waits, or file IO are performed on the graphics callback.
class D3D11SceneDrawWitness {
public:
    D3D11SceneDrawWitness();~D3D11SceneDrawWitness();
    bool Enable(bool explicitDiagnostic)noexcept;
    // Worker only: monotonically request 1..3 after arranging each scene.
    // The same ordinal window is used for all three scene states. Selection is
    // fixed by request 1 and cannot be changed silently for the other eye/pair.
    bool Request(unsigned sequence,unsigned firstDrawOrdinal=0)noexcept;
    unsigned Completed()const noexcept{return completed_.load(std::memory_order_acquire);}
    unsigned Failed()const noexcept{return failed_.load(std::memory_order_acquire);}
    void BeginEye(ID3D11DeviceContext*,const SceneWitnessKey&,unsigned eye,std::uint64_t view,
        std::span<const std::byte> expectedCamera={})noexcept;
    void Observe(ID3D11DeviceContext*,const SceneWitnessDrawArguments&)noexcept;
    // Exact texture about to be handed to FrameBridge::CaptureEye, not an
    // arbitrary target sampled during a different render pass.
    void CaptureColor(ID3D11DeviceContext*,ID3D11Texture2D*,const SceneWitnessKey&,unsigned eye)noexcept;
    void EndEye(ID3D11DeviceContext*,const SceneWitnessKey&,unsigned eye)noexcept;
    void Poll(ID3D11DeviceContext*)noexcept;
    // Worker: close admission, then retry until callbacks have drained. Pending
    // private copies are explicitly incomplete, never mapped/released from
    // this thread; their bounded refs remain in the game-lifetime module.
    bool Stop()noexcept;
    bool Write(const std::filesystem::path& directory)const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    std::atomic<bool> accepting_=false;
    std::atomic<unsigned> callbacks_=0,requested_=0,completed_=0,failed_=0;
    std::atomic<unsigned> firstDrawOrdinal_=0;
    struct Admission {
        D3D11SceneDrawWitness& owner;bool entered=false;
        explicit Admission(D3D11SceneDrawWitness&)noexcept;~Admission();
    };
};
}
