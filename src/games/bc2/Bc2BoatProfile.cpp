#include "Bc2BoatProfile.h"
#include <algorithm>
#include <cstring>
#include <string_view>
namespace fvr::bc2 {
namespace {
struct Reader {
    const VehicleRouteMemory& memory;bool okay=true;
    bool Read(std::uint64_t at,void* dst,std::size_t size)noexcept {
        if(at<0x10000||at+size>UINT32_MAX||!size||size>4096||!memory.read||!memory.read(memory.context,std::uint32_t(at),dst,size)){okay=false;return false;}return true;
    }
    unsigned Word(std::uint64_t at)noexcept{unsigned value=0;Read(at,&value,4);return value;}
    bool Text(unsigned at,std::string_view expected)noexcept{
        std::array<char,128> data{};return expected.size()<data.size()&&Read(at,data.data(),expected.size()+1)&&!std::memcmp(data.data(),expected.data(),expected.size())&&!data[expected.size()];
    }
    unsigned Info(unsigned object)noexcept {
        std::array<unsigned char,6> code{};if(!Read(Word(std::uint64_t(Word(object))+8),code.data(),6)||code[0]!=0xb8||code[5]!=0xc3)return 0;
        unsigned info=0;std::memcpy(&info,code.data()+1,4);return info;
    }
    bool Field(unsigned object,std::string_view parent,std::string_view field,unsigned offset,std::string_view type)noexcept {
        auto info=Info(object);std::array<unsigned,12> visited{};
        for(unsigned depth=0;info&&depth<visited.size();++depth){
            if(std::find(visited.begin(),visited.begin()+depth,info)!=visited.begin()+depth)return false;visited[depth]=info;
            const auto meta=Word(std::uint64_t(info)+4);std::array<unsigned char,16> header{};if(!Read(meta,header.data(),header.size()))return false;
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
// Authored InputActionMappingsData and live router independently agreed for
// the observed PBLB driver. Addresses are intentionally absent from identity.
constexpr std::array<VehicleRoute,30> PblRoutes{{
    {0,0},{1,6},{2,37},{4,1},{5,7},{6,8},{8,9},{9,51},{10,52},{12,13},{13,35},{14,13},
    {16,36},{17,42},{18,43},{19,44},{20,45},{21,46},{22,47},{23,48},{30,14},
    {39,59},{40,56},{41,75},{42,74},{43,61},{44,62},{45,68},{46,69},{47,70}
}};
}
std::optional<VehicleSeatProfile> ReadPblDriverProfile(const VehicleRouteMemory& memory,const VehicleRouteSnapshot& seat)noexcept {
    if(!memory.read||!memory.type||seat.slot||!seat.fingerprint||seat.count!=PblRoutes.size()||!std::equal(PblRoutes.begin(),PblRoutes.end(),seat.routes.begin()))return {};
    Reader r{memory};const auto data=r.Word(std::uint64_t(seat.identity.controlled)+12),entry=r.Word(std::uint64_t(seat.identity.entry)+12);
    if(!memory.type(memory.context,data,"VehicleEntityData")||!memory.type(memory.context,entry,"PlayerEntryComponentData"))return {};
    if(!r.Field(data,"GameObjectData","Name",12,"String")||!r.Field(data,"VehicleEntityData","Mesh",500,"CompositeMeshAsset")||
       !r.Field(entry,"GameObjectData","Name",12,"String")||!r.Field(entry,"EntryComponentData","EntryOrderNumber",176,"Int32")||
       !r.Field(entry,"EntryComponentData","EntryClass",184,"EntryClass")||!r.Field(entry,"EntryComponentData","InputMapping",192,"InputActionMappingsData"))return {};
    const auto mesh=r.Word(std::uint64_t(data)+500),mapping=r.Word(std::uint64_t(entry)+192);
    if(!r.Text(r.Word(std::uint64_t(data)+12),"PBLB")||!r.Text(r.Word(std::uint64_t(entry)+12),"Entry Driver")||r.Word(std::uint64_t(entry)+176)||r.Word(std::uint64_t(entry)+184)||
       !memory.type(memory.context,mesh,"CompositeMeshAsset")||!r.Text(r.Word(std::uint64_t(mesh)+12),"Objects/Vehicles/Sea/Pbl/Pbl_Mesh")||
       !memory.type(memory.context,mapping,"InputActionMappingsData")||!r.Field(mapping,"InputActionMappingsData","Mappings",12,"ArrayBase"))return {};
    // The observed authored array uses ArrayBase count+12 and data+16. Its
    // reflected row fields must independently match the current hash table.
    if(r.Word(std::uint64_t(mapping)+24)!=PblRoutes.size())return {};
    const auto items=r.Word(std::uint64_t(mapping)+28);std::array<VehicleRoute,30> routes{};
    for(unsigned n=0;n<routes.size();++n){const auto row=r.Word(std::uint64_t(items)+n*4);
        if(!memory.type(memory.context,row,"EntryInputActionMappingData")||!r.Field(row,"EntryInputActionMappingData","ConceptIdentifier",12,"InputConceptIdentifiers")||!r.Field(row,"EntryInputActionMappingData","ActionIdentifier",16,"EntryInputActionEnum"))return {};
        routes[n]={r.Word(std::uint64_t(row)+16),r.Word(std::uint64_t(row)+12)};
    }
    std::sort(routes.begin(),routes.end(),[](const auto& a,const auto& b){return a.action<b.action;});
    if(!r.okay||routes!=PblRoutes||r.Word(std::uint64_t(seat.identity.controlled)+12)!=data||r.Word(std::uint64_t(seat.identity.entry)+12)!=entry||r.Word(std::uint64_t(data)+500)!=mesh||r.Word(std::uint64_t(entry)+192)!=mapping)return {};
    VehicleSeatProfile out;out.role=VehicleSeatRole::Driver;out.routeFingerprint=seat.fingerprint;
    out.axes[unsigned(VehicleAxis::Throttle)]=EntryAction::Throttle;out.axes[unsigned(VehicleAxis::Steer)]=EntryAction::Yaw;out.exitVerified=true;
    // Driver StaticCameraData is fixed, Roll6/Pitch5 with zero ranges and
    // sensitivities. No unproven native look/fire writes; HMD view is separate.
    return out;
}
}
