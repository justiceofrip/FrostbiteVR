// Test-only OpenXR loader. Exercises the actual host frame loop with real D3D11
// textures, without loading SteamVR, changing runtime configuration, or an HMD.
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#define XR_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr_platform.h>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <limits>
using Microsoft::WRL::ComPtr;
namespace {
struct Chain {std::array<ComPtr<ID3D11Texture2D>,2> texture;int acquired=-1,released=-1;bool waited=false;unsigned next=0;};
ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
std::array<Chain,2> chains;
#ifdef FVR_NATIVE_CAMPAIGN_FIXTURE
constexpr bool nativeCampaign=true;
#else
constexpr bool nativeCampaign=false;
#endif
#ifdef FVR_NATIVE_SUPPORT_FIXTURE
constexpr bool nativeSupport=true;
#else
constexpr bool nativeSupport=false;
#endif
#ifdef FVR_NATIVE_RECOVERY_FIXTURE
constexpr bool nativeRecovery=true;
unsigned recoveryScenario=5;
#else
constexpr bool nativeRecovery=false;
unsigned recoveryScenario=0;
#endif
ULONGLONG nativeStarted=0;
ULONGLONG NativeElapsed(){return nativeStarted?GetTickCount64()-nativeStarted:0;}
unsigned frames=0,errors=0,layers=0,reuses=0,lastSource=0,chainCount=0,eventStep=0;
std::map<std::string,XrPath> paths;std::map<std::uintptr_t,std::string> actionNames;
std::map<std::uintptr_t,std::pair<unsigned,unsigned>> actionSpaces;
unsigned actionCount=0,suggested=0,syncs=0;bool attached=false,stageAvailable=true;
XrReferenceSpaceType referenceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
bool visibleSent=false,focusSent=false,spaceSent=false,exitSent=false,presenceInitial=false,presenceRemoved=false,presenceReturned=false,presenceEnabled=false;
constexpr XrTime baseTime=1000000000,period=11000000;
XrInstance Instance(){return reinterpret_cast<XrInstance>(std::uintptr_t{1});}
XrSession Session(){return reinterpret_cast<XrSession>(std::uintptr_t{2});}
void Verify(bool okay,const char* reason){if(!okay){++errors;std::fprintf(stderr,"FakeXR frame %u: %s\n",frames,reason);}}
unsigned Index(XrSwapchain chain){return unsigned(reinterpret_cast<std::uintptr_t>(chain)-10);}
XrPosef RecoveryPose(XrPosef pose,unsigned frame){
    if(recoveryScenario&&(nativeRecovery?NativeElapsed()>=8000:(recoveryScenario==4?frame>=40:(recoveryScenario!=3&&frame>=101)))){
        constexpr float c=.707106781f,s=.707106781f;
        const auto p=pose.position;pose.position={1.2f+c*p.x+s*p.z,p.y,-.8f-s*p.x+c*p.z};
        pose.orientation={0,.382683432f,0,.923879533f};
    }return pose;
}
XrPosef Eye(unsigned frame,unsigned eye){if(recoveryScenario)return RecoveryPose({{0,0,0,1},{eye?.032f:-.032f,1.7f,0}},frame);if(nativeCampaign)return {{0,0,0,1},{eye?.032f:-.032f,1.7f,0}};const float yaw=float(frame)*.002f;return {{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},{float(frame)*.001f+(eye?.032f:-.032f),1.7f,-float(frame)*.003f}};}
XrFovf Fov(unsigned frame,unsigned eye){if(nativeCampaign)return {-.785398163f,.785398163f,.674740942f,-.674740942f};return {eye?-.65f:-.95f,eye?.95f:.65f,.8f+float(frame)*.0001f,-.7f};}
bool Near(float a,float b){return std::abs(a-b)<.00001f;}
XrResult XRAPI_CALL EnumerateInstanceExtensionProperties(const char*,uint32_t capacity,uint32_t* count,XrExtensionProperties* out){*count=(recoveryScenario==6||nativeRecovery)?2:1;if(capacity>=*count){strcpy_s(out[0].extensionName,XR_KHR_D3D11_ENABLE_EXTENSION_NAME);out[0].extensionVersion=1;if(*count==2){strcpy_s(out[1].extensionName,XR_EXT_USER_PRESENCE_EXTENSION_NAME);out[1].extensionVersion=1;}}return XR_SUCCESS;}
XrResult XRAPI_CALL CreateInstance(const XrInstanceCreateInfo* info,XrInstance* out){presenceInitial=presenceRemoved=presenceReturned=presenceEnabled=false;for(unsigned n=0;n<info->enabledExtensionCount;++n)presenceEnabled|=std::strcmp(info->enabledExtensionNames[n],XR_EXT_USER_PRESENCE_EXTENSION_NAME)==0;nativeStarted=GetTickCount64();frames=errors=layers=reuses=lastSource=chainCount=eventStep=0;visibleSent=focusSent=spaceSent=exitSent=false;paths.clear();actionNames.clear();actionSpaces.clear();actionCount=suggested=syncs=0;attached=false;referenceType=XR_REFERENCE_SPACE_TYPE_LOCAL;*out=Instance();return XR_SUCCESS;}
XrResult XRAPI_CALL DestroyInstance(XrInstance){if(nativeCampaign)std::fprintf(stderr,"NativeCampaignTest frames=%u layers=%u errors=%u\n",frames,layers,errors);return XR_SUCCESS;}
XrResult XRAPI_CALL GetInstanceProperties(XrInstance,XrInstanceProperties* p){strcpy_s(p->runtimeName,nativeCampaign?"FVR NATIVE CAMPAIGN TEST - no headset":"FVR TEST ONLY - no headset");p->runtimeVersion=1;return XR_SUCCESS;}
XrResult XRAPI_CALL GetSystem(XrInstance,const XrSystemGetInfo*,XrSystemId* out){*out=1;return XR_SUCCESS;}
XrResult XRAPI_CALL GetSystemProperties(XrInstance,XrSystemId,XrSystemProperties* p){strcpy_s(p->systemName,"Deterministic stereo validation");if(presenceEnabled&&p->next){auto* presence=static_cast<XrSystemUserPresencePropertiesEXT*>(p->next);Verify(presence->type==XR_TYPE_SYSTEM_USER_PRESENCE_PROPERTIES_EXT,"presence property type");presence->supportsUserPresence=XR_TRUE;}return XR_SUCCESS;}
XrResult XRAPI_CALL GetD3D11GraphicsRequirementsKHR(XrInstance,XrSystemId,XrGraphicsRequirementsD3D11KHR* r){ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 desc{};
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapters1(0,&adapter))||FAILED(adapter->GetDesc1(&desc)))return XR_ERROR_RUNTIME_FAILURE;
    r->adapterLuid=desc.AdapterLuid;r->minFeatureLevel=D3D_FEATURE_LEVEL_11_0;return XR_SUCCESS;}
