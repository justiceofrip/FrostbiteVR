#pragma once
// One-time initialisation that is safe against re-entrancy on the initialising thread
// (the dxgi proxy's init creates a D3D device, which calls back into the proxy's own
// CreateDXGIFactory exports) and against concurrent first calls from other threads
// (they wait for init to finish). Platform-neutral: thread ids are passed in.
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace fvr::platform {

class InitGate {
public:
    enum class Decision { RunInit, Reentrant, AlreadyDone };

    Decision Enter(std::uint64_t threadId) {
        std::unique_lock lock(mutex_);
        if (state_ == State::Done) return Decision::AlreadyDone;
        if (state_ == State::Running) {
            if (owner_ == threadId) return Decision::Reentrant;
            done_.wait(lock, [&] { return state_ == State::Done; });
            return Decision::AlreadyDone;
        }
        state_ = State::Running;
        owner_ = threadId;
        return Decision::RunInit;
    }

    void Finish() {
        { std::lock_guard lock(mutex_); state_ = State::Done; }
        done_.notify_all();
    }

private:
    enum class State { NotStarted, Running, Done };
    std::mutex mutex_;
    std::condition_variable done_;
    State state_ = State::NotStarted;
    std::uint64_t owner_ = 0;
};

} // namespace fvr::platform
