#include "Test.h"
#include "Bc2BodyHolster.h"
#include "Bc2BodyHolsterObservation.h"
#include "Bc2BodyHolsterLifecycle.h"
#include <bit>
#include <cstring>
#include <sstream>
#include "../src/platform/windows/BodyCrossDrawProbe.h"
using namespace fvr;using namespace bc2;using namespace interaction;
namespace {
constexpr unsigned A=0x40000,B=0x50000;
void Word(std::span<std::byte> b,unsigned offset,unsigned value){std::memcpy(b.data()+offset,&value,4);}
unsigned Word(std::span<const std::byte> b,unsigned offset){unsigned value;std::memcpy(&value,b.data()+offset,4);return value;}
struct Fixture {
    Bc2BodyHolster adapter{{true,true,1}};BodyInventory policy;HandInteraction hands;BodyHolsterSample s;BodyHolsterResult out;
    std::vector<BodyInventoryItem> items{{{A,1},{1,2},2,100},{{B,2},{2,1},2,50}};
    std::uint64_t intent=0;std::array<std::byte,InputBytes> cache{};bool ownerCurrent=true,xm8=false;
    Fixture(){s.nativeOwner={0x10000,0x20000,0x30000,A,4,7,9};
        s.hand={{(std::uint64_t(0x30000)<<32)|0x20000,4,99,9},1,1000000000,1100000000,1000000000,true,{true,true}};
        s.gun={A,99};s.nativeTick=1;s.cache=0x60000;s.body.focused=s.body.handTracked=true;
        s.body.inventory={{{(std::uint64_t(0x30000)<<32)|0x20000},4,9},1,1,s.hand.nowNs,items,items[0].key};
        s.body.nowNs=s.hand.nowNs;BodyCarriedIdentity carried;carried.inventory=0xc0000;carried.switching=0xd0000;carried.count=2;
        carried.items[0]={A,0xe0000,11,1,0};carried.items[1]={B,0xf0000,12,0,1};s.carried=carried;Metadata();hands.Update(s.hand);
        hands.Acquire(s.hand,{s.hand.owner,InteractionHand::Right,HandClaimKind::GunHold,s.gun,{{1,99},1,s.hand.deadlineNs,true},++intent,0});
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
        p->observedNs=r.input.observedNs;p->deadlineNs=std::min(r.input.deadlineNs,r.selected->deadlineNs);if(r.authorizationDeadlineNs)p->deadlineNs=std::min(p->deadlineNs,r.authorizationDeadlineNs);p->inputDeadlineNs=r.input.deadlineNs;
        s.visibility=WeaponVisibilityReceipt{r.nativeOwner,p->rig,r.request,r.input.sequence,r.input.owner.equipGeneration,100+r.input.sequence,s.hand.nowNs,p->deadlineNs,3,r.hide,p};}
    void Cycle(bool render=true,bool suppress=true){const auto request=out.visibility;Advance();if(render)Render(request);if(suppress)Suppress();Tick();}
    bool Empty(){Advance();s.body.intent={++intent,BodyInventoryOperation::Holster,1,items[0].key};Tick();
        if(out.phase!=BodyHolsterPhase::HidePending||out.visibility.enabled)return false;Cycle();Cycle();
        return out.phase==BodyHolsterPhase::Empty&&out.freeRight&&out.inventory.emptyHands&&!hands.Current(InteractionHand::Right);}
};
void Rebase(Fixture& f){
    ++f.s.nativeOwner.space;f.s.hand.owner.space=f.s.nativeOwner.space;f.s.body.inventory.owner.space=f.s.nativeOwner.space;
    ++f.s.body.inventory.revision;for(auto& item:f.items)++item.key.generation;f.s.body.inventory.selected=f.items[0].key;
}
void ExchangeUnsupported(Fixture& f,unsigned relocation=0){
    f.Advance();++f.s.nativeOwner.equipGeneration;++f.s.hand.owner.equipGeneration;
    f.s.gun={A,f.s.hand.owner.equipGeneration};++f.s.body.inventory.revision;++f.items[0].key.generation;
    f.s.body.inventory.selected=f.items[0].key;f.s.carried->items[0].data=0x120000;
    f.s.carried->items[0].persistence=0x130000;f.s.carried->items[0].category=0;
    f.s.equipment={A,0x120000,0x130000,{}};std::memcpy(f.s.equipment.asset.data(),"AEK971_sp",sizeof("AEK971_sp"));
    if(relocation){f.s.nativeOwner.weapon=0x270000;f.s.equipment.weapon=f.s.nativeOwner.weapon;
        f.s.carried->items[0].weapon=f.s.nativeOwner.weapon;f.items[0].key.id=f.s.nativeOwner.weapon;
        f.s.body.inventory.selected=f.items[0].key;f.s.gun={f.s.nativeOwner.weapon,f.s.hand.owner.equipGeneration};
        if(relocation==2){std::swap(f.s.carried->items[0],f.s.carried->items[1]);
            f.s.carried->items[0].slot=0;f.s.carried->items[1].slot=1;}}
    f.Metadata();auto meshes=std::make_shared<SelectedMeshesSnapshot>(*f.s.selected);
    std::memcpy(meshes->weaponName.data(),"AEK971_sp",sizeof("AEK971_sp"));meshes->weaponData=f.s.equipment.data;
    meshes->states[0].count=0;f.s.selected=meshes;f.s.triggerNeutral=false;
    f.hands.Update(f.s.hand);f.Tick();
}
void ContinueUnsupported(Fixture& f,bool pair=true,bool neutral=true){
    const auto request=f.out.ordinaryRecovery;const auto mesh=f.s.selected;
    f.Advance();auto next=std::make_shared<SelectedMeshesSnapshot>(*mesh);
    next->sequence=f.s.hand.sequence;next->observedNs=f.s.hand.observedNs;next->deadlineNs=f.s.hand.deadlineNs;
    f.s.selected=next;f.s.triggerNeutral=neutral;f.s.ordinaryPair.reset();
    if(pair&&request){RigIdentity rig{};rig.soldier=f.s.nativeOwner.soldier;rig.weak=f.s.nativeOwner.weak;
        rig.pose=0x180000;rig.count=147;
        f.s.ordinaryPair=OrdinaryEquipmentPair{*request,rig,100+request->input.sequence,f.s.hand.nowNs,request->input.deadlineNs,3};}
    f.Suppress();f.Tick();
}
int UnsupportedExchangeRequiresNewOrdinaryPair(){
    Fixture f;CHECK(f.Empty());const auto old=*f.out.freeRight;ExchangeUnsupported(f);
    CHECK(f.out.phase==BodyHolsterPhase::Recovering&&f.out.ordinaryRecovery&&!f.out.visibility.enabled);
    CHECK(f.out.blockWeaponActions&&!f.hands.Current(InteractionHand::Right)&&!f.out.select);
    CHECK(!f.adapter.AcceptsProfile(f.s.nativeOwner,f.s.selected,f.s.hand.nowNs));
    const auto request=f.out.ordinaryRecovery->request;CHECK(request>old.visibility.request);
    ContinueUnsupported(f,false);CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRetired);
    ContinueUnsupported(f,true,false);CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRetired);
    ContinueUnsupported(f);CHECK(f.out.phase==BodyHolsterPhase::Held&&f.out.ordinaryRetired&&!f.out.blockWeaponActions);
    CHECK(!f.out.freeRight&&!f.out.visibility.enabled&&!f.out.select&&!f.out.inventory.committedRequest);
    CHECK(f.out.inventory.slots.size()==2&&f.out.inventory.held==f.items[0].key);
    const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun&&gun->token.item==f.s.gun&&gun->token.owner==f.s.hand.owner);
    CHECK(!f.adapter.Demand(f.s)&&!f.adapter.AcceptsProfile(f.s.nativeOwner,f.s.selected,f.s.hand.nowNs));
    CHECK(!BodyFreeRightCurrent(old,f.s,f.hands));return 0;
}
int NativeExchangeReconcilesRebuiltCarriedInventory(){
    for(unsigned mode=0;mode<5;++mode){Fixture f;CHECK(f.Empty());const auto stale=*f.out.freeRight;
        // Native-equivalent snapshot mutation. Same pointer or different pointer,
        // attachment replacement, container reconstruction and coherent capacity
        // expansion are synthetic variants; the old headset trace lacked these
        // full carried fields. All need the same REAL ordinary consumer receipts.
        f.s.carried->items[1].data=0x210000;f.s.carried->items[1].persistence=0x220000;
        if(mode==1){f.s.carried->inventory=0x230000;f.s.carried->switching=0x240000;}
        if(mode==2){f.s.carried->count=3;f.s.carried->items[2]={0x250000,0x260000,0x130000,5,2};}
        ExchangeUnsupported(f,mode>=3?mode-2:0);
        CHECK(f.out.ordinaryRecovery&&f.out.blockWeaponActions&&!f.out.visibility.enabled&&!f.out.freeRight);
        CHECK(!f.hands.Current(InteractionHand::Right)&&!BodyFreeRightCurrent(stale,f.s,f.hands));
        ContinueUnsupported(f,false);CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRetired);
        ContinueUnsupported(f,true,false);CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRetired);
        ContinueUnsupported(f);CHECK(f.out.ordinaryRetired&&f.out.phase==BodyHolsterPhase::Held&&!f.out.blockWeaponActions);
        const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun&&gun->token.owner==f.s.hand.owner&&gun->token.item==f.s.gun);
        CHECK(!f.adapter.Demand(f.s)&&!f.adapter.AcceptsProfile(f.s.nativeOwner,f.s.selected,f.s.hand.nowNs));
    }return 0;
}
int CarriedMutationInvalidatesPendingOrdinaryPair(){
    for(unsigned change=0;change<5;++change){Fixture f;CHECK(f.Empty());ExchangeUnsupported(f);
        ContinueUnsupported(f,true,false);const auto oldPair=*f.s.ordinaryPair;const auto oldRequest=f.adapter.RequestId();
        if(change==0)++f.s.carried->items[1].persistence;
        if(change==1)++f.s.carried->inventory;
        if(change==2)++f.s.carried->switching;
        if(change==3){f.s.carried->count=3;f.s.carried->items[2]={0x280000,0x290000,0,5,2};}
        if(change==4){std::swap(f.s.carried->items[0],f.s.carried->items[1]);f.s.carried->items[0].slot=0;f.s.carried->items[1].slot=1;}
        f.s.triggerNeutral=true;f.Tick();
        CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRetired&&!f.hands.Current(InteractionHand::Right));
        CHECK(f.out.ordinaryRecovery&&f.adapter.RequestId()>oldRequest);
        f.s.ordinaryPair=oldPair;f.Suppress();f.Tick();CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRetired);
        ContinueUnsupported(f);CHECK(f.out.ordinaryRetired&&f.hands.Current(InteractionHand::Right));
    }return 0;
}
int OrdinaryRetirementRejectsStaleOrForgedPair(){
    for(unsigned bad=0;bad<12;++bad){Fixture f;CHECK(f.Empty());ExchangeUnsupported(f);
        ContinueUnsupported(f,true,false);CHECK(f.s.ordinaryPair&&f.out.ordinaryRecovery);
        auto p=*f.s.ordinaryPair;f.s.triggerNeutral=true;
        if(bad==0)p.copyMask=1;
        if(bad==1)p.copyMask=2;
        if(bad==2)--p.source.request;
        if(bad==3)--p.source.input.owner.equipGeneration;
        if(bad==4)++p.source.equipment.data;
        if(bad==5)++p.source.equipment.persistence;
        if(bad==6)p.source.equipment.asset[0]='X';
        if(bad==7)p.deadlineNs=f.s.hand.nowNs;
        if(bad==8)p.observedNs=f.s.hand.nowNs+1;
        if(bad==9)++p.rig.weak;
        if(bad==10)p.drawSerial=0;
        if(bad==11)p.source.input.sequence=f.s.hand.sequence+1;
        f.s.ordinaryPair=p;f.Tick();CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRetired&&!f.hands.Current(InteractionHand::Right));
    }return 0;
}
int OrdinaryRetirementIsOnlyExactSelectedSlotReplacement(){
    for(unsigned bad=0;bad<9;++bad){Fixture f;CHECK(f.Empty());
        if(bad==0)f.s.carried->inventory=0;
        if(bad==1)f.s.carried->switching=0;
        if(bad==2)f.s.carried->items[1].weapon=A;
        if(bad==3)++f.s.nativeOwner.player;
        if(bad==4)++f.s.nativeOwner.actorGeneration;
        if(bad==5)++f.s.nativeOwner.space;
        if(bad==6)++f.s.carried->count;
        if(bad==7)--f.s.nativeOwner.equipGeneration;
        if(bad==8)--f.s.hand.owner.equipGeneration;
        ExchangeUnsupported(f);CHECK(!f.out.ordinaryRecovery&&!f.out.ordinaryRetired&&f.out.blockWeaponActions);
        CHECK(!f.hands.Current(InteractionHand::Right)&&!f.out.visibility.enabled);
    }return 0;
}
int OrdinaryRetirementCancellationCannotReplayPair(){
    Fixture f;CHECK(f.Empty());ExchangeUnsupported(f);ContinueUnsupported(f,true,false);
    const auto old=*f.s.ordinaryPair;const auto request=f.adapter.RequestId();
    f.s.cancel=true;f.Tick();CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRecovery);
    f.s.cancel=false;f.s.triggerNeutral=true;f.Tick();CHECK(f.out.ordinaryRecovery&&f.adapter.RequestId()>request);
    f.s.ordinaryPair=old;f.Tick();CHECK(!f.out.ordinaryRetired&&!f.hands.Current(InteractionHand::Right));
    ContinueUnsupported(f);CHECK(f.out.ordinaryRetired);
    return 0;
}
int OrdinaryRetirementCannotStealLeftClaimOrSkipSuppression(){
    for(unsigned bad=0;bad<3;++bad){Fixture f;CHECK(f.Empty());ExchangeUnsupported(f);ContinueUnsupported(f,true,false);
        f.s.triggerNeutral=true;
        if(bad==0)f.s.suppression.reset();
        if(bad==1)f.s.ordinary.reset();
        if(bad==2){CHECK(f.hands.Acquire(f.s.hand,{f.s.hand.owner,InteractionHand::Left,HandClaimKind::AmmoObject,{123,1},
            {{3,1},f.s.hand.sequence,f.s.hand.deadlineNs,true},++f.intent,0}).accepted);}
        f.Tick();CHECK(f.out.blockWeaponActions&&!f.out.ordinaryRetired&&!f.hands.Current(InteractionHand::Right));
    }return 0;
}
int OrdinaryRetirementNeedsReleasedThenNewTrigger(){
    Fixture f;CHECK(f.Empty());ExchangeUnsupported(f);ControllerActions actions;
    InputFrame input;input.focused=input.headValid=true;input.spaceGeneration=f.s.nativeOwner.space;
    for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
    const auto action=[&](float trigger){input.generation=f.s.hand.sequence;input.predictedNs=f.s.hand.observedNs;
        input.hands[1].trigger=trigger;CHECK(ValidInput(input));return 0;};
    CHECK(!action(1));actions.Update(input,{f.s.nativeOwner.soldier,f.s.nativeOwner.equipGeneration,true,true},input.predictedNs);
    ContinueUnsupported(f,true,false);CHECK(f.out.blockWeaponActions);
    CHECK(!action(1));auto out=actions.Update(input,{f.s.nativeOwner.soldier,f.s.nativeOwner.equipGeneration,true,true},input.predictedNs);
    CHECK(!(out.held&Fire));
    ContinueUnsupported(f,true,true);CHECK(f.out.ordinaryRetired);
    CHECK(!action(0));out=actions.Update(input,{f.s.nativeOwner.soldier,f.s.nativeOwner.equipGeneration,true,true},input.predictedNs);CHECK(!(out.held&Fire));
    ++input.generation;input.predictedNs+=10000000;out=actions.Update(input,{f.s.nativeOwner.soldier,f.s.nativeOwner.equipGeneration,true,true},input.predictedNs);CHECK(!(out.held&Fire));
    ++input.generation;input.predictedNs+=10000000;input.hands[1].trigger=1;
    out=actions.Update(input,{f.s.nativeOwner.soldier,f.s.nativeOwner.equipGeneration,true,true},input.predictedNs);CHECK(out.active&&(out.held&Fire));return 0;
}

