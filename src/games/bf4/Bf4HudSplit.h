#pragma once
// BF4 composites its scene into the swapchain buffer (a draw whose pixel shader reads the
// render-resolution target) and then draws the HUD straight onto it (draws reading the UI
// atlases), all on the immediate context (live, 2026-10-20, research `drawprobe`). This
// listener (Bf4ContextHooks.h) copies the buffer just before the first HUD draw of a frame:
// the scene without the HUD. At Present the head-tracked view can then take a box around the
// frame's centre from that copy: BF4's crosshair and hit marker sit there, at the head's centre,
// meaningless in VR (the barrel shows the aim; the player wants no crosshair). The rest of the
// HUD stays. Only our copy and that box of the presented image are written.

#include "Bf4ContextHooks.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace fvr::bf4 {

class HudSplit : public ContextHooks::Listener {
public:
    struct Stats {
        std::uint64_t frames = 0;     // Presents seen
        std::uint64_t copies = 0;     // frames whose scene was copied before the HUD
        std::uint64_t restores = 0;   // centre boxes put back
        std::uint64_t noComposite = 0; // frames without a recognised composite
    };
    ~HudSplit() override;
    bool Install(ID3D11DeviceContext* immediate);
    void Remove();
    bool Installed() const { return installed_; }
    // Each Present, before the frame is captured: the current back buffer (it may change with
    // the swap chain); with `restoreCentre` the box (fraction of the height, square) around the
    // centre is copied back from the scene. Re-arms for the next frame.
    void OnPresent(ID3D11Texture2D* backBuffer, bool restoreCentre, float boxFraction = 0.10f);
    Stats GetStats() const;
    std::string Describe() const;

    void OnDraw(ID3D11DeviceContext*, const char*, UINT, UINT) override;
    void OnSetRenderTarget(ID3D11DeviceContext*, ID3D11RenderTargetView* first) override;

private:
    bool EnsureCopy(ID3D11Texture2D* back);
    bool installed_ = false;
    ID3D11DeviceContext* immediate_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_;
    D3D11_TEXTURE2D_DESC copyDesc_{};
    ID3D11Resource* back_ = nullptr; // the current back buffer (not owned; refreshed each Present)
    bool targetIsBack_ = false;      // the immediate context draws into it now
    bool compositeSeen_ = false, copied_ = false;
    mutable std::mutex mutex_;
    Stats stats_;
};

} // namespace fvr::bf4
