#include "fvr/graphics/D3D11MenuProducer.h"
namespace fvr::graphics {
void D3D11MenuProducer::Reset()noexcept {
    producer_.Reset();for(auto& s:scratch_)s.Reset();context_.Reset();device_.Reset();
    exporting_=publishing_=awaiting_=false;width_=height_=format_=0;pending_={};
}
bool D3D11MenuProducer::Advance(ipc::MenuChannel& channel)noexcept {
    if(!channel.Connected())return false;
    if(awaiting_){
        const auto result=channel.PollOutcome();if(result==ipc::Outcome::Pending)return false;
        if(result==ipc::Outcome::Consumed)producer_.Acknowledge(pending_.ticket,true);else {++discarded_;producer_.Reset();}
        awaiting_=false;
    }
    if(exporting_){
        const std::array<TextureSlice,2> sources{{{scratch_[0].Get(),0},{scratch_[1].Get(),0}}};
        const auto result=producer_.Publish(sources,{frame_,pending_.state.epoch,pending_.state.sequence,1},pending_.ticket);
        if(result==TransferResult::Busy)return false;
        exporting_=false;if(result!=TransferResult::Ok){++failed_;producer_.Reset();return false;}
        pending_.descriptor=producer_.Descriptor();publishing_=true;
    }
    if(publishing_){
        const auto result=channel.PublishSurface(pending_);if(result==ipc::ChannelResult::Busy)return false;
        publishing_=false;if(result==ipc::ChannelResult::Ok){++published_;awaiting_=true;}else{if(result==ipc::ChannelResult::Timeout||result==ipc::ChannelResult::Closed)++discarded_;else ++failed_;producer_.Reset();}
        return false;
    }
    return channel.ReadState(state_)==ipc::ChannelResult::Ok&&state_.mode==ipc::MenuMode::Menu;
}
void D3D11MenuProducer::Pump(IDXGISwapChain* chain,ipc::MenuChannel& channel)noexcept {
    if(!chain||!Advance(channel))return;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source;if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&source)))){++failed_;return;}
    Capture(source.Get());
}
void D3D11MenuProducer::Pump(ID3D11Texture2D* source,ipc::MenuChannel& channel)noexcept {
    if(source&&Advance(channel))Capture(source);
}
void D3D11MenuProducer::Capture(ID3D11Texture2D* source)noexcept {
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(!d.Width||!d.Height||d.Width>16384||d.Height>16384||d.SampleDesc.Count!=1||d.ArraySize!=1||d.MipLevels!=1||!CopyCompatibleFormats(d.Format,d.Format)){++failed_;return;}
    Microsoft::WRL::ComPtr<ID3D11Device> actual;source->GetDevice(&actual);
    if(actual.Get()!=device_.Get()||d.Width!=width_||d.Height!=height_||d.Format!=format_){
        Reset();device_=actual;device_->GetImmediateContext(&context_);width_=d.Width;height_=d.Height;format_=d.Format;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=0;d.CPUAccessFlags=0;d.MiscFlags=0;
        for(auto& s:scratch_)if(FAILED(device_->CreateTexture2D(&d,nullptr,&s))){++failed_;Reset();return;}
    }
    if(!Valid(producer_.Descriptor())&&producer_.Create(device_.Get(),width_,height_,DXGI_FORMAT(format_),++epoch_,TextureSharing::LegacyFenced)!=TransferResult::Ok){++failed_;return;}
    for(auto& s:scratch_)context_->CopyResource(s.Get(),source);
    pending_={};pending_.state=state_;++frame_;++captured_;exporting_=true;
}
}