int RebaseKeepsCommittedEmptyIntent(){
    Fixture f;CHECK(f.Empty());const auto old=*f.out.freeRight;const auto request=f.adapter.RequestId();
    Rebase(f);f.Cycle();
    CHECK(f.out.phase==BodyHolsterPhase::HidePending); // Previously issued SHOW without any draw gesture.
    CHECK(f.adapter.RequestId()>request&&!f.out.freeRight&&!f.out.visibility.enabled&&!f.out.select);
    CHECK(f.out.blockWeaponActions&&!f.out.allowAutomaticGunHold&&!f.hands.Current(InteractionHand::Right));
    CHECK(!BodyFreeRightCurrent(old,f.s,f.hands));
    f.Cycle();CHECK(f.out.visibility.enabled&&f.out.visibility.hide&&!f.out.freeRight);
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&f.out.inventory.emptyHands);
    CHECK(!f.out.inventory.committedRequest&&!f.hands.Current(InteractionHand::Right));
    CHECK(f.out.freeRight->input.owner.space==f.s.nativeOwner.space&&f.out.freeRight->visibility.request>request);
    return 0;
}
int TrackingGapRebindRequiresNewEvidence(){
    Fixture f;CHECK(f.Empty());const auto old=*f.out.freeRight;f.adapter.Invalidate(true);f.policy.Update({});f.hands.Reset();
    Rebase(f);f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!f.out.freeRight);
    const auto request=f.adapter.RequestId();f.Cycle(false,false);
    CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!f.out.visibility.enabled&&!f.out.freeRight);
    f.Cycle(false);CHECK(f.out.visibility.enabled&&f.out.visibility.hide&&!f.out.freeRight);
    const auto fresh=f.out.visibility;f.Advance();f.Suppress();f.s.visibility=old.receipt;f.Tick();
    CHECK(!f.out.freeRight&&!f.out.inventory.committedRequest&&f.out.blockWeaponActions);
    f.Advance();f.Suppress();f.Render(fresh);f.s.visibility->verifiedCopyMask=1;f.Tick();CHECK(!f.out.freeRight);
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&!f.out.inventory.committedRequest);
    CHECK(f.out.freeRight->visibility.request==request&&!f.hands.Current(InteractionHand::Right));
    f.Cycle();f.Advance();f.Suppress();f.Render(f.out.visibility);
    f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,1,f.items[0].key};f.Tick();CHECK(f.out.select);
    f.Cycle();f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held&&f.hands.Current(InteractionHand::Right));return 0;
}
int RebaseCannotMigrateCancellationOrIdentity(){
    for(unsigned bad=0;bad<11;++bad){Fixture f;CHECK(f.Empty());Rebase(f);
        if(bad==0)f.adapter.Invalidate();
        if(bad==1)++f.s.nativeOwner.equipGeneration;
        if(bad==2){++f.s.hand.owner.equipGeneration;f.s.gun.generation=f.s.hand.owner.equipGeneration;}
        if(bad==3)++f.s.nativeOwner.player;
        if(bad==4){++f.items[1].key.id;++f.s.carried->items[1].weapon;} // A pickup coinciding with recenter is still a new inventory.
        if(bad==5){++f.items[1].priority;++f.s.carried->items[1].category;}
        if(bad==6)f.s.cancel=true;
        if(bad==7)f.s.reloadBusy=true;
        if(bad==8)++f.s.carried->items[0].data;
        if(bad==9)++f.s.carried->items[0].persistence;
        if(bad==10)++f.s.carried->inventory;
        f.Cycle();CHECK(!f.out.freeRight&&!f.out.select&&!f.hands.Current(InteractionHand::Right));
        CHECK(f.out.blockWeaponActions&&!f.out.allowAutomaticGunHold);
        CHECK(!f.out.visibility.enabled||!f.out.visibility.hide);
        if(bad==6||bad==7){f.s.cancel=false;f.s.reloadBusy=false;f.Cycle();}
        CHECK(f.out.phase==BodyHolsterPhase::ShowPending);f.Cycle();
        CHECK(f.out.phase==BodyHolsterPhase::Held&&f.hands.Current(InteractionHand::Right));
    }return 0;
}
int RebaseDuringRebindGetsAnotherRequest(){
    Fixture f;CHECK(f.Empty());Rebase(f);f.Cycle();f.Cycle();const auto first=f.out.visibility;CHECK(first.hide);
    Rebase(f);f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::HidePending&&f.adapter.RequestId()>first.request&&!f.out.freeRight);
    f.Cycle();f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight);
    CHECK(f.out.freeRight->input.owner.space==f.s.nativeOwner.space&&!f.out.inventory.committedRequest);return 0;
}
int IncompleteHolsterDoesNotBecomeCommittedOnRebase(){
    Fixture f;f.Advance();f.s.body.intent={++f.intent,BodyInventoryOperation::Holster,1,f.items[0].key};f.Tick();
    CHECK(f.out.phase==BodyHolsterPhase::HidePending);Rebase(f);f.Cycle();
    CHECK(f.out.phase==BodyHolsterPhase::ShowPending&&!f.out.freeRight&&!f.out.inventory.committedRequest);return 0;
}
int SecondGapWhileRebindingDoesNotDraw(){
    Fixture f;CHECK(f.Empty());Rebase(f);f.Cycle();f.Cycle();const auto unfinished=f.out.visibility;
    CHECK(unfinished.enabled&&unfinished.hide);
    f.adapter.Invalidate(true);f.policy.Update({});f.hands.Reset();
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!f.out.freeRight&&!f.out.visibility.enabled);
    CHECK(f.adapter.RequestId()>unfinished.request&&!f.hands.Current(InteractionHand::Right));
    f.Cycle();f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&!f.out.inventory.committedRequest);return 0;
}
int SameSpaceTrackingGapKeepsCommittedEmpty(){
    for(unsigned mode=0;mode<2;++mode){Fixture f;CHECK(f.Empty());const auto old=*f.out.freeRight;
        if(mode==0){f.adapter.Invalidate(true);f.policy.Update({});f.hands.Reset();}
        else {f.Advance();f.s.hand.focused=false;f.Tick();CHECK(f.out.phase==BodyHolsterPhase::Recovering&&!f.out.freeRight);f.s.hand.focused=true;}
        f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!f.out.freeRight&&!f.out.visibility.enabled);
        CHECK(f.adapter.RequestId()>old.visibility.request&&f.s.nativeOwner.space==old.input.owner.space);
        CHECK(!f.hands.Current(InteractionHand::Right)&&!f.out.select&&f.out.blockWeaponActions);
        f.Cycle(false,false);CHECK(!f.out.freeRight&&!f.out.visibility.enabled);
        f.Cycle(false);CHECK(f.out.visibility.enabled&&f.out.visibility.hide&&!f.out.freeRight);
        f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&!f.out.inventory.committedRequest);
        CHECK(f.out.freeRight->visibility.request>old.visibility.request&&!f.hands.Current(InteractionHand::Right));
    }return 0;
}
int FailedInputApplyCannotInventDraw(){
    for(unsigned mode=0;mode<2;++mode){Fixture f;CHECK(f.Empty());const auto old=*f.out.freeRight;
        if(mode)Rebase(f);
        BodyHolsterLifecycleLog log;
        log.Invalidate(f.adapter,BodyHolsterLifecycleReason::InputApplyRejected,false,f.s.hand,f.s.nativeOwner.equipGeneration);
        f.policy.Update({});f.hands.Reset();
        CHECK(f.adapter.Phase()==BodyHolsterPhase::Recovering);
        f.Cycle(false,false);CHECK(f.out.phase==BodyHolsterPhase::HidePending);
        CHECK(!f.out.freeRight&&!f.out.visibility.enabled&&!f.out.select&&!f.hands.Current(InteractionHand::Right));
        CHECK(f.out.blockWeaponActions&&f.adapter.RequestId()>old.visibility.request);
        f.Cycle(false,false);CHECK(!f.out.freeRight&&!f.out.visibility.enabled&&!f.out.select);
        f.Cycle(false);CHECK(f.out.visibility.enabled&&f.out.visibility.hide&&!f.out.freeRight);
        f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight);
        CHECK(!f.out.select&&!f.out.inventory.committedRequest&&!f.hands.Current(InteractionHand::Right));
    }return 0;
}
int SameSpaceExplicitCancellationStillShows(){
    Fixture f;CHECK(f.Empty());f.adapter.Invalidate();f.Cycle();
    CHECK(f.out.phase==BodyHolsterPhase::ShowPending&&f.out.visibility.enabled&&!f.out.visibility.hide&&!f.out.freeRight);
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held&&f.hands.Current(InteractionHand::Right));return 0;
}
// The fallback keeps this regression buildable against the original header;
// its first post-pause HidePending assertion must fail there.
template<class Adapter> bool NativeGap(Adapter& a,const ReloadStateOwner& before,const ReloadStateOwner& after){
    if constexpr(requires {a.ObserveNativeInputGap(before,after);})return a.ObserveNativeInputGap(before,after);
    else return false;
}
int PauseEpochNeedsFreshRehide(){
    for(unsigned mode=0;mode<2;++mode){Fixture f;CHECK(f.Empty());const auto old=*f.out.freeRight;
        const auto before=f.s.nativeOwner;auto after=before;++after.equipGeneration;
        NativeGap(f.adapter,before,after);f.s.nativeOwner=after;
        // Actual UpdateHook ran no Gather during pause. Old input and both-eye
        // authority expire; only the next genuinely new packet can proceed.
        f.s.hand.observedNs+=30000000000ll;f.s.hand.deadlineNs+=30000000000ll;
        if(mode)Rebase(f);
        f.Cycle(false,false);CHECK(f.out.phase==BodyHolsterPhase::HidePending);
        CHECK(f.out.blockWeaponActions&&!f.out.freeRight&&!f.out.visibility.enabled&&!f.out.select);
        CHECK(f.adapter.RequestId()>old.visibility.request&&!f.hands.Current(InteractionHand::Right));
        CHECK(!BodyFreeRightEvidenceCurrent(old,f.s.hand.nowNs));
        f.Cycle(false,false);CHECK(!f.out.visibility.enabled&&!f.out.freeRight);
        f.Cycle(false);CHECK(f.out.visibility.enabled&&f.out.visibility.hide&&!f.out.freeRight);
        f.Advance();CHECK(f.Suppress());f.s.visibility=old.receipt;f.Tick();
        CHECK(!f.out.freeRight&&f.out.phase==BodyHolsterPhase::HidePending);
        f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&!f.out.inventory.committedRequest);
        CHECK(f.out.freeRight->visibility.nativeOwner==after||mode);
        CHECK(f.out.freeRight->visibility.nativeOwner.equipGeneration==after.equipGeneration);
        CHECK(!f.hands.Current(InteractionHand::Right));
    }return 0;
}
int NativeGapAdmissionRequiresCommittedExactEpoch(){
    for(unsigned bad=0;bad<8;++bad){Fixture f;CHECK(f.Empty());auto before=f.s.nativeOwner,after=before;++after.equipGeneration;
        if(bad==0)++before.player;if(bad==1)++after.weapon;if(bad==2)++after.actorGeneration;
        if(bad==3)++after.space;if(bad==4)++after.equipGeneration;if(bad==5)after=before;
        if(bad==6)f.adapter.Invalidate();if(bad==7)++before.equipGeneration;
        CHECK(!NativeGap(f.adapter,before,after));
    }
    Fixture incomplete;incomplete.Advance();incomplete.s.body.intent={++incomplete.intent,BodyInventoryOperation::Holster,1,incomplete.items[0].key};incomplete.Tick();
    auto next=incomplete.s.nativeOwner;++next.equipGeneration;CHECK(!NativeGap(incomplete.adapter,incomplete.s.nativeOwner,next));return 0;
}
int NativeGapDoesNotHideReplacementOrCancellation(){
    for(unsigned bad=0;bad<8;++bad){Fixture f;CHECK(f.Empty());auto after=f.s.nativeOwner;++after.equipGeneration;
        CHECK(NativeGap(f.adapter,f.s.nativeOwner,after));f.s.nativeOwner=after;
        if(bad==0)++f.s.carried->items[0].data;if(bad==1)++f.s.carried->items[0].persistence;
        if(bad==2)++f.s.carried->switching;if(bad==3)++f.s.carried->items[1].weapon;
        if(bad==4){++f.s.hand.owner.equipGeneration;f.s.gun.generation=f.s.hand.owner.equipGeneration;}
        if(bad==5)f.adapter.Invalidate();if(bad==6)f.s.cancel=true;if(bad==7)f.s.reloadBusy=true;
        f.Cycle();CHECK(!f.out.freeRight&&!f.out.select&&!f.out.visibility.hide);
        if(bad==6||bad==7){f.s.cancel=false;f.s.reloadBusy=false;f.Cycle();}
        CHECK(f.out.phase==BodyHolsterPhase::ShowPending);f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held);
    }return 0;
}
int NativeGapCannotBlessAnUnobservedEpoch(){
    Fixture f;CHECK(f.Empty());++f.s.nativeOwner.equipGeneration;f.Cycle();
    CHECK(f.out.phase==BodyHolsterPhase::ShowPending&&!f.out.visibility.hide&&!f.out.freeRight);
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held);return 0;
}
int RepeatedNativeGapStillNeedsLatestReceipt(){
    Fixture f;CHECK(f.Empty());auto after=f.s.nativeOwner;++after.equipGeneration;
    CHECK(NativeGap(f.adapter,f.s.nativeOwner,after));
    // A second registration must continue from the already observed owner;
    // replaying the original transition or swapping its actor cannot bless it.
    CHECK(!NativeGap(f.adapter,f.s.nativeOwner,after));
    auto wrongBefore=after;++wrongBefore.player;auto wrongAfter=wrongBefore;++wrongAfter.equipGeneration;
    CHECK(!NativeGap(f.adapter,wrongBefore,wrongAfter));
    f.s.nativeOwner=after;f.Cycle();f.Cycle();const auto old=f.out.visibility;
    CHECK(old.hide);after=f.s.nativeOwner;++after.equipGeneration;
    CHECK(NativeGap(f.adapter,f.s.nativeOwner,after));f.s.nativeOwner=after;f.Cycle();
    CHECK(f.out.phase==BodyHolsterPhase::HidePending&&f.adapter.RequestId()>old.request&&!f.out.freeRight);
    f.Cycle();f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight);
    CHECK(f.out.freeRight->visibility.nativeOwner==after);return 0;
}
int DefaultOff(){Fixture f;Bc2BodyHolster off;const auto result=off.Tick(f.policy,f.s,f.hands,f.intent);
    CHECK(result.phase==BodyHolsterPhase::Disabled&&!result.visibility.enabled&&!result.freeRight&&!result.blockWeaponActions&&result.allowAutomaticGunHold);
    CHECK(f.hands.Current(InteractionHand::Right));CHECK(!off.Demand(f.s));return 0;}
