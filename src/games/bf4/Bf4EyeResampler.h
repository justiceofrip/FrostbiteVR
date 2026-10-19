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

class EyeResampler {
public:
    // source: the frame to read (the backbuffer; MSAA is resolved). width/height/format: the
    // host's eye requirements (format RGBA8 or BGRA8, any of their typeless/UNORM/sRGB forms).
    // reticle: drawn over both eyes at its frame position (a mono frame: same direction).
    // overlays: optional per-eye gesture markers (eye image positions).
    bool Render(ID3D11DeviceContext* context, ID3D11Texture2D* source, const EyeCrop (&crops)[2], std::uint32_t width,
                std::uint32_t height, std::uint32_t format, const Reticle& reticle = {},
                const EyeOverlay* overlays = nullptr);
    ID3D11Texture2D* Eye(int eye) const { return eyes_[eye & 1].Get(); }
    void Reset(); // size-dependent resources (swapchain resize); shaders stay
    HRESULT LastError() const { return error_; }
    unsigned LastStage() const { return stage_; }

private:
    bool Initialize(ID3D11DeviceContext* context);
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
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_[2];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> eyes_[2];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targets_[2];
    D3D11_TEXTURE2D_DESC sourceDesc_{};
    std::uint32_t eyeWidth_ = 0, eyeHeight_ = 0, eyeFormat_ = 0;
    bool initialized_ = false, broken_ = false;
    HRESULT error_ = S_OK;
    unsigned stage_ = 0;
};

} // namespace fvr::bf4
