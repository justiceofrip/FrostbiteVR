#pragma once
#include "fvr/ipc/MenuChannel.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/interaction/MenuPointer.h"
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
#include <string>
namespace fvr::xr {
class OpenXrMenu {
public:
    ~OpenXrMenu();
    void Initialize(XrInstance,XrSession,PFN_xrGetInstanceProcAddr,ID3D11Device*,ipc::MenuChannel*);
    // Always forwards release/focus state, including frames without a world pair.
    bool Frame(const interaction::InputFrame&);
    void FilterGameplayInput(interaction::InputFrame& input)const noexcept {current_.FilterGameplayInput(input);}
    void Layers(XrSpace,std::vector<const XrCompositionLayerBaseHeader*>&);
    std::uint64_t Frames()const{return frames_;}std::uint64_t Errors()const{return errors_;}
private:
    struct Chain {XrSwapchain handle=XR_NULL_HANDLE;std::vector<XrSwapchainImageD3D11KHR> images;unsigned width=0,height=0;std::int64_t format=0;};
    XrSession session_=XR_NULL_HANDLE;ipc::MenuChannel* channel_=nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Chain panel_,white_;std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> scratch_;
    graphics::D3D11PairConsumer consumer_;graphics::TextureDescriptor opened_{};
    std::vector<std::int64_t> formats_;interaction::MenuPointer pointer_;
    ipc::MenuState retainedState_{};
    interaction::MenuPointerFrame current_{};std::array<XrCompositionLayerQuad,3> layers_{};
    graphics::PairTicket feedback_{};bool feedbackPending_=false,feedbackConsumed_=false,hasImage_=false,pointerReady_=false;
    std::uint64_t imageEpoch_=0,lastImageWall_=0,frames_=0,errors_=0,lastToggle_=0,pointerRetryWall_=0;
    std::string lastErrorStage_;std::int64_t lastErrorCode_=0;
    unsigned lastMenuInputState_=~0u;
#define FIELD(n) PFN_xr##n n=nullptr;
    FIELD(CreateSwapchain) FIELD(DestroySwapchain) FIELD(EnumerateSwapchainImages) FIELD(EnumerateSwapchainFormats)
    FIELD(AcquireSwapchainImage) FIELD(WaitSwapchainImage) FIELD(ReleaseSwapchainImage)
#undef FIELD
    bool Allocate(Chain&,unsigned,unsigned,std::int64_t);
    bool Upload(Chain&,ID3D11Texture2D* source);
    void Drop(Chain&);
    void Failure(const char* stage,std::int64_t code,const Chain&,ID3D11Texture2D* image=nullptr);
};
}
