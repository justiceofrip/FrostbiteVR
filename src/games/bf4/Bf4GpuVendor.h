#pragma once
// Reads the primary adapter's PCI vendor and, for stereo probes only, optionally makes
// DXGI report an NVIDIA adapter so BF4 takes its 3D Vision path on AMD/Intel (the same
// technique DXVK uses for its NVAPI layer). Opt-in per session, never on by default.
namespace fvr::bf4 {

struct GpuVendorResult {
    unsigned realVendorId = 0;
    bool spoofInstalled = false;
};

// Must run before the game enumerates adapters (the dxgi proxy guarantees that ordering).
// MinHook must be initialised.
GpuVendorResult QueryGpuVendor(bool installNvidiaSpoof);

} // namespace fvr::bf4
