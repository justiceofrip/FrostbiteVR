#pragma once
#include "fvr/graphics/D3D11BodyPropCompositor.h"
#include <filesystem>
namespace fvr::graphics {
// Private converted assets. No texture role/UV is inferred here. A sidecar is
// explicitly supplied and must match the exact geometry and section fingerprints.
struct BodyPropMaterialResult {
    std::shared_ptr<const BodyPropCatalog> catalog;
    unsigned textures=0,sections=0;
    const char* error="uninitialized";
};
BodyPropMaterialResult ApplyBodyPropMaterials(std::span<const std::byte>,const BodyPropCatalog&)noexcept;
BodyPropMaterialResult LoadBodyPropMaterials(const std::filesystem::path&,const BodyPropCatalog&)noexcept;
} // namespace fvr::graphics