int BothCapabilities(){for(unsigned n=0;n<2;++n){Fixture f;Bc2BodyHolster off{{n==0,n==1,1}};
    CHECK(off.Tick(f.policy,f.s,f.hands,f.intent).phase==BodyHolsterPhase::Disabled);}return 0;}
int UnsupportedProfile(){Fixture f;auto m=std::make_shared<SelectedMeshesSnapshot>(*f.s.selected);m->weaponName[0]='?';f.s.selected=m;
    f.Tick();CHECK(f.out.phase==BodyHolsterPhase::Disabled&&!f.out.blockWeaponActions&&f.hands.Current(InteractionHand::Right));
    f.Metadata();CHECK(f.Empty());m=std::make_shared<SelectedMeshesSnapshot>(*f.s.selected);m->weaponName[0]='?';f.s.selected=m;
    f.Tick();CHECK(f.out.phase==BodyHolsterPhase::Recovering&&f.out.blockWeaponActions&&!f.out.freeRight&&!f.out.visibility.enabled);return 0;}
int NativeMask(){Fixture f;HolsterSuppressionRequest r{f.s.nativeOwner,f.s.hand,7,f.s.nativeTick,f.s.cache};
    f.cache.fill(std::byte{0xff});Word(f.cache,8+4*7,std::bit_cast<unsigned>(-1.f));Word(f.cache,8+4*8,std::bit_cast<unsigned>(1.f));const auto original=f.cache;
    HolsterInputOverride patch;CHECK(patch.Apply(f.cache,r,{&f,[](void* c,const HolsterSuppressionRequest&)noexcept{return static_cast<Fixture*>(c)->ownerCurrent;}}));
    CHECK(Word(f.cache,36)==0&&Word(f.cache,40)==0);CHECK((Word(f.cache,0x98)&((1u<<12)|(1u<<14)|(1u<<29)))==0);
    CHECK((Word(f.cache,0x9c)&0x72)==0);CHECK(Word(f.cache,0x98)&(1u<<15));CHECK(Word(f.cache,0x98)&(1u<<27));
    CHECK(patch.Restore()&&f.cache==original);
    CHECK(patch.Apply(f.cache,r,{&f,[](void* c,const HolsterSuppressionRequest&)noexcept{return static_cast<Fixture*>(c)->ownerCurrent;}}));
    auto receipt=patch.Commit();CHECK(receipt&&HolsterSuppressionCurrent(*receipt,r));++r.nativeTick;CHECK(!HolsterSuppressionCurrent(*receipt,r));return 0;}
