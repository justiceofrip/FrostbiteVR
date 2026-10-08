#include "Bc2TrackedViews.h"
#include "Bc2StereoProgressEvidence.h"
#include "Bc2StereoRecovery.h"
#include "fvr/runtime/RenderOwnerRecovery.h"
#include "Bc2Gameplay.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2BodyInventorySession.h"
#include "Bc2BodyAmmoRenderer.h"
#include "Bc2BodyAmmoHost.h"
#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2WeaponVisibilityProbe.h"
#include "Bc2BodyHolsterProbe.h"
#include "Bc2MenuRuntime.h"
#include "Bc2ReloadDrawCapture.h"
#include "Bc2ReloadProducerBinding.h"
#include "Bc2RigWorkerRuntime.h"
#include "Bc2OpticFilterRuntime.h"
#include <memory>
#include "Bc2RigPublication.h"
#include "Bc2BodyPropPair.h"
#include "fvr/math/ProjectionOverride.h"
#include "fvr/runtime/ExactWriteBatch.h"
#include "fvr/graphics/D3D11FrameBridge.h"
#include "fvr/graphics/D3D11PresentationPacing.h"
#include "fvr/graphics/DesktopImageEvidence.h"
#include "fvr/graphics/DiagnosticImageLifetime.h"
#include "fvr/graphics/DesktopWindowMode.h"
#include "NativeWorkPool.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "Bc2Profile.h"
#include "Bc2Camera.h"
#include "fvr/interaction/TrackingMath.h"
#include "NativeProbeConfig.h"
#include "Bc2MagazineReloadSession.h"
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
#include <stdexcept>
#include <vector>
namespace {
using Microsoft::WRL::ComPtr;
using WorldFn=void(__thiscall*)(void*,void*);
using PrepareFn=void*(__thiscall*)(void*,void*,void*);
using DrawFn=void(__thiscall*)(void*,void*,void*,void*);
using VisibilityFn=void(__thiscall*)(void*,void*,void*,void*,void*,void*,void*);
using UpdateFn=void(__thiscall*)(void*,void*,unsigned,float,void*,void*,void*);
UpdateFn updateOriginal=nullptr;std::atomic<unsigned> updateCalls=0;
VisibilityFn visibilityOriginal=nullptr;
WorldFn worldOriginal=nullptr;PrepareFn prepareOriginal=nullptr;DrawFn drawOriginal=nullptr;
std::atomic<bool> sampling=false;
std::atomic<unsigned> allocated=0,worldCalls=0,prepareCalls=0,drawCalls=0;
std::atomic<unsigned> visibilityAllocated=0,visibilityCalls=0;
LONG started=0;unsigned cameraPulseMode=0;bool lifecycleMode=false,stereoMode=false;
fvr::bc2::ViewLifecycleCandidates lifecycle{};fvr::bc2::ViewCallbackCandidates viewCallbacks{};
struct LifecycleResult {unsigned borrowedRedirects=0,borrowedRestores=0,allBefore=0,allAfter=0,requestRefsBefore=0,requestRefsAfter=0;bool allRestored=false,callbacksRestored=false;bool claimed=false,created=false,registered=false,inactive=false,restored=false,ownerUnchanged=false;unsigned view=0,refsBefore=0,refsOwned=0,refsReleased=0,countBefore=0,countDuring=0,countAfter=0,thread=0;std::atomic<bool> complete=false;} lifecycleResult;
using CacheFn=void(__thiscall*)(void*);
CacheFn updateView=nullptr,updateProjection=nullptr,updateFrustum=nullptr;
struct CameraVariant {fvr::math::Matrix4 projection{};std::uint64_t frustumHash=0;};
struct MappedCameraResult {bool complete=false;float viewError=-1,projectionError=-1,viewProjectionRelativeError=-1;};
struct CullCameraResult {bool passed=false;float maxDistance=-1,normalError=-1;};
struct CameraExperiment {
    std::atomic<bool> claimed=false,complete=false;
    bool liveUnchanged=false;float baselineViewError=-1,baselineProjectionError=-1;
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
std::array<void*,11> hookEntries{};std::atomic<bool> deferredCleanup=false;
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
void CaptureWorldColor(ColorCapture& c,unsigned frame,ID3D11Texture2D* explicitSource=nullptr) noexcept {
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
               (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)){fail(E_INVALIDARG);return;}
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
        updateView(copy.bytes.data());updateProjection(copy.bytes.data());updateFrustum(copy.bytes.data());
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
    if(camera)for(unsigned i=0;i<cameraExperiment.mapped.size();++i){
        fvr::math::Pose reference{},pose{};pose.position={i%2?-.032f:.032f,.15f,-.2f};
        const float angle=float(i)*.07f;pose.orientation={0,std::sin(angle),0,std::cos(angle)};
        const auto world=fvr::math::ComposeRuntimeHeadWithLhCamera(camera->camera,reference,pose,1);if(!world)continue;
        const fvr::math::FovTangents fov{i%2?-1.3f:-.7f,i%2?.7f:1.3f,i%3?.85f:1.1f,i%3?-.65f:-1.1f};
        const float nearPlane=i<4?.04f:.1f,farPlane=i<4?100.f:2000.f;
        auto copy=fvr::bc2::BuildRenderViewCopy(original,*world,fov,nearPlane,farPlane);if(!copy)continue;
        const auto projection=fvr::math::MakeLhProjectionFromFovTangents(fov,nearPlane,farPlane);if(!projection)continue;
        const auto native=fvr::engine::NativeEye({*world,*projection});if(!native)continue;
        updateView(copy->bytes.data());updateProjection(copy->bytes.data());updateFrustum(copy->bytes.data());
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
        updateView(copy->bytes.data());updateProjection(copy->bytes.data());updateFrustum(copy->bytes.data());
        std::array<fvr::math::Vec4,6> planes{};std::memcpy(planes.data(),copy->bytes.data()+0x180,sizeof(planes));
        auto& result=cameraExperiment.culling[i];result.normalError=0;result.maxDistance=-farPlane;
        for(const auto& plane:planes){const float length=std::sqrt(plane.x*plane.x+plane.y*plane.y+plane.z*plane.z);if(!std::isfinite(length)||!std::isfinite(plane.w)){result.normalError=1;break;}result.normalError=(std::max)(result.normalError,std::abs(length-1));}
        for(unsigned eye=0;eye<2;++eye){const auto& fov=eye?right:left;const auto& world=eye?*eye1:*eye0;
            for(float z:{nearPlane,(nearPlane+farPlane)*.5f,farPlane})for(float x:{fov.left,(fov.left+fov.right)*.5f,fov.right})for(float y:{fov.down,(fov.down+fov.up)*.5f,fov.up}){
                const auto point=fvr::math::TransformRowVector({x*z,y*z,z,1},world);
                for(const auto& plane:planes){const float distance=plane.x*point.x+plane.y*point.y-plane.z*point.z+plane.w;
                    if(!std::isfinite(distance))result.normalError=1;else result.maxDistance=(std::max)(result.maxDistance,distance);}
            }
        }
        result.passed=result.normalError<.0001f&&result.maxDistance<=.001f;
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
            updateView(copy->bytes.data());updateFrustum(copy->bytes.data());written_[i]=*copy;
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
struct CallbackSnapshot {unsigned object=0,vtable=0,borrowed=0;bool remember=false,list=false;ListSnapshot registered{};};
bool SnapshotCallbacks(std::uintptr_t world,std::uintptr_t mainView,ListSnapshot& list,std::array<CallbackSnapshot,16>& callbacks)noexcept {
    if(!list.Capture(world+0x9c)||!list.count||list.count>callbacks.size())return false;
    for(unsigned i=0;i<list.count;++i){auto& cb=callbacks[i];cb.object=list.values[i];cb.vtable=U32(cb.object);const auto create=U32(cb.vtable+28),destroy=U32(cb.vtable+32);
        const auto noop=[](unsigned fn){std::array<unsigned char,3> code{};return fn>=imageBase&&fn<imageBase+profile.imageSize&&Read(fn,code.data(),3)&&code==std::array<unsigned char,3>{0xc2,4,0};};
        if(create==imageBase+viewCallbacks.rememberMain&&noop(destroy)){cb.remember=true;cb.borrowed=U32(cb.object+0xbc);if(cb.borrowed!=mainView)return false;}
        else if(create==imageBase+viewCallbacks.registerView&&destroy==imageBase+viewCallbacks.unregisterView){cb.list=true;if(!cb.registered.Capture(cb.object+0x20))return false;}
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
fvr::bc2::ViewInitializationCandidates initialization{};
std::atomic<unsigned> inputSamples=0,inputTrackedSamples=0;std::uint64_t lastInputGeneration=0;
bool opticEvidenceEnabled=false;bool passEvidenceEnabled=false,uncapMirror=false;bool streamMode=false;std::wstring streamToken;fvr::graphics::D3D11FrameBridge frameBridge;
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
    bool afterCaptured=false;
    ListSnapshot historical{},before{},expected{},after{};
} stereoShutdown;
fvr::bc2::StereoProgressEvidence stereoProgressEvidence;
struct StereoRecoveryNative {
    struct Code {unsigned address=0,size=0;std::uint64_t hash=0;};
    std::array<Code,19> code{};
    unsigned worldType=0,subsystemType=0,requestAddRef=0,requestRelease=0,retiredCacheResize=0;
    fvr::runtime::RenderOwnerRecovery policy;
    std::atomic<unsigned> callbacks=0,attempts=0,completed=0,rejected=0,failure=0,retirementStep=0;
    std::atomic<unsigned> cacheClearCalls=0,cacheClearReceipts=0;
    std::atomic<unsigned> oldWorld=0,oldRequest=0,oldEye=0,newWorld=0,newRequest=0,retiredViews=0;
    std::atomic<bool> completedOnStop=false;
    std::atomic<std::uint64_t> cancelUnwrittenRequest=0;
} stereoRecovery;
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
    const int length=snprintf(line,sizeof(line),"{\"step\":\"%s\",\"frame\":%u,\"main\":%u,\"eye\":%u,\"initialized\":%u,\"children\":%u,\"cached_restored\":%u,\"world\":%u,\"request\":%u,\"old_world\":%u,\"old_request\":%u,\"new_world\":%u,\"new_request\":%u,\"recovery_attempts\":%u,\"recovery_completed\":%u,\"recovery_failure\":%u,\"retirement_step\":%u,\"tick_ms\":%llu,\"published\":%llu,\"consumed\":%llu}\n",label,e.frame.load(),e.main,e.eye,unsigned(e.initialized),e.childrenAfter,unsigned(e.cachedViewsRestored),e.world,e.request,stereoRecovery.oldWorld.load(),stereoRecovery.oldRequest.load(),stereoRecovery.newWorld.load(),stereoRecovery.newRequest.load(),stereoRecovery.attempts.load(),stereoRecovery.completed.load(),stereoRecovery.failure.load(),stereoRecovery.retirementStep.load(),GetTickCount64(),frameBridge.Published(),frameBridge.Consumed());
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
        auto copy=fvr::bc2::BuildTransformCopy(original,shifted);if(!copy)return false;updateView(copy->bytes.data());updateFrustum(copy->bytes.data());camera[i]=*copy;
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
std::array<WeaponContext,32> weaponContexts;std::atomic<unsigned> weaponContextCount=0,weaponProjectionCopies=0,weaponContextFailures=0;
struct ProjectionBinding {unsigned context=0,address=0,callsite=0,thread=0,frame=0;int gather=-1;std::array<float,16> values{};};
std::array<ProjectionBinding,512> projectionEvidence;std::atomic<unsigned> projectionCount=0;
void* __fastcall ProjectionContextHook(void* self,void*,const void* matrix){
    RenderCallbackScope callback;
    const auto returnSite=reinterpret_cast<unsigned>(_ReturnAddress());
    std::array<float,16> weaponProjection{};bool weapon=false;
    if(viewAnchorMode&&streamMode&&tracked.state.load(std::memory_order_acquire)==1&&returnSite==imageBase+viewAnchor.projectionCaller){
        std::array<float,16> source{},right{};
        if(Read(reinterpret_cast<std::uintptr_t>(matrix),source.data(),64)){
            std::memcpy(right.data(),tracked.right[0].bytes.data()+0x2e0,64);
            if(const auto adjusted=fvr::math::RetargetFirstPersonProjection(source,right)){weaponProjection=*adjusted;matrix=weaponProjection.data();weapon=true;++weaponProjectionCopies;}
        }
        if(!weapon)++weaponContextFailures;
    }
    void* const result=projectionContextOriginal(self,matrix);
    if(!streamMode||tracked.state.load(std::memory_order_acquire)!=1)return result;
    if(weapon){
        const auto index=weaponContextCount.fetch_add(1);
        if(index>=weaponContexts.size()){++weaponContextFailures;return result;}
        auto& w=weaponContexts[index];const auto at=reinterpret_cast<unsigned>(self);
        w.view=U32(at+0x1c);w.projection=U32(at+0x20);w.position=U32(at+0x24);
        if(!Read(w.view,w.right.data(),64)||!Read(w.projection,w.right.data()+16,64)||!Read(w.position,w.right.data()+32,16)){++weaponContextFailures;return result;}
        // The authored first-person transform now follows the tracked right eye.
        std::array<float,16> expectedView{};std::memcpy(expectedView.data(),tracked.right[0].bytes.data()+0x220,64);
        for(unsigned i=0;i<16;++i)if(std::abs(w.right[i]-expectedView[i])>.001f){++weaponContextFailures;return result;}
        w.left=w.right;std::memcpy(w.left.data(),tracked.written[0].bytes.data()+0x220,64);
        std::array<float,16> leftProjection{};std::memcpy(leftProjection.data(),tracked.written[0].bytes.data()+0x2e0,64);
        const auto adjusted=fvr::math::RetargetFirstPersonProjection(weaponProjection,leftProjection);if(!adjusted){++weaponContextFailures;return result;}
        std::memcpy(w.left.data()+16,adjusted->data(),64);std::memcpy(w.left.data()+32,tracked.written[0].bytes.data()+0x80,16);
        w.frame.store(stereoExperiment.frame,std::memory_order_release);
    }
    const auto callsite=reinterpret_cast<unsigned>(_ReturnAddress()),context=reinterpret_cast<unsigned>(self),address=U32(context+0x20);
    std::array<float,16> values{};if(!Read(address,values.data(),64)){
        if(callsite==imageBase+projectionOverrides.meshCaller||callsite==imageBase+projectionOverrides.terrainCaller){derivedFrameFailed.store(true,std::memory_order_release);derivedFailures.fetch_add(1);}return result;}
    if(trackedCameraEvidence.frame.load(std::memory_order_acquire)==stereoExperiment.frame){
        const auto index=projectionCount.fetch_add(1);if(index<projectionEvidence.size()){
            auto& e=projectionEvidence[index];e.context=context;e.address=address;e.callsite=callsite;e.thread=GetCurrentThreadId();e.frame=stereoExperiment.frame;e.gather=gatheringEvidenceEye;e.values=values;}}
    // Only the two signature-verified depth-bias paths are eligible. Worker jobs
    // finish at the native gather/render barrier before Prepare/Draw scopes read.
    if(drawingNativeView||(callsite!=imageBase+projectionOverrides.meshCaller&&callsite!=imageBase+projectionOverrides.terrainCaller))return result;
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
        if(weapons>weaponContexts.size()||weaponContextFailures.load())return false;
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
std::int64_t reloadDrawFrequency=0;
std::int64_t ReloadDrawNowNs()noexcept {
    if(reloadDrawFrequency<=0)return 0;const auto qpc=Qpc();
    return (qpc/reloadDrawFrequency)*1000000000+(qpc%reloadDrawFrequency)*1000000000/reloadDrawFrequency;
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
void STDMETHODCALLTYPE NonIndexedDrawHook(ID3D11DeviceContext* context,UINT count,UINT start){
    if(opticEvidenceEnabled)fvr::bc2::opticFilterRuntime::CaptureDraw(context,false,count,start);
    if(context==observedImmediateContext&&drawingEvidenceEye>=0)CapturePassBuffers(context,unsigned(drawingEvidenceEye),count,0);
    nonIndexedDrawOriginal(context,count,start);
}
void STDMETHODCALLTYPE IndexedDrawHook(ID3D11DeviceContext* context,UINT count,UINT start,INT base){
    if(opticEvidenceEnabled)fvr::bc2::opticFilterRuntime::CaptureDraw(context,true,count,start,base);
    if(context==observedImmediateContext)reloadDrawCapture.ObserveIndexed(context,count,start,base);
    if(context==observedImmediateContext&&drawingEvidenceEye>=0){CaptureGeometryBuffers(context,unsigned(drawingEvidenceEye),count);CapturePassBuffers(context,unsigned(drawingEvidenceEye),count,1);}
    indexedDrawOriginal(context,count,start,base);
}
void SaveTrackedCameraEvidence(unsigned view,unsigned eye,unsigned step)noexcept {
    if(trackedCameraEvidence.frame.load(std::memory_order_acquire)!=stereoExperiment.frame)return;
    const unsigned offsets[]={layout.primaryOffset,layout.secondaryOffset,layout.thirdOffset,layout.fourthOffset};
    for(unsigned block=0;block<4;++block)if(!Read(view+offsets[block],trackedCameraEvidence.cameras[eye][step][block].bytes.data(),0x460))return;
    trackedCameraEvidence.mask.fetch_or(1u<<(eye*3+step),std::memory_order_release);
}
void BeginTrackedFrame(unsigned incomingFrame)noexcept {
    auto& e=stereoExperiment;
    const auto prior=tracked.state.load(std::memory_order_acquire);
    if(prior==1||prior==3){if(prior==3)stereoRecovery.cancelUnwrittenRequest.store(tracked.lease.requestId,std::memory_order_release);
        RecordStereoProgress(fvr::bc2::StereoProgressReason::BeginActiveTransaction,incomingFrame);return;}
    reinterpret_cast<fvr::bc2::SetViewActiveFn>(imageBase+lifecycle.setActive)(reinterpret_cast<void*>(e.eye),false);
    e.drawMask.store(0,std::memory_order_release);e.visibilityMask.store(0,std::memory_order_release);e.frame.store(incomingFrame,std::memory_order_release);
    if(tracked.restoreFailures.load()||contextRestoreFailures.load()){RecordStereoProgress(fvr::bc2::StereoProgressReason::BeginRestoreFailure,incomingFrame);return;}
    fvr::ipc::FrameLease lease{};if(!frameBridge.TryBegin({(std::uint64_t(e.world)<<32)|e.request,incomingFrame,1},lease)){RecordStereoProgress(fvr::bc2::StereoProgressReason::BridgeNoLease,incomingFrame);return;}
    weaponContextCount.store(0);derivedCount.store(0);derivedFrameFailed.store(false);for(auto& binding:derivedProjections)binding.frame.store(0);
    tracked.lease=lease;tracked.bodyPropPair.Reset();tracked.capturesOkay=true;tracked.state.store(3,std::memory_order_release);
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
        for(unsigned i=0;i<2;++i)if(!Read(e.main+(i?layout.secondaryOffset:layout.primaryOffset),tracked.saved[i].bytes.data(),0x460)){cancel();return;}
        auto source=tracked.saved;if(!fvr::bc2::gameplay::AdjustViewBase(source,tracked.lease.tracking)){cancel();return;}
        auto plan=fvr::bc2::BuildTrackedViews(source,tracked.lease.tracking,tracked.lease.tracking.worldUnitsPerMeter);if(!plan){cancel();return;}
        for(auto& eye:plan->camera)for(auto& camera:eye){updateView(camera.bytes.data());updateProjection(camera.bytes.data());updateFrustum(camera.bytes.data());}
        vehicleCameraEvidenceFrame.store(fvr::bc2::gameplay::ObserveVehicleCameraPlan(e.frame,tracked.lease.tracking,tracked.saved[0],source[0],{plan->camera[0][0],plan->camera[1][0]})?e.frame.load():0,std::memory_order_release);
        if(!trackedCameraEvidence.frame.load(std::memory_order_acquire))trackedCameraEvidence.frame.store(e.frame,std::memory_order_release);
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
    }else if(view==e.eye&&tracked.state.load(std::memory_order_acquire)==1){
        // Apply the right camera at the same post-clamp boundary. This also
        // preserves the conservative culling extent from the owned stereo plan.
        SetDiagnosticCameras(e.eye,tracked.right);SaveTrackedCameraEvidence(e.eye,1,0);
        if(vehicleCameraEvidenceFrame.load(std::memory_order_acquire)==e.frame.load()){fvr::bc2::RenderViewCopy actualRight{};if(Read(e.eye+layout.primaryOffset,actualRight.bytes.data(),0x460))fvr::bc2::gameplay::ObserveVehicleCameraApplied(e.frame,1,actualRight);}
    }
}

void FinishTrackedFrame(bool completePair)noexcept {
    const auto state=tracked.state.load(std::memory_order_acquire);if(state!=1&&state!=3)return;auto& e=stereoExperiment;bool restored=state==3;
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
    if(!(completePair&&restored&&tracked.capturesOkay&&e.visibilityMask.load(std::memory_order_acquire)==3&&frameBridge.PublishRestored(tracked.lease)))frameBridge.Cancel(tracked.lease);
    tracked.bodyPropPair.Reset();
    tracked.state.store(2,std::memory_order_release);
}
void PumpFrameBridge()noexcept {
    bodyAmmoRenderer.PumpStop();
    if(!streamMode)return;PollEyeGpuEvidence();PollGeometryBuffers();PollPassBuffers();
    const auto cancelRequest=stereoRecovery.cancelUnwrittenRequest.exchange(0,std::memory_order_acq_rel);
    if(fvr::runtime::ShouldCancelUnwritten(cancelRequest,tracked.lease.requestId,
       tracked.state.load(std::memory_order_acquire)==3))FinishTrackedFrame(false);
    if(!frameBridge.Connected()){
        static bool attempted=false;if(attempted)return;attempted=true;
        const auto renderer=U32(imageBase+profile.rendererGlobal);if(U32(renderer)!=imageBase+profile.rendererVtable)return;
        const auto device=U32(renderer+0x7c);frameBridge.ConnectGraphics(reinterpret_cast<ID3D11Device*>(device),streamToken);
    }
    frameBridge.PumpGraphics();
    fvr::interaction::InputFrame controls{};
    const auto controlsResult=frameBridge.ReadInput(controls);
    if(opticEvidenceEnabled&&controlsResult!=fvr::ipc::ChannelResult::Busy)
        fvr::bc2::opticFilterRuntime::SetConnected(controlsResult==fvr::ipc::ChannelResult::Ok&&controls.focused&&controls.headValid);
    if(controlsResult==fvr::ipc::ChannelResult::Ok&&controls.generation!=lastInputGeneration){
        lastInputGeneration=controls.generation;++inputSamples;
        if(controls.focused&&controls.headValid&&controls.hands[0].gripTracked&&controls.hands[1].aimTracked)++inputTrackedSamples;
    }
    if(!sampling.load(std::memory_order_acquire)&&stereoExperiment.stage.load(std::memory_order_acquire)==StereoExperiment::Released)frameBridge.CloseGraphics();
}
fvr::graphics::SwapChainPresentFn desktopPresentOriginal=nullptr;
IDXGISwapChain* observedSwapChain=nullptr;unsigned observedRenderer=0;
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
HRESULT STDMETHODCALLTYPE DesktopPresentHook(IDXGISwapChain* chain,UINT interval,UINT flags){
    fvr::graphics::DiagnosticImageLifetime::Scope imageScope(diagnosticImageLifetime);
    if(!imageScope.Active())return desktopPresentOriginal(chain,interval,flags);
    const bool matching=chain==observedSwapChain&&U32(imageBase+profile.rendererGlobal)==observedRenderer&&
        U32(observedRenderer)==imageBase+profile.rendererVtable&&U32(observedRenderer+0x88)==reinterpret_cast<unsigned>(chain);
    const bool active=matching&&uncapMirror&&sampling.load(std::memory_order_acquire)&&frameBridge.Connected()&&!frameBridge.Fatal();
    unsigned scheduled=0;
    if(matching){++desktopPresentCalls;if(interval==1)++desktopSyncOne;
        if(flags&DXGI_PRESENT_TEST)++desktopPresentTests;
        if(active&&!(flags&DXGI_PRESENT_TEST)&&interval>0&&interval<=4)++desktopSyncBypassed;
        // The swapchain buffer is later than DrawView/eye capture and includes
        // native desktop/UI composition. Read it; never replace its contents.
        ObserveDesktopMode(chain,flags);
        scheduled=ObserveDesktopBuffer(chain,flags);
        if(!(flags&DXGI_PRESENT_TEST))fvr::bc2::menu::Present(chain);
    }
    const auto result=fvr::graphics::PresentDesktopForVr(desktopPresentOriginal,chain,interval,flags,active);
    if(matching){
        desktopPresentLastResult.store(result,std::memory_order_release);
        if(result==S_OK)++desktopPresentOkay;
        else if(result==DXGI_STATUS_OCCLUDED)++desktopPresentOccluded;
        else if(SUCCEEDED(result))++desktopPresentStatus;
        else {++desktopPresentFailed;long empty=0;desktopPresentFirstFailure.compare_exchange_strong(empty,result);}
        for(unsigned i=0;i<desktopCaptures.size();++i)if(scheduled&(1u<<i))desktopCaptures[i].firstPresentResult=result;
    }
    return result;
}
bool CaptureTrackedEye(unsigned eye)noexcept {
    const auto renderer=U32(imageBase+profile.rendererGlobal);if(U32(renderer)!=imageBase+profile.rendererVtable)return false;
    auto* device=reinterpret_cast<ID3D11Device*>(U32(renderer+0x7c));ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
    ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);if(!target)return false;
    ComPtr<ID3D11Resource> resource;target->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return false;
    return frameBridge.CaptureEye(eye,texture.Get(),tracked.lease);
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
struct LiveStereoSnapshot {
    fvr::bc2::LiveStereoRetirementGraph graph{};fvr::bc2::StereoSurvivors survivors{};
    ListSnapshot main{},all{},requests{},callbacks{};
    std::array<CallbackSnapshot,16> callbackValues{};
};
bool CaptureLiveStereoShutdown(LiveStereoSnapshot& out)noexcept {
    const auto& e=stereoExperiment;auto& g=out.graph;
    const auto quiescent=[]()noexcept{const auto state=tracked.state.load(std::memory_order_acquire);
        const auto pool=workPool.stage.load(std::memory_order_acquire);
        return state!=1&&state!=3&&!tracked.anchorWritten&&!tracked.restoreFailures.load()&&!contextRestoreFailures.load()&&
            !stereoRecovery.callbacks.load(std::memory_order_acquire)&&pool!=1&&pool!=3&&fvr::bc2::rigWorkerRuntime::Quiescent();};
    // Existing native Update boundary: request state3 follows completed original
    // draw/visibility jobs, before the next world update can enqueue new jobs.
    // Callback count is an additional check, not the serialization mechanism.
    unsigned char eyeActive=1;
    if(!CurrentScene(e.world,e.request)||!quiescent()||!e.callbacks.Matches()||
       U32(e.main)!=imageBase+layout.vtable||U32(e.main+0x70)!=e.request||
       U32(e.eye)!=imageBase+layout.vtable||U32(e.eye+0x70)!=e.request||
       !Read(e.eye+layout.activeOffset,&eyeActive,1)||eyeActive)return false;
    for(const auto& code:stereoRecovery.code)if(!code.address||Hash(code.address,code.size)!=code.hash)return false;
    if(!out.main.Capture(e.request+0x24)||out.main.count!=2||out.main.values[0]!=e.main||out.main.values[1]!=e.eye||
       !out.all.Capture(e.request+0x14)||!out.requests.Capture(e.world+0xac)||out.requests.count!=1||out.requests.values[0]!=e.request||
       !SnapshotCallbacks(e.world,e.main,out.callbacks,out.callbackValues)||!SameEntries(out.callbacks,e.callbacks))return false;
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
    for(unsigned i=0;i<out.callbacks.count;++i){const auto& cb=out.callbackValues[i];const auto& original=e.callbackValues[i];
        if(cb.object!=original.object||cb.vtable!=original.vtable||cb.remember!=original.remember||cb.list!=original.list||
           (cb.remember&&(cb.borrowed!=original.borrowed||cb.borrowed!=e.main))||
           (cb.list&&!SameEntries(cb.registered,out.all)))return false;
    }
    if(!fvr::bc2::CaptureLiveStereoSurvivors(g,out.survivors)||out.survivors.count!=e.allViews.count||
       out.survivors.referencesAfter!=e.refsBefore||!out.main.Matches()||!out.all.Matches()||
       !out.requests.Matches()||!out.callbacks.Matches())return false;
    for(unsigned i=0;i<out.callbacks.count;++i)if(out.callbackValues[i].list&&!out.callbackValues[i].registered.Matches())return false;
    return CurrentScene(e.world,e.request)&&quiescent()&&U32(e.request+4)==g.requestReferences;
}
bool RefreshShutdownGeneration()noexcept {
    LiveStereoSnapshot first{},again{};
    if(!CaptureLiveStereoShutdown(first)||!CaptureLiveStereoShutdown(again)||first.graph!=again.graph||
       first.survivors!=again.survivors||!SameEntries(first.main,again.main)||!SameEntries(first.all,again.all)||
       !SameEntries(first.callbacks,again.callbacks)){++stereoShutdown.rejected;return false;}
    auto& e=stereoExperiment;stereoShutdown.historical=e.allViews;stereoShutdown.before=again.all;
    stereoShutdown.requestReferencesBefore=again.graph.requestReferences;
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
    ListSnapshot main{},all{},requests{},callbacks{},currentCallbacks{};
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
       !out.requests.Matches()||!out.callbacks.Matches()||!out.currentCallbacks.Matches())return false;
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
           !snapshot.main.Matches()||!snapshot.all.Matches()||stereoRecovery.callbacks.load(std::memory_order_acquire))return false;
        for(unsigned i=0;i<g.count;++i){const auto& v=g.views[i];
            if(U32(v.address+0x70)!=g.request||U32(v.address+0x20)!=1||
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
    const auto ready=stereoRecovery.policy.Observe({e.world,e.request},{world,request},incomingFrame,
        CurrentScene(world,request),state==1||state==3,tracked.restoreFailures.load()||contextRestoreFailures.load());
    if(ready==fvr::runtime::RecoveryReadiness::DrainTransaction){
        if(state==3)stereoRecovery.cancelUnwrittenRequest.store(tracked.lease.requestId,std::memory_order_release);return false;
    }
    if(ready!=fvr::runtime::RecoveryReadiness::Ready||stereoRecovery.callbacks.load(std::memory_order_acquire)||
       workPool.stage.load(std::memory_order_acquire)==1||workPool.stage.load(std::memory_order_acquire)==3||
       !fvr::bc2::rigWorkerRuntime::Quiescent())return false;
    RetiredStereoSnapshot snapshot{};
    if(!CaptureRetiredStereo(world,request,snapshot)){++stereoRecovery.rejected;return false;}
    ++stereoRecovery.attempts;stereoRecovery.oldWorld=e.world;stereoRecovery.oldRequest=e.request;stereoRecovery.oldEye=e.eye;
    stereoRecovery.newWorld=world;stereoRecovery.newRequest=request;stereoRecovery.retiredViews=snapshot.graph.count;
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
    StereoCheckpoint("before_native_rebuild");
    const auto frameBefore=U32(world+0x88);const auto timeBefore=Hash(world+0x8c,8);
    const auto primaryBefore=Hash(e.main+layout.primaryOffset,0x460),secondaryBefore=Hash(e.main+layout.secondaryOffset,0x460);
    using Rebuild=void(__thiscall*)(void*);
    reinterpret_cast<Rebuild>(imageBase+initialization.rebuild)(reinterpret_cast<void*>(world));
    e.clockPreserved=frameBefore==U32(world+0x88)&&timeBefore==Hash(world+0x8c,8);
    e.camerasPreserved=primaryBefore==Hash(e.main+layout.primaryOffset,0x460)&&secondaryBefore==Hash(e.main+layout.secondaryOffset,0x460);
    const bool borrowedOkay=RestoreBorrowed(e.callbacks,e.callbackValues,e.eye,e.redirects,e.restores);
    e.childrenAfter=ChildCount(e.eye);
    bool generationOkay=CaptureOriginalGeneration(e.allViews,unsigned(request+0x14),e.eye,e.allViews.count);
    for(unsigned i=0;i<e.callbacks.count;++i){auto& cb=e.callbackValues[i];if(cb.list)generationOkay&=CaptureOriginalGeneration(cb.registered,cb.object+0x20,e.eye,cb.registered.count);}
    e.initialized=e.childrenAfter>0&&ChildCount(e.main)==e.childrenBefore&&generationOkay&&borrowedOkay&&e.camerasPreserved&&e.clockPreserved;
    StereoCheckpoint("after_native_rebuild");
    if(!e.initialized){
        if(borrowedOkay&&U32(e.eye+0x20)==1){reinterpret_cast<fvr::bc2::ViewRefFn>(imageBase+lifecycle.release)(fresh);e.cachedViewsRestored=RefreshRegisteredCallbacks();}StereoCheckpoint("initialization_rejected");
        e.stage.store(StereoExperiment::Failed,std::memory_order_release);return;
    }
    if(initializationOnly){e.retireFrame=incomingFrame+120;e.stage.store(StereoExperiment::Retiring,std::memory_order_release);}
    else if(streamMode){BeginTrackedFrame(incomingFrame);e.stage.store(StereoExperiment::Armed,std::memory_order_release);}
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
    updateOriginal(self,request,frame,dt,extra,out1,out2); // One simulation update.
    if(deferredCleanup.load(std::memory_order_acquire)){
        const auto stage=stereoExperiment.stage.load(std::memory_order_acquire);
        if(stage!=StereoExperiment::Armed&&stage!=StereoExperiment::Retiring){
            if(hookEntries[4])MH_DisableHook(hookEntries[4]);
            if(streamMode&&bodyAmmoRenderer.Quiescent())for(unsigned index=0;index<3;++index)if(hookEntries[index])MH_DisableHook(hookEntries[index]);
        }
    }
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
    visibilityOriginal(self,request,view,params,extra,out1,out2);
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
    worldOriginal(self,request); // Exactly one original call; no simulation/render replay.
    if(workPool.stage.load(std::memory_order_acquire)==1&&GetCurrentThreadId()==workPool.thread){const auto begin=U32(workPool.owner+0x850),end=U32(workPool.owner+0x854);if(end>=begin&&end-begin<=workPool.storage.size())workPool.peak=(std::max)(workPool.peak,(end-begin)/40);}
    if(streamMode)FinishTrackedFrame(false);
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
        data=prepareOriginal(self,request,view);
        if(producerScope)producerScope->Complete(producerKey&&ReloadProducerViewKey(reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(request),reinterpret_cast<std::uintptr_t>(view))==producerKey,ReloadDrawNowNs());
    }
    if(current&&current->request==reinterpret_cast<std::uintptr_t>(request)&&current->world==reinterpret_cast<std::uintptr_t>(self))++current->prepared;
    return data;
}
void __fastcall DrawHook(void* self,void*,void* request,void* view,void* data){
    fvr::graphics::DiagnosticImageLifetime::Scope imageScope(diagnosticImageLifetime);
    if(!imageScope.Active()){drawOriginal(self,request,view,data);return;}
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
    const bool bodyAmmoEyeOwned=bodyAmmoObservationEnabled&&trackedEvidence&&
        reinterpret_cast<unsigned>(self)==stereoExperiment.world&&reinterpret_cast<unsigned>(request)==stereoExperiment.request&&
        U32(stereoExperiment.world+0x88)==stereoExperiment.frame&&U32(address)==imageBase+layout.vtable&&
        U32(address+0x70)==stereoExperiment.request&&U32(stereoExperiment.request+8)==stereoExperiment.world;
    const auto bodyAmmoBefore=bodyAmmoEyeOwned?fvr::bc2::rigPublication::ReadBodyAmmoRenderSource(ReloadDrawNowNs()):std::nullopt;
    const auto bodyHolsteredBefore=bodyAmmoEyeOwned?fvr::bc2::rigPublication::ReadHolsteredBodyRenderSource(ReloadDrawNowNs()):std::nullopt;
    const auto bodyCarriedCurrent=bodyAmmoEyeOwned?fvr::bc2::rigPublication::ReadCarriedBodyRenderSource(ReloadDrawNowNs()):std::nullopt;
    const fvr::bc2::BodyPropSources bodySourcesBefore{bodyAmmoBefore,bodyHolsteredBefore,bodyCarriedCurrent};
    const auto bodyPairKey=bodyAmmoEyeOwned?fvr::bc2::CarriedPairKey(tracked.lease):fvr::bc2::BodyCarriedPairKey{};
    const auto bodyPair=bodyAmmoEyeOwned?tracked.bodyPropPair.Begin(bodyPairKey,evidenceEye,bodySourcesBefore):nullptr;
    const auto holsterBefore=trackedEvidence&&bodyHolsterProbeMode?
        fvr::bc2::gameplay::ReadBodyHolsterFixture(ReloadDrawNowNs()):nullptr;
    const auto visibilityBefore=trackedEvidence&&weaponVisibilityProbeMode?
        fvr::bc2::gameplay::ReadWeaponVisibilityProbe(ReloadDrawNowNs()):nullptr;
    if(trackedEvidence){ObserveContexts(evidenceEye+2);SaveTrackedCameraEvidence(unsigned(address),evidenceEye,1);BeginEyeGpuEvidence(evidenceEye);}
    if(trackedEvidence){
        fvr::bc2::ReloadDrawFrameEvidence frame{};
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
        void Finish()noexcept {if(active){active=false;reloadDrawCapture.EndEye();reloadDrawCapture.Poll(observedImmediateContext);}}
        ~DrawEvidenceEnd(){Finish();}
    } drawEvidenceEnd{trackedEvidence};
    const auto priorEvidenceEye=drawingEvidenceEye;
    if(trackedEvidence&&trackedCameraEvidence.frame.load(std::memory_order_acquire)==stereoExperiment.frame)drawingEvidenceEye=int(evidenceEye);
    {ScopedContextEye constants;
        if(trackedEvidence&&evidenceEye==0&&reinterpret_cast<unsigned>(self)==stereoExperiment.world&&reinterpret_cast<unsigned>(request)==stereoExperiment.request){
            if(!constants.Apply()){contextPatchFailures.fetch_add(1);tracked.capturesOkay=false;}
        }
        std::optional<fvr::bc2::opticFilterRuntime::View> opticView;
        if(trackedEvidence&&reinterpret_cast<unsigned>(self)==stereoExperiment.world&&reinterpret_cast<unsigned>(request)==stereoExperiment.request)
            opticView=fvr::bc2::opticFilterRuntime::View{reinterpret_cast<unsigned>(request),unsigned(address),stereoExperiment.frame.load(),int(evidenceEye)};
        OpticViewScope opticScope(opticView);
        const auto wasDrawing=drawingNativeView;drawingNativeView=true;drawOriginal(self,request,view,data);drawingNativeView=wasDrawing;
    }drawingEvidenceEye=priorEvidenceEye;
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
    if(bodyAmmoEyeOwned&&tracked.state.load(std::memory_order_acquire)==1&&
        U32(stereoExperiment.world+0x88)==stereoExperiment.frame&&U32(address)==imageBase+layout.vtable&&
        U32(address+0x70)==stereoExperiment.request&&U32(stereoExperiment.request+8)==stereoExperiment.world){
        const auto now=ReloadDrawNowNs();const auto after=fvr::bc2::rigPublication::ReadBodyAmmoRenderSource(now);
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
        const fvr::bc2::BodyPropSources bodySourcesAfter{after,
            fvr::bc2::rigPublication::ReadHolsteredBodyRenderSource(now),fvr::bc2::rigPublication::ReadCarriedBodyRenderSource(now)};
        // Optional per-eye ammo HUD telemetry is assigned to props.ammo here.
        // The pair helper preserves it independently of prop admission.
        props.ammo=fvr::bc2::gameplay::ReadAmmoCounter(ReloadDrawNowNs());
        if(const auto pair=tracked.bodyPropPair.End(bodyPair,bodyPairKey,evidenceEye,bodySourcesBefore,bodySourcesAfter,props,ReloadDrawNowNs()))
            if(tracked.state.load(std::memory_order_acquire)==1&&fvr::bc2::CarriedPairKey(tracked.lease)==bodyPairKey&&
               U32(stereoExperiment.world+0x88)==stereoExperiment.frame&&U32(address+0x70)==stereoExperiment.request){
                frameBridge.SetBodyPropPair(tracked.lease,*pair);
            }
    }
    if(stereoMode){
        const auto& e=stereoExperiment;
        if((!streamMode||tracked.state.load(std::memory_order_acquire)==1)&&e.stage.load(std::memory_order_acquire)==StereoExperiment::Armed&&reinterpret_cast<unsigned>(self)==e.world&&reinterpret_cast<unsigned>(request)==e.request&&U32(e.world+0x88)==e.frame){
            if(address==e.main){if(streamMode&&tracked.state.load(std::memory_order_acquire)==1)tracked.capturesOkay&=CaptureTrackedEye(0);CaptureWorldColor(colorCaptures[1],e.frame);stereoExperiment.drawMask.fetch_or(1,std::memory_order_release);}
            if(address==e.eye){if(streamMode&&tracked.state.load(std::memory_order_acquire)==1)tracked.capturesOkay=tracked.capturesOkay&&CaptureTrackedEye(1);CaptureWorldColor(colorCaptures[2],e.frame);if(stereoExperiment.drawMask.fetch_or(2,std::memory_order_acq_rel)==1)stereoExperiment.framesRendered.fetch_add(1,std::memory_order_release);if(streamMode)FinishTrackedFrame(true);}
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
}
bool DisableProbeHooks()noexcept {
    bodyAmmoRenderer.RequestStop();
    fvr::bc2::opticFilterRuntime::Disable();
    fvr::bc2::rigWorkerRuntime::Disable();
    workerViewLease.store({},std::memory_order_release);
    const bool gameplayDisabled=fvr::bc2::gameplay::Stop();
    const bool menuDisabled=fvr::bc2::menu::Stop();
    deferredCleanup.store(true,std::memory_order_release);bool allDisabled=true;
    for(unsigned index=0;index<hookEntries.size();++index){if(!hookEntries[index])continue;
        // Keep the owning graphics boundary until its private resources drain.
        if(index==2&&!bodyAmmoRenderer.Quiescent()){allDisabled=false;continue;}
        const auto stage=stereoExperiment.stage.load(std::memory_order_acquire);
        if((((streamMode&&index<3)||index==4)&&(stage==StereoExperiment::Armed||stage==StereoExperiment::Retiring))||(index==5&&workPool.stage.load(std::memory_order_acquire)==1)){allDisabled=false;continue;}
        const auto result=MH_DisableHook(hookEntries[index]);if(result!=MH_OK&&result!=MH_ERROR_DISABLED)allDisabled=false;
    }
    bool drawCaptureDrained=false;
    fvr::bc2::ReloadProducerBinding().Enable(false);
    for(unsigned attempt=0;attempt<200;++attempt){if(reloadDrawCapture.Stop()){drawCaptureDrained=true;break;}Sleep(1);}
    bool workerDrained=false;
    for(unsigned attempt=0;attempt<200;++attempt){if(fvr::bc2::rigWorkerRuntime::Quiescent()){workerDrained=true;break;}Sleep(1);}
    bool opticDrained=false;
    for(unsigned attempt=0;attempt<200;++attempt){if(fvr::bc2::opticFilterRuntime::Quiescent()){opticDrained=true;break;}Sleep(1);}
    return allDisabled&&gameplayDisabled&&menuDisabled&&drawCaptureDrained&&workerDrained&&opticDrained&&bodyAmmoRenderer.Quiescent();
}
void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
std::size_t Offset(const fvr::engine::PeImage& pe,unsigned rva,std::size_t count){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&count<=s.rawSize-(rva-s.rva))return std::size_t(s.rawOffset)+rva-s.rva;throw std::runtime_error("Unbacked profile address");}
void WriteTarget(std::ostream& out,const Target& t){out<<"{\"width\":"<<t.width<<",\"height\":"<<t.height<<",\"format\":"<<t.format<<",\"samples\":"<<t.samples<<'}';}
}
extern "C" DWORD WINAPI FvrRunNativeProbe(void* raw){
    fvr::bc2::NativeProbeConfig config{};
    if(!Read(reinterpret_cast<std::uintptr_t>(raw),&config,sizeof(config)))return 10;
    const bool continuous=(config.flags&0x400u)!=0;const auto mode=config.flags&0xffu;passEvidenceEnabled=(config.flags&0x100u)!=0;uncapMirror=(config.flags&0x200u)!=0;
    if(config.magic!=0x32504246||config.bytes!=sizeof(config)||!fvr::bc2::ValidPumpHoldDiagnostic(config.pumpHoldDiagnostic,config.flags,config.durationMs)||!fvr::bc2::ValidBodyHolsterDiagnosticConfig(config.bodyHolsterDiagnostic,config.flags)||!fvr::bc2::ValidBoatHeadAimConfig(config.boatHeadAim,config.flags,config.magazineReloadSession)||!fvr::bc2::ValidMagazineReloadSession(config.magazineReloadSession,config.flags,config.durationMs)||!fvr::bc2::ValidBodyHolsterProbeConfig(config.flags,config.durationMs)||!fvr::bc2::ValidBodyInventoryConfig(config.flags)||!fvr::bc2::ValidWeaponVisibilityProbeConfig(config.flags,config.durationMs)||!fvr::bc2::ValidOpticFilterProbeConfig(config.flags,config.durationMs)||((config.flags&0xc00000u)==0xc00000u)||((config.flags&0xc00000u)&&((config.flags&0x268400u)||!(config.flags&0x10000u)||config.durationMs>15000))||!fvr::bc2::ValidReloadRequestProbeConfig(config.flags,config.durationMs)||!fvr::bc2::ValidPhysicalReloadConfig(config.flags)||!fvr::bc2::ValidPhysicalReloadProbeConfig(config.flags,config.durationMs)||!fvr::bc2::ValidPhysicalReloadRepeatProbeConfig(config.flags,config.durationMs)||((config.flags&0x200000u)&&!(config.flags&0x100000u))||((config.flags&0x100000u)&&!(config.flags&0x80000u))||((config.flags&0x80000u)&&!(config.flags&0x10000u))||((config.flags&0x40000u)&&((config.flags&0x400u)||!(config.flags&0x1000u)))||((config.flags&0x20000u)&&((config.flags&0x400u)||!(config.flags&0x1000u)))||mode>9||((config.flags&0x10000u)&&((config.flags&0x8000u)||(config.flags&0x6000u)!=0x6000u))||((config.flags&0x8000u)&&((config.flags&0x400u)||!(config.flags&0x4000u)))||((config.flags&0x1000u)&&!(config.flags&0x800u))||((config.flags&0x1e000u)&&!(config.flags&0x1000u))||
       ((passEvidenceEnabled||uncapMirror||continuous||(config.flags&0xfff800u))&&mode!=9)||
       (continuous?!config.hostPid:(config.durationMs<100||config.durationMs>(mode==9?60000u:5000u)))||config.reportPath[511]||config.frameChannel[63])return 10;
    if(InterlockedCompareExchange(&started,1,0))return 11;
    cameraPulseMode=mode<=2?mode:0;lifecycleMode=mode==4;streamMode=mode==9;streamToken=config.frameChannel;stereoMode=mode==5||mode==6||mode==8||streamMode;burstMode=mode==8||streamMode;initializationOnly=mode==6;poolOnly=mode==7;
    opticEvidenceEnabled=bool(config.flags&0x8000000u);
    bool hooksEnabled=false,disabled=true,workerObservation=false;std::ofstream report;fvr::platform::ProcessLifetime hostLifetime;
    try {
        const auto output=std::filesystem::path(config.reportPath);Require(output.is_absolute()&&!std::filesystem::exists(output),"Report must be a new absolute path");report.open(output);Require(bool(report),"Cannot create report");
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
        Require(same(render->worldRender,streamMode?0x199:40)&&same(render->prepareView,41)&&same(render->drawView,16),"Native code already modified");
        Require(same(visibility->worldUpdate,35)&&same(visibility->worldUpdate+0x383,5)&&same(visibility->worldUpdate+0x3e8,9)&&same(visibility->prepareVisibility,37)&&same(visibility->prepareVisibility+0x2e9a,9),"Visibility code changed");
        Require(same(caches->getView,21)&&same(caches->getProjection,21)&&same(caches->getFrustum,21)&&same(caches->updateView,26)&&same(caches->updateProjection,22)&&same(caches->updateFrustum,20),"Camera math code changed");
        Require(same(caches->updateView+0x1d5,10)&&same(caches->updateProjection+0x1c1,10)&&same(caches->updateFrustum+0x7c,6),"Camera math epilogues changed");
        if(lifecycleMode||stereoMode){
            for(auto span:{std::pair{lifecycle.requestVtable,20u},std::pair{layout.vtable,240u},std::pair{lifecycle.createView,43u},std::pair{lifecycle.constructor,0x111u},std::pair{lifecycle.addRef,14u},std::pair{lifecycle.release,34u},std::pair{lifecycle.setActive,13u},std::pair{lifecycle.deletingDestructor,30u},std::pair{lifecycle.destructor,0xf3u}})Require(same(span.first,span.second),"Native view lifecycle code changed");
        }
        if(lifecycleMode||stereoMode)Require(same(viewCallbacks.rememberMain,59)&&same(viewCallbacks.registerView,57)&&same(viewCallbacks.unregisterView,61),"View lifecycle callbacks modified");
        if(stereoMode)Require(same(initialization.rebuild,0xa26)&&same(initialization.parentGetter,7)&&same(initialization.refreshRegistered,0x164),"Native initialization code modified");

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
            const unsigned char renderingGate[]={0x83,0xbe,0xc4,0,0,0,1};
            const unsigned char renderingDone[]={0xc7,0x86,0xc4,0,0,0,3,0,0,0};
            const unsigned char updatingDone[]={0xc7,0x83,0xc4,0,0,0,1,0,0,0};
            Require(!std::memcmp(bytes.data()+Offset(pe.image,render->worldRender+0x1e,7),renderingGate,7)&&
                !std::memcmp(bytes.data()+Offset(pe.image,render->worldRender+0x175,10),renderingDone,10)&&
                !std::memcmp(bytes.data()+Offset(pe.image,visibility->worldUpdate+0x3da,10),updatingDone,10),"Native request phase contract changed");
            const std::array<std::pair<unsigned,unsigned>,19> spans{{{add,14},{release,31},{deleting,30},{requestDestructor,0x7a},{baseDestructor,0xc3},
                {lifecycle.release,34},{lifecycle.destructor,0xf3},{lifecycle.deletingDestructor,30},{lifecycle.setActive,13},
                {viewCallbacks.unregisterView,61},{initialization.refreshRegistered,0x164},{initialization.parentGetter,7},
                {render->worldRender+0x1e,7},{render->worldRender+0x175,10},{visibility->worldUpdate+0x3da,10},
                {cacheResize,cacheSizes[0]},{cacheShrink,cacheSizes[1]},{cacheMove,cacheSizes[2]},{cacheDestroy,cacheSizes[3]}}};
            for(unsigned i=0;i<spans.size();++i){const auto [rva,n]=spans[i];Require(same(rva,n),"Retirement code modified");
                stereoRecovery.code[i]={unsigned(imageBase+rva),n,Hash(imageBase+rva,n)};}
        }
        if(stereoMode){const auto vt=Offset(pe.image,layout.vtable+15*4,4);unsigned setter=0;std::memcpy(&setter,bytes.data()+vt,4);Require(setter>=profile.preferredBase&&same(setter-profile.preferredBase,48),"Viewport setter changed");}
        updateView=reinterpret_cast<CacheFn>(imageBase+caches->updateView);updateProjection=reinterpret_cast<CacheFn>(imageBase+caches->updateProjection);updateFrustum=reinterpret_cast<CacheFn>(imageBase+caches->updateFrustum);
        // Full x86 argument cleanup conventions, independently observed in the binary.
        for(auto pair:{std::pair{render->worldRender+0x196,4u},std::pair{render->prepareView+0xf02,8u},std::pair{render->drawView+0x1e2f,12u}}){
            const auto offset=Offset(pe.image,pair.first,3);Require(bytes[offset]==std::byte{0xc2}&&bytes[offset+1]==std::byte(pair.second)&&bytes[offset+2]==std::byte{0}&&same(pair.first,3),"Native callback ABI changed");}
        const auto game=U32(imageBase+profile.gameRendererGlobal),renderer=U32(imageBase+profile.rendererGlobal);Require(U32(renderer)==imageBase+profile.rendererVtable,"Renderer ownership mismatch");
        const auto subsystem=U32(std::uintptr_t(game)+0x108c);Require(U32(subsystem)==imageBase+render->subsystemVtable,"Subsystem ownership mismatch");const auto world=U32(std::uintptr_t(subsystem)+0x2c),request=U32(std::uintptr_t(subsystem)+0x30);
        Require(U32(world)==imageBase+render->worldRendererVtable&&U32(std::uintptr_t(request)+8)==world,"World request mismatch");
        if(stereoMode){for(unsigned index=0;index<16;++index){const auto begin=U32(world+0x9c),end=U32(world+0xa0);if(begin+index*4>=end)break;const auto cb=U32(begin+index*4),vt=U32(cb);if(U32(vt+28)==imageBase+viewCallbacks.registerView)Require(U32(vt+24)==imageBase+initialization.refreshRegistered,"Registered-view refresh mismatch");}
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
        if(config.boatHeadAim!=fvr::bc2::BoatHeadAimMode::Disabled)
            Require(fvr::bc2::gameplay::EnableBoatHeadAim(config.boatHeadAim==fvr::bc2::BoatHeadAimMode::AimAndFire),
                "Boat head aim native ownership binding unverified");
        if(config.flags&0x800u)Require(fvr::bc2::gameplay::SetFeedbackWriter(
            [](const fvr::interaction::FeedbackEvent& event)noexcept{return frameBridge.PublishFeedback(event);}),
            "Gameplay feedback writer configuration rejected");
        if(config.pumpHoldDiagnostic==fvr::bc2::PumpHoldDiagnostic::SelectedManualEmptyFire){Require(config.magazineReloadSession==3,"Empty-fire requires normal shared manual families");Require(fvr::bc2::gameplay::EnableEmptyFireProbe(),"Empty-fire diagnostic configuration rejected");}
        if(config.pumpHoldDiagnostic==fvr::bc2::PumpHoldDiagnostic::SpasOneShot)Require(fvr::bc2::reloadFlowRuntime::EnablePumpHoldDiagnostic(),"Pump hold diagnostic configuration rejected");
        if(config.flags&0x10000000u)Require(fvr::bc2::gameplay::EnableBodyInventory(),"Body inventory native selector/anchor binding unverified");
        bodyAmmoObservationEnabled=bool(config.flags&0x10000000u);bodyAmmoRenderer.EnableObservation(bodyAmmoObservationEnabled);
        if(bodyAmmoObservationEnabled){
            // Owned CPU geometry is loaded before native callbacks. The report
            // folder receives this PRIVATE installed-asset cache from the launcher.
            bodyAmmoCache=fvr::bc2::LoadBodyAmmoGeometryCache(output.parent_path()/L"body-ammo-assets.fvrprop");
            if(bodyAmmoCache.catalog&&!bodyAmmoRenderer.QueueGeometryCatalog(bodyAmmoCache.catalog))
                bodyAmmoCache.status=fvr::bc2::BodyAmmoCacheStatus::Geometry;
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
        // Configured-mesh reads remain a bounded PassEvidence/Hands diagnostic.
        // Continuous HMD sessions do not opt in through this path.
        if(config.flags&0x2000000u)
            Require(fvr::bc2::gameplay::EnableSelectedMeshesObservation(bytes,pe.image,imageBase,true),"Physical reload configured mesh binding unverified");
        else if(!continuous&&config.durationMs<=15000&&(config.flags&0x800u)&&(config.flags&0x14000u)&&
            (passEvidenceEnabled||(config.flags&0x10000u)))
            Require(fvr::bc2::gameplay::EnableSelectedMeshesObservation(bytes,pe.image,imageBase),"Configured mesh observation binding unverified");
        Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+render->worldRender),WorldHook,reinterpret_cast<void**>(&worldOriginal))==MH_OK,"Create world trace");
        Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+render->prepareView),PrepareHook,reinterpret_cast<void**>(&prepareOriginal))==MH_OK,"Create prepare trace");
        Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+render->drawView),DrawHook,reinterpret_cast<void**>(&drawOriginal))==MH_OK,"Create draw trace");
        Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+visibility->prepareVisibility),VisibilityHook,reinterpret_cast<void**>(&visibilityOriginal))==MH_OK,"Create visibility trace");
        if(lifecycleMode||stereoMode)Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+visibility->worldUpdate),UpdateHook,reinterpret_cast<void**>(&updateOriginal))==MH_OK,"Create pre-update lifecycle boundary");
        if(stereoMode||poolOnly)Require(MH_CreateHook(reinterpret_cast<void*>(imageBase+workPool.reset),ResetPoolHook,reinterpret_cast<void**>(&resetPoolOriginal))==MH_OK,"Create native pool cleanup boundary");
        hookEntries={reinterpret_cast<void*>(imageBase+render->worldRender),reinterpret_cast<void*>(imageBase+render->prepareView),reinterpret_cast<void*>(imageBase+render->drawView),reinterpret_cast<void*>(imageBase+visibility->prepareVisibility),(lifecycleMode||stereoMode)?reinterpret_cast<void*>(imageBase+visibility->worldUpdate):nullptr,(stereoMode||poolOnly)?reinterpret_cast<void*>(imageBase+workPool.reset):nullptr};
        if(streamMode){auto* device=reinterpret_cast<ID3D11Device*>(U32(renderer+0x7c));ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
            observedImmediateContext=context.Get();auto* entry=(*reinterpret_cast<void***>(context.Get()))[12];
            HMODULE owner=nullptr;Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(entry),&owner)&&owner==GetModuleHandleW(L"d3d11.dll"),"Unexpected D3D11 DrawIndexed owner");
            Require(MH_CreateHook(entry,IndexedDrawHook,reinterpret_cast<void**>(&indexedDrawOriginal))==MH_OK,"Create bounded geometry constant observer");hookEntries[6]=entry;
            if(passEvidenceEnabled||opticEvidenceEnabled){auto* nonIndexedEntry=(*reinterpret_cast<void***>(observedImmediateContext))[13];HMODULE nonIndexedOwner=nullptr;
            Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(nonIndexedEntry),&nonIndexedOwner)&&nonIndexedOwner==GetModuleHandleW(L"d3d11.dll"),"Unexpected D3D11 Draw owner");
            Require(MH_CreateHook(nonIndexedEntry,NonIndexedDrawHook,reinterpret_cast<void**>(&nonIndexedDrawOriginal))==MH_OK,"Create bounded pass observer");hookEntries[8]=nonIndexedEntry;}
        }
        if(streamMode){const auto contextCamera=fvr::bc2::DiscoverContextCamera(bytes,pe.image);
            Require(contextCamera&&same(*contextCamera,0xce),"Native context camera code changed");
            auto* entry=reinterpret_cast<void*>(imageBase+*contextCamera);
            Require(MH_CreateHook(entry,ContextCameraHook,reinterpret_cast<void**>(&contextCameraOriginal))==MH_OK,"Create native camera context observer");hookEntries[7]=entry;
            const auto overrides=fvr::bc2::DiscoverProjectionOverrides(bytes,pe.image);
            Require(overrides&&same(overrides->setter,0x6d)&&same(overrides->meshCaller-0x25,41)&&same(overrides->terrainCaller-0x27,43),"Projection override code/ABI mismatch");projectionOverrides=*overrides;
            auto* projectionEntry=reinterpret_cast<void*>(imageBase+overrides->setter);
            Require(MH_CreateHook(projectionEntry,ProjectionContextHook,reinterpret_cast<void**>(&projectionContextOriginal))==MH_OK,"Create projection override observer");hookEntries[9]=projectionEntry;
        }
        // Observe the final desktop buffer in either pacing mode; the hook
        // still bypasses sync only when uncapMirror explicitly authorizes it.
        if(streamMode){
            Require(same(profile.presentWrapper,49),"Native DXGI presentation call changed");
            observedRenderer=renderer;observedSwapChain=reinterpret_cast<IDXGISwapChain*>(U32(renderer+0x88));
            Require(observedSwapChain!=nullptr,"No desktop swap chain");
            ComPtr<ID3D11Device> ownerDevice;Require(SUCCEEDED(observedSwapChain->GetDevice(IID_PPV_ARGS(&ownerDevice)))&&
                reinterpret_cast<unsigned>(ownerDevice.Get())==U32(renderer+0x7c),"Desktop swap-chain device mismatch");
            auto* entry=(*reinterpret_cast<void***>(observedSwapChain))[8];HMODULE owner=nullptr;
            Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(entry),&owner)&&owner==GetModuleHandleW(L"dxgi.dll"),"Unexpected DXGI Present owner");
            Require(MH_CreateHook(entry,DesktopPresentHook,reinterpret_cast<void**>(&desktopPresentOriginal))==MH_OK,"Create bounded desktop pacing hook");
            hookEntries[10]=entry;
        }
        if(streamMode)fvr::bc2::menu::Install(bytes,pe.image,imageBase,streamToken);
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
        sampling.store(true,std::memory_order_release);fvr::bc2::menu::Start();if(config.flags&0x800u)fvr::bc2::gameplay::Start();
        if(continuous){
            std::ofstream telemetry(output.parent_path()/L"session-telemetry.jsonl");
            const auto sessionStarted=GetTickCount64();auto nextSample=sessionStarted;
            while(!hostLifetime.Ended()){
                const auto now=GetTickCount64();if(now>=nextSample){
                    FILETIME ft{};GetSystemTimeAsFileTime(&ft);const auto utc=(std::uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime;
                    const auto emptyGate=fvr::bc2::reloadFlowRuntime::ReadMagazineEmptyControlCounters();
                    telemetry<<"{\"elapsed_ms\":"<<(now-sessionStarted)<<",\"utc_filetime_100ns\":"<<utc<<",\"world_calls\":"<<worldCalls.load()
                        <<",\"published\":"<<frameBridge.Published()<<",\"consumed\":"<<frameBridge.Consumed()<<",\"discarded\":"<<frameBridge.Discarded()
                        <<",\"input_samples\":"<<inputSamples.load()<<",\"tracked_input_samples\":"<<inputTrackedSamples.load()<<",\"gpu_failure_stage\":"<<frameBridge.FailureStage()<<",\"camera_restore_failures\":"<<tracked.restoreFailures.load()
                        <<",\"magazine_empty_gate\":{\"requested\":["<<emptyGate.requested[0]<<','<<emptyGate.requested[1]<<','<<emptyGate.requested[2]
                        <<"],\"applied\":["<<emptyGate.applied[0]<<','<<emptyGate.applied[1]<<','<<emptyGate.applied[2]
                        <<"],\"restored\":["<<emptyGate.restored[0]<<','<<emptyGate.restored[1]<<','<<emptyGate.restored[2]
                        <<"],\"patch_failures\":"<<emptyGate.patchFailures<<",\"restore_failures\":"<<emptyGate.restoreFailures
                        <<",\"receipt_failures\":"<<emptyGate.receiptFailures<<"}}\n";telemetry.flush();nextSample=now+1000;
                }Sleep(100);
            }
        }else Sleep(config.durationMs);
        sampling.store(false,std::memory_order_release);
        if(stereoMode){for(unsigned attempt=0;attempt<200;++attempt){const auto state=stereoExperiment.stage.load(std::memory_order_acquire);if(state!=StereoExperiment::Armed&&state!=StereoExperiment::Retiring)break;Sleep(5);}}
        for(unsigned attempt=0;attempt<200&&workPool.stage.load(std::memory_order_acquire)==1;++attempt)Sleep(5);
        disabled=DisableProbeHooks();hooksEnabled=false;
        // Both capture-producing hook bodies must have returned. Sealing also
        // makes a delayed callback delegate directly to its native original.
        if(disabled)for(unsigned attempt=0;attempt<200;++attempt){
            if(diagnosticImageLifetime.TryQuiesce(true))break;Sleep(1);
        }
        Sleep(50);unsigned complete=0;for(const auto& r:records)if(r.complete.load(std::memory_order_acquire))++complete;
        report<<"{\"state\":\"observed\",\"pid\":"<<GetCurrentProcessId()<<",\"hooks_disabled\":"<<(disabled?"true":"false")<<",\"module_retained_until_exit\":true,\"native_render_replayed\":false,\"camera_written\":"<<(cameraPulse.complete.load(std::memory_order_acquire)&&cameraPulse.applied?"true":"false")<<",\"world_calls\":"<<worldCalls.load()<<",\"prepare_calls\":"<<prepareCalls.load()<<",\"draw_calls\":"<<drawCalls.load()<<",\"complete_records\":"<<complete<<",\"records\":[";
        bool first=true;for(const auto& r:records)if(r.complete.load(std::memory_order_acquire)){
            if(!first)report<<',';first=false;report<<"{\"thread\":"<<r.thread<<",\"frame\":"<<r.frame<<",\"world_frame\":"<<r.worldFrame<<",\"world\":"<<r.world<<",\"request\":"<<r.request<<",\"view\":"<<r.view<<",\"begin_qpc\":"<<r.beginQpc<<",\"end_qpc\":"<<r.endQpc<<",\"request_before\":"<<r.requestBefore<<",\"request_after\":"<<r.requestAfter<<",\"views\":"<<r.viewCount<<",\"prepare_calls\":"<<r.prepared<<",\"draw_calls\":"<<r.drawn<<",\"arena_before\":"<<r.arenaBefore<<",\"arena_after\":"<<r.arenaAfter<<",\"view_items_before\":"<<r.viewItemsBefore<<",\"view_items_after\":"<<r.viewItemsAfter<<",\"camera_hash_before\":"<<r.cameraBefore<<",\"camera_hash_after\":"<<r.cameraAfter<<",\"prepared_hash_before\":"<<r.dataBefore<<",\"prepared_hash_after\":"<<r.dataAfter<<",\"target_before\":";WriteTarget(report,r.targetBefore);report<<",\"target_after\":";WriteTarget(report,r.targetAfter);report<<",\"children\":[";for(unsigned i=0;i<r.childrenCount;++i){if(i)report<<',';const auto& child=r.children[i];report<<"{\"entry\":"<<child.entry<<",\"view\":"<<child.view<<",\"vtable\":"<<child.vtable<<",\"owner\":"<<child.owner<<",\"active\":"<<child.active<<",\"shadow\":"<<child.shadow<<",\"primary_hash\":"<<child.primary<<",\"secondary_hash\":"<<child.secondary<<'}';}report<<"]}";}
        const bool cameraComplete=cameraExperiment.complete.load(std::memory_order_acquire);
        report<<"],\"weapon_projection_copies\":"<<weaponProjectionCopies.load()<<",\"weapon_context_failures\":"<<weaponContextFailures.load()<<",\"view_anchor_copies\":"<<anchorCopies.load()<<",\"camera_copy_test\":";
        if(cameraComplete){report<<"{\"complete\":true"<<",\"live_camera_unchanged\":"<<(cameraExperiment.liveUnchanged?"true":"false")<<",\"baseline_view_error\":"<<cameraExperiment.baselineViewError<<",\"baseline_projection_error\":"<<cameraExperiment.baselineProjectionError<<",\"variants\":[";
        unsigned variantIndex=0;for(const auto& variant:cameraExperiment.variants){if(variantIndex++)report<<',';report<<"{\"projection\":[";bool firstElement=true;for(const auto& row:variant.projection.values)for(float value:row){if(!firstElement)report<<',';firstElement=false;if(std::isfinite(value))report<<value;else report<<"null";}report<<"],\"frustum_hash\":"<<variant.frustumHash<<'}';}
        report<<"],\"mapped_cameras\":[";unsigned mappedIndex=0;for(const auto& result:cameraExperiment.mapped){if(mappedIndex++)report<<',';report<<"{\"passed\":"<<(result.complete?"true":"false")<<",\"view_error\":"<<result.viewError<<",\"projection_error\":"<<result.projectionError<<",\"view_projection_relative_error\":"<<result.viewProjectionRelativeError<<'}';}report<<"],\"culling_cameras\":[";unsigned cullIndex=0;for(const auto& result:cameraExperiment.culling){if(cullIndex++)report<<',';report<<"{\"passed\":"<<(result.passed?"true":"false")<<",\"max_distance\":"<<result.maxDistance<<",\"normal_error\":"<<result.normalError<<'}';}report<<"]}";
        }else report<<"{\"complete\":false}";
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
                <<",\"request_refs_before_release\":"<<stereoShutdown.requestReferencesBefore<<",\"after_captured\":"<<stereoShutdown.afterCaptured
                <<",\"historical_original_order\":";list(stereoShutdown.historical);
            report<<",\"before_release_order\":";list(stereoShutdown.before);report<<",\"expected_survivor_order\":";list(stereoShutdown.expected);
            report<<",\"after_release_order\":";list(stereoShutdown.after);report<<'}';
        }
        report<<",\"stereo_recovery\":{\"attempts\":"<<stereoRecovery.attempts.load()<<",\"completed\":"<<stereoRecovery.completed.load()
            <<",\"rejected\":"<<stereoRecovery.rejected.load()<<",\"failure\":"<<stereoRecovery.failure.load()<<",\"retirement_step\":"<<stereoRecovery.retirementStep.load()
            <<",\"old_world\":"<<stereoRecovery.oldWorld.load()<<",\"old_request\":"<<stereoRecovery.oldRequest.load()<<",\"old_eye\":"<<stereoRecovery.oldEye.load()
            <<",\"new_world\":"<<stereoRecovery.newWorld.load()<<",\"new_request\":"<<stereoRecovery.newRequest.load()<<",\"retired_views\":"<<stereoRecovery.retiredViews.load()
            <<",\"cache_clear_calls\":"<<stereoRecovery.cacheClearCalls.load()<<",\"cache_clear_receipts\":"<<stereoRecovery.cacheClearReceipts.load()
            <<",\"completed_on_stop\":"<<(stereoRecovery.completedOnStop.load()?"true":"false")<<'}';
        report<<",\"stereo_progress\":";
        fvr::bc2::StereoProgressEvidence::WriteJson(report,stereoProgressEvidence.Read());
        const auto poolStage=workPool.stage.load(std::memory_order_acquire);
        report<<",\"work_pool\":{\"stage\":"<<poolStage;
        if(poolStage==2)report<<",\"frame\":"<<workPool.frame<<",\"peak_entries\":"<<workPool.peak<<",\"native_capacity\":32,\"expanded_capacity\":128";
        report<<'}';
        if(tracked.restoreFailures.load(std::memory_order_acquire))for(unsigned i=0;i<2;++i){
            for(auto item:{std::pair{"saved",&tracked.saved[i]},std::pair{"written",&tracked.written[i]},std::pair{"observed",&tracked.observed[i]}}){std::ofstream bytesOut(output.parent_path()/("tracked-"+std::string(item.first)+"-"+std::to_string(i)+".bin"),std::ios::binary);bytesOut.write(reinterpret_cast<const char*>(item.second->bytes.data()),0x460);}
        }
        report<<",\"desktop_pacing\":{\"requested\":"<<(uncapMirror?"true":"false")<<",\"calls\":"<<desktopPresentCalls.load()<<",\"native_sync_one\":"<<desktopSyncOne.load()<<",\"bypassed_waits\":"<<desktopSyncBypassed.load()<<"}";
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
        report<<",\"gameplay\":";fvr::bc2::gameplay::Report(report);
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
        report<<",\"body_ammo_render\":";bodyAmmoRenderer.Report(report);
        report<<",\"reload_draw_evidence\":";reloadDrawCapture.Report(report);
        report<<",\"reload_palette_producer\":";fvr::bc2::ReloadProducerBinding().Report(report);
        report<<",\"optic_filter_observation\":";fvr::bc2::opticFilterRuntime::Report(report);
        report<<",\"reload_worker_binding\":";fvr::bc2::rigWorkerRuntime::Report(report);
        report<<",\"native_stream\":{\"published\":"<<frameBridge.Published()<<",\"consumed\":"<<frameBridge.Consumed()<<",\"discarded\":"<<frameBridge.Discarded()<<",\"camera_restore_failures\":"<<tracked.restoreFailures.load()<<",\"captured_eyes\":"<<frameBridge.CapturedEyes()<<",\"sharing_mode\":"<<frameBridge.SharingMode()<<",\"gpu_failure_stage\":"<<frameBridge.FailureStage()<<",\"gpu_failure_code\":"<<frameBridge.FailureCode()<<",\"gpu_failure_operation\":"<<frameBridge.FailureOperation()<<",\"device_flags\":"<<frameBridge.DeviceFlags()<<",\"feature_level\":"<<frameBridge.FeatureLevel()<<",\"sharing_compatibility\":["<<frameBridge.Compatibility(0)<<","<<frameBridge.Compatibility(1)<<","<<frameBridge.Compatibility(2)<<","<<frameBridge.Compatibility(3)<<","<<frameBridge.Compatibility(4)<<","<<frameBridge.Compatibility(5)<<","<<frameBridge.Compatibility(6)<<","<<frameBridge.Compatibility(7)<<","<<frameBridge.Compatibility(8)<<"]"<<"}";
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
            for(auto& capture:desktopCaptures)releaseImage(capture.color);
        }
        std::ofstream imageRetirement(output.parent_path()/"diagnostic-image-retirement.json");
        imageRetirement<<"{\"hooks_disabled\":"<<(disabled?"true":"false")
            <<",\"graphics_quiescent\":"<<(diagnosticImageLifetime.Quiescent()?"true":"false")
            <<",\"report_saved\":"<<(reportSaved?"true":"false")<<",\"released_images\":"<<releasedImages
            <<",\"released_pixel_bytes\":"<<releasedPixelBytes<<",\"released_capacity_bytes\":"<<releasedPixelCapacity<<"}\n";
        const bool lifecyclePassed=!lifecycleMode||(lifecycleComplete&&lifecycleResult.created&&lifecycleResult.registered&&lifecycleResult.inactive&&lifecycleResult.restored&&lifecycleResult.refsBefore==0&&lifecycleResult.refsOwned==1&&lifecycleResult.refsReleased==0&&lifecycleResult.borrowedRedirects>0&&lifecycleResult.borrowedRedirects==lifecycleResult.borrowedRestores&&lifecycleResult.allRestored&&lifecycleResult.callbacksRestored&&lifecycleResult.requestRefsBefore==lifecycleResult.requestRefsAfter);
        bool stereoPassed=!stereoMode;
        if(stereoMode&&stereoState==StereoExperiment::Released){stereoPassed=(!streamMode||(!tracked.restoreFailures.load()&&!contextPatchFailures.load()&&!contextRestoreFailures.load()&&!derivedFailures.load()&&frameBridge.Published()>=4&&frameBridge.Consumed()>=3))&&((e.listsRestored&&e.callbacksRestored&&e.refsBefore==e.refsAfter&&e.initialized&&e.cachedViewsRestored)||
            (stereoRecovery.completedOnStop.load()&&stereoRecovery.completed.load()&&!stereoRecovery.failure.load()&&
             stereoRecovery.retirementStep.load()==unsigned(fvr::runtime::RetirementStep::Complete)))&&(!burstMode||streamMode||e.framesRendered.load()==120)&&(initializationOnly||streamMode||(e.visibilityMask.load()==3&&e.drawMask.load()==3));
            for(unsigned i=1;!initializationOnly&&i<3;++i)stereoPassed&=colorCaptures[i].complete.load(std::memory_order_acquire)&&colorCaptures[i].step==ColorCapture::Step::Finished&&colorCaptures[i].frame==e.initialColorFrame;
        }
        if(disabled&&stereoProgress!=INVALID_HANDLE_VALUE){CloseHandle(stereoProgress);stereoProgress=INVALID_HANDLE_VALUE;}
        return report&&disabled&&complete&&cameraPassed&&lifecyclePassed&&stereoPassed&&(!(poolOnly||(stereoMode&&!initializationOnly))||poolStage==2)&&(!cameraPulseMode||(pulseComplete&&cameraPulse.applied&&cameraPulse.restored&&(cameraPulseMode==2||cameraPulse.drawLeftCopiesUnchanged)))?0:12;
    }catch(const std::exception& e){sampling.store(false,std::memory_order_release);if(hooksEnabled)disabled=DisableProbeHooks();else {fvr::bc2::opticFilterRuntime::Disable();fvr::bc2::rigWorkerRuntime::Disable();disabled=fvr::bc2::rigWorkerRuntime::Quiescent()&&fvr::bc2::opticFilterRuntime::Quiescent();fvr::bc2::gameplay::Stop();fvr::bc2::menu::Stop();}
        if(report)report<<"{\"state\":\"failed\",\"error\":\""<<e.what()<<"\",\"hooks_disabled\":"<<(disabled?"true":"false")<<"}\n";return 20;}
}

