#include "Bf4ContextHooks.h"

#include <MinHook.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace fvr::bf4 {

namespace {
using Microsoft::WRL::ComPtr;
using Listener = ContextHooks::Listener;
using Stage = ContextHooks::Stage;

// ID3D11DeviceContext / ID3D11DeviceContext1 vtable slots.
constexpr unsigned kVsSetConstantBuffers = 7, kDrawIndexed = 12, kDraw = 13, kMap = 14, kUnmap = 15,
                   kPsSetConstantBuffers = 16, kDrawIndexedInstanced = 20, kDrawInstanced = 21, kOmSetRenderTargets = 33,
                   kOmSetRenderTargetsAndUav = 34, kUpdateSubresource = 48,
                   kVsSetConstantBuffers1 = 119, kPsSetConstantBuffers1 = 123;
constexpr UINT kMaxSlots = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;

using SetCbFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*);
using SetCb1Fn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*, const UINT*, const UINT*);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
                                          D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
using UpdateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT,
                                          UINT);
using SetRtFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
using SetRtUavFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*,
                                            UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
// [0] the immediate context's implementation, [1] the deferred one's.
std::array<SetCbFn, 2> g_vsSet{}, g_psSet{};
std::array<SetCb1Fn, 2> g_vsSet1{}, g_psSet1{};
std::array<DrawIndexedFn, 2> g_drawIndexed{};
std::array<DrawFn, 2> g_draw{};
std::array<MapFn, 2> g_map{};
std::array<UnmapFn, 2> g_unmap{};
std::array<DrawIndexedInstancedFn, 2> g_drawIndexedInstanced{};
std::array<DrawInstancedFn, 2> g_drawInstanced{};
std::array<UpdateFn, 2> g_update{};
std::array<SetRtFn, 2> g_setRt{};
std::array<SetRtUavFn, 2> g_setRtUav{};

std::array<std::atomic<Listener*>, ContextHooks::kMaxListeners> g_listeners{};
std::atomic<int> g_active{0}; // listeners registered
std::atomic<int> g_busy{0};   // detours currently calling listeners
std::mutex g_installMutex;
std::vector<void*> g_entries;
bool g_installed = false;

// Calls f(listener) for each registered listener, counted so Remove can wait them out.
template <class F> void Each(F&& f) {
    if (g_active.load(std::memory_order_acquire) == 0) return;
    g_busy.fetch_add(1, std::memory_order_acq_rel);
    for (auto& slot : g_listeners)
        if (Listener* l = slot.load(std::memory_order_acquire)) f(l);
    g_busy.fetch_sub(1, std::memory_order_acq_rel);
}