int NativeFailure(){Fixture f;HolsterSuppressionRequest r{f.s.nativeOwner,f.s.hand,7,f.s.nativeTick,f.s.cache};HolsterInputOverride patch;
    Word(f.cache,40,std::bit_cast<unsigned>(1.f));auto before=f.cache;
    CHECK(patch.Apply(f.cache,r,{&f,[](void* c,const HolsterSuppressionRequest&)noexcept{return static_cast<Fixture*>(c)->ownerCurrent;}}));
    f.ownerCurrent=false;CHECK(!patch.Commit());CHECK(f.cache==before);f.ownerCurrent=true;
    CHECK(patch.Apply(f.cache,r,{&f,[](void* c,const HolsterSuppressionRequest&)noexcept{return static_cast<Fixture*>(c)->ownerCurrent;}}));
    Word(f.cache,40,std::bit_cast<unsigned>(.5f));CHECK(!patch.Commit());CHECK(Word(f.cache,40)==std::bit_cast<unsigned>(.5f));return 0;}
int ProtectedEquipment(){for(auto action:{EntryAction::SwitchPrimaryWeapon,EntryAction::GrenadeLauncher,EntryAction::DynamicGadget2}){
    Fixture f;HolsterSuppressionRequest r{f.s.nativeOwner,f.s.hand,7,f.s.nativeTick,f.s.cache};HolsterInputOverride patch;
    Word(f.cache,40,std::bit_cast<unsigned>(1.f));CHECK(patch.Apply(f.cache,r,{&f,[](void*,const HolsterSuppressionRequest&)noexcept{return true;}},action));
    const auto receipt=patch.Commit();CHECK(receipt&&receipt->equipmentDispatched&&!HolsterSuppressionCurrent(*receipt,r));CHECK(Word(f.cache,40)==0);
    if(action==EntryAction::SwitchPrimaryWeapon)CHECK(Word(f.cache,36)==std::bit_cast<unsigned>(1.f));else CHECK(Word(f.cache,0x9c)==(1u<<(unsigned(action)-32)));}
    return 0;}
