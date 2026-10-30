#pragma once
#define ExperimentalMagazineGeometry PreservedBaseline235Geometry
#include "../resource-batch227/zero-bolt-batch/BaselineGeometry.h"
#undef ExperimentalMagazineGeometry
#define ExperimentalMagazineGeometry PreservedResource233Geometry
#include "../resource-batch227/zero-bolt-batch/ZeroBoltGeometry.h"
#undef ExperimentalMagazineGeometry
#define ExperimentalMagazineGeometry Scar235Geometry
#include "../resource-enrollment235-scar/ScarGeometry.h"
#undef ExperimentalMagazineGeometry
#define ExperimentalMagazineGeometry F2000240Geometry
#include "../resource-enrollment240-f2000/F2000Geometry.h"
#undef ExperimentalMagazineGeometry
#define ExperimentalMagazineGeometry SampledPistol242Geometry
#include "../experimental-pistol-sampled242/PistolGeometry.h"
#undef ExperimentalMagazineGeometry
#define ExperimentalMagazineGeometry Opening426Geometry
#include "../opening426/NambuGeometry.generated.h"
#undef ExperimentalMagazineGeometry
#define ExperimentalMagazineGeometry Pistol41Geometry
#include "../mechanism-family41/PistolGeometry.generated.h"
#undef ExperimentalMagazineGeometry
namespace fvr::bc2::generated {
inline const auto Magazines45PreservedExperimentalMagazineGeometry=[] {
 std::array<MagazineGeometryProfile,PreservedBaseline235Geometry.size()+PreservedResource233Geometry.size()+Scar235Geometry.size()+F2000240Geometry.size()+SampledPistol242Geometry.size()+Opening426Geometry.size()+Pistol41Geometry.size()> out{};
 std::size_t n=0;for(const auto& p:PreservedBaseline235Geometry)out[n++]=p;
 for(const auto& p:PreservedResource233Geometry)out[n++]=p;
 for(const auto& p:Scar235Geometry)out[n++]=p;
 for(const auto& p:F2000240Geometry)out[n++]=p;
 for(const auto& p:SampledPistol242Geometry)out[n++]=p;
 for(const auto& p:Opening426Geometry)out[n++]=p;
 for(const auto& p:Pistol41Geometry)out[n++]=p;
 return out;
}();
}

#define generated magazines45_added
#include "Geometry.additions.generated.h"
#undef generated
namespace fvr::bc2::generated {
inline const auto ExperimentalMagazineGeometry=[] {
 std::array<MagazineGeometryProfile,Magazines45PreservedExperimentalMagazineGeometry.size()+magazines45_added::ExperimentalMagazineGeometry.size()> out{};
 std::size_t n=0;for(const auto& p:Magazines45PreservedExperimentalMagazineGeometry)out[n++]=p;
 for(const auto& p:magazines45_added::ExperimentalMagazineGeometry)out[n++]=p;
 return out;
}();
}
