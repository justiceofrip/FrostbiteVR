#include "fvr/xr/OpenXrHost.h"
#include "fvr/xr/HostCopyLease.h"
#include "fvr/xr/HostSourceImportCache.h"
#include "fvr/xr/HostComposedCopyService.h"
#include "fvr/runtime/RetainedPresentation.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11SceneDepthPair.h"
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
#include "fvr/graphics/D3D11StereoComfort.h"
#include "fvr/interaction/RecenterPolicy.h"
#include "fvr/interaction/TrackingReference.h"

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
struct HostCopyTimerBackend {
    HANDLE timer=nullptr;DWORD error=0;
    bool Create()noexcept {
        // Omitting MANUAL_RESET creates an auto-reset timer. Unsupported high
        // resolution timers disable the private experiment; there is no fallback.
        timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
        if(!timer)error=GetLastError();return timer!=nullptr;
    }
    bool Arm()noexcept {
        // SetWaitableTimerEx cancels an active due time and resets the signal.
        // This also clears a late signal after WAIT_TIMEOUT; no APC is installed
        // and no cancel/drain loop or second timer is needed between episodes.
        LARGE_INTEGER due{};due.QuadPart=-10000; // One relative millisecond.
        if(SetWaitableTimerEx(timer,&due,0,nullptr,nullptr,nullptr,0))return true;
        error=GetLastError();return false;
    }
    HostCopyWaitResult Wait()noexcept {
        const auto result=WaitForSingleObject(timer,2);
        if(result==WAIT_OBJECT_0)return HostCopyWaitResult::Signaled;
        if(result==WAIT_TIMEOUT)return HostCopyWaitResult::TimedOut;
        error=result==WAIT_FAILED?GetLastError():result;return HostCopyWaitResult::Failed;
    }
    DWORD Error()const noexcept{return error;}
    void Close()noexcept {CloseHandle(timer);timer=nullptr;}
};
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
    std::array<std::vector<ComPtr<ID3D11RenderTargetView>>,2> imageTargets;
    std::array<bool,2> acquired{},waited{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::array<graphics::D3D11PairConsumer,HostSourceImportCache::Capacity> consumers;
    HostSourceImportCache sourceImports;
    graphics::D3D11SceneDepthConsumer sceneDepth;bool sceneDepthSeen=false;

    runtime::RetainedPresentation retained;
    // Own the exact delivered pair until ALL shared color/depth reads finish.
    // XR input, menus and frame submission keep running while the GPU is busy.
    struct PendingCopy : HostCopyLease {
        runtime::RetainedPresentation presentation;
        graphics::SceneDepthViews depth{};
        graphics::BodyPropFrame props{};
        std::int64_t startedNs=0,submittedNs=0,completedNs=0;
        bool depthCopied=false,propsRead=false,gpuOrdered=false;
        std::int64_t composedNs=0;
        HostSourceImportCache& imports;
        graphics::TextureDescriptor descriptor{};
        unsigned importIndex=HostSourceImportCache::Capacity,sourceCount=1;
        PendingCopy(IFrameProvider& p,const graphics::PairTicket& t,HostSourceImportCache& cache):HostCopyLease(p,t),imports(cache){}
        ~PendingCopy(){if(importIndex<HostSourceImportCache::Capacity)(void)imports.Release(ticket,Copying());}
        bool Bind(const HostSourceImportCache::Selection& selected)noexcept {
            if(importIndex<HostSourceImportCache::Capacity||!imports.Bind(selected))return false;
            importIndex=selected.index;sourceCount=selected.count;descriptor=selected.descriptor;return true;
        }
    };
    std::unique_ptr<PendingCopy> pendingCopy;
    std::array<ComPtr<ID3D11Texture2D>,2> scratch;
    graphics::D3D11StereoComfort stereoComfort;
    std::unique_ptr<graphics::D3D11BodyPropCompositor> bodyProps;
    HostReport& report;
    PrivateHostCopyWait<HostCopyTimerBackend> copyServiceWait;
    bool running=false,exit=false,frameOpen=false,referenceValid=false;
    XrTime frameTime=0;
    std::vector<XrTime> spaceChanges;
    math::Pose referenceHead{};
    std::uint64_t trackingGeneration=0,spaceGeneration=1;
    std::uint32_t trackingBasisGeneration=1;
    DWORD copyServiceThread=0;
    ComPtr<ID3D11Query> shutdownCompletion;
    // Used only at teardown; the source-copy event precedes composition and
    // therefore cannot prove that commands referencing XR images are finished.
    bool DrainFinalGraphics()noexcept {
        auto& r=report.privateGpuOrderedPresent;
        if(!r.configured||!session)return true;
        r.drainAttempted=true;
        if(!context||!shutdownCompletion){r.drainError=E_UNEXPECTED;return false;}
        context->End(shutdownCompletion.Get());context->Flush();
        const auto start=BodyPropClockNs();
        if(start<=0){r.drainError=ERROR_INVALID_TIME;return false;}
        for(unsigned poll=0;poll<2000;++poll){
            BOOL done=FALSE;++r.drainPolls;
            const auto result=context->GetData(shutdownCompletion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            const auto now=BodyPropClockNs();
            if(now>=start)r.drainElapsedNs=std::uint64_t(now-start);
            if(result==S_OK&&done){r.drained=true;return true;}
            if(FAILED(result)){r.drainError=std::uint32_t(result);return false;}
            if(result!=S_OK&&result!=S_FALSE){r.drainError=E_UNEXPECTED;return false;}
            if(now<start||now<=0){r.drainError=ERROR_INVALID_TIME;return false;}
            if(r.drainElapsedNs>=2000000000){r.drainError=WAIT_TIMEOUT;return false;}
            Sleep(1); // Shutdown only; no per-frame waiting or timer policy.
        }
        r.drainError=WAIT_TIMEOUT;return false;
    }
    void RetainUndrainedRuntime()noexcept {
        // Bounded failure: do not call xrDestroySwapchain on outstanding GPU
        // writes. Retain the runtime/device graph until this host process exits.
        // Menu/HUD destructors themselves destroy swapchains, so retain those
        // owners too. Already queued D3D commands retain their resource refs.
        report.privateGpuOrderedPresent.retainedUntilProcessExit=true;
        report.okay=false;
        try {report.error="Final XR graphics drain failed; runtime retained until process exit";}catch(...){}
        controls.release();menu.release();ammoHud.release();
        shutdownCompletion.Detach();context.Detach();device.Detach();
        // Raw XR handles and loader HMODULE deliberately remain undisposed.
    }
    void CancelPendingPresentation()noexcept {
        if(pendingCopy){
            if(pendingCopy->Composed())retained.Reset();
            pendingCopy->CancelPresentation();pendingCopy->presentation.Reset();
        }
    }
    void RetirePendingCopy()noexcept {
        if(!pendingCopy)return;
        // Negative feedback retires the source generation; it does not authorize
        // overwriting resources still referenced by the immediate-context queue.
        if(pendingCopy->Copying()){
            if(pendingCopy->importIndex<HostSourceImportCache::Capacity){auto& consumer=consumers[pendingCopy->importIndex];
                if(consumer.HasPendingCopy())consumer.CancelCopy(pendingCopy->ticket);}
            // The containing lease poisons this exact import generation only.
        }
        // Ready owns only local images: no CancelCopy or second acknowledgement.
        pendingCopy.reset();
    }
    void AdvanceReference(interaction::TrackingReferenceChange change){
        CancelPendingPresentation();
        if(!interaction::AdvanceTrackingReference(spaceGeneration,trackingBasisGeneration,change))
            throw std::runtime_error("Tracking reference epoch exhausted");
    }
    explicit Host(HostReport& r):report(r),copyServiceWait(r.privateHostCopyService){
        const auto error=GetLastError();wchar_t raw[8]{};
        const auto size=GetEnvironmentVariableW(L"FVR_PRIVATE_HOST_COPY_SERVICE_MS",raw,unsigned(std::size(raw)));
        if(size&&size<std::size(raw))report.privateHostCopyService.budgetMs=ParseHostCopyBudget({raw,size});
        if(report.privateHostCopyService.budgetMs){
            LARGE_INTEGER f{};
            if(QueryPerformanceFrequency(&f)&&f.QuadPart>=1000){
                report.privateHostCopyService.frequency=std::uint64_t(f.QuadPart);
                if(copyServiceWait.Initialize())copyServiceThread=GetCurrentThreadId();
            }else ++report.privateHostCopyService.clockFailed;
        }
        wchar_t ordered[4]{};
        const auto orderedSize=GetEnvironmentVariableW(L"FVR_PRIVATE_GPU_ORDERED_PRESENT",ordered,unsigned(std::size(ordered)));
        report.privateGpuOrderedPresent.configured=orderedSize==1&&ordered[0]==L'1';
        SetLastError(error);
        Require(!report.privateGpuOrderedPresent.configured||!report.privateHostCopyService.budgetMs,
            "GPU-ordered presentation requires the private CPU copy-wait service disabled");
    }
    ~Host(){
        if(menu)report.firstMenuFailure=menu->FirstFailure();
        AbortFrame();
        const bool graphicsDrained=DrainFinalGraphics();
        RetirePendingCopy();
        // One bounded shutdown snapshot in the existing event stream. Frame
        // diagnostics never perform per-frame file I/O or change XR scheduling.
        if(eventLog){eventLog<<"{\"event\":\"host_frame_diagnostics\",\"wall_ms\":"<<GetTickCount64()<<",\"diagnostics\":";
            report.frameDiagnostics.Write(eventLog);eventLog<<"}\n";eventLog.flush();}
        if(eventLog){const auto& imports=sourceImports.Stats();const auto depth=sceneDepth.ImportStatistics();
            eventLog<<"{\"event\":\"host_source_imports\",\"source_slots\":"<<sourceImports.SourceCount()<<",\"color_open_attempts\":"<<imports.opens
                <<",\"color_cache_hits\":"<<imports.hits<<",\"lifetime_resets\":"<<imports.resets
                <<",\"rejected\":"<<imports.rejected<<",\"poisoned\":"<<imports.poisoned
                <<",\"depth_source_opens\":"<<depth.sourceOpens<<",\"depth_local_creates\":"<<depth.localCreates<<"}\n";eventLog.flush();}
        for(auto& consumer:consumers)consumer.Reset();
        if(!graphicsDrained){RetainUndrainedRuntime();return;}
        controls.reset();menu.reset();ammoHud.reset();bodyProps.reset();
        for(auto& targets:imageTargets)targets.clear();
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
            const auto started=BodyPropClockNs();const auto result=api.EndFrame(session,&end);
            report.frameDiagnostics.CleanupEnd(started,BodyPropClockNs(),std::int32_t(result),XR_SUCCEEDED(result));
            frameOpen=false;
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
        report.recommendedEyeWidth=req.width;report.recommendedEyeHeight=req.height;
        report.maxEyeWidth=(std::min)(viewInfo[0].maxImageRectWidth,viewInfo[1].maxImageRectWidth);
        report.maxEyeHeight=(std::min)(viewInfo[0].maxImageRectHeight,viewInfo[1].maxImageRectHeight);
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
        if(report.privateGpuOrderedPresent.configured){
            D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
            Hr(device->CreateQuery(&query,&shutdownCompletion),"Create final XR graphics drain query");
            report.privateGpuOrderedPresent.available=true;
        }
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
            imageTargets[eye].resize(count);
            D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};targetDesc.Format=DXGI_FORMAT(req.format);targetDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            for(unsigned n=0;n<count;++n)Hr(device->CreateRenderTargetView(images[eye][n].texture,&targetDesc,&imageTargets[eye][n]),"Create eye presentation target");
        }
        // Copy incoming pairs into private scratch first. A failed/partial copy must
        // never replace the swapchain images that supply the retained pair.
        D3D11_TEXTURE2D_DESC scratchDesc{};scratchDesc.Width=req.width;scratchDesc.Height=req.height;
        scratchDesc.Format=DXGI_FORMAT(req.format);scratchDesc.MipLevels=scratchDesc.ArraySize=1;
        scratchDesc.SampleDesc.Count=1;scratchDesc.Usage=D3D11_USAGE_DEFAULT;
        scratchDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        for(auto& texture:scratch)Hr(device->CreateTexture2D(&scratchDesc,nullptr,&texture),"Create private eye scratch");
        Require(stereoComfort.Initialize(context.Get()),"Initialize paired eye comfort safeguard");
        if(options.provider){auto hud=std::make_unique<OpenXrAmmoHud>();
            if(hud->Initialize(instance,session,api.get,device.Get(),req.format)){ammoHud=std::move(hud);report.ammoCounterReady=true;}}
        if(options.bodyProps&&!options.bodyProps->empty()){
            auto compositor=std::make_unique<graphics::D3D11BodyPropCompositor>();
            if(compositor->Initialize(context.Get(),req.width,req.height,req.format,options.bodyProps)){bodyProps=std::move(compositor);report.bodyPropsReady=true;}
        }
        Require(sceneDepth.Initialize(device.Get()),"Initialize local scene-depth consumer");
        if(!options.readyPath.empty()){
            Require(options.readyPath.is_absolute()&&!std::filesystem::exists(options.readyPath),"Readiness report requires a new absolute path");
            std::ofstream ready(options.readyPath);ready<<"{\"session_created\":true,\"width\":"<<req.width<<",\"height\":"<<req.height<<",\"format\":"<<req.format
                <<",\"recommended_eye_width\":"<<report.recommendedEyeWidth<<",\"recommended_eye_height\":"<<report.recommendedEyeHeight
                <<",\"max_eye_width\":"<<report.maxEyeWidth<<",\"max_eye_height\":"<<report.maxEyeHeight
                <<",\"user_presence_supported\":"<<(report.userPresenceSupported?"true":"false")<<"}\n";
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
                    if(!userPresent){if(referenceValid)recoveryPending=true;presenceReturnAt=0;retained.Reset();CancelPendingPresentation();if(controls)controls->StopFeedback();}
                    else if(wasAbsent)presenceReturnAt=GetTickCount64();
                    Log("presence");
                }
            }
            if(event.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)continue;
            const auto& change=reinterpret_cast<const XrEventDataSessionStateChanged&>(event);if(change.session!=session)continue;
            sessionState=change.state;report.lastSessionState=std::int32_t(sessionState);Log("session_state");
            if(sessionState!=XR_SESSION_STATE_FOCUSED){retained.Reset();CancelPendingPresentation();if(controls)controls->StopFeedback();}
            if(sessionState==XR_SESSION_STATE_READY&&!running){XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                Check(api.BeginSession(session,&begin),"Begin OpenXR session");running=true;referenceValid=false;AdvanceReference(interaction::TrackingReferenceChange::SessionRestart);}
            if(sessionState==XR_SESSION_STATE_STOPPING&&running){
                // No further Frame calls are guaranteed before a new session.
                RetirePendingCopy();Check(api.EndSession(session),"End OpenXR session");running=false;
            }
            if(sessionState==XR_SESSION_STATE_EXITING||sessionState==XR_SESSION_STATE_LOSS_PENDING)exit=true;
        }
    }
    void Frame(IFrameProvider* provider,bool stopping){
        static_assert(runtime::RetainedPresentation::MaxAgeNs==250000000);
        HostFrameObservation diagnostic;diagnostic.frame=report.waitedFrames+1;
        diagnostic.wallMs=GetTickCount64();diagnostic.startedNs=BodyPropClockNs();
        diagnostic.provider=provider!=nullptr;diagnostic.present=userPresent;
        diagnostic.focused=sessionState==XR_SESSION_STATE_FOCUSED;diagnostic.space=spaceGeneration;
        struct Record {HostReport& report;HostFrameObservation& frame;~Record(){
            frame.endedNs=BodyPropClockNs();report.frameDiagnostics.Observe(frame);
        }} record{report,diagnostic};
        const auto timed=[&](HostStage stage,auto&& operation)->decltype(auto){
            HostStageTimer timer(diagnostic,stage,BodyPropClockNs);return operation();
        };
        // An uncomposed carried image still needs this frame's presentation slot.
        // A composed image owns only source-query retirement: once that exact
        // query completes, a successor may be fetched later in this frame.
        bool carriedCopy=pendingCopy&&!pendingCopy->Composed();
        const bool carriedComposed=pendingCopy&&pendingCopy->Composed();
        const auto observeCopy=[&]{
            if(!pendingCopy)return;
            const auto& pending=*pendingCopy;diagnostic.SetCopyTicket(pending.ticket);
            diagnostic.copyCancelled=pending.Cancelled();
            diagnostic.copyComposed=pending.Composed();diagnostic.copyComposedNs=pending.composedNs;
            diagnostic.copyPending=pending.Copying();
            diagnostic.copySubmittedNs=pending.submittedNs;diagnostic.copyCompletedNs=pending.completedNs;
            const auto now=pending.Ready()?pending.completedNs:BodyPropClockNs();
            diagnostic.copyPendingAgeNs=pending.startedNs>0&&now>=pending.startedNs?std::uint64_t(now-pending.startedNs):0;
        };
        const auto completeCopy=[&](bool beforeWait){
            auto& pending=*pendingCopy;
            // Timestamp observed completion before IPC acknowledgement work.
            pending.completedNs=BodyPropClockNs();
            diagnostic.copyCompleted=pending.Complete();
            diagnostic.copyPreWaitCompleted=beforeWait&&diagnostic.copyCompleted;
            observeCopy();
        };
        const auto pollCopy=[&](bool beforeWait){
            if(!pendingCopy||!pendingCopy->Copying())return;
            Require(pendingCopy->importIndex<HostSourceImportCache::Capacity&&sourceImports.ActiveEntry(pendingCopy->ticket)==pendingCopy->importIndex,
                "Pending copy lost its exact imported source entry");
            auto& consumer=consumers[pendingCopy->importIndex];
            observeCopy();diagnostic.copyPreWaitPolled|=beforeWait;
            const auto copied=timed(HostStage::ColorPoll,[&]{return consumer.PollCopy(pendingCopy->ticket);});
            diagnostic.copyResult=std::uint32_t(copied);diagnostic.copyError=consumer.LastError();
            if(copied==graphics::TransferResult::Ok){completeCopy(beforeWait);return;}
            // Single zero-wait poll. Busy retains images and still permits this
            // frame's input/menu work; it never spins or extends the pose age.
            constexpr std::uint64_t CopyRetirementNs=2000000000;
            const bool expired=diagnostic.copyPendingAgeNs>=CopyRetirementNs;
            if(copied!=graphics::TransferResult::Busy||!consumer.HasPendingCopy()||expired){
                if(expired)diagnostic.copyError=WAIT_TIMEOUT;
                if(pendingCopy->Composed())retained.Reset();
                RetirePendingCopy();++report.rejectedPairs;
                diagnostic.copyPending=false;
                diagnostic.pairRejected=diagnostic.copyRetired=true;
            }
        };
        // Events and known stop/focus loss precede source release. A reference
        // change announced for a future frame also cancels the queued successor,
        // while the already delivered image keeps its original effective-space
        // validation below. No new request is made by this service point.
        if(stopping||sessionState!=XR_SESSION_STATE_FOCUSED||!userPresent){
            CancelPendingPresentation();if(provider)provider->Suspend();
        }else if(!spaceChanges.empty()&&provider)provider->Suspend();
        pollCopy(true);
        // A fault retired the old generation, not a normal completion. Keep
        // this frame's only copy observation and retry delivery next frame.
        if(carriedComposed&&!pendingCopy)carriedCopy=true;
        if(pendingCopy&&pendingCopy->Composed()&&pendingCopy->Ready()){
            // Fixed second diagnostic slot, never an ownership/ACK authority.
            (void)diagnostic.PreserveCompletedCopy();
            pendingCopy.reset();++report.privateGpuOrderedPresent.preWaitRetirements;
        }
        if(provider)timed(HostStage::Feedback,[&]{provider->ServiceFeedback();});
        const auto publishInput=[&](const interaction::InputFrame& input){
            if(!provider)return;
            diagnostic.priorInputSuccessGeneration=report.frameDiagnostics.lastInputSuccessGeneration;
            diagnostic.priorInputSuccessReturnedNs=report.frameDiagnostics.lastInputSuccessReturnedNs;
            diagnostic.inputAttempted=true;diagnostic.inputGeneration=input.generation;
            diagnostic.inputFocused=input.focused;diagnostic.inputHeadValid=input.headValid;
            diagnostic.inputHandsTracked=input.hands[0].gripTracked&&input.hands[1].gripTracked;
            diagnostic.inputStartedNs=BodyPropClockNs();
            const auto result=timed(HostStage::Input,[&]{return provider->UpdateInputObserved(input);});
            diagnostic.inputReturnedNs=BodyPropClockNs();diagnostic.inputKnown=result.known;
            diagnostic.inputSucceeded=result.succeeded;diagnostic.inputResult=result.code;
            report.frameDiagnostics.Input(input.generation,diagnostic.inputReturnedNs,result.known,result.succeeded);
        };
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame{XR_TYPE_FRAME_STATE};
        Check(timed(HostStage::WaitFrame,[&]{return api.WaitFrame(session,&wait,&frame);}),"Wait frame");++report.waitedFrames;frameTime=frame.predictedDisplayTime;
        diagnostic.predictedNs=frameTime;diagnostic.shouldRender=frame.shouldRender==XR_TRUE;
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};Check(timed(HostStage::BeginFrame,[&]{return api.BeginFrame(session,&begin);}),"Begin frame");frameOpen=true;
        for(auto at=spaceChanges.begin();at!=spaceChanges.end();)if(*at<=frameTime){AdvanceReference(interaction::TrackingReferenceChange::RuntimeSpaceReplacement);referenceValid=false;retained.Reset();at=spaceChanges.erase(at);}else ++at;
        const auto wall=GetTickCount64();
        diagnostic.focused=sessionState==XR_SESSION_STATE_FOCUSED&&userPresent&&(!presenceReturnAt||wall-presenceReturnAt>=250);
        // Some wireless runtimes stop delivering frames instead of reporting a
        // tracking-loss frame. Treat a long delivery gap as a reference break.
        if(roomscale&&referenceValid&&lastFrameWall&&wall-lastFrameWall>=1500)recoveryPending=true;
        lastFrameWall=wall;
        std::array<XrCompositionLayerProjectionView,2> projectionViews{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=local;layer.viewCount=2;layer.views=projectionViews.data();
        bool submit=false,fresh=false,inputSent=false,menuActive=false;runtime::TrackingFrame tracking{};
        interaction::InputFrame hudInput{};
        if(frame.shouldRender){
            std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};XrViewState viewState{XR_TYPE_VIEW_STATE};
            XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=frameTime;locate.space=local;uint32_t count=0;
            Check(timed(HostStage::LocateViews,[&]{return api.LocateViews(session,&locate,&viewState,2,&count,views.data());}),"Locate stereo views");
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};Check(timed(HostStage::LocateHead,[&]{return api.LocateSpace(headSpace,local,frameTime,&head);}),"Locate head");
            constexpr auto validViews=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            constexpr auto validHead=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            diagnostic.viewsValid=count==2&&(viewState.viewStateFlags&validViews)==validViews;
            diagnostic.headValid=(head.locationFlags&validHead)==validHead;
            if(count==2&&(viewState.viewStateFlags&validViews)==validViews&&(head.locationFlags&validHead)==validHead){
                tracking.generation=++trackingGeneration;tracking.spaceGeneration=spaceGeneration;tracking.predictedNs=frameTime;
                tracking.worldUnitsPerMeter=worldUnitsPerMeter;
                tracking.focused=sessionState==XR_SESSION_STATE_FOCUSED&&userPresent&&(!presenceReturnAt||wall-presenceReturnAt>=250);tracking.headValid=true;tracking.head=Pose(head.pose);
                if(roomscale&&trackingRecovery.Update(tracking.focused,wall))recoveryPending=true;
                if(roomscale&&tracking.focused&&recoveryPending&&interaction::UprightReference(tracking.head)){
                    referenceValid=false;AdvanceReference(interaction::TrackingReferenceChange::AutomaticRecovery);tracking.spaceGeneration=spaceGeneration;
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
                input.trackingBasisGeneration=trackingBasisGeneration;
                input.focused=tracking.focused;input.headValid=tracking.headValid;input.floorRelative=report.floorRelative;input.worldUnitsPerMeter=worldUnitsPerMeter;
                input.referenceHead=referenceHead;input.head=tracking.head;
                if(controls)timed(HostStage::Controls,[&]{controls->Sample(local,frameTime,input);});
                if(roomscale&&recenter.Update(input))if(const auto reference=interaction::ExplicitRecenterReference(tracking.head,referenceHead)){
                    referenceHead=*reference;AdvanceReference(interaction::TrackingReferenceChange::ManualRecenter);retained.Reset();if(provider)provider->Suspend();++report.recenters;
                    tracking.referenceHead=input.referenceHead=referenceHead;tracking.spaceGeneration=input.spaceGeneration=spaceGeneration;Log("manual_recenter");
                }
                if(menu){menuActive=timed(HostStage::Menu,[&]{return menu->Frame(input);});menu->FilterGameplayInput(input);if(menuActive){input.focused=false;input.hands={};retained.Reset();}}
                hudInput=input;
                publishInput(input);timed(HostStage::Feedback,[&]{DispatchFeedback(provider,input);});inputSent=true;++report.inputFrames;
                if(input.hands[0].gripTracked&&input.hands[1].aimTracked)++report.trackedHandFrames;
            }
        }
        if(!inputSent){
            if(roomscale)trackingRecovery.Update(false,wall);
            interaction::InputFrame inactive{};inactive.generation=++inputGeneration;inactive.spaceGeneration=spaceGeneration;inactive.predictedNs=frameTime;
            inactive.trackingBasisGeneration=trackingBasisGeneration;
            inactive.worldUnitsPerMeter=worldUnitsPerMeter;inactive.floorRelative=report.floorRelative;
            recenter.Update(inactive);if(menu)menuActive=timed(HostStage::Menu,[&]{return menu->Frame(inactive);});publishInput(inactive);timed(HostStage::Feedback,[&]{DispatchFeedback(provider,inactive);});++report.inputFrames;
        }
        report.recenterInput=recenter.Evidence();
        // Menu entry also cancels an outstanding world request/flushes deferred
        // feedback. Repeat the zero-wait suspension while menu-owned so a busy
        // IPC gate cannot carry a pre-menu pair through to the resumed world.
        if(menuActive)retained.Reset();
        // A menu channel must explicitly admit Gameplay with its original
        // native lease. Unknown/expired loading ownership is not a menu close.
        // Providers without a menu channel retain their existing world route.
        const bool worldAllowed=!stopping&&frame.shouldRender&&!menuActive&&
            (!menu||menu->WorldAvailable())&&tracking.focused&&tracking.headValid;
        if(!worldAllowed){CancelPendingPresentation();if(provider)provider->Suspend();}
        // A previously composed source may finish while WaitFrame/input ran.
        // Retire it before provider admission, after current cancellation. This
        // is one zero-wait poll of the held ticket, never another delivery.
        const auto postInput=ServiceComposedCopyAfterInput(pendingCopy,[&]{
            diagnostic.copyPostInputPolled=true;++report.privateGpuOrderedPresent.postInputPolls;
            pollCopy(false);
        },[&](const PendingCopy&){
            diagnostic.copyPostInputCompleted=true;++report.privateGpuOrderedPresent.postInputRetirements;
            return diagnostic.PreserveCompletedCopy();
        });
        // A fault or diagnostic overflow must not be overwritten by admission
        // of a new image in this frame. Busy keeps ordinary input/presentation.
        if(postInput==ComposedCopyServiceResult::Retired||postInput==ComposedCopyServiceResult::ArchiveFailed)carriedCopy=true;
        // Current menu/recenter/tracking cancellation precedes a retry of an
        // early completion receipt that encountered a contended IPC mutex.
        if(provider)timed(HostStage::Feedback,[&]{provider->ServiceFeedback();});
        bool submittedCopy=false;
        // Request metadata can lead pixel delivery by one immutable pose. This
        // service remains after current input/cancellation and real old-copy
        // retirement; it cannot overwrite or release the held source images.
        if(worldAllowed&&provider)timed(HostStage::Provider,[&]{provider->ServiceRequests(report.requirements,tracking);});
        if(!carriedCopy&&!pendingCopy&&worldAllowed&&provider){
            graphics::TextureDescriptor descriptor{};graphics::PairTicket ticket{};runtime::TrackingFrame renderedTracking{};
            diagnostic.providerQueried=true;
            const auto requestStart=std::chrono::steady_clock::now();
            const bool available=timed(HostStage::Provider,[&]{return provider->TryGetCompletedPair(report.requirements,tracking,renderedTracking,descriptor,ticket);});
            diagnostic.newPair=available;
            const auto requestUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-requestStart).count();
            report.maxProviderWaitUs=(std::max)(report.maxProviderWaitUs,std::uint64_t(requestUs));
            if(!available)++report.providerMisses;
            if(available){
                struct DeliveryGuard {IFrameProvider& provider;const graphics::PairTicket& ticket;bool transferred=false;
                    ~DeliveryGuard(){if(!transferred)provider.PairConsumed(ticket,false);}} delivered{*provider,ticket};
                pendingCopy=std::make_unique<PendingCopy>(*provider,ticket,sourceImports);
                delivered.transferred=true;
                auto& pending=*pendingCopy;
                diagnostic.SetCopyTicket(ticket);
                // Validate original poses before changing presenter-owned pixels.
                bool accepted=pending.presentation.Commit(report.requirements,renderedTracking,descriptor,ticket)&&
                    pending.presentation.Select(report.requirements,tracking,worldAllowed);
                HostSourceImportCache::Selection import;runtime::SourceSlotIdentity source;
                if(accepted){
                    const unsigned count=provider->SourceSlotCount();
                    // v8 is unchanged. A negotiated v9 pair must have explicit,
                    // exact delivered-slot evidence; failure is never fallback.
                    accepted=(count==1||((count==2||count==3)&&provider->ReadSourceSlot(ticket,source)))&&
                        sourceImports.Select(count,source,descriptor,ticket,import);
                    if(accepted){
                        if(import.resetPresentation){retained.Reset();sceneDepth.ResetSources();sceneDepthSeen=false;if(ammoHud)ammoHud->Reset();}
                        if(import.resetAll)for(auto& entry:consumers)entry.Reset();
                        auto& consumer=consumers[import.index];
                        if(import.open){const auto opened=timed(HostStage::SharedOpen,[&]{return consumer.Open(device.Get(),descriptor);});
                            diagnostic.copyResult=std::uint32_t(opened);diagnostic.copyError=consumer.LastError();
                            accepted=opened==graphics::TransferResult::Ok;
                            if(!accepted)sourceImports.OpenFailed(import);}
                        if(accepted)accepted=pending.Bind(import);
                    }
                }
                if(accepted){
                    auto& consumer=consumers[pending.importIndex];
                    graphics::SceneDepthFrame depthFrame;
                    const bool hasDepth=provider->ReadSceneDepth(descriptor,ticket,depthFrame);
                    sceneDepthSeen|=hasDepth;
                    // Submit color AFTER every depth read on the same context.
                    // Keep sources owned until a later zero-wait poll completes.
                    // A v9 delivery explicitly distinguishes canonical absence
                    // from a missing/malformed advertised attachment. Do not copy
                    // unexpected depth or borrow the previous pair's local views.
                    const bool depthConsistent=pending.sourceCount==1||source.depthPresent==hasDepth;
                    pending.depthCopied=depthConsistent&&hasDepth&&timed(HostStage::DepthCopy,[&]{return sceneDepth.CopyBeforeColor(depthFrame,descriptor,ticket,pending.importIndex,pending.sourceCount)&&sceneDepth.Views(ticket,pending.depth);});
                    if(hasDepth)++report.sceneDepthReceived;if(pending.depthCopied)++report.sceneDepthCopied;
                    if(sceneDepthSeen&&!pending.depthCopied)++report.sceneDepthOmitted;
                    pending.propsRead=provider->ReadBodyProps(ticket,pending.props);
                    if(pending.sourceCount>1&&(!depthConsistent||(hasDepth&&!pending.depthCopied))){
                        RetirePendingCopy();++report.rejectedPairs;diagnostic.pairRejected=diagnostic.copyRetired=true;
                    }else {
                    const std::array<graphics::TextureSlice,2> destinations{{{scratch[0].Get(),0},{scratch[1].Get(),0}}};
                    pending.startedNs=BodyPropClockNs();submittedCopy=diagnostic.copySubmitted=true;
                    const auto copied=timed(HostStage::ColorCopy,[&]{return consumer.SubmitCopy(ticket,destinations);});
                    pending.submittedNs=BodyPropClockNs();diagnostic.copySubmittedNs=pending.submittedNs;
                    diagnostic.copyResult=std::uint32_t(copied);diagnostic.copyError=consumer.LastError();
                    if(copied==graphics::TransferResult::Ok)completeCopy(false);
                    accepted=pending.Ready()||(copied==graphics::TransferResult::Busy&&consumer.HasPendingCopy());
                    pending.gpuOrdered=accepted&&report.privateGpuOrderedPresent.available&&
                        descriptor.sharing==graphics::TextureSharing::LegacyFenced;
                    }
                }
                if(!accepted&&!diagnostic.copyRetired){RetirePendingCopy();++report.rejectedPairs;diagnostic.pairRejected=diagnostic.copyRetired=true;}
            }
        }
        // A short, opt-in opportunity to release THIS source set before the
        // next xrWaitFrame. The current frame already published input/menu
        // state. Expiry keeps the ordinary asynchronous copy path unchanged.
        bool copyPolledAfterSubmit=false;
        if(report.privateHostCopyService.budgetMs&&report.privateHostCopyService.available&&
           submittedCopy&&!carriedCopy&&worldAllowed&&pendingCopy&&pendingCopy->Copying()&&
           pendingCopy->importIndex<HostSourceImportCache::Capacity&&pendingCopy->descriptor.sharing==graphics::TextureSharing::LegacyFenced&&
           consumers[pendingCopy->importIndex].HasPendingCopy()){
            auto& consumer=consumers[pendingCopy->importIndex];
            const auto* const expected=pendingCopy.get();const auto ticket=expected->ticket;
            auto* const expectedContext=context.Get();auto* const expectedDevice=device.Get();
            const auto expectedSpace=spaceGeneration;
            const auto admitted=[&]()noexcept {
                return report.privateHostCopyService.available&&GetCurrentThreadId()==copyServiceThread&&expectedContext&&expectedDevice&&
                    context.Get()==expectedContext&&device.Get()==expectedDevice&&running&&frameOpen&&!exit&&
                    sessionState==XR_SESSION_STATE_FOCUSED&&userPresent&&referenceValid&&worldAllowed&&
                    spaceGeneration==expectedSpace&&tracking.spaceGeneration==expectedSpace&&
                    pendingCopy&&pendingCopy.get()==expected&&!pendingCopy->Cancelled()&&
                    std::memcmp(&pendingCopy->ticket,&ticket,sizeof(ticket))==0&&
                    sourceImports.ActiveEntry(ticket)==expected->importIndex&&
                    expected->descriptor.sharing==graphics::TextureSharing::LegacyFenced&&graphics::Valid(ticket,expected->descriptor)&&
                    (pendingCopy->Ready()||consumer.HasPendingCopy());
            };
            const auto pending=[&]()noexcept {return pendingCopy&&pendingCopy->Copying();};
            const auto pump=[&]()noexcept {copyPolledAfterSubmit=true;pollCopy(false);};
            const auto clock=[]()noexcept {LARGE_INTEGER q{};return QueryPerformanceCounter(&q)&&q.QuadPart>0?std::uint64_t(q.QuadPart):0;};
            const auto pause=[&]()noexcept {return copyServiceWait.Pause();};
            const auto serviced=ServiceBoundedHostCopy(report.privateHostCopyService.BudgetTicks(),admitted,pending,pump,clock,pause);
            report.privateHostCopyService.Observe(serviced);
            // Complete() owns the sole positive ACK. A busy IPC mutex may have
            // deferred it; this is a zero-wait flush, never another delivery.
            if(provider)timed(HostStage::Feedback,[&]{provider->ServiceFeedback();});
        }
        if(pendingCopy){
            // Give a newly submitted normal copy one nonblocking completion
            // query before composition, unless the private service polled it.
            // Busy retains the exact source; only query success makes it Ready.
            ServiceUncomposedCopyBeforePresentation(pendingCopy,submittedCopy,
                copyPolledAfterSubmit,[&]{pollCopy(false);});
            observeCopy();
            if(pendingCopy&&!pendingCopy->Composed()&&(pendingCopy->Ready()||
               (pendingCopy->gpuOrdered&&pendingCopy->Copying()&&pendingCopy->importIndex<HostSourceImportCache::Capacity&&consumers[pendingCopy->importIndex].HasPendingCopy()))){
                auto& pending=*pendingCopy;
                if(!pending.Cancelled()&&pending.presentation.Select(report.requirements,tracking,worldAllowed)){
                    const std::array<graphics::TextureSlice,2> destinations{{{scratch[0].Get(),0},{scratch[1].Get(),0}}};
                    {HostStageTimer propsTimer(diagnostic,HostStage::Props,BodyPropClockNs);
                    Require(stereoComfort.Prepare(destinations),"Prepare paired eye comfort safeguard");
                    if(ammoHud)ammoHud->Receive(pending.propsRead?&pending.props:nullptr,pending.ticket,BodyPropClockNs());
                    if(bodyProps){
                        if(pending.propsRead)bodyProps->Compose(pending.props,pending.ticket,{scratch[0].Get(),scratch[1].Get()},BodyPropClockNs,pending.depthCopied?&pending.depth:nullptr,
                            sceneDepthSeen?graphics::BodyPropMissingDepthPolicy::VehicleReticleOnly:graphics::BodyPropMissingDepthPolicy::AllowBodyProps);
                        report.bodyProps=bodyProps->Statistics();
                    }
                    }
                    std::array<ID3D11RenderTargetView*,2> outputTargets{};
                    for(unsigned eye=0;eye<2;++eye){uint32_t index=0;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                        Check(timed(HostStage::SwapchainAcquire,[&]{return api.AcquireSwapchainImage(swapchains[eye],&acquire,&index);}),"Acquire eye image");acquired[eye]=true;
                        XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=50000000;
                        const auto ready=timed(HostStage::SwapchainWait,[&]{return api.WaitSwapchainImage(swapchains[eye],&imageWait);});Check(ready,"Wait eye image");Require(ready!=XR_TIMEOUT_EXPIRED,"OpenXR eye image wait timed out");waited[eye]=true;
                        Require(index<images[eye].size(),"Invalid acquired eye index");
                        outputTargets[eye]=imageTargets[eye][index].Get();
                    }
                    Require(timed(HostStage::Present,[&]{return stereoComfort.Present(outputTargets);}),"Present paired eye comfort safeguard");
                    for(unsigned eye=0;eye<2;++eye){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                        Check(timed(HostStage::SwapchainRelease,[&]{return api.ReleaseSwapchainImage(swapchains[eye],&release);}),"Release eye image");waited[eye]=acquired[eye]=false;}
                    if(pending.gpuOrdered){
                        Require(pending.MarkComposed(),"Mark one GPU-ordered image composition");
                        pending.composedNs=BodyPropClockNs();++report.privateGpuOrderedPresent.composed;
                        observeCopy();
                    }
                    retained=pending.presentation;fresh=true;
                }else if(pending.Copying()){
                    // Cancellation/staleness may suppress presentation but must
                    // not fabricate query completion or recycle the source.
                    pending.CancelPresentation();pending.presentation.Reset();
                }else {++report.rejectedPairs;diagnostic.pairRejected=true;}
                if(pending.Ready())pendingCopy.reset();
            }
            diagnostic.copyPending=pendingCopy&&pendingCopy->Copying();
        }
        diagnostic.space=spaceGeneration;diagnostic.menu=menuActive;
        diagnostic.trackingValid=tracking.headValid;
        if(const auto* old=retained.SavedTracking()){diagnostic.retained=true;
            diagnostic.retainedGeneration=old->generation;diagnostic.retainedSpace=old->spaceGeneration;
            diagnostic.retainedPredictedNs=old->predictedNs;diagnostic.retainedAgeNs=frameTime-old->predictedNs;}
        if(const auto* rendered=retained.Select(report.requirements,tracking,worldAllowed)){
            for(unsigned eye=0;eye<2;++eye){auto& projection=projectionViews[eye];const auto& pose=rendered->eyes[eye];const auto& fov=rendered->fov[eye];
                projection.pose={{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w},{pose.position.x,pose.position.y,pose.position.z}};
                projection.fov={std::atan(fov.left),std::atan(fov.right),std::atan(fov.up),std::atan(fov.down)};
                projection.subImage.swapchain=swapchains[eye];projection.subImage.imageRect.extent={std::int32_t(report.requirements.width),std::int32_t(report.requirements.height)};
            }
            submit=true;report.maxRetainedAgeNs=(std::max)(report.maxRetainedAgeNs,std::uint64_t(tracking.predictedNs-rendered->predictedNs));
        }
        std::vector<const XrCompositionLayerBaseHeader*> layers;
        if(submit)layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer));
        if(ammoHud){if(const auto* hud=ammoHud->Layer(local,hudInput,BodyPropClockNs,
            submit&&!menuActive&&tracking.focused&&tracking.headValid)){
                layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(hud));++report.ammoCounterFrames;}
            report.ammoCounterUploads=ammoHud->Uploads();report.ammoCounterErrors=ammoHud->Errors();
            report.ammoCounterValidSamples=ammoHud->ValidSamples();report.ammoCounterInvalidSamples=ammoHud->InvalidSamples();
        }
        if(menu){const auto before=layers.size();timed(HostStage::Menu,[&]{menu->Layers(local,layers);});
            diagnostic.menuLayers=std::uint32_t(layers.size()-before);report.menuFrames=menu->Frames();report.menuErrors=menu->Errors();}
        // Normal copy completion may have deferred its sole ACK on IPC
        // contention. Retry once after composition, before EndFrame can wait;
        // cancellation already precedes feedback and Busy keeps the receipt.
        if(provider)timed(HostStage::Feedback,[&]{provider->ServiceFeedback();});
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frameTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount=std::uint32_t(layers.size());end.layers=layers.data();
        diagnostic.worldLayer=submit;diagnostic.layers=end.layerCount;diagnostic.endAttempted=true;
        const auto result=timed(HostStage::EndFrame,[&]{return api.EndFrame(session,&end);});
        diagnostic.endResult=std::int32_t(result);diagnostic.endSucceeded=XR_SUCCEEDED(result);
        frameOpen=false;Check(result,"End frame");++report.endedFrames;
        if(submit){++report.presentedFrames;if(fresh)++report.submittedPairs;else ++report.reusedFrames;}
        else ++report.blankFrames;
        // This is an existing source lease, not the discarded successor-fetch
        // idea: EndFrame gives composition/runtime useful work before one real
        // zero-wait completion observation. No second presentation or delivery.
        if(pendingCopy&&pendingCopy->Composed()){
            diagnostic.copyAfterEndPolled=true;++report.privateGpuOrderedPresent.postEndPolls;
            pollCopy(false);
            if(pendingCopy&&pendingCopy->Ready()){
                diagnostic.copyAfterEndCompleted=true;++report.privateGpuOrderedPresent.postEndCompletions;
                pendingCopy.reset();
            }
            if(provider)timed(HostStage::Feedback,[&]{return provider->ServiceFeedback();});
            diagnostic.copyPending=pendingCopy&&pendingCopy->Copying();
        }
    }
    void Run(const HostOptions& options){
        if(options.menu){menu=std::make_unique<OpenXrMenu>();menu->Initialize(instance,session,api.get,device.Get(),options.menu);}
        const auto start=std::chrono::steady_clock::now();bool requested=false;auto shutdown=start;
        while(!exit){Events();if(exit)break;const auto now=std::chrono::steady_clock::now();
            if(!requested&&((options.seconds&&now-start>=std::chrono::seconds(options.seconds))||(options.stopRequested&&options.stopRequested()))){
                CancelPendingPresentation();if(options.provider)options.provider->Suspend();
                if(!running)break;Check(api.RequestExitSession(session),"Request session exit");requested=true;shutdown=now;
            }
            if(requested&&(!running||now-shutdown>=std::chrono::seconds(2)))break;
            if(running)Frame(options.provider,requested);else Sleep(10);
        }
    }
};
}
HostReport RunOpenXrHost(const HostOptions& options)noexcept {
    HostReport report;
    try {Require(options.probeOnly||options.seconds||bool(options.stopRequested),"Continuous session requires a stop source");Host host(report);
        struct ReleaseInput {Host& host;IFrameProvider* provider;~ReleaseInput(){if(provider){interaction::InputFrame f{};f.generation=++host.inputGeneration;f.spaceGeneration=host.spaceGeneration;f.trackingBasisGeneration=host.trackingBasisGeneration;f.predictedNs=host.frameTime>0?host.frameTime:1;provider->UpdateInput(f);provider->Suspend();host.RetirePendingCopy();provider->ServiceFeedback();}}} release{host,options.provider};
        host.Initialize(options);if(!options.probeOnly)host.Run(options);report.okay=true;}
    catch(const std::exception& error){report.error=error.what();}
    catch(...){report.error="Unknown OpenXR host failure";}
    return report;
}
}
