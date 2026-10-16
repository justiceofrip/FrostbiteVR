#include "Test.h"
#include "Bc2KnifeDrawReturnGate.h"
#include "Bc2ChestKnifeRendering.h"
#include <cstdio>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
int main(){
    {private_knife::OriginalKnifeSource source;HandInteractionSample value{{1,2,3,4},9,1000000000,1100000000,1000000000,true,{true,true}};
     CHECK(source.Advance(value));CHECK(!source.Advance(value));++value.nowNs;CHECK(!source.Advance(value));
     ++value.sequence;CHECK(source.Advance(value));value.focused=false;CHECK(source.Advance(value));
     value.focused=true;CHECK(source.Advance(value));value.tracked[1]=false;CHECK(source.Advance(value));
     value.released[1]=true;CHECK(source.Advance(value));++value.owner.space;CHECK(source.Advance(value));
     ++value.deadlineNs;CHECK(source.Advance(value));value={};CHECK(source.Advance(value));CHECK(!source.Advance(value));}
    {ChestKnifeRenderSource source;source.owner={0x10000,0x20000,0x30000,0x52000,4,7,9};
     source.input={{(std::uint64_t(0x30000)<<32)|0x20000,4,99,9},2,1000000000,1100000000,1000000000,true,{true,true}};
     source.knife={0x52000,3};source.bodyOperation=17;auto current=source.input;
     CHECK(ChestKnifeRenderCurrent(source,source.owner,current,current.nowNs));
     ++current.sequence;current.observedNs+=10000000;current.deadlineNs+=10000000;current.nowNs=current.observedNs;
     CHECK(ChestKnifeRenderCurrent(source,source.owner,current,current.nowNs));
     CHECK(!ChestKnifeRenderCurrent(source,source.owner,current,source.input.deadlineNs)); // no old lease renewal
     CHECK(!ChestKnifeRenderCurrent(source,source.owner,current,current.observedNs-1));
     for(unsigned fault=0;fault<7;++fault){auto bad=source;auto now=current;
       if(fault==0)++bad.owner.equipGeneration;if(fault==1)++now.owner.equipGeneration;if(fault==2)++now.owner.space;
       if(fault==3)now.released[1]=true;if(fault==4)bad.input.focused=false;if(fault==5)bad.input.tracked[1]=false;
       if(fault==6)bad.bodyOperation=0;
       CHECK(!ChestKnifeRenderCurrent(bad,source.owner,now,current.nowNs));}}
    {const auto& profile=ChestKnifeGripProfiles()[0];CHECK(authored_grip_detail::Profile(profile));CHECK(!profile.authoredRifleSupport);
     CHECK(profile.authoredAxisEvidence.empty()&&!private_knife::LeftHandGraspSupported);
     auto mesh=std::make_shared<SelectedMeshesSnapshot>();WeaponEquipmentIdentity equipment{15,16,17,{}};
     std::copy(profile.assetName.begin(),profile.assetName.end(),equipment.asset.begin());
     mesh->owner={9,10,11,15,12,18,14};mesh->sequence=19;mesh->observedNs=990000000;mesh->deadlineNs=1200000000;
     mesh->weaponData=16;mesh->stateTypeInfo=1;mesh->meshTypeInfo=2;mesh->inventory=3;mesh->selectedSlot=7;
     std::copy(profile.assetName.begin(),profile.assetName.end(),mesh->weaponName.begin());
     mesh->stateCount=1;mesh->soleConfiguredArray=5;mesh->states[0].array=5;mesh->states[0].state=6;mesh->states[0].count=1;
     auto& selected=mesh->states[0].meshes[0];selected.address=7;selected.typeInfo=2;selected.namePointer=8;
     std::copy(profile.meshPath.begin(),profile.meshPath.end(),selected.assetPath.begin());
     const AuthoredGripOwner owner{10,11,12,13,14,20,1090000000};
     const auto bind=[&](std::uint64_t rig){return BindAuthoredGrip(ChestKnifeGripProfiles(),mesh,equipment,owner,rig,1000000000,false);};
     const auto bound=bind(profile.rigFingerprint);CHECK(bound.binding&&!AuthoredRifleSupport(*bound.binding));
     CHECK(!bind(profile.rigFingerprint+1).binding);selected.assetPath[0]='?';CHECK(!bind(profile.rigFingerprint).binding);
     CHECK(!AuthoredGripBinding::aimCapability&&!AuthoredGripBinding::muzzleCapability&&!AuthoredGripBinding::reloadCapability);}
    std::puts("Three knife source/render groups PASS; original epoch invalidation, immutable render custody/expiry, measured grip exact binding.");
}