XrResult XRAPI_CALL EnumerateViewConfigurationViews(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t capacity,uint32_t* count,XrViewConfigurationView* out){*count=2;if(capacity>=2)for(unsigned i=0;i<2;++i){out[i].recommendedImageRectWidth=64;out[i].recommendedImageRectHeight=48;out[i].maxImageRectWidth=out[i].maxImageRectHeight=4096;out[i].recommendedSwapchainSampleCount=out[i].maxSwapchainSampleCount=1;}return XR_SUCCESS;}
XrResult XRAPI_CALL EnumerateEnvironmentBlendModes(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t capacity,uint32_t* count,XrEnvironmentBlendMode* out){*count=1;if(capacity)out[0]=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;return XR_SUCCESS;}
XrResult XRAPI_CALL CreateSession(XrInstance,const XrSessionCreateInfo* info,XrSession* out){const auto* binding=static_cast<const XrGraphicsBindingD3D11KHR*>(info->next);device=binding->device;device->GetImmediateContext(&context);*out=Session();return XR_SUCCESS;}
XrResult XRAPI_CALL DestroySession(XrSession){Verify(actionSpaces.empty()&&actionNames.empty(),"input resources leaked at session destruction");context.Reset();device.Reset();return XR_SUCCESS;}
XrResult XRAPI_CALL BeginSession(XrSession,const XrSessionBeginInfo*){return XR_SUCCESS;}
XrResult XRAPI_CALL EndSession(XrSession){return XR_SUCCESS;}
XrResult XRAPI_CALL RequestExitSession(XrSession){frames=100;if(nativeCampaign)nativeStarted=GetTickCount64()-18000;return XR_SUCCESS;}
XrResult XRAPI_CALL PollEvent(XrInstance,XrEventDataBuffer* out){
    if(presenceEnabled&&eventStep>=2&&(!presenceInitial||((nativeRecovery?NativeElapsed()>=6000:frames>=20)&&!presenceRemoved)||((nativeRecovery?NativeElapsed()>=8000:frames>=101)&&!presenceReturned))){
        XrEventDataUserPresenceChangedEXT e{XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT};e.session=Session();
        if(!presenceInitial){presenceInitial=true;e.isUserPresent=XR_TRUE;}
        else if(!presenceRemoved){presenceRemoved=true;e.isUserPresent=XR_FALSE;}
        else{presenceReturned=true;e.isUserPresent=XR_TRUE;}
        std::memcpy(out,&e,sizeof(e));return XR_SUCCESS;
    }
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    if(eventStep==0){state=XR_SESSION_STATE_READY;++eventStep;}else if(eventStep==1){state=XR_SESSION_STATE_FOCUSED;++eventStep;}
    else if(!nativeCampaign&&!recoveryScenario&&frames==30&&!visibleSent){visibleSent=true;state=XR_SESSION_STATE_VISIBLE;}
    else if(!nativeCampaign&&!recoveryScenario&&frames==33&&!focusSent){focusSent=true;state=XR_SESSION_STATE_FOCUSED;}
    else if(!nativeCampaign&&!recoveryScenario&&frames==50&&!spaceSent){spaceSent=true;XrEventDataReferenceSpaceChangePending e{XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING};e.session=Session();e.referenceSpaceType=referenceType;e.changeTime=baseTime+period*51;std::memcpy(out,&e,sizeof(e));return XR_SUCCESS;}
    else if((recoveryScenario==1&&frames==20)&&!visibleSent){visibleSent=true;state=XR_SESSION_STATE_VISIBLE;}
    else if((recoveryScenario==1&&frames==101)&&!focusSent){focusSent=true;state=XR_SESSION_STATE_FOCUSED;}
    else if((nativeCampaign?NativeElapsed()>=18000:frames>=(recoveryScenario?260u:100u))&&!exitSent){exitSent=true;state=XR_SESSION_STATE_EXITING;}
    else return XR_EVENT_UNAVAILABLE;
    XrEventDataSessionStateChanged e{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};e.session=Session();e.state=state;std::memcpy(out,&e,sizeof(e));return XR_SUCCESS;}
