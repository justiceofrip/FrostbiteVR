#pragma once
// Emulated 3D Vision "Direct mode" driver state. Platform-neutral: the nvapi64.dll shim
// is a thin C ABI over this class, and the tests drive it directly.
//
// What BF4 sees: stereo enabled + activated, Direct mode accepted, a stereo handle per
// device, separation/convergence values we choose. What we see: every SetActiveEye call,
// which tells the adapter which eye the engine is rendering right now.
#include "NvapiIds.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace fvr::bf4::stereo {

enum class Eye : std::int8_t { Mono = -1, Left = 0, Right = 1 };
Eye EyeFromNvapi(int nvEye) noexcept; // 1 right, 2 left, 3 mono; anything else -> Mono

class NvapiStereoEmu {
public:
    struct Config {
        float separationPercent = 100.0f; // what GetSeparation reports (engine scales by it)
        float convergence = 1.0f;
        float eyeSeparation = 0.065f;     // GetEyeSeparation: ratio eye distance / screen width
    };

    NvapiStereoEmu() = default;
    explicit NvapiStereoEmu(Config config) : config_(config) {}

    // Whether the shim offers this function at all (nullptr from QueryInterface otherwise).
    static bool Implements(std::uint32_t functionId) noexcept;

    // --- NVAPI surface (return NvAPI_Status) -----------------------------------------
    int Initialize();
    int SetDriverMode(int mode);
    int Enable();
    int Disable();
    int IsEnabled(std::uint8_t* out) const;
    int CreateHandle(void* device, void** outHandle);
    int DestroyHandle(void* handle);
    int Activate(void* handle);
    int Deactivate(void* handle);
    int IsActivated(void* handle, std::uint8_t* out) const;
    int GetSeparation(void* handle, float* out) const;
    int SetSeparation(void* handle, float value);
    int GetConvergence(void* handle, float* out) const;
    int SetConvergence(void* handle, float value);
    int GetEyeSeparation(void* handle, float* out) const;
    int IsWindowedModeSupported(std::uint8_t* out) const;
    int SetSurfaceCreationMode(void* handle, int mode);
    int GetSurfaceCreationMode(void* handle, int* out) const;
    int SetActiveEye(void* handle, int nvEye);

    // --- FrostbiteVR side -----------------------------------------------------------
    // Called on the engine's render thread from inside SetActiveEye.
    void SetEyeCallback(std::function<void(Eye)> callback);
    // Lockdown / user toggle: report "stereo off" so the engine returns to one pass.
    void SetSuppressed(bool suppressed) { suppressed_ = suppressed; }
    bool Suppressed() const { return suppressed_; }

    // Every QueryInterface call; callerRva = return address minus the exe base (0 = unknown),
    // kept from the first query of each ID: it locates the game's NVAPI wrapper for RE.
    void NoteQuery(std::uint32_t functionId, bool answered, std::uint64_t callerRva = 0);
    std::string Report() const; // plain text for the probe

    bool DirectMode() const { return directMode_; }
    bool Activated() const { return activated_ && !suppressed_; }
    Eye ActiveEye() const { return activeEye_; }
    std::uint64_t EyeSwitches() const { return eyeSwitches_; }
    std::size_t HandleCount() const { std::lock_guard lock(mutex_); return handles_.size(); }
    float EngineSeparation() const { return separation_; }

private:
    bool ValidHandle(void* handle) const;

    Config config_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool enabled_ = true; // "enabled in the registry"
    bool directMode_ = false;
    bool activated_ = false;
    std::atomic<bool> suppressed_{false};
    int surfaceMode_ = 0;
    float separation_ = 0.0f;
    float convergence_ = 0.0f;
    Eye activeEye_ = Eye::Mono;
    std::uint64_t eyeSwitches_ = 0;
    std::map<void*, void*> handles_; // handle -> device
    std::uintptr_t nextHandle_ = 0x5EED0000;
    std::function<void(Eye)> onEye_;
    struct Query { std::uint64_t count = 0; bool answered = false; std::uint64_t callerRva = 0; };
    std::map<std::uint32_t, Query> queries_;
};

} // namespace fvr::bf4::stereo
