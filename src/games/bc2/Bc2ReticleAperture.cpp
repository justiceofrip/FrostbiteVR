#include "Bc2ReticleAperture.h"
#include "fvr/interaction/TrackingMath.h"
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
const Bc2ReticleApertureProfile& AcogReticleApertureProfile()noexcept{
    // Glass perimeter vertices, not the domed centre vertices. The reticle box
    // is deliberately conservative, derived from extrema rather than exporting
    // the authored red-dot triangles. Native +Z points through this optic.
    static const Bc2ReticleApertureProfile profile=[] {
        Bc2ReticleApertureProfile p;
        p.mesh="Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh";
        p.material="Objects/Weapons/Unlock/ACOG_4X/Shaders/ACOG_RedDot";
        p.section="jntWpn_10_ACOG_RedDot";
        p.meshDataSha256="801361ce3160ef3392a7bb3317d77ed70b8631fb9abc661b67517314059a36a9";
        p.lod=0;p.indexCount=12;p.firstIndex=5331;p.vertexOffset=165240;
        p.stride=68;p.vertices=8;p.assetPaletteId=35;p.boneNameHash=0xf7315722;
        auto& rear=p.apertures[0];rear.z=-.1631731241941452f;rear.count=8;
        rear.outline={math::AperturePoint{0,1.1593289375305176f},
            {.010896957479417324f,1.1548151969909668f},{.01541062816977501f,1.1439182758331299f},
            {.010896959342062473f,1.1330212354660034f},{0,1.1285076141357422f},
            {-.010896955616772175f,1.1330212354660034f},{-.015410623513162136f,1.1439182758331299f},
            {-.010896955616772175f,1.1548151969909668f}};
        auto& front=p.apertures[1];front.z=-.04944847151637077f;front.count=8;
        front.outline={math::AperturePoint{0,1.1627293825149536f},
            {.012451053597033024f,1.1559113264083862f},{.01942591369152069f,1.1439276933670044f},
            {.014071192592382431f,1.130069375038147f},{0,1.1240707635879517f},
            {-.014071189798414707f,1.130069375038147f},{-.01942591182887554f,1.1439276933670044f},
            {-.012451049871742725f,1.1559113264083862f}};
        p.reticle={{-.0012105780187994242f,1.1427199840545654f,.06431540101766586f},
                   {.0012105826754122972f,1.1439274549484253f,.06440060585737228f}};
        return p;
    }();return profile;
}
std::optional<math::Vec3> AcogEyeInBindSpace(
    std::span<const std::byte,48> submittedNativeSkin,math::Vec3 eye)noexcept{
    if(!std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z))return {};
    math::Matrix4 canonical{};canonical.values[3][3]=1;
    for(unsigned column=0;column<3;++column)for(unsigned row=0;row<4;++row)
        std::memcpy(&canonical.values[row][column],submittedNativeSkin.data()+column*16+row*4,4);
    for(unsigned i=0;i<4;++i){canonical.values[2][i]=-canonical.values[2][i];canonical.values[i][2]=-canonical.values[i][2];}
    const auto inverse=interaction::InverseAnimatedTransform(canonical);if(!inverse)return {};
    const auto& m=inverse->values;
    math::Vec3 point{eye.x*m[0][0]+eye.y*m[1][0]+eye.z*m[2][0]+m[3][0],
        eye.x*m[0][1]+eye.y*m[1][1]+eye.z*m[2][1]+m[3][1],
        -(eye.x*m[0][2]+eye.y*m[1][2]+eye.z*m[2][2]+m[3][2])};
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z))return {};
    return point;
}
bool MatchesAcogReticleSection(std::string_view mesh,std::string_view material,
    std::uint32_t lod,std::uint32_t indexCount,std::uint32_t firstIndex,
    std::uint32_t vertexOffset,std::uint32_t stride,std::uint32_t vertices)noexcept{
    const auto& p=AcogReticleApertureProfile();
    return mesh==p.mesh&&material==p.material&&lod==p.lod&&indexCount==p.indexCount&&
        firstIndex==p.firstIndex&&vertexOffset==p.vertexOffset&&stride==p.stride&&vertices==p.vertices;
}
}
