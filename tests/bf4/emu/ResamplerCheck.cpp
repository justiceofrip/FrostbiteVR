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
        // Gun follows aim: a turned render brings its own HUD corners into the eyes' edges; with
        // the insets on they are drawn dark there. The eye ray at tangent (0.86, -0.82), eye
        // pixel (55, 54), lands on the frame's ammo panel outside its inset.
        fvr::bf4::FrameView turned;
        turned.rotation.enabled = true; // no turn: the frame's corner is simply in view
        turned.tanX = turned.tanY = 1.0f;
        if (!resampler.Render(context.Get(), hudFrame.Get(), centre, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, corners, nullptr,
                              &turned))
            return why = "HUD corners with a turned render", false;
        for (int e = 0; e < 2; ++e) {
            if (!ReadPixels(device, context.Get(), resampler.Eye(e), eye, width)) return why = "read back the turned HUD eye", false;
            if (blue(eye[54 * width + 55])) return why = "turned render: its own HUD corner is not shown a second time", false;
            if (!blue(eye[52 * width + 49])) return why = "turned render: the inset still shows the ammo panel", false;
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
    // Gun follows aim: the frame was rendered turned right by atan(0.25) (frame tangents +-1),
    // so each eye's centre ray lands a quarter tangent left of the frame's centre: column 24
    // (red ~96) instead of 32; the eyes' +-0.5 views stay inside the frame.
    {
        std::vector<std::uint32_t> stripes(kSize * kSize);
        for (UINT y = 0; y < kSize; ++y)
            for (UINT x = 0; x < kSize; ++x) stripes[y * kSize + x] = 0xFF808000u | (x * 4);
        const D3D11_SUBRESOURCE_DATA stripeData{stripes.data(), kSize * 4, 0};
        ComPtr<ID3D11Texture2D> striped;
        if (FAILED(device->CreateTexture2D(&d, &stripeData, &striped))) return why = "create the striped frame (aim)", false;
        const float c = 1.0f / std::sqrt(1.0f + 0.0625f), s = 0.25f * c; // cos, sin of atan(0.25)
        fvr::bf4::CameraAxes head, aim;
        const float right[3] = {c, 0, s}, back[3] = {-s, 0, c};
        for (int k = 0; k < 3; ++k) {
            aim.row[0][k] = right[k];
            aim.row[2][k] = back[k];
        }
        fvr::bf4::FrameView view;
        view.rotation = fvr::bf4::RotationBetween(head, aim);
        view.tanX = view.tanY = 1.0f;
        fvr::bf4::EyeOverlay eyesFov[2];
        for (auto& o : eyesFov) o.fov = {-0.5f, 0.5f, 0.5f, -0.5f};
        if (!resampler.Render(context.Get(), striped.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov, nullptr,
                              &view))
            return why = "aim-turned resample", false;
        for (int e = 0; e < 2; ++e) {
            if (!ReadPixels(device, context.Get(), resampler.Eye(e), eye, width)) return why = "read back the aim-turned eye", false;
            const int centre = eye[32 * width + 32] & 0xFF, edge = eye[32 * width + 0] & 0xFF;
            if (std::abs(centre - 96) > 6) return why = "aim-turned render: the eye's centre ray lands left of the frame's", false;
            if (!(edge > 0 && edge < 40) || (eye[32 * width + 0] >> 24) != 0xFF)
                return why = "aim-turned render: the eye's edge is the frame's far left, not black", false;
        }
        // With depth stereo (everything 1 m away, 5 cm half eye distance): each eye's offset is
        // rotated into the render too; the left eye samples ~0.1 tangent further left.
        std::vector<float> metre(kSize * kSize, 1000.0f * (1 - 0.1f / 1.0f) / (1000.0f - 0.1f));
        D3D11_TEXTURE2D_DESC dd = d;
        dd.Format = DXGI_FORMAT_R32_TYPELESS;
        const D3D11_SUBRESOURCE_DATA metreData{metre.data(), kSize * 4, 0};
        ComPtr<ID3D11Texture2D> metreDepth;
        if (FAILED(device->CreateTexture2D(&dd, &metreData, &metreDepth))) return why = "create the 1 m depth", false;
        fvr::bf4::DepthStereoInput depth;
        depth.texture = metreDepth.Get();
        depth.format = DXGI_FORMAT_R32_TYPELESS;
        depth.params.enabled = true;
        depth.params.nearZ = 0.1f;
        depth.params.farZ = 1000.0f;
        view.halfEye = 0.05f;
        if (!resampler.Render(context.Get(), striped.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov, &depth,
                              &view))
            return why = "aim-turned depth stereo resample", false;
        std::vector<std::uint32_t> leftEye, rightEye;
        if (!ReadPixels(device, context.Get(), resampler.Eye(0), leftEye, width) ||
            !ReadPixels(device, context.Get(), resampler.Eye(1), rightEye, width))
            return why = "read back the aim-turned depth stereo eyes", false;
        const int l = leftEye[32 * width + 32] & 0xFF, r = rightEye[32 * width + 32] & 0xFF;
        if (!(l < r - 6 && l > 70 && r < 115)) return why = "aim-turned depth stereo: each eye's offset rotated into the render", false;
        if (!resampler.Render(context.Get(), striped.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov))
            return why = "plain resample after the aim-turned one", false;
        if (!ReadPixels(device, context.Get(), resampler.Eye(0), eye, width) || std::abs(int(eye[32 * width + 32] & 0xFF) - 130) > 6)
            return why = "without the turn the centre is the frame's centre", false;

        // Alternate eyes: each frame is rendered from one eye and kept as its eye's; the other eye
        // comes from its own kept frame, turned onto this frame's camera. Red from the left,
        // stripes from the right, then green from the left with the head turned since the stripes
        // (yaw atan 0.25).
        std::vector<std::uint32_t> red(kSize * kSize, 0xFF0000C0u), green(kSize * kSize, 0xFF00C000u);
        const D3D11_SUBRESOURCE_DATA redData{red.data(), kSize * 4, 0}, greenData{green.data(), kSize * 4, 0};
        ComPtr<ID3D11Texture2D> redFrame, greenFrame;
        if (FAILED(device->CreateTexture2D(&d, &redData, &redFrame)) || FAILED(device->CreateTexture2D(&d, &greenData, &greenFrame)))
            return why = "create the alternate-eye frames", false;
        auto channel = [&](int e, int shift) {
            return ReadPixels(device, context.Get(), resampler.Eye(e), eye, width) ? int((eye[32 * width + 32] >> shift) & 0xFF) : -1;
        };
        fvr::bf4::AlternateEyes alt;
        alt.fresh = 0;
        alt.useKept = true; // nothing kept yet: both eyes from this frame
        alt.staleRotation.enabled = true;
        alt.keptTanX = alt.keptTanY = 1.0f;
        resampler.DropKept();
        if (!resampler.Keep(context.Get(), redFrame.Get(), 0) ||
            !resampler.Render(context.Get(), redFrame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov, nullptr,
                              nullptr, &alt))
            return why = "alternate eyes: first frame", false;
        if (channel(0, 0) < 180 || channel(1, 0) < 180) return why = "alternate eyes: the first pair is one frame twice", false;
        if (!resampler.HasKept(0) || resampler.HasKept(1)) return why = "alternate eyes: the left frame not kept", false;
        alt.fresh = 1;
        if (!resampler.Keep(context.Get(), striped.Get(), 1) ||
            !resampler.Render(context.Get(), striped.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov, nullptr,
                              nullptr, &alt))
            return why = "alternate eyes: second frame", false;
        if (channel(0, 0) < 180 || channel(0, 8) > 40) return why = "alternate eyes: the left eye is not its kept frame", false;
        if (std::abs(channel(1, 0) - 130) > 6) return why = "alternate eyes: the right eye is not the new frame", false;
        alt.fresh = 0;
        alt.staleRotation = fvr::bf4::RotationBetween(head, aim); // the right frame's camera from this one's
        if (!resampler.Keep(context.Get(), greenFrame.Get(), 0) ||
            !resampler.Render(context.Get(), greenFrame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov, nullptr,
                              nullptr, &alt))
            return why = "alternate eyes: third frame", false;
        if (channel(0, 8) < 180) return why = "alternate eyes: the left eye is not the new frame", false;
        if (std::abs(channel(1, 0) - 96) > 6) return why = "alternate eyes: the kept right frame not turned onto this camera", false;
        alt.useKept = false; // a run restarts: this frame in both eyes
        if (!resampler.Render(context.Get(), redFrame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov, nullptr,
                              nullptr, &alt) ||
            channel(1, 0) < 180)
            return why = "alternate eyes: without a kept frame the other eye shows this one", false;

        // Native eyes: both eyes are this frame's own renders. The left eye's (red) is kept with
        // BF4's HUD put over it where the presented frame differs from the same frame before its
        // HUD (grey): no HUD, then a green square at the centre; the right eye is the presented
        // frame, the pair cut from both with the same camera.
        std::vector<std::uint32_t> hud(kSize * kSize, 0xFF808080u);
        for (UINT y = 28; y < 36; ++y)
            for (UINT x = 28; x < 36; ++x) hud[y * kSize + x] = 0xFF00C000u;
        const D3D11_SUBRESOURCE_DATA hudData{hud.data(), kSize * 4, 0};
        ComPtr<ID3D11Texture2D> hudFrame;
        if (FAILED(device->CreateTexture2D(&d, &hudData, &hudFrame))) return why = "create the HUD frame", false;
        alt.fresh = 1;
        alt.useKept = true;
        alt.staleRotation = {};
        resampler.DropKept();
        if (!resampler.KeepWithHud(context.Get(), redFrame.Get(), frame.Get(), frame.Get(), 0) ||
            !resampler.Render(context.Get(), frame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov, nullptr,
                              nullptr, &alt))
            return why = "native eyes: a pair without a HUD", false;
        if (channel(0, 0) < 180 || channel(0, 8) > 40) return why = "native eyes: the left eye is not its own render", false;
        if (std::abs(channel(1, 0) - 128) > 6) return why = "native eyes: the right eye is not the presented frame", false;
        if (!resampler.KeepWithHud(context.Get(), redFrame.Get(), frame.Get(), hudFrame.Get(), 0) ||
            !resampler.Render(context.Get(), hudFrame.Get(), crops, kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, {}, eyesFov, nullptr,
                              nullptr, &alt))
            return why = "native eyes: a pair with a HUD", false;
        if (channel(0, 8) < 180 || channel(0, 0) > 40) return why = "native eyes: the HUD is not over the left eye", false;
        if (channel(1, 8) < 180) return why = "native eyes: the HUD is not in the right eye", false;
        if (resampler.KeepWithHud(context.Get(), redFrame.Get(), frame.Get(), nullptr, 0))
            return why = "native eyes: kept without the presented frame", false;
        resampler.DropKept();
    }
    return true;
}
