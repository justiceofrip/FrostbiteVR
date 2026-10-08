#include "Test.h"
#include "Bc2NativeCycleService.h"
#include "Bc2ReloadConfigDescriptor.h"
#include "NativeProbeConfig.h"
#include "Bc2BodyInventory.h"
#include "Bc2BodyHolster.h"
#include "Bc2BoltInputStartup.h"
#include "Bc2BodyHolsterLifecycle.h"
#include "fvr/interaction/ControllerInput.h"
#include "Bc2InputBinding.h"
#include "Bc2BodyInventorySession.h"
#include "../src/platform/windows/BodyCrossDrawProbe.h"
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
    Run r;bc2::Bc2BodyHolster holster;bc2::BodyHolsterResult out;bool scopedXm8=false;unsigned invalidMetadata=0;
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
        out=r.adapter.TickHolster(r.f.Memory(),r.s,s,holster,r.hands,r.intent);}
    void Step(float grip=0){r.Advance(grip);Tick();}
    bool Empty(){Step(1);if(holster.Phase()!=bc2::BodyHolsterPhase::HidePending)return false;Step(1);Step(1);
        return out.freeRight&&out.inventory.emptyHands&&!r.hands.Current(interaction::InteractionHand::Right);}
};
}
int ActualUnsupportedExchangeRetirement(){
    for(unsigned mode=0;mode<3;++mode){
        HolsterRun h(bc2::BodyInventoryHolsterAcceptance,true);CHECK(h.Empty());
        const auto oldOther=h.r.adapter.AssignedSlot(Fixture::b);CHECK(oldOther);
        const auto originalRightItem=h.r.s.gun.id;CHECK(originalRightItem!=Fixture::a);
        const auto request=h.holster.RequestId();
        constexpr unsigned replacementData=0x14800,replacementPersistence=0x14980;
        h.r.f.Word(Fixture::a+4,replacementData);h.r.f.Word(replacementData,Fixture::table);
        h.r.f.Word(replacementData+0x64,replacementPersistence);h.r.f.Word(replacementData+0x84,0);
        h.r.f.types[replacementData]="SoldierWeaponData";
        ++h.r.s.owner.equipGeneration;++h.r.s.hand.owner.equipGeneration;
        h.r.s.gun={Fixture::a,h.r.s.hand.owner.equipGeneration};
        for(auto& hand:h.r.s.input.hands){hand.active=interaction::Components;hand.gripTracked=hand.aimTracked=true;}
        h.r.s.input.hands[1].grip.position={.2f,1.2f,-.3f};
        const auto tick=[&](bool pair,bool neutral,bool readable){
            const auto prior=h.out.ordinaryRecovery;
            h.r.Advance();h.r.s.input.hands[1].trigger=neutral?0.f:1.f;
            auto s=h.Sample();s.visibility.reset();
            auto mesh=std::make_shared<bc2::SelectedMeshesSnapshot>(*s.selected);
            std::fill(mesh->weaponName.begin(),mesh->weaponName.end(),'\0');std::memcpy(mesh->weaponName.data(),"AEK971_sp",10);
            mesh->states[0].count=0;mesh->weaponData=replacementData;s.selected=mesh;
            s.equipment={Fixture::a,replacementData,replacementPersistence,{}};
            std::memcpy(s.equipment.asset.data(),"AEK971_sp",10);
            if(pair&&prior){bc2::RigIdentity rig{};rig.soldier=Fixture::soldier;rig.weak=Fixture::weak;rig.pose=0x42000;rig.count=147;
                s.ordinaryPair=bc2::OrdinaryEquipmentPair{*prior,rig,100+h.nativeTick,s.hand.nowNs,prior->input.deadlineNs,3};}
            if(const auto demand=h.holster.Demand(s)){bc2::HolsterInputOverride patch;
                if(patch.Apply(h.cache,*demand,{&h,[](void*,const bc2::HolsterSuppressionRequest&)noexcept{return true;}}))s.suppression=patch.Commit();}
            h.r.f.denyMap=!readable;
            h.out=h.r.adapter.TickHolster(h.r.f.Memory(),h.r.s,s,h.holster,h.r.hands,h.r.intent);
        };
        tick(false,false,true);CHECK(h.out.ordinaryRecovery&&h.out.blockWeaponActions&&!h.r.hands.Current(interaction::InteractionHand::Right));
        CHECK(h.holster.RequestId()>request);
        tick(true,false,true);CHECK(!h.out.ordinaryRetired&&h.out.blockWeaponActions); // mask never fakes neutral
        if(mode==1){tick(true,true,false);CHECK(h.out.blockWeaponActions&&!h.out.ordinaryRetired);tick(false,true,true);}
        if(mode==2){tick(false,true,true);CHECK(h.out.blockWeaponActions&&!h.out.ordinaryRetired);}
        tick(true,true,true);CHECK(h.out.ordinaryRetired&&h.holster.Phase()==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions);
        const auto gun=h.r.hands.Current(interaction::InteractionHand::Right);CHECK(gun&&gun->token.item==h.r.s.gun&&gun->token.owner==h.r.s.hand.owner);
        CHECK(h.r.adapter.AssignedSlot(Fixture::b)==oldOther);CHECK(h.out.inventory.slots.size()==2);
        CHECK(!h.out.visibility.enabled&&!h.out.select&&!h.out.inventory.committedRequest&&!h.out.freeRight);
    }
    std::puts("Native exchange retirement: actual reader/suppression/arbiter composition, three variants passed");return 0;
}

