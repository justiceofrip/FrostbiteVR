#pragma once
// Eye images for the BF4 world lane: the part of the game frame each eye covers (EyeCrop,
// Bf4WorldView.h), resampled to the XR host's exact eye size and copy format family, which
// D3D11FrameBridge::CaptureEye requires. Raw bits are kept (8-bit families are read and
// written as UNORM views of typeless textures: no sRGB conversion either way).
//
// Runs on the game's immediate context, on the thread that presents, inside its own device
// context state: SwapDeviceContextState takes the game's whole pipeline state out and puts
// it back unchanged, so no game binding is ever overwritten or left behind.
#include "Bf4WorldView.h"

#include <d3d11_1.h>
#include <wrl/client.h>

#include <cstdint>

namespace fvr::bf4 {

// The frame's depth for depth stereo: a texture of a typeless depth family (R24G8, R32G8X24,
// R32 or R16), single-sample, any size (sampled in frame uv).
struct DepthStereoInput {
    ID3D11Texture2D* texture = nullptr;
    std::uint32_t format = 0;
    DepthStereoParams params;
};

// Alternate-eye stereo: the source frame was rendered from eye `fresh` (the camera at that
// eye). The other eye comes from the frame Keep() kept for it, its rays turned by
// `staleRotation` (the kept frame's camera from this frame's: a pair stays labelled with
// exactly this frame's pose) at the kept frame's tangents; without one it comes from this
// frame too.
struct AlternateEyes {
    int fresh = -1;
    bool useKept = false;
    FrameRotation staleRotation;
    float keptTanX = 0, keptTanY = 0;
};

// Native eyes: BF4 draws its HUD once, flat on the frame. Copied onto the other eye at the same
// pixels it would be seen at infinity, behind everything near that it covers; this shift (in
// frame pixels, for the eye `kept`, the presented one being the other) puts it at `depth`
// metres instead: the disparity of a point straight ahead, half eye distance `halfEye`, in a
// frame `width` pixels wide spanning tangents -tanX..tanX. 0 without valid inputs.
int HudShiftPixels(int kept, float halfEye, float depth, std::uint32_t width, float tanX);

class EyeResampler {
public:
    // source: the frame to read (the backbuffer; MSAA is resolved). width/height/format: the
    // host's eye requirements (format RGBA8 or BGRA8, any of their typeless/UNORM/sRGB forms).
    // reticle: drawn over both eyes at its frame position (a mono frame: same direction).
    // overlays: optional per-eye gesture markers (eye image positions).
    // view: gun follows aim; each eye's rays are rotated into the render (the crops' source
    // regions still bound what was rendered).
    bool Render(ID3D11DeviceContext* context, ID3D11Texture2D* source, const EyeCrop (&crops)[2], std::uint32_t width,
                std::uint32_t height, std::uint32_t format, const Reticle& reticle = {},
                const EyeOverlay* overlays = nullptr, const DepthStereoInput* depth = nullptr,
                const FrameView* view = nullptr, const AlternateEyes* alternate = nullptr);
    // Alternate eyes: keep this frame as `eye`'s (a copy; MSAA is resolved). Every frame of the
    // eye is kept, so the other half of a pair is at most a frame old.
    bool Keep(ID3D11DeviceContext* context, ID3D11Texture2D* source, int eye);
    // Native eyes (BF4 drew the world once per eye this frame, Bf4WorldRenderOverride.h): keep
    // `world`, `eye`'s own render of this frame, as that eye's, with BF4's HUD put over it at the
    // same place. The HUD is drawn over the other eye's render only: its pixels are where `shown`
    // (the presented frame) differs from `scene` (the same frame before its HUD, Bf4HudSplit.h).
    // All three the same size, 8-bit. False (nothing kept) otherwise. `hudShiftPx`: the HUD moved
    // that many pixels to the right (negative: left) in the kept eye, so that it is seen at a
    // finite depth instead of at infinity (HudShiftPixels).
    bool KeepWithHud(ID3D11DeviceContext* context, ID3D11Texture2D* world, ID3D11Texture2D* scene,
                     ID3D11Texture2D* shown, int eye, int hudShiftPx = 0);
    bool HasKept(int eye) const { return (eye == 0 || eye == 1) && keptValid_[eye]; }
    void DropKept() { keptValid_[0] = keptValid_[1] = false; }
    ID3D11Texture2D* Eye(int eye) const { return eyes_[eye & 1].Get(); }
    void Reset(); // size-dependent resources (swapchain resize); shaders stay
    HRESULT LastError() const { return error_; }
    unsigned LastStage() const { return stage_; }

private:
    bool Initialize(ID3D11DeviceContext* context);
    bool CopyToView(ID3D11Texture2D* source, Microsoft::WRL::ComPtr<ID3D11Texture2D>& copy,
                    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view);
    bool Fail(unsigned stage, HRESULT hr) {
        stage_ = stage;
        error_ = hr;
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_, pointSampler_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthView_;
    bool depthMultisampled_ = false; // depthView_ is a Texture2DMS view (read at sample 0)
    ID3D11Texture2D* depthSource_ = nullptr; // what depthView_ views (held by the caller)
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthHeld_; // keeps it alive while viewed
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_[2];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> eyes_[2];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> kept_[2]; // alternate eyes: each eye's last frame
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> keptViews_[2];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> keptTargets_[2]; // native eyes: the HUD drawn into the kept eye
    Microsoft::WRL::ComPtr<ID3D11VertexShader> hudVs_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> hudConstants_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> hudPs_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> hudScene_, hudShown_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hudSceneView_, hudShownView_;
    bool keptValid_[2]{};
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targets_[2];
    D3D11_TEXTURE2D_DESC sourceDesc_{};
    std::uint32_t eyeWidth_ = 0, eyeHeight_ = 0, eyeFormat_ = 0;
    bool initialized_ = false, broken_ = false;
    HRESULT error_ = S_OK;
    unsigned stage_ = 0;
};

} // namespace fvr::bf4
