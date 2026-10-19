#include "Bf4DepthCapture.h"

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fvr::bf4 {

namespace {
using Microsoft::WRL::ComPtr;
using ClearFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
constexpr unsigned kClearDepthSlot = 53; // ID3D11DeviceContext::ClearDepthStencilView
std::atomic<DepthCapture*> g_capture{nullptr};

// The D3D11 runtime resolves a context's vtable entries lazily (the first call goes through
// a stub that then points the entry at the real implementation), and immediate and deferred
// contexts have their own implementations. So each implementation seen gets its own detour
// (and trampoline), up to kMaxEntries.
constexpr std::size_t kMaxEntries = 4;
std::array<ClearFn, kMaxEntries> g_originals{};

template <std::size_t I>
void STDMETHODCALLTYPE ClearHook(ID3D11DeviceContext* context, ID3D11DepthStencilView* dsv, UINT flags, FLOAT value,
                                 UINT8 stencil) {
    DepthCapture* self = g_capture.load(std::memory_order_acquire);
    if (self && dsv && (flags & D3D11_CLEAR_DEPTH)) self->OnClear(context, dsv, value);
    g_originals[I](context, dsv, flags, value, stencil);
}
const std::array<LPVOID, kMaxEntries> kDetours{reinterpret_cast<LPVOID>(&ClearHook<0>), reinterpret_cast<LPVOID>(&ClearHook<1>),
                                                   reinterpret_cast<LPVOID>(&ClearHook<2>), reinterpret_cast<LPVOID>(&ClearHook<3>)};

bool InD3D11(void* function) {
    HMODULE owner = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(function), &owner) &&
           owner == GetModuleHandleW(L"d3d11.dll");
}

void* ClearEntry(ID3D11DeviceContext* context) { return (*reinterpret_cast<void***>(context))[kClearDepthSlot]; }

// The typeless family of a depth format (what a shader-readable copy is created with).
DXGI_FORMAT TypelessDepth(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return DXGI_FORMAT_R32G8X24_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
} // namespace

DepthCapture::~DepthCapture() {
    DepthCapture* expected = this;
    g_capture.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

bool DepthCapture::HookEntry(void* entry) {
    for (std::size_t i = 0; i < hooked_; ++i)
        if (entries_[i] == entry) return true;
    if (hooked_ >= kMaxEntries || !entry) return false;
    if (!InD3D11(entry)) return false; // someone else's function (an overlay, a wrapper): leave it alone
    const std::size_t i = hooked_;
    if (MH_CreateHook(entry, kDetours[i], reinterpret_cast<LPVOID*>(&g_originals[i])) != MH_OK) return false;
    if (MH_EnableHook(entry) != MH_OK) {
        MH_RemoveHook(entry);
        return false;
    }
    entries_[i] = entry;
    ++hooked_;
    return true;
}

bool DepthCapture::Install(ID3D11DeviceContext* immediate) {
    if (installed_) return true;
    if (!immediate) return false;
    ComPtr<ID3D11Device> device;
    immediate->GetDevice(&device);
    if (!device) return false;
    // Resolve the runtime's lazy entries with one clear of a tiny target of our own, on the
    // immediate context and on a deferred one (whose recording is thrown away).
    D3D11_TEXTURE2D_DESC d{};
    d.Width = d.Height = 4;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> scratch;
    ComPtr<ID3D11DepthStencilView> view;
    if (FAILED(device->CreateTexture2D(&d, nullptr, &scratch)) ||
        FAILED(device->CreateDepthStencilView(scratch.Get(), nullptr, &view)))
        return false;
    immediate->ClearDepthStencilView(view.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ComPtr<ID3D11DeviceContext> deferred;
    if (SUCCEEDED(device->CreateDeferredContext(0, &deferred))) {
        deferred->ClearDepthStencilView(view.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        ComPtr<ID3D11CommandList> discard;
        deferred->FinishCommandList(FALSE, &discard);
    }
    g_capture.store(this, std::memory_order_release);
    if (!HookEntry(ClearEntry(immediate))) {
        g_capture.store(nullptr, std::memory_order_release);
        return false;
    }
    if (deferred) HookEntry(ClearEntry(deferred.Get())); // optional: the game may not record clears there
    installed_ = true;
    return true;
}

void DepthCapture::Remove() {
    if (!installed_) return;
    for (std::size_t i = 0; i < hooked_; ++i) {
        MH_DisableHook(entries_[i]);
        MH_RemoveHook(entries_[i]);
        entries_[i] = nullptr;
    }
    hooked_ = 0;
    DepthCapture* expected = this;
    g_capture.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    installed_ = false;
}

bool DepthCapture::EnsureCopy(ID3D11Device* device) {
    if (copy_) return true;
    D3D11_TEXTURE2D_DESC d = mainDesc_;
    d.Format = TypelessDepth(mainDesc_.Format);
    if (d.Format == DXGI_FORMAT_UNKNOWN || !device) return false;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = 0;
    d.MiscFlags = 0;
    return SUCCEEDED(device->CreateTexture2D(&d, nullptr, &copy_));
}

void DepthCapture::OnClear(ID3D11DeviceContext* context, ID3D11DepthStencilView* dsv, float value) {
    ComPtr<ID3D11Resource> resource;
    dsv->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    if (!resource || FAILED(resource.As(&texture))) return;
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.clears;
    if (d.ArraySize != 1 || !d.Width || !d.Height || TypelessDepth(d.Format) == DXGI_FORMAT_UNKNOWN) return;
    if (aspect_ > 0 && std::fabs(float(d.Width) / float(d.Height) - aspect_) > 0.03f * aspect_) return;
    const std::uint64_t area = std::uint64_t(d.Width) * d.Height;
    if (main_.Get() != texture.Get()) {
        // A smaller target is not the main depth, unless the main one stopped being used
        // (the game recreated its targets, e.g. after a resolution change).
        const bool stale = stats_.presents > lastMainClear_ + 30;
        if (main_ && !stale && area <= std::uint64_t(mainDesc_.Width) * mainDesc_.Height) return;
        main_ = texture; // the largest one seen becomes the main depth
        mainDesc_ = d;
        copy_.Reset();
        stats_.width = d.Width;
        stats_.height = d.Height;
        stats_.format = static_cast<std::uint32_t>(d.Format);
    }
    ++stats_.mainClears;
    ++clearsThisFrame_;
    lastMainClear_ = stats_.presents;
    stats_.clearValue = value;
    if (d.SampleDesc.Count != 1) ++stats_.msaaRefused; // counted only: copied as it is, read per sample
    ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    if (!EnsureCopy(device.Get())) return;
    context->CopyResource(copy_.Get(), texture.Get()); // recorded before the clear, in order
    ++stats_.copies;
}

bool DepthCapture::TakeFrame(ID3D11DeviceContext* immediate, float aspect, Frame& out) {
    out = {};
    if (installed_ && immediate) HookEntry(ClearEntry(immediate)); // the runtime may have swapped it
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.presents;
    if (aspect > 0.2f && aspect < 5.0f) aspect_ = aspect;
    // Chosen before the backbuffer's shape was known (e.g. a shadow map cleared before the
    // first Present): not the main depth after all.
    if (main_ && aspect_ > 0 &&
        std::fabs(float(mainDesc_.Width) / float(mainDesc_.Height) - aspect_) > 0.03f * aspect_) {
        main_.Reset();
        copy_.Reset();
        mainDesc_ = {};
    }
    clearHistory_[historyIndex_++ % clearHistory_.size()] = clearsThisFrame_;
    clearsThisFrame_ = 0;
    unsigned total = 0;
    for (const unsigned c : clearHistory_) total += c;
    stats_.split = historyIndex_ >= clearHistory_.size() && total >= 3 * clearHistory_.size() / 2;
    if (!main_ || !immediate) return false;
    ComPtr<ID3D11Device> device;
    immediate->GetDevice(&device);
    if (!EnsureCopy(device.Get())) return false;
    // One clear a frame: the buffer at Present holds this frame's scene. Two or more: the
    // copy taken before the last clear holds the world (the first-person pass follows it).
    if (!stats_.split) immediate->CopyResource(copy_.Get(), main_.Get());
    out.texture = copy_; // a reference: a clear on another thread may replace copy_ meanwhile
    out.format = static_cast<std::uint32_t>(TypelessDepth(mainDesc_.Format));
    out.clear = stats_.clearValue;
    ++stats_.picks;
    return true;
}

DepthCapture::Stats DepthCapture::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

std::string DepthCapture::Describe() const {
    const Stats s = GetStats();
    char line[256];
    std::snprintf(line, sizeof(line),
                  "depth_capture installed=%d main=%ux%u format=%u clear=%.1f split=%d clears=%llu main_clears=%llu "
                  "copies=%llu picks=%llu msaa_clears=%llu hooked=%zu",
                  installed_ ? 1 : 0, s.width, s.height, s.format, s.clearValue, s.split ? 1 : 0,
                  static_cast<unsigned long long>(s.clears), static_cast<unsigned long long>(s.mainClears),
                  static_cast<unsigned long long>(s.copies), static_cast<unsigned long long>(s.picks),
                  static_cast<unsigned long long>(s.msaaRefused), hooked_);
    return line;
}

bool DepthCapture::WriteBmp(ID3D11DeviceContext* immediate, const std::string& path, float nearZ, float farZ) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!copy_ || !immediate) return false;
    D3D11_TEXTURE2D_DESC d{};
    copy_->GetDesc(&d);
    if (d.SampleDesc.Count != 1) return false; // no CPU copy of a multisampled buffer: no snapshot
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Device> device;
    immediate->GetDevice(&device);
    ComPtr<ID3D11Texture2D> staging;
    if (!device || FAILED(device->CreateTexture2D(&d, nullptr, &staging))) return false;
    immediate->CopyResource(staging.Get(), copy_.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    const bool reversed = stats_.clearValue < 0.5f;
    const std::uint32_t w = d.Width, h = d.Height;
    std::vector<std::uint8_t> pixels(std::size_t(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(m.pData) + std::size_t(y) * m.RowPitch;
        for (std::uint32_t x = 0; x < w; ++x) {
            float v = 0;
            switch (d.Format) {
            case DXGI_FORMAT_R24G8_TYPELESS: {
                std::uint32_t raw;
                std::memcpy(&raw, row + x * 4, 4);
                v = float(raw & 0xFFFFFF) / 16777215.0f;
                break;
            }
            case DXGI_FORMAT_R32G8X24_TYPELESS: std::memcpy(&v, row + x * 8, 4); break;
            case DXGI_FORMAT_R32_TYPELESS: std::memcpy(&v, row + x * 4, 4); break;
            case DXGI_FORMAT_R16_TYPELESS: {
                std::uint16_t raw;
                std::memcpy(&raw, row + x * 2, 2);
                v = raw / 65535.0f;
                break;
            }
            default: break;
            }
            float z = farZ;
            if (nearZ > 0 && farZ > nearZ) {
                const float denom = reversed ? nearZ + v * (farZ - nearZ) : farZ - v * (farZ - nearZ);
                if (denom > 0) z = nearZ * farZ / denom;
            }
            // Near bright, far dark: 1 m reads white, 50 m about 10 %.
            const float shade = std::fmin(1.0f, 1.0f / std::fmax(z, 1.0f) * 5.0f);
            const auto g = static_cast<std::uint8_t>(std::lround(std::fmin(1.0f, shade) * 255.0f));
            std::uint8_t* p = &pixels[(std::size_t(y) * w + x) * 4];
            p[0] = p[1] = p[2] = g;
            p[3] = 255;
        }
    }
    immediate->Unmap(staging.Get(), 0);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::uint32_t size = w * h * 4;
    std::uint8_t header[54] = {'B', 'M'};
    auto put32 = [&](int at, std::uint32_t value) { std::memcpy(header + at, &value, 4); };
    put32(2, 54 + size);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, static_cast<std::uint32_t>(-static_cast<std::int32_t>(h)));
    header[26] = 1;
    header[28] = 32;
    put32(34, size);
    std::fwrite(header, 1, sizeof(header), f);
    std::fwrite(pixels.data(), 1, pixels.size(), f);
    std::fclose(f);
    return true;
}

} // namespace fvr::bf4
