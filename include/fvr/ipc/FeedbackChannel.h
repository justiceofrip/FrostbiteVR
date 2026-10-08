#pragma once
#include "fvr/ipc/FeedbackProtocol.h"
#include <memory>
#include <string>
namespace fvr::ipc {
// Independent session-token lane. Every runtime operation uses a zero-wait
// mutex; bounded queue overflow drops feedback rather than delaying the game.
class FeedbackChannel {
public:
    FeedbackChannel();~FeedbackChannel();
    FeedbackChannel(const FeedbackChannel&)=delete;FeedbackChannel& operator=(const FeedbackChannel&)=delete;
    bool CreateHost(const std::wstring&)noexcept;
    bool ConnectProducer(const std::wstring&)noexcept;
    bool Publish(const interaction::FeedbackEvent&)noexcept;
    bool Take(interaction::FeedbackEvent&)noexcept;
    void Close()noexcept;
private:
    struct State;std::unique_ptr<State> state_;
    bool Open(const std::wstring&,bool)noexcept;
};
}