template <int I> void STDMETHODCALLTYPE VsSetHook(ID3D11DeviceContext* c, UINT start, UINT num, ID3D11Buffer* const* b) {
    if (g_active.load(std::memory_order_acquire) == 0 || !b || num == 0 || num > kMaxSlots) return g_vsSet[I](c, start, num, b);
    ID3D11Buffer* copy[kMaxSlots];
    std::memcpy(copy, b, num * sizeof(*b));
    Each([&](Listener* l) { l->OnBindConstants(c, Stage::Vertex, start, num, copy, nullptr, nullptr); });
    g_vsSet[I](c, start, num, copy);
}
template <int I> void STDMETHODCALLTYPE PsSetHook(ID3D11DeviceContext* c, UINT start, UINT num, ID3D11Buffer* const* b) {
    if (g_active.load(std::memory_order_acquire) == 0 || !b || num == 0 || num > kMaxSlots) return g_psSet[I](c, start, num, b);
    ID3D11Buffer* copy[kMaxSlots];
    std::memcpy(copy, b, num * sizeof(*b));
    Each([&](Listener* l) { l->OnBindConstants(c, Stage::Pixel, start, num, copy, nullptr, nullptr); });
    g_psSet[I](c, start, num, copy);
}
template <int I, Stage S>
void Set1(SetCb1Fn original, ID3D11DeviceContext* c, UINT start, UINT num, ID3D11Buffer* const* b, const UINT* first,
          const UINT* count) {
    if (g_active.load(std::memory_order_acquire) == 0 || !b || num == 0 || num > kMaxSlots)
        return original(c, start, num, b, first, count);
    ID3D11Buffer* copy[kMaxSlots];
    UINT f[kMaxSlots], n[kMaxSlots];
    std::memcpy(copy, b, num * sizeof(*b));
    if (first) std::memcpy(f, first, num * sizeof(UINT));
    if (count) std::memcpy(n, count, num * sizeof(UINT));
    Each([&](Listener* l) { l->OnBindConstants(c, S, start, num, copy, first ? f : nullptr, count ? n : nullptr); });
    original(c, start, num, copy, first ? f : nullptr, count ? n : nullptr);
}
template <int I> void STDMETHODCALLTYPE VsSet1Hook(ID3D11DeviceContext* c, UINT start, UINT num, ID3D11Buffer* const* b,
                                                   const UINT* first, const UINT* count) {
    Set1<I, Stage::Vertex>(g_vsSet1[I], c, start, num, b, first, count);
}
template <int I> void STDMETHODCALLTYPE PsSet1Hook(ID3D11DeviceContext* c, UINT start, UINT num, ID3D11Buffer* const* b,
                                                   const UINT* first, const UINT* count) {
    Set1<I, Stage::Pixel>(g_psSet1[I], c, start, num, b, first, count);
}
template <int I> void STDMETHODCALLTYPE DrawIndexedHook(ID3D11DeviceContext* c, UINT n, UINT start, INT base) {
    Each([&](Listener* l) { l->OnDraw(c, "di", n, 1); });
    g_drawIndexed[I](c, n, start, base);
}
template <int I> void STDMETHODCALLTYPE DrawHook(ID3D11DeviceContext* c, UINT n, UINT start) {
    Each([&](Listener* l) { l->OnDraw(c, "d", n, 1); });
    g_draw[I](c, n, start);
}
template <int I> void STDMETHODCALLTYPE DrawIndexedInstancedHook(ID3D11DeviceContext* c, UINT n, UINT inst, UINT start,
                                                                 INT base, UINT startInst) {
    Each([&](Listener* l) { l->OnDraw(c, "dii", n, inst); });
    g_drawIndexedInstanced[I](c, n, inst, start, base, startInst);
}
template <int I> void STDMETHODCALLTYPE DrawInstancedHook(ID3D11DeviceContext* c, UINT n, UINT inst, UINT start,
                                                          UINT startInst) {
    Each([&](Listener* l) { l->OnDraw(c, "dn", n, inst); });
    g_drawInstanced[I](c, n, inst, start, startInst);
}
template <int I> HRESULT STDMETHODCALLTYPE MapHook(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, D3D11_MAP type,
                                                   UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    const HRESULT hr = g_map[I](c, r, sub, type, flags, mapped);
    if (SUCCEEDED(hr) && mapped && sub == 0) Each([&](Listener* l) { l->OnMap(c, r, type, mapped->pData); });
    return hr;
}
template <int I> void STDMETHODCALLTYPE UnmapHook(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub) {
    if (sub == 0) Each([&](Listener* l) { l->OnUnmap(c, r); }); // still mapped here
    g_unmap[I](c, r, sub);
}
template <int I> void STDMETHODCALLTYPE UpdateHook(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, const D3D11_BOX* box,
                                                   const void* src, UINT rowPitch, UINT depthPitch) {
    if (sub == 0) Each([&](Listener* l) { l->OnUpdate(c, r, box, src); });
    g_update[I](c, r, sub, box, src, rowPitch, depthPitch);
}

