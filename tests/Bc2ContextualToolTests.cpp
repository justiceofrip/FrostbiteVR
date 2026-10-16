#include "Test.h"
#include "Bc2ContextualTool.h"
#include <fstream>
#include <iterator>
using namespace fvr;
#include "ContextualToolFixture.h"
namespace {
struct Tools:Fixture {
    static constexpr unsigned tool=0x13400,toolData=0x14800,toolFiring=0x15000,primaryFiring=0x15200;
    bc2::ContextualToolBinding binding{0x20000,0x30000,true};
    Tools(){
        Word(array+16,tool);Word(tool+4,toolData);Word(toolData,table);Word(toolData+0x84,12);types[toolData]="SoldierWeaponData";
        Byte(meta+13,3);Word(fields+24,strings+704);Text(strings+704,"WeaponAssetName");Word(fields+24+16,0x40);
        Word(fields+48,strings+768);Text(strings+768,"WeaponFiring");Word(fields+48+16,0x98);
        Byte(enumMeta+13,7);Word(enumFields+6*24,strings+512);Text(strings+512,"wcLaserDesignator");Word(enumFields+6*24+16,12);
        Word(toolData+0x40,0x3e000);Text(0x3e000,"Objects/Weapons/Handheld/LaserDesignator/SP_LaserDesignator_Dummy");
        Word(ad+0x40,0x3e100);Text(0x3e100,"Objects/Weapons/Handheld/Example/CarriedPrimary");
        Word(toolData+0x98,toolFiring);Word(ad+0x98,primaryFiring);
        for(unsigned data:{toolFiring,primaryFiring}){Word(data,0x30000);types[data]="WeaponFiringData";}
        Word(0x30008,0x30100);Byte(0x30100,0xb8);Word(0x30101,0x30200);Byte(0x30105,0xc3);
        Word(0x30204,0x30300);Word(0x30300,0x30700);Text(0x30700,"WeaponFiringData");Word(0x30304,0x50u<<16|0x35);
        Byte(0x3030d,1);Word(0x30224,0x30400);Word(0x30400,0x30740);Text(0x30740,"FireOnDeploy");Word(0x30408,0x30500);Word(0x30410,0x48);
        Word(0x30504,0x30600);Word(0x30600,0x30780);Text(0x30780,"Boolean");Word(0x30604,1u<<16|1);
        Word(map+16,34);Word(map+512,4);Word(map+24+20,4);Word(map+520,9);
        Word(inventory+0x154,0);
    }
    void SelectTool(){Word(inventory+0x14c,4);owner.weapon=tool;++owner.equipGeneration;}
    auto Read(){return bc2::ReadContextualTool(Memory(),binding,owner);}
};
int Inventory(){
    Tools f;auto o=f.Read();CHECK(o);CHECK(!o->tool.item.persistence);CHECK(o->tool.item.category==12);
    const auto equip=bc2::ResolveStaticToolEquip(*o);CHECK(equip);CHECK(equip->action==34&&equip->targetWeapon==Tools::tool&&equip->targetSlot==4);
    const auto original=o->inventory.items[0];CHECK(!bc2::ResolveStaticToolReturn(*o,original));
    f.SelectTool();o=f.Read();CHECK(o);CHECK(!bc2::ResolveStaticToolEquip(*o));
    const auto ret=bc2::ResolveStaticToolReturn(*o,original);CHECK(ret&&ret->action==7&&ret->targetWeapon==original.weapon);
    auto changed=original;changed.data+=4;CHECK(!bc2::ResolveStaticToolReturn(*o,changed));
    std::puts("typed tool/null persistence/static equip/native last-primary return: PASS");return 0;
}
int Rejections(){
    for(unsigned n=0;n<12;++n){Tools f;
        if(n==0)f.binding.verified=false;
        if(n==1)f.Word(Tools::toolData+0x84,0);
        if(n==2)f.Word(Fixture::enumFields+6*24+16,10);
        if(n==3)f.Word(Fixture::fields+24+16,0x44);
        if(n==4)f.Byte(Tools::toolFiring+0x48,1);
        if(n==5)f.Byte(Tools::primaryFiring+0x48,1);
        if(n==6)f.Word(Fixture::inventory+0x154,4);
        if(n==7)f.Word(Fixture::player+0xc60,0x10100);
        if(n==8)f.Word(Tools::tool+4,Fixture::ad);
        if(n==9)f.Text(0x30780,"UInt8");
        if(n==10)f.Word(Fixture::fields+48+16,0x9c);
        if(n==11)f.binding.imageSize=0x100;
        CHECK(!f.Read());
    }
    Tools f;auto o=f.Read();CHECK(o);
    o->inventory.routes[0].targets[0]=1;CHECK(!bc2::ResolveStaticToolEquip(*o));
    o=f.Read();CHECK(o);o->inventory.routes[1]=o->inventory.routes[0];CHECK(!bc2::ResolveStaticToolEquip(*o));
    f.SelectTool();o=f.Read();CHECK(o);auto original=o->inventory.items[0];o->inventory.routes[1].targets[0]=10;
    CHECK(!bc2::ResolveStaticToolReturn(*o,original));
    std::puts("wrong owner/category/reflection/deploy/route/return: 15 variants PASS");return 0;
}
int Coherence(){
    Tools f;struct Mutator {bc2::WeaponModeMemory original;unsigned reads=0;} state{f.Memory()};
    bc2::WeaponModeMemory memory{&state,[](void* c,unsigned at,void* p,std::size_t n){auto& s=*static_cast<Mutator*>(c);
        if(!s.original.read(s.original.context,at,p,n))return false;
        if(at==Tools::toolData+0x40&&++s.reads==2){const unsigned other=0x3e100;std::memcpy(p,&other,4);}return true;},
        [](void* c,unsigned at,const char* type){auto& s=*static_cast<Mutator*>(c);return s.original.type(s.original.context,at,type);}};
    CHECK(!bc2::ReadContextualTool(memory,f.binding,f.owner));CHECK(state.reads==2);
    std::puts("same pointer/changed native asset rejects coherent observation: PASS");return 0;
}
struct Secondary {
    Tools f;bc2::ContextualToolObservation o;interaction::InputFrame in;
    interaction::HandInteractionSample s;interaction::HandInteraction hands;interaction::HandClaimToken hold;
    bc2::ContextualToolSecondary policy;
    Secondary(){
        f.SelectTool();o=*f.Read();in.generation=1;in.spaceGeneration=9;in.predictedNs=1000000000;in.headValid=in.focused=true;
        for(auto& h:in.hands){h.active=interaction::Trigger;h.gripTracked=h.aimTracked=true;}
        s={{(std::uint64_t(Fixture::weak)<<32)|Fixture::soldier,4,99,9},1,1000000000,1100000000,1000000000,true,{true,true}};
        hands.Update(s);auto result=hands.Acquire(s,{s.owner,interaction::InteractionHand::Right,interaction::HandClaimKind::GunHold,
            {Tools::tool,1},{{Tools::tool,1},s.sequence,s.deadlineNs,true},1,0});hold=result.claim->token;
    }
    void Step(float trigger){++s.sequence;s.observedNs+=10000000;s.deadlineNs+=10000000;s.nowNs=s.observedNs;
        in.generation=s.sequence;in.predictedNs=s.observedNs;in.hands[0].trigger=trigger;
        hands.Update(s);hands.Renew(s,hold,{hold.contact,s.sequence,s.deadlineNs,true});}
    bool Tick(){return policy.Update(o,in,s,hold,hands);}
};
int FreshSecondary(){
    Secondary r;r.in.hands[0].trigger=1;CHECK(!r.Tick());r.Step(0);CHECK(!r.Tick());r.Step(1);CHECK(r.Tick());
    CHECK(r.Tick());const auto deadline=r.hands.Current(interaction::InteractionHand::Right)->deadlineNs;
    CHECK(deadline==r.s.deadlineNs);r.s.nowNs=deadline;CHECK(!r.Tick());
    Secondary duplicate;CHECK(!duplicate.Tick());duplicate.in.hands[0].trigger=1;CHECK(!duplicate.Tick());
    duplicate.Step(1);CHECK(!duplicate.Tick());duplicate.Step(0);CHECK(!duplicate.Tick());duplicate.Step(1);CHECK(duplicate.Tick());
    Secondary inherited;CHECK(!inherited.Tick());inherited.Step(1);CHECK(!inherited.Tick());
    inherited.Step(0);CHECK(!inherited.Tick());inherited.Step(1);CHECK(inherited.Tick());
    for(unsigned n=0;n<8;++n){Secondary x;CHECK(!x.Tick());x.Step(0);CHECK(!x.Tick());x.Step(1);CHECK(x.Tick());
        if(n==0)x.in.focused=false;
        if(n==1)x.s.deadlineNs=x.s.nowNs;
        if(n==2)x.o.inventory.owner.weapon=Fixture::a;
        if(n==3)x.o.tool.item.category=0;
        if(n==4)++x.o.inventory.owner.equipGeneration;
        if(n==5)++x.s.owner.space;
        if(n==6)x.hands.Release(x.s,x.hold);
        if(n==7)++x.s.deadlineNs; // duplicate source cannot renew its lease
        CHECK(!x.Tick());
    }
    std::puts("fresh neutral/tool-only secondary/exact custody/duplicate expiry: PASS");return 0;
}
int NativeBinding(const char* path){
    struct Image {
        std::vector<char> raw;unsigned mutation=0;
        unsigned Word(std::size_t at)const{unsigned value=0;if(at+4<=raw.size())std::memcpy(&value,raw.data()+at,4);return value;}
        bool Read(unsigned address,void* output,std::size_t size)const {
            if(address<0x400000||raw.size()<0x100)return false;const auto rva=address-0x400000;
            const auto nt=Word(0x3c);if(std::uint64_t(nt)+24>raw.size())return false;
            const auto count=Word(nt+4)>>16,optional=Word(nt+20)&65535;const auto first=nt+24+optional;
            if(count>32||std::uint64_t(first)+count*40>raw.size())return false;
            for(unsigned n=0;n<count;++n){const auto at=first+n*40,start=Word(at+12),bytes=Word(at+16),offset=Word(at+20);
                if(rva<start||std::uint64_t(rva)-start+size>bytes)continue;
                const auto file=std::uint64_t(offset)+rva-start;if(file+size>raw.size())return false;
                std::memcpy(output,raw.data()+file,size);
                if(mutation>=address&&std::uint64_t(mutation)-address<size)static_cast<unsigned char*>(output)[mutation-address]^=1;
                return true;
            }return false;
        }
    } image;
    std::ifstream file(path,std::ios::binary);CHECK(file.good());image.raw={std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    bc2::WeaponModeMemory memory{&image,[](void* p,unsigned at,void* out,std::size_t n){return static_cast<Image*>(p)->Read(at,out,n);},nullptr};
    CHECK(bc2::VerifyContextualToolBinding(memory,0x400000,0x1913000));
    for(unsigned at:{0x6ef590u,0x1c27c58u+14*24+16,0x1c27c58u+34*24+16}){image.mutation=at;CHECK(!bc2::VerifyContextualToolBinding(memory,0x400000,0x1913000));}
    image.mutation=0;CHECK(!bc2::VerifyContextualToolBinding(memory,0x400000,0x1912fff));
    std::puts("installed exact native selector/entry actions and four altered binding rejections: PASS");return 0;
}
}
int main(int argc,char** argv){CHECK(argc==1||argc==2);if(Inventory()||Rejections()||Coherence()||FreshSecondary()||(argc==2&&NativeBinding(argv[1])))return 1;std::puts(argc==2?"Contextual tool evidence slice: 5 focused groups PASS":"Contextual tool admission: 4 focused groups PASS (installed native binding omitted)");}
