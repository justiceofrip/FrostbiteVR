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
namespace fvr::bc2::generated {
inline const auto ExperimentalMagazineGeometry=[] {
 std::array<MagazineGeometryProfile,PreservedBaseline235Geometry.size()+PreservedResource233Geometry.size()+Scar235Geometry.size()+F2000240Geometry.size()+SampledPistol242Geometry.size()> out{};
 std::size_t n=0;for(const auto& p:PreservedBaseline235Geometry)out[n++]=p;
 for(const auto& p:PreservedResource233Geometry)out[n++]=p;
 for(const auto& p:Scar235Geometry)out[n++]=p;
 for(const auto& p:F2000240Geometry)out[n++]=p;
 for(const auto& p:SampledPistol242Geometry)out[n++]=p;
 return out;
}();
}
