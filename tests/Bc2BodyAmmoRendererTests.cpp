#include "Bc2BodyAmmoRenderer.h"
#include "Bc2BodyAmmo.h"
#include "Bc2MagazineConsumerFixture.h"
#include "Test.h"
#include <cstring>
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using namespace fvr::graphics;
namespace {
auto Pose(){return reload_insertion_detail::Identity();}
struct Source {
    std::vector<std::byte> vb=std::vector<std::byte>(3*24),ib=std::vector<std::byte>(3*2);
    BeltPropSectionProfile profile{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","plastic",{3,24,1,0,RigidPropPosition::Float3,0,0,1}};
    Source(){for(unsigned i=0;i<3;++i){const std::array<float,3> p{float(i==1)*.03f,float(i==2)*.04f,0};
        std::memcpy(vb.data()+i*24,p.data(),12);vb[i*24+16]=std::byte{255};const auto n=std::uint16_t(i);std::memcpy(ib.data()+i*2,&n,2);}
        const auto hash=FingerprintRigidPropDraw(profile.geometry,Draw());profile.geometry.vertexSkinHash=hash->skin;profile.geometry.positionHash=hash->positions;}
    RigidPropDrawBytes Draw()const{return {vb,ib,2,0,0};}
    auto Section()const{return BodyAmmoSectionBytes{&profile,Draw(),Pose(),RigidPropBasis::NativeRightHanded};}
    auto Geometry()const{const auto section=Section();return BuildBodyAmmoGeometry(profile.asset,profile.mesh,profile.part,Xm8MagazineEquipment().geometry->rigFingerprint,std::span(&section,1));}
};
BodyAmmoRenderSource Render(){magazine_consumer_fixture::Fixture f;f.Send();auto authority=std::make_shared<BodyAmmoTracking>();
    authority->magazine=f.result.tracking;auto& v=authority->visual;v.enabled=true;
    const auto owners=BindMagazineOwners(f.s.input.owner,f.s.weapon,f.reserve,0,f.now,f.s.family);
    v.source=*MagazineSupply(*owners,f.reserve,f.s.trackingEpoch,f.now);v.input=f.s.input;v.gun=*f.gun;
    v.contact=*ChestAmmoSupply().alternateContact;v.frame=SupplyAnchorFrame::RecenteredBody;
    const auto& g=*Xm8MagazineEquipment().geometry;BodyAmmoRenderSource s;s.asset=g.asset;s.mesh=g.mesh;s.part=g.bones.magazine;s.rigFingerprint=g.rigFingerprint;
    s.geometry=MakeBodyAmmoGeometryKey(s.asset,s.mesh,s.part,s.rigFingerprint);s.prop.partWorld=Pose();s.prop.source=v;s.authority=authority;return s;}
BodyAmmoBoundaryProof Proof(){BodyAmmoBoundaryProof p;p.eye={1,2,3,4,0};p.immediateContext=7;p.observedNs=1000000000;p.deadlineNs=1100000000;
    p.target={8,9,10,11,1920,1080,1920,1080,1,1,0,0,28,45,true,true,true};p.queryObservationGeneration=18;
    p.nativeDrawReturned=p.diagnosticQueryEnded=p.nativeQueriesObserved=p.depthProjectionAssociated=true;
    p.eyeView=Pose();p.projection=*math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);return p;}
int GeometryIsOwnedAndExact(){Source s;const auto g=s.Geometry();CHECK(g&&g->sections.size()==1);const auto original=g->sections[0].mesh.vertices[1];
    CHECK(BodyAmmoGeometryMatches(*g,Render()));s.vb.assign(s.vb.size(),std::byte{0});CHECK(g->sections[0].mesh.vertices[1].x==original.x);
    for(unsigned n=0;n<5;++n){auto r=Render();if(n==0)r.asset="AR_B";if(n==1)r.mesh="Mesh_B";if(n==2)r.part="other";if(n==3)++r.rigFingerprint;if(n==4)++r.geometry.revision;
        CHECK(!BodyAmmoGeometryMatches(*g,r));}return 0;}
int ExactSourceBeforeAnyUpload(){for(unsigned n=0;n<7;++n){Source s;auto section=s.Section();auto p=s.profile;
    if(n==0)p.asset="different";if(n==1)p.mesh="other";if(n==2)p.part="other";if(n==3)++p.geometry.vertexSkinHash;
    if(n==4)++p.geometry.positionHash;if(n==5)p.geometry.expectedPartTriangles=2;if(n==6)section.canonicalInverseBind.values[0][0]=0;
    section.profile=&p;CHECK(!BuildBodyAmmoGeometry(s.profile.asset,s.profile.mesh,s.profile.part,Xm8MagazineEquipment().geometry->rigFingerprint,std::span(&section,1)));}
    Source s;const std::array<BodyAmmoSectionBytes,2> duplicate{s.Section(),s.Section()};CHECK(!BuildBodyAmmoGeometry(s.profile.asset,s.profile.mesh,s.profile.part,Xm8MagazineEquipment().geometry->rigFingerprint,duplicate));
    CHECK(!graphics::ValidRigidPropKey(MakeBodyAmmoGeometryKey("","Mesh_A","mag",19)));return 0;}
int SourceClaimLeaseAndReplacement(){auto r=Render();const auto now=r.prop.source.input.observedNs;CHECK(BodyAmmoSourceRetained(r,r,now));
    for(unsigned n=0;n<7;++n){auto current=r;if(n==0)current.prop.source.occupied=true;if(n==1)++current.prop.source.gun.token.id;
        if(n==2)++current.prop.source.input.owner.equipGeneration;if(n==3)++current.prop.source.source.identity.pool.generation;
        if(n==4)++current.prop.source.input.deadlineNs;if(n==5)current.prop.source.source.reserveUnits=0;if(n==6)current.part="same_hash_is_not_authority";
        CHECK(!BodyAmmoSourceRetained(r,current,now));}CHECK(!BodyAmmoSourceRetained(r,r,r.prop.source.input.deadlineNs));return 0;}
int PreDrawSelectedLeaseCannotRenew(){auto original=Render();const auto now=original.prop.source.input.observedNs;
    auto pre=std::make_shared<BodyAmmoTracking>(*original.authority);auto oldMesh=std::make_shared<SelectedMeshesSnapshot>(*pre->magazine->selected);
    oldMesh->deadlineNs=now+5000000;pre->magazine->selected=oldMesh;original.authority=pre;CHECK(BodyAmmoSourceRetained(original,original,now));
    auto current=original;auto post=std::make_shared<BodyAmmoTracking>(*pre);auto newMesh=std::make_shared<SelectedMeshesSnapshot>(*oldMesh);
    ++newMesh->sequence;newMesh->observedNs=now+10000000;newMesh->deadlineNs=now+80000000;post->magazine->selected=newMesh;current.authority=post;
    CHECK(BodyAmmoFresh(*post,now+10000000));CHECK(interaction::AmmoSupplyPropRetained(original.prop,current.prop.source,now+10000000));
    CHECK(!BodyAmmoSourceRetained(original,current,now+10000000));return 0;}
int ProofCannotBeInferredFromTargetShape(){auto p=Proof();CHECK(!BodyAmmoBoundaryMissing(p,p.eye,p.target,p.immediateContext,1000000000));
    const std::array<unsigned,10> wanted{BodyAmmoNoQueryProof,BodyAmmoQueryActive,BodyAmmoNoDepthProjectionProof,BodyAmmoStaleProof,
        BodyAmmoWrongEye,BodyAmmoNoDepth,BodyAmmoTargetLayout,BodyAmmoNoDepthProjectionProof,BodyAmmoWrongEye,BodyAmmoNoQueryProof};
    for(unsigned n=0;n<wanted.size();++n){auto q=p;auto target=p.target;auto eye=p.eye;auto context=p.immediateContext;auto now=1000000000ll;
        if(n==0)q.nativeQueriesObserved=false;if(n==1)q.activeScopedQueries=1;if(n==2)q.depthProjectionAssociated=false;if(n==3)now=q.deadlineNs;
        if(n==4)++eye.frame;if(n==5)target.depthView=0;if(n==6)target.depthWidth=1;if(n==7)++target.depthResource;if(n==8)++context;if(n==9)q.diagnosticQueryEnded=false;
        CHECK(BodyAmmoBoundaryMissing(q,eye,target,context,now)&wanted[n]);}return 0;}
int DisabledAndUnprovenNeverTouchGpu(){Bc2BodyAmmoRenderer renderer;Source s;CHECK(renderer.QueueGeometry(s.Geometry()));
    auto r=Render();renderer.EndEye(reinterpret_cast<ID3D11DeviceContext*>(1),{},&r,&r,1000000000,{});CHECK(renderer.Quiescent());
    renderer.EnableObservation(true);renderer.EndEye(nullptr,Proof().eye,&r,&r,1000000000,{});CHECK(renderer.Quiescent());
    std::ostringstream report;renderer.Report(report);CHECK(report.str().find("\"attempts\":1")!=std::string::npos);CHECK(report.str().find("\"draws\":0")!=std::string::npos);
    renderer.RequestStop();CHECK(renderer.PumpStop());CHECK(!renderer.QueueGeometry(s.Geometry()));return 0;}
int CatalogAndLateBothEyeEvidence(){Bc2BodyAmmoRenderer renderer;Source s;auto catalog=std::make_shared<BodyAmmoGeometryCatalog>();catalog->push_back(s.Geometry());
    CHECK(renderer.QueueGeometryCatalog(catalog));auto duplicate=std::make_shared<BodyAmmoGeometryCatalog>(*catalog);duplicate->push_back(catalog->front());CHECK(!renderer.QueueGeometryCatalog(duplicate));
    renderer.EnableObservation(true);auto source=Render();for(unsigned n=0;n<300;++n){auto eye=Proof().eye;eye.eye=n%2;eye.view+=eye.eye;eye.frame+=n;
        renderer.EndEye(nullptr,eye,&source,&source,1000000000+std::int64_t(n)*100000000,{});}
    std::ostringstream report;renderer.Report(report);const auto text=report.str();CHECK(text.find("\"catalog_parts\":1")!=std::string::npos);
    CHECK(text.find("\"record_total\":300")!=std::string::npos&&text.find("\"retained_serial_begin\":44")!=std::string::npos);
    CHECK(text.find("\"now_ns\":30900000000")!=std::string::npos);CHECK(text.find("\"eye\":0")!=std::string::npos&&text.find("\"eye\":1")!=std::string::npos);
    CHECK(renderer.Quiescent());renderer.RequestStop();CHECK(renderer.PumpStop());return 0;}
}
int main(){if(GeometryIsOwnedAndExact()||ExactSourceBeforeAnyUpload()||SourceClaimLeaseAndReplacement()||PreDrawSelectedLeaseCannotRenew()||ProofCannotBeInferredFromTargetShape()||DisabledAndUnprovenNeverTouchGpu()||CatalogAndLateBothEyeEvidence())return 1;
    std::cout<<"Bc2BodyAmmoRenderer: 7 CPU groups passed; no GPU/native acceptance\n";}
