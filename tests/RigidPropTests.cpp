#include "fvr/graphics/D3D11RigidPropRenderer.h"
#include "fvr/interaction/AmmoSupplyProp.h"
#include "Bc2Xm8MagazineCalibration.h"
#include "Bc2BeltPropProfiles.h"
#include "Test.h"
#include <cstring>
#include <iostream>
#include <limits>
using namespace fvr;using namespace fvr::graphics;using namespace fvr::interaction;
namespace {
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
struct Source {
    std::vector<std::byte> vb=std::vector<std::byte>(8+6*24),ib=std::vector<std::byte>(6*2);
    RigidPropSection section{6,24,2,1,RigidPropPosition::Float3,0,0,1};
    Source(){for(unsigned i=0;i<6;++i){const std::array<float,3> p{float(i%3==1),float(i%3==2),.25f};
        std::memcpy(vb.data()+8+i*24,p.data(),12);vb[8+i*24+12]=std::byte(i<3?0:1);vb[8+i*24+16]=std::byte{255};
        const auto index=std::uint16_t(i);std::memcpy(ib.data()+i*2,&index,2);}Hash();}
    RigidPropDrawBytes Draw()const{return {vb,ib,2,8,0};}
    void Hash(){const auto h=FingerprintRigidPropDraw(section,Draw());section.vertexSkinHash=h?h->skin:0;section.positionHash=h?h->positions:0;}
    auto Extract()const{return ExtractRigidProp(section,Draw(),Pose(),RigidPropBasis::NativeRightHanded);}
};
int ExactBoneAndIndependentBytes(){Source s;const auto original=s.vb;const auto mesh=s.Extract();CHECK(mesh&&mesh->vertices.size()==3);CHECK(s.vb==original);
    CHECK(Near(mesh->vertices[0].z,-.25f));const auto saved=mesh->vertices[0].z;s.vb.assign(s.vb.size(),std::byte{0});CHECK(mesh->vertices[0].z==saved);return 0;}
int BadHashesIndicesAndBase(){
    for(unsigned k=0;k<8;++k){Source s;auto d=s.Draw();auto p=s.section;
        if(k==0)++p.vertexSkinHash;if(k==1)++p.positionHash;if(k==2)d.baseVertex=-1;
        if(k==3)d.baseVertex=std::numeric_limits<std::int32_t>::max();if(k==4)d.vertexOffset=0xffffffffu;
        if(k==5)d.selectedIndices=d.selectedIndices.first(10);if(k==6)d.indexBytes=1;if(k==7)p.indexCount=5;
        CHECK(!ExtractRigidProp(p,d,Pose(),RigidPropBasis::NativeRightHanded));}
    Source s;std::vector<std::byte> ib(24);for(unsigned i=0;i<6;++i){const std::uint32_t n=i+2;std::memcpy(ib.data()+i*4,&n,4);}
    auto d=s.Draw();d.selectedIndices=ib;d.indexBytes=4;d.baseVertex=-2;CHECK(ExtractRigidProp(s.section,d,Pose(),RigidPropBasis::NativeRightHanded));
    std::uint32_t bad=0xffffffff;std::memcpy(ib.data(),&bad,4);CHECK(!ExtractRigidProp(s.section,d,Pose(),RigidPropBasis::NativeRightHanded));return 0;}
int MixedSkinAndTriangleRejected(){for(unsigned k=0;k<5;++k){Source s;
    if(k==0){s.vb[8+3*24+16]=std::byte{127};s.vb[8+3*24+17]=std::byte{128};}
    if(k==1)s.vb[8+4*24+12]=std::byte{0};if(k==2)s.vb[8+3*24+12]=std::byte{2};
    if(k==3)s.vb[8+16]=std::byte{0};if(k==4)s.section.expectedPartTriangles=2;s.Hash();CHECK(!s.Extract());}return 0;}
int BasisAndWindingAreExplicit(){Source s;auto inverse=Pose(.1f,.2f,.3f);
    const auto native=ExtractRigidProp(s.section,s.Draw(),inverse,RigidPropBasis::NativeRightHanded);
    const auto canonical=ExtractRigidProp(s.section,s.Draw(),inverse,RigidPropBasis::CanonicalLeftHanded);
    CHECK(native&&canonical&&Near(native->vertices[0].z,.05f)&&Near(canonical->vertices[0].z,.55f));
    // Native reflection also reverses the copied triangle's winding; canonical
    // input retains its winding. Normals follow the actual copied surface.
    CHECK(Near(native->vertices[0].nz,-1)&&Near(canonical->vertices[0].nz,1));inverse.values[0][0]=0;CHECK(!ExtractRigidProp(s.section,s.Draw(),inverse,RigidPropBasis::NativeRightHanded));
    CHECK(!ExtractRigidProp(s.section,s.Draw(),Pose(),static_cast<RigidPropBasis>(8)));return 0;}
int HalfPositionsAndNonfinite(){Source s;auto p=s.section;p.position=RigidPropPosition::Half4;
    for(unsigned i=0;i<6;++i){auto* b=s.vb.data()+8+i*24;std::memset(b,0,24);
        const std::uint16_t one=0x3c00;std::memcpy(b+(i%3==1?0:2),&one,2);std::memcpy(b+4,&one,2);
        b[8]=std::byte(i<3?0:1);b[12]=std::byte{255};}
    auto hash=FingerprintRigidPropDraw(p,s.Draw());CHECK(hash);p.vertexSkinHash=hash->skin;p.positionHash=hash->positions;
    const auto out=ExtractRigidProp(p,s.Draw(),Pose(),RigidPropBasis::NativeRightHanded);CHECK(out&&Near(out->vertices[0].z,-1));
    const std::uint16_t nan=0x7c01;std::memcpy(s.vb.data()+8+3*24,&nan,2);hash=FingerprintRigidPropDraw(p,s.Draw());p.vertexSkinHash=hash->skin;p.positionHash=hash->positions;
    CHECK(!ExtractRigidProp(p,s.Draw(),Pose(),RigidPropBasis::NativeRightHanded));return 0;}
RigidPropEye Eye(){return {1,2,3,4,5,6,7,8,9,10,0,1000000000,1100000000};}
int DefaultOffAndEachEyeOnce(){RigidPropFrameGate off;auto f=Eye();CHECK(!off.Admit(f,f,f.observedNs));RigidPropFrameGate on(true);
    CHECK(on.Admit(f,f,f.observedNs));CHECK(!on.Admit(f,f,f.observedNs));auto changed=f;++changed.input;++changed.observedNs;CHECK(!on.Admit(changed,changed,changed.observedNs));
    f.eye=1;f.view=99;CHECK(on.Admit(f,f,f.observedNs));++f.frame;++f.observedNs;CHECK(on.Admit(f,f,f.observedNs));
    f.request=400;f.frame=1;CHECK(on.Admit(f,f,f.observedNs));return 0;}
int StaleAndDifferentOwnerEye(){for(unsigned k=0;k<8;++k){RigidPropFrameGate gate(true);auto f=Eye(),current=f;auto now=f.observedNs;
    if(k==0)++current.weapon;if(k==1)++current.equipmentGeneration;if(k==2)++current.space;if(k==3)++current.view;
    if(k==4)now=f.deadlineNs;if(k==5)now--;if(k==6)f.eye=current.eye=2;if(k==7)f.deadlineNs=current.deadlineNs+=1;
    CHECK(!gate.Admit(f,current,now));}return 0;}
struct Supply {
    AmmoSupplyVisualSample v;InputFrame input;
    Supply(){v.enabled=true;v.input={{11,12,13,14},15,1000000000,1100000000,1000000000,true,{true,true},{true,false}};
        auto& s=v.source;s.identity={v.input.owner,{17,13},{18,1},{19,1},14};s.sequence=20;s.observedNs=1000000000;s.deadlineNs=1100000000;
        s.verified=true;s.family=ReloadInsertionFamily::Magazine;s.reserveUnits=90;s.objectUnits=30;
        v.gun={{21,v.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.identity.weapon,{22,1},0},1100000000,15};
        v.contact={{-.2f,-.55f,.02f},.15f};v.frame=SupplyAnchorFrame::RecenteredBody;
        input.generation=15;input.spaceGeneration=14;input.predictedNs=1000000000;input.focused=input.headValid=true;
        input.referenceHead.position={0,1.7f,0};input.head.position={.1f,1.6f,-.2f};
        input.referenceHead.orientation={0,std::sin(.3f),0,std::cos(.3f)};input.head.orientation={0,std::sin(.9f),0,std::cos(.9f)};
        for(auto& h:input.hands){h.gripTracked=h.aimTracked=true;}}
};
int SamePickupAnchorAndMeasuredGrasp(){for(float units:{.01f,1.f,100.f,1000.f})for(auto frame:{SupplyAnchorFrame::HeadYaw,SupplyAnchorFrame::RecenteredBody}){
    Supply s;s.input.worldUnitsPerMeter=units;s.v.frame=frame;const auto anchor=AmmoSupplyAnchorWorld(s.input,Pose(30,-10,20),s.v);CHECK(anchor);
    const auto grasp=bc2::xm8_magazine_calibration::ItemFromHand;const auto inv=InverseRigid(grasp);CHECK(inv);
    const auto plan=BuildAmmoSupplyPropPose(s.v,s.input,Pose(30,-10,20),*inv,1000000000);CHECK(plan&&RigidPropWorldValid(plan->partWorld));
    // The authored contact origin, transformed through the copied part, lands
    // exactly on the pickup anchor for rotated heads and non-unit game scale.
    for(unsigned c=0;c<3;++c){double at=plan->partWorld.values[3][c];for(unsigned r=0;r<3;++r)at+=double(grasp.values[3][r])*plan->partWorld.values[r][c];
        CHECK(std::abs(at-anchor->values[3][c])<.02f);}
    const auto proj=math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);CHECK(proj&&RigidPropClipTransform(plan->partWorld,Pose(),*proj));
    CHECK(plan->source.source.reserveUnits==90&&plan->source.source.objectUnits==30);
}return 0;}
int SupplyDisappearsOnClaimOrSourceChange(){Supply s;const auto plan=BuildAmmoSupplyPropPose(s.v,s.input,Pose(),Pose(),1000000000);CHECK(plan);
    for(unsigned k=0;k<7;++k){auto v=s.v;if(k==0)v.occupied=true;if(k==1)v.source.reserveUnits=0;if(k==2)v.input.focused=false;
        if(k==3)++v.gun.token.id;if(k==4)++v.source.identity.pool.generation;if(k==5)++v.source.deadlineNs;if(k==6)v.input.tracked[0]=false;
        CHECK(!AmmoSupplyPropRetained(*plan,v,1000000000));}
    CHECK(!AmmoSupplyPropRetained(*plan,s.v,1100000000));return 0;}
int DisabledBackendDoesNotTouchDevice(){D3D11RigidPropRenderer renderer;CHECK(!renderer.Initialize(reinterpret_cast<ID3D11DeviceContext*>(1)));
    Source s;const RigidPropSectionUpload section{*s.Extract()};CHECK(!renderer.Upload({1,2,3},std::span(&section,1)));
    CHECK(!renderer.Draw({1,2,3},Eye(),Eye(),1000000000,Pose(),Pose(),Pose(),{}));CHECK(!bc2::IndependentBeltPropNativeAdmitted);return 0;}
}
int main(){if(ExactBoneAndIndependentBytes()||BadHashesIndicesAndBase()||MixedSkinAndTriangleRejected()||BasisAndWindingAreExplicit()||HalfPositionsAndNonfinite()||DefaultOffAndEachEyeOnce()||StaleAndDifferentOwnerEye()||SamePickupAnchorAndMeasuredGrasp()||SupplyDisappearsOnClaimOrSourceChange()||DisabledBackendDoesNotTouchDevice())return 1;
    std::cout<<"RigidProp: 10 groups passed; CPU only, no GPU/native rendering acceptance\n";}