XrResult XRAPI_CALL EnumerateReferenceSpaces(XrSession,uint32_t capacity,uint32_t* count,XrReferenceSpaceType* out){
    *count=stageAvailable?3:2;if(capacity>=*count){out[0]=XR_REFERENCE_SPACE_TYPE_VIEW;out[1]=XR_REFERENCE_SPACE_TYPE_LOCAL;if(stageAvailable)out[2]=XR_REFERENCE_SPACE_TYPE_STAGE;}return XR_SUCCESS;
}
XrResult XRAPI_CALL CreateReferenceSpace(XrSession,const XrReferenceSpaceCreateInfo* info,XrSpace* out){if(info->referenceSpaceType!=XR_REFERENCE_SPACE_TYPE_VIEW)referenceType=info->referenceSpaceType;*out=reinterpret_cast<XrSpace>(std::uintptr_t(info->referenceSpaceType)+20);return XR_SUCCESS;}
XrResult XRAPI_CALL DestroySpace(XrSpace space){actionSpaces.erase(reinterpret_cast<std::uintptr_t>(space));return XR_SUCCESS;}
XrResult XRAPI_CALL LocateSpace(XrSpace space,XrSpace base,XrTime time,XrSpaceLocation* out){
    Verify(time==baseTime+period*frames,"input/eye prediction times differ");
    Verify(base==reinterpret_cast<XrSpace>(std::uintptr_t(referenceType)+20),"pose reference spaces differ");
    const auto key=reinterpret_cast<std::uintptr_t>(space);
    if(actionSpaces.contains(key)){
        Verify(attached&&syncs==frames,"controller located without current sync");const auto [hand,pose]=actionSpaces[key];
        out->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if(nativeCampaign||recoveryScenario||frames!=8)out->locationFlags|=XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        out->pose={{0,0,0,1},{hand?.25f:-.25f,1.25f,pose?-.6f:-.4f}};
        if(recoveryScenario)out->pose=RecoveryPose(out->pose,frames);
        if(nativeCampaign&&!nativeRecovery&&hand){const auto elapsed=NativeElapsed();if(!nativeSupport&&elapsed>=9500&&elapsed<11500){out->pose.position.x+=.2f;out->pose.position.y-=.1f;}
            if(elapsed>=13000&&elapsed<13400)out->locationFlags=0;}
        if(nativeSupport&&hand)out->pose.position={0,1.3f,pose?-.2f:0.f};
        if(nativeSupport&&!hand){out->pose.position={-.068f,1.319f,-.437f};const auto ms=NativeElapsed();
            if(ms>=7500&&ms<8900)out->pose.position.y+=.08f;
            if(ms>=9800&&ms<11200)out->pose.position.x+=.05f;
        }
    }else{out->locationFlags=!nativeCampaign&&frames==75?0:XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;out->pose=Eye(frames,0);out->pose.position.x+=.032f;
        if(recoveryScenario){out->pose=RecoveryPose({{0,0,0,1},{0,1.7f,0}},frames);if((recoveryScenario==2&&frames>=20&&frames<=100)||(recoveryScenario==3&&frames>=20&&frames<=25))out->locationFlags&=~(XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT);}}
    return XR_SUCCESS;
}
XrResult XRAPI_CALL StringToPath(XrInstance,const char* text,XrPath* out){auto [it,inserted]=paths.emplace(text,paths.size()+1);*out=it->second;return XR_SUCCESS;}
XrResult XRAPI_CALL CreateActionSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* out){*out=reinterpret_cast<XrActionSet>(std::uintptr_t{100});return XR_SUCCESS;}
XrResult XRAPI_CALL DestroyActionSet(XrActionSet){Verify(actionSpaces.empty(),"action spaces outlive action set");actionNames.clear();return XR_SUCCESS;}
XrResult XRAPI_CALL CreateAction(XrActionSet,const XrActionCreateInfo* info,XrAction* out){
    Verify(info->countSubactionPaths==2&&info->subactionPaths[0]==paths["/user/hand/left"]&&info->subactionPaths[1]==paths["/user/hand/right"],"wrong hand subaction paths");
    const auto key=std::uintptr_t(101+actionCount++);actionNames[key]=info->actionName;*out=reinterpret_cast<XrAction>(key);return XR_SUCCESS;
}
XrResult XRAPI_CALL SuggestInteractionProfileBindings(XrInstance,const XrInteractionProfileSuggestedBinding* info){
    constexpr std::array<const char*,12> expectedActions{"grip_pose","aim_pose","trigger","squeeze","stick","primary","secondary","stick_click","menu","thumb_touch","index_touch","feedback"};
    Verify(info->countSuggestedBindings>0&&actionCount==expectedActions.size(),"incomplete action set bindings");
    for(const auto* expected:expectedActions){unsigned matches=0;for(const auto& [id,name]:actionNames){(void)id;if(name==expected)++matches;}Verify(matches==1,"missing or duplicate action name");}
    for(unsigned i=0;i<info->countSuggestedBindings;++i)Verify(actionNames.contains(reinterpret_cast<std::uintptr_t>(info->suggestedBindings[i].action)),"unknown suggested action");
    ++suggested;return XR_SUCCESS;
}
XrResult XRAPI_CALL AttachSessionActionSets(XrSession,const XrSessionActionSetsAttachInfo* info){Verify(!attached&&info->countActionSets==1&&suggested==3,"attach actions once after suggestions");attached=true;return XR_SUCCESS;}
XrResult XRAPI_CALL ApplyHapticFeedback(XrSession,const XrHapticActionInfo* info,const XrHapticBaseHeader* base){
    Verify(attached&&actionNames[reinterpret_cast<std::uintptr_t>(info->action)]=="feedback","wrong haptic action");
    Verify(base&&base->type==XR_TYPE_HAPTIC_VIBRATION,"wrong haptic type");
    if(base&&base->type==XR_TYPE_HAPTIC_VIBRATION){const auto& pulse=*reinterpret_cast<const XrHapticVibration*>(base);
        Verify(pulse.duration>0&&pulse.duration<=45000000&&pulse.amplitude>0&&pulse.amplitude<=1,"unbounded haptic pulse");}
    return XR_SUCCESS;
}
XrResult XRAPI_CALL StopHapticFeedback(XrSession,const XrHapticActionInfo* info){
    Verify(actionNames[reinterpret_cast<std::uintptr_t>(info->action)]=="feedback","wrong stop haptic action");return XR_SUCCESS;
}
XrResult XRAPI_CALL CreateActionSpace(XrSession,const XrActionSpaceCreateInfo* info,XrSpace* out){
    Verify(attached&&info->poseInActionSpace.orientation.w==1,"bad controller space initialization");
    const auto key=std::uintptr_t(200+actionSpaces.size());const auto name=actionNames[reinterpret_cast<std::uintptr_t>(info->action)];
    Verify(name=="grip_pose"||name=="aim_pose","space from non-pose action");
    actionSpaces[key]={info->subactionPath==paths["/user/hand/right"]?1u:0u,name=="aim_pose"?1u:0u};*out=reinterpret_cast<XrSpace>(key);return XR_SUCCESS;
}
XrResult XRAPI_CALL SyncActions(XrSession,const XrActionsSyncInfo* info){Verify(attached&&info->countActiveActionSets==1,"sync without attached actions");syncs=frames;return !nativeCampaign&&!recoveryScenario&&frames>=31&&frames<=33?XR_SESSION_NOT_FOCUSED:XR_SUCCESS;}
XrResult XRAPI_CALL GetActionStatePose(XrSession,const XrActionStateGetInfo*,XrActionStatePose* out){out->isActive=nativeCampaign||recoveryScenario||frames!=9;return XR_SUCCESS;}
XrResult XRAPI_CALL GetActionStateFloat(XrSession,const XrActionStateGetInfo* info,XrActionStateFloat* out){if(recoveryScenario){out->isActive=XR_TRUE;out->currentState=0;return XR_SUCCESS;}if(nativeCampaign){const auto ms=NativeElapsed();out->isActive=XR_TRUE;out->currentState=info->subactionPath==paths["/user/hand/right"]&&actionNames[reinterpret_cast<std::uintptr_t>(info->action)]=="trigger"&&((ms>=8000&&ms<8800)||(ms>=10000&&ms<10800)||(ms>=13200&&ms<13280))?1.f:0.f;
    if(nativeSupport&&info->subactionPath==paths["/user/hand/left"]&&actionNames[reinterpret_cast<std::uintptr_t>(info->action)]=="squeeze")
        out->currentState=((ms>=7000&&ms<8900)||(ms>=9400&&ms<11200)||(ms>=12000&&ms<14000)||(ms>=14500&&ms<15500))?1.f:0.f;
    return XR_SUCCESS;}
    out->isActive=frames!=12;out->currentState=actionNames[reinterpret_cast<std::uintptr_t>(info->action)]=="trigger"?.8f:.4f;return XR_SUCCESS;}