int HeldFireMissingPresentationKeepsOnlyMetadata(){
    for(unsigned mode=0;mode<7;++mode){HolsterRun h;const auto before=h.r.adapter.AssignedSlot(h.r.s.owner.weapon);CHECK(before);
        h.r.s.input.hands[1].active|=interaction::Trigger;h.r.s.input.hands[1].trigger=1;h.r.s.cancel=h.r.s.cancelOnlyForFire=true;h.r.Advance(1);auto sample=h.Sample();
        if(mode!=1)h.r.s.visible.reset();
        if(mode==1)std::const_pointer_cast<bc2::SelectedMeshesSnapshot>(sample.selected)->states[0].meshes[0].kind=bc2::SelectedMeshKind::Unknown;
        if(mode==2)h.r.s.hand.deadlineNs=h.r.s.hand.nowNs;
        if(mode==3)h.r.s.hand.tracked[1]=false;
        if(mode==4)h.r.f.Word(Fixture::a+4,Fixture::bd);
        if(mode==5){++h.r.s.owner.actorGeneration;++h.r.s.hand.owner.actorGeneration;}
        if(mode==6){constexpr unsigned replacement=0x18500;std::memcpy(h.r.f.bytes.data()+replacement-0x10000,h.r.f.bytes.data()+Fixture::switching-0x10000,0x30);h.r.f.types[replacement]="WeaponSwitchingData";h.r.f.Word(Fixture::inventory+4,replacement);}
        h.out=h.r.adapter.TickHolster(h.r.f.Memory(),h.r.s,sample,h.holster,h.r.hands,h.r.intent);
        CHECK(!h.out.visibility.enabled&&!h.out.freeRight&&!h.out.inventory.request&&!h.out.ordinaryDraw.command);
        if(mode<4)CHECK(h.r.adapter.AssignedSlot(h.r.s.owner.weapon)==before&&h.out.blockWeaponActions==(mode>=2)&&!h.out.allowAutomaticGunHold&&!h.r.adapter.Display(h.r.s.hand.nowNs));
        else CHECK(h.r.adapter.AssignedSlot(h.r.s.owner.weapon)!=before&&h.out.blockWeaponActions);
    }return 0;
}
int HeldFireSlotContinuity(){
    HolsterRun h;CHECK(h.Empty());h.Step();h.Step(1);h.Step(1);h.Step(1);
    CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held);
    const auto before=h.r.adapter.AssignedSlot(h.r.s.owner.weapon);CHECK(before);
    const auto claim=h.r.hands.Current(interaction::InteractionHand::Right);CHECK(claim);
    h.r.s.input.hands[1].active|=interaction::Trigger;h.r.s.input.hands[1].trigger=1;
    h.r.s.cancel=h.r.s.cancelOnlyForFire=true;
    for(unsigned n=0;n<5;++n){h.Step(1);CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held);
        CHECK(h.r.adapter.AssignedSlot(h.r.s.owner.weapon)==before&&!h.out.blockWeaponActions);
        CHECK(h.r.hands.Current(interaction::InteractionHand::Right)->token==claim->token);CHECK(!h.out.inventory.request);}
    h.r.s.cancel=h.r.s.cancelOnlyForFire=false;h.r.s.input.hands[1].trigger=0;
    h.Step(1);CHECK(h.r.adapter.AssignedSlot(h.r.s.owner.weapon)==before&&!h.out.inventory.request);
    h.Step();h.Step(1);CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::HidePending);
    for(unsigned bad=0;bad<5;++bad){HolsterRun blocked;if(bad==4)CHECK(blocked.Empty());
        blocked.r.s.input.hands[1].active|=interaction::Trigger;blocked.r.s.input.hands[1].trigger=1;
        blocked.r.s.cancel=blocked.r.s.cancelOnlyForFire=true;
        if(bad==0)blocked.r.s.cancelOnlyForFire=false;
        if(bad==1)blocked.r.s.reloadBusy=true;
        if(bad==2)blocked.r.s.input.hands[1].trigger=0;
        if(bad==3)blocked.r.s.visible.reset();
        blocked.Tick();
        if(bad<3)CHECK(blocked.r.adapter.AssignedSlot(blocked.r.s.owner.weapon)&&!blocked.out.inventory.request&&!blocked.out.visibility.enabled);
        else if(bad==3)CHECK(blocked.r.adapter.AssignedSlot(blocked.r.s.owner.weapon)&&!blocked.out.blockWeaponActions&&!blocked.out.allowAutomaticGunHold&&!blocked.r.adapter.Display(blocked.r.s.hand.nowNs));
        else CHECK(!blocked.out.freeRight&&blocked.holster.Phase()!=bc2::BodyHolsterPhase::Empty);
    }
    return 0;
}
int GameplayInputRearmComposition(){
    // Compare historical unconditional invalidation, registered input rearm,
    // and current failed-Apply recovery using REAL action/cache/holster policies.
    for(unsigned repaired=0;repaired<3;++repaired){HolsterRun h;CHECK(h.Empty());
        for(auto& hand:h.r.s.input.hands){hand.active=interaction::Components;hand.gripTracked=hand.aimTracked=true;}
        interaction::ControllerActions actions;interaction::ActionOutput action;
        for(unsigned n=0;n<5;++n){h.r.Advance();action=actions.Update(h.r.s.input,{Fixture::soldier,h.r.s.owner.equipGeneration,true,true},h.r.s.input.predictedNs);h.Tick();}
        CHECK(action.active&&h.holster.Phase()==bc2::BodyHolsterPhase::Empty);const auto old=*h.out.freeRight;
        const auto before=h.r.s.owner;auto after=before;++after.equipGeneration;
        CHECK(h.holster.ObserveNativeInputGap(before,after));h.r.s.owner=after;
        h.r.s.input.predictedNs+=3000000000ll;h.r.s.hand.observedNs+=3000000000ll;h.r.s.hand.deadlineNs+=3000000000ll;
        bc2::BodyHolsterLifecycleLog journal;unsigned rejected=0,accepted=0;
        for(unsigned n=0;n<10;++n){h.r.Advance();
            action=actions.Update(h.r.s.input,{Fixture::soldier,h.r.s.owner.equipGeneration,true,true},h.r.s.input.predictedNs);
            // Exact production normalization before InputOverride::Apply.
            if(!action.active||action.owner!=Fixture::soldier){action={};action.owner=Fixture::soldier;}
            bc2::InputOverride patch;const auto originalCache=h.cache;
            if(patch.Apply(h.cache,action)){
                patch.Commit();++accepted;h.r.s.cancel=h.r.s.cancelOnlyForFire=false;h.Tick();
            }else{
                ++rejected;CHECK(!action.active&&h.cache==originalCache);
                const bool preserve=repaired==1&&bc2::BodyHolsterInputRearm(h.holster,h.r.s.owner,action,false);
                if(repaired==1)CHECK(preserve);
                if(repaired)journal.Invalidate(h.holster,preserve?bc2::BodyHolsterLifecycleReason::InputRearm:bc2::BodyHolsterLifecycleReason::InputApplyRejected,
                    preserve,h.r.s.hand,h.r.s.owner.equipGeneration);
                else { // Preserve the historical failure as an explicit negative control.
                    const auto beforePhase=h.holster.Phase();h.holster.Invalidate(false);
                    journal.Observe(bc2::BodyHolsterLifecycleReason::InputApplyRejected,false,beforePhase,h.holster.Phase(),h.r.s.hand,h.r.s.owner.equipGeneration);
                }
                h.r.adapter.Cancel();h.out.visibility={};
            }
        }
        CHECK(rejected>=2&&accepted>=3&&!bc2::BodyFreeRightEvidenceCurrent(old,h.r.s.hand.nowNs));
        if(repaired){CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);
            CHECK(!h.r.hands.Current(interaction::InteractionHand::Right)&&h.out.freeRight->visibility.request>old.visibility.request);
            CHECK(h.out.freeRight->visibility.nativeOwner==after&&h.out.freeRight->receipt.verifiedCopyMask==3);
            std::ostringstream proof;h.holster.Report(proof);CHECK(proof.str().find("\"phase\":4,")==std::string::npos);
        }else{CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&!h.out.freeRight);
            CHECK(h.r.hands.Current(interaction::InteractionHand::Right));}
        std::ostringstream report;journal.Report(report);CHECK(report.str().find(repaired==1?"\"reason\":\"input_rearm\"":"\"reason\":\"input_apply_rejected\"")!=std::string::npos);
    }
    return 0;
}
int GameplayInputRearmRejectsUnregisteredOrActiveFailure(){
    HolsterRun h;CHECK(h.Empty());interaction::ActionOutput inactive;inactive.owner=Fixture::soldier;
    CHECK(!bc2::BodyHolsterInputRearm(h.holster,h.r.s.owner,inactive,false));
    auto after=h.r.s.owner;++after.equipGeneration;CHECK(h.holster.ObserveNativeInputGap(h.r.s.owner,after));
    CHECK(bc2::BodyHolsterInputRearm(h.holster,after,inactive,false));
    CHECK(!bc2::BodyHolsterInputRearm(h.holster,after,inactive,true));
    auto wrong=after;++wrong.weapon;CHECK(!bc2::BodyHolsterInputRearm(h.holster,wrong,inactive,false));
    auto active=inactive;active.active=true;CHECK(!bc2::BodyHolsterInputRearm(h.holster,after,active,false));
    active=inactive;active.held=interaction::Fire;CHECK(!bc2::BodyHolsterInputRearm(h.holster,after,active,false));
    active=inactive;active.forward=.1f;CHECK(!bc2::BodyHolsterInputRearm(h.holster,after,active,false));
    active=inactive;active.owner=0;CHECK(!bc2::BodyHolsterInputRearm(h.holster,after,active,false));
    h.holster.Invalidate();CHECK(!bc2::BodyHolsterInputRearm(h.holster,after,inactive,false));return 0;
}
int GameplayInvalidationJournalIsBounded(){
    bc2::BodyHolsterLifecycleLog log;interaction::HandInteractionSample input;input.sequence=7;input.nowNs=1000;
    for(unsigned n=0;n<100;++n)log.Observe(n%2?bc2::BodyHolsterLifecycleReason::TrackingPublicationUnavailable:bc2::BodyHolsterLifecycleReason::InputApplyRejected,
        false,bc2::BodyHolsterPhase::Empty,bc2::BodyHolsterPhase::Recovering,input,2);
    std::ostringstream out;log.Report(out);CHECK(out.str().find("\"capacity\":64,\"total\":100,\"dropped\":36")!=std::string::npos);
    CHECK(input.sequence==7&&input.nowNs==1000);return 0;
}
int DefaultStowTests(){
    unsigned groups=0;
    {HolsterRun h;
        // No synthetic grip edge/contact: startup at a normal resting hand
        // position still requests the actual assigned native item.
        h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};
        CHECK(h.r.adapter.AutomaticStowPending());h.Step(0);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.inventory.request);
        h.Step(0);CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&h.out.inventory.request);
        CHECK(h.out.inventory.request->operation==interaction::BodyInventoryOperation::Holster);
        CHECK(h.out.inventory.request->item.id==Fixture::a&&!h.out.freeRight&&!h.out.visibility.enabled);
        CHECK(h.r.hands.Current(interaction::InteractionHand::Right)&&!h.r.adapter.AutomaticStowPending());
        h.Step(0);CHECK(!h.out.freeRight&&h.out.visibility.hide);h.Step(0);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight&&!h.r.hands.Current(interaction::InteractionHand::Right));
        CHECK(h.out.freeRight->receipt.verifiedCopyMask==3&&h.out.freeRight->suppression.nativeTick==h.nativeTick);
        std::ostringstream report;h.r.adapter.Report(report);
        CHECK(report.str().find("\"automatic_stow\":{\"enabled\":true,\"pending\":false,\"entries\":1,\"requests\":1")!=std::string::npos);++groups;}
    {HolsterRun h;h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        for(unsigned n=0;n<5;++n)h.Step(0);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.r.adapter.AutomaticStowPending());++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,false);
        for(unsigned n=0;n<4;++n)h.Step(0);CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.r.adapter.AutomaticStowPending());
        // A fresh, actually observed vehicle -> foot boundary queues once.
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        for(unsigned n=0;n<4;++n){h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);h.Step(0);}
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);
        h.Step(0);h.Step(1);CHECK(h.out.select);h.Step(1);h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::Held);
        for(unsigned n=0;n<5;++n){h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);h.Step(0);}
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.r.adapter.AutomaticStowPending());++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);h.Step();
        h.allowReadback=false;h.Step();h.Step();h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&!h.out.visibility.enabled&&!h.out.freeRight);
        CHECK(h.r.hands.Current(interaction::InteractionHand::Right));++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);h.Step();h.Step();
        // Suppression alone cannot complete the default stow; throw away the
        // previous renderer request so the fixture cannot generate any pair.
        h.out.visibility={};h.Step();h.out.visibility={};h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&!h.out.freeRight&&h.r.hands.Current(interaction::InteractionHand::Right));++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        h.r.s.reloadBusy=true;for(unsigned n=0;n<4;++n)h.Step();
        CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&!h.out.inventory.request&&h.r.adapter.AutomaticStowPending());
        h.r.s.reloadBusy=false;for(unsigned n=0;n<5;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        h.r.s.input.hands[1].trigger=.8f;for(unsigned n=0;n<4;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&h.r.adapter.AutomaticStowPending());
        h.r.s.input.hands[1].trigger=0;for(unsigned n=0;n<4;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        auto claim=h.r.hands.Acquire(h.r.s.hand,{h.r.s.hand.owner,interaction::InteractionHand::Left,interaction::HandClaimKind::AmmoObject,
            {0x7733,7},{{1,h.r.s.hand.owner.equipGeneration},h.r.s.hand.sequence,h.r.s.hand.deadlineNs,true},++h.r.intent,0});
        CHECK(claim.accepted);for(unsigned n=0;n<4;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&h.r.adapter.AutomaticStowPending());
        const auto held=h.r.hands.Current(interaction::InteractionHand::Left);CHECK(held);
        CHECK(h.r.hands.Release(h.r.s.hand,held->token).accepted);for(unsigned n=0;n<5;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty);++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);h.Step();
        // Observation loss does not acknowledge the queued request or convert
        // the returning pointer to a retained render/hand receipt.
        h.r.adapter.Cancel();h.r.s.input.focused=false;h.r.s.hand.focused=false;h.Step();
        CHECK(h.r.adapter.AutomaticStowPending()&&!h.out.freeRight);
        h.r.s.input.focused=true;h.r.s.hand.focused=true;
        h.r.Advance();h.r.Gun();h.Tick(); // Production ordinary GunHold reacquisition.
        for(unsigned n=0;n<5;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);h.Step();
        // A real item replacement while default intent is queued is not the
        // original carried item, even if the native weapon pointer is reused.
        h.r.f.Word(Fixture::ad+0x64,123);
        for(unsigned n=0;n<5;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.r.adapter.AutomaticStowPending());++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);h.Step();
        // Replacing native actor while not on foot clears the queued entry;
        // a later verified on-foot observation is a new event.
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier+0x100,Fixture::weak,false);
        CHECK(!h.r.adapter.AutomaticStowPending());
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        CHECK(h.r.adapter.AutomaticStowPending());for(unsigned n=0;n<4;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty);++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0,Fixture::player,Fixture::soldier,Fixture::weak,true);h.Step();
        CHECK(!h.r.adapter.AutomaticStowPending()&&h.out.phase==bc2::BodyHolsterPhase::Held);++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        for(unsigned n=0;n<4;++n)h.Step();CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty);
        const auto old=*h.out.freeRight;
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,false);
        h.holster.Invalidate();h.r.adapter.Cancel();h.r.hands.Reset();
        // The native seat exit produces a new actor/equip observation. Recovery
        // must Show the fresh owner before obtaining an entirely new hide.
        ++h.r.f.owner.actorGeneration;++h.r.f.owner.equipGeneration;h.r.s.owner=h.r.f.owner;
        ++h.r.s.hand.owner.actorGeneration;++h.r.s.hand.owner.equipGeneration;
        h.r.s.gun={Fixture::a,h.r.s.hand.owner.equipGeneration};
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        h.Step();CHECK(h.out.phase==bc2::BodyHolsterPhase::ShowPending&&!h.out.freeRight&&!h.out.select);
        CHECK(h.out.visibility.enabled&&!h.out.visibility.hide);
        CHECK(!bc2::BodyFreeRightCurrent(old,h.Sample(),h.r.hands));
        for(unsigned n=0;n<6;++n)h.Step();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);
        CHECK(h.out.freeRight->visibility.request>old.visibility.request&&h.out.freeRight->visibility.nativeOwner==h.r.s.owner);++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        auto sample=h.Sample();auto meshes=std::make_shared<bc2::SelectedMeshesSnapshot>(*sample.selected);
        std::fill(meshes->weaponName.begin(),meshes->weaponName.end(),'\0');std::memcpy(meshes->weaponName.data(),"XM8_sp_s",9);
        meshes->states[0].count=2;auto& gun=meshes->states[0].meshes[0];gun.kind=bc2::SelectedMeshKind::Xm8;
        std::fill(gun.assetPath.begin(),gun.assetPath.end(),'\0');
        constexpr char xm8[]="Objects/Weapons/Handheld/US_a_XM8/US_a_XM8_Mesh";
        std::memcpy(gun.assetPath.data(),xm8,sizeof(xm8));auto& acog=meshes->states[0].meshes[1];acog=gun;
        acog.kind=bc2::SelectedMeshKind::Acog4x;acog.address+=0x100;acog.namePointer+=0x100;
        std::fill(acog.assetPath.begin(),acog.assetPath.end(),'\0');
        constexpr char scope[]="Objects/Weapons/Attachments/Scope_ACOG/Scope_ACOG_Mesh";
        std::memcpy(acog.assetPath.data(),scope,sizeof(scope));sample.selected=meshes;
        CHECK(bc2::FindSelectedMesh(*meshes,h.r.s.owner,bc2::SelectedMeshKind::Xm8,h.r.s.hand.nowNs));
        CHECK(bc2::FindSelectedMesh(*meshes,h.r.s.owner,bc2::SelectedMeshKind::Acog4x,h.r.s.hand.nowNs));
        const auto result=h.r.adapter.TickHolster(h.r.f.Memory(),h.r.s,sample,h.holster,h.r.hands,h.r.intent);
        CHECK(!result.visibility.enabled&&!result.freeRight&&!h.r.adapter.AutomaticStowPending());
        CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(interaction::InteractionHand::Right));
        for(unsigned n=0;n<4;++n)h.Step();CHECK(h.out.phase==bc2::BodyHolsterPhase::Held);++groups;}
    {HolsterRun h;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        for(auto& hand:h.r.s.input.hands){hand.active=interaction::Components;hand.gripTracked=hand.aimTracked=true;}
        // Use the real finite receiver stream with its ordinary coordinate
        // frame. Fixture a is native assault class, so target its left slot by
        // reflecting X only; production SPAS uses the existing right shoulder.
        for(unsigned ms=0;ms<=2990;ms+=10){
            probe::BodyCrossDrawInput(h.r.s.input,ms);
            for(auto& hand:h.r.s.input.hands){hand.grip.position.y+=1.7f;hand.aim=hand.grip;}
            h.r.s.input.hands[1].grip.position.x=-h.r.s.input.hands[1].grip.position.x;
            CHECK(interaction::ValidInput(h.r.s.input));h.r.Advance(h.r.s.input.hands[1].squeeze);h.Tick();
            CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&!h.out.inventory.request&&h.r.adapter.AutomaticStowPending());
        }
        probe::BodyCrossDrawInput(h.r.s.input,3000);
        for(auto& hand:h.r.s.input.hands){hand.grip.position.y+=1.7f;hand.aim=hand.grip;}
        h.r.s.input.hands[1].grip.position.x=-h.r.s.input.hands[1].grip.position.x;
        h.r.Advance(1);h.Tick();
        CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&h.out.inventory.request&&!h.r.adapter.AutomaticStowPending());
        std::ostringstream report;h.r.adapter.Report(report);
        CHECK(report.str().find("\"entries\":1,\"requests\":0,\"unsupported\":0,\"superseded\":1")!=std::string::npos);++groups;}
    std::printf("Automatic stow: %u groups passed\n",groups);return 0;
}

