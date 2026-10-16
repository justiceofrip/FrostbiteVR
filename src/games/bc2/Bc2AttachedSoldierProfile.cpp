#include "Bc2AttachedSoldierProfile.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string_view>
namespace fvr::bc2 {namespace {
#include "Bc2CampaignFieldReader.inc"
struct Code {unsigned rva;std::initializer_list<unsigned char> bytes;const char* name;};
const Code Proofs[]{
#include "Bc2AttachedSoldierProof.inc"
};
struct Capture {
    Reader r;AttachedSoldierSnapshot& s;
    unsigned Keep(std::uint64_t at,unsigned mask=~0u)noexcept {
        const auto value=r.Word(at)&mask;
        if(at>UINT32_MAX){r.okay=false;return 0;}
        for(unsigned n=0;n<s.wordCount;++n)if(s.words[n].address==at&&s.words[n].mask==mask){if(s.words[n].value!=value)r.okay=false;return value;}
        if(s.wordCount==s.words.size()){r.okay=false;return 0;}
        s.words[s.wordCount++]={static_cast<unsigned>(at),value,mask};return value;
    }
    bool Type(unsigned at,const char* name)noexcept{return at&&r.memory.type(r.memory.context,at,name);}
};
bool SameSeat(const VehicleRouteSnapshot& a,const VehicleRouteSnapshot& b)noexcept {
    return a.identity==b.identity&&a.player==b.player&&a.weak==b.weak&&a.slot==b.slot&&a.fingerprint==b.fingerprint&&a.count==b.count&&a.routes==b.routes;
}
struct Row {unsigned component=0,entry=0,flags=0,packed=0;};
}
AttachedSoldierBinding VerifyAttachedSoldierCode(const VehicleRouteMemory& memory,unsigned image)noexcept {
    // The installed image proof includes absolute operands. Another load base
    // needs separately relocated proof rather than silently masking bytes.
    if(!memory.read||image!=0x400000)return {};
    for(const auto& p:Proofs){std::array<unsigned char,512> bytes{};
        if(p.bytes.size()>bytes.size()||!memory.read(memory.context,image+p.rva,bytes.data(),p.bytes.size())||
           !std::equal(p.bytes.begin(),p.bytes.end(),bytes.begin()))return {};}
    return {image,true};
}
bool RevalidateAttachedSoldierOwnership(const VehicleRouteMemory& m,const AttachedSoldierSnapshot& s,const VehicleRouteSnapshot& seat)noexcept {
    if(!m.read||!m.type||!SameSeat(s.seat,seat)||!s.wordCount||s.wordCount>s.words.size())return false;
    Reader r{m};for(unsigned n=0;n<s.wordCount;++n){const auto& w=s.words[n];if((r.Word(w.address)&w.mask)!=w.value)return false;}
    const WeaponModeMemory equipmentMemory{m.context,m.read,m.type};
    return r.okay&&WeaponEquipmentStillCurrent(equipmentMemory,s.equipment);
}
std::optional<AttachedSoldierSnapshot> ReadAttachedSoldierOwnership(const VehicleRouteMemory& m,const AttachedSoldierBinding& b,const VehicleRouteSnapshot& seat)noexcept {
    const auto& id=seat.identity;
    if(!b.verified||b.image!=0x400000||!m.read||!m.type||!seat.player||!seat.weak||!seat.fingerprint||!seat.count||seat.count>50||
       !id.actor||!id.actorGeneration||!id.seatGeneration||!id.entry||!id.router||!id.cache||!id.controlled)return {};
    AttachedSoldierSnapshot s;s.seat=seat;s.soldier=static_cast<unsigned>(id.actor);
    Capture c{{m},s};auto& r=c.r;const std::uint64_t player=seat.player,soldier=s.soldier,entry=id.entry;
    if(static_cast<unsigned>(id.actor>>32)!=seat.weak||!c.Type(s.soldier,"ClientSoldierEntity")||
       !c.Type(id.controlled,"ClientVehicleEntity")||!c.Type(id.entry,"ClientSoldierEntryComponent")||
       c.Keep(player+0xc54)!=seat.weak||c.Keep(seat.weak)!=soldier+4||c.Keep(soldier+0x220)!=player||
       c.Keep(player+0xc68)!=soldier||c.Keep(player+0xc60)!=id.controlled||c.Keep(player+0xc64)!=seat.slot||
       c.Keep(player+0xc74)!=id.cache||c.Keep(entry+0x198)!=id.router||c.Keep(entry+0x1c4)!=player)return {};
    const auto begin=c.Keep(std::uint64_t(id.controlled)+0x7c),end=c.Keep(std::uint64_t(id.controlled)+0x80);
    if(!begin||end<begin||end-begin>256||(end-begin)%4||seat.slot>=(end-begin)/4||c.Keep(std::uint64_t(begin)+seat.slot*4)!=id.entry)return {};
    if(c.Keep(entry)!=b.image+0x10305a0||c.Keep(b.image+0x10305a8)!=b.image+0x3beba0||
       c.Keep(b.image+0x10305a0+0x114)!=b.image+0x3d8090||c.Keep(b.image+0x10305a0+0x118)!=b.image+0x3c7250)return {};
    s.entryData=c.Keep(entry+12);
    if(!c.Type(s.entryData,"SoldierEntryComponentData")||
       !r.Field(s.entryData,"SoldierEntryComponentData","AimingConstraints",0xf8,"AimingConstraintsData"))return {};
    s.constraints=c.Keep(entry+0x1e0);
    if(!s.constraints||c.Keep(soldier+0x258)!=s.constraints)return {};
    for(unsigned n=0;n<4;++n){
        s.authoredDegrees[n]=std::bit_cast<float>(c.Keep(std::uint64_t(s.entryData)+0xf8+n*4));
        s.nativeLimits[n]=std::bit_cast<float>(c.Keep(std::uint64_t(s.constraints)+4+n*4));
        if(!std::isfinite(s.authoredDegrees[n])||!std::isfinite(s.nativeLimits[n]))return {};
    }
    // Native constructor 0x60fa10: data[2,3,0,1]*nativePi/180 -> +4,+8,+c,+10.
    constexpr unsigned sourceIndex[]{2,3,0,1};
    for(unsigned n=0;n<4;++n){const double expected=double(s.authoredDegrees[sourceIndex[n]])*double(3.1415927410125732f)/180.;
        if(std::abs(double(s.nativeLimits[n])-expected)>1e-5*std::max(1.,std::abs(expected)))return {};}
    if(s.nativeLimits[0]>s.nativeLimits[1]||s.nativeLimits[2]>s.nativeLimits[3])return {};
    const auto nativeFlags=c.Keep(soldier+0x114,1); // Native selector bit only; mutable0x18 does not change ownership.
    s.inventory=c.Keep(soldier+(nativeFlags?0x24c:0x248));
    const auto switching=c.Keep(std::uint64_t(s.inventory)+4);
    const auto first=c.Keep(soldier+0x260),last=c.Keep(soldier+0x264);s.selected=c.Keep(std::uint64_t(s.inventory)+0x14c);
    if(!c.Type(switching,"WeaponSwitchingData")||!first||last<first||last-first>256||(last-first)%4||s.selected>=(last-first)/4)return {};
    s.weapon=c.Keep(std::uint64_t(first)+s.selected*4);const auto data=c.Keep(std::uint64_t(s.weapon)+4);
    s.aim=c.Keep(std::uint64_t(s.weapon)+0x2c);const auto aimData=c.Keep(std::uint64_t(data)+0x9c);
    if(!c.Type(data,"SoldierWeaponData")||!s.aim||c.Keep(s.aim)!=aimData||!c.Type(aimData,"SoldierAimingSimulationData"))return {};
    s.aimer=c.Keep(std::uint64_t(s.aim)+8);
    if(!s.aimer||c.Keep(std::uint64_t(s.aimer)+4)!=s.constraints)return {};
    const WeaponModeMemory equipmentMemory{m.context,m.read,m.type};const auto equipment=ReadWeaponEquipmentIdentity(equipmentMemory,s.weapon);
    if(!equipment||equipment->data!=data)return {};s.equipment=*equipment;
    const auto camera=ReadActiveEntryCamera(id.entry,b.image,[&](unsigned at){return c.Keep(at);});
    if(!camera||!c.Type(camera->component,"ClientCameraComponent"))return {};s.camera=*camera;
    s.collection=c.Keep(entry+0x24);const auto packed=c.Keep(std::uint64_t(s.collection)+12,0xffff00);
    const unsigned count=(packed>>8)&255,layout=(packed>>16)&255;
    s.entryIndex=c.Keep(entry+0x28,255);
    if(!count||layout<count||s.entryIndex>=count||c.Keep(s.collection)!=id.controlled)return {};
    std::array<Row,256> rows{};unsigned cameraIndex=count;
    for(unsigned n=0;n<count;++n){const std::uint64_t at=std::uint64_t(s.collection)+16+n*16;
        rows[n].component=c.Keep(at);rows[n].entry=c.Keep(at+4);rows[n].packed=c.Keep(at+12,0xff00);
        if(!rows[n].component||c.Keep(std::uint64_t(rows[n].component)+0x24)!=s.collection||c.Keep(std::uint64_t(rows[n].component)+0x28,255)!=n)return {};
        const auto parent=(rows[n].packed>>8)&255;if(parent!=255&&parent>=count)return {};
        if(rows[n].component==camera->component){if(cameraIndex!=count)return {};cameraIndex=n;}
    }
    if(rows[s.entryIndex].component!=id.entry||cameraIndex==count)return {};s.cameraIndex=cameraIndex;
    std::array<bool,256> seen{};unsigned at=cameraIndex;
    while(at<count){if(seen[at])return {};seen[at]=true;if(rows[at].entry){if(rows[at].entry!=id.entry)return {};break;}at=(rows[at].packed>>8)&255;}
    if(at>=count||!r.okay||!RevalidateAttachedSoldierOwnership(m,s,seat))return {};return s;
}
}
