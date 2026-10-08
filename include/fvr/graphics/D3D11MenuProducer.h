#pragma once
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/ipc/MenuChannel.h"
#include <wrl/client.h>
namespace fvr::graphics {
// Called only on the native graphics thread, after native menu composition.
// Two transport copies contain the same 2D raster; they are consumed as one
// OpenXR quad, never labelled or submitted as stereoscopic world images.
class D3D11MenuProducer {
public:
    void Pump(IDXGISwapChain*,ipc::MenuChannel&)noexcept;
    void Reset()noexcept;
    std::uint64_t Captured()const noexcept{return captured_;}
    std::uint64_t Published()const noexcept{return published_;}
    std::uint64_t Failed()const noexcept{return failed_;}
    std::uint64_t Discarded()const noexcept{return discarded_;}
private:
    D3D11PairProducer producer_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> scratch_;
    ipc::MenuSurface pending_{};
    std::uint64_t epoch_=0,frame_=0,captured_=0,published_=0,failed_=0,discarded_=0;
    unsigned width_=0,height_=0,format_=0;
    bool exporting_=false,publishing_=false,awaiting_=false;
};
}