int ScopedXm8OrdinaryAutoStowDraw(){
    HolsterRun h(bc2::BodyInventoryHolsterAcceptance,true);
    CHECK(h.r.s.gun.id!=h.r.s.owner.weapon);
    const auto nativeBefore=h.r.f.bytes;
    const auto slot=h.r.adapter.AssignedSlot(h.r.s.owner.weapon);CHECK(slot);
    const auto other=h.r.adapter.AssignedSlot(Fixture::b);CHECK(other);
    const auto oldGun=h.r.hands.Current(interaction::InteractionHand::Right);CHECK(oldGun);
    h.r.adapter.EnableAutomaticStow();
    h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
    h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};
    h.Step();h.Step();CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::HidePending&&!h.out.freeRight);
    CHECK(h.r.hands.Current(interaction::InteractionHand::Right)->token==oldGun->token);
    h.Step();CHECK(h.out.visibility.hide&&!h.out.freeRight);h.Step();
    CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight&&!h.r.hands.Current(interaction::InteractionHand::Right));
    CHECK(h.out.freeRight->receipt.verifiedCopyMask==3&&h.out.freeRight->suppression.nativeTick==h.nativeTick);
    CHECK(h.out.freeRight->authorizationDeadlineNs==0&&h.holster.DiagnosticDeadline()==0);
    CHECK(h.r.adapter.AssignedSlot(h.r.s.owner.weapon)==slot&&h.r.adapter.AssignedSlot(Fixture::b)==other);
    h.r.s.input.hands[1].grip.position={-.20f,1.58f,.18f};h.Step();h.Step(1);
    CHECK(h.out.select==std::optional(slot->item)&&h.out.blockWeaponActions&&!h.r.hands.Current(interaction::InteractionHand::Right));
    h.Step(1);h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions);
    const auto drawn=h.r.hands.Current(interaction::InteractionHand::Right);CHECK(drawn&&drawn->token.item==h.r.s.gun&&drawn->token.id!=oldGun->token.id);
    h.r.s.input.hands[1].active|=interaction::Trigger;h.r.s.input.hands[1].trigger=1;
    h.r.s.cancel=h.r.s.cancelOnlyForFire=true;
    for(unsigned n=0;n<5;++n){h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions&&!h.out.inventory.request);
        CHECK(h.r.adapter.AssignedSlot(h.r.s.owner.weapon)==slot&&h.r.adapter.AssignedSlot(Fixture::b)==other);
        CHECK(h.r.hands.Current(interaction::InteractionHand::Right)->token==drawn->token);}
    CHECK(h.r.f.bytes==nativeBefore); // No capacity/item/ammunition edits in this adapter.
    return 0;
}
int ScopedXm8OrdinaryAdmissionNegatives(){
    for(unsigned bad=1;bad<=5;++bad){HolsterRun h(bc2::BodyInventoryHolsterAcceptance,true);h.invalidMetadata=bad;
        const auto before=h.r.f.bytes;h.r.adapter.EnableAutomaticStow();
        h.r.adapter.ObserveNativeActor(0x50000,Fixture::player,Fixture::soldier,Fixture::weak,true);
        h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};
        for(unsigned n=0;n<5;++n){h.Step();CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions&&!h.out.freeRight&&!h.out.visibility.enabled);}
        CHECK(h.r.hands.Current(interaction::InteractionHand::Right)&&h.r.f.bytes==before);
    }
    {HolsterRun h({},true);for(unsigned n=0;n<4;++n)h.Step();CHECK(!h.out.freeRight&&!h.out.visibility.enabled&&!h.out.blockWeaponActions);}
    return 0;
}
int UnevaluatedHolsterRetiresChangedLifetimes(){
    for(unsigned mode=0;mode<3;++mode){HolsterRun h;const auto first=h.r.adapter.AssignedSlot(Fixture::a),other=h.r.adapter.AssignedSlot(Fixture::b);CHECK(first&&other);
        if(mode==0)h.r.f.Word(Fixture::a+4,Fixture::bd);
        if(mode==1){++h.r.s.owner.actorGeneration;++h.r.s.hand.owner.actorGeneration;}
        if(mode==2){constexpr unsigned replacement=0x18500;
            std::memcpy(h.r.f.bytes.data()+replacement-0x10000,h.r.f.bytes.data()+Fixture::switching-0x10000,0x30);
            h.r.f.types[replacement]="WeaponSwitchingData";h.r.f.Word(Fixture::inventory+4,replacement);}
        h.r.Advance();auto sample=h.Sample();h.r.f.onRead=[&]{h.holster=bc2::Bc2BodyHolster{};};
        h.out=h.r.adapter.TickHolster(h.r.f.Memory(),h.r.s,sample,h.holster,h.r.hands,h.r.intent);
        CHECK(h.out.inventoryEvaluation==bc2::BodyInventoryEvaluation::Disabled);
        CHECK(!h.r.adapter.AssignedSlot(Fixture::a));
        if(mode==0)CHECK(h.r.adapter.AssignedSlot(Fixture::b)==other);
        else CHECK(!h.r.adapter.AssignedSlot(Fixture::b));
        CHECK(!h.out.visibility.enabled&&!h.out.freeRight&&!h.out.inventory.request&&!h.r.adapter.Display(h.r.s.hand.nowNs));
    }return 0;
}
int UnevaluatedHolsterDoesNotEraseCurrentSlots(){
    for(unsigned mode=0;mode<2;++mode){HolsterRun h;const auto rifle=h.r.adapter.AssignedSlot(Fixture::a),shotgun=h.r.adapter.AssignedSlot(Fixture::b);CHECK(rifle&&shotgun);
        h.r.Advance();auto sample=h.Sample();
        // Reproduce eligibility changing during a real coherent inventory read,
        // between reader precheck and holster consumer. No stale render proof.
        h.r.f.onRead=[&]{if(mode==0)h.holster=bc2::Bc2BodyHolster{};
            else std::const_pointer_cast<bc2::SelectedMeshesSnapshot>(sample.selected)->states[0].meshes[0].kind=bc2::SelectedMeshKind::Unknown;};
        h.out=h.r.adapter.TickHolster(h.r.f.Memory(),h.r.s,sample,h.holster,h.r.hands,h.r.intent);
        CHECK(h.out.inventoryEvaluation==(mode==0?bc2::BodyInventoryEvaluation::Disabled:bc2::BodyInventoryEvaluation::UnsupportedProfile));
        CHECK(h.r.adapter.AssignedSlot(Fixture::a)==rifle&&h.r.adapter.AssignedSlot(Fixture::b)==shotgun);
        CHECK(!h.out.visibility.enabled&&!h.out.freeRight&&!h.out.inventory.request&&!h.r.adapter.Display(h.r.s.hand.nowNs));
        h.r.adapter.Cancel();CHECK(!h.r.adapter.AssignedSlot(Fixture::a)&&!h.r.adapter.AssignedSlot(Fixture::b));
    }return 0;
}
int ScopedXm8OrdinaryKeepsSlotsAcrossDraw(){
    HolsterRun h(bc2::BodyInventoryHolsterAcceptance,true);
    const auto rifle=h.r.adapter.AssignedSlot(Fixture::a),shotgun=h.r.adapter.AssignedSlot(Fixture::b);CHECK(rifle&&shotgun);
    CHECK(h.Empty());h.Step();h.r.s.input.hands[1].grip.position.x=.20f;h.Step(1);
    CHECK(h.out.select==std::optional(shotgun->item)&&h.out.blockWeaponActions&&!h.r.hands.Current(interaction::InteractionHand::Right));
    h.r.f.Select(1);h.r.s.owner=h.r.f.owner;++h.r.s.hand.owner.equipGeneration;
    h.r.s.gun={Fixture::b,h.r.s.hand.owner.equipGeneration};h.scopedXm8=false;
    h.Step(1);CHECK(!h.out.freeRight&&h.out.blockWeaponActions);h.Step(1);
    CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions&&h.r.hands.Current(interaction::InteractionHand::Right)->token.item==h.r.s.gun);
    CHECK(h.r.adapter.AssignedSlot(Fixture::a)==rifle&&h.r.adapter.AssignedSlot(Fixture::b)==shotgun);
    h.Step();CHECK(h.Empty()); // The same ordinary acceptance still supports SPAS.
    return 0;
}

