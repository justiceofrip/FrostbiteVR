#include "Test.h"
#include "Bc2ReticleAperture.h"
#include <limits>
#include <cstring>
#include <cmath>
using namespace fvr;
int main(){
 const auto& p=bc2::AcogReticleApertureProfile();
 CHECK(p.meshDataSha256.size()==64);CHECK(p.assetPaletteId==35&&p.boneNameHash==0xf7315722);
 CHECK(bc2::MatchesAcogReticleSection(p.mesh,p.material,0,12,5331,165240,68,8));
 CHECK(!bc2::MatchesAcogReticleSection(p.mesh,p.material,1,12,5331,165240,68,8));
 CHECK(!bc2::MatchesAcogReticleSection(p.mesh,"Objects/Weapons/Unlock/ACOG_4X/Shaders/ACOG4X_Standard",0,12,5331,165240,68,8));
 CHECK(!bc2::MatchesAcogReticleSection("another mesh",p.material,0,12,5331,165240,68,8));
 CHECK(!bc2::MatchesAcogReticleSection(p.mesh,p.material,0,12,5332,165240,68,8));
 CHECK(!bc2::MatchesAcogReticleSection(p.mesh,p.material,0,12,5331,165241,68,8));
 CHECK(!bc2::MatchesAcogReticleSection(p.mesh,p.material,0,12,5331,165240,48,8));
 CHECK(!bc2::MatchesAcogReticleSection(p.mesh,p.material,0,12,5331,165240,68,9));
 auto eval=[&](math::Vec3 eye){return math::EvaluateOpticAperture(p.apertures,p.reticle,eye);};
 CHECK(eval({0,1.1439183f,-.25f}).visibility==math::ApertureVisibility::Visible);
 CHECK(eval({.25f,1.1439183f,-.25f}).CanSkipDraw());
 CHECK(eval({0,1.4f,-.25f}).CanSkipDraw());
 CHECK(eval({0,1.1439183f,.2f}).CanSkipDraw());
 // An aiming eye and the other eye make independent visibility decisions.
 CHECK(eval({0,1.1439183f,-.25f}).visibility==math::ApertureVisibility::Visible);
 CHECK(eval({-.064f,1.1439183f,-.25f}).CanSkipDraw());
 CHECK(eval({0,1.1439183f,-.25f}).visibility==math::ApertureVisibility::Visible);
 bool partial=false;
 for(unsigned i=0;i<2000;++i){const auto r=eval({float(i)*.00002f,1.1439183f,-.25f});
  CHECK(r.visibility!=math::ApertureVisibility::Invalid);if(r.visibility==math::ApertureVisibility::Partial){CHECK(!r.CanSkipDraw());partial=true;}}
 CHECK(partial);CHECK(p.apertures[0].z<p.apertures[1].z&&p.apertures[1].z<p.reticle.minimum.z);
 // Reconstruct authored bind eye using the EXACT submitted native skin.
 // Asymmetric rotation + translation catch transpose/handedness/bone-world mistakes.
 const math::Vec3 local{.012f,1.14f,-.25f};
 math::Matrix4 skin{};skin.values={std::array<float,4>{.8f,0,.6f,0},
  {0,1,0,0},{-.6f,0,.8f,0},{13,7,-5,1}};
 auto pack=[&](){std::array<std::byte,48> out{};for(unsigned c=0;c<3;++c)for(unsigned r=0;r<4;++r)
  std::memcpy(out.data()+c*16+r*4,&skin.values[r][c],4);return out;};
 auto transform=[&](){const auto& m=skin.values;return math::Vec3{
  local.x*m[0][0]+local.y*m[1][0]+local.z*m[2][0]+m[3][0],
  local.x*m[0][1]+local.y*m[1][1]+local.z*m[2][1]+m[3][1],
  -(local.x*m[0][2]+local.y*m[1][2]+local.z*m[2][2]+m[3][2])};};
 auto recovered=bc2::AcogEyeInBindSpace(pack(),transform());CHECK(recovered);
 CHECK(Near(recovered->x,local.x,.00001f)&&Near(recovered->y,local.y,.00001f)&&Near(recovered->z,local.z,.00001f));
 skin.values[0][0]+=.001f;skin.values[2][2]-=.001f; // Native blended near-rigid basis.
 recovered=bc2::AcogEyeInBindSpace(pack(),transform());CHECK(recovered);
 CHECK(Near(recovered->x,local.x,.00001f)&&Near(recovered->y,local.y,.00001f)&&Near(recovered->z,local.z,.00001f));
 skin.values[1][1]=0;CHECK(!bc2::AcogEyeInBindSpace(pack(),transform()));
 skin.values[1][1]=std::numeric_limits<float>::quiet_NaN();CHECK(!bc2::AcogEyeInBindSpace(pack(),{0,0,0}));
 return 0;
}
