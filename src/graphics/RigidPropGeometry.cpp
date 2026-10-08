#include "fvr/graphics/RigidPropGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
namespace fvr::graphics {
namespace {
constexpr std::uint64_t Basis=14695981039346656037ull,Prime=1099511628211ull;
unsigned PositionBytes(RigidPropPosition p)noexcept{return p==RigidPropPosition::Float3?12:p==RigidPropPosition::Half4?8:0;}
bool Layout(const RigidPropSection& p,const RigidPropDrawBytes& d)noexcept {
    const auto bytes=PositionBytes(p.position);
    return bytes&&p.indexCount&&p.indexCount%3==0&&p.indexCount<=100000&&p.stride>=bytes+8&&p.stride<=256&&
        p.paletteSize&&p.paletteSize<=256&&p.partPaletteIndex<p.paletteSize&&
        (d.indexBytes==2||d.indexBytes==4)&&d.selectedIndices.size()==std::size_t(p.indexCount)*d.indexBytes&&
        !d.vertices.empty()&&d.vertices.size()<=4*1024*1024;
}
std::optional<std::size_t> At(const RigidPropSection& p,const RigidPropDrawBytes& d,unsigned n)noexcept {
    std::uint32_t index=0;std::memcpy(&index,d.selectedIndices.data()+std::size_t(n)*d.indexBytes,d.indexBytes);
    const auto vertex=std::int64_t(index)+d.baseVertex;if(vertex<0)return {};
    const auto offset=std::uint64_t(d.vertexOffset)+std::uint64_t(vertex)*p.stride;
    const auto required=PositionBytes(p.position)+8;
    if(offset>d.vertices.size()||required>d.vertices.size()-std::size_t(offset))return {};
    return std::size_t(offset);
}
float Half(std::uint16_t h)noexcept {
    const auto exponent=(h>>10)&31,tail=h&1023;
    const float value=exponent==31?(tail?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity()):
        exponent?std::ldexp(float(1024+tail),int(exponent)-25):std::ldexp(float(tail),-24);
    return h&0x8000?-value:value;
}
std::optional<std::array<float,3>> Point(const RigidPropSection& p,const RigidPropDrawBytes& d,std::size_t at,
    const math::Matrix4& inverse,bool flip)noexcept {
    std::array<float,3> v{};
    if(p.position==RigidPropPosition::Float3)std::memcpy(v.data(),d.vertices.data()+at,12);
    else for(unsigned i=0;i<3;++i){std::uint16_t h=0;std::memcpy(&h,d.vertices.data()+at+i*2,2);v[i]=Half(h);}
    for(auto x:v)if(!std::isfinite(x)||std::abs(x)>10000)return {};
    if(flip)v[2]=-v[2];std::array<float,3> result{};
    for(unsigned col=0;col<3;++col){
        double x=inverse.values[3][col];for(unsigned row=0;row<3;++row)x+=double(v[row])*inverse.values[row][col];
        if(!std::isfinite(x)||std::abs(x)>10)return {};result[col]=float(x);
    }
    return result;
}
std::optional<unsigned> Bone(const RigidPropSection& p,const RigidPropDrawBytes& d,std::size_t at)noexcept {
    const auto pos=PositionBytes(p.position);unsigned found=256;
    for(unsigned i=0;i<4;++i){const auto weight=std::to_integer<unsigned>(d.vertices[at+pos+4+i]);
        if(!weight)continue;if(weight!=255||found!=256)return {};
        found=std::to_integer<unsigned>(d.vertices[at+pos+i]);if(found>=p.paletteSize)return {};
    }
    return found==256?std::nullopt:std::optional<unsigned>(found);
}
}
std::optional<RigidPropFingerprint> FingerprintRigidPropDraw(const RigidPropSection& p,const RigidPropDrawBytes& d)noexcept {
    if(!Layout(p,d))return {};RigidPropFingerprint result{Basis,Basis};const auto pos=PositionBytes(p.position);
    for(unsigned i=0;i<p.indexCount;++i){const auto at=At(p,d,i);if(!at)return {};
        for(unsigned b=0;b<pos+8;++b){const auto v=std::to_integer<unsigned char>(d.vertices[*at+b]);
            result.skin=(result.skin^v)*Prime;if(b<pos)result.positions=(result.positions^v)*Prime;}}
    return result;
}
std::optional<RigidPropMesh> ExtractRigidProp(const RigidPropSection& p,const RigidPropDrawBytes& d,
    const math::Matrix4& inverse,RigidPropBasis basis){
    const auto fingerprint=FingerprintRigidPropDraw(p,d);
    if(!fingerprint||!p.vertexSkinHash||!p.positionHash||fingerprint->skin!=p.vertexSkinHash||
        fingerprint->positions!=p.positionHash||!p.expectedPartTriangles||p.expectedPartTriangles>p.indexCount/3||
        !interaction::InverseAnimatedTransform(inverse)||
        (basis!=RigidPropBasis::NativeRightHanded&&basis!=RigidPropBasis::CanonicalLeftHanded))return {};
    const bool flip=basis==RigidPropBasis::NativeRightHanded;
    RigidPropMesh result;result.sourceVertexSkinHash=fingerprint->skin;result.sourcePositionHash=fingerprint->positions;
    result.vertices.reserve(std::size_t(p.expectedPartTriangles)*3);
    for(unsigned i=0;i<p.indexCount;i+=3){std::array<std::size_t,3> at{};std::array<unsigned,3> bones{};
        for(unsigned k=0;k<3;++k){const auto offset=At(p,d,i+k);if(!offset)return {};at[k]=*offset;
            const auto bone=Bone(p,d,*offset);if(!bone)return {};bones[k]=*bone;}
        if(bones[0]!=bones[1]||bones[0]!=bones[2])return {};
        if(bones[0]!=p.partPaletteIndex)continue;
        std::array<std::array<float,3>,3> points{};
        for(unsigned k=0;k<3;++k){const auto point=Point(p,d,at[k],inverse,flip);if(!point)return {};points[k]=*point;}
        // Reflection reverses handedness. Reverse the copied triangle as well
        // so its outward geometric normal reflects with the original surface.
        if(flip)std::swap(points[1],points[2]);
        const std::array<double,3> a{points[1][0]-points[0][0],points[1][1]-points[0][1],points[1][2]-points[0][2]},
            b{points[2][0]-points[0][0],points[2][1]-points[0][1],points[2][2]-points[0][2]};
        std::array<double,3> normal{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
        const auto length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
        if(!std::isfinite(length))return {};
        // Authored degenerate triangles remain degenerate; they never add area.
        for(auto& v:normal)v=length>1e-12?v/length:0;
        for(const auto& v:points)result.vertices.push_back({v[0],v[1],v[2],float(normal[0]),float(normal[1]),float(normal[2])});
        if(result.vertices.size()>std::size_t(p.expectedPartTriangles)*3)return {};
    }
    if(result.vertices.size()!=std::size_t(p.expectedPartTriangles)*3)return {};return result;
}
} // namespace fvr::graphics
