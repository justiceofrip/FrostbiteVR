#pragma once
#include "Bc2WeaponMode.h"
#include <array>
#include <algorithm>
#include <optional>
#include <string_view>
namespace fvr::bc2 {
// Values already read by the asset/profile and carried-inventory bindings.
// A weapon object can be reused in place by ordinary native ground pickup.
struct WeaponEquipmentIdentity {
    unsigned weapon=0,data=0,persistence=0;
    std::array<char,64> asset{};
    bool operator==(const WeaponEquipmentIdentity&)const=default;
    std::string_view Asset()const noexcept {
        const auto end=std::find(asset.begin(),asset.end(),'\0');
        return end==asset.end()?std::string_view{}:std::string_view(asset.data(),std::size_t(end-asset.begin()));
    }
};
inline std::optional<WeaponEquipmentIdentity> ReadWeaponEquipmentIdentity(const WeaponModeMemory& m,unsigned weapon)noexcept {
    if(!m.read||!m.type||weapon<0x10000||weapon>UINT32_MAX-8)return {};
    const auto read=[&](unsigned at,void* out,std::size_t n){return at>=0x10000&&std::uint64_t(at)+n<=UINT32_MAX&&m.read(m.context,at,out,n);};
    const auto sample=[&]()->std::optional<WeaponEquipmentIdentity>{
        WeaponEquipmentIdentity s;s.weapon=weapon;unsigned name=0;
        if(!read(weapon+4,&s.data,4)||s.data<0x10000||s.data>UINT32_MAX-0x68||!m.type(m.context,s.data,"SoldierWeaponData")||
           !read(s.data+0x64,&s.persistence,4)||!read(s.data+0xc,&name,4)||!read(name,s.asset.data(),s.asset.size()))return {};
        const auto end=std::find(s.asset.begin(),s.asset.end(),'\0');if(end==s.asset.begin()||end==s.asset.end())return {};
        for(auto it=s.asset.begin();it!=end;++it)if(static_cast<unsigned char>(*it)<32||static_cast<unsigned char>(*it)>126)return {};
        std::fill(end,s.asset.end(),'\0');return s;
    };
    const auto a=sample(),b=sample();if(!a||!b||*a!=*b)return {};return a;
}
inline bool WeaponEquipmentStillCurrent(const WeaponModeMemory& memory,const WeaponEquipmentIdentity& expected)noexcept {
    const auto current=ReadWeaponEquipmentIdentity(memory,expected.weapon);
    return current&&*current==expected;
}
}