template <int I> void STDMETHODCALLTYPE SetRtHook(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtv,
                                                   ID3D11DepthStencilView* dsv) {
    g_setRt[I](c, n, rtv, dsv);
    ID3D11RenderTargetView* first = n && rtv ? rtv[0] : nullptr;
    Each([&](Listener* l) { l->OnSetRenderTarget(c, first); });
}
template <int I> void STDMETHODCALLTYPE SetRtUavHook(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtv,
                                                      ID3D11DepthStencilView* dsv, UINT uavStart, UINT uavCount,
                                                      ID3D11UnorderedAccessView* const* uavs, const UINT* counts) {
    g_setRtUav[I](c, n, rtv, dsv, uavStart, uavCount, uavs, counts);
    if (n == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) return; // only the UAVs changed
    ID3D11RenderTargetView* first = n && rtv ? rtv[0] : nullptr;
    Each([&](Listener* l) { l->OnSetRenderTarget(c, first); });
}

bool InD3D11(void* function) {
    HMODULE owner = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(function), &owner) &&
           owner == GetModuleHandleW(L"d3d11.dll");
}

template <int I> void HookAll(void** table, bool context1) {
    auto hook = [&](unsigned slot, LPVOID detour, LPVOID* original) {
        void* entry = table[slot];
        if (!entry || !InD3D11(entry)) return; // someone else's function (an overlay): leave it alone
        for (void* e : g_entries)
            if (e == entry) return; // the same implementation as the other context's: hooked already
        if (MH_CreateHook(entry, detour, original) != MH_OK) return;
        if (MH_EnableHook(entry) != MH_OK) {
            MH_RemoveHook(entry);
            return;
        }
        g_entries.push_back(entry);
    };
    hook(kVsSetConstantBuffers, reinterpret_cast<LPVOID>(&VsSetHook<I>), reinterpret_cast<LPVOID*>(&g_vsSet[I]));
    hook(kPsSetConstantBuffers, reinterpret_cast<LPVOID>(&PsSetHook<I>), reinterpret_cast<LPVOID*>(&g_psSet[I]));
    hook(kDrawIndexed, reinterpret_cast<LPVOID>(&DrawIndexedHook<I>), reinterpret_cast<LPVOID*>(&g_drawIndexed[I]));
    hook(kDraw, reinterpret_cast<LPVOID>(&DrawHook<I>), reinterpret_cast<LPVOID*>(&g_draw[I]));
    hook(kMap, reinterpret_cast<LPVOID>(&MapHook<I>), reinterpret_cast<LPVOID*>(&g_map[I]));
    hook(kUnmap, reinterpret_cast<LPVOID>(&UnmapHook<I>), reinterpret_cast<LPVOID*>(&g_unmap[I]));
    hook(kDrawIndexedInstanced, reinterpret_cast<LPVOID>(&DrawIndexedInstancedHook<I>),
         reinterpret_cast<LPVOID*>(&g_drawIndexedInstanced[I]));
    hook(kDrawInstanced, reinterpret_cast<LPVOID>(&DrawInstancedHook<I>), reinterpret_cast<LPVOID*>(&g_drawInstanced[I]));
    hook(kUpdateSubresource, reinterpret_cast<LPVOID>(&UpdateHook<I>), reinterpret_cast<LPVOID*>(&g_update[I]));
    hook(kOmSetRenderTargets, reinterpret_cast<LPVOID>(&SetRtHook<I>), reinterpret_cast<LPVOID*>(&g_setRt[I]));
    hook(kOmSetRenderTargetsAndUav, reinterpret_cast<LPVOID>(&SetRtUavHook<I>), reinterpret_cast<LPVOID*>(&g_setRtUav[I]));
    if (context1) {
        hook(kVsSetConstantBuffers1, reinterpret_cast<LPVOID>(&VsSet1Hook<I>), reinterpret_cast<LPVOID*>(&g_vsSet1[I]));
        hook(kPsSetConstantBuffers1, reinterpret_cast<LPVOID>(&PsSet1Hook<I>), reinterpret_cast<LPVOID*>(&g_psSet1[I]));
    }
}
} // namespace

