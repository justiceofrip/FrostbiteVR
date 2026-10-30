#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2BodyEquipment.h"
#include "Bc2MagazineEquipmentProfile.h"
#include "Test.h"
#include <cstring>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::graphics;
namespace {
void Number(std::vector<std::byte>& bytes,std::uint64_t n,unsigned count){for(unsigned k=0;k<count;++k){bytes.push_back(std::byte(n&255));n>>=8;}}
void Text(std::vector<std::byte>& bytes,std::string_view text){Number(bytes,text.size(),2);for(char c:text)bytes.push_back(std::byte(c));}
struct Fixture {
    std::vector<std::byte> vertices=std::vector<std::byte>(72),indices=std::vector<std::byte>(6);
    BeltPropSectionProfile section{"Rifle","Mesh","Magazine","plastic",{3,24,1,0,RigidPropPosition::Float3,0,0,1}};
    BodyAmmoAssetProfile profile{"Rifle","Mesh","Magazine",19,interaction::reload_insertion_detail::Identity(),std::span(&section,1)};
    Fixture(){for(unsigned n=0;n<3;++n){const float p[]{float(n==1)*.025f,float(n==2)*.15f,.03f};std::memcpy(vertices.data()+n*24,p,12);vertices[n*24+16]=std::byte{255};indices[n*2]=std::byte(n);}
        const auto hash=FingerprintRigidPropDraw(section.geometry,{vertices,indices,2,0,0});section.geometry.vertexSkinHash=hash->skin;section.geometry.positionHash=hash->positions;}
    std::vector<std::byte> Pack()const{std::vector<std::byte> out;for(char c:std::string_view("BC2PROP1"))out.push_back(std::byte(c));Number(out,1,4);
        Text(out,"Rifle");Text(out,"Mesh");Text(out,"Magazine");Number(out,19,8);Number(out,1,4);Text(out,"plastic");
        Number(out,vertices.size(),4);Number(out,indices.size(),4);Number(out,2,4);Number(out,0,4);Number(out,0,4);
        out.insert(out.end(),vertices.begin(),vertices.end());out.insert(out.end(),indices.begin(),indices.end());return out;}
    auto Parse(std::span<const std::byte> bytes)const{return ParseBodyAmmoGeometryCacheForProfiles(bytes,std::span(&profile,1));}
};
int ExactOwnedGeometry(){Fixture f;auto bytes=f.Pack();const auto result=f.Parse(bytes);CHECK(result.status==BodyAmmoCacheStatus::Loaded&&result.catalog&&result.catalog->size()==1);
    const auto& g=*result.catalog->front();CHECK(g.asset=="Rifle"&&g.sections.size()==1&&g.sections.front().mesh.vertices.size()==3);
    const auto z=g.sections.front().mesh.vertices.front().z;CHECK(Near(z,-.03f));bytes.assign(bytes.size(),std::byte{});CHECK(g.sections.front().mesh.vertices.front().z==z);return 0;}
int RejectMalformedBeforeAnyUpload(){Fixture f;const auto valid=f.Pack();
    for(std::size_t count=0;count<valid.size();++count){const auto result=f.Parse(std::span(valid).first(count));CHECK(!result.catalog&&result.status!=BodyAmmoCacheStatus::Loaded);}
    auto bytes=valid;bytes[0]=std::byte{0};CHECK(f.Parse(bytes).status==BodyAmmoCacheStatus::Header);bytes=valid;
    for(unsigned n=0;n<4;++n)bytes[8+n]=std::byte(((MaxBodyPropAssets+1)>>(n*8))&255);
    CHECK(f.Parse(bytes).status==BodyAmmoCacheStatus::Count);
    bytes=valid;bytes.push_back(std::byte{});CHECK(f.Parse(bytes).status==BodyAmmoCacheStatus::Trailing);
    bytes=valid;bytes[14]=std::byte{'X'};CHECK(f.Parse(bytes).status==BodyAmmoCacheStatus::Identity);
    bytes=valid;bytes[bytes.size()-15]=std::byte{1};CHECK(f.Parse(bytes).status==BodyAmmoCacheStatus::Geometry);return 0;}
int AtomicCatalogRejectsDuplicateOrInverse(){Fixture f;const auto first=f.Pack();auto twice=first;twice[8]=std::byte{2};twice.insert(twice.end(),first.begin()+12,first.end());
    CHECK(f.Parse(twice).status==BodyAmmoCacheStatus::Identity);f.profile.inverseBind.values[0][0]=0;CHECK(f.Parse(first).status==BodyAmmoCacheStatus::Geometry);
    const auto missing=LoadBodyAmmoGeometryCache("Z:/no-such-body-ammo-cache.fvrprop");CHECK(missing.status==BodyAmmoCacheStatus::Missing&&!missing.catalog);return 0;}
int ActualChestClearance(const BodyAmmoGeometryCatalog& catalog){
    interaction::InputFrame input;input.generation=input.spaceGeneration=input.predictedNs=1;
    input.focused=input.headValid=true;input.head.position=input.referenceHead.position={0,1.7f,0};
    const auto identity=interaction::reload_insertion_detail::Identity();unsigned measured=0;
    for(const auto& part:catalog){const auto* equipment=FindMagazineEquipment(part->asset);
        if(!equipment||!equipment->Ready())continue;const auto& g=*equipment->geometry;
        if(part->mesh!=g.mesh||part->part!=g.bones.magazine||part->rigFingerprint!=g.rigFingerprint)continue;
        const auto grasp=interaction::InverseRigid(g.interaction.insertion.itemFromHand);CHECK(grasp);
        const auto world=BodyEquipmentPose(input,identity,interaction::BodyAnchorConfig{}.chest,*grasp);CHECK(world);
        float top=-10.f,bottom=10.f;
        for(const auto& section:part->sections)for(const auto& vertex:section.mesh.vertices){
            const float y=vertex.x*world->values[0][1]+vertex.y*world->values[1][1]+vertex.z*world->values[2][1]+world->values[3][1];
            top=(std::max)(top,y);bottom=(std::min)(bottom,y);
        }
        std::cout<<"Chest magazine "<<part->asset<<" below-head y=["<<bottom<<","<<top<<"] metres\n";
        // Test real extracted vertices, not only the nominal contact origin:
        // the grasp/mesh transform must not put a magazine up beside the eyes.
        CHECK(top<=-.20f&&bottom>=-.85f);++measured;
    }
    CHECK(measured);std::cout<<"Chest-clearance cached magazine profiles: "<<measured<<"\n";return 0;
}
int ActualInstalledCache(const char* path,unsigned expected=5){const auto loaded=LoadBodyAmmoGeometryCache(path);CHECK(loaded.status==BodyAmmoCacheStatus::Loaded&&loaded.catalog&&loaded.catalog->size()==expected);
    unsigned triangles=0;for(const auto& part:*loaded.catalog)for(const auto& section:part->sections)triangles+=unsigned(section.mesh.vertices.size()/3);
    if(expected==5)CHECK(triangles==15616);
    CHECK(!ActualChestClearance(*loaded.catalog));
    Bc2BodyAmmoRenderer renderer;CHECK(renderer.QueueGeometryCatalog(loaded.catalog));renderer.RequestStop();CHECK(renderer.PumpStop());
    std::cout<<"Installed cache:"<<expected<<" ammo/equipment parts/"<<triangles<<" triangles reconstructed and queued; CPU only\n";return 0;}
}
int main(int argc,char** argv){unsigned expected=5;if(argc==3){const auto value=std::string_view(argv[2]);const auto result=std::from_chars(value.data(),value.data()+value.size(),expected);
        CHECK(result.ec==std::errc{}&&result.ptr==value.data()+value.size()&&expected&&expected<=MaxBodyPropAssets);}
    CHECK(argc>=1&&argc<=3);
    if(ExactOwnedGeometry()||RejectMalformedBeforeAnyUpload()||AtomicCatalogRejectsDuplicateOrInverse()||(argc>=2&&ActualInstalledCache(argv[1],expected)))return 1;
    std::cout<<"Bc2BodyAmmoCache:3 deterministic CPU groups passed; no GPU/native admission\n";}

