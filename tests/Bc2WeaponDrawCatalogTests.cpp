#include "Bc2WeaponDrawCatalog.h"
#include "Bc2ReloadDrawGeometry.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace fvr::bc2;using namespace fvr::graphics;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Fixture {
 std::vector<std::byte> vertices,indices;
 WeaponDrawSection section{"Test.res","variant","part",0,0,{6,48,1,0,RigidPropPosition::Half4,1,1,0},0};
 Fixture(unsigned count=6,unsigned stride=48){section.signature.indexCount=count;section.signature.stride=stride;
  section.signature.position=stride==68?RigidPropPosition::Float3:RigidPropPosition::Half4;
  vertices.resize(stride*3);indices.resize(count*2);
  for(unsigned i=0;i<vertices.size();++i)vertices[i]=std::byte(i%251);
  for(unsigned i=0;i<count;++i){const std::uint16_t n=std::uint16_t(i%3);std::memcpy(indices.data()+i*2,&n,2);}
  auto hash=FingerprintRigidPropDraw(section.signature,Bytes());CHECK(hash);
  section.signature.vertexSkinHash=hash->skin;section.signature.positionHash=hash->positions;
 }
 RigidPropDrawBytes Bytes(){return {vertices,indices,2,0,0};}
 WeaponDrawMatch Match(){return MatchWeaponDrawSections({&section,1},section.signature.indexCount,section.signature.stride,Bytes());}
};
int main(){unsigned tests=0;
 {Fixture f;auto m=f.Match();CHECK(m.status==WeaponDrawMatchStatus::Unique&&m.matches==1&&m.index==0);CHECK(!m.nativeAssociationVerified);++tests;}
 {Fixture f;auto b=f.Bytes();std::vector<std::byte> moved(17+f.vertices.size());std::copy(f.vertices.begin(),f.vertices.end(),moved.begin()+17);b.vertices=moved;b.vertexOffset=17;
  auto m=MatchWeaponDrawSections({&f.section,1},6,48,b);CHECK(m.status==WeaponDrawMatchStatus::Unique);++tests;}
 {Fixture f;std::vector<std::byte> indices(24);for(unsigned i=0;i<6;++i){std::uint32_t n=i%3+10;std::memcpy(indices.data()+i*4,&n,4);}auto b=f.Bytes();b.selectedIndices=indices;b.indexBytes=4;b.baseVertex=-10;
  CHECK(MatchWeaponDrawSections({&f.section,1},6,48,b).status==WeaponDrawMatchStatus::Unique);b.baseVertex=-11;CHECK(MatchWeaponDrawSections({&f.section,1},6,48,b).status==WeaponDrawMatchStatus::NoMatch);++tests;}
 {Fixture f;auto other=f.section;other.resource="Other.res";std::array catalog{f.section,other};auto m=MatchWeaponDrawSections(catalog,6,48,f.Bytes());
  CHECK(m.status==WeaponDrawMatchStatus::Ambiguous&&m.matches==2&&m.index==UINT32_MAX);++tests;}
 {Fixture f;auto other=f.section;other.variant="another-build";std::array catalog{f.section,other};CHECK(MatchWeaponDrawSections(catalog,6,48,f.Bytes()).status==WeaponDrawMatchStatus::Ambiguous);++tests;}
 {Fixture f;std::array catalog{f.section,f.section};CHECK(MatchWeaponDrawSections(catalog,6,48,f.Bytes()).status==WeaponDrawMatchStatus::MalformedCatalog);++tests;}
 {Fixture f;f.vertices[0]^=std::byte{1};CHECK(f.Match().status==WeaponDrawMatchStatus::NoMatch);++tests;}
 {Fixture f;f.indices.pop_back();CHECK(f.Match().status==WeaponDrawMatchStatus::NoMatch);CHECK(!f.Match().fingerprintValid);++tests;}
 {Fixture f;f.section.resource="bad\nresource";CHECK(!WeaponDrawCatalogValid({&f.section,1}));f.section.resource="Good.res";f.section.signature.position=static_cast<RigidPropPosition>(5);CHECK(!WeaponDrawCatalogValid({&f.section,1}));++tests;}
 {Fixture f;CHECK(MatchWeaponDrawSections({&f.section,1},9,48,f.Bytes()).status==WeaponDrawMatchStatus::NotCandidate);CHECK(!WeaponDrawCountCandidate({&f.section,1},9));CHECK(!WeaponDrawLayoutCandidate({&f.section,1},6,68));++tests;}
 {for(auto [count,stride]:{std::pair{270u,48u},{90u,48u},{12u,68u}}){Fixture f(count,stride);const auto legacy=FingerprintReloadDraw(f.vertices,f.indices,count,2,stride,0,0);CHECK(legacy);auto m=f.Match();CHECK(m.fingerprint.skin==legacy->vertexSkinHash&&m.fingerprint.positions==legacy->positionHash);}++tests;}
 {const auto catalog=LegacyReloadDrawCatalog();CHECK(catalog.size()==3&&WeaponDrawCatalogValid(catalog));for(const auto& s:catalog){CHECK(WeaponDrawCountCandidate(catalog,s.signature.indexCount));CHECK(IdentifyReloadDrawGeometry(s.signature.indexCount,s.signature.vertexSkinHash,s.signature.positionHash)==s.legacyReloadSection);}++tests;}
 std::cout<<tests<<" weapon draw catalog groups passed\n";
}
