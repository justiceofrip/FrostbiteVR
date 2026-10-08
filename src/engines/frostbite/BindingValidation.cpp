#include "fvr/engine/BindingValidation.h"
#include "fvr/engine/ModuleApi.h"
#include <cctype>
namespace fvr::engine {
namespace {int Hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}}
std::optional<std::vector<PatternByte>> ParsePattern(std::string_view text){
    std::vector<PatternByte> out;
    std::size_t pos=0;bool concrete=false;
    while(pos<text.size()){
        while(pos<text.size()&&std::isspace(static_cast<unsigned char>(text[pos])))++pos;
        if(pos==text.size())break;
        auto end=pos;while(end<text.size()&&!std::isspace(static_cast<unsigned char>(text[end])))++end;
        const auto token=text.substr(pos,end-pos);
        if(token=="?"||token=="??")out.push_back({0,true});
        else {if(token.size()!=2||Hex(token[0])<0||Hex(token[1])<0)return {};out.push_back({static_cast<std::uint8_t>(Hex(token[0])*16+Hex(token[1])),false});concrete=true;}
        if(out.size()>4096)return {};pos=end;
    }
    if(out.empty()||!concrete)return {};return out;
}
std::optional<std::size_t> UniqueMatch(std::span<const std::byte> bytes,std::span<const PatternByte> pattern) noexcept {
    if(pattern.empty()||pattern.size()>bytes.size())return {};
    bool concrete=false;for(auto b:pattern)concrete|=!b.wildcard;if(!concrete)return {};
    std::optional<std::size_t> found;
    for(std::size_t i=0;i<=bytes.size()-pattern.size();++i){bool match=true;
        for(std::size_t j=0;j<pattern.size();++j)if(!pattern[j].wildcard&&std::to_integer<unsigned char>(bytes[i+j])!=pattern[j].value){match=false;break;}
        if(match){if(found)return {};found=i;}
    }return found;
}
std::uint64_t FrostbiteEvidence::Capabilities() const noexcept {
    std::uint64_t result=0;
    const bool stereo=coordinates&&simulationOnce&&gpuOwnership&&camera.Verified()&&renderOnly.Verified()&&stateRestore.Verified()&&eyeTargets.Verified();
    if(stereo)result|=FVR_CAP_STEREO;
    const bool owns=localOwner.Verified();
    if(stereo&&owns&&input.Verified())result|=FVR_CAP_INPUT;
    if(stereo&&owns&&weapon.Verified()&&input.Verified())result|=FVR_CAP_WEAPON_AIM;
    if((result&FVR_CAP_WEAPON_AIM)&&skeleton.Verified())result|=FVR_CAP_HANDS;
    if(stereo&&ui.Verified())result|=FVR_CAP_UI;
    if((result&FVR_CAP_INPUT)&&vehicle.Verified())result|=FVR_CAP_VEHICLES;
    return result;
}
}