#pragma once
#include "fvr/ipc/FrameChannel.h"
#include "fvr/runtime/FrameProvider.h"
#include <atomic>
namespace fvr::ipc {
// Local identity, never a wire pointer. The engine adapter supplies generations
// for world ownership and the graphics device, plus its native frame serial.
struct NativeFrameKey {
    std::uint64_t owner=0,frameId=0,deviceEpoch=0;
    bool operator==(const NativeFrameKey&)const=default;
};
struct FrameLease {
    std::uint64_t channelGeneration=0,requestId=0;
    NativeFrameKey native{};
    runtime::PresentationRequirements requirements{};
    runtime::TrackingFrame tracking{};
};
// One request can span native visibility and graphics callbacks on different
// threads. No native calls, simulation updates or GPU work occur in TryBegin.
// Runtime operations use try-locks only; contention drops/defer work.
class StagedFrameProducer {
public:
    explicit StagedFrameProducer(runtime::IFrameProvider& feedback):feedback_(feedback){}
    ~StagedFrameProducer(){CloseGraphics();}
    StagedFrameProducer(const StagedFrameProducer&)=delete;
    StagedFrameProducer& operator=(const StagedFrameProducer&)=delete;
    // Setup/teardown only with both callback threads quiescent, on graphics thread.
    bool Connect(const std::wstring&)noexcept;
    void CloseGraphics()noexcept;
    // Once taken, a frame cannot be retried, even after cancellation/reconnect.
    bool TryBegin(const NativeFrameKey&,FrameLease&)noexcept;
    ChannelResult ReadInput(interaction::InputFrame& out,std::int64_t* deadlineQpc=nullptr)noexcept {
        if(deadlineQpc)*deadlineQpc=0;out={};Lock lock(gate_);return lock?channel_.ReadInput(out,deadlineQpc):ChannelResult::Busy;
    }
    bool PublishFeedback(const interaction::FeedbackEvent& e)noexcept {Lock lock(gate_);return lock&&channel_.PublishFeedback(e);}
    // Ok transfers ticket ownership. On Busy/Invalid caller still owns the pair
    // and must retry or reclaim it. Submit ONLY after exact native restoration.
    ChannelResult Submit(const FrameLease&,const graphics::TextureDescriptor&,const graphics::PairTicket&)noexcept;
    ChannelResult Cancel(const FrameLease&)noexcept;
    ChannelResult SetBodyProps(const FrameLease&,unsigned eye,const graphics::BodyPropEye&)noexcept;
    // Graphics thread only: publish and return GPU ownership feedback exactly once.
    void PumpGraphics()noexcept;
private:
    enum class Step{Idle,Rendering,Skipping,Publishing,Waiting};
    struct Lock {
        std::atomic_flag& flag;bool held;
        explicit Lock(std::atomic_flag& f):flag(f),held(!f.test_and_set(std::memory_order_acquire)){}
        ~Lock(){if(held)flag.clear(std::memory_order_release);}
        explicit operator bool()const{return held;}
    };
    bool Matches(const FrameLease&)const noexcept;
    FrameChannel channel_;runtime::IFrameProvider& feedback_;
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    Step step_=Step::Idle;std::uint64_t generation_=0;
    NativeFrameKey lastNative_{};
    graphics::BodyPropFrame bodyProps_{};
    FrameLease lease_{};graphics::TextureDescriptor descriptor_{};graphics::PairTicket ticket_{};
};
}
