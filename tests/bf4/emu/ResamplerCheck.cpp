// GPU check of the world lane's eye resampler (the adapter's runtime-compiled shader): a
// flat grey frame resampled into two eyes, once plain and once with the aim reticle at the
// frame centre. Runs on the emulator's device (WARP in CI).
#include "../../../src/games/bf4/Bf4EyeResampler.h"

#include <d3d11.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;

bool ReadPixels(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
                std::vector<std::uint32_t>& out, UINT& width) {
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&d, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    out.resize(std::size_t(d.Width) * d.Height);
    for (UINT y = 0; y < d.Height; ++y)
        std::memcpy(&out[std::size_t(y) * d.Width], static_cast<const std::uint8_t*>(m.pData) + std::size_t(y) * m.RowPitch,
                    d.Width * 4);
    context->Unmap(staging.Get(), 0);
    width = d.Width;
    return true;
}

bool Grey(std::uint32_t p) {
    const int r = p & 0xFF, g = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF;
    return r > 110 && r < 145 && g > 110 && g < 145 && b > 110 && b < 145;
}
bool Amber(std::uint32_t p) {
    const int r = p & 0xFF, g = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF;
    return r > 200 && g > 130 && g < 200 && b < 90;
}
bool Green(std::uint32_t p) {
    const int r = p & 0xFF, g = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF;
    return g > 200 && r < 120 && b < 140;
}
} // namespace

// Returns false with `why` set when any expectation fails.
bool CheckEyeResamplerReticle(ID3D11Device* device, const char*& why) {
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    constexpr UINT kSize = 64;
    std::vector<std::uint32_t> grey(kSize * kSize, 0xFF808080u);
    D3D11_TEXTURE2D_DESC d{};
    d.Width = d.Height = kSize;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA init{grey.data(), kSize * 4, 0};
    ComPtr<ID3D11Texture2D> frame;
    if (FAILED(device->CreateTexture2D(&d, &init, &frame))) return why = "create the frame", false;
    fvr::bf4::EyeResampler resampler;
    fvr::bf4::EyeCrop crops[2]; // the whole frame for both eyes
    std::vector<std::uint32_t> eye;
    UINT width = 0;
    if (!resampler.Render(context.Get(), frame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM))
        return why = "plain resample (shader compile / state)", false;
    if (!ReadPixels(device, context.Get(), resampler.Eye(0), eye, width)) return why = "read back the plain eye", false;
    if (!Grey(eye[32 * width + 32]) || !Grey(eye[2 * width + 2])) return why = "plain eye is the frame", false;
    // The frame spans 0.05 tangent units: the ring (0.011) is ~14 px from the centre.
    fvr::bf4::Reticle reticle;
    reticle.show = true;
    reticle.scaleX = reticle.scaleY = 0.05f;
    if (!resampler.Render(context.Get(), frame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, reticle))
        return why = "reticle resample", false;
    for (int e = 0; e < 2; ++e) {
        if (!ReadPixels(device, context.Get(), resampler.Eye(e), eye, width)) return why = "read back the reticle eye", false;
        if (!Green(eye[32 * width + 32])) return why = "reticle dot at the aim point", false;
        if (!Green(eye[32 * width + 32 + 14]) && !Green(eye[32 * width + 32 + 15])) return why = "reticle ring", false;
        if (!Grey(eye[2 * width + 2]) || !Grey(eye[32 * width + 32 + 7])) return why = "frame untouched away from the reticle", false;
    }
    // A gesture marker in each eye's own image position: an amber ring of 0.03 tangent units
    // around (0.25, 0.5); with an eye span of 0.2 that is ~9.6 px. No reticle this time.
    fvr::bf4::EyeOverlay overlays[2];
    for (auto& o : overlays) {
        o.spanX = o.spanY = 0.2f;
        o.markers[0].show = true;
        o.markers[0].u = 0.25f;
        o.markers[0].v = 0.5f;
    }
    if (!resampler.Render(context.Get(), frame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, overlays))
        return why = "marker resample", false;
    if (!ReadPixels(device, context.Get(), resampler.Eye(1), eye, width)) return why = "read back the marker eye", false;
    if (!Amber(eye[32 * width + 16 + 10]) && !Amber(eye[32 * width + 16 + 9])) return why = "marker ring", false;
    if (!Grey(eye[32 * width + 16]) || !Grey(eye[32 * width + 48]) || Green(eye[32 * width + 32]))
        return why = "marker only at its place, no reticle", false;
    // Comfort vignette at full strength over a +-1.6 tangent eye: centre clear, corners dark.
    for (auto& o : overlays) {
        o.markers[0].show = false;
        o.fov = {-1.6f, 1.6f, 1.6f, -1.6f};
        o.vignette = 1;
    }
    if (!resampler.Render(context.Get(), frame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, overlays))
        return why = "vignette resample", false;
    if (!ReadPixels(device, context.Get(), resampler.Eye(0), eye, width)) return why = "read back the vignette eye", false;
    if (!Grey(eye[32 * width + 32])) return why = "vignette leaves the centre clear", false;
    if ((eye[1 * width + 1] & 0xFF) > 30) return why = "vignette darkens the corners", false;
    return true;
}
