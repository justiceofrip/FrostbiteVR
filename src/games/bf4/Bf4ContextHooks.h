#pragma once
// One owner for the D3D11 device-context detours the developer research tools share (BF4's
// immediate context and its deferred contexts have separate implementations in d3d11.dll):
// Draw*, Map, Unmap, UpdateSubresource and the VS/PS constant-buffer binds. Listeners see the
// calls; a constant-buffer listener may substitute buffers in the bind it is shown (the game's
// arrays are never written: the call proceeds with a copy). Every hooked call proceeds. With
// no listener the detours only forward (one atomic load).

#include <d3d11_1.h>

#include <atomic>
#include <cstdint>

namespace fvr::bf4 {

class ContextHooks {
public:
    enum class Stage : std::uint8_t { Vertex, Pixel };
    struct Listener {
        virtual ~Listener() = default;
        virtual void OnMap(ID3D11DeviceContext*, ID3D11Resource*, D3D11_MAP, void* /*data*/) {}
        virtual void OnUnmap(ID3D11DeviceContext*, ID3D11Resource*) {}
        virtual void OnUpdate(ID3D11DeviceContext*, ID3D11Resource*, const D3D11_BOX*, const void* /*source*/) {}
        virtual void OnDraw(ID3D11DeviceContext*, const char* /*kind*/, UINT /*count*/, UINT /*instances*/) {}
        // `buffers` (and, for the *1 binds, `first` / `count` in 16-byte constants; null for the
        // plain binds) are a copy of the game's arguments: entries may be replaced.
        virtual void OnBindConstants(ID3D11DeviceContext*, Stage, UINT /*startSlot*/, UINT /*numBuffers*/,
                                     ID3D11Buffer** /*buffers*/, UINT* /*first*/, UINT* /*count*/) {}
    };
    static constexpr int kMaxListeners = 4;

    // Hooks the immediate context's implementations and a deferred context's (d3d11.dll's own
    // functions only); idempotent.
    static bool Install(ID3D11DeviceContext* immediate);
    static bool Installed();
    static bool Add(Listener* listener);
    static void Remove(Listener* listener);
    // Unhooks everything (tests; a detaching adapter disables all hooks at once instead).
    static void Uninstall();
};

} // namespace fvr::bf4
