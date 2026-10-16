#include "Test.h"
#include "Bc2CampaignVehicleProfile.h"
#include "Bc2VehicleCommit.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using namespace fvr;using namespace fvr::bc2;
namespace {
// Synthetic native layout and reflected metadata. These bytes are not a game
// acceptance receipt. Every positive case needs current consumer ownership.
struct Memory {
    std::vector<std::byte> bytes=std::vector<std::byte>(0x1600000);unsigned cursor=0x20000;
    std::map<std::string,unsigned> kinds;std::map<unsigned,std::string> types;
    VehicleRouteSnapshot seat{};CampaignVehicleBinding binding{0x400000,true};
    unsigned collection=0,entryData=0,map=0,mapRows=0,driver=0,prediction=0,bridge=0,camera=0,weapon=0,native=0,primary=0;
    std::array<unsigned,8> nodes{};std::array<unsigned,2> config{};unsigned mutationAddress=0,reads=0;
    unsigned Alloc(unsigned size){auto at=cursor;cursor+=(size+15)&~15u;return at;}
    void Put(unsigned at,unsigned value){std::memcpy(bytes.data()+at,&value,4);}
    unsigned Word(unsigned at)const{unsigned value=0;std::memcpy(&value,bytes.data()+at,4);return value;}
    unsigned Text(const char* text){auto at=Alloc(unsigned(std::strlen(text))+1);std::memcpy(bytes.data()+at,text,std::strlen(text)+1);return at;}
    struct Field{const char* name;unsigned offset;const char* type;};
    unsigned Kind(const char* name,unsigned parent=0,std::initializer_list<Field> fields={}){
        if(auto it=kinds.find(name);it!=kinds.end())return it->second;
        auto info=Alloc(64),meta=Alloc(32),rows=Alloc(unsigned(fields.size())*24+4);kinds[name]=info;
        Put(info+4,meta);Put(info+20,parent);Put(info+36,rows);Put(meta,Text(name));Put(meta+4,0x35|(1024<<16));bytes[meta+13]=std::byte(fields.size());
        unsigned n=0;for(const auto& f:fields){Put(rows+n*24,Text(f.name));Put(rows+n*24+8,Kind(f.type));Put(rows+n*24+16,f.offset);++n;}return info;
    }
    unsigned Object(const char* name,unsigned info){const auto at=Alloc(1024),table=Alloc(16),getter=Alloc(16);Put(at,table);Put(table+8,getter);bytes[getter]=std::byte{0xb8};Put(getter+1,info);bytes[getter+5]=std::byte{0xc3};types[at]=name;return at;}
    void Row(unsigned n,unsigned owner,unsigned parent){Put(collection+16+n*16,nodes[n]);Put(collection+20+n*16,owner);Put(collection+28+n*16,parent<<8);}
    void Map(){
        const auto rowType=kinds.at("EntryInputActionMappingData");mapRows=Alloc(seat.count*4);Put(map+24,seat.count);Put(map+28,mapRows);
        for(unsigned n=0;n<seat.count;++n){auto row=Object("EntryInputActionMappingData",rowType);Put(mapRows+n*4,row);Put(row+12,seat.routes[n].conceptId);Put(row+16,seat.routes[n].action);}
    }
    explicit Memory(bool ownsDriver=true,bool ownsGun=true){
        auto entryBase=Kind("EntryComponentData",0,{{"EntryClass",184,"EntryClass"},{"InputMapping",192,"InputActionMappingsData"}});
        auto entryKind=Kind("PlayerEntryComponentData",entryBase);
        kinds.erase("InputActionMappingsData");auto mappingKind=Kind("InputActionMappingsData",0,{{"Mappings",12,"ArrayBase"}});
        Kind("EntryInputActionMappingData",0,{{"ConceptIdentifier",12,"InputConceptIdentifiers"},{"ActionIdentifier",16,"EntryInputActionEnum"}});
        auto childKind=Kind("ChildComponentData",0,{{"MovingBody",244,"MovingBodyData"}});
        auto rotateBase=Kind("RotationBodyData",0,{{"RotationAxis",28,"Int32"}});
        auto rotateKind=Kind("ChildRotationBodyData",rotateBase,{{"RotationInput",56,"EntryInputActionEnum"}});
        auto weaponKind=Kind("WeaponComponentData",0,{{"WeaponFiring",216,"WeaponFiringData"}});
        kinds.erase("WeaponFiringData");auto firingKind=Kind("WeaponFiringData",0,{{"PrimaryFire",64,"FiringFunctionData"}});
        kinds.erase("FiringFunctionData");auto primaryKind=Kind("FiringFunctionData",0,{{"FireLogic",12,"FireLogicData"}});
        seat.identity={1,2,3,Alloc(1024),0,Alloc(1024),Alloc(1024)};seat.player=Alloc(4096);seat.weak=1;seat.fingerprint=44;
        for(auto& at:nodes)at=Alloc(1024);driver=nodes[0];seat.identity.entry=nodes[1];camera=Alloc(1024);weapon=nodes[5];native=Alloc(1024);
        types[seat.identity.controlled]="ClientVehicleEntity";types[nodes[1]]="ClientPlayerEntryComponent";types[driver]="ClientVehicleComponent";
        types[nodes[2]]=types[nodes[3]]="ClientChildComponent";types[nodes[4]]="ClientCameraComponent";types[weapon]="ClientWeaponComponent";
        Put(nodes[1]+0x1c4,seat.player);Put(seat.player+0xc74,seat.identity.cache);Put(nodes[1]+0x198,seat.identity.router);
        entryData=Object("PlayerEntryComponentData",entryKind);Put(nodes[1]+12,entryData);Put(entryData+184,ownsDriver?0:1);
        map=Object("InputActionMappingsData",mappingKind);Put(entryData+192,map);
        for(auto pair:{VehicleRoute{0,0},{4,1},{5,7},{6,8},{8,9},{16,36}})seat.routes[seat.count++]=pair;Map();
        collection=Alloc(16+8*16);Put(collection,seat.identity.controlled);bytes[collection+13]=bytes[collection+14]=std::byte{8};
        for(unsigned n=0;n<8;++n){Put(nodes[n]+0x24,collection);bytes[nodes[n]+0x28]=std::byte(n);Row(n,0,0);}
        Row(0,ownsDriver?nodes[1]:nodes[7],255);Row(1,nodes[1],0);Row(2,ownsGun?nodes[1]:nodes[7],0);Row(3,0,2);Row(4,nodes[1],ownsGun?3:1);Row(5,0,3);Row(7,nodes[7],0);
        Put(driver,binding.image+0x1030b98);prediction=Alloc(1024);bridge=Alloc(1024);auto pt=Alloc(16),bt=Alloc(16);
        Put(driver+0x1c4,prediction);Put(driver+0x1cc,prediction);Put(driver+0x1d0,bridge);Put(driver+0xd4,Alloc(16));Put(driver+0xd8,Alloc(16));
        Put(prediction,pt);Put(pt+4,binding.image+0x473b60);Put(prediction+0x16c,bridge);Put(bridge,bt);Put(bt,binding.image+0x3c77c0);Put(bridge+4,driver);
        auto cameraRows=Alloc(12),selector=Alloc(4),wrapper=Alloc(16),wt=Alloc(16);Put(nodes[1]+0x19c,cameraRows);Put(nodes[1]+0x1a0,cameraRows+12);Put(nodes[1]+0xd8,selector);Put(cameraRows,camera);Put(cameraRows+4,wrapper);Put(wrapper,wt);Put(wrapper+4,nodes[4]);Put(wt+4,binding.image+0x3df4e0);Put(wt+8,binding.image+0x3c4cb0);
        auto entryTable=Alloc(0x180),cameraState=Alloc(0x40),cameraNode=Alloc(0x20);
        Put(nodes[1],entryTable);Put(entryTable+0x154,binding.image+0x3d3e60);Put(entryTable+0x170,binding.image+0x3ce480);
        Put(nodes[1]+0x1ac,cameraState);Put(cameraState+0x20,cameraNode);Put(cameraNode+0x10,0);Put(camera+0x80,wrapper);
        for(unsigned n=0;n<2;++n){auto at=nodes[n+2],data=Object("ChildComponentData",childKind);config[n]=Object("ChildRotationBodyData",rotateKind);
            Put(at,binding.image+0x1031028);Put(at+12,data);Put(data+244,config[n]);Put(config[n]+56,n?5:6);Put(config[n]+28,n?0:1);Put(at+0xd0,config[n]);
            auto pred=Alloc(32),table=Alloc(16);Put(at+0x100,pred);Put(pred,table);Put(pred+0x10,at);Put(table+4,binding.image+0x3d2ee0);
        }
        Put(weapon,binding.image+0x1030d38);auto data=Object("WeaponComponentData",weaponKind),firing=Object("WeaponFiringData",firingKind);primary=Object("FiringFunctionData",primaryKind);
        Put(weapon+12,data);Put(data+216,firing);Put(firing+64,primary);Put(primary+0x4c,8);Put(weapon+0xd0,native);Put(native+12,firing);auto nt=Alloc(16);Put(native,nt);Put(nt+8,binding.image+0x3d4c10);
    }
    VehicleRouteMemory Access(){return {this,[](void* p,unsigned at,void* out,std::size_t n){auto& m=*static_cast<Memory*>(p);if(at<0x10000||std::uint64_t(at)+n>m.bytes.size())return false;if(m.mutationAddress==at&&++m.reads==2)m.Put(at,m.Word(at)+16);std::memcpy(out,m.bytes.data()+at,n);return true;},[](void* p,unsigned at,const char* type){auto& m=*static_cast<Memory*>(p);auto i=m.types.find(at);return i!=m.types.end()&&i->second==type;}};}
};
int SharedConsumerRoles(){
    for(unsigned n=0;n<3;++n){Memory m(n!=1,n!=0);const auto s=ReadCampaignVehicleProfile(m.Access(),m.binding,m.seat);CHECK(s);
        CHECK(s->profile.role==(n==0?VehicleSeatRole::Driver:n==1?VehicleSeatRole::Gunner:VehicleSeatRole::DriverGunner));
        CHECK(s->profile.exitVerified&&s->weapons==(n?1u:0u));CHECK(RevalidateCampaignVehicleProfile(m.Access(),*s,m.seat));}
    Memory passenger(false,false);passenger.Put(passenger.entryData+184,0);CHECK(!ReadCampaignVehicleProfile(passenger.Access(),passenger.binding,passenger.seat));
    Memory separate; // Authored camera pitch proxy shares the gun's config.
    std::memcpy(separate.bytes.data()+separate.nodes[6],separate.bytes.data()+separate.nodes[3],1024);
    separate.bytes[separate.nodes[6]+0x28]=std::byte{6};separate.types[separate.nodes[6]]="ClientChildComponent";
    const auto oldPrediction=separate.Word(separate.nodes[3]+0x100),proxyPrediction=separate.Alloc(32);
    std::memcpy(separate.bytes.data()+proxyPrediction,separate.bytes.data()+oldPrediction,32);
    separate.Put(proxyPrediction+0x10,separate.nodes[6]);separate.Put(separate.nodes[6]+0x100,proxyPrediction);
    separate.Row(6,0,2);separate.Row(4,separate.nodes[1],6);
    auto proxy=ReadCampaignVehicleProfile(separate.Access(),separate.binding,separate.seat);CHECK(proxy&&proxy->profile.role==VehicleSeatRole::DriverGunner);
    separate.Row(6,separate.nodes[7],2);proxy=ReadCampaignVehicleProfile(separate.Access(),separate.binding,separate.seat);
    CHECK(proxy&&proxy->profile.role==VehicleSeatRole::Driver&&!proxy->profile.axes[4]);return 0;
}
int CurrentConsumerRejections(){
    for(unsigned variant=0;variant<13;++variant){Memory m(false,true);
        if(variant==0)m.Row(5,m.nodes[7],3);
        if(variant==1)m.Put(m.native+12,0);
        if(variant==2)m.Put(m.config[0]+28,0);
        if(variant==3)m.Put(m.config[1]+56,4);
        if(variant==4)m.Put(m.nodes[1]+0x1c4,m.seat.player+16);
        if(variant==5)m.Put(m.Word(m.mapRows)+12,76);
        if(variant==6)m.Row(2,m.nodes[1],3);
        if(variant==7)m.binding.verified=false;
        if(variant==8)m.Put(m.primary+0x4c,9);
        if(variant==9)m.Row(4,m.nodes[7],3);
        if(variant==10)m.Put(m.nodes[3]+0x100,0);
        if(variant==11)m.mutationAddress=m.nodes[1]+0x1c4;
        if(variant==12)m.Put(m.entryData+184,3);
        CHECK(!ReadCampaignVehicleProfile(m.Access(),m.binding,m.seat));
    }
    for(unsigned variant=0;variant<5;++variant){Memory m(true,false);
        if(variant==0)m.Put(m.driver+0x1c4,m.prediction+16);
        if(variant==1)m.Put(m.bridge+4,m.nodes[7]);
        if(variant==2){m.seat.routes[1].conceptId=6;m.Map();} // Aircraft yaw is not ground steering.
        if(variant==3)m.Put(m.entryData+184,1);
        if(variant==4)m.Put(m.driver+0xd4,0);
        CHECK(!ReadCampaignVehicleProfile(m.Access(),m.binding,m.seat));
    }
    return 0;
}
int CachedJoinsCannotGrantNewAuthority(){
    for(unsigned variant=0;variant<6;++variant){Memory m;auto s=ReadCampaignVehicleProfile(m.Access(),m.binding,m.seat);CHECK(s);
        if(variant==0)m.Put(m.config[0]+56,4);if(variant==1)m.Put(m.bridge+4,m.nodes[7]);if(variant==2)m.Row(5,m.nodes[7],3);
        if(variant==3)++m.seat.identity.seatGeneration;if(variant==4)++m.seat.fingerprint;if(variant==5)m.Put(m.Word(m.mapRows)+12,1);
        CHECK(!RevalidateCampaignVehicleProfile(m.Access(),*s,m.seat));}
    return 0;
}
int FixedGunnerUsesExistingPlanAndPermissions(){
    Memory m(false,true);auto s=ReadCampaignVehicleProfile(m.Access(),m.binding,m.seat);CHECK(s);
    CHECK(CampaignVehiclePlaying(s->profile,0x60));CHECK(!CampaignVehiclePlaying(s->profile,0x11));CHECK(!CampaignVehiclePlaying(s->profile,0));
    interaction::VehicleControlOutput controls{{.8f,-.7f,.4f,-.3f,.9f,true},true,true,true};
    auto command=CampaignVehicleCommand(m.seat.identity,controls);CHECK(command.axes[2]==.4f&&command.axes[3]==-.3f&&command.axes[4]==.9f&&command.exitHeld);
    std::array<std::byte,InputBytes> cache{};unsigned allowed=0x60;std::memcpy(cache.data()+0x98,&allowed,4);
    const auto plan=BuildVehicleInputPlan(s->profile,{m.seat.identity,m.seat.fingerprint,true,true},command,cache);
    CHECK(plan.status==VehicleInputPlanStatus::Ready&&plan.count==3&&plan.deniedAxes==(1u<<4));
    CHECK(plan.edits[0].offset==32&&plan.edits[1].offset==28&&plan.edits[2].offset==0x98);
    CHECK((plan.edits[2].after&0x7ff)==allowed); // A blocked fire action remains blocked.
    const auto before=cache;VehicleInputOverride patch;CHECK(patch.Apply(cache,plan));CHECK(patch.Restore()&&cache==before);
    controls.active=false;command=CampaignVehicleCommand(m.seat.identity,controls);CHECK(BuildVehicleInputPlan(s->profile,{m.seat.identity,m.seat.fingerprint,true,true},command,cache).status==VehicleInputPlanStatus::Inactive);
    return 0;
}
int ActualNativeCameraSelection(){
    Memory m;const auto entry=m.nodes[1],oldRows=m.Word(entry+0x19c),rows=m.Alloc(24);
    // Old +d8 selects zero. The actual native state selects one. Row zero is
    // deliberately invalid so using the old selector would fail this case.
    std::memcpy(m.bytes.data()+rows+12,m.bytes.data()+oldRows,12);
    m.Put(entry+0x19c,rows);m.Put(entry+0x1a0,rows+24);
    const auto state=m.Word(entry+0x1ac),node=m.Word(state+0x20);m.Put(node+0x10,1);
    auto s=ReadCampaignVehicleProfile(m.Access(),m.binding,m.seat);CHECK(s&&s->camera==m.camera);
    m.Put(node+0x10,0);CHECK(!RevalidateCampaignVehicleProfile(m.Access(),*s,m.seat));
    for(unsigned variant=0;variant<7;++variant){Memory bad;const auto e=bad.nodes[1],cs=bad.Word(e+0x1ac),cn=bad.Word(cs+0x20);
        if(variant==0)bad.Put(bad.camera+0x80,0);
        if(variant==1)bad.Put(cn+0x10,1);
        if(variant==2)bad.Put(cs+0x20,0);
        if(variant==3)bad.Put(bad.Word(e)+0x154,0);
        if(variant==4)bad.Put(e+0x1ac,0xfffffff0);
        if(variant==5)bad.mutationAddress=bad.camera+0x80;
        if(variant==6)bad.mutationAddress=cs+0x20;
        CHECK(!ReadCampaignVehicleProfile(bad.Access(),bad.binding,bad.seat));
    }
    Memory sentinel;const auto cs=sentinel.Word(sentinel.nodes[1]+0x1ac);sentinel.Put(cs+0x20,cs+8);
    CHECK(ReadCampaignVehicleProfile(sentinel.Access(),sentinel.binding,sentinel.seat));
    for(unsigned variant=0;variant<3;++variant){Memory stable;auto before=ReadCampaignVehicleProfile(stable.Access(),stable.binding,stable.seat);CHECK(before);
        if(variant==0)stable.Put(stable.camera+0x80,stable.Word(stable.camera+0x80)+16);
        if(variant==1)stable.Put(stable.nodes[1]+0x1ac,stable.Word(stable.nodes[1]+0x1ac)+16);
        if(variant==2){auto t=stable.Word(stable.nodes[1]);stable.Put(t+0x170,stable.Word(t+0x170)+1);}
        CHECK(!RevalidateCampaignVehicleProfile(stable.Access(),*before,stable.seat));
    }
    return 0;
}
int ExactCodeBinding(){
    Memory m;
    struct Code{unsigned rva;std::initializer_list<unsigned char> bytes;const char* name;};
    const Code proofs[]{
#include "Bc2BoatAimProof.inc"
#include "Bc2CampaignVehicleProof.inc"
    };
    for(const auto& p:proofs)std::memcpy(m.bytes.data()+m.binding.image+p.rva,p.bytes.begin(),p.bytes.size());
    CHECK(VerifyCampaignVehicleCode(m.Access(),m.binding.image).verified);
    m.bytes[m.binding.image+0x3c77c0]^=std::byte{1};CHECK(!VerifyCampaignVehicleCode(m.Access(),m.binding.image).verified);return 0;
}
}
int main(){if(SharedConsumerRoles()||CurrentConsumerRejections()||CachedJoinsCannotGrantNewAuthority()||FixedGunnerUsesExistingPlanAndPermissions()||ActualNativeCameraSelection()||ExactCodeBinding())return 1;std::cout<<"6 campaign vehicle groups passed (synthetic layouts, active native camera ownership included)\n";}
