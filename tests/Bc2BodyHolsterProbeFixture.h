#pragma once
#include "Bc2BodyHolster.h"
#include <cstring>
using namespace fvr;using namespace bc2;using namespace interaction;
namespace body_probe_test {
constexpr unsigned A=0x40000,B=0x50000;
struct Fixture {
    Bc2BodyHolster adapter{{true,true,1}};BodyInventory policy;HandInteraction hands;BodyHolsterSample s;BodyHolsterResult out;
    std::vector<BodyInventoryItem> items{{{A,1},{1,2},2,100},{{B,2},{2,1},2,50}};
    std::uint64_t intent=0;std::array<std::byte,InputBytes> cache{};bool ownerCurrent=true,xm8=false;
    explicit Fixture(bool xm8Profile=false):xm8(xm8Profile){s.nativeOwner={0x10000,0x20000,0x30000,A,4,7,9};
        s.hand={{(std::uint64_t(0x30000)<<32)|0x20000,4,99,9},1,1000000000,1100000000,1000000000,true,{true,true}};
        s.gun={xm8?0xc0000u:A,99};s.nativeTick=1;s.cache=0x60000;s.body.focused=s.body.handTracked=true;
        s.body.inventory={{{(std::uint64_t(0x30000)<<32)|0x20000},4,9},1,1,s.hand.nowNs,items,items[0].key};
        s.body.nowNs=s.hand.nowNs;Metadata();hands.Update(s.hand);
        hands.Acquire(s.hand,{s.hand.owner,InteractionHand::Right,HandClaimKind::GunHold,s.gun,{{1,99},1,s.hand.deadlineNs,true},++intent,0});
        if(xm8){adapter=Bc2BodyHolster{};adapter.AdmitDiagnostic(1000000000,16000000000ll,BodyHolsterDiagnosticProfile::ScopedXm8);}
        Tick();Advance();Tick();}
    void Metadata(){auto m=std::make_shared<SelectedMeshesSnapshot>();m->owner=s.nativeOwner;m->sequence=s.hand.sequence;
        m->observedNs=s.hand.observedNs;m->deadlineNs=s.hand.deadlineNs;m->stateCount=1;m->soleConfiguredArray=0x70000;m->weaponData=0x80000;
        std::memcpy(m->weaponName.data(),"SPAS12_sp",sizeof("SPAS12_sp"));m->states[0].array=0x70000;m->states[0].count=1;
        auto& mesh=m->states[0].meshes[0];mesh.kind=SelectedMeshKind::Spas12;mesh.address=0x90000;mesh.typeInfo=0xa0000;mesh.namePointer=0xb0000;
        constexpr char path[]="Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh";std::memcpy(mesh.assetPath.data(),path,sizeof(path));
        if(xm8){std::memcpy(m->weaponName.data(),"XM8_sp_s",sizeof("XM8_sp_s"));m->states[0].count=2;
            mesh.kind=SelectedMeshKind::Xm8;constexpr char xm8Path[]="Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh";
            std::memcpy(mesh.assetPath.data(),xm8Path,sizeof(xm8Path));auto& optic=m->states[0].meshes[1];
            optic.kind=SelectedMeshKind::Acog4x;optic.address=0x91000;optic.typeInfo=0xa1000;optic.namePointer=0xb1000;
            constexpr char opticPath[]="Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh";std::memcpy(optic.assetPath.data(),opticPath,sizeof(opticPath));}
        s.selected=m;
        s.ordinary=BodyVisibleRig{s.nativeOwner,s.hand.sequence,s.hand.observedNs,s.hand.deadlineNs};}
    void Advance(){++s.hand.sequence;s.hand.observedNs+=10000000;s.hand.deadlineNs+=10000000;s.hand.nowNs=s.hand.observedNs;
        ++s.nativeTick;++s.body.inventory.sequence;s.body.inventory.observedNs=s.hand.nowNs;s.body.nowNs=s.hand.nowNs;s.body.intent={};
        s.suppression.reset();s.visibility.reset();Metadata();hands.Update(s.hand);
        if(const auto gun=hands.Current(InteractionHand::Right))hands.Renew(s.hand,gun->token,{{1,s.hand.owner.equipGeneration},s.hand.sequence,s.hand.deadlineNs,true});}
    void Tick(){out=adapter.Tick(policy,s,hands,intent);}
    bool Suppress(){auto r=adapter.Demand(s);if(!r)return false;HolsterInputOverride input;
        if(!input.Apply(cache,*r,{this,[](void* c,const HolsterSuppressionRequest&)noexcept{return static_cast<Fixture*>(c)->ownerCurrent;}}))return false;
        s.suppression=input.Commit();return s.suppression.has_value();}
    void Render(const WeaponVisibilityRequest& r){if(!r.enabled)return;auto p=std::make_shared<WeaponVisibilityPlan>();
        p->reason=WeaponVisibilityReason::None;p->rig.soldier=r.nativeOwner.soldier;p->rig.weak=r.nativeOwner.weak;p->rig.count=147;
        p->nativeOwner=r.nativeOwner;p->request=r.request;p->hidden=r.hide;p->inputSequence=r.input.sequence;
        p->physicalEquipGeneration=r.input.owner.equipGeneration;p->meshSequence=r.selected->sequence;p->selected=r.selected;
        p->observedNs=r.input.observedNs;p->deadlineNs=std::min(r.input.deadlineNs,r.selected->deadlineNs);p->inputDeadlineNs=r.input.deadlineNs;
        s.visibility=WeaponVisibilityReceipt{r.nativeOwner,p->rig,r.request,r.input.sequence,r.input.owner.equipGeneration,100+r.input.sequence,s.hand.nowNs,p->deadlineNs,3,r.hide,p};}
    void Cycle(bool render=true,bool suppress=true){const auto request=out.visibility;Advance();if(render)Render(request);if(suppress)Suppress();Tick();}
    bool Empty(){Advance();s.body.intent={++intent,BodyInventoryOperation::Holster,1,items[0].key};Tick();
        if(out.phase!=BodyHolsterPhase::HidePending||out.visibility.enabled)return false;Cycle();Cycle();
        return out.phase==BodyHolsterPhase::Empty&&out.freeRight&&out.inventory.emptyHands&&!hands.Current(InteractionHand::Right);}
};
}
