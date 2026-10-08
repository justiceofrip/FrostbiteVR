#pragma once
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11LegacyRelay.h"
#include "fvr/ipc/StagedFrameProducer.h"
#include <wrl/client.h>
#include <atomic>
namespace fvr::graphics {
enum class FrameSharingPreference {Automatic,LegacyRelay,LegacyIpc};
// Reusable native-renderer transport: no game layout, hooks, or XR SDK types.
// All GPU/setup calls belong to the original immediate-context thread.
class D3D11FrameBridge final:public runtime::IFrameProvider {
public:
    D3D11FrameBridge():channel_(*this){}
    ~D3D11FrameBridge(){CloseGraphics();}
    bool ConnectGraphics(ID3D11Device*,const std::wstring&,FrameSharingPreference preference=FrameSharingPreference::Automatic)noexcept;
    bool TryBegin(const ipc::NativeFrameKey&,ipc::FrameLease&)noexcept;
    ipc::ChannelResult ReadInput(interaction::InputFrame& out,std::int64_t* deadlineQpc=nullptr)noexcept{return channel_.ReadInput(out,deadlineQpc);}
    bool PublishFeedback(const interaction::FeedbackEvent& event)noexcept{return channel_.PublishFeedback(event);}
    bool CaptureEye(unsigned,ID3D11Texture2D*,const ipc::FrameLease&)noexcept;
    ipc::ChannelResult SetBodyProps(const ipc::FrameLease& lease,unsigned eye,const BodyPropEye& props)noexcept {
        return channel_.SetBodyProps(lease,eye,props);
    }
    bool PublishRestored(const ipc::FrameLease&)noexcept;
    void Cancel(const ipc::FrameLease&)noexcept;
    void PumpGraphics()noexcept;
    void CloseGraphics()noexcept;
    std::uint64_t Published()const noexcept{return published_.load();}
    std::uint64_t Consumed()const noexcept{return consumed_.load();}
    std::uint64_t Discarded()const noexcept{return discarded_.load();}
    unsigned FailureStage()const noexcept{return failureStage_.load();}
    long FailureCode()const noexcept{return failureCode_.load();}
    std::uint64_t CapturedEyes()const noexcept{return capturedEyes_.load();}
    long Compatibility(unsigned index)const noexcept{return compatibility_[index].load();}
    unsigned DeviceFlags()const noexcept{return deviceFlags_.load();}
    unsigned FeatureLevel()const noexcept{return featureLevel_.load();}
    unsigned FailureOperation()const noexcept{return failureOperation_.load();}
    unsigned SharingMode()const noexcept{return sharingMode_.load();}
    bool Fatal()const noexcept{const auto stage=failureStage_.load();return (stage>=2&&stage<=5)||stage>=10;}
    bool Connected()const noexcept{return connected_.load(std::memory_order_acquire);}
private:
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,TextureDescriptor&,PairTicket&)noexcept override{return false;}
    void PairConsumed(const PairTicket&,bool)noexcept override;
    D3D11LegacyRelay relay_;bool forceLegacy_=false,forceLegacyIpc_=false,waitingGameCopies_=false,waitingLegacyExport_=false;
    std::atomic<unsigned> sharingMode_=0;
    ipc::StagedFrameProducer channel_;D3D11PairProducer producer_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> eyes_;
    runtime::PresentationRequirements allocated_{};std::uint64_t epoch_=0,capturedRequest_=0;unsigned mask_=0;
    ipc::FrameLease pending_{};PairTicket ticket_{};bool submitting_=false,canceling_=false;
    void DiagnoseSharing()noexcept;
    std::array<std::atomic<long>,9> compatibility_{};
    std::atomic<unsigned> deviceFlags_=0,featureLevel_=0,failureOperation_=0;
    std::atomic<unsigned> failureStage_=0;std::atomic<long> failureCode_=0;std::atomic<std::uint64_t> capturedEyes_=0;
    bool Fail(unsigned stage,HRESULT code)noexcept{if(!failureStage_.load()){failureCode_.store(code);failureStage_.store(stage);}return false;}
    std::atomic<bool> connected_=false;
    std::atomic<std::uint64_t> published_=0,consumed_=0,discarded_=0;
};
}
