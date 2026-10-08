#include "OpenXrAmmoHud.h"
namespace fvr::xr {
OpenXrAmmoHud::~OpenXrAmmoHud(){if(chain_&&DestroySwapchain)DestroySwapchain(chain_);}
bool OpenXrAmmoHud::Initialize(XrInstance instance,XrSession session,PFN_xrGetInstanceProcAddr get,
    ID3D11Device* device,std::int64_t format)noexcept {
    if(chain_||!get||!device||!session||(format!=28&&format!=29&&format!=87&&format!=91))return false;
    try {
#define LOAD(n) {PFN_xrVoidFunction raw=nullptr;if(XR_FAILED(get(instance,"xr" #n,&raw))||!raw)return false;n=reinterpret_cast<PFN_xr##n>(raw);}
        LOAD(CreateSwapchain) LOAD(DestroySwapchain) LOAD(EnumerateSwapchainImages)
        LOAD(AcquireSwapchainImage) LOAD(WaitSwapchainImage) LOAD(ReleaseSwapchainImage)
#undef LOAD
        device->GetImmediateContext(&context_);if(!context_)return false;
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format=format;info.sampleCount=info.faceCount=info.arraySize=info.mipCount=1;
        info.width=graphics::AmmoCounterBitmap::Width;info.height=graphics::AmmoCounterBitmap::Height;
        if(XR_FAILED(CreateSwapchain(session,&info,&chain_)))return false;
        std::uint32_t count=0;if(XR_FAILED(EnumerateSwapchainImages(chain_,0,&count,nullptr))||!count||count>64)return false;
        images_.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        if(XR_FAILED(EnumerateSwapchainImages(chain_,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data()))))return false;
        for(const auto& image:images_){if(!image.texture)return false;D3D11_TEXTURE2D_DESC d{};image.texture->GetDesc(&d);
            if(d.Width!=info.width||d.Height!=info.height||d.ArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||
                !graphics::CopyCompatibleFormats(d.Format,std::uint32_t(format)))return false;}
        return true;
    }catch(...){return false;}
}
void OpenXrAmmoHud::Receive(const graphics::BodyPropFrame* frame,const graphics::PairTicket& ticket,std::int64_t now)noexcept {
    if(!frame||!graphics::BodyPropFrameMatches(*frame,ticket)){
        ++invalidSamples_;presentation_.Receive({},{},ticket.spaceGeneration,now);return;
    }
    if(presentation_.Receive(frame->eyes[0].ammo,frame->eyes[1].ammo,frame->spaceGeneration,now))++validSamples_;
    else ++invalidSamples_;
}
bool OpenXrAmmoHud::Upload(const graphics::AmmoCounterSample* sample)noexcept {
    if(!pending_){
        if(!sample)return false;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(XR_FAILED(AcquireSwapchainImage(chain_,&acquire,&pendingIndex_))){++errors_;return false;}
        pending_=true;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=0;
    const auto ready=WaitSwapchainImage(chain_,&wait);
    if(ready==XR_TIMEOUT_EXPIRED)return false; // Bounded retry; never block world rendering.
    if(XR_FAILED(ready)){++errors_;failed_=true;return false;}
    bool copied=sample&&pendingIndex_<images_.size();
    if(copied){
        copied=bitmap_.Render(sample->loaded,sample->reserve);
        if(copied)context_->UpdateSubresource(images_[pendingIndex_].texture,0,nullptr,bitmap_.pixels.data(),
                                            graphics::AmmoCounterBitmap::Width*4,0);
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const auto released=ReleaseSwapchainImage(chain_,&release);pending_=false;
    if(XR_FAILED(released)){++errors_;failed_=true;hasImage_=false;return false;}
    if(copied){loaded_=sample->loaded;reserve_=sample->reserve;hasImage_=true;++uploads_;}
    else hasImage_=false;
    return copied;
}
const XrCompositionLayerQuad* OpenXrAmmoHud::Layer(XrSpace headSpace,std::uint64_t space,
    std::int64_t (*clockNs)()noexcept,bool active)noexcept {
    if(!chain_||failed_||!clockNs)return nullptr;
    if(!active||!headSpace)presentation_.Reset();
    const auto* sample=active&&headSpace?presentation_.Select(space,clockNs()):nullptr;
    if(!sample){if(pending_)Upload(nullptr);return nullptr;}
    if(pending_||!hasImage_||loaded_!=sample->loaded||reserve_!=sample->reserve)if(!Upload(sample))return nullptr;
    if(!presentation_.Select(space,clockNs()))return nullptr;
    layer_={XR_TYPE_COMPOSITION_LAYER_QUAD};layer_.space=headSpace;
    layer_.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    layer_.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    layer_.subImage.swapchain=chain_;
    layer_.subImage.imageRect.extent={std::int32_t(graphics::AmmoCounterBitmap::Width),std::int32_t(graphics::AmmoCounterBitmap::Height)};
    layer_.pose.orientation.w=1;layer_.pose.position={0.f,-.30f,-1.f};
    layer_.size={.42f,.07875f};return &layer_;
}
} // namespace fvr::xr
