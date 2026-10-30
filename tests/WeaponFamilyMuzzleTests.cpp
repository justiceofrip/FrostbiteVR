#include "Test.h"
#include "Bc2MuzzleAdmission.h"
#include "../profiles/weapons54-muzzle/CombinedMuzzle.h"
#include "../profiles/weapons43/CombinedSupport.h"
#include <set>
using namespace fvr;using namespace fvr::bc2;
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view s){out={};std::copy(s.begin(),s.end(),out.begin());}
struct Fixture {
 static constexpr std::int64_t now=1000000000;
 MuzzleEnrollmentProfile profile;
 AuthoredSupportProfile support{};
 std::shared_ptr<SelectedMeshesSnapshot> selected=std::make_shared<SelectedMeshesSnapshot>();
 WeaponEquipmentIdentity equipment{};AuthoredGripOwner owner{10,11,12,13,14,20,now+90000000};
 math::Matrix4 weapon=At(-558,27,-77),flash{};
 explicit Fixture(const MuzzleEnrollmentProfile& p):profile(p){
  for(const auto& s:generated::AuthoredSupports)if(s.bindingDigest==p.bindingDigest)support=s;
  equipment.weapon=15;equipment.data=16;Text(equipment.asset,p.asset);
  auto& s=*selected;s.owner={9,10,11,15,12,18,14};s.sequence=19;s.observedNs=now-10000000;s.deadlineNs=now+200000000;
  s.weaponData=16;s.stateTypeInfo=1;s.meshTypeInfo=2;s.inventory=3;Text(s.weaponName,p.asset);
  s.stateCount=1;s.soleConfiguredArray=5;s.states[0].array=5;s.states[0].state=6;s.states[0].count=1;
  auto& m=s.states[0].meshes[0];m.address=7;m.typeInfo=2;m.namePointer=8;Text(m.assetPath,p.meshPath);
  s.configurationPathVerified=true;s.configurationPathPointer=9;Text(s.configurationPath,p.configurationPath);
  flash=interaction::Multiply(p.flashInWeapon,weapon);
 }
 std::optional<AuthoredSupportBinding> Support(){return BindAuthoredSupportReference({&support,1},selected,equipment,owner,support.rigFingerprint,now);}
 std::optional<MuzzleBinding> Bind(){auto s=Support();return s?BindAuthoredMuzzle({&profile,1},*s,weapon,flash,1,now):std::nullopt;}
 bool Current(const MuzzleBinding& b,std::int64_t at=now){return MuzzleBindingCurrent(b,selected.get(),equipment,owner,at);}
 NativeMuzzleConfig Native(){NativeMuzzleConfig c;c.speed=profile.initialSpeed;c.direction=profile.initialDirection;c.position=profile.initialPosition;
  c.bulletsPerShell=profile.bulletsPerShell;c.bulletsPerShot=profile.bulletsPerShot;c.cameraFlags[0]=static_cast<unsigned char>(profile.activeForceSpawnToCamera);
  c.inheritedSpeed=profile.inheritedWeaponSpeed;c.projectileData=0x10000;return c;}
};
int AllExactConfigurations(){
 CHECK(generated_muzzle::Firearm54MuzzleEnrollments.size()==181);
 CHECK(generated_muzzle::MuzzleEnrollments.size()==182); // preserves original Nambu authored admission
 std::set<std::string_view> names,configurations;std::set<unsigned> shellCounts,shotCounts;
 for(const auto& p:generated_muzzle::Firearm54MuzzleEnrollments){
  Fixture f(p);auto b=f.Bind();CHECK(b&&b->support&&!b->authored.profile&&f.Current(*b));
  CHECK(MuzzleBindingSelected(*b)==f.selected.get());CHECK(MatchesMuzzleEnrollment(f.Native(),*b));
  names.insert(p.asset);CHECK(configurations.insert(p.configurationPath).second);
  shellCounts.insert(p.bulletsPerShell);shotCounts.insert(p.bulletsPerShot);
  // Same gun name and mesh cannot enable an unlisted configuration variant.
  Text(f.selected->configurationPath,"Objects/Weapons/UnlistedConfiguration");CHECK(!f.Bind());
 }
 CHECK(names.size()==77&&shellCounts.size()==3&&shotCounts.size()==3);
 return 0;
}
int ExactSourceAndGeometry(){
 Fixture f(generated_muzzle::Firearm54MuzzleEnrollments[0]);const auto b=f.Bind();CHECK(b);
 const auto original=f.profile;f.profile.clipSha256=std::string_view("0f");CHECK(!f.Bind());f.profile=original;
 f.profile.skeletonSha256=f.profile.bindingDigest;CHECK(!f.Bind());f.profile=original;
 f.profile.bindingDigest=f.profile.axisEvidence;CHECK(!f.Bind());f.profile=original;
 f.profile.rigFingerprint++;CHECK(!f.Bind());f.profile=original;
 f.profile.admission=MuzzleAdmission::Disabled;CHECK(!f.Bind());f.profile=original;
 f.flash.values[3][0]+=.02f;CHECK(!f.Bind());f.flash=interaction::Multiply(f.profile.flashInWeapon,f.weapon);
 // Flash orientation is a checked reference, never copied over native direction.
 f.flash.values[0][0]*=-1;CHECK(!f.Bind());f.flash=interaction::Multiply(f.profile.flashInWeapon,f.weapon);
 auto support=f.Support();CHECK(support);auto duplicate=std::array{f.profile,f.profile};
 CHECK(!BindAuthoredMuzzle(duplicate,*support,f.weapon,f.flash,1,Fixture::now));
 f.selected->configurationPathVerified=false;CHECK(!f.Bind());f.selected->configurationPathVerified=true;
 auto c=f.Native();c.cameraFlags[1]=1;CHECK(!MatchesMuzzleEnrollment(c,*b));c=f.Native();
 c.cameraFlags[2]=1;CHECK(!MatchesMuzzleEnrollment(c,*b));c=f.Native();c.muzzleExplosion=0x10000;CHECK(!MatchesMuzzleEnrollment(c,*b));
 c=f.Native();c.direction[2]=-1;CHECK(!MatchesMuzzleEnrollment(c,*b));c=f.Native();c.bulletsPerShell++;CHECK(!MatchesMuzzleEnrollment(c,*b));
 c=f.Native();c.bulletsPerShot++;CHECK(!MatchesMuzzleEnrollment(c,*b));c=f.Native();c.speed[2]++;CHECK(!MatchesMuzzleEnrollment(c,*b));
 c=f.Native();c.position[0]=.01f;CHECK(!MatchesMuzzleEnrollment(c,*b));c=f.Native();c.inheritedSpeed=.1f;CHECK(!MatchesMuzzleEnrollment(c,*b));
 c=f.Native();c.cameraFlags[0]^=1;CHECK(!MatchesMuzzleEnrollment(c,*b));
 return 0;
}
int OriginalLeasesAndSourceRetirement(){
 Fixture f(generated_muzzle::Firearm54MuzzleEnrollments[0]);const auto old=f.Bind();CHECK(old);
 f.selected=std::make_shared<SelectedMeshesSnapshot>(*f.selected);f.selected->sequence++;f.selected->observedNs=Fixture::now;
 f.selected->deadlineNs=Fixture::now+240000000;f.owner.inputSequence++;f.owner.inputDeadlineNs=Fixture::now+150000000;
 const auto live=f.Bind();CHECK(live&&SameMuzzleSource(*old,*live));
 CHECK(MuzzleBindingDeadlineNs(*old)==Fixture::now+90000000);
 CHECK(MuzzleBindingContinues(*old,*live,Fixture::now+89999999));
 CHECK(!MuzzleBindingContinues(*old,*live,Fixture::now+90000000));CHECK(f.Current(*live,Fixture::now+90000000));
 auto changed=*live;changed.support->owner.space++;CHECK(!SameMuzzleSource(*old,changed));
 changed=*live;changed.support->owner.equipmentGeneration++;CHECK(!SameMuzzleSource(*old,changed));
 changed=*live;changed.support->equipment.persistence++;CHECK(!SameMuzzleSource(*old,changed));
 f.selected->configurationPathPointer++;CHECK(!f.Current(*old));f.selected->configurationPathPointer--;
 f.owner.space++;CHECK(!f.Current(*old));f.owner.space--;f.equipment.data++;CHECK(!f.Current(*old));
 return 0;
}
}
int main(){return AllExactConfigurations()||ExactSourceAndGeometry()||OriginalLeasesAndSourceRetirement();}