XrResult XRAPI_CALL GetActionStateVector2f(XrSession,const XrActionStateGetInfo* info,XrActionStateVector2f* out){out->isActive=XR_TRUE;if(recoveryScenario){out->currentState={};return XR_SUCCESS;}if(nativeCampaign){const auto ms=NativeElapsed();out->currentState={0,info->subactionPath==paths["/user/hand/right"]&&ms>=3500&&ms<3700?-1.f:0.f};return XR_SUCCESS;}out->currentState={frames==13?std::numeric_limits<float>::quiet_NaN():.3f,-.2f};return XR_SUCCESS;}
XrResult XRAPI_CALL GetActionStateBoolean(XrSession,const XrActionStateGetInfo* info,XrActionStateBoolean* out){out->isActive=XR_TRUE;if(recoveryScenario){out->currentState=actionNames[reinterpret_cast<std::uintptr_t>(info->action)]=="stick_click"&&(nativeRecovery?(NativeElapsed()>=14000&&NativeElapsed()<16300):(frames>=140&&frames<=239));return XR_SUCCESS;}out->currentState=actionNames[reinterpret_cast<std::uintptr_t>(info->action)]=="secondary"&&(!nativeCampaign||(info->subactionPath==paths["/user/hand/right"]&&NativeElapsed()>=4300&&NativeElapsed()<4450));return XR_SUCCESS;}