int HideThenRelease(){Fixture f;CHECK(f.Empty());CHECK(f.out.blockWeaponActions&&!f.out.allowAutomaticGunHold);
    CHECK(BodyFreeRightCurrent(*f.out.freeRight,f.s,f.hands));CHECK(f.s.nativeOwner.equipGeneration==7&&f.s.hand.owner.equipGeneration==99);
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight);return 0;}
int NoPackOnlyEmpty(){Fixture f;f.Advance();f.s.body.intent={++f.intent,BodyInventoryOperation::Holster,1,f.items[0].key};f.Tick();f.Cycle();
    f.Cycle(true,false);CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!f.out.freeRight&&f.hands.Current(InteractionHand::Right));
    CHECK(!f.out.visibility.enabled);return 0;}
int InvalidReceipt(){for(unsigned bad=0;bad<7;++bad){Fixture f;f.Advance();f.s.body.intent={++f.intent,BodyInventoryOperation::Holster,1,f.items[0].key};f.Tick();f.Cycle();
    auto request=f.out.visibility;f.Advance();f.Render(request);CHECK(f.Suppress());
    if(bad==0)f.s.visibility->verifiedCopyMask=1;if(bad==1)++f.s.visibility->request;if(bad==2)++f.s.visibility->nativeOwner.equipGeneration;
    if(bad==3)++f.s.visibility->physicalEquipGeneration;if(bad==4)f.s.visibility->deadlineNs=f.s.hand.nowNs;
    if(bad==5)++f.s.visibility->inputSequence;if(bad==6)f.s.visibility->hidden=false;
    f.Tick();CHECK(!f.out.freeRight&&f.hands.Current(InteractionHand::Right));}return 0;}
