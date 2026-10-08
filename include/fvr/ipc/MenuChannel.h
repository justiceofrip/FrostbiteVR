#pragma once
#include "fvr/ipc/FrameChannel.h"
#include "fvr/ipc/MenuProtocol.h"
namespace fvr::ipc {
// Both menu input and the final native menu image use a separate, session-bound
// channel. All operations are zero-wait; repeated reads retain original expiry.
class MenuChannel {
public:
    MenuChannel();~MenuChannel();
    MenuChannel(const MenuChannel&)=delete;MenuChannel& operator=(const MenuChannel&)=delete;
    bool CreateHost(const std::wstring& frameToken)noexcept;
    bool ConnectProducer(const std::wstring& frameToken)noexcept;
    void Close()noexcept;
    bool Connected()noexcept;
    ChannelResult PublishState(MenuState)noexcept;
    ChannelResult ReadState(MenuState&)noexcept;
    ChannelResult PublishControl(MenuControl)noexcept;
    ChannelResult ReadControl(MenuControl&)noexcept;
    ChannelResult PublishSurface(const MenuSurface&)noexcept;
    ChannelResult TakeSurface(MenuSurface&)noexcept;
    ChannelResult SurfaceConsumed(const graphics::PairTicket&,bool)noexcept;
    Outcome PollOutcome()noexcept;
private:
    struct State;std::unique_ptr<State> state_;
    bool Open(const std::wstring&,bool host)noexcept;
};
}
