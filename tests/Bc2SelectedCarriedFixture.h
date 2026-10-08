#pragma once
#include "Bc2BodyInventory.h"
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
namespace fvr::test {
struct Fixture {
    std::vector<std::byte> bytes=std::vector<std::byte>(0x40000);
    std::unordered_map<unsigned,std::string> types;
    static constexpr unsigned soldier=0x10000,inventory=0x11000,array=0x12000,a=0x13000,b=0x13200,
        ad=0x14000,bd=0x14400,player=0x16000,weak=0x17000,switching=0x18000,map=0x19000,
        table=0x20000,getter=0x21000,info=0x22000,meta=0x23000,fields=0x24000,
        enumInfo=0x25000,enumMeta=0x26000,enumFields=0x27000,strings=0x28000;
    unsigned currentReads=0,categoryReads=0,enumReads=0,maxRead=0;bool mutate=false,mutateCategory=false,mutateEnum=false,denyMap=false;
    bc2::ReloadStateOwner owner{player,soldier,weak,a,4,7,9};
    void Word(unsigned at,unsigned value){std::memcpy(bytes.data()+at-0x10000,&value,4);}
    void Byte(unsigned at,unsigned char value){bytes[at-0x10000]=std::byte(value);}
    void Short(unsigned at,unsigned short value){std::memcpy(bytes.data()+at-0x10000,&value,2);}
    void Text(unsigned at,const char* value){std::memcpy(bytes.data()+at-0x10000,value,std::strlen(value)+1);}
    Fixture(){
        types={{soldier,"ClientSoldierEntity"},{ad,"SoldierWeaponData"},{bd,"SoldierWeaponData"},{switching,"WeaponSwitchingData"}};
        Word(player+0xc54,weak);Word(weak,soldier+4);Word(soldier+0x220,player);Word(player+0xc68,soldier);Byte(player+0xccd,8);
        Word(soldier+0x248,inventory);Word(soldier+0x260,array);Word(soldier+0x264,array+9*4);
        Word(inventory+4,switching);Word(inventory+0x14c,0);Word(array,a);Word(array+4,b);
        Word(a+4,ad);Word(b+4,bd);Word(ad+0x84,0);Word(bd+0x84,1);
        for(unsigned data:{ad,bd})Word(data,table);Word(table+8,getter);Byte(getter,0xb8);Word(getter+1,info);Byte(getter+5,0xc3);
        Word(info+4,meta);Word(meta,strings);Text(strings,"SoldierWeaponData");Short(meta+4,0x35);Short(meta+6,0x130);Byte(meta+13,1);Word(info+36,fields);
        Word(fields,strings+64);Text(strings+64,"WeaponClass");Word(fields+8,enumInfo);Word(fields+16,0x84);
        Word(enumInfo+4,enumMeta);Word(enumMeta,strings+128);Text(strings+128,"WeaponClassEnum");Short(enumMeta+4,0x179);Short(enumMeta+6,4);Byte(enumMeta+13,6);Word(enumMeta+24,enumFields);
        const char* names[]={"wcAssault","wcShotgun","wcSmg","wcLmg","wcSniper","wcUgl"};
        for(unsigned n=0;n<6;++n){Word(enumFields+n*24,strings+256+n*32);Text(strings+256+n*32,names[n]);Word(enumFields+n*24+16,n);}
        Word(switching+0x18,map);Word(switching+0x1c,map+48);
        Word(map+8,map+512);Word(map+12,map+516);Word(map+16,7);Word(map+20,0);Word(map+512,1);
        Word(map+24+8,map+520);Word(map+24+12,map+524);Word(map+24+16,7);Word(map+24+20,1);Word(map+520,0);
    }
    bc2::WeaponModeMemory Memory(){return {this,[](void* c,unsigned at,void* out,std::size_t n){auto& f=*static_cast<Fixture*>(c);
        if(at<0x10000||std::uint64_t(at)+n>0x50000)return false;
        f.maxRead=std::max(f.maxRead,unsigned(n));if(f.denyMap&&at==map)return false;
        std::memcpy(out,f.bytes.data()+at-0x10000,n);
        const auto contains=[&](unsigned address){return at<=address&&std::uint64_t(at)+n>=std::uint64_t(address)+4;};
        const auto change=[&](unsigned address,unsigned value){std::memcpy(static_cast<std::byte*>(out)+address-at,&value,4);};
        if(contains(inventory+0x14c)&&f.mutate&&++f.currentReads>1)change(inventory+0x14c,1);
        if(contains(bd+0x84)&&f.mutateCategory&&++f.categoryReads>1)change(bd+0x84,4);
        if(contains(enumFields+24+16)&&f.mutateEnum&&++f.enumReads>1)change(enumFields+24+16,5);
        return true;},[](void* c,unsigned at,const char* name){auto& f=*static_cast<Fixture*>(c);auto i=f.types.find(at);return i!=f.types.end()&&i->second==name;}};}
    void Select(unsigned slot){Word(inventory+0x14c,slot);owner.weapon=slot?b:a;++owner.equipGeneration;}
};
}
