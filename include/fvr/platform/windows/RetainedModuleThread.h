#pragma once
#include <new>
#include <thread>
#include <utility>

namespace fvr::platform {
// Only for static workers in a module retained until process exit. The owner
// must still signal Stop and join on ordinary retirement, outside loader lock.
// If native retirement refuses, the module and thread remain owned. Windows
// process exit can then reach CRT destruction after terminating other threads;
// destroying a still-joinable std::thread would call terminate(). Keep its
// bounded storage/handle until process exit instead. Never detach, wait, or
// infer successful native retirement from this storage lifetime.
class RetainedModuleThread {
public:
    RetainedModuleThread() noexcept { ::new(static_cast<void*>(storage_)) std::thread; }
    RetainedModuleThread(const RetainedModuleThread&)=delete;
    RetainedModuleThread& operator=(const RetainedModuleThread&)=delete;
    // Intentionally trivial: no thread destructor or loader-lock cleanup.
    ~RetainedModuleThread()=default;
    bool joinable()const noexcept {return Thread().joinable();}
    template<class Function,class... Args>
    void Start(Function&& function,Args&&... args){
        Thread()=std::thread(std::forward<Function>(function),std::forward<Args>(args)...);
    }
    void join(){Thread().join();}
private:
    std::thread& Thread()noexcept {return *std::launder(reinterpret_cast<std::thread*>(storage_));}
    const std::thread& Thread()const noexcept {return *std::launder(reinterpret_cast<const std::thread*>(storage_));}
    alignas(std::thread) unsigned char storage_[sizeof(std::thread)];
};
}
