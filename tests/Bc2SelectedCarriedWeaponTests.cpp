#include "Test.h"
#include "Bc2SelectedCarriedWeapon.h"
#include "Bc2SelectedCarriedFixture.h"
#include <cstdio>
using namespace fvr;
namespace {
struct Case {
 test::Fixture native;
 interaction::HandInteractionSample input{{(std::uint64_t(test::Fixture::weak)<<32)|test::Fixture::soldier,4,99,9},1,1000000000,1100000000,1000000000,true,{true,true}};
 interaction::HandInteractionKey key{test::Fixture::a,99};
 unsigned mode=0,nameReads=0,inventoryReads=0,totalReads=0;
 static constexpr unsigned name=0x31000,persistence=0x32000;
 Case(){native.Word(test::Fixture::ad+0xc,name);native.Text(name,"AEK971_sp");native.Word(test::Fixture::ad+0x64,persistence);}
 bc2::WeaponModeMemory Memory(){return {this,[](void* context,unsigned address,void* out,std::size_t size){
  auto& c=*static_cast<Case*>(context);++c.totalReads;
  if(address==test::Fixture::ad+0xc){++c.nameReads;
   if(c.mode==1&&c.nameReads==2)c.native.Text(name,"XM8_sp_s");
   if(c.mode==2&&c.nameReads==3)c.native.Text(name,"XM8_sp_s");}
  if(address==test::Fixture::inventory){++c.inventoryReads;
   if(c.mode==3&&c.inventoryReads==3)c.native.Word(test::Fixture::inventory+0x14c,1);
   if(c.mode==4&&c.inventoryReads==3)c.native.Word(test::Fixture::ad+0x64,persistence+4);
   if(c.mode==5&&c.inventoryReads==3)c.native.Word(test::Fixture::ad+0x84,2);}
  if(c.mode==6&&address==name)return false;
  const auto m=c.native.Memory();return m.read(m.context,address,out,size);
 },[](void* context,unsigned address,const char* type){auto& c=*static_cast<Case*>(context);const auto m=c.native.Memory();return m.type(m.context,address,type);}};}
 auto Read(bool verified=true){return bc2::ReadSelectedCarriedWeapon(Memory(),native.owner,input,key,verified);}
};
int ActualNonLauncherInventoryAndEquipmentJoin(){
 Case c;const auto before=c.native.bytes;const auto e=c.Read();CHECK(e);
 CHECK(e->equipment.Asset()=="AEK971_sp"&&e->equipment.data==test::Fixture::ad&&e->equipment.persistence==Case::persistence);
 CHECK(e->weapon.id==c.native.owner.weapon&&e->inventory==test::Fixture::inventory&&e->selectedSlot==0);
 CHECK(e->observedNs==c.input.observedNs&&e->deadlineNs==c.input.deadlineNs&&e->sequence==c.input.sequence);
 CHECK(c.native.bytes==before);CHECK(c.nameReads==4&&c.inventoryReads==4);
 // Existing inventory has no launcher item/type/route; no alias is invented.
 CHECK(bc2::SelectedCarriedWeaponFresh(*e,c.native.owner,c.input.owner,c.key,c.input.nowNs));return 0;
}
int ExpiryAndGenerationsCannotBeRenewed(){
 Case c;const auto e=c.Read();CHECK(e);
 CHECK(!bc2::SelectedCarriedWeaponFresh(*e,c.native.owner,c.input.owner,c.key,e->deadlineNs));
 CHECK(!bc2::SelectedCarriedWeaponFresh(*e,c.native.owner,c.input.owner,c.key,e->observedNs-1));
 auto owner=c.native.owner;++owner.equipGeneration;CHECK(!bc2::SelectedCarriedWeaponFresh(*e,owner,c.input.owner,c.key,c.input.nowNs));
 owner=c.native.owner;++owner.actorGeneration;CHECK(!bc2::SelectedCarriedWeaponFresh(*e,owner,c.input.owner,c.key,c.input.nowNs));
 auto physical=c.input.owner;++physical.space;CHECK(!bc2::SelectedCarriedWeaponFresh(*e,c.native.owner,physical,c.key,c.input.nowNs));
 auto key=c.key;++key.generation;CHECK(!bc2::SelectedCarriedWeaponFresh(*e,c.native.owner,c.input.owner,key,c.input.nowNs));
 c.input.nowNs=e->deadlineNs;CHECK(!c.Read());return 0;
}
int NativePickupAndSelectionRacesReject(){
 for(unsigned mode=1;mode<=6;++mode){Case c;c.mode=mode;CHECK(!c.Read());}
 Case category;category.native.mutateCategory=true;CHECK(!category.Read());
 Case selected;selected.native.mutate=true;CHECK(!selected.Read());return 0;
}
int NoAliasOrBindingsFromPhysicalKeyGuess(){
 Case c;c.key.id=Case::persistence;CHECK(!c.Read());CHECK(c.totalReads==0);
 c.key.id=test::Fixture::b;CHECK(!c.Read());CHECK(c.totalReads==0);
 c.key.id=test::Fixture::a;CHECK(!c.Read(false));CHECK(c.totalReads==0);
 auto memory=c.Memory();CHECK(!bc2::ReadSelectedCarriedWeapon(memory,c.native.owner,c.input,c.key));CHECK(c.totalReads==0);return 0;
}
int FocusReadAndInputFailuresHaveNoEvidence(){
 for(unsigned mode=0;mode<8;++mode){Case c;
  switch(mode){case 0:c.input.focused=false;break;case 1:c.input.tracked[0]=false;break;case 2:c.input.tracked[1]=false;break;
   case 3:c.input.released[1]=true;break;case 4:c.input.sequence=0;break;case 5:c.input.observedNs=0;break;
   case 6:c.input.deadlineNs=c.input.observedNs+200000001;break;case 7:c.input.nowNs=c.input.observedNs-1;break;}
  CHECK(!c.Read());CHECK(c.totalReads==0);
 }
 Case map;map.native.denyMap=true;CHECK(!map.Read());return 0;
}
int ReusedNativePointerRequiresNewCompleteEvidence(){
 Case c;const auto old=c.Read();CHECK(old);
 c.native.Text(Case::name,"OtherNativeAsset");c.native.Word(test::Fixture::ad+0x64,Case::persistence+4);
 ++c.native.owner.equipGeneration;++c.input.owner.equipGeneration;++c.key.generation;++c.input.sequence;
 c.input.observedNs+=1000000;c.input.nowNs=c.input.observedNs;c.input.deadlineNs+=1000000;
 const auto current=c.Read();CHECK(current&&current->equipment.Asset()=="OtherNativeAsset");
 CHECK(current->equipment!=old->equipment&&current->equipment.weapon==old->equipment.weapon);
 CHECK(!bc2::SelectedCarriedWeaponFresh(*old,c.native.owner,c.input.owner,c.key,c.input.nowNs));
 CHECK(bc2::SelectedCarriedWeaponFresh(*current,c.native.owner,c.input.owner,c.key,c.input.nowNs));return 0;
}
}
int main(){CHECK(!ActualNonLauncherInventoryAndEquipmentJoin());CHECK(!ExpiryAndGenerationsCannotBeRenewed());
 CHECK(!NativePickupAndSelectionRacesReject());CHECK(!NoAliasOrBindingsFromPhysicalKeyGuess());
 CHECK(!FocusReadAndInputFailuresHaveNoEvidence());CHECK(!ReusedNativePointerRequiresNewCompleteEvidence());
 std::puts("6 selected carried-item identity groups passed through actual native readers");return 0;}
