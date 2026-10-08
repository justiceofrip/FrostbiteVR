#include "OpenXrMenu.h"
#include "Test.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <thread>
using namespace fvr;
using Microsoft::WRL::ComPtr;
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
    bool failAcquire=false,timeoutWait=false,typelessImages=false,failPointerAcquire=false;
    std::vector<std::int64_t> formats{DXGI_FORMAT_R8G8B8A8_UNORM};
    static FakeRuntime* current;
    static FakeChain* Find(XrSwapchain h){auto& r=*current;const auto it=r.chains.find(h);if(it!=r.chains.end())return it->second.get();++r.errors;return nullptr;}
    static XrResult XRAPI_CALL Create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){
        auto& r=*current;if(!info||!out)return XR_ERROR_VALIDATION_FAILURE;
        const auto flags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        if(info->type!=XR_TYPE_SWAPCHAIN_CREATE_INFO||info->next||info->usageFlags!=flags||info->createFlags||
           info->sampleCount!=1||info->faceCount!=1||info->arraySize!=1||info->mipCount!=1||!info->width||!info->height){++r.errors;return XR_ERROR_VALIDATION_FAILURE;}
        if(std::find(r.formats.begin(),r.formats.end(),info->format)==r.formats.end())return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
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
    void ResetImages(unsigned w,unsigned h,DXGI_FORMAT format,std::uint64_t epoch){
        width=w;height=h;Require(producer.Create(device.Get(),w,h,format,epoch)==graphics::TransferResult::Ok,"GPU shared pair create");
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
        Require(producer.Publish(slices,{++frame,input.spaceGeneration,input.generation,input.predictedNs},ticket)==graphics::TransferResult::Ok,"publish GPU pair");
        Require(producerChannel.PublishSurface({state,producer.Descriptor(),ticket})==ipc::ChannelResult::Ok,"publish menu surface");return ticket;
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
    ++f.input.spaceGeneration;CHECK(f.Step());CHECK(!(f.control.flags&ipc::MenuDown));
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
    const auto created=runtime.created,destroyed=runtime.destroyed,acquired=runtime.acquired;
    for(unsigned n=0;n<20;++n){CHECK(f.Step());CHECK(f.Layers().size()==1);}
    CHECK(runtime.created==created&&runtime.destroyed==destroyed&&runtime.acquired==acquired&&f.presenter.Errors()==1);
    runtime.failPointerAcquire=false;Sleep(510);CHECK(f.Step(true));CHECK(f.Layers().size()==3);
    CHECK(runtime.created==created&&runtime.destroyed==destroyed&&f.presenter.Errors()==1);
    return 0;
}
int BusyStatePreservesMenuUntilOriginalExpiry(FakeRuntime& runtime){
    Fixture f(runtime);CHECK(f.Step(true));CHECK(f.Step());const auto before=Quad(f.Layers()[0]).pose;
    const auto frameOnly=[&]{++f.input.generation;f.input.predictedNs+=11000000;return f.presenter.Frame(f.input);};
    f.input.head.position.x=.3f;
    {BusyMenuChannel busy(f.frameChannel.Token());
        CHECK(frameOnly());CHECK(f.Layers().size()==3); // Must not resume world presentation during Busy.
        CHECK(Near(Quad(f.Layers()[0]).pose.position.x,before.position.x));
    }
    CHECK(f.Step());CHECK(Near(Quad(f.Layers()[0]).pose.position.x,before.position.x)); // No anchor reset after contention clears.
    {BusyMenuChannel busy(f.frameChannel.Token());
        CHECK(frameOnly());Sleep(60);CHECK(frameOnly());
        Sleep(55);CHECK(!frameOnly());CHECK(f.Layers().empty()); // Repeated Busy reads never renew original100ms state deadline.
    }
    CHECK(f.Step(true));CHECK(f.Layers().size()==3);
    f.state.mode=ipc::MenuMode::Gameplay;++f.state.epoch;CHECK(!f.Step());CHECK(f.Layers().empty());
    f.state.mode=ipc::MenuMode::Menu;++f.state.epoch;CHECK(f.Step(true));
    f.producerChannel.Close();CHECK(!frameOnly());CHECK(f.Layers().empty()); // Closed invalidates even fresh cached state.
    return 0;
}
int Failures(FakeRuntime& runtime){
    // An unproven 4:3 native raster remains visible, with no guessed cursor UV.
    {Fixture f(runtime);f.ResetImages(40,30,DXGI_FORMAT_R8G8B8A8_UNORM,2);CHECK(f.Step(true));
        CHECK(f.Layers().size()==1&&!(f.control.flags&(ipc::MenuPoint|ipc::MenuDown)));
        f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(!(f.control.flags&ipc::MenuDown));
        f.ResetImages(32,18,DXGI_FORMAT_R8G8B8A8_UNORM,3);CHECK(f.Step(true));CHECK(f.Layers().size()==3&&!(f.control.flags&ipc::MenuDown));
        f.input.hands[1].trigger=0;CHECK(f.Step());f.input.hands[1].trigger=.8f;CHECK(f.Step());CHECK(f.control.flags&ipc::MenuDown);}

    {Fixture f(runtime);runtime.failAcquire=true;CHECK(f.Step(true));CHECK(f.Layers().empty()&&f.presenter.Errors()==1);
        CHECK(f.Step(true));CHECK(f.Layers().size()==3&&f.presenter.Frames()==1);}
    {Fixture f(runtime);f.ResetImages(32,18,DXGI_FORMAT_B8G8R8A8_UNORM,2);CHECK(f.Step(true));CHECK(f.Layers().empty()&&f.presenter.Errors()==1);}
    const auto releases=runtime.released,retired=runtime.retired;
    {Fixture f(runtime);runtime.timeoutWait=true;bool threw=false;try{f.Step(true);}catch(const std::runtime_error& e){threw=std::strstr(e.what(),"swapchain wait failed")!=nullptr;}
        CHECK(threw&&f.Layers().empty()&&runtime.released==releases);}
    CHECK(runtime.retired==retired+1);
    return 0;
}
}
int main(){
    try{ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
        FakeRuntime runtime;runtime.device=device.Get();FakeRuntime::current=&runtime;
        CHECK(PixelsAndLayers(runtime)==0);CHECK(Lifecycle(runtime)==0);CHECK(FormatNegotiation(runtime)==0);CHECK(ChordGameplayFilter(runtime)==0);CHECK(TypelessPointerImages(runtime)==0);CHECK(PointerFailureDoesNotChurn(runtime)==0);CHECK(BusyStatePreservesMenuUntilOriginalExpiry(runtime)==0);CHECK(Failures(runtime)==0);
        CHECK(runtime.errors==0&&runtime.chains.empty()&&runtime.created==runtime.destroyed);
        CHECK(runtime.acquired==runtime.released+runtime.retired&&runtime.waited==runtime.released);
        std::cout<<"OpenXrMenu actual GPU pixels, quad/cursor/laser, ownership releases, stale raster and swapchain failures passed; chains="<<runtime.created<<" releases="<<runtime.released<<" retired="<<runtime.retired<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
