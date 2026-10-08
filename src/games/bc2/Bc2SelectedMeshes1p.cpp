#include "Bc2SelectedMeshes1p.h"
#include "fvr/engine/BindingValidation.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace fvr::bc2 {
SelectedMeshKind ClassifySelectedMeshPath(std::string_view path)noexcept {
    if(path=="Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh")return SelectedMeshKind::Spas12;
    if(path=="Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh")return SelectedMeshKind::Xm8;
    if(path=="Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh")return SelectedMeshKind::Acog4x;
    return SelectedMeshKind::Unknown;
}
namespace {
constexpr std::array<const char*,4> signatures{
    "B8 01 00 00 00 84 05 ?? ?? ?? ?? 75 66 09 05 ?? ?? ?? ?? 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C0 68 ?? ?? ?? ?? C7 05",
    "51 53 55 8B 6C 24 10 56 57 55 8B F1 E8 ?? ?? ?? ?? 8D 4E 10 E8 ?? ?? ?? ?? 8D 7E 4C 8B CF E8 ?? ?? ?? ?? 8D 4F 14 33 DB",
    "8B 81 54 0C 00 00 85 C0 74 0A 8B 00 85 C0 74 04 83 C0 FC C3 33 C0 C3",
    "8B 44 24 04 56 50 8B F1 E8 ?? ?? ?? ?? 8B 4C 24 10 8B 44 24 0C 8B 54 24 14 89 4E 10 33 C9 38 0D ?? ?? ?? ?? C7 06 ?? ?? ?? ?? 89 46 14 89 4E 18 66 89 4E 1C 66 89 4E 1E 89 4E 20 89 56 24"};
constexpr std::array<unsigned,4> sizes{0x79,0x74,0x1a,0x3e};
template<class T>T Value(const std::byte* p){T v{};std::memcpy(&v,p,sizeof(v));return v;}
std::uint64_t Hash(std::span<const std::byte> bytes){std::uint64_t h=14695981039346656037ull;for(auto c:bytes){h^=std::to_integer<unsigned char>(c);h*=1099511628211ull;}return h;}
std::optional<std::size_t> Offset(const SelectedMeshesBinding& b,std::uint32_t address,std::size_t n,bool code=false){
    if(address<b.preferredBase)return {};const std::uint64_t rva=address-b.preferredBase;
    for(const auto& s:b.pe.sections)if(rva>=s.rva&&(!code||(s.flags&0x20000000))){
        const auto delta=rva-s.rva,at=std::uint64_t(s.rawOffset)+delta;
        if(delta<=s.rawSize&&n<=s.rawSize-delta&&at<=b.executable.size()&&n<=b.executable.size()-at)return std::size_t(at);
    }return {};
}
std::optional<std::uint32_t> Unique(const SelectedMeshesBinding& b,const char* signature){
    const auto pattern=engine::ParsePattern(signature);if(!pattern)return {};
    std::optional<std::uint32_t> found;
    for(const auto& s:b.pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=b.executable.size()&&s.rawSize<=b.executable.size()-s.rawOffset&&pattern->size()<=s.rawSize){
        for(std::size_t i=0;i<=s.rawSize-pattern->size();++i){bool match=true;
            for(std::size_t k=0;k<pattern->size();++k)if(!(*pattern)[k].wildcard&&std::to_integer<unsigned char>(b.executable[s.rawOffset+i+k])!=(*pattern)[k].value){match=false;break;}
            if(match){if(found)return {};found=s.rva+std::uint32_t(i);}
        }
    }return found;
}
struct Reader {
    const ReloadStateMemory& memory;const SelectedMeshesBinding& binding;
    SelectedMeshesStatus error=SelectedMeshesStatus::MalformedMetadata;
    std::uint32_t calls=0,bytes=0;
    bool Read(std::uint64_t at,void* dst,std::size_t n){
        if(calls>=131072||n>2097152-bytes){error=SelectedMeshesStatus::ReadBudget;return false;}
        if(at<0x10000||n==0||n>4096||at+n>UINT32_MAX){error=SelectedMeshesStatus::ReadFailure;return false;}
        ++calls;bytes+=std::uint32_t(n);
        if(!memory.read(memory.context,std::uint32_t(at),dst,n)){error=SelectedMeshesStatus::ReadFailure;return false;}return true;
    }
    template<class T>bool Get(std::uint64_t at,T& value){return Read(at,&value,sizeof(value));}
    bool Image(std::uint32_t at,std::size_t n)const{return at>=binding.preferredBase&&std::uint64_t(at)+n<=std::uint64_t(binding.preferredBase)+binding.pe.imageSize;}
    bool SameFile(std::uint32_t at,std::size_t n,bool code=false){
        std::array<std::byte,256> live{};const auto off=Offset(binding,at,n,code);
        return n<=live.size()&&off&&Read(at,live.data(),n)&&std::memcmp(live.data(),binding.executable.data()+*off,n)==0;
    }
    template<std::size_t N>bool Text(std::uint32_t at,std::array<char,N>& out,bool inImage=false){
        out={};for(std::size_t n=0;n<N;++n){unsigned char c=0;
            if(std::uint64_t(at)+n>UINT32_MAX||(inImage&&!Image(at+std::uint32_t(n),1))||!Get(std::uint64_t(at)+n,c))return false;
            if(!c)return n>0;if(c<32||c>126)return false;out[n]=char(c);
        }return false;
    }
};
struct Type {std::uint32_t address=0,metadata=0,fields=0;std::uint16_t flags=0,size=0;std::uint8_t count=0;std::array<char,192> name{};};
bool Info(Reader& r,std::uint32_t at,Type& t){
    t={};t.address=at;std::uint32_t name=0;
    if(!r.Image(at,8)||!r.Get(std::uint64_t(at)+4,t.metadata)||!r.Image(t.metadata,28)||!r.Get(t.metadata,name)||!r.Text(name,t.name,true)||
       !r.Get(std::uint64_t(t.metadata)+4,t.flags)||!r.Get(std::uint64_t(t.metadata)+6,t.size)||!r.Get(std::uint64_t(t.metadata)+13,t.count)||!t.size||t.count>64)return false;
    if(t.flags==0x35){if(!r.Image(at,40)||!r.Get(std::uint64_t(at)+36,t.fields))return false;}
    else if(t.flags==0x29){if(!r.Get(std::uint64_t(t.metadata)+24,t.fields))return false;}
    else {t.count=0;return true;}
    return !t.count||r.Image(t.fields,std::size_t(t.count)*24);
}
bool Object(Reader& r,std::uint32_t at,const char* expected,Type& t){
    std::uint32_t table=0,getter=0;std::array<std::byte,6> code{};
    if(!r.Get(at,table)||!r.Image(table,12)||!r.Get(std::uint64_t(table)+8,getter)||!r.SameFile(getter,6,true)||!r.Read(getter,code.data(),6)||
       code[0]!=std::byte{0xb8}||code[5]!=std::byte{0xc3}||!Info(r,Value<std::uint32_t>(code.data()+1),t)||std::strcmp(t.name.data(),expected))return false;
    return true;
}
bool IsObject(Reader& r,std::uint32_t at,const char* expected){Type t;return Object(r,at,expected,t);}
struct Field {std::array<std::uint32_t,6> words{};};
bool FieldNamed(Reader& r,const Type& t,const char* wanted,Field& out,unsigned& matches){
    for(unsigned i=0;i<t.count;++i){Field f;std::array<char,192> name{};
        if(!r.Read(std::uint64_t(t.fields)+i*24,f.words.data(),24)||!r.Text(f.words[0],name,true)||f.words[4]>=t.size)return false;
        if(!std::strcmp(name.data(),wanted)){out=f;++matches;}
    }return true;
}
bool FieldType(Reader& r,const Field& f,const char* expected,std::uint32_t offset){Type t;
    return f.words[4]==offset&&Info(r,f.words[2],t)&&!std::strcmp(t.name.data(),expected);
}
struct Reflection {
    std::uint32_t weaponInfo=0,stateInfo=0,meshInfo=0;
    std::array<std::uint32_t,8> inheritance{};
    bool operator==(const Reflection&)const=default;
};
bool Reflect(Reader& r,std::uint32_t data,Reflection& proof){
    Type weapon,state,mesh;Field states,meshes,name;unsigned matches=0;
    if(!Object(r,data,"SoldierWeaponData",weapon)||!FieldNamed(r,weapon,"WeaponStates",states,matches)||matches!=1||!FieldType(r,states,"ArrayBase",0x88)||
       !Info(r,states.words[3],state)||std::strcmp(state.name.data(),"WeaponStateData")||state.size!=0xc8||state.flags!=0x29)return false;
    matches=0;if(!FieldNamed(r,state,"Meshes1p",meshes,matches)||matches!=1||!FieldType(r,meshes,"ArrayBase",0x80)||
       !Info(r,meshes.words[3],mesh)||std::strcmp(mesh.name.data(),"SkinnedMeshAsset")||mesh.size!=0x44||mesh.flags!=0x35)return false;
    proof.weaponInfo=weapon.address;proof.stateInfo=state.address;proof.meshInfo=mesh.address;
    matches=0;auto current=mesh;std::uint16_t size=mesh.size;
    for(unsigned depth=0;depth<proof.inheritance.size();++depth){
        if(current.flags!=0x35||current.size>size||std::find(proof.inheritance.begin(),proof.inheritance.end(),current.address)!=proof.inheritance.end())return false;
        proof.inheritance[depth]=current.address;
        if(!FieldNamed(r,current,"Name",name,matches))return false;
        std::uint32_t parent=0;if(!r.Get(std::uint64_t(current.address)+0x14,parent))return false;
        if(parent==current.address)return matches==1&&FieldType(r,name,"String",0xc);
        size=current.size;if(!Info(r,parent,current))return false;
    }return false;
}
struct OwnerBytes {
    std::uint32_t manager=0,inventory=0,switching=0,begin=0,end=0,slot=0;
    std::uint8_t playerFlags=0,soldierFlags=0;
    std::array<std::uint32_t,64> items{};
    bool operator==(const OwnerBytes&)const=default;
};
bool Owner(Reader& r,const ReloadStateOwner& expected,OwnerBytes& out){
    std::uint32_t table=0,player=0,weak=0,target=0,controller=0,controlled=0;
    if(!r.Get(std::uint64_t(r.binding.context)+8,out.manager)||!r.Get(out.manager,table)||table!=r.binding.managerTable||
       !r.Get(std::uint64_t(out.manager)+0xb4,player)||player!=expected.player||
       !r.Get(std::uint64_t(player)+0xccd,out.playerFlags)||!(out.playerFlags&8)||
       !r.Get(std::uint64_t(player)+0xc54,weak)||weak!=expected.weak||!r.Get(weak,target)||std::uint64_t(expected.soldier)+4!=target||
       !IsObject(r,expected.soldier,"ClientSoldierEntity")||!r.Get(std::uint64_t(expected.soldier)+0x220,controller)||controller!=player||
       !r.Get(std::uint64_t(player)+0xc68,controlled)||controlled!=expected.soldier||
       !r.Get(std::uint64_t(expected.soldier)+0x114,out.soldierFlags)||
       !r.Get(std::uint64_t(expected.soldier)+((out.soldierFlags&1)?0x24c:0x248),out.inventory)||
       !r.Get(std::uint64_t(out.inventory)+4,out.switching)||!IsObject(r,out.switching,"WeaponSwitchingData")||
       !r.Get(std::uint64_t(expected.soldier)+0x260,out.begin)||!r.Get(std::uint64_t(expected.soldier)+0x264,out.end)||
       out.end<=out.begin||out.end-out.begin>256||(out.end-out.begin)%4||
       !r.Get(std::uint64_t(out.inventory)+0x14c,out.slot)||out.slot>=(out.end-out.begin)/4||
       !r.Read(out.begin,out.items.data(),out.end-out.begin)||out.items[out.slot]!=expected.weapon)return false;
    return std::count(out.items.begin(),out.items.end(),expected.weapon)==1;
}
struct Config {
    std::uint32_t data=0,namePointer=0;
    std::uint32_t configurationPathPointer=0;
    std::array<char,128> name{};
    std::array<char,512> configurationPath{};
    std::array<std::uint32_t,4> stateHeader{};
    Reflection reflection{};
    std::array<SelectedMeshState,8> states{};
    std::uint8_t count=0;
    bool operator==(const Config&)const=default;
};
bool ConfigRead(Reader& r,std::uint32_t weapon,Config& out){
    if(!r.Get(std::uint64_t(weapon)+4,out.data)||!Reflect(r,out.data,out.reflection)||
       !r.Get(std::uint64_t(out.data)+0xc,out.namePointer)||!r.Text(out.namePointer,out.name)||
       // Same verified SoldierWeaponData path field used by the established
       // ReadReloadState/ReadConfig and capture_reload_state.py, not a new scan.
       !r.Get(std::uint64_t(out.data)+0x40,out.configurationPathPointer)||
       !r.Read(std::uint64_t(out.data)+0x88,out.stateHeader.data(),16)||out.stateHeader[1]!=out.reflection.stateInfo)return false;
    if(out.configurationPathPointer){
        if(!r.Text(out.configurationPathPointer,out.configurationPath))return false;
        const std::string_view path(out.configurationPath.data());
        if(path.front()=='/'||path.back()=='/'||path.find('/')==std::string_view::npos||
           path.find('\\')!=std::string_view::npos||path.find(':')!=std::string_view::npos)return false;
        for(std::size_t at=0;at<path.size();){const auto end=path.find('/',at);const auto part=path.substr(at,end==std::string_view::npos?path.size()-at:end-at);
            if(part.empty()||part=="."||part=="..")return false;if(end==std::string_view::npos)break;at=end+1;}
    }
    const auto begin=out.stateHeader[2],end=out.stateHeader[3];
    if(begin<0x10000||end<=begin||end-begin>8*0xc8||(end-begin)%0xc8){r.error=SelectedMeshesStatus::ArrayBounds;return false;}
    out.count=std::uint8_t((end-begin)/0xc8);
    for(unsigned i=0;i<out.count;++i){auto& state=out.states[i];state.state=begin+i*0xc8;state.array=state.state+0x80;
        if(!r.Read(state.array,state.header.data(),20))return false;
        const auto table=state.header[0],capacity=state.header[2],count=state.header[3],items=state.header[4];
        std::array<std::uint32_t,3> getters{};
        if(!r.SameFile(table,12)||!r.Read(table,getters.data(),12)||!r.SameFile(getters[0],4,true)||!r.SameFile(getters[2],4,true))return false;
        const auto countOff=Offset(r.binding,getters[0],4,true),dataOff=Offset(r.binding,getters[2],4,true);
        constexpr std::array<std::byte,4> countCode{std::byte{0x8b},std::byte{0x41},std::byte{0x0c},std::byte{0xc3}},dataCode{std::byte{0x8b},std::byte{0x41},std::byte{0x10},std::byte{0xc3}};
        if(!countOff||!dataOff||std::memcmp(r.binding.executable.data()+*countOff,countCode.data(),4)||std::memcmp(r.binding.executable.data()+*dataOff,dataCode.data(),4))return false;
        if(count>8||count>capacity||capacity>1024||(count&&(items<0x10000||std::uint64_t(items)+count*4>UINT32_MAX))){r.error=SelectedMeshesStatus::ArrayBounds;return false;}
        state.count=std::uint8_t(count);std::array<std::uint32_t,8> pointers{};
        if(count&&!r.Read(items,pointers.data(),count*4))return false;
        for(unsigned j=0;j<count;++j){auto& asset=state.meshes[j];Type actual;asset.address=pointers[j];
            if(!Object(r,asset.address,"SkinnedMeshAsset",actual)||actual.address!=out.reflection.meshInfo||
               !r.Get(std::uint64_t(asset.address)+0xc,asset.namePointer)||!r.Text(asset.namePointer,asset.assetPath)||!std::strchr(asset.assetPath.data(),'/'))return false;
            asset.typeInfo=actual.address;asset.kind=ClassifySelectedMeshPath(asset.assetPath.data());
        }
    }return true;
}
}
std::optional<SelectedMeshesBinding> DiscoverSelectedMeshes1p(std::span<const std::byte> bytes,const engine::PeImage& pe){
    if(pe.machine!=0x14c||bytes.size()<64)return {};
    const auto nt=Value<std::uint32_t>(bytes.data()+0x3c);if(std::uint64_t(nt)+56>bytes.size())return {};
    SelectedMeshesBinding b;b.executable=bytes;b.pe=pe;b.preferredBase=Value<std::uint32_t>(bytes.data()+nt+52);
    if(b.preferredBase!=0x400000||!pe.imageSize||std::uint64_t(b.preferredBase)+pe.imageSize>UINT32_MAX)return {};
    for(unsigned i=0;i<4;++i){auto rva=Unique(b,signatures[i]);if(!rva)return {};auto off=Offset(b,b.preferredBase+*rva,sizes[i],true);if(!off)return {};
        b.code[i]={*rva,sizes[i],Hash(bytes.subspan(*off,sizes[i]))};}
    const auto context=Offset(b,b.preferredBase+b.code[0].rva+21,4),manager=Offset(b,b.preferredBase+b.code[1].rva+0x6f,4);
    if(!context||!manager)return {};b.context=Value<std::uint32_t>(bytes.data()+*context);b.managerTable=Value<std::uint32_t>(bytes.data()+*manager);
    bool contextStorage=false;
    for(const auto& section:pe.sections)if(b.context>=b.preferredBase&&b.context-b.preferredBase>=section.rva&&
        std::uint64_t(b.context-b.preferredBase)+12<=std::uint64_t(section.rva)+section.virtualSize&&
        (section.flags&0x80000000)&&!(section.flags&0x20000000))contextStorage=true;
    if(!contextStorage||std::uint64_t(b.context)+12>std::uint64_t(b.preferredBase)+pe.imageSize||!Offset(b,b.managerTable,12))return {};
    b.operationBinding.executableFingerprint=Hash(bytes);b.operationBinding.executableBytes=bytes.size();
    b.operationBinding.imageBase=b.preferredBase;b.operationBinding.imageBytes=pe.imageSize;
    for(unsigned n=0;n<4;++n){b.operationBinding.codeRva[n]=b.code[n].rva;b.operationBinding.codeBytes[n]=b.code[n].size;
        b.operationBinding.codeFingerprint[n]=b.code[n].fingerprint;}
    return b;
}
SelectedMeshesResult ReadSelectedMeshes1p(const ReloadStateMemory& memory,const SelectedMeshesBinding& b,std::uint32_t imageBase,
    const ReloadStateOwner& owner,std::uint64_t sequence,std::int64_t observedNs,std::int64_t deadlineNs,bool enabled)noexcept {
    if(!enabled)return {};
    if(!memory.read||!sequence||!owner.player||!owner.soldier||!owner.weak||!owner.weapon||!owner.actorGeneration||!owner.equipGeneration||!owner.space||
       observedNs<=0||deadlineNs<=observedNs||deadlineNs-observedNs>250000000||imageBase!=b.preferredBase||imageBase!=0x400000||!b.pe.imageSize)return {SelectedMeshesStatus::InvalidArguments};
    Reader r{memory,b};auto fail=[&](SelectedMeshesStatus status){return SelectedMeshesResult{
        r.error==SelectedMeshesStatus::ReadFailure||r.error==SelectedMeshesStatus::ReadBudget?r.error:status,{},r.calls,r.bytes};};
    if(!CompleteOperationBinding(b.operationBinding)||b.operationBinding.imageBase!=imageBase||
       b.operationBinding.imageBytes!=b.pe.imageSize||b.operationBinding.executableBytes!=b.executable.size())return fail(SelectedMeshesStatus::BindingMismatch);
    for(unsigned i=0;i<4;++i){const auto& c=b.code[i];const auto off=Offset(b,imageBase+c.rva,c.size,true);
        if(b.operationBinding.codeRva[i]!=c.rva||b.operationBinding.codeBytes[i]!=c.size||
           b.operationBinding.codeFingerprint[i]!=c.fingerprint)return fail(SelectedMeshesStatus::BindingMismatch);
        if(c.size!=sizes[i]||!off||Hash(b.executable.subspan(*off,c.size))!=c.fingerprint||!r.SameFile(imageBase+c.rva,c.size,true))return fail(SelectedMeshesStatus::BindingMismatch);}
    OwnerBytes initial{},final{};if(!Owner(r,owner,initial))return fail(SelectedMeshesStatus::OwnerMismatch);
    Config first{},second{};if(!ConfigRead(r,owner.weapon,first))return fail(r.error);
    r.error=SelectedMeshesStatus::MalformedMetadata;
    if(!ConfigRead(r,owner.weapon,second)||second!=first)return fail(SelectedMeshesStatus::ChangedDuringRead);
    if(!Owner(r,owner,final)||final!=initial)return fail(SelectedMeshesStatus::ChangedDuringRead);
    SelectedMeshesSnapshot s;s.owner=owner;s.sequence=sequence;s.observedNs=observedNs;s.deadlineNs=deadlineNs;
    s.operationBinding=b.operationBinding;
    s.weaponData=first.data;s.stateTypeInfo=first.reflection.stateInfo;s.meshTypeInfo=first.reflection.meshInfo;
    s.inventory=initial.inventory;s.selectedSlot=initial.slot;s.weaponName=first.name;s.states=first.states;s.stateCount=first.count;
    // Config equality above includes BOTH path pointer and complete text; the
    // complete native owner/inventory is also repeated before publication.
    s.configurationPath=first.configurationPath;s.configurationPathVerified=first.configurationPathPointer!=0;s.configurationPathPointer=first.configurationPathPointer;
    if(s.stateCount==1)s.soleConfiguredArray=s.states[0].array;
    return {SelectedMeshesStatus::Observed,s,r.calls,r.bytes};
}
CarriedMeshesResult ReadCarriedMeshes1p(const ReloadStateMemory& memory,const SelectedMeshesBinding& b,std::uint32_t imageBase,
    const ReloadStateOwner& owner,std::uint32_t inventory,std::uint32_t slot,std::uint32_t weapon,std::uint32_t data,
    std::uint32_t persistence,std::uint64_t sequence,std::int64_t observed,std::int64_t deadline,bool enabled)noexcept {
    if(!enabled)return {};
    if(!memory.read||!sequence||!owner.player||!owner.soldier||!owner.weak||!owner.weapon||!owner.actorGeneration||!owner.equipGeneration||!owner.space||
       weapon<0x10000||weapon==owner.weapon||data<0x10000||slot>=32||!inventory||observed<=0||deadline<=observed||deadline-observed>200000000||
       imageBase!=b.preferredBase||imageBase!=0x400000||!b.pe.imageSize)return {SelectedMeshesStatus::InvalidArguments};
    Reader r{memory,b};
    for(unsigned i=0;i<4;++i){const auto& c=b.code[i];const auto off=Offset(b,imageBase+c.rva,c.size,true);
        if(c.size!=sizes[i]||!off||Hash(b.executable.subspan(*off,c.size))!=c.fingerprint||!r.SameFile(imageBase+c.rva,c.size,true))return {SelectedMeshesStatus::BindingMismatch};}
    OwnerBytes before{},after{};
    const auto member=[&](const OwnerBytes& o){return o.inventory==inventory&&slot<(o.end-o.begin)/4&&o.items[slot]==weapon&&
        std::count(o.items.begin(),o.items.end(),weapon)==1;};
    if(!Owner(r,owner,before)||!member(before))return {SelectedMeshesStatus::OwnerMismatch};
    Config a{},c{};unsigned p1=0,p2=0;
    if(!ConfigRead(r,weapon,a)||a.data!=data||!r.Get(std::uint64_t(data)+0x64,p1)||p1!=persistence)return {r.error};
    if(!ConfigRead(r,weapon,c)||c!=a||!r.Get(std::uint64_t(data)+0x64,p2)||p2!=p1||
       !Owner(r,owner,after)||after!=before||!member(after))return {SelectedMeshesStatus::ChangedDuringRead};
    CarriedMeshesSnapshot out;out.weapon=weapon;out.nativeSlot=slot;out.persistence=persistence;
    auto& s=out.configured;s.owner=owner;s.sequence=sequence;s.observedNs=observed;s.deadlineNs=deadline;
    s.weaponData=a.data;s.stateTypeInfo=a.reflection.stateInfo;s.meshTypeInfo=a.reflection.meshInfo;
    s.inventory=before.inventory;s.selectedSlot=before.slot;s.weaponName=a.name;s.states=a.states;s.stateCount=a.count;
    s.configurationPath=a.configurationPath;s.configurationPathVerified=a.configurationPathPointer!=0;
    s.configurationPathPointer=a.configurationPathPointer;
    if(s.stateCount==1)s.soleConfiguredArray=s.states[0].array;
    return {SelectedMeshesStatus::Observed,out};
}
const SelectedMeshAsset* FindSelectedMesh(const SelectedMeshesSnapshot& s,const ReloadStateOwner& owner,SelectedMeshKind wanted,std::int64_t nowNs)noexcept {
    if(s.owner!=owner||nowNs<s.observedNs||nowNs>=s.deadlineNs||s.stateCount!=1||!s.soleConfiguredArray||s.states[0].count>8||wanted==SelectedMeshKind::Unknown)return nullptr;
    const SelectedMeshAsset* found=nullptr;
    for(unsigned i=0;i<s.states[0].count;++i)if(s.states[0].meshes[i].kind==wanted){if(found)return nullptr;found=&s.states[0].meshes[i];}
    return found;
}
}
