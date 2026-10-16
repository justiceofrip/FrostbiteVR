#include "Bc2TemporaryToolFixture.h"
namespace {
constexpr unsigned Knife=0x52000;
struct ToolFixture:Fixture {
    BodyInventoryDisplay display{};std::optional<BodyFreeRightEvidence> before;std::uint64_t op=17;
    ToolFixture(){
        items.push_back({{Knife,3},{},0,0});s.body.inventory.items=items;++s.body.inventory.revision;
        s.carried->count=3;s.carried->items[2]={Knife,0x53000,0x54000,11,2};s.triggerNeutral=true;Advance();Tick();
    }
    void Display(){
        display={};display.selectedOwner=s.nativeOwner;display.physicalOwner=s.hand.owner;
        display.physicalSelected=*s.body.inventory.selected;display.carried=*s.carried;
        display.revision=s.body.inventory.revision;display.sequence=s.hand.sequence;display.cohort=1;
        display.observedNs=s.hand.observedNs;display.deadlineNs=s.hand.deadlineNs;
        display.count=2;display.slots[0]={{1,items[0].key},s.carried->items[0]};display.slots[1]={{2,items[1].key},s.carried->items[1]};
        s.temporaryDisplay=&display;s.temporaryNativeObserved=true;
    }
    bool Begin(){
        if(!Empty())return false;before=out.freeRight;Display();
        const auto held=hands.Acquire(s.hand,{s.hand.owner,InteractionHand::Right,HandClaimKind::BodyInventory,
            {Knife,3},{{5,1},s.hand.sequence,s.hand.deadlineNs,true},++intent,0});
        return held.claim&&adapter.BeginTemporaryTool(s,display,items[2].key,op,*before,held.claim->token,hands);
    }
    void Prepare(){
        Metadata();if(s.nativeOwner.weapon==Knife){auto m=std::make_shared<SelectedMeshesSnapshot>(*s.selected);
            m->weaponName={};std::memcpy(m->weaponName.data(),"KNV-1",6);m->states[0].count=0;m->weaponData=0x53000;s.selected=m;}
        Display();
    }
    void Step(){Advance();Prepare();Tick();}
    void Select(unsigned weapon){
        Advance();s.nativeOwner.weapon=weapon;++s.nativeOwner.equipGeneration;++s.hand.owner.equipGeneration;
        s.gun={weapon,s.hand.owner.equipGeneration};s.body.inventory.selected=weapon==Knife?items[2].key:weapon==A?items[0].key:items[1].key;
        hands.Update(s.hand);Prepare();Tick();
    }
    void ReturnCycle(bool old=false){const auto request=old?before->visibility:out.visibility;Advance();Prepare();Render(request);Suppress();Tick();}
};
int HoldReturnEmpty(){
    ToolFixture f;CHECK(f.Begin());const auto oldRequest=f.adapter.RequestId();
    // Original observation before actual selection cannot end accepted draw.
    f.Step();CHECK(f.adapter.TemporaryToolActive()&&f.adapter.LastTemporaryToolReturn()==0&&!f.out.visibility.enabled);
    f.Select(Knife);CHECK(f.out.phase==BodyHolsterPhase::TemporaryTool&&!f.out.freeRight&&!f.hands.Current(InteractionHand::Right));
    for(unsigned i=0;i<130;++i)f.Step();
    CHECK(f.adapter.TemporaryToolActive()&&f.adapter.LastTemporaryToolReturn()==0&&f.out.blockWeaponActions&&!f.out.allowAutomaticGunHold);
    CHECK(!f.adapter.RequestTemporaryToolRehide(f.op+1));CHECK(f.adapter.RequestTemporaryToolRehide(f.op));
    f.Select(A);CHECK(f.out.phase==BodyHolsterPhase::HidePending&&f.adapter.RequestId()>oldRequest);
    f.ReturnCycle(true);CHECK(!f.out.freeRight&&f.adapter.LastTemporaryToolReturn()==0&&!f.hands.Current(InteractionHand::Right));
    f.ReturnCycle();CHECK(f.out.phase==BodyHolsterPhase::Empty&&f.out.freeRight&&f.out.inventory.emptyHands);
    CHECK(!f.adapter.TemporaryToolActive()&&f.adapter.LastTemporaryToolReturn()==f.op&&!f.hands.Current(InteractionHand::Right));
    CHECK(f.out.inventory.slots.size()==2&&f.out.inventory.slots[0].item==f.items[0].key&&f.out.inventory.slots[1].item==f.items[1].key);
    const auto returned=*f.out.freeRight;f.Display();
    const auto next=f.hands.Acquire(f.s.hand,{f.s.hand.owner,InteractionHand::Right,HandClaimKind::BodyInventory,
        {Knife,3},{{5,1},f.s.hand.sequence,f.s.hand.deadlineNs,true},++f.intent,0});CHECK(next.claim);
    CHECK(!f.adapter.BeginTemporaryTool(f.s,f.display,f.items[2].key,f.op,returned,next.claim->token,f.hands));
    CHECK(f.adapter.BeginTemporaryTool(f.s,f.display,f.items[2].key,f.op+1,returned,next.claim->token,f.hands));
    return 0;
}
int ExplicitReturnAndGaps(){
    ToolFixture f;CHECK(f.Begin());f.Select(Knife);
    f.adapter.Invalidate(true);f.s.hand.focused=false;f.s.temporaryDisplay=nullptr;f.s.temporaryNativeObserved=false;f.Tick();
    CHECK(f.adapter.TemporaryToolActive()&&!f.out.visibility.enabled&&!f.out.freeRight);
    f.s.hand.focused=true;++f.s.nativeOwner.space;f.s.hand.owner.space=f.s.nativeOwner.space;
    f.s.body.inventory.owner.space=f.s.nativeOwner.space;++f.s.body.inventory.revision;f.Step();
    CHECK(f.adapter.TemporaryToolActive()&&!f.out.visibility.enabled);
    f.Select(A);CHECK(f.out.phase==BodyHolsterPhase::TemporaryTool&&f.adapter.LastTemporaryToolReturn()==0);
    CHECK(f.adapter.RequestTemporaryToolRehide(f.op));f.Step();CHECK(f.out.phase==BodyHolsterPhase::HidePending);
    f.ReturnCycle();f.ReturnCycle();CHECK(f.out.freeRight&&f.adapter.LastTemporaryToolReturn()==f.op&&!f.hands.Current(InteractionHand::Right));
    return 0;
}
int WrongBodyDoesNotInheritEmpty(){
    for(unsigned variant=0;variant<5;++variant){ToolFixture f;CHECK(f.Begin());f.Select(Knife);f.Advance();
        if(variant==0)++f.s.carried->items[0].data;
        if(variant==1)++f.s.body.inventory.revision;
        if(variant==2||variant==4){++f.s.nativeOwner.actorGeneration;++f.s.hand.owner.actorGeneration;
            ++f.s.body.inventory.owner.generation;f.hands.Update(f.s.hand);}
        f.Prepare();if(variant==3)std::swap(f.display.slots[0].assignment.slot,f.display.slots[1].assignment.slot);
        if(variant==4)f.s.temporaryDisplay=nullptr; // New actor may have no displayable slots.
        f.Tick();CHECK(!f.out.freeRight&&!f.out.allowAutomaticGunHold&&!f.hands.Current(InteractionHand::Right));
        CHECK(!f.adapter.TemporaryToolActive());
    }
    return 0;
}
int ForeignSelectionRetiresSpecialCase(){
    ToolFixture f;CHECK(f.Begin());f.Select(Knife);f.Select(B);
    CHECK(!f.adapter.TemporaryToolActive()&&!f.out.freeRight&&!f.hands.Current(InteractionHand::Right));return 0;
}
int BeginningNeedsCurrentProof(){
    for(unsigned variant=0;variant<6;++variant){ToolFixture f;CHECK(f.Empty());f.before=f.out.freeRight;f.Display();
        const auto claim=f.hands.Acquire(f.s.hand,{f.s.hand.owner,InteractionHand::Right,HandClaimKind::BodyInventory,
            {Knife,3},{{5,1},f.s.hand.sequence,f.s.hand.deadlineNs,true},++f.intent,0});CHECK(claim.claim);
        auto proof=*f.before;
        if(variant==0)++f.s.nativeTick;
        if(variant==1)proof.receipt.verifiedCopyMask=1;
        if(variant==2)++f.display.revision;
        if(variant==3)proof.input.deadlineNs=f.s.hand.nowNs;
        if(variant==4)f.s.triggerNeutral=false;
        if(variant==5)f.s.interactionSuspended=true;
        CHECK(!f.adapter.BeginTemporaryTool(f.s,f.display,f.items[2].key,f.op,proof,claim.claim->token,f.hands));
        CHECK(!f.adapter.TemporaryToolActive());
    }return 0;
}
}
int main(){
    CHECK(HoldReturnEmpty()==0);CHECK(ExplicitReturnAndGaps()==0);CHECK(WrongBodyDoesNotInheritEmpty()==0);
    CHECK(ForeignSelectionRetiresSpecialCase()==0);CHECK(BeginningNeedsCurrentProof()==0);
    std::puts("Five focused temporary-tool body custody groups PASS; current empty handoff, 1.3s hold, explicit return, new paired hide, gap/space and exact-body guards.");
}
