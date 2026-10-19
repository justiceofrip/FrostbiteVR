#include "Bf4EyeResampler.h"

#include <d3dcompiler.h>

#include <cstring>

namespace fvr::bf4 {

using Microsoft::WRL::ComPtr;

namespace {
constexpr char kShader[] = R"(
Texture2D<float4> source : register(t0);
Texture2D<float> sceneDepth : register(t1);
SamplerState linearClamp : register(s0);
SamplerState pointClamp : register(s1);
cbuffer Crop : register(b0) {
    float4 rect;    // u0 v0 u1 v1
    float4 bounds;  // bu0 - bu1 -
    float4 reticle; // u v show -
    float4 scale;   // frame uv -> tangent units: x y
    float4 marker0; // eye image u v, show, kind
    float4 marker1;
    float4 span;    // eye image uv -> tangent units: x y
    float4 eyeFov;  // left right up down (tangents)
    float4 comfort; // vignette strength, -, -, -
    float4 stereo;  // depth stereo: shift (frame uv x world units), near, far, mode (0 off, 1, 2 reversed)
    float4 stereo2; // min depth, HUD insets on, -, -
    float4 insetSrc[2]; // HUD corner sources (frame uv rect)
    float4 insetTan[2]; // and where they go (view tangents: left top right bottom)
};
// A HUD corner copied into its inset, with a thin dark frame; c unchanged outside it.
float3 Inset(float3 c, float2 t, float4 src, float4 dst) {
    const float2 f = float2((t.x - dst.x) / (dst.z - dst.x), (dst.y - t.y) / (dst.y - dst.w));
    if (any(f < 0) || any(f > 1)) return c;
    const float2 edge = min(f, 1 - f) * float2(dst.z - dst.x, dst.y - dst.w);
    if (min(edge.x, edge.y) < 0.006) return float3(0.08, 0.09, 0.11);
    return source.SampleLevel(linearClamp, lerp(src.xy, src.zw, f), 0).rgb;
}
float LinearZ(float d) {
    const float n = stereo.y, f = stereo.z;
    const float denom = stereo.w > 1.5 ? n + d * (f - n) : f - d * (f - n);
    return denom > 0 ? n * f / denom : f;
}
float3 Marker(float3 c, float2 uv, float4 m) {
    if (m.z < 0.5) return c;
    const float d = length((uv - m.xy) * span.xy);
    const float ring = 1 - smoothstep(0.003, 0.0055, abs(d - 0.03));
    const float shade = 1 - smoothstep(0.004, 0.010, abs(d - 0.03));
    const float3 colour = m.w > 0.5 ? float3(0.25, 1.0, 0.35) : float3(1.0, 0.72, 0.15);
    c = lerp(c, c * 0.35, shade * 0.5);
    return lerp(c, colour, ring * 0.85);
}
struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint n : SV_VertexID) {
    const float2 p[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    V o;
    o.position = float4(p[n], 0, 1);
    o.uv = float2((p[n].x + 1) * 0.5, (1 - p[n].y) * 0.5);
    return o;
}
float4 ps(V v) : SV_Target {
    const float2 s0 = float2(lerp(rect.x, rect.z, v.uv.x), lerp(rect.y, rect.w, v.uv.y));
    float2 s = s0;
    if (stereo.w > 0.5) { // depth stereo: this eye's parallax, by fixed-point search on depth
        [unroll] for (int i = 0; i < 4; ++i) {
            const float z = max(LinearZ(sceneDepth.SampleLevel(pointClamp, s, 0)), stereo2.x);
            s.x = s0.x + stereo.x / z;
        }
    }
    if (s.x < bounds.x || s.x > bounds.z || s.y < 0 || s.y > 1) return float4(0, 0, 0, 1); // not rendered
    float3 c = source.SampleLevel(linearClamp, s, 0).rgb;
    if (reticle.z > 0.5) { // aim reticle: a ring and a dot, sized in tangent units (~0.6 deg)
        const float d = length((s0 - reticle.xy) * scale.xy); // at infinity: no parallax
        const float ring = 1 - smoothstep(0.0012, 0.0024, abs(d - 0.011));
        const float centre = 1 - smoothstep(0.0016, 0.0028, d);
        const float shade = 1 - smoothstep(0.0020, 0.0045, abs(d - 0.011)); // dark rim for contrast
        c = lerp(c, c * 0.35, shade * 0.6);
        c = lerp(c, float3(0.25, 1.0, 0.35), max(ring, centre) * 0.9);
    }
    c = Marker(c, v.uv, marker0);
    c = Marker(c, v.uv, marker1);
    const float2 t = float2(lerp(eyeFov.x, eyeFov.y, v.uv.x), lerp(eyeFov.z, eyeFov.w, v.uv.y));
    if (comfort.x > 0.001) { // comfort vignette: clear to ~35 deg off centre, dark by ~50 deg
        c *= 1 - comfort.x * 0.9 * smoothstep(0.70, 1.19, length(t));
    }
    if (stereo2.y > 0.5) { // HUD corners stay readable over the vignette
        c = Inset(c, t, insetSrc[0], insetTan[0]);
        c = Inset(c, t, insetSrc[1], insetTan[1]);
    }
    return float4(c, 1);
}
)";

// 8-bit colour families: typeless storage plus a UNORM view keeps the bits as they are.
DXGI_FORMAT Typeless(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    default: return f;
    }
}
DXGI_FORMAT Unorm(DXGI_FORMAT f) {
    switch (Typeless(f)) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: return f;
    }
}
bool EightBit(DXGI_FORMAT f) { return Typeless(f) != f || f == DXGI_FORMAT_R8G8B8A8_TYPELESS || f == DXGI_FORMAT_B8G8R8A8_TYPELESS; }

