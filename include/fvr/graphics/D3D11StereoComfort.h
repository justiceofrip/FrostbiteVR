#pragma once
#include "fvr/graphics/D3D11SharedPair.h"
#include <memory>
namespace fvr::graphics {
// Host-owned GPU safeguard for the reported extreme black/white eye mismatch.
// This is not death detection or a native postprocess repair. A single 16x16
// sample grid classifies BOTH original eyes; one flag then controls both outputs.
// No CPU readback, native context use, or simulation work is performed.
class D3D11StereoComfort {
public:
    D3D11StereoComfort();
    ~D3D11StereoComfort();
    bool Initialize(ID3D11DeviceContext*);
    // Single-slice, same-size RGBA8/BGRA8 textures on this immediate context.
    // Call before host props; their later composition cannot hide the mismatch.
    bool Prepare(const std::array<TextureSlice,2>&);
    // Validates BOTH destinations before any write. Consumes preparation even
    // on failure. No second call or failed Prepare can reuse an older flag.
    bool Present(const std::array<ID3D11RenderTargetView*,2>&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
