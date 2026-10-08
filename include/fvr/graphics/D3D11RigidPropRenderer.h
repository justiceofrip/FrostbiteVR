#pragma once
#include "fvr/graphics/RigidPropFrame.h"
#include "fvr/graphics/RigidPropGeometry.h"
#include <array>
#include <memory>
#include <span>
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
namespace fvr::graphics {
struct RigidPropSectionUpload {RigidPropMesh mesh;std::array<float,4> color{.3f,.3f,.3f,1.f};};
struct RigidPropTarget {
    ID3D11RenderTargetView* color=nullptr;ID3D11DepthStencilView* depth=nullptr;
    unsigned width=0,height=0;
    // Must match the current native eye projection/depth convention.
    bool reversedDepth=false;
    // Only on an adapter-owned depth surface; native depth stays read-only.
    bool writeOwnedDepth=false;
};
// Render-only backend; no game hook, asset loader, spawn, simulation, inventory,
// palette mutation, query scope, or normal capability is enabled here.
// Lifecycle contract: construct, initialize, upload, draw and DESTROY on the
// SAME native graphics thread. Borrowed targets must outlive the synchronous
// draw. The adapter must prove native query/draw scopes have ended before Draw.
// D3D11.1 context-state support is mandatory; no partial manual state fallback.
class D3D11RigidPropRenderer final {
public:
    explicit D3D11RigidPropRenderer(bool enabled=false)noexcept;
    ~D3D11RigidPropRenderer();
    D3D11RigidPropRenderer(const D3D11RigidPropRenderer&)=delete;
    D3D11RigidPropRenderer& operator=(const D3D11RigidPropRenderer&)=delete;
    bool Initialize(ID3D11DeviceContext*);
    // Transactional replacement: failure keeps prior immutable owned buffers.
    // Up to eight sections / 32768 triangles / 2.25 MiB total vertex data.
    bool Upload(RigidPropGeometryKey,std::span<const RigidPropSectionUpload>);
    bool Draw(RigidPropGeometryKey,const RigidPropEye&,const RigidPropEye& current,
        std::int64_t now,const math::Matrix4& partWorld,const math::Matrix4& eyeView,
        const math::Matrix4& eyeProjection,const RigidPropTarget&);
private:
    struct Impl;std::unique_ptr<Impl> impl_;bool enabled_=false;
};
} // namespace fvr::graphics
