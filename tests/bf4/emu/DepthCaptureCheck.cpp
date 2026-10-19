// The depth-stereo depth capture on the emulator's device (WARP in CI), with its real
// ClearDepthStencilView detour: it must pick the frame's main depth (16:9) over a bigger
// square shadow map, notice a second clear per frame (Frostbite's first-person pass), and
// keep the depth from just before that clear (the world).
#include "../../../src/games/bf4/Bf4DepthCapture.h"

#include <MinHook.h>
#include <d3d11.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
using Microsoft::WRL::ComPtr;

bool MakeDepth(ID3D11Device* device, UINT w, UINT h, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11DepthStencilView>& view) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R24G8_TYPELESS;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&d, nullptr, &texture))) return false;
    D3D11_DEPTH_STENCIL_VIEW_DESC v{};
    v.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    v.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    return SUCCEEDED(device->CreateDepthStencilView(texture.Get(), &v, &view));
}

// The 24-bit depth at the texture's centre.
bool CentreDepth(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, float& out) {
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&d, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    std::uint32_t raw = 0;
    std::memcpy(&raw, static_cast<const std::uint8_t*>(m.pData) + std::size_t(d.Height / 2) * m.RowPitch + (d.Width / 2) * 4, 4);
    context->Unmap(staging.Get(), 0);
    out = float(raw & 0xFFFFFF) / 16777215.0f;
    return true;
}
} // namespace

bool CheckDepthCapture(ID3D11Device* device, const char*& why) {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return why = "MinHook", false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    ComPtr<ID3D11Texture2D> mainDepth, shadow;
    ComPtr<ID3D11DepthStencilView> mainView, shadowView;
    if (!MakeDepth(device, 64, 36, mainDepth, mainView) || !MakeDepth(device, 128, 128, shadow, shadowView))
        return why = "create depth targets", false;
    bool ok = true;
    {
        fvr::bf4::DepthCapture capture;
        if (!capture.Install(context.Get())) return why = "install the clear observer", false;
        fvr::bf4::DepthCapture::Frame frame;
        // Frostbite-like frames: shadow map, world (cleared, "drawn" = cleared to 0.25 here),
        // then the first-person clear (to 0.75) before the weapon.
        for (int i = 0; i < 12; ++i) {
            context->ClearDepthStencilView(shadowView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
            context->ClearDepthStencilView(mainView.Get(), D3D11_CLEAR_DEPTH, 0.25f, 0);
            context->ClearDepthStencilView(mainView.Get(), D3D11_CLEAR_DEPTH, 0.75f, 0);
            capture.TakeFrame(context.Get(), 16.0f / 9.0f, frame);
        }
        const auto st = capture.GetStats();
        float world = 0;
        if (!(st.width == 64 && st.height == 36)) why = "picks the 16:9 main depth, not the bigger shadow map", ok = false;
        else if (!st.split) why = "notices the second clear (first-person pass)", ok = false;
        else if (!frame.texture || !CentreDepth(device, context.Get(), frame.texture.Get(), world) || std::fabs(world - 0.25f) > 0.01f)
            why = "keeps the world depth from before the first-person clear", ok = false;
        if (!ok) std::printf("%s\n", capture.Describe().c_str());
        capture.Remove();
    }
    if (ok) {
        // One clear a frame: the buffer itself at Present.
        fvr::bf4::DepthCapture capture;
        if (!capture.Install(context.Get())) return why = "install again", false;
        fvr::bf4::DepthCapture::Frame frame;
        for (int i = 0; i < 12; ++i) {
            context->ClearDepthStencilView(mainView.Get(), D3D11_CLEAR_DEPTH, 0.4f, 0);
            capture.TakeFrame(context.Get(), 16.0f / 9.0f, frame);
        }
        float live = 0;
        if (capture.GetStats().split || !frame.texture || !CentreDepth(device, context.Get(), frame.texture.Get(), live) ||
            std::fabs(live - 0.4f) > 0.01f)
            why = "uses the live buffer with one clear a frame", ok = false;
        capture.Remove();
    }
    if (ok) {
        // A multisampled main depth (BF4 with deferred MSAA) is copied as it is.
        UINT quality = 0;
        if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, 4, &quality)) && quality > 0) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = 64;
            d.Height = 36;
            d.MipLevels = d.ArraySize = 1;
            d.Format = DXGI_FORMAT_R24G8_TYPELESS;
            d.SampleDesc.Count = 4;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> ms;
            ComPtr<ID3D11DepthStencilView> msView;
            D3D11_DEPTH_STENCIL_VIEW_DESC v{};
            v.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            v.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &ms)) || FAILED(device->CreateDepthStencilView(ms.Get(), &v, &msView)))
                return why = "create the MSAA depth target", false;
            fvr::bf4::DepthCapture capture;
            if (!capture.Install(context.Get())) return why = "install for MSAA", false;
            fvr::bf4::DepthCapture::Frame frame;
            for (int i = 0; i < 12; ++i) {
                context->ClearDepthStencilView(msView.Get(), D3D11_CLEAR_DEPTH, 0.5f, 0);
                capture.TakeFrame(context.Get(), 16.0f / 9.0f, frame);
            }
            D3D11_TEXTURE2D_DESC got{};
            if (frame.texture) frame.texture->GetDesc(&got);
            if (!frame.texture || got.SampleDesc.Count != 4) why = "hands back a multisampled copy as it is", ok = false;
            // The developer snapshot reads sample 0 of it: 0.5 with planes 0.06/20000 is 0.12 m.
            char temp[MAX_PATH]{};
            GetTempPathA(MAX_PATH, temp);
            const std::string bmp = std::string(temp) + "fvr-depthcheck.bmp";
            std::string summary;
            if (ok && (!capture.WriteBmp(context.Get(), bmp, 0.06f, 20000.0f, &summary) ||
                       summary.rfind("centre=0.12m", 0) != 0)) {
                std::printf("summary: %s\n", summary.c_str());
                why = "snapshots sample 0 of a multisampled copy", ok = false;
            }
            DeleteFileA(bmp.c_str());
            capture.Remove();
        }
    }
    // The detour is gone: clears reach d3d11 directly again.
    context->ClearDepthStencilView(mainView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    return ok;
}
