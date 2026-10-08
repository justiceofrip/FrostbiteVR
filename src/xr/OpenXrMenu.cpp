#include "OpenXrMenu.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <iostream>
namespace fvr::xr {
namespace {
XrPosef Pose(const math::Pose& p){return {{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},{p.position.x,p.position.y,p.position.z}};}
std::optional<math::Pose> Beam(math::Vec3 a,math::Vec3 b,math::Vec3 eye,float& length){
    const auto cross=[](math::Vec3 x,math::Vec3 y){return math::Vec3{x.y*y.z-x.z*y.y,x.z*y.x-x.x*y.z,x.x*y.y-x.y*y.x};};
    const auto unit=[](math::Vec3 p)->std::optional<math::Vec3>{const float n=std::hypot(p.x,p.y,p.z);if(!std::isfinite(n)||n<.0001f)return {};return math::Vec3{p.x/n,p.y/n,p.z/n};};
    length=std::hypot(b.x-a.x,b.y-a.y,b.z-a.z);const auto up=unit({b.x-a.x,b.y-a.y,b.z-a.z});if(!up)return {};
    math::Pose out;out.position={(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f};
    auto right=unit(cross(*up,{eye.x-out.position.x,eye.y-out.position.y,eye.z-out.position.z}));
    if(!right)right=unit(cross(*up,{0,1,0}));if(!right)right=unit(cross(*up,{1,0,0}));if(!right)return {};
    const auto normal=cross(*right,*up);const float m[3][3]={{right->x,up->x,normal.x},{right->y,up->y,normal.y},{right->z,up->z,normal.z}};
    auto& q=out.orientation;const float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0){const float s=std::sqrt(trace+1)*2;q={ (m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,.25f*s};}
    else if(m[0][0]>m[1][1]&&m[0][0]>m[2][2]){const float s=std::sqrt(1+m[0][0]-m[1][1]-m[2][2])*2;q={.25f*s,(m[0][1]+m[1][0])/s,(m[0][2]+m[2][0])/s,(m[2][1]-m[1][2])/s};}
    else if(m[1][1]>m[2][2]){const float s=std::sqrt(1+m[1][1]-m[0][0]-m[2][2])*2;q={(m[0][1]+m[1][0])/s,.25f*s,(m[1][2]+m[2][1])/s,(m[0][2]-m[2][0])/s};}
    else{const float s=std::sqrt(1+m[2][2]-m[0][0]-m[1][1])*2;q={(m[0][2]+m[2][0])/s,(m[1][2]+m[2][1])/s,.25f*s,(m[1][0]-m[0][1])/s};}
    return math::MakeRelativePose(math::Pose{},out)?std::optional<math::Pose>{out}:std::nullopt;
}
}
OpenXrMenu::~OpenXrMenu(){Drop(panel_);Drop(white_);}
void OpenXrMenu::Drop(Chain& c){if(c.handle&&DestroySwapchain)DestroySwapchain(c.handle);c={};}
void OpenXrMenu::Initialize(XrInstance instance,XrSession session,PFN_xrGetInstanceProcAddr get,ID3D11Device* device,ipc::MenuChannel* channel){
    session_=session;channel_=channel;device_=device;device_->GetImmediateContext(&context_);
#define LOAD(n) {PFN_xrVoidFunction raw=nullptr;if(XR_FAILED(get(instance,"xr" #n,&raw))||!raw)throw std::runtime_error("Missing menu XR function");n=reinterpret_cast<PFN_xr##n>(raw);}
    LOAD(CreateSwapchain) LOAD(DestroySwapchain) LOAD(EnumerateSwapchainImages) LOAD(EnumerateSwapchainFormats)
    LOAD(AcquireSwapchainImage) LOAD(WaitSwapchainImage) LOAD(ReleaseSwapchainImage)
#undef LOAD
    std::uint32_t count=0;if(XR_FAILED(EnumerateSwapchainFormats(session_,0,&count,nullptr))||count>4096)throw std::runtime_error("Menu formats unavailable");
    formats_.resize(count);if(XR_FAILED(EnumerateSwapchainFormats(session_,count,&count,formats_.data())))throw std::runtime_error("Menu formats unavailable");
    std::cerr<<"menu_supported_formats=";for(const auto format:formats_)std::cerr<<format<<",";std::cerr<<'\n';
}
bool OpenXrMenu::Allocate(Chain& c,unsigned width,unsigned height,std::int64_t format){
    const auto sourceFormat=format;
    if(std::find(formats_.begin(),formats_.end(),format)==formats_.end()){
        // XR may offer only the SRGB view of BC2's UNORM backbuffer format.
        // Copying within a DXGI format family preserves its encoded texel bytes;
        // it neither swaps RGBA/BGRA channels nor performs a gamma conversion.
        const auto compatible=std::find_if(formats_.begin(),formats_.end(),[&](const auto candidate){
            return candidate>=0&&candidate<=UINT32_MAX&&
                (candidate==28||candidate==29||candidate==87||candidate==91)&&
                graphics::CopyCompatibleFormats(std::uint32_t(sourceFormat),std::uint32_t(candidate));});
        if(compatible==formats_.end())return false;format=*compatible;
    }
    if(c.handle&&c.width==width&&c.height==height&&c.format==format)return true;
    Drop(c);
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format=format;info.sampleCount=info.faceCount=info.arraySize=info.mipCount=1;info.width=width;info.height=height;
    if(XR_FAILED(CreateSwapchain(session_,&info,&c.handle)))return false;
    std::uint32_t n=0;if(XR_FAILED(EnumerateSwapchainImages(c.handle,0,&n,nullptr))||!n||n>64){Drop(c);return false;}
    c.images.assign(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(XR_FAILED(EnumerateSwapchainImages(c.handle,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(c.images.data())))){Drop(c);return false;}
    c.width=width;c.height=height;c.format=format;
    std::cerr<<"menu_swapchain source_format="<<sourceFormat<<" target_format="<<format<<" width="<<width<<" height="<<height<<'\n';return true;
}
void OpenXrMenu::Failure(const char* stage,std::int64_t code,const Chain& chain,ID3D11Texture2D* image){
    // Retain the first instance of a repeated failure instead of flooding each
    // frame. Include the actual runtime texture format to distinguish typeless
    // storage from the typed format negotiated with OpenXR.
    if(lastErrorStage_==stage&&lastErrorCode_==code)return;
    lastErrorStage_=stage;lastErrorCode_=code;D3D11_TEXTURE2D_DESC desc{};if(image)image->GetDesc(&desc);
    std::cerr<<"menu_failure stage="<<stage<<" result="<<code<<" swapchain_format="<<chain.format
        <<" texture_format="<<unsigned(desc.Format)<<" width="<<chain.width<<" height="<<chain.height<<'\n';
}
bool OpenXrMenu::Upload(Chain& chain,ID3D11Texture2D* source){
    std::uint32_t index=0;XrSwapchainImageAcquireInfo a{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    const auto acquire=AcquireSwapchainImage(chain.handle,&a,&index);
    if(XR_FAILED(acquire)){Failure(source?"panel_acquire":"pointer_acquire",acquire,chain);return false;}
    XrSwapchainImageWaitInfo w{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};w.timeout=50000000;
    const auto wait=WaitSwapchainImage(chain.handle,&w);
    if(wait!=XR_SUCCESS){Failure(source?"panel_wait":"pointer_wait",wait,chain);throw std::runtime_error("Menu swapchain wait failed; session must retire acquired image");}
    bool okay=index<chain.images.size();
    if(!okay)Failure(source?"panel_image_index":"pointer_image_index",index,chain);
    if(okay&&source)context_->CopyResource(chain.images[index].texture,source);
    else if(okay){
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        // Runtime images may use typeless storage. RTV inference with nullptr
        // fails in that case, even when the negotiated XR format is SRGB/UNORM.
        D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT(chain.format);view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        const auto hr=device_->CreateRenderTargetView(chain.images[index].texture,&view,&rtv);okay=SUCCEEDED(hr);
        if(okay){const float color[]={.1f,.8f,1.f,1.f};context_->ClearRenderTargetView(rtv.Get(),color);}
        else Failure("pointer_rtv",hr,chain,chain.images[index].texture);
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};const auto result=ReleaseSwapchainImage(chain.handle,&release);
    if(XR_FAILED(result))Failure(source?"panel_release":"pointer_release",result,chain);
    return XR_SUCCEEDED(result)&&okay;
}
bool OpenXrMenu::Frame(const interaction::InputFrame& input){
    const unsigned menuInputState=(input.focused?1u:0u)|(input.headValid?2u:0u)|
        ((input.hands[0].active&interaction::MenuClick)?4u:0u)|((input.hands[0].held&interaction::MenuClick)?8u:0u);
    if(menuInputState!=lastMenuInputState_){
        std::cerr<<"menu_input focused="<<input.focused<<" head_valid="<<input.headValid
            <<" button_active="<<bool(menuInputState&4)<<" button_held="<<bool(menuInputState&8)<<'\n';
        lastMenuInputState_=menuInputState;
    }
    ipc::MenuState state{};const auto read=channel_?channel_->ReadState(state):ipc::ChannelResult::Closed;
    if(read==ipc::ChannelResult::Ok)retainedState_=state;
    else if(read==ipc::ChannelResult::Busy)state=retainedState_;
    else retainedState_={};
    LARGE_INTEGER nowQpc{};const bool known=QueryPerformanceCounter(&nowQpc)&&ipc::ValidMenuState(state)&&
        state.deadlineQpc>nowQpc.QuadPart;
    // Producer writes and reads share a zero-wait mutex. Contention is not a
    // menu close: keep the last state only until its ORIGINAL source deadline.
    // Busy never renews it; expiry, Closed and an actual Gameplay state win.
    if(!known){retainedState_={};state={};}
    const bool active=known&&state.mode==ipc::MenuMode::Menu;
    if(feedbackPending_){const auto result=channel_->SurfaceConsumed(feedback_,feedbackConsumed_);if(result!=ipc::ChannelResult::Busy)feedbackPending_=false;}
    if(!active||!input.focused||!input.headValid||imageEpoch_!=state.epoch)hasImage_=false;
    ipc::MenuSurface surface;
    if(channel_&&!feedbackPending_&&channel_->TakeSurface(surface)==ipc::ChannelResult::Ok){
        feedback_=surface.ticket;feedbackPending_=true;feedbackConsumed_=false;
        const auto& d=surface.descriptor;
        if(active&&surface.state.epoch==state.epoch&&input.focused&&input.headValid){
            if(std::memcmp(&opened_,&d,sizeof(d))){
                opened_={};consumer_.Reset();hasImage_=false;
                if(consumer_.Open(device_.Get(),d)==graphics::TransferResult::Ok){
                    D3D11_TEXTURE2D_DESC desc{};desc.Width=d.width;desc.Height=d.height;desc.Format=DXGI_FORMAT(d.format);desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
                    bool okay=true;for(auto& s:scratch_){s.Reset();okay&=SUCCEEDED(device_->CreateTexture2D(&desc,nullptr,&s));}if(okay)opened_=d;
                }
            }
            if(graphics::Valid(opened_)){
                const std::array<graphics::TextureSlice,2> slices{{{scratch_[0].Get(),0},{scratch_[1].Get(),0}}};
                feedbackConsumed_=consumer_.Copy(surface.ticket,slices)==graphics::TransferResult::Ok;
                if(feedbackConsumed_&&Allocate(panel_,d.width,d.height,d.format)&&Upload(panel_,scratch_[0].Get())){
                    hasImage_=true;imageEpoch_=state.epoch;lastImageWall_=GetTickCount64();++frames_;
                }else{hasImage_=false;++errors_;}
            }
        }
        const auto result=channel_->SurfaceConsumed(feedback_,feedbackConsumed_);if(result!=ipc::ChannelResult::Busy)feedbackPending_=false;
    }
    if(hasImage_&&GetTickCount64()-lastImageWall_>250)hasImage_=false;
    // The native BC2 logical mapping is verified for a 16:9 raster only.
    // Keep an unsupported-aspect menu visible, but suppress pointing/clicks
    // until its actual viewport/letterbox conversion has its own evidence.
    auto pointerInput=input;
    if(hasImage_&&(state.logicalWidth!=1280||state.logicalHeight!=720||
       std::uint64_t(panel_.width)*720!=std::uint64_t(panel_.height)*1280))pointerInput.hands[1].aimTracked=false;
    current_=pointer_.Update(pointerInput,known?&state:nullptr,hasImage_?panel_.width:0,hasImage_?panel_.height:0);
    if(current_.control.toggle!=lastToggle_){
        std::cerr<<"menu_toggle sequence="<<current_.control.toggle<<" left_face_chord="<<current_.suppressLeftFaceButtons<<'\n';lastToggle_=current_.control.toggle;
    }
    if(channel_)channel_->PublishControl(current_.control);
    const auto now=GetTickCount64();
    if(current_.pointing&&!pointerReady_&&now>=pointerRetryWall_){
        // Separate tiny texture, so pointer updates never overwrite native UI.
        // A failed initialization must neither submit uninitialized pixels nor
        // destroy/recreate its swapchain on every XR frame.
        pointerReady_=Allocate(white_,2,2,panel_.format)&&Upload(white_,nullptr);
        if(!pointerReady_){pointerRetryWall_=now+500;++errors_;}
    }
    if(current_.pointing){float length=0;const auto beam=Beam(current_.rayStart,current_.rayEnd,input.head.position,length);
        layers_[2]={XR_TYPE_COMPOSITION_LAYER_QUAD};if(beam&&pointerReady_&&white_.handle){layers_[2].pose=Pose(*beam);layers_[2].size={.0025f,length};}
    }
    return active;
}
void OpenXrMenu::Layers(XrSpace space,std::vector<const XrCompositionLayerBaseHeader*>& out){
    if(!current_.visible||!hasImage_||!panel_.handle)return;
    auto& panel=layers_[0];panel={XR_TYPE_COMPOSITION_LAYER_QUAD};panel.space=space;panel.eyeVisibility=XR_EYE_VISIBILITY_BOTH;panel.pose=Pose(current_.panel);panel.size={current_.width,current_.height};
    panel.subImage.swapchain=panel_.handle;panel.subImage.imageRect.extent={std::int32_t(panel_.width),std::int32_t(panel_.height)};out.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&panel));
    if(current_.pointing&&pointerReady_&&white_.handle){
        auto& cursor=layers_[1];cursor={XR_TYPE_COMPOSITION_LAYER_QUAD};cursor.space=space;cursor.eyeVisibility=XR_EYE_VISIBILITY_BOTH;cursor.pose=Pose(current_.cursor);cursor.size={.012f,.012f};cursor.subImage.swapchain=white_.handle;cursor.subImage.imageRect.extent={2,2};
        out.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cursor));
        auto& beam=layers_[2];if(beam.size.height>0){beam.space=space;beam.eyeVisibility=XR_EYE_VISIBILITY_BOTH;beam.subImage=cursor.subImage;out.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&beam));}
    }
}
}