struct StateScope {
    ID3D11DeviceContext1* context;
    ComPtr<ID3DDeviceContextState> previous;
    StateScope(ID3D11DeviceContext1* c, ID3DDeviceContextState* s) : context(c) {
        c->SwapDeviceContextState(s, &previous);
        c->ClearState();
    }
    ~StateScope() {
        context->ClearState();
        context->SwapDeviceContextState(previous.Get(), nullptr);
    }
};

struct alignas(16) CropConstants {
    float rect[4];
    float bounds[4];
    float reticle[4];
    float scale[4];
    float marker[2][4];
    float span[4];
    float eyeFov[4];
    float comfort[4];
    float stereo[4];
    float stereo2[4];
    float insetSrc[2][4];
    float insetTan[2][4];
};

// Shader view of a typeless depth family.
DXGI_FORMAT DepthViewFormat(DXGI_FORMAT typeless) {
    switch (typeless) {
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
} // namespace

bool EyeResampler::Initialize(ID3D11DeviceContext* context) {
    context->GetDevice(&device_);
    ComPtr<ID3D11Device1> device1;
    if (!device_ || device_->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_0 || FAILED(device_.As(&device1)) ||
        FAILED(context->QueryInterface(IID_PPV_ARGS(&context_))))
        return Fail(1, E_NOINTERFACE);
    const D3D_FEATURE_LEVEL level = device_->GetFeatureLevel();
    D3D_FEATURE_LEVEL chosen{};
    const UINT flags = (device_->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                           ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
    HRESULT hr = device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &chosen,
                                                   &state_);
    if (FAILED(hr) || chosen != level) return Fail(2, FAILED(hr) ? hr : E_FAIL);
    ComPtr<ID3DBlob> code, errors;
    hr = D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, "vs", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS,
                    0, &code, &errors);
    if (FAILED(hr) || FAILED(hr = device_->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs_)))
        return Fail(3, hr);
    code.Reset();
    errors.Reset();
    hr = D3DCompile(kShader, sizeof(kShader) - 1, nullptr, nullptr, nullptr, "ps", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS,
                    0, &code, &errors);
    if (FAILED(hr) || FAILED(hr = device_->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps_)))
        return Fail(4, hr);
    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(hr = device_->CreateSamplerState(&s, &sampler_))) return Fail(5, hr);
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // depth: never blend across an edge
    if (FAILED(hr = device_->CreateSamplerState(&s, &pointSampler_))) return Fail(5, hr);
    D3D11_RASTERIZER_DESC r{};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.DepthClipEnable = TRUE;
    if (FAILED(hr = device_->CreateRasterizerState(&r, &raster_))) return Fail(6, hr);
    D3D11_BUFFER_DESC b{};
    b.ByteWidth = sizeof(CropConstants);
    b.Usage = D3D11_USAGE_DEFAULT;
    b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    for (auto& c : constants_)
        if (FAILED(hr = device_->CreateBuffer(&b, nullptr, &c))) return Fail(7, hr);
    initialized_ = true;
    return true;
}

