#include "Bc2TrackedViews.h"
#include "fvr/diagnostics/PrivateCpuAttribution.h"
#include "fvr/diagnostics/PrivateGpuAttribution.h"
#include "Bc2WeatherObservation.h"
#include "Bc2WeatherTranslation.h"
#include "Bc2StereoColorScale.h"
#include "Bc2NativeFadeDiagnostic.h"
#include "Bc2SceneMilestoneWriter.h"
#include "Bc2GraphicsThreadObservation.h"
#include "Bc2StereoProgressEvidence.h"
#include "Bc2StereoFrameAdmission.h"
#include "Bc2StereoRecovery.h"
#include "fvr/runtime/RenderOwnerRecovery.h"
#include "Bc2Gameplay.h"
#include "Bc2SelectedKnifeNative.h"
#include "Bc2ProbeFailure.h"
#include "Bc2MechanismAudio.h"
#include "Bc2ShellReloadAudio.h"
#include "Bc2PumpCalibration225.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2NativeCycleRuntime.h"
#include "Bc2BodyInventorySession.h"
#include "Bc2BodyAmmoRenderer.h"
#include "Bc2BodyAmmoHost.h"
#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2WeaponVisibilityProbe.h"
#include "Bc2BodyHolsterProbe.h"
#include "Bc2MenuRuntime.h"
#include "Bc2MenuState.h"
#include "Bc2ReloadDrawCapture.h"
#include "fvr/graphics/D3D11SceneDrawWitness.h"
#include "Bc2ReloadProducerBinding.h"
#include "Bc2RigWorkerRuntime.h"
#include "Bc2OpticFilterRuntime.h"
#include <memory>
#include "Bc2RigPublication.h"
#include "Bc2BodyPropPair.h"
#include "fvr/math/ProjectionOverride.h"
#include "fvr/runtime/ExactWriteBatch.h"
#include "fvr/runtime/FrameObservationFailures.h"
#include "fvr/graphics/D3D11FrameBridge.h"
#include "fvr/graphics/FrameBridgeDiagnosticsJson.h"
#include "fvr/graphics/DepthEyeDiagnostics.h"
#include "fvr/graphics/DeferredDepthAdmission.h"
#include "fvr/graphics/FrameBridgePresentService.h"
#include "fvr/graphics/PrivatePresentationExperiment.h"
#include "fvr/graphics/BoundedFrameProgress.h"
#include "fvr/graphics/D3D11PresentationPacing.h"
#include "fvr/graphics/DesktopImageEvidence.h"
#include "fvr/graphics/DiagnosticImageLifetime.h"
#include "fvr/graphics/RenderBootstrap.h"
#include "fvr/graphics/DesktopWindowMode.h"
#include "NativeWorkPool.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include "fvr/graphics/D3D11CallProvenance.h"
#include <wrl/client.h>
#include "Bc2Profile.h"
#include "Bc2Camera.h"
#include "fvr/interaction/TrackingMath.h"
#include "NativeProbeConfig.h"
#include "Bc2MagazineReloadSession.h"
#include "Bc2ResourceMagazineSession.h"
#include "fvr/platform/windows/ProcessLifetime.h"
#include "NativeViewAbi.h"
#include "fvr/math/StereoMath.h"
#include <cmath>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace {
using Microsoft::WRL::ComPtr;
namespace sceneTrace=fvr::bc2::sceneMilestones;
using fvr::runtime::SceneMilestoneKind;
using fvr::runtime::SceneMilestoneStage;
using WorldFn=void(__thiscall*)(void*,void*);
using PrepareFn=void*(__thiscall*)(void*,void*,void*);
using DrawFn=void(__thiscall*)(void*,void*,void*,void*);
using VisibilityFn=void(__thiscall*)(void*,void*,void*,void*,void*,void*,void*);
using UpdateFn=void(__thiscall*)(void*,void*,unsigned,float,void*,void*,void*);
UpdateFn updateOriginal=nullptr;std::atomic<unsigned> updateCalls=0;
VisibilityFn visibilityOriginal=nullptr;
WorldFn worldOriginal=nullptr;PrepareFn prepareOriginal=nullptr;DrawFn drawOriginal=nullptr;
std::atomic<bool> sampling=false;
bool startedFromMainMenu=false;
std::atomic<unsigned> allocated=0,worldCalls=0,prepareCalls=0,drawCalls=0;
std::atomic<unsigned> visibilityAllocated=0,visibilityCalls=0;
LONG started=0;unsigned cameraPulseMode=0;bool lifecycleMode=false,stereoMode=false;
fvr::bc2::ViewLifecycleCandidates lifecycle{};fvr::bc2::ViewCallbackCandidates viewCallbacks{};
struct LifecycleResult {unsigned borrowedRedirects=0,borrowedRestores=0,allBefore=0,allAfter=0,requestRefsBefore=0,requestRefsAfter=0;bool allRestored=false,callbacksRestored=false;bool claimed=false,created=false,registered=false,inactive=false,restored=false,ownerUnchanged=false;unsigned view=0,refsBefore=0,refsOwned=0,refsReleased=0,countBefore=0,countDuring=0,countAfter=0,thread=0;std::atomic<bool> complete=false;} lifecycleResult;
using CacheFn=void(__thiscall*)(void*);
CacheFn updateView=nullptr,updateProjection=nullptr,updateFrustum=nullptr,updateScale=nullptr;
struct CameraVariant {fvr::math::Matrix4 projection{};std::uint64_t frustumHash=0;};
struct MappedCameraResult {bool complete=false;float viewError=-1,projectionError=-1,viewProjectionRelativeError=-1;};
struct CullCameraResult {bool passed=false;double maxDistance=-1,maxRoundoffAllowance=0,maxExcess=0;float normalError=-1;};
struct CameraExperiment {
    std::atomic<bool> claimed=false,complete=false;
    bool liveUnchanged=false;float baselineViewError=-1,baselineProjectionError=-1;
    bool sourcePositionValid=false;std::array<float,3> sourcePosition{};
    std::array<CameraVariant,6> variants{};
    std::array<MappedCameraResult,8> mapped{};
    std::array<CullCameraResult,8> culling{};
} cameraExperiment;
std::uintptr_t imageBase=0;fvr::bc2::DiscoveryProfile profile{};fvr::bc2::ViewLayoutCandidates layout{};
struct Target {unsigned width=0,height=0,format=0,samples=0;};
struct ColorCapture {
    enum class Step {Idle,Pending,Finished,Failed};
    Step step=Step::Idle;ComPtr<ID3D11Texture2D> staging;ComPtr<ID3D11DeviceContext> context;
    std::vector<unsigned char> rgba;Target target{};unsigned frame=0,attempts=0;long error=0;
    bool serialized=false; // Writer-only receipt after successful file close.
    std::atomic<bool> complete=false;
};
std::array<ColorCapture,3> colorCaptures;
std::array<ColorCapture,2> weatherColorCaptures;
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
std::array<ColorCapture,2> sceneDepthCaptures,sceneDepthColors;
std::array<fvr::graphics::BodyPropEye,2> sceneDepthProps{};
unsigned sceneDepthFrame=0,sceneDepthMask=0;
unsigned sceneDepthFirstValidFrame=0;
thread_local int sceneDepthDrawingEye=-1;
struct SceneDepthCandidate {
    ComPtr<ID3D11Texture2D> texture;ColorCapture capture;
    std::uint64_t resource=0;D3D11_VIEWPORT viewport{};
    unsigned draws=0,depthFunction=0,depthWrite=0;
};
struct SceneDepthCalls {
    unsigned indexed=0,nonIndexed=0,indexedInstanced=0,instanced=0;
    unsigned noDepth=0,unsupported=0,wrongSize=0,wrongViewport=0;
    std::array<unsigned,128> formats{};
};
std::array<SceneDepthCalls,2> sceneDepthCalls{};
std::array<std::array<SceneDepthCandidate,8>,2> sceneDepthCandidates;
std::array<unsigned,2> sceneDepthCandidateCounts{},sceneDepthCandidateOverflow{};
std::array<std::array<std::array<std::byte,0x460>,4>,2> sceneDepthViewCopies{};
std::array<unsigned,2> sceneDepthViewMask{};
#endif
fvr::graphics::DiagnosticImageLifetime diagnosticImageLifetime;
struct WeaponVisibilityEyeRow {
    unsigned frame=0,eye=0,phase=0;std::uint64_t request=0,input=0,draw=0,receiptInput=0;
    std::int64_t nowNs=0,receiptObservedNs=0,receiptDeadlineNs=0;bool hidden=false,paired=false;
};
std::array<WeaponVisibilityEyeRow,512> weaponVisibilityEyes{};unsigned weaponVisibilityEyeCount=0;
bool weaponVisibilityProbeMode=false,bodyHolsterProbeMode=false;
struct BodyHolsterEyeRow {
    unsigned frame=0,eye=0,phase=0,bodyPhase=0;std::uint64_t request=0,input=0,tick=0,right=0,left=0,draw=0,pairedFree=0;
    std::int64_t nowNs=0,receiptDeadlineNs=0;bool hidden=false,paired=false,free=false,suppressed=false;
};
std::array<BodyHolsterEyeRow,512> bodyHolsterEyes{};unsigned bodyHolsterEyeCount=0;
std::array<void*,20> hookEntries{};std::atomic<bool> deferredCleanup=false;
struct WorkPoolExperiment {
    std::atomic<unsigned> stage=0; // 0 idle, 1 leased, 2 restored, 3 rejected
    unsigned owner=0,vtable=0,global=0,reset=0,thread=0,frame=0,peak=0;
    fvr::bc2::WorkPoolLease lease;alignas(16) std::array<std::byte,128*40> storage{};
} workPool;
bool poolOnly=false;
using ResetPoolFn=void(__thiscall*)(void*);ResetPoolFn resetPoolOriginal=nullptr;
void __fastcall ResetPoolHook(void* self,void*){
    resetPoolOriginal(self);
    if(workPool.stage.load(std::memory_order_acquire)!=1||reinterpret_cast<unsigned>(self)!=workPool.owner||GetCurrentThreadId()!=workPool.thread)return;
    auto& header=*reinterpret_cast<fvr::bc2::WorkPoolHeader*>(workPool.owner+0x850);
    if(workPool.lease.RestoreEmpty(header)){workPool.stage.store(2,std::memory_order_release);if(deferredCleanup.load(std::memory_order_acquire)&&hookEntries[5])MH_DisableHook(hookEntries[5]);}
}

struct CameraPulse {
    bool claimed=false,applied=false,restored=false,drawLeftCopiesUnchanged=false,ownerUnchanged=false;
    unsigned frame=0;std::atomic<bool> complete=false;
} cameraPulse;
struct ChildObservation {unsigned entry=0,view=0,vtable=0,owner=0,active=0,shadow=0;std::uint64_t primary=0,secondary=0;};
struct Record {
    std::atomic<bool> complete=false;
    unsigned thread=0,frame=0,worldFrame=0,requestBefore=0,requestAfter=0,viewCount=0,prepared=0,drawn=0;
    std::int64_t beginQpc=0,endQpc=0;
    unsigned arenaBefore=0,arenaAfter=0,viewItemsBefore=0,viewItemsAfter=0;
    std::uintptr_t world=0,request=0,view=0,preparedData=0;
    std::uint64_t cameraBefore=0,cameraAfter=0,dataBefore=0,dataAfter=0;
    Target targetBefore{},targetAfter{};
    unsigned childrenCount=0;std::array<ChildObservation,8> children{};
};
std::array<Record,256> records;
struct VisibilityRecord {
    std::atomic<bool> complete=false;
    unsigned thread=0,frame=0,worldFrame=0,requestBefore=0,requestAfter=0,job1=0,job2=0;
    bool ownerValid=false;
    std::uintptr_t world=0,request=0,view=0;std::int64_t beginQpc=0,endQpc=0;
    std::array<std::uint64_t,4> cameraBefore{},cameraAfter{};
};
std::array<VisibilityRecord,256> visibilityRecords;
thread_local Record* current=nullptr;
bool Read(std::uintptr_t address,void* out,std::size_t bytes)noexcept {
    if(address<0x10000||bytes>0x2000||address>UINT32_MAX-bytes)return false;SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,bytes,&got)&&got==bytes;
}
std::int64_t Qpc()noexcept{LARGE_INTEGER time{};QueryPerformanceCounter(&time);return time.QuadPart;}
unsigned U32(std::uintptr_t address)noexcept{unsigned value=0;Read(address,&value,4);return value;}
std::uint64_t Hash(std::uintptr_t address,std::size_t size)noexcept{
    std::array<unsigned char,0x2000> bytes{};if(size>bytes.size()||!Read(address,bytes.data(),size))return 0;
    std::uint64_t h=14695981039346656037ULL;for(std::size_t i=0;i<size;++i){h^=bytes[i];h*=1099511628211ULL;}return h;
}
unsigned ItemCount(std::uintptr_t view)noexcept{const auto begin=U32(view+0x1670),end=U32(view+0x1674);return end>=begin&&end-begin<0x100000?(end-begin)/4:UINT32_MAX;}
unsigned Arena(std::uintptr_t world)noexcept{return U32(std::uintptr_t(U32(world+0x78))+0xc);}
Target BoundTarget()noexcept {
    Target result{};const auto renderer=U32(imageBase+profile.rendererGlobal);
    if(U32(renderer)!=imageBase+profile.rendererVtable)return result;
    const auto deviceAddress=U32(std::uintptr_t(renderer)+0x7c);const auto vtable=U32(deviceAddress);HMODULE owner=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(vtable),&owner)||owner!=GetModuleHandleW(L"d3d11.dll"))return result;
    auto* device=reinterpret_cast<ID3D11Device*>(deviceAddress);ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
    ComPtr<ID3D11RenderTargetView> rtv;context->OMGetRenderTargets(1,&rtv,nullptr);if(!rtv)return result;
    ComPtr<ID3D11Resource> resource;rtv->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return result;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);return {desc.Width,desc.Height,unsigned(desc.Format),desc.SampleDesc.Count};
}
// A single read-only GPU copy after the original world draw. Polls Map with
// DO_NOT_WAIT on the same graphics thread; never waits for GPU completion or
// changes any bindings. Only the completed owned pixels cross to the writer.
void CaptureWorldColor(ColorCapture& c,unsigned frame,ID3D11Texture2D* explicitSource=nullptr,bool rawDepth=false) noexcept {
    if(c.step==ColorCapture::Step::Finished||c.step==ColorCapture::Step::Failed)return;
    const auto fail=[&](HRESULT hr){c.error=hr;c.step=ColorCapture::Step::Failed;c.complete.store(true,std::memory_order_release);};
    try {
        if(c.step==ColorCapture::Step::Idle){
            const auto renderer=U32(imageBase+profile.rendererGlobal);if(U32(renderer)!=imageBase+profile.rendererVtable)return;
            const auto deviceAddress=U32(std::uintptr_t(renderer)+0x7c),vtable=U32(deviceAddress);HMODULE owner=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(vtable),&owner)||owner!=GetModuleHandleW(L"d3d11.dll"))return;
            auto* device=reinterpret_cast<ID3D11Device*>(deviceAddress);device->GetImmediateContext(&c.context);
            ComPtr<ID3D11Texture2D> texture;
            if(explicitSource){
                texture=explicitSource;ComPtr<ID3D11Device> sourceDevice;texture->GetDevice(&sourceDevice);
                if(sourceDevice.Get()!=device){fail(E_INVALIDARG);return;}
            }else{
                ComPtr<ID3D11RenderTargetView> rtv;c.context->OMGetRenderTargets(1,&rtv,nullptr);if(!rtv){fail(E_FAIL);return;}
                ComPtr<ID3D11Resource> resource;rtv->GetResource(&resource);if(FAILED(resource.As(&texture))){fail(E_NOINTERFACE);return;}
            }
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            if(!desc.Width||!desc.Height||desc.Width>4096||desc.Height>4096||desc.ArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||
               (rawDepth?(desc.Format!=DXGI_FORMAT_R24G8_TYPELESS&&desc.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT&&
                          desc.Format!=DXGI_FORMAT_R32_TYPELESS&&desc.Format!=DXGI_FORMAT_D32_FLOAT):
               (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))){fail(E_INVALIDARG);return;}
            c.target={desc.Width,desc.Height,unsigned(desc.Format),1};c.frame=frame;
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
            HRESULT hr=device->CreateTexture2D(&desc,nullptr,&c.staging);if(FAILED(hr)){fail(hr);return;}
            c.rgba.resize(std::size_t(desc.Width)*desc.Height*4);c.context->CopyResource(c.staging.Get(),texture.Get());c.step=ColorCapture::Step::Pending;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};const auto hr=c.context->Map(c.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(hr==DXGI_ERROR_WAS_STILL_DRAWING){if(++c.attempts>120)fail(hr);return;}
        if(FAILED(hr)){fail(hr);return;}
        const std::size_t stride=std::size_t(c.target.width)*4;
        if(mapped.RowPitch<stride){c.context->Unmap(c.staging.Get(),0);fail(E_FAIL);return;}
        for(unsigned y=0;y<c.target.height;++y)std::memcpy(c.rgba.data()+y*stride,static_cast<unsigned char*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,stride);
        c.context->Unmap(c.staging.Get(),0);
        if(c.target.format==87||c.target.format==91)for(std::size_t pixel=0;pixel<c.rgba.size();pixel+=4)std::swap(c.rgba[pixel],c.rgba[pixel+2]);
        c.staging.Reset();c.context.Reset();c.step=ColorCapture::Step::Finished;c.complete.store(true,std::memory_order_release);
    }catch(...){fail(E_OUTOFMEMORY);}
}
void ObserveChildren(Record& record)noexcept {
    const auto begin=U32(record.view+0x1680),end=U32(record.view+0x1684);if(end<begin||end-begin>8*8)return;
    record.childrenCount=(end-begin)/8;
    for(unsigned i=0;i<record.childrenCount;++i){auto& child=record.children[i];child.entry=U32(std::uintptr_t(begin)+i*8);child.view=U32(std::uintptr_t(child.entry)+0xc);child.vtable=U32(child.view);
        child.owner=U32(std::uintptr_t(child.view)+layout.ownerRequestOffset);unsigned char active=0,shadow=0;Read(std::uintptr_t(child.entry)+0x14,&active,1);Read(std::uintptr_t(child.entry)+0x370,&shadow,1);child.active=active;child.shadow=shadow;
        // Inherited native view types share these setter/getter thunks, but
        // their owning request and role must still be verified before writes.
        if(U32(std::uintptr_t(child.vtable)+20)==imageBase+layout.setPrimary&&U32(std::uintptr_t(child.vtable)+28)==imageBase+layout.setSecondary){
            child.primary=Hash(std::uintptr_t(child.view)+layout.primaryOffset,0x460);child.secondary=Hash(std::uintptr_t(child.view)+layout.secondaryOffset,0x460);
        }
    }
}
void SampleCameraCopies(std::uintptr_t address)noexcept {
    bool expected=false;if(!cameraExperiment.claimed.compare_exchange_strong(expected,true))return;
    using Copy=fvr::bc2::RenderViewCopy;
    Copy original{};if(!Read(address,original.bytes.data(),original.bytes.size()))return;
    const auto before=Hash(address,original.bytes.size());
    for(unsigned variant=0;variant<cameraExperiment.variants.size();++variant){
        Copy copy=original;const auto put=[&](unsigned offset,float value){std::memcpy(copy.bytes.data()+offset,&value,4);};
        if(variant){
            const unsigned perspective=0;std::memcpy(copy.bytes.data()+4,&perspective,4);copy.bytes[8]=std::byte{1};
            put(0xc,1);put(0x10,1.5707963267948966f);put(0x24,1);put(0x3c,0);put(0x40,0);put(0x44,1);put(0x48,1);
            if(variant==2)put(0x3c,.1f);if(variant==3)put(0x40,.1f);if(variant==4)put(0x44,.8f);if(variant==5)put(0x48,.8f);
        }
        // These verified math methods touch only this owned RenderView copy.
        // No live view, frame arena or game callback is passed to them.
        fvr::bc2::PrecomputeCameraCaches(copy,updateView,updateProjection,updateFrustum,updateScale);
        auto& result=cameraExperiment.variants[variant];std::memcpy(&result.projection,copy.bytes.data()+0x2e0,64);
        result.frustumHash=Hash(reinterpret_cast<std::uintptr_t>(copy.bytes.data()+0x90),0x180);
        if(!variant){
            const auto difference=[&](unsigned offset){float error=0;for(unsigned i=0;i<16;++i){float a=0,b=0;std::memcpy(&a,original.bytes.data()+offset+i*4,4);std::memcpy(&b,copy.bytes.data()+offset+i*4,4);if(!std::isfinite(a)||!std::isfinite(b))return -1.f;error=(std::max)(error,std::abs(a-b));}return error;};
            cameraExperiment.baselineViewError=difference(0x220);cameraExperiment.baselineProjectionError=difference(0x2e0);
        }
    }
    fvr::engine::FrostbiteCameraInput input{};std::memcpy(&input.transform,original.bytes.data()+0x50,64);
    std::memcpy(&input.nearPlane,original.bytes.data()+0x1c,4);std::memcpy(&input.farPlane,original.bytes.data()+0x20,4);input.worldUnitsPerMeter=1;
    const auto camera=fvr::engine::CanonicalCamera(input);
    if(camera){cameraExperiment.sourcePositionValid=true;
        for(unsigned axis=0;axis<3;++axis)cameraExperiment.sourcePosition[axis]=camera->camera.values[3][axis];}
    if(camera)for(unsigned i=0;i<cameraExperiment.mapped.size();++i){
        fvr::math::Pose reference{},pose{};pose.position={i%2?-.032f:.032f,.15f,-.2f};
        const float angle=float(i)*.07f;pose.orientation={0,std::sin(angle),0,std::cos(angle)};
        const auto world=fvr::math::ComposeRuntimeHeadWithLhCamera(camera->camera,reference,pose,1);if(!world)continue;
        const fvr::math::FovTangents fov{i%2?-1.3f:-.7f,i%2?.7f:1.3f,i%3?.85f:1.1f,i%3?-.65f:-1.1f};
        const float nearPlane=i<4?.04f:.1f,farPlane=i<4?100.f:2000.f;
        auto copy=fvr::bc2::BuildRenderViewCopy(original,*world,fov,nearPlane,farPlane);if(!copy)continue;
        const auto projection=fvr::math::MakeLhProjectionFromFovTangents(fov,nearPlane,farPlane);if(!projection)continue;
        const auto native=fvr::engine::NativeEye({*world,*projection});if(!native)continue;
        fvr::bc2::PrecomputeCameraCaches(*copy,updateView,updateProjection,updateFrustum,updateScale);
        const auto error=[&](unsigned offset,const fvr::math::Matrix4& expected,bool relative){
            fvr::math::Matrix4 actual{};std::memcpy(&actual,copy->bytes.data()+offset,64);float difference=0;
            for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){const float a=actual.values[r][c],b=expected.values[r][c];if(!std::isfinite(a)||!std::isfinite(b))return -1.f;
                difference=(std::max)(difference,std::abs(a-b)/(relative?(std::max)(1.f,std::abs(b)):1.f));}return difference;};
        auto& result=cameraExperiment.mapped[i];result.viewError=error(0x220,native->view,false);result.projectionError=error(0x2e0,native->projection,false);
        result.viewProjectionRelativeError=error(0x3a0,fvr::interaction::Multiply(native->view,native->projection),true);
        result.complete=result.viewError>=0&&result.viewError<.005f&&result.projectionError>=0&&result.projectionError<.00002f&&result.viewProjectionRelativeError>=0&&result.viewProjectionRelativeError<.0002f;
    }
    if(camera)for(unsigned i=0;i<cameraExperiment.culling.size();++i){
        const fvr::math::FovTangents left{-.7f,1.3f,1.1f,-.8f},right{-1.3f,.7f,.9f,-.65f};
        fvr::math::Pose reference{},head{};const float turn=float(i)*.08f;head.orientation={0,std::sin(turn),0,std::cos(turn)};head.position={.1f,.15f,-.2f};
        const auto poses=fvr::math::ComputeEyePoses(head,.064f);if(!poses)continue;
        const auto center=fvr::math::ComposeRuntimeHeadWithLhCamera(camera->camera,reference,head,1);
        const auto eye0=fvr::math::ComposeRuntimeHeadWithLhCamera(camera->camera,reference,poses->left,1),eye1=fvr::math::ComposeRuntimeHeadWithLhCamera(camera->camera,reference,poses->right,1);if(!center||!eye0||!eye1)continue;
        const float nearPlane=i<4?.04f:.1f,farPlane=i<4?100.f:2000.f;
        const auto envelope=fvr::math::EncloseStereoFrusta(*center,{*eye0,*eye1},{left,right},nearPlane,farPlane);if(!envelope)continue;
        auto copy=fvr::bc2::BuildRenderViewCopy(original,envelope->world,envelope->fov,envelope->nearPlane,envelope->farPlane);if(!copy)continue;
        fvr::bc2::PrecomputeCameraCaches(*copy,updateView,updateProjection,updateFrustum,updateScale);
        std::array<fvr::math::Vec4,6> planes{};std::memcpy(planes.data(),copy->bytes.data()+0x180,sizeof(planes));
        auto& result=cameraExperiment.culling[i];result.normalError=0;result.maxDistance=-farPlane;
        for(const auto& plane:planes){const float length=std::sqrt(plane.x*plane.x+plane.y*plane.y+plane.z*plane.z);if(!std::isfinite(length)||!std::isfinite(plane.w)){result.normalError=1;break;}result.normalError=(std::max)(result.normalError,std::abs(length-1));}
        for(unsigned eye=0;eye<2;++eye){const auto& fov=eye?right:left;const auto& world=eye?*eye1:*eye0;
            for(float z:{nearPlane,(nearPlane+farPlane)*.5f,farPlane})for(float x:{fov.left,(fov.left+fov.right)*.5f,fov.right})for(float y:{fov.down,(fov.down+fov.up)*.5f,fov.up}){
                const auto point=fvr::math::TransformRowVector({x*z,y*z,z,1},world);
                for(const auto& plane:planes){
                    // Native planes are RH; only this copied-camera diagnostic
                    // converts their z coefficient for the canonical LH point.
                    const auto evidence=fvr::math::MeasureFloatPlaneContainment({plane.x,plane.y,-plane.z,plane.w},point);
                    if(!evidence.valid)result.normalError=1;
                    else {result.maxDistance=(std::max)(result.maxDistance,evidence.distance);
                        result.maxRoundoffAllowance=(std::max)(result.maxRoundoffAllowance,evidence.roundoffAllowance);
                        result.maxExcess=(std::max)(result.maxExcess,evidence.excess);}
                }
            }
        }
        result.passed=result.normalError<.0001f&&result.maxExcess==0;
    }
    cameraExperiment.liveUnchanged=before&&before==Hash(address,original.bytes.size());cameraExperiment.complete.store(true,std::memory_order_release);
}
class CameraPulseScope {
public:
    CameraPulseScope(const Record* record,std::uintptr_t view,unsigned requestState=1)noexcept:record_(record),view_(view),requestState_(requestState){
        if(cameraPulseMode!=(requestState==1?1u:2u)||cameraPulse.claimed||!record||!Owns()||!colorCaptures[0].complete.load(std::memory_order_acquire)||colorCaptures[0].step!=ColorCapture::Step::Finished||!cameraExperiment.complete.load(std::memory_order_acquire))return;
        cameraPulse.claimed=true;
        for(const auto& result:cameraExperiment.mapped)if(!result.complete){cameraPulse.complete.store(true,std::memory_order_release);return;}
        for(const auto& result:cameraExperiment.culling)if(!result.passed){cameraPulse.complete.store(true,std::memory_order_release);return;}
        for(unsigned i=0;i<2;++i){
            const auto address=view_+(i?layout.secondaryOffset:layout.primaryOffset);
            if(!Read(address,saved_[i].bytes.data(),saved_[i].bytes.size()))return;
            fvr::engine::FrostbiteCameraInput input{};std::memcpy(&input.transform,saved_[i].bytes.data()+0x50,64);
            std::memcpy(&input.nearPlane,saved_[i].bytes.data()+0x1c,4);std::memcpy(&input.farPlane,saved_[i].bytes.data()+0x20,4);input.worldUnitsPerMeter=1;
            const auto camera=fvr::engine::CanonicalCamera(input);if(!camera)return;
            // A diagnostic 0.25 engine-unit shift and small yaw. Physical scale
            // is not calibrated and these are NOT claimed to be headset poses.
            fvr::math::Pose origin{},moved{};moved.position.x=.25f;moved.orientation={0,std::sin(.015f),0,std::cos(.015f)};
            const auto world=fvr::math::ComposeRuntimeHeadWithLhCamera(camera->camera,origin,moved,1);if(!world)return;
            auto copy=fvr::bc2::BuildTransformCopy(saved_[i],*world);if(!copy)return;
            fvr::bc2::PrecomputeCameraCaches(*copy,updateView,updateProjection,updateFrustum,updateScale,false);written_[i]=*copy;
        }
        if(!Owns())return;
        for(unsigned i=0;i<2;++i)if(Hash(view_+(i?layout.secondaryOffset:layout.primaryOffset),0x460)!=Hash(reinterpret_cast<std::uintptr_t>(saved_[i].bytes.data()),0x460))return;
        for(unsigned i=0;i<2;++i)std::memcpy(reinterpret_cast<void*>(view_+(i?layout.secondaryOffset:layout.primaryOffset)),written_[i].bytes.data(),0x460);
        active_=true;cameraPulse.applied=true;cameraPulse.frame=record_->worldFrame;
    }
    ~CameraPulseScope(){Restore();}
    bool Active()const noexcept{return active_;}
    void Restore()noexcept {
        if(!active_)return;active_=false;cameraPulse.ownerUnchanged=Owns();
        if(cameraPulse.ownerUnchanged){
            cameraPulse.drawLeftCopiesUnchanged=true;
            for(unsigned i=0;i<2;++i){const auto address=view_+(i?layout.secondaryOffset:layout.primaryOffset);
                cameraPulse.drawLeftCopiesUnchanged&=Hash(address,0x460)==Hash(reinterpret_cast<std::uintptr_t>(written_[i].bytes.data()),0x460);
                std::memcpy(reinterpret_cast<void*>(address),saved_[i].bytes.data(),0x460);
            }
            cameraPulse.restored=true;
            for(unsigned i=0;i<2;++i)cameraPulse.restored&=Hash(view_+(i?layout.secondaryOffset:layout.primaryOffset),0x460)==Hash(reinterpret_cast<std::uintptr_t>(saved_[i].bytes.data()),0x460);
        }
        cameraPulse.complete.store(true,std::memory_order_release);
    }
private:
    bool Owns()const noexcept{return record_&&U32(view_)==imageBase+layout.vtable&&U32(view_+layout.ownerRequestOffset)==record_->request&&U32(record_->request+8)==record_->world&&U32(record_->request+0xc4)==requestState_&&U32(record_->world+0x88)==record_->worldFrame;}
    const Record* record_;std::uintptr_t view_;unsigned requestState_;bool active_=false;
    std::array<fvr::bc2::RenderViewCopy,2> saved_{},written_{};
};

struct ListSnapshot {
    std::uintptr_t address=0;unsigned count=0;std::array<unsigned,64> values{};
    bool Capture(std::uintptr_t at)noexcept{address=at;const auto begin=U32(at),end=U32(at+4),capacity=U32(at+8);if(end<begin||capacity<end||(end-begin)%4||end-begin>values.size()*4)return false;count=(end-begin)/4;return !count||Read(begin,values.data(),count*4);}
    bool Matches()const noexcept {ListSnapshot now{};return now.Capture(address)&&now.count==count&&now.values==values;}
};
fvr::bc2::ViewInitializationCandidates initialization{};
struct CallbackSnapshot {unsigned object=0,vtable=0,borrowed=0;bool remember=false,list=false;ListSnapshot registered{};};
bool SnapshotCallbacks(std::uintptr_t world,std::uintptr_t mainView,ListSnapshot& list,std::array<CallbackSnapshot,16>& callbacks)noexcept {
    if(!list.Capture(world+0x9c)||!list.count||list.count>callbacks.size())return false;
    for(unsigned i=0;i<list.count;++i){auto& cb=callbacks[i];cb.object=list.values[i];cb.vtable=U32(cb.object);const auto create=U32(cb.vtable+28),destroy=U32(cb.vtable+32);
        const auto noop=[](unsigned fn){std::array<unsigned char,3> code{};return fn>=imageBase&&fn<imageBase+profile.imageSize&&Read(fn,code.data(),3)&&code==std::array<unsigned char,3>{0xc2,4,0};};
        if(create==imageBase+viewCallbacks.rememberMain&&noop(destroy)){cb.remember=true;cb.borrowed=U32(cb.object+0xbc);if(cb.borrowed!=mainView)return false;}
        else if(create==imageBase+viewCallbacks.registerView&&destroy==imageBase+viewCallbacks.unregisterView){cb.list=true;if((stereoMode&&U32(cb.vtable+24)!=imageBase+initialization.refreshRegistered)||!cb.registered.Capture(cb.object+0x20))return false;}
        else if(!noop(create)||!noop(destroy))return false;
    }return true;
}
bool RestoreBorrowed(const ListSnapshot& callbacks,const std::array<CallbackSnapshot,16>& values,unsigned fresh,unsigned& changed,unsigned& restored)noexcept {
    if(!callbacks.Matches())return false;bool okay=true;
    for(unsigned i=0;i<callbacks.count;++i){const auto& cb=values[i];if(U32(cb.object)!=cb.vtable){okay=false;continue;}
        if(cb.remember){const auto now=U32(cb.object+0xbc);if(now==fresh){++changed;
            if(fvr::bc2::RestoreBorrowedViewPointer(reinterpret_cast<volatile LONG*>(cb.object+0xbc),fresh,cb.borrowed))++restored;else okay=false;
        }else if(now!=cb.borrowed)okay=false;}
    }return okay;
}
std::atomic<unsigned> inputSamples=0,inputTrackedSamples=0;std::uint64_t lastInputGeneration=0;
bool opticEvidenceEnabled=false;bool passEvidenceEnabled=false,uncapMirror=false;bool streamMode=false;std::wstring streamToken;fvr::graphics::D3D11FrameBridge frameBridge;
fvr::graphics::PrivatePresentationExperiment privatePresentationExperiment;
struct PrivatePresentProgress {
    unsigned budgetMs=0;
    std::uint64_t frequency=0,attempts=0,completed=0,admissionLost=0,budgetExpired=0,clockFailed=0,pollLimit=0;
    std::uint64_t pumps=0,sleeps=0,totalTicks=0,maxTicks=0;
    void Configure()noexcept {
        const auto error=GetLastError();wchar_t text[4]{};
        const auto length=GetEnvironmentVariableW(L"FVR_PRIVATE_PRESENT_PROGRESS_MS",text,4);
        if(length&&length<4)budgetMs=fvr::graphics::ParseFrameProgressBudget({text,length});
        LARGE_INTEGER value{};
        if(QueryPerformanceFrequency(&value)&&value.QuadPart>0)frequency=std::uint64_t(value.QuadPart);
        if(!frequency||frequency>UINT64_MAX/16)budgetMs=0;
        SetLastError(error);
    }
    void Record(const fvr::graphics::FrameProgressResult& result)noexcept {
        ++attempts;pumps+=result.pumps;sleeps+=result.sleeps;totalTicks+=result.ticks;
        maxTicks=(std::max)(maxTicks,result.ticks);
        using Stop=fvr::graphics::FrameProgressStop;
        switch(result.stop){case Stop::Completed:++completed;break;case Stop::AdmissionLost:++admissionLost;break;
            case Stop::BudgetExpired:++budgetExpired;break;case Stop::ClockFailed:++clockFailed;break;
            case Stop::PollLimit:++pollLimit;break;}
    }
    void Write(std::ostream& out,bool quiescent)const {
        out<<"{\"quiescent\":"<<(quiescent?"true":"false");
        // Counters belong to the graphics owner; never race live callbacks.
        if(quiescent)out<<",\"budget_ms\":"<<budgetMs<<",\"qpc_frequency\":"<<frequency
            <<",\"attempts\":"<<attempts<<",\"completed\":"<<completed<<",\"admission_lost\":"<<admissionLost
            <<",\"budget_expired\":"<<budgetExpired<<",\"clock_failed\":"<<clockFailed<<",\"poll_limit\":"<<pollLimit
            <<",\"pumps\":"<<pumps<<",\"sleeps\":"<<sleeps<<",\"total_ticks\":"<<totalTicks<<",\"max_ticks\":"<<maxTicks;
        out<<'}';
    }
} privatePresentProgress;
void ClosePrivatePresentationExperiment()noexcept;
// Private performance evidence only. Observe the camera copy already used by
// ApplyTrackedView, before VR/body adjustments; never read additional native
// memory or use this record to authorize a frame or a native operation.
struct PrivatePresentationCameras {
    struct Row {
        std::uint64_t count=0,invalid=0,firstFrame=0,lastFrame=0,owner=0,space=0;
        std::int64_t firstQpc=0,lastQpc=0;
        bool ownerChanged=false,spaceChanged=false;
        std::array<float,16> first{},last{};
        std::array<float,3> minimum{},maximum{};
        double maxTranslation=0,maxBasisDelta=0;
    };
    std::array<Row,4> rows{};std::atomic_flag gate=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> dropped=0;
    void Observe(unsigned phase,const fvr::bc2::RenderViewCopy& camera,const fvr::ipc::FrameLease& lease)noexcept {
        if(phase<1||phase>4)return;
        if(gate.test_and_set(std::memory_order_acquire)){dropped.fetch_add(1,std::memory_order_relaxed);return;}
        auto& r=rows[phase-1];std::array<float,16> transform{};
        std::memcpy(transform.data(),camera.bytes.data()+0x50,sizeof(transform));
        bool finite=true;for(unsigned i=0;i<16;++i)if(i%4!=3)finite&=std::isfinite(transform[i]);
        if(!finite){++r.invalid;gate.clear(std::memory_order_release);return;}
        for(unsigned i=3;i<16;i+=4)transform[i]=0; // Native padding is not homogeneous W.
        const auto now=Qpc();
        if(!r.count){
            r.first=transform;r.firstQpc=now;r.firstFrame=lease.native.frameId;
            r.owner=lease.native.owner;r.space=lease.tracking.spaceGeneration;
            for(unsigned i=0;i<3;++i)r.minimum[i]=r.maximum[i]=transform[12+i];
        }
        ++r.count;r.last=transform;r.lastQpc=now;r.lastFrame=lease.native.frameId;
        r.ownerChanged|=r.owner!=lease.native.owner;r.spaceChanged|=r.space!=lease.tracking.spaceGeneration;
        double distance=0;
        for(unsigned i=0;i<3;++i){
            const auto value=transform[12+i];r.minimum[i]=std::min(r.minimum[i],value);r.maximum[i]=std::max(r.maximum[i],value);
            const double delta=double(value)-r.first[12+i];distance+=delta*delta;
        }
        r.maxTranslation=std::max(r.maxTranslation,std::sqrt(distance));
        for(unsigned i=0;i<12;++i)if(i%4!=3)r.maxBasisDelta=std::max(r.maxBasisDelta,std::abs(double(transform[i])-r.first[i]));
        gate.clear(std::memory_order_release);
    }
    void Write(std::ostream& out,bool available,bool enabled)const {
        out<<"{\"available\":"<<(available?"true":"false")<<",\"enabled\":"<<(enabled?"true":"false");
        if(!available){out<<'}';return;}
        const auto values=[&](const auto& array){out<<'[';bool first=true;for(auto value:array){if(!first)out<<',';first=false;out<<value;}out<<']';};
        out<<",\"dropped\":"<<dropped.load()<<",\"rows\":[";
        for(unsigned i=0;i<rows.size();++i){const auto& r=rows[i];if(i)out<<',';
            out<<"{\"phase\":"<<i+1<<",\"count\":"<<r.count<<",\"invalid\":"<<r.invalid
               <<",\"first_qpc\":"<<r.firstQpc<<",\"last_qpc\":"<<r.lastQpc<<",\"first_frame\":"<<r.firstFrame<<",\"last_frame\":"<<r.lastFrame
               <<",\"owner\":"<<r.owner<<",\"owner_changed\":"<<(r.ownerChanged?"true":"false")
               <<",\"space_generation\":"<<r.space<<",\"space_changed\":"<<(r.spaceChanged?"true":"false")
               <<",\"first_transform\":";values(r.first);out<<",\"last_transform\":";values(r.last);
            out<<",\"min_position\":";values(r.minimum);out<<",\"max_position\":";values(r.maximum);
            out<<",\"max_translation\":"<<r.maxTranslation<<",\"max_basis_delta\":"<<r.maxBasisDelta<<'}';
        }out<<"]}";
    }
} privatePresentationCameras;
fvr::bc2::Bc2BodyAmmoRenderer bodyAmmoRenderer;bool bodyAmmoObservationEnabled=false;
fvr::bc2::BodyAmmoCacheResult bodyAmmoCache;
struct TrackedTransaction {
    // Idle, camera written, finished/restored, cancel without camera write.
    std::atomic<unsigned> state=0,restoreFailures=0;
    fvr::ipc::FrameLease lease{};std::array<fvr::bc2::RenderViewCopy,2> saved{},written{},observed{},right{};
    bool capturesOkay=true;
    fvr::bc2::BodyPropPair bodyPropPair;
    std::array<float,16> savedAnchor{},writtenAnchor{};bool anchorWritten=false;
} tracked;
enum class TrackedFinishCallsite {RightEyeDraw,WorldReturn,UnwrittenCancellation};
unsigned GraphicsBootstrapThreadForDiagnostics()noexcept;
struct FirstTrackedFinishFailure {
    // Single assignment, no waiting. A callback publishes only a complete row;
    // final serialization additionally requires the existing callback drain.
    std::atomic<unsigned> state=0;
    fvr::graphics::FrameBridgeTimingKey key{};
    std::int64_t qpc=0;unsigned evaluated=0,passed=0,trackedState=0,visibilityMask=0;
    unsigned thread=0,expectedThread=0;
    TrackedFinishCallsite callsite=TrackedFinishCallsite::RightEyeDraw;
    fvr::graphics::RestoredPublicationResult publication=fvr::graphics::RestoredPublicationResult::NotAttempted;
    void Observe(const fvr::ipc::FrameLease& lease,TrackedFinishCallsite site,unsigned nativeState,
        unsigned evaluatedMask,unsigned passedMask,unsigned visibility,
        fvr::graphics::RestoredPublicationResult result,unsigned graphicsThread)noexcept {
        unsigned empty=0;if(!state.compare_exchange_strong(empty,1,std::memory_order_acq_rel))return;
        const auto error=GetLastError();qpc=Qpc();thread=GetCurrentThreadId();expectedThread=graphicsThread;
        key={lease.channelGeneration,lease.requestId,lease.native.owner,lease.native.frameId,lease.native.deviceEpoch,
            lease.tracking.generation,lease.tracking.spaceGeneration,lease.tracking.predictedNs};
        callsite=site;trackedState=nativeState;evaluated=evaluatedMask;passed=passedMask;visibilityMask=visibility;publication=result;
        state.store(2,std::memory_order_release);SetLastError(error);
    }
    void Write(std::ostream& out,bool drained)const {
        const auto status=state.load(std::memory_order_acquire);
        out<<"{\"available\":"<<(drained&&status!=1?"true":"false");
        if(!drained||status==1){out<<'}';return;}
        out<<",\"observed\":"<<(status==2?"true":"false");if(status!=2){out<<'}';return;}
        constexpr const char* sites[]{"right_eye_draw","world_return","unwritten_cancellation"};
        constexpr const char* predicates[]{"complete_pair","restored","captures_okay","color_ready",
            "effect_ready","postprocess_ready","visibility_pair","publication_accepted"};
        unsigned failed=0;while(failed<8&&(!(evaluated&(1u<<failed))||(passed&(1u<<failed))))++failed;
        out<<",\"callsite\":\""<<sites[unsigned(callsite)]<<"\",\"reason\":\""<<(failed<8?predicates[failed]:"unknown")
           <<"\",\"qpc\":"<<qpc<<",\"clock\":\"qpc_ticks\",\"tracked_state\":"<<trackedState
           <<",\"evaluated_mask\":"<<evaluated<<",\"passed_mask\":"<<passed<<",\"predicate_names\":[";
        for(unsigned i=0;i<8;++i){if(i)out<<',';out<<'"'<<predicates[i]<<'"';}
        out<<"],\"visibility_mask\":"<<visibilityMask<<",\"visibility_evaluated\":"<<((evaluated&64)?"true":"false")
           <<",\"publication_result\":\""<<fvr::graphics::RestoredPublicationResultName(publication)<<"\""
           <<",\"thread\":"<<thread<<",\"expected_graphics_thread\":"<<expectedThread
           <<",\"lease\":{\"channel\":"<<key.channel<<",\"request\":"<<key.request<<",\"owner\":"<<key.owner
           <<",\"frame\":"<<key.frame<<",\"device\":"<<key.device<<",\"tracking\":"<<key.tracking
           <<",\"space\":"<<key.space<<",\"predicted_ns\":"<<key.predictedNs<<"}}";
    }
} firstTrackedFinishFailure;
fvr::bc2::stereoColorScale::Pair stereoColorScalePair;
fvr::bc2::stereoColorScale::EffectPair stereoEffectPair;
fvr::bc2::stereoColorScale::PostProcessPair stereoPostProcessPair;
bool stereoColorScaleVerified=false;
fvr::bc2::stereoColorScale::Key CurrentColorScaleKey(unsigned world,unsigned request)noexcept;
fvr::bc2::ViewAnchorCandidates viewAnchor{};bool viewAnchorMode=false;std::atomic<unsigned> anchorCopies=0;
bool burstMode=false;bool initializationOnly=false;HANDLE stereoProgress=INVALID_HANDLE_VALUE;
struct StereoExperiment {
    enum Stage:unsigned {Idle,Armed,Retiring,Released,Failed};
    std::atomic<unsigned> stage=Idle,drawMask=0,visibilityMask=0;
    unsigned world=0,request=0,main=0,eye=0,firstFrame=0,initialColorFrame=0,retireFrame=0,refsBefore=0,refsAfter=0,redirects=0,restores=0;
    std::atomic<unsigned> frame=0,framesRendered=0;
    bool listsRestored=false,callbacksRestored=false,initialized=false,camerasPreserved=false,clockPreserved=false,cachedViewsRestored=false;
    unsigned childrenBefore=0,childrenAfter=0;
    ListSnapshot mainList{},allViews{},callbacks{};std::array<CallbackSnapshot,16> callbackValues{};
} stereoExperiment;
struct StereoShutdownEvidence {
    unsigned captured=0,rejected=0,requestReferencesBefore=0;
    unsigned pendingReferencesBefore=0;
    bool afterCaptured=false;
    ListSnapshot historical{},before{},expected{},after{};
} stereoShutdown;
fvr::bc2::StereoProgressEvidence stereoProgressEvidence;
struct StereoRecoveryNative {
    struct Code {unsigned address=0,size=0;std::uint64_t hash=0;};
    std::array<Code,26> code{};
    unsigned worldType=0,subsystemType=0,requestAddRef=0,requestRelease=0,retiredCacheResize=0;
    fvr::runtime::RenderOwnerRecovery policy;
    fvr::runtime::RenderOwnerTransition sourceTransition;
    std::atomic<unsigned> sourceTransitions=0,sourceTransitionWaits=0,sourceTransitionFailures=0;
    std::atomic<unsigned> callbacks=0,attempts=0,completed=0,rejected=0,failure=0,retirementStep=0;
    std::atomic<unsigned> cacheClearCalls=0,cacheClearReceipts=0;
    std::atomic<unsigned> oldWorld=0,oldRequest=0,oldEye=0,newWorld=0,newRequest=0,retiredViews=0,retiredPendingReferences=0;
    std::atomic<bool> completedOnStop=false;
    std::atomic<std::uint64_t> cancelUnwrittenRequest=0;
} stereoRecovery;
struct LiveCacheEvidence {
    std::atomic<unsigned> calls=0,refreshed=0,disabled=0,rejected=0;
    std::atomic<unsigned> callback=0,beforeCount=0,registeredCount=0,afterCount=0;
} liveCacheEvidence;
struct StereoFrameAdmissionEvidence {
    std::array<std::atomic<std::uint64_t>,unsigned(fvr::bc2::StereoFrameAdmission::Count)> counts{};
    std::atomic<std::uint64_t> blocked=0,lastBlockedTick=0,lastReplacement=0;
    void Record(fvr::bc2::StereoFrameAdmission result,const fvr::bc2::StereoFrameAdmissionSnapshot& snapshot)noexcept {
        counts[unsigned(result)].fetch_add(1,std::memory_order_relaxed);
        if(fvr::bc2::BlocksStereoFrameAdmission(result)){
            blocked.fetch_add(1,std::memory_order_relaxed);
            lastReplacement.store((std::uint64_t(snapshot.currentWorld)<<32)|snapshot.currentRequest,std::memory_order_relaxed);
            lastBlockedTick.store(GetTickCount64(),std::memory_order_relaxed);
        }
    }
    // Fixed-size aggregate only. Reporting never rereads native objects and
    // hooks do not allocate or write files. Independently sampled counters are
    // diagnostics, not a new ownership or transaction authority.
    void Write(std::ostream& out)const {
        out<<"{\"blocked\":"<<blocked.load()<<",\"last_blocked_tick_ms\":"<<lastBlockedTick.load()
           <<",\"last_replacement_key\":"<<lastReplacement.load()<<",\"reasons\":{";
        for(unsigned i=0;i<counts.size();++i){if(i)out<<',';
            out<<'\"'<<fvr::bc2::StereoFrameAdmissionNames[i]<<"\":"<<counts[i].load();}
        out<<"}}";
    }
} stereoFrameAdmissionEvidence;
struct RenderCallbackScope {
    RenderCallbackScope()noexcept{stereoRecovery.callbacks.fetch_add(1,std::memory_order_acq_rel);}
    ~RenderCallbackScope(){stereoRecovery.callbacks.fetch_sub(1,std::memory_order_release);}
};

fvr::bc2::StereoProgressSample StereoProgressSample(unsigned world,unsigned request,unsigned frame)noexcept {
    const auto& e=stereoExperiment;fvr::bc2::StereoProgressSample sample;
    sample.tickMs=GetTickCount64();sample.stage=e.stage.load(std::memory_order_acquire);
    sample.frame=e.frame.load(std::memory_order_acquire);sample.incomingFrame=frame;sample.retireFrame=e.retireFrame;
    sample.world=world;sample.request=request;sample.expectedWorld=e.world;sample.expectedRequest=e.request;
    sample.eye=e.eye;sample.expectedEyeType=unsigned(imageBase+layout.vtable);
    sample.trackedState=tracked.state.load(std::memory_order_acquire);sample.sampling=sampling.load(std::memory_order_acquire)?1u:0u;
    return sample;
}
void RecordStereoProgress(fvr::bc2::StereoProgressReason reason,unsigned frame)noexcept {
    stereoProgressEvidence.Record(reason,StereoProgressSample(stereoExperiment.world,stereoExperiment.request,frame));
}
bool ExpandWorkPool(unsigned frame)noexcept {
    if(workPool.stage.load(std::memory_order_acquire)!=0)return false;
    const auto owner=U32(imageBase+workPool.global);
    if(U32(owner)!=imageBase+workPool.vtable)return false;
    auto& header=*reinterpret_cast<fvr::bc2::WorkPoolHeader*>(owner+0x850);
    if(!workPool.lease.Expand(header,owner+0x860,0x500,reinterpret_cast<unsigned>(workPool.storage.data()),unsigned(workPool.storage.size())))return false;
    workPool.owner=owner;workPool.thread=GetCurrentThreadId();workPool.frame=frame;workPool.stage.store(1,std::memory_order_release);return true;
}
void StereoCheckpoint(const char* label)noexcept {
    if(stereoProgress==INVALID_HANDLE_VALUE)return;const auto& e=stereoExperiment;char line[1024]{};
    const int length=snprintf(line,sizeof(line),"{\"step\":\"%s\",\"frame\":%u,\"main\":%u,\"eye\":%u,\"initialized\":%u,\"children\":%u,\"cached_restored\":%u,\"world\":%u,\"request\":%u,\"old_world\":%u,\"old_request\":%u,\"new_world\":%u,\"new_request\":%u,\"recovery_attempts\":%u,\"recovery_completed\":%u,\"recovery_failure\":%u,\"retirement_step\":%u,\"tick_ms\":%llu,\"published\":%llu,\"consumed\":%llu,\"live_cache_calls\":%u,\"live_cache_before\":%u,\"live_cache_registered\":%u,\"live_cache_after\":%u}\n",label,e.frame.load(),e.main,e.eye,unsigned(e.initialized),e.childrenAfter,unsigned(e.cachedViewsRestored),e.world,e.request,stereoRecovery.oldWorld.load(),stereoRecovery.oldRequest.load(),stereoRecovery.newWorld.load(),stereoRecovery.newRequest.load(),stereoRecovery.attempts.load(),stereoRecovery.completed.load(),stereoRecovery.failure.load(),stereoRecovery.retirementStep.load(),GetTickCount64(),frameBridge.Published(),frameBridge.Consumed(),liveCacheEvidence.calls.load(),liveCacheEvidence.beforeCount.load(),liveCacheEvidence.registeredCount.load(),liveCacheEvidence.afterCount.load());
    if(length>0&&unsigned(length)<sizeof(line)){DWORD written=0;WriteFile(stereoProgress,line,DWORD(length),&written,nullptr);FlushFileBuffers(stereoProgress);}
}
bool RefreshRegisteredCallbacks()noexcept {
    auto& e=stereoExperiment;if(!e.callbacks.Matches())return false;
    using Refresh=void(__thiscall*)(void*,void*);
    for(unsigned i=0;i<e.callbacks.count;++i){const auto& cb=e.callbackValues[i];if(!cb.list)continue;
        if(U32(cb.object)!=cb.vtable||U32(cb.vtable+24)!=imageBase+initialization.refreshRegistered)return false;
        reinterpret_cast<Refresh>(imageBase+initialization.refreshRegistered)(reinterpret_cast<void*>(cb.object),reinterpret_cast<void*>(U32(e.world+0x94)));
        if(U32(cb.object+0xc)){
            ListSnapshot registered{};if(!registered.Capture(cb.object+0x20))return false;
            const auto begin=U32(cb.object+0x10),end=U32(cb.object+0x14);if(end<begin||end-begin!=registered.count*20)return false;
            for(unsigned index=0;index<registered.count;++index)if(U32(begin+index*20)!=registered.values[index])return false;
        }
    }return true;
}
// A native rebuild replaces child identities. Preserve its new original-view
// generation, and exclude only the additional eye and its verified children.
bool CaptureOriginalGeneration(ListSnapshot& list,unsigned at,unsigned eye,unsigned expectedCount)noexcept {
    ListSnapshot snapshot{};if(!snapshot.Capture(at))return false;list={};list.address=at;
    for(unsigned i=0;i<snapshot.count;++i){const auto item=snapshot.values[i];if(item==eye)continue;
        const auto getter=U32(U32(item)+12);
        if(getter==imageBase+initialization.parentGetter&&U32(item+0x1670)==eye)continue;
        list.values[list.count++]=item;
    }return list.count==expectedCount;
}
unsigned ChildCount(unsigned view)noexcept {const auto begin=U32(view+0x1680),end=U32(view+0x1684);return begin&&end>=begin&&(end-begin)%8==0&&end-begin<=64?(end-begin)/8:0;}
bool DiagnosticCameras(unsigned main,std::array<fvr::bc2::RenderViewCopy,2>& camera)noexcept {
    for(unsigned i=0;i<2;++i){fvr::bc2::RenderViewCopy original{};if(!Read(main+(i?layout.secondaryOffset:layout.primaryOffset),original.bytes.data(),0x460))return false;
        fvr::engine::FrostbiteCameraInput input{};std::memcpy(&input.transform,original.bytes.data()+0x50,64);std::memcpy(&input.nearPlane,original.bytes.data()+0x1c,4);std::memcpy(&input.farPlane,original.bytes.data()+0x20,4);input.worldUnitsPerMeter=1;
        const auto canonical=fvr::engine::CanonicalCamera(input);if(!canonical)return false;auto shifted=canonical->camera;
        for(unsigned axis=0;axis<3;++axis)shifted.values[3][axis]+=.25f*shifted.values[0][axis];
        auto copy=fvr::bc2::BuildTransformCopy(original,shifted);if(!copy)return false;fvr::bc2::PrecomputeCameraCaches(*copy,updateView,updateProjection,updateFrustum,updateScale,false);camera[i]=*copy;
    }
    return true;
}
void SetDiagnosticCameras(unsigned view,const std::array<fvr::bc2::RenderViewCopy,2>& camera)noexcept {
    using SetCamera=void(__thiscall*)(void*,const void*);
    reinterpret_cast<SetCamera>(imageBase+layout.setPrimary)(reinterpret_cast<void*>(view),camera[0].bytes.data());
    reinterpret_cast<SetCamera>(imageBase+layout.setSecondary)(reinterpret_cast<void*>(view),camera[1].bytes.data());
}
struct TrackedCameraEvidence {
    std::atomic<unsigned> frame=0,mask=0;
    std::array<std::array<std::array<fvr::bc2::RenderViewCopy,4>,3>,2> cameras{};
} trackedCameraEvidence;
namespace weatherObservation=fvr::bc2::weatherObservation;
weatherObservation::Gate weatherGate;
fvr::graphics::DiagnosticImageLifetime weatherCallbackLifetime;
bool weatherHooksDisabled=false;
bool weatherObservationEnabled=false;unsigned weatherObservationDelayMs=50000;
namespace weatherTranslation=fvr::bc2::weatherTranslation;
bool weatherTranslationEnabled=false;
bool weatherTranslationRuntime=false;
std::atomic<bool> weatherRuntimeStopping=false;
weatherTranslation::FrameCache weatherFrameCache;
std::atomic<unsigned> weatherRuntimeFrames=0,weatherRuntimeCompleted=0,weatherRuntimeSkipped=0;
std::atomic<unsigned> weatherRuntimeScopes=0,weatherRuntimeApplied=0,weatherRuntimeRestored=0,weatherRuntimeRestoreFailures=0,weatherRuntimeRejectedPublish=0;
std::array<std::atomic<unsigned>,unsigned(weatherTranslation::FrameCache::Skip::Count)> weatherRuntimeClaimSkips{};
constexpr std::array<const char*,10> weatherRuntimeReasonNames{"overlapping_scope","camera_read","producer_camera_changed","packet_lifetime","packet_matrix_or_arena","missing_or_overlapping_packets","apply_or_inside_validation","restore_conflict","producer_overlap","release_conflict"};
std::array<std::atomic<unsigned>,weatherRuntimeReasonNames.size()> weatherRuntimeReasons{};
std::atomic<std::uint64_t> weatherRuntimeOverheadTotal=0,weatherRuntimeOverheadMax=0;
struct WeatherRuntimeTimer {
    std::int64_t started=weatherTranslationRuntime?Qpc():0;
    ~WeatherRuntimeTimer(){if(started){const auto elapsed=std::uint64_t(Qpc()-started);weatherRuntimeOverheadTotal.fetch_add(elapsed);
        auto max=weatherRuntimeOverheadMax.load();while(elapsed>max&&!weatherRuntimeOverheadMax.compare_exchange_weak(max,elapsed)){} }}
};
std::atomic<bool> weatherTranslationBusy=false,weatherTranslationFailed=false;
std::atomic<unsigned> weatherTranslationAttempts=0,weatherTranslationOverflow=0,weatherTranslationWrites=0;
struct WeatherPayload {
    unsigned kind=0,thread=0,phase=0,eye=0,rootEye=UINT32_MAX,invocation=0,callsite=0;
    std::array<unsigned,6> args{};
    std::array<std::array<unsigned,16>,2> cameraTransforms{};
    unsigned context=0,input=0,validMask=0;
    std::array<unsigned,16> inputWorld{};
    std::array<unsigned,4> addresses{};
    std::array<std::array<unsigned,16>,4> values{};
    bool activeAfter=false;
    weatherTranslation::Packet packet{};bool packetValid=false;
};
struct WeatherRow {WeatherPayload value{};std::atomic<bool> ready=false;};
std::array<std::array<WeatherRow,weatherObservation::Gate::CapacityPerEye>,2> weatherRows;
std::atomic<unsigned> weatherUnattributed=0,weatherIdentityMismatches=0,weatherImageInvalidMask=0;
thread_local WeatherRow* weatherInvocation=nullptr;
thread_local int weatherBoundaryEye=-1;
thread_local unsigned weatherBoundaryPhase=0;
weatherObservation::Key CurrentWeatherKey()noexcept {
    const auto& e=stereoExperiment;return {e.world,e.request,e.main,e.eye,e.frame.load(std::memory_order_acquire)};
}
bool WeatherOwnerCurrent(weatherObservation::Key key)noexcept {
    return weatherGate.Active(key)&&key==CurrentWeatherKey()&&tracked.state.load(std::memory_order_acquire)==1&&
        U32(key.world+0x88)==key.frame&&U32(key.request+8)==key.world&&
        U32(key.main)==imageBase+layout.vtable&&U32(key.eye)==imageBase+layout.vtable&&
        U32(key.main+0x70)==key.request&&U32(key.eye+0x70)==key.request;
}
bool WeatherDrawCurrent(weatherObservation::Key key,unsigned world,unsigned request,unsigned view)noexcept {
    return world==key.world&&request==key.request&&(view==key.main||view==key.eye)&&WeatherOwnerCurrent(key);
}
bool WeatherRestoreOwnerCurrent(weatherObservation::Key key)noexcept {
    // Shutdown may close observation admission while an admitted scope unwinds.
    // Restoration still requires the exact live render identity, not admission.
    return key.Valid()&&key==CurrentWeatherKey()&&tracked.state.load(std::memory_order_acquire)==1&&
        U32(key.world+0x88)==key.frame&&U32(key.request+8)==key.world&&
        U32(key.main)==imageBase+layout.vtable&&U32(key.eye)==imageBase+layout.vtable&&
        U32(key.main+0x70)==key.request&&U32(key.eye+0x70)==key.request;
}
std::optional<weatherTranslation::Packet> ReadWeatherPacket(unsigned address,unsigned owner)noexcept {
    weatherTranslation::Packet p{};p.address=address;p.owner=owner;
    if(address<0x10000||address>UINT32_MAX-128||owner<0x10000||owner>UINT32_MAX-0x40)return {};
    p.packetOwner=U32(address);p.world=U32(address+0x18);p.flags=U32(address+8);p.arena=U32(owner+8);
    if(p.arena<0x10000||p.arena>UINT32_MAX-12)return {};
    p.capacity=U32(p.arena);p.base=U32(p.arena+4);p.used=U32(p.arena+8);
    if(!weatherTranslation::Bounds(p)||!Read(p.world,p.matrix.data(),64)||
       U32(address)!=owner||U32(address+0x18)!=p.world||U32(owner+8)!=p.arena||
       U32(p.arena)!=p.capacity||U32(p.arena+4)!=p.base)return {};
    return p;
}
struct WeatherBoundaryScope {
    int savedEye=weatherBoundaryEye;unsigned savedPhase=weatherBoundaryPhase;
    WeatherBoundaryScope(unsigned world,unsigned request,unsigned view,unsigned phase)noexcept {
        weatherBoundaryEye=-1;weatherBoundaryPhase=0;
        if(!weatherObservationEnabled)return;
        const auto key=weatherGate.Selected();
        if(WeatherDrawCurrent(key,world,request,view)){
            weatherBoundaryEye=view==key.main?0:1;weatherBoundaryPhase=phase;
        }
    }
    ~WeatherBoundaryScope(){weatherBoundaryEye=savedEye;weatherBoundaryPhase=savedPhase;}
};
// Native795745 environment: thiscall(renderer,contextOwner,kind,currentCamera,
// rootCamera,viewport,weather). Copies evidence only; calls original exactly once.
using EnvironmentFn=void*(__thiscall*)(void*,void*,unsigned,void*,void*,void*,void*);
using WorldMatrixFn=void*(__thiscall*)(void*,const void*);
EnvironmentFn environmentOriginal=nullptr;WorldMatrixFn weatherWorldOriginal=nullptr;
struct WeatherFrameProducer {
    weatherTranslation::FrameToken token{};unsigned owner=0,slot=0,root=0;bool captured=false,duplicate=false,cameraValid=false;
    weatherTranslation::Packet packet{};weatherTranslation::Words camera{};
};
thread_local WeatherFrameProducer* weatherFrameProducer=nullptr;
void FinishWeatherProducer(WeatherFrameProducer& p,bool returned)noexcept {
    if(!p.token)return;WeatherRuntimeTimer timer;
    weatherTranslation::Words camera{};
    const bool valid=returned&&p.cameraValid&&p.captured&&!p.duplicate&&!weatherRuntimeStopping.load()&&weatherFrameCache.Current(p.token)&&WeatherRestoreOwnerCurrent(p.token.key)&&
       Read(p.root+0x50,camera.data(),64)&&camera==p.camera;
    if(!weatherFrameCache.CompleteProducer(p.token,p.slot,valid?&p.packet:nullptr,valid?&p.camera:nullptr))weatherRuntimeRejectedPublish.fetch_add(1);
}
void* OriginalEnvironmentWithWeather(void* self,void* owner,unsigned kind,void* camera,void* rootCamera,void* viewport,void* weather,
    WeatherFrameProducer* producer,WeatherFrameProducer* priorProducer,WeatherRow* priorInvocation,
    std::optional<fvr::graphics::DiagnosticImageLifetime::Scope>* lifetime,std::optional<RenderCallbackScope>* callback){
    struct Call {void* self;void* owner;unsigned kind;void* camera;void* rootCamera;void* viewport;void* weather;
        WeatherFrameProducer* producer;WeatherFrameProducer* priorProducer;WeatherRow* priorInvocation;
        std::optional<fvr::graphics::DiagnosticImageLifetime::Scope>* lifetime;std::optional<RenderCallbackScope>* callback;
    } call{self,owner,kind,camera,rootCamera,viewport,weather,producer,priorProducer,priorInvocation,lifetime,callback};
    return weatherTranslation::InvokeWithFinally([](void* raw)->void* {const auto& c=*static_cast<Call*>(raw);
        return environmentOriginal(c.self,c.owner,c.kind,c.camera,c.rootCamera,c.viewport,c.weather);
    },&call,[](void* raw,bool returned)noexcept {const auto& c=*static_cast<Call*>(raw);
        weatherFrameProducer=c.priorProducer;
        if(c.producer)FinishWeatherProducer(*c.producer,returned);
        if(!returned){weatherInvocation=c.priorInvocation;c.callback->reset();c.lifetime->reset();}
    });
}
void* __fastcall EnvironmentObserveHook(void* self,void*,void* owner,unsigned kind,void* camera,void* rootCamera,void* viewport,void* weather){
    std::optional<fvr::graphics::DiagnosticImageLifetime::Scope> weatherScope;weatherScope.emplace(weatherCallbackLifetime);
    if(!weatherScope->Active()||(!weatherTranslationRuntime&&weatherGate.Closed())){weatherScope.reset();return environmentOriginal(self,owner,kind,camera,rootCamera,viewport,weather);}
    std::optional<RenderCallbackScope> callback;callback.emplace();auto* previous=weatherInvocation;weatherInvocation=nullptr;
    WeatherFrameProducer producer;auto* priorProducer=weatherFrameProducer;weatherFrameProducer=nullptr;
    if(weatherTranslationRuntime&&!weatherRuntimeStopping.load()&&(kind==1||kind==8)){
        WeatherRuntimeTimer timer;const auto current=CurrentWeatherKey();
        if(reinterpret_cast<unsigned>(rootCamera)==current.eye+layout.primaryOffset&&WeatherRestoreOwnerCurrent(current)){
            producer.owner=reinterpret_cast<unsigned>(owner);producer.slot=kind==1?0:1;producer.root=reinterpret_cast<unsigned>(rootCamera);
            producer.token=weatherFrameCache.EnterProducer(current,producer.slot);
            if(producer.token){producer.cameraValid=Read(producer.root+0x50,producer.camera.data(),64);weatherFrameProducer=&producer;}
        }
    }
    const auto key=weatherGate.Selected();
    if(WeatherOwnerCurrent(key)){
        const auto root=reinterpret_cast<unsigned>(rootCamera);
        const auto rootEye=root==key.main+layout.primaryOffset?0u:(root==key.eye+layout.primaryOffset?1u:UINT32_MAX);
        const unsigned eye=weatherBoundaryEye>=0?unsigned(weatherBoundaryEye):rootEye;
        const auto index=weatherGate.Reserve(key,eye);
        if(index!=weatherObservation::Gate::NoSlot){
            auto& row=weatherRows[eye][index];auto& p=row.value;weatherInvocation=&row;
            p.kind=0;p.thread=GetCurrentThreadId();p.phase=weatherBoundaryPhase;p.eye=eye;p.rootEye=rootEye;p.invocation=index;
            p.callsite=reinterpret_cast<unsigned>(_ReturnAddress());
            p.args={reinterpret_cast<unsigned>(owner),kind,reinterpret_cast<unsigned>(camera),root,reinterpret_cast<unsigned>(viewport),reinterpret_cast<unsigned>(weather)};
            if(Read(p.args[2]+0x50,p.cameraTransforms[0].data(),64))p.validMask|=1;
            if(Read(p.args[3]+0x50,p.cameraTransforms[1].data(),64))p.validMask|=2;
        }else if(eye>1)weatherUnattributed.fetch_add(1,std::memory_order_relaxed);
    }
    auto* result=OriginalEnvironmentWithWeather(self,owner,kind,camera,rootCamera,viewport,weather,
        &producer,priorProducer,previous,&weatherScope,&callback);
    if(weatherInvocation){weatherInvocation->value.activeAfter=WeatherOwnerCurrent(key);
        if(!weatherInvocation->value.activeAfter)weatherIdentityMismatches.fetch_add(1,std::memory_order_relaxed);
        weatherInvocation->ready.store(true,std::memory_order_release);}
    weatherInvocation=previous;
    return result; // Preserve native EAX, including callers outside weather.
}
void* OriginalWorldWithWeather(void* self,const void* matrix,
    std::optional<fvr::graphics::DiagnosticImageLifetime::Scope>* lifetime,std::optional<RenderCallbackScope>* callback){
    struct Call {void* self;const void* matrix;std::optional<fvr::graphics::DiagnosticImageLifetime::Scope>* lifetime;
        std::optional<RenderCallbackScope>* callback;} call{self,matrix,lifetime,callback};
    return weatherTranslation::InvokeWithFinally([](void* raw)->void* {const auto& c=*static_cast<Call*>(raw);return weatherWorldOriginal(c.self,c.matrix);
    },&call,[](void* raw,bool returned)noexcept {const auto& c=*static_cast<Call*>(raw);if(!returned){c.callback->reset();c.lifetime->reset();}});
}
void* __fastcall WeatherWorldObserveHook(void* self,void*,const void* matrix){
    if(!weatherInvocation&&!weatherFrameProducer)return weatherWorldOriginal(self,matrix);
    std::optional<fvr::graphics::DiagnosticImageLifetime::Scope> weatherScope;weatherScope.emplace(weatherCallbackLifetime);
    if(!weatherScope->Active()||(!weatherTranslationRuntime&&weatherGate.Closed())){weatherScope.reset();return weatherWorldOriginal(self,matrix);}
    std::optional<RenderCallbackScope> callback;callback.emplace();WeatherRow* row=nullptr;
    const auto site=reinterpret_cast<unsigned>(_ReturnAddress());const auto key=weatherGate.Selected();
    if(weatherInvocation&&site>=imageBase&&weatherObservation::WorldSite(site-unsigned(imageBase))&&WeatherOwnerCurrent(key)){
        const unsigned eye=weatherInvocation->value.eye,index=weatherGate.Reserve(key,eye);
        if(index!=weatherObservation::Gate::NoSlot){row=&weatherRows[eye][index];row->value=weatherInvocation->value;
            auto& p=row->value;p.kind=1;p.thread=GetCurrentThreadId();p.callsite=site;p.context=reinterpret_cast<unsigned>(self);p.input=reinterpret_cast<unsigned>(matrix);
            if(Read(p.input,p.inputWorld.data(),64))p.validMask|=4;
        }
    }
    auto* result=OriginalWorldWithWeather(self,matrix,&weatherScope,&callback);
    if(weatherFrameProducer&&weatherFrameProducer->token&&site==imageBase+(weatherFrameProducer->slot?0x7df13e:0x7de3cc)){
        WeatherRuntimeTimer timer;auto& p=*weatherFrameProducer;
        if(p.captured)p.duplicate=true;
        else if(weatherFrameCache.Current(p.token)&&WeatherRestoreOwnerCurrent(p.token.key))if(const auto packet=ReadWeatherPacket(reinterpret_cast<unsigned>(self),p.owner)){p.packet=*packet;p.captured=true;}
    }
    if(row){auto& p=row->value;
        for(unsigned part=0;part<4;++part){p.addresses[part]=U32(p.context+0x18+part*4);
            if(Read(p.addresses[part],p.values[part].data(),part==3?16:64))p.validMask|=8u<<part;}
        p.activeAfter=WeatherOwnerCurrent(key);if(!p.activeAfter)weatherIdentityMismatches.fetch_add(1,std::memory_order_relaxed);
        if(weatherTranslationEnabled||weatherTranslationRuntime)if(const auto packet=ReadWeatherPacket(p.context,p.args[0])){p.packet=*packet;p.packetValid=true;}
        row->ready.store(true,std::memory_order_release);
    }
    return result;
}
struct WeatherTranslationAttempt {
    weatherObservation::Key beforeKey{},insideKey{},afterKey{};
    unsigned phase=0,eye=0,thread=0,beforeMask=0,insideMask=0,afterMask=0,writtenMask=0,unresolvedMask=0;
    const char* reason="not_started";bool applied=false,restored=false,validated=false;
    std::array<weatherTranslation::Packet,2> before{},inside{},after{};
    std::atomic<bool> ready=false;
};
std::array<WeatherTranslationAttempt,2> weatherTranslationEvidence;
std::mutex weatherRuntimeEvidenceMutex;
struct WeatherRuntimePair {weatherTranslation::FrameToken token{};unsigned mask=0;std::array<WeatherTranslationAttempt,2> scopes{};};
WeatherRuntimePair weatherRuntimePending,weatherRuntimeFirst,weatherRuntimeSelected,weatherRuntimeLatest;
void CopyWeatherAttempt(WeatherTranslationAttempt& to,const WeatherTranslationAttempt& from)noexcept {
    to.beforeKey=from.beforeKey;to.insideKey=from.insideKey;to.afterKey=from.afterKey;
    to.phase=from.phase;to.eye=from.eye;to.thread=from.thread;to.beforeMask=from.beforeMask;to.insideMask=from.insideMask;to.afterMask=from.afterMask;
    to.writtenMask=from.writtenMask;to.unresolvedMask=from.unresolvedMask;to.reason=from.reason;to.applied=from.applied;to.restored=from.restored;to.validated=from.validated;
    to.before=from.before;to.inside=from.inside;to.after=from.after;to.ready.store(from.ready.load(std::memory_order_acquire),std::memory_order_release);
}
void CopyWeatherRuntimePair(WeatherRuntimePair& to,const WeatherRuntimePair& from)noexcept {
    to.token=from.token;to.mask=from.mask;for(unsigned i=0;i<2;++i)CopyWeatherAttempt(to.scopes[i],from.scopes[i]);
}
void BeginWeatherRuntime()noexcept {
    if(!weatherTranslationRuntime||weatherRuntimeStopping.load())return;WeatherRuntimeTimer timer;
    const auto key=CurrentWeatherKey();if(!weatherFrameCache.Begin(key))return;
    std::lock_guard lock(weatherRuntimeEvidenceMutex);weatherRuntimePending.token=weatherFrameCache.Capture(key);weatherRuntimePending.mask=0;
    weatherRuntimeFrames.fetch_add(1);
}
void RecordWeatherRuntime(weatherTranslation::FrameToken token,const WeatherTranslationAttempt& scope)noexcept {
    weatherRuntimeScopes.fetch_add(1);weatherRuntimeApplied.fetch_add(scope.applied);weatherRuntimeRestored.fetch_add(scope.restored);
    std::lock_guard lock(weatherRuntimeEvidenceMutex);
    if(token!=weatherRuntimePending.token||scope.phase<1||scope.phase>2)return;
    CopyWeatherAttempt(weatherRuntimePending.scopes[scope.phase-1],scope);weatherRuntimePending.mask|=1u<<(scope.phase-1);
}
void EndWeatherRuntime()noexcept {
    if(!weatherTranslationRuntime)return;WeatherRuntimeTimer timer;
    const auto token=weatherFrameCache.Capture(CurrentWeatherKey());if(!token)return;
    {std::lock_guard lock(weatherRuntimeEvidenceMutex);
        const auto& p=weatherRuntimePending;
        const bool complete=p.token==token&&p.mask==3&&p.scopes[0].restored&&p.scopes[1].restored&&p.scopes[0].validated&&p.scopes[1].validated;
        if(complete){weatherRuntimeCompleted.fetch_add(1);if(!weatherRuntimeFirst.mask)CopyWeatherRuntimePair(weatherRuntimeFirst,p);
            if(weatherObservationEnabled&&token.key==weatherGate.Selected())CopyWeatherRuntimePair(weatherRuntimeSelected,p);
            CopyWeatherRuntimePair(weatherRuntimeLatest,p);
        }else weatherRuntimeSkipped.fetch_add(1);
    }
    if(!weatherFrameCache.Retire(token)){weatherRuntimeRestoreFailures.fetch_add(1);tracked.restoreFailures.fetch_add(1);tracked.capturesOkay=false;}
}
// This leaf cannot run a C++ destructor during a structured exception. All reads
// and the twelve-byte write are protected; no protection change or stale restore.
weatherTranslation::Replaced CompareWeatherTranslation(unsigned address,const weatherTranslation::Words& expected,
    const weatherTranslation::Words& replacement)noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||(region.Protect&0xff)!=PAGE_READWRITE||
       address-reinterpret_cast<std::uintptr_t>(region.BaseAddress)>region.RegionSize||
       64>region.RegionSize-(address-reinterpret_cast<std::uintptr_t>(region.BaseAddress)))return weatherTranslation::Replaced::Rejected;
    bool writing=false;
    __try {
        if(std::memcmp(reinterpret_cast<void*>(address),expected.data(),64))return weatherTranslation::Replaced::Rejected;
        writing=true;std::memcpy(reinterpret_cast<void*>(address+48),replacement.data()+12,12);
        weatherTranslationWrites.fetch_add(1,std::memory_order_relaxed);
        return weatherTranslation::Replaced::Applied;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return writing?weatherTranslation::Replaced::Indeterminate:weatherTranslation::Replaced::Rejected;
    }
}
struct WeatherTranslationScope {
    weatherObservation::Key key{};WeatherTranslationAttempt* evidence=nullptr;
    WeatherTranslationAttempt localEvidence{};weatherTranslation::FrameToken runtimeToken{};bool runtimeClaimed=false;
    std::array<weatherTranslation::Plan,2> plans{};weatherTranslation::Transaction transaction;
    std::optional<fvr::graphics::DiagnosticImageLifetime::Scope> callback;
    bool owns=false,finished=false;
    WeatherTranslationScope(unsigned world,unsigned request,unsigned view,unsigned phase)noexcept {
        if(!weatherTranslationEnabled&&!weatherTranslationRuntime)return;WeatherRuntimeTimer timer;
        key=weatherTranslationRuntime?CurrentWeatherKey():weatherGate.Selected();
        if(view!=key.main||world!=key.world||request!=key.request||
           !(weatherTranslationRuntime?(!weatherRuntimeStopping.load()&&WeatherRestoreOwnerCurrent(key)):WeatherDrawCurrent(key,world,request,view)))return;
        callback.emplace(weatherCallbackLifetime);if(!callback->Active())return;
        std::optional<weatherTranslation::FramePackets> runtimePackets;
        if(weatherTranslationRuntime){runtimeToken=weatherFrameCache.Capture(key);weatherTranslation::FrameCache::Skip skip{};
            runtimePackets=weatherFrameCache.Claim(runtimeToken,phase,&skip);
            if(!runtimePackets){weatherRuntimeClaimSkips[unsigned(skip)].fetch_add(1);return;}runtimeClaimed=true;evidence=&localEvidence;
        }else {const auto index=weatherTranslationAttempts.fetch_add(1,std::memory_order_relaxed);
            if(index>=weatherTranslationEvidence.size()){weatherTranslationOverflow.fetch_add(1);weatherTranslationFailed=true;return;}
            evidence=&weatherTranslationEvidence[index];evidence->beforeKey=key;evidence->phase=phase;evidence->thread=GetCurrentThreadId();
            if(weatherTranslationFailed.load()||phase!=index+1){Reject("prior_failure_or_phase_order");return;}}
        evidence->beforeKey=key;evidence->phase=phase;evidence->thread=GetCurrentThreadId();
        bool free=false;
        if(!weatherTranslationBusy.compare_exchange_strong(free,true,std::memory_order_acq_rel)){Reject("overlapping_scope");return;}owns=true;
        weatherTranslation::Words left{},right{};
        if(!Read(key.main+layout.primaryOffset+0x50,left.data(),64)||!Read(key.eye+layout.primaryOffset+0x50,right.data(),64)){Reject("camera_read");return;}
        unsigned mask=0;
        if(runtimePackets){
            for(unsigned slot=0;slot<2;++slot){const auto& captured=runtimePackets->packets[slot];
                if(runtimePackets->cameras[slot]!=right){Reject("producer_camera_changed");return;}
                const auto current=ReadWeatherPacket(captured.address,captured.owner);
                if(!current){Reject("packet_lifetime");return;}const auto plan=weatherTranslation::MakePlan(captured,*current,left,right);
                if(!plan){Reject("packet_matrix_or_arena");return;}plans[slot]=*plan;evidence->before[slot]=*current;mask|=1u<<slot;
            }
        }else for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<weatherGate.Count(eye);++i){
            const auto& row=weatherRows[eye][i];if(!row.ready.load(std::memory_order_acquire)){Reject("builder_incomplete");return;}
            const auto& p=row.value;if(p.kind!=1)continue;
            const int slot=p.args[1]==1&&p.callsite==imageBase+0x7de3cc?0:(p.args[1]==8&&p.callsite==imageBase+0x7df13e?1:-1);
            if(slot<0)continue;
            if((mask&(1u<<slot))||!p.activeAfter||!p.packetValid||p.rootEye!=1||p.cameraTransforms[1]!=right||
               p.invocation>=weatherGate.Count(eye)||!weatherRows[eye][p.invocation].ready.load(std::memory_order_acquire)||
               weatherRows[eye][p.invocation].value.kind!=0||!weatherRows[eye][p.invocation].value.activeAfter){Reject("packet_identity");return;}
            const auto current=ReadWeatherPacket(p.context,p.args[0]);
            if(!current){Reject("packet_lifetime");return;}
            const auto plan=weatherTranslation::MakePlan(p.packet,*current,left,right);
            if(!plan){Reject("packet_matrix_or_arena");return;}
            plans[slot]=*plan;evidence->before[slot]=*current;mask|=1u<<slot;
        }
        evidence->beforeMask=mask;
        if(mask!=3||!WeatherRestoreOwnerCurrent(key)||!transaction.Stage(plans)){Reject("missing_or_overlapping_packets");return;}
        evidence->applied=transaction.Apply([&](unsigned i,const auto& a,const auto& b){return Replace(i,a,b);});
        evidence->writtenMask=transaction.WrittenMask();evidence->insideKey=CurrentWeatherKey();
        evidence->insideMask=Snapshot(evidence->inside);
        bool insideExact=evidence->insideMask==3&&WeatherRestoreOwnerCurrent(key);
        if(insideExact)for(unsigned i=0;i<2;++i)insideExact&=weatherTranslation::SameStorage(plans[i].packet,evidence->inside[i])&&evidence->inside[i].matrix==plans[i].after;
        if(!evidence->applied||!insideExact){Reject("apply_or_inside_validation");Finish();return;}
        evidence->validated=true;
        evidence->reason="applied";
    }
    void Reject(const char* reason)noexcept {if(evidence){evidence->reason=reason;evidence->validated=false;}
        if(!weatherTranslationRuntime)weatherTranslationFailed.store(true,std::memory_order_release);
        else for(unsigned i=0;i<weatherRuntimeReasonNames.size();++i)if(!std::strcmp(reason,weatherRuntimeReasonNames[i])){weatherRuntimeReasons[i].fetch_add(1);break;}}
    unsigned Snapshot(std::array<weatherTranslation::Packet,2>& result)noexcept {
        if(!WeatherRestoreOwnerCurrent(key))return 0;unsigned mask=0;
        for(unsigned i=0;i<2;++i)if(const auto p=ReadWeatherPacket(plans[i].packet.address,plans[i].packet.owner)){result[i]=*p;mask|=1u<<i;}
        return WeatherRestoreOwnerCurrent(key)?mask:0;
    }
    weatherTranslation::Replaced Replace(unsigned i,const weatherTranslation::Words& expected,const weatherTranslation::Words& replacement)noexcept {
        if(!owns||(runtimeClaimed&&!weatherFrameCache.Current(runtimeToken))||!WeatherRestoreOwnerCurrent(key)||GetCurrentThreadId()!=evidence->thread)return weatherTranslation::Replaced::Rejected;
        if(runtimeClaimed&&replacement==plans[i].after&&!weatherFrameCache.ConsumerIntact(runtimeToken,evidence->phase))return weatherTranslation::Replaced::Rejected;
        const auto current=ReadWeatherPacket(plans[i].packet.address,plans[i].packet.owner);
        if(!current||!weatherTranslation::SameStorage(plans[i].packet,*current)||current->matrix!=expected||!WeatherRestoreOwnerCurrent(key))return weatherTranslation::Replaced::Rejected;
        return CompareWeatherTranslation(current->world,expected,replacement);
    }
    void Finish()noexcept {
        if(finished)return;finished=true;WeatherRuntimeTimer timer;
        if(evidence){
            const bool restored=transaction.Restore([&](unsigned i,const auto& a,const auto& b){return Replace(i,a,b);});
            evidence->restored=restored&&evidence->applied;evidence->unresolvedMask=transaction.UnresolvedMask();
            evidence->afterKey=CurrentWeatherKey();evidence->afterMask=Snapshot(evidence->after);
            bool exact=WeatherRestoreOwnerCurrent(key)&&evidence->afterMask==3;
            if(exact)for(unsigned i=0;i<2;++i)exact&=weatherTranslation::SameStorage(plans[i].packet,evidence->after[i])&&evidence->after[i].matrix==plans[i].packet.matrix;
            if(!restored||(evidence->writtenMask&&!exact)){
                Reject("restore_conflict");tracked.restoreFailures.fetch_add(1,std::memory_order_release);tracked.capturesOkay=false;
                if(weatherTranslationRuntime)weatherRuntimeRestoreFailures.fetch_add(1);
            }else if(evidence->applied)evidence->reason="restored";
            evidence->ready.store(true,std::memory_order_release);
        }
        if(runtimeClaimed){
            if(!weatherFrameCache.ConsumerIntact(runtimeToken,evidence->phase)&&evidence->writtenMask){Reject("producer_overlap");weatherRuntimeRestoreFailures.fetch_add(1);tracked.restoreFailures.fetch_add(1);tracked.capturesOkay=false;evidence->restored=false;}
            if(!weatherFrameCache.Release(runtimeToken,evidence->phase)){Reject("release_conflict");weatherRuntimeRestoreFailures.fetch_add(1);tracked.restoreFailures.fetch_add(1);tracked.capturesOkay=false;evidence->restored=false;}
            RecordWeatherRuntime(runtimeToken,*evidence);runtimeClaimed=false;}
        if(owns){weatherTranslationBusy.store(false,std::memory_order_release);owns=false;}
        callback.reset();
    }
    ~WeatherTranslationScope(){Finish();}
};
void* OriginalPrepareWithWeatherScope(void* self,void* request,void* view,WeatherTranslationScope* weather){
    __try {return prepareOriginal(self,request,view);}
    __finally {if(weather)weather->Finish();}
}
std::atomic<unsigned> vehicleCameraEvidenceFrame=0;
// Observe native camera bindings; correction scopes below restore exact writes.
using ContextCameraFn=void(__thiscall*)(void*,void*,void*);
ContextCameraFn contextCameraOriginal=nullptr;
thread_local int gatheringEvidenceEye=-1;
using ProjectionContextFn=void*(__thiscall*)(void*,const void*);
ProjectionContextFn projectionContextOriginal=nullptr;
fvr::bc2::ProjectionOverrideCandidates projectionOverrides{};
struct DerivedProjection {unsigned context=0,address=0;std::array<float,16> before{},written{};std::atomic<unsigned> frame=0;};
std::array<DerivedProjection,256> derivedProjections;std::atomic<unsigned> derivedCount=0,derivedFailures=0,derivedWrites=0;
std::atomic<bool> derivedFrameFailed=false;
thread_local bool drawingNativeView=false;
struct WeaponContext {unsigned view=0,projection=0,position=0;std::array<float,36> right{},left{};std::atomic<unsigned> frame=0;};
std::array<WeaponContext,32> weaponContexts;std::atomic<unsigned> weaponContextCount=0,weaponProjectionCopies=0;
fvr::runtime::FrameObservationFailures weaponContextFailures;
struct ProjectionBinding {unsigned context=0,address=0,callsite=0,thread=0,frame=0;int gather=-1;std::array<float,16> values{};};
std::array<ProjectionBinding,512> projectionEvidence;std::atomic<unsigned> projectionCount=0;
void* __fastcall ProjectionContextHook(void* self,void*,const void* matrix){
    RenderCallbackScope callback;
    const auto observationToken=weaponContextFailures.Capture();
    const auto returnSite=reinterpret_cast<unsigned>(_ReturnAddress());
    std::array<float,16> weaponProjection{};bool weapon=false;
    if(viewAnchorMode&&streamMode&&tracked.state.load(std::memory_order_acquire)==1&&returnSite==imageBase+viewAnchor.projectionCaller){
        std::array<float,16> source{},right{};
        if(Read(reinterpret_cast<std::uintptr_t>(matrix),source.data(),64)){
            std::memcpy(right.data(),tracked.right[0].bytes.data()+0x2e0,64);
            if(const auto adjusted=fvr::math::RetargetFirstPersonProjection(source,right)){weaponProjection=*adjusted;matrix=weaponProjection.data();weapon=true;++weaponProjectionCopies;}
        }
        if(!weapon)weaponContextFailures.Reject(observationToken);
    }
    void* const result=projectionContextOriginal(self,matrix);
    if(!streamMode||tracked.state.load(std::memory_order_acquire)!=1)return result;
    if(!weaponContextFailures.Current(observationToken))return result;
    if(weapon){
        const auto index=weaponContextCount.fetch_add(1);
        if(index>=weaponContexts.size()){weaponContextFailures.Reject(observationToken);return result;}
        auto& w=weaponContexts[index];const auto at=reinterpret_cast<unsigned>(self);
        w.view=U32(at+0x1c);w.projection=U32(at+0x20);w.position=U32(at+0x24);
        if(!Read(w.view,w.right.data(),64)||!Read(w.projection,w.right.data()+16,64)||!Read(w.position,w.right.data()+32,16)){weaponContextFailures.Reject(observationToken);return result;}
        // The authored first-person transform now follows the tracked right eye.
        std::array<float,16> expectedView{};std::memcpy(expectedView.data(),tracked.right[0].bytes.data()+0x220,64);
        for(unsigned i=0;i<16;++i)if(std::abs(w.right[i]-expectedView[i])>.001f){weaponContextFailures.Reject(observationToken);return result;}
        w.left=w.right;std::memcpy(w.left.data(),tracked.written[0].bytes.data()+0x220,64);
        std::array<float,16> leftProjection{};std::memcpy(leftProjection.data(),tracked.written[0].bytes.data()+0x2e0,64);
        const auto adjusted=fvr::math::RetargetFirstPersonProjection(weaponProjection,leftProjection);if(!adjusted){weaponContextFailures.Reject(observationToken);return result;}
        std::memcpy(w.left.data()+16,adjusted->data(),64);std::memcpy(w.left.data()+32,tracked.written[0].bytes.data()+0x80,16);
        w.frame.store(stereoExperiment.frame,std::memory_order_release);
    }
    const auto callsite=reinterpret_cast<unsigned>(_ReturnAddress()),context=reinterpret_cast<unsigned>(self),address=U32(context+0x20);
    const bool surfaceDerived=projectionOverrides.skinDecalCaller&&projectionOverrides.sootCaller&&
        (callsite==imageBase+projectionOverrides.skinDecalCaller||callsite==imageBase+projectionOverrides.sootCaller);
    const bool eligibleDerived=surfaceDerived||callsite==imageBase+projectionOverrides.meshCaller||callsite==imageBase+projectionOverrides.terrainCaller||callsite==imageBase+projectionOverrides.edgeCaller;
    std::array<float,16> values{};if(!Read(address,values.data(),64)){
        if(eligibleDerived){derivedFrameFailed.store(true,std::memory_order_release);derivedFailures.fetch_add(1);}return result;}
    if(trackedCameraEvidence.frame.load(std::memory_order_acquire)==stereoExperiment.frame){
        const auto index=projectionCount.fetch_add(1);if(index<projectionEvidence.size()){
            auto& e=projectionEvidence[index];e.context=context;e.address=address;e.callsite=callsite;e.thread=GetCurrentThreadId();e.frame=stereoExperiment.frame;e.gather=gatheringEvidenceEye;e.values=values;}}
    // Only signature-verified mesh, terrain, edge, skin-decal and soot depth-bias paths are eligible. Worker jobs
    // finish at the native gather/render barrier before Prepare/Draw scopes read.
    if(drawingNativeView||!eligibleDerived)return result;
    const auto fail=[](){derivedFrameFailed.store(true,std::memory_order_release);derivedFailures.fetch_add(1);};
    std::array<float,16> left{},right{};std::memcpy(left.data(),tracked.written[0].bytes.data()+0x2e0,64);std::memcpy(right.data(),tracked.right[0].bytes.data()+0x2e0,64);
    const auto replacement=fvr::math::RetargetDepthProjection(values,right,left);
    if(!replacement){if(!fvr::math::RetargetDepthProjection(values,left,left))fail();return result;}
    if(*replacement==values)return result;
    const auto index=derivedCount.fetch_add(1);if(index>=derivedProjections.size()){fail();return result;}
    auto& binding=derivedProjections[index];binding.context=context;binding.address=address;binding.before=values;binding.written=*replacement;
    binding.frame.store(stereoExperiment.frame,std::memory_order_release);
    return result;
}
struct ContextSnapshot {unsigned view=0,projection=0,position=0;std::array<float,36> values{};bool valid=false;};
struct ContextEvidence {unsigned context=0,primary=0,secondary=0,callsite=0,role=UINT32_MAX;fvr::bc2::RenderViewCopy source{};std::array<ContextSnapshot,4> snapshots{};};
std::array<std::array<ContextEvidence,64>,2> contextEvidence;
std::array<unsigned,2> contextCounts{};std::atomic<unsigned> contextEvidenceMask=0;
std::array<std::array<ContextEvidence,64>,2> frameContexts;
std::array<unsigned,2> frameContextCounts{};std::array<bool,2> frameContextOverflow{};
std::atomic<unsigned> contextPatchedScopes=0,contextPatchFailures=0,contextRestoreFailures=0;
unsigned ContextRole(unsigned primary,unsigned eye)noexcept {
    const auto view=eye?stereoExperiment.eye:stereoExperiment.main;
    if(primary==view+layout.primaryOffset)return 0;
    const auto begin=U32(view+0x1680),count=ChildCount(view);
    for(unsigned i=0;i<count;++i){const auto child=U32(U32(begin+i*8)+0xc);
        // Setters are independently verified by DiscoverViewLayout; identify by
        // the same child objects already constructed for this owned main view.
        if(primary==child+layout.primaryOffset&&U32(U32(child)+20)==imageBase+layout.setPrimary)return i+1;
    }return UINT32_MAX;
}
ContextSnapshot ObserveContext(unsigned address)noexcept {
    ContextSnapshot s{};s.view=U32(address+0x1c);s.projection=U32(address+0x20);s.position=U32(address+0x24);
    s.valid=Read(s.view,s.values.data(),64)&&Read(s.projection,s.values.data()+16,64)&&Read(s.position,s.values.data()+32,16);return s;
}
void __fastcall ContextCameraHook(void* self,void*,void* primary,void* secondary){
    RenderCallbackScope callback;
    contextCameraOriginal(self,primary,secondary);
    if(gatheringEvidenceEye<0)return;const auto eye=unsigned(gatheringEvidenceEye);auto& count=frameContextCounts[eye];
    if(count>=frameContexts[eye].size()){frameContextOverflow[eye]=true;return;}auto& e=frameContexts[eye][count++];e={};
    e.context=reinterpret_cast<unsigned>(self);e.primary=reinterpret_cast<unsigned>(primary);e.secondary=reinterpret_cast<unsigned>(secondary);
    e.callsite=reinterpret_cast<unsigned>(_ReturnAddress());e.role=ContextRole(e.primary,eye);
    if(!Read(e.primary,e.source.bytes.data(),e.source.bytes.size()))frameContextOverflow[eye]=true;
    e.snapshots[0]=ObserveContext(e.context);
    if(trackedCameraEvidence.frame.load(std::memory_order_acquire)==stereoExperiment.frame){contextEvidence[eye][count-1]=e;contextCounts[eye]=count;}

}
void ObserveContexts(unsigned step)noexcept {
    if(trackedCameraEvidence.frame.load(std::memory_order_acquire)!=stereoExperiment.frame)return;
    for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<contextCounts[eye];++i)contextEvidence[eye][i].snapshots[step]=ObserveContext(contextEvidence[eye][i].context);
    contextEvidenceMask.fetch_or(1u<<step,std::memory_order_release);
}
bool SameContext(const ContextSnapshot& a,const ContextSnapshot& b)noexcept {
    return a.valid&&b.valid&&a.view==b.view&&a.projection==b.projection&&a.position==b.position&&std::memcmp(a.values.data(),b.values.data(),sizeof(a.values))==0;
}
// The native command builder and GPU draw both consume the last gather's
// bindings. Scope the corresponding left values at both boundaries. Only value
// bytes change; native context/list identities and job scheduling are preserved.
struct ScopedContextEye {
    fvr::runtime::ExactWriteBatch<64*3+256+32*3> writes;bool applied=false;
    bool Apply()noexcept {
        const auto n=frameContextCounts[0];if(!n||n!=frameContextCounts[1]||n>64||frameContextOverflow[0]||frameContextOverflow[1]||contextRestoreFailures.load())return false;
        for(unsigned i=0;i<n;++i){const auto& l=frameContexts[0][i];const auto& r=frameContexts[1][i];
            if(l.role==UINT32_MAX||l.role!=r.role||l.callsite!=r.callsite||l.context==r.context||
               std::memcmp(l.source.bytes.data()+4,r.source.bytes.data()+4,4)||std::memcmp(l.source.bytes.data()+0x1c,r.source.bytes.data()+0x1c,8)||
               !SameContext(ObserveContext(l.context),l.snapshots[0])||!SameContext(ObserveContext(r.context),r.snapshots[0]))return false;
            const auto& from=l.snapshots[0];const auto& to=r.snapshots[0];
            const unsigned addresses[]={to.view,to.projection,to.position},offsets[]={0,64,128},sizes[]={64,64,16};
            for(unsigned part=0;part<3;++part)if(!writes.Stage(
                {reinterpret_cast<std::byte*>(addresses[part]),sizes[part]},
                {reinterpret_cast<const std::byte*>(to.values.data())+offsets[part],sizes[part]},
                {reinterpret_cast<const std::byte*>(from.values.data())+offsets[part],sizes[part]}))return false;
        }
        const auto count=derivedCount.load(std::memory_order_acquire);
        if(derivedFrameFailed.load(std::memory_order_acquire)||count>derivedProjections.size())return false;
        for(unsigned i=0;i<count;++i){const auto& binding=derivedProjections[i];std::array<float,16> observed{};
            if(binding.frame.load(std::memory_order_acquire)!=stereoExperiment.frame||U32(binding.context+0x20)!=binding.address||
               !Read(binding.address,observed.data(),64)||observed!=binding.before)return false;
            if(!writes.Stage({reinterpret_cast<std::byte*>(binding.address),64},
                {reinterpret_cast<const std::byte*>(binding.before.data()),64},{reinterpret_cast<const std::byte*>(binding.written.data()),64}))return false;
        }
        const auto weapons=weaponContextCount.load(std::memory_order_acquire);
        if(weapons>weaponContexts.size()||!weaponContextFailures.CanApply(contextRestoreFailures.load()!=0))return false;
        for(unsigned i=0;i<weapons;++i){const auto& w=weaponContexts[i];if(w.frame.load(std::memory_order_acquire)!=stereoExperiment.frame)return false;
            const unsigned addresses[]={w.view,w.projection,w.position},offsets[]={0,64,128},sizes[]={64,64,16};
            for(unsigned part=0;part<3;++part)if(!writes.Stage({reinterpret_cast<std::byte*>(addresses[part]),sizes[part]},
                {reinterpret_cast<const std::byte*>(w.right.data())+offsets[part],sizes[part]},
                {reinterpret_cast<const std::byte*>(w.left.data())+offsets[part],sizes[part]}))return false;
        }
        applied=writes.Apply();if(applied){contextPatchedScopes.fetch_add(1);derivedWrites.fetch_add(count);}return applied;
    }
    ~ScopedContextEye(){if(applied&&!writes.Restore()){contextRestoreFailures.fetch_add(1);tracked.capturesOkay=false;}}
};

struct EyeGpuEvidence {ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11Query> query;
    D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics{};std::atomic<bool> complete=false;HRESULT error=S_OK;bool begun=false,ended=false;};
std::array<EyeGpuEvidence,2> eyeGpuEvidence;
void BeginEyeGpuEvidence(unsigned eye)noexcept {
    if(trackedCameraEvidence.frame.load(std::memory_order_acquire)!=stereoExperiment.frame)return;
    auto& e=eyeGpuEvidence[eye];if(e.begun||e.complete.load())return;
    const auto renderer=U32(imageBase+profile.rendererGlobal);if(U32(renderer)!=imageBase+profile.rendererVtable)return;
    auto* device=reinterpret_cast<ID3D11Device*>(U32(renderer+0x7c));device->GetImmediateContext(&e.context);
    D3D11_QUERY_DESC description{D3D11_QUERY_PIPELINE_STATISTICS,0};e.error=device->CreateQuery(&description,&e.query);
    if(FAILED(e.error)){e.complete.store(true,std::memory_order_release);return;}
    e.context->Begin(e.query.Get());e.begun=true;
}
void EndEyeGpuEvidence(unsigned eye)noexcept {auto& e=eyeGpuEvidence[eye];if(e.begun&&!e.ended){e.context->End(e.query.Get());e.ended=true;}}
void PollEyeGpuEvidence()noexcept {for(auto& e:eyeGpuEvidence)if(e.ended&&!e.complete.load()){
    e.error=e.context->GetData(e.query.Get(),&e.statistics,sizeof(e.statistics),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(e.error!=S_FALSE){e.query.Reset();e.context.Reset();e.complete.store(true,std::memory_order_release);}
}}
using IndexedDrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
IndexedDrawFn indexedDrawOriginal=nullptr;ID3D11DeviceContext* observedImmediateContext=nullptr;
thread_local int drawingEvidenceEye=-1;
fvr::bc2::Bc2ReloadDrawCapture reloadDrawCapture;
fvr::graphics::D3D11SceneDrawWitness destructionWitness;
bool destructionWitnessEnabled=false;
bool graphicsProvenanceEnabled=false;
fvr::graphics::SceneWitnessKey DestructionWitnessKey()noexcept {
    const auto& lease=tracked.lease;
    return {stereoExperiment.world,lease.requestId,lease.native.frameId,lease.native.deviceEpoch,
        lease.channelGeneration,lease.tracking.generation,lease.tracking.spaceGeneration,lease.tracking.predictedNs,
        stereoExperiment.main,stereoExperiment.eye,lease.sourceSlot,lease.requirements.width,lease.requirements.height,
        lease.native.owner,stereoExperiment.request};
}
std::int64_t reloadDrawFrequency=0;
std::int64_t ReloadDrawNowNs()noexcept {
    if(reloadDrawFrequency<=0)return 0;const auto qpc=Qpc();
    return (qpc/reloadDrawFrequency)*1000000000+(qpc%reloadDrawFrequency)*1000000000/reloadDrawFrequency;
}
std::array<std::array<std::atomic<std::uint64_t>,4>,4> bodySourceReads{};
template<class Reader> auto ReadBodySource(Reader read,unsigned kind)noexcept {
    // A tracking publication may change during a source's validated read.
    // Re-read current evidence with a current validation clock, at most three
    // times. Never reuse an earlier frame or extend a source's own deadline.
    for(unsigned attempt=0;attempt<3;++attempt){
        auto source=read(ReloadDrawNowNs());
        if(source){++bodySourceReads[kind][attempt];return source;}
    }
    ++bodySourceReads[kind][3];return decltype(read(std::int64_t{})){};
}
fvr::bc2::BodyPropSources ReadBodySources()noexcept {
    return {ReadBodySource(fvr::bc2::rigPublication::ReadBodyAmmoRenderSource,0),
        ReadBodySource(fvr::bc2::rigPublication::ReadHolsteredBodyRenderSource,1),
        ReadBodySource(fvr::bc2::rigPublication::ReadCarriedBodyRenderSource,2),
        ReadBodySource(fvr::bc2::rigPublication::ReadStowedAmmoRenderSource,3),
        fvr::bc2::gameplay::ReadVehicleReticle(ReloadDrawNowNs()),
        fvr::bc2::rigPublication::ReadFixedContainerRenderSource(ReloadDrawNowNs())};
}
struct BufferEvidence {ComPtr<ID3D11Buffer> staging;std::vector<unsigned char> bytes;unsigned source=0;bool done=false;};
struct EyeBufferEvidence {bool queued=false;std::atomic<bool> complete=false;std::array<BufferEvidence,14> buffers;ComPtr<ID3D11DeviceContext> context;unsigned indices=0;};
std::array<EyeBufferEvidence,2> eyeBufferEvidence;
void CaptureGeometryBuffers(ID3D11DeviceContext* context,unsigned eye,UINT indices)noexcept {
    auto& evidence=eyeBufferEvidence[eye];if(evidence.queued||indices<300)return;
    UINT count=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&count,&viewport);
    if(count!=1||viewport.Width!=1920||viewport.Height!=1080)return;
    evidence.queued=true;evidence.context=context;evidence.indices=indices;
    try {ComPtr<ID3D11Device> device;context->GetDevice(&device);std::array<ID3D11Buffer*,14> buffers{};context->VSGetConstantBuffers(0,14,buffers.data());
        for(unsigned slot=0;slot<14;++slot){ComPtr<ID3D11Buffer> source;source.Attach(buffers[slot]);if(!source)continue;
            D3D11_BUFFER_DESC d{};source->GetDesc(&d);if(!d.ByteWidth||d.ByteWidth>65536)continue;
            auto& target=evidence.buffers[slot];target.source=reinterpret_cast<unsigned>(source.Get());target.bytes.resize(d.ByteWidth);
            d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;d.StructureByteStride=0;
            if(SUCCEEDED(device->CreateBuffer(&d,nullptr,&target.staging)))context->CopyResource(target.staging.Get(),source.Get());
        }
    }catch(...){}
}
void PollGeometryBuffers()noexcept {for(auto& evidence:eyeBufferEvidence)if(evidence.queued&&!evidence.complete.load()){
    bool done=true;for(auto& buffer:evidence.buffers)if(buffer.staging&&!buffer.done){D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto result=evidence.context->Map(buffer.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(result==DXGI_ERROR_WAS_STILL_DRAWING){done=false;continue;}
        if(SUCCEEDED(result)){std::memcpy(buffer.bytes.data(),mapped.pData,buffer.bytes.size());evidence.context->Unmap(buffer.staging.Get(),0);buffer.done=true;}
        buffer.staging.Reset();
    }if(done){evidence.context.Reset();evidence.complete.store(true,std::memory_order_release);}
}}
// One-frame read-only pass inventory. Copy into private staging buffers and
// poll later: never map a live constant buffer or wait on the graphics thread.
struct PassBufferEvidence {
    unsigned vs=0,ps=0,target=0,depth=0,width=0,height=0,indices=0,sequence=0,kind=0;
    bool complete=false;std::array<BufferEvidence,4> buffers;
    ComPtr<ID3D11DeviceContext> context;
};
std::array<std::array<PassBufferEvidence,512>,2> passEvidence;
std::array<unsigned,2> passCounts{},passSequences{},passOverflow{};
void CapturePassBuffers(ID3D11DeviceContext* context,unsigned eye,UINT count,unsigned kind)noexcept {
    if(!passEvidenceEnabled)return;
    const auto sequence=++passSequences[eye];
    UINT viewportCount=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&viewportCount,&viewport);
    if(viewportCount!=1||viewport.Width!=1920||viewport.Height!=1080)return;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);
    ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(1,&target,&depth);
    const auto v=reinterpret_cast<unsigned>(vs.Get()),p=reinterpret_cast<unsigned>(ps.Get()),
        t=reinterpret_cast<unsigned>(target.Get()),d=reinterpret_cast<unsigned>(depth.Get());
    for(unsigned i=0;i<passCounts[eye];++i){const auto& e=passEvidence[eye][i];
        if(e.vs==v&&e.ps==p&&e.target==t&&e.depth==d)return;}
    if(passCounts[eye]>=passEvidence[eye].size()){++passOverflow[eye];return;}
    auto& e=passEvidence[eye][passCounts[eye]++];e.vs=v;e.ps=p;e.target=t;e.depth=d;
    e.width=unsigned(viewport.Width);e.height=unsigned(viewport.Height);e.indices=count;e.sequence=sequence;e.kind=kind;e.context=context;
    try {ComPtr<ID3D11Device> device;context->GetDevice(&device);std::array<ID3D11Buffer*,4> sources{};
        context->VSGetConstantBuffers(0,2,sources.data());context->PSGetConstantBuffers(0,2,sources.data()+2);
        std::array<ComPtr<ID3D11Buffer>,4> owned;for(unsigned i=0;i<4;++i)owned[i].Attach(sources[i]);
        for(unsigned i=0;i<4;++i){if(!owned[i])continue;auto& b=e.buffers[i];D3D11_BUFFER_DESC desc{};owned[i]->GetDesc(&desc);
            if(!desc.ByteWidth||desc.ByteWidth>65536)continue;b.source=reinterpret_cast<unsigned>(owned[i].Get());b.bytes.resize(desc.ByteWidth);
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
            if(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&b.staging)))context->CopyResource(b.staging.Get(),owned[i].Get());
        }
    }catch(...){}
}
void PollPassBuffers()noexcept {for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<passCounts[eye];++i){auto& e=passEvidence[eye][i];if(e.complete)continue;
    bool done=true;for(auto& b:e.buffers)if(b.staging&&!b.done){D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto result=e.context->Map(b.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(result==DXGI_ERROR_WAS_STILL_DRAWING){done=false;continue;}
        if(SUCCEEDED(result)){std::memcpy(b.bytes.data(),mapped.pData,b.bytes.size());e.context->Unmap(b.staging.Get(),0);b.done=true;}
        b.staging.Reset();
    }if(done){e.context.Reset();e.complete=true;}
}}
using NonIndexedDrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
NonIndexedDrawFn nonIndexedDrawOriginal=nullptr;
std::optional<fvr::bc2::opticFilterRuntime::Source> ReadOpticSource()noexcept {
    fvr::interaction::InputFrame input{};
    if(frameBridge.ReadInput(input)!=fvr::ipc::ChannelResult::Ok||!input.focused||!input.headValid)return {};
    const auto s=fvr::bc2::gameplay::ReadCurrentSelectedMeshes(ReloadDrawNowNs());
    if(!s||s->owner.space!=input.spaceGeneration||input.generation<s->sequence)return {};
    return fvr::bc2::opticFilterRuntime::Source{s->owner,s};
}
fvr::bc2::ReloadReticleDrawCurrent ReadReticleDrawCurrent(const fvr::bc2::ReloadDrawFrameEvidence& f)noexcept {
    fvr::bc2::ReloadReticleDrawCurrent out;
    if(f.world>UINT32_MAX-0x88||f.request>UINT32_MAX-8||f.view>UINT32_MAX-layout.primaryOffset-0x460)return out;
    const auto world=unsigned(f.world),request=unsigned(f.request),view=unsigned(f.view);
    const auto exact=[&]{return streamMode&&tracked.state.load(std::memory_order_acquire)==1&&f.eye<2&&
        f.world==stereoExperiment.world&&f.request==stereoExperiment.request&&f.nativeFrame==stereoExperiment.frame&&
        f.view==(f.eye?stereoExperiment.eye:stereoExperiment.main)&&U32(world+0x88)==f.nativeFrame&&
        U32(view)==imageBase+layout.vtable&&U32(view+0x70)==f.request&&U32(request+8)==f.world;};
    if(!exact())return out;
    const auto owner=fvr::bc2::rigPublication::ReadReloadProducerOwner();if(!owner)return out;
    const auto equipment=fvr::bc2::rigPublication::ReadReticleEquipmentGeneration();if(!equipment)return out;
    out.nowNs=ReloadDrawNowNs();out.selected=fvr::bc2::gameplay::ReadCurrentSelectedMeshes(out.nowNs);
    // Read the actual owned eye transform at this candidate's DrawIndexed,
    // not the centre eye or a later tracking publication.
    fvr::engine::FrostbiteCameraInput camera{};camera.worldUnitsPerMeter=1;
    if(Read(view+layout.primaryOffset+0x50,&camera.transform,64)&&
       Read(view+layout.primaryOffset+0x1c,&camera.nearPlane,4)&&Read(view+layout.primaryOffset+0x20,&camera.farPlane,4)){
        if(const auto canonical=fvr::engine::CanonicalCamera(camera)){
            const auto& t=canonical->camera.values[3];out.eyeCanonicalLh={t[0],t[1],t[2]};out.eyeWorldValid=true;
        }
    }
    if(!exact()||fvr::bc2::rigPublication::ReadReloadProducerOwner()!=owner||
       fvr::bc2::rigPublication::ReadReticleEquipmentGeneration()!=equipment)return {};
    out.world=f.world;out.request=f.request;out.view=f.view;out.nativeFrame=f.nativeFrame;out.eye=f.eye;
    out.physicalEquipmentGeneration=equipment;
    out.actor=owner->actor;out.weak=owner->weak;out.weapon=owner->weapon;out.ownerGeneration=owner->ownerGeneration;out.space=owner->space;
    return out;
}
thread_local std::optional<fvr::bc2::opticFilterRuntime::View> opticDrawView;
struct OpticViewScope {
    std::optional<fvr::bc2::opticFilterRuntime::View> previous;
    explicit OpticViewScope(std::optional<fvr::bc2::opticFilterRuntime::View> next):previous(opticDrawView){opticDrawView=next;}
    ~OpticViewScope(){opticDrawView=previous;}
};
#ifdef FVR_BC2_BODY_SCENE_DEPTH
// The measured BC2 pass clears one full-resolution D24S8 allocation for
// world, then again for first-person geometry. This is a scoped observation,
// not a draw-count signature. Any different sequence omits the attachment.
struct BodySceneDepthEye {
    fvr::ipc::FrameLease lease{};unsigned eye=0,clears=0;
    bool invalid=false;ComPtr<ID3D11Texture2D> source;
    bool observing=false;std::optional<fvr::graphics::DepthEyeFailureRow> diagnostic;
    fvr::graphics::DeferredDepthAdmission admission;
};
thread_local BodySceneDepthEye* bodySceneDepthEye=nullptr;
std::atomic<unsigned> bodyDepthBegins=0,bodyDepthEnds=0,bodyDepthRejected=0;
fvr::graphics::FirstDepthEyeFailure firstBodyDepthFailure;
using ClearDepthFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);
ClearDepthFn clearDepthOriginal=nullptr;
fvr::graphics::DepthAdmissionKey BodyDepthAdmissionKey(const fvr::ipc::FrameLease& lease)noexcept {
    return {lease.channelGeneration,lease.requestId,lease.native.owner,lease.native.frameId,lease.native.deviceEpoch,
        lease.tracking.generation,lease.tracking.spaceGeneration,lease.tracking.predictedNs,lease.sourceSlot,lease.sourceSlotCount};
}
fvr::graphics::DepthAdmissionResult BeginBodyDepth(const fvr::ipc::FrameLease& lease)noexcept {
    using Result=fvr::graphics::DepthAdmissionResult;
    auto prepared=fvr::graphics::RestoredPublicationResult::NotAttempted;
    if(frameBridge.BeginDepthCapture(lease,false,&prepared))return Result::Ready;
    return prepared==fvr::graphics::RestoredPublicationResult::PrepareBusy?Result::Busy:Result::Rejected;
}
bool CaptureBodySceneDepth(BodySceneDepthEye& e,unsigned stage,ID3D11Texture2D* source)noexcept {
    if(e.observing)e.diagnostic->captureAttemptedMask|=1u<<stage;
    bool admitted=true;
    if(e.eye==0&&stage==0){
        const bool current=tracked.state.load(std::memory_order_acquire)==1&&
            ((std::uint64_t(stereoExperiment.world)<<32)|stereoExperiment.request)==e.lease.native.owner&&
            U32(stereoExperiment.world+0x88)==e.lease.native.frameId;
        const auto key=current?BodyDepthAdmissionKey(tracked.lease):fvr::graphics::DepthAdmissionKey{};
        admitted=e.admission.BeforeWorldCopy(key,[&]()noexcept{return BeginBodyDepth(e.lease);});
    }
    const bool captured=admitted&&frameBridge.CaptureDepth(e.eye,stage,source,e.lease);
    if(e.observing){
        if(captured)e.diagnostic->captureAcceptedMask|=1u<<stage;
        else {e.diagnostic->bridgeFailureStage=frameBridge.FailureStage();e.diagnostic->bridgeFailureCode=frameBridge.FailureCode();}
    }
    return captured;
}
void STDMETHODCALLTYPE ClearBodySceneDepthHook(ID3D11DeviceContext* context,ID3D11DepthStencilView* dsv,UINT flags,FLOAT depth,UINT8 stencil){
    fvr::graphics::callProvenance::Scope provenance(fvr::graphics::callProvenance::Kind::ClearDepth,
        nullptr,context,nullptr,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    auto* e=bodySceneDepthEye;firstBodyDepthFailure.Hook(e!=nullptr);
    auto* observation=e&&e->observing?&*e->diagnostic:nullptr;
    fvr::graphics::DepthClearSample sample{};
    using Filter=fvr::graphics::DepthClearFilter;using Failure=fvr::graphics::DepthClearFailure;
    if(observation){
        sample.context=reinterpret_cast<std::uintptr_t>(context);sample.view=reinterpret_cast<std::uintptr_t>(dsv);
        sample.flags=flags;sample.stencil=stencil;std::memcpy(&sample.depthBits,&depth,sizeof(depth));
        if(!observation->scopedHooks++)observation->firstHook=sample;
        if(e->invalid)observation->Filter(Filter::InvalidScope,sample);
        else if(context!=observedImmediateContext)observation->Filter(Filter::Context,sample);
        else if(!dsv)observation->Filter(Filter::NullView,sample);
    }
    if(e&&!e->invalid&&context==observedImmediateContext&&dsv){
        ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> source;dsv->GetResource(&resource);
        if(SUCCEEDED(resource.As(&source))){
            D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
            if(observation){
                sample.source=reinterpret_cast<std::uintptr_t>(source.Get());sample.hasDescriptor=true;
                sample.width=d.Width;sample.height=d.Height;sample.format=unsigned(d.Format);
                sample.samples=d.SampleDesc.Count;sample.quality=d.SampleDesc.Quality;sample.mips=d.MipLevels;sample.array=d.ArraySize;
                if(!observation->firstTexture.hasDescriptor)observation->firstTexture=sample;
                if(d.Width!=e->lease.requirements.width||d.Height!=e->lease.requirements.height)observation->Filter(Filter::Dimensions,sample);
                else if(d.Format!=DXGI_FORMAT_R24G8_TYPELESS)observation->Filter(Filter::Format,sample);
            }
            if(d.Width==e->lease.requirements.width&&d.Height==e->lease.requirements.height&&d.Format==DXGI_FORMAT_R24G8_TYPELESS){
                if(observation)++observation->matchingHooks;
                // Keep the original short circuit and increment order. The
                // diagnostic never changes which clear or source is accepted.
                if(flags!=(D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL)||depth!=1.f||stencil||
                   ++e->clears>2||(e->source&&e->source.Get()!=source.Get())){
                    if(observation){
                        observation->invalidClear=sample;
                        observation->invalidCause=flags!=(D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL)?Failure::Flags:
                            depth!=1.f?Failure::DepthValue:stencil?Failure::Stencil:e->clears>2?Failure::TooManyClears:Failure::SourceChanged;
                    }
                    e->invalid=true;
                }
                else if(e->clears==1)e->source=source;
                else if(!CaptureBodySceneDepth(*e,0,source.Get())){
                    if(observation){observation->invalidClear=sample;observation->invalidCause=Failure::WorldCapture;}
                    e->invalid=true;
                }
            }
        }else if(observation)observation->Filter(Filter::NotTexture,sample);
    }
    clearDepthOriginal(context,dsv,flags,depth,stencil);
}
struct BodySceneDepthScope {
    BodySceneDepthEye state;BodySceneDepthEye* previous=bodySceneDepthEye;bool active=false;
    BodySceneDepthScope(bool owned,unsigned eye){
        bodySceneDepthEye=nullptr;
        if(!owned)return;
        state.lease=tracked.lease;state.eye=eye;
        if(eye==0&&!state.admission.Start(BodyDepthAdmissionKey(state.lease),
            [&]()noexcept{return BeginBodyDepth(state.lease);}))return;
        state.observing=firstBodyDepthFailure.Pending();
        if(state.observing){
            const auto error=GetLastError();auto& d=state.diagnostic.emplace();const auto& lease=state.lease;
            d.key={lease.channelGeneration,lease.requestId,lease.native.owner,lease.native.frameId,lease.native.deviceEpoch,
                lease.tracking.generation,lease.tracking.spaceGeneration,lease.tracking.predictedNs};
            d.slot=lease.sourceSlot;d.slotCount=lease.sourceSlotCount;d.eye=eye;
            d.width=lease.requirements.width;d.height=lease.requirements.height;
            d.thread=GetCurrentThreadId();d.expectedThread=GraphicsBootstrapThreadForDiagnostics();
            d.expectedContext=reinterpret_cast<std::uintptr_t>(observedImmediateContext);d.beginQpc=Qpc();
            firstBodyDepthFailure.Begin(d);SetLastError(error);
        }
        active=true;bodySceneDepthEye=&state;++bodyDepthBegins;
    }
    void End()noexcept {
        if(!active)return;active=false;bodySceneDepthEye=previous;
        const auto test=[&](unsigned bit,bool value)noexcept {
            return state.observing?state.diagnostic->Test(bit,value):value;
        };
        if(test(0,!state.invalid)&&test(1,state.clears==2)&&test(2,bool(state.source))&&
           test(3,tracked.state.load(std::memory_order_acquire)==1)&&
           test(4,tracked.lease.requestId==state.lease.requestId)&&
           test(5,U32(stereoExperiment.world+0x88)==state.lease.native.frameId)&&
           test(6,CaptureBodySceneDepth(state,1,state.source.Get())))++bodyDepthEnds;
        else {
            ++bodyDepthRejected;
            if(state.observing){const auto error=GetLastError();auto& d=*state.diagnostic;
                d.endQpc=Qpc();d.clears=state.clears;d.retainedSource=reinterpret_cast<std::uintptr_t>(state.source.Get());
                firstBodyDepthFailure.Observe(d);SetLastError(error);
            }
        }
        state.source.Reset();
    }
    ~BodySceneDepthScope(){bodySceneDepthEye=previous;}
};
#endif
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
void ObserveSceneDepthCandidate(ID3D11DeviceContext* context,unsigned kind)noexcept;
#endif
void STDMETHODCALLTYPE NonIndexedDrawHook(ID3D11DeviceContext* context,UINT count,UINT start){
    fvr::graphics::callProvenance::Scope provenance(fvr::graphics::callProvenance::Kind::Draw,
        nullptr,context,nullptr,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    if(destructionWitnessEnabled)destructionWitness.Observe(context,{fvr::graphics::SceneWitnessDrawKind::Vertices,count,1,start});
    if(opticEvidenceEnabled)fvr::bc2::opticFilterRuntime::CaptureDraw(context,false,count,start);
    if(context==observedImmediateContext&&drawingEvidenceEye>=0)CapturePassBuffers(context,unsigned(drawingEvidenceEye),count,0);
    nonIndexedDrawOriginal(context,count,start);
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
    ObserveSceneDepthCandidate(context,1);
#endif
}
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
void ObserveSceneDepthCandidate(ID3D11DeviceContext* context,unsigned kind)noexcept {
    if(context!=observedImmediateContext||sceneDepthDrawingEye<0||sceneDepthDrawingEye>1||
       !sceneDepthFrame||sceneDepthFrame!=stereoExperiment.frame)return;
    const unsigned eye=unsigned(sceneDepthDrawingEye);
    auto& calls=sceneDepthCalls[eye];
    if(kind==0)++calls.indexed;else if(kind==1)++calls.nonIndexed;else if(kind==2)++calls.indexedInstanced;else ++calls.instanced;
    ComPtr<ID3D11DepthStencilView> dsv;context->OMGetRenderTargets(0,nullptr,&dsv);if(!dsv){++calls.noDepth;return;}
    ComPtr<ID3D11Resource> resource;dsv->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture)))return;D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(unsigned(desc.Format)<calls.formats.size())++calls.formats[desc.Format];
    if(desc.Format!=DXGI_FORMAT_R24G8_TYPELESS&&desc.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT&&
       desc.Format!=DXGI_FORMAT_R32_TYPELESS&&desc.Format!=DXGI_FORMAT_D32_FLOAT){++calls.unsupported;return;}
    if(desc.Width!=1920||desc.Height!=1080||desc.SampleDesc.Count!=1){++calls.wrongSize;return;}
    D3D11_VIEWPORT viewport{};UINT count=1;context->RSGetViewports(&count,&viewport);if(count!=1){++calls.wrongViewport;return;}
    D3D11_DEPTH_STENCIL_DESC state{};ComPtr<ID3D11DepthStencilState> ds;UINT stencil=0;
    context->OMGetDepthStencilState(&ds,&stencil);if(ds)ds->GetDesc(&state);
    else {state.DepthEnable=TRUE;state.DepthFunc=D3D11_COMPARISON_LESS;state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;}
    const auto id=reinterpret_cast<std::uintptr_t>(texture.Get());
    for(unsigned n=0;n<sceneDepthCandidateCounts[eye];++n){auto& c=sceneDepthCandidates[eye][n];
        if(c.resource==id&&std::memcmp(&c.viewport,&viewport,sizeof(viewport))==0){++c.draws;return;}}
    if(sceneDepthCandidateCounts[eye]==sceneDepthCandidates[eye].size()){++sceneDepthCandidateOverflow[eye];return;}
    auto& c=sceneDepthCandidates[eye][sceneDepthCandidateCounts[eye]++];c.texture=texture;c.resource=id;
    c.viewport=viewport;c.draws=1;c.depthFunction=state.DepthFunc;c.depthWrite=state.DepthWriteMask;
}
#endif
using IndexedInstancedDrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using InstancedDrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
IndexedInstancedDrawFn indexedInstancedDrawOriginal=nullptr;
InstancedDrawFn instancedDrawOriginal=nullptr;
void STDMETHODCALLTYPE IndexedInstancedDrawHook(ID3D11DeviceContext* context,UINT count,UINT instances,UINT start,INT base,UINT firstInstance){
    if(destructionWitnessEnabled)destructionWitness.Observe(context,{fvr::graphics::SceneWitnessDrawKind::IndexedInstanced,count,instances,start,firstInstance,base});
    indexedInstancedDrawOriginal(context,count,instances,start,base,firstInstance);
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
    ObserveSceneDepthCandidate(context,2);
#endif
}
void STDMETHODCALLTYPE InstancedDrawHook(ID3D11DeviceContext* context,UINT count,UINT instances,UINT start,UINT firstInstance){
    if(destructionWitnessEnabled)destructionWitness.Observe(context,{fvr::graphics::SceneWitnessDrawKind::Instanced,count,instances,start,firstInstance});
    instancedDrawOriginal(context,count,instances,start,firstInstance);
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
    ObserveSceneDepthCandidate(context,3);
#endif
}
using DrawAutoFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using DrawIndirectFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);
DrawAutoFn drawAutoOriginal=nullptr;
DrawIndirectFn indexedIndirectOriginal=nullptr,indirectOriginal=nullptr;
void STDMETHODCALLTYPE DrawAutoHook(ID3D11DeviceContext* context){
    destructionWitness.Observe(context,{fvr::graphics::SceneWitnessDrawKind::Auto});drawAutoOriginal(context);
}
void STDMETHODCALLTYPE IndexedIndirectHook(ID3D11DeviceContext* context,ID3D11Buffer* args,UINT offset){
    destructionWitness.Observe(context,{fvr::graphics::SceneWitnessDrawKind::IndexedIndirect,0,0,0,0,0,reinterpret_cast<std::uintptr_t>(args),offset});
    indexedIndirectOriginal(context,args,offset);
}
void STDMETHODCALLTYPE IndirectHook(ID3D11DeviceContext* context,ID3D11Buffer* args,UINT offset){
    destructionWitness.Observe(context,{fvr::graphics::SceneWitnessDrawKind::Indirect,0,0,0,0,0,reinterpret_cast<std::uintptr_t>(args),offset});
    indirectOriginal(context,args,offset);
}
void STDMETHODCALLTYPE IndexedDrawHook(ID3D11DeviceContext* context,UINT count,UINT start,INT base){
    fvr::graphics::callProvenance::Scope provenance(fvr::graphics::callProvenance::Kind::Indexed,
        nullptr,context,nullptr,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    if(destructionWitnessEnabled)destructionWitness.Observe(context,{fvr::graphics::SceneWitnessDrawKind::Indexed,count,1,start,0,base});
#if defined(FVR_BC2_ORDINARY_RESOURCE_SCENE_PROBE)
    fvr::bc2::graphicsThreadObservation::Scope graphicsThread(fvr::bc2::graphicsThreadObservation::Boundary::Indexed,context==observedImmediateContext);
#endif
    if(opticEvidenceEnabled)fvr::bc2::opticFilterRuntime::CaptureDraw(context,true,count,start,base);
    if(context==observedImmediateContext)reloadDrawCapture.ObserveIndexed(context,count,start,base);
    if(context==observedImmediateContext&&drawingEvidenceEye>=0){CaptureGeometryBuffers(context,unsigned(drawingEvidenceEye),count);CapturePassBuffers(context,unsigned(drawingEvidenceEye),count,1);}
    indexedDrawOriginal(context,count,start,base);
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
    ObserveSceneDepthCandidate(context,0);
#endif
}
void SaveTrackedCameraEvidence(unsigned view,unsigned eye,unsigned step)noexcept {
    if(trackedCameraEvidence.frame.load(std::memory_order_acquire)!=stereoExperiment.frame)return;
    const unsigned offsets[]={layout.primaryOffset,layout.secondaryOffset,layout.thirdOffset,layout.fourthOffset};
    for(unsigned block=0;block<4;++block)if(!Read(view+offsets[block],trackedCameraEvidence.cameras[eye][step][block].bytes.data(),0x460))return;
    trackedCameraEvidence.mask.fetch_or(1u<<(eye*3+step),std::memory_order_release);
}
void BeginTrackedFrame(unsigned incomingFrame)noexcept {
    auto& e=stereoExperiment;
    // A successful native rebuild does not retire the host's old GPU sources.
    // Keep input alive while the graphics owner drains those exact leases.
    if(!stereoRecovery.sourceTransition.Allows({e.world,e.request}))return;
    const auto prior=tracked.state.load(std::memory_order_acquire);
    if(prior==1||prior==3){if(prior==3)stereoRecovery.cancelUnwrittenRequest.store(tracked.lease.requestId,std::memory_order_release);
        RecordStereoProgress(fvr::bc2::StereoProgressReason::BeginActiveTransaction,incomingFrame);return;}
    const fvr::bc2::StereoFrameAdmissionBinding binding{unsigned(imageBase+profile.gameRendererGlobal),
        stereoRecovery.subsystemType,stereoRecovery.worldType,unsigned(imageBase+lifecycle.requestVtable),unsigned(imageBase+layout.vtable)};
    fvr::bc2::StereoFrameAdmissionSnapshot admission{};
    const auto reason=fvr::bc2::InspectStereoFrameAdmission(binding,{e.world,e.request,e.main,e.eye},
        [](unsigned at,void* dst,std::size_t bytes)noexcept{return Read(at,dst,bytes);},admission);
    stereoFrameAdmissionEvidence.Record(reason,admission);
    // A retained old-owner callback must not activate an eye after the native
    // subsystem has published a verified replacement. Leave existing written
    // transactions and native calls alone; the new owner's ordinary Update
    // still reaches the existing detached recovery before this function.
    // Same-request main/registry changes remain diagnostic-only until their
    // separate native rebind is proved. No actor-loss or timer gate is added.
    if(fvr::bc2::BlocksStereoFrameAdmission(reason))return;
    reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(reinterpret_cast<void*>(e.eye),false);
    e.drawMask.store(0,std::memory_order_release);e.visibilityMask.store(0,std::memory_order_release);e.frame.store(incomingFrame,std::memory_order_release);
    if(tracked.restoreFailures.load()||contextRestoreFailures.load()||(stereoColorScalePair.counts.restoreFailures||stereoEffectPair.counts.restoreFailures||stereoPostProcessPair.RestoreFailed())){RecordStereoProgress(fvr::bc2::StereoProgressReason::BeginRestoreFailure,incomingFrame);return;}
    fvr::ipc::FrameLease lease{};if(!frameBridge.TryBegin({(std::uint64_t(e.world)<<32)|e.request,incomingFrame,1},lease)){RecordStereoProgress(fvr::bc2::StereoProgressReason::BridgeNoLease,incomingFrame);return;}
    weaponContextCount.store(0);weaponContextFailures.BeginFrame();derivedCount.store(0);derivedFrameFailed.store(false);for(auto& binding:derivedProjections)binding.frame.store(0);
    tracked.lease=lease;tracked.bodyPropPair.Reset();stereoColorScalePair.Reset();stereoEffectPair.Reset();stereoPostProcessPair.Reset();tracked.capturesOkay=true;tracked.state.store(3,std::memory_order_release);
    std::array<unsigned,4> viewport{};if(!Read(e.main+layout.viewportOffset,viewport.data(),16)){stereoRecovery.cancelUnwrittenRequest.store(lease.requestId,std::memory_order_release);RecordStereoProgress(fvr::bc2::StereoProgressReason::ViewportUnreadable,incomingFrame);return;}
    if(viewport[2]!=lease.requirements.width||viewport[3]!=lease.requirements.height){stereoRecovery.cancelUnwrittenRequest.store(lease.requestId,std::memory_order_release);RecordStereoProgress(fvr::bc2::StereoProgressReason::ViewportSizeChanged,incomingFrame);return;}
    // Acquire before native update, but apply after its per-view far/LOD edits.
    // WorldUpdate bb4b4f..bb4cb6 precedes the verified visibility callback.
    // The extra view must be active before that native iteration reaches it.
    reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(reinterpret_cast<void*>(e.eye),true);
    RecordStereoProgress(fvr::bc2::StereoProgressReason::BeginAccepted,incomingFrame);
}
void ApplyTrackedView(unsigned world,unsigned request,unsigned view)noexcept {
    auto& e=stereoExperiment;
    if(!streamMode||world!=e.world||request!=e.request||U32(world+0x88)!=e.frame||
       U32(view)!=imageBase+layout.vtable||U32(view+0x70)!=request)return;
    if(view==e.main&&tracked.state.load(std::memory_order_acquire)==3){
        const auto cancel=[&]{reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(reinterpret_cast<void*>(e.eye),false);};
        if(!fvr::runtime::ApplyUnwrittenFrame(stereoRecovery.cancelUnwrittenRequest.load(std::memory_order_acquire),tracked.lease.requestId,[&]()noexcept {
        for(unsigned i=0;i<2;++i)if(!Read(e.main+(i?layout.secondaryOffset:layout.primaryOffset),tracked.saved[i].bytes.data(),0x460)){cancel();return;}
        if(privatePresentationExperiment.Enabled())privatePresentationCameras.Observe(privatePresentationExperiment.Phase(),tracked.saved[0],tracked.lease);
        auto source=tracked.saved;if(!fvr::bc2::gameplay::AdjustViewBase(source,tracked.lease.tracking)){cancel();return;}
        auto plan=fvr::bc2::BuildTrackedViews(source,tracked.lease.tracking,tracked.lease.tracking.worldUnitsPerMeter);if(!plan){cancel();return;}
        for(auto& eye:plan->camera)for(auto& camera:eye)fvr::bc2::PrecomputeCameraCaches(camera,updateView,updateProjection,updateFrustum,updateScale);
        vehicleCameraEvidenceFrame.store(fvr::bc2::gameplay::ObserveVehicleCameraPlan(e.frame,tracked.lease.tracking,tracked.saved[0],source[0],{plan->camera[0][0],plan->camera[1][0]})?e.frame.load():0,std::memory_order_release);
        if(weatherObservationEnabled)weatherGate.Begin(GetTickCount64(),CurrentWeatherKey());
        if(!trackedCameraEvidence.frame.load(std::memory_order_acquire)&&
           (!weatherObservationEnabled||weatherGate.Active(CurrentWeatherKey())))trackedCameraEvidence.frame.store(e.frame,std::memory_order_release);
        tracked.anchorWritten=false;
        if(viewAnchorMode){
            if(!Read(e.main+0x30,tracked.savedAnchor.data(),64)){cancel();return;}
            using Setter=void(__thiscall*)(void*,const void*);
            tracked.writtenAnchor=tracked.savedAnchor;
            for(unsigned row=0;row<4;++row)std::memcpy(tracked.writtenAnchor.data()+row*4,plan->camera[0][0].bytes.data()+0x50+row*16,12);
            reinterpret_cast<Setter>(imageBase+viewAnchor.setter)(reinterpret_cast<void*>(e.main),tracked.writtenAnchor.data());tracked.anchorWritten=true;
        }
        tracked.right=plan->camera[1];SetDiagnosticCameras(e.main,plan->camera[0]);
        for(unsigned i=0;i<2;++i)Read(e.main+(i?layout.secondaryOffset:layout.primaryOffset),tracked.written[i].bytes.data(),0x460);
        if(!e.framesRendered.load(std::memory_order_acquire)){e.firstFrame=e.frame;e.initialColorFrame=e.frame;}
        if(vehicleCameraEvidenceFrame.load(std::memory_order_acquire)==e.frame.load()){fvr::bc2::RenderViewCopy actualLeft{};if(Read(e.main+layout.primaryOffset,actualLeft.bytes.data(),0x460))fvr::bc2::gameplay::ObserveVehicleCameraApplied(e.frame,0,actualLeft);}
        SaveTrackedCameraEvidence(e.main,0,0);
        tracked.state.store(1,std::memory_order_release);
        BeginWeatherRuntime();
        }))cancel();
    }else if(view==e.eye&&tracked.state.load(std::memory_order_acquire)==1){
        // Apply the right camera at the same post-clamp boundary. This also
        // preserves the conservative culling extent from the owned stereo plan.
        SetDiagnosticCameras(e.eye,tracked.right);SaveTrackedCameraEvidence(e.eye,1,0);
        if(vehicleCameraEvidenceFrame.load(std::memory_order_acquire)==e.frame.load()){fvr::bc2::RenderViewCopy actualRight{};if(Read(e.eye+layout.primaryOffset,actualRight.bytes.data(),0x460))fvr::bc2::gameplay::ObserveVehicleCameraApplied(e.frame,1,actualRight);}
    }
}

void FinishTrackedFrame(bool completePair,TrackedFinishCallsite callsite)noexcept {
    const auto state=tracked.state.load(std::memory_order_acquire);if(state!=1&&state!=3)return;auto& e=stereoExperiment;bool restored=state==3;
    EndWeatherRuntime();
    const auto milestone=sceneTrace::Native(SceneMilestoneKind::FinishTrackedFrame,SceneMilestoneKind::FinishTrackedFrame,e.world,e.request);
    fvr::runtime::SceneMilestoneContext milestoneResult{};milestoneResult.outcome=(state<<8)|(completePair?1u:0u);
    if(state==1){restored=U32(e.main)==imageBase+layout.vtable&&U32(e.main+0x70)==e.request&&U32(e.world+0x88)==tracked.lease.native.frameId;
        if(restored)for(unsigned i=0;i<2;++i)restored&=Hash(e.main+(i?layout.secondaryOffset:layout.primaryOffset),0x460)==Hash(reinterpret_cast<std::uintptr_t>(tracked.written[i].bytes.data()),0x460);
        if(restored)for(unsigned i=0;i<2;++i)std::memcpy(reinterpret_cast<void*>(e.main+(i?layout.secondaryOffset:layout.primaryOffset)),tracked.saved[i].bytes.data(),0x460);
        else {for(unsigned i=0;i<2;++i)Read(e.main+(i?layout.secondaryOffset:layout.primaryOffset),tracked.observed[i].bytes.data(),0x460);tracked.restoreFailures.fetch_add(1,std::memory_order_release);}
        if(tracked.anchorWritten){
            std::array<float,16> observedAnchor{};
            const bool exact=U32(e.main)==imageBase+layout.vtable&&U32(e.main+0x70)==e.request&&
                Read(e.main+0x30,observedAnchor.data(),64)&&observedAnchor==tracked.writtenAnchor;
            if(exact)std::memcpy(reinterpret_cast<void*>(e.main+0x30),tracked.savedAnchor.data(),64);
            else {restored=false;tracked.restoreFailures.fetch_add(1,std::memory_order_release);}
            tracked.anchorWritten=false;
        }
    }
    // Observe the existing short-circuit predicates exactly once and in their
    // original order. No extra native read is made for a skipped predicate.
    unsigned evaluated=0,passed=0,visibility=0;
    const auto check=[&](unsigned bit,bool value)noexcept {evaluated|=1u<<bit;if(value)passed|=1u<<bit;return value;};
    auto admission=fvr::graphics::RestoredPublicationResult::NotAttempted;
    const bool published=check(0,completePair)&&check(1,restored)&&check(2,tracked.capturesOkay)&&
        check(3,stereoColorScalePair.Ready(CurrentColorScaleKey(e.world,e.request)))&&
        check(4,stereoEffectPair.Ready(CurrentColorScaleKey(e.world,e.request)))&&
        check(5,stereoPostProcessPair.Ready(CurrentColorScaleKey(e.world,e.request)))&&
        check(6,(visibility=e.visibilityMask.load(std::memory_order_acquire))==3)&&
        check(7,frameBridge.PublishRestored(tracked.lease,&admission));
    if(!published){
        firstTrackedFinishFailure.Observe(tracked.lease,callsite,state,evaluated,passed,visibility,admission,GraphicsBootstrapThreadForDiagnostics());
        frameBridge.Cancel(tracked.lease);
    }
    if(weatherObservationEnabled)weatherGate.Finish(CurrentWeatherKey(),published);
    milestoneResult.outcome|=(restored?2u:0u)|(tracked.capturesOkay?4u:0u)|(published?8u:0u);
    tracked.bodyPropPair.Reset();
    tracked.state.store(2,std::memory_order_release);
    sceneTrace::Mark(milestone,SceneMilestoneStage::Exit,milestoneResult);
}
void PumpFrameBridge()noexcept {
    bodyAmmoRenderer.PumpStop();
    if(destructionWitnessEnabled)destructionWitness.Poll(observedImmediateContext);
    if(!streamMode)return;PollEyeGpuEvidence();PollGeometryBuffers();PollPassBuffers();
    if(weatherObservationEnabled)for(auto& capture:weatherColorCaptures)
        if(capture.step==ColorCapture::Step::Pending)CaptureWorldColor(capture,capture.frame);
    fvr::runtime::DrainUnwrittenFrameCancellation(stereoRecovery.cancelUnwrittenRequest,tracked.lease.requestId,
        tracked.state.load(std::memory_order_acquire)==3,[]{FinishTrackedFrame(false,TrackedFinishCallsite::UnwrittenCancellation);});
    if(!frameBridge.Connected()){
        static bool attempted=false;if(attempted)return;attempted=true;
        const auto renderer=U32(imageBase+profile.rendererGlobal);if(U32(renderer)!=imageBase+profile.rendererVtable)return;
        const auto device=U32(renderer+0x7c);frameBridge.ConnectGraphics(reinterpret_cast<ID3D11Device*>(device),streamToken);
    }
    frameBridge.PumpGraphics();
    stereoRecovery.sourceTransition.Service(frameBridge.OnGraphicsThread(),[](const fvr::runtime::RenderOwnerTransition::Request& request)noexcept {
        const auto oldOwner=(request.oldOwner.world<<32)|request.oldOwner.request;
        const auto newOwner=(request.newOwner.world<<32)|request.newOwner.request;
        const auto result=frameBridge.TransitionWorldOwnerGraphics(
            {oldOwner,request.frame,request.deviceEpoch},{newOwner,request.frame,request.deviceEpoch});
        if(result==fvr::ipc::ChannelResult::Busy){++stereoRecovery.sourceTransitionWaits;return fvr::runtime::OwnerTransitionResult::Busy;}
        if(result==fvr::ipc::ChannelResult::Ok){++stereoRecovery.sourceTransitions;return fvr::runtime::OwnerTransitionResult::Complete;}
        ++stereoRecovery.sourceTransitionFailures;return fvr::runtime::OwnerTransitionResult::Failed;
    });
    fvr::interaction::InputFrame controls{};
    const auto controlsResult=frameBridge.ReadInput(controls);
    if(opticEvidenceEnabled&&controlsResult!=fvr::ipc::ChannelResult::Busy)
        fvr::bc2::opticFilterRuntime::SetConnected(controlsResult==fvr::ipc::ChannelResult::Ok&&controls.focused&&controls.headValid);
    if(controlsResult==fvr::ipc::ChannelResult::Ok&&controls.generation!=lastInputGeneration){
        lastInputGeneration=controls.generation;++inputSamples;
        if(controls.focused&&controls.headValid&&controls.hands[0].gripTracked&&controls.hands[1].aimTracked)++inputTrackedSamples;
    }
    if(!sampling.load(std::memory_order_acquire)&&stereoExperiment.stage.load(std::memory_order_acquire)==StereoExperiment::Released){
        ClosePrivatePresentationExperiment();frameBridge.CloseGraphics();
    }
}
fvr::graphics::SwapChainPresentFn desktopPresentOriginal=nullptr;
fvr::graphics::FrameBridgePresentService desktopFrameService;
void WriteDesktopGraphicsOwnership(std::ostream& out){
    const auto omissions=desktopFrameService.Omissions();
    out<<R"({"foreign_thread_omissions":)"<<omissions.foreignThread
        <<R"(,"reentrant_omissions":)"<<omissions.reentrant<<R"(,"first":)";
    if(omissions.firstAvailable)out<<R"({"reason":")"<<(omissions.firstForeignThread?"foreign_thread":"reentrant")
        <<R"(","actual_thread":)"<<omissions.actualThread<<R"(,"expected_thread":)"<<omissions.expectedThread
        <<R"(,"presented_swapchain":)"<<omissions.presentedSwapchain<<R"(,"expected_swapchain":)"<<omissions.expectedSwapchain<<'}';
    else out<<"null";
    out<<'}';
}
std::atomic<std::uint64_t> desktopFrameServiceCalls=0,desktopFrameServiceAfterCalls=0;
IDXGISwapChain* observedSwapChain=nullptr;unsigned observedRenderer=0;
struct GraphicsBootstrapSnapshot {
    std::uintptr_t device=0,context=0;
    std::array<std::uintptr_t,8> entries{}; // Indexed, Draw, instanced, ClearDepth, Auto, two indirect.
    unsigned thread=0,deviceFlags=0;
    std::uintptr_t createTexture2D=0;
    HRESULT multithreadQuery=S_FALSE;bool multithreadKnown=false,multithreadProtected=false;
};
fvr::graphics::RenderBootstrap<GraphicsBootstrapSnapshot> graphicsBootstrap;
GraphicsBootstrapSnapshot graphicsBootstrapEvidence;
std::atomic<bool> graphicsRuntimeReady=false;
unsigned GraphicsBootstrapThreadForDiagnostics()noexcept {
    return graphicsRuntimeReady.load(std::memory_order_acquire)?graphicsBootstrapEvidence.thread:0;
}
unsigned graphicsBootstrapWorkerThread=0;
void ClosePrivatePresentationExperiment()noexcept {
    if(!privatePresentationExperiment.Enabled())return;
    const auto renderer=U32(imageBase+profile.rendererGlobal);
    const fvr::graphics::FrameBridgePresentIdentity current{renderer,U32(renderer),U32(renderer+0x7c),
        reinterpret_cast<std::uintptr_t>(observedImmediateContext),U32(renderer+0x88),GetCurrentThreadId()};
    privatePresentationExperiment.Close(reinterpret_cast<ID3D11Device*>(current.device),current);
}
bool CaptureGraphicsBootstrap(IDXGISwapChain* chain,GraphicsBootstrapSnapshot& out)noexcept {
    // Called only from this chain's native Present. Never transfer owning COM
    // references to the setup worker, including on mismatch or cancellation.
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))||
       reinterpret_cast<unsigned>(device.Get())!=U32(observedRenderer+0x7c))return false;
    device->GetImmediateContext(&context);if(!context)return false;
    const unsigned slots[]={12,13,20,21,53,38,39,40};
    const auto table=*reinterpret_cast<void***>(context.Get());
    for(unsigned n=0;n<(destructionWitnessEnabled?out.entries.size():5);++n){out.entries[n]=reinterpret_cast<std::uintptr_t>(table[slots[n]]);if(!out.entries[n])return false;}
    out.device=reinterpret_cast<std::uintptr_t>(device.Get());out.context=reinterpret_cast<std::uintptr_t>(context.Get());
    out.thread=GetCurrentThreadId();out.deviceFlags=device->GetCreationFlags();
    if(graphicsProvenanceEnabled){
        const auto error=GetLastError();
        // SDK ID3D11Device slot5; only borrowed entry metadata leaves this callback.
        out.createTexture2D=reinterpret_cast<std::uintptr_t>((*reinterpret_cast<void***>(device.Get()))[5]);
        {ComPtr<ID3D11Multithread> protection;out.multithreadQuery=context.As(&protection);
            if(SUCCEEDED(out.multithreadQuery)&&protection){out.multithreadKnown=true;out.multithreadProtected=protection->GetMultithreadProtected()!=FALSE;}}
        SetLastError(error); // Query temporary was released on this owner too.
    }
    return U32(imageBase+profile.rendererGlobal)==observedRenderer&&
        U32(observedRenderer)==imageBase+profile.rendererVtable&&
        U32(observedRenderer+0x88)==reinterpret_cast<unsigned>(chain)&&U32(observedRenderer+0x7c)==out.device;
}
std::atomic<unsigned> desktopPresentCalls=0,desktopSyncBypassed=0,desktopSyncOne=0;
std::atomic<unsigned> desktopPresentOkay=0,desktopPresentOccluded=0,desktopPresentStatus=0,desktopPresentFailed=0,desktopPresentTests=0;
std::atomic<long> desktopPresentFirstFailure=0,desktopPresentLastResult=0;
struct DesktopCapture {
    ColorCapture color;DXGI_SWAP_CHAIN_DESC description{};RECT client{},window{};
    unsigned thread=0,stage=0,published=0;std::uint64_t tickMs=0;std::int64_t qpc=0;
    DWORD windowThread=0,windowProcess=0;HWND ghostWindow=nullptr;char windowClass[128]{};
    bool hung=false,ghostLookupAvailable=false;
    bool scheduled=false,descriptionValid=false,clientValid=false,windowValid=false,visible=false,minimized=false,foreground=false;
    HRESULT firstPresentResult=S_OK;
};
std::array<DesktopCapture,5> desktopCaptures;
std::uint64_t desktopFirstTick=0;
fvr::graphics::DesktopWindowMode desktopWindowMode;
fvr::graphics::DesktopWindowIdentity desktopWindowIdentity{};
void ObserveDesktopMode(IDXGISwapChain* chain,UINT flags)noexcept {
    using namespace fvr::graphics;
    DesktopWindowModeCalls calls;calls.context=chain;
    calls.observe=[](void* raw,DesktopWindowObservation& out){
        auto* sc=static_cast<IDXGISwapChain*>(raw);DXGI_SWAP_CHAIN_DESC d{};BOOL full=FALSE;
        out.descriptionResult=sc->GetDesc(&d);out.fullscreenResult=sc->GetFullscreenState(&full,nullptr);
        if(FAILED(out.descriptionResult)||FAILED(out.fullscreenResult))return false;
        ComPtr<ID3D11Device> device;if(FAILED(sc->GetDevice(IID_PPV_ARGS(&device))))return false;
        DWORD process=0;out.windowThread=GetWindowThreadProcessId(d.OutputWindow,&process);
        out.identity={reinterpret_cast<std::uintptr_t>(sc),reinterpret_cast<std::uintptr_t>(device.Get()),reinterpret_cast<std::uintptr_t>(d.OutputWindow),process,GetCurrentThreadId()};
        char name[128]{};GetClassNameA(d.OutputWindow,name,sizeof(name));out.windowClassVerified=std::strcmp(name,"Battlefield: Bad Company 2")==0;
        out.windowValid=IsWindow(d.OutputWindow)!=FALSE;out.windowed=d.Windowed!=FALSE;out.fullscreen=full!=FALSE;
        out.visible=IsWindowVisible(d.OutputWindow)!=FALSE;out.minimized=IsIconic(d.OutputWindow)!=FALSE;
        out.width=d.BufferDesc.Width;out.height=d.BufferDesc.Height;out.swapEffect=unsigned(d.SwapEffect);
        out.foregroundWindow=reinterpret_cast<std::uintptr_t>(GetForegroundWindow());return true;
    };
    calls.setWindowed=[](void* raw)->std::int32_t{return static_cast<IDXGISwapChain*>(raw)->SetFullscreenState(FALSE,nullptr);};
    if(!desktopWindowIdentity.chain){DesktopWindowObservation observation;if(!calls.observe(chain,observation))return;
        if(observation.identity.process!=GetCurrentProcessId()||observation.identity.device!=U32(observedRenderer+0x7c))return;
        desktopWindowIdentity=observation.identity;}
    LARGE_INTEGER freq{};if(!QueryPerformanceFrequency(&freq)||freq.QuadPart<=0)return;const auto now=Qpc();
    DesktopWindowModeGuard guard;guard.nowNs=(now/freq.QuadPart)*1000000000+(now%freq.QuadPart)*1000000000/freq.QuadPart;
    guard.enabled=sampling.load(std::memory_order_acquire);guard.nativeOwnerVerified=true;guard.presentBoundary=true;guard.testPresent=(flags&DXGI_PRESENT_TEST)!=0;
    // Present follows native frame restoration. Diagnostic/menu paths acquire
    // backbuffers only in scopes after this call; none persist between Presents.
    const auto trackedState=tracked.state.load(std::memory_order_acquire);
    guard.nativeStateRestored=trackedState!=1&&trackedState!=3&&!tracked.restoreFailures.load()&&!contextRestoreFailures.load();
    guard.noModLocks=true;guard.noModBackbufferReferences=true;
    desktopWindowMode.Step(desktopWindowIdentity,guard,calls);
}

unsigned ObserveDesktopBuffer(IDXGISwapChain* chain,UINT flags)noexcept {
    if(flags&DXGI_PRESENT_TEST)return 0;
    const auto now=GetTickCount64();if(!desktopFirstTick)desktopFirstTick=now;
    const auto stage=stereoExperiment.stage.load(std::memory_order_acquire);
    const auto published=frameBridge.Published();
    const bool active=sampling.load(std::memory_order_acquire)&&stage==StereoExperiment::Armed&&published>=2;
    const bool retired=!sampling.load(std::memory_order_acquire)&&stage==StereoExperiment::Released;
    const bool due[]={true,active,active&&now-desktopFirstTick>=2000,active&&now-desktopFirstTick>=8000,retired};
    unsigned scheduledMask=0;
    for(unsigned i=0;i<desktopCaptures.size();++i){
        auto& item=desktopCaptures[i];auto& c=item.color;
        if(!item.scheduled&&due[i]){
            item.scheduled=true;scheduledMask|=1u<<i;item.thread=GetCurrentThreadId();item.stage=stage;
            item.published=published;item.tickMs=now;item.qpc=Qpc();
            item.descriptionValid=SUCCEEDED(chain->GetDesc(&item.description));
            if(item.descriptionValid){
                const auto hwnd=item.description.OutputWindow;
                item.clientValid=GetClientRect(hwnd,&item.client)!=FALSE;item.windowValid=GetWindowRect(hwnd,&item.window)!=FALSE;
                item.visible=IsWindowVisible(hwnd)!=FALSE;item.minimized=IsIconic(hwnd)!=FALSE;item.foreground=GetForegroundWindow()==hwnd;
                item.windowThread=GetWindowThreadProcessId(hwnd,&item.windowProcess);GetClassNameA(hwnd,item.windowClass,sizeof(item.windowClass));
                item.hung=IsHungAppWindow(hwnd)!=FALSE;
                // Optional Windows ghost lookup; never activate either window.
                using GhostLookup=HWND(WINAPI*)(HWND);
                const auto ghost=reinterpret_cast<GhostLookup>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GhostWindowFromHungWindow"));
                item.ghostLookupAvailable=ghost!=nullptr;if(ghost)item.ghostWindow=ghost(hwnd);
            }
            ComPtr<ID3D11Texture2D> backbuffer;const auto hr=chain->GetBuffer(0,IID_PPV_ARGS(&backbuffer));
            if(FAILED(hr)){c.error=hr;c.step=ColorCapture::Step::Failed;c.complete.store(true,std::memory_order_release);}
            else CaptureWorldColor(c,U32(std::uintptr_t(U32(imageBase+profile.gameRendererGlobal))+0x1080),backbuffer.Get());
        }else if(item.scheduled&&c.step==ColorCapture::Step::Pending)CaptureWorldColor(c,c.frame);
    }
    return scheduledMask;
}
using CreateTexture2DFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
CreateTexture2DFn createTexture2DOriginal=nullptr;
HRESULT STDMETHODCALLTYPE ObserveCreateTexture2D(ID3D11Device* device,const D3D11_TEXTURE2D_DESC* description,
    const D3D11_SUBRESOURCE_DATA* initial,ID3D11Texture2D** texture){
    // No descriptor/output dereference, QueryInterface, allocation or extra
    // device call. Native arguments, result and last-error propagate unchanged.
    fvr::graphics::callProvenance::Scope provenance(fvr::graphics::callProvenance::Kind::CreateTexture2D,
        device,nullptr,nullptr,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    const auto result=createTexture2DOriginal(device,description,initial,texture);
    provenance.Result(result);return result;
}
HRESULT STDMETHODCALLTYPE DesktopPresentHook(IDXGISwapChain* chain,UINT interval,UINT flags){
    fvr::graphics::callProvenance::Scope provenance(fvr::graphics::callProvenance::Kind::Present,
        nullptr,nullptr,chain,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    if(!graphicsRuntimeReady.load(std::memory_order_acquire)){
        if(chain==observedSwapChain&&!(flags&DXGI_PRESENT_TEST)&&
           U32(imageBase+profile.rendererGlobal)==observedRenderer&&
           U32(observedRenderer)==imageBase+profile.rendererVtable&&U32(observedRenderer+0x88)==reinterpret_cast<unsigned>(chain))
            graphicsBootstrap.Observe([&](GraphicsBootstrapSnapshot& out)noexcept{return CaptureGraphicsBootstrap(chain,out);});
        return desktopPresentOriginal(chain,interval,flags);
    }
    fvr::graphics::FrameBridgePresentService::Scope serviceBoundary(desktopFrameService,
        GetCurrentThreadId(),graphicsBootstrapEvidence.thread,
        reinterpret_cast<std::uintptr_t>(chain),reinterpret_cast<std::uintptr_t>(observedSwapChain));
    // The native device may be SINGLETHREADED. The owner/nesting guard must
    // cover menu capture, desktop diagnostics and mirror work too, before any
    // added D3D/chain calls. Keep the scope held across the original Present.
    if(!serviceBoundary.Owned())return desktopPresentOriginal(chain,interval,flags);
#if defined(FVR_BC2_ORDINARY_RESOURCE_SCENE_PROBE)
    fvr::bc2::graphicsThreadObservation::Scope graphicsThread(fvr::bc2::graphicsThreadObservation::Boundary::Present,chain==observedSwapChain);
#endif
    fvr::graphics::DiagnosticImageLifetime::Scope imageScope(diagnosticImageLifetime);
    if(!imageScope.Active())return desktopPresentOriginal(chain,interval,flags);
    const bool matching=chain==observedSwapChain&&U32(imageBase+profile.rendererGlobal)==observedRenderer&&
        U32(observedRenderer)==imageBase+profile.rendererVtable&&U32(observedRenderer+0x88)==reinterpret_cast<unsigned>(chain);
    const bool active=matching&&uncapMirror&&sampling.load(std::memory_order_acquire)&&frameBridge.Connected()&&!frameBridge.Fatal();
    unsigned scheduled=0;
    auto presentDisplay=fvr::bc2::menu::MenuDisplayState::Unknown;
    if(matching){++desktopPresentCalls;if(interval==1)++desktopSyncOne;
        if(flags&DXGI_PRESENT_TEST)++desktopPresentTests;
        if(active&&!(flags&DXGI_PRESENT_TEST)&&interval>0&&interval<=4)++desktopSyncBypassed;
        // Capture native menu pixels before replacing the unstable desktop
        // world composition with one completed eye. XR sources stay untouched.
        ObserveDesktopMode(chain,flags);
        const auto display=(flags&DXGI_PRESENT_TEST)?fvr::bc2::menu::MenuDisplayState::Unknown:
            fvr::bc2::menu::Present(chain);
        presentDisplay=display;
        // A contended loading transition must preserve the original raster;
        // the cached UI observation can still describe the preceding world.
        if(display!=fvr::bc2::menu::MenuDisplayState::Unknown)
            frameBridge.PresentDesktop(chain,streamMode&&sampling.load(std::memory_order_acquire)&&
            frameBridge.Connected()&&!frameBridge.Fatal(),
            true,
            display==fvr::bc2::menu::MenuDisplayState::Menu||
            display==fvr::bc2::menu::MenuDisplayState::Passive,(flags&DXGI_PRESENT_TEST)!=0);
        scheduled=ObserveDesktopBuffer(chain,flags);
    }
    // Pin one identity across the original call; re-read current ownership and
    // restoration before each phase. Present may release GPU/host progress.
    const fvr::graphics::FrameBridgePresentIdentity expected{observedRenderer,
        imageBase+profile.rendererVtable,graphicsBootstrapEvidence.device,
        graphicsBootstrapEvidence.context,reinterpret_cast<std::uintptr_t>(observedSwapChain),
        graphicsBootstrapEvidence.thread};
    const auto currentBoundary=[&]()noexcept {
        const auto renderer=U32(imageBase+profile.rendererGlobal);
        fvr::graphics::FrameBridgePresentBoundary boundary;
        boundary.current={renderer,U32(renderer),U32(renderer+0x7c),
            reinterpret_cast<std::uintptr_t>(observedImmediateContext),U32(renderer+0x88),GetCurrentThreadId()};
        boundary.presentedSwapchain=reinterpret_cast<std::uintptr_t>(chain);
        boundary.enabled=streamMode&&sampling.load(std::memory_order_acquire)&&graphicsRuntimeReady.load(std::memory_order_acquire);
        boundary.lifetimeHeld=imageScope.Active();boundary.connected=frameBridge.Connected();boundary.fatal=frameBridge.Fatal();
        boundary.bridgeOwnerCurrent=frameBridge.MatchesGraphicsOwner(reinterpret_cast<ID3D11Device*>(expected.device),
            reinterpret_cast<ID3D11DeviceContext*>(expected.context));
        boundary.test=(flags&DXGI_PRESENT_TEST)!=0;boundary.drawingNativeView=drawingNativeView;
        const auto state=tracked.state.load(std::memory_order_acquire);
        boundary.nativeStateRestored=state!=1&&state!=3&&!tracked.anchorWritten&&!tracked.restoreFailures.load()&&
            !contextRestoreFailures.load()&&!stereoColorScalePair.counts.restoreFailures&&
            !stereoEffectPair.counts.restoreFailures&&!stereoPostProcessPair.RestoreFailed();
        return boundary;
    };
    if(serviceBoundary.Owned()&&privatePresentationExperiment.Enabled()){
        RenderCallbackScope callback;
        privatePresentationExperiment.Tick(reinterpret_cast<ID3D11Device*>(expected.device),expected,
            currentBoundary(),frameBridge.RenderQueueSnapshotValue().mode);
        fvr::diagnostics::privateGpu::recorder.ObserveActivationPhase(privatePresentationExperiment.Phase(),
            privatePresentationExperiment.AfterEnabled());
    }
    const bool allowAfter=privatePresentationExperiment.AfterEnabled();
    const auto service=[&](bool after,HRESULT originalResult)noexcept {
        if(!serviceBoundary.Owned())return;
        RenderCallbackScope callback;
        // Existing graphics work only. New camera admission remains at Update.
        const auto boundary=currentBoundary();
        const auto pump=[&]()noexcept {
            frameBridge.PumpGraphics();
            if(!after||!privatePresentProgress.budgetMs||
               presentDisplay!=fvr::bc2::menu::MenuDisplayState::Gameplay||
               (privatePresentationExperiment.Enabled()&&privatePresentationExperiment.Phase()!=3))return;
            fvr::graphics::RestoredPairLease lease;
            if(!frameBridge.TryGetRestoredPairLease(lease))return;
            const auto admitted=[&]()noexcept {
                return fvr::graphics::AdmitsFrameBridgePresent(expected,currentBoundary())&&
                    fvr::bc2::menu::CurrentDisplayState()==fvr::bc2::menu::MenuDisplayState::Gameplay;
            };
            const auto pending=[&]()noexcept {return frameBridge.SameRestoredPairPending(lease);};
            const auto progress=[]()noexcept {frameBridge.PumpGraphics();};
            const auto clock=[]()noexcept {const auto value=Qpc();return value>0?std::uint64_t(value):0;};
            const auto pause=[]()noexcept {Sleep(1);};
            // No bridge lock spans sleep. This scope and imageScope pin callback
            // lifetime, and the next native Update alone admits a successor.
            privatePresentProgress.Record(fvr::graphics::ServiceBoundedFrameProgress(
                privatePresentProgress.frequency*privatePresentProgress.budgetMs/1000,
                admitted,pending,progress,clock,pause));
        };
        const bool serviced=after?serviceBoundary.RunAfterPresent(originalResult,expected,boundary,pump):
            serviceBoundary.Run(expected,boundary,pump);
        if(serviced){
            if(after)desktopFrameServiceAfterCalls.fetch_add(1,std::memory_order_relaxed);
            else desktopFrameServiceCalls.fetch_add(1,std::memory_order_relaxed);
        }
    };
    service(false,S_OK);
    const auto result=fvr::graphics::PresentDesktopForVr(desktopPresentOriginal,chain,interval,flags,active);
    const auto originalError=GetLastError();
    if(result==S_OK&&allowAfter)service(true,result);
    if(matching){
        desktopPresentLastResult.store(result,std::memory_order_release);
        if(result==S_OK)++desktopPresentOkay;
        else if(result==DXGI_STATUS_OCCLUDED)++desktopPresentOccluded;
        else if(SUCCEEDED(result))++desktopPresentStatus;
        else {++desktopPresentFailed;long empty=0;desktopPresentFirstFailure.compare_exchange_strong(empty,result);}
        for(unsigned i=0;i<desktopCaptures.size();++i)if(scheduled&(1u<<i))desktopCaptures[i].firstPresentResult=result;
    }
    SetLastError(originalError);
    return result;
}
// No new native hook: verified inline color/effect scalars share the view lifetime.
bool ReadColorScale(void*,std::uint32_t address,void* out,std::size_t bytes)noexcept {
    if(address<0x10000||!bytes||bytes>0x2000||address>UINT32_MAX-bytes)return false;
    // Local protected reads avoid ReadProcessMemory syscalls on every eye.
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
template<std::size_t N> bool ReplaceColorScale(void*,std::uint32_t address,const std::array<float,N>& expected,
    const std::array<float,N>& replacement)noexcept {
    if(address<0x10000||address>UINT32_MAX-sizeof(expected)||(address&3))return false;
    // Native view draw lifetime is confined to the proven graphics thread.
    // A changed owner/value is never overwritten during restoration.
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
       (region.Protect&0xff)!=PAGE_READWRITE||
       address-reinterpret_cast<std::uintptr_t>(region.BaseAddress)>region.RegionSize||
       sizeof(expected)>region.RegionSize-(address-reinterpret_cast<std::uintptr_t>(region.BaseAddress)))return false;
    __try {
        if(std::memcmp(reinterpret_cast<void*>(address),expected.data(),sizeof(expected)))return false;
        std::memcpy(reinterpret_cast<void*>(address),replacement.data(),sizeof(replacement));return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
fvr::bc2::stereoColorScale::Key CurrentColorScaleKey(unsigned world,unsigned request)noexcept {
    const auto& e=stereoExperiment;
    if(!stereoColorScaleVerified||!streamMode||tracked.state.load(std::memory_order_acquire)!=1||
       e.stage.load(std::memory_order_acquire)!=StereoExperiment::Armed||world!=e.world||request!=e.request||
       tracked.lease.native.frameId!=e.frame.load(std::memory_order_acquire))return {};
    return {world,request,e.main,e.eye,e.frame.load(std::memory_order_acquire),graphicsBootstrapEvidence.thread,
        GetCurrentThreadId(),tracked.lease.requestId,tracked.lease.tracking.generation,tracked.lease.tracking.spaceGeneration};
}
// A native structured exception does not unwind C++ RAII under /EHsc. This
// leaf owns no C++ destructors: __finally restores all scalar scopes on that path.
void OriginalDrawWithColorScope(void* self,void* request,void* view,void* data,
    fvr::bc2::stereoColorScale::Pair::Scope* color,fvr::bc2::stereoColorScale::EffectPair::Scope* effect,fvr::bc2::stereoColorScale::PostProcessPair::Scope* postProcess,bool wasDrawing,WeatherTranslationScope* weather){
    // Diagnostic-only, owner-validated and trivially destructible for SEH.
    fvr::diagnostics::privateCpu::Token cpuToken{};
    fvr::diagnostics::privateGpu::Token gpuToken{};
    if(fvr::diagnostics::privateCpu::recorder.AllowOwner(fvr::diagnostics::privateCpu::Stage::MainOrdinary)){
        const auto& e=stereoExperiment;const auto address=reinterpret_cast<unsigned>(view);
        const bool owned=e.stage.load(std::memory_order_acquire)==StereoExperiment::Armed&&
            reinterpret_cast<unsigned>(self)==e.world&&reinterpret_cast<unsigned>(request)==e.request&&
            (address==e.main||address==e.eye)&&U32(address)==imageBase+layout.vtable&&
            U32(address+0x70)==e.request&&U32(e.request+8)==e.world&&U32(e.world+0x88)==e.frame.load();
        const bool trackedMain=owned&&tracked.state.load(std::memory_order_acquire)==1&&tracked.lease.native.frameId==e.frame.load();
        const auto stage=address==e.eye?fvr::diagnostics::privateCpu::Stage::ExtraEye:
            trackedMain?fvr::diagnostics::privateCpu::Stage::MainTracked:fvr::diagnostics::privateCpu::Stage::MainOrdinary;
        cpuToken=fvr::diagnostics::privateCpu::recorder.Begin(stage,owned&&(address==e.main||trackedMain));
        if(owned&&(address==e.main||trackedMain)&&frameBridge.SharingMode()==3){
            fvr::diagnostics::privateGpu::Key key{};
            key.owner=(std::uint64_t(e.world)<<32)|e.request;key.frame=e.frame.load();
            if(trackedMain){
                key.request=tracked.lease.requestId;key.channel=tracked.lease.channelGeneration;
                key.deviceEpoch=tracked.lease.native.deviceEpoch;key.tracking=tracked.lease.tracking.generation;
                key.space=tracked.lease.tracking.spaceGeneration;key.predictedNs=tracked.lease.tracking.predictedNs;
            }
            gpuToken=fvr::diagnostics::privateGpu::recorder.Begin(
                trackedMain?fvr::diagnostics::privateGpu::Kind::TrackedDraw:fvr::diagnostics::privateGpu::Kind::OrdinaryDraw,
                key,address==e.eye?1u:0u,0,observedImmediateContext);
        }
    }
    __try {drawOriginal(self,request,view,data);}
    __finally {
        fvr::diagnostics::privateGpu::recorder.End(gpuToken,!AbnormalTermination());
        fvr::diagnostics::privateCpu::recorder.End(cpuToken,!AbnormalTermination());
        if(weather)weather->Finish();
        drawingNativeView=wasDrawing;
        if(AbnormalTermination()){postProcess->Abort();effect->Abort();color->Abort();tracked.capturesOkay=false;}
    }
}

#if defined(FVR_BC2_NATIVE_FADE_DIAGNOSTIC)
fvr::bc2::fadeDiagnostic::Observer nativeFadeObserver;
#endif
void ObserveNativeFade(fvr::bc2::fadeDiagnostic::Phase phase,std::uintptr_t world,
    std::uintptr_t request,std::uintptr_t view=0,ID3D11DeviceContext* captureContext=nullptr,
    ID3D11RenderTargetView* captureTarget=nullptr,ID3D11Texture2D* captureSource=nullptr,
    const fvr::ipc::FrameLease* lease=nullptr)noexcept {
#if defined(FVR_BC2_NATIVE_FADE_DIAGNOSTIC)
    if(!nativeFadeObserver.Enabled()||!nativeFadeObserver.Due(GetTickCount64()))return;
    fvr::bc2::fadeDiagnostic::Key key;key.world=world;key.request=request;key.view=view;
    // Ordinary brackets report the first two request-list candidates, not guessed eye ownership.
    unsigned begin=0,end=0;
    fvr::bc2::fadeDiagnostic::ReadField(Read,request,0x24,&begin,4);
    fvr::bc2::fadeDiagnostic::ReadField(Read,request,0x28,&end,4);
    if(begin&&end>=begin&&end-begin<=64&&(end-begin)%4==0){
        key.candidateCount=(end-begin)/4;unsigned candidates[2]{};
        if(fvr::bc2::fadeDiagnostic::ReadField(Read,begin,0,candidates,(std::min)(key.candidateCount,2u)*4))
            for(unsigned i=0;i<2;++i)key.candidates[i]=candidates[i];
    }
    // CaptureTrackedEye supplies the same lease and stereo identities it will copy.
    if(lease){key.candidates={stereoExperiment.main,stereoExperiment.eye};
        key.candidateCount=2;key.candidatesAreStereoPair=true;key.pairRequest=lease->requestId;
        key.tracking=lease->tracking.generation;key.space=lease->tracking.spaceGeneration;
        key.leaseFrame=lease->native.frameId;}
    // Acquire the existing immutable Present bootstrap before reading its identity.
    // A World/Prepare callback on another thread still records native fields, but
    // introduces no COM/context access. Explicit CaptureSource also obeys this gate.
    const bool graphicsReady=graphicsRuntimeReady.load(std::memory_order_acquire);
    auto* context=graphicsReady?(captureContext?captureContext:observedImmediateContext):nullptr;
    key.graphicsThread=graphicsReady?graphicsBootstrapEvidence.thread:0;
    key.contextStatus=fvr::bc2::fadeDiagnostic::CheckContext(graphicsReady,key.graphicsThread,
        GetCurrentThreadId(),graphicsReady?graphicsBootstrapEvidence.context:0,
        reinterpret_cast<std::uintptr_t>(context));
    if(key.contextStatus!=fvr::bc2::fadeDiagnostic::ContextStatus::Ready)context=nullptr;
    nativeFadeObserver.Sample(phase,key,context,captureTarget,captureSource);
#else
    (void)phase;(void)world;(void)request;(void)view;(void)captureContext;
    (void)captureTarget;(void)captureSource;(void)lease;
#endif
}

bool CaptureTrackedEye(unsigned eye)noexcept {
    const auto renderer=U32(imageBase+profile.rendererGlobal);if(U32(renderer)!=imageBase+profile.rendererVtable)return false;
    auto* device=reinterpret_cast<ID3D11Device*>(U32(renderer+0x7c));ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
    ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);if(!target)return false;
    ComPtr<ID3D11Resource> resource;target->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return false;
    ObserveNativeFade(fvr::bc2::fadeDiagnostic::Phase::CaptureSource,stereoExperiment.world,stereoExperiment.request,
        eye?stereoExperiment.eye:stereoExperiment.main,context.Get(),target.Get(),texture.Get(),&tracked.lease);
    // Native minimize/restore can replace the target between admission and
    // either eye. Reject that pair, then restore/cancel at the normal boundary;
    // keep the channel and its old GPU acknowledgements alive for the next pair.
    const bool captured=frameBridge.CaptureEye(eye,texture.Get(),tracked.lease,fvr::graphics::SourceDimensionPolicy::DiscardPair);
    if(captured&&destructionWitnessEnabled)destructionWitness.CaptureColor(context.Get(),texture.Get(),DestructionWitnessKey(),eye);
    return captured;
}

bool SameEntries(const ListSnapshot& a,const ListSnapshot& b)noexcept {
    if(a.count!=b.count)return false;
    for(unsigned i=0;i<a.count;++i)if(a.values[i]!=b.values[i])return false;
    return true;
}
bool ListContains(const ListSnapshot& list,unsigned value)noexcept {
    for(unsigned i=0;i<list.count;++i)if(list.values[i]==value)return true;return false;
}
bool CurrentScene(unsigned world,unsigned request)noexcept {
    const auto game=U32(imageBase+profile.gameRendererGlobal),subsystem=U32(game+0x108c);
    return U32(subsystem)==stereoRecovery.subsystemType&&U32(subsystem+0x2c)==world&&
        U32(subsystem+0x30)==request&&U32(world)==stereoRecovery.worldType&&
        U32(request)==imageBase+lifecycle.requestVtable&&U32(request+8)==world&&U32(request+0xc4)==3;
}
struct LiveCacheCalls {
    unsigned world=0,request=0;const CallbackSnapshot& callback;
    bool Boundary()const noexcept {
        const auto& e=stereoExperiment;const auto stage=e.stage.load(std::memory_order_acquire);
        const auto pool=workPool.stage.load(std::memory_order_acquire);
        return (stage==StereoExperiment::Armed||stage==StereoExperiment::Retiring)&&
            world==e.world&&request==e.request&&CurrentScene(world,request)&&
            !stereoRecovery.callbacks.load(std::memory_order_acquire)&&
            pool!=1&&pool!=3&&tracked.state.load(std::memory_order_acquire)!=1&&
            fvr::bc2::rigWorkerRuntime::Quiescent();
    }
    bool Capture(fvr::bc2::LiveViewCache& out)const noexcept {
        const auto& e=stereoExperiment;
        if(!Boundary()||!e.callbacks.Matches()||!callback.list||U32(callback.object)!=callback.vtable||
           U32(callback.vtable+8)!=imageBase+0x569d50||
           U32(callback.vtable+24)!=imageBase+initialization.refreshRegistered||
           U32(callback.vtable+28)!=imageBase+viewCallbacks.registerView||
           U32(callback.vtable+32)!=imageBase+viewCallbacks.unregisterView)return false;
        fvr::bc2::LiveViewCache c{};c.world=world;c.request=request;c.callback=callback.object;c.type=callback.vtable;
        c.context=U32(world+0x94);c.scene=U32(callback.object+0xc);c.references=U32(callback.object+4);
        if(!Read(callback.object+0x30,&c.dirty,1))return false;
        ListSnapshot registered{},all{},roots{};
        if(!registered.Capture(callback.object+0x20)||!all.Capture(request+0x14)||
           !roots.Capture(request+0x24)||roots.count!=2||!ListContains(roots,e.main)||!ListContains(roots,e.eye)||
           registered.count!=all.count||!registered.count)return false;
        c.registeredBegin=U32(callback.object+0x20);c.registeredEnd=U32(callback.object+0x24);
        c.registeredCapacity=U32(callback.object+0x28);c.registeredCount=registered.count;
        if(!c.registeredBegin||!fvr::bc2::ValidLiveViewVector(c.registeredBegin,c.registeredEnd,
            c.registeredCapacity,4,c.registeredCount))return false;
        for(unsigned i=0;i<registered.count;++i){
            const auto view=registered.values[i],type=U32(view);
            if(!view||!ListContains(all,view)||U32(view+0x70)!=request||!U32(view+0x20))return false;
            if(view==e.main||view==e.eye){if(type!=imageBase+layout.vtable)return false;}
            else if(U32(type+12)!=imageBase+initialization.parentGetter||
                (U32(view+0x1670)!=e.main&&U32(view+0x1670)!=e.eye))return false;
            c.registered[i]=view;
        }
        c.cacheBegin=U32(callback.object+0x10);c.cacheEnd=U32(callback.object+0x14);c.cacheCapacity=U32(callback.object+0x18);
        if(!c.cacheBegin){if(c.cacheEnd||c.cacheCapacity)return false;}
        else {
            if((c.cacheBegin&3u)||c.cacheEnd<c.cacheBegin||c.cacheCapacity<c.cacheEnd||
               (c.cacheEnd-c.cacheBegin)%20u||c.cacheEnd-c.cacheBegin>c.cached.size()*20u)return false;
            c.cacheCount=(c.cacheEnd-c.cacheBegin)/20u;
            if(!fvr::bc2::ValidLiveViewVector(c.cacheBegin,c.cacheEnd,c.cacheCapacity,20,c.cacheCount))return false;
            for(unsigned i=0;i<c.cacheCount;++i)
                if(!Read(c.cacheBegin+i*20,&c.cached[i],4))return false; // Never dereference a cached view.
        }
        c.ownerVerified=c.registeredViewsVerified=true;c.boundaryVerified=Boundary();
        if(!registered.Matches()||!all.Matches()||!roots.Matches()||!e.callbacks.Matches())return false;
        out=c;return true;
    }
    bool Refresh(const fvr::bc2::LiveViewCache& before)const noexcept {
        // The existing native refresh releases only its owned terrain records;
        // registered RenderViews keep their native holders and queued refs.
        for(const auto& code:stereoRecovery.code)
            if(!code.address||Hash(code.address,code.size)!=code.hash)return false;
        fvr::bc2::LiveViewCache again{};if(!Capture(again)||again!=before)return false;
        liveCacheEvidence.callback=callback.object;liveCacheEvidence.beforeCount=before.cacheCount;
        liveCacheEvidence.registeredCount=before.registeredCount;++liveCacheEvidence.calls;
        StereoCheckpoint("live_cache_before_refresh");
        using RefreshFn=void(__thiscall*)(void*,void*);
        reinterpret_cast<RefreshFn>(imageBase+initialization.refreshRegistered)(
            reinterpret_cast<void*>(callback.object),reinterpret_cast<void*>(before.context));
        return true;
    }
};
void RefreshDirtyLiveCaches(unsigned world,unsigned request)noexcept {
    const auto& e=stereoExperiment;
    if(!streamMode||!stereoMode||world!=e.world||request!=e.request)return;
    for(unsigned i=0;i<e.callbacks.count;++i){const auto& cb=e.callbackValues[i];if(!cb.list)continue;
        LiveCacheCalls calls{world,request,cb};
        // Native root Draw drains the old child queue after rebuilding caches.
        // The next phase-3 Update is before any terrain cache consumer and has
        // no in-flight visibility/draw callbacks. Refresh only a dirty mismatch.
        if(!calls.Boundary())return;
        unsigned char dirty=0;
        if(U32(cb.object)==cb.vtable&&Read(cb.object+0x30,&dirty,1)&&dirty==0)continue;
        const auto result=fvr::bc2::RefreshLiveViewCache(calls);
        if(result==fvr::bc2::LiveViewCacheResult::Unchanged)continue;
        if(result==fvr::bc2::LiveViewCacheResult::Refreshed)++liveCacheEvidence.refreshed;
        else if(result==fvr::bc2::LiveViewCacheResult::Disabled)++liveCacheEvidence.disabled;
        else ++liveCacheEvidence.rejected;
        fvr::bc2::LiveViewCache after{};
        if(calls.Capture(after))liveCacheEvidence.afterCount=after.cacheCount;
        // One bounded receipt per attempted repair; no per-frame log growth for
        // a deliberately disabled terrain consumer or rejected stable owner.
        if(result==fvr::bc2::LiveViewCacheResult::Refreshed)StereoCheckpoint("live_cache_refreshed");
        else if(result==fvr::bc2::LiveViewCacheResult::Rejected&&liveCacheEvidence.rejected.load()==1)
            StereoCheckpoint("live_cache_rejected");
    }
}
template<class Graph> bool CapturePendingStereoChildren(Graph& g,unsigned children,ListSnapshot& pending,ListSnapshot& holders)noexcept {
    if(!pending.Capture(g.eye+0x1670)||pending.count>g.pendingEye.size())return false;
    g.pendingEyeCount=pending.count;
    for(unsigned i=0;i<pending.count;++i)g.pendingEye[i]=pending.values[i];
    if(!pending.count)return true;
    if(!children||children>g.eyeHolders.size()||!holders.Capture(g.eye+0x1680)||holders.count!=children*2)return false;
    for(unsigned i=0;i<children;++i){auto& h=g.eyeHolders[i];
        h.object=holders.values[i*2];h.control=holders.values[i*2+1];
        if(!h.object||!h.control)return false;
        h.owners=U32(h.control);h.first=U32(h.object+0xc);h.second=U32(h.object+0x10);
    }
    // Callers fingerprint both native destructor loops before capturing this
    // proof. The root destructor alone releases its holders and pending queue.
    g.pendingEyeOwnershipVerified=true;return true;
}
template<class Graph> bool PendingStereoChildrenMatch(const Graph& g,unsigned children,const ListSnapshot& pending,const ListSnapshot& holders)noexcept {
    if(!pending.Matches())return false;
    if(!g.pendingEyeCount)return true;
    if(children>g.eyeHolders.size()||!holders.Matches()||holders.count!=children*2)return false;
    for(unsigned i=0;i<children;++i){const auto& h=g.eyeHolders[i];
        if(U32(h.control)!=h.owners||U32(h.object+0xc)!=h.first||U32(h.object+0x10)!=h.second)return false;
    }
    return true;
}
struct LiveStereoSnapshot {
    fvr::bc2::LiveStereoRetirementGraph graph{};fvr::bc2::StereoSurvivors survivors{};
    ListSnapshot main{},all{},requests{},callbacks{},eyePending{},eyeHolders{};
    std::array<CallbackSnapshot,16> callbackValues{};
};
bool CaptureLiveStereoShutdown(LiveStereoSnapshot& out)noexcept {
    const auto& e=stereoExperiment;auto& g=out.graph;
    using Reason=fvr::bc2::StereoProgressReason;
    const auto reject=[&](Reason reason)noexcept {
        auto sample=StereoProgressSample(e.world,e.request,e.frame.load(std::memory_order_acquire));
        sample.shutdownViews=g.count;sample.shutdownReferences=g.requestReferences;
        sample.shutdownMainChildren=g.mainChildren;sample.shutdownEyeChildren=g.eyeChildren;
        sample.shutdownSurvivors=out.survivors.count;sample.shutdownReferencesAfter=out.survivors.referencesAfter;
        stereoProgressEvidence.Record(reason,sample);return false;
    };
    const auto quiescent=[]()noexcept{const auto state=tracked.state.load(std::memory_order_acquire);
        const auto pool=workPool.stage.load(std::memory_order_acquire);
        return state!=1&&state!=3&&!tracked.anchorWritten&&!tracked.restoreFailures.load()&&!contextRestoreFailures.load()&&
            !stereoRecovery.callbacks.load(std::memory_order_acquire)&&pool!=1&&pool!=3&&fvr::bc2::rigWorkerRuntime::Quiescent();};
    // Existing native Update boundary: request state3 follows completed original
    // draw/visibility jobs, before the next world update can enqueue new jobs.
    // Callback count is an additional check, not the serialization mechanism.
    unsigned char eyeActive=1;
    if(!CurrentScene(e.world,e.request))return reject(Reason::ShutdownScene);
    if(!quiescent())return reject(Reason::ShutdownQuiescence);
    if(!e.callbacks.Matches())return reject(Reason::ShutdownCallbacks);
    if(U32(e.main)!=imageBase+layout.vtable||U32(e.main+0x70)!=e.request||
       U32(e.eye)!=imageBase+layout.vtable||U32(e.eye+0x70)!=e.request)return reject(Reason::ShutdownView);
    if(!Read(e.eye+layout.activeOffset,&eyeActive,1)||eyeActive)return reject(Reason::ShutdownActiveEye);
    for(const auto& code:stereoRecovery.code)if(!code.address||Hash(code.address,code.size)!=code.hash)return reject(Reason::ShutdownCode);
    if(!out.main.Capture(e.request+0x24)||out.main.count!=2||out.main.values[0]!=e.main||out.main.values[1]!=e.eye||
       !out.all.Capture(e.request+0x14)||!out.requests.Capture(e.world+0xac)||out.requests.count!=1||out.requests.values[0]!=e.request||
       !SnapshotCallbacks(e.world,e.main,out.callbacks,out.callbackValues)||!SameEntries(out.callbacks,e.callbacks))return reject(Reason::ShutdownLists);
    g.world=e.world;g.request=e.request;g.main=e.main;g.eye=e.eye;g.count=out.all.count;
    g.requestReferences=U32(e.request+4);g.mainChildren=ChildCount(e.main);g.eyeChildren=ChildCount(e.eye);
    g.nativeOwnersVerified=g.requestRegistered=g.callbacksVerified=true;
    for(unsigned i=0;i<g.count;++i){auto& v=g.views[i];v.address=out.all.values[i];
        v.request=U32(v.address+0x70);v.references=U32(v.address+0x20);const auto type=U32(v.address);
        if(v.address==g.main||v.address==g.eye)v.nativeTypeVerified=type==imageBase+layout.vtable;
        else {v.nativeTypeVerified=type>=imageBase&&type<imageBase+profile.imageSize-16&&
            U32(type+12)==imageBase+initialization.parentGetter&&U32(type+4)==imageBase+lifecycle.release;
            if(v.nativeTypeVerified)v.parent=U32(v.address+0x1670);}
    }
    if(!CapturePendingStereoChildren(g,g.eyeChildren,out.eyePending,out.eyeHolders))return reject(Reason::ShutdownGraph);
    for(unsigned i=0;i<out.callbacks.count;++i){const auto& cb=out.callbackValues[i];const auto& original=e.callbackValues[i];
        if(cb.object!=original.object||cb.vtable!=original.vtable||cb.remember!=original.remember||cb.list!=original.list||
           (cb.remember&&(cb.borrowed!=original.borrowed||cb.borrowed!=e.main)))return reject(Reason::ShutdownCallbackIdentity);
        if(cb.list&&!SameEntries(cb.registered,out.all))return reject(Reason::ShutdownCallbackViews);
    }
    if(!fvr::bc2::CaptureLiveStereoSurvivors(g,out.survivors))return reject(Reason::ShutdownGraph);
    if(out.survivors.count!=e.allViews.count||out.survivors.referencesAfter!=e.refsBefore)return reject(Reason::ShutdownSurvivors);
    if(!out.main.Matches()||!out.all.Matches()||!out.requests.Matches()||!out.callbacks.Matches()||
       !PendingStereoChildrenMatch(g,g.eyeChildren,out.eyePending,out.eyeHolders))return reject(Reason::ShutdownListMutation);
    for(unsigned i=0;i<out.callbacks.count;++i)if(out.callbackValues[i].list&&!out.callbackValues[i].registered.Matches())return reject(Reason::ShutdownListMutation);
    if(!CurrentScene(e.world,e.request)||!quiescent()||U32(e.request+4)!=g.requestReferences)return reject(Reason::ShutdownFinalBoundary);
    return true;
}
bool RefreshShutdownGeneration()noexcept {
    LiveStereoSnapshot first{},again{};
    if(!CaptureLiveStereoShutdown(first)||!CaptureLiveStereoShutdown(again)){++stereoShutdown.rejected;return false;}
    if(first.graph!=again.graph||first.survivors!=again.survivors||!SameEntries(first.main,again.main)||!SameEntries(first.all,again.all)||
       !SameEntries(first.callbacks,again.callbacks)){
        RecordStereoProgress(fvr::bc2::StereoProgressReason::ShutdownGenerationChanged,stereoExperiment.frame);
        ++stereoShutdown.rejected;return false;
    }
    auto& e=stereoExperiment;stereoShutdown.historical=e.allViews;stereoShutdown.before=again.all;
    stereoShutdown.requestReferencesBefore=again.graph.requestReferences;
    stereoShutdown.pendingReferencesBefore=again.graph.pendingEyeCount;
    ListSnapshot current{};current.address=e.request+0x14;current.count=again.survivors.count;current.values=again.survivors.ordered;
    // These are owned metadata copies only. Native vectors retain their current
    // order; never write old entries or revive a previous child generation.
    e.allViews=current;stereoShutdown.expected=current;
    for(unsigned i=0;i<e.callbacks.count;++i)if(e.callbackValues[i].list){
        e.callbackValues[i].registered=current;e.callbackValues[i].registered.address=e.callbackValues[i].object+0x20;
    }
    ++stereoShutdown.captured;StereoCheckpoint("shutdown_generation_captured");return true;
}
struct RetiredStereoSnapshot {
    fvr::bc2::RetiredStereoGraph graph{};
    ListSnapshot main{},all{},requests{},callbacks{},currentCallbacks{},eyePending{},eyeHolders{};
    std::array<CallbackSnapshot,16> callbackValues{};
};
bool CaptureRetiredStereo(unsigned world,unsigned request,RetiredStereoSnapshot& out)noexcept {
    const auto& e=stereoExperiment;auto& g=out.graph;
    g.world=e.world;g.request=e.request;g.eye=e.eye;g.currentWorld=world;g.currentRequest=request;
    if(!CurrentScene(world,request)||world==e.world||request==e.request||
       U32(e.world)!=stereoRecovery.worldType||U32(e.request)!=imageBase+lifecycle.requestVtable||
       U32(e.request+8)!=e.world||U32(e.request+0xc4)!=3)return false;
    for(const auto& code:stereoRecovery.code)if(!code.address||Hash(code.address,code.size)!=code.hash)return false;
    if(U32(e.eye)!=imageBase+layout.vtable||U32(e.eye+0x70)!=e.request||
       !out.main.Capture(e.request+0x24)||!out.all.Capture(e.request+0x14)||
       !out.requests.Capture(e.world+0xac)||!out.callbacks.Capture(e.world+0x9c)||
       !out.currentCallbacks.Capture(world+0x9c)||!out.callbacks.count||out.callbacks.count>out.callbackValues.size())return false;
    g.nativeOwnersVerified=true;g.requestRegistered=out.requests.count==1&&out.requests.values[0]==e.request;
    g.requestReferences=U32(e.request+4);g.worldReferences=U32(e.world+4);
    g.mainCount=out.main.count;g.mainEntry=out.main.count?out.main.values[0]:0;
    g.count=out.all.count;g.children=ChildCount(e.eye);
    if(g.count>g.views.size())return false;
    for(unsigned i=0;i<g.count;++i){auto& v=g.views[i];v.address=out.all.values[i];
        v.request=U32(v.address+0x70);v.references=U32(v.address+0x20);
        const auto type=U32(v.address);
        if(v.address==e.eye)v.nativeTypeVerified=type==imageBase+layout.vtable;
        else {v.nativeTypeVerified=type>=imageBase&&type<imageBase+profile.imageSize-16&&
            U32(type+12)==imageBase+initialization.parentGetter&&U32(type+4)==imageBase+lifecycle.release;
            if(v.nativeTypeVerified)v.parent=U32(v.address+0x1670);}
    }
    if(!CapturePendingStereoChildren(g,g.children,out.eyePending,out.eyeHolders))return false;
    g.callbacksVerified=true;
    const auto noop=[](unsigned address){std::array<unsigned char,3> b{};return address>=imageBase&&
        address<imageBase+profile.imageSize-3&&Read(address,b.data(),b.size())&&b==std::array<unsigned char,3>{0xc2,4,0};};
    for(unsigned i=0;i<out.callbacks.count;++i){auto& cb=out.callbackValues[i];cb.object=out.callbacks.values[i];cb.vtable=U32(cb.object);
        if(ListContains(out.currentCallbacks,cb.object))return false;
        if(cb.vtable<imageBase||cb.vtable>=imageBase+profile.imageSize-36)return false;
        const auto create=U32(cb.vtable+28),destroy=U32(cb.vtable+32);
        if(create==imageBase+viewCallbacks.registerView&&destroy==imageBase+viewCallbacks.unregisterView){
            cb.list=true;if(ListContains(out.currentCallbacks,cb.object)||U32(cb.vtable+24)!=imageBase+initialization.refreshRegistered||
                !cb.registered.Capture(cb.object+0x20)||!SameEntries(cb.registered,out.all))return false;
        }else if(create==imageBase+viewCallbacks.rememberMain&&noop(destroy)){
            cb.remember=true;cb.borrowed=U32(cb.object+0xbc);
            // This field may legitimately point to a rebound original main.
            // Its destroy callback is a no-op: never restore its old snapshot.
            if(ListContains(out.all,cb.borrowed))return false;
        }else if(!noop(create)||!noop(destroy))return false;
    }
    if(!fvr::bc2::CanRetireStereoGraph(g)||!out.main.Matches()||!out.all.Matches()||
       !out.requests.Matches()||!out.callbacks.Matches()||!out.currentCallbacks.Matches()||
       !PendingStereoChildrenMatch(g,g.children,out.eyePending,out.eyeHolders))return false;
    return U32(g.eye+0x20)==1&&U32(g.request+4)==g.requestReferences&&
        U32(g.world+4)==1&&CurrentScene(world,request);
}
struct RetireStereoCalls {
    RetiredStereoSnapshot& snapshot;bool held=false;
    bool RetainOwner()noexcept {
        const auto& g=snapshot.graph;
        const auto refs=reinterpret_cast<fvr::bc2::ViewRefFn>(stereoRecovery.requestAddRef)(reinterpret_cast<void*>(g.request));
        held=true;
        if(refs==g.requestReferences+1&&snapshot.main.Matches()&&snapshot.all.Matches()&&snapshot.requests.Matches()&&
           snapshot.callbacks.Matches()&&snapshot.currentCallbacks.Matches()&&CurrentScene(g.currentWorld,g.currentRequest)&&
           U32(g.request+0xc4)==3&&U32(g.request+8)==g.world&&U32(g.world+4)==1){
            stereoRecovery.retirementStep=unsigned(fvr::runtime::RetirementStep::OwnerRetained);StereoCheckpoint("owner_retained");return true;}
        // No root mutation has occurred: balance ONLY the just-acquired ref.
        reinterpret_cast<fvr::bc2::ViewRefFn>(stereoRecovery.requestRelease)(reinterpret_cast<void*>(g.request));held=false;return false;
    }
    bool ReleaseRoot()noexcept {
        const auto& g=snapshot.graph;
        if(!held||U32(g.eye)!=imageBase+layout.vtable||U32(g.eye+0x70)!=g.request||U32(g.eye+0x20)!=1||
           U32(g.request+0xc4)!=3||U32(g.request+4)!=g.requestReferences+1||U32(g.world+4)!=1||
           !CurrentScene(g.currentWorld,g.currentRequest)||!snapshot.callbacks.Matches()||
           !snapshot.main.Matches()||!snapshot.all.Matches()||stereoRecovery.callbacks.load(std::memory_order_acquire)||
           !fvr::bc2::CanRetireStereoGraph(g)||!PendingStereoChildrenMatch(g,g.children,snapshot.eyePending,snapshot.eyeHolders))return false;
        for(unsigned i=0;i<g.count;++i){const auto& v=g.views[i];
            if(U32(v.address+0x70)!=g.request||U32(v.address+0x20)!=v.references||
               (v.address!=g.eye&&(U32(U32(v.address)+12)!=imageBase+initialization.parentGetter||U32(v.address+0x1670)!=g.eye)))return false;
        }
        reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(reinterpret_cast<void*>(g.eye),false);
        StereoCheckpoint("before_retired_eye_release");
        const bool released=reinterpret_cast<fvr::bc2::ViewRefFn>(imageBase+lifecycle.release)(reinterpret_cast<void*>(g.eye))==0;
        if(released){stereoRecovery.retirementStep=unsigned(fvr::runtime::RetirementStep::RootReleased);StereoCheckpoint("after_retired_eye_release");}
        return released;
    }
    bool Detached()noexcept {
        const auto& g=snapshot.graph;ListSnapshot main{},all{};
        if(U32(g.request)!=imageBase+lifecycle.requestVtable||U32(g.request+8)!=g.world||U32(g.request+4)!=1||
           U32(g.world)!=stereoRecovery.worldType||!main.Capture(g.request+0x24)||main.count||
           !all.Capture(g.request+0x14)||all.count||!snapshot.callbacks.Matches())return false;
        for(unsigned i=0;i<snapshot.callbacks.count;++i){const auto& cb=snapshot.callbackValues[i];
            if(U32(cb.object)!=cb.vtable)return false;
            if(cb.list){ListSnapshot list{};if(!list.Capture(cb.object+0x20)||list.count)return false;}
            if(cb.remember&&U32(cb.object+0xbc)!=cb.borrowed)return false;
        }return true;
    }
    bool RetireRegistry()noexcept {
        // Native refresh enumerates cb+8's scene type table BEFORE clearing its
        // view cache. That table is already gone on checkpoint reload (dump
        // 060816: refresh->iterator1040). Shrink the owned cache directly using
        // the verified native vector operation; never query the retired scene.
        struct CacheCalls {
            RetireStereoCalls& owner;const CallbackSnapshot& callback;
            bool Detached()noexcept{return owner.held&&owner.Detached()&&
                CurrentScene(owner.snapshot.graph.currentWorld,owner.snapshot.graph.currentRequest);}
            bool Capture(fvr::bc2::RetiredViewCache& c)noexcept {
                const auto at=callback.object;std::array<unsigned,8> words{};
                if(!Read(at,words.data(),sizeof(words))||words[0]!=callback.vtable||
                   U32(callback.vtable+24)!=imageBase+initialization.refreshRegistered)return false;
                c={at,words[0],words[1],words[2],words[4],words[5],words[6]};return true;
            }
            bool ShrinkToZero(const fvr::bc2::RetiredViewCache& expected)noexcept {
                if(!stereoRecovery.retiredCacheResize||!Detached())return false;
                for(const auto& code:stereoRecovery.code)
                    if(!code.address||Hash(code.address,code.size)!=code.hash)return false;
                // Validate the bounded native entries before handing them to
                // their own native destructor. These are native ref interfaces,
                // not scene pointers or COM objects synthesized by the adapter.
                for(unsigned at=expected.begin;at<expected.end;at+=20){std::array<unsigned,5> item{};
                    if(!Read(at,item.data(),sizeof(item)))return false;
                    for(unsigned i:{1u,2u})if(item[i]){const auto type=U32(item[i]),release=U32(type+4);
                        if(type<imageBase||type>=imageBase+profile.imageSize-8||release<imageBase||release>=imageBase+profile.imageSize)return false;}
                }
                fvr::bc2::RetiredViewCache observed{};
                if(!Capture(observed)||observed!=expected||!Detached())return false;
                StereoCheckpoint("before_retired_cache_clear");
                using Resize=void(__thiscall*)(void*,unsigned);
                ++stereoRecovery.cacheClearCalls;
                reinterpret_cast<Resize>(stereoRecovery.retiredCacheResize)(reinterpret_cast<void*>(expected.callback+0x10),0);
                return true;
            }
        };
        for(unsigned i=0;i<snapshot.callbacks.count;++i){const auto& cb=snapshot.callbackValues[i];if(!cb.list)continue;
            CacheCalls calls{*this,cb};if(!fvr::bc2::RetireDetachedViewCache(calls))return false;
            ++stereoRecovery.cacheClearReceipts;
            StereoCheckpoint("after_retired_cache_clear");
        }
        if(!Detached())return false;
        stereoRecovery.retirementStep=unsigned(fvr::runtime::RetirementStep::RegistryRetired);return true;
    }
    bool ReleaseOwner()noexcept {
        // This may destroy the old request and world. Never read them afterwards.
        if(!held)return false;held=false;StereoCheckpoint("before_retired_owner_release");
        const bool released=reinterpret_cast<fvr::bc2::ViewRefFn>(stereoRecovery.requestRelease)(reinterpret_cast<void*>(snapshot.graph.request))==0;
        if(released)StereoCheckpoint("after_retired_owner_release");return released;
    }
};
bool RecoverStereoOwner(unsigned world,unsigned request,unsigned incomingFrame)noexcept {
    auto& e=stereoExperiment;const auto state=tracked.state.load(std::memory_order_acquire);
    // Transport failure blocks new rendering, never the final native graph
    // retirement once the session is stopping.
    if(!stereoRecovery.sourceTransition.AllowsNativeRetirement(sampling.load(std::memory_order_acquire)))return false;
    const auto ready=stereoRecovery.policy.Observe({e.world,e.request},{world,request},incomingFrame,
        CurrentScene(world,request),state==1||state==3,tracked.restoreFailures.load()||contextRestoreFailures.load());
    if(ready==fvr::runtime::RecoveryReadiness::DrainTransaction){
        if(state==3)stereoRecovery.cancelUnwrittenRequest.store(tracked.lease.requestId,std::memory_order_release);return false;
    }
    if(ready!=fvr::runtime::RecoveryReadiness::Ready||stereoRecovery.callbacks.load(std::memory_order_acquire)||
       workPool.stage.load(std::memory_order_acquire)==1||workPool.stage.load(std::memory_order_acquire)==3||
       !fvr::bc2::rigWorkerRuntime::Quiescent())return false;
    RetiredStereoSnapshot first{},snapshot{};
    if(!CaptureRetiredStereo(world,request,first)||!CaptureRetiredStereo(world,request,snapshot)||first.graph!=snapshot.graph||
       !SameEntries(first.main,snapshot.main)||!SameEntries(first.all,snapshot.all)||!SameEntries(first.requests,snapshot.requests)||
       !SameEntries(first.callbacks,snapshot.callbacks)||!SameEntries(first.currentCallbacks,snapshot.currentCallbacks)){
        ++stereoRecovery.rejected;return false;
    }
    ++stereoRecovery.attempts;stereoRecovery.oldWorld=e.world;stereoRecovery.oldRequest=e.request;stereoRecovery.oldEye=e.eye;
    stereoRecovery.newWorld=world;stereoRecovery.newRequest=request;stereoRecovery.retiredViews=snapshot.graph.count;
    stereoRecovery.retiredPendingReferences=snapshot.graph.pendingEyeCount;
    StereoCheckpoint("owner_cleanup_begin");
    auto step=fvr::runtime::RetirementStep::Untouched;RetireStereoCalls calls{snapshot};
    const bool retired=fvr::runtime::RetireOwnedRenderRoot(calls,step);stereoRecovery.retirementStep=unsigned(step);
    if(!retired){stereoRecovery.failure=1;e.stage.store(StereoExperiment::Failed,std::memory_order_release);StereoCheckpoint("owner_cleanup_failed");return false;}
    ++stereoRecovery.completed;StereoCheckpoint("owner_cleanup_complete");
    stereoRecovery.completedOnStop=!sampling.load(std::memory_order_acquire);
    // Snapshot/receipts above own all retired metadata. Do not touch old native
    // addresses while clearing local state; the original main may now be reused.
    e.world=e.request=e.main=e.eye=0;e.firstFrame=e.retireFrame=e.refsBefore=e.refsAfter=0;
    e.redirects=e.restores=e.childrenBefore=e.childrenAfter=0;e.frame=0;e.drawMask=0;e.visibilityMask=0;
    e.listsRestored=e.callbacksRestored=e.initialized=e.camerasPreserved=e.clockPreserved=e.cachedViewsRestored=false;
    e.mainList={};e.allViews={};e.callbacks={};e.callbackValues={};tracked.lease={};tracked.anchorWritten=false;
    tracked.state.store(0,std::memory_order_release);stereoRecovery.policy.Reset();
    e.stage.store(stereoRecovery.completedOnStop.load()?StereoExperiment::Released:StereoExperiment::Idle,std::memory_order_release);
    return true;
}

void AdvanceStereo(std::uintptr_t world,std::uintptr_t request,unsigned incomingFrame)noexcept {
    auto& e=stereoExperiment;if(!stereoMode)return;
    const auto stage=e.stage.load(std::memory_order_acquire);
    if(stage==StereoExperiment::Armed||stage==StereoExperiment::Retiring){
        auto evidence=StereoProgressSample(unsigned(world),unsigned(request),incomingFrame);
        const auto ownerReason=fvr::bc2::CheckStereoProgressOwner(evidence,[](std::uintptr_t at)noexcept{return U32(at);});
        stereoProgressEvidence.Record(ownerReason,evidence);
        if(ownerReason!=fvr::bc2::StereoProgressReason::OwnerMatched){
            if(streamMode&&(world!=e.world||request!=e.request))RecoverStereoOwner(unsigned(world),unsigned(request),incomingFrame);
            return;
        }
        stereoRecovery.policy.Reset();
        if(stage==StereoExperiment::Armed&&incomingFrame>e.frame){
            if(streamMode&&sampling.load(std::memory_order_acquire)&&!tracked.restoreFailures.load()&&!frameBridge.Fatal()){BeginTrackedFrame(incomingFrame);return;}
            if(!streamMode&&burstMode&&sampling.load(std::memory_order_acquire)&&e.framesRendered.load(std::memory_order_acquire)<120&&e.drawMask.load(std::memory_order_acquire)==3){
                std::array<fvr::bc2::RenderViewCopy,2> camera{};
                if(DiagnosticCameras(e.main,camera)){
                    SetDiagnosticCameras(e.eye,camera);e.drawMask.store(0,std::memory_order_release);e.visibilityMask.store(0,std::memory_order_release);
                    e.frame.store(incomingFrame,std::memory_order_release);return;
                }
            }
            reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(reinterpret_cast<void*>(e.eye),false);
            RestoreBorrowed(e.callbacks,e.callbackValues,e.eye,e.redirects,e.restores);
            e.retireFrame=incomingFrame+2;e.stage.store(StereoExperiment::Retiring,std::memory_order_release);
        }else if(stage==StereoExperiment::Retiring&&incomingFrame>=e.retireFrame){
            if(!RestoreBorrowed(e.callbacks,e.callbackValues,e.eye,e.redirects,e.restores)){RecordStereoProgress(fvr::bc2::StereoProgressReason::RetirementBorrowedWait,incomingFrame);return;}
            if(U32(e.eye+0x20)!=1){RecordStereoProgress(fvr::bc2::StereoProgressReason::RetirementReferenceWait,incomingFrame);return;}
            if(streamMode&&!RefreshShutdownGeneration()){
                if(stereoShutdown.rejected==1||stereoShutdown.rejected%256==0)StereoCheckpoint("shutdown_generation_rejected");return;}
            StereoCheckpoint("before_release");
            reinterpret_cast<fvr::bc2::ViewRefFn>(imageBase+lifecycle.release)(reinterpret_cast<void*>(e.eye));
            StereoCheckpoint("after_release");e.cachedViewsRestored=RefreshRegisteredCallbacks();StereoCheckpoint("after_cached_view_refresh");
            if(streamMode)stereoShutdown.afterCaptured=stereoShutdown.after.Capture(request+0x14);
            StereoCheckpoint(e.mainList.Matches()?"main_list_restored":"main_list_changed");StereoCheckpoint(e.allViews.Matches()?"all_views_restored":"child_generation_changed");
            e.refsAfter=U32(request+4);e.listsRestored=e.mainList.Matches()&&e.allViews.Matches();e.callbacksRestored=e.callbacks.Matches();
            for(unsigned i=0;i<e.callbacks.count;++i){const auto& cb=e.callbackValues[i];e.callbacksRestored&=U32(cb.object)==cb.vtable;if(cb.remember)e.callbacksRestored&=U32(cb.object+0xbc)==cb.borrowed;if(cb.list)e.callbacksRestored&=cb.registered.Matches();}
            e.stage.store(StereoExperiment::Released,std::memory_order_release);
        }else stereoProgressEvidence.Record(stage==StereoExperiment::Armed?fvr::bc2::StereoProgressReason::FrameNotAdvanced:fvr::bc2::StereoProgressReason::RetirementFrameWait,evidence);
        return;
    }
    if(stage!=StereoExperiment::Idle||!sampling.load(std::memory_order_acquire)||!cameraExperiment.complete.load(std::memory_order_acquire)||!colorCaptures[0].complete.load(std::memory_order_acquire))return;
    if(U32(request)!=imageBase+lifecycle.requestVtable||U32(request+8)!=world||U32(request+0xc4)!=3||!e.mainList.Capture(request+0x24)||e.mainList.count!=1||!e.allViews.Capture(request+0x14))return;
    e.main=e.mainList.values[0];if(U32(e.main)!=imageBase+layout.vtable||U32(e.main+0x70)!=request||!SnapshotCallbacks(world,e.main,e.callbacks,e.callbackValues))return;
    std::array<fvr::bc2::RenderViewCopy,2> camera{};std::array<unsigned,4> viewport{};
    if(!Read(e.main+layout.viewportOffset,viewport.data(),16)||!viewport[2]||!viewport[3]||!DiagnosticCameras(e.main,camera))return;
    e.world=unsigned(world);e.request=unsigned(request);e.frame=incomingFrame;e.firstFrame=incomingFrame;e.refsBefore=U32(request+4);e.childrenBefore=ChildCount(e.main);
    // One-time GPU evidence belongs to the initial scene, not a later rebuilt owner.
    if(!e.framesRendered.load(std::memory_order_acquire))e.initialColorFrame=incomingFrame;
    static const char name[]="Fvr.SecondView.SingleFrame";const fvr::bc2::NativeStringRange range{name,name+sizeof(name)-1,name+sizeof(name)};
    void* fresh=reinterpret_cast<fvr::bc2::CreateViewFn>(imageBase+lifecycle.createView)(reinterpret_cast<void*>(request),&range);e.eye=reinterpret_cast<unsigned>(fresh);
    if(!fresh||U32(e.eye)!=imageBase+layout.vtable||U32(e.eye+0x70)!=request){e.stage.store(StereoExperiment::Failed,std::memory_order_release);return;}
    const auto refs=reinterpret_cast<fvr::bc2::ViewRefFn>(imageBase+lifecycle.addRef)(fresh);
    reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(fresh,false);
    if(refs!=1||!RestoreBorrowed(e.callbacks,e.callbackValues,e.eye,e.redirects,e.restores)){e.stage.store(StereoExperiment::Failed,std::memory_order_release);return;}
    using SetCamera=void(__thiscall*)(void*,const void*);using SetViewport=void(__thiscall*)(void*,const unsigned*);
    SetDiagnosticCameras(e.eye,camera);
    reinterpret_cast<SetViewport>(U32(U32(e.eye)+15*4))(fresh,viewport.data());
    const auto sceneRebuild=sceneTrace::journal.BeginRebuild({e.world,e.request,e.main,e.eye,incomingFrame},sceneTrace::Stamp);
    StereoCheckpoint("before_native_rebuild");
    const auto frameBefore=U32(world+0x88);const auto timeBefore=Hash(world+0x8c,8);
    const auto primaryBefore=Hash(e.main+layout.primaryOffset,0x460),secondaryBefore=Hash(e.main+layout.secondaryOffset,0x460);
    using Rebuild=void(__thiscall*)(void*);
    sceneTrace::Mark(sceneRebuild,SceneMilestoneStage::CallingOriginal);
    reinterpret_cast<Rebuild>(imageBase+initialization.rebuild)(reinterpret_cast<void*>(world));
    sceneTrace::Mark(sceneRebuild,SceneMilestoneStage::OriginalReturned);
    e.clockPreserved=frameBefore==U32(world+0x88)&&timeBefore==Hash(world+0x8c,8);
    e.camerasPreserved=primaryBefore==Hash(e.main+layout.primaryOffset,0x460)&&secondaryBefore==Hash(e.main+layout.secondaryOffset,0x460);
    const bool borrowedOkay=RestoreBorrowed(e.callbacks,e.callbackValues,e.eye,e.redirects,e.restores);
    e.childrenAfter=ChildCount(e.eye);
    bool generationOkay=CaptureOriginalGeneration(e.allViews,unsigned(request+0x14),e.eye,e.allViews.count);
    for(unsigned i=0;i<e.callbacks.count;++i){auto& cb=e.callbackValues[i];if(cb.list)generationOkay&=CaptureOriginalGeneration(cb.registered,cb.object+0x20,e.eye,cb.registered.count);}
    e.initialized=e.childrenAfter>0&&ChildCount(e.main)==e.childrenBefore&&generationOkay&&borrowedOkay&&e.camerasPreserved&&e.clockPreserved;
    sceneTrace::journal.CompleteRebuild(sceneRebuild,e.initialized,sceneTrace::Stamp);
    StereoCheckpoint("after_native_rebuild");
    if(!e.initialized){
        if(borrowedOkay&&U32(e.eye+0x20)==1){reinterpret_cast<fvr::bc2::ViewRefFn>(imageBase+lifecycle.release)(fresh);e.cachedViewsRestored=RefreshRegisteredCallbacks();}StereoCheckpoint("initialization_rejected");
        e.stage.store(StereoExperiment::Failed,std::memory_order_release);return;
    }
    if(initializationOnly){e.retireFrame=incomingFrame+120;e.stage.store(StereoExperiment::Retiring,std::memory_order_release);}
    else if(streamMode){
        if(frameBridge.TwoSourceSlotsEnabled()&&stereoRecovery.completed.load()&&
           !stereoRecovery.sourceTransition.Arm({{stereoRecovery.oldWorld.load(),stereoRecovery.oldRequest.load()},
                {e.world,e.request},incomingFrame,1})){
            stereoRecovery.sourceTransition.Reject();++stereoRecovery.sourceTransitionFailures;
            e.stage.store(StereoExperiment::Armed,std::memory_order_release);return;
        }
        BeginTrackedFrame(incomingFrame);e.stage.store(StereoExperiment::Armed,std::memory_order_release);
    }
    else {reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(fresh,true);e.stage.store(StereoExperiment::Armed,std::memory_order_release);}
}
void ProbeViewLifecycle(std::uintptr_t world,std::uintptr_t request)noexcept {
    auto& r=lifecycleResult;
    if(!lifecycleMode||r.claimed||!cameraExperiment.complete.load(std::memory_order_acquire)||!colorCaptures[0].complete.load(std::memory_order_acquire))return;
    if(U32(request)!=imageBase+lifecycle.requestVtable||U32(request+8)!=world||U32(request+0xc4)!=3)return;
    ListSnapshot main{},all{},callbacks{};std::array<CallbackSnapshot,16> callbackValues{};
    if(!main.Capture(request+0x24)||main.count!=1||!all.Capture(request+0x14))return;
    const auto mainView=main.values[0];if(U32(mainView)!=imageBase+layout.vtable||U32(mainView+0x70)!=request||!SnapshotCallbacks(world,mainView,callbacks,callbackValues))return;
    r.claimed=true;r.thread=GetCurrentThreadId();r.countBefore=main.count;r.allBefore=all.count;r.requestRefsBefore=U32(request+4);
    static const char name[]="Fvr.InactiveView.OwnershipV2";const fvr::bc2::NativeStringRange range{name,name+sizeof(name)-1,name+sizeof(name)};
    auto* fresh=reinterpret_cast<fvr::bc2::CreateViewFn>(imageBase+lifecycle.createView)(reinterpret_cast<void*>(request),&range);
    r.view=reinterpret_cast<unsigned>(fresh);r.created=fresh!=nullptr;
    if(fresh){const auto at=std::uintptr_t(r.view);r.refsBefore=U32(at+0x20);
        if(U32(at)==imageBase+layout.vtable&&U32(at+0x70)==request){
            r.refsOwned=reinterpret_cast<fvr::bc2::ViewRefFn>(imageBase+lifecycle.addRef)(fresh);
            reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(fresh,false);
            unsigned char active=1;Read(at+layout.activeOffset,&active,1);r.inactive=active==0;
            ListSnapshot during{};if(during.Capture(request+0x24)){r.countDuring=during.count;r.registered=during.count==main.count+1&&during.values[main.count]==at;}
            // The native remember-main callback has no removal action. Restore
            // only its exact pointer write BEFORE freeing the fresh view.
            const bool borrowedOkay=RestoreBorrowed(callbacks,callbackValues,r.view,r.borrowedRedirects,r.borrowedRestores);
            if(r.registered&&borrowedOkay&&r.refsOwned==1)r.refsReleased=reinterpret_cast<fvr::bc2::ViewRefFn>(imageBase+lifecycle.release)(fresh);
        }
    }
    r.ownerUnchanged=U32(request)==imageBase+lifecycle.requestVtable&&U32(request+8)==world&&U32(mainView+0x70)==request&&U32(request+0xc4)==3;
    ListSnapshot after{},allAfter{};if(after.Capture(request+0x24))r.countAfter=after.count;if(allAfter.Capture(request+0x14))r.allAfter=allAfter.count;
    r.restored=r.ownerUnchanged&&main.Matches();r.allRestored=all.Matches();r.requestRefsAfter=U32(request+4);r.callbacksRestored=callbacks.Matches();
    for(unsigned i=0;i<callbacks.count;++i){const auto& cb=callbackValues[i];r.callbacksRestored&=U32(cb.object)==cb.vtable;if(cb.remember)r.callbacksRestored&=U32(cb.object+0xbc)==cb.borrowed;if(cb.list)r.callbacksRestored&=cb.registered.Matches();}
    r.complete.store(true,std::memory_order_release);
}
void __fastcall UpdateHook(void* self,void*,void* request,unsigned frame,float dt,void* extra,void* out1,void* out2){
    if(sampling.load(std::memory_order_acquire)){updateCalls.fetch_add(1,std::memory_order_relaxed);ProbeViewLifecycle(reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request));}
    AdvanceStereo(reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),frame);
    RefreshDirtyLiveCaches(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request));
    const auto milestone=sceneTrace::Native(SceneMilestoneKind::WorldUpdate,SceneMilestoneKind::WorldUpdate,
        reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request));
    sceneTrace::Mark(milestone,SceneMilestoneStage::CallingOriginal);
    updateOriginal(self,request,frame,dt,extra,out1,out2); // One simulation update.
    sceneTrace::Mark(milestone,SceneMilestoneStage::OriginalReturned);
    if(deferredCleanup.load(std::memory_order_acquire)){
        const auto stage=stereoExperiment.stage.load(std::memory_order_acquire);
        if(stage!=StereoExperiment::Armed&&stage!=StereoExperiment::Retiring){
            if(hookEntries[4])MH_DisableHook(hookEntries[4]);
            if(streamMode&&bodyAmmoRenderer.Quiescent())for(unsigned index=0;index<3;++index)if(hookEntries[index])MH_DisableHook(hookEntries[index]);
        }
    }
    sceneTrace::Mark(milestone,SceneMilestoneStage::Exit);
}
std::optional<fvr::bc2::ReloadProducerView> ReloadProducerViewKey(std::uintptr_t world,std::uintptr_t request,std::uintptr_t view)noexcept {
    const auto& e=stereoExperiment;
    if(!fvr::bc2::ReloadProducerBinding().Enabled()||!streamMode||tracked.state.load(std::memory_order_acquire)!=1||
       world!=e.world||request!=e.request||(view!=e.main&&view!=e.eye)||
       U32(world+0x88)!=e.frame||U32(view)!=imageBase+layout.vtable||U32(view+0x70)!=request||U32(request+8)!=world)return {};
    return fvr::bc2::ReloadProducerView{world,request,view,e.frame,view==e.eye?1u:0u};
}
std::atomic<std::shared_ptr<const fvr::bc2::RigWorkerViewLease>> workerViewLease;
void PublishWorkerProducerViews(std::uintptr_t world,std::uintptr_t request) {
    // This runs only at the existing owned visibility boundary, where the
    // stereo control fields are already read. Workers consume copied keys.
    const auto left=ReloadProducerViewKey(world,request,stereoExperiment.main);
    const auto right=ReloadProducerViewKey(world,request,stereoExperiment.eye);
    const auto now=ReloadDrawNowNs();
    if(!left||!right||now<=0)return;
    fvr::bc2::RigWorkerViewLease value{{*left,*right},now,now+200000000};
    if(!fvr::bc2::LookupRigWorkerViewLease(value,unsigned(world),unsigned(request),unsigned(left->view),left->nativeFrame,now))return;
    auto previous=workerViewLease.load(std::memory_order_acquire);
    for(unsigned attempt=0;attempt<4;++attempt){
        // Repeated visibility for one frame cannot renew its first deadline.
        // CAS also prevents a delayed publisher from overwriting a newer frame.
        const auto next=std::make_shared<const fvr::bc2::RigWorkerViewLease>(
            fvr::bc2::AdvanceRigWorkerViewLease(previous.get(),value));
        if(workerViewLease.compare_exchange_weak(previous,next,std::memory_order_release,std::memory_order_acquire))return;
    }
}
std::optional<fvr::bc2::ReloadProducerView> LookupWorkerProducerView(unsigned world,unsigned request,unsigned view,unsigned frame)noexcept {
    if(!fvr::bc2::ReloadProducerBinding().Enabled()||tracked.state.load(std::memory_order_acquire)!=1)return {};
    const auto lease=workerViewLease.load(std::memory_order_acquire);
    if(!lease||U32(world+0x88)!=frame||U32(view)!=imageBase+layout.vtable||U32(view+0x70)!=request||U32(request+8)!=world)return {};
    return fvr::bc2::LookupRigWorkerViewLease(*lease,world,request,view,frame,ReloadDrawNowNs());
}
void __fastcall VisibilityHook(void* self,void*,void* request,void* view,void* params,void* extra,void* out1,void* out2){
    RenderCallbackScope callback;
    VisibilityRecord* record=nullptr;const auto world=reinterpret_cast<std::uintptr_t>(self),req=reinterpret_cast<std::uintptr_t>(request),object=reinterpret_cast<std::uintptr_t>(view);
    const auto milestone=sceneTrace::Native(SceneMilestoneKind::MainVisibility,SceneMilestoneKind::EyeVisibility,world,req,object);
    const std::array<unsigned,4> offsets{layout.primaryOffset,layout.secondaryOffset,layout.thirdOffset,layout.fourthOffset};
    if(sampling.load(std::memory_order_acquire)){
        visibilityCalls.fetch_add(1,std::memory_order_relaxed);const auto index=visibilityAllocated.fetch_add(1,std::memory_order_relaxed);
        if(index<visibilityRecords.size()){record=&visibilityRecords[index];record->beginQpc=Qpc();record->world=world;record->request=req;record->view=object;record->thread=GetCurrentThreadId();record->frame=U32(std::uintptr_t(U32(imageBase+profile.gameRendererGlobal))+0x1080);record->worldFrame=U32(world+0x88);record->requestBefore=U32(req+0xc4);
            record->ownerValid=U32(object)==imageBase+layout.vtable&&U32(object+layout.ownerRequestOffset)==req&&U32(req+8)==world;
            if(record->ownerValid)for(unsigned i=0;i<4;++i)record->cameraBefore[i]=Hash(object+offsets[i],0x460);
        }
    }
    Record identity{};identity.world=world;identity.request=req;identity.view=object;identity.worldFrame=U32(world+0x88);
    CameraPulseScope pulse(record&&record->ownerValid?&identity:nullptr,object,3);
    ApplyTrackedView(unsigned(world),unsigned(req),unsigned(object));
    const auto priorGatheringEye=gatheringEvidenceEye;
    if(streamMode&&tracked.state.load(std::memory_order_acquire)==1&&world==stereoExperiment.world&&req==stereoExperiment.request&&
       U32(world+0x88)==stereoExperiment.frame&&(object==stereoExperiment.main||object==stereoExperiment.eye)){
        gatheringEvidenceEye=object==stereoExperiment.eye?1:0;frameContextCounts[gatheringEvidenceEye]=0;frameContextOverflow[gatheringEvidenceEye]=false;
    }
    if(viewAnchorMode&&object==stereoExperiment.eye&&world==stereoExperiment.world&&req==stereoExperiment.request&&
       U32(object)==imageBase+layout.vtable&&U32(object+0x70)==req){
        // This native transform is distinct from the RenderView matrices. The
        // visibility routine forwards it to the child renderer (vt + b8).
        // The owned second view must not forward the constructor's identity.
        alignas(16) std::array<float,16> source{};
        const bool captured=tracked.state.load(std::memory_order_acquire)==1?Read(reinterpret_cast<std::uintptr_t>(tracked.right[0].bytes.data()+0x50),source.data(),64):Read(stereoExperiment.main+0x30,source.data(),64);
        if(captured&&std::all_of(source.begin(),source.end(),[](float x){return std::isfinite(x);})){
            using Setter=void(__thiscall*)(void*,const void*);
            reinterpret_cast<Setter>(imageBase+viewAnchor.setter)(view,source.data());++anchorCopies;
        }
    }
    const auto producerKey=ReloadProducerViewKey(world,req,object);
    if(producerKey)PublishWorkerProducerViews(world,req);
    std::optional<fvr::bc2::Bc2ReloadProducerBinding::Scope> producerScope;
    // A rejected nested call deliberately masks its parent's TLS association.
    if(fvr::bc2::ReloadProducerBinding().Enabled())producerScope.emplace(fvr::bc2::ReloadProducerBinding(),
        producerKey.value_or(fvr::bc2::ReloadProducerView{}),bool(producerKey),ReloadDrawNowNs());
    sceneTrace::Mark(milestone,SceneMilestoneStage::CallingOriginal);
    visibilityOriginal(self,request,view,params,extra,out1,out2);
    sceneTrace::Mark(milestone,SceneMilestoneStage::OriginalReturned);
    if(producerScope)producerScope->Complete(producerKey&&ReloadProducerViewKey(world,req,object)==producerKey,ReloadDrawNowNs());
    if(gatheringEvidenceEye==1)ObserveContexts(1);
    gatheringEvidenceEye=priorGatheringEye;
    pulse.Restore();
    if(stereoMode&&stereoExperiment.stage.load(std::memory_order_acquire)==StereoExperiment::Armed&&world==stereoExperiment.world&&req==stereoExperiment.request&&U32(world+0x88)==stereoExperiment.frame){
        if(object==stereoExperiment.main)stereoExperiment.visibilityMask.fetch_or(1,std::memory_order_release);
        if(object==stereoExperiment.eye)stereoExperiment.visibilityMask.fetch_or(2,std::memory_order_release);
        // Child shadow-view construction also invokes the remember-main callback.
        RestoreBorrowed(stereoExperiment.callbacks,stereoExperiment.callbackValues,stereoExperiment.eye,stereoExperiment.redirects,stereoExperiment.restores);
    }
    if(record){record->endQpc=Qpc();record->requestAfter=U32(req+0xc4);record->job1=U32(reinterpret_cast<std::uintptr_t>(out1));record->job2=U32(reinterpret_cast<std::uintptr_t>(out2));
        if(record->ownerValid)for(unsigned i=0;i<4;++i)record->cameraAfter[i]=Hash(object+offsets[i],0x460);
        record->complete.store(true,std::memory_order_release);
    }
    sceneTrace::Mark(milestone,SceneMilestoneStage::Exit);
}
void __fastcall WorldHook(void* self,void*,void* request){
    RenderCallbackScope callback;
    Record* record=nullptr;
    if(sampling.load(std::memory_order_acquire)){worldCalls.fetch_add(1,std::memory_order_relaxed);const auto index=allocated.fetch_add(1,std::memory_order_relaxed);
        if(index<records.size()){record=&records[index];record->beginQpc=Qpc();record->thread=GetCurrentThreadId();record->world=reinterpret_cast<std::uintptr_t>(self);record->request=reinterpret_cast<std::uintptr_t>(request);
            const auto game=U32(imageBase+profile.gameRendererGlobal);record->frame=U32(std::uintptr_t(game)+0x1080);record->worldFrame=U32(record->world+0x88);record->requestBefore=U32(record->request+0xc4);
            const auto begin=U32(record->request+0x24),end=U32(record->request+0x28);record->viewCount=end>=begin&&end-begin<=64?(end-begin)/4:UINT32_MAX;record->arenaBefore=Arena(record->world);}}
    auto* previous=current;current=record;
    if(poolOnly&&workPool.stage.load(std::memory_order_acquire)==0&&cameraExperiment.complete.load(std::memory_order_acquire)&&colorCaptures[0].complete.load(std::memory_order_acquire))ExpandWorkPool(U32(reinterpret_cast<unsigned>(self)+0x88));
    if(stereoMode&&(!streamMode||tracked.state.load(std::memory_order_acquire)==1)&&stereoExperiment.stage.load(std::memory_order_acquire)==StereoExperiment::Armed&&U32(reinterpret_cast<unsigned>(self)+0x88)==stereoExperiment.frame){
        if(burstMode&&workPool.stage.load(std::memory_order_acquire)==2)workPool.stage.store(0,std::memory_order_release);
        if(!ExpandWorkPool(stereoExperiment.frame)){
            // Visibility was built, but insufficient verified render storage must
            // cancel the extra draw before the engine can overflow its pool.
            reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(reinterpret_cast<void*>(stereoExperiment.eye),false);
        }
    }
    ObserveNativeFade(fvr::bc2::fadeDiagnostic::Phase::WorldBefore,reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request));
    worldOriginal(self,request); // Exactly one original call; no simulation/render replay.
    ObserveNativeFade(fvr::bc2::fadeDiagnostic::Phase::WorldAfter,reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request));
    if(workPool.stage.load(std::memory_order_acquire)==1&&GetCurrentThreadId()==workPool.thread){const auto begin=U32(workPool.owner+0x850),end=U32(workPool.owner+0x854);if(end>=begin&&end-begin<=workPool.storage.size())workPool.peak=(std::max)(workPool.peak,(end-begin)/40);}
    if(streamMode)FinishTrackedFrame(false,TrackedFinishCallsite::WorldReturn);
    current=previous;
    if(record){record->endQpc=Qpc();record->requestAfter=U32(record->request+0xc4);record->arenaAfter=Arena(record->world);record->complete.store(true,std::memory_order_release);}
}
void* __fastcall PrepareHook(void* self,void*,void* request,void* view){
    RenderCallbackScope callback;
    if(sampling.load(std::memory_order_relaxed))prepareCalls.fetch_add(1,std::memory_order_relaxed);
    void* data=nullptr;
    {ScopedContextEye constants;
        if(streamMode&&tracked.state.load(std::memory_order_acquire)==1&&reinterpret_cast<unsigned>(self)==stereoExperiment.world&&
           reinterpret_cast<unsigned>(request)==stereoExperiment.request&&reinterpret_cast<unsigned>(view)==stereoExperiment.main){
            if(!constants.Apply()){contextPatchFailures.fetch_add(1);tracked.capturesOkay=false;}
        }
        const auto producerKey=ReloadProducerViewKey(reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),reinterpret_cast<std::uintptr_t>(view));
        std::optional<fvr::bc2::Bc2ReloadProducerBinding::Scope> producerScope;
        if(fvr::bc2::ReloadProducerBinding().Enabled())producerScope.emplace(fvr::bc2::ReloadProducerBinding(),
            producerKey.value_or(fvr::bc2::ReloadProducerView{}),bool(producerKey),ReloadDrawNowNs());
        ObserveNativeFade(fvr::bc2::fadeDiagnostic::Phase::PrepareBefore,reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),reinterpret_cast<std::uintptr_t>(view));
        std::optional<WeatherBoundaryScope> weatherBoundary;
        if(weatherObservationEnabled)weatherBoundary.emplace(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request),reinterpret_cast<unsigned>(view),1);
        std::optional<WeatherTranslationScope> weatherTranslation;
        if(weatherTranslationEnabled||weatherTranslationRuntime)weatherTranslation.emplace(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request),reinterpret_cast<unsigned>(view),1);
        data=weatherTranslation?OriginalPrepareWithWeatherScope(self,request,view,&*weatherTranslation):prepareOriginal(self,request,view);
        ObserveNativeFade(fvr::bc2::fadeDiagnostic::Phase::PrepareAfter,reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),reinterpret_cast<std::uintptr_t>(view));
        if(producerScope)producerScope->Complete(producerKey&&ReloadProducerViewKey(reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),reinterpret_cast<std::uintptr_t>(view))==producerKey,ReloadDrawNowNs());
    }
    if(current&&current->request==reinterpret_cast<std::uintptr_t>(request)&&current->world==reinterpret_cast<std::uintptr_t>(self))++current->prepared;
    return data;
}
void __fastcall DrawHook(void* self,void*,void* request,void* view,void* data){
#if defined(FVR_BC2_ORDINARY_RESOURCE_SCENE_PROBE)
    fvr::bc2::graphicsThreadObservation::Scope graphicsThread(fvr::bc2::graphicsThreadObservation::Boundary::View);
#endif
    const auto milestone=sceneTrace::Native(SceneMilestoneKind::MainDraw,SceneMilestoneKind::EyeDraw,
        reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),reinterpret_cast<std::uintptr_t>(view));
    fvr::graphics::DiagnosticImageLifetime::Scope imageScope(diagnosticImageLifetime);
    if(!imageScope.Active()){sceneTrace::Mark(milestone,SceneMilestoneStage::CallingOriginal);drawOriginal(self,request,view,data);sceneTrace::Mark(milestone,SceneMilestoneStage::OriginalReturned);
        sceneTrace::Mark(milestone,SceneMilestoneStage::Exit);return;}
    RenderCallbackScope callback;
    PumpFrameBridge();
    if(sampling.load(std::memory_order_relaxed))drawCalls.fetch_add(1,std::memory_order_relaxed);
    auto* record=current;const auto address=reinterpret_cast<std::uintptr_t>(view);
    if(record&&(record->request!=reinterpret_cast<std::uintptr_t>(request)||record->world!=reinterpret_cast<std::uintptr_t>(self)))record=nullptr;
    if(record){++record->drawn;if(record->drawn==1&&U32(address)==imageBase+layout.vtable){record->view=address;record->preparedData=reinterpret_cast<std::uintptr_t>(data);
        record->cameraBefore=Hash(address+layout.primaryOffset,0x460);record->dataBefore=Hash(record->preparedData,0x64);record->viewItemsBefore=ItemCount(address);record->targetBefore=BoundTarget();ObserveChildren(*record);SampleCameraCopies(address+layout.primaryOffset);}}
    CameraPulseScope pulse(record,address);
    const bool trackedEvidence=streamMode&&tracked.state.load(std::memory_order_acquire)==1&&
        (address==stereoExperiment.main||address==stereoExperiment.eye);
    const unsigned evidenceEye=address==stereoExperiment.eye?1u:0u;
    const auto weatherKey=weatherObservationEnabled?weatherGate.Selected():weatherObservation::Key{};
    const bool weatherDrawOwned=weatherObservationEnabled&&WeatherDrawCurrent(weatherKey,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request),unsigned(address));
    if(weatherObservationEnabled&&weatherGate.Active(weatherKey)&&(address==weatherKey.main||address==weatherKey.eye)&&!weatherDrawOwned)
        weatherIdentityMismatches.fetch_add(1,std::memory_order_relaxed);
    fvr::bc2::stereoColorScale::Pair::Scope colorScope(stereoColorScalePair);
    fvr::bc2::stereoColorScale::EffectPair::Scope effectScope(stereoEffectPair);
    fvr::bc2::stereoColorScale::PostProcessPair::Scope postProcessScope(stereoPostProcessPair);
    const bool bodyAmmoEyeOwned=bodyAmmoObservationEnabled&&trackedEvidence&&
        reinterpret_cast<unsigned>(self)==stereoExperiment.world&&reinterpret_cast<unsigned>(request)==stereoExperiment.request&&
        U32(stereoExperiment.world+0x88)==stereoExperiment.frame&&U32(address)==imageBase+layout.vtable&&
        U32(address+0x70)==stereoExperiment.request&&U32(stereoExperiment.request+8)==stereoExperiment.world;
    const auto bodySourcesBefore=bodyAmmoEyeOwned?ReadBodySources():fvr::bc2::BodyPropSources{};
    const auto& bodyAmmoBefore=bodySourcesBefore.ammo;
    const auto bodyPairKey=bodyAmmoEyeOwned?fvr::bc2::CarriedPairKey(tracked.lease):fvr::bc2::BodyCarriedPairKey{};
    const auto bodyPair=bodyAmmoEyeOwned?tracked.bodyPropPair.Begin(bodyPairKey,evidenceEye,bodySourcesBefore):nullptr;
    const auto holsterBefore=trackedEvidence&&bodyHolsterProbeMode?
        fvr::bc2::gameplay::ReadBodyHolsterFixture(ReloadDrawNowNs()):nullptr;
    const auto visibilityBefore=trackedEvidence&&weaponVisibilityProbeMode?
        fvr::bc2::gameplay::ReadWeaponVisibilityProbe(ReloadDrawNowNs()):nullptr;
    if(trackedEvidence){ObserveContexts(evidenceEye+2);SaveTrackedCameraEvidence(unsigned(address),evidenceEye,1);BeginEyeGpuEvidence(evidenceEye);}
    if(trackedEvidence){
        fvr::bc2::ReloadDrawFrameEvidence frame{};
        if(destructionWitnessEnabled&&reinterpret_cast<unsigned>(self)==stereoExperiment.world&&
            reinterpret_cast<unsigned>(request)==stereoExperiment.request&&U32(stereoExperiment.world+0x88)==stereoExperiment.frame&&
            U32(address)==imageBase+layout.vtable&&U32(address+0x70)==stereoExperiment.request){
            const auto& expected=evidenceEye?tracked.right[0]:tracked.written[0];
            destructionWitness.BeginEye(observedImmediateContext,DestructionWitnessKey(),evidenceEye,address,expected.bytes);
        }
        frame.world=reinterpret_cast<std::uintptr_t>(self);frame.request=reinterpret_cast<std::uintptr_t>(request);
        frame.view=address;frame.nativeFrame=stereoExperiment.frame;frame.eye=evidenceEye;
        frame.nowNs=ReloadDrawNowNs();frame.tickMs=GetTickCount64();
        // Draw/request identity is observed here. No selected-mesh or skin
        // association is inferred from an index count or nearby pose timestamp.
        if(const auto owner=fvr::bc2::rigPublication::ReadReloadProducerOwner())
            if(const auto source=fvr::bc2::ReloadProducerBinding().Read({frame.world,frame.request,frame.view,frame.nativeFrame,frame.eye},*owner,frame.nowNs))frame.producer=source->evidence;
        reloadDrawCapture.BeginEye(observedImmediateContext,frame);
    }
    struct DrawEvidenceEnd {
        bool active=false;
        unsigned eye=0;fvr::graphics::SceneWitnessKey key{};
        void Finish()noexcept {if(active){active=false;reloadDrawCapture.EndEye();reloadDrawCapture.Poll(observedImmediateContext);
            if(destructionWitnessEnabled)destructionWitness.EndEye(observedImmediateContext,key,eye);}}
        ~DrawEvidenceEnd(){Finish();}
    } drawEvidenceEnd{trackedEvidence,evidenceEye,destructionWitnessEnabled?DestructionWitnessKey():fvr::graphics::SceneWitnessKey{}};
    const auto priorEvidenceEye=drawingEvidenceEye;
    if(trackedEvidence&&trackedCameraEvidence.frame.load(std::memory_order_acquire)==stereoExperiment.frame)drawingEvidenceEye=int(evidenceEye);
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
    const auto priorSceneDepthEye=sceneDepthDrawingEye;
    if(bodyAmmoEyeOwned&&bodyAmmoBefore&&evidenceEye==0&&!sceneDepthFrame){
        if(!sceneDepthFirstValidFrame)sceneDepthFirstValidFrame=stereoExperiment.frame;
        if(stereoExperiment.frame-sceneDepthFirstValidFrame>=120)sceneDepthFrame=stereoExperiment.frame;
    }
    sceneDepthDrawingEye=bodyAmmoEyeOwned&&sceneDepthFrame==stereoExperiment.frame?int(evidenceEye):-1;
#endif
#ifdef FVR_BC2_BODY_SCENE_DEPTH
    BodySceneDepthScope bodyDepth(bodyAmmoEyeOwned,evidenceEye);
#endif
    {ScopedContextEye constants;
        if(trackedEvidence&&evidenceEye==0&&reinterpret_cast<unsigned>(self)==stereoExperiment.world&&reinterpret_cast<unsigned>(request)==stereoExperiment.request){
            if(!constants.Apply()){contextPatchFailures.fetch_add(1);tracked.capturesOkay=false;}
        }
        std::optional<fvr::bc2::opticFilterRuntime::View> opticView;
        if(trackedEvidence&&reinterpret_cast<unsigned>(self)==stereoExperiment.world&&reinterpret_cast<unsigned>(request)==stereoExperiment.request)
            opticView=fvr::bc2::opticFilterRuntime::View{reinterpret_cast<unsigned>(request),unsigned(address),stereoExperiment.frame.load(),int(evidenceEye)};
        OpticViewScope opticScope(opticView);
        if(trackedEvidence&&!colorScope.Begin(CurrentColorScaleKey(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request)),evidenceEye))tracked.capturesOkay=false;
        if(trackedEvidence&&!effectScope.Begin(CurrentColorScaleKey(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request)),evidenceEye))tracked.capturesOkay=false;
        if(trackedEvidence&&!postProcessScope.Begin(CurrentColorScaleKey(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request)),evidenceEye))tracked.capturesOkay=false;
        const auto wasDrawing=drawingNativeView;drawingNativeView=true;sceneTrace::Mark(milestone,SceneMilestoneStage::CallingOriginal);
    ObserveNativeFade(fvr::bc2::fadeDiagnostic::Phase::DrawBefore,reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),reinterpret_cast<std::uintptr_t>(view));
    std::optional<WeatherBoundaryScope> weatherBoundary;
    if(weatherObservationEnabled)weatherBoundary.emplace(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request),unsigned(address),2);
    std::optional<WeatherTranslationScope> weatherTranslation;
    if(weatherTranslationEnabled||weatherTranslationRuntime)weatherTranslation.emplace(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request),unsigned(address),2);
    OriginalDrawWithColorScope(self,request,view,data,&colorScope,&effectScope,&postProcessScope,wasDrawing,weatherTranslation?&*weatherTranslation:nullptr);
    if(trackedEvidence&&!colorScope.DrawReturned(CurrentColorScaleKey(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request))))tracked.capturesOkay=false;
    if(trackedEvidence&&!effectScope.DrawReturned(CurrentColorScaleKey(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request))))tracked.capturesOkay=false;
    if(trackedEvidence&&!postProcessScope.DrawReturned(CurrentColorScaleKey(reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request))))tracked.capturesOkay=false;
    ObserveNativeFade(fvr::bc2::fadeDiagnostic::Phase::DrawAfter,reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),reinterpret_cast<std::uintptr_t>(view));
    sceneTrace::Mark(milestone,SceneMilestoneStage::OriginalReturned);
    }drawingEvidenceEye=priorEvidenceEye;
#ifdef FVR_BC2_BODY_SCENE_DEPTH
    bodyDepth.End();
#endif
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
    sceneDepthDrawingEye=priorSceneDepthEye;
#endif
    drawEvidenceEnd.Finish();
    if(trackedEvidence&&weaponVisibilityProbeMode&&weaponVisibilityEyeCount<weaponVisibilityEyes.size()){
        const auto ns=ReloadDrawNowNs();
        if(const auto sample=fvr::bc2::gameplay::ReadWeaponVisibilityProbe(ns);sample&&visibilityBefore&&
            sample->phase==visibilityBefore->phase&&sample->intent.request==visibilityBefore->intent.request&&
            sample->intent.nativeOwner==visibilityBefore->intent.nativeOwner){
            auto& row=weaponVisibilityEyes[weaponVisibilityEyeCount++];row.frame=stereoExperiment.frame;row.eye=evidenceEye;
            row.phase=unsigned(sample->phase);row.request=sample->intent.request;row.input=sample->intent.input.sequence;row.nowNs=ns;row.hidden=sample->intent.hide;
            // Phase is sampled, not asserted GPU visibility. A current exact
            // paired Pack receipt is recorded separately from eye pixels.
            if(sample->receipt&&sample->receipt->deadlineNs>ns){const auto& receipt=*sample->receipt;row.paired=true;
                row.draw=receipt.drawSerial;row.receiptInput=receipt.inputSequence;row.receiptObservedNs=receipt.observedNs;row.receiptDeadlineNs=receipt.deadlineNs;}
        }
    }
    if(trackedEvidence&&bodyHolsterProbeMode&&bodyHolsterEyeCount<bodyHolsterEyes.size()){
        const auto ns=ReloadDrawNowNs();const auto after=fvr::bc2::gameplay::ReadBodyHolsterFixture(ns);
        if(after&&after->actual&&holsterBefore&&holsterBefore->actual&&after->phase==holsterBefore->phase&&
            after->actual->request==holsterBefore->actual->request&&after->actual->nativeOwner==holsterBefore->actual->nativeOwner){
            const auto& s=*after->actual;auto& row=bodyHolsterEyes[bodyHolsterEyeCount++];row.frame=stereoExperiment.frame;row.eye=evidenceEye;
            row.phase=unsigned(after->phase);row.bodyPhase=unsigned(s.phase);row.request=s.request;row.input=s.hand.sequence;row.tick=s.nativeTick;row.nowNs=ns;
            row.right=s.right?s.right->token.id:0;row.left=s.left?s.left->token.id:0;row.free=bool(s.outcome.freeRight);row.pairedFree=s.pack.pairedCopies;
            row.suppressed=s.suppression&&fvr::bc2::HolsterSuppressionCurrent(*s.suppression,*s.suppression);
            if(s.visibility&&s.visibility->deadlineNs>ns){row.hidden=s.visibility->hidden;row.paired=s.visibility->verifiedCopyMask==3;
                row.draw=s.visibility->drawSerial;row.receiptDeadlineNs=s.visibility->deadlineNs;}
        }
    }
    if(trackedEvidence){EndEyeGpuEvidence(evidenceEye);SaveTrackedCameraEvidence(unsigned(address),evidenceEye,2);}
    if(weatherDrawOwned){
        const unsigned eye=address==weatherKey.eye?1u:0u;
        if(WeatherDrawCurrent(weatherKey,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request),unsigned(address))){
            auto& capture=weatherColorCaptures[eye];CaptureWorldColor(capture,weatherKey.frame);
            if(WeatherDrawCurrent(weatherKey,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(request),unsigned(address))&&
               capture.frame==weatherKey.frame&&(capture.step==ColorCapture::Step::Pending||capture.step==ColorCapture::Step::Finished))weatherGate.Image(weatherKey,eye);
            else weatherImageInvalidMask.fetch_or(1u<<eye,std::memory_order_relaxed);
        }else weatherImageInvalidMask.fetch_or(1u<<eye,std::memory_order_relaxed);
    }
    if(bodyAmmoEyeOwned&&tracked.state.load(std::memory_order_acquire)==1&&
        U32(stereoExperiment.world+0x88)==stereoExperiment.frame&&U32(address)==imageBase+layout.vtable&&
        U32(address+0x70)==stereoExperiment.request&&U32(stereoExperiment.request+8)==stereoExperiment.world){
        const auto bodySourcesAfter=ReadBodySources();const auto& after=bodySourcesAfter.ammo;
        const auto now=ReloadDrawNowNs();
        const fvr::bc2::BodyAmmoEyeKey key{stereoExperiment.world,stereoExperiment.request,address,stereoExperiment.frame.load(),evidenceEye};
        fvr::bc2::BodyAmmoBoundaryProof proof;proof.eye=key;proof.nativeDrawReturned=true;
        proof.immediateContext=reinterpret_cast<std::uintptr_t>(observedImmediateContext);
        proof.diagnosticQueryEnded=!eyeGpuEvidence[evidenceEye].begun||eyeGpuEvidence[evidenceEye].ended;
        // Native query lifetime and scene-depth/projection association are NOT
        // inferred from returning Draw. Empty evidence rejects extra drawing,
        // while this real boundary records source/RTV/DSV/layout gaps separately.
        bodyAmmoRenderer.EndEye(observedImmediateContext,key,bodyAmmoBefore?&*bodyAmmoBefore:nullptr,
            after?&*after:nullptr,now,proof);
        // Host-only composition packet: copying owned metadata cannot affect
        // native query state, depth or simulation. Image/metadata publication
        // remains one frame-channel transaction after exact restoration.
        fvr::graphics::BodyPropEye props;fvr::math::Matrix4 nativeView{},nativeProjection{};
        if(Read(address+layout.primaryOffset+0x220,&nativeView,64)&&Read(address+layout.primaryOffset+0x2e0,&nativeProjection,64)){
            if(const auto eye=fvr::bc2::CanonicalBodyPropEye(nativeView,nativeProjection)){
                props=*eye;
            }
        }
        // Optional per-eye ammo HUD telemetry is assigned to props.ammo here.
        // The pair helper preserves it independently of prop admission.
        props.ammo=fvr::bc2::gameplay::ReadAmmoCounter();
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
        // Diagnostic copies only: never bind a target, dispatch or draw. Preserve
        // matching native color/depth and the exact prop projection for review.
        if(sceneDepthFrame==stereoExperiment.frame&&!(sceneDepthMask&(1u<<evidenceEye))&&bodyAmmoBefore&&after){
            ComPtr<ID3D11DepthStencilView> dsv;observedImmediateContext->OMGetRenderTargets(0,nullptr,&dsv);
            ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> depth;
            if(dsv){dsv->GetResource(&resource);resource.As(&depth);}
            if(depth){
                sceneDepthProps[evidenceEye]=props;
                if(const auto instance=fvr::bc2::BodyAmmoHostInstance(*bodyAmmoBefore,*after,now)){
                    sceneDepthProps[evidenceEye].count=1;sceneDepthProps[evidenceEye].instances[0]=*instance;
                }
                CaptureWorldColor(sceneDepthCaptures[evidenceEye],sceneDepthFrame,depth.Get(),true);
                CaptureWorldColor(sceneDepthColors[evidenceEye],sceneDepthFrame);
                const unsigned offsets[]{layout.primaryOffset,layout.secondaryOffset,layout.thirdOffset,layout.fourthOffset};
                for(unsigned v=0;v<4;++v)if(Read(address+offsets[v],sceneDepthViewCopies[evidenceEye][v].data(),0x460))sceneDepthViewMask[evidenceEye]|=1u<<v;
                for(unsigned n=0;n<sceneDepthCandidateCounts[evidenceEye];++n){auto& c=sceneDepthCandidates[evidenceEye][n];
                    CaptureWorldColor(c.capture,sceneDepthFrame,c.texture.Get(),true);c.texture.Reset();}
                sceneDepthMask|=1u<<evidenceEye;
            }
        }
        for(unsigned n=0;n<2;++n){
            if(sceneDepthCaptures[n].step==ColorCapture::Step::Pending)CaptureWorldColor(sceneDepthCaptures[n],sceneDepthFrame,nullptr,true);
            if(sceneDepthColors[n].step==ColorCapture::Step::Pending)CaptureWorldColor(sceneDepthColors[n],sceneDepthFrame);
            for(unsigned i=0;i<sceneDepthCandidateCounts[n];++i){auto& c=sceneDepthCandidates[n][i].capture;
                if(c.step==ColorCapture::Step::Pending)CaptureWorldColor(c,sceneDepthFrame,nullptr,true);}
        }
#endif
        if(const auto pair=tracked.bodyPropPair.End(bodyPair,bodyPairKey,evidenceEye,bodySourcesBefore,bodySourcesAfter,props,ReloadDrawNowNs()))
            if(tracked.state.load(std::memory_order_acquire)==1&&fvr::bc2::CarriedPairKey(tracked.lease)==bodyPairKey&&
               U32(stereoExperiment.world+0x88)==stereoExperiment.frame&&U32(address+0x70)==stereoExperiment.request){
                frameBridge.SetBodyPropPair(tracked.lease,*pair);
            }
    }
    if(stereoMode){
        const auto& e=stereoExperiment;
        if((!streamMode||tracked.state.load(std::memory_order_acquire)==1)&&e.stage.load(std::memory_order_acquire)==StereoExperiment::Armed&&reinterpret_cast<unsigned>(self)==e.world&&reinterpret_cast<unsigned>(request)==e.request&&U32(e.world+0x88)==e.frame){
            if(address==e.main){if(streamMode&&tracked.state.load(std::memory_order_acquire)==1){const bool captured=CaptureTrackedEye(0);tracked.capturesOkay=tracked.capturesOkay&&captured;
                const bool capturesOkay=tracked.capturesOkay;
                const bool colorComplete=colorScope.Complete(CurrentColorScaleKey(e.world,e.request),capturesOkay);
                const bool effectComplete=effectScope.Complete(CurrentColorScaleKey(e.world,e.request),capturesOkay);
                const bool postProcessComplete=postProcessScope.Complete(CurrentColorScaleKey(e.world,e.request),capturesOkay);
                if(!colorComplete||!effectComplete||!postProcessComplete)tracked.capturesOkay=false;}CaptureWorldColor(colorCaptures[1],e.frame);stereoExperiment.drawMask.fetch_or(1,std::memory_order_release);}
            if(address==e.eye){if(streamMode&&tracked.state.load(std::memory_order_acquire)==1){const bool captured=tracked.capturesOkay&&CaptureTrackedEye(1);tracked.capturesOkay=tracked.capturesOkay&&captured;
                const bool capturesOkay=tracked.capturesOkay;
                const bool colorComplete=colorScope.Complete(CurrentColorScaleKey(e.world,e.request),capturesOkay);
                const bool effectComplete=effectScope.Complete(CurrentColorScaleKey(e.world,e.request),capturesOkay);
                const bool postProcessComplete=postProcessScope.Complete(CurrentColorScaleKey(e.world,e.request),capturesOkay);
                if(!colorComplete||!effectComplete||!postProcessComplete)tracked.capturesOkay=false;}CaptureWorldColor(colorCaptures[2],e.frame);if(stereoExperiment.drawMask.fetch_or(2,std::memory_order_acq_rel)==1)stereoExperiment.framesRendered.fetch_add(1,std::memory_order_release);if(streamMode)FinishTrackedFrame(true,TrackedFinishCallsite::RightEyeDraw);}
        }
        for(unsigned i=0;i<3;++i)if(colorCaptures[i].step==ColorCapture::Step::Pending||(i==0&&colorCaptures[0].step==ColorCapture::Step::Idle))CaptureWorldColor(colorCaptures[i],U32(reinterpret_cast<unsigned>(self)+0x88));
    }
    // Inactive-view diagnostic: inspect native output after registration/rebuild
    // without activating or drawing the additional view.
    if(initializationOnly&&stereoExperiment.initialized&&address==stereoExperiment.main&&
       stereoExperiment.stage.load(std::memory_order_acquire)==StereoExperiment::Retiring)
        CaptureWorldColor(colorCaptures[1],U32(reinterpret_cast<unsigned>(self)+0x88));
    if(!stereoMode&&record&&record->drawn==1&&record->view==address){
        const bool completedPulse=cameraPulse.complete.load(std::memory_order_acquire);
        if(pulse.Active()||(completedPulse&&cameraPulse.applied&&cameraPulse.frame==record->worldFrame))CaptureWorldColor(colorCaptures[1],record->worldFrame);
        else if(colorCaptures[1].step==ColorCapture::Step::Pending)CaptureWorldColor(colorCaptures[1],record->worldFrame);
        else if(completedPulse&&cameraPulse.applied&&colorCaptures[1].step==ColorCapture::Step::Finished)CaptureWorldColor(colorCaptures[2],record->worldFrame);
        else CaptureWorldColor(colorCaptures[0],record->worldFrame);
    }
    pulse.Restore();
    if(record&&record->drawn==1&&record->view==address){record->cameraAfter=Hash(address+layout.primaryOffset,0x460);record->dataAfter=Hash(record->preparedData,0x64);record->viewItemsAfter=ItemCount(address);record->targetAfter=BoundTarget();}
    sceneTrace::Mark(milestone,SceneMilestoneStage::Exit);
}
bool DisableProbeHooks()noexcept {
    if(weatherTranslationRuntime)weatherRuntimeStopping.store(true,std::memory_order_release);
    if(weatherObservationEnabled)weatherGate.Close();
    // A normal owned knife must return before its native observer/guards stop.
    // Failure preserves the dependencies and reports hooks_disabled=false.
    if(!fvr::bc2::gameplay::PrepareSessionStop())return false;
    graphicsBootstrap.Cancel();
    bodyAmmoRenderer.RequestStop();
    fvr::bc2::opticFilterRuntime::Disable();
    fvr::bc2::rigWorkerRuntime::Disable();
    workerViewLease.store({},std::memory_order_release);
    fvr::bc2::shellReloadAudio::Stop();
    const bool audioDisabled=fvr::bc2::mechanismAudio::Stop();
    const bool gameplayDisabled=fvr::bc2::gameplay::Stop();
    const bool menuDisabled=fvr::bc2::menu::Stop();
    deferredCleanup.store(true,std::memory_order_release);bool allDisabled=true;
    weatherHooksDisabled=true;
    for(unsigned index=0;index<hookEntries.size();++index){if(!hookEntries[index])continue;
        // Keep the owning graphics boundary until its private resources drain.
        if(index==2&&!bodyAmmoRenderer.Quiescent()){allDisabled=false;continue;}
        const auto stage=stereoExperiment.stage.load(std::memory_order_acquire);
        if((((streamMode&&index<3)||index==4)&&(stage==StereoExperiment::Armed||stage==StereoExperiment::Retiring))||(index==5&&workPool.stage.load(std::memory_order_acquire)==1)){allDisabled=false;continue;}
        const auto result=MH_DisableHook(hookEntries[index]);if(result!=MH_OK&&result!=MH_ERROR_DISABLED){allDisabled=false;if(index==14||index==15)weatherHooksDisabled=false;}
    }
    if(weatherObservationEnabled||weatherTranslationRuntime)for(unsigned attempt=0;attempt<200;++attempt){
        if(weatherCallbackLifetime.TryQuiesce(weatherHooksDisabled))break;Sleep(1);
    }
    bool provenanceDrained=false;
    for(unsigned attempt=0;attempt<200;++attempt){if(fvr::graphics::callProvenance::recorder.Stop()){provenanceDrained=true;break;}Sleep(1);}
    bool drawCaptureDrained=false;
    fvr::bc2::ReloadProducerBinding().Enable(false);
    for(unsigned attempt=0;attempt<200;++attempt){if(reloadDrawCapture.Stop()){drawCaptureDrained=true;break;}Sleep(1);}
    bool destructionDrained=false;
    for(unsigned attempt=0;attempt<200;++attempt){if(destructionWitness.Stop()){destructionDrained=true;break;}Sleep(1);}
    bool workerDrained=false;
    for(unsigned attempt=0;attempt<200;++attempt){if(fvr::bc2::rigWorkerRuntime::Quiescent()){workerDrained=true;break;}Sleep(1);}
    bool opticDrained=false;
    for(unsigned attempt=0;attempt<200;++attempt){if(fvr::bc2::opticFilterRuntime::Quiescent()){opticDrained=true;break;}Sleep(1);}
    const bool audioDrained=fvr::bc2::mechanismAudio::DrainAndRemove();
    const bool shellAudioDrained=fvr::bc2::shellReloadAudio::Drain();
    return allDisabled&&(!(weatherObservationEnabled||weatherTranslationRuntime)||weatherCallbackLifetime.Quiescent())&&gameplayDisabled&&menuDisabled&&audioDisabled&&audioDrained&&shellAudioDrained&&provenanceDrained&&drawCaptureDrained&&destructionDrained&&workerDrained&&opticDrained&&bodyAmmoRenderer.Quiescent();
}
void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
std::size_t Offset(const fvr::engine::PeImage& pe,unsigned rva,std::size_t count){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&count<=s.rawSize-(rva-s.rva))return std::size_t(s.rawOffset)+rva-s.rva;throw std::runtime_error("Unbacked profile address");}
void WriteTarget(std::ostream& out,const Target& t){out<<"{\"width\":"<<t.width<<",\"height\":"<<t.height<<",\"format\":"<<t.format<<",\"samples\":"<<t.samples<<'}';}
void WriteWeatherKey(std::ostream& out,const weatherObservation::Key& key){
    out<<"{\"world\":"<<key.world<<",\"request\":"<<key.request<<",\"main\":"<<key.main<<",\"eye_view\":"<<key.eye<<",\"native_frame\":"<<key.frame<<'}';
}
void WriteWeatherPacket(std::ostream& out,const weatherTranslation::Packet& p){
    out<<"{\"packet\":"<<p.address<<",\"owner\":"<<p.owner<<",\"packet_owner\":"<<p.packetOwner<<",\"world\":"<<p.world<<",\"flags\":"<<p.flags
       <<",\"arena\":"<<p.arena<<",\"base\":"<<p.base<<",\"capacity\":"<<p.capacity<<",\"used\":"<<p.used<<",\"matrix\":[";
    for(unsigned i=0;i<16;++i){if(i)out<<',';out<<p.matrix[i];}out<<"]}";
}
void WriteWeatherTranslation(const std::filesystem::path& output){
    if(!weatherTranslationEnabled)return;
    std::ofstream out(output.parent_path()/"weather-translation-experiment.json");
    const auto count=(std::min)(weatherTranslationAttempts.load(),unsigned(weatherTranslationEvidence.size()));
    unsigned incomplete=0,applied=0,restored=0;
    for(unsigned i=0;i<count;++i){const auto& e=weatherTranslationEvidence[i];if(!e.ready.load(std::memory_order_acquire)){++incomplete;continue;}
        applied+=e.applied;restored+=e.restored;}
    out<<"{\"schema\":\"fvr.bc2.weather-translation423.v1\",\"enabled\":true,\"selected\":";WriteWeatherKey(out,weatherGate.Selected());
    out<<",\"attempt_count\":"<<weatherTranslationAttempts.load()<<",\"overflow\":"<<weatherTranslationOverflow.load()<<",\"failed\":"<<weatherTranslationFailed.load()
       <<",\"callbacks_drained\":"<<weatherCallbackLifetime.Quiescent()<<",\"incomplete_attempts\":"<<incomplete<<",\"applied_scopes\":"<<applied
       <<",\"restored_scopes\":"<<restored<<",\"native_write_calls\":"<<weatherTranslationWrites.load()<<",\"translation_bytes_per_packet\":12,\"attempts\":[";
    bool first=true;
    for(unsigned i=0;i<count;++i){const auto& e=weatherTranslationEvidence[i];if(!e.ready.load(std::memory_order_acquire))continue;
        if(!first)out<<',';first=false;
        out<<"{\"phase\":"<<e.phase<<",\"eye\":"<<e.eye<<",\"thread\":"<<e.thread<<",\"reason\":\""<<e.reason<<"\",\"applied\":"<<e.applied<<",\"restored\":"<<e.restored
           <<",\"written_mask\":"<<e.writtenMask<<",\"unresolved_mask\":"<<e.unresolvedMask<<",\"before_valid_mask\":"<<e.beforeMask<<",\"inside_valid_mask\":"<<e.insideMask<<",\"after_valid_mask\":"<<e.afterMask
           <<",\"key_before\":";WriteWeatherKey(out,e.beforeKey);out<<",\"key_inside\":";WriteWeatherKey(out,e.insideKey);out<<",\"key_after\":";WriteWeatherKey(out,e.afterKey);
        for(unsigned step=0;step<3;++step){out<<(step==0?",\"packets_before\":[":(step==1?",\"packets_inside\":[":",\"packets_after\":["));
            const auto& packets=step==0?e.before:(step==1?e.inside:e.after);for(unsigned p=0;p<2;++p){if(p)out<<',';WriteWeatherPacket(out,packets[p]);}out<<']';}
        out<<'}';
    }out<<"]}\n";out.close();Require(bool(out),"Write weather translation experiment");
}
void WriteWeatherRuntimePair(std::ostream& out,const WeatherRuntimePair& pair){
    out<<"{\"epoch\":"<<pair.token.epoch<<",\"mask\":"<<pair.mask<<",\"selected\":";WriteWeatherKey(out,pair.token.key);out<<",\"attempts\":[";
    if(pair.mask==3)for(unsigned i=0;i<2;++i){if(i)out<<',';const auto& e=pair.scopes[i];
        out<<"{\"phase\":"<<e.phase<<",\"eye\":"<<e.eye<<",\"thread\":"<<e.thread<<",\"reason\":\""<<e.reason<<"\",\"applied\":"<<e.applied<<",\"restored\":"<<e.restored<<",\"validated\":"<<e.validated
           <<",\"written_mask\":"<<e.writtenMask<<",\"unresolved_mask\":"<<e.unresolvedMask<<",\"before_valid_mask\":"<<e.beforeMask<<",\"inside_valid_mask\":"<<e.insideMask<<",\"after_valid_mask\":"<<e.afterMask
           <<",\"key_before\":";WriteWeatherKey(out,e.beforeKey);out<<",\"key_inside\":";WriteWeatherKey(out,e.insideKey);out<<",\"key_after\":";WriteWeatherKey(out,e.afterKey);
        for(unsigned step=0;step<3;++step){out<<(step==0?",\"packets_before\":[":(step==1?",\"packets_inside\":[":",\"packets_after\":["));
            const auto& packets=step==0?e.before:(step==1?e.inside:e.after);for(unsigned p=0;p<2;++p){if(p)out<<',';WriteWeatherPacket(out,packets[p]);}out<<']';}out<<'}';
    }out<<"]}";
}
void WriteWeatherRuntime(const std::filesystem::path& output){
    if(!weatherTranslationRuntime)return;
    std::ofstream out(output.parent_path()/"weather-translation-runtime.json");LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    out<<"{\"schema\":\"fvr.bc2.weather-translation424.v1\",\"enabled\":true,\"stopping\":"<<weatherRuntimeStopping.load()<<",\"callbacks_drained\":"<<weatherCallbackLifetime.Quiescent()
       <<",\"frames_started\":"<<weatherRuntimeFrames.load()<<",\"frames_completed\":"<<weatherRuntimeCompleted.load()<<",\"frames_skipped\":"<<weatherRuntimeSkipped.load()
       <<",\"scope_attempts\":"<<weatherRuntimeScopes.load()<<",\"scopes_applied\":"<<weatherRuntimeApplied.load()<<",\"scopes_restored\":"<<weatherRuntimeRestored.load()
       <<",\"restore_failures\":"<<weatherRuntimeRestoreFailures.load()<<",\"rejected_publishes\":"<<weatherRuntimeRejectedPublish.load()<<",\"native_write_calls\":"<<weatherTranslationWrites.load()
       <<",\"translation_bytes_per_packet\":12,\"overhead_frequency\":"<<frequency.QuadPart<<",\"overhead_total_ticks\":"<<weatherRuntimeOverheadTotal.load()<<",\"overhead_max_ticks\":"<<weatherRuntimeOverheadMax.load()
       <<",\"overhead_excludes_native_calls\":true,\"claim_skips\":{";
    constexpr std::array<const char*,7> skips{"none","contended","stale","invalid","busy","missing","phase"};
    for(unsigned i=0;i<skips.size();++i){if(i)out<<',';out<<'"'<<skips[i]<<"\":"<<weatherRuntimeClaimSkips[i].load();}out<<"},\"scope_rejections\":{";
    for(unsigned i=0;i<weatherRuntimeReasonNames.size();++i){if(i)out<<',';out<<'"'<<weatherRuntimeReasonNames[i]<<"\":"<<weatherRuntimeReasons[i].load();}
    out<<"},\"first\":";WriteWeatherRuntimePair(out,weatherRuntimeFirst);out<<",\"selected\":";WriteWeatherRuntimePair(out,weatherRuntimeSelected);
    out<<",\"latest\":";WriteWeatherRuntimePair(out,weatherRuntimeLatest);out<<"}\n";out.close();Require(bool(out),"Write weather runtime correction");
}
void WriteWeatherObservation(const std::filesystem::path& output){
    if(!weatherObservationEnabled)return;
    std::ofstream out(output.parent_path()/"weather-observation.json");
    const auto key=weatherGate.Selected();
    const std::array<unsigned,2> reserved{weatherGate.Count(0),weatherGate.Count(1)};
    std::array<unsigned,2> ready{};
    for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<reserved[eye];++i)if(weatherRows[eye][i].ready.load(std::memory_order_acquire))++ready[eye];
    out<<"{\"schema\":\"fvr.bc2.weather-observer422.v1\",\"observer_only\":"<<((weatherTranslationEnabled||weatherTranslationRuntime)?"false":"true")<<",\"native_writes\":"<<weatherTranslationWrites.load()<<",\"translation_experiment\":"<<(weatherTranslationEnabled?"true":"false")<<",\"translation_runtime\":"<<(weatherTranslationRuntime?"true":"false")<<",\"delay_ms\":"<<weatherObservationDelayMs
        <<",\"state\":"<<weatherGate.State()<<",\"world\":"<<key.world<<",\"request\":"<<key.request<<",\"main\":"<<key.main<<",\"eye_view\":"<<key.eye<<",\"native_frame\":"<<key.frame
        <<",\"pair_published\":"<<weatherGate.Published()<<",\"image_scheduled_mask\":"<<weatherGate.Images()<<",\"overflow\":"<<weatherGate.Overflow()<<",\"unattributed\":"<<weatherUnattributed.load()
        <<",\"admission_closed\":"<<weatherGate.Closed()<<",\"observer_hooks_disabled\":"<<weatherHooksDisabled<<",\"callbacks_drained\":"<<weatherCallbackLifetime.Quiescent()
        <<",\"reserved_rows\":["<<reserved[0]<<','<<reserved[1]<<"],\"ready_rows\":["<<ready[0]<<','<<ready[1]<<"],\"incomplete_rows\":"<<(reserved[0]+reserved[1]-ready[0]-ready[1])
        <<",\"identity_mismatches\":"<<weatherIdentityMismatches.load()<<",\"image_invalid_mask\":"<<weatherImageInvalidMask.load()
        <<",\"matrix_encoding\":\"uint32 little-endian IEEE754 bits; position uses first4 values; cameraTransforms position indices12..14\",\"phases\":{\"0\":\"eye inferred from root camera; no enclosing render boundary proven\",\"1\":\"Prepare\",\"2\":\"Draw\"},\"matrix_snapshot_atomic\":false,\"rows\":[";
    const auto values=[&](const auto& array){out<<'[';for(unsigned i=0;i<array.size();++i){if(i)out<<',';out<<array[i];}out<<']';};
    bool first=true;
    for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<reserved[eye];++i){const auto& row=weatherRows[eye][i];if(!row.ready.load(std::memory_order_acquire))continue;
        if(!first)out<<',';first=false;const auto& p=row.value;
        out<<"{\"eye\":"<<eye<<",\"index\":"<<i<<",\"kind\":"<<p.kind<<",\"thread\":"<<p.thread<<",\"phase\":"<<p.phase<<",\"root_eye\":"<<p.rootEye<<",\"invocation\":"<<p.invocation
            <<",\"callsite\":"<<p.callsite<<",\"valid_mask\":"<<p.validMask<<",\"active_after\":"<<p.activeAfter<<",\"args\":";values(p.args);
        out<<",\"camera_transforms\":[";values(p.cameraTransforms[0]);out<<',';values(p.cameraTransforms[1]);out<<"],\"context\":"<<p.context<<",\"input\":"<<p.input<<",\"input_world\":";values(p.inputWorld);
        out<<",\"addresses_world_view_projection_position\":";values(p.addresses);out<<",\"values_world_view_projection_position\":[";
        for(unsigned part=0;part<4;++part){if(part)out<<',';values(p.values[part]);}out<<"]}";
    }
    out<<"],\"images\":[";
    for(unsigned eye=0;eye<2;++eye){if(eye)out<<',';auto& capture=weatherColorCaptures[eye];
        const bool complete=capture.complete.load(std::memory_order_acquire),captured=complete&&capture.step==ColorCapture::Step::Finished;
        out<<"{\"eye\":"<<eye<<",\"captured\":"<<captured<<",\"pending\":"<<!complete
            <<",\"world\":"<<key.world<<",\"request\":"<<key.request<<",\"view\":"<<(eye?key.eye:key.main)
            <<",\"identity_valid\":"<<((weatherGate.Images()&(1u<<eye))&&!(weatherImageInvalidMask.load()&(1u<<eye)));
        if(complete){out<<",\"native_frame\":"<<capture.frame<<",\"hresult\":"<<capture.error<<",\"target\":";WriteTarget(out,capture.target);}
        if(captured){const auto name="weather-eye-"+std::to_string(eye)+".rgba";out<<",\"file\":\""<<name<<"\"";
            std::ofstream pixels(output.parent_path()/name,std::ios::binary);pixels.write(reinterpret_cast<const char*>(capture.rgba.data()),std::streamsize(capture.rgba.size()));pixels.close();Require(bool(pixels),"Write weather eye pixels");capture.serialized=true;}
        out<<'}';
    }out<<"]}\n";out.close();Require(bool(out),"Write weather observation");
}
}
extern "C" DWORD WINAPI FvrRunNativeProbe(void* raw){
    fvr::bc2::NativeProbeConfig config{};
    if(!Read(reinterpret_cast<std::uintptr_t>(raw),&config,sizeof(config)))return 10;
    const bool continuous=(config.flags&0x400u)!=0;const auto mode=config.flags&0xffu;passEvidenceEnabled=(config.flags&0x100u)!=0;uncapMirror=(config.flags&0x200u)!=0;
    if(config.magic!=0x32504246||config.bytes!=sizeof(config)||!fvr::bc2::ValidOrdinaryResourceInputSession(config.ordinaryResourceInput,config.flags,config.durationMs,config.magazineReloadSession,unsigned(config.pumpHoldDiagnostic),unsigned(config.boatHeadAim),unsigned(config.bodyHolsterDiagnostic),config.m95StockShot,config.m95PhysicalBoltCycles,config.m95OrdinaryBoltInputCycles)||!fvr::bc2::ValidM95OrdinaryBoltInputSession(config.m95OrdinaryBoltInputCycles,config.flags,config.durationMs,config.magazineReloadSession,unsigned(config.pumpHoldDiagnostic),unsigned(config.boatHeadAim),unsigned(config.bodyHolsterDiagnostic),config.m95StockShot,config.m95PhysicalBoltCycles)||!fvr::bc2::ValidM95PhysicalBoltSession(config.m95PhysicalBoltCycles,config.flags,config.durationMs,config.magazineReloadSession,unsigned(config.pumpHoldDiagnostic),unsigned(config.boatHeadAim),unsigned(config.bodyHolsterDiagnostic),config.m95StockShot)||!fvr::bc2::ValidM95StockShotSession(config.m95StockShot,config.flags,config.durationMs,config.magazineReloadSession,unsigned(config.pumpHoldDiagnostic),unsigned(config.boatHeadAim),unsigned(config.bodyHolsterDiagnostic))||!fvr::bc2::ValidPumpHoldDiagnostic(config.pumpHoldDiagnostic,config.flags,config.durationMs)||!fvr::bc2::ValidBodyHolsterDiagnosticConfig(config.bodyHolsterDiagnostic,config.flags)||!fvr::bc2::ValidBoatHeadAimConfig(config.boatHeadAim,config.flags,config.magazineReloadSession)||!fvr::bc2::ValidMagazineReloadSession(config.magazineReloadSession,config.flags,config.durationMs)||!fvr::bc2::ValidBodyHolsterProbeConfig(config.flags,config.durationMs)||!fvr::bc2::ValidBodyInventoryConfig(config.flags)||!fvr::bc2::ValidWeaponVisibilityProbeConfig(config.flags,config.durationMs)||!fvr::bc2::ValidOpticFilterProbeConfig(config.flags,config.durationMs)||((config.flags&0xc00000u)==0xc00000u)||((config.flags&0xc00000u)&&((config.flags&0x268400u)||!(config.flags&0x10000u)||config.durationMs>15000))||!fvr::bc2::ValidReloadRequestProbeConfig(config.flags,config.durationMs)||!fvr::bc2::ValidPhysicalReloadConfig(config.flags)||!fvr::bc2::ValidPhysicalReloadProbeConfig(config.flags,config.durationMs)||!fvr::bc2::ValidPhysicalReloadRepeatProbeConfig(config.flags,config.durationMs)||((config.flags&0x200000u)&&!(config.flags&0x100000u))||((config.flags&0x100000u)&&!(config.flags&0x80000u))||((config.flags&0x80000u)&&!(config.flags&0x10000u))||((config.flags&0x40000u)&&((config.flags&0x400u)||!(config.flags&0x1000u)))||((config.flags&0x20000u)&&((config.flags&0x400u)||!(config.flags&0x1000u)))||mode>9||((config.flags&0x10000u)&&((config.flags&0x8000u)||(config.flags&0x6000u)!=0x6000u))||((config.flags&0x8000u)&&((config.flags&0x400u)||!(config.flags&0x4000u)))||((config.flags&0x1000u)&&!(config.flags&0x800u))||((config.flags&0x1e000u)&&!(config.flags&0x1000u))||
       ((passEvidenceEnabled||uncapMirror||continuous||(config.flags&0xfff800u))&&mode!=9)||
       (continuous?!config.hostPid:(config.durationMs<100||config.durationMs>(mode==9?60000u:5000u)))||config.reportPath[511]||config.frameChannel[63])return 10;
#ifdef FVR_BC2_RESOURCE_INVENTORY_PROBE
    if(!fvr::bc2::ValidResourceInventorySession(config.magazineReloadSession,config.flags,config.durationMs)||
       config.pumpHoldDiagnostic!=fvr::bc2::PumpHoldDiagnostic::Disabled||
       config.bodyHolsterDiagnostic!=fvr::bc2::BodyHolsterDiagnosticProfile::Disabled)return 10;
#elif defined(FVR_BC2_RESOURCE_MAGAZINES)
    if(!fvr::bc2::ValidResourceMagazineSession(config.magazineReloadSession,config.flags,config.durationMs)||
       config.pumpHoldDiagnostic!=fvr::bc2::PumpHoldDiagnostic::Disabled||
       config.bodyHolsterDiagnostic!=fvr::bc2::BodyHolsterDiagnosticProfile::Disabled)return 10;
#endif
#ifdef FVR_BC2_PLAYER_GRENADE_NORMAL
    constexpr bool normalPlayerGrenadesCompiled=true;
#else
    constexpr bool normalPlayerGrenadesCompiled=false;
#endif
    if(!fvr::bc2::ValidNormalPlayerGrenades(config.normalPlayerGrenades,config.flags,config.hostPid,
        config.magazineReloadSession,config.chestKnifeProbe||config.ordinaryGrenadeInput||config.ordinaryResourceInput||
        config.m95OrdinaryBoltInputCycles||config.m95PhysicalBoltCycles||config.m95StockShot||
        unsigned(config.bodyHolsterDiagnostic)||unsigned(config.pumpHoldDiagnostic),normalPlayerGrenadesCompiled))return 10;
#ifdef FVR_BC2_PLAYER_KNIFE_NORMAL
    constexpr bool normalKnifeCompiled=true;
#else
    constexpr bool normalKnifeCompiled=false;
#endif
#ifdef FVR_BC2_PLAYER_CONTEXTUAL_NORMAL
    constexpr bool normalContextualCompiled=true;
#else
    constexpr bool normalContextualCompiled=false;
#endif
    if(!fvr::bc2::ValidNormalPlayerTools(config.normalPlayerKnife,config.normalContextualTools,config.flags,config.hostPid,
        config.magazineReloadSession,config.chestKnifeProbe||config.ordinaryGrenadeInput||config.ordinaryResourceInput||
        config.m95OrdinaryBoltInputCycles||config.m95PhysicalBoltCycles||config.m95StockShot||
        unsigned(config.bodyHolsterDiagnostic)||unsigned(config.pumpHoldDiagnostic),normalKnifeCompiled,normalContextualCompiled))return 10;
#ifdef FVR_BC2_CHEST_KNIFE_PROBE
    constexpr bool knifeCompiled=true;
#else
    constexpr bool knifeCompiled=false;
#endif
    if(!fvr::bc2::ValidChestKnifeProbe(config.chestKnifeProbe,config.flags,config.durationMs,config.magazineReloadSession,
        continuous||config.normalPlayerGrenades||config.normalPlayerKnife||config.normalContextualTools||config.ordinaryGrenadeInput||config.ordinaryResourceInput||config.m95OrdinaryBoltInputCycles||
        config.m95PhysicalBoltCycles||config.m95StockShot||unsigned(config.boatHeadAim)||unsigned(config.bodyHolsterDiagnostic)||
        unsigned(config.pumpHoldDiagnostic),knifeCompiled))return 10;
#ifdef FVR_BC2_CONTEXTUAL_TOOL_PROBE
    constexpr bool contextualFixtureCompiled=true;
#else
    constexpr bool contextualFixtureCompiled=false;
#endif
    (void)contextualFixtureCompiled;
#ifdef FVR_BC2_CONTEXTUAL_TOOL_PROBE
    if(!fvr::bc2::ValidContextualToolFixture(config.flags,config.durationMs,config.magazineReloadSession,
        continuous||config.normalPlayerGrenades||config.normalPlayerKnife||config.normalContextualTools||config.chestKnifeProbe||config.ordinaryGrenadeInput||config.ordinaryResourceInput||
        config.m95OrdinaryBoltInputCycles||config.m95PhysicalBoltCycles||config.m95StockShot||unsigned(config.boatHeadAim)||
        unsigned(config.bodyHolsterDiagnostic)||unsigned(config.pumpHoldDiagnostic)))return 10;
#endif
    if(InterlockedCompareExchange(&started,1,0))return 11;
    if(config.ordinaryGrenadeInput){
#ifdef FVR_BC2_ORDINARY_GRENADE_PROBE
        constexpr bool stockCompiled=true;
#else
        constexpr bool stockCompiled=false;
#endif
#if defined(FVR_BC2_PLAYER_GRENADE) && !defined(FVR_BC2_PLAYER_GRENADE_NORMAL)
        constexpr bool playerCompiled=true;
#else
        constexpr bool playerCompiled=false;
#endif
#if defined(FVR_BC2_ORDINARY_GRENADE_FADE_PROBE) && defined(FVR_BC2_NATIVE_FADE_DIAGNOSTIC)
        constexpr bool combinedCompiled=true;
#else
        constexpr bool combinedCompiled=false;
#endif
#if defined(FVR_BC2_TWO_GRENADE_DEATH_PROBE) && defined(FVR_BC2_NATIVE_FADE_DIAGNOSTIC)
        constexpr bool twoDeathCompiled=true;
#else
        constexpr bool twoDeathCompiled=false;
#endif
        if(!((config.ordinaryGrenadeInput==3||config.ordinaryGrenadeInput==4)?playerCompiled:stockCompiled)||
           !fvr::bc2::ValidOrdinaryGrenadeInputSession(config.ordinaryGrenadeInput,config.flags,config.durationMs,
           config.magazineReloadSession,continuous||config.ordinaryResourceInput||config.m95OrdinaryBoltInputCycles||
           config.m95PhysicalBoltCycles||config.m95StockShot||unsigned(config.boatHeadAim)||
           unsigned(config.bodyHolsterDiagnostic)||unsigned(config.pumpHoldDiagnostic),combinedCompiled,playerCompiled,twoDeathCompiled))return 10;
    }
    cameraPulseMode=mode<=2?mode:0;lifecycleMode=mode==4;streamMode=mode==9;streamToken=config.frameChannel;stereoMode=mode==5||mode==6||mode==8||streamMode;burstMode=mode==8||streamMode;initializationOnly=mode==6;poolOnly=mode==7;
    opticEvidenceEnabled=bool(config.flags&0x8000000u);
    bool hooksEnabled=false,disabled=true,workerObservation=false;std::ofstream report;fvr::platform::ProcessLifetime hostLifetime;
    try {
        const auto output=std::filesystem::path(config.reportPath);Require(output.is_absolute()&&!std::filesystem::exists(output),"Report must be a new absolute path");report.open(output);Require(bool(report),"Cannot create report");
        wchar_t provenanceFlag[4]{};const auto provenanceLength=GetEnvironmentVariableW(L"FVR_BC2_GRAPHICS_PROVENANCE",provenanceFlag,4);
        Require(!provenanceLength||(provenanceLength==1&&provenanceFlag[0]==L'1'),"Graphics provenance must be 1 or absent");
        graphicsProvenanceEnabled=provenanceLength==1;
        Require(!graphicsProvenanceEnabled||(streamMode&&!std::filesystem::exists(output.parent_path()/L"graphics-call-provenance.jsonl")),"Graphics provenance requires a fresh tracked session");
        wchar_t destructionFlag[4]{};const auto destructionLength=GetEnvironmentVariableW(L"FVR_BC2_DESTRUCTION_WITNESS",destructionFlag,4);
        Require(!destructionLength||(destructionLength==1&&destructionFlag[0]==L'1'),"Destruction witness must be 1 or absent");
        destructionWitnessEnabled=destructionLength==1;
        Require(!destructionWitnessEnabled||(streamMode&&destructionWitness.Enable(true)),"Destruction witness requires tracked stereo");
        Require(!destructionWitnessEnabled||(!std::filesystem::exists(output.parent_path()/L"destruction-witness")&&
            !std::filesystem::exists(output.parent_path()/L"destruction-request.txt")),"Destruction witness requires a fresh output directory");
        wchar_t weatherFlag[4]{},weatherDelay[16]{},weatherExperiment[4]{},weatherRuntime[4]{};
        const auto weatherFlagLength=GetEnvironmentVariableW(L"BC2_WEATHER_OBSERVER",weatherFlag,4);
        Require(!weatherFlagLength||(weatherFlagLength==1&&weatherFlag[0]==L'1'),"Weather observer flag must be1 or absent");
        weatherObservationEnabled=weatherFlagLength==1;
        const auto weatherExperimentLength=GetEnvironmentVariableW(L"BC2_WEATHER_TRANSLATION_EXPERIMENT",weatherExperiment,4);
        Require(!weatherExperimentLength||(weatherExperimentLength==1&&weatherExperiment[0]==L'1'),"Weather translation flag must be1 or absent");
        weatherTranslationEnabled=weatherExperimentLength==1;
        Require(!weatherTranslationEnabled||weatherObservationEnabled,"Weather translation requires bounded observer");
        const auto weatherRuntimeLength=GetEnvironmentVariableW(L"BC2_WEATHER_TRANSLATION",weatherRuntime,4);
        Require(!weatherRuntimeLength||(weatherRuntimeLength==1&&weatherRuntime[0]==L'1'),"Weather translation runtime flag must be1 or absent");
        weatherTranslationRuntime=weatherRuntimeLength==1;
        Require(!weatherTranslationRuntime||(streamMode&&!weatherTranslationEnabled),"Weather runtime requires tracked stereo and excludes one-frame experiment");
        if(weatherObservationEnabled){
            Require(streamMode,"Weather observer requires tracked stereo");
            const auto length=GetEnvironmentVariableW(L"BC2_WEATHER_OBSERVER_DELAY_MS",weatherDelay,16);
            Require(length<16,"Weather observer delay too long");
            if(length){weatherObservationDelayMs=0;for(unsigned i=0;i<length;++i){Require(weatherDelay[i]>=L'0'&&weatherDelay[i]<=L'9'&&weatherObservationDelayMs<=90000,"Invalid weather observer delay");weatherObservationDelayMs=weatherObservationDelayMs*10+unsigned(weatherDelay[i]-L'0');}}
            Require(weatherGate.Arm(GetTickCount64(),weatherObservationDelayMs),"Weather observer delay must be1000..90000ms");
        }
        Require(!continuous||hostLifetime.Open(config.hostPid,L"BC2XrHost.exe"),"Continuous stream host is not alive or does not match");
        Require(mode!=3,"Unsafe old lifecycle mode is permanently disabled; use ownership-v2");Require(!streamMode||!streamToken.empty(),"Native stream requires frame channel");
        wchar_t name[32768]{};Require(GetModuleFileNameW(nullptr,name,32768)>0,"No game image");const auto path=std::filesystem::path(name);Require(!_wcsicmp(path.filename().c_str(),L"BFBC2Game.exe"),"Unexpected process image");
        const auto size=std::filesystem::file_size(path);Require(size>64&&size<512ull*1024*1024,"Invalid game image size");std::vector<std::byte> bytes(static_cast<std::size_t>(size));std::ifstream input(path,std::ios::binary);Require(bool(input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size))),"Read game executable");
        const auto pe=fvr::engine::InspectPe(bytes);Require(pe.valid,"Invalid PE");const auto discovered=fvr::bc2::DiscoverProfile(bytes,pe.image);const auto render=fvr::bc2::DiscoverRenderPath(bytes,pe.image);const auto view=fvr::bc2::DiscoverViewLayout(bytes,pe.image);const auto caches=fvr::bc2::DiscoverCameraCaches(bytes,pe.image);const auto visibility=fvr::bc2::DiscoverVisibilityPath(bytes,pe.image);Require(discovered&&render&&view&&caches&&visibility,"Native profile not verified");
        const auto life=fvr::bc2::DiscoverViewLifecycle(bytes,pe.image);Require(!(lifecycleMode||stereoMode)||life.has_value(),"View lifecycle profile not verified");if(life)lifecycle=*life;
        const auto callbacks=fvr::bc2::DiscoverViewCallbacks(bytes,pe.image);Require(!(lifecycleMode||stereoMode)||callbacks.has_value(),"Native view callbacks not verified");if(callbacks)viewCallbacks=*callbacks;
        const auto init=fvr::bc2::DiscoverViewInitialization(bytes,pe.image);Require(!stereoMode||init.has_value(),"View initialization not verified");if(init)initialization=*init;
        profile=*discovered;layout=*view;imageBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto same=[&](unsigned rva,std::size_t n){std::vector<std::byte> live(n);return Read(imageBase+rva,live.data(),n)&&std::memcmp(live.data(),bytes.data()+Offset(pe.image,rva,n),n)==0;};
        if(weatherObservationEnabled||weatherTranslationRuntime){
            Require(imageBase==0x400000&&profile.preferredBase==0x400000,"Weather observer image base changed");
            for(const auto& pin:weatherObservation::CodePins){const auto at=Offset(pe.image,pin.rva,pin.bytes);std::uint64_t hash=14695981039346656037ULL;
                for(unsigned i=0;i<pin.bytes;++i){hash^=std::to_integer<unsigned char>(bytes[at+i]);hash*=1099511628211ULL;}
                Require(hash==pin.hash&&same(pin.rva,pin.bytes),"Weather observer native signature changed");
            }
        }
        if(weatherTranslationEnabled||weatherTranslationRuntime)for(const auto& pin:weatherTranslation::CodePins){
            const auto at=Offset(pe.image,pin.rva,pin.bytes);std::uint64_t hash=14695981039346656037ULL;
            for(unsigned i=0;i<pin.bytes;++i){hash^=std::to_integer<unsigned char>(bytes[at+i]);hash*=1099511628211ULL;}
            Require(hash==pin.hash&&same(pin.rva,pin.bytes),"Weather translation ownership signature changed");
        }
        Require(same(render->worldRender,streamMode?0x199:40)&&same(render->prepareView,41)&&same(render->drawView,16),"Native code already modified");
        if(streamMode){
            const fvr::bc2::stereoColorScale::Memory colorMemory{nullptr,ReadColorScale,ReplaceColorScale<3>};
            const fvr::bc2::stereoColorScale::EffectMemory effectMemory{nullptr,ReadColorScale,ReplaceColorScale<4>};
            Require(fvr::bc2::stereoColorScale::VerifyCode(colorMemory,unsigned(imageBase+layout.vtable),unsigned(imageBase+render->drawView))&&
                same(render->drawView+0x1820,646),"Native view color modulation consumer changed");
            Require(fvr::bc2::stereoColorScale::VerifyEffectCode(effectMemory,unsigned(imageBase+layout.vtable),unsigned(imageBase+render->drawView))&&
                same(render->drawView+0x1b3f,161),"Native view four-float effect consumer changed");
            Require(fvr::bc2::stereoColorScale::VerifyPostProcessCode(colorMemory,unsigned(imageBase+layout.vtable),unsigned(imageBase+render->drawView),unsigned(imageBase))&&
                same(render->drawView+0x12ac,224)&&same(render->drawView+0x1a94,18),"Native view numeric postprocess consumer changed");
            stereoColorScalePair.Configure(colorMemory,{unsigned(imageBase+layout.vtable),
                unsigned(imageBase+lifecycle.requestVtable),unsigned(imageBase+render->worldRendererVtable)});
            stereoEffectPair.Configure(effectMemory,{unsigned(imageBase+layout.vtable),
                unsigned(imageBase+lifecycle.requestVtable),unsigned(imageBase+render->worldRendererVtable)});
            stereoPostProcessPair.Configure(colorMemory,{nullptr,ReadColorScale,ReplaceColorScale<5>},{nullptr,ReadColorScale,ReplaceColorScale<16>},
                {unsigned(imageBase+layout.vtable),unsigned(imageBase+lifecycle.requestVtable),unsigned(imageBase+render->worldRendererVtable)});
            stereoColorScaleVerified=true;
        }
        Require(same(visibility->worldUpdate,35)&&same(visibility->worldUpdate+0x383,5)&&same(visibility->worldUpdate+0x3e8,9)&&same(visibility->prepareVisibility,37)&&same(visibility->prepareVisibility+0x2e9a,9),"Visibility code changed");
        Require(same(caches->getView,21)&&same(caches->getProjection,21)&&same(caches->getFrustum,21)&&same(caches->updateView,26)&&same(caches->updateProjection,22)&&same(caches->updateFrustum,20),"Camera math code changed");
        Require(same(caches->updateView+0x1d5,10)&&same(caches->updateProjection+0x1c1,10)&&same(caches->updateFrustum+0x7c,6),"Camera math epilogues changed");
        Require(same(caches->getScale,21)&&same(caches->getScaleSquared,21)&&same(caches->updateScale,0x1ad),"Camera scalar cache code changed");
        if(lifecycleMode||stereoMode){
            for(auto span:{std::pair{lifecycle.requestVtable,20u},std::pair{layout.vtable,240u},std::pair{lifecycle.createView,43u},std::pair{lifecycle.constructor,0x111u},std::pair{lifecycle.addRef,14u},std::pair{lifecycle.release,34u},std::pair{lifecycle.setActive,13u},std::pair{lifecycle.deletingDestructor,30u},std::pair{lifecycle.destructor,0xf3u}})Require(same(span.first,span.second),"Native view lifecycle code changed");
        }
        if(lifecycleMode||stereoMode)Require(same(viewCallbacks.rememberMain,59)&&same(viewCallbacks.registerView,57)&&same(viewCallbacks.unregisterView,61),"View lifecycle callbacks modified");
        if(stereoMode)Require(same(initialization.rebuild,0xa26)&&same(initialization.parentGetter,7)&&same(initialization.refreshRegistered,0x164),"Native initialization code modified");
#if defined(FVR_BC2_NATIVE_FADE_DIAGNOSTIC)
        Require(streamMode,"Native fade observer requires the existing stream capture path");
        Require(nativeFadeObserver.ConfigureFile(output.parent_path()/L"native-fade-window.txt",imageBase+layout.vtable,Read),
            "Native fade window or verified getter bytes invalid");
        Require((config.ordinaryGrenadeInput!=2&&config.ordinaryGrenadeInput!=4)||nativeFadeObserver.Enabled(),"Combined grenade observation requires an enabled native fade window");
#endif

        if(streamMode){
            stereoRecovery.worldType=unsigned(imageBase+render->worldRendererVtable);stereoRecovery.subsystemType=unsigned(imageBase+render->subsystemVtable);
            const auto word=[&](unsigned rva){unsigned value=0;std::memcpy(&value,bytes.data()+Offset(pe.image,rva,4),4);return value;};
            const auto rel=[&](unsigned rva,unsigned char opcode){const auto at=Offset(pe.image,rva,5);Require(bytes[at]==std::byte(opcode),"Retirement native branch changed");
                std::int32_t delta=0;std::memcpy(&delta,bytes.data()+at+1,4);const auto target=std::int64_t(rva)+5+delta;
                Require(target>0&&target<profile.imageSize,"Retirement branch outside image");return unsigned(target);};
            const auto add=word(lifecycle.requestVtable)-profile.preferredBase,release=word(lifecycle.requestVtable+4)-profile.preferredBase;
            const unsigned char addCode[]={0x83,0xc1,4,0xb8,1,0,0,0,0xf0,0x0f,0xc1,1,0x40,0xc3};
            const unsigned char releaseCode[]={0x56,0x8d,0x41,4,0x83,0xce,0xff,0xf0,0x0f,0xc1,0x30,0x4e,0x75,0x0d,0x85,0xc9,0x74,9,0x8b,0x11,0x8b,0x42,0x10,0x6a,1,0xff,0xd0,0x8b,0xc6,0x5e,0xc3};
            Require(!std::memcmp(bytes.data()+Offset(pe.image,add,sizeof(addCode)),addCode,sizeof(addCode))&&
                !std::memcmp(bytes.data()+Offset(pe.image,release,sizeof(releaseCode)),releaseCode,sizeof(releaseCode)),"Request ref ABI unverified");
            const auto deleting=word(lifecycle.requestVtable+16)-profile.preferredBase,requestDestructor=rel(deleting+3,0xe8);
            const auto baseDestructor=rel(lifecycle.destructor+0xee,0xe9);
            const unsigned char deletingHead[]={0x56,0x8b,0xf1};
            const unsigned char requestHead[]={0x56,0x8b,0xf1,0x57,0x8b,0x7e,8};
            const unsigned char requestShrink[]={0x83,0x87,0xb0,0,0,0,0xfc};
            const unsigned char baseHead[]={0x56,0x8b,0xf1,0x57,0x8b,0x7e,0x70};
            Require(!std::memcmp(bytes.data()+Offset(pe.image,deleting,3),deletingHead,3)&&
                !std::memcmp(bytes.data()+Offset(pe.image,requestDestructor,7),requestHead,7)&&
                word(requestDestructor+9)==profile.preferredBase+lifecycle.requestVtable&&
                !std::memcmp(bytes.data()+Offset(pe.image,requestDestructor+0x40,7),requestShrink,7)&&
                !std::memcmp(bytes.data()+Offset(pe.image,baseDestructor,7),baseHead,7),"Retirement destructors unverified");
            stereoRecovery.requestAddRef=unsigned(imageBase+add);stereoRecovery.requestRelease=unsigned(imageBase+release);
            // Discover from the already verified refresh call; count=0 takes
            // only the native shrink/destruct path, with no scene query or grow.
            const auto cacheResize=rel(initialization.refreshRegistered+0x46,0xe8);
            const auto cacheShrink=rel(cacheResize+0x76,0xe8);
            const auto cacheMove=rel(cacheShrink+0x13,0xe8);
            const auto cacheDestroy=rel(cacheShrink+0x22,0xe8);
            const std::array<unsigned,4> cacheRvas{cacheResize,cacheShrink,cacheMove,cacheDestroy};
            constexpr std::array<unsigned,4> cacheSizes{0x84,0x4c,0x32,0x38};
            constexpr std::array<std::uint64_t,4> cacheHashes{
                0x603206a6dcefbb88ULL,0x99b6c27ab835b9e9ULL,0x430e15752aaa0e01ULL,0x1f2e159a0bfe8735ULL};
            for(unsigned i=0;i<cacheRvas.size();++i)
                Require(same(cacheRvas[i],cacheSizes[i])&&Hash(imageBase+cacheRvas[i],cacheSizes[i])==cacheHashes[i],"Retired cache native shrink ABI unverified");
            stereoRecovery.retiredCacheResize=unsigned(imageBase+cacheResize);
            const auto holderRelease=rel(lifecycle.destructor+0x1a,0xe8);
            const auto pendingRelease=rel(lifecycle.destructor+0xe4,0xe8);
            Require(rel(lifecycle.destructor+0xca,0xe8)==holderRelease&&
                same(holderRelease,0x74)&&same(pendingRelease,0x37)&&
                Hash(imageBase+holderRelease,0x74)==0x28fb8a386238ded6ULL&&
                Hash(imageBase+pendingRelease,0x37)==0xfeea1c870d1c1239ULL,
                "Owned child-holder/pending-queue destructors unverified");
            const unsigned char renderingGate[]={0x83,0xbe,0xc4,0,0,0,1};
            const unsigned char renderingDone[]={0xc7,0x86,0xc4,0,0,0,3,0,0,0};
            const unsigned char updatingDone[]={0xc7,0x83,0xc4,0,0,0,1,0,0,0};
            Require(!std::memcmp(bytes.data()+Offset(pe.image,render->worldRender+0x1e,7),renderingGate,7)&&
                !std::memcmp(bytes.data()+Offset(pe.image,render->worldRender+0x175,10),renderingDone,10)&&
                !std::memcmp(bytes.data()+Offset(pe.image,visibility->worldUpdate+0x3da,10),updatingDone,10),"Native request phase contract changed");
            const std::array<std::pair<unsigned,unsigned>,21> spans{{{add,14},{release,31},{deleting,30},{requestDestructor,0x7a},{baseDestructor,0xc3},
                {lifecycle.release,34},{lifecycle.destructor,0xf3},{lifecycle.deletingDestructor,30},{lifecycle.setActive,13},
                {viewCallbacks.unregisterView,61},{initialization.refreshRegistered,0x164},{initialization.parentGetter,7},
                {render->worldRender+0x1e,7},{render->worldRender+0x175,10},{visibility->worldUpdate+0x3da,10},
                {cacheResize,cacheSizes[0]},{cacheShrink,cacheSizes[1]},{cacheMove,cacheSizes[2]},{cacheDestroy,cacheSizes[3]},
                {holderRelease,0x74},{pendingRelease,0x37}}};
            for(unsigned i=0;i<spans.size();++i){const auto [rva,n]=spans[i];Require(same(rva,n),"Retirement code modified");
                stereoRecovery.code[i]={unsigned(imageBase+rva),n,Hash(imageBase+rva,n)};}
            // Root Draw releases its pending child queue before WorldRender
            // closes phase3. The next Update's terrain callback is a no-op,
            // followed by visibility consuming the borrowed cache. Pin this
            // exact ordering before enabling the conditional live-cache repair.
            struct CacheBoundarySpan {unsigned rva,size;std::uint64_t hash;};
            constexpr std::array<CacheBoundarySpan,6> boundaries{{
                {0x7b3b80,0x62,0x01d74859dccddd55ULL},
                {0x7b4ea0,0x5f,0x0cdae3876391e11bULL},
                {0x7b4980,0x23,0x942939b1d3847c3bULL},
                {0x7b4aa7,0x2f,0x78ce7b259c4e066dULL},
                {0x569d50,3,0x69c9e01c23e9b9f1ULL},
                {0x7b4cec,0x1c,0x818bd3a56cb39563ULL}}};
            Require(visibility->worldUpdate==boundaries[2].rva&&render->drawView+0x1dd0==boundaries[0].rva&&
                render->worldRender+0x120==boundaries[1].rva,"Live cache boundary functions changed");
            unsigned codeIndex=unsigned(spans.size());
            for(unsigned i=0;i<boundaries.size();++i){const auto& span=boundaries[i];
                Require(same(span.rva,span.size)&&Hash(imageBase+span.rva,span.size)==span.hash,
                    "Live cache native retirement/update boundary unverified");
                // The Update entry is verified here, then intentionally patched
                // by our existing detour. Retain only untouched spans for later
                // native-call admission; never compare the detour with stock.
                if(i!=2)stereoRecovery.code[codeIndex++]={unsigned(imageBase+span.rva),span.size,span.hash};
            }
        }
        if(stereoMode){const auto vt=Offset(pe.image,layout.vtable+15*4,4);unsigned setter=0;std::memcpy(&setter,bytes.data()+vt,4);Require(setter>=profile.preferredBase&&same(setter-profile.preferredBase,48),"Viewport setter changed");}
        updateView=reinterpret_cast<CacheFn>(imageBase+caches->updateView);updateProjection=reinterpret_cast<CacheFn>(imageBase+caches->updateProjection);updateFrustum=reinterpret_cast<CacheFn>(imageBase+caches->updateFrustum);updateScale=reinterpret_cast<CacheFn>(imageBase+caches->updateScale);
        // Full x86 argument cleanup conventions, independently observed in the binary.
        for(auto pair:{std::pair{render->worldRender+0x196,4u},std::pair{render->prepareView+0xf02,8u},std::pair{render->drawView+0x1e2f,12u}}){
            const auto offset=Offset(pe.image,pair.first,3);Require(bytes[offset]==std::byte{0xc2}&&bytes[offset+1]==std::byte(pair.second)&&bytes[offset+2]==std::byte{0}&&same(pair.first,3),"Native callback ABI changed");}
        const auto game=U32(imageBase+profile.gameRendererGlobal),renderer=U32(imageBase+profile.rendererGlobal);Require(U32(renderer)==imageBase+profile.rendererVtable,"Renderer ownership mismatch");
        unsigned subsystem=0;
        Require(game>=0x10000&&Read(std::uintptr_t(game)+0x108c,&subsystem,sizeof(subsystem)),"Game renderer unavailable");
        const auto world=subsystem?U32(std::uintptr_t(subsystem)+0x2c):0u;
        const auto request=subsystem?U32(std::uintptr_t(subsystem)+0x30):0u;
        if(subsystem){
            Require(U32(subsystem)==imageBase+render->subsystemVtable,"Subsystem ownership mismatch");
            Require(U32(world)==imageBase+render->worldRendererVtable&&U32(std::uintptr_t(request)+8)==world,"World request mismatch");
        }else{
            // The front end has a renderer/swapchain but no campaign world.
            // Admit only that exact native owner, never an arbitrary missing or
            // partially constructed scene. Later Draw/Update callbacks retain
            // their normal world/request/view and callback ownership checks.
            Require(streamMode,"Missing world requires a main-menu VR stream");
            const auto menuBinding=fvr::bc2::DiscoverMenuState(bytes,pe.image);
            Require(bool(menuBinding),"Main-menu startup binding unavailable");
            for(const auto span:fvr::bc2::MenuStateCodeSpans())Require(same(span.rva,span.size),"Main-menu startup code changed");
            const fvr::bc2::MenuMemory menuMemory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);}};
            const auto menu=fvr::bc2::ReadMenuState(menuMemory,*menuBinding,unsigned(imageBase));
            unsigned repeatedSubsystem=1;
            Require(menu&&menu->frontEnd&&menu->entered&&
                U32(imageBase+profile.gameRendererGlobal)==game&&
                Read(std::uintptr_t(game)+0x108c,&repeatedSubsystem,sizeof(repeatedSubsystem))&&!repeatedSubsystem,
                "Missing world is not a stable native main menu");
            startedFromMainMenu=true;
        }
        if(stereoMode){if(world)for(unsigned index=0;index<16;++index){const auto begin=U32(world+0x9c),end=U32(world+0xa0);if(begin+index*4>=end)break;const auto cb=U32(begin+index*4),vt=U32(cb);if(U32(vt+28)==imageBase+viewCallbacks.registerView)Require(U32(vt+24)==imageBase+initialization.refreshRegistered,"Registered-view refresh mismatch");}
            stereoProgress=CreateFileW((output.parent_path()/L"stereo-progress.jsonl").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Require(stereoProgress!=INVALID_HANDLE_VALUE,"Create stereo progress evidence");}
        if(stereoMode||poolOnly){
            const auto call=Offset(pe.image,render->prepareView+0xeb0,21);
            const unsigned char dispatch[]={0x8b,0x31,0x2b,0xc2,0x57,0xc1,0xf8,5,0x52,0x50,0x8b,0x46,0x44,0xff,0xd0};
            Require(bytes[call]==std::byte{0x8b}&&bytes[call+1]==std::byte{0x0d}&&std::memcmp(bytes.data()+call+6,dispatch,15)==0&&same(render->prepareView+0xeb0,21),"Render work dispatch changed");
            unsigned global=0;std::memcpy(&global,bytes.data()+call+2,4);Require(global>=profile.preferredBase&&global-profile.preferredBase<profile.imageSize-4,"Work manager global outside image");workPool.global=global-profile.preferredBase;
            const auto owner=U32(imageBase+workPool.global),vt=U32(owner);Require(vt>=imageBase&&vt<imageBase+profile.imageSize-72,"Work manager vtable outside image");workPool.vtable=vt-unsigned(imageBase);
            const auto table=Offset(pe.image,workPool.vtable,72);Require(same(workPool.vtable,72),"Work manager vtable modified");
            unsigned reset=0,submit=0;std::memcpy(&reset,bytes.data()+table+64,4);std::memcpy(&submit,bytes.data()+table+68,4);
            Require(reset>=profile.preferredBase&&submit>=profile.preferredBase,"Work manager methods outside image");workPool.reset=reset-profile.preferredBase;
            const auto resetAt=Offset(pe.image,workPool.reset,47),submitAt=Offset(pe.image,submit-profile.preferredBase,0xd9);
            const unsigned char resetHead[]={0x83,0xec,8,0x53,0x56,0x8b,0xf1,0x57,0x8d,0xbe,0x50,8,0,0,0x33,0xdb,0x88,0x9e,0x30,2,0,0,0x8b,0x47,4,0x8b,0x0f,0x50,0x51,0x8b,0xcf,0xe8};
            const unsigned char clearTail[]={0x8b,0x17,0x89,0x57,4};
            const unsigned char submitHead[]={0x51,0x55,0x56,0x8b,0xf1,0x89,0x74,0x24,8,0xe8};
            Require(std::memcmp(bytes.data()+resetAt,resetHead,sizeof(resetHead))==0&&std::memcmp(bytes.data()+resetAt+42,clearTail,5)==0&&std::memcmp(bytes.data()+submitAt,submitHead,sizeof(submitHead))==0&&same(workPool.reset,0xf3)&&same(submit-profile.preferredBase,0xd9),"Work pool reset/submit not verified");
        }
        Require(MH_Initialize()==MH_OK,"Initialize private hook library");
        viewAnchorMode=bool(config.flags&0x1000u);
        if(viewAnchorMode){const auto candidate=fvr::bc2::DiscoverViewAnchor(bytes,pe.image);Require(bool(candidate),"Native view anchor unverified");viewAnchor=*candidate;
            Require(same(viewAnchor.getter,4)&&same(viewAnchor.setter,0x56)&&same(viewAnchor.objectPrepare,0x2a4),"Native view anchor modified");}
        if(config.flags&0x800u)Require(fvr::bc2::gameplay::Install(bytes,pe.image,imageBase,[](fvr::interaction::InputFrame& out,std::int64_t& deadline)noexcept{return frameBridge.ReadInput(out,&deadline);},bool(config.flags&0x1000u),bool(config.flags&0x2000u),bool(config.flags&0x4000u),bool(config.flags&0x8000u),bool(config.flags&0x10000u),bool(config.flags&0x20000u),bool(config.flags&0x40000u),bool(config.flags&0x80000u),bool(config.flags&0x100000u),bool(config.flags&0x200000u),bool(config.flags&0x400000u),bool(config.flags&0x800000u),bool(config.flags&0x1000000u),bool(config.flags&0x2000000u),bool(config.flags&0x4000000u),config.magazineReloadSession),"Native gameplay binding unverified");
        if(config.flags&0x800u){
            fvr::bc2::mechanismAudio::Config audioConfig{};
            // Default is observation only. The private replacement opt-in may
            // be used only after the local 1P dispatch witness is reviewed.
            wchar_t replace[4]{},cache[1024]{};
            if(GetEnvironmentVariableW(L"BC2_MECHANISM_AUDIO_REPLACE_SPAS",replace,4)==1&&replace[0]==L'1'){
                const auto size=GetEnvironmentVariableW(L"BC2_MECHANISM_AUDIO_CACHE",cache,1024);
                Require(size>0&&size<1024,"Mechanism audio requires its validated local cue cache");
                audioConfig.replaceSpas=true;audioConfig.rearWave=std::filesystem::path(cache)/L"spas-rear.wav";
                audioConfig.closureWave=std::filesystem::path(cache)/L"spas-closure.wav";
            }
            const bool audioInstalled=fvr::bc2::mechanismAudio::Install(imageBase,audioConfig);
            if(audioConfig.replaceSpas)Require(audioInstalled,"Mechanism audio callback binding or private cache unverified");
            fvr::bc2::shellReloadAudio::Config shellConfig{};
            wchar_t shellReplace[4]{},shellCache[1024]{};
            if(GetEnvironmentVariableW(L"BC2_SHELL_RELOAD_AUDIO_REPLACE",shellReplace,4)==1&&shellReplace[0]==L'1'){
                const auto size=GetEnvironmentVariableW(L"BC2_SHELL_RELOAD_AUDIO_CACHE",shellCache,1024);
                Require(size>0&&size<1024&&std::filesystem::path(shellCache).is_absolute(),"Shell insertion audio requires its absolute private cue cache");
                Require(audioInstalled,"Shell insertion audio requires the verified native audio callback");
                shellConfig.replace=true;shellConfig.insertionWave=std::filesystem::path(shellCache)/L"spas-shell-insert.wav";
            }
            const bool shellAudioInstalled=fvr::bc2::shellReloadAudio::Install(shellConfig);
            if(shellConfig.replace)Require(shellAudioInstalled,"Shell insertion audio private cache unverified");
        }
        if(config.boatHeadAim!=fvr::bc2::BoatHeadAimMode::Disabled)
            Require(fvr::bc2::gameplay::EnableBoatHeadAim(config.boatHeadAim==fvr::bc2::BoatHeadAimMode::AimAndFire),
                "Boat head aim native ownership binding unverified");
        if(config.flags&0x800u)Require(fvr::bc2::gameplay::SetFeedbackWriter(
            [](const fvr::interaction::FeedbackEvent& event)noexcept{return frameBridge.PublishFeedback(event);}),
            "Gameplay feedback writer configuration rejected");
        if(config.m95PhysicalBoltCycles)Require(fvr::bc2::gameplay::EnablePhysicalBoltProbe(config.m95PhysicalBoltCycles),"Physical bolt requires its explicit isolated private build");
        if(config.m95StockShot)Require(fvr::bc2::gameplay::EnableM95StockShotProbe(config.m95StockShot),"M95 stock shot requires its explicit isolated private build");
        if(config.pumpHoldDiagnostic==fvr::bc2::PumpHoldDiagnostic::SelectedManualEmptyFire){Require(config.magazineReloadSession==3,"Empty-fire requires normal shared manual families");Require(fvr::bc2::gameplay::EnableEmptyFireProbe(),"Empty-fire diagnostic configuration rejected");}
        if(config.pumpHoldDiagnostic==fvr::bc2::PumpHoldDiagnostic::SpasOneShot)Require(fvr::bc2::reloadFlowRuntime::EnablePumpHoldDiagnostic(),"Pump hold diagnostic configuration rejected");
        if(const auto cycles=fvr::bc2::PumpPhysicalCycles(config.pumpHoldDiagnostic)){
            Require(!config.magazineReloadSession,"Physical pump trial excludes magazine/resource sessions");
            Require(fvr::bc2::gameplay::EnablePhysicalPumpProbe(cycles),"Physical pump requires the explicit private candidate build and measured binding");
        }
        if(config.flags&0x10000000u)Require(fvr::bc2::gameplay::EnableBodyInventory(),"Body inventory native selector/anchor binding unverified");
#ifdef FVR_BC2_AMMO_MOVE_PROBE
        // Private resource diagnostics need the same native lifetime domain as
        // player holsters. No synthetic generation or second inventory tracker.
        else Require(fvr::bc2::gameplay::EnableBodyInventory(),"Private ammunition inventory binding unverified");
#endif
        bodyAmmoObservationEnabled=bool(config.flags&0x10000000u);bodyAmmoRenderer.EnableObservation(bodyAmmoObservationEnabled);
        if(bodyAmmoObservationEnabled){
            // Owned CPU geometry is loaded before native callbacks. The report
            // folder receives this PRIVATE installed-asset cache from the launcher.
            bodyAmmoCache=fvr::bc2::LoadBodyAmmoGeometryCache(output.parent_path()/L"body-ammo-assets.fvrprop");
            if(bodyAmmoCache.catalog&&!bodyAmmoRenderer.QueueGeometryCatalog(bodyAmmoCache.catalog))
                bodyAmmoCache.status=fvr::bc2::BodyAmmoCacheStatus::Catalog;
        }
        if(config.flags&0x20000000u)Require(fvr::bc2::gameplay::EnablePhysicalReloadProbeRepeat(),"Two-shell physical fixture configuration rejected");
        weaponVisibilityProbeMode=bool(config.flags&fvr::bc2::WeaponVisibilityProbeFlag);
        bodyHolsterProbeMode=bool(config.flags&fvr::bc2::BodyHolsterProbeFlag);
        if(weaponVisibilityProbeMode||bodyHolsterProbeMode||bodyAmmoObservationEnabled){
            // Scalar phase/eye correlation needs a clock even though visibility
            // intentionally excludes the GPU draw/optic evidence collectors.
            LARGE_INTEGER frequency{};Require(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0,"Read visibility evidence clock");
            reloadDrawFrequency=frequency.QuadPart;Require(ReloadDrawNowNs()>0,"Visibility evidence clock unavailable");
        }
        if(weaponVisibilityProbeMode)Require(fvr::bc2::gameplay::EnableWeaponVisibilityProbe(),"Weapon visibility diagnostic binding unverified");
        if(bodyHolsterProbeMode)Require(fvr::bc2::gameplay::EnableBodyHolsterFixture(config.bodyHolsterDiagnostic),"Body holster diagnostic binding unverified");
        else if(config.flags&0x10000000u){
            // Explicit BodyInventory: SPAS and exact scoped XM8 reuse one
            // holster policy. Native hide/show and post-draw fire are observed;
            // combined headset comfort and complete shot-effect pairing remain separate.
            Require(fvr::bc2::gameplay::EnableBodyHolsters(fvr::bc2::BodyInventoryHolsterAcceptance),"Body holster binding unverified");
        }
#ifdef FVR_BC2_ORDINARY_MANUAL_CYCLES
        // Persistent player input selects a measured native mechanism through
        // the shared dispatcher. No finite controller driver is installed.
        Require(fvr::bc2::ValidResourceMagazineSession(config.magazineReloadSession,config.flags,config.durationMs)&&
            fvr::bc2::gameplay::EnableOrdinaryManualCycles(),
            "Ordinary manual cycles require tracked body, shell and resource controls with measured native bindings");
#endif
#ifdef FVR_BC2_ORDINARY_RESOURCE_INTERRUPTION_PROBE
        constexpr bool interruptOrdinaryPump=true;
#else
        constexpr bool interruptOrdinaryPump=false;
#endif
        if(config.ordinaryGrenadeInput==4)Require(fvr::bc2::gameplay::EnableTwoGrenadeDeathObservation(),"Private two-grenade death observation setup rejected");
        else if(config.ordinaryGrenadeInput==3)Require(fvr::bc2::gameplay::EnablePlayerGrenades(),"Private player grenade setup rejected");
        else if(config.ordinaryGrenadeInput)Require(fvr::bc2::gameplay::EnableOrdinaryGrenadeProbe(),"Private ordinary grenade setup rejected");
        if(config.ordinaryResourceInput)
            Require(fvr::bc2::reloadFlowRuntime::EnableOrdinaryResourceInputRecording()&&
                fvr::bc2::gameplay::EnableOrdinaryResourceInputProbe(interruptOrdinaryPump,config.ordinaryResourceInput==2),
                "Ordinary resource input requires its explicit bounded diagnostic build");
        if(config.m95OrdinaryBoltInputCycles)
            Require(fvr::bc2::reloadFlowRuntime::EnableOrdinaryBoltInputRecording(config.m95OrdinaryBoltInputCycles)&&
                fvr::bc2::gameplay::EnableOrdinaryBoltInputProbe(config.m95OrdinaryBoltInputCycles),
                "Ordinary bolt input requires its explicit bounded diagnostic build");
#ifdef FVR_BC2_MANUAL_PUMP_HANDS
        // Ordinary mode3 has real tracked input and persistent body/shell/resource
        // consumers. Exact native SPAS admission and measured contact stay in
        // the existing adapter; no finite controller driver is constructed.
        Require(fvr::bc2::ValidResourceMagazineSession(config.magazineReloadSession,config.flags,config.durationMs)&&
            fvr::bc2::gameplay::EnablePhysicalPumpCandidate(fvr::bc2::MeasuredSpasPump225()),
            "Ordinary SPAS pump requires tracked body, shell and resource controls with measured native binding");
#endif
#ifdef FVR_BC2_RESOURCE_PUMP_PROBE
        Require(config.magazineReloadSession==7&&fvr::bc2::gameplay::EnableResourcePumpCombination(),
            "Combined resource pump requires the finite persistent consumer sequence");
#endif
        // Configured-mesh reads remain a bounded PassEvidence/Hands diagnostic.
        // Continuous HMD sessions do not opt in through this path.
        if(config.flags&0x2000000u)
            Require(fvr::bc2::gameplay::EnableSelectedMeshesObservation(bytes,pe.image,imageBase,true),"Physical reload configured mesh binding unverified");
        else if(!continuous&&config.durationMs<=15000&&(config.flags&0x800u)&&(config.flags&0x14000u)&&
            (passEvidenceEnabled||(config.flags&0x10000u)))
            // Existing finite PassEvidence opt-in also records the exact authored
            // M95 zoom mesh. Hands-only and persistent consumers do no zoom reads.
            Require(fvr::bc2::gameplay::EnableSelectedMeshesObservation(bytes,pe.image,imageBase,false,
                passEvidenceEnabled?"Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95":""),"Configured mesh observation binding unverified");
        Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+render->worldRender),WorldHook,reinterpret_cast<void**>(&worldOriginal))==MH_OK,"Create world trace");
        Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+render->prepareView),PrepareHook,reinterpret_cast<void**>(&prepareOriginal))==MH_OK,"Create prepare trace");
        Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+render->drawView),DrawHook,reinterpret_cast<void**>(&drawOriginal))==MH_OK,"Create draw trace");
        Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+visibility->prepareVisibility),VisibilityHook,reinterpret_cast<void**>(&visibilityOriginal))==MH_OK,"Create visibility trace");
        if(lifecycleMode||stereoMode)Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+visibility->worldUpdate),UpdateHook,reinterpret_cast<void**>(&updateOriginal))==MH_OK,"Create pre-update lifecycle boundary");
        if(stereoMode||poolOnly)Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+workPool.reset),ResetPoolHook,reinterpret_cast<void**>(&resetPoolOriginal))==MH_OK,"Create native pool cleanup boundary");
        hookEntries={reinterpret_cast<void*>(imageBase+render->worldRender),reinterpret_cast<void*>(imageBase+render->prepareView),reinterpret_cast<void*>(imageBase+render->drawView),reinterpret_cast<void*>(imageBase+visibility->prepareVisibility),(lifecycleMode||stereoMode)?reinterpret_cast<void*>(imageBase+visibility->worldUpdate):nullptr,(stereoMode||poolOnly)?reinterpret_cast<void*>(imageBase+workPool.reset):nullptr};
        // Bootstrap through native Present before enabling the other hooks.
        // The setup worker inspects raw identity/vtable addresses only; D3D11
        // calls and their temporary reference releases stay on the render thread.
        if(streamMode){
            Require(same(profile.presentWrapper,49),"Native DXGI presentation call changed");
            observedRenderer=renderer;observedSwapChain=reinterpret_cast<IDXGISwapChain*>(U32(renderer+0x88));
            Require(observedSwapChain!=nullptr,"No desktop swap chain");
            auto* entry=reinterpret_cast<void*>(U32(U32(reinterpret_cast<unsigned>(observedSwapChain))+8*sizeof(unsigned)));HMODULE owner=nullptr;
            Require(entry&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(entry),&owner)&&owner==GetModuleHandleW(L"dxgi.dll"),"Unexpected DXGI Present owner");
            Require(MH_CreateHook(entry,DesktopPresentHook,reinterpret_cast<void**>(&desktopPresentOriginal))==MH_OK,"Create bounded desktop pacing hook");
            hookEntries[10]=entry;graphicsBootstrapWorkerThread=GetCurrentThreadId();hooksEnabled=true;
            Require(MH_EnableHook(entry)==MH_OK,"Enable native graphics bootstrap");
            const auto deadline=GetTickCount64()+5000;
            using Phase=decltype(graphicsBootstrap)::Phase;
            while((graphicsBootstrap.State()==Phase::Waiting||graphicsBootstrap.State()==Phase::Capturing)&&GetTickCount64()<deadline)Sleep(1);
            const auto captured=graphicsBootstrap.Read();
            Require(captured.has_value(),"Native Present graphics bootstrap unavailable");
            Require(captured->thread!=graphicsBootstrapWorkerThread&&captured->device==U32(renderer+0x7c)&&
                U32(imageBase+profile.rendererGlobal)==renderer&&U32(renderer)==imageBase+profile.rendererVtable&&
                U32(renderer+0x88)==reinterpret_cast<unsigned>(observedSwapChain),"Graphics bootstrap identity changed");
            graphicsBootstrapEvidence=*captured;
            observedImmediateContext=reinterpret_cast<ID3D11DeviceContext*>(captured->context);
            if(graphicsProvenanceEnabled){
                Require(fvr::graphics::callProvenance::Configure({captured->device,captured->context,
                    reinterpret_cast<std::uintptr_t>(observedSwapChain),captured->thread},
                    {captured->deviceFlags,captured->multithreadQuery,captured->multithreadKnown,captured->multithreadProtected}),"Configure bounded native device provenance");
                auto* createEntry=reinterpret_cast<void*>(captured->createTexture2D);HMODULE createOwner=nullptr;
                Require(createEntry&&GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(createEntry),&createOwner)&&createOwner==GetModuleHandleW(L"d3d11.dll"),"Unexpected D3D11 CreateTexture2D owner");
                Require(MH_CreateHook(createEntry,ObserveCreateTexture2D,reinterpret_cast<void**>(&createTexture2DOriginal))==MH_OK,"Create native device provenance hook");
                hookEntries[19]=createEntry;
            }
            fvr::diagnostics::privateCpu::recorder.ConfigureFromEnvironment(captured->thread,L"FVR_BC2_PRIVATE_CPU_ATTRIBUTION");
            privatePresentationExperiment.ConfigureFromEnvironment();
            fvr::diagnostics::privateGpu::recorder.ConfigureActivation(fvr::diagnostics::privateGpu::ActivationModeFromEnvironment(),
                fvr::diagnostics::privateCpu::recorder.Enabled()&&privatePresentationExperiment.Enabled());
            privatePresentProgress.Configure();
        }
        if(streamMode){auto* entry=reinterpret_cast<void*>(graphicsBootstrapEvidence.entries[0]);
            HMODULE owner=nullptr;Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(entry),&owner)&&owner==GetModuleHandleW(L"d3d11.dll"),"Unexpected D3D11 DrawIndexed owner");
            Require(MH_CreateHook(entry,IndexedDrawHook,reinterpret_cast<void**>(&indexedDrawOriginal))==MH_OK,"Create bounded geometry constant observer");hookEntries[6]=entry;
            bool observeNonIndexed=passEvidenceEnabled||opticEvidenceEnabled||destructionWitnessEnabled||graphicsProvenanceEnabled;
#ifdef FVR_BC2_BODY_SCENE_DEPTH
            auto* clearEntry=reinterpret_cast<void*>(graphicsBootstrapEvidence.entries[4]);HMODULE clearOwner=nullptr;
            Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(clearEntry),&clearOwner)&&clearOwner==GetModuleHandleW(L"d3d11.dll"),"Unexpected D3D11 ClearDepthStencilView owner");
            Require(MH_CreateHook(clearEntry,ClearBodySceneDepthHook,reinterpret_cast<void**>(&clearDepthOriginal))==MH_OK,"Create scoped world/first-person depth copies");hookEntries[13]=clearEntry;
#endif
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
            observeNonIndexed=true;const bool observeInstanced=true;
#else
            const bool observeInstanced=destructionWitnessEnabled;
#endif
            const auto addDrawObserver=[&](unsigned entryIndex,void* detour,void** original,unsigned record){
                auto* entry=reinterpret_cast<void*>(graphicsBootstrapEvidence.entries[entryIndex]);HMODULE owner=nullptr;
                Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(entry),&owner)&&owner==GetModuleHandleW(L"d3d11.dll"),"Unexpected instanced draw owner");
                Require(MH_CreateHook(entry,detour,original)==MH_OK,"Create diagnostic instanced draw observer");hookEntries[record]=entry;
            };
            if(observeInstanced){
                addDrawObserver(2,reinterpret_cast<void*>(IndexedInstancedDrawHook),reinterpret_cast<void**>(&indexedInstancedDrawOriginal),11);
                addDrawObserver(3,reinterpret_cast<void*>(InstancedDrawHook),reinterpret_cast<void**>(&instancedDrawOriginal),12);
            }
            if(destructionWitnessEnabled){
                addDrawObserver(5,reinterpret_cast<void*>(DrawAutoHook),reinterpret_cast<void**>(&drawAutoOriginal),16);
                addDrawObserver(6,reinterpret_cast<void*>(IndexedIndirectHook),reinterpret_cast<void**>(&indexedIndirectOriginal),17);
                addDrawObserver(7,reinterpret_cast<void*>(IndirectHook),reinterpret_cast<void**>(&indirectOriginal),18);
            }
            if(observeNonIndexed){auto* nonIndexedEntry=reinterpret_cast<void*>(graphicsBootstrapEvidence.entries[1]);HMODULE nonIndexedOwner=nullptr;
            Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(nonIndexedEntry),&nonIndexedOwner)&&nonIndexedOwner==GetModuleHandleW(L"d3d11.dll"),"Unexpected D3D11 Draw owner");
            Require(MH_CreateHook(nonIndexedEntry,NonIndexedDrawHook,reinterpret_cast<void**>(&nonIndexedDrawOriginal))==MH_OK,"Create bounded pass observer");hookEntries[8]=nonIndexedEntry;}
        }
        if(streamMode){const auto contextCamera=fvr::bc2::DiscoverContextCamera(bytes,pe.image);
            Require(contextCamera&&same(*contextCamera,0xce),"Native context camera code changed");
            auto* entry=reinterpret_cast<void*>(imageBase+*contextCamera);
            Require(MH_CreateHook(entry,ContextCameraHook,reinterpret_cast<void**>(&contextCameraOriginal))==MH_OK,"Create native camera context observer");hookEntries[7]=entry;
            const auto overrides=fvr::bc2::DiscoverProjectionOverrides(bytes,pe.image);
            Require(overrides&&same(overrides->setter,0x6d)&&same(overrides->meshCaller-0x25,41)&&same(overrides->terrainCaller-0x27,43)&&same(overrides->edgeCaller-0x21,33),"Projection override code/ABI mismatch");projectionOverrides=*overrides;
            if(overrides->skinDecalCaller&&overrides->sootCaller){
                Require(same(overrides->skinDecalCaller-0x1f,31)&&same(overrides->sootCaller-0x1f,31),"Surface projection override code/ABI mismatch");
            }else projectionOverrides.skinDecalCaller=projectionOverrides.sootCaller=0;
            auto* projectionEntry=reinterpret_cast<void*>(imageBase+overrides->setter);
            Require(MH_CreateHook(projectionEntry,ProjectionContextHook,reinterpret_cast<void**>(&projectionContextOriginal))==MH_OK,"Create projection override observer");hookEntries[9]=projectionEntry;
        }
        if(weatherObservationEnabled||weatherTranslationRuntime){
            auto* environmentEntry=reinterpret_cast<void*>(imageBase+weatherObservation::EnvironmentRva);
            auto* worldEntry=reinterpret_cast<void*>(imageBase+weatherObservation::WorldSetterRva);
            Require(MH_CreateHook(environmentEntry,EnvironmentObserveHook,reinterpret_cast<void**>(&environmentOriginal))==MH_OK,"Create weather environment observer");hookEntries[14]=environmentEntry;
            Require(MH_CreateHook(worldEntry,WeatherWorldObserveHook,reinterpret_cast<void**>(&weatherWorldOriginal))==MH_OK,"Create weather matrix observer");hookEntries[15]=worldEntry;
        }
        if(streamMode){const bool menuInstalled=fvr::bc2::menu::Install(bytes,pe.image,imageBase,streamToken);
            Require(!startedFromMainMenu||menuInstalled,"Main-menu startup requires native menu capture and input");
            Require(((config.ordinaryGrenadeInput!=3&&config.ordinaryGrenadeInput!=4)&&!config.normalPlayerGrenades&&!config.chestKnifeProbe&&!config.normalPlayerKnife&&!config.normalContextualTools)||menuInstalled,"Player grenade requires the real native menu source");
            // Ordinary setup and actual menu binding must succeed first. This
            // enables the unchanged player policy, never the finite stock probe.
            if(config.chestKnifeProbe||config.normalPlayerKnife)Require(fvr::bc2::gameplay::EnableChestKnifeDrawReturn(
                bytes,pe.image,imageBase,config.normalPlayerKnife||config.chestKnifeProbe==2),"Chest knife setup rejected");
            if(config.normalPlayerGrenades)Require(fvr::bc2::gameplay::EnablePlayerGrenades(),"Normal player grenade setup rejected");
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
            if(config.normalContextualTools||contextualFixtureCompiled){
            Require(menuInstalled,"Contextual tools require the current native menu source");
            Require(fvr::bc2::gameplay::EnableContextualTools(),"Contextual tool setup rejected");
            }
#endif
        }
        // Retain trampolines/module until process exit, including partial setup.
        // Disabling entry patches is safe even if a callback is still returning.
        if(streamMode&&!continuous&&config.durationMs<=15000&&(passEvidenceEnabled||(config.flags&0xc00000u))){
            LARGE_INTEGER frequency{};Require(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0,"Read draw diagnostic clock");
            reloadDrawFrequency=frequency.QuadPart;fvr::bc2::ReloadProducerBinding().Enable(true);
            Require(reloadDrawCapture.SetReticleResolver(ReadReticleDrawCurrent),"Set bounded reticle draw observation");
            Require(reloadDrawCapture.Enable(true,ReloadDrawNowNs()),"Enable bounded draw observation");
            if(config.flags&0x10000u){
                const fvr::bc2::RigWorkerSceneTypes scene{unsigned(imageBase+layout.vtable),unsigned(imageBase+lifecycle.requestVtable),unsigned(imageBase+render->worldRendererVtable)};
                Require(fvr::bc2::rigWorkerRuntime::Install(bytes,pe.image,unsigned(imageBase),scene,
                    fvr::bc2::rigPublication::ReadReloadProducerOwner,LookupWorkerProducerView,true),"Install verified palette worker diagnostic");
                workerObservation=true;
            }
        }
        if(opticEvidenceEnabled){
            LARGE_INTEGER frequency{};Require(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0,"Read optic diagnostic clock");
            reloadDrawFrequency=frequency.QuadPart;
            Require(fvr::bc2::opticFilterRuntime::Install(bytes,pe.image,unsigned(imageBase),ReadOpticSource,[]()noexcept{return opticDrawView;},true),"Install bounded native lens-filter observer");
            Require(fvr::bc2::opticFilterRuntime::BeginGlobalStart(),"Begin optic observer startup");
        }
        if(workerObservation)Require(fvr::bc2::rigWorkerRuntime::BeginGlobalStart(),"Begin bounded palette worker startup");
        hooksEnabled=true;
        const auto globalEnable=MH_EnableHook(MH_ALL_HOOKS);
        const bool workerStarted=!workerObservation||fvr::bc2::rigWorkerRuntime::CompleteGlobalStart(globalEnable==MH_OK);
        const bool opticStarted=!opticEvidenceEnabled||fvr::bc2::opticFilterRuntime::CompleteGlobalStart(globalEnable==MH_OK);
        Require(globalEnable==MH_OK,"Enable native trace");
        Require(opticStarted,"Enable bounded native lens-filter observer");
        Require(workerStarted,"Enable bounded palette worker observation");
        graphicsRuntimeReady.store(true,std::memory_order_release);
        sampling.store(true,std::memory_order_release);fvr::bc2::menu::Start();
        if(config.flags&0x800u){
            // Allocate once before gameplay callbacks; failure only disables
            // diagnostic recording, never input or native weapon behavior.
            (void)fvr::bc2::gameplay::EnableFireRouteTelemetry();
            fvr::bc2::gameplay::Start();
        }
        sceneTrace::Writer sceneWriter(output.parent_path()/L"scene-milestones.jsonl");
        bool cycleFailureSaved=false,originObservationSaved=false;
#ifdef FVR_BC2_PUMP_RECEIVER_GAP_PROBE
        bool receiverGapObservationSaved=false;
#endif
#ifdef FVR_BC2_SHELL_FOCUS_LOSS_PROBE
        bool shellFocusSaved=false;
#endif
        const auto persistCycleFailure=[&]{
#ifdef FVR_BC2_SHELL_FOCUS_LOSS_PROBE
            if(!shellFocusSaved){
                LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
                const auto now=(q.QuadPart/f.QuadPart)*1000000000ll+(q.QuadPart%f.QuadPart)*1000000000ll/f.QuadPart;
                if(const auto w=fvr::bc2::gameplay::ReadShellFocusReady(now)){
                    const auto pending=output.parent_path()/L"shell-focus-ready.pending";
                    std::ofstream ready(pending,std::ios::binary);
                    ready<<w->space<<' '<<w->cycle<<' '<<w->input<<' '<<w->observedNs<<' '<<w->deadlineNs<<' '
                        <<w->completedObservedNs<<' '<<w->loaded<<' '<<w->reserve<<' '<<w->capacity<<'\n';
                    ready.close();Require(bool(ready),"Save shell focus witness");
                    std::filesystem::rename(pending,output.parent_path()/L"shell-focus-ready.txt");shellFocusSaved=true;
                }
            }
#endif
#ifdef FVR_BC2_PUMP_RECEIVER_GAP_PROBE
            if(!receiverGapObservationSaved){
                LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
                const auto now=(q.QuadPart/f.QuadPart)*1000000000ll+(q.QuadPart%f.QuadPart)*1000000000ll/f.QuadPart;
                if(const auto w=fvr::bc2::gameplay::ReadPumpReceiverGapReady(now)){
                    const auto pending=output.parent_path()/L"pump-receiver-gap-ready.pending";
                    std::ofstream ready(pending,std::ios::binary);ready<<w->space<<' '<<w->cycle<<' '<<w->shot<<' '<<w->input<<' '
                        <<w->observedNs<<' '<<w->deadlineNs<<' '<<w->loaded<<' '<<w->reserve<<' '<<w->capacity<<' '<<w->travel<<'\n';
                    ready.close();Require(bool(ready),"Save receiver gap witness");
                    std::filesystem::rename(pending,output.parent_path()/L"pump-receiver-gap-ready.txt");receiverGapObservationSaved=true;
                }
            }
#endif
#if defined(FVR_BC2_ORDINARY_RESOURCE_INPUT_PROBE)
            // A diagnostic observer only: publish an existing native debt to
            // the external input receiver. It never changes game state. The
            // opt-in receiver can then change tracking/input space together.
            if(!originObservationSaved){
                LARGE_INTEGER qpc{},frequency{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&frequency);
                const auto nowNs=std::int64_t((qpc.QuadPart/frequency.QuadPart)*1000000000ll+
                    (qpc.QuadPart%frequency.QuadPart)*1000000000ll/frequency.QuadPart);
                const auto cycle=fvr::bc2::reloadFlowRuntime::ReadNativeCycleView(nowNs);
                if(cycle&&cycle->phase==fvr::bc2::Bc2NativeCyclePhase::Converging&&cycle->blocksFire&&cycle->cycle&&cycle->shot){
                    const auto pending=output.parent_path()/L"pump-origin-ready.pending";
                    std::ofstream observation(pending,std::ios::binary);
                    observation<<cycle->native.owner.space<<' '<<cycle->cycle<<' '<<cycle->shot<<'\n';
                    observation.close();Require(bool(observation),"Save pump origin observation");
                    std::filesystem::rename(pending,output.parent_path()/L"pump-origin-ready.txt");originObservationSaved=true;
                }
            }
#endif
            if(cycleFailureSaved)return;
            std::ostringstream captured;
            if(!fvr::bc2::reloadFlowRuntime::WriteNativeCycleFirstFailure(captured))return;
            std::ofstream evidence(output.parent_path()/L"native-cycle-first-failure.json",std::ios::binary);
            evidence<<captured.str();evidence.flush();cycleFailureSaved=bool(evidence);
        };
        // File IO stays on this existing bounded worker, never a native callback.
        FILETIME deathCreated{},deathExit{},deathKernel{},deathUser{};
        if(config.ordinaryGrenadeInput==4)Require(GetProcessTimes(GetCurrentProcess(),&deathCreated,&deathExit,&deathKernel,&deathUser)!=0,"Read exact death fixture process identity");
        std::ofstream deathWitnessLog;if(config.ordinaryGrenadeInput==4)deathWitnessLog.open(output.parent_path()/L"two-grenade-native.jsonl");
        unsigned deathWitnessRows=0;std::uint64_t deathWitnessLastTick=0;
        const auto persistDeathReadiness=[&](){
            if(config.ordinaryGrenadeInput!=4)return;
            const auto tick=GetTickCount64();if(deathWitnessLastTick&&tick-deathWitnessLastTick<50)return;deathWitnessLastTick=tick;
            Require(deathWitnessRows<650,"Two-grenade readiness output limit");
            LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
            const auto now=(q.QuadPart/f.QuadPart)*1000000000ll+(q.QuadPart%f.QuadPart)*1000000000ll/f.QuadPart;
            auto w=fvr::bc2::gameplay::ReadTwoGrenadeDeathReadiness(now).value_or(fvr::bc2::TwoGrenadeNativeWitness{});
            w.pid=GetCurrentProcessId();w.creation=(std::uint64_t(deathCreated.dwHighDateTime)<<32)|deathCreated.dwLowDateTime;w.tickMs=tick;
            const auto temporary=output.parent_path()/L"two-grenade-native.pending",final=output.parent_path()/L"two-grenade-native.bin";
            std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(&w),sizeof(w));file.close();Require(bool(file),"Write two-grenade readiness");
            // Existing receiver handles share DELETE but MoveFileEx replacement
            // still fails while they are open. ReplaceFile preserves their old
            // complete snapshot and publishes these unchanged witness bytes.
            const bool published=GetFileAttributesW(final.c_str())!=INVALID_FILE_ATTRIBUTES?
                ReplaceFileW(final.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr)!=0:
                MoveFileExW(temporary.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
            Require(published,"Publish two-grenade readiness");
            deathWitnessLog<<"{\"index\":"<<deathWitnessRows++<<",\"tick_ms\":"<<tick<<",\"witness\":[";
            std::array<std::uint64_t,sizeof(w)/sizeof(std::uint64_t)> words{};std::memcpy(words.data(),&w,sizeof(w));
            for(unsigned i=0;i<words.size();++i){if(i)deathWitnessLog<<',';deathWitnessLog<<words[i];}deathWitnessLog<<"]}\n";
            deathWitnessLog.flush();Require(bool(deathWitnessLog),"Save two-grenade readiness evidence");
        };
        FILETIME knifeCreated{},knifeExit{},knifeKernel{},knifeUser{};
        if(config.chestKnifeProbe)Require(GetProcessTimes(GetCurrentProcess(),&knifeCreated,&knifeExit,&knifeKernel,&knifeUser)!=0,"Read exact knife fixture identity");
        unsigned knifeWitnessRows=0,knifeWitnessFailures=0,knifeWitnessLastError=0;
        const auto persistKnifeWitness=[&](){
            if(!config.chestKnifeProbe)return;
            Require(knifeWitnessRows++<450,"Knife witness output bound");
            LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
            const auto now=(q.QuadPart/f.QuadPart)*1000000000ll+(q.QuadPart%f.QuadPart)*1000000000ll/f.QuadPart;
            auto w=fvr::bc2::gameplay::ReadChestKnifeProbeWitness(now).value_or(fvr::bc2::ChestKnifeProbeWitness{});
            w.pid=GetCurrentProcessId();w.creation=(std::uint64_t(knifeCreated.dwHighDateTime)<<32)|knifeCreated.dwLowDateTime;w.tickMs=GetTickCount64();
            const auto temporary=output.parent_path()/L"chest-knife.pending",final=output.parent_path()/L"chest-knife.bin";
            std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(&w),sizeof(w));file.close();
            // ReplaceFile permits an existing delete-shared snapshot reader.
            // A failed diagnostic publication retains its old source deadline;
            // it cannot stop an accepted operation or invent fresh authority.
            if(!file){++knifeWitnessFailures;knifeWitnessLastError=GetLastError();return;}
            const bool exists=GetFileAttributesW(final.c_str())!=INVALID_FILE_ATTRIBUTES;
            const bool published=exists?ReplaceFileW(final.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr)!=0:
                MoveFileExW(temporary.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
            if(!published){++knifeWitnessFailures;knifeWitnessLastError=GetLastError();}
        };
#ifdef FVR_BC2_CONTEXTUAL_TOOL_PROBE
        FILETIME toolCreated{},toolExit{},toolKernel{},toolUser{};
        Require(GetProcessTimes(GetCurrentProcess(),&toolCreated,&toolExit,&toolKernel,&toolUser)!=0,"Read exact tool fixture identity");
        unsigned toolWitnessRows=0,toolWitnessFailures=0,toolWitnessLastError=0;
        const auto persistToolWitness=[&](){
            Require(toolWitnessRows++<450,"Tool witness output bound");
            LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
            const auto now=(q.QuadPart/f.QuadPart)*1000000000ll+(q.QuadPart%f.QuadPart)*1000000000ll/f.QuadPart;
            auto w=fvr::bc2::gameplay::ReadContextualToolProbeWitness(now).value_or(fvr::bc2::ContextualToolProbeWitness{});
            w.pid=GetCurrentProcessId();w.creation=(std::uint64_t(toolCreated.dwHighDateTime)<<32)|toolCreated.dwLowDateTime;w.tickMs=GetTickCount64();
            const auto temporary=output.parent_path()/L"contextual-tool.pending",final=output.parent_path()/L"contextual-tool.bin";
            std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(&w),sizeof(w));file.close();
            // ReplaceFile permits an existing delete-shared snapshot reader.
            // A failed diagnostic publication retains its old source deadline;
            // it cannot stop an accepted operation or invent fresh authority.
            if(!file){++toolWitnessFailures;toolWitnessLastError=GetLastError();return;}
            const bool exists=GetFileAttributesW(final.c_str())!=INVALID_FILE_ATTRIBUTES;
            const bool published=exists?ReplaceFileW(final.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr)!=0:
                MoveFileExW(temporary.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
            if(!published){++toolWitnessFailures;toolWitnessLastError=GetLastError();}
        };
#else
        const auto persistToolWitness=[](){};
#endif
        // Finite emulated runs need the same crash-surviving evidence as live
        // sessions. Snapshot readers never wait for a gameplay transaction.
        std::ofstream telemetry(output.parent_path()/L"session-telemetry.jsonl");
        const auto sessionStarted=GetTickCount64();auto nextSample=sessionStarted;
        const auto persistSessionTelemetry=[&](bool force=false){
                const auto now=GetTickCount64();if(force||now>=nextSample){
                    FILETIME ft{};GetSystemTimeAsFileTime(&ft);const auto utc=(std::uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime;
                    const auto emptyGate=fvr::bc2::reloadFlowRuntime::ReadMagazineEmptyControlCounters();
                    // Flush presentation evidence during play: a game crash
                    // cannot run the final detailed interaction report.
                    const auto pump=fvr::bc2::rigPublication::ReadPumpPackCounters();
                    const auto magazine=fvr::bc2::rigPublication::ReadMagazinePackCounters();
                    const auto knife=fvr::bc2::selectedKnife::ReadTelemetry();
                    const auto menuConnection=fvr::bc2::menu::ReadConnectionTelemetry();
                    telemetry<<"{\"elapsed_ms\":"<<(now-sessionStarted)<<",\"utc_filetime_100ns\":"<<utc<<",\"world_calls\":"<<worldCalls.load()
                        <<",\"published\":"<<frameBridge.Published()<<",\"consumed\":"<<frameBridge.Consumed()<<",\"discarded\":"<<frameBridge.Discarded()
                        <<",\"input_samples\":"<<inputSamples.load()<<",\"tracked_input_samples\":"<<inputTrackedSamples.load()<<",\"gpu_failure_stage\":"<<frameBridge.FailureStage()<<",\"camera_restore_failures\":"<<tracked.restoreFailures.load()
                        <<",\"menu_connection\":{\"ready\":"<<(menuConnection.ready?"true":"false")
                        <<",\"input_samples\":"<<menuConnection.inputSamples<<",\"published\":"<<menuConnection.published
                        <<",\"graphics_failures\":"<<menuConnection.graphicsFailures<<'}'
                        <<",\"stereo_frame_admission\":";
                    stereoFrameAdmissionEvidence.Write(telemetry);
                    // CPU-only bounded atomic snapshot on the existing cadence;
                    // preserve ownership evidence even if no final report runs.
                    telemetry<<R"(,"desktop_graphics_ownership":)";
                    WriteDesktopGraphicsOwnership(telemetry);
                    telemetry<<",\"magazine_empty_gate\":{\"requested\":["<<emptyGate.requested[0]<<','<<emptyGate.requested[1]<<','<<emptyGate.requested[2]
                        <<"],\"applied\":["<<emptyGate.applied[0]<<','<<emptyGate.applied[1]<<','<<emptyGate.applied[2]
                        <<"],\"restored\":["<<emptyGate.restored[0]<<','<<emptyGate.restored[1]<<','<<emptyGate.restored[2]
                        <<"],\"patch_failures\":"<<emptyGate.patchFailures<<",\"restore_failures\":"<<emptyGate.restoreFailures
                        <<",\"receipt_failures\":"<<emptyGate.receiptFailures<<'}'
                        <<",\"pump_presentation\":{\"poses\":"<<pump.poses<<",\"pairs\":"<<pump.pairs<<",\"fallbacks\":"<<pump.fallbacks
                        <<",\"rest_poses\":"<<pump.restPoses<<",\"rest_pairs\":"<<pump.restPairs<<'}'
                        <<",\"magazine_presentation\":{\"pairs\":"<<magazine.pairs<<",\"fallbacks\":"<<magazine.fallbacks
                        <<",\"role_pairs\":["<<magazine.rolePairs[0]<<','<<magazine.rolePairs[1]<<','<<magazine.rolePairs[2]<<','<<magazine.rolePairs[3]<<"]},\"selected_knife\":";
                    fvr::bc2::selectedKnife::WriteTelemetry(telemetry,knife);
                    telemetry<<",\"rig_publication\":";
                    fvr::bc2::rigPublication::ReportLiveCounters(telemetry);
                    telemetry<<",\"weapon_routes\":";fvr::bc2::gameplay::ReportWeaponRoutes(telemetry);
                    telemetry<<",\"fire_routes\":";fvr::bc2::gameplay::ReportFireRouteTelemetry(telemetry);
                    telemetry<<"}\n";
                    telemetry.flush();nextSample=now+1000;
                }
        };
        unsigned destructionRequested=0,destructionCompleted=0,destructionFailed=0;
        const auto serviceDestructionRequest=[&]{
            if(!destructionWitnessEnabled)return;
            const auto requestPath=output.parent_path()/L"destruction-request.txt";
            const auto attributes=GetFileAttributesW(requestPath.c_str());
            // Controller writes "sequence firstDrawOrdinal" and atomically
            // renames it here. No graphics callback reads files or waits.
            if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){
                std::ifstream request(requestPath,std::ios::binary);char data[32]{};request.read(data,32);const auto n=request.gcount();
                if(n>0&&n<32){
                    std::istringstream command(std::string(data,std::size_t(n)));unsigned sequence=0,firstDrawOrdinal=0;char extra=0;
                    if((command>>sequence>>firstDrawOrdinal)&&!(command>>extra)&&sequence>destructionRequested&&
                        destructionWitness.Request(sequence,firstDrawOrdinal))destructionRequested=sequence;
                }
            }
            const auto completed=destructionWitness.Completed(),failed=destructionWitness.Failed();
            if(completed!=destructionCompleted||failed!=destructionFailed){
                destructionCompleted=completed;destructionFailed=failed;
                std::ofstream status(output.parent_path()/L"destruction-status.json");
                status<<"{\"schema\":\"fvr.bc2.destruction-witness-status.v1\",\"requested\":"<<destructionRequested<<",\"copied\":"<<completed<<",\"failed\":"<<failed<<"}\n";
            }
        };
        if(continuous){
            while(!hostLifetime.Ended()){
                serviceDestructionRequest();if(graphicsProvenanceEnabled)fvr::graphics::callProvenance::Flush(output.parent_path()/L"graphics-call-provenance.jsonl");
                persistCycleFailure();sceneWriter.Flush();persistSessionTelemetry();
#if defined(FVR_BC2_ORDINARY_RESOURCE_SCENE_PROBE)
                fvr::bc2::graphicsThreadObservation::Flush(output.parent_path()/L"graphics-threads.jsonl");
#endif
                Sleep(100);
            }
        }else {
            const auto until=GetTickCount64()+config.durationMs;
            for(auto now=GetTickCount64();now<until;now=GetTickCount64()){
                serviceDestructionRequest();if(graphicsProvenanceEnabled)fvr::graphics::callProvenance::Flush(output.parent_path()/L"graphics-call-provenance.jsonl");
                persistCycleFailure();persistDeathReadiness();persistKnifeWitness();persistToolWitness();sceneWriter.Flush();persistSessionTelemetry();
#if defined(FVR_BC2_ORDINARY_RESOURCE_SCENE_PROBE)
                fvr::bc2::graphicsThreadObservation::Flush(output.parent_path()/L"graphics-threads.jsonl");
#endif
#ifdef FVR_BC2_CONTEXTUAL_TOOL_PROBE
                constexpr bool toolFixture=true;
#else
                constexpr bool toolFixture=false;
#endif
Sleep(DWORD(std::min<std::uint64_t>((toolFixture||config.ordinaryGrenadeInput==4||config.chestKnifeProbe)?50:100,until-now)));
            }
        }
        persistCycleFailure();sceneWriter.Flush();
#if defined(FVR_BC2_ORDINARY_RESOURCE_SCENE_PROBE)
                fvr::bc2::graphicsThreadObservation::Flush(output.parent_path()/L"graphics-threads.jsonl");
#endif

        sampling.store(false,std::memory_order_release);
        if(stereoMode){for(unsigned attempt=0;attempt<200;++attempt){const auto state=stereoExperiment.stage.load(std::memory_order_acquire);if(state!=StereoExperiment::Armed&&state!=StereoExperiment::Retiring)break;Sleep(5);}}
        for(unsigned attempt=0;attempt<200&&workPool.stage.load(std::memory_order_acquire)==1;++attempt)Sleep(5);
        disabled=DisableProbeHooks();hooksEnabled=false;
        persistSessionTelemetry(true);
        if(graphicsProvenanceEnabled)fvr::graphics::callProvenance::Flush(output.parent_path()/L"graphics-call-provenance.jsonl");
        // Both capture-producing hook bodies must have returned. Sealing also
        // makes a delayed callback delegate directly to its native original.
        if(disabled)for(unsigned attempt=0;attempt<200;++attempt){
            if(diagnosticImageLifetime.TryQuiesce(true))break;Sleep(1);
        }
        Sleep(50);sceneWriter.Flush();unsigned complete=0;for(const auto& r:records)if(r.complete.load(std::memory_order_acquire))++complete;
        if(destructionWitnessEnabled)Require(destructionWitness.Write(output.parent_path()/L"destruction-witness"),"Write bounded destruction witness after callback drain");
#if defined(FVR_BC2_NATIVE_FADE_DIAGNOSTIC)
        if(nativeFadeObserver.Enabled()){
            std::ofstream fade(output.parent_path()/L"native-fade-observation.jsonl");
            nativeFadeObserver.Write(fade);fade.close();Require(bool(fade),"Write native fade observation");
        }
#endif
        report<<"{\"state\":\"observed\",\"pid\":"<<GetCurrentProcessId()<<",\"hooks_disabled\":"<<(disabled?"true":"false")<<",\"module_retained_until_exit\":true,\"native_render_replayed\":false,\"camera_written\":"<<(cameraPulse.complete.load(std::memory_order_acquire)&&cameraPulse.applied?"true":"false")<<",\"world_calls\":"<<worldCalls.load()<<",\"prepare_calls\":"<<prepareCalls.load()<<",\"draw_calls\":"<<drawCalls.load()<<",\"complete_records\":"<<complete<<",\"records\":[";
        bool first=true;for(const auto& r:records)if(r.complete.load(std::memory_order_acquire)){
            if(!first)report<<',';first=false;report<<"{\"thread\":"<<r.thread<<",\"frame\":"<<r.frame<<",\"world_frame\":"<<r.worldFrame<<",\"world\":"<<r.world<<",\"request\":"<<r.request<<",\"view\":"<<r.view<<",\"begin_qpc\":"<<r.beginQpc<<",\"end_qpc\":"<<r.endQpc<<",\"request_before\":"<<r.requestBefore<<",\"request_after\":"<<r.requestAfter<<",\"views\":"<<r.viewCount<<",\"prepare_calls\":"<<r.prepared<<",\"draw_calls\":"<<r.drawn<<",\"arena_before\":"<<r.arenaBefore<<",\"arena_after\":"<<r.arenaAfter<<",\"view_items_before\":"<<r.viewItemsBefore<<",\"view_items_after\":"<<r.viewItemsAfter<<",\"camera_hash_before\":"<<r.cameraBefore<<",\"camera_hash_after\":"<<r.cameraAfter<<",\"prepared_hash_before\":"<<r.dataBefore<<",\"prepared_hash_after\":"<<r.dataAfter<<",\"target_before\":";WriteTarget(report,r.targetBefore);report<<",\"target_after\":";WriteTarget(report,r.targetAfter);report<<",\"children\":[";for(unsigned i=0;i<r.childrenCount;++i){if(i)report<<',';const auto& child=r.children[i];report<<"{\"entry\":"<<child.entry<<",\"view\":"<<child.view<<",\"vtable\":"<<child.vtable<<",\"owner\":"<<child.owner<<",\"active\":"<<child.active<<",\"shadow\":"<<child.shadow<<",\"primary_hash\":"<<child.primary<<",\"secondary_hash\":"<<child.secondary<<'}';}report<<"]}";}
        const bool cameraComplete=cameraExperiment.complete.load(std::memory_order_acquire);
        report<<"],\"weapon_projection_copies\":"<<weaponProjectionCopies.load()<<",\"weapon_context_failures\":"<<weaponContextFailures.Total()<<",\"view_anchor_copies\":"<<anchorCopies.load()<<",\"camera_copy_test\":";
        if(cameraComplete){report<<"{\"complete\":true"<<",\"live_camera_unchanged\":"<<(cameraExperiment.liveUnchanged?"true":"false")<<",\"baseline_view_error\":"<<cameraExperiment.baselineViewError<<",\"baseline_projection_error\":"<<cameraExperiment.baselineProjectionError<<",\"source_position_valid\":"<<cameraExperiment.sourcePositionValid<<",\"source_position\":["<<cameraExperiment.sourcePosition[0]<<','<<cameraExperiment.sourcePosition[1]<<','<<cameraExperiment.sourcePosition[2]<<"],\"variants\":[";
        unsigned variantIndex=0;for(const auto& variant:cameraExperiment.variants){if(variantIndex++)report<<',';report<<"{\"projection\":[";bool firstElement=true;for(const auto& row:variant.projection.values)for(float value:row){if(!firstElement)report<<',';firstElement=false;if(std::isfinite(value))report<<value;else report<<"null";}report<<"],\"frustum_hash\":"<<variant.frustumHash<<'}';}
        report<<"],\"mapped_cameras\":[";unsigned mappedIndex=0;for(const auto& result:cameraExperiment.mapped){if(mappedIndex++)report<<',';report<<"{\"passed\":"<<(result.complete?"true":"false")<<",\"view_error\":"<<result.viewError<<",\"projection_error\":"<<result.projectionError<<",\"view_projection_relative_error\":"<<result.viewProjectionRelativeError<<'}';}report<<"],\"culling_cameras\":[";unsigned cullIndex=0;for(const auto& result:cameraExperiment.culling){if(cullIndex++)report<<',';report<<"{\"passed\":"<<(result.passed?"true":"false")<<",\"max_distance\":"<<result.maxDistance<<",\"max_roundoff_allowance\":"<<result.maxRoundoffAllowance<<",\"max_excess\":"<<result.maxExcess<<",\"absolute_tolerance\":0.001,\"normal_error\":"<<result.normalError<<'}';}report<<"]}";
        }else report<<"{\"complete\":false}";
        report<<",\"scene_milestones\":";sceneWriter.Status(report);
        report<<",\"visibility_calls\":"<<visibilityCalls.load()<<",\"visibility_records\":[";bool firstVisibility=true;
        for(const auto& r:visibilityRecords)if(r.complete.load(std::memory_order_acquire)){if(!firstVisibility)report<<',';firstVisibility=false;report<<"{\"thread\":"<<r.thread<<",\"frame\":"<<r.frame<<",\"world_frame\":"<<r.worldFrame<<",\"world\":"<<r.world<<",\"request\":"<<r.request<<",\"view\":"<<r.view<<",\"begin_qpc\":"<<r.beginQpc<<",\"end_qpc\":"<<r.endQpc<<",\"owner_valid\":"<<(r.ownerValid?"true":"false")<<",\"request_before\":"<<r.requestBefore<<",\"request_after\":"<<r.requestAfter<<",\"job1\":"<<r.job1<<",\"job2\":"<<r.job2<<",\"camera_before\":[";for(unsigned i=0;i<4;++i){if(i)report<<',';report<<r.cameraBefore[i];}report<<"],\"camera_after\":[";for(unsigned i=0;i<4;++i){if(i)report<<',';report<<r.cameraAfter[i];}report<<"]}";}
        report<<"],\"camera_pulse\":";
        const bool pulseComplete=cameraPulse.complete.load(std::memory_order_acquire);
        if(pulseComplete)report<<"{\"complete\":true,\"frame\":"<<cameraPulse.frame<<",\"applied\":"<<(cameraPulse.applied?"true":"false")<<",\"restored\":"<<(cameraPulse.restored?"true":"false")<<",\"owner_unchanged\":"<<(cameraPulse.ownerUnchanged?"true":"false")<<",\"draw_left_copies_unchanged\":"<<(cameraPulse.drawLeftCopiesUnchanged?"true":"false")<<'}';
        else report<<"{\"complete\":false}";
        const bool lifecycleComplete=lifecycleResult.complete.load(std::memory_order_acquire);
        report<<",\"view_lifecycle\":{\"complete\":"<<(lifecycleComplete?"true":"false");
        if(lifecycleComplete){const auto& r=lifecycleResult;report<<",\"created\":"<<r.created<<",\"registered\":"<<r.registered<<",\"inactive\":"<<r.inactive<<",\"restored\":"<<r.restored<<",\"owner_unchanged\":"<<r.ownerUnchanged<<",\"view\":"<<r.view<<",\"refs_before\":"<<r.refsBefore<<",\"refs_owned\":"<<r.refsOwned<<",\"refs_released\":"<<r.refsReleased<<",\"count_before\":"<<r.countBefore<<",\"count_during\":"<<r.countDuring<<",\"count_after\":"<<r.countAfter<<",\"thread\":"<<r.thread;}
        if(lifecycleComplete){const auto& r=lifecycleResult;report<<",\"borrowed_redirects\":"<<r.borrowedRedirects<<",\"borrowed_restores\":"<<r.borrowedRestores<<",\"all_before\":"<<r.allBefore<<",\"all_after\":"<<r.allAfter<<",\"request_refs_before\":"<<r.requestRefsBefore<<",\"request_refs_after\":"<<r.requestRefsAfter<<",\"all_restored\":"<<r.allRestored<<",\"callbacks_restored\":"<<r.callbacksRestored;}
        report<<'}';
        const auto stereoState=stereoExperiment.stage.load(std::memory_order_acquire);const auto& e=stereoExperiment;
        report<<",\"stereo_probe\":{\"stage\":"<<stereoState;
        if(stereoState==StereoExperiment::Released||stereoState==StereoExperiment::Failed)report<<",\"frame\":"<<e.frame<<",\"main_view\":"<<e.main<<",\"eye_view\":"<<e.eye<<",\"visibility_mask\":"<<e.visibilityMask.load()<<",\"draw_mask\":"<<e.drawMask.load()<<",\"lists_restored\":"<<e.listsRestored<<",\"callbacks_restored\":"<<e.callbacksRestored<<",\"refs_before\":"<<e.refsBefore<<",\"refs_after\":"<<e.refsAfter<<",\"borrowed_redirects\":"<<e.redirects<<",\"borrowed_restores\":"<<e.restores;
        if(stereoState==StereoExperiment::Released||stereoState==StereoExperiment::Failed)report<<",\"initialized\":"<<e.initialized<<",\"cameras_preserved\":"<<e.camerasPreserved<<",\"clock_preserved\":"<<e.clockPreserved<<",\"children_before\":"<<e.childrenBefore<<",\"eye_children\":"<<e.childrenAfter;
        report<<",\"frames_rendered\":"<<e.framesRendered.load();
        if(stereoState!=StereoExperiment::Idle)report<<",\"first_frame\":"<<e.firstFrame<<",\"initial_color_frame\":"<<e.initialColorFrame;
        report<<'}';
        if(stereoState==StereoExperiment::Released){
            const auto list=[&](const ListSnapshot& s){report<<'[';for(unsigned i=0;i<s.count;++i){if(i)report<<',';report<<s.values[i];}report<<']';};
            report<<",\"stereo_shutdown\":{\"captured\":"<<stereoShutdown.captured<<",\"rejected\":"<<stereoShutdown.rejected
                <<",\"owned_pending_references\":"<<stereoShutdown.pendingReferencesBefore
                <<",\"request_refs_before_release\":"<<stereoShutdown.requestReferencesBefore<<",\"after_captured\":"<<stereoShutdown.afterCaptured
                <<",\"historical_original_order\":";list(stereoShutdown.historical);
            report<<",\"before_release_order\":";list(stereoShutdown.before);report<<",\"expected_survivor_order\":";list(stereoShutdown.expected);
            report<<",\"after_release_order\":";list(stereoShutdown.after);report<<'}';
        }
        report<<",\"stereo_recovery\":{\"attempts\":"<<stereoRecovery.attempts.load()<<",\"completed\":"<<stereoRecovery.completed.load()
            <<",\"rejected\":"<<stereoRecovery.rejected.load()<<",\"failure\":"<<stereoRecovery.failure.load()<<",\"retirement_step\":"<<stereoRecovery.retirementStep.load()
            <<",\"old_world\":"<<stereoRecovery.oldWorld.load()<<",\"old_request\":"<<stereoRecovery.oldRequest.load()<<",\"old_eye\":"<<stereoRecovery.oldEye.load()
            <<",\"new_world\":"<<stereoRecovery.newWorld.load()<<",\"new_request\":"<<stereoRecovery.newRequest.load()<<",\"retired_views\":"<<stereoRecovery.retiredViews.load()
            <<",\"owned_pending_references\":"<<stereoRecovery.retiredPendingReferences.load()
            <<",\"cache_clear_calls\":"<<stereoRecovery.cacheClearCalls.load()<<",\"cache_clear_receipts\":"<<stereoRecovery.cacheClearReceipts.load()
            <<",\"completed_on_stop\":"<<(stereoRecovery.completedOnStop.load()?"true":"false")<<'}';
        report<<",\"live_view_cache\":{\"calls\":"<<liveCacheEvidence.calls.load()
            <<",\"refreshed\":"<<liveCacheEvidence.refreshed.load()<<",\"disabled\":"<<liveCacheEvidence.disabled.load()
            <<",\"rejected\":"<<liveCacheEvidence.rejected.load()<<",\"callback\":"<<liveCacheEvidence.callback.load()
            <<",\"before_count\":"<<liveCacheEvidence.beforeCount.load()<<",\"registered_count\":"<<liveCacheEvidence.registeredCount.load()
            <<",\"after_count\":"<<liveCacheEvidence.afterCount.load()<<'}';
        report<<",\"stereo_progress\":";
        fvr::bc2::StereoProgressEvidence::WriteJson(report,stereoProgressEvidence.Read());
        report<<",\"stereo_frame_admission\":";stereoFrameAdmissionEvidence.Write(report);
        const auto poolStage=workPool.stage.load(std::memory_order_acquire);
        // A front-end-only session never owns a world, expanded pool or stereo
        // views. Admit its own completion only after the menu and graphics
        // callbacks have drained; any world observation keeps the strict world
        // retirement and stereo checks below.
        const bool mainMenuOnly=startedFromMainMenu&&streamMode&&!worldCalls.load()&&!prepareCalls.load()&&!drawCalls.load()&&
            !complete&&stereoState==StereoExperiment::Idle&&poolStage==0&&!frameBridge.Connected()&&!frameBridge.Published();
        const bool mainMenuPassed=mainMenuOnly&&disabled&&diagnosticImageLifetime.Quiescent()&&fvr::bc2::menu::CompletedMenuPresentation();
        report<<",\"work_pool\":{\"stage\":"<<poolStage;
        if(poolStage==2)report<<",\"frame\":"<<workPool.frame<<",\"peak_entries\":"<<workPool.peak<<",\"native_capacity\":32,\"expanded_capacity\":128";
        report<<'}';
        if(tracked.restoreFailures.load(std::memory_order_acquire))for(unsigned i=0;i<2;++i){
            for(auto item:{std::pair{"saved",&tracked.saved[i]},std::pair{"written",&tracked.written[i]},std::pair{"observed",&tracked.observed[i]}}){std::ofstream bytesOut(output.parent_path()/("tracked-"+std::string(item.first)+"-"+std::to_string(i)+".bin"),std::ios::binary);bytesOut.write(reinterpret_cast<const char*>(item.second->bytes.data()),0x460);}
        }
        report<<",\"desktop_pacing\":{\"requested\":"<<(uncapMirror?"true":"false")<<",\"calls\":"<<desktopPresentCalls.load()<<",\"native_sync_one\":"<<desktopSyncOne.load()<<",\"bypassed_waits\":"<<desktopSyncBypassed.load()<<",\"serviced_calls\":"<<desktopFrameServiceCalls.load()<<",\"serviced_after_calls\":"<<desktopFrameServiceAfterCalls.load()<<"}";
        report<<R"(,"desktop_graphics_ownership":)";
        WriteDesktopGraphicsOwnership(report);
        report<<",\"desktop_mirror\":{\"completed_left_commits\":"<<frameBridge.DesktopMirror().Committed()
            <<",\"copies\":"<<frameBridge.DesktopMirror().Copied()<<",\"neutral\":"<<frameBridge.DesktopMirror().Neutral()
            <<",\"failures\":"<<frameBridge.DesktopMirror().Failed()<<"}";
        report<<",\"private_render_queue\":";
        fvr::graphics::WriteRenderQueueSnapshot(report,frameBridge.RenderQueueSnapshotValue());
        report<<",\"private_presentation_experiment\":";privatePresentationExperiment.Write(report,disabled&&diagnosticImageLifetime.Quiescent());
        report<<",\"private_present_progress\":";privatePresentProgress.Write(report,disabled&&diagnosticImageLifetime.Quiescent());
        report<<",\"private_early_frame_admission\":{\"enabled\":"<<frameBridge.EarlyFrameAdmissionEnabled()
              <<",\"acknowledged\":"<<frameBridge.EarlyAcknowledgements()<<",\"graphics_deferrals\":"<<frameBridge.EarlyGraphicsDeferrals()
              <<",\"prepared_publications\":"<<frameBridge.PreparedPublications()
              <<",\"prepared_graphics_reuses\":"<<frameBridge.PreparedGraphicsReuses()<<'}';
        report<<",\"private_presentation_cameras\":";privatePresentationCameras.Write(report,disabled&&diagnosticImageLifetime.Quiescent(),privatePresentationExperiment.Enabled());
        report<<",\"desktop_presentation\":{\"diagnostic_only\":true,\"successful\":"<<desktopPresentOkay.load()
            <<",\"occluded\":"<<desktopPresentOccluded.load()<<",\"other_status\":"<<desktopPresentStatus.load()
            <<",\"failed\":"<<desktopPresentFailed.load()<<",\"test_calls\":"<<desktopPresentTests.load()
            <<",\"first_failure\":"<<desktopPresentFirstFailure.load()<<",\"last_hresult\":"<<desktopPresentLastResult.load()<<",\"captures\":[";
        const char* desktopPhases[]={"first_present","first_active_pairs","active_2_seconds","active_8_seconds","retired"};
        for(unsigned i=0;i<desktopCaptures.size();++i){
            if(i)report<<',';auto& d=desktopCaptures[i];auto& c=d.color;
            const bool complete=c.complete.load(std::memory_order_acquire),captured=complete&&c.step==ColorCapture::Step::Finished;
            report<<"{\"phase\":\""<<desktopPhases[i]<<"\",\"scheduled\":"<<d.scheduled<<",\"captured\":"<<captured
                <<",\"pending\":"<<(d.scheduled&&!complete)<<",\"tick_ms\":"<<d.tickMs<<",\"qpc\":"<<d.qpc
                <<",\"thread\":"<<d.thread<<",\"stage\":"<<d.stage<<",\"published\":"<<d.published
                <<",\"frame\":"<<c.frame<<",\"readback_hresult\":"<<c.error<<",\"map_busy_count\":"<<c.attempts
                <<",\"first_present_hresult\":"<<d.firstPresentResult<<",\"target\":";WriteTarget(report,c.target);
            if(d.descriptionValid){const auto& desc=d.description;
                report<<",\"swapchain\":{\"width\":"<<desc.BufferDesc.Width<<",\"height\":"<<desc.BufferDesc.Height
                    <<",\"format\":"<<unsigned(desc.BufferDesc.Format)<<",\"samples\":"<<desc.SampleDesc.Count
                    <<",\"buffer_count\":"<<desc.BufferCount<<",\"usage\":"<<desc.BufferUsage
                    <<",\"effect\":"<<unsigned(desc.SwapEffect)<<",\"flags\":"<<desc.Flags
                    <<",\"windowed\":"<<desc.Windowed<<",\"hwnd\":"<<reinterpret_cast<std::uintptr_t>(desc.OutputWindow)<<'}';
                report<<",\"window\":{\"visible\":"<<d.visible<<",\"minimized\":"<<d.minimized<<",\"foreground\":"<<d.foreground
                    <<",\"client_valid\":"<<d.clientValid<<",\"rect_valid\":"<<d.windowValid<<",\"client\":["<<d.client.left<<','<<d.client.top<<','<<d.client.right<<','<<d.client.bottom
                    <<"],\"rect\":["<<d.window.left<<','<<d.window.top<<','<<d.window.right<<','<<d.window.bottom
                    <<"],\"thread\":"<<d.windowThread<<",\"process\":"<<d.windowProcess<<",\"hung\":"<<d.hung
                    <<",\"ghost_lookup_available\":"<<d.ghostLookupAvailable<<",\"ghost_hwnd\":"<<reinterpret_cast<std::uintptr_t>(d.ghostWindow)<<",\"class_ascii\":[";
                for(unsigned k=0;k<sizeof(d.windowClass)&&d.windowClass[k];++k){if(k)report<<',';report<<unsigned(static_cast<unsigned char>(d.windowClass[k]));}report<<"]}";
            }
            if(captured){
                const auto summary=fvr::graphics::InspectDesktopRgba(c.rgba,c.target.width,c.target.height,std::size_t(c.target.width)*4);
                if(summary){report<<",\"pixels\":{\"count\":"<<summary->pixels<<",\"near_white\":"<<summary->nearWhite
                    <<",\"near_black\":"<<summary->nearBlack<<",\"opaque\":"<<summary->opaque<<",\"rgba_hash\":"<<summary->rgbaHash<<",\"rgb_sum\":[";
                    for(unsigned k=0;k<3;++k){if(k)report<<',';report<<summary->channelSum[k];}report<<"],\"rgb_min\":[";
                    for(unsigned k=0;k<3;++k){if(k)report<<',';report<<unsigned(summary->minimum[k]);}report<<"],\"rgb_max\":[";
                    for(unsigned k=0;k<3;++k){if(k)report<<',';report<<unsigned(summary->maximum[k]);}report<<"]}";
                }
                std::ofstream pixels(output.parent_path()/("desktop-color-"+std::to_string(i)+".rgba"),std::ios::binary);
                pixels.write(reinterpret_cast<const char*>(c.rgba.data()),std::streamsize(c.rgba.size()));pixels.close();
                Require(bool(pixels),"Write desktop evidence");c.serialized=true;
            }
            report<<'}';
        }
        report<<"]}";
        const auto& windowMode=desktopWindowMode.Report();
        report<<",\"desktop_window_mode\":{\"phase\":"<<unsigned(windowMode.phase)<<",\"reason\":"<<unsigned(windowMode.reason)
            <<",\"attempts\":"<<windowMode.attempts<<",\"observations\":"<<windowMode.observations<<",\"last_hresult\":"<<windowMode.lastSetResult
            <<",\"confirmed\":"<<windowMode.confirmed<<",\"initially_windowed\":"<<windowMode.initiallyWindowed
            <<",\"foreground_changed\":"<<windowMode.foregroundChanged<<",\"foreground_became_game\":"<<windowMode.foregroundBecameGame
            <<",\"before_windowed\":"<<windowMode.before.windowed<<",\"before_fullscreen\":"<<windowMode.before.fullscreen
            <<",\"before_visible\":"<<windowMode.before.visible<<",\"after_visible\":"<<windowMode.after.visible<<"}";
        report<<",\"menu\":";fvr::bc2::menu::Report(report);
        report<<",\"chest_knife_probe\":"<<config.chestKnifeProbe<<",\"knife_witness_publications\":"<<(knifeWitnessRows-knifeWitnessFailures)<<",\"knife_witness_failures\":"<<knifeWitnessFailures<<",\"knife_witness_last_error\":"<<knifeWitnessLastError;
#ifdef FVR_BC2_CONTEXTUAL_TOOL_PROBE
        report<<",\"contextual_tool_fixture\":true,\"tool_witness_publications\":"<<(toolWitnessRows-toolWitnessFailures)
            <<",\"tool_witness_failures\":"<<toolWitnessFailures<<",\"tool_witness_last_error\":"<<toolWitnessLastError;
#endif
        report<<",\"normal_player_grenades\":"<<(config.normalPlayerGrenades?"true":"false")
            <<",\"normal_player_knife\":"<<(config.normalPlayerKnife?"true":"false")
            <<",\"normal_contextual_tools\":"<<(config.normalContextualTools?"true":"false");
        report<<",\"private_cpu_attribution\":";fvr::diagnostics::privateCpu::recorder.Report(report,disabled&&diagnosticImageLifetime.Quiescent());
        report<<",\"private_gpu_attribution\":";fvr::diagnostics::privateGpu::recorder.WriteJson(report,disabled&&diagnosticImageLifetime.Quiescent());
        report<<",\"gameplay\":";fvr::bc2::gameplay::Report(report);
        report<<",\"mechanism_audio\":";fvr::bc2::mechanismAudio::Report(report);
        report<<",\"shell_reload_audio\":";fvr::bc2::shellReloadAudio::Report(report);
        report<<",\"weapon_visibility_clock_frequency\":"<<(weaponVisibilityProbeMode?reloadDrawFrequency:0);
        report<<",\"weapon_visibility_eyes\":[";
        for(unsigned n=0;n<weaponVisibilityEyeCount;++n){if(n)report<<',';const auto& r=weaponVisibilityEyes[n];
            report<<"{\"native_frame\":"<<r.frame<<",\"eye\":"<<r.eye<<",\"phase\":"<<r.phase<<",\"request\":"<<r.request
                <<",\"input\":"<<r.input<<",\"now_ns\":"<<r.nowNs<<",\"hidden_intent\":"<<(r.hidden?"true":"false")
                <<",\"paired_pack_receipt\":"<<(r.paired?"true":"false")<<",\"draw_serial\":"<<r.draw<<",\"receipt_input\":"<<r.receiptInput
                <<",\"receipt_observed_ns\":"<<r.receiptObservedNs<<",\"receipt_deadline_ns\":"<<r.receiptDeadlineNs<<'}';
        }report<<']';
        report<<",\"body_holster_eyes\":[";
        for(unsigned n=0;n<bodyHolsterEyeCount;++n){if(n)report<<',';const auto& r=bodyHolsterEyes[n];
            report<<"{\"native_frame\":"<<r.frame<<",\"eye\":"<<r.eye<<",\"phase\":"<<r.phase<<",\"body_phase\":"<<r.bodyPhase
                <<",\"request\":"<<r.request<<",\"input\":"<<r.input<<",\"native_tick\":"<<r.tick<<",\"now_ns\":"<<r.nowNs
                <<",\"right_claim\":"<<r.right<<",\"left_claim\":"<<r.left<<",\"free_right\":"<<(r.free?"true":"false")
                <<",\"suppressed\":"<<(r.suppressed?"true":"false")<<",\"hidden_receipt\":"<<(r.hidden?"true":"false")
                <<",\"paired_pack_receipt\":"<<(r.paired?"true":"false")<<",\"draw_serial\":"<<r.draw
                <<",\"receipt_deadline_ns\":"<<r.receiptDeadlineNs<<",\"paired_free_copies\":"<<r.pairedFree<<'}';}
        report<<']';
        report<<",\"body_ammo_cache\":{\"status\":\""<<fvr::bc2::BodyAmmoCacheStatusName(bodyAmmoCache.status)
            <<"\",\"bytes\":"<<bodyAmmoCache.bytes<<",\"parts\":"<<(bodyAmmoCache.catalog?bodyAmmoCache.catalog->size():0)
            <<",\"failure_part\":"<<bodyAmmoCache.part<<",\"failure_section\":"<<bodyAmmoCache.section<<'}';
        report<<",\"body_source_reads\":{\"order\":[\"ammo\",\"selected_holster\",\"carried\",\"stowed_ammo\"],\"columns\":[\"first\",\"second\",\"third\",\"unavailable\"],\"counts\":[";
        for(unsigned kind=0;kind<bodySourceReads.size();++kind){if(kind)report<<',';report<<'[';
            for(unsigned attempt=0;attempt<4;++attempt){if(attempt)report<<',';report<<bodySourceReads[kind][attempt].load();}report<<']';}report<<"]}";
        report<<",\"body_ammo_render\":";bodyAmmoRenderer.Report(report);
#ifdef FVR_BC2_BODY_SCENE_DEPTH
        report<<",\"body_scene_depth\":{\"begun_eyes\":"<<bodyDepthBegins.load()<<",\"completed_eyes\":"<<bodyDepthEnds.load()<<",\"rejected_eyes\":"<<bodyDepthRejected.load()<<",\"extra_native_draws\":0}";
        report<<",\"first_body_scene_depth_failure\":";firstBodyDepthFailure.Write(report,
            disabled&&diagnosticImageLifetime.Quiescent()&&stereoRecovery.callbacks.load(std::memory_order_acquire)==0);
        report<<R"(,"graphics_call_provenance":)";fvr::graphics::callProvenance::Summary(report);
        report<<R"(,"graphics_bootstrap":{"worker_thread":)"<<graphicsBootstrapWorkerThread
            <<R"(,"started_from_main_menu":)"<<(startedFromMainMenu?"true":"false")
            <<R"(,"main_menu_only":)"<<(mainMenuOnly?"true":"false")
            <<R"(,"main_menu_completed":)"<<(mainMenuPassed?"true":"false")
            <<R"(,"world_stereo_tested":)"<<(mainMenuOnly?"false":"true")
            <<R"(,"present_thread":)"<<graphicsBootstrapEvidence.thread<<R"(,"device_flags":)"<<graphicsBootstrapEvidence.deviceFlags
            <<R"(,"device":)"<<graphicsBootstrapEvidence.device<<R"(,"context":)"<<graphicsBootstrapEvidence.context
            <<R"(,"worker_com_calls":0,"temporary_refs_released_before_publication":true})";

#endif
        report<<",\"reload_draw_evidence\":";reloadDrawCapture.Report(report);
        report<<",\"reload_palette_producer\":";fvr::bc2::ReloadProducerBinding().Report(report);
        report<<",\"optic_filter_observation\":";fvr::bc2::opticFilterRuntime::Report(report);
        report<<",\"reload_worker_binding\":";fvr::bc2::rigWorkerRuntime::Report(report);
        report<<",\"stereo_postprocess_scalars\":{\"verified\":"<<(stereoColorScaleVerified?"true":"false")<<",\"groups\":[";
        const auto postGroup=[&](unsigned offset,unsigned count,const fvr::bc2::stereoColorScale::Counters& value){
            report<<"{\"offset\":"<<offset<<",\"count\":"<<count<<",\"main\":"<<value.main<<",\"right\":"<<value.right
                <<",\"writes\":"<<value.writes<<",\"restores\":"<<value.restores<<",\"rejected\":"<<value.rejected<<",\"restore_failures\":"<<value.restoreFailures<<"}";
        };
        postGroup(0x1560,3,stereoPostProcessPair.triplet.counts);report<<",";
        postGroup(0x1570,5,stereoPostProcessPair.five.counts);report<<",";
        postGroup(0x1590,16,stereoPostProcessPair.matrix.counts);report<<"]}";
        const auto& effect=stereoEffectPair.counts;
        report<<",\"stereo_effect_scalars\":{\"verified\":"<<(stereoColorScaleVerified?"true":"false")
            <<",\"offset\":5712,\"count\":4,\"main\":"<<effect.main<<",\"right\":"<<effect.right
            <<",\"writes\":"<<effect.writes<<",\"restores\":"<<effect.restores<<",\"rejected\":"<<effect.rejected
            <<",\"restore_failures\":"<<effect.restoreFailures<<"}";
        const auto& color=stereoColorScalePair.counts;
        report<<",\"stereo_color_scale\":{\"verified\":"<<(stereoColorScaleVerified?"true":"false")
            <<",\"main\":"<<color.main<<",\"right\":"<<color.right<<",\"writes\":"<<color.writes
            <<",\"restores\":"<<color.restores<<",\"rejected\":"<<color.rejected
            <<",\"restore_failures\":"<<color.restoreFailures<<'}';
        report<<",\"native_stream\":{\"published\":"<<frameBridge.Published()<<",\"consumed\":"<<frameBridge.Consumed()<<",\"discarded\":"<<frameBridge.Discarded()<<",\"camera_restore_failures\":"<<tracked.restoreFailures.load()<<",\"captured_eyes\":"<<frameBridge.CapturedEyes()<<",\"sharing_mode\":"<<frameBridge.SharingMode()<<",\"gpu_failure_stage\":"<<frameBridge.FailureStage()<<",\"gpu_failure_code\":"<<frameBridge.FailureCode()<<",\"gpu_failure_operation\":"<<frameBridge.FailureOperation()<<",\"device_flags\":"<<frameBridge.DeviceFlags()<<",\"feature_level\":"<<frameBridge.FeatureLevel()<<",\"sharing_compatibility\":["<<frameBridge.Compatibility(0)<<","<<frameBridge.Compatibility(1)<<","<<frameBridge.Compatibility(2)<<","<<frameBridge.Compatibility(3)<<","<<frameBridge.Compatibility(4)<<","<<frameBridge.Compatibility(5)<<","<<frameBridge.Compatibility(6)<<","<<frameBridge.Compatibility(7)<<","<<frameBridge.Compatibility(8)<<"]"<<"}";
        auto nativeTiming=std::make_unique<fvr::graphics::FrameBridgeTimingSnapshot>();
        frameBridge.TimingSnapshot(*nativeTiming);
        report<<",\"native_frame_timing\":";fvr::graphics::WriteFrameBridgeTiming(report,*nativeTiming);
        const bool pipelineReportDrained=disabled&&diagnosticImageLifetime.Quiescent()&&
            stereoRecovery.callbacks.load(std::memory_order_acquire)==0;
        report<<",\"native_source_slot_count\":"<<frameBridge.SourceSlotCount();
        report<<",\"world_source_transition\":{\"state\":"<<unsigned(stereoRecovery.sourceTransition.Status())
              <<",\"completed\":"<<stereoRecovery.sourceTransitions.load()<<",\"waits\":"<<stereoRecovery.sourceTransitionWaits.load()
              <<",\"failures\":"<<stereoRecovery.sourceTransitionFailures.load()<<'}';
        report<<(frameBridge.SourceSlotCount()==3?",\"native_three_source_slots\":":",\"native_two_source_slots\":");
        frameBridge.PipelineSnapshot(pipelineReportDrained).Write(report);
        report<<",\"native_frame_timing_slots\":[";
        if(frameBridge.TwoSourceSlotsEnabled())for(unsigned slot=0;slot<frameBridge.SourceSlotCount();++slot){
            *nativeTiming={}; // Reuse the existing heap snapshot, not another x86 stack journal.
            if(pipelineReportDrained)frameBridge.SlotTimingSnapshot(slot,*nativeTiming);
            if(slot)report<<',';report<<"{\"slot\":"<<slot<<",\"timing\":";
            fvr::graphics::WriteFrameBridgeTiming(report,*nativeTiming);report<<'}';
        }
        report<<']';
        report<<",\"first_tracked_finish_failure\":";firstTrackedFinishFailure.Write(report,
            disabled&&diagnosticImageLifetime.Quiescent()&&stereoRecovery.callbacks.load(std::memory_order_acquire)==0);
        report<<",\"update_calls\":"<<updateCalls.load();
        report<<",\"world_color_captures\":[";
        for(unsigned captureIndex=0;captureIndex<colorCaptures.size();++captureIndex){if(captureIndex)report<<',';auto& capture=colorCaptures[captureIndex];
            if(capture.complete.load(std::memory_order_acquire)){
                const bool captured=capture.step==ColorCapture::Step::Finished;
                report<<"{\"captured\":"<<(captured?"true":"false")<<",\"frame\":"<<capture.frame<<",\"hresult\":"<<capture.error<<",\"map_busy_count\":"<<capture.attempts<<",\"target\":";WriteTarget(report,capture.target);report<<'}';
                if(captured){std::ofstream pixels(output.parent_path()/("world-color-"+std::to_string(captureIndex)+".rgba"),std::ios::binary);pixels.write(reinterpret_cast<const char*>(capture.rgba.data()),std::streamsize(capture.rgba.size()));pixels.close();Require(bool(pixels),"Write captured world color");capture.serialized=true;}
            }else report<<"{\"captured\":false,\"pending\":true}";
        }
        report<<']';
        report<<"}\n";report.flush();
        WriteWeatherObservation(output);
        WriteWeatherTranslation(output);
        WriteWeatherRuntime(output);
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
        {
            std::ofstream evidence(output.parent_path()/"scene-depth-evidence.json");
            evidence<<std::setprecision(9);
            evidence<<"{\"diagnostic_only\":true,\"draws_added\":0,\"frame\":"<<sceneDepthFrame<<",\"mask\":"<<sceneDepthMask<<",\"eyes\":[";
            for(unsigned eye=0;eye<2;++eye){
                if(eye)evidence<<',';const auto& p=sceneDepthProps[eye];
                evidence<<"{\"eye\":"<<eye<<",\"props\":"<<p.count<<",\"view\":[";
                const auto matrix=[&](const fvr::math::Matrix4& m){for(unsigned n=0;n<16;++n){if(n)evidence<<',';evidence<<m.values[n/4][n%4];}};
                matrix(p.view);evidence<<"],\"projection\":[";matrix(p.projection);evidence<<"],\"world\":[";matrix(p.instances[0].world);
                evidence<<"],\"geometry\":["<<p.instances[0].geometry.asset<<','<<p.instances[0].geometry.part<<','<<p.instances[0].geometry.revision<<"],\"captures\":[";
                for(unsigned kind=0;kind<2;++kind){if(kind)evidence<<',';auto& c=kind?sceneDepthColors[eye]:sceneDepthCaptures[eye];
                    const bool okay=c.complete.load(std::memory_order_acquire)&&c.step==ColorCapture::Step::Finished;
                    evidence<<"{\"captured\":"<<okay<<",\"frame\":"<<c.frame<<",\"error\":"<<c.error<<",\"target\":";WriteTarget(evidence,c.target);evidence<<'}';
                    if(okay){std::ofstream bytes(output.parent_path()/("scene-depth-"+std::to_string(eye)+(kind?".rgba":".d24s8")),std::ios::binary);
                        bytes.write(reinterpret_cast<const char*>(c.rgba.data()),std::streamsize(c.rgba.size()));bytes.close();Require(bool(bytes),"Write scene-depth evidence");c.serialized=true;}
                }evidence<<"],\"view_mask\":"<<sceneDepthViewMask[eye]<<",\"candidate_overflow\":"<<sceneDepthCandidateOverflow[eye];
                const auto& calls=sceneDepthCalls[eye];
                evidence<<",\"draw_observation\":{\"indexed\":"<<calls.indexed<<",\"non_indexed\":"<<calls.nonIndexed
                    <<",\"indexed_instanced\":"<<calls.indexedInstanced<<",\"instanced\":"<<calls.instanced
                    <<",\"no_depth\":"<<calls.noDepth<<",\"unsupported\":"<<calls.unsupported
                    <<",\"wrong_size\":"<<calls.wrongSize<<",\"wrong_viewport\":"<<calls.wrongViewport<<",\"formats\":[";
                bool firstFormat=true;for(unsigned f=0;f<calls.formats.size();++f)if(calls.formats[f]){
                    if(!firstFormat)evidence<<',';firstFormat=false;evidence<<"{\"format\":"<<f<<",\"draws\":"<<calls.formats[f]<<'}';}
                evidence<<"]},\"depth_candidates\":[";
                for(unsigned v=0;v<4;++v)if(sceneDepthViewMask[eye]&(1u<<v)){
                    std::ofstream bytes(output.parent_path()/("scene-view-"+std::to_string(eye)+"-"+std::to_string(v)+".bin"),std::ios::binary);
                    bytes.write(reinterpret_cast<const char*>(sceneDepthViewCopies[eye][v].data()),0x460);bytes.close();Require(bool(bytes),"Write scene-view evidence");}
                for(unsigned n=0;n<sceneDepthCandidateCounts[eye];++n){if(n)evidence<<',';auto& c=sceneDepthCandidates[eye][n];auto& cap=c.capture;
                    const bool okay=cap.complete.load(std::memory_order_acquire)&&cap.step==ColorCapture::Step::Finished;
                    evidence<<"{\"index\":"<<n<<",\"captured\":"<<okay<<",\"resource\":"<<c.resource<<",\"draws\":"<<c.draws
                        <<",\"viewport\":["<<c.viewport.TopLeftX<<','<<c.viewport.TopLeftY<<','<<c.viewport.Width<<','<<c.viewport.Height<<','<<c.viewport.MinDepth<<','<<c.viewport.MaxDepth
                        <<"],\"depth_function\":"<<c.depthFunction<<",\"depth_write\":"<<c.depthWrite<<",\"target\":";WriteTarget(evidence,cap.target);evidence<<'}';
                    if(okay){std::ofstream bytes(output.parent_path()/("scene-candidate-"+std::to_string(eye)+"-"+std::to_string(n)+".d24s8"),std::ios::binary);
                        bytes.write(reinterpret_cast<const char*>(cap.rgba.data()),std::streamsize(cap.rgba.size()));bytes.close();Require(bool(bytes),"Write depth candidate");cap.serialized=true;}
                }evidence<<"]}";
            }evidence<<"]}\n";evidence.close();Require(bool(evidence),"Write scene-depth metadata");
        }
#endif
        bool cameraPassed=cameraComplete&&cameraExperiment.liveUnchanged;
        if(cameraComplete){for(const auto& result:cameraExperiment.mapped)cameraPassed&=result.complete;for(const auto& result:cameraExperiment.culling)cameraPassed&=result.passed;}
        if(streamMode){
            const auto contextMask=contextEvidenceMask.load(std::memory_order_acquire);
            std::ofstream contexts(output.parent_path()/"camera-context-evidence.json");contexts<<"{\"patched_scopes\":"<<contextPatchedScopes.load()<<",\"patch_failures\":"<<contextPatchFailures.load()<<",\"restore_failures\":"<<contextRestoreFailures.load()<<",\"mask\":"<<contextMask<<",\"contexts\":[";bool firstContext=true;
            if(contextMask&8)for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<contextCounts[eye];++i){
                const auto& binding=contextEvidence[eye][i];if(!firstContext)contexts<<',';firstContext=false;
                contexts<<"{\"eye\":"<<eye<<",\"index\":"<<i<<",\"context\":"<<binding.context<<",\"callsite\":"<<binding.callsite<<",\"role\":"<<binding.role<<",\"primary\":"<<binding.primary<<",\"secondary\":"<<binding.secondary<<",\"snapshots\":[";
                for(unsigned step=0;step<4;++step){if(step)contexts<<',';const auto& snap=binding.snapshots[step];contexts<<"{\"valid\":"<<snap.valid<<",\"view\":"<<snap.view<<",\"projection\":"<<snap.projection<<",\"position\":"<<snap.position<<",\"values\":[";
                    for(unsigned k=0;k<36;++k){if(k)contexts<<',';contexts<<snap.values[k];}contexts<<"]}";}contexts<<"]}";
                std::ofstream source(output.parent_path()/("context-"+std::to_string(eye)+"-"+std::to_string(i)+"-source.bin"),std::ios::binary);source.write(reinterpret_cast<const char*>(binding.source.bytes.data()),binding.source.bytes.size());
            }contexts<<"]}\n";
            std::ofstream derived(output.parent_path()/"derived-projection-correction.json");derived<<"{\"writes\":"<<derivedWrites.load()<<",\"capture_failures\":"<<derivedFailures.load()<<"}";
            std::ofstream projections(output.parent_path()/"projection-bindings.json");projections<<'[';
            for(unsigned i=0;i<(std::min)(projectionCount.load(),unsigned(projectionEvidence.size()));++i){const auto& b=projectionEvidence[i];if(i)projections<<',';
                projections<<"{\"context\":"<<b.context<<",\"address\":"<<b.address<<",\"callsite\":"<<b.callsite<<",\"thread\":"<<b.thread<<",\"frame\":"<<b.frame<<",\"gather\":"<<b.gather<<",\"projection\":[";
                for(unsigned k=0;k<16;++k){if(k)projections<<',';projections<<b.values[k];}projections<<"]}";
            }projections<<']';
            std::ofstream passes(output.parent_path()/"pass-buffer-evidence.json");passes<<'[';bool firstPass=true;
            for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<passCounts[eye];++i){const auto& pass=passEvidence[eye][i];
                if(!firstPass)passes<<',';firstPass=false;
                passes<<"{\"eye\":"<<eye<<",\"pass\":"<<i<<",\"sequence\":"<<pass.sequence<<",\"kind\":"<<pass.kind<<",\"count\":"<<pass.indices<<",\"vs\":"<<pass.vs<<",\"ps\":"<<pass.ps<<",\"target\":"<<pass.target<<",\"depth\":"<<pass.depth<<",\"complete\":"<<(pass.complete?"true":"false")<<",\"overflow\":"<<passOverflow[eye]<<",\"buffers\":[";
                for(unsigned j=0;j<4;++j){if(j)passes<<',';const auto& b=pass.buffers[j];passes<<"{\"slot\":"<<j<<",\"source\":"<<b.source<<",\"bytes\":"<<b.bytes.size()<<",\"done\":"<<(b.done?"true":"false")<<'}';
                    if(b.done){std::ofstream f(output.parent_path()/("pass-"+std::to_string(eye)+"-"+std::to_string(i)+"-cb-"+std::to_string(j)+".bin"),std::ios::binary);f.write(reinterpret_cast<const char*>(b.bytes.data()),b.bytes.size());}}
                passes<<"]}";
            }passes<<']';
            std::ofstream buffers(output.parent_path()/"eye-buffer-evidence.json");buffers<<'[';bool firstBuffer=true;
            for(unsigned eye=0;eye<2;++eye)if(eyeBufferEvidence[eye].complete.load(std::memory_order_acquire))for(unsigned slot=0;slot<14;++slot){const auto& buffer=eyeBufferEvidence[eye].buffers[slot];if(!buffer.done)continue;
                if(!firstBuffer)buffers<<',';firstBuffer=false;buffers<<"{\"eye\":"<<eye<<",\"slot\":"<<slot<<",\"source\":"<<buffer.source<<",\"bytes\":"<<buffer.bytes.size()<<",\"indices\":"<<eyeBufferEvidence[eye].indices<<'}';
                std::ofstream dump(output.parent_path()/("eye-"+std::to_string(eye)+"-vs-cb-"+std::to_string(slot)+".bin"),std::ios::binary);dump.write(reinterpret_cast<const char*>(buffer.bytes.data()),buffer.bytes.size());
            }buffers<<"]\n";
            std::ofstream gpu(output.parent_path()/"eye-gpu-evidence.json");gpu<<'[';
            for(unsigned eye=0;eye<2;++eye){if(eye)gpu<<',';const auto& evidence=eyeGpuEvidence[eye];const bool gpuComplete=evidence.complete.load(std::memory_order_acquire);
                gpu<<"{\"eye\":"<<eye<<",\"complete\":"<<gpuComplete;
                if(gpuComplete)gpu<<",\"hresult\":"<<evidence.error<<",\"vertices\":"<<evidence.statistics.IAVertices<<",\"primitives\":"<<evidence.statistics.IAPrimitives<<",\"vertex_shader_invocations\":"<<evidence.statistics.VSInvocations<<",\"pixel_shader_invocations\":"<<evidence.statistics.PSInvocations;
                gpu<<'}';}gpu<<"]\n";
            const auto mask=trackedCameraEvidence.mask.load(std::memory_order_acquire);
            std::ofstream metadata(output.parent_path()/"tracked-camera-evidence.json");
            metadata<<"{\"native_frame\":"<<trackedCameraEvidence.frame.load()<<",\"captured_mask\":"<<mask<<",\"steps\":[\"applied\",\"before_draw\",\"after_draw\"]}\n";
            for(unsigned eye=0;eye<2;++eye)for(unsigned step=0;step<3;++step)if(mask&(1u<<(eye*3+step)))for(unsigned block=0;block<4;++block){
                const auto& copy=trackedCameraEvidence.cameras[eye][step][block];
                std::ofstream dump(output.parent_path()/("eye-"+std::to_string(eye)+"-step-"+std::to_string(step)+"-camera-"+std::to_string(block)+".bin"),std::ios::binary);
                dump.write(reinterpret_cast<const char*>(copy.bytes.data()),copy.bytes.size());}
        }
        // All ordinary trace/image serialization has finished. Retain failed or
        // pending evidence and every live/failed-detach resource. This frees only
        // completed saved CPU pixels; never unload the retained hook module.
        report.close();const bool reportSaved=bool(report);
        std::uint64_t releasedPixelBytes=0,releasedPixelCapacity=0;unsigned releasedImages=0;
        const auto releaseImage=[&](ColorCapture& capture){
            const bool captured=capture.complete.load(std::memory_order_acquire)&&capture.step==ColorCapture::Step::Finished;
            const auto released=diagnosticImageLifetime.ReleaseSaved(capture.rgba,captured,capture.serialized,reportSaved);
            releasedPixelBytes+=released.bytes;releasedPixelCapacity+=released.capacityBytes;if(released.released)++releasedImages;
        };
        if(disabled&&diagnosticImageLifetime.Quiescent()){
            for(auto& capture:colorCaptures)releaseImage(capture);
            for(auto& capture:weatherColorCaptures)releaseImage(capture);
            for(auto& capture:desktopCaptures)releaseImage(capture.color);
#ifdef FVR_BC2_SCENE_DEPTH_CAPTURE
            for(auto& capture:sceneDepthCaptures)releaseImage(capture);
            for(auto& capture:sceneDepthColors)releaseImage(capture);
            for(auto& eye:sceneDepthCandidates)for(auto& candidate:eye)releaseImage(candidate.capture);
#endif
        }
        std::ofstream imageRetirement(output.parent_path()/"diagnostic-image-retirement.json");
        imageRetirement<<"{\"hooks_disabled\":"<<(disabled?"true":"false")
            <<",\"graphics_quiescent\":"<<(diagnosticImageLifetime.Quiescent()?"true":"false")
            <<",\"report_saved\":"<<(reportSaved?"true":"false")<<",\"released_images\":"<<releasedImages
            <<",\"released_pixel_bytes\":"<<releasedPixelBytes<<",\"released_capacity_bytes\":"<<releasedPixelCapacity<<"}\n";
        const bool lifecyclePassed=!lifecycleMode||(lifecycleComplete&&lifecycleResult.created&&lifecycleResult.registered&&lifecycleResult.inactive&&lifecycleResult.restored&&lifecycleResult.refsBefore==0&&lifecycleResult.refsOwned==1&&lifecycleResult.refsReleased==0&&lifecycleResult.borrowedRedirects>0&&lifecycleResult.borrowedRedirects==lifecycleResult.borrowedRestores&&lifecycleResult.allRestored&&lifecycleResult.callbacksRestored&&lifecycleResult.requestRefsBefore==lifecycleResult.requestRefsAfter);
        bool stereoPassed=!stereoMode;
        if(stereoMode&&stereoState==StereoExperiment::Released){stereoPassed=(!streamMode||(!tracked.restoreFailures.load()&&!(stereoColorScalePair.counts.restoreFailures||stereoEffectPair.counts.restoreFailures||stereoPostProcessPair.RestoreFailed())&&!contextPatchFailures.load()&&!contextRestoreFailures.load()&&!derivedFailures.load()&&frameBridge.Published()>=4&&frameBridge.Consumed()>=3))&&((e.listsRestored&&e.callbacksRestored&&e.refsBefore==e.refsAfter&&e.initialized&&e.cachedViewsRestored)||
            (stereoRecovery.completedOnStop.load()&&stereoRecovery.completed.load()&&!stereoRecovery.failure.load()&&
             stereoRecovery.retirementStep.load()==unsigned(fvr::runtime::RetirementStep::Complete)))&&(!burstMode||streamMode||e.framesRendered.load()==120)&&(initializationOnly||streamMode||(e.visibilityMask.load()==3&&e.drawMask.load()==3));
            for(unsigned i=1;!initializationOnly&&i<3;++i)stereoPassed&=colorCaptures[i].complete.load(std::memory_order_acquire)&&colorCaptures[i].step==ColorCapture::Step::Finished&&colorCaptures[i].frame==e.initialColorFrame;
        }
        if(disabled&&stereoProgress!=INVALID_HANDLE_VALUE){CloseHandle(stereoProgress);stereoProgress=INVALID_HANDLE_VALUE;}
        if(mainMenuOnly)return reportSaved&&mainMenuPassed?0:12;
        return report&&disabled&&complete&&cameraPassed&&lifecyclePassed&&stereoPassed&&(!(poolOnly||(stereoMode&&!initializationOnly))||poolStage==2)&&(!cameraPulseMode||(pulseComplete&&cameraPulse.applied&&cameraPulse.restored&&(cameraPulseMode==2||cameraPulse.drawLeftCopiesUnchanged)))?0:12;
    }catch(const std::exception& e){sampling.store(false,std::memory_order_release);if(hooksEnabled)disabled=DisableProbeHooks();else {
            if(!fvr::bc2::gameplay::PrepareSessionStop())disabled=false;
            else{
                fvr::bc2::opticFilterRuntime::Disable();fvr::bc2::rigWorkerRuntime::Disable();
                const bool gameplayStopped=fvr::bc2::gameplay::Stop(),menuStopped=fvr::bc2::menu::Stop();
                const bool audioStopped=fvr::bc2::mechanismAudio::DrainAndRemove();
                const bool shellAudioStopped=fvr::bc2::shellReloadAudio::Drain();
                disabled=gameplayStopped&&menuStopped&&audioStopped&&shellAudioStopped&&fvr::bc2::rigWorkerRuntime::Quiescent()&&fvr::bc2::opticFilterRuntime::Quiescent();
            }
        }
        if(destructionWitnessEnabled){
            bool drained=false;for(unsigned attempt=0;attempt<200;++attempt){if(destructionWitness.Stop()){drained=true;break;}Sleep(1);}
            disabled=disabled&&drained;
            if(drained)destructionWitness.Write(std::filesystem::path(config.reportPath).parent_path()/L"destruction-witness");
        }
        if(report)fvr::bc2::WriteProbeFailureJson(report,e.what(),disabled);return 20;}
}
