#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace fvr::bc2::provisioning_candidate {
// Pure reader for the supported x86 image. No process access, native invocation,
// setting mutation, selection permission, ownership lease or runtime admission.
struct Memory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
    bool (*type)(void*,std::uint32_t,const char*)=nullptr;
};
struct CodeSlice {std::uint32_t address;std::span<const unsigned char> bytes;};
inline constexpr unsigned char ItemBytes[]{0x8b,0x44,0x24,0x04,0x8b,0x49,0x08,0xc1,0xe0,0x05,0x8b,0x04,0x08,0xc2,0x04,0x00};
inline constexpr unsigned char CountBytes[]{0x8b,0x41,0x0c,0x2b,0x41,0x08,0xc1,0xf8,0x05,0xc3};
inline constexpr unsigned char LinkedBytes[]{0x8b,0x44,0x24,0x04,0x8b,0x49,0x08,0xc1,0xe0,0x05,0x33,0xd2,0x83,0x7c,0x08,0x0c,0xff,0x0f,0x95,0xc2,0x8a,0xc2,0xc2,0x04,0x00};
inline constexpr unsigned char TypeBytes[]{0xb8,0x88,0x4c,0xbf,0x01,0xc3};
inline constexpr unsigned char UiStoreBytes[]{0x89,0xb7,0x20,0x2b,0x08,0x00};
inline constexpr unsigned char OwnerLoadBytes[]{0x8b,0x81,0xc8,0x73,0x02,0x00};
inline constexpr unsigned char UiPrimaryBytes[]{0xc7,0x06,0x30,0x08,0x44,0x01};
inline constexpr unsigned char PickupPrimaryBytes[]{0xc7,0x06,0x40,0x60,0x43,0x01};
inline constexpr std::array<CodeSlice,8> Code{{
    {0x61b350,ItemBytes},{0x61b360,CountBytes},{0x61b370,LinkedBytes},
    {0x877d60,TypeBytes},{0x8fec48,UiStoreBytes},{0x747610,OwnerLoadBytes},
    {0x955a18,UiPrimaryBytes},{0x89dac1,PickupPrimaryBytes}}};
struct Row {
    std::uint32_t index=0,weaponData=0,persistence=0;
    std::int32_t linkedSlot=-1;
    std::array<char,64> name{};
    std::array<char,192> path{};
    bool operator==(const Row&)const=default;
};
struct Catalog {
    std::uint32_t ui=0,inGame=0,weak=0,pickup=0,begin=0,end=0,count=0;
    std::array<Row,128> rows{};
    bool operator==(const Catalog&)const=default;
};
namespace detail {
inline bool Read(const Memory& m,std::uint64_t at,void* output,std::size_t size)noexcept {
    return m.read&&at>=0x10000&&size&&size<=4096&&at+size<=UINT32_MAX&&
        m.read(m.context,std::uint32_t(at),output,size);
}
template<class T>bool Get(const Memory& m,std::uint64_t at,T& value)noexcept{return Read(m,at,&value,sizeof(value));}
template<std::size_t N>bool Text(const Memory& m,std::uint32_t at,std::array<char,N>& text)noexcept {
    for(std::size_t n=0;n<N;++n){
        unsigned char c=0;if(!Get(m,std::uint64_t(at)+n,c))return false;
        if(!c)return n>0;
        if(c<32||c>126)return false;text[n]=char(c);
    }return false;
}
inline bool CodeVerified(const Memory& m)noexcept {
    std::array<unsigned char,32> bytes{};
    for(const auto& c:Code)if(!Read(m,c.address,bytes.data(),c.bytes.size())||
        std::memcmp(bytes.data(),c.bytes.data(),c.bytes.size()))return false;
    return true;
}
inline bool ReadOnce(const Memory& m,Catalog& out)noexcept {
    std::uint32_t table=0,back=0,target=0,getType=0;
    // Constructor stores 14407f0 only at the secondary interface +82afc.
    if(!m.type||!Get(m,0x19a4ca8,out.ui)||!Get(m,out.ui,table)||table!=0x1440830||
        !Get(m,std::uint64_t(out.ui)+0x82b20,out.inGame)||!Get(m,out.inGame,table)||table!=0x142b7a0||
        !Get(m,std::uint64_t(out.inGame)+0x27284,back)||back!=out.inGame||
        !Get(m,std::uint64_t(out.inGame)+0x273c8,out.weak)||!Get(m,out.weak,target)||target<0x10004)return false;
    out.pickup=target-4;
    if(!Get(m,out.pickup,table)||table!=0x1436040||!Get(m,std::uint64_t(table)+8,getType)||getType!=0x877d60||
        !m.type(m.context,out.pickup,"ClientPickupEntity")||
        !Get(m,std::uint64_t(out.pickup)+0x34,out.begin)||!Get(m,std::uint64_t(out.pickup)+0x38,out.end)||
        out.begin<0x10000||out.end<=out.begin||out.end-out.begin>128*32||(out.end-out.begin)%32)return false;
    out.count=(out.end-out.begin)/32;
    for(std::uint32_t n=0;n<out.count;++n){
        Row& row=out.rows[n];row.index=n;std::uint32_t name=0,path=0;
        const std::uint64_t at=std::uint64_t(out.begin)+n*32;
        if(!Get(m,at,row.weaponData)||!Get(m,at+12,row.linkedSlot)||row.linkedSlot< -1||row.linkedSlot>=128||
            !m.type(m.context,row.weaponData,"SoldierWeaponData")||
            !Get(m,std::uint64_t(row.weaponData)+0xc,name)||!Get(m,std::uint64_t(row.weaponData)+0x40,path)||
            !Get(m,std::uint64_t(row.weaponData)+0x64,row.persistence)||
            !m.type(m.context,row.persistence,"PersistentWeapon")||!Text(m,name,row.name)||!Text(m,path,row.path))return false;
    }return true;
}
}
inline std::optional<Catalog> ReadFlynnCatalog(const Memory& memory)noexcept {
    Catalog first{},second{};
    if(!detail::CodeVerified(memory)||!detail::ReadOnce(memory,first)||
        !detail::ReadOnce(memory,second)||first!=second||!detail::CodeVerified(memory))return {};
    return first;
}
} // namespace fvr::bc2::provisioning_candidate