int HeldApproachBoundaries(){
 using namespace interaction;
 for(unsigned mode=0;mode<14;++mode){BodyGripApproach policy;
  HandInteractionSample in{{1,2,3,4},10,1000000000,1100000000,1000000000,true,{true,true}};
  HandInteractionKey weapon{5,3};std::uint64_t revision=1,claim=7;
  const auto step=[&](std::int64_t delta=10000000){++in.sequence;in.observedNs+=delta;in.deadlineNs=in.observedNs+100000000;in.nowNs=in.observedNs;};
  CHECK(!policy.Update(in,weapon,revision,claim,false,true,true,false)); // startup-held cannot arm
  step();CHECK(!policy.Update(in,weapon,revision,claim,false,true,true,true));
  step();CHECK(!policy.Update(in,weapon,revision,claim,true,true,true,false));
  const auto oldOwner=in.owner;const auto oldKey=weapon;
  if(mode==1){step(151000000);}
  else if(mode==2){for(unsigned n=0;n<61;++n){step();CHECK(!policy.Update(in,weapon,revision,claim,false,true,true,false));}}
  else step();
  if(mode==3)++in.owner.space;
  if(mode==4){++in.owner.equipGeneration;++weapon.generation;}
  if(mode==5)++revision;
  if(mode==6)++claim;
  if(mode==7)in.focused=false;
  if(mode==8)in.tracked[0]=false;
  if(mode==9)in.deadlineNs=in.nowNs;
  if(mode==10)in.sequence-=2;
  if(mode==11)--in.owner.actorGeneration;
  const bool held=mode!=12,eligible=mode!=13;
  CHECK(policy.Update(in,weapon,revision,claim,false,held,eligible,true)==(mode==0));
  CHECK(!policy.Update(in,weapon,revision,claim,false,held,eligible,true)); // exact duplicate never repeats
  in.owner=oldOwner;weapon=oldKey;in.focused=true;in.tracked={true,true};in.sequence+=5;step();
  CHECK(!policy.Update(in,weapon,revision,claim,false,true,true,true)); // no resurrected approach after failure/consume
 }
 std::puts("Body held-grip approach: 14 boundary variants passed");return 0;
}
int ScrollThenActualHeldApproach(){
 HolsterRun h;h.r.f.Word(Fixture::ad+0x84,1);h.r.f.Word(Fixture::bd+0x84,0);
 h.r.s.cancel=true;h.Step();h.r.s.cancel=false;
 // Ordinary native scroll away and back; no invented hide/show acknowledgement.
 for(unsigned slot:{1u,0u}){h.r.f.Select(slot);h.r.s.owner=h.r.f.owner;++h.r.s.hand.owner.equipGeneration;
  h.r.s.gun={h.r.s.owner.weapon,h.r.s.hand.owner.equipGeneration};h.r.Advance();h.r.Gun();h.Tick();}
 const auto assigned=h.r.adapter.AssignedSlot(Fixture::a);CHECK(assigned&&assigned->slot==2);
 h.r.s.input.hands[1].grip.position={-.035177f,1.7454658f,.213527f};h.Step();h.Step(1);
 CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.inventory.request);
 for(unsigned n=0;n<20;++n)h.Step(1);
 // Actual093210 raw body contacts7040 (outside) ->7061 (inside), grip held.
 h.r.s.input.hands[1].grip.position={.202042f,1.6885846f,.0539406f};h.Step(1);
 if(h.out.phase!=bc2::BodyHolsterPhase::HidePending){std::ostringstream report;h.r.adapter.Report(report);std::fprintf(stderr,"%s\n",report.str().c_str());}
 CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&h.out.inventory.request);
 h.Step(1);h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);
 CHECK(!h.r.hands.Current(interaction::InteractionHand::Right));
 // Unsupported profile stays on ordinary input; no held approach admits hide.
 HolsterRun unsupported(bc2::BodyInventoryHolsterAcceptance,true);unsupported.invalidMetadata=1;
 unsupported.r.s.input.hands[1].grip.position={0,1.3f,-.4f};unsupported.Step();unsupported.Step(1);
 unsupported.r.s.input.hands[1].grip.position={-.2f,1.58f,.18f};unsupported.Step(1);
 CHECK(unsupported.out.phase==bc2::BodyHolsterPhase::Disabled&&!unsupported.out.visibility.enabled&&!unsupported.out.inventory.request);
 CHECK(unsupported.holster.Phase()==bc2::BodyHolsterPhase::Held);
 std::puts("Body held-grip approach: actual native reader/scroll/claims/suppression composition passed");return 0;
}

