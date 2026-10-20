#pragma once
// Developer research: which constant buffers and variables BF4's vertex shaders declare
// (D3DReflect on each CreateVertexShader), to learn how first-person meshes are placed on
// the GPU before any render-side work on the gun at the hand. Observes only: the creation
// call proceeds unchanged and nothing in the game is written. Developer sessions only.

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fvr::bf4 {

class ShaderCensus {
public:
    ~ShaderCensus();
    // Hooks the device's CreateVertexShader implementation (d3d11.dll's own only).
    bool Install(ID3D11Device* device);
    bool Installed() const { return installed_; }
    void Remove(); // unhooks (a detaching adapter disables every hook at once instead)
    // One line per variable name (shaders using it), then each distinct layout with its count.
    std::string Report() const;
    // Writes the bytecode of up to `limit` layouts whose reflection text contains `needle`
    // (case-insensitive) as <dir>\vs-<index>.dxbc; returns how many were written.
    int Dump(const std::string& dir, const std::string& needle, int limit) const;
    std::string Describe() const;

    void OnCreate(const void* code, SIZE_T size, const void* shader);
    // The layout id (order of first appearance, shown as #id in Report) of a created shader,
    // -1 when it was not seen; and the layout text of an id.
    int LayoutOf(const void* shader) const;
    std::string LayoutText(int id) const;

    // The layout text of one shader's reflection ("cb Name[size]{var@offset:size ...} ...",
    // then bound resources). Empty when the bytecode has no reflection. Public for tests.
    static std::string Layout(const void* code, SIZE_T size);

private:
    struct Entry {
        std::uint64_t count = 0;
        int id = -1;
        std::vector<std::uint8_t> code; // the first shader seen with this layout
    };
    bool installed_ = false;
    void* entry_ = nullptr;
    mutable std::mutex mutex_;
    std::map<std::string, Entry> layouts_;
    std::map<std::string, std::uint64_t> names_; // variable/buffer/resource name -> shaders
    std::vector<const std::string*> ids_;         // layout text by id (keys of layouts_)
    std::map<const void*, int> byShader_;          // created shader -> layout id
    std::uint64_t shaders_ = 0, unreflected_ = 0;
    std::size_t codeBytes_ = 0;
};

} // namespace fvr::bf4