int ExpiryRecovery(){Fixture f;CHECK(f.Empty());auto evidence=*f.out.freeRight;f.Cycle(false);
    CHECK(!f.out.freeRight&&f.out.blockWeaponActions&&!f.out.allowAutomaticGunHold&&!f.out.visibility.enabled);
    CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!BodyFreeRightCurrent(evidence,f.s,f.hands));
    f.Cycle();CHECK(!f.out.freeRight&&f.out.visibility.enabled&&f.out.visibility.hide);
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&!f.hands.Current(InteractionHand::Right));return 0;}
int ReceiptGapBeforePauseEpochKeepsEmpty(){
    Fixture f;CHECK(f.Empty());const auto original=*f.out.freeRight;
    // Actual014401: current source continues during menu entry but the paired
    // render lease vanishes BEFORE UpdateHook advances its timeout epoch.
    f.Cycle(false);CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!f.out.freeRight&&!f.out.visibility.enabled);
    const auto gapRequest=f.adapter.RequestId();CHECK(gapRequest>original.visibility.request);
    f.Cycle(false);CHECK(f.out.visibility.enabled&&f.out.visibility.hide&&!f.out.freeRight);
    const auto interrupted=f.out.visibility;
    for(unsigned n=0;n<3;++n){f.Cycle(false);CHECK(f.out.phase==BodyHolsterPhase::HidePending&&f.adapter.RequestId()==gapRequest&&!f.out.freeRight);}
    auto after=f.s.nativeOwner;++after.equipGeneration;
    CHECK(NativeGap(f.adapter,f.s.nativeOwner,after));f.s.nativeOwner=after;
    f.s.hand.observedNs+=3000000000ll;f.s.hand.deadlineNs+=3000000000ll;
    f.Cycle(false);CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!f.out.freeRight&&f.adapter.RequestId()>gapRequest);
    CHECK(!BodyFreeRightEvidenceCurrent(original,f.s.hand.nowNs));
    f.Advance();CHECK(f.Suppress());f.Render(interrupted);f.Tick();CHECK(!f.out.freeRight&&!f.hands.Current(InteractionHand::Right));
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&!f.out.inventory.committedRequest);
    CHECK(f.out.freeRight->visibility.nativeOwner==after&&!f.hands.Current(InteractionHand::Right));
    std::ostringstream log;f.adapter.Report(log);CHECK(log.str().find("\"empty_receipt_gaps\":1,\"empty_receipt_rehides\":1,\"empty_receipt_rejected\":0")!=std::string::npos);
    return 0;
}
int ReceiptGapDoesNotMigrateChangedIdentity(){
    for(unsigned bad=0;bad<5;++bad){Fixture f;CHECK(f.Empty());
        if(bad==0)++f.s.carried->items[0].data;if(bad==1)++f.s.carried->items[0].persistence;
        if(bad==2)++f.s.carried->items[1].weapon;if(bad==3)++f.s.carried->inventory;if(bad==4)f.s.carried.reset();
        f.Cycle(false);CHECK(f.out.phase==BodyHolsterPhase::ShowPending&&!f.out.freeRight&&f.out.visibility.enabled&&!f.out.visibility.hide);
        f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held&&f.hands.Current(InteractionHand::Right));
        std::ostringstream log;f.adapter.Report(log);CHECK(log.str().find("\"empty_receipt_rejected\":1")!=std::string::npos);
    }return 0;
}
int ReceiptRehideRequiresNewSuppressionAndPair(){
    Fixture f;CHECK(f.Empty());const auto original=*f.out.freeRight;f.Cycle(false,false);
    CHECK(f.out.phase==BodyHolsterPhase::HidePending&&!f.out.visibility.enabled&&!f.out.freeRight);
    f.Cycle(false,false);CHECK(!f.out.visibility.enabled&&!f.out.freeRight);
    f.Cycle(false);CHECK(f.out.visibility.enabled&&f.out.visibility.hide&&!f.out.freeRight);
    const auto request=f.out.visibility;f.Advance();f.Suppress();f.s.visibility=original.receipt;f.Tick();CHECK(!f.out.freeRight);
    f.Advance();f.Suppress();f.Render(request);f.s.visibility->verifiedCopyMask=1;f.Tick();CHECK(!f.out.freeRight);
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&f.out.freeRight->visibility.request>original.visibility.request);
    f.Advance();f.Suppress();f.Render(f.out.visibility);f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,1,f.items[0].key};f.Tick();
    CHECK(f.out.select);f.Cycle();f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held&&f.hands.Current(InteractionHand::Right));return 0;
}
int SameItemDraw(){Fixture f;CHECK(f.Empty());f.Cycle();f.Advance();f.Suppress();f.Render(f.out.visibility);
    f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,1,f.items[0].key};f.Tick();CHECK(f.out.select==std::optional(f.items[0].key));
    CHECK(!f.hands.Current(InteractionHand::Right)&&f.out.blockWeaponActions);f.Cycle();f.Cycle();
    CHECK(f.out.phase==BodyHolsterPhase::Held&&f.out.inventory.committedRequest&&f.hands.Current(InteractionHand::Right));return 0;}
int ChangedEquipDraw(){Fixture f;CHECK(f.Empty());f.Cycle();f.Advance();f.Suppress();f.Render(f.out.visibility);
    f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,2,f.items[1].key};f.Tick();CHECK(f.out.select==std::optional(f.items[1].key));
    f.Cycle();f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::ShowPending&&!f.hands.Current(InteractionHand::Right));
    f.s.nativeOwner.weapon=B;++f.s.nativeOwner.equipGeneration;++f.s.hand.owner.equipGeneration;f.s.gun={B,f.s.hand.owner.equipGeneration};
    f.s.body.inventory.selected=f.items[1].key;f.Cycle();CHECK(!f.hands.Current(InteractionHand::Right));
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held&&f.hands.Current(InteractionHand::Right)->token.item==f.s.gun);return 0;}
int DrawIntoOrdinaryXm8(){
    // Actual rejected headset route: empty SPAS -> ordinary scoped XM8. The
    // old ChangedEquipDraw fixture used SPAS metadata for BOTH weapons.
    Fixture f;CHECK(f.Empty());f.Cycle();f.Advance();f.Suppress();f.Render(f.out.visibility);
    f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,2,f.items[1].key};f.Tick();
    CHECK(f.out.select==std::optional(f.items[1].key));
    f.s.nativeOwner.weapon=B;++f.s.nativeOwner.equipGeneration;++f.s.hand.owner.equipGeneration;
    f.s.gun={B,f.s.hand.owner.equipGeneration};f.s.body.inventory.selected=f.items[1].key;f.xm8=true;
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::ShowPending&&f.out.visibility.enabled&&!f.out.visibility.hide);
    CHECK(f.out.blockWeaponActions&&!f.hands.Current(InteractionHand::Right));
    // Neither an old SPAS hide receipt nor missing current suppression can
    // reacquire GunHold or release action suppression for the new owner.
    f.Cycle(false);CHECK(f.out.blockWeaponActions&&!f.hands.Current(InteractionHand::Right));
    f.Cycle(true,false);CHECK(f.out.blockWeaponActions&&!f.hands.Current(InteractionHand::Right));
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held&&!f.out.blockWeaponActions&&f.out.inventory.committedRequest);
    CHECK(f.hands.Current(InteractionHand::Right)->token.item==f.s.gun&&!f.out.freeRight);
    CHECK(!f.adapter.AcceptsProfile(f.s.nativeOwner,f.s.selected,f.s.hand.nowNs));
    f.Advance();f.s.body.intent={++f.intent,BodyInventoryOperation::Holster,2,f.items[1].key};f.Tick();
    CHECK(!f.out.visibility.enabled&&!f.out.freeRight&&!f.out.blockWeaponActions);
    CHECK(f.adapter.Phase()==BodyHolsterPhase::Held);
    std::ostringstream report;f.adapter.Report(report);const auto text=report.str();
    CHECK(text.find("\"blocks_actions\":false")!=std::string::npos&&text.find("\"receipt_current\":true")!=std::string::npos);
    CHECK(text.find("\"weapon\":327680")!=std::string::npos&&text.find("\"copy_mask\":3")!=std::string::npos);return 0;
}
int CrossDrawSourceOnlyGestures(){InputFrame input;unsigned rising=0;bool prior=false;
    for(unsigned ms=0;ms<16000;++ms){probe::BodyCrossDrawInput(input,ms);const auto& r=input.hands[1];
        for(const auto& h:input.hands)CHECK(h.trigger==0&&h.stickX==0&&h.stickY==0&&h.held==0);
        CHECK(Near(input.hands[0].squeeze,ms<3000?.4f:0.f));CHECK(input.hands[0].squeeze<.5f);const bool high=r.squeeze>.5f;if(high&&!prior)++rising;prior=high;
        if(high)CHECK((ms>=3000&&ms<4000&&Near(r.grip.position.x,.2f))||(ms>=7000&&ms<8000&&Near(r.grip.position.x,-.2f)));
        if(ms>=9500)CHECK(r.squeeze==0&&Near(r.grip.position.z,-.45f));}
    CHECK(rising==2);return 0;}
