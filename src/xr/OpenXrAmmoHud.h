#pragma once
#include "fvr/graphics/BodyPropFrame.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr_platform.h>
#include <vector>
namespace fvr::xr {
// Host-only transparent quad. Counts never enter the retained world image, so
// expiry/menu/focus/owner loss can hide them without replacing either eye.
class OpenXrAmmoHud {
public:
    ~OpenXrAmmoHud();
    bool Initialize(XrInstance,XrSession,PFN_xrGetInstanceProcAddr,ID3D11Device*,std::int64_t format)noexcept;
    void Receive(const graphics::BodyPropFrame*,const graphics::PairTicket&,std::int64_t now)noexcept;
    void Reset()noexcept {presentation_.Reset();}
    const XrCompositionLayerQuad* Layer(XrSpace headSpace,std::uint64_t space,
        std::int64_t (*clockNs)()noexcept,bool active)noexcept;
    std::uint64_t Uploads()const noexcept{return uploads_;}
    std::uint64_t Errors()const noexcept{return errors_;}
    std::uint64_t ValidSamples()const noexcept{return validSamples_;}
    std::uint64_t InvalidSamples()const noexcept{return invalidSamples_;}
private:
    bool Upload(const graphics::AmmoCounterSample*)noexcept;
    XrSwapchain chain_=XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> images_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    graphics::AmmoCounterPresentation presentation_;
    graphics::AmmoCounterBitmap bitmap_;
    XrCompositionLayerQuad layer_{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::int32_t loaded_=-1,reserve_=-1;
    std::uint32_t pendingIndex_=0;
    bool pending_=false,hasImage_=false,failed_=false;
    std::uint64_t uploads_=0,errors_=0;
    std::uint64_t validSamples_=0,invalidSamples_=0;
#define FIELD(n) PFN_xr##n n=nullptr;
    FIELD(CreateSwapchain) FIELD(DestroySwapchain) FIELD(EnumerateSwapchainImages)
    FIELD(AcquireSwapchainImage) FIELD(WaitSwapchainImage) FIELD(ReleaseSwapchainImage)
#undef FIELD
};
} // namespace fvr::xr
