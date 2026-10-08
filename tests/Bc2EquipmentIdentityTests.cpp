#include "Test.h"
#include "Bc2EquipmentIdentity.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/ControllerInput.h"
#include <cstring>
#include <cstdio>
#include "CapturedPickupMatrices.h"
using namespace fvr;
namespace {
struct Memory {
 std::array<std::byte,0x5000> bytes{};unsigned reads=0;bool missing=false,wrongType=false,mutate=false;
 static constexpr unsigned weapon=0x10000,data=0x11000,name=0x12000;
 void Word(unsigned at,unsigned value){std::memcpy(bytes.data()+at-weapon,&value,4);}
 void Name(const char* text){std::memset(bytes.data()+name-weapon,0,64);std::memcpy(bytes.data()+name-weapon,text,std::strlen(text)+1);}
 Memory(){Word(weapon+4,data);Word(data+0xc,name);Word(data+0x64,0x13000);Name("SPAS12_sp");}
 bc2::WeaponModeMemory Source(){return {this,[](void* c,unsigned at,void* dst,std::size_t n){auto& m=*static_cast<Memory*>(c);
   if(m.missing||at<weapon||std::uint64_t(at)+n>weapon+m.bytes.size())return false;
   if(at==name&&++m.reads==2&&m.mutate)m.Name("AEK971_sp");
   std::memcpy(dst,m.bytes.data()+at-weapon,n);return true;
 },[](void* c,unsigned at,const char* text){return !static_cast<Memory*>(c)->wrongType&&at==data&&std::string_view(text)=="SoldierWeaponData";}};}
};
math::Matrix4 At(float x,float y,float z){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1; m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],.0002f))return false;return true;}
int PointerReuse(){
 Memory m;auto spas=bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);CHECK(spas);
 interaction::TrackedRig rig;interaction::TrackedRigOwner owner{41,2,Memory::weapon,0x33000};
 interaction::InputFrame f{};f.generation=f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;
 for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;h.grip.position={.2f,-.3f,-.4f};} f.hands[0].grip.position.x=-.2f;
 const auto body=At(0,1.6f,0),left=At(-.2f,1.3f,.4f),right=At(.2f,1.3f,.4f),spasGun=At(.2f,1.3f,.7f),aekGun=At(.31f,1.45f,.64f);
 const std::array<interaction::ArmAnchor,2> anchors{interaction::ArmAnchor{{-.2f,1.5f,0},{-.1f,-.2f,.1f}},interaction::ArmAnchor{{.2f,1.5f,0},{.1f,-.2f,.1f}}};
 const auto initial=rig.Update(owner,f,body,left,right,spasGun,anchors);CHECK(initial&&Same(initial->weapon,spasGun));
 m.Name("AEK971_sp");const auto aek=bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);CHECK(aek&&aek->weapon==spas->weapon&&aek->data==spas->data&&*aek!=*spas);
 #ifndef EQUIPMENT_BASELINE
 ++owner.equipmentGeneration;
 #endif
 ++f.generation;f.predictedNs+=10000000;const auto replaced=rig.Update(owner,f,body,left,right,aekGun,anchors);
 CHECK(replaced&&!replaced->calibrated&&Same(replaced->weapon,aekGun));CHECK(Same(replaced->left,initial->left)&&Same(replaced->right,initial->right));
 f.predictedNs+=700000000;const auto settled=rig.Update(owner,f,body,left,right,aekGun,anchors);
 CHECK(settled&&!settled->weaponAttachmentPending&&Same(settled->weapon,replaced->weapon));
 // Native animation drift on the same settled item does not redefine its attachment.
 const auto animated=rig.Update(owner,f,body,left,At(.4f,1.1f,.2f),At(.9f,1.4f,.6f),anchors);
 CHECK(animated&&Same(animated->weapon,replaced->weapon)&&Same(animated->left,initial->left));
 f.focused=false;CHECK(!rig.Update(owner,f,body,left,right,aekGun,anchors));f.focused=true;++f.spaceGeneration;
 const auto reconnect=rig.Update(owner,f,body,left,right,aekGun,anchors);CHECK(reconnect&&Same(reconnect->weapon,aekGun));
 m.Name("SPAS12_sp");const auto back=bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);CHECK(back&&*back==*spas);
 #ifndef EQUIPMENT_BASELINE
 ++owner.equipmentGeneration;
 #endif
 const auto returned=rig.Update(owner,f,body,left,right,spasGun,anchors);CHECK(returned&&Same(returned->weapon,spasGun));return 0;
}
int CapturedPointerReuse(){
 interaction::TrackedRig rig;interaction::TrackedRigOwner owner{1009841728,2,127569344,0x33000};
 interaction::InputFrame f{};f.generation=f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;
 for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;h.grip.position={.2f,-.3f,-.4f};}f.hands[0].grip.position.x=-.2f;
 const auto body=At(0,1.6f,0),left=At(-.2f,1.3f,.4f);
 const std::array<interaction::ArmAnchor,2> anchors{interaction::ArmAnchor{{-.2f,1.5f,0},{-.1f,-.2f,.1f}},interaction::ArmAnchor{{.2f,1.5f,0},{.1f,-.2f,.1f}}};
 const auto first=rig.Update(owner,f,body,left,CapturedSpasRight,CapturedSpasWeapon,anchors);CHECK(first);
 #ifndef EQUIPMENT_BASELINE
 ++owner.equipmentGeneration;
 #endif
 ++f.generation;f.predictedNs+=10000000;
 const auto next=rig.Update(owner,f,body,left,CapturedAekRight,CapturedAekWeapon,anchors);CHECK(next&&!next->calibrated);
 const auto expected=interaction::Multiply(interaction::Multiply(CapturedAekWeapon,*interaction::InverseAnimatedTransform(CapturedAekRight)),next->right);
 CHECK(Same(next->weapon,expected));CHECK(Same(next->left,first->left)&&Same(next->right,first->right));
 #ifndef EQUIPMENT_BASELINE
 ++owner.equipmentGeneration;
 #endif
 const auto restored=rig.Update(owner,f,body,left,CapturedSpasRight,CapturedSpasWeapon,anchors);CHECK(restored&&Same(restored->weapon,first->weapon));return 0;
}
}
int main(){unsigned groups=0;
 {Memory m;const auto a=bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);CHECK(a&&a->Asset()=="SPAS12_sp"&&a->persistence==0x13000);++groups;}
 {Memory m;m.mutate=true;CHECK(!bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon));++groups;}
 {Memory m;m.missing=true;CHECK(!bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon));m.missing=false;m.wrongType=true;CHECK(!bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon));++groups;}
 {Memory m;const auto a=bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);m.Word(Memory::data+0x64,0x14000);const auto b=bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);CHECK(a&&b&&*a!=*b);++groups;}
 {Memory m;const auto a=bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);m.bytes[Memory::name-Memory::weapon+40]=std::byte{90};const auto b=bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);CHECK(a&&b&&*a==*b);++groups;}
 {Memory m;m.Name("");CHECK(!bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon));m.Name("SPAS12_sp");std::memset(m.bytes.data()+Memory::name-Memory::weapon,'x',64);CHECK(!bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon));++groups;}
 // A proved replacement found by animation/Pack before the next Gather must
 // reject the old immutable publication, even with unchanged palette bytes.
 {Memory m;const auto old=*bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);CHECK(bc2::WeaponEquipmentStillCurrent(m.Source(),old));m.Name("AEK971_sp");CHECK(!bc2::WeaponEquipmentStillCurrent(m.Source(),old));const auto newer=*bc2::ReadWeaponEquipmentIdentity(m.Source(),Memory::weapon);CHECK(bc2::WeaponEquipmentStillCurrent(m.Source(),newer));CHECK(!bc2::WeaponEquipmentStillCurrent(m.Source(),old));m.missing=true;CHECK(!bc2::WeaponEquipmentStillCurrent(m.Source(),newer));++groups;}
 CHECK(CapturedPointerReuse()==0);++groups;
 CHECK(PointerReuse()==0);++groups;
 std::printf("Native equipment identity: %u groups passed\n",groups);return 0;
}