int OrdinaryXm8RecoveryNeedsFreshOwner(){
    Fixture f;CHECK(f.Empty());f.s.nativeOwner.weapon=B;++f.s.nativeOwner.equipGeneration;++f.s.hand.owner.equipGeneration;
    f.s.gun={B,f.s.hand.owner.equipGeneration};f.s.body.inventory.selected=f.items[1].key;f.xm8=true;
    f.Cycle();CHECK(f.out.blockWeaponActions&&!f.out.freeRight);
    CHECK(f.out.visibility.enabled&&!f.out.visibility.hide);
    const auto request=f.out.visibility;f.Advance();f.Render(request);f.Suppress();
    f.s.hand.focused=false;f.Tick();CHECK(f.out.blockWeaponActions&&!f.hands.Current(InteractionHand::Right)&&!f.out.visibility.enabled);
    f.s.hand.focused=true;f.Cycle();CHECK(f.out.blockWeaponActions&&!f.hands.Current(InteractionHand::Right));
    f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held&&!f.out.blockWeaponActions&&f.hands.Current(InteractionHand::Right));return 0;
}
int ReplacementAndTracking(){for(unsigned n=0;n<3;++n){Fixture f;CHECK(f.Empty());const auto old=*f.out.freeRight;
    f.Advance();if(n==0){f.items[0].key.generation=3;++f.s.body.inventory.revision;f.s.body.inventory.selected=f.items[0].key;}
    if(n==1)f.s.hand.focused=false;if(n==2){++f.s.hand.owner.space;f.s.nativeOwner.space=f.s.hand.owner.space;}
    f.Suppress();f.Render(f.out.visibility);f.Tick();CHECK(!f.out.freeRight&&f.out.blockWeaponActions&&!f.out.allowAutomaticGunHold);
    CHECK(!BodyFreeRightCurrent(old,f.s,f.hands));}return 0;}
int SharedClaims(){Fixture f;f.Advance();f.hands.Acquire(f.s.hand,{f.s.hand.owner,InteractionHand::Left,HandClaimKind::AmmoObject,{123,1},
    {{12,1},f.s.hand.sequence,f.s.hand.deadlineNs,true},++f.intent,0});f.s.body.intent={++f.intent,BodyInventoryOperation::Holster,1,f.items[0].key};
    f.Tick();CHECK(f.out.phase==BodyHolsterPhase::Held&&!f.out.inventory.request&&f.hands.Current(InteractionHand::Left));return 0;}
int LostTrackingSuppression(){Fixture f;CHECK(f.Empty());f.s.hand.nowNs=f.s.hand.deadlineNs+1;f.s.body.nowNs=f.s.hand.nowNs;f.s.hand.focused=false;++f.s.nativeTick;
    Word(f.cache,40,std::bit_cast<unsigned>(1.f));CHECK(f.Suppress());CHECK(Word(f.cache,40)==0);
    const auto demand=f.adapter.Demand(f.s);CHECK(demand&&!HolsterSuppressionCurrent(*f.s.suppression,*demand));
    f.Tick();CHECK(!f.out.freeRight&&f.out.blockWeaponActions&&!f.out.allowAutomaticGunHold&&!f.out.visibility.enabled);return 0;}
int ActualRenderGuard(){Fixture f;CHECK(f.Empty());const auto source=*f.out.freeRight;auto current=source;auto visibility=source.visibility;const auto now=f.s.hand.nowNs;
    CHECK(source.receipt.observedNs>source.receipt.evidence->observedNs); // Native Pack completion, not source-input time.
    CHECK(BodyFreeRightRenderCurrent(source,&current,&visibility,now));
    CHECK(!BodyFreeRightRenderCurrent(source,nullptr,&visibility,now));visibility.enabled=false;CHECK(!BodyFreeRightRenderCurrent(source,&current,&visibility,now));
    visibility=source.visibility;visibility.hide=false;CHECK(!BodyFreeRightRenderCurrent(source,&current,&visibility,now));
    visibility=source.visibility;++current.input.owner.space;CHECK(!BodyFreeRightRenderCurrent(source,&current,&visibility,now));
    current=source;CHECK(!BodyFreeRightRenderCurrent(source,&current,&visibility,source.input.deadlineNs));return 0;}
int FreePose(){Fixture f;CHECK(f.Empty());std::vector<std::int32_t> parents{-1,0};std::vector<math::Matrix4> world(2,bc2_hand_detail::Identity());
    Bc2HandBinding bind;bind.rightHand=true;bind.pose.wrist=1;bind.wristToGrip=bc2_hand_detail::Identity();
    for(unsigned finger=0;finger<5;++finger){auto& chain=bind.pose.fingers[finger];chain.count=3;
        for(unsigned joint=0;joint<3;++joint){const auto index=unsigned(parents.size());parents.push_back(joint?std::int32_t(index-1):1);
            auto m=bc2_hand_detail::Identity();m.values[3]={float(finger)*.02f,0,.03f*float(joint+1),1};world.push_back(m);
            chain.joints[joint]={index,{1,0,0},0,1,0};}}
    parents.push_back(0);world.push_back({});bind.referenceWorld=world;const auto original=world;
    ControllerState controller;auto raw=bc2_hand_detail::Identity();raw.values[3]={10,20,30,1};auto calibrated=bc2_hand_detail::Identity();calibrated.values[3]={1,2,3,1};
    const auto pose=PoseHolsteredRight(*f.out.freeRight,f.s,f.hands,bind,parents,world,raw,calibrated,controller);CHECK(pose&&pose->writes.size()==16);
    auto output=world;for(const auto& w:pose->writes)output[w.index]=w.transform;
    CHECK(output[1].values[3]==calibrated.values[3]);CHECK(output.back().values==world.back().values);CHECK(world[0].values==original[0].values);
    controller.squeeze=1;controller.trigger=.5f;controller.touchActive=ThumbTouch;controller.touched=0;
    const auto curled=PoseHolsteredRight(*f.out.freeRight,f.s,f.hands,bind,parents,world,raw,calibrated,controller);CHECK(curled);
    CHECK(curled->writes[1].transform.values==pose->writes[1].transform.values); // untouched thumb stays open.
    CHECK(curled->writes[5].transform.values!=pose->writes[5].transform.values); // index curls.
    const auto old=*f.out.freeRight;f.Cycle(false);CHECK(!PoseHolsteredRight(old,f.s,f.hands,bind,parents,world,raw,calibrated,controller));return 0;}
int DiagnosticAdmission(){Fixture f;Bc2BodyHolster off;
    CHECK(!off.AdmitDiagnostic(0,1)&&!off.AdmitDiagnostic(100,100)&&!off.AdmitDiagnostic(100,99));
    CHECK(!off.AdmitDiagnostic(100,15000000101ll));CHECK(off.AdmitDiagnostic(100,15000000100ll));
    CHECK(off.DiagnosticStart()==100&&off.DiagnosticDeadline()==15000000100ll);
    CHECK(!off.AdmitDiagnostic(101,15000000101ll));CHECK(!f.adapter.AdmitDiagnostic(100,200));
    CHECK(!BodyHolsterProbeSample::productionVisibilityAccepted&&!BodyHolsterProbeSample::productionInputAccepted);
    CHECK(off.AcceptsProfile(f.s.nativeOwner,f.s.selected,f.s.hand.nowNs));
    CHECK(!off.AcceptsProfile(f.s.nativeOwner,f.s.selected,99));
    CHECK(!off.AcceptsProfile(f.s.nativeOwner,f.s.selected,off.DiagnosticDeadline()));
    auto changed=std::make_shared<SelectedMeshesSnapshot>(*f.s.selected);std::memcpy(changed->weaponName.data(),"XM8_sp_s",sizeof("XM8_sp_s"));
    CHECK(!off.AcceptsProfile(f.s.nativeOwner,changed,f.s.hand.nowNs));return 0;}
