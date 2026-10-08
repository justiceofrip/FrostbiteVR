#include "Bc2ReloadState.h"
#include "fvr/engine/BindingValidation.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace fvr::bc2 {
namespace {
constexpr std::array<std::uint32_t,6> codeSizes{78,1732,772,163,254,44};
// Whole inspected functions, not an executable-name or asset-name allowlist.
constexpr std::array<std::uint64_t,6> codeHashes{0xd6e5e29dc167db84ull,0x71118b4d657473d3ull,
    0x7344b32b39a9bd85ull,0x3cae7f07de53d0f7ull,0xd83485215d90a16aull,0x5b5607034a1b32afull};
constexpr std::array<const char*,6> signatures{
    "56 8B F1 C7 06 ?? ?? ?? ?? C7 46 04 ?? ?? ?? ?? 8B 46 18 85 C0 74 0E 3B 46 28 74 09 50 E8 ?? ?? ?? ?? 83 C4 04 8B 4E 14 85 C9 74 0C 8B 01 8B 90 8C 00 00 00 6A 01 FF D2 F6 44 24 08 01",
    "F3 0F 10 44 24 08 53 8B 5C 24 08 55 56 8B F1 8B 46 08 8B 68 40 F3 0F 11",
    "51 53 56 8B F1 8B 4C 24 14 D9 46 50 F3 0F 10 01 8B 46 08 8B 58 40 F3 0F",
    "53 8B 5C 24 08 56 8B F1 8B 46 3C 83 E8 08 57 74 2A 83 E8 01 75 4B 38 43",
    "51 55 56 8B F1 8B AE 80 00 00 00 85 ED 75 2D F6 86 A8 00 00 00 08 75 24",
    "51 8B 81 98 00 00 00 83 F8 FF 75 10 8B 41 0C 8B 40 14 83 F8 FF 89 04 24"};
std::uint64_t Hash(std::span<const std::byte> b){std::uint64_t h=14695981039346656037ull;for(auto x:b){h^=std::to_integer<unsigned char>(x);h*=1099511628211ull;}return h;}
template<class T>T Value(const std::byte* b,std::size_t at){T out{};std::memcpy(&out,b+at,sizeof(out));return out;}
std::optional<std::size_t> Offset(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,std::size_t size,bool executable=false){
    for(const auto& s:pe.sections)if(rva>=s.rva&&(!executable||(s.flags&0x20000000))){
        const auto delta=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+delta;
        if(delta<=s.rawSize&&size<=s.rawSize-delta&&at<=b.size()&&size<=b.size()-at)return std::size_t(at);
    }return {};
}
std::optional<std::uint32_t> Unique(std::span<const std::byte> b,const engine::PeImage& pe,const char* signature){
    const auto p=engine::ParsePattern(signature);if(!p)return {};std::optional<std::uint32_t> found;
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=b.size()&&s.rawSize<=b.size()-s.rawOffset&&p->size()<=s.rawSize){
        for(std::size_t at=0;at<=s.rawSize-p->size();++at){
            bool matches=true;for(std::size_t k=0;k<p->size();++k)if(!(*p)[k].wildcard&&std::to_integer<unsigned char>(b[s.rawOffset+at+k])!=(*p)[k].value){matches=false;break;}
            if(matches){if(found)return {};found=s.rva+std::uint32_t(at);}
        }
    }return found;
}
struct Reader {
    const ReloadStateMemory& m;std::uint32_t imageBase,imageSize;mutable bool failed=false;
    bool Read(std::uint64_t at,void* dst,std::size_t size)const{
        if(!m.read||at<0x10000||!size||size>4096||at+size>UINT32_MAX||!m.read(m.context,std::uint32_t(at),dst,size)){failed=true;return false;}return true;
    }
    template<class T>bool Get(std::uint64_t at,T& out)const{return Read(at,&out,sizeof(out));}
    bool Type(std::uint32_t at,const char* name)const{return m.type&&at>=0x10000&&m.type(m.context,at,name);}
    bool Image(std::uint32_t at,std::size_t n)const{return at>=imageBase&&std::uint64_t(at)+n<=std::uint64_t(imageBase)+imageSize;}
    template<std::size_t N>bool Text(std::uint32_t at,std::array<char,N>& out)const{
        out={};for(std::size_t i=0;i<N;++i){unsigned char c=0;if(!Get(std::uint64_t(at)+i,c))return false;if(!c)return i>0;if(c<32||c>126)return false;out[i]=char(c);}return false;
    }
    bool TypeInfo(std::uint32_t info,const char* name,std::uint16_t wantedSize,std::uint32_t& metadata)const{
        std::uint32_t text=0;std::uint16_t size=0;std::array<char,64> actual{};
        return Image(info,8)&&Get(std::uint64_t(info)+4,metadata)&&Image(metadata,28)&&Get(metadata,text)&&Image(text,std::strlen(name)+1)&&
            Text(text,actual)&&!std::strcmp(actual.data(),name)&&Get(std::uint64_t(metadata)+6,size)&&size==wantedSize;
    }
};
struct NativeOwner {
    std::uint8_t flags=0,playerFlags=0;std::uint32_t inventory=0,switching=0,begin=0,end=0,slot=0;
    std::array<std::uint32_t,64> items{};
    bool operator==(const NativeOwner&)const=default;
};
bool ReadOwner(const Reader& r,const ReloadStateOwner& expected,NativeOwner& o){
    std::uint32_t weak=0,target=0,player=0,controlled=0;
    if(!r.Type(expected.soldier,"ClientSoldierEntity")||!r.Get(std::uint64_t(expected.player)+0xccd,o.playerFlags)||!(o.playerFlags&8)||
       !r.Get(std::uint64_t(expected.player)+0xc54,weak)||weak!=expected.weak||!r.Get(weak,target)||std::uint64_t(expected.soldier)+4!=target||
       !r.Get(std::uint64_t(expected.soldier)+0x220,player)||player!=expected.player||
       !r.Get(std::uint64_t(expected.player)+0xc68,controlled)||controlled!=expected.soldier||
       !r.Get(std::uint64_t(expected.soldier)+0x114,o.flags)||
       !r.Get(std::uint64_t(expected.soldier)+((o.flags&1)?0x24c:0x248),o.inventory)||
       !r.Get(std::uint64_t(o.inventory)+4,o.switching)||!r.Type(o.switching,"WeaponSwitchingData")||
       !r.Get(std::uint64_t(expected.soldier)+0x260,o.begin)||!r.Get(std::uint64_t(expected.soldier)+0x264,o.end)||
       o.end<=o.begin||o.end-o.begin>256||(o.end-o.begin)%4||
       !r.Get(std::uint64_t(o.inventory)+0x14c,o.slot)||o.slot>=(o.end-o.begin)/4||
       !r.Read(o.begin,o.items.data(),o.end-o.begin)||o.items[o.slot]!=expected.weapon)return false;
    return std::count(o.items.begin(),o.items.end(),expected.weapon)==1;
}
struct ConfigBytes {
    std::array<std::byte,0x8c> logic{};std::array<std::byte,0x24> ammo{};
    std::array<std::uint32_t,4> stateArray{};std::uint32_t name=0,path=0;
    bool operator==(const ConfigBytes&)const=default;
};
bool ReadConfig(const Reader& r,std::uint32_t weapon,ReloadObservedConfig& c,ConfigBytes& raw){
    if(!r.Get(std::uint64_t(weapon)+4,c.weaponData)||!r.Type(c.weaponData,"SoldierWeaponData")||
       !r.Get(std::uint64_t(c.weaponData)+0x98,c.firingData)||!r.Type(c.firingData,"WeaponFiringData")||
       !r.Get(std::uint64_t(c.firingData)+0x40,c.primaryFire)||!r.Type(c.primaryFire,"FiringFunctionData")||
       std::uint64_t(c.primaryFire)+0x194>UINT32_MAX||
       !r.Get(std::uint64_t(c.weaponData)+0xc,raw.name)||!r.Text(raw.name,c.assetName)||
       !r.Get(std::uint64_t(c.weaponData)+0x40,raw.path)||!r.Text(raw.path,c.assetPath)||
       !r.Read(std::uint64_t(c.primaryFire)+0xc,raw.logic.data(),raw.logic.size())||
       !r.Read(std::uint64_t(c.primaryFire)+0x170,raw.ammo.data(),raw.ammo.size())||
       !r.Read(std::uint64_t(c.weaponData)+0x88,raw.stateArray.data(),16))return false;
    c.ammoAddress=c.primaryFire+0x170;
    const auto* l=raw.logic.data();c.fireLogicType=Value<std::int32_t>(l,0x18);c.reloadType=Value<std::int32_t>(l,0x14);
    c.fireInputAction=Value<std::int32_t>(l,0x40);c.reloadInputAction=Value<std::int32_t>(l,0x80);
    c.reloadDelay=Value<float>(l,8);c.reloadTime=Value<float>(l,12);c.reloadThreshold=Value<float>(l,4);c.postReloadTime=Value<float>(l,0);
    c.boltDelay=Value<float>(l,0x48);c.boltTime=Value<float>(l,0x44);
    const auto fire=Value<unsigned char>(l,0x4d),zoom=Value<unsigned char>(l,0x4c);
    if(fire>1||zoom>1)return false;c.holdBoltUntilFireRelease=fire!=0;c.holdBoltUntilZoomRelease=zoom!=0;
    c.baseCapacity=Value<std::int32_t>(raw.ammo.data(),0x14);c.numberOfMagazines=Value<std::int32_t>(raw.ammo.data(),0x10);
    if(c.fireLogicType<0||c.fireLogicType>255||c.reloadType<0||c.reloadType>255||c.fireInputAction<0||c.fireInputAction>=49||c.reloadInputAction<0||c.reloadInputAction>=49||
       c.baseCapacity< -1||c.baseCapacity>1000000||c.numberOfMagazines< -1||c.numberOfMagazines>1000000)return false;
    for(float time:{c.reloadDelay,c.reloadTime,c.postReloadTime,c.boltDelay,c.boltTime})if(!std::isfinite(time)||time<0||time>3600)return false;
    if(!std::isfinite(c.reloadThreshold)||c.reloadThreshold<0||c.reloadThreshold>1)return false;
    // Verify the reflected value-type layout before sampling visual pump flags.
    std::uint32_t meta=0,fields=0;std::uint16_t flags=0;std::uint8_t count=0;
    if(!r.TypeInfo(raw.stateArray[1],"WeaponStateData",0xc8,meta)||!r.Get(std::uint64_t(meta)+4,flags)||flags!=0x29||
       !r.Get(std::uint64_t(meta)+13,count)||!count||count>64||!r.Get(std::uint64_t(meta)+24,fields)||!r.Image(fields,std::size_t(count)*24))return false;
    unsigned matches=0;
    for(unsigned n=0;n<count;++n){
        std::array<std::uint32_t,6> field{};std::array<char,64> name{};
        if(!r.Read(std::uint64_t(fields)+n*24,field.data(),24)||!r.Image(field[0],1)||!r.Text(field[0],name))return false;
        if(std::strcmp(name.data(),"IsPumpAction"))continue;
        std::uint32_t booleanMeta=0;
        if(++matches!=1||field[4]!=0xc0||!r.TypeInfo(field[2],"Boolean",1,booleanMeta))return false;
    }
    const auto begin=raw.stateArray[2],end=raw.stateArray[3];
    if(matches!=1||end<=begin||end-begin>8*0xc8||(end-begin)%0xc8)return false;
    c.authoredStateCount=std::uint8_t((end-begin)/0xc8);
    for(unsigned i=0;i<c.authoredStateCount;++i){std::uint8_t pump=0;if(!r.Get(std::uint64_t(begin)+i*0xc8+0xc0,pump)||pump>1)return false;c.authoredPumpHandling[i]=pump!=0;}
    return true;
}
ReloadObservedPhase Phase(std::uint32_t s){switch(s){
    case 1:return ReloadObservedPhase::ReturnWait;case 2:return ReloadObservedPhase::InputProcessing;
    case 5:case 6:return ReloadObservedPhase::ShotStep;case 7:return ReloadObservedPhase::BoltHold;case 8:return ReloadObservedPhase::BoltCycle;
    case 10:return ReloadObservedPhase::ReloadBegin;case 11:return ReloadObservedPhase::ReloadWait;case 12:return ReloadObservedPhase::ReloadTransfer;default:return ReloadObservedPhase::Unknown;}}
