#include "Bf4HudSplit.h"

#include <cstdio>

namespace fvr::bf4 {

namespace {
using Microsoft::WRL::ComPtr;

// The resource behind a view (no reference kept).
ID3D11Resource* ResourceOf(ID3D11View* view) {
    if (!view) return nullptr;
    ComPtr<ID3D11Resource> r;
    view->GetResource(&r);
    return r.Get(); // the view keeps it alive while it is bound
}
} // namespace

HudSplit::~HudSplit() { Remove(); }

bool HudSplit::Install(ID3D11DeviceContext* immediate) {
    if (installed_) return true;
    if (!immediate) return false;
    immediate_ = immediate;
    if (!ContextHooks::Install(immediate) || !ContextHooks::Add(this)) return false;
    installed_ = true;
    return true;
}

void HudSplit::Remove() {
    if (installed_) ContextHooks::Remove(this);
    installed_ = false;
}

void HudSplit::OnSetRenderTarget(ID3D11DeviceContext* context, ID3D11RenderTargetView* first) {
    if (context != immediate_) return;
    targetIsBack_ = back_ && ResourceOf(first) == back_;
}

void HudSplit::OnDraw(ID3D11DeviceContext* context, const char*, UINT, UINT) {
    if (context != immediate_ || !targetIsBack_ || copied_) return;
    // Into the back buffer: the composite reads a render target (the scene); a draw after it
    // that does not is the HUD's first.
    ID3D11ShaderResourceView* srv = nullptr;
    context->PSGetShaderResources(0, 1, &srv);
    bool readsTarget = false;
    if (srv) {
        ComPtr<ID3D11Resource> r;
        srv->GetResource(&r);
        ComPtr<ID3D11Texture2D> t;
        if (r && SUCCEEDED(r.As(&t))) {
            D3D11_TEXTURE2D_DESC d{};
            t->GetDesc(&d);
            readsTarget = (d.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
        }
        srv->Release();
    }
    if (readsTarget) {
        compositeSeen_ = true;
        return;
    }
    if (!compositeSeen_) return;
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(back_->QueryInterface(IID_PPV_ARGS(&back))) || !EnsureCopy(back.Get())) return;
    context->CopyResource(copy_.Get(), back_); // recorded before this (the first HUD) draw
    copied_ = true;
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.copies;
}

bool HudSplit::EnsureCopy(ID3D11Texture2D* back) {
    D3D11_TEXTURE2D_DESC d{};
    back->GetDesc(&d);
    if (copy_ && copyDesc_.Width == d.Width && copyDesc_.Height == d.Height && copyDesc_.Format == d.Format &&
        copyDesc_.SampleDesc.Count == d.SampleDesc.Count)
        return true;
    copy_.Reset();
    D3D11_TEXTURE2D_DESC c = d;
    c.Usage = D3D11_USAGE_DEFAULT;
    c.BindFlags = 0;
    c.CPUAccessFlags = 0;
    c.MiscFlags = 0;
    c.MipLevels = 1;
    c.ArraySize = 1;
    ComPtr<ID3D11Device> device;
    back->GetDevice(&device);
    if (!device || FAILED(device->CreateTexture2D(&c, nullptr, &copy_))) return false;
    copyDesc_ = d;
    return true;
}

void HudSplit::OnPresent(ID3D11Texture2D* backBuffer, bool restoreCentre, float boxFraction) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.frames;
        if (!copied_) ++stats_.noComposite;
    }
    if (restoreCentre && copied_ && backBuffer && backBuffer == back_ && copy_ && immediate_) {
        D3D11_TEXTURE2D_DESC d{};
        backBuffer->GetDesc(&d);
        const UINT side = static_cast<UINT>(boxFraction * static_cast<float>(d.Height)) & ~1u;
        if (side >= 2 && side <= d.Height && d.SampleDesc.Count == 1) {
            D3D11_BOX box{};
            box.left = (d.Width - side) / 2;
            box.top = (d.Height - side) / 2;
            box.right = box.left + side;
            box.bottom = box.top + side;
            box.back = 1;
            immediate_->CopySubresourceRegion(backBuffer, 0, box.left, box.top, 0, copy_.Get(), 0, &box);
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.restores;
        }
    }
    // The next frame: the back buffer may change with the swap chain.
    back_ = backBuffer;
    compositeSeen_ = copied_ = false;
}

HudSplit::Stats HudSplit::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::string HudSplit::Describe() const {
    const Stats s = GetStats();
    char line[200];
    std::snprintf(line, sizeof(line), "hud_split installed=%d frames=%llu copies=%llu restores=%llu no_composite=%llu",
                  static_cast<int>(installed_), static_cast<unsigned long long>(s.frames),
                  static_cast<unsigned long long>(s.copies), static_cast<unsigned long long>(s.restores),
                  static_cast<unsigned long long>(s.noComposite));
    return line;
}

} // namespace fvr::bf4
