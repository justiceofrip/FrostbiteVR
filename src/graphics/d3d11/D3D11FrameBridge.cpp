#include "fvr/graphics/D3D11FrameBridge.h"
#include "fvr/diagnostics/PrivateCpuAttribution.h"
#include "fvr/diagnostics/PrivateGpuAttribution.h"
#include <cstring>
#include <d3d11_1.h>
#include <dxgi1_4.h>
namespace fvr::graphics {
namespace {
std::int64_t TimingQpc()noexcept {LARGE_INTEGER now{};return QueryPerformanceCounter(&now)?now.QuadPart:0;}
FrameBridgeTimingKey TimingKey(const ipc::FrameLease& lease)noexcept {
    return {lease.channelGeneration,lease.requestId,lease.native.owner,lease.native.frameId,lease.native.deviceEpoch,
        lease.tracking.generation,lease.tracking.spaceGeneration,lease.tracking.predictedNs};
}
fvr::diagnostics::privateGpu::Key GpuTimingKey(const ipc::FrameLease& lease)noexcept {
    return {lease.native.owner,lease.native.frameId,lease.requestId,lease.channelGeneration,lease.native.deviceEpoch,
        lease.tracking.generation,lease.tracking.spaceGeneration,lease.tracking.predictedNs,0};
}
bool SamePose(const math::Pose& a,const math::Pose& b)noexcept {
    return a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&
        a.orientation.x==b.orientation.x&&a.orientation.y==b.orientation.y&&
        a.orientation.z==b.orientation.z&&a.orientation.w==b.orientation.w;
}
}
bool D3D11FrameBridge::PreparedGraphicsMatches(const ipc::FrameLease& lease)const noexcept {
    if((!earlyFrameAdmission_&&!pipelineEnabled_)||!preparedGraphicsValid_||deferredCancellationPending_||GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire)||
       preparedDevice_!=device_.Get()||preparedContext_!=context_.Get())return false;
    const auto& old=preparedGraphics_;const auto& a=old.requirements;const auto& b=lease.requirements;
    const auto& x=old.tracking;const auto& y=lease.tracking;
    if(old.channelGeneration!=lease.channelGeneration||old.requestId!=lease.requestId||old.native!=lease.native||
       old.sourceSlot!=lease.sourceSlot||old.sourceSlotCount!=lease.sourceSlotCount||
       a.width!=b.width||a.height!=b.height||a.format!=b.format||a.adapterLow!=b.adapterLow||a.adapterHigh!=b.adapterHigh||
       x.generation!=y.generation||x.spaceGeneration!=y.spaceGeneration||x.predictedNs!=y.predictedNs||
       x.focused!=y.focused||x.headValid!=y.headValid||x.worldUnitsPerMeter!=y.worldUnitsPerMeter||
       !SamePose(x.referenceHead,y.referenceHead)||!SamePose(x.head,y.head))return false;
    for(unsigned eye=0;eye<2;++eye){const auto& p=x.fov[eye];const auto& q=y.fov[eye];
        if(!SamePose(x.eyes[eye],y.eyes[eye])||p.left!=q.left||p.right!=q.right||p.up!=q.up||p.down!=q.down)return false;}
    return true;
}
void D3D11FrameBridge::ObservePublication(void* context,const ipc::FrameLease& lease,const PairTicket& ticket,ipc::StagedFrameObservation event)noexcept {
    auto& bridge=*static_cast<D3D11FrameBridge*>(context);
    if(bridge.pipelineEnabled_){
        if(lease.sourceSlotCount!=bridge.pipelineSourceCount_||lease.sourceSlot>=bridge.pipelineSourceCount_)return;
        auto& journal=bridge.pipelineTiming_[lease.sourceSlot];
        if(event==ipc::StagedFrameObservation::Acknowledged)journal.MarkRetiring(TimingKey(lease),ticket,TimingQpc());
        else if(event==ipc::StagedFrameObservation::Published){
            const auto qpc=TimingQpc();journal.Mark(TimingKey(lease),FrameBridgeTimingStage::Published,qpc);
            const auto& source=bridge.pipeline_[lease.sourceSlot];
            if(PipelineMatches(source.lease,lease)&&!std::memcmp(&source.ticket,&ticket,sizeof(ticket)))
                bridge.pipelineEvidence_.slots[lease.sourceSlot].lastPublication=
                    {true,TimingKey(lease),ticket,source.color.Descriptor(),source.depthFrame,qpc};
        }
        else journal.Reaped(TimingKey(lease),ticket,TimingQpc());return;
    }
    if(event==ipc::StagedFrameObservation::Acknowledged){
        // The sole visibility-thread observation: value-only diagnostics, no
        // GPU calls, depth reset or mutation of graphics-owned bridge state.
        ++bridge.earlyAcknowledgements_;
        bridge.timing_.MarkRetiring(TimingKey(lease),ticket,TimingQpc());
    }
    else if(event==ipc::StagedFrameObservation::Published)bridge.timing_.Mark(TimingKey(lease),FrameBridgeTimingStage::Published,TimingQpc());
    else bridge.timing_.Reaped(TimingKey(lease),ticket,TimingQpc());
}
bool D3D11FrameBridge::ConnectGraphics(ID3D11Device* device,const std::wstring& token,FrameSharingPreference preference)noexcept {
    if(!device||connected_.load(std::memory_order_acquire))return false;
    wchar_t pipeline[4]{},third[4]{};const auto pipelineError=GetLastError();
    pipelineEnabled_=GetEnvironmentVariableW(L"FVR_PRIVATE_TWO_SOURCE_SLOTS",pipeline,4)==1&&pipeline[0]==L'1';
    const bool thirdSource=GetEnvironmentVariableW(L"FVR_PRIVATE_THREE_SOURCE_SLOTS",third,4)==1&&third[0]==L'1';
    SetLastError(pipelineError);
    if(thirdSource&&!pipelineEnabled_)return false;
    pipelineSourceCount_=pipelineEnabled_?(thirdSource?3u:2u):1u;
    if(pipelineEnabled_){
        if(preference==FrameSharingPreference::LegacyRelay)return false;
        preference=FrameSharingPreference::LegacyIpc; // Explicit opt-in selects only the verified private transport.
    }
    forceLegacy_=preference==FrameSharingPreference::LegacyRelay;forceLegacyIpc_=preference==FrameSharingPreference::LegacyIpc;device_=device;deviceFlags_=device->GetCreationFlags();featureLevel_=device->GetFeatureLevel();device_->GetImmediateContext(&context_);
    if(!channel_.Connect(token,pipelineEnabled_,thirdSource)||channel_.SourceSlotCount()!=pipelineSourceCount_){channel_.CloseGraphics();device_.Reset();context_.Reset();return false;}
    // Preserve the negotiated mode in final evidence after channel teardown.
    pipelineEvidence_.sourceSlots=pipelineEnabled_?pipelineSourceCount_:2u;
    pipelineEvidence_.exportOverlap=channel_.ExportOverlapEnabled();
    wchar_t early[4]{};const auto lastError=GetLastError();
    earlyFrameAdmission_=GetEnvironmentVariableW(L"FVR_PRIVATE_EARLY_FRAME_ADMISSION",early,4)==1&&early[0]==L'1';
    SetLastError(lastError);
    if(pipelineEnabled_)earlyFrameAdmission_=false;
    if(!pipelineEnabled_&&!channel_.SetDeferredGraphicsRetirement(earlyFrameAdmission_)){channel_.CloseGraphics();device_.Reset();context_.Reset();return false;}
    renderQueue_.Configure(device_.Get());
    // Opt-in allocation only; the verified native tracked draw starts timing.
    fvr::diagnostics::privateGpu::recorder.Configure(device_.Get(),context_.Get());
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);timing_.SetFrequency(std::uint64_t(frequency.QuadPart));
    if(pipelineEnabled_)for(unsigned i=0;i<pipelineSourceCount_;++i)pipelineTiming_[i].SetFrequency(std::uint64_t(frequency.QuadPart));
    graphicsThread_.store(GetCurrentThreadId(),std::memory_order_release);
    connected_.store(true,std::memory_order_release);return true;
}
bool D3D11FrameBridge::TryBegin(const ipc::NativeFrameKey& native,ipc::FrameLease& lease)noexcept {
    const auto sharing=sharingMode_.load(std::memory_order_acquire);
    const bool begun=connected_.load(std::memory_order_acquire)&&!Fatal()&&(sharing!=2||relay_.SourceWritable())&&channel_.TryBegin(native,lease,sharing==3);
    if(begun){
        if(pipelineEnabled_){
            if(lease.sourceSlotCount!=pipelineSourceCount_||lease.sourceSlot>=pipelineSourceCount_)return false;
            pipelineTiming_[lease.sourceSlot].Begin(TimingKey(lease),lease.requirements.width,lease.requirements.height,TimingQpc());
        }else timing_.Begin(TimingKey(lease),lease.requirements.width,lease.requirements.height,TimingQpc());
    }
    return begun;
}
bool D3D11FrameBridge::PrepareGraphics(const ipc::FrameLease& lease,RestoredPublicationResult* observation)noexcept {
    if(pipelineEnabled_)return PreparePipeline(lease,observation);
    if(!earlyFrameAdmission_)return true;
    if(GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire)){
        if(observation)*observation=RestoredPublicationResult::WrongGraphicsOwner;return false;
    }
    if(deferredCancellationPending_){if(observation)*observation=RestoredPublicationResult::PrepareInvalid;return false;}
    if(PreparedGraphicsMatches(lease)){++preparedGraphicsReuses_;return true;}
    const auto result=channel_.PrepareGraphicsResult(lease);
    if(result==ipc::ChannelResult::Busy)++earlyGraphicsDeferrals_;
    if(observation&&result!=ipc::ChannelResult::Ok)*observation=result==ipc::ChannelResult::Busy?
        RestoredPublicationResult::PrepareBusy:RestoredPublicationResult::PrepareInvalid;
    if(result!=ipc::ChannelResult::Ok)return false;
    preparedGraphics_=lease;preparedDevice_=device_.Get();preparedContext_=context_.Get();preparedGraphicsValid_=true;
    return true;
}
bool D3D11FrameBridge::BeginDepthCapture(const ipc::FrameLease& lease,bool reversed,RestoredPublicationResult* observation)noexcept {
    if(observation)*observation=RestoredPublicationResult::NotAttempted;
    if(pipelineEnabled_)return BeginPipelineDepth(lease,reversed,observation);
    // This check precedes even metadata reset: an old GPU receipt may still be
    // awaiting its graphics-thread drain after a new camera lease was admitted.
    if(!PrepareGraphics(lease,observation))return false;
    depthActive_=false;depthLease_={};
    if(!connected_.load(std::memory_order_acquire)||sharingMode_.load(std::memory_order_acquire)!=3||
       submitting_||canceling_||waitingGameCopies_||waitingLegacyExport_||!lease.channelGeneration||!lease.requestId)return false;
    try {
        if(!depthInitialized_){if(!depthProducer_.Initialize(device_.Get()))return false;depthInitialized_=true;}
        const auto& t=lease.tracking;
        if(!depthProducer_.Begin({lease.native.frameId,t.spaceGeneration,t.generation,t.predictedNs}))return false;
        depthLease_=lease;depthReversed_=reversed;depthActive_=true;return true;
    }catch(...){return false;}
}
bool D3D11FrameBridge::CaptureDepth(unsigned eye,unsigned stage,ID3D11Texture2D* source,const ipc::FrameLease& lease)noexcept {
    if(pipelineEnabled_)return CapturePipelineDepth(eye,stage,source,lease);
    if(!PrepareGraphics(lease))return false;
    if(!depthActive_||!source||lease.channelGeneration!=depthLease_.channelGeneration||lease.requestId!=depthLease_.requestId||
       lease.native!=depthLease_.native||lease.tracking.generation!=depthLease_.tracking.generation||
       lease.tracking.spaceGeneration!=depthLease_.tracking.spaceGeneration||lease.tracking.predictedNs!=depthLease_.tracking.predictedNs)return false;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(d.Width!=lease.requirements.width||d.Height!=lease.requirements.height){
        depthProducer_.Cancel();depthActive_=false;return false;}
    const auto gpuCopy=fvr::diagnostics::privateGpu::recorder.Begin(
        fvr::diagnostics::privateGpu::Kind::DepthCopy,GpuTimingKey(lease),eye,stage,context_.Get());
    const bool captured=depthProducer_.Capture(eye,stage,source);
    fvr::diagnostics::privateGpu::recorder.End(gpuCopy,captured);
    if(!captured){
        depthProducer_.Cancel();depthActive_=false;return false;}
    timing_.Depth(TimingKey(lease),eye,stage,d.SampleDesc.Count,d.SampleDesc.Quality,unsigned(d.Format),TimingQpc());
    return true;
}
void D3D11FrameBridge::DiagnoseSharing()noexcept {
    Microsoft::WRL::ComPtr<ID3D11Device1> newer;compatibility_[0]=device_.As(&newer);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};compatibility_[1]=device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options));compatibility_[2]=options.ExtendedResourceSharing;
    for(unsigned i=0;i<6;++i){D3D11_TEXTURE2D_DESC d{};d.Width=i==3?1920:32;d.Height=i==3?1080:16;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=(i==1||i==3)?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        d.MiscFlags=i==4?D3D11_RESOURCE_MISC_SHARED:i==5?0:D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX|(i==2?0:D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;compatibility_[3+i]=device_->CreateTexture2D(&d,nullptr,&texture);
    }
}
bool D3D11FrameBridge::CaptureEye(unsigned eye,ID3D11Texture2D* source,const ipc::FrameLease& lease,SourceDimensionPolicy dimensions)noexcept {
    if(pipelineEnabled_)return CapturePipelineEye(eye,source,lease);
    if(!PrepareGraphics(lease))return false;
    if(!connected_.load(std::memory_order_acquire)||!source||eye>1||submitting_||canceling_||waitingGameCopies_||waitingLegacyExport_)return Fail(1,E_UNEXPECTED);
    timing_.Mark(TimingKey(lease),eye?FrameBridgeTimingStage::CaptureRightBegin:FrameBridgeTimingStage::CaptureLeftBegin,TimingQpc());
    const auto& req=lease.requirements;D3D11_TEXTURE2D_DESC description{};source->GetDesc(&description);
    Microsoft::WRL::ComPtr<ID3D11Device> sourceDevice;source->GetDevice(&sourceDevice);
    if(sourceDevice.Get()!=device_.Get()||description.MipLevels!=1||description.ArraySize!=1||description.SampleDesc.Count!=1||!CopyCompatibleFormats(description.Format,req.format))return Fail(2,E_INVALIDARG);
    if(description.Width!=req.width||description.Height!=req.height){
        // No copy, allocation, producer reset or old-ticket acknowledgement is
        // permitted on this path. The owner restores then cancels this pair.
        if(dimensions==SourceDimensionPolicy::DiscardPair)return false;
        return Fail(2,E_INVALIDARG);
    }
    if(eye==0){
        desktopMirror_.ObserveOwner(lease.native.owner,lease.native.deviceEpoch,lease.tracking.spaceGeneration);
        capturedRequest_=lease.requestId;mask_=0;
        if(!sharingMode_.load()||std::memcmp(&allocated_,&req,sizeof(req))||
           allocatedNative_.owner!=lease.native.owner||allocatedNative_.deviceEpoch!=lease.native.deviceEpoch){
            producer_.Reset();for(auto& texture:eyes_)texture.Reset();relay_.Reset();sharingMode_.store(0);
            if(!forceLegacy_&&!forceLegacyIpc_){
                const auto result=producer_.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_);
                if(result!=TransferResult::Ok){const auto operation=producer_.LastOperation();const auto error=producer_.LastError();
                    if(error!=E_INVALIDARG||operation!=5)return Fail(4,error,operation);
                    forceLegacyIpc_=true;
                }
            }
            if(forceLegacyIpc_){
                if(producer_.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_,TextureSharing::LegacyFenced)!=TransferResult::Ok)return Fail(4,producer_.LastError(),producer_.LastOperation());
                description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=0;description.CPUAccessFlags=0;description.MiscFlags=0;
                for(auto& texture:eyes_){const auto hr=device_->CreateTexture2D(&description,nullptr,&texture);if(FAILED(hr))return Fail(3,hr);}
                sharingMode_.store(3,std::memory_order_release);
            }else if(forceLegacy_){
                if(!relay_.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format)))return Fail(10,relay_.LastError(),100+relay_.LastOperation());
                if(producer_.Create(relay_.Device(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_)!=TransferResult::Ok)return Fail(10,producer_.LastError(),200+producer_.LastOperation());
                sharingMode_.store(2,std::memory_order_release);
            }else{
                description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=0;description.CPUAccessFlags=0;description.MiscFlags=0;
                for(auto& texture:eyes_){const auto hr=device_->CreateTexture2D(&description,nullptr,&texture);if(FAILED(hr))return Fail(3,hr);}
                sharingMode_.store(1,std::memory_order_release);
            }
            allocated_=req;allocatedNative_=lease.native;
        }
        if(!Valid(producer_.Descriptor())){
            auto* exporting=sharingMode_.load()==2?relay_.Device():device_.Get();
            if(producer_.Create(exporting,req.width,req.height,DXGI_FORMAT(req.format),++epoch_,sharingMode_.load()==3?TextureSharing::LegacyFenced:TextureSharing::NamedKeyed)!=TransferResult::Ok)return Fail(4,producer_.LastError(),producer_.LastOperation());
        }
        const auto descriptor=producer_.Descriptor();if(descriptor.adapterLow!=req.adapterLow||descriptor.adapterHigh!=req.adapterHigh)return Fail(5,E_INVALIDARG);
    }else if(capturedRequest_!=lease.requestId||mask_!=1)return Fail(6,E_UNEXPECTED);
    if(sharingMode_.load()==2){if(!relay_.SourceWritable())return Fail(12,E_UNEXPECTED);relay_.Capture(eye,source);}
    else if(sharingMode_.load()==3){
        const auto gpuCopy=fvr::diagnostics::privateGpu::recorder.Begin(
            fvr::diagnostics::privateGpu::Kind::EyeCapture,GpuTimingKey(lease),eye,0,context_.Get());
        context_->CopyResource(eyes_[eye].Get(),source);
        fvr::diagnostics::privateGpu::recorder.End(gpuCopy);
    }
    else context_->CopyResource(eyes_[eye].Get(),source);
    timing_.Mark(TimingKey(lease),eye?FrameBridgeTimingStage::CaptureRightQueued:FrameBridgeTimingStage::CaptureLeftQueued,TimingQpc());
    mask_|=1u<<eye;++capturedEyes_;
    return true;
}
bool D3D11FrameBridge::PublishRestored(const ipc::FrameLease& lease,RestoredPublicationResult* observation)noexcept {
    if(observation)*observation=RestoredPublicationResult::NotAttempted;
    if(pipelineEnabled_)return PublishPipelineRestored(lease,observation);
    const bool prepared=PreparedGraphicsMatches(lease);
    if(!prepared&&!PrepareGraphics(lease,observation))return false;
    if(mask_!=3||capturedRequest_!=lease.requestId||submitting_||canceling_||waitingGameCopies_||waitingLegacyExport_){
        if(observation)*observation=RestoredPublicationResult::CaptureStateRejected;return Fail(7,E_UNEXPECTED);
    }
    preparedGraphicsValid_=false;
    if(prepared)++preparedPublications_;
    timing_.Mark(TimingKey(lease),FrameBridgeTimingStage::Restored,TimingQpc());
    pending_=lease;mask_=0;pendingDepth_={};restoredPairPending_=true;
    // Queue the desktop-only copy after the VR export/fence submission, so it
    // is not a prerequisite for VR readiness. All copies stay on their original
    // immediate context, before any later eye reuse. Preserve the restored left
    // image even when export fails; mirror failure never cancels publication.
    if(sharingMode_.load()==3){
        waitingLegacyExport_=true;PumpGraphics();
        desktopMirror_.CommitLeft(eyes_[0].Get());
        if(observation)*observation=RestoredPublicationResult::Accepted;return true;
    }
    if(sharingMode_.load()==2){
        timing_.Export(TimingKey(lease),2,TimingQpc());relay_.SealGameCopies();
        desktopMirror_.CommitLeft(relay_.GameEye(0));
        waitingGameCopies_=true;PumpGraphics();
        if(observation)*observation=RestoredPublicationResult::Accepted;return true;
    }
    const std::array<TextureSlice,2> sources={TextureSlice{eyes_[0].Get(),0},TextureSlice{eyes_[1].Get(),0}};
    const auto& tracking=lease.tracking;
    timing_.Export(TimingKey(lease),sharingMode_.load(),TimingQpc());
    const auto result=producer_.Publish(sources,{lease.native.frameId,tracking.spaceGeneration,tracking.generation,tracking.predictedNs},ticket_);
    desktopMirror_.CommitLeft(eyes_[0].Get());
    if(result!=TransferResult::Ok){
        if(observation)*observation=RestoredPublicationResult::ExportRejected;
        return Fail(8,producer_.LastError(),producer_.LastOperation());
    }
    timing_.Ticket(TimingKey(lease),ticket_);
    submitting_=true;PumpGraphics();
    if(observation)*observation=RestoredPublicationResult::Accepted;return true;
}
void D3D11FrameBridge::Cancel(const ipc::FrameLease& lease)noexcept {
    if(pipelineEnabled_){CancelPipeline(lease);PumpPipeline();return;}
    if(earlyFrameAdmission_){
        if(GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire))return;
        preparedGraphicsValid_=false;
        const auto result=channel_.PrepareGraphicsResult(lease);
        if(result==ipc::ChannelResult::Busy){
            // One admitted Rendering lease can exist. Keep its cancellation
            // separate from pending_, which still describes the old GPU pair.
            deferredCancellation_=lease;deferredCancellationPending_=true;
            ++earlyGraphicsDeferrals_;return;
        }
        if(result!=ipc::ChannelResult::Ok)return;
    }
    CancelPrepared(lease);PumpGraphics();
}
void D3D11FrameBridge::CancelPrepared(const ipc::FrameLease& lease)noexcept {
    // Called on the graphics thread after restoration. TryBegin is the only
    // method permitted on the visibility thread.
    preparedGraphicsValid_=false;restoredPairPending_=false;
    timing_.Cancel(TimingKey(lease),TimingQpc());
    depthProducer_.Cancel();depthActive_=false;pendingDepth_={};
    pending_=lease;canceling_=true;submitting_=false;mask_=0;
}
bool D3D11FrameBridge::TryGetRestoredPairLease(RestoredPairLease& out)const noexcept {
    out={};if(pipelineEnabled_)return false; // R22 single-pair service has no v9 authority.
    if(GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire)||
       !connected_.load(std::memory_order_acquire)||sharingMode_.load()!=3||
       failureStage_.load()!=0||canceling_||!restoredPairPending_)return false;
    out={pending_.channelGeneration,pending_.requestId,pending_.native,
        pending_.tracking.generation,pending_.tracking.spaceGeneration,pending_.tracking.predictedNs};
    return true;
}
bool D3D11FrameBridge::SameRestoredPairPending(const RestoredPairLease& expected)const noexcept {
    RestoredPairLease current{};
    return TryGetRestoredPairLease(current)&&current==expected;
}
void D3D11FrameBridge::PumpGraphics()noexcept {
    fvr::diagnostics::privateCpu::Scope cpuPump(fvr::diagnostics::privateCpu::Stage::GraphicsPump);
    if(!connected_.load(std::memory_order_acquire))return;
    if(pipelineEnabled_){PumpPipeline();return;}
    if(earlyFrameAdmission_&&GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire))return;
    if(deferredCancellationPending_){
        const auto result=channel_.PrepareGraphicsResult(deferredCancellation_);
        if(result==ipc::ChannelResult::Busy){++earlyGraphicsDeferrals_;return;}
        const auto lease=deferredCancellation_;deferredCancellation_={};deferredCancellationPending_=false;
        if(result==ipc::ChannelResult::Ok)CancelPrepared(lease);
    }
    fvr::diagnostics::privateGpu::recorder.Poll();
    if(sharingMode_.load()==2){
        const auto relayRead=relay_.PollRelayRead();if(FAILED(relayRead))Fail(11,relayRead);
        if(waitingGameCopies_){const auto ready=relay_.GameCopiesReady();
            if(FAILED(ready)){waitingGameCopies_=false;Fail(11,ready);canceling_=true;}
            else if(ready==S_OK){
                timing_.Mark(TimingKey(pending_),FrameBridgeTimingStage::RelayCopiesReady,TimingQpc());
                waitingGameCopies_=false;const auto& t=pending_.tracking;
                const auto result=producer_.Publish(relay_.Sources(),{pending_.native.frameId,t.spaceGeneration,t.generation,t.predictedNs},ticket_);
                // Even rejected/abandoned host pairs cannot allow the game to
                // overwrite a legacy source until the helper GPU read finishes.
                relay_.SealRelayRead();
                if(result==TransferResult::Ok){timing_.Ticket(TimingKey(pending_),ticket_);submitting_=true;}
                else {Fail(8,producer_.LastError(),producer_.LastOperation());canceling_=true;}
            }
        }
    }
    if(waitingLegacyExport_){
        const std::array<TextureSlice,2> sources={TextureSlice{eyes_[0].Get(),0},TextureSlice{eyes_[1].Get(),0}};const auto& t=pending_.tracking;
        timing_.Export(TimingKey(pending_),3,TimingQpc());
        const auto result=producer_.Publish(sources,{pending_.native.frameId,t.spaceGeneration,t.generation,t.predictedNs},ticket_);
        if(result==TransferResult::Ok){
            timing_.Mark(TimingKey(pending_),FrameBridgeTimingStage::ProducerFenceReady,TimingQpc());
            timing_.Ticket(TimingKey(pending_),ticket_);
            // Publish has completed the producer event query. Depth copies were
            // queued earlier on this SAME immediate context, so it covers them.
            if(depthActive_&&depthLease_.requestId==pending_.requestId)
                depthProducer_.Seal(producer_.Descriptor(),ticket_,depthReversed_,pendingDepth_);
            if(!pendingDepth_.flags)depthProducer_.Cancel();depthActive_=false;
            waitingLegacyExport_=false;submitting_=true;}
        else if(result==TransferResult::Busy)timing_.ExportBusy(TimingKey(pending_),TimingQpc());
        else {restoredPairPending_=false;waitingLegacyExport_=false;Fail(8,producer_.LastError(),producer_.LastOperation());producer_.Reset();depthProducer_.Retire();depthActive_=false;canceling_=true;}
    }
    if(submitting_){const auto result=channel_.Submit(pending_,producer_.Descriptor(),ticket_,pendingDepth_.flags?&pendingDepth_:nullptr);
        if(result==ipc::ChannelResult::Ok){timing_.Mark(TimingKey(pending_),FrameBridgeTimingStage::Staged,TimingQpc());submitting_=false;++published_;}
        else if(result!=ipc::ChannelResult::Busy){restoredPairPending_=false;Fail(9,HRESULT(result));submitting_=false;producer_.Reset();depthProducer_.Retire();depthActive_=false;canceling_=true;}
    }
    if(canceling_){const auto result=channel_.Cancel(pending_);if(result!=ipc::ChannelResult::Busy)canceling_=false;}
    channel_.PumpGraphics();
}
void D3D11FrameBridge::PairConsumed(const PairTicket& ticket,bool consumed)noexcept {
    if(pipelineEnabled_){PipelineConsumed(ticket,consumed);return;}
    // Staged publication failure and both terminal host outcomes release the
    // exact ticket. Clearing here does not wait for the next native Update.
    preparedGraphicsValid_=false;
    if(std::memcmp(&ticket_,&ticket,sizeof(ticket))==0)restoredPairPending_=false;
    depthProducer_.Acknowledge(ticket,consumed);
    if(consumed){producer_.Acknowledge(ticket,true);++consumed_;}else{++discarded_;producer_.Reset();}
    timing_.Released(ticket,consumed,TimingQpc());
}
ipc::ChannelResult D3D11FrameBridge::TransitionWorldOwnerGraphics(const ipc::NativeFrameKey& expectedOld,
    const ipc::NativeFrameKey& replacement)noexcept {
    if(!Connected()||Fatal())return ipc::ChannelResult::Closed;
    if(GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire)||!device_||!context_||
       !expectedOld.owner||!expectedOld.deviceEpoch||!replacement.owner||!replacement.frameId||
       replacement.owner==expectedOld.owner||replacement.deviceEpoch!=expectedOld.deviceEpoch)
        return ipc::ChannelResult::Invalid;
    // v8 already recreates its source on an ordinary owner change. Retain that
    // behavior and its existing leases; this explicit operation adds no reset.
    if(!pipelineEnabled_){
        PumpGraphics();if(Fatal())return ipc::ChannelResult::Closed;
        return restoredPairPending_||preparedGraphicsValid_||deferredCancellationPending_||submitting_||canceling_||
            waitingGameCopies_||waitingLegacyExport_||mask_||depthActive_?
            ipc::ChannelResult::Busy:ipc::ChannelResult::Ok;
    }
    if(pipelineAllocated_&&(allocatedNative_.owner!=expectedOld.owner||
       allocatedNative_.deviceEpoch!=expectedOld.deviceEpoch))return ipc::ChannelResult::Invalid;
    // Normal exact feedback must retire every old source. No timeout, queued
    // request or consumer disappearance grants physical source reuse here.
    PumpPipeline();if(Fatal())return ipc::ChannelResult::Closed;
    if(pipelineCaptureSlot_!=PipelineInvalidSlot||preparedGraphicsValid_||deferredCancellationPending_)
        return ipc::ChannelResult::Busy;
    for(unsigned i=0;i<pipelineSourceCount_;++i){const auto& s=pipeline_[i];
        if(s.state!=PipelineStep::Idle||s.lease.requestId||s.cancelPending||s.cancelLease.requestId||s.depthActive||
           s.colorMask||s.depthMask||s.restorationCommitted||s.captureReleased)return ipc::ChannelResult::Busy;
    }
    const auto result=channel_.TransitionWorldOwner(expectedOld,replacement);
    if(result!=ipc::ChannelResult::Ok)return result;
    // The staged and wire sources are now drained, on the same graphics owner.
    // A new scene may restart its native frame serial, so do not reuse a color
    // descriptor whose consumer still remembers the previous serial. Retiring
    // these sets preserves host-open references; lazy recreation uses the
    // existing per-set budget check and monotonic resource epochs.
    if(pipelineAllocated_){for(unsigned i=0;i<pipelineSourceCount_;++i){auto& s=pipeline_[i];
        s.color.Reset();s.depth.Retire();s.depthAllocationAdmitted=false;
    }
        allocatedNative_=replacement;
    }
    // Before the first allocation, the staged gate alone proves either an
    // exact drained old owner or a never-admitted endpoint. Leave allocation
    // requirements unbound for the ordinary first-frame initialization.
    preparedGraphics_={};preparedDevice_=nullptr;preparedContext_=nullptr;
    return ipc::ChannelResult::Ok;
}
void D3D11FrameBridge::CloseGraphics()noexcept {
    if(pipelineEnabled_&&graphicsThread_.load(std::memory_order_acquire)&&
       GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire)){Fail(20,E_UNEXPECTED);return;}
    preparedGraphicsValid_=false;preparedGraphics_={};preparedDevice_=nullptr;preparedContext_=nullptr;
    deferredCancellationPending_=false;deferredCancellation_={};
    restoredPairPending_=false;
    renderQueue_.Close();
    fvr::diagnostics::privateGpu::recorder.Stop();
    desktopMirror_.Reset();
    connected_.store(false,std::memory_order_release);channel_.CloseGraphics();producer_.Reset();relay_.Reset();depthProducer_.Close();depthInitialized_=depthActive_=false;depthLease_={};pendingDepth_={};waitingGameCopies_=waitingLegacyExport_=false;
    timing_.Close(TimingQpc());
    if(pipelineEnabled_){for(unsigned i=0;i<pipelineSourceCount_;++i){auto& s=pipeline_[i];
        s.color.Reset();s.depth.Close();s.depthInitialized=s.depthAllocationAdmitted=false;s.cancelPending=false;s.cancelLease={};ClearPipelineLease(s);
        pipelineTiming_[i].Close(TimingQpc());
    }pipelineAllocated_=false;pipelineCaptureSlot_=PipelineInvalidSlot;}
    for(auto& eye:eyes_)eye.Reset();context_.Reset();device_.Reset();submitting_=canceling_=false;mask_=0;
    graphicsThread_.store(0,std::memory_order_release);
}

