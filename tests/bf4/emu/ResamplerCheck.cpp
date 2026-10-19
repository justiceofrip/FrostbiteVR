// GPU check of the world lane's eye resampler (the adapter's runtime-compiled shader): a
// flat grey frame resampled into two eyes, once plain and once with the aim reticle at the
// frame centre. Runs on the emulator's device (WARP in CI).
#include "../../../src/games/bf4/Bf4EyeResampler.h"

#include <d3d11.h>

#include <cstdint>
#include <cstdlib>
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
    // Depth stereo: a striped frame (red = 4 x column) whose left half is 1 m away and right
    // half at the far plane. Each eye samples near things shifted its own way, far ones not.
    {
        std::vector<std::uint32_t> stripes(kSize * kSize);
        for (UINT y = 0; y < kSize; ++y)
            for (UINT x = 0; x < kSize; ++x) stripes[y * kSize + x] = 0xFF808000u | (x * 4);
        const D3D11_SUBRESOURCE_DATA stripeData{stripes.data(), kSize * 4, 0};
        ComPtr<ID3D11Texture2D> striped;
        if (FAILED(device->CreateTexture2D(&d, &stripeData, &striped))) return why = "create the striped frame", false;
        const float n = 0.1f, f = 1000.0f;
        const float nearValue = f * (1 - n / 1.0f) / (f - n); // standard depth of 1 m
        std::vector<float> depthValues(kSize * kSize);
        for (UINT y = 0; y < kSize; ++y)
            for (UINT x = 0; x < kSize; ++x) depthValues[y * kSize + x] = x < kSize / 2 ? nearValue : 1.0f;
        D3D11_TEXTURE2D_DESC dd = d;
        dd.Format = DXGI_FORMAT_R32_TYPELESS;
        const D3D11_SUBRESOURCE_DATA depthData{depthValues.data(), kSize * 4, 0};
        ComPtr<ID3D11Texture2D> depthTexture;
        if (FAILED(device->CreateTexture2D(&dd, &depthData, &depthTexture))) return why = "create the depth", false;
        fvr::bf4::DepthStereoInput depth;
        depth.texture = depthTexture.Get();
        depth.format = DXGI_FORMAT_R32_TYPELESS;
        depth.params.enabled = true;
        depth.params.shift[0] = -0.05f;
        depth.params.shift[1] = 0.05f;
        depth.params.nearZ = n;
        depth.params.farZ = f;
        if (!resampler.Render(context.Get(), striped.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, nullptr, &depth))
            return why = "depth stereo resample", false;
        std::vector<std::uint32_t> left, right;
        if (!ReadPixels(device, context.Get(), resampler.Eye(0), left, width) ||
            !ReadPixels(device, context.Get(), resampler.Eye(1), right, width))
            return why = "read back the depth stereo eyes", false;
        const int nearLeft = left[32 * width + 16] & 0xFF, nearRight = right[32 * width + 16] & 0xFF;
        const int farLeft = left[32 * width + 48] & 0xFF, farRight = right[32 * width + 48] & 0xFF;
        // 0.05 uv = 3.2 columns = ~13 red steps each way at 1 m; nothing at 1000 m.
        if (!(nearLeft < 66 - 8 && nearRight > 66 + 8 && std::abs(farLeft - farRight) <= 2))
            return why = "depth stereo shifts near things per eye and leaves far ones", false;
    }
    // HUD corners: a blue ammo panel in the frame's bottom-right corner (outside the eyes'
    // +-1.2 tangents) shows up in the inset inside each eye's lower right, nowhere else.
    {
        std::vector<std::uint32_t> hud(kSize * kSize, 0xFF808080u);
        for (UINT y = 52; y < kSize; ++y)
            for (UINT x = 52; x < kSize; ++x) hud[y * kSize + x] = 0xFFFF0000u; // blue (ABGR)
        const D3D11_SUBRESOURCE_DATA hudData{hud.data(), kSize * 4, 0};
        ComPtr<ID3D11Texture2D> hudFrame;
        if (FAILED(device->CreateTexture2D(&d, &hudData, &hudFrame))) return why = "create the HUD frame", false;
        fvr::bf4::EyeOverlay corners[2];
        for (auto& o : corners) {
            o.fov = {-1.2f, 1.2f, 1.2f, -1.2f};
            o.hudInsets = true;
        }
        fvr::bf4::EyeCrop centre[2];
        for (auto& c : centre) { // the eyes see the middle of the frame
            c.u0 = c.v0 = 0.25f;
            c.u1 = c.v1 = 0.75f;
        }
        if (!resampler.Render(context.Get(), hudFrame.Get(), centre, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, corners))
            return why = "HUD corners resample", false;
        auto blue = [](std::uint32_t p) { return ((p >> 16) & 0xFF) > 200 && (p & 0xFF) < 60; };
        for (int e = 0; e < 2; ++e) {
            if (!ReadPixels(device, context.Get(), resampler.Eye(e), eye, width)) return why = "read back the HUD eye", false;
            // Inset centre: tangents (0.64, -0.78) -> eye uv (0.767, 0.825).
            if (!blue(eye[52 * width + 49])) return why = "ammo panel shown in the lower right inset", false;
            if (blue(eye[32 * width + 32]) || blue(eye[52 * width + 14])) return why = "HUD corners only in their insets", false;
        }
    }
    // Depth stereo from a 4x multisampled depth buffer (BF4 with deferred MSAA): sample 0.
    {
        UINT quality = 0;
        if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, 4, &quality)) && quality > 0) {
            std::vector<std::uint32_t> stripes(kSize * kSize);
            for (UINT y = 0; y < kSize; ++y)
                for (UINT x = 0; x < kSize; ++x) stripes[y * kSize + x] = 0xFF808000u | (x * 4);
            const D3D11_SUBRESOURCE_DATA stripeData{stripes.data(), kSize * 4, 0};
            ComPtr<ID3D11Texture2D> striped;
            if (FAILED(device->CreateTexture2D(&d, &stripeData, &striped))) return why = "create the striped frame (MSAA)", false;
            D3D11_TEXTURE2D_DESC md = d;
            md.Format = DXGI_FORMAT_R32_TYPELESS;
            md.SampleDesc.Count = 4;
            md.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> msDepth;
            ComPtr<ID3D11DepthStencilView> msView;
            D3D11_DEPTH_STENCIL_VIEW_DESC dv{};
            dv.Format = DXGI_FORMAT_D32_FLOAT;
            dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
            if (FAILED(device->CreateTexture2D(&md, nullptr, &msDepth)) ||
                FAILED(device->CreateDepthStencilView(msDepth.Get(), &dv, &msView)))
                return why = "create the MSAA depth", false;
            const float n = 0.1f, f = 1000.0f;
            context->ClearDepthStencilView(msView.Get(), D3D11_CLEAR_DEPTH, f * (1 - n / 1.0f) / (f - n), 0); // all at 1 m
            fvr::bf4::DepthStereoInput depth;
            depth.texture = msDepth.Get();
            depth.format = DXGI_FORMAT_R32_TYPELESS;
            depth.params.enabled = true;
            depth.params.shift[0] = -0.05f;
            depth.params.shift[1] = 0.05f;
            depth.params.nearZ = n;
            depth.params.farZ = f;
            if (!resampler.Render(context.Get(), striped.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, nullptr, &depth))
                return why = "MSAA depth stereo resample", false;
            std::vector<std::uint32_t> left, right;
            if (!ReadPixels(device, context.Get(), resampler.Eye(0), left, width) ||
                !ReadPixels(device, context.Get(), resampler.Eye(1), right, width))
                return why = "read back the MSAA depth stereo eyes", false;
            const int l = left[32 * width + 32] & 0xFF, r = right[32 * width + 32] & 0xFF;
            if (!(l < 130 - 8 && r > 130 + 8)) return why = "MSAA depth stereo shifts each eye its way", false;
        }
    }
    return true;
}
