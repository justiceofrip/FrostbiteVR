#pragma once
#include <memory>
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
namespace fvr::graphics {
struct SceneDepthComposition {
    ID3D11ShaderResourceView* originalColor=nullptr;
    ID3D11ShaderResourceView* propColor=nullptr;
    ID3D11ShaderResourceView* propDepth=nullptr;
    ID3D11ShaderResourceView* worldDepth=nullptr;
    ID3D11ShaderResourceView* firstPersonDepth=nullptr;
    ID3D11RenderTargetView* output=nullptr;
    bool reversedDepth=false;
};
// Host-device renderer only. Caller must supply the SAME eye, frame and clip
// depth convention for all five inputs, and retain their GPU read ownership.
// This class validates resources, not native provenance or IPC/frame lifetimes.
// World/first-person depth may have 1,2,4,8,16 or32 samples; other inputs and
// output are single-sample. Occlusion preserves per-sample edge coverage.
// No in-place composition: output must be distinct from every source resource.
class D3D11SceneDepthCompositor final {
public:
    D3D11SceneDepthCompositor();
    ~D3D11SceneDepthCompositor();
    bool Initialize(ID3D11DeviceContext*);
    bool Compose(const SceneDepthComposition&);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
