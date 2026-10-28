#include "OpenXrMenu.h"
#include "Test.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <thread>
#include <sstream>
using namespace fvr;
using Microsoft::WRL::ComPtr;
namespace fvr::xr {struct OpenXrMenuTestAccess {
    static bool Pending(const OpenXrMenu& p){return p.surfacePending_&&p.consumer_.HasPendingCopy();}
    static bool Feedback(const OpenXrMenu& p){return p.feedbackPending_;}
    static ipc::MenuSurface Surface(const OpenXrMenu& p){return p.pendingSurface_;}
    static ID3D11Texture2D* Scratch(const OpenXrMenu& p){return p.scratch_[0].Get();}
    static std::uint64_t ImageTime(const OpenXrMenu& p){return p.lastImageWall_;}
    static bool Reset(const OpenXrMenu& p){return !p.surfacePending_&&!p.consumer_.HasPendingCopy()&&
        !graphics::Valid(p.opened_)&&!p.scratch_[0]&&!p.scratch_[1];}
};}
namespace {
void Require(bool okay,const char* text){if(!okay)throw std::runtime_error(text);}
struct FakeChain {
    XrSwapchainCreateInfo info{};
    std::array<ComPtr<ID3D11Texture2D>,2> images;
    unsigned next=0,current=0,last=0;bool acquired=false,waited=false,timedOut=false,hasReleased=false;
};
// Only swapchain PFNs used by OpenXrMenu; no loader, SteamVR, action emulation,
// native game calls or relaxation of the existing two-eye runtime fixture.
struct FakeRuntime {
    ID3D11Device* device=nullptr;
    std::unordered_map<XrSwapchain,std::unique_ptr<FakeChain>> chains;
    unsigned errors=0,created=0,destroyed=0,acquired=0,waited=0,released=0,retired=0;
    bool failAcquire=false,timeoutWait=false,typelessImages=false,failPointerAcquire=false,failCreate=false,failImages=false;
    std::vector<std::int64_t> formats{DXGI_FORMAT_R8G8B8A8_UNORM};
    static FakeRuntime* current;
    static FakeChain* Find(XrSwapchain h){auto& r=*current;const auto it=r.chains.find(h);if(it!=r.chains.end())return it->second.get();++r.errors;return nullptr;}
    static XrResult XRAPI_CALL Create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){
        auto& r=*current;if(!info||!out)return XR_ERROR_VALIDATION_FAILURE;
        const auto flags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        if(info->type!=XR_TYPE_SWAPCHAIN_CREATE_INFO||info->next||info->usageFlags!=flags||info->createFlags||
           info->sampleCount!=1||info->faceCount!=1||info->arraySize!=1||info->mipCount!=1||!info->width||!info->height){++r.errors;return XR_ERROR_VALIDATION_FAILURE;}
        if(std::find(r.formats.begin(),r.formats.end(),info->format)==r.formats.end())return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
        if(r.failCreate){r.failCreate=false;return XR_ERROR_RUNTIME_FAILURE;}
        auto c=std::make_unique<FakeChain>();c->info=*info;
        D3D11_TEXTURE2D_DESC d{};d.Width=info->width;d.Height=info->height;d.Format=DXGI_FORMAT(info->format);
        d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(r.typelessImages){
            if(d.Format==DXGI_FORMAT_R8G8B8A8_UNORM||d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
            if(d.Format==DXGI_FORMAT_B8G8R8A8_UNORM||d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)d.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;
        }
        for(auto& image:c->images)if(FAILED(r.device->CreateTexture2D(&d,nullptr,&image)))return XR_ERROR_RUNTIME_FAILURE;
        *out=reinterpret_cast<XrSwapchain>(c.get());r.chains.emplace(*out,std::move(c));++r.created;return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL Destroy(XrSwapchain h){auto& r=*current;auto* c=Find(h);if(!c)return XR_ERROR_HANDLE_INVALID;
        if(c->acquired){if(c->timedOut)++r.retired;else ++r.errors;}r.chains.erase(h);++r.destroyed;return XR_SUCCESS;}
    static XrResult XRAPI_CALL Formats(XrSession,std::uint32_t capacity,std::uint32_t* count,std::int64_t* formats){
        if(!count)return XR_ERROR_VALIDATION_FAILURE;const auto& supported=current->formats;*count=std::uint32_t(supported.size());if(!capacity)return XR_SUCCESS;
        if(capacity<supported.size())return XR_ERROR_SIZE_INSUFFICIENT;
        if(!formats)return XR_ERROR_VALIDATION_FAILURE;std::copy(supported.begin(),supported.end(),formats);return XR_SUCCESS;}
    static XrResult XRAPI_CALL Images(XrSwapchain h,std::uint32_t capacity,std::uint32_t* count,XrSwapchainImageBaseHeader* images){
        auto* c=Find(h);if(!c||!count)return XR_ERROR_VALIDATION_FAILURE;*count=2;if(!capacity)return XR_SUCCESS;
        if(current->failImages){current->failImages=false;return XR_ERROR_RUNTIME_FAILURE;}
        if(capacity<2)return XR_ERROR_SIZE_INSUFFICIENT;auto* d=reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
        if(!d)return XR_ERROR_VALIDATION_FAILURE;for(unsigned n=0;n<2;++n){if(d[n].type!=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR){++current->errors;return XR_ERROR_VALIDATION_FAILURE;}d[n].texture=c->images[n].Get();}return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL Acquire(XrSwapchain h,const XrSwapchainImageAcquireInfo* info,std::uint32_t* index){
        auto* c=Find(h);if(!c||!info||!index)return XR_ERROR_VALIDATION_FAILURE;
        if(c->acquired){++current->errors;return XR_ERROR_CALL_ORDER_INVALID;}
        if(current->failAcquire){current->failAcquire=false;return XR_ERROR_RUNTIME_FAILURE;}
        if(current->failPointerAcquire&&c->info.width==2&&c->info.height==2)return XR_ERROR_RUNTIME_FAILURE;
        c->acquired=true;c->waited=c->timedOut=false;c->current=c->next++%2;*index=c->current;++current->acquired;return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL Wait(XrSwapchain h,const XrSwapchainImageWaitInfo* info){
        auto* c=Find(h);if(!c||!info)return XR_ERROR_VALIDATION_FAILURE;
        if(!c->acquired||c->waited){++current->errors;return XR_ERROR_CALL_ORDER_INVALID;}
        if(info->timeout<0||info->timeout>50000000){++current->errors;return XR_ERROR_VALIDATION_FAILURE;}
        if(current->timeoutWait){current->timeoutWait=false;c->timedOut=true;return XR_TIMEOUT_EXPIRED;}
        c->waited=true;++current->waited;return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL Release(XrSwapchain h,const XrSwapchainImageReleaseInfo*){
        auto* c=Find(h);if(!c)return XR_ERROR_HANDLE_INVALID;
        if(!c->acquired||!c->waited){++current->errors;return XR_ERROR_CALL_ORDER_INVALID;}
        c->last=c->current;c->hasReleased=true;c->acquired=c->waited=false;++current->released;return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL Get(XrInstance,const char* name,PFN_xrVoidFunction* out){
        if(!name||!out)return XR_ERROR_VALIDATION_FAILURE;*out=nullptr;
#define FN(n,f) if(!std::strcmp(name,"xr" #n))*out=reinterpret_cast<PFN_xrVoidFunction>(&f);
        FN(CreateSwapchain,Create) FN(DestroySwapchain,Destroy) FN(EnumerateSwapchainFormats,Formats)
        FN(EnumerateSwapchainImages,Images) FN(AcquireSwapchainImage,Acquire) FN(WaitSwapchainImage,Wait) FN(ReleaseSwapchainImage,Release)
#undef FN
        return *out?XR_SUCCESS:XR_ERROR_FUNCTION_UNSUPPORTED;
    }
};
FakeRuntime* FakeRuntime::current=nullptr;
struct Fixture {
    FakeRuntime& runtime;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ipc::FrameChannel frameChannel;ipc::MenuChannel host,producerChannel;
    graphics::D3D11PairProducer producer;std::array<ComPtr<ID3D11Texture2D>,2> images;
    xr::OpenXrMenu presenter;interaction::InputFrame input{};ipc::MenuState state{};ipc::MenuControl control{};
    graphics::PairTicket lastTicket{};
    std::uint64_t stateSequence=0,frame=0;unsigned width=32,height=18;
    explicit Fixture(FakeRuntime& r):runtime(r),device(r.device){
        device->GetImmediateContext(&context);Require(frameChannel.CreateHost(),"frame host");
        Require(host.CreateHost(frameChannel.Token())&&producerChannel.ConnectProducer(frameChannel.Token()),"menu channel");
        input.spaceGeneration=1;input.focused=input.headValid=true;input.head.position.y=1.6f;
        input.hands[0].active=interaction::MenuClick;input.hands[1].active=interaction::Trigger|interaction::Secondary;
        input.hands[1].aimTracked=true;input.hands[1].aim.position={0,1.6f,0};
        state.epoch=1;state.mode=ipc::MenuMode::Menu;state.logicalWidth=1280;state.logicalHeight=720;state.inputReady=1;
        ResetImages(width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1);
        presenter.Initialize(reinterpret_cast<XrInstance>(1),reinterpret_cast<XrSession>(2),FakeRuntime::Get,device.Get(),&host);
    }
    void ResetImages(unsigned w,unsigned h,DXGI_FORMAT format,std::uint64_t epoch,
                     graphics::TextureSharing sharing=graphics::TextureSharing::NamedKeyed){
        width=w;height=h;Require(producer.Create(device.Get(),w,h,format,epoch,sharing)==graphics::TransferResult::Ok,"GPU shared pair create");
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.Format=format;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        for(auto& image:images){image.Reset();Require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&image)),"source texture");}
    }
    static std::uint32_t Pixel(unsigned x,unsigned y,unsigned eye){return 0xff000000u|((eye+1)*41u<<16)|(y<<8)|x;}
    void State(){state.sequence=++stateSequence;Require(producerChannel.PublishState(state)==ipc::ChannelResult::Ok,"publish state");Require(host.ReadState(state)==ipc::ChannelResult::Ok,"read state");}
    graphics::PairTicket Surface(){
        for(unsigned eye=0;eye<2;++eye){std::vector<std::uint32_t> pixels(std::size_t(width)*height);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)pixels[y*width+x]=Pixel(x,y,eye);
            context->UpdateSubresource(images[eye].Get(),0,nullptr,pixels.data(),width*4,0);}
        graphics::PairTicket ticket;const std::array<graphics::TextureSlice,2> slices{{{images[0].Get(),0},{images[1].Get(),0}}};
        const auto sourceSpace=producer.Descriptor().sharing==graphics::TextureSharing::LegacyFenced?state.epoch:input.spaceGeneration;
        const graphics::FrameMetadata metadata{++frame,sourceSpace,input.generation,input.predictedNs};
        const auto end=GetTickCount64()+2000;
        graphics::TransferResult result;
        do{result=producer.Publish(slices,metadata,ticket);if(result==graphics::TransferResult::Busy)Sleep(1);}
        while(result==graphics::TransferResult::Busy&&GetTickCount64()<end);
        Require(result==graphics::TransferResult::Ok,"publish GPU pair");
        Require(producerChannel.PublishSurface({state,producer.Descriptor(),ticket})==ipc::ChannelResult::Ok,"publish menu surface");lastTicket=ticket;return ticket;
    }
    bool Step(bool surface=false){
        ++input.generation;input.predictedNs=1000000000+std::int64_t(input.generation)*11000000;State();
        graphics::PairTicket ticket;if(surface)ticket=Surface();const bool active=presenter.Frame(input);
        Require(producerChannel.ReadControl(control)==ipc::ChannelResult::Ok,"control feedback");
        Require(control.sequence==input.generation&&control.space==input.spaceGeneration,"control source identity");
        if(surface){const auto outcome=producerChannel.PollOutcome();Require(outcome==ipc::Outcome::Consumed,"GPU feedback consumed");producer.Acknowledge(ticket,true);}
        return active;
    }
    std::vector<const XrCompositionLayerBaseHeader*> Layers(){std::vector<const XrCompositionLayerBaseHeader*> layers;presenter.Layers(reinterpret_cast<XrSpace>(3),layers);return layers;}
    std::vector<std::uint32_t> Read(XrSwapchain chain){
        auto* c=FakeRuntime::Find(chain);Require(c&&c->hasReleased,"released image");auto* source=c->images[c->last].Get();
        D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;Require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"readback texture");context->CopyResource(staging.Get(),source);
        D3D11_MAPPED_SUBRESOURCE mapped{};Require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback map");
        std::vector<std::uint32_t> pixels(std::size_t(d.Width)*d.Height);
        for(unsigned y=0;y<d.Height;++y)std::memcpy(pixels.data()+y*d.Width,static_cast<const std::byte*>(mapped.pData)+y*mapped.RowPitch,d.Width*4);
        context->Unmap(staging.Get(),0);return pixels;
    }
};
// Hold the real menu channel mutex from another thread; same-thread Windows
// mutex acquisition is recursive and would not exercise ChannelResult::Busy.
class BusyMenuChannel {
public:
    explicit BusyMenuChannel(const std::wstring& token){
        mutex_=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,(L"Local\\FrostbiteVR.Menu.v1."+token+L".mutex").c_str());
        entered_=CreateEventW(nullptr,TRUE,FALSE,nullptr);release_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Require(mutex_&&entered_&&release_,"busy menu test handles");
        thread_=std::thread([this]{if(WaitForSingleObject(mutex_,2000)==WAIT_OBJECT_0){SetEvent(entered_);WaitForSingleObject(release_,3000);ReleaseMutex(mutex_);}});
        if(WaitForSingleObject(entered_,2000)!=WAIT_OBJECT_0){SetEvent(release_);thread_.join();throw std::runtime_error("busy menu mutex not acquired");}
    }
    ~BusyMenuChannel(){SetEvent(release_);thread_.join();CloseHandle(release_);CloseHandle(entered_);CloseHandle(mutex_);}
private:
    HANDLE mutex_=nullptr,entered_=nullptr,release_=nullptr;std::thread thread_;
};
const XrCompositionLayerQuad& Quad(const XrCompositionLayerBaseHeader* layer){Require(layer&&layer->type==XR_TYPE_COMPOSITION_LAYER_QUAD,"quad type");return *reinterpret_cast<const XrCompositionLayerQuad*>(layer);}
int PixelsAndLayers(FakeRuntime& runtime){
    Fixture f(runtime);CHECK(f.Step(true));auto layers=f.Layers();CHECK(layers.size()==3);CHECK(f.presenter.Frames()==1&&f.presenter.Errors()==0);
    CHECK(!f.presenter.FirstFailure().available);
    const auto panel=Quad(layers[0]),cursor=Quad(layers[1]),beam=Quad(layers[2]);
    for(const auto* layer:layers){const auto& q=Quad(layer);CHECK(q.space==reinterpret_cast<XrSpace>(3)&&q.eyeVisibility==XR_EYE_VISIBILITY_BOTH&&!q.layerFlags);
        CHECK(Near(q.pose.orientation.x*q.pose.orientation.x+q.pose.orientation.y*q.pose.orientation.y+q.pose.orientation.z*q.pose.orientation.z+q.pose.orientation.w*q.pose.orientation.w,1));}
    CHECK(panel.subImage.imageRect.extent.width==32&&panel.subImage.imageRect.extent.height==18);
    CHECK(Near(panel.size.width,2.2f)&&Near(panel.size.height,1.2375f)&&Near(panel.pose.position.z,-1.5f));
    CHECK(cursor.subImage.swapchain!=panel.subImage.swapchain&&cursor.subImage.swapchain==beam.subImage.swapchain);
    CHECK(Near(cursor.size.width,.012f)&&Near(cursor.pose.position.z,panel.pose.position.z+.002f));
    CHECK(Near(beam.size.width,.0025f)&&Near(beam.size.height,1.498f));
    CHECK(Near(beam.pose.position.z,-.749f)&&Near(beam.pose.position.y,1.6f));
    auto pixels=f.Read(panel.subImage.swapchain);for(unsigned y=0;y<18;++y)for(unsigned x=0;x<32;++x)CHECK(pixels[y*32+x]==Fixture::Pixel(x,y,0));
    const auto cyan=f.Read(cursor.subImage.swapchain);CHECK(cyan.size()==4);
    for(auto p:cyan){CHECK((p>>24)==255&&((p>>16)&255)==255&&std::abs(int((p>>8)&255)-204)<=1&&std::abs(int(p&255)-26)<=1);}
    const auto uploads=runtime.released;CHECK(f.Step());CHECK(runtime.released==uploads&&f.Layers().size()==3);
    f.input.hands[1].aim.position.x=.4f;CHECK(f.Step());layers=f.Layers();CHECK(layers.size()==3&&f.control.u>.5f);
    CHECK(f.Read(panel.subImage.swapchain)==pixels);CHECK(runtime.released==uploads);
    // Descriptor/size changes allocate a new menu chain and copy the new source.
    f.ResetImages(48,27,DXGI_FORMAT_R8G8B8A8_UNORM,2);CHECK(f.Step(true));layers=f.Layers();CHECK(layers.size()==3);
    const auto resized=Quad(layers[0]);CHECK(resized.subImage.imageRect.extent.width==48&&resized.subImage.imageRect.extent.height==27);
    CHECK(Near(resized.size.height,1.2375f));pixels=f.Read(resized.subImage.swapchain);CHECK(pixels.size()==1296&&pixels.back()==Fixture::Pixel(47,26,0));
    return 0;
}
int Lifecycle(FakeRuntime& runtime){
    Fixture f(runtime);CHECK(f.Step(true));f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);
    f.input.hands[1].aimTracked=false;CHECK(f.Step());CHECK(!(f.control.flags&(ipc::MenuDown|ipc::MenuPoint))&&f.Layers().size()==1);
    f.input.hands[1].aimTracked=true;CHECK(f.Step());CHECK(!(f.control.flags&ipc::MenuDown));
    f.input.hands[1].trigger=0;CHECK(f.Step());f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);
    f.input.focused=false;CHECK(f.Step());CHECK(f.control.flags==0&&f.Layers().empty());
    f.input.focused=true;CHECK(f.Step(true));CHECK(!(f.control.flags&ipc::MenuDown)&&f.Layers().size()==3);
    f.input.hands[1].trigger=0;CHECK(f.Step());f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);
    ++f.input.spaceGeneration;CHECK(f.Step());CHECK(!(f.control.flags&ipc::MenuDown)&&f.Layers().empty());
    CHECK(f.Step(true));
    f.input.hands[1].trigger=0;CHECK(f.Step());f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);
    ++f.state.epoch;CHECK(f.Step());CHECK(!(f.control.flags&ipc::MenuDown)&&f.Layers().empty());
    CHECK(f.Step(true));CHECK(!(f.control.flags&ipc::MenuDown)&&f.Layers().size()==3);
    f.input.hands[1].trigger=0;CHECK(f.Step());f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);
    f.state.mode=ipc::MenuMode::Gameplay;CHECK(!f.Step());CHECK(!(f.control.flags&(ipc::MenuDown|ipc::MenuPoint))&&f.Layers().empty());
    f.input.hands[0].held=interaction::MenuClick;CHECK(!f.Step());CHECK(f.control.toggle==1);
    CHECK(!f.Step());CHECK(f.control.toggle==1);
    f.state.mode=ipc::MenuMode::Menu;++f.state.epoch;f.input.hands[1].trigger=0;CHECK(f.Step(true));
    f.input.hands[1].held=interaction::Secondary;CHECK(f.Step());CHECK(f.control.cancel==1);CHECK(f.Step());CHECK(f.control.cancel==1);
    f.input.headValid=false;CHECK(f.Step());CHECK(f.control.flags==0&&f.Layers().empty());
    // A live state cannot keep an old raster alive indefinitely.
    f.input.headValid=true;CHECK(f.Step(true));Sleep(270);CHECK(f.Step());CHECK(f.Layers().empty()&&!(f.control.flags&ipc::MenuPoint));
    return 0;
}
int PassivePixelsAndControls(FakeRuntime& runtime){
    Fixture f(runtime);f.state.mode=ipc::MenuMode::Passive;f.state.inputReady=0;
    const auto created=runtime.created;
    CHECK(f.Step()&&f.Layers().empty()); // Display ownership precedes the first raster.
    auto gameplay=f.input;f.presenter.FilterGameplayInput(gameplay);
    CHECK(gameplay.menu.owner==interaction::InputMenuOwner::Pending);
    CHECK(!interaction::GameplayOwnsInput(gameplay.menu,gameplay.menu.deadlineNs-1));
    CHECK(f.Step(true));auto layers=f.Layers();CHECK(layers.size()==1&&runtime.created==created+1);
    const auto panel=Quad(layers[0]);const auto pixels=f.Read(panel.subImage.swapchain);
    for(unsigned y=0;y<f.height;++y)for(unsigned x=0;x<f.width;++x)
        CHECK(pixels[y*f.width+x]==Fixture::Pixel(x,y,0));
    f.input.hands[0].active|=interaction::Primary|interaction::Secondary;
    f.input.hands[0].held=interaction::MenuClick|interaction::Primary|interaction::Secondary;
    f.input.hands[1].held=interaction::Secondary;f.input.hands[1].trigger=.8f;
    for(unsigned n=0;n<64;++n){CHECK(f.Step());
        CHECK(f.control.flags==ipc::MenuFocused&&!f.control.toggle&&!f.control.cancel);}
    // Even an unchanged epoch cannot relabel cached passive pixels as an
    // interactive image or dispatch buttons held during the loading panel.
    f.state.mode=ipc::MenuMode::Menu;f.state.inputReady=1;
    CHECK(f.Step()&&f.Layers().empty());CHECK(!(f.control.flags&ipc::MenuDown));
    CHECK(f.Step(true)&&f.Layers().size()==3);CHECK(!f.control.toggle&&!f.control.cancel&&!(f.control.flags&ipc::MenuDown));
    f.input.hands[0].held=f.input.hands[1].held=0;f.input.hands[1].trigger=0;CHECK(f.Step());
    f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);
    f.state.mode=ipc::MenuMode::Passive;f.state.inputReady=0;
    CHECK(f.Step()&&f.Layers().empty()&&f.control.flags==ipc::MenuFocused);
    CHECK(f.Step(true)&&f.Layers().size()==1);
    f.input.focused=false;CHECK(f.Step()&&f.Layers().empty()&&f.control.flags==0);
    f.input.focused=true;++f.input.spaceGeneration;CHECK(f.Step(true)&&f.Layers().size()==1);
    CHECK(f.control.flags==ipc::MenuFocused&&f.presenter.Errors()==0);return 0;
}
int ChordGameplayFilter(FakeRuntime& runtime){
    Fixture f(runtime);f.state.mode=ipc::MenuMode::Gameplay;
    constexpr auto face=std::uint32_t(interaction::Primary|interaction::Secondary);
    f.input.hands[0].active|=face;CHECK(!f.Step());
    f.input.hands[0].held=face;CHECK(!f.Step());auto gameplay=f.input;f.presenter.FilterGameplayInput(gameplay);
    CHECK(gameplay.focused&&gameplay.hands[0].active==f.input.hands[0].active&&!(gameplay.hands[0].held&face));
    for(int n=0;n<46;++n)CHECK(!f.Step());CHECK(f.control.toggle==1);
    f.state.mode=ipc::MenuMode::Menu;++f.state.epoch;CHECK(f.Step(true));CHECK(f.Layers().size()==3);
    f.input.hands[0].held=interaction::Secondary;CHECK(f.Step());
    f.state.mode=ipc::MenuMode::Gameplay;++f.state.epoch;CHECK(!f.Step());gameplay=f.input;f.presenter.FilterGameplayInput(gameplay);CHECK(!(gameplay.hands[0].held&face));
    f.input.hands[0].held=0;CHECK(!f.Step());f.input.hands[0].held=interaction::Secondary;CHECK(!f.Step());
    gameplay=f.input;f.presenter.FilterGameplayInput(gameplay);CHECK(gameplay.hands[0].held==interaction::Secondary);return 0;
}
int FormatNegotiation(FakeRuntime& runtime){
    // BC2's actual menu raster is UNORM28; the XR world target is SRGB29.
    // A runtime offering only29 must still receive the exact source texel bytes.
    for(const auto source:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                           DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}){
        const auto target=source==DXGI_FORMAT_R8G8B8A8_UNORM?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            source==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB?DXGI_FORMAT_R8G8B8A8_UNORM:
            source==DXGI_FORMAT_B8G8R8A8_UNORM?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM;
        runtime.formats={target};
        Fixture f(runtime);f.ResetImages(32,18,source,2);CHECK(f.Step(true));
        const auto layers=f.Layers();CHECK(layers.size()==3&&f.presenter.Frames()==1&&f.presenter.Errors()==0);
        const auto& panel=Quad(layers[0]);const auto* chain=FakeRuntime::Find(panel.subImage.swapchain);CHECK(chain&&chain->info.format==target);
        const auto pixels=f.Read(panel.subImage.swapchain);for(unsigned y=0;y<18;++y)for(unsigned x=0;x<32;++x)CHECK(pixels[y*32+x]==Fixture::Pixel(x,y,0));
    }
    // Exact format wins even when a compatible format appears earlier.
    runtime.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM};
    {Fixture f(runtime);CHECK(f.Step(true));const auto layers=f.Layers();CHECK(layers.size()==3);
        CHECK(FakeRuntime::Find(Quad(layers[0]).subImage.swapchain)->info.format==DXGI_FORMAT_R8G8B8A8_UNORM);}
    runtime.formats={DXGI_FORMAT_R8G8B8A8_UNORM};return 0;
}
int TypelessPointerImages(FakeRuntime& runtime){
    // XR image storage can be typeless even though the negotiated swapchain
    // format is typed. Default RTV inference fails on the real D3D11 device.
    runtime.typelessImages=true;runtime.formats={DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    {Fixture f(runtime);CHECK(f.Step(true));const auto layers=f.Layers();CHECK(!layers.empty());
        const auto* chain=FakeRuntime::Find(Quad(layers[0]).subImage.swapchain);CHECK(chain);
        D3D11_TEXTURE2D_DESC desc{};chain->images[0]->GetDesc(&desc);CHECK(desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS);
        ComPtr<ID3D11RenderTargetView> untyped;const auto hr=runtime.device->CreateRenderTargetView(chain->images[0].Get(),nullptr,&untyped);
        std::cout<<"typeless default RTV HRESULT="<<std::hex<<unsigned(hr)<<std::dec<<'\n';CHECK(FAILED(hr));
        CHECK(layers.size()==3&&f.presenter.Errors()==0);
        const auto pointer=Quad(layers[1]).subImage.swapchain;const auto pixels=f.Read(pointer);CHECK(pixels.size()==4);
        for(auto pixel:pixels)CHECK((pixel>>24)==255&&((pixel>>16)&255)>200&&((pixel>>8)&255)>150&&(pixel&255)>0);
        const auto created=runtime.created,destroyed=runtime.destroyed,released=runtime.released;
        for(unsigned n=0;n<60;++n){f.input.hands[1].aim.position.x=float(n%10)*.01f;CHECK(f.Step());CHECK(f.Layers().size()==3);}
        CHECK(runtime.created==created&&runtime.destroyed==destroyed&&runtime.released==released);
    }
    runtime.typelessImages=false;runtime.formats={DXGI_FORMAT_R8G8B8A8_UNORM};return 0;
}
int PointerFailureDoesNotChurn(FakeRuntime& runtime){
    Fixture f(runtime);runtime.failPointerAcquire=true;CHECK(f.Step(true));
    CHECK(f.Layers().size()==1&&f.presenter.Errors()==1&&(f.control.flags&ipc::MenuPoint));
    const auto first=f.presenter.FirstFailure();
    CHECK(first.available&&first.errorCounted&&!std::strcmp(first.stage,"pointer_acquire")&&!std::strcmp(first.resultDomain,"XrResult"));
    CHECK(first.result==XR_ERROR_RUNTIME_FAILURE&&first.transferResult==-1&&first.width==2&&first.height==2);
    CHECK(first.hasSurface&&first.surface.ticket.sequence==f.lastTicket.sequence&&first.state.epoch==f.state.epoch);
    const auto created=runtime.created,destroyed=runtime.destroyed,acquired=runtime.acquired;
    for(unsigned n=0;n<20;++n){CHECK(f.Step());CHECK(f.Layers().size()==1);}
    CHECK(runtime.created==created&&runtime.destroyed==destroyed&&runtime.acquired==acquired&&f.presenter.Errors()==1);
    runtime.failPointerAcquire=false;Sleep(510);CHECK(f.Step(true));CHECK(f.Layers().size()==3);
    CHECK(runtime.created==created&&runtime.destroyed==destroyed&&f.presenter.Errors()==1);
    CHECK(f.presenter.FirstFailure().qpc==first.qpc&&f.presenter.FirstFailure().surface.ticket.sequence==first.surface.ticket.sequence);
    return 0;
}
int BusyStatePreservesMenuUntilOriginalExpiry(FakeRuntime& runtime){
    for(const auto mode:{ipc::MenuMode::Menu,ipc::MenuMode::Passive}){
    Fixture f(runtime);f.state.mode=mode;f.state.inputReady=mode==ipc::MenuMode::Menu?1u:0u;
    const auto layerCount=mode==ipc::MenuMode::Menu?3u:1u;
    CHECK(f.Step(true));CHECK(f.Step());CHECK(!f.presenter.WorldAvailable());const auto before=Quad(f.Layers()[0]).pose;
    const auto frameOnly=[&]{++f.input.generation;f.input.predictedNs+=11000000;return f.presenter.Frame(f.input);};
    f.input.head.position.x=.3f;
    {BusyMenuChannel busy(f.frameChannel.Token());
        CHECK(frameOnly());CHECK(f.Layers().size()==layerCount); // Must not resume world presentation during Busy.
        CHECK(Near(Quad(f.Layers()[0]).pose.position.x,before.position.x));
    }
    CHECK(f.Step());CHECK(Near(Quad(f.Layers()[0]).pose.position.x,before.position.x)); // No anchor reset after contention clears.
    {BusyMenuChannel busy(f.frameChannel.Token());
        CHECK(frameOnly());Sleep(60);CHECK(frameOnly());
        Sleep(55);CHECK(!frameOnly());CHECK(f.Layers().empty());CHECK(!f.presenter.WorldAvailable()); // Repeated Busy reads never renew original100ms state deadline.
    }
    CHECK(f.Step(true));CHECK(f.Layers().size()==layerCount);
    f.state.mode=ipc::MenuMode::Gameplay;++f.state.epoch;CHECK(!f.Step());CHECK(f.Layers().empty());CHECK(f.presenter.WorldAvailable());
    {BusyMenuChannel busy(f.frameChannel.Token());
        CHECK(!frameOnly());CHECK(f.presenter.WorldAvailable()); // Same native Gameplay lease survives a zero-wait mutex miss.
    }
    f.state.mode=ipc::MenuMode::Unknown;++f.state.epoch;CHECK(!f.Step());CHECK(!f.presenter.WorldAvailable());
    f.state.mode=mode;++f.state.epoch;CHECK(f.Step(true));
    f.state.mode=ipc::MenuMode::Gameplay;++f.state.epoch;CHECK(!f.Step());CHECK(f.presenter.WorldAvailable());
    f.producerChannel.Close();CHECK(!frameOnly());CHECK(f.Layers().empty());CHECK(!f.presenter.WorldAvailable()); // Closed invalidates even fresh cached state.
    CHECK(!f.presenter.FirstFailure().available&&f.presenter.Errors()==0);
    }
    return 0;
}
int RasterAspects(FakeRuntime& runtime){
    // Exercise the actual presenter, not just pointer math: the old Frame()
    // suppression made a valid 4:3 raster visible but removed its laser/input.
    for(const auto size:{std::array{1024u,768u},std::array{1280u,800u},
                         std::array{1920u,1080u},std::array{2560u,1080u}}){
        Fixture f(runtime);f.ResetImages(size[0],size[1],DXGI_FORMAT_R8G8B8A8_UNORM,2);
        f.input.hands[1].aim.position={.22f,1.72f,0};
        CHECK(f.Step(true));auto layers=f.Layers();
        CHECK(layers.size()==3&&(f.control.flags&ipc::MenuPoint)&&!(f.control.flags&ipc::MenuDown));
        CHECK(f.control.u>.5f&&f.control.u<1&&f.control.v>0&&f.control.v<.5f);
        const auto panel=Quad(layers[0]);
        CHECK(panel.subImage.imageRect.extent.width==int(size[0])&&panel.subImage.imageRect.extent.height==int(size[1]));
        CHECK(Near(panel.size.height/panel.size.width,float(size[1])/float(size[0])));
        f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);
        f.input.focused=false;CHECK(f.Step());CHECK(f.Layers().empty()&&f.control.flags==0);
        f.input.focused=true;CHECK(f.Step(true));CHECK(f.Layers().size()==3&&!(f.control.flags&ipc::MenuDown));
        f.input.hands[1].trigger=0;CHECK(f.Step());
        f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);
        CHECK(f.presenter.Errors()==0);
    }
    return 0;
}
int Failures(FakeRuntime& runtime){
    {Fixture f(runtime);runtime.failAcquire=true;CHECK(f.Step(true));CHECK(f.Layers().empty()&&f.presenter.Errors()==1);
        CHECK(f.presenter.FirstFailure().available&&!std::strcmp(f.presenter.FirstFailure().stage,"panel_acquire"));
        CHECK(f.Step(true));CHECK(f.Layers().size()==3&&f.presenter.Frames()==1);}
    {Fixture f(runtime);f.ResetImages(32,18,DXGI_FORMAT_B8G8R8A8_UNORM,2);CHECK(f.Step(true));CHECK(f.Layers().empty()&&f.presenter.Errors()==1);
        const auto& first=f.presenter.FirstFailure();CHECK(first.available&&first.errorCounted&&!std::strcmp(first.stage,"panel_format"));
        CHECK(first.result==XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED&&first.format==DXGI_FORMAT_B8G8R8A8_UNORM);}
    const auto releases=runtime.released,retired=runtime.retired;
    {Fixture f(runtime);runtime.timeoutWait=true;bool threw=false;try{f.Step(true);}catch(const std::runtime_error& e){threw=std::strstr(e.what(),"swapchain wait failed")!=nullptr;}
        CHECK(threw&&f.Layers().empty()&&runtime.released==releases);
        const auto& first=f.presenter.FirstFailure();CHECK(first.available&&!first.errorCounted&&f.presenter.Errors()==0);
        CHECK(!std::strcmp(first.stage,"panel_wait")&&first.result==XR_TIMEOUT_EXPIRED&&first.swapchain);
        CHECK(first.hasSurface&&first.surface.ticket.sequence==f.lastTicket.sequence);}
    CHECK(runtime.retired==retired+1);
    return 0;
}
graphics::PairTicket BeginLegacy(Fixture& f){
    ++f.input.generation;f.input.predictedNs=1000000000+std::int64_t(f.input.generation)*11000000;
    f.State();const auto ticket=f.Surface();Require(f.presenter.Frame(f.input),"legacy menu active");
    Require(xr::OpenXrMenuTestAccess::Pending(f.presenter),"legacy submission remains owned until a later poll");
    Require(f.producerChannel.PollOutcome()==ipc::Outcome::Pending,"no premature source acknowledgement");
    return ticket;
}
void FinishLegacy(Fixture& f,const graphics::PairTicket& ticket){
    const auto end=GetTickCount64()+2000;
    while(xr::OpenXrMenuTestAccess::Pending(f.presenter)&&GetTickCount64()<end){Sleep(1);f.Step();}
    Require(!xr::OpenXrMenuTestAccess::Pending(f.presenter),"legacy copy stopped");
    Require(f.producerChannel.PollOutcome()==ipc::Outcome::Consumed,"fenced legacy copy consumed");
    f.producer.Acknowledge(ticket,true);
}
int DeferredLegacyCopy(FakeRuntime& runtime){
    Fixture f(runtime);f.input.spaceGeneration=2;f.state.epoch=7;
    f.ResetImages(32,18,DXGI_FORMAT_R8G8B8A8_UNORM,2,graphics::TextureSharing::LegacyFenced);
    const auto acquired=runtime.acquired,released=runtime.released;
    const auto ticket=BeginLegacy(f);const auto original=xr::OpenXrMenuTestAccess::Surface(f.presenter);
    CHECK(ticket.spaceGeneration==7&&f.input.spaceGeneration==2);
    CHECK(runtime.acquired==acquired&&runtime.released==released&&f.Layers().empty());
    CHECK(f.presenter.Frames()==0&&f.presenter.Errors()==0&&!f.presenter.FirstFailure().available);
    CHECK(!std::memcmp(&original.ticket,&ticket,sizeof(ticket))&&original.state.deadlineQpc==f.state.deadlineQpc);
    // SubmitCopy itself guarantees Busy, independent of GPU speed. The old
    // synchronous Frame implementation cannot satisfy this pending/ACK/XR gate.
    FinishLegacy(f,ticket);CHECK(f.presenter.Frames()==1&&f.presenter.Errors()==0);
    auto layers=f.Layers();CHECK(layers.size()==3);
    const auto panel=Quad(layers[0]);const auto pixels=f.Read(panel.subImage.swapchain);
    for(unsigned y=0;y<f.height;++y)for(unsigned x=0;x<f.width;++x)CHECK(pixels[y*f.width+x]==Fixture::Pixel(x,y,0));
    // A subsequent pending source keeps the previously released image, without
    // renewing its visibility clock or acquiring another XR image.
    const auto imageTime=xr::OpenXrMenuTestAccess::ImageTime(f.presenter),oldFrames=f.presenter.Frames();
    const auto previousAcquires=runtime.acquired;const auto second=BeginLegacy(f);
    CHECK(f.Layers().size()==3&&runtime.acquired==previousAcquires&&f.presenter.Frames()==oldFrames);
    CHECK(xr::OpenXrMenuTestAccess::ImageTime(f.presenter)==imageTime);
    CHECK(f.Read(panel.subImage.swapchain)==pixels);FinishLegacy(f,second);
    CHECK(f.presenter.Frames()==2&&f.presenter.Errors()==0);return 0;
}
int DeferredLegacyCancellation(FakeRuntime& runtime){
    // All use the actual producer/channel/consumer/presenter. No simulated GPU
    // completion and no injected HRESULT can authorize the positive ACK.
    for(unsigned cause=0;cause<6;++cause){
        Fixture f(runtime);f.ResetImages(32,18,DXGI_FORMAT_R8G8B8A8_UNORM,2,graphics::TextureSharing::LegacyFenced);
        const auto acquired=runtime.acquired;const auto ticket=BeginLegacy(f);
        ComPtr<ID3D11Texture2D> retiredScratch=xr::OpenXrMenuTestAccess::Scratch(f.presenter);
        if(cause==0)f.input.focused=false;
        if(cause==1)f.input.headValid=false;
        if(cause==2)++f.input.spaceGeneration;
        if(cause==3)++f.state.epoch;
        if(cause==4)f.state.mode=ipc::MenuMode::Gameplay;
        if(cause==5){
            // Current state is refreshed by Step; the ORIGINAL surface lease
            // is not. Completion, even if the GPU finished meanwhile, is late.
            Sleep(110);
        }
        f.Step();CHECK(xr::OpenXrMenuTestAccess::Reset(f.presenter));
        CHECK(f.producerChannel.PollOutcome()==ipc::Outcome::Discarded);f.producer.Acknowledge(ticket,false);
        CHECK(!graphics::Valid(f.producer.Descriptor())&&f.Layers().empty()&&runtime.acquired==acquired);
        CHECK(f.presenter.Errors()==(cause==5?1u:0u));
        if(cause==5){CHECK(f.presenter.FirstFailure().available&&
            !std::strcmp(f.presenter.FirstFailure().stage,"panel_copy_deadline")&&
            f.presenter.FirstFailure().surface.ticket.sequence==ticket.sequence);}
        f.input.focused=f.input.headValid=true;f.state.mode=ipc::MenuMode::Menu;
        f.ResetImages(32,18,DXGI_FORMAT_R8G8B8A8_UNORM,3,graphics::TextureSharing::LegacyFenced);
        const auto fresh=BeginLegacy(f);
        CHECK(xr::OpenXrMenuTestAccess::Scratch(f.presenter)!=retiredScratch.Get());
        FinishLegacy(f,fresh);CHECK(f.presenter.Frames()==1&&f.Layers().size()==3);
    }
    return 0;
}
int DeferredFeedbackContention(FakeRuntime& runtime){
    Fixture f(runtime);f.ResetImages(32,18,DXGI_FORMAT_R8G8B8A8_UNORM,2,graphics::TextureSharing::LegacyFenced);
    const auto ticket=BeginLegacy(f);const auto acquired=runtime.acquired;
    f.input.focused=false;
    {BusyMenuChannel busy(f.frameChannel.Token());
        ++f.input.generation;f.input.predictedNs+=11000000;CHECK(f.presenter.Frame(f.input));
        CHECK(xr::OpenXrMenuTestAccess::Reset(f.presenter)&&xr::OpenXrMenuTestAccess::Feedback(f.presenter));
        ++f.input.generation;f.input.predictedNs+=11000000;CHECK(f.presenter.Frame(f.input));
        CHECK(xr::OpenXrMenuTestAccess::Feedback(f.presenter)&&runtime.acquired==acquired&&f.presenter.Errors()==0);
    }
    CHECK(f.producerChannel.PollOutcome()==ipc::Outcome::Pending);f.Step();
    CHECK(!xr::OpenXrMenuTestAccess::Feedback(f.presenter)&&f.producerChannel.PollOutcome()==ipc::Outcome::Discarded);
    f.producer.Acknowledge(ticket,false);CHECK(!graphics::Valid(f.producer.Descriptor()));
    CHECK(runtime.acquired==acquired&&f.Layers().empty()&&f.presenter.Errors()==0);return 0;
}
int FirstFailureAttribution(FakeRuntime& runtime){
    std::ostringstream absent;xr::OpenXrMenuFailure{}.Write(absent);CHECK(absent.str()=="null");
    // A stale, structurally valid ticket reaches the real GPU consumer and is
    // rejected before any new copy. The channel must still receive discard,
    // and later allocation/upload failures must not replace its exact identity.
    {Fixture f(runtime);CHECK(f.Step(true));const auto ticket=f.lastTicket;
        ++f.input.generation;f.input.predictedNs+=11000000;f.State();
        CHECK(f.producerChannel.PublishSurface({f.state,f.producer.Descriptor(),ticket})==ipc::ChannelResult::Ok);
        CHECK(f.presenter.Frame(f.input));CHECK(f.producerChannel.PollOutcome()==ipc::Outcome::Discarded);
        CHECK(f.presenter.Errors()==1&&f.Layers().empty());const auto first=f.presenter.FirstFailure();
        CHECK(first.available&&first.errorCounted&&!std::strcmp(first.stage,"panel_copy")&&!std::strcmp(first.resultDomain,"HRESULT"));
        CHECK(first.result==E_INVALIDARG&&first.transferResult==std::int64_t(graphics::TransferResult::Invalid));
        CHECK(first.stageStartedQpc>0&&first.qpc>=first.stageStartedQpc&&first.qpcFrequency>0);
        CHECK(first.inputGeneration==f.input.generation&&first.inputSpace==f.input.spaceGeneration&&first.inputPredictedNs==f.input.predictedNs);
        CHECK(first.stateRead==ipc::ChannelResult::Ok&&first.stateKnown&&first.active&&first.focused&&first.headValid);
        CHECK(first.state.sequence==f.state.sequence&&first.state.deadlineQpc==f.state.deadlineQpc);
        CHECK(first.hasSurface&&!std::memcmp(&first.surface.ticket,&ticket,sizeof(ticket)));
        const auto descriptor=f.producer.Descriptor();CHECK(!std::memcmp(&first.surface.descriptor,&descriptor,sizeof(descriptor)));
        CHECK(first.surface.state.epoch==f.state.epoch&&first.surface.state.mode==ipc::MenuMode::Menu);
        CHECK(first.width==f.width&&first.height==f.height&&first.format==DXGI_FORMAT_R8G8B8A8_UNORM);
        std::ostringstream serialized;first.Write(serialized);const auto json=serialized.str();
        CHECK(json.size()<4096&&json.front()=='{'&&json.back()=='}');
        CHECK(json.find("\"stage\":\"panel_copy\"")!=std::string::npos&&json.find("\"result_domain\":\"HRESULT\"")!=std::string::npos);
        CHECK(json.find("\"result\":"+std::to_string(E_INVALIDARG))!=std::string::npos);
        CHECK(json.find("\"input_generation\":"+std::to_string(first.inputGeneration))!=std::string::npos);
        CHECK(json.find("\"deadline_qpc\":"+std::to_string(first.state.deadlineQpc))!=std::string::npos);
        CHECK(json.find("\"ticket\":{\"resource_epoch\":"+std::to_string(ticket.resourceEpoch)+",\"sequence\":"+std::to_string(ticket.sequence)+",\"frame\":"+std::to_string(ticket.frameId))!=std::string::npos);
        CHECK(json.find("\"error_counted\":true")!=std::string::npos&&json.find("host_stage_elapsed_not_gpu_time")!=std::string::npos);
        CHECK(f.Step(true)&&f.Layers().size()==3);runtime.failAcquire=true;CHECK(f.Step(true));CHECK(f.presenter.Errors()==2);
        CHECK(f.presenter.FirstFailure().qpc==first.qpc&&f.presenter.FirstFailure().inputGeneration==first.inputGeneration);
        CHECK(!std::strcmp(f.presenter.FirstFailure().stage,"panel_copy"));}
    // Exercise the two formerly silent allocation failures with real shared
    // textures. A failed XR allocation does not change consumed-copy feedback.
    for(const bool images:{false,true}){Fixture f(runtime);runtime.failCreate=!images;runtime.failImages=images;
        CHECK(f.Step(true)&&f.Layers().empty()&&f.presenter.Errors()==1);const auto first=f.presenter.FirstFailure();
        CHECK(first.available&&first.errorCounted&&!std::strcmp(first.stage,images?"panel_images":"panel_create"));
        CHECK(!std::strcmp(first.resultDomain,"XrResult")&&first.result==XR_ERROR_RUNTIME_FAILURE);
        CHECK(first.hasSurface&&first.surface.ticket.sequence==f.lastTicket.sequence&&first.width==f.width&&first.height==f.height);
        CHECK(first.qpc>=first.stageStartedQpc&&first.stageStartedQpc>0&&first.qpcFrequency>0);
        CHECK(f.Step(true)&&f.Layers().size()==3&&f.presenter.Errors()==1);
        CHECK(f.presenter.FirstFailure().qpc==first.qpc);}
    return 0;
}
}
int main(){
    try{ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
        FakeRuntime runtime;runtime.device=device.Get();FakeRuntime::current=&runtime;
        CHECK(PixelsAndLayers(runtime)==0);CHECK(Lifecycle(runtime)==0);CHECK(PassivePixelsAndControls(runtime)==0);CHECK(FormatNegotiation(runtime)==0);CHECK(ChordGameplayFilter(runtime)==0);CHECK(TypelessPointerImages(runtime)==0);CHECK(PointerFailureDoesNotChurn(runtime)==0);CHECK(BusyStatePreservesMenuUntilOriginalExpiry(runtime)==0);CHECK(RasterAspects(runtime)==0);CHECK(Failures(runtime)==0);CHECK(FirstFailureAttribution(runtime)==0);
        CHECK(DeferredLegacyCopy(runtime)==0);CHECK(DeferredLegacyCancellation(runtime)==0);CHECK(DeferredFeedbackContention(runtime)==0);
        CHECK(runtime.errors==0&&runtime.chains.empty()&&runtime.created==runtime.destroyed);
        CHECK(runtime.acquired==runtime.released+runtime.retired&&runtime.waited==runtime.released);
        std::cout<<"OpenXrMenu actual GPU pixels, quad/cursor/laser, ownership releases, stale raster and swapchain failures passed; chains="<<runtime.created<<" releases="<<runtime.released<<" retired="<<runtime.retired<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
