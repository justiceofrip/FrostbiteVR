#pragma once
// The launcher publishes a small key=value block in a named shared-memory section
// ("Local\FrostbiteVR_BF4_Session"). The dxgi proxy reads it to decide whether to load
// the adapter at all; if the section is absent the proxy is a pure pass-through.
// This keeps plain (non-VR) launches - including multiplayer - completely untouched.
#include "fvr/interaction/ReloadPolicy.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fvr::bf4 {

inline constexpr int kSessionProtocol = 1;
inline constexpr wchar_t kSessionSectionName[] = L"Local\\FrostbiteVR_BF4_Session";
inline constexpr std::size_t kSessionSectionBytes = 4096;

enum class StereoSetting : std::uint8_t { Off, Probe, On };
// How BF4's own stereo renderer is switched on (both verified in bf4.exe, see BF4-STEREO.md):
//   Split: RenderDevice.StereoEnable + StereoSplitScreenEnable; the engine draws both eyes
//          side by side into the backbuffer itself. No driver involved, any GPU.
//   Nvapi: StereoEnable without split screen; needs a 3D Vision driver (emulated shim).
enum class StereoRoute : std::uint8_t { Split, Nvapi };

struct SessionInfo {
    int protocol = 0;
    std::string adapterPath;   // absolute path to FrostbiteVR.Bf4.dll in the install
    std::string logDir;
    std::string exeSha256;     // computed by the launcher, lowercase hex
    std::string hostPipe;      // XR host IPC name
    interaction::ReloadMode reloadMode = interaction::ReloadMode::ButtonPress;
    bool developerFeatures = false; // allow Candidate features
    StereoSetting stereo = StereoSetting::Off; // engine stereo on/probe/off
    StereoRoute stereoRoute = StereoRoute::Split;
    float stereoSeparation = 100.0f;            // percent reported to the engine
    bool spoofGpuVendor = false;                // report an NVIDIA adapter (probe on AMD/Intel)
    unsigned long launcherPid = 0;
    // Developer research only: write one read-only copy of the decrypted in-memory
    // bf4.exe image here (for offline RE; never packaged or committed). Empty = off.
    std::string researchDumpDir;
};

// Lines "key=value", '#' comments, unknown keys ignored (forward compatible).
std::optional<SessionInfo> ParseSessionInfo(std::string_view text, std::string* error = nullptr);

} // namespace fvr::bf4