using FiringBytes=std::array<std::byte,0xb0>;
bool ReadBranch(const Reader& r,const ReloadStateBinding& binding,std::uint32_t weapon,const ReloadObservedConfig& config,unsigned offset,ReloadFiringObservation& b,FiringBytes& raw){
    b.wrapperOffset=offset;
    if(!r.Get(std::uint64_t(weapon)+offset,b.address)||!r.Read(b.address,raw.data(),raw.size()))return false;
    const auto* p=raw.data();
    if(Value<std::uint32_t>(p,0)!=r.imageBase+binding.firingVtableRva||Value<std::uint32_t>(p,8)!=config.firingData||Value<std::uint32_t>(p,12)!=config.ammoAddress)return false;
    b.currentState=Value<std::uint32_t>(p,0x3c);b.previousState=Value<std::uint32_t>(p,0x40);b.nextState=Value<std::uint32_t>(p,0x44);
    b.phaseTimer=Value<float>(p,0x50);b.capacityMultiplier=Value<float>(p,0x74);b.reserveMultiplier=Value<float>(p,0x78);
    b.loaded=Value<std::int32_t>(p,0x7c);b.reserve=Value<std::int32_t>(p,0x80);b.capacityOverride=Value<std::int32_t>(p,0x98);b.flagsA8=Value<std::uint8_t>(p,0xa8);
    if(b.currentState>15||b.previousState>15||b.nextState>15||!std::isfinite(b.phaseTimer)||std::abs(b.phaseTimer)>1000000||
       !std::isfinite(b.capacityMultiplier)||b.capacityMultiplier<0||b.capacityMultiplier>1024||
       !std::isfinite(b.reserveMultiplier)||b.reserveMultiplier<0||b.reserveMultiplier>1024)return false;
    for(auto n:{b.loaded,b.reserve,b.capacityOverride})if(n< -1||n>1000000)return false;
    if(b.capacityOverride>=0)b.effectiveCapacity=b.capacityOverride;
    else if(config.baseCapacity>=0){const double product=double(config.baseCapacity)*b.capacityMultiplier;
        // The native conversion helper's nonintegral rounding is not inferred.
        if(product<=1000000&&std::floor(product)==product)b.effectiveCapacity=std::int32_t(product);
    }
    b.phase=Phase(b.currentState);b.nativeBoltCycleConfigured=config.fireLogicType==1;
    const bool finiteAmmo=b.loaded>=0&&b.reserve>=0&&config.numberOfMagazines>=0&&!(b.flagsA8&8)&&b.effectiveCapacity.has_value();
    if(finiteAmmo&&(config.reloadType==0||config.reloadType==1)&&config.fireInputAction==8&&config.reloadInputAction==29){
        if(b.currentState!=2)b.reloadAmmo=ReloadAmmoEligibility::NativePhaseBusy;
        else if(b.loaded>=*b.effectiveCapacity)b.reloadAmmo=ReloadAmmoEligibility::NoRoom;
        else if(!b.reserve)b.reloadAmmo=ReloadAmmoEligibility::NoReserve;
        else b.reloadAmmo=ReloadAmmoEligibility::ObservedCandidate;
    }
    return true;
}
}
std::optional<ReloadStateBinding> DiscoverReloadState(std::span<const std::byte> bytes,const engine::PeImage& pe){
    if(pe.machine!=0x14c||bytes.size()<64)return {};
    const auto nt=Value<std::uint32_t>(bytes.data(),0x3c);
    if(std::uint64_t(nt)+56>bytes.size())return {};
    ReloadStateBinding b;b.preferredBase=Value<std::uint32_t>(bytes.data(),std::size_t(nt)+52);b.imageSize=pe.imageSize;
    if(b.preferredBase!=0x400000||!b.imageSize||std::uint64_t(b.preferredBase)+b.imageSize>UINT32_MAX)return {};
    for(unsigned n=0;n<b.code.size();++n){const auto rva=Unique(bytes,pe,signatures[n]);if(!rva)return {};
        const auto at=Offset(bytes,pe,*rva,codeSizes[n],true);if(!at||Hash(bytes.subspan(*at,codeSizes[n]))!=codeHashes[n])return {};
        b.code[n]={*rva,codeSizes[n],codeHashes[n]};}
    const auto destructor=Offset(bytes,pe,b.code[0].rva,78,true);if(!destructor)return {};
    const auto table=Value<std::uint32_t>(bytes.data(),*destructor+5);if(table<b.preferredBase)return {};b.firingVtableRva=table-b.preferredBase;
    const auto tableAt=Offset(bytes,pe,b.firingVtableRva,12);if(!tableAt)return {};
    std::memcpy(b.firingVtableWords.data(),bytes.data()+*tableAt,12);
    for(auto pointer:b.firingVtableWords)if(pointer<b.preferredBase||!Offset(bytes,pe,pointer-b.preferredBase,1,true))return {};
    return b;
}
bool ValidateReloadStateLive(const ReloadStateMemory& memory,const ReloadStateBinding& b,std::uint32_t imageBase)noexcept {
    if(imageBase!=b.preferredBase||imageBase!=0x400000||!b.imageSize||std::uint64_t(imageBase)+b.imageSize>UINT32_MAX)return false;
    Reader r{memory,imageBase,b.imageSize};std::array<std::byte,2048> code{};std::array<std::uint32_t,3> table{};
    for(unsigned i=0;i<b.code.size();++i){const auto& proof=b.code[i];
        if(proof.size!=codeSizes[i]||proof.fingerprint!=codeHashes[i]||std::uint64_t(proof.rva)+proof.size>b.imageSize||
           !r.Read(std::uint64_t(imageBase)+proof.rva,code.data(),proof.size)||Hash({code.data(),proof.size})!=proof.fingerprint)return false;}
    return std::uint64_t(b.firingVtableRva)+12<=b.imageSize&&r.Read(std::uint64_t(imageBase)+b.firingVtableRva,table.data(),12)&&table==b.firingVtableWords;
}
ReloadStateResult ReadReloadState(const ReloadStateMemory& memory,const ReloadStateBinding& binding,std::uint32_t imageBase,
    const ReloadStateOwner& owner,std::uint64_t sequence,std::int64_t observedNs)noexcept {
    if(!memory.read||!memory.type||!sequence||observedNs<=0||!owner.player||!owner.soldier||!owner.weak||!owner.weapon||
       !owner.actorGeneration||!owner.equipGeneration||!owner.space||imageBase!=binding.preferredBase||!binding.imageSize||
       std::uint64_t(imageBase)+binding.imageSize>UINT32_MAX||std::uint64_t(binding.firingVtableRva)+12>binding.imageSize)return {};
    Reader r{memory,imageBase,binding.imageSize};
    const auto fail=[&](ReloadStateStatus status){return ReloadStateResult{r.failed?ReloadStateStatus::ReadFailure:status,{}};};
    NativeOwner native{};if(!ReadOwner(r,owner,native))return fail(ReloadStateStatus::OwnerMismatch);
    ReloadStateSnapshot out;out.owner=owner;out.sequence=sequence;out.observedNs=observedNs;
    out.inventory=native.inventory;out.selectedSlot=native.slot;out.soldierFlags=native.flags;out.eligibilityBranch=(native.flags&0x10)?1:0;
    ConfigBytes configRaw{};if(!ReadConfig(r,owner.weapon,out.config,configRaw))return fail(ReloadStateStatus::MalformedConfiguration);
    std::array<FiringBytes,2> raw{};
    for(unsigned i=0;i<2;++i)if(!ReadBranch(r,binding,owner.weapon,out.config,i?0x40:0x3c,out.branches[i],raw[i]))return fail(ReloadStateStatus::MalformedState);
    if(out.branches[0].address==out.branches[1].address)return fail(ReloadStateStatus::MalformedState);
    for(unsigned i=0;i<2;++i){ReloadFiringObservation again;FiringBytes bytes{};
        if(!ReadBranch(r,binding,owner.weapon,out.config,i?0x40:0x3c,again,bytes)||again.address!=out.branches[i].address||bytes!=raw[i])return fail(ReloadStateStatus::ChangedDuringRead);}
    ReloadObservedConfig againConfig;ConfigBytes againRaw{};NativeOwner againOwner;
    if(!ReadConfig(r,owner.weapon,againConfig,againRaw)||againConfig!=out.config||againRaw!=configRaw)return fail(ReloadStateStatus::ChangedDuringRead);
    for(unsigned i=0;i<2;++i){std::uint32_t pointer=0;
        if(!r.Get(std::uint64_t(owner.weapon)+(i?0x40:0x3c),pointer)||pointer!=out.branches[i].address)return fail(ReloadStateStatus::ChangedDuringRead);}
    if(!ReadOwner(r,owner,againOwner)||againOwner!=native)return fail(ReloadStateStatus::ChangedDuringRead);
    out.branchesAgreeOnAmmo=out.branches[0].loaded==out.branches[1].loaded&&out.branches[0].reserve==out.branches[1].reserve;
    out.branchesAgreeOnPhase=out.branches[0].currentState==out.branches[1].currentState;
    return {ReloadStateStatus::Observed,out};
}
}


