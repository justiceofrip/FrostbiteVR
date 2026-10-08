#include "fvr/graphics/D3D11FrameBridge.h"
#include <cstring>
#include <d3d11_1.h>
namespace fvr::graphics {
bool D3D11FrameBridge::ConnectGraphics(ID3D11Device* device,const std::wstring& token,FrameSharingPreference preference)noexcept {
    if(!device||connected_.load(std::memory_order_acquire))return false;
    forceLegacy_=preference==FrameSharingPreference::LegacyRelay;forceLegacyIpc_=preference==FrameSharingPreference::LegacyIpc;device_=device;deviceFlags_=device->GetCreationFlags();featureLevel_=device->GetFeatureLevel();device_->GetImmediateContext(&context_);
    if(!channel_.Connect(token)){device_.Reset();context_.Reset();return false;}
    connected_.store(true,std::memory_order_release);return true;
}
bool D3D11FrameBridge::TryBegin(const ipc::NativeFrameKey& native,ipc::FrameLease& lease)noexcept {
    return connected_.load(std::memory_order_acquire)&&!Fatal()&&(sharingMode_.load(std::memory_order_acquire)!=2||relay_.SourceWritable())&&channel_.TryBegin(native,lease);
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
bool D3D11FrameBridge::CaptureEye(unsigned eye,ID3D11Texture2D* source,const ipc::FrameLease& lease)noexcept {
    if(!connected_.load(std::memory_order_acquire)||!source||eye>1||submitting_||canceling_||waitingGameCopies_||waitingLegacyExport_)return Fail(1,E_UNEXPECTED);
    const auto& req=lease.requirements;D3D11_TEXTURE2D_DESC description{};source->GetDesc(&description);
    Microsoft::WRL::ComPtr<ID3D11Device> sourceDevice;source->GetDevice(&sourceDevice);
    if(sourceDevice.Get()!=device_.Get()||description.Width!=req.width||description.Height!=req.height||description.MipLevels!=1||description.ArraySize!=1||description.SampleDesc.Count!=1||!CopyCompatibleFormats(description.Format,req.format))return Fail(2,E_INVALIDARG);
    if(eye==0){
        capturedRequest_=lease.requestId;mask_=0;
        if(!sharingMode_.load()||std::memcmp(&allocated_,&req,sizeof(req))){
            producer_.Reset();for(auto& texture:eyes_)texture.Reset();relay_.Reset();sharingMode_.store(0);
            if(!forceLegacy_&&!forceLegacyIpc_){
                const auto result=producer_.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_);
                if(result!=TransferResult::Ok){failureOperation_=producer_.LastOperation();const auto error=producer_.LastError();
                    if(error!=E_INVALIDARG||failureOperation_.load()!=5)return Fail(4,error);
                    forceLegacyIpc_=true;
                }
            }
            if(forceLegacyIpc_){
                if(producer_.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_,TextureSharing::LegacyFenced)!=TransferResult::Ok)return Fail(4,producer_.LastError());
                description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=0;description.CPUAccessFlags=0;description.MiscFlags=0;
                for(auto& texture:eyes_){const auto hr=device_->CreateTexture2D(&description,nullptr,&texture);if(FAILED(hr))return Fail(3,hr);}
                sharingMode_.store(3,std::memory_order_release);
            }else if(forceLegacy_){
                if(!relay_.Create(device_.Get(),req.width,req.height,DXGI_FORMAT(req.format))){failureOperation_=100+relay_.LastOperation();return Fail(10,relay_.LastError());}
                if(producer_.Create(relay_.Device(),req.width,req.height,DXGI_FORMAT(req.format),++epoch_)!=TransferResult::Ok){failureOperation_=200+producer_.LastOperation();return Fail(10,producer_.LastError());}
                sharingMode_.store(2,std::memory_order_release);
            }else{
                description.Usage=D3D11_USAGE_DEFAULT;description.BindFlags=0;description.CPUAccessFlags=0;description.MiscFlags=0;
                for(auto& texture:eyes_){const auto hr=device_->CreateTexture2D(&description,nullptr,&texture);if(FAILED(hr))return Fail(3,hr);}
                sharingMode_.store(1,std::memory_order_release);
            }
            allocated_=req;
        }
        if(!Valid(producer_.Descriptor())){
            auto* exporting=sharingMode_.load()==2?relay_.Device():device_.Get();
            if(producer_.Create(exporting,req.width,req.height,DXGI_FORMAT(req.format),++epoch_,sharingMode_.load()==3?TextureSharing::LegacyFenced:TextureSharing::NamedKeyed)!=TransferResult::Ok)return Fail(4,producer_.LastError());
        }
        const auto descriptor=producer_.Descriptor();if(descriptor.adapterLow!=req.adapterLow||descriptor.adapterHigh!=req.adapterHigh)return Fail(5,E_INVALIDARG);
    }else if(capturedRequest_!=lease.requestId||mask_!=1)return Fail(6,E_UNEXPECTED);
    if(sharingMode_.load()==2){if(!relay_.SourceWritable())return Fail(12,E_UNEXPECTED);relay_.Capture(eye,source);}
    else context_->CopyResource(eyes_[eye].Get(),source);
    mask_|=1u<<eye;++capturedEyes_;return true;
}
bool D3D11FrameBridge::PublishRestored(const ipc::FrameLease& lease)noexcept {
    if(mask_!=3||capturedRequest_!=lease.requestId||submitting_||canceling_||waitingGameCopies_||waitingLegacyExport_)return Fail(7,E_UNEXPECTED);
    pending_=lease;mask_=0;
    if(sharingMode_.load()==3){waitingLegacyExport_=true;PumpGraphics();return true;}
    if(sharingMode_.load()==2){relay_.SealGameCopies();waitingGameCopies_=true;PumpGraphics();return true;}
    const std::array<TextureSlice,2> sources={TextureSlice{eyes_[0].Get(),0},TextureSlice{eyes_[1].Get(),0}};
    const auto& tracking=lease.tracking;
    if(producer_.Publish(sources,{lease.native.frameId,tracking.spaceGeneration,tracking.generation,tracking.predictedNs},ticket_)!=TransferResult::Ok)return Fail(8,producer_.LastError());
    submitting_=true;PumpGraphics();return true;
}
void D3D11FrameBridge::Cancel(const ipc::FrameLease& lease)noexcept {
    // Called on the graphics thread after restoration. TryBegin is the only
    // method permitted on the visibility thread.
    pending_=lease;canceling_=true;submitting_=false;mask_=0;PumpGraphics();
}
void D3D11FrameBridge::PumpGraphics()noexcept {
    if(!connected_.load(std::memory_order_acquire))return;
    if(sharingMode_.load()==2){
        const auto relayRead=relay_.PollRelayRead();if(FAILED(relayRead))Fail(11,relayRead);
        if(waitingGameCopies_){const auto ready=relay_.GameCopiesReady();
            if(FAILED(ready)){waitingGameCopies_=false;Fail(11,ready);canceling_=true;}
            else if(ready==S_OK){
                waitingGameCopies_=false;const auto& t=pending_.tracking;
                const auto result=producer_.Publish(relay_.Sources(),{pending_.native.frameId,t.spaceGeneration,t.generation,t.predictedNs},ticket_);
                // Even rejected/abandoned host pairs cannot allow the game to
                // overwrite a legacy source until the helper GPU read finishes.
                relay_.SealRelayRead();
                if(result==TransferResult::Ok)submitting_=true;
                else {Fail(8,producer_.LastError());canceling_=true;}
            }
        }
    }
    if(waitingLegacyExport_){
        const std::array<TextureSlice,2> sources={TextureSlice{eyes_[0].Get(),0},TextureSlice{eyes_[1].Get(),0}};const auto& t=pending_.tracking;
        const auto result=producer_.Publish(sources,{pending_.native.frameId,t.spaceGeneration,t.generation,t.predictedNs},ticket_);
        if(result==TransferResult::Ok){waitingLegacyExport_=false;submitting_=true;}
        else if(result!=TransferResult::Busy){waitingLegacyExport_=false;Fail(8,producer_.LastError());producer_.Reset();canceling_=true;}
    }
    if(submitting_){const auto result=channel_.Submit(pending_,producer_.Descriptor(),ticket_);
        if(result==ipc::ChannelResult::Ok){submitting_=false;++published_;}
        else if(result!=ipc::ChannelResult::Busy){Fail(9,HRESULT(result));submitting_=false;producer_.Reset();canceling_=true;}
    }
    if(canceling_){const auto result=channel_.Cancel(pending_);if(result!=ipc::ChannelResult::Busy)canceling_=false;}
    channel_.PumpGraphics();
}
void D3D11FrameBridge::PairConsumed(const PairTicket& ticket,bool consumed)noexcept {
    if(consumed){producer_.Acknowledge(ticket,true);++consumed_;}else{++discarded_;producer_.Reset();}
}
void D3D11FrameBridge::CloseGraphics()noexcept {
    connected_.store(false,std::memory_order_release);channel_.CloseGraphics();producer_.Reset();relay_.Reset();waitingGameCopies_=waitingLegacyExport_=false;
    for(auto& eye:eyes_)eye.Reset();context_.Reset();device_.Reset();submitting_=canceling_=false;mask_=0;
}
}