void EyeResampler::Reset() {
    source_.Reset();
    sourceView_.Reset();
    sourceDesc_ = {};
    for (auto& t : targets_) t.Reset();
    for (auto& e : eyes_) e.Reset();
    eyeWidth_ = eyeHeight_ = eyeFormat_ = 0;
    depthView_.Reset();
    depthHeld_.Reset();
    depthSource_ = nullptr;
}

bool EyeResampler::Render(ID3D11DeviceContext* context, ID3D11Texture2D* source, const EyeCrop (&crops)[2],
                          std::uint32_t width, std::uint32_t height, std::uint32_t format, const Reticle& reticle,
                          const EyeOverlay* overlays, const DepthStereoInput* depth) {
    if (broken_ || !context || !source || !width || !height || width > 16384 || height > 16384) return false;
    if (!initialized_ && !Initialize(context)) {
        broken_ = true; // no D3D11.1 state objects / shader compiler: the world lane stays off
        return false;
    }
    const DXGI_FORMAT eyeFormat = Typeless(static_cast<DXGI_FORMAT>(format));
    if (!EightBit(eyeFormat)) return Fail(10, E_INVALIDARG);
    HRESULT hr = S_OK;

    // A shader-readable copy of the frame (swapchain buffers need not be).
    D3D11_TEXTURE2D_DESC sd{};
    source->GetDesc(&sd);
    if (!source_ || sd.Width != sourceDesc_.Width || sd.Height != sourceDesc_.Height || sd.Format != sourceDesc_.Format) {
        source_.Reset();
        sourceView_.Reset();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = sd.Width;
        d.Height = sd.Height;
        d.MipLevels = d.ArraySize = 1;
        d.Format = Typeless(sd.Format);
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(hr = device_->CreateTexture2D(&d, nullptr, &source_))) return Fail(11, hr);
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = Unorm(sd.Format);
        v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        v.Texture2D.MipLevels = 1;
        if (FAILED(hr = device_->CreateShaderResourceView(source_.Get(), &v, &sourceView_))) return Fail(12, hr);
        sourceDesc_ = sd;
    }
    if (!eyes_[0] || width != eyeWidth_ || height != eyeHeight_ || static_cast<std::uint32_t>(eyeFormat) != eyeFormat_) {
        for (int i = 0; i < 2; ++i) {
            targets_[i].Reset();
            eyes_[i].Reset();
            D3D11_TEXTURE2D_DESC d{};
            d.Width = width;
            d.Height = height;
            d.MipLevels = d.ArraySize = 1;
            d.Format = eyeFormat;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(hr = device_->CreateTexture2D(&d, nullptr, &eyes_[i]))) return Fail(13, hr);
            D3D11_RENDER_TARGET_VIEW_DESC t{};
            t.Format = Unorm(eyeFormat);
            t.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            if (FAILED(hr = device_->CreateRenderTargetView(eyes_[i].Get(), &t, &targets_[i]))) return Fail(14, hr);
        }
        eyeWidth_ = width;
        eyeHeight_ = height;
        eyeFormat_ = static_cast<std::uint32_t>(eyeFormat);
    }

    // Copies and draws: the copy runs in the game's own state (it binds nothing); the draws
    // run in ours, swapped in and out around them.
    if (sd.SampleDesc.Count > 1) context_->ResolveSubresource(source_.Get(), 0, source, 0, Unorm(sd.Format));
    else context_->CopyResource(source_.Get(), source);
    bool stereo = false;
    if (depth && depth->texture && depth->params.enabled) {
        if (depth->texture != depthSource_) {
            depthView_.Reset();
            depthHeld_.Reset();
            depthSource_ = nullptr;
            D3D11_TEXTURE2D_DESC dd{};
            depth->texture->GetDesc(&dd);
            const DXGI_FORMAT view = DepthViewFormat(dd.Format);
            D3D11_SHADER_RESOURCE_VIEW_DESC v{};
            v.Format = view;
            v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            v.Texture2D.MipLevels = 1;
            if (view != DXGI_FORMAT_UNKNOWN && dd.SampleDesc.Count == 1 &&
                SUCCEEDED(device_->CreateShaderResourceView(depth->texture, &v, &depthView_))) {
                depthSource_ = depth->texture;
                depthHeld_ = depth->texture;
            }
        }
        stereo = depthView_ != nullptr;
    }
    for (int i = 0; i < 2; ++i) {
        CropConstants c{{crops[i].u0, crops[i].v0, crops[i].u1, crops[i].v1},
                        {crops[i].bu0, 0, crops[i].bu1, 1},
                        {reticle.u, reticle.v, reticle.show ? 1.0f : 0.0f, 0},
                        {reticle.scaleX, reticle.scaleY, 0, 0},
                        {},
                        {1, 1, 0, 0}};
        if (overlays) {
            for (int m = 0; m < 2; ++m) {
                const EyeMarker& k = overlays[i].markers[m];
                c.marker[m][0] = k.u;
                c.marker[m][1] = k.v;
                c.marker[m][2] = k.show ? 1.0f : 0.0f;
                c.marker[m][3] = static_cast<float>(k.kind);
            }
            c.span[0] = overlays[i].spanX;
            c.span[1] = overlays[i].spanY;
            const auto& fov = overlays[i].fov;
            c.eyeFov[0] = fov.left;
            c.eyeFov[1] = fov.right;
            c.eyeFov[2] = fov.up;
            c.eyeFov[3] = fov.down;
            c.comfort[0] = overlays[i].vignette;
            if (overlays[i].hudInsets) {
                c.stereo2[1] = 1;
                const HudInset* insets[2] = {&kAmmoInset, &kMinimapInset};
                for (int k = 0; k < 2; ++k)
                    for (int n = 0; n < 4; ++n) {
                        c.insetSrc[k][n] = insets[k]->src[n];
                        c.insetTan[k][n] = insets[k]->tan[n];
                    }
            }
        }
        if (stereo) {
            c.stereo[0] = depth->params.shift[i];
            c.stereo[1] = depth->params.nearZ;
            c.stereo[2] = depth->params.farZ;
            c.stereo[3] = depth->params.reversed ? 2.0f : 1.0f;
            c.stereo2[0] = depth->params.minDepth;
        }
        context_->UpdateSubresource(constants_[i].Get(), 0, nullptr, &c, 0, 0);
    }
    {
        StateScope scope(context_.Get(), state_.Get());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vs_.Get(), nullptr, 0);
        context_->PSSetShader(ps_.Get(), nullptr, 0);
        context_->RSSetState(raster_.Get());
        ID3D11SamplerState* samplers[] = {sampler_.Get()};
        context_->PSSetSamplers(0, 1, samplers);
        ID3D11SamplerState* points[] = {pointSampler_.Get()};
        context_->PSSetSamplers(1, 1, points);
        ID3D11ShaderResourceView* views[] = {sourceView_.Get()};
        context_->PSSetShaderResources(0, 1, views);
        ID3D11ShaderResourceView* depthViews[] = {stereo ? depthView_.Get() : nullptr};
        context_->PSSetShaderResources(1, 1, depthViews);
        const D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        context_->RSSetViewports(1, &viewport);
        for (int i = 0; i < 2; ++i) {
            ID3D11RenderTargetView* target = targets_[i].Get();
            context_->OMSetRenderTargets(1, &target, nullptr);
            ID3D11Buffer* buffers[] = {constants_[i].Get()};
            context_->PSSetConstantBuffers(0, 1, buffers);
            context_->Draw(3, 0);
        }
    }
    stage_ = 0;
    error_ = S_OK;
    return true;
}

} // namespace fvr::bf4
