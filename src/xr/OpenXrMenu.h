#pragma once
#include "fvr/ipc/MenuChannel.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/interaction/MenuPointer.h"
#include "fvr/interaction/MenuOwnershipPolicy.h"
#include "fvr/xr/MenuFailureDiagnostics.h"
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
    // True also suppresses world submission while a passive raster owns display.
    bool Frame(const interaction::InputFrame&);
    // Rechecks the original native state lease at world admission. Frame()
    // being false alone is not evidence that the native world is available.
    bool WorldAvailable()const noexcept;
    void FilterGameplayInput(interaction::InputFrame& input)const noexcept {
        current_.FilterGameplayInput(input);input.menu=currentOwnership_;
    }
    void Layers(XrSpace,std::vector<const XrCompositionLayerBaseHeader*>&);
    std::uint64_t Frames()const{return frames_;}std::uint64_t Errors()const{return errors_;}
    const OpenXrMenuFailure& FirstFailure()const noexcept{return firstFailure_;}
private:
    friend struct OpenXrMenuTestAccess;
    struct Chain {XrSwapchain handle=XR_NULL_HANDLE;std::vector<XrSwapchainImageD3D11KHR> images;unsigned width=0,height=0;std::int64_t format=0;};
    XrSession session_=XR_NULL_HANDLE;ipc::MenuChannel* channel_=nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Chain panel_,white_;std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> scratch_;
    graphics::D3D11PairConsumer consumer_;graphics::TextureDescriptor opened_{};
    std::vector<std::int64_t> formats_;interaction::MenuPointer pointer_;
    ipc::MenuState retainedState_{};
    ipc::MenuMode imageMode_=ipc::MenuMode::Unknown;
    interaction::MenuOwnershipPolicy ownership_;
    interaction::InputMenuOwnership currentOwnership_{};
    std::int64_t frequency_=0;
    interaction::MenuPointerFrame current_{};std::array<XrCompositionLayerQuad,3> layers_{};
    graphics::PairTicket feedback_{};bool feedbackPending_=false,feedbackConsumed_=false,hasImage_=false,pointerReady_=false;
    std::uint64_t imageEpoch_=0,lastImageWall_=0,frames_=0,errors_=0,lastToggle_=0,pointerRetryWall_=0;
    std::string lastErrorStage_;std::int64_t lastErrorCode_=0;
    unsigned lastMenuInputState_=~0u;
    OpenXrMenuFailure firstFailure_{},failureContext_{};
    ipc::MenuSurface imageSurface_{};
    // One immutable source lease, copied only into menu-owned scratch. The
    // producer may not reuse its resources until exact completion/cancellation.
    ipc::MenuSurface pendingSurface_{};
    std::uint64_t pendingSpace_=0,imageSpace_=0;
    bool surfacePending_=false;
#define FIELD(n) PFN_xr##n n=nullptr;
    FIELD(CreateSwapchain) FIELD(DestroySwapchain) FIELD(EnumerateSwapchainImages) FIELD(EnumerateSwapchainFormats)
    FIELD(AcquireSwapchainImage) FIELD(WaitSwapchainImage) FIELD(ReleaseSwapchainImage)
#undef FIELD
    bool Allocate(Chain&,unsigned,unsigned,std::int64_t);
    bool Upload(Chain&,ID3D11Texture2D* source);
    void Drop(Chain&);
    void Failure(const char* stage,std::int64_t code,const Chain&,ID3D11Texture2D* image=nullptr);
    void StartStage()noexcept;
    void FlushFeedback()noexcept;
    void FinishSurface(bool consumed)noexcept;
    void AdvanceSurface(const interaction::InputFrame&,const ipc::MenuState&,bool active);
    void RetainFailure(const char* stage,const char* domain,std::int64_t code,const Chain&,
                      ID3D11Texture2D* image=nullptr,std::int64_t transfer=-1,bool counted=true)noexcept;
};
}
