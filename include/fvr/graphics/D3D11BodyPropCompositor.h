#pragma once
#include "fvr/graphics/BodyPropFrame.h"
#include "fvr/graphics/D3D11RigidPropRenderer.h"
#include <vector>
struct ID3D11Texture2D;
namespace fvr::graphics {
struct BodyPropAsset {RigidPropGeometryKey key{};std::vector<RigidPropSectionUpload> sections;};
using BodyPropCatalog=std::vector<BodyPropAsset>;
struct BodyPropComposeStats {std::uint64_t frames=0,pairs=0,instances=0,missing=0,expired=0,invalid=0,failures=0;};
// HOST DEVICE ONLY. Owns both color/depth work pairs. Never receives native
// targets or runs inside engine queries. A failed composition leaves the input
// pair untouched. Private depth provides prop self-occlusion, not scene depth.
class D3D11BodyPropCompositor final {
public:
    D3D11BodyPropCompositor();~D3D11BodyPropCompositor();
    bool Initialize(ID3D11DeviceContext*,unsigned width,unsigned height,unsigned format,
        std::shared_ptr<const BodyPropCatalog>);
    bool Compose(const BodyPropFrame&,const PairTicket&,const std::array<ID3D11Texture2D*,2>&,
        std::int64_t (*clockNs)()noexcept)noexcept;
    const BodyPropComposeStats& Statistics()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace fvr::graphics