#include "Bc2ShoulderGestureChecks.inc"
#include "Bc2ShoulderSwapChecks.inc"
#include "Bc2HolsterRepeatChecks.inc"
int OrdinaryOwnSlotStowIsExplicitlyUnavailable(){
    Run r;r.Tick();r.s.input.hands[1].grip.position.x=-.20f;r.Advance(1);
    const auto before=r.hands.Current(interaction::InteractionHand::Right);
    const auto result=r.Tick();CHECK(!result.command&&!result.pending);
    const auto after=r.hands.Current(interaction::InteractionHand::Right);
    CHECK(before&&after&&before->token==after->token);
    std::ostringstream report;r.adapter.Report(report);const auto text=report.str();
    CHECK(text.find("own_slot_stow_unavailable")!=std::string::npos);
    CHECK(text.find("\"kind\":4")!=std::string::npos);
    return 0;
}
int ResourceLifetimeBinding(){
 Run r;r.Tick();const auto a=r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs);CHECK(a);
 const auto other=r.adapter.AssignedSlot(Fixture::b);CHECK(other);
 CHECK(a->context.resource.weaponGeneration==r.adapter.AssignedSlot(Fixture::a)->item.generation);
 CHECK(!r.adapter.ResourceBinding(r.s.owner,a->deadlineNs));
 r.s.reloadBusy=true; // native lifetime remains observed while interaction is suspended
 for(unsigned cycle=0;cycle<32;++cycle){
  const auto slot=cycle%2?0u:1u;r.f.Select(slot);r.s.owner=r.f.owner;r.Advance();r.Tick();
  const auto binding=r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs);CHECK(binding);
  CHECK(binding->context.resource.weaponGeneration==(slot?other->item.generation:a->context.resource.weaponGeneration));
  CHECK(binding->context.equipGeneration==r.s.owner.equipGeneration);
  CHECK(!r.adapter.Display(r.s.hand.nowNs)); // display is not the identity authority
 }
 ++r.s.owner.space;r.Advance();r.Tick();const auto recentered=r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs);CHECK(recentered);
 CHECK(recentered->context.resource==a->context.resource&&recentered->context.space!=a->context.space);
 auto oldOwner=r.s.owner;--oldOwner.space;CHECK(!r.adapter.ResourceBinding(oldOwner,r.s.hand.nowNs));
 r.f.Word(Fixture::a+4,Fixture::bd);r.Advance();r.Tick();
 const auto replacement=r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs);CHECK(replacement);
 CHECK(replacement->context.resource.weaponGeneration!=a->context.resource.weaponGeneration&&replacement->data==Fixture::bd);
 r.s.hand.nowNs+=151000000;r.Tick();const auto gap=r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs);CHECK(gap);
 CHECK(gap->context.resource.weaponGeneration!=replacement->context.resource.weaponGeneration);
 r.f.denyMap=true;r.s.hand.nowNs+=10000000;r.Tick();CHECK(!r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs));
 return 0;
}
int SharedLifecycleBoundary(){
 CHECK(ResourceLifetimeBinding()==0);
 for(unsigned reason=0;reason<5;++reason){Run r;r.Tick();const auto slot=r.adapter.AssignedSlot(Fixture::a);CHECK(slot);
  r.Advance();if(reason==0)r.s.reloadBusy=true;else if(reason==1)r.s.cancel=true;else if(reason==2)r.s.hand.tracked[1]=false;
  else if(reason==3)r.s.interaction=bc2::BodyInteractionState::Suspended;else r.s.input.focused=false;
  auto blocked=r.Tick();CHECK(!blocked.command&&!blocked.pending&&!r.adapter.Display(r.s.hand.nowNs));
  const auto retained=r.adapter.AssignedSlot(Fixture::a);CHECK(retained&&*retained==*slot);
  r.s.reloadBusy=r.s.cancel=false;r.s.hand.tracked[1]=true;r.s.input.focused=true;r.s.interaction=bc2::BodyInteractionState::Available;
  r.Advance(1);CHECK(!r.Tick().command);CHECK(r.adapter.AssignedSlot(Fixture::a)==slot); // held squeeze cannot rearm
 }
 {Run r;r.Tick();auto old=r.adapter.AssignedSlot(Fixture::a);CHECK(old);r.s.hand.nowNs+=151000000;r.s.hand.observedNs=r.s.hand.nowNs;r.s.hand.deadlineNs=r.s.hand.nowNs+100000000;
  ++r.s.input.generation;r.s.hand.sequence=r.s.input.generation;r.Tick();auto now=r.adapter.AssignedSlot(Fixture::a);CHECK(now&&now->item.generation!=old->item.generation);}
 for(unsigned structural=0;structural<3;++structural){Run r;r.Tick();auto old=r.adapter.AssignedSlot(Fixture::a);CHECK(old);r.Advance();r.s.reloadBusy=true;
  if(structural==0)r.f.Word(Fixture::a+4,Fixture::bd);else if(structural==1){++r.s.owner.actorGeneration;++r.s.hand.owner.actorGeneration;}
  else r.f.Word(Fixture::ad+0x64,0x123456);
  r.Tick();auto now=r.adapter.AssignedSlot(Fixture::a);CHECK(!now||now->item.generation!=old->item.generation);
 }
 {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);CHECK(r.Tick().command);auto slot=r.adapter.AssignedSlot(Fixture::a);CHECK(slot);
  r.Advance(1);r.s.interaction=bc2::BodyInteractionState::Suspended;auto stopped=r.Tick();CHECK(stopped.cancelled&&!stopped.pending&&!stopped.command);
  r.f.Select(1);r.s.owner=r.f.owner;r.s.gun={r.s.owner.weapon,r.s.hand.owner.equipGeneration};r.Advance(1);r.s.interaction=bc2::BodyInteractionState::Available;
  auto restored=r.Tick();CHECK(!restored.committed&&!restored.command);}
 {Run r;r.Tick();auto old=r.adapter.AssignedSlot(Fixture::a);CHECK(old);r.Advance();r.Tick();r.Advance(1);++r.s.owner.space;++r.s.hand.owner.space;++r.s.input.spaceGeneration;r.Gun();r.Advance(1);r.Gun();auto hold=r.hands.Current(interaction::InteractionHand::Right);CHECK(hold);CHECK(!r.Tick().command);CHECK(r.hands.Current(interaction::InteractionHand::Right)->token==hold->token);CHECK(r.adapter.AssignedSlot(Fixture::a)==old);}
 {Run r;r.Tick();auto old=r.adapter.AssignedSlot(Fixture::a);CHECK(old);r.adapter.Cancel();CHECK(!r.adapter.AssignedSlot(Fixture::a));r.Advance();r.Tick();CHECK(r.adapter.AssignedSlot(Fixture::a)!=old);}
 {Run r;r.Tick();r.Advance();r.s.reloadBusy=true;r.f.denyMap=true;r.Tick();CHECK(!r.adapter.AssignedSlot(Fixture::a));}
 {HolsterRun h;auto old=h.r.adapter.AssignedSlot(Fixture::a);CHECK(old);auto claim=h.r.hands.Current(interaction::InteractionHand::Right);CHECK(claim);
  h.r.s.reloadBusy=true;for(unsigned n=0;n<30;++n){h.Step();CHECK(h.r.adapter.AssignedSlot(Fixture::a)==old&&!h.out.inventory.request&&!h.out.visibility.enabled&&!h.out.freeRight&&!h.r.adapter.Display(h.r.s.hand.nowNs));}
  CHECK(h.r.hands.Current(interaction::InteractionHand::Right)->token==claim->token);}
 {Run r;r.f.Word(Fixture::ad+0x84,1);r.f.Word(Fixture::bd+0x84,0);r.Tick();
  r.f.Word(Fixture::ad+0x84,0);r.Advance();r.Tick();auto a=r.adapter.AssignedSlot(Fixture::a),b=r.adapter.AssignedSlot(Fixture::b);CHECK(a&&b&&a->slot==2&&b->slot==1);
  r.f.Word(Fixture::array,Fixture::b);r.f.Word(Fixture::array+4,Fixture::a);r.f.Word(Fixture::inventory+0x14c,1);
  r.Advance(1);++r.s.owner.space;++r.s.hand.owner.space;++r.s.input.spaceGeneration;
  CHECK(!r.Tick().command&&r.adapter.AssignedSlot(Fixture::a)==a&&r.adapter.AssignedSlot(Fixture::b)==b);
  if(const auto display=r.adapter.Display(r.s.hand.nowNs))CHECK(display->physicalOwner.space==r.s.owner.space&&display->physicalOwner.space!=9); // never reuse old spatial publication
 }
 {Run r;r.Tick();r.Advance();r.Tick();r.s.hand.nowNs-=20000000;r.s.hand.observedNs=r.s.hand.nowNs;
  auto reversed=r.Tick();CHECK(reversed.blockFire&&!reversed.command&&!r.adapter.AssignedSlot(Fixture::a)&&!r.adapter.Display(r.s.hand.nowNs));
  r.s.reloadBusy=true;auto repeated=r.Tick();CHECK(repeated.blockFire&&!r.adapter.AssignedSlot(Fixture::a));}
 for(unsigned mode=0;mode<3;++mode){Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);const auto request=r.Tick();CHECK(request.command&&request.pending);
  const auto a=r.adapter.AssignedSlot(Fixture::a),b=r.adapter.AssignedSlot(Fixture::b);CHECK(a&&b);
  r.f.Word(Fixture::map+16,0);const auto native=bc2::ReadBodyInventory(r.f.Memory(),r.s.owner,true);CHECK(native.snapshot);
  CHECK(!bc2::ResolveBodyDraw(*native.snapshot,request.command->targetWeapon)); // actual selector rejection, same carried items
  const auto hold=r.hands.Current(interaction::InteractionHand::Right);CHECK(hold);
  r.adapter.SuspendInteraction();CHECK(!r.adapter.Pending()&&!r.adapter.Display(r.s.hand.nowNs));
  CHECK(r.adapter.AssignedSlot(Fixture::a)==a&&r.adapter.AssignedSlot(Fixture::b)==b);
  CHECK(r.hands.Current(interaction::InteractionHand::Right)->token==hold->token);
  if(mode==1)r.f.Word(Fixture::a+4,Fixture::bd);
  if(mode==2){++r.s.owner.actorGeneration;++r.s.hand.owner.actorGeneration;}
  r.Advance(1);const auto next=r.Tick();CHECK(!next.command&&!next.committed);
  CHECK((r.adapter.AssignedSlot(Fixture::a)==a)==(mode==0));
  CHECK((r.adapter.AssignedSlot(Fixture::b)==b)==(mode<2));
 }
 std::puts("shared lifetime boundary: suspension, neutral rearm, bounded native gap and exact replacement passed");return 0;
}

