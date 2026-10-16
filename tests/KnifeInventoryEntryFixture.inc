#include "Test.h"
#include "Bc2BodyInventory.h"
#include "Bc2BodyHolster.h"
#include "fvr/interaction/ControllerInput.h"
#include "Bc2InputBinding.h"
#include <array>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <functional>
using namespace fvr;
namespace {
struct Fixture {
    std::vector<std::byte> bytes=std::vector<std::byte>(0x40000);
    std::unordered_map<unsigned,std::string> types;
    static constexpr unsigned soldier=0x10000,inventory=0x11000,array=0x12000,a=0x13000,b=0x13200,
        ad=0x14000,bd=0x14400,player=0x16000,weak=0x17000,switching=0x18000,map=0x19000,
        table=0x20000,getter=0x21000,info=0x22000,meta=0x23000,fields=0x24000,
        enumInfo=0x25000,enumMeta=0x26000,enumFields=0x27000,strings=0x28000;
    std::function<void()> onRead;
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
        Word(ad+0xc,0x2b000);Text(0x2b000,"XM8_sp_s");Word(bd+0xc,0x2b100);Text(0x2b100,"SPAS12_sp");
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
        if(f.onRead){auto callback=std::move(f.onRead);f.onRead={};callback();}
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
struct Run {
    Fixture f;bc2::Bc2BodyInventory adapter{true};interaction::HandInteraction hands;
    bc2::BodyDrawSample s;std::uint64_t intent=0;
    Run(){s.owner=f.owner;s.input.generation=1;s.input.spaceGeneration=9;s.input.predictedNs=1000000000;s.input.focused=s.input.headValid=true;
        s.input.head.position={0,1.7f,0};s.input.referenceHead=s.input.head;
        s.input.hands[1].active=interaction::Squeeze;s.input.hands[1].gripTracked=s.input.hands[1].aimTracked=true;
        // Right shoulder is behind (+Z OpenXR, -Z canonical).
        s.input.hands[1].grip.position={.20f,1.58f,.18f};s.input.hands[1].aim=s.input.hands[1].grip;
        s.hand={{(std::uint64_t(Fixture::weak)<<32)|Fixture::soldier,4,99,9},1,1000000000,1100000000,1000000000,true,{true,true}};
        s.gun={Fixture::a,99};Visible();Gun();}
    void Visible(){s.visible=bc2::BodyVisibleRig{s.owner,s.input.generation,s.hand.observedNs,s.hand.deadlineNs};}
    void Gun(){hands.Update(s.hand);if(const auto claim=hands.Current(interaction::InteractionHand::Right))hands.Release(s.hand,claim->token);
        hands.Acquire(s.hand,{s.hand.owner,interaction::InteractionHand::Right,interaction::HandClaimKind::GunHold,s.gun,{{1,s.hand.owner.equipGeneration},s.hand.sequence,s.hand.deadlineNs,true},++intent,0});}
    void Advance(float grip=0){++s.input.generation;s.input.predictedNs+=10000000;s.input.hands[1].squeeze=grip;s.hand.sequence=s.input.generation;s.hand.observedNs+=10000000;s.hand.deadlineNs+=10000000;s.hand.nowNs=s.hand.observedNs;Visible();
        hands.Update(s.hand);const auto gun=hands.Current(interaction::InteractionHand::Right);if(gun)hands.Renew(s.hand,gun->token,{gun->token.contact,s.hand.sequence,s.hand.deadlineNs,true});}
    bc2::BodyDrawResult Tick(){return adapter.Tick(f.Memory(),s,hands,intent);}
};
struct HolsterRun {
    Run r;bc2::Bc2BodyHolster holster;bc2::BodyHolsterResult out;bc2::BodyHolsterSample last;bool scopedXm8=false;unsigned invalidMetadata=0;
    std::array<std::byte,bc2::InputBytes> cache{};std::uint64_t nativeTick=0;bool allowReadback=true;unsigned suppressions=0;
    explicit HolsterRun(bc2::BodyHolsterCapabilities acceptance={true,true,1},bool xm8=false):holster(acceptance),scopedXm8(xm8){
        if(xm8){r.s.gun.id=Fixture::a+0x80;r.Gun();} // Actual rifle uses a persistent physical key, not its native pointer.
        r.s.input.hands[1].grip.position.x=-.20f;Tick();r.Advance();Tick();}
    bc2::BodyHolsterSample Sample(){bc2::BodyHolsterSample s;s.nativeOwner=r.s.owner;s.hand=r.s.hand;s.gun=r.s.gun;s.cache=0x49000;s.nativeTick=++nativeTick;
        auto m=std::make_shared<bc2::SelectedMeshesSnapshot>();m->owner=r.s.owner;m->sequence=r.s.input.generation;m->observedNs=r.s.hand.observedNs;m->deadlineNs=r.s.hand.deadlineNs;
        m->weaponData=Fixture::ad;m->stateCount=1;m->soleConfiguredArray=0x45000;m->states[0].array=m->soleConfiguredArray;m->states[0].count=1;
        constexpr char asset[]="SPAS12_sp",path[]="Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh";
        std::memcpy(m->weaponName.data(),asset,sizeof(asset));auto& mesh=m->states[0].meshes[0];mesh.address=0x46000;mesh.namePointer=0x47000;mesh.typeInfo=0x48000;mesh.kind=bc2::SelectedMeshKind::Spas12;std::memcpy(mesh.assetPath.data(),path,sizeof(path));s.selected=m;
        if(scopedXm8){
            std::memcpy(m->weaponName.data(),"XM8_sp_s",sizeof("XM8_sp_s"));m->states[0].count=2;
            mesh.kind=bc2::SelectedMeshKind::Xm8;
            constexpr char riflePath[]="Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh";
            std::memcpy(mesh.assetPath.data(),riflePath,sizeof(riflePath));
            auto& optic=m->states[0].meshes[1];optic.kind=bc2::SelectedMeshKind::Acog4x;
            optic.address=0x46100;optic.namePointer=0x47100;optic.typeInfo=0x48100;
            constexpr char opticPath[]="Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh";
            std::memcpy(optic.assetPath.data(),opticPath,sizeof(opticPath));
            if(invalidMetadata==1)m->weaponName[0]='?';
            if(invalidMetadata==2)m->states[0].count=1;
            if(invalidMetadata==3)++m->owner.equipGeneration;
            if(invalidMetadata==4)m->deadlineNs=r.s.hand.nowNs;
            if(invalidMetadata==5)mesh.kind=bc2::SelectedMeshKind::Spas12;
        }
        if(out.visibility.enabled){const auto& v=out.visibility;auto p=std::make_shared<bc2::WeaponVisibilityPlan>();p->reason=bc2::WeaponVisibilityReason::None;
            p->rig.soldier=v.nativeOwner.soldier;p->rig.weak=v.nativeOwner.weak;p->rig.count=147;p->nativeOwner=v.nativeOwner;p->request=v.request;p->hidden=v.hide;
            p->inputSequence=v.input.sequence;p->physicalEquipGeneration=v.input.owner.equipGeneration;p->meshSequence=v.selected->sequence;p->selected=v.selected;
            p->observedNs=v.input.observedNs;p->deadlineNs=v.input.deadlineNs;p->inputDeadlineNs=v.input.deadlineNs;
            s.visibility=bc2::WeaponVisibilityReceipt{v.nativeOwner,p->rig,v.request,v.input.sequence,v.input.owner.equipGeneration,100+nativeTick,r.s.hand.nowNs,p->deadlineNs,3,v.hide,p};}
        return s;}
    void Tick(){auto s=Sample();if(const auto request=holster.Demand(s)){bc2::HolsterInputOverride patch;
            if(patch.Apply(cache,*request,{this,[](void* c,const bc2::HolsterSuppressionRequest&)noexcept{return static_cast<HolsterRun*>(c)->allowReadback;}}))s.suppression=patch.Commit();
            if(s.suppression)++suppressions;}
        out=r.adapter.TickHolster(r.f.Memory(),r.s,s,holster,r.hands,r.intent);last=s;}
    void Step(float grip=0){r.Advance(grip);Tick();}
    bool Empty(){Step(1);if(holster.Phase()!=bc2::BodyHolsterPhase::HidePending)return false;Step(1);Step(1);
        return out.freeRight&&out.inventory.emptyHands&&!r.hands.Current(interaction::InteractionHand::Right);}
};
}
