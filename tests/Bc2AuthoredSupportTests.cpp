#include "Test.h"
#include "Bc2AuthoredSupport.h"
#include <cstring>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view text){out={};std::copy(text.begin(),text.end(),out.begin());}
int main(){
 math::Matrix4 left{};for(unsigned n=0;n<4;++n)left.values[n][n]=1;left.values[3]={0,-.1f,-.6f,1};
 constexpr std::string_view digest="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
 const std::array<AuthoredSupportProfile,1> profiles{{{"SyntheticSupport","Objects/Weapons/SyntheticMesh",digest,digest,digest,91,left}}};
 const auto& p=profiles[0];constexpr auto now=1000000000ll;
 WeaponEquipmentIdentity equipment;equipment.weapon=15;equipment.data=16;equipment.persistence=17;Text(equipment.asset,p.assetName);
 AuthoredGripOwner owner{10,11,12,13,14,20,now+90000000};SelectedMeshesSnapshot s;
 s.owner={9,10,11,15,12,18,14};s.sequence=19;s.observedNs=now-10000000;s.deadlineNs=now+200000000;
 s.weaponData=16;s.stateTypeInfo=1;s.meshTypeInfo=2;s.inventory=3;s.selectedSlot=4;Text(s.weaponName,p.assetName);
 s.stateCount=1;s.soleConfiguredArray=5;s.states[0].array=5;s.states[0].state=6;s.states[0].count=1;
 auto& m=s.states[0].meshes[0];m.address=7;m.typeInfo=2;m.namePointer=8;Text(m.assetPath,p.meshPath);
 const auto bind=[&]{return BindAuthoredSupport(profiles,s,equipment,owner,p.rigFingerprint,now);};
 CHECK(bind()==&p);const auto originalEquipment=equipment;
 // Current binding does not conflate physical equip13 with native equip18.
 // Replacing the actual selected native identity invalidates the anchor.
 CHECK(bind());CHECK(equipment==originalEquipment);
 auto changed=s;changed.owner.weapon++;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 changed=s;changed.weaponData++;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 changed=s;changed.deadlineNs=now;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 changed=s;changed.sequence=owner.inputSequence+1;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 changed=s;changed.states[0].count=2;changed.states[0].meshes[1]=m;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 auto duplicate=std::array{p,p};CHECK(!BindAuthoredSupport(duplicate,s,equipment,owner,p.rigFingerprint,now));
 // First same-asset row can target another configured mesh. Order is immaterial.
 auto variants=std::array{p,p};variants[0].meshPath="Objects/Weapons/OtherMesh";
 CHECK(BindAuthoredSupport(variants,s,equipment,owner,p.rigFingerprint,now)==&variants[1]);
 std::swap(variants[0],variants[1]);CHECK(BindAuthoredSupport(variants,s,equipment,owner,p.rigFingerprint,now)==&variants[0]);
 changed=s;changed.deadlineNs=owner.inputDeadlineNs-1;
 CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 CHECK(!BindAuthoredSupport(profiles,s,equipment,owner,p.rigFingerprint+1,now));
 owner.space++;CHECK(!bind());owner.space--;owner.inputDeadlineNs=now;CHECK(!bind());
 std::cout<<"Measured support-only binding ownership, ambiguity and expiry passed\n";
 return 0;
}

