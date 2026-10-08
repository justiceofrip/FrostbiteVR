#include "fvr/xr/OpenXrHost.h"
#include "fvr/runtime/RetainedPresentation.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <memory>
#include "OpenXrInput.h"
#include "OpenXrMenu.h"
#include "OpenXrAmmoHud.h"
#include "fvr/interaction/RecenterPolicy.h"

namespace fvr::xr {
using Microsoft::WRL::ComPtr;
namespace {
#define XR_FUNCTIONS(F) \
 F(DestroyInstance) F(GetInstanceProperties) F(GetSystem) F(GetSystemProperties) \
 F(GetD3D11GraphicsRequirementsKHR) F(EnumerateViewConfigurationViews) F(EnumerateEnvironmentBlendModes) \
 F(CreateSession) F(DestroySession) F(PollEvent) F(BeginSession) F(EndSession) F(RequestExitSession) \
 F(CreateReferenceSpace) F(DestroySpace) F(LocateSpace) F(LocateViews) \
 F(EnumerateSwapchainFormats) F(CreateSwapchain) F(DestroySwapchain) F(EnumerateSwapchainImages) \
 F(AcquireSwapchainImage) F(WaitSwapchainImage) F(ReleaseSwapchainImage) F(WaitFrame) F(BeginFrame) F(EndFrame)
struct Api {
    PFN_xrGetInstanceProcAddr get=nullptr;
#define FIELD(name) PFN_xr##name name=nullptr;
    XR_FUNCTIONS(FIELD)
#undef FIELD
    template<class T>void Load(XrInstance instance,const char* name,T& function){
        PFN_xrVoidFunction raw=nullptr;const auto result=get(instance,name,&raw);
        if(XR_FAILED(result)||!raw)throw std::runtime_error(std::string("Cannot resolve ")+name);
        function=reinterpret_cast<T>(raw);
    }
    void LoadInstance(XrInstance instance){
#define LOAD(name) Load(instance,"xr" #name,name);
        XR_FUNCTIONS(LOAD)
#undef LOAD
    }
};
void Check(XrResult result,const char* operation){if(XR_FAILED(result))throw std::runtime_error(std::string(operation)+" XrResult="+std::to_string(result));}
void Hr(HRESULT result,const char* operation){if(FAILED(result))throw std::runtime_error(std::string(operation)+" HRESULT="+std::to_string(std::uint32_t(result)));}
void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
math::Pose Pose(const XrPosef& pose){return {{pose.position.x,pose.position.y,pose.position.z},{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w}};}
std::int64_t BodyPropClockNs()noexcept {LARGE_INTEGER q{},f{};if(!QueryPerformanceCounter(&q)||!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return 0;
    return (q.QuadPart/f.QuadPart)*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
struct Host {
    Api api;
    HMODULE loader=nullptr;
    XrInstance instance=XR_NULL_HANDLE;
    XrSystemId system=XR_NULL_SYSTEM_ID;
    XrSession session=XR_NULL_HANDLE;
    XrSpace local=XR_NULL_HANDLE,headSpace=XR_NULL_HANDLE;
    std::unique_ptr<OpenXrInput> controls;
    std::unique_ptr<OpenXrMenu> menu;
    std::unique_ptr<OpenXrAmmoHud> ammoHud;
    interaction::RecenterGesture recenter;
    interaction::RecenterPolicy trackingRecovery;
    ULONGLONG lastFrameWall=0,presenceReturnAt=0;bool recoveryPending=false,userPresent=true,presenceKnown=false;
    std::ofstream eventLog;
    void Log(const char* kind){if(eventLog){eventLog<<"{\"event\":\""<<kind<<"\",\"wall_ms\":"<<GetTickCount64()<<",\"state\":"<<int(sessionState)<<",\"space\":"<<spaceGeneration<<",\"present\":"<<(userPresent?"true":"false")<<",\"reference\":["<<referenceHead.position.x<<','<<referenceHead.position.y<<','<<referenceHead.position.z<<"],\"recenters\":"<<report.recenters<<",\"automatic_recenters\":"<<report.automaticRecenters<<"}\n";eventLog.flush();}}
    bool roomscale=false;float worldUnitsPerMeter=1;
    XrReferenceSpaceType referenceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    std::uint64_t inputGeneration=0;
    XrSessionState sessionState=XR_SESSION_STATE_UNKNOWN;
    std::array<XrSwapchain,2> swapchains{};
    std::array<std::vector<XrSwapchainImageD3D11KHR>,2> images;
    std::array<bool,2> acquired{},waited{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    graphics::D3D11PairConsumer consumer;
    graphics::TextureDescriptor openedDescriptor{};
    runtime::RetainedPresentation retained;
    std::array<ComPtr<ID3D11Texture2D>,2> scratch;
    std::unique_ptr<graphics::D3D11BodyPropCompositor> bodyProps;
    HostReport& report;
    bool running=false,exit=false,frameOpen=false,referenceValid=false;
    XrTime frameTime=0;
    std::vector<XrTime> spaceChanges;
    math::Pose referenceHead{};
    std::uint64_t trackingGeneration=0,spaceGeneration=1;
    explicit Host(HostReport& r):report(r){}
    ~Host(){
        AbortFrame();consumer.Reset();controls.reset();menu.reset();ammoHud.reset();bodyProps.reset();
        for(auto chain:swapchains)if(chain&&api.DestroySwapchain)api.DestroySwapchain(chain);
        if(headSpace&&api.DestroySpace)api.DestroySpace(headSpace);
        if(local&&api.DestroySpace)api.DestroySpace(local);
        if(session&&api.DestroySession)api.DestroySession(session);
        context.Reset();device.Reset();
        if(instance&&api.DestroyInstance)api.DestroyInstance(instance);
        if(loader)FreeLibrary(loader);
    }
    void AbortFrame()noexcept {
        for(unsigned eye=0;eye<2;++eye)if(waited[eye]&&api.ReleaseSwapchainImage){
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            api.ReleaseSwapchainImage(swapchains[eye],&release);waited[eye]=acquired[eye]=false;
        }
        if(frameOpen&&api.EndFrame){
            XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frameTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            api.EndFrame(session,&end);frameOpen=false;
        }
    }
    void Initialize(const HostOptions& options){
        Require(std::isfinite(options.worldUnitsPerMeter)&&options.worldUnitsPerMeter>=.01f&&options.worldUnitsPerMeter<=1000.f,"World scale must be .01..1000 native units per metre");
        roomscale=options.roomscale;worldUnitsPerMeter=options.worldUnitsPerMeter;
        if(!options.readyPath.empty()){eventLog.open(options.readyPath.parent_path()/L"xr-events.jsonl");Require(bool(eventLog),"Open XR event log");}
        Require(options.loaderPath.is_absolute(),"OpenXR loader requires an absolute path");
        loader=LoadLibraryExW(options.loaderPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        Require(loader!=nullptr,"Cannot load the local x64 OpenXR loader");
        api.get=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));
        Require(api.get!=nullptr,"OpenXR loader has no xrGetInstanceProcAddr");
        PFN_xrEnumerateInstanceExtensionProperties enumerate=nullptr;api.Load(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",enumerate);
        uint32_t count=0;Check(enumerate(nullptr,0,&count,nullptr),"Enumerate extension count");Require(count<=4096,"Invalid OpenXR extension count");
        std::vector<XrExtensionProperties> extensions(count,{XR_TYPE_EXTENSION_PROPERTIES});
        Check(enumerate(nullptr,count,&count,extensions.data()),"Enumerate extensions");
        bool d3d11=false,presenceExtension=false;for(const auto& extension:extensions){d3d11|=std::strcmp(extension.extensionName,XR_KHR_D3D11_ENABLE_EXTENSION_NAME)==0;presenceExtension|=std::strcmp(extension.extensionName,XR_EXT_USER_PRESENCE_EXTENSION_NAME)==0;}
        Require(d3d11,"Active runtime does not expose XR_KHR_D3D11_enable");
        PFN_xrCreateInstance create=nullptr;api.Load(XR_NULL_HANDLE,"xrCreateInstance",create);
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(info.applicationInfo.applicationName,"FrostbiteVR BC2 Host");strcpy_s(info.applicationInfo.engineName,"FrostbiteVR Modular");
        info.applicationInfo.applicationVersion=1;info.applicationInfo.engineVersion=1;
        // 2142 compatibility lesson: core 1.0 + advertised extensions, not an
        // unnecessary 1.1 requirement merely because the headers are newer.
        info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        const char* enabled[]={XR_KHR_D3D11_ENABLE_EXTENSION_NAME,XR_EXT_USER_PRESENCE_EXTENSION_NAME};info.enabledExtensionCount=presenceExtension?2:1;info.enabledExtensionNames=enabled;
        Check(create(&info,&instance),"Create OpenXR instance");report.instanceCreated=true;api.LoadInstance(instance);
        XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};Check(api.GetInstanceProperties(instance,&props),"Runtime properties");report.runtimeName=props.runtimeName;
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        Check(api.GetSystem(instance,&systemInfo,&system),"Find HMD system");report.systemAvailable=true;
        XrSystemUserPresencePropertiesEXT presence{XR_TYPE_SYSTEM_USER_PRESENCE_PROPERTIES_EXT};
        XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};if(presenceExtension)systemProps.next=&presence;
        Check(api.GetSystemProperties(instance,system,&systemProps),"System properties");report.systemName=systemProps.systemName;report.userPresenceSupported=presenceExtension&&presence.supportsUserPresence;
        XrGraphicsRequirementsD3D11KHR graphics{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        Check(api.GetD3D11GraphicsRequirementsKHR(instance,system,&graphics),"D3D11 graphics requirements");
        report.requirements.adapterLow=graphics.adapterLuid.LowPart;report.requirements.adapterHigh=graphics.adapterLuid.HighPart;report.minimumFeatureLevel=graphics.minFeatureLevel;
        std::array<XrViewConfigurationView,2> viewInfo{{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
        Check(api.EnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,viewInfo.data()),"Stereo view configuration");
        Require(count==2,"Runtime must expose two primary stereo views");
        auto& req=report.requirements;req.width=(std::max)(viewInfo[0].recommendedImageRectWidth,viewInfo[1].recommendedImageRectWidth);
        req.height=(std::max)(viewInfo[0].recommendedImageRectHeight,viewInfo[1].recommendedImageRectHeight);
        Require((options.eyeWidth==0)==(options.eyeHeight==0),"Both eye dimensions are required for an override");
        if(options.eyeWidth){req.width=options.eyeWidth;req.height=options.eyeHeight;}
        for(const auto& view:viewInfo)Require(req.width&&req.height&&req.width<=view.maxImageRectWidth&&req.height<=view.maxImageRectHeight,"Incompatible per-eye image sizes");
        if(options.probeOnly)return;
        Require(req.width<=16384&&req.height<=16384,"Runtime eye dimensions exceed the D3D11 bridge limits");
        std::array<XrEnvironmentBlendMode,16> modes{};
        Check(api.EnumerateEnvironmentBlendModes(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,UINT(modes.size()),&count,modes.data()),"Environment blend modes");
        Require(count<=modes.size()&&std::find(modes.begin(),modes.begin()+count,XR_ENVIRONMENT_BLEND_MODE_OPAQUE)!=modes.begin()+count,"Opaque HMD presentation required");
        ComPtr<IDXGIFactory1> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");ComPtr<IDXGIAdapter1> selected;
        for(UINT index=0;;++index){ComPtr<IDXGIAdapter1> candidate;const auto result=factory->EnumAdapters1(index,&candidate);
            if(result==DXGI_ERROR_NOT_FOUND)break;Hr(result,"Enumerate graphics adapter");DXGI_ADAPTER_DESC1 desc{};Hr(candidate->GetDesc1(&desc),"Graphics adapter properties");
            if(desc.AdapterLuid.LowPart==graphics.adapterLuid.LowPart&&desc.AdapterLuid.HighPart==graphics.adapterLuid.HighPart){selected=candidate;break;}}
        Require(bool(selected),"OpenXR-required graphics adapter not found");
        std::vector<D3D_FEATURE_LEVEL> levels;for(auto level:{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0})if(level>=graphics.minFeatureLevel)levels.push_back(level);
        Require(!levels.empty(),"Runtime requires an unsupported D3D11 feature level");D3D_FEATURE_LEVEL level{};
        Hr(D3D11CreateDevice(selected.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels.data(),UINT(levels.size()),D3D11_SDK_VERSION,&device,&level,&context),"Create XR graphics device");
        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};binding.device=device.Get();
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};sessionInfo.next=&binding;sessionInfo.systemId=system;
        Check(api.CreateSession(instance,&sessionInfo,&session),"Create OpenXR session");report.sessionCreated=true;
        if(options.controllers){controls=std::make_unique<OpenXrInput>();controls->Initialize(instance,session,api.get);report.controllersEnabled=true;report.controllerProfiles=controls->ProfilesSuggested();}
        if(roomscale){
            PFN_xrEnumerateReferenceSpaces enumerateSpaces=nullptr;api.Load(instance,"xrEnumerateReferenceSpaces",enumerateSpaces);
            std::array<XrReferenceSpaceType,16> spaces{};std::uint32_t spaceCount=0;Check(enumerateSpaces(session,UINT(spaces.size()),&spaceCount,spaces.data()),"Enumerate tracking spaces");
            Require(spaceCount<=spaces.size(),"Invalid tracking space count");
            if(std::find(spaces.begin(),spaces.begin()+spaceCount,XR_REFERENCE_SPACE_TYPE_STAGE)!=spaces.begin()+spaceCount)referenceType=XR_REFERENCE_SPACE_TYPE_STAGE;
        }
        report.floorRelative=referenceType==XR_REFERENCE_SPACE_TYPE_STAGE;
        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};spaceInfo.poseInReferenceSpace.orientation.w=1;spaceInfo.referenceSpaceType=referenceType;
        Check(api.CreateReferenceSpace(session,&spaceInfo,&local),"Create tracking reference space");spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
        Check(api.CreateReferenceSpace(session,&spaceInfo,&headSpace),"Create VIEW reference space");
        Check(api.EnumerateSwapchainFormats(session,0,&count,nullptr),"Swapchain format count");Require(count<=4096,"Invalid swapchain format count");
        std::vector<int64_t> formats(count);Check(api.EnumerateSwapchainFormats(session,count,&count,formats.data()),"Swapchain formats");
        for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM})if(std::find(formats.begin(),formats.end(),int64_t(format))!=formats.end()){req.format=format;break;}
        Require(req.format!=0,"Runtime has no supported RGBA8/BGRA8 swapchain format");
        for(unsigned eye=0;eye<2;++eye){
            XrSwapchainCreateInfo chain{XR_TYPE_SWAPCHAIN_CREATE_INFO};chain.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            chain.format=req.format;chain.sampleCount=1;chain.width=req.width;chain.height=req.height;chain.faceCount=chain.arraySize=chain.mipCount=1;
            Check(api.CreateSwapchain(session,&chain,&swapchains[eye]),"Create eye swapchain");
            Check(api.EnumerateSwapchainImages(swapchains[eye],0,&count,nullptr),"Eye image count");Require(count&&count<=64,"Invalid eye image count");
            images[eye].resize(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            Check(api.EnumerateSwapchainImages(swapchains[eye],count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images[eye].data())),"Eye images");
        }
        // Copy incoming pairs into private scratch first. A failed/partial copy must
        // never replace the swapchain images that supply the retained pair.
        D3D11_TEXTURE2D_DESC scratchDesc{};scratchDesc.Width=req.width;scratchDesc.Height=req.height;
        scratchDesc.Format=DXGI_FORMAT(req.format);scratchDesc.MipLevels=scratchDesc.ArraySize=1;
        scratchDesc.SampleDesc.Count=1;scratchDesc.Usage=D3D11_USAGE_DEFAULT;
        for(auto& texture:scratch)Hr(device->CreateTexture2D(&scratchDesc,nullptr,&texture),"Create private eye scratch");
        if(options.provider){auto hud=std::make_unique<OpenXrAmmoHud>();
            if(hud->Initialize(instance,session,api.get,device.Get(),req.format)){ammoHud=std::move(hud);report.ammoCounterReady=true;}}
        if(options.bodyProps&&!options.bodyProps->empty()){
            auto compositor=std::make_unique<graphics::D3D11BodyPropCompositor>();
            if(compositor->Initialize(context.Get(),req.width,req.height,req.format,options.bodyProps)){bodyProps=std::move(compositor);report.bodyPropsReady=true;}
        }
        if(!options.readyPath.empty()){
            Require(options.readyPath.is_absolute()&&!std::filesystem::exists(options.readyPath),"Readiness report requires a new absolute path");
            std::ofstream ready(options.readyPath);ready<<"{\"session_created\":true,\"width\":"<<req.width<<",\"height\":"<<req.height<<",\"format\":"<<req.format<<",\"user_presence_supported\":"<<(report.userPresenceSupported?"true":"false")<<"}\n";
            ready.close();Require(bool(ready),"Cannot write XR readiness report");
        }
    }
    void DispatchFeedback(IFrameProvider* provider,const interaction::InputFrame& input){
        if(!controls)return;
        LARGE_INTEGER q{},frequency{};std::int64_t now=0;
        if(QueryPerformanceCounter(&q)&&QueryPerformanceFrequency(&frequency)&&q.QuadPart>0&&frequency.QuadPart>0&&frequency.QuadPart<=1000000000)
            now=q.QuadPart/frequency.QuadPart*1000000000+(q.QuadPart%frequency.QuadPart)*1000000000/frequency.QuadPart;
        controls->Feedback(input,now);
        if(provider)for(unsigned n=0;n<8;++n){
            interaction::FeedbackEvent event;if(!provider->TakeFeedback(event))break;
            const auto applied=controls->FeedbackApplied(),rejected=controls->FeedbackRejected(),errors=controls->FeedbackErrors();
            controls->Feedback(input,now,&event);
            // Persist actual dispatch at event time. Forced host termination cannot
            // erase the only evidence of a cue's acceptance/rejection by OpenXR.
            if(eventLog){eventLog<<"{\"event\":\"haptic_dispatch\",\"wall_ms\":"<<GetTickCount64()
                <<",\"id\":"<<event.id<<",\"kind\":"<<unsigned(event.kind)<<",\"hand\":"<<event.hand
                <<",\"observed_ns\":"<<event.observedNs<<",\"deadline_ns\":"<<event.deadlineNs<<",\"now_ns\":"<<now
                <<",\"source_input\":"<<event.inputSequence<<",\"current_input\":"<<input.generation
                <<",\"source_space\":"<<event.space<<",\"current_space\":"<<input.spaceGeneration
                <<",\"focused\":"<<(input.focused?"true":"false")
                <<",\"head_valid\":"<<(input.headValid?"true":"false")
                <<",\"left_tracked\":"<<(input.hands[0].gripTracked?"true":"false")
                <<",\"right_tracked\":"<<(input.hands[1].gripTracked?"true":"false")
                <<",\"applied\":"<<(controls->FeedbackApplied()-applied)
                <<",\"rejected\":"<<(controls->FeedbackRejected()-rejected)
                <<",\"errors\":"<<(controls->FeedbackErrors()-errors)<<"}\n";eventLog.flush();}
        }
        report.feedbackApplied=controls->FeedbackApplied();report.feedbackRejected=controls->FeedbackRejected();report.feedbackErrors=controls->FeedbackErrors();
        report.captureFeedbackApplied=controls->CaptureFeedbackApplied();report.receiptFeedbackApplied=controls->ReceiptFeedbackApplied();report.lastFeedbackEvent=controls->LastFeedbackEvent();
    }
    void Events(){
        while(true){XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};const auto result=api.PollEvent(instance,&event);
            if(result==XR_EVENT_UNAVAILABLE)break;Check(result,"Poll OpenXR event");
            if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING){exit=true;return;}
            if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){
                const auto& change=reinterpret_cast<const XrEventDataReferenceSpaceChangePending&>(event);
                if(change.session==session&&change.referenceSpaceType==referenceType)spaceChanges.push_back(change.changeTime);
            }
            if(event.type==XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT&&report.userPresenceSupported){
                const auto& presence=reinterpret_cast<const XrEventDataUserPresenceChangedEXT&>(event);
                if(presence.session==session){
                    const bool wasAbsent=presenceKnown&&!userPresent;
                    presenceKnown=true;userPresent=presence.isUserPresent==XR_TRUE;++report.userPresenceEvents;
                    if(!userPresent){if(referenceValid)recoveryPending=true;presenceReturnAt=0;retained.Reset();if(controls)controls->StopFeedback();}
                    else if(wasAbsent)presenceReturnAt=GetTickCount64();
                    Log("presence");
                }
            }
            if(event.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)continue;
            const auto& change=reinterpret_cast<const XrEventDataSessionStateChanged&>(event);if(change.session!=session)continue;
            sessionState=change.state;report.lastSessionState=std::int32_t(sessionState);Log("session_state");
            if(sessionState!=XR_SESSION_STATE_FOCUSED){retained.Reset();if(controls)controls->StopFeedback();}
            if(sessionState==XR_SESSION_STATE_READY&&!running){XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                Check(api.BeginSession(session,&begin),"Begin OpenXR session");running=true;referenceValid=false;++spaceGeneration;}
            if(sessionState==XR_SESSION_STATE_STOPPING&&running){Check(api.EndSession(session),"End OpenXR session");running=false;}
            if(sessionState==XR_SESSION_STATE_EXITING||sessionState==XR_SESSION_STATE_LOSS_PENDING)exit=true;
        }
    }
    void Frame(IFrameProvider* provider){
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame{XR_TYPE_FRAME_STATE};
        Check(api.WaitFrame(session,&wait,&frame),"Wait frame");++report.waitedFrames;frameTime=frame.predictedDisplayTime;
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};Check(api.BeginFrame(session,&begin),"Begin frame");frameOpen=true;
        for(auto at=spaceChanges.begin();at!=spaceChanges.end();)if(*at<=frameTime){++spaceGeneration;referenceValid=false;retained.Reset();at=spaceChanges.erase(at);}else ++at;
        const auto wall=GetTickCount64();
        // Some wireless runtimes stop delivering frames instead of reporting a
        // tracking-loss frame. Treat a long delivery gap as a reference break.
        if(roomscale&&referenceValid&&lastFrameWall&&wall-lastFrameWall>=1500)recoveryPending=true;
        lastFrameWall=wall;
        std::array<XrCompositionLayerProjectionView,2> projectionViews{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=local;layer.viewCount=2;layer.views=projectionViews.data();
        bool submit=false,fresh=false,inputSent=false,menuActive=false;runtime::TrackingFrame tracking{};
        if(frame.shouldRender){
            std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};XrViewState viewState{XR_TYPE_VIEW_STATE};
            XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=frameTime;locate.space=local;uint32_t count=0;
            Check(api.LocateViews(session,&locate,&viewState,2,&count,views.data()),"Locate stereo views");
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};Check(api.LocateSpace(headSpace,local,frameTime,&head),"Locate head");
            constexpr auto validViews=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            constexpr auto validHead=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            if(count==2&&(viewState.viewStateFlags&validViews)==validViews&&(head.locationFlags&validHead)==validHead){
                tracking.generation=++trackingGeneration;tracking.spaceGeneration=spaceGeneration;tracking.predictedNs=frameTime;
                tracking.worldUnitsPerMeter=worldUnitsPerMeter;
                tracking.focused=sessionState==XR_SESSION_STATE_FOCUSED&&userPresent&&(!presenceReturnAt||wall-presenceReturnAt>=250);tracking.headValid=true;tracking.head=Pose(head.pose);
                if(roomscale&&trackingRecovery.Update(tracking.focused,wall))recoveryPending=true;
                if(roomscale&&tracking.focused&&recoveryPending&&interaction::UprightReference(tracking.head)){
                    referenceValid=false;++spaceGeneration;tracking.spaceGeneration=spaceGeneration;
                    retained.Reset();if(provider)provider->Suspend();
                    recoveryPending=false;++report.recenters;++report.automaticRecenters;
                }
                if(!referenceValid&&tracking.focused&&math::MakeRelativePose(tracking.head,tracking.head)){
                    const auto reference=roomscale?interaction::UprightReference(tracking.head):std::optional<math::Pose>{tracking.head};
                    if(reference){referenceHead=*reference;referenceValid=true;Log("capture_reference");}
                }
                tracking.referenceHead=referenceHead;
                bool valid=referenceValid&&bool(math::MakeRelativePose(tracking.referenceHead,tracking.head));
                for(unsigned eye=0;eye<2;++eye){tracking.eyes[eye]=Pose(views[eye].pose);const auto& fov=views[eye].fov;
                    tracking.fov[eye]={std::tan(fov.angleLeft),std::tan(fov.angleRight),std::tan(fov.angleUp),std::tan(fov.angleDown)};
                    valid&=bool(math::MakeRelativePose(tracking.referenceHead,tracking.eyes[eye]))&&bool(math::MakeLhProjectionFromFovTangents(tracking.fov[eye],.05f,100.f));}
                tracking.headValid=valid;if(valid)++report.validTrackingFrames;
                interaction::InputFrame input{};input.generation=++inputGeneration;input.spaceGeneration=spaceGeneration;input.predictedNs=frameTime;
                input.focused=tracking.focused;input.headValid=tracking.headValid;input.floorRelative=report.floorRelative;input.worldUnitsPerMeter=worldUnitsPerMeter;
                input.referenceHead=referenceHead;input.head=tracking.head;
                if(controls)controls->Sample(local,frameTime,input);
                if(roomscale&&recenter.Update(input))if(const auto reference=interaction::ExplicitRecenterReference(tracking.head,referenceHead)){
                    referenceHead=*reference;++spaceGeneration;retained.Reset();if(provider)provider->Suspend();++report.recenters;
                    tracking.referenceHead=input.referenceHead=referenceHead;tracking.spaceGeneration=input.spaceGeneration=spaceGeneration;Log("manual_recenter");
                }
                if(menu){menuActive=menu->Frame(input);menu->FilterGameplayInput(input);if(menuActive){input.focused=false;input.hands={};retained.Reset();}}
                if(provider)provider->UpdateInput(input);DispatchFeedback(provider,input);inputSent=true;++report.inputFrames;
                if(input.hands[0].gripTracked&&input.hands[1].aimTracked)++report.trackedHandFrames;
                graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};runtime::TrackingFrame renderedTracking{};
                const auto requestStart=std::chrono::steady_clock::now();
                const bool available=!menuActive&&valid&&tracking.focused&&provider&&provider->TryGetCompletedPair(report.requirements,tracking,renderedTracking,descriptor,ticket);
                const auto requestUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-requestStart).count();
                report.maxProviderWaitUs=(std::max)(report.maxProviderWaitUs,std::uint64_t(requestUs));
                if(!menuActive&&valid&&tracking.focused&&provider&&!available)++report.providerMisses;
                if(available){
                    struct Feedback {IFrameProvider& provider;const graphics::PairTicket& ticket;bool consumed=false;~Feedback(){provider.PairConsumed(ticket,consumed);}} feedback{*provider,ticket};
                    runtime::RetainedPresentation candidate;
                    // Validate metadata before replacing either released image. If
                    // Commit failed after release, old poses could label new pixels.
                    if(candidate.Commit(report.requirements,renderedTracking,descriptor,ticket)&&
                       renderedTracking.spaceGeneration==tracking.spaceGeneration&&renderedTracking.predictedNs<=tracking.predictedNs&&
                       tracking.predictedNs-renderedTracking.predictedNs<=runtime::RetainedPresentation::MaxAgeNs){
                        if(!graphics::Valid(openedDescriptor)||std::memcmp(&descriptor,&openedDescriptor,sizeof(descriptor))!=0){
                            retained.Reset();if(ammoHud)ammoHud->Reset();
                            if(consumer.Open(device.Get(),descriptor)==graphics::TransferResult::Ok)openedDescriptor=descriptor;else openedDescriptor={};
                        }
                        if(graphics::Valid(openedDescriptor)){
                            const std::array<graphics::TextureSlice,2> destinations{{{scratch[0].Get(),0},{scratch[1].Get(),0}}};
                            const bool copied=consumer.Copy(ticket,destinations)==graphics::TransferResult::Ok;
                            feedback.consumed=copied;
                            if(copied){
                                graphics::BodyPropFrame props;
                                const bool propsRead=provider->ReadBodyProps(ticket,props);
                                if(ammoHud)ammoHud->Receive(propsRead?&props:nullptr,ticket,BodyPropClockNs());
                                if(bodyProps){
                                    if(propsRead)bodyProps->Compose(props,ticket,{scratch[0].Get(),scratch[1].Get()},BodyPropClockNs);
                                    report.bodyProps=bodyProps->Statistics();
                                }
                                for(unsigned eye=0;eye<2;++eye){uint32_t index=0;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                                    Check(api.AcquireSwapchainImage(swapchains[eye],&acquire,&index),"Acquire eye image");acquired[eye]=true;
                                    XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=50000000;
                                    const auto ready=api.WaitSwapchainImage(swapchains[eye],&imageWait);Check(ready,"Wait eye image");Require(ready!=XR_TIMEOUT_EXPIRED,"OpenXR eye image wait timed out");waited[eye]=true;
                                    Require(index<images[eye].size(),"Invalid acquired eye index");
                                    context->CopyResource(images[eye][index].texture,scratch[eye].Get());
                                }
                                for(unsigned eye=0;eye<2;++eye){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                                    Check(api.ReleaseSwapchainImage(swapchains[eye],&release),"Release eye image");waited[eye]=acquired[eye]=false;}
                                retained=candidate;fresh=true;
                            }
                        }
                    }
                    if(!fresh)++report.rejectedPairs;
                }
            }
        }
        if(!inputSent){
            if(roomscale)trackingRecovery.Update(false,wall);
            interaction::InputFrame inactive{};inactive.generation=++inputGeneration;inactive.spaceGeneration=spaceGeneration;inactive.predictedNs=frameTime;
            inactive.worldUnitsPerMeter=worldUnitsPerMeter;inactive.floorRelative=report.floorRelative;
            recenter.Update(inactive);if(menu)menuActive=menu->Frame(inactive);if(provider)provider->UpdateInput(inactive);DispatchFeedback(provider,inactive);++report.inputFrames;
        }
        report.recenterInput=recenter.Evidence();
        // Menu entry also cancels an outstanding world request/flushes deferred
        // feedback. Repeat the zero-wait suspension while menu-owned so a busy
        // IPC gate cannot carry a pre-menu pair through to the resumed world.
        if(menuActive)retained.Reset();
        if(provider&&(menuActive||!frame.shouldRender||!tracking.focused||!tracking.headValid))provider->Suspend();
        if(const auto* rendered=retained.Select(report.requirements,tracking,frame.shouldRender&&!menuActive)){
            for(unsigned eye=0;eye<2;++eye){auto& projection=projectionViews[eye];const auto& pose=rendered->eyes[eye];const auto& fov=rendered->fov[eye];
                projection.pose={{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w},{pose.position.x,pose.position.y,pose.position.z}};
                projection.fov={std::atan(fov.left),std::atan(fov.right),std::atan(fov.up),std::atan(fov.down)};
                projection.subImage.swapchain=swapchains[eye];projection.subImage.imageRect.extent={std::int32_t(report.requirements.width),std::int32_t(report.requirements.height)};
            }
            submit=true;report.maxRetainedAgeNs=(std::max)(report.maxRetainedAgeNs,std::uint64_t(tracking.predictedNs-rendered->predictedNs));
        }
        std::vector<const XrCompositionLayerBaseHeader*> layers;
        if(submit)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer));
        if(ammoHud){if(const auto* hud=ammoHud->Layer(headSpace,tracking.spaceGeneration,BodyPropClockNs,
            submit&&!menuActive&&tracking.focused&&tracking.headValid)){
                layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(hud));++report.ammoCounterFrames;}
            report.ammoCounterUploads=ammoHud->Uploads();report.ammoCounterErrors=ammoHud->Errors();
            report.ammoCounterValidSamples=ammoHud->ValidSamples();report.ammoCounterInvalidSamples=ammoHud->InvalidSamples();
        }
        if(menu){menu->Layers(local,layers);report.menuFrames=menu->Frames();report.menuErrors=menu->Errors();}
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frameTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount=std::uint32_t(layers.size());end.layers=layers.data();
        const auto result=api.EndFrame(session,&end);frameOpen=false;Check(result,"End frame");++report.endedFrames;
        if(submit){++report.presentedFrames;if(fresh)++report.submittedPairs;else ++report.reusedFrames;}
        else ++report.blankFrames;
    }
    void Run(const HostOptions& options){
        if(options.menu){menu=std::make_unique<OpenXrMenu>();menu->Initialize(instance,session,api.get,device.Get(),options.menu);}
        const auto start=std::chrono::steady_clock::now();bool requested=false;auto shutdown=start;
        while(!exit){Events();if(exit)break;const auto now=std::chrono::steady_clock::now();
            if(!requested&&((options.seconds&&now-start>=std::chrono::seconds(options.seconds))||(options.stopRequested&&options.stopRequested()))){
                if(!running)break;Check(api.RequestExitSession(session),"Request session exit");requested=true;shutdown=now;
            }
            if(requested&&(!running||now-shutdown>=std::chrono::seconds(2)))break;
            if(running)Frame(options.provider);else Sleep(10);
        }
    }
};
}
HostReport RunOpenXrHost(const HostOptions& options)noexcept {
    HostReport report;
    try {Require(options.probeOnly||options.seconds||bool(options.stopRequested),"Continuous session requires a stop source");Host host(report);
        struct ReleaseInput {Host& host;IFrameProvider* provider;~ReleaseInput(){if(provider){interaction::InputFrame f{};f.generation=++host.inputGeneration;f.spaceGeneration=host.spaceGeneration;f.predictedNs=host.frameTime>0?host.frameTime:1;provider->UpdateInput(f);provider->Suspend();}}} release{host,options.provider};
        host.Initialize(options);if(!options.probeOnly)host.Run(options);report.okay=true;}
    catch(const std::exception& error){report.error=error.what();}
    catch(...){report.error="Unknown OpenXR host failure";}
    return report;
}
}
