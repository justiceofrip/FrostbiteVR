#pragma once
// Developer research: one frame of BF4's draw calls as the GPU receives them (vertex shader
// layout from the shader census, bound constant buffers and their contents, targets), to
// learn where the first-person meshes' bone palettes and the UI pass are. Observes only:
// every hooked call proceeds unchanged and nothing in the game is written. Idle (one atomic
// load per call) except during the one requested frame. Developer sessions only.

#include "Bf4ContextHooks.h"

#include <d3d11_1.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace fvr::bf4 {

class ShaderCensus;

class DrawProbe : public ContextHooks::Listener {
public:
    ~DrawProbe();
    // Listens to the shared context hooks (Bf4ContextHooks.h), installing them if needed.
    bool Install(ID3D11DeviceContext* immediate, const ShaderCensus* census);
    void Remove();
    bool Installed() const { return installed_; }
    // Records the frame between the next two Presents (at most maxDraws draws).
    void Start(std::uint32_t maxDraws = 20000);
    // Each Present (render thread). True once the requested frame is complete: TakeReport().
    bool OnPresent();
    std::string TakeReport();

    // Hook callbacks (any thread).
    void OnMap(ID3D11DeviceContext* context, ID3D11Resource* resource, D3D11_MAP type, void* data) override;
    void OnUnmap(ID3D11DeviceContext* context, ID3D11Resource* resource) override;
    void OnUpdate(ID3D11DeviceContext* context, ID3D11Resource* resource, const D3D11_BOX* box, const void* source) override;
    void OnDraw(ID3D11DeviceContext* context, const char* kind, UINT count, UINT instances) override;

    enum class State : int { Idle, Armed, Capturing };
    static std::atomic<int> state; // read by the hooks

private:
    struct Content {
        void* mapped = nullptr;          // what Map returned (read at draw time when large)
        std::uint32_t size = 0;          // the buffer's ByteWidth
        std::vector<std::uint8_t> copy;  // a copy taken at Unmap / UpdateSubresource (small buffers)
        const char* how = "?";           // how it was filled last
    };
    static bool Capturing();
    bool Bytes(ID3D11DeviceContext* context, ID3D11Buffer* buffer, std::uint32_t offset, std::uint32_t size,
               std::vector<std::uint8_t>& out);
    std::string Target(ID3D11View* view);
    int ContextId(ID3D11DeviceContext* context);

    bool installed_ = false;
    const ShaderCensus* census_ = nullptr;
    std::mutex mutex_;
    std::uint32_t maxDraws_ = 0, draws_ = 0;
    std::map<std::pair<const void*, const void*>, Content> contents_; // (context, buffer)
    std::map<const void*, int> contexts_;
    std::map<const void*, std::string> targets_;
    std::map<const void*, std::vector<float>> cameraPos_; // per context, from viewConstants
    std::map<int, std::uint32_t> perLayout_;
    std::string lines_;
    std::string report_;
    bool ready_ = false;
};

} // namespace fvr::bf4
