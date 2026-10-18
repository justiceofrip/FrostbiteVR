#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
namespace fvr::graphics {
// Graphics-thread-only, display-only copy of a completed, restored stereo pair.
// Never retains a swapchain buffer, reads pixels on the CPU, waits for the GPU,
// re-renders the world, changes pipeline bindings or writes the XR eye sources.
class D3D11DesktopMirror {
public:
    void ObserveOwner(std::uint64_t owner,std::uint64_t device,std::uint64_t space)noexcept;
    bool CommitLeft(ID3D11Texture2D*)noexcept;
    bool Present(IDXGISwapChain*,bool active,bool menuKnown,bool menuVisible,bool test)noexcept;
    // The same path is exposed for software-GPU pixel/state regression tests.
    bool Compose(ID3D11Texture2D*,bool active,bool menuKnown,bool menuVisible,bool test=false)noexcept;
    void Reset()noexcept;
    std::uint64_t Committed()const noexcept{return committed_;}
    std::uint64_t Copied()const noexcept{return copied_;}
    std::uint64_t Neutral()const noexcept{return neutral_;}
    std::uint64_t Failed()const noexcept{return failed_;}
private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> left_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    D3D11_TEXTURE2D_DESC description_{};
    std::uint64_t owner_=0,deviceEpoch_=0,space_=0;
    std::uint64_t committed_=0,copied_=0,neutral_=0,failed_=0;
    bool ready_=false;
};
}