bool ContextHooks::Install(ID3D11DeviceContext* immediate) {
    std::lock_guard<std::mutex> lock(g_installMutex);
    if (g_installed) return true;
    if (!immediate) return false;
    ComPtr<ID3D11Device> device;
    immediate->GetDevice(&device);
    if (!device) return false;
    // A deferred context of our own, each hooked function called once so the runtime's lazily
    // resolved entries point at the real implementations (its recording is thrown away).
    ComPtr<ID3D11DeviceContext> deferred;
    if (SUCCEEDED(device->CreateDeferredContext(0, &deferred))) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 256;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> dynamic, fixed;
        device->CreateBuffer(&bd, nullptr, &dynamic);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.CPUAccessFlags = 0;
        device->CreateBuffer(&bd, nullptr, &fixed);
        if (dynamic) {
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(deferred->Map(dynamic.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) deferred->Unmap(dynamic.Get(), 0);
        }
        if (fixed) {
            const std::uint8_t zeros[256]{};
            deferred->UpdateSubresource(fixed.Get(), 0, nullptr, zeros, 0, 0);
            ID3D11Buffer* one[1] = {fixed.Get()};
            deferred->VSSetConstantBuffers(0, 1, one);
            deferred->PSSetConstantBuffers(0, 1, one);
            ComPtr<ID3D11DeviceContext1> d1;
            if (SUCCEEDED(deferred.As(&d1))) {
                const UINT first[1] = {0}, count[1] = {16};
                d1->VSSetConstantBuffers1(0, 1, one, first, count);
                d1->PSSetConstantBuffers1(0, 1, one, first, count);
            }
        }
        ID3D11RenderTargetView* noTargets[1]{};
        deferred->OMSetRenderTargets(1, noTargets, nullptr);
        deferred->OMSetRenderTargetsAndUnorderedAccessViews(1, noTargets, nullptr, 1, 0, nullptr, nullptr);
        deferred->Draw(0, 0);
        deferred->DrawIndexed(0, 0, 0);
        deferred->DrawInstanced(0, 0, 0, 0);
        deferred->DrawIndexedInstanced(0, 0, 0, 0, 0);
        ComPtr<ID3D11CommandList> discard;
        deferred->FinishCommandList(FALSE, &discard);
    }
    ComPtr<ID3D11DeviceContext1> immediate1;
    HookAll<0>(*reinterpret_cast<void***>(immediate), SUCCEEDED(immediate->QueryInterface(IID_PPV_ARGS(&immediate1))) &&
                                                          immediate1.Get() == immediate);
    if (deferred) {
        ComPtr<ID3D11DeviceContext1> deferred1;
        HookAll<1>(*reinterpret_cast<void***>(deferred.Get()),
                   SUCCEEDED(deferred.As(&deferred1)) && deferred1.Get() == deferred.Get());
    }
    g_installed = !g_entries.empty();
    return g_installed;
}

bool ContextHooks::Installed() {
    std::lock_guard<std::mutex> lock(g_installMutex);
    return g_installed;
}

bool ContextHooks::Add(Listener* listener) {
    if (!listener) return false;
    for (auto& slot : g_listeners) {
        if (slot.load(std::memory_order_acquire) == listener) return true;
        Listener* expected = nullptr;
        if (slot.compare_exchange_strong(expected, listener, std::memory_order_acq_rel)) {
            g_active.fetch_add(1, std::memory_order_acq_rel);
            return true;
        }
    }
    return false;
}

void ContextHooks::Remove(Listener* listener) {
    for (auto& slot : g_listeners) {
        Listener* expected = listener;
        if (listener && slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel))
            g_active.fetch_sub(1, std::memory_order_acq_rel);
    }
    while (g_busy.load(std::memory_order_acquire) != 0) std::this_thread::yield(); // a call in flight may still use it
}

void ContextHooks::Uninstall() {
    std::lock_guard<std::mutex> lock(g_installMutex);
    for (void* e : g_entries) {
        MH_DisableHook(e);
        MH_RemoveHook(e);
    }
    g_entries.clear();
    g_installed = false;
}

} // namespace fvr::bf4