#include "Bc2PlayerHolsterChecks.inc"

int NativePumpDebtProtectsActualShoulderAndResourceLifetime(){
 using namespace bc2;using namespace interaction;
 const auto config=[] {const auto& d=SpasReloadDescriptor;ReloadObservedConfig c;
  c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
  std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
  const auto& v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
  c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
  c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
  c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;}();
 for(bool debt:{false,true}){HolsterRun h;auto& r=h.r;Bc2NativeCycleService pump;
  const auto binding=r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs);CHECK(binding);
  const auto slot=r.adapter.AssignedSlot(Fixture::a);CHECK(slot);
  const auto gun=r.hands.Current(InteractionHand::Right);CHECK(gun);
  Bc2NativeCycleControl control{r.s.owner,r.s.hand,r.s.gun,{991,99},{},true};CHECK(pump.Control(control));
  if(debt){ReloadHoldInput source;source.identity.owner=r.s.owner;source.identity.firing={0x30000,0x31000,0x32000};
   source.identity.serverPlayer=0x33000;source.identity.serverSoldier=0x34000;source.identity.serverItem=0x35000;
   source.config=config;source.verified=true;source.branch=0;source.nowNs=source.contextObservedNs=r.s.hand.nowNs;
   source.leaseDeadlineNs=source.nowNs+100000000;source.context.deltaSeconds=.016f;source.context.reloadTimeMultiplier=1;
   source.context.flags24Through28[0]=true;
   for(unsigned b=0;b<3;++b){auto& v=source.branches[b];v.address=source.identity.firing[b];v.wrapperOffset=b==0?0x3c:b==1?0x40:0x10;
    v.currentState=v.nextState=2;v.previousState=1;v.loaded=8;v.reserve=24;source.capacities[b]=8;}
   const auto decision=pump.Evaluate(source,1);CHECK(decision.tracked&&!decision.hold);
   ReloadFlowRecord shot;shot.id=1;shot.entry.nativeInvocation=shot.entry.update=shot.entry.nativeUpdate=1;
   shot.entry.kind=ReloadFlowEvent::Update;shot.entry.thread=1;shot.entry.depth=1;shot.entry.caller=0x6e90b0;shot.entry.context=0x36000;
   shot.entry.nowNs=source.nowNs;shot.entry.contextCopied=true;float dt=.016f,multiplier=1;
   std::memcpy(shot.entry.copiedContext.data()+0x18,&dt,4);std::memcpy(shot.entry.copiedContext.data()+0x20,&multiplier,4);
   shot.entry.copiedContext[0x24]=std::byte{1};auto& before=shot.entry.boundary;before.owner=r.s.owner;
   before.snapshotSequence=1;before.firing=source.identity.firing[0];before.wrapperOffset=0x3c;before.current=before.next=2;
   before.previous=1;before.loaded=8;before.reserve=24;shot.exit.boundary=before;
   shot.exit.boundary->loaded=7;shot.exit.boundary->current=6;shot.exit.boundary->previous=5;shot.exit.boundary->next=7;
   shot.exit.boundary->timer=.7f;shot.exit.boundary->flagsA8=2;shot.exit.thread=1;shot.exit.nowNs=source.nowNs+100;
   shot.exit.contextCopied=true;shot.exit.copiedContext=shot.entry.copiedContext;shot.finished=shot.identityRetained=true;
   pump.Finish(decision,shot);CHECK(pump.View(source.nowNs+100).phase==Bc2NativeCyclePhase::ShotObserved);
  }
  r.s.reloadBusy=pump.View(r.s.hand.nowNs+100).blocksFire;CHECK(r.s.reloadBusy==debt);h.Step(1);
  if(debt){CHECK(h.holster.Phase()==BodyHolsterPhase::Held&&!h.out.visibility.enabled&&!h.out.inventory.request);
   CHECK(r.hands.Current(InteractionHand::Right)->token==gun->token&&!pump.YieldForReload());}
  else CHECK(h.holster.Phase()==BodyHolsterPhase::HidePending&&h.out.inventory.request);
  CHECK(r.adapter.AssignedSlot(Fixture::a)==slot);
  const auto retained=r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs);CHECK(retained&&retained->context.resource==binding->context.resource);
 }
 std::puts("Pump/body composition: idle stow and exact native debt suspension passed");return 0;
}
int OrdinaryBoltStartupPreservesOriginalRigPublication(){
 using namespace bc2;using namespace interaction;
 for(bool reproduceMissingVisibility:{true,false}){
  Run r;Bc2BodyHolster holster;Bc2BoltInputStartup startup;std::shared_ptr<BodyHolsterProbeSample> published;
  r.s.input.head=r.s.input.referenceHead={};
  for(auto& hand:r.s.input.hands){hand.active=Components;hand.gripTracked=hand.aimTracked=true;}
  r.s.input.hands[1].grip.position={.15f,-.1f,-.2f};
  // No accepted holster/selected-mesh profile. Real inventory and existing
  // shared GunHold still follow TickHolster's ordinary visible-weapon route.
  for(unsigned tick=0;tick<650&&!startup.Ready()&&!startup.Failed();++tick){
   r.Advance(1);auto input=r.s.input;
   if(published)startup.Prepare(input,r.s.owner,published,r.s.hand.nowNs);
   r.s.input=input;BodyHolsterSample command;command.nativeOwner=r.s.owner;command.hand=r.s.hand;
   command.nativeTick=tick+1;command.gun=r.s.gun;
   CHECK(!command.ordinary&&!command.selected);
   const auto outcome=r.adapter.TickHolster(r.f.Memory(),r.s,command,holster,r.hands,r.intent);
   CHECK(!command.ordinary&&holster.Phase()==BodyHolsterPhase::Held&&!outcome.freeRight&&!outcome.visibility.enabled);
   auto sample=std::make_shared<BodyHolsterProbeSample>();CaptureBodyHolsterProbeSource(*sample,r.s);
   sample->sampledNs=r.s.hand.nowNs;sample->nativeTick=command.nativeTick;sample->phase=holster.Phase();sample->outcome=outcome;
   sample->selectedSlot=r.adapter.AssignedSlot(r.s.owner.weapon);sample->right=r.hands.Current(InteractionHand::Right);
   sample->left=r.hands.Current(InteractionHand::Left);
   CHECK(sample->ordinaryVisible&&sample->ordinaryVisible->observedNs==r.s.visible->observedNs&&sample->ordinaryVisible->deadlineNs==r.s.visible->deadlineNs);
   CHECK(sample->right&&sample->right->token.kind==HandClaimKind::GunHold&&sample->right->inputSequence==r.s.hand.sequence);
   if(reproduceMissingVisibility)sample->ordinaryVisible=command.ordinary; // Exact former Gameplay publication.
   published=std::move(sample);CHECK(input.hands[1].trigger==0);
  }
  CHECK(reproduceMissingVisibility?startup.Failed()&&!startup.Ready():startup.Ready()&&!startup.Failed());
 }
 std::puts("Ordinary bolt startup publication: former pass-by-value source reproduces timeout; original visible rig and real GunHold succeed");
 return 0;
}
int main(){if(OrdinaryBoltStartupPreservesOriginalRigPublication())return 1;if(NativePumpDebtProtectsActualShoulderAndResourceLifetime())return 1;if(PlayerRepeatedHolsterRecovery())return 1;if(SharedLifecycleBoundary())return 1;if(OrdinaryOwnSlotStowIsExplicitlyUnavailable())return 1;if(ActualCarriedCrossDrawRetirement()||CarriedCrossDrawIdentityFailures()||CarriedCrossDrawAuthorityFailures()||HolsterTransitionJournalKeepsLatest())return 1;if(ShoulderEarlyDirectSwap()||ShoulderDirectSwapFailures()||ShoulderSwapRereadAndTimeout())return 1;if(ShoulderEarlyGrabAndReleaseStow()||ShoulderMissKeepsUsableGun()||ShoulderReleaseExactContinuity()||ShoulderFailedHideRecoversHeld()||ShoulderEmptyApproachInputBreaks())return 1;if(HeldApproachBoundaries()||ScrollThenActualHeldApproach())return 1;if(ActualUnsupportedExchangeRetirement())return 1;if(ScopedXm8OrdinaryAutoStowDraw()||ScopedXm8OrdinaryAdmissionNegatives()||ScopedXm8OrdinaryKeepsSlotsAcrossDraw())return 1;
    std::puts("Ordinary scoped XM8: automatic stow/draw/fire continuity, six admission negatives, cross-draw slots passed");
    if(GameplayInputRearmComposition()||GameplayInputRearmRejectsUnregisteredOrActiveFailure()||GameplayInvalidationJournalIsBounded())return 1;std::puts("Gameplay pause rearm: 3 groups passed (actual policy/input/consumer)");if(UnevaluatedHolsterRetiresChangedLifetimes()||UnevaluatedHolsterDoesNotEraseCurrentSlots()||HeldFireMissingPresentationKeepsOnlyMetadata()||HeldFireSlotContinuity())return 1;std::puts("Held firing slot continuity: positive and five negative variants passed");
    if(DefaultStowTests())return 1;
    unsigned groups=0;
    {Fixture f;const auto raw=f.bytes;auto r=bc2::ReadBodyInventory(f.Memory(),f.owner,true);CHECK(r.snapshot&&r.snapshot->items[1].category==1&&r.reads<16384&&raw==f.bytes);
        CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner).snapshot);auto route=bc2::ResolveBodyDraw(*r.snapshot,Fixture::b);CHECK(route&&route->action==7&&route->targetSlot==1);++groups;}
    {Fixture f;f.Word(Fixture::fields+16,0x80);CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot);++groups;}
    {Fixture f;f.Word(Fixture::enumFields+24+16,5);CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot);++groups;}
    {Fixture f;f.Word(Fixture::array+8,Fixture::a);CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot);++groups;}
    {Fixture f;f.mutate=true;CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot);++groups;}
    {Fixture f;f.mutateCategory=true;auto result=bc2::ReadBodyInventory(f.Memory(),f.owner,true);CHECK(!result.snapshot&&result.status==bc2::BodyReadStatus::Changed);++groups;}
    {Fixture f;f.mutateEnum=true;auto result=bc2::ReadBodyInventory(f.Memory(),f.owner,true);CHECK(!result.snapshot&&result.status==bc2::BodyReadStatus::Layout);++groups;}
    {Fixture f;f.denyMap=true;CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot);++groups;}
    {Fixture f;f.Byte(Fixture::enumMeta+13,65);CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot&&f.maxRead<=4096);++groups;}
    {Fixture f;f.Word(Fixture::switching+0x1c,Fixture::map+129*24);CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot&&f.maxRead<=4096);++groups;}
    {Fixture f;std::fill_n(f.bytes.begin()+Fixture::strings+64-0x10000,96,std::byte{'x'});CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot);++groups;}
    {Fixture f;auto result=bc2::ReadBodyInventory(f.Memory(),f.owner,true);CHECK(result.snapshot&&result.reads<150&&f.maxRead<=4096);++groups;}
    {Fixture f;f.Word(Fixture::player+0xc68,Fixture::b);CHECK(!bc2::ReadBodyInventory(f.Memory(),f.owner,true).snapshot);++groups;}
    {Fixture f;f.Word(Fixture::map+512,9);auto r=bc2::ReadBodyInventory(f.Memory(),f.owner,true);CHECK(r.snapshot&&!bc2::ResolveBodyDraw(*r.snapshot,Fixture::b));++groups;}
    {Fixture f;f.Word(Fixture::map+16,38);auto r=bc2::ReadBodyInventory(f.Memory(),f.owner,true);CHECK(r.snapshot&&!bc2::ResolveBodyDraw(*r.snapshot,Fixture::b));++groups;}
    {Fixture f;f.Word(Fixture::map+12,Fixture::map+520);f.Word(Fixture::map+512,0);f.Word(Fixture::map+516,1);auto r=bc2::ReadBodyInventory(f.Memory(),f.owner,true);CHECK(r.snapshot&&!bc2::ResolveBodyDraw(*r.snapshot,Fixture::b));++groups;}
    {Run r;CHECK(!r.Tick().command);r.Advance();CHECK(!r.Tick().command);r.Advance(1);auto request=r.Tick();CHECK(request.command&&request.command->targetWeapon==Fixture::b&&request.blockFire);CHECK(!r.Tick().command);
        r.f.Select(1);r.s.owner=r.f.owner;r.s.gun={Fixture::b,100};r.s.hand.owner.equipGeneration=100;r.Advance(1);r.Gun();auto ack=r.Tick();CHECK(ack.committed&&!ack.pending&&!ack.blockFire);++groups;}
    {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);const auto request=r.Tick();CHECK(request.command&&request.command->action==7&&request.blockFire);
        std::array<std::byte,bc2::InputBytes> cache{};float heldFire=.85f;std::memcpy(cache.data()+8+4*8,&heldFire,4);unsigned grenade=1u<<6;std::memcpy(cache.data()+0x9c,&grenade,4);const auto original=cache;
        interaction::ActionOutput action;action.active=true;action.owner=Fixture::soldier;bc2::InputOverride patch;CHECK(patch.Apply(cache,action,false,true));
        float fire=1,cycle=0;std::memcpy(&fire,cache.data()+8+4*8,4);std::memcpy(&cycle,cache.data()+8+4*7,4);std::memcpy(&grenade,cache.data()+0x9c,4);
        CHECK(fire==0&&cycle==1&&!(grenade&(1u<<6)));CHECK(patch.Restore()&&cache==original);++groups;}
    {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);++r.s.visible->owner.equipGeneration;CHECK(!r.Tick().command);r.Advance(0);r.Tick();r.Advance(1);r.s.visible->deadlineNs=r.s.hand.nowNs;CHECK(!r.Tick().command);++groups;}
    {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);r.s.visible.reset();CHECK(!r.Tick().command);r.Advance(1);CHECK(!r.Tick().command);r.Advance(0);r.Tick();r.Advance(1);CHECK(r.Tick().command);++groups;}
    {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);r.s.reloadBusy=true;CHECK(!r.Tick().command);r.Advance(1);r.s.reloadBusy=false;CHECK(!r.Tick().command);++groups;}
    {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);CHECK(r.Tick().command);r.Advance(1);r.s.input.focused=false;auto out=r.Tick();CHECK(out.cancelled&&!out.command);++groups;}
    {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);CHECK(r.Tick().command);r.f.Word(Fixture::bd+0x84,4);r.Advance(1);CHECK(r.Tick().cancelled);++groups;}
    {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);r.hands.Acquire(r.s.hand,{r.s.hand.owner,interaction::InteractionHand::Left,interaction::HandClaimKind::AmmoObject,{999,1},{{998,1},r.s.hand.sequence,r.s.hand.deadlineNs,true},++r.intent,0});CHECK(!r.Tick().command);++groups;}
    {Run r;bc2::Bc2BodyInventory disabled;r.Tick();r.Advance(1);CHECK(!disabled.Tick(r.f.Memory(),r.s,r.hands,r.intent).command);++groups;}
    {Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);CHECK(r.Tick().command);r.f.Word(Fixture::array+4,Fixture::b+128);r.f.Word(Fixture::b+128+4,Fixture::bd);r.Advance(1);CHECK(r.Tick().cancelled);r.Advance(1);CHECK(!r.Tick().command);r.Advance(0);r.Tick();r.Advance(1);auto replacement=r.Tick();CHECK(replacement.command&&replacement.command->targetWeapon==Fixture::b+128);++groups;}
    {constexpr unsigned valid=0x10000000u|0x2197800u|9;CHECK(bc2::ValidBodyInventoryConfig(valid)&&bc2::ValidBodyInventoryConfig(valid|0x400u));
        for(unsigned bit:{0x400000u,0x800000u,0x1000000u,0x4000000u,0x8000000u,0x8000u,0x20000u,0x40000u})CHECK(!bc2::ValidBodyInventoryConfig(valid|bit));
        for(unsigned bit:{0x800u,0x1000u,0x2000u,0x4000u,0x10000u,0x80000u,0x100000u,0x2000000u})CHECK(!bc2::ValidBodyInventoryConfig(valid&~bit));
        CHECK(!bc2::ValidBodyInventoryConfig(valid^1u));++groups;}
    {Run r;const auto p=interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Right);CHECK(p&&interaction::BodyAnchorContains(interaction::BodyAnchorConfig{}.shoulders[1],*p));
        r.s.input.head.orientation={0,.70710678f,0,.70710678f};const auto turn=interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Right);CHECK(turn&&Near(turn->values[3][0],p->values[3][0])&&Near(turn->values[3][2],p->values[3][2]));
        r.s.input.referenceHead.orientation=r.s.input.head.orientation;r.s.input.hands[1].grip.position={.18f,1.58f,-.20f};const auto recenter=interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Right);CHECK(recenter&&interaction::BodyAnchorContains(interaction::BodyAnchorConfig{}.shoulders[1],*recenter));++groups;}
    {Run r;r.s.input.hands[0].gripTracked=true;r.s.input.hands[0].grip.position={-.16f,1.45f,-.10f};const auto chest=interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Left);CHECK(chest&&interaction::BodyAnchorContains(interaction::BodyAnchorConfig{}.chest,*chest));const auto c=interaction::ChestAmmoSupply();CHECK(Near(c.pouchCenterMeters[1],-.25f));++groups;}
    {HolsterRun h;CHECK(h.Empty());CHECK(h.suppressions==2&&h.out.blockWeaponActions);
        CHECK(bc2::BodyFreeRightEvidenceCurrent(*h.out.freeRight,h.r.s.hand.nowNs));h.Step(0);h.Step(1);CHECK(h.out.select&&h.out.select->id==Fixture::a);
        h.Step(1);h.Step(1);CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(interaction::InteractionHand::Right));++groups;}
    {HolsterRun h;h.Step(1);h.allowReadback=false;h.Step(1);h.Step(1);CHECK(!h.out.freeRight&&h.r.hands.Current(interaction::InteractionHand::Right)&&h.out.blockWeaponActions);++groups;}
    {HolsterRun h;CHECK(h.Empty());h.Step(0);h.r.s.input.hands[1].grip.position.x=.20f;h.Step(1);CHECK(h.out.select&&h.out.select->id==Fixture::b);
        auto inventory=bc2::ReadBodyInventory(h.r.f.Memory(),h.r.s.owner,true);CHECK(inventory.snapshot);auto route=bc2::ResolveBodyDraw(*inventory.snapshot,unsigned(h.out.select->id));CHECK(route&&route->action==7);
        h.r.f.Select(1);h.r.s.owner=h.r.f.owner;++h.r.s.hand.owner.equipGeneration;h.r.s.gun={Fixture::b,h.r.s.hand.owner.equipGeneration};
        h.Step(1);CHECK(!h.r.hands.Current(interaction::InteractionHand::Right));h.Step(1);CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(interaction::InteractionHand::Right));++groups;}
    {HolsterRun h;h.r.s.input.hands[1].grip.position.x=.20f;h.Step(1);CHECK(h.out.ordinaryDraw.command&&h.out.ordinaryDraw.command->targetWeapon==Fixture::b);++groups;}
    {HolsterRun h;h.r.s.reloadBusy=true;h.Step(1);CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&!h.out.blockWeaponActions&&!h.out.freeRight);
        h.r.s.reloadBusy=false;h.r.s.cancel=true;h.Step(1);CHECK(!h.out.blockWeaponActions&&h.r.hands.Current(interaction::InteractionHand::Right));++groups;}
    {HolsterRun h;CHECK(h.Empty());const auto old=*h.out.freeRight;
        // Picking up a different carried item changes the inventory revision;
        // empty-hand evidence must retire before any new shoulder gesture.
        constexpr auto replacement=Fixture::b+128;
        h.r.f.Word(Fixture::array+4,replacement);h.r.f.Word(replacement+4,Fixture::bd);h.Step(0);
        CHECK(!h.out.freeRight&&h.out.blockWeaponActions&&!h.out.select&&!h.out.ordinaryDraw.command);
        CHECK(!bc2::BodyFreeRightRenderCurrent(old,nullptr,&h.out.visibility,h.r.s.hand.nowNs));
        h.Step(0);CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(interaction::InteractionHand::Right));
        CHECK(!h.r.adapter.AssignedSlot(Fixture::b)&&h.r.adapter.AssignedSlot(replacement));
        h.r.s.input.hands[1].grip.position.x=.20f;h.Step(1);
        CHECK(h.out.ordinaryDraw.command&&h.out.ordinaryDraw.command->targetWeapon==replacement);++groups;}
    {HolsterRun h;CHECK(h.Empty());const auto old=*h.out.freeRight;
        // An unexpected native weapon switch is not an acknowledgement of a
        // previous hidden item. Only a fresh show receipt reacquires GunHold.
        h.r.f.Select(1);h.r.s.owner=h.r.f.owner;++h.r.s.hand.owner.equipGeneration;
        h.r.s.gun={Fixture::b,h.r.s.hand.owner.equipGeneration};h.Step(0);
        CHECK(!h.out.freeRight&&!h.out.select&&h.out.blockWeaponActions&&!h.r.hands.Current(interaction::InteractionHand::Right));
        CHECK(h.out.visibility.enabled&&!h.out.visibility.hide&&h.out.visibility.nativeOwner.weapon==Fixture::b);
        CHECK(!bc2::BodyFreeRightRenderCurrent(old,nullptr,&h.out.visibility,h.r.s.hand.nowNs));
        h.Step(0);CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(interaction::InteractionHand::Right)->token.item.id==Fixture::b);++groups;}
    {Run r;r.s.input.hands[0].gripTracked=true;r.s.input.hands[0].grip.position={-.23f,1.15f,-.02f};
        const auto c=interaction::ChestAmmoSupply();CHECK(c.alternateContact);
        const auto original=interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Left);CHECK(original);
        for(unsigned n=0;n<3;++n)CHECK(Near(original->values[3][n],c.alternateContact->centerMeters[n]));
        r.s.input.head.orientation={0,.70710678f,0,.70710678f};
        const auto looked=interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Left);CHECK(looked);
        for(unsigned n=0;n<3;++n)CHECK(Near(looked->values[3][n],original->values[3][n]));
        r.s.input.referenceHead.orientation=r.s.input.head.orientation;
        const auto oldWorld=interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Left);CHECK(oldWorld);
        CHECK(!Near(oldWorld->values[3][0],original->values[3][0])); // Old world point is not retained across recenter.
        r.s.input.hands[0].grip.position={-.02f,1.15f,.23f};
        const auto recentered=interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Left);CHECK(recentered);
        for(unsigned n=0;n<3;++n)CHECK(Near(recentered->values[3][n],c.alternateContact->centerMeters[n]));
        r.s.input.focused=false;CHECK(!interaction::BodyAnchorHandPose(r.s.input,interaction::InteractionHand::Left));++groups;}
    {HolsterRun h;CHECK(h.Empty());const auto old=*h.out.freeRight;
        // Production input-unavailable resets both policy observation and hand
        // claims. Reconnect must preserve the committed intent, not old proofs.
        h.holster.Invalidate(true);h.r.adapter.Cancel();h.r.hands.Reset();
        ++h.r.f.owner.space;h.r.s.owner=h.r.f.owner;h.r.s.hand.owner.space=h.r.s.owner.space;h.r.s.input.spaceGeneration=h.r.s.owner.space;
        h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&!h.out.freeRight&&!h.out.select);
        CHECK(!bc2::BodyFreeRightCurrent(old,h.Sample(),h.r.hands));h.Step(1);h.Step(1);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight&&!h.out.inventory.committedRequest);
        h.Step(1);CHECK(!h.out.select&&!h.r.hands.Current(interaction::InteractionHand::Right));
        h.Step(0);h.Step(1);CHECK(h.out.select);h.Step(1);h.Step(1);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(interaction::InteractionHand::Right));++groups;}
    {HolsterRun h;CHECK(h.Empty());const auto old=*h.out.freeRight;
        h.holster.Invalidate(true);h.r.adapter.Cancel();h.r.hands.Reset();h.Step(1);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&!h.out.freeRight&&!h.out.select);
        h.Step(1);h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);
        CHECK(h.out.freeRight->input.owner.space==old.input.owner.space&&h.out.freeRight->visibility.request>old.visibility.request);
        CHECK(!h.out.inventory.committedRequest&&!h.r.hands.Current(interaction::InteractionHand::Right));++groups;}
    {HolsterRun h;CHECK(h.Empty());const auto old=*h.out.freeRight;
        // A same-pointer weapon replacement can change native persistence
        // without changing class/slot preferences. Recenter cannot hide it.
        h.r.f.Word(Fixture::ad+0x64,0x34567);
        ++h.r.f.owner.space;h.r.s.owner=h.r.f.owner;h.r.s.hand.owner.space=h.r.s.owner.space;h.r.s.input.spaceGeneration=h.r.s.owner.space;
        h.Step(0);CHECK(h.out.phase==bc2::BodyHolsterPhase::ShowPending&&!h.out.freeRight&&!h.out.select);
        CHECK(h.out.visibility.enabled&&!h.out.visibility.hide);
        CHECK(!bc2::BodyFreeRightEvidenceCurrent(old,h.r.s.hand.deadlineNs));h.Step(0);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&h.r.hands.Current(interaction::InteractionHand::Right));++groups;}
    {HolsterRun h;
        h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::Held);
        const auto outside=h.r.s.input.generation;
        h.r.s.input.hands[1].grip.position={-.20f,1.58f,.18f};h.Step(1);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::HidePending&&h.out.inventory.request);
        std::ostringstream report;h.r.adapter.Report(report);const auto text=report.str();
        CHECK(text.find("\"input\":"+std::to_string(outside))!=std::string::npos);
        const auto row=text.find("\"input\":"+std::to_string(h.r.s.input.generation),text.find("\"gestures\":"));CHECK(row!=std::string::npos);
        const auto body=text.substr(row,text.find('}',row)-row);
        CHECK(body.find("\"slot\":1")!=std::string::npos&&body.find("\"flags\":313")!=std::string::npos);
        CHECK(body.find("\"squeeze\":1")!=std::string::npos&&body.find("\"operation\":1")!=std::string::npos);
        CHECK(body.find("\"observed_ns\":"+std::to_string(h.r.s.hand.observedNs))!=std::string::npos);
        std::ostringstream before;h.r.adapter.Report(before);h.Tick();std::ostringstream after;h.r.adapter.Report(after);
        const auto a=before.str().substr(before.str().find("\"gestures\":"));const auto b=after.str().substr(after.str().find("\"gestures\":"));CHECK(a==b);
        h.Step(1);h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&h.out.freeRight);
        for(unsigned n=0;n<5;++n){h.Step(1);CHECK(h.out.phase==bc2::BodyHolsterPhase::Empty&&!h.out.inventory.request);}++groups;}
    {HolsterRun h;h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};
        for(unsigned n=0;n<300;++n)h.Step(n%2?1.f:0.f);
        std::ostringstream report;h.r.adapter.Report(report);const auto text=report.str();
        const auto start=text.find("\"gestures\":[");CHECK(start!=std::string::npos);
        unsigned rows=0;for(auto at=text.find("\"point\":[",start);at!=std::string::npos;at=text.find("\"point\":[",at+1))++rows;
        CHECK(rows==128&&text.find("\"gestures_dropped\":0")==std::string::npos);
        CHECK(text.find("\"input\":"+std::to_string(h.r.s.input.generation),start)!=std::string::npos);
        CHECK(h.out.phase==bc2::BodyHolsterPhase::Held&&!h.out.inventory.request);++groups;}
    std::printf("Body inventory adapter: %u groups passed\n",groups);return 0;
}




