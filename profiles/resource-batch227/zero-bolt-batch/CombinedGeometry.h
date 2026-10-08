// Composes baseline geometry unchanged with exact disabled candidate geometry.
#pragma once
#define ExperimentalMagazineGeometry PreservedBaselineMagazineGeometry
#include "BaselineGeometry.h"
#undef ExperimentalMagazineGeometry
#define ExperimentalMagazineGeometry PreparedZeroBoltMagazineGeometry
#include "ZeroBoltGeometry.h"
#undef ExperimentalMagazineGeometry
namespace fvr::bc2::generated {
inline const auto ExperimentalMagazineGeometry=[] {
 std::array<MagazineGeometryProfile,PreservedBaselineMagazineGeometry.size()+PreparedZeroBoltMagazineGeometry.size()> out{};
 std::size_t n=0;for(const auto& g:PreservedBaselineMagazineGeometry)out[n++]=g;
 for(const auto& g:PreparedZeroBoltMagazineGeometry)out[n++]=g;
 return out;
}();
}
