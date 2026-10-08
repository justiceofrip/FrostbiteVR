#pragma once
#include "Bc2BodyAmmoRenderer.h"
#include "Bc2BodyAmmoAssetProfile.h"
#include <filesystem>
namespace fvr::bc2 {
enum class BodyAmmoCacheStatus:unsigned {Loaded,Missing,Size,Read,Header,Count,Identity,Section,Geometry,Trailing,Exception};
struct BodyAmmoCacheResult {
    BodyAmmoCacheStatus status=BodyAmmoCacheStatus::Missing;
    std::shared_ptr<const BodyAmmoGeometryCatalog> catalog;
    unsigned part=0,section=0;
    std::uint64_t bytes=0;
};
BodyAmmoCacheResult ParseBodyAmmoGeometryCache(std::span<const std::byte>)noexcept;
// Portable parser/extractor seam. Production always supplies its compiled,
// reviewed catalog; test callers can supply synthetic geometry descriptions.
BodyAmmoCacheResult ParseBodyAmmoGeometryCacheForProfiles(std::span<const std::byte>,std::span<const BodyAmmoAssetProfile>)noexcept;
BodyAmmoCacheResult LoadBodyAmmoGeometryCache(const std::filesystem::path&)noexcept;
const char* BodyAmmoCacheStatusName(BodyAmmoCacheStatus)noexcept;
} // namespace fvr::bc2
