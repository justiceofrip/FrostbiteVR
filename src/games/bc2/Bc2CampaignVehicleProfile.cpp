#include "Bc2CampaignVehicleProfile.h"
#include "Bc2ActiveEntryCamera.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string_view>
namespace fvr::bc2 {namespace {
#include "Bc2CampaignFieldReader.inc"
struct Code {unsigned rva;std::initializer_list<unsigned char> bytes;const char* name;};
const Code Proofs[]{
#include "Bc2BoatAimProof.inc"
#include "Bc2CampaignVehicleProof.inc"
};
unsigned Parent(const CampaignVehicleRow& row)noexcept{return (row.packed>>8)&255;}
unsigned Associated(const CampaignVehicleSnapshot& s,unsigned index)noexcept {
    std::array<bool,256> seen{};
    while(index<s.count){if(seen[index]||!s.rows[index].component)return 0;seen[index]=true;
        if(s.rows[index].entry)return s.rows[index].entry;index=Parent(s.rows[index]);}
    return 0;
}
struct Capture {
    Reader r;CampaignVehicleSnapshot& s;
    unsigned Keep(unsigned at)noexcept {
        const auto value=r.Word(at);
        for(unsigned n=0;n<s.wordCount;++n)if(s.words[n].address==at){if(s.words[n].value!=value)r.okay=false;return value;}
        if(s.wordCount==s.words.size()){r.okay=false;return 0;}
        s.words[s.wordCount++]={at,value};return value;
    }
    bool Type(unsigned at,const char* name)noexcept{return r.memory.type(r.memory.context,at,name);}
};
bool Route(const VehicleRouteSnapshot& seat,unsigned action,unsigned conceptId)noexcept {
    return std::find(seat.routes.begin(),seat.routes.begin()+seat.count,VehicleRoute{action,conceptId})!=seat.routes.begin()+seat.count;
}
bool Mapping(Capture& c,const VehicleRouteSnapshot& seat)noexcept {
    auto& r=c.r;const auto data=c.Keep(seat.identity.entry+12);
    if(!c.Type(data,"PlayerEntryComponentData")||
       !r.Field(data,"EntryComponentData","EntryClass",184,"EntryClass")||
       !r.Field(data,"EntryComponentData","InputMapping",192,"InputActionMappingsData"))return false;
    c.s.entryClass=c.Keep(data+184);if(c.s.entryClass>1)return false;
    const auto map=c.Keep(data+192);
    if(!c.Type(map,"InputActionMappingsData")||!r.Field(map,"InputActionMappingsData","Mappings",12,"ArrayBase")||c.Keep(map+24)!=seat.count)return false;
    const auto array=c.Keep(map+28);std::array<VehicleRoute,50> rows{};
    for(unsigned n=0;n<seat.count;++n){const auto row=c.Keep(array+4*n);
        if(!c.Type(row,"EntryInputActionMappingData")||
           !r.Field(row,"EntryInputActionMappingData","ConceptIdentifier",12,"InputConceptIdentifiers")||
           !r.Field(row,"EntryInputActionMappingData","ActionIdentifier",16,"EntryInputActionEnum"))return false;
        rows[n]={c.Keep(row+16),c.Keep(row+12)};
    }
    std::sort(rows.begin(),rows.begin()+seat.count,[](const auto& a,const auto& b){return a.action<b.action;});
    for(unsigned n=0;n<seat.count;++n)if(rows[n]!=seat.routes[n]||(n&&rows[n].action==rows[n-1].action))return false;
    return r.okay;
}
// Every rotating consumer is joined to this entry and its current prediction.
// Camera and gun may use distinct native joints sharing authored configs (the
// existing PBL path and BMD3 driver both do). No transform is invented here.
bool Joints(Capture& c,unsigned image,unsigned index,std::array<unsigned,2>& joints)noexcept {
    auto& s=c.s;auto& r=c.r;std::array<bool,256> seen{};
    while(index<s.count){if(seen[index])return false;if(Associated(s,index)!=s.owner.entry)break;seen[index]=true;
        const auto component=s.rows[index].component;
        if(r.Word(component)==image+0x1031028){
            if(!c.Type(component,"ClientChildComponent"))return false;
            c.Keep(component);const auto data=c.Keep(component+12);
            if(!r.Field(data,"ChildComponentData","MovingBody",244,"MovingBodyData"))return false;
            const auto config=c.Keep(data+244);
            if(config&&c.Type(config,"ChildRotationBodyData")){
                if(!r.Field(config,"ChildRotationBodyData","RotationInput",56,"EntryInputActionEnum")||
                   !r.Field(config,"RotationBodyData","RotationAxis",28,"Int32"))return false;
                const auto action=c.Keep(config+56),axis=c.Keep(config+28);
                if(action==5||action==6){const unsigned semantic=action==6?0:1;
                    if(axis!=(semantic==0?1u:0u)||joints[semantic]||c.Keep(component+0xd0)!=config)return false;
                    const auto prediction=c.Keep(component+0x100),table=c.Keep(prediction);
                    if(c.Keep(prediction+0x10)!=component||c.Keep(table+4)!=image+0x3d2ee0)return false;
                    joints[semantic]=config;
                }
            }
        }
        index=Parent(s.rows[index]);
    }
    return r.okay;
}
}
CampaignVehicleBinding VerifyCampaignVehicleCode(const VehicleRouteMemory& memory,unsigned image)noexcept {
    if(!memory.read||!image||image>UINT32_MAX-0x1800000)return {};
    for(const auto& p:Proofs){std::array<unsigned char,1024> bytes{};
        if(p.bytes.size()>bytes.size()||!memory.read(memory.context,image+p.rva,bytes.data(),p.bytes.size())||
           !std::equal(p.bytes.begin(),p.bytes.end(),bytes.begin()))return {};}
    return {image,true};
}
bool RevalidateCampaignVehicleProfile(const VehicleRouteMemory& memory,const CampaignVehicleSnapshot& s,const VehicleRouteSnapshot& seat)noexcept {
    if(!memory.read||s.owner!=seat.identity||s.profile.routeFingerprint!=seat.fingerprint||!s.collection||
       !s.count||s.count>s.rows.size()||s.wordCount>s.words.size())return false;
    Reader r{memory};std::array<unsigned char,16> header{};std::array<CampaignVehicleRow,256> after{};
    if(!r.Read(s.collection,header.data(),16)||r.Word(s.collection)!=s.owner.controlled||header[13]!=s.count||header[14]!=s.layout||
       !r.Read(s.collection+16,after.data(),s.count*16))return false;
    for(unsigned n=0;n<s.count;++n)if(after[n].component!=s.rows[n].component||after[n].entry!=s.rows[n].entry||Parent(after[n])!=Parent(s.rows[n]))return false;
    for(unsigned n=0;n<s.wordCount;++n)if(r.Word(s.words[n].address)!=s.words[n].value)return false;
    return r.okay;
}
std::optional<CampaignVehicleSnapshot> ReadCampaignVehicleProfile(const VehicleRouteMemory& memory,const CampaignVehicleBinding& binding,const VehicleRouteSnapshot& seat)noexcept {
    const auto& id=seat.identity;
    if(!binding.verified||!binding.image||!memory.read||!memory.type||!seat.player||!seat.fingerprint||!seat.count||seat.count>50||
       !id.actor||!id.actorGeneration||!id.seatGeneration||!id.controlled||!id.entry||!id.router||!id.cache)return {};
    CampaignVehicleSnapshot s;s.owner=id;s.profile.routeFingerprint=seat.fingerprint;Capture c{{memory},s};auto& r=c.r;
    if(!c.Type(id.controlled,"ClientVehicleEntity")||!c.Type(id.entry,"ClientPlayerEntryComponent")||
       c.Keep(id.entry+0x1c4)!=seat.player||c.Keep(seat.player+0xc74)!=id.cache||c.Keep(id.entry+0x198)!=id.router||!Mapping(c,seat))return {};
    s.collection=c.Keep(id.entry+0x24);std::array<unsigned char,16> header{};unsigned char entryIndex=0;
    if(!r.Read(s.collection,header.data(),16)||!r.Read(id.entry+0x28,&entryIndex,1))return {};
    s.count=header[13];s.layout=header[14];
    if(r.Word(s.collection)!=id.controlled||!s.count||entryIndex>=s.count||s.layout<s.count||
       !r.Read(s.collection+16,s.rows.data(),s.count*16)||s.rows[entryIndex].component!=id.entry)return {};
    for(unsigned n=0;n<s.count;++n){const auto& row=s.rows[n];unsigned char index=0;
        if(!row.component||(Parent(row)!=255&&Parent(row)>=s.count)||r.Word(row.component+0x24)!=s.collection||
           !r.Read(row.component+0x28,&index,1)||index!=n)return {};}
    // Ground/sea driving uses the native MoveLR->Yaw route. Air's yaw/strafe
    // mapping fails here. EntryClass is corroboration, never the authority.
    const auto root=s.rows[0].component;
    if(Associated(s,0)==id.entry&&c.Type(root,"ClientVehicleComponent")&&c.Keep(root)==binding.image+0x1030b98&&
       s.entryClass==0&&Route(seat,0,0)&&Route(seat,4,1)){
        const auto prediction=c.Keep(root+0x1c4),current=c.Keep(root+0x1cc),bridge=c.Keep(root+0x1d0);
        const auto table=c.Keep(prediction),bridgeTable=c.Keep(bridge);
        if(prediction&&prediction==current&&c.Keep(table+4)==binding.image+0x473b60&&
           c.Keep(prediction+0x16c)==bridge&&c.Keep(bridge+4)==root&&c.Keep(bridgeTable)==binding.image+0x3c77c0&&
           c.Keep(root+0xd4)&&c.Keep(root+0xd8)){
            s.driver=root;s.profile.axes[0]=EntryAction::Throttle;s.profile.axes[1]=EntryAction::Yaw;
        }
    }
    // Follow the actual native camera state, which can differ from the entry
    // variant at +d8. Keep every join for the normal cached revalidation.
    const auto activeCamera=ReadActiveEntryCamera(id.entry,binding.image,[&](unsigned at){return c.Keep(at);});
    if(!activeCamera||!c.Type(activeCamera->component,"ClientCameraComponent"))return {};
    s.camera=activeCamera->camera;const auto component=activeCamera->component;
    unsigned cameraIndex=s.count;for(unsigned n=0;n<s.count;++n)if(s.rows[n].component==component)cameraIndex=n;
    if(cameraIndex==s.count||Associated(s,cameraIndex)!=id.entry)return {};
    std::array<unsigned,2> cameraJoints{};if(!Joints(c,binding.image,cameraIndex,cameraJoints))return {};
    if(cameraJoints[0]&&cameraJoints[1]&&Route(seat,6,8)&&Route(seat,5,7)&&Route(seat,8,9)){
        for(unsigned n=0;n<s.count;++n){const auto weapon=s.rows[n].component;
            if(Associated(s,n)!=id.entry||r.Word(weapon)!=binding.image+0x1030d38)continue;
            c.Keep(weapon);if(!c.Type(weapon,"ClientWeaponComponent"))return {};
            const auto data=c.Keep(weapon+12),native=c.Keep(weapon+0xd0),nativeTable=c.Keep(native);
            if(c.Keep(nativeTable+8)!=binding.image+0x3d4c10||!r.Field(data,"WeaponComponentData","WeaponFiring",216,"WeaponFiringData"))return {};
            const auto firing=c.Keep(data+216);
            if(c.Keep(native+12)!=firing||!c.Type(firing,"WeaponFiringData")||!r.Field(firing,"WeaponFiringData","PrimaryFire",64,"FiringFunctionData"))return {};
            const auto primary=c.Keep(firing+64);
            if(!c.Type(primary,"FiringFunctionData")||!r.Field(primary,"FiringFunctionData","FireLogic",12,"FireLogicData"))return {};
            if(c.Keep(primary+0x4c)!=8)continue; // Alt fire/horn/smoke remain native.
            std::array<unsigned,2> weaponJoints{};
            if(!Joints(c,binding.image,n,weaponJoints)||weaponJoints!=cameraJoints)return {};
            ++s.weapons;
        }
        if(s.weapons){s.profile.axes[2]=static_cast<EntryAction>(6);s.profile.axes[3]=EntryAction::Pitch;s.profile.axes[4]=EntryAction::Fire;}
    }
    if(!s.driver&&!s.weapons)return {};
    s.profile.role=s.driver?(s.weapons?VehicleSeatRole::DriverGunner:VehicleSeatRole::Driver):VehicleSeatRole::Gunner;
    s.profile.exitVerified=Route(seat,16,36);
    if(!r.okay||!RevalidateCampaignVehicleProfile(memory,s,seat))return {};return s;
}
bool CampaignVehiclePlaying(const VehicleSeatProfile& p,unsigned allowed)noexcept {
    const auto pair=[&](unsigned a,unsigned b){if(!p.axes[a]||!p.axes[b])return false;
        const auto x=unsigned(*p.axes[a]),y=unsigned(*p.axes[b]);if(x>10||y>10)return false;
        const auto mask=(1u<<x)|(1u<<y);return (allowed&mask)==mask;};
    return ((p.role==VehicleSeatRole::Driver||p.role==VehicleSeatRole::DriverGunner)&&pair(0,1))||
           ((p.role==VehicleSeatRole::Gunner||p.role==VehicleSeatRole::DriverGunner)&&pair(2,3));
}
VehicleInputCommand CampaignVehicleCommand(const VehicleSeatIdentity& owner,const interaction::VehicleControlOutput& controls)noexcept {
    return {owner,controls.active,{controls.axes.throttle,controls.axes.steer,controls.axes.lookYaw,controls.axes.lookPitch,controls.axes.fire},controls.axes.exit};
}
}
