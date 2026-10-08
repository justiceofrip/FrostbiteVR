#include "Bc2BoatAim.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string_view>
namespace fvr::bc2 {namespace {
struct Reader {
    const VehicleRouteMemory& memory;bool okay=true;
    bool Read(std::uint64_t at,void* dst,std::size_t size)noexcept {
        if(at<0x10000||at+size>UINT32_MAX||!size||size>4096||!memory.read||!memory.read(memory.context,std::uint32_t(at),dst,size)){okay=false;return false;}return true;
    }
    unsigned Word(std::uint64_t at)noexcept{unsigned value=0;Read(at,&value,4);return value;}
    bool Text(unsigned at,std::string_view expected)noexcept{
        if(expected.size()>=128)return false;for(unsigned n=0;n<=expected.size();++n){char c=0;if(!Read(std::uint64_t(at)+n,&c,1)||c!=(n==expected.size()?0:expected[n]))return false;}return true;
    }
    unsigned Info(unsigned object)noexcept {
        std::array<unsigned char,6> code{};if(!Read(Word(std::uint64_t(Word(object))+8),code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return 0;
        unsigned info=0;std::memcpy(&info,code.data()+1,4);return info;
    }
    bool Field(unsigned object,std::string_view parent,std::string_view field,unsigned offset,std::string_view type)noexcept {
        auto info=Info(object);std::array<unsigned,12> visited{};
        for(unsigned depth=0;info&&depth<visited.size();++depth){
            if(std::find(visited.begin(),visited.begin()+depth,info)!=visited.begin()+depth)return false;visited[depth]=info;
            const auto meta=Word(std::uint64_t(info)+4);std::array<unsigned char,16> header{};if(!Read(meta+4,header.data()+4,4)||!Read(meta+13,header.data()+13,1))return false;
            const unsigned flags=header[4]|(unsigned(header[5])<<8),size=header[6]|(unsigned(header[7])<<8),count=header[13];
            if(Text(Word(meta),parent)){
                if(flags!=0x35||count>128||offset>=size)return false;
                const auto records=Word(std::uint64_t(info)+36);unsigned matches=0;
                for(unsigned n=0;n<count;++n){std::array<unsigned,6> row{};if(!Read(std::uint64_t(records)+n*24,row.data(),24))return false;
                    if(Text(row[0],field)){if(row[4]!=offset||!Text(Word(Word(std::uint64_t(row[2])+4)),type))return false;++matches;}}
                return okay&&matches==1;
            }
            info=Word(std::uint64_t(info)+20);
        }return false;
    }
};

struct Code {unsigned rva;std::initializer_list<unsigned char> bytes;const char* name;};
const Code Proofs[]{
#include "Bc2BoatAimProof.inc"
};
struct Row {unsigned component=0,entry=0,flags=0,packed=0;};
unsigned Parent(const Row& r)noexcept{return (r.packed>>8)&255;}
unsigned Associated(const std::array<Row,96>& rows,unsigned count,unsigned index)noexcept {
    std::array<bool,96> seen{};
    for(unsigned n=0;index<count&&n<count;++n){if(seen[index]||!rows[index].component)return 0;seen[index]=true;
        if(rows[index].entry)return rows[index].entry;index=Parent(rows[index]);}
    return 0;
}
std::optional<math::Matrix4> Matrix(Reader& r,unsigned at)noexcept {
    math::Matrix4 m{};if(!r.Read(at,&m,64))return {};
    for(unsigned n=0;n<4;++n)m.values[n][3]=n==3?1.f:0.f;
    for(unsigned n=0;n<4;++n){m.values[2][n]=-m.values[2][n];m.values[n][2]=-m.values[n][2];}
    if(!interaction::InverseRigid(m))return {};return m;
}
math::Matrix4 Rotation(math::Matrix4 m)noexcept {m.values[3]={0,0,0,1};return m;}
float Scalar(Reader& r,unsigned at)noexcept {float v=0;r.Read(at,&v,4);return v;}
bool RotationConfig(Reader& r,unsigned at,unsigned action,unsigned axis)noexcept {
    if(!r.memory.type(r.memory.context,at,"ChildRotationBodyData")||
        !r.Field(at,"ChildRotationBodyData","RotationInput",56,"EntryInputActionEnum")||
        !r.Field(at,"RotationBodyData","RotationAxis",28,"Int32")||
        !r.Field(at,"RotationBodyData","AngularConstraintMin",20,"Float32")||
        !r.Field(at,"RotationBodyData","AngularConstraintMax",24,"Float32")||
        !r.Field(at,"RotationBodyData","AngularMomentumMultiplier",44,"Float32")||
        !r.Field(at,"ChildRotationBodyData","UseLinearInput",72,"Boolean"))return false;
    unsigned char linear=0;r.Read(at+72,&linear,1);
    return r.Word(at+56)==action&&r.Word(at+28)==axis&&linear==1&&Scalar(r,at+44)==600.f&&
        Scalar(r,at+20)==(action==6?-90.f:-35.f)&&Scalar(r,at+24)==(action==6?90.f:15.f);
}
}
BoatAimBinding VerifyBoatAimCode(const VehicleRouteMemory& memory,unsigned image)noexcept {
    if(!memory.read||!image||image>UINT32_MAX-0x1800000)return {};
    for(const auto& p:Proofs){std::array<unsigned char,1024> bytes{};
        if(p.bytes.size()>bytes.size()||!memory.read(memory.context,image+p.rva,bytes.data(),p.bytes.size())||!std::equal(p.bytes.begin(),p.bytes.end(),bytes.begin()))return {};}
    return {image,true};
}
std::optional<BoatAimSnapshot> ReadPblDriverAim(const VehicleRouteMemory& memory,const BoatAimBinding& binding,const VehicleRouteSnapshot& seat)noexcept {
    if(!binding.verified||!binding.image||!memory.read||!memory.type||seat.slot||!seat.player||!seat.identity.actor||!seat.identity.actorGeneration||!seat.identity.seatGeneration)return {};
    Reader r{memory};const auto entry=seat.identity.entry,entity=seat.identity.controlled;
    if(!memory.type(memory.context,entry,"ClientPlayerEntryComponent")||r.Word(entry+0x1c4)!=seat.player||r.Word(seat.player+0xc74)!=seat.identity.cache||r.Word(entry+0x198)!=seat.identity.router)return {};
    const auto collection=r.Word(entry+0x24);std::array<unsigned char,16> header{};
    unsigned char entryIndex=0;if(!r.Read(collection,header.data(),16)||!r.Read(entry+0x28,&entryIndex,1))return {};
    unsigned root=0;std::memcpy(&root,header.data(),4);const unsigned count=header[13],layout=header[14];
    if(root!=entity||!count||count>96||entryIndex>=count||layout<count||layout>128)return {};
    std::array<Row,96> rows{};if(!r.Read(collection+16,rows.data(),count*16)||rows[entryIndex].component!=entry)return {};
    for(unsigned n=0;n<count;++n){const auto parent=Parent(rows[n]);if(parent!=255&&parent>=count)return {};
        unsigned char index=0;if(!rows[n].component||r.Word(rows[n].component+0x24)!=collection||!r.Read(rows[n].component+0x28,&index,1)||index!=n)return {};}
    if(!memory.type(memory.context,rows[0].component,"ClientVehicleComponent"))return {};
    // Actual selected camera record, not the nearest named camera in the asset.
    const auto begin=r.Word(entry+0x19c),end=r.Word(entry+0x1a0),selector=r.Word(entry+0xd8);
    if(!begin||end-begin!=24||r.Word(selector)!=0)return {};
    std::array<unsigned,3> cameraRecord{};if(!r.Read(begin,cameraRecord.data(),12))return {};
    const auto camera=cameraRecord[0],wrapper=cameraRecord[1],table=r.Word(wrapper);
    if(r.Word(table+4)!=binding.image+0x3df4e0||r.Word(table+8)!=binding.image+0x3c4cb0)return {};
    const auto cameraComponent=r.Word(wrapper+4);unsigned cameraIndex=count,weaponIndex=count;
    for(unsigned n=0;n<count;++n){
        if(rows[n].component==cameraComponent)cameraIndex=n;
        if(Associated(rows,count,n)!=entry)continue;
        if(r.Word(rows[n].component)!=binding.image+0x1030d38)continue;
        if(!memory.type(memory.context,rows[n].component,"ClientWeaponComponent"))return {};
        const auto data=r.Word(rows[n].component+12);
        if(!memory.type(memory.context,data,"WeaponComponentData")||!r.Field(data,"GameObjectData","Name",12,"String")||!r.Text(r.Word(data+12),"PBLB#GMG"))return {};
        if(weaponIndex!=count)return {};weaponIndex=n;
    }
    if(cameraIndex==count||weaponIndex==count||Associated(rows,count,cameraIndex)!=entry||!memory.type(memory.context,cameraComponent,"ClientCameraComponent"))return {};
    const auto cameraData=r.Word(cameraComponent+12);
    if(!r.Field(cameraData,"GameObjectData","Name",12,"String")||!r.Text(r.Word(cameraData+12),"Camera 1stP Driver"))return {};
    // Join both camera and gun to their own exact rotating ancestors. Equal
    // configuration pointers alone are insufficient without current ownership.
    std::array<std::array<unsigned,2>,2> joints{},configs{};
    for(unsigned side=0;side<2;++side){unsigned index=side?weaponIndex:cameraIndex;std::array<bool,96> seen{};
        for(unsigned n=0;index<count&&n<count;++n){if(seen[index])return {};seen[index]=true;const auto at=rows[index].component;
            if(Associated(rows,count,index)!=entry)return {};
            if(r.Word(at)==binding.image+0x1031028){const auto data=r.Word(at+12),move=r.Word(data+244);
                if(move&&memory.type(memory.context,move,"ChildRotationBodyData")){
                    const auto action=r.Word(move+56);if(action==5||action==6){const unsigned axis=action==6?0u:1u;
                        if(configs[side][axis]||!r.Field(data,"ChildComponentData","MovingBody",244,"MovingBodyData")||!RotationConfig(r,move,action,action==6?1u:0u)||r.Word(at+0xd0)!=move)return {};
                        const auto prediction=r.Word(at+0x100),predictionTable=r.Word(prediction);
                        if(r.Word(prediction+0x10)!=at||r.Word(predictionTable+4)!=binding.image+0x3d2ee0)return {};
                        joints[side][axis]=index;configs[side][axis]=move;
                    }
                }
            }
            index=Parent(rows[index]);
        }
    }
    if(!configs[0][0]||!configs[0][1]||configs[0]!=configs[1])return {};
    const auto weaponComponent=rows[weaponIndex].component,weaponData=r.Word(weaponComponent+12),nativeWeapon=r.Word(weaponComponent+0xd0),nativeTable=r.Word(nativeWeapon);
    if(r.Word(nativeTable+8)!=binding.image+0x3d4c10||!r.Field(weaponData,"WeaponComponentData","WeaponFiring",216,"WeaponFiringData"))return {};
    const auto firing=r.Word(weaponData+216),primary=r.Word(firing+64);
    if(r.Word(nativeWeapon+12)!=firing||!memory.type(memory.context,firing,"WeaponFiringData")||!r.Field(firing,"WeaponFiringData","PrimaryFire",64,"FiringFunctionData")||
        !memory.type(memory.context,primary,"FiringFunctionData")||!r.Field(primary,"FiringFunctionData","FireLogic",12,"FireLogicData")||r.Word(primary+0x4c)!=8)return {};
    const unsigned local=collection+(layout+1)*16,world=collection+(layout+1)*80;
    const auto hull=Matrix(r,world),cameraWorld=Matrix(r,world+cameraIndex*64),selected=Matrix(r,camera+16),
        yaw=Matrix(r,local+joints[0][0]*64),pitch=Matrix(r,local+joints[0][1]*64);
    if(!hull||!cameraWorld||!selected||!yaw||!pitch)return {};
    for(unsigned n=0;n<3;++n)if(std::abs(cameraWorld->values[3][n]-selected->values[3][n])>.08f)return {};
    const auto inverseHull=interaction::InverseRigid(*hull),inverseJoints=interaction::InverseRigid(interaction::Multiply(Rotation(*pitch),Rotation(*yaw)));
    if(!inverseHull||!inverseJoints)return {};
    // The selected StaticCamera is the renderer-facing basis. The component
    // transform describes the authored +Z frame and has opposite right/forward
    // rows. Use the already ownership-verified camera, not a guessed yaw offset.
    const auto neutral=Rotation(interaction::Multiply(interaction::Multiply(*selected,*inverseHull),*inverseJoints));
    std::array<Row,96> after{};std::array<unsigned char,16> headerAfter{};std::array<unsigned,3> cameraAfter{};
    if(!r.Read(collection,headerAfter.data(),16)||headerAfter!=header||!r.Read(collection+16,after.data(),count*16)||!r.Read(begin,cameraAfter.data(),12)||cameraAfter!=cameraRecord||
       r.Word(entry+0x24)!=collection||r.Word(entry+0x1c4)!=seat.player||r.Word(entry+0x19c)!=begin||r.Word(entry+0x1a0)!=end||r.Word(entry+0xd8)!=selector||r.Word(selector)!=0||r.Word(wrapper+4)!=cameraComponent||r.Word(weaponComponent+0xd0)!=nativeWeapon)return {};
    for(unsigned n=0;n<count;++n)if(rows[n].component!=after[n].component||rows[n].entry!=after[n].entry||Parent(rows[n])!=Parent(after[n]))return {};
    if(!r.okay)return {};
    BoatAimSnapshot out;out.owner=seat.identity;out.player=seat.player;out.collection=collection;out.camera=camera;out.cameraComponent=cameraComponent;out.weaponComponent=weaponComponent;out.nativeWeapon=nativeWeapon;
    out.cameraIndex=cameraIndex;out.weaponIndex=weaponIndex;out.yawIndex=joints[0][0];out.pitchIndex=joints[0][1];out.hull=*hull;out.cameraWorld=*selected;out.neutralCameraLocal=neutral;
    out.jointYaw=std::atan2(yaw->values[2][0],yaw->values[2][2]);out.jointPitch=std::atan2(pitch->values[2][1],pitch->values[2][2]);return out;
}
}
