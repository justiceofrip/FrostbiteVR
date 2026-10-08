#include "Bc2MagazineNativeProfile.h"
#include "Test.h"
#include <cstdio>
using namespace fvr::bc2;
int main(){
 CHECK(sizeof(NativeMagazineProfileId)==8);
 CHECK(ResolveMagazineNativeProfile(NativeMagazineProfileId::ScopedXm8)==&Xm8MagazineNativeProfile);
 CHECK(ResolveMagazineNativeProfile(NativeMagazineProfileId::AuthoredAek)==&AekMagazineNativeProfile);
 CHECK(!ResolveMagazineNativeProfile(static_cast<NativeMagazineProfileId>(255)));
 constexpr auto digest="123456789abcdef0111111111111111111111111111111111111111111111111";
 const auto id=MagazineProfileKey(digest);CHECK(id&&std::uint64_t(*id)==0x123456789abcdef0ull);
 CHECK(!MagazineProfileKey(""));CHECK(!MagazineProfileKey("0000000000000001111111111111111111111111111111111111111111111111"));
 auto profile=AekMagazineNativeProfile;profile.configuration.assetName="fixture";profile.configuration.assetPath="fixture/A";
 std::array<MagazineNativeRegistration,2> rows{{{*id,&profile,digest,true},{}}};
 CHECK(SelectGeneratedMagazineRegistration(std::span(rows).first(1),*id));
 rows[0].enabled=false;CHECK(!SelectGeneratedMagazineRegistration(std::span(rows).first(1),*id));rows[0].enabled=true;
 profile.cycleAdmission=MagazineCycleAdmission::Candidate;CHECK(!SelectGeneratedMagazineRegistration(std::span(rows).first(1),*id));
 profile.cycleAdmission=MagazineCycleAdmission::ReviewedReload11Transfer12;
 rows[1]=rows[0];rows[1].descriptorDigest="123456789abcdef0222222222222222222222222222222222222222222222222";
 CHECK(!SelectGeneratedMagazineRegistration(rows,*id)); // Equal truncated IDs, unequal full digests.
 rows[1].id=static_cast<NativeMagazineProfileId>(5);rows[1].enabled=false;
 CHECK(!SelectGeneratedMagazineRegistration(rows,*id)); // Disabled exact path conflict still ambiguous.
 rows[1]={};rows[0].profile=&Xm8MagazineNativeProfile;CHECK(!SelectGeneratedMagazineRegistration(std::span(rows).first(1),*id));
 rows[0].profile=&profile;rows[0].id=NativeMagazineProfileId::ScopedXm8;CHECK(!SelectGeneratedMagazineRegistration(std::span(rows).first(1),rows[0].id));
 rows[0].id=*id;rows[0].descriptorDigest="ffffffffffffffff111111111111111111111111111111111111111111111111";
 CHECK(!SelectGeneratedMagazineRegistration(std::span(rows).first(1),*id));
 std::puts("4 registry baseline, disabled, exact-identity and digest-collision groups passed");return 0;
}
