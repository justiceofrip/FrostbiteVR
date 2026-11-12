#pragma once
#include "fvr/graphics/SpentCase.h"
#include "fvr/graphics/D3D11BodyPropCompositor.h"
#include <cmath>

namespace fvr::graphics {
inline BodyPropAsset MakeSpentCaseAsset(){
    BodyPropAsset asset;asset.key=SpentCaseGeometry;
    RigidPropSectionUpload shell;shell.color={.58f,.39f,.12f,1};
    shell.mesh.sourceVertexSkinHash=SpentCaseGeometry.asset;
    shell.mesh.sourcePositionHash=SpentCaseGeometry.part;
    // Explicit generic visual dimensions, not recovered native case geometry.
    // The hollow mouth faces -Z; the closed base faces the mod's +Z outward axis.
    constexpr float outer=.020f,inner=.0175f,mouth=-.0225f,base=.0225f,insideBase=.019f;
    constexpr unsigned segments=24;
    const auto vertex=[](float radius,float angle,float z,float nx,float ny,float nz){
        return RigidPropVertex{radius*std::cos(angle),radius*std::sin(angle),z,nx,ny,nz};};
    const auto triangle=[&](RigidPropVertex a,RigidPropVertex b,RigidPropVertex c){
        for(const auto v:{a,b,c})shell.mesh.vertices.push_back(v);};
    for(unsigned n=0;n<segments;++n){
        const float a=float(n)*6.28318530718f/segments,b=float(n+1)*6.28318530718f/segments;
        const float ax=std::cos(a),ay=std::sin(a),bx=std::cos(b),by=std::sin(b);
        const auto a0=vertex(outer,a,mouth,ax,ay,0),a1=vertex(outer,a,base,ax,ay,0);
        const auto b0=vertex(outer,b,mouth,bx,by,0),b1=vertex(outer,b,base,bx,by,0);
        triangle(a0,b0,b1);triangle(a0,b1,a1);
        const auto ai0=vertex(inner,a,mouth,-ax,-ay,0),ai1=vertex(inner,a,insideBase,-ax,-ay,0);
        const auto bi0=vertex(inner,b,mouth,-bx,-by,0),bi1=vertex(inner,b,insideBase,-bx,-by,0);
        triangle(ai0,bi1,bi0);triangle(ai0,ai1,bi1);
        const auto ar=vertex(outer,a,mouth,0,0,-1),br=vertex(outer,b,mouth,0,0,-1);
        const auto air=vertex(inner,a,mouth,0,0,-1),bir=vertex(inner,b,mouth,0,0,-1);
        triangle(ar,bir,br);triangle(ar,air,bir);
        triangle({0,0,base,0,0,1},vertex(outer,a,base,0,0,1),vertex(outer,b,base,0,0,1));
        triangle({0,0,insideBase,0,0,-1},vertex(inner,b,insideBase,0,0,-1),vertex(inner,a,insideBase,0,0,-1));
    }
    asset.sections.push_back(std::move(shell));return asset;
}
}
