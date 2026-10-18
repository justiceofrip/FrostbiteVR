#pragma once
#include "fvr/graphics/VehicleReticle.h"
#include "fvr/graphics/D3D11BodyPropCompositor.h"
namespace fvr::graphics {
inline BodyPropAsset MakeVehicleReticleAsset(){
    BodyPropAsset asset;asset.key=VehicleReticleGeometry;
    RigidPropSectionUpload section;section.color={1,1,1,1};
    section.mesh.sourceVertexSkinHash=VehicleReticleGeometry.asset;section.mesh.sourcePositionHash=VehicleReticleGeometry.part;
    const auto rectangle=[&](float l,float b,float r,float t){
        const RigidPropVertex a{l,b,0,0,1,0},c{r,t,0,0,1,0};
        const RigidPropVertex br{r,b,0,0,1,0},tl{l,t,0,0,1,0};
        for(const auto& v:{a,br,c,a,c,tl})section.mesh.vertices.push_back(v);
    };
    // Keep the sight center, gap and outer extent fixed. The stroke must cover
    // pixels at the native 1920x1080 projection, including subpixel eye offsets.
    rectangle(-1,-.25f,-.35f,.25f);rectangle(.35f,-.25f,1,.25f);
    rectangle(-.25f,-1,.25f,-.35f);rectangle(-.25f,.35f,.25f,1);
    asset.sections.push_back(std::move(section));return asset;
}
}