XrResult XRAPI_CALL LocateViews(XrSession,const XrViewLocateInfo*,XrViewState* state,uint32_t,uint32_t* count,XrView* out){*count=2;state->viewStateFlags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;for(unsigned eye=0;eye<2;++eye){out[eye].pose=Eye(frames,eye);out[eye].fov=Fov(frames,eye);}return XR_SUCCESS;}
XrResult XRAPI_CALL EnumerateSwapchainFormats(XrSession,uint32_t capacity,uint32_t* count,int64_t* out){*count=1;if(capacity)out[0]=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;return XR_SUCCESS;}
XrResult XRAPI_CALL CreateSwapchain(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){if(chainCount>=2)return XR_ERROR_LIMIT_REACHED;auto& chain=chains[chainCount];chain={};
    D3D11_TEXTURE2D_DESC d{};d.Width=info->width;d.Height=info->height;d.Format=DXGI_FORMAT(info->format);d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    for(auto& texture:chain.texture)if(FAILED(device->CreateTexture2D(&d,nullptr,&texture)))return XR_ERROR_RUNTIME_FAILURE;
    *out=reinterpret_cast<XrSwapchain>(std::uintptr_t(10+chainCount++));return XR_SUCCESS;}
XrResult XRAPI_CALL DestroySwapchain(XrSwapchain chain){chains[Index(chain)]={};return XR_SUCCESS;}
XrResult XRAPI_CALL EnumerateSwapchainImages(XrSwapchain chain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out){*count=2;if(capacity>=2){auto* images=reinterpret_cast<XrSwapchainImageD3D11KHR*>(out);for(unsigned i=0;i<2;++i)images[i].texture=chains[Index(chain)].texture[i].Get();}return XR_SUCCESS;}
XrResult XRAPI_CALL AcquireSwapchainImage(XrSwapchain handle,const XrSwapchainImageAcquireInfo*,uint32_t* out){auto& c=chains[Index(handle)];Verify(c.acquired<0,"double acquire");c.acquired=int(c.next++%2);*out=unsigned(c.acquired);return XR_SUCCESS;}
XrResult XRAPI_CALL WaitSwapchainImage(XrSwapchain handle,const XrSwapchainImageWaitInfo*){auto& c=chains[Index(handle)];Verify(c.acquired>=0,"wait without acquire");c.waited=true;return XR_SUCCESS;}
XrResult XRAPI_CALL ReleaseSwapchainImage(XrSwapchain handle,const XrSwapchainImageReleaseInfo*){auto& c=chains[Index(handle)];Verify(c.waited,"release before wait");c.released=c.acquired;c.acquired=-1;c.waited=false;return XR_SUCCESS;}
XrResult XRAPI_CALL WaitFrame(XrSession,const XrFrameWaitInfo*,XrFrameState* out){if(nativeCampaign||recoveryScenario)Sleep(11);if(recoveryScenario==4&&frames==39)Sleep(1600);++frames;out->predictedDisplayTime=baseTime+period*frames;out->predictedDisplayPeriod=period;out->shouldRender=recoveryScenario?!(recoveryScenario==1&&frames>=20&&frames<=100):(nativeCampaign||frames!=70);return XR_SUCCESS;}
XrResult XRAPI_CALL BeginFrame(XrSession,const XrFrameBeginInfo*){return XR_SUCCESS;}
XrResult XRAPI_CALL EndFrame(XrSession,const XrFrameEndInfo* end){
    Verify(end->displayTime==baseTime+period*frames,"end time must be current display time");
    if(!end->layerCount)return XR_SUCCESS;++layers;
    if(nativeCampaign){
        Verify(end->layerCount==1,"native layer count");if(end->layerCount!=1)return XR_ERROR_VALIDATION_FAILURE;
        const auto* layer=reinterpret_cast<const XrCompositionLayerProjection*>(end->layers[0]);Verify(layer->viewCount==2,"native stereo eye count");
        if(layer->viewCount!=2)return XR_ERROR_VALIDATION_FAILURE;
        for(unsigned eye=0;eye<2;++eye){const auto& view=layer->views[eye];const auto index=Index(view.subImage.swapchain);
            Verify(index<2,"native swapchain identity");if(index>=2)return XR_ERROR_VALIDATION_FAILURE;
            const auto& c=chains[index];Verify(c.released>=0&&c.acquired<0&&!c.waited,"native swapchain ownership");
            Verify(view.subImage.imageRect.extent.width==1920&&view.subImage.imageRect.extent.height==1080,"native eye dimensions");
            const auto pose=Eye(frames,eye);Verify(Near(view.pose.position.x,pose.position.x)&&Near(view.pose.position.y,pose.position.y)&&Near(view.pose.position.z,pose.position.z),"native source pose");
        }return errors?XR_ERROR_VALIDATION_FAILURE:XR_SUCCESS;
    }
    Verify(frames!=70&&frames!=75&&!(frames>=31&&frames<=33),"submitted during visibility/tracking suspension");
    Verify(end->layerCount==1,"unexpected layer count");const auto* layer=reinterpret_cast<const XrCompositionLayerProjection*>(end->layers[0]);Verify(layer->viewCount==2,"not two eyes");
    unsigned pairFrame=0;
    for(unsigned eye=0;eye<2;++eye){const auto& view=layer->views[eye];const auto& c=chains[Index(view.subImage.swapchain)];Verify(c.released>=0,"no released image");if(c.released<0)continue;
        D3D11_TEXTURE2D_DESC d{};c.texture[c.released]->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&staging))){Verify(false,"readback allocation");continue;}context->CopyResource(staging.Get(),c.texture[c.released].Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))){Verify(false,"readback map");continue;}
        const auto* pixel=static_cast<const unsigned char*>(mapped.pData);const unsigned rendered=pixel[0];Verify(pixel[1]==eye+1&&pixel[2]==0x5a&&pixel[3]==255,"eye identity/pixels corrupted");
        for(unsigned row=0;row<d.Height;++row)for(unsigned col=0;col<d.Width;++col){const auto* p=static_cast<const unsigned char*>(mapped.pData)+std::size_t(row)*mapped.RowPitch+col*4;Verify(p[0]==rendered&&p[1]==eye+1&&p[2]==0x5a&&p[3]==255,"partial eye image");}
        context->Unmap(staging.Get(),0);if(eye==0)pairFrame=rendered;else Verify(pairFrame==rendered,"mixed source frames");
        const auto pose=Eye(rendered,eye);const auto fov=Fov(rendered,eye);
        Verify(Near(view.pose.position.x,pose.position.x)&&Near(view.pose.position.y,pose.position.y)&&Near(view.pose.position.z,pose.position.z)&&Near(view.pose.orientation.y,pose.orientation.y)&&Near(view.pose.orientation.w,pose.orientation.w),"pixels relabeled with a different pose");
        Verify(Near(view.fov.angleLeft,fov.angleLeft)&&Near(view.fov.angleRight,fov.angleRight)&&Near(view.fov.angleUp,fov.angleUp)&&Near(view.fov.angleDown,fov.angleDown),"pixels relabeled with a different FOV");
        Verify(rendered<=frames&&(frames-rendered)*period<=250000000,"stale or future pair presented");
    }
    if(pairFrame==lastSource)++reuses;lastSource=pairFrame;return XR_SUCCESS;
}
}
extern "C" __declspec(dllexport) void FakeXrStageAvailable(bool available){stageAvailable=available;}
extern "C" __declspec(dllexport) unsigned FakeXrErrors(){return errors;}
extern "C" __declspec(dllexport) unsigned FakeXrReuses(){return reuses;}
extern "C" __declspec(dllexport) unsigned FakeXrLayers(){return layers;}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance,const char* name,PFN_xrVoidFunction* out){
#define API(n) if(std::strcmp(name,"xr" #n)==0){*out=reinterpret_cast<PFN_xrVoidFunction>(n);return XR_SUCCESS;}
    API(EnumerateInstanceExtensionProperties) API(CreateInstance) API(DestroyInstance) API(GetInstanceProperties) API(GetSystem) API(GetSystemProperties)
    API(GetD3D11GraphicsRequirementsKHR) API(EnumerateViewConfigurationViews) API(EnumerateEnvironmentBlendModes)
    API(CreateSession) API(DestroySession) API(PollEvent) API(BeginSession) API(EndSession) API(RequestExitSession)
    API(EnumerateReferenceSpaces) API(StringToPath) API(CreateActionSet) API(DestroyActionSet) API(CreateAction)
    API(SuggestInteractionProfileBindings) API(AttachSessionActionSets) API(CreateActionSpace) API(SyncActions)
    API(GetActionStatePose) API(GetActionStateFloat) API(GetActionStateVector2f) API(GetActionStateBoolean)
    API(ApplyHapticFeedback) API(StopHapticFeedback)
    API(CreateReferenceSpace) API(DestroySpace) API(LocateSpace) API(LocateViews)
    API(EnumerateSwapchainFormats) API(CreateSwapchain) API(DestroySwapchain) API(EnumerateSwapchainImages)
    API(AcquireSwapchainImage) API(WaitSwapchainImage) API(ReleaseSwapchainImage) API(WaitFrame) API(BeginFrame) API(EndFrame)
#undef API
    *out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
}

extern "C" __declspec(dllexport) void FakeXrRecoveryScenario(unsigned scenario){recoveryScenario=scenario;}