// Explicit private source pipeline; negotiated count never changes in a session.
bool D3D11FrameBridge::PipelineOwner()const noexcept {
    return pipelineEnabled_&&connected_.load(std::memory_order_acquire)&&!Fatal()&&
        GetCurrentThreadId()==graphicsThread_.load(std::memory_order_acquire)&&device_&&context_;
}
bool D3D11FrameBridge::PipelineMatches(const ipc::FrameLease& a,const ipc::FrameLease& b)noexcept {
    const auto& x=a.tracking;const auto& y=b.tracking;
    if(!(a.channelGeneration&&a.requestId&&a.channelGeneration==b.channelGeneration&&a.requestId==b.requestId&&
        (a.sourceSlotCount==2||a.sourceSlotCount==3)&&a.sourceSlotCount==b.sourceSlotCount&&
        a.sourceSlot<a.sourceSlotCount&&a.sourceSlot==b.sourceSlot&&a.native==b.native&&
        std::memcmp(&a.requirements,&b.requirements,sizeof(a.requirements))==0&&
        x.generation==y.generation&&x.spaceGeneration==y.spaceGeneration&&x.predictedNs==y.predictedNs&&
        x.focused==y.focused&&x.headValid==y.headValid&&x.worldUnitsPerMeter==y.worldUnitsPerMeter&&
        SamePose(x.referenceHead,y.referenceHead)&&SamePose(x.head,y.head)))return false;
    for(unsigned eye=0;eye<2;++eye){const auto& p=x.fov[eye];const auto& q=y.fov[eye];
        if(!SamePose(x.eyes[eye],y.eyes[eye])||p.left!=q.left||p.right!=q.right||p.up!=q.up||p.down!=q.down)return false;}
    return true;
}
bool D3D11FrameBridge::CheckPipelineBudget(const runtime::PresentationRequirements& r,std::uint64_t bytes,bool initial)noexcept {
    Microsoft::WRL::ComPtr<IDXGIDevice> dx;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> budgetAdapter;DXGI_QUERY_VIDEO_MEMORY_INFO info{};DXGI_ADAPTER_DESC adapterInfo{};
    if(FAILED(device_.As(&dx))||FAILED(dx->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&adapterInfo))||
       adapterInfo.AdapterLuid.LowPart!=r.adapterLow||adapterInfo.AdapterLuid.HighPart!=r.adapterHigh)return Fail(21,E_INVALIDARG);
    if(SUCCEEDED(adapter.As(&budgetAdapter))){
        const auto result=budgetAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info);
        if(FAILED(result))return Fail(21,result);
        ++pipelineEvidence_.budgetChecks;
        if(initial){pipelineEvidence_.budgetAvailable=true;pipelineEvidence_.budgetBytes=info.Budget;pipelineEvidence_.usageBytes=info.CurrentUsage;}
        if(info.CurrentUsage>info.Budget||bytes>info.Budget-info.CurrentUsage||
           PipelineBudgetHeadroom>info.Budget-info.CurrentUsage-bytes)return Fail(21,E_OUTOFMEMORY);
    }
    return true;
}
bool D3D11FrameBridge::InitializePipeline(const ipc::FrameLease& lease)noexcept {
    const auto& r=lease.requirements;
    if(pipelineAllocated_){
        if(std::memcmp(&allocated_,&r,sizeof(r))||allocatedNative_.owner!=lease.native.owner||
           allocatedNative_.deviceEpoch!=lease.native.deviceEpoch)return Fail(20,E_INVALIDARG);
        return true;
    }
    // Six images per source, two same-context capture images and one existing
    // desktop-left mirror. This bounds logical payload, not physical VRAM.
    if(!r.width||!r.height||r.width>16384||r.height>16384||
       (r.format!=28&&r.format!=29&&r.format!=87&&r.format!=91))return Fail(21,E_INVALIDARG);
    const auto bytes=std::uint64_t(r.width)*r.height*4*pipelineEvidence_.LogicalTextureCount();
    if(bytes>pipelineEvidence_.LogicalByteLimit())return Fail(21,E_OUTOFMEMORY);
    pipelineEvidence_.logicalReservationBytes=bytes;
    if(!CheckPipelineBudget(r,bytes,true))return false;
    D3D11_TEXTURE2D_DESC d{};d.Width=r.width;d.Height=r.height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT(r.format);d.Usage=D3D11_USAGE_DEFAULT;
    for(auto& eye:eyes_){const auto hr=device_->CreateTexture2D(&d,nullptr,&eye);if(FAILED(hr))return Fail(21,hr);}
    for(unsigned i=0;i<pipelineSourceCount_;++i){auto& s=pipeline_[i];
        if(epoch_==UINT64_MAX)return Fail(21,E_UNEXPECTED);
        if(s.color.Create(device_.Get(),r.width,r.height,DXGI_FORMAT(r.format),++epoch_,TextureSharing::LegacyFenced)!=TransferResult::Ok)
            return Fail(21,s.color.LastError(),s.color.LastOperation());
        const auto descriptor=s.color.Descriptor();
        if(descriptor.adapterLow!=r.adapterLow||descriptor.adapterHigh!=r.adapterHigh)return Fail(21,E_INVALIDARG);
        try {if(!s.depth.Initialize(device_.Get()))return Fail(21,E_FAIL);}
        catch(...){return Fail(21,E_OUTOFMEMORY);}
        s.depthInitialized=true;++s.generations;
    }
    allocated_=r;allocatedNative_=lease.native;pipelineAllocated_=true;pipelineEvidence_.allocationAdmitted=true;
    sharingMode_.store(3,std::memory_order_release);return true;
}
bool D3D11FrameBridge::PreparePipeline(const ipc::FrameLease& lease,RestoredPublicationResult* observation)noexcept {
    if(!PipelineOwner()){
        if(observation)*observation=RestoredPublicationResult::WrongGraphicsOwner;return false;
    }
    if(lease.sourceSlotCount!=pipelineSourceCount_||lease.sourceSlot>=pipelineSourceCount_){if(observation)*observation=RestoredPublicationResult::PrepareInvalid;return false;}
    auto& s=pipeline_[lease.sourceSlot];
    if(s.cancelPending){if(observation)*observation=RestoredPublicationResult::PrepareInvalid;return false;}
    if(s.state==PipelineStep::Capturing&&pipelineCaptureSlot_==lease.sourceSlot&&
       PipelineMatches(s.lease,lease)&&PreparedGraphicsMatches(lease)){++preparedGraphicsReuses_;return true;}
    const auto result=channel_.PrepareGraphicsResult(lease);
    if(result!=ipc::ChannelResult::Ok){
        if(result==ipc::ChannelResult::Busy)++pipelineEvidence_.graphicsDeferrals;
        if(observation)*observation=result==ipc::ChannelResult::Busy?RestoredPublicationResult::PrepareBusy:RestoredPublicationResult::PrepareInvalid;
        return false;
    }
    if(!InitializePipeline(lease))return false;
    if(s.state==PipelineStep::Idle){
        if(pipelineCaptureSlot_!=PipelineInvalidSlot)return Fail(20,E_UNEXPECTED);
        if(!Valid(s.color.Descriptor())){
            if(epoch_==UINT64_MAX)return Fail(21,E_UNEXPECTED);
            // Recheck six replacement images, including the depth set that is
            // allocated lazily at its first copy. Retired generations may still
            // occupy physical memory; a logical topology cap does not bound it.
            if(!CheckPipelineBudget(allocated_,std::uint64_t(allocated_.width)*allocated_.height*4*6,false))return false;
            if(s.color.Create(device_.Get(),allocated_.width,allocated_.height,DXGI_FORMAT(allocated_.format),++epoch_,TextureSharing::LegacyFenced)!=TransferResult::Ok)
                return Fail(21,s.color.LastError(),s.color.LastOperation());
            ++s.generations;
        }
        s.lease=lease;s.state=PipelineStep::Capturing;pipelineCaptureSlot_=lease.sourceSlot;
    }
    if(s.state!=PipelineStep::Capturing||!PipelineMatches(s.lease,lease)||pipelineCaptureSlot_!=lease.sourceSlot)return false;
    preparedGraphics_=lease;preparedDevice_=device_.Get();preparedContext_=context_.Get();preparedGraphicsValid_=true;
    return true;
}
bool D3D11FrameBridge::BeginPipelineDepth(const ipc::FrameLease& lease,bool reversed,RestoredPublicationResult* observation)noexcept {
    if(!PreparePipeline(lease,observation))return false;auto& s=pipeline_[lease.sourceSlot];
    if(s.depthActive||s.depthMask||s.colorMask)return false;
    const auto& t=lease.tracking;
    if(!s.depth.Begin({lease.native.frameId,t.spaceGeneration,t.generation,t.predictedNs}))return false;
    s.depthActive=true;s.reversed=reversed;return true;
}
bool D3D11FrameBridge::CapturePipelineDepth(unsigned eye,unsigned stage,ID3D11Texture2D* source,const ipc::FrameLease& lease)noexcept {
    if(!PreparePipeline(lease))return false;auto& s=pipeline_[lease.sourceSlot];
    if(!source||eye>1||stage>1||!s.depthActive)return false;
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    // No resource allocation/copy is allowed for an unproved AA or shape.
    if(d.Width!=allocated_.width||d.Height!=allocated_.height||d.SampleDesc.Count!=1||d.SampleDesc.Quality||
       d.Format!=DXGI_FORMAT_R24G8_TYPELESS||d.MipLevels!=1||d.ArraySize!=1)return Fail(22,E_INVALIDARG);
    // Color-only pairs can precede the first depth allocation by many frames.
    // Recheck the actual validated 1x quartet at that boundary, not just startup.
    if(!s.depthAllocationAdmitted&&
       !CheckPipelineBudget(allocated_,std::uint64_t(d.Width)*d.Height*4*4,false))return false;
    const auto token=fvr::diagnostics::privateGpu::recorder.Begin(
        fvr::diagnostics::privateGpu::Kind::DepthCopy,GpuTimingKey(lease),eye,stage,context_.Get());
    const bool copied=s.depth.Capture(eye,stage,source);fvr::diagnostics::privateGpu::recorder.End(token,copied);
    if(!copied)return Fail(22,E_FAIL);
    s.depthAllocationAdmitted=true;s.depthMask|=1u<<(eye*2+stage);++s.depthCopies;
    pipelineTiming_[lease.sourceSlot].Depth(TimingKey(lease),eye,stage,1,0,unsigned(d.Format),TimingQpc());return true;
}
bool D3D11FrameBridge::CapturePipelineEye(unsigned eye,ID3D11Texture2D* source,const ipc::FrameLease& lease)noexcept {
    if(!PreparePipeline(lease))return false;auto& s=pipeline_[lease.sourceSlot];
    if(!source||eye>1||s.colorMask!=(eye?1u:0u))return false;
    auto& journal=pipelineTiming_[lease.sourceSlot];
    journal.Mark(TimingKey(lease),eye?FrameBridgeTimingStage::CaptureRightBegin:FrameBridgeTimingStage::CaptureLeftBegin,TimingQpc());
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);Microsoft::WRL::ComPtr<ID3D11Device> owner;source->GetDevice(&owner);
    if(owner.Get()!=device_.Get()||d.Width!=allocated_.width||d.Height!=allocated_.height||d.SampleDesc.Count!=1||d.SampleDesc.Quality||
       d.MipLevels!=1||d.ArraySize!=1||!CopyCompatibleFormats(d.Format,allocated_.format))return Fail(22,E_INVALIDARG);
    if(!eye)desktopMirror_.ObserveOwner(lease.native.owner,lease.native.deviceEpoch,lease.tracking.spaceGeneration);
    // Every prior export's CopySubresourceRegion and mirror read has already
    // been queued on THIS immediate context before this overwrite. Polling a
    // producer event never rereads the capture eyes; no CPU fence wait is needed.
    const auto token=fvr::diagnostics::privateGpu::recorder.Begin(
        fvr::diagnostics::privateGpu::Kind::EyeCapture,GpuTimingKey(lease),eye,0,context_.Get());
    context_->CopyResource(eyes_[eye].Get(),source);fvr::diagnostics::privateGpu::recorder.End(token);
    s.colorMask|=1u<<eye;++s.eyeCopies;++capturedEyes_;
    journal.Mark(TimingKey(lease),eye?FrameBridgeTimingStage::CaptureRightQueued:FrameBridgeTimingStage::CaptureLeftQueued,TimingQpc());
    return true;
}
bool D3D11FrameBridge::PublishPipelineRestored(const ipc::FrameLease& lease,RestoredPublicationResult* observation)noexcept {
    const bool prepared=PipelineOwner()&&PreparedGraphicsMatches(lease);
    if(!prepared&&!PreparePipeline(lease,observation))return false;auto& s=pipeline_[lease.sourceSlot];
    if(s.state!=PipelineStep::Capturing||pipelineCaptureSlot_!=lease.sourceSlot||!PipelineMatches(s.lease,lease)||
       s.colorMask!=3||s.cancelPending){
        if(observation)*observation=RestoredPublicationResult::CaptureStateRejected;return false;
    }
    preparedGraphicsValid_=false;if(prepared)++preparedPublications_;
    pipelineTiming_[lease.sourceSlot].Mark(TimingKey(lease),FrameBridgeTimingStage::Restored,TimingQpc());
    s.state=PipelineStep::Exporting;
    // This first service MUST enqueue color reads before releasing the shared
    // capture images. No channel lock is needed to enqueue this exact source.
    PumpPipelineSlot(lease.sourceSlot);
    desktopMirror_.CommitLeft(eyes_[0].Get());pipelineCaptureSlot_=PipelineInvalidSlot;
    if(Fatal()){if(observation)*observation=RestoredPublicationResult::ExportRejected;return false;}
    // Only this restored boundary proves all shared capture-image reads and
    // the desktop mirror read precede another camera's writes on this context.
    s.restorationCommitted=true;TryReleasePipelineCapture(lease.sourceSlot);
    if(observation)*observation=RestoredPublicationResult::Accepted;
    channel_.PumpGraphics();return true;
}
void D3D11FrameBridge::ClearPipelineLease(PipelineSource& s)noexcept {
    s.lease={};s.ticket={};s.depthFrame={};s.state=PipelineStep::Idle;
    s.colorMask=s.depthMask=0;s.depthActive=s.reversed=false;
    s.restorationCommitted=s.captureReleased=false;
}
void D3D11FrameBridge::TryReleasePipelineCapture(unsigned index)noexcept {
    if(!channel_.ExportOverlapEnabled()||index>=pipelineSourceCount_||!PipelineOwner())return;
    auto& s=pipeline_[index];
    if(s.state!=PipelineStep::Exporting||!s.restorationCommitted||s.captureReleased||s.cancelPending||pipelineCaptureSlot_==index)return;
    const auto& t=s.lease.tracking;
    const FrameMetadata frame{s.lease.native.frameId,t.spaceGeneration,t.generation,t.predictedNs};
    if(!s.color.ExportReadsQueued(device_.Get(),context_.Get(),frame))return;
    auto& journal=pipelineTiming_[index];
    journal.Mark(TimingKey(s.lease),FrameBridgeTimingStage::CaptureReleaseBegin,TimingQpc());
    ++pipelineEvidence_.captureReleaseAttempts;
    const auto result=channel_.MarkCaptureReleased(s.lease);
    if(result==ipc::ChannelResult::Ok){
        s.captureReleased=true;++pipelineEvidence_.captureReleases;
        journal.Mark(TimingKey(s.lease),FrameBridgeTimingStage::CaptureReleased,TimingQpc());
    }else if(result==ipc::ChannelResult::Busy)++pipelineEvidence_.captureReleaseBusy;
    else ++pipelineEvidence_.captureReleaseDeclined;
    // A contended or cancelled marker cannot turn this optimization into a
    // failure or delay real publication. Retry only on an existing pump while
    // still exporting; the original fence/Submit/retirement path stays in force.
}
void D3D11FrameBridge::PumpPipelineSlot(unsigned index)noexcept {
    auto& s=pipeline_[index];auto& journal=pipelineTiming_[index];
    if(s.state==PipelineStep::Exporting){
        TryReleasePipelineCapture(index);
        const auto& t=s.lease.tracking;const std::array<TextureSlice,2> sources{{{eyes_[0].Get(),0},{eyes_[1].Get(),0}}};
        journal.Export(TimingKey(s.lease),3,TimingQpc());
        const auto result=s.color.Publish(sources,{s.lease.native.frameId,t.spaceGeneration,t.generation,t.predictedNs},s.ticket);
        if(result==TransferResult::Busy){journal.ExportBusy(TimingKey(s.lease),TimingQpc());return;}
        if(result!=TransferResult::Ok){Fail(23,s.color.LastError(),s.color.LastOperation());return;}
        journal.Mark(TimingKey(s.lease),FrameBridgeTimingStage::ProducerFenceReady,TimingQpc());journal.Ticket(TimingKey(s.lease),s.ticket);
        s.depthFrame={};
        if(s.depthActive&&s.depthMask==15){
            if(!s.depth.Seal(s.color.Descriptor(),s.ticket,s.reversed,s.depthFrame)||
               s.depthFrame.samples!=1||s.depthFrame.quality){Fail(22,E_FAIL);return;}
        }else{
            // Optional absence includes an incomplete first-person depth pass.
            // Cancel discards metadata only and RETAINS allocated images. Any
            // partial copies precede this color query on the same context.
            s.depth.Cancel();
        }
        s.depthActive=false;s.state=PipelineStep::Submitting;++s.exports;
    }
    if(s.state==PipelineStep::Submitting){
        const auto result=channel_.Submit(s.lease,s.color.Descriptor(),s.ticket,SceneDepthAbsent(s.depthFrame)?nullptr:&s.depthFrame);
        if(result==ipc::ChannelResult::Busy)return;
        if(result!=ipc::ChannelResult::Ok){Fail(24,HRESULT(result));return;}
        journal.Mark(TimingKey(s.lease),FrameBridgeTimingStage::Staged,TimingQpc());s.state=PipelineStep::Published;++s.submissions;++published_;
    }
}
void D3D11FrameBridge::CancelPipeline(const ipc::FrameLease& lease)noexcept {
    if(!pipelineEnabled_||GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire)||
       lease.sourceSlotCount!=pipelineSourceCount_||lease.sourceSlot>=pipelineSourceCount_)return;
    if(PipelineMatches(preparedGraphics_,lease))preparedGraphicsValid_=false;
    auto& s=pipeline_[lease.sourceSlot];
    // Do not revoke a submitted consumer-owned source. Channel close/its exact
    // terminal callback handles that source independently of the active camera.
    if(s.state==PipelineStep::Published)return;
    const auto prepared=channel_.PrepareGraphicsResult(lease);
    if(prepared==ipc::ChannelResult::Busy){s.cancelLease=lease;s.cancelPending=true;++pipelineEvidence_.graphicsDeferrals;return;}
    if(prepared!=ipc::ChannelResult::Ok){
        if(PipelineMatches(s.cancelLease,lease)){s.cancelLease={};s.cancelPending=false;}
        return;
    }
    if(s.state!=PipelineStep::Idle&&!PipelineMatches(s.lease,lease))return;
    if(s.state!=PipelineStep::Cancelling){
        pipelineTiming_[lease.sourceSlot].Cancel(TimingKey(lease),TimingQpc());
        if(s.state==PipelineStep::Exporting||s.state==PipelineStep::Submitting){s.color.Reset();s.depth.Retire();s.depthAllocationAdmitted=false;}
        else s.depth.Cancel();
        if(pipelineCaptureSlot_==lease.sourceSlot)pipelineCaptureSlot_=PipelineInvalidSlot;
        s.lease=lease;s.state=PipelineStep::Cancelling;++s.cancellations;
    }
    s.cancelLease=lease;s.cancelPending=true;
    const auto result=channel_.Cancel(lease);
    if(result!=ipc::ChannelResult::Busy){s.cancelLease={};s.cancelPending=false;ClearPipelineLease(s);}
}
void D3D11FrameBridge::PumpPipeline()noexcept {
    if(GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire))return;
    fvr::diagnostics::privateGpu::recorder.Poll();
    channel_.PumpGraphics();
    for(unsigned i=0;i<pipelineSourceCount_;++i){auto& s=pipeline_[i];
        if(s.cancelPending){const auto lease=s.cancelLease;CancelPipeline(lease);}
        if(!Fatal()&&!s.cancelPending)PumpPipelineSlot(i);
    }
    channel_.PumpGraphics();
}
void D3D11FrameBridge::PipelineConsumed(const PairTicket& ticket,bool consumed)noexcept {
    if(GetCurrentThreadId()!=graphicsThread_.load(std::memory_order_acquire)){++pipelineEvidence_.rejectedFeedback;Fail(20,E_UNEXPECTED);return;}
    for(unsigned i=0;i<pipelineSourceCount_;++i){auto& s=pipeline_[i];
        if(s.state!=PipelineStep::Published||std::memcmp(&s.ticket,&ticket,sizeof(ticket)))continue;
        if(!Valid(ticket,s.color.Descriptor())){++pipelineEvidence_.rejectedFeedback;Fail(24,E_INVALIDARG);return;}
        if(PipelineMatches(preparedGraphics_,s.lease))preparedGraphicsValid_=false;
        s.depth.Acknowledge(ticket,consumed);
        if(consumed){s.color.Acknowledge(ticket,true);++s.consumed;++consumed_;}
        else{s.color.Reset();s.depth.Retire();s.depthAllocationAdmitted=false;++s.discarded;++discarded_;}
        pipelineTiming_[i].Released(ticket,consumed,TimingQpc());ClearPipelineLease(s);return;
    }
    ++pipelineEvidence_.rejectedFeedback;Fail(24,E_INVALIDARG);
}
PipelineGraphicsSnapshot D3D11FrameBridge::PipelineSnapshot(bool drained)const noexcept {
    PipelineGraphicsSnapshot out{};out.configured=pipelineEnabled_;
    out.sourceSlots=pipelineEnabled_?pipelineSourceCount_:2u;if(!drained)return out;
    out=pipelineEvidence_;out.configured=pipelineEnabled_;out.available=pipelineEnabled_;
    if(pipelineEnabled_)for(unsigned i=0;i<pipelineSourceCount_;++i){const auto& s=pipeline_[i];out.slots[i]={s.generations,s.eyeCopies,s.depthCopies,s.exports,s.submissions,s.consumed,s.discarded,s.cancellations,pipelineEvidence_.slots[i].lastPublication};}
    return out;
}

}