int ScopedXm8AdmissionIsBoundedAndSeparate(){
    Fixture f;f.xm8=true;f.Metadata();
    CHECK(!f.adapter.AcceptsProfile(f.s.nativeOwner,f.s.selected,f.s.hand.nowNs)); // Production mask1 remains unchanged.
    Bc2BodyHolster trial;const auto begin=f.s.hand.nowNs,deadline=begin+1000000000;
    CHECK(!trial.AdmitDiagnostic(begin,deadline,BodyHolsterDiagnosticProfile::Disabled));
    CHECK(!trial.AdmitDiagnostic(begin,deadline,static_cast<BodyHolsterDiagnosticProfile>(6)));
    CHECK(trial.AdmitDiagnostic(begin,deadline,BodyHolsterDiagnosticProfile::ScopedXm8));
    CHECK(trial.DiagnosticProfile()==BodyHolsterDiagnosticProfile::ScopedXm8);
    CHECK(trial.AcceptsProfile(f.s.nativeOwner,f.s.selected,begin));
    CHECK(!trial.AcceptsProfile(f.s.nativeOwner,f.s.selected,deadline));
    const auto xm8=f.s.selected;auto missing=std::make_shared<SelectedMeshesSnapshot>(*xm8);missing->states[0].count=1;
    CHECK(!trial.AcceptsProfile(f.s.nativeOwner,missing,begin));
    f.xm8=false;f.Metadata();CHECK(!trial.AcceptsProfile(f.s.nativeOwner,f.s.selected,begin));
    CHECK(!trial.AdmitDiagnostic(begin+1,deadline+1,BodyHolsterDiagnosticProfile::Spas));
    f.xm8=true;f.Metadata();f.adapter=std::move(trial);CHECK(f.Empty());CHECK(f.out.visibility.authorizationDeadlineNs==deadline);
    f.s.hand.nowNs=deadline;f.s.body.nowNs=deadline;f.Tick();
    CHECK(!f.out.freeRight&&!f.out.visibility.enabled&&!f.adapter.Demand(f.s));return 0;
}
int DiagnosticBoundsActualSequence(){Fixture f;f.adapter=Bc2BodyHolster{};
    const auto start=f.s.hand.nowNs,deadline=start+100000000;
    CHECK(f.adapter.AdmitDiagnostic(start,deadline));CHECK(f.Empty());
    CHECK(f.out.freeRight&&f.out.visibility.authorizationDeadlineNs==deadline);
    const auto proof=*f.out.freeRight;
    CHECK(proof.input.deadlineNs>deadline&&proof.receipt.deadlineNs==deadline);
    CHECK(proof.receipt.evidence->inputDeadlineNs==proof.receipt.evidence->observedNs+100000000);
    CHECK(proof.visibility.input.deadlineNs==f.s.hand.deadlineNs);
    CHECK(BodyFreeRightCurrent(proof,f.s,f.hands)&&BodyFreeRightEvidenceCurrent(proof,f.s.hand.nowNs));
    CHECK(!BodyFreeRightEvidenceCurrent(proof,deadline));
    f.s.hand.nowNs=deadline;f.s.body.nowNs=deadline;
    CHECK(!f.adapter.Demand(f.s));f.Tick();CHECK(!f.out.freeRight&&!f.out.visibility.enabled);
    CHECK(f.out.phase==BodyHolsterPhase::Recovering&&!f.out.allowAutomaticGunHold);
    CHECK(!f.adapter.AdmitDiagnostic(deadline,deadline+100000000));return 0;}
int DiagnosticDrawAndOriginalSource(){Fixture f;f.adapter=Bc2BodyHolster{};
    CHECK(f.adapter.AdmitDiagnostic(f.s.hand.nowNs,f.s.hand.nowNs+1000000000));CHECK(f.Empty());
    const auto sequence=f.out.freeRight->input.sequence;const auto observed=f.out.freeRight->input.observedNs;
    const auto original=f.out.freeRight->input.deadlineNs;
    CHECK(sequence==f.s.hand.sequence&&observed==f.s.hand.observedNs&&original==f.s.hand.deadlineNs);
    f.Cycle();f.Advance();f.Suppress();f.Render(f.out.visibility);f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,1,f.items[0].key};f.Tick();
    CHECK(f.out.select&&f.out.select->id==f.s.nativeOwner.weapon);f.Cycle();f.Cycle();
    CHECK(f.out.phase==BodyHolsterPhase::Held&&f.hands.Current(InteractionHand::Right));
    CHECK(!f.out.freeRight&&!f.out.visibility.enabled);return 0;}
int ProcessingClockKeepsOriginalInput(){
    // Native selected-mesh observation can complete AFTER the gather's input
    // processing sample. Using the old time rejects good current metadata.
    for(unsigned mode=0;mode<3;++mode){Fixture f;f.Advance();const auto source=f.s.hand;
        auto mesh=std::make_shared<SelectedMeshesSnapshot>(*f.s.selected);mesh->observedNs+=2000000;f.s.selected=mesh;
        if(mode){f.s.hand.nowNs=mode==1?source.nowNs+3000000:source.deadlineNs;f.s.body.nowNs=f.s.hand.nowNs;f.s.body.inventory.observedNs=f.s.hand.nowNs;}
        f.s.body.intent={++f.intent,BodyInventoryOperation::Holster,1,f.items[0].key};f.Tick();
        CHECK(f.s.hand.sequence==source.sequence&&f.s.hand.observedNs==source.observedNs&&f.s.hand.deadlineNs==source.deadlineNs);
        CHECK(bool(f.out.inventory.request)==(mode==1));
        if(mode!=1)CHECK(!f.out.freeRight&&!f.out.visibility.enabled);
    }return 0;}
}
int main(){if(FailedInputApplyCannotInventDraw()||NativeExchangeReconcilesRebuiltCarriedInventory()||CarriedMutationInvalidatesPendingOrdinaryPair()||UnsupportedExchangeRequiresNewOrdinaryPair()||OrdinaryRetirementRejectsStaleOrForgedPair()||OrdinaryRetirementIsOnlyExactSelectedSlotReplacement()||OrdinaryRetirementCancellationCannotReplayPair()||OrdinaryRetirementCannotStealLeftClaimOrSkipSuppression()||OrdinaryRetirementNeedsReleasedThenNewTrigger()||ReceiptGapBeforePauseEpochKeepsEmpty()||ReceiptGapDoesNotMigrateChangedIdentity()||ReceiptRehideRequiresNewSuppressionAndPair()||PauseEpochNeedsFreshRehide()||NativeGapAdmissionRequiresCommittedExactEpoch()||NativeGapDoesNotHideReplacementOrCancellation()||NativeGapCannotBlessAnUnobservedEpoch()||RepeatedNativeGapStillNeedsLatestReceipt()||SameSpaceTrackingGapKeepsCommittedEmpty()||SameSpaceExplicitCancellationStillShows()||SecondGapWhileRebindingDoesNotDraw()||TrackingGapRebindRequiresNewEvidence()||RebaseCannotMigrateCancellationOrIdentity()||RebaseDuringRebindGetsAnotherRequest()||IncompleteHolsterDoesNotBecomeCommittedOnRebase()||RebaseKeepsCommittedEmptyIntent()||ScopedXm8AdmissionIsBoundedAndSeparate()||CrossDrawSourceOnlyGestures()||DrawIntoOrdinaryXm8()||OrdinaryXm8RecoveryNeedsFreshOwner()||ProcessingClockKeepsOriginalInput()||DiagnosticAdmission()||DiagnosticBoundsActualSequence()||DiagnosticDrawAndOriginalSource()||DefaultOff()||BothCapabilities()||UnsupportedProfile()||NativeMask()||NativeFailure()||ProtectedEquipment()||HideThenRelease()||NoPackOnlyEmpty()||InvalidReceipt()||ExpiryRecovery()||SameItemDraw()||ChangedEquipDraw()||ReplacementAndTracking()||SharedClaims()||LostTrackingSuppression()||ActualRenderGuard()||FreePose())return 1;std::puts("50 holster coordinator groups passed");return 0;}



