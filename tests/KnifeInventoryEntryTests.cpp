#include "KnifeInventoryEntryFixture.inc"
int main(){using namespace interaction;using namespace bc2;
    constexpr unsigned knife=0x13400,data=0x14800;
    for(unsigned fault=0;fault<7;++fault){HolsterRun h;
        h.r.f.Word(Fixture::array+7*4,knife);h.r.f.Word(knife+4,data);h.r.f.Word(data,Fixture::table);
        h.r.f.Word(data+0x84,11);h.r.f.Word(data+0xc,0x2b200);h.r.f.Text(0x2b200,"KNV-1");h.r.f.types[data]="SoldierWeaponData";
        for(auto& hand:h.r.s.input.hands)hand.active|=Trigger;h.Step();CHECK(h.Empty());h.Step(0);
        h.r.s.input.hands[1].grip.position={.12f,1.45f,-.15f};h.Step(1);CHECK(h.out.freeRight);
        const auto key=h.r.adapter.ItemKey(knife);CHECK(key);
        const auto acquired=h.r.hands.Acquire(h.r.s.hand,{h.r.s.hand.owner,InteractionHand::Right,HandClaimKind::BodyInventory,
            {key->id,key->generation},{{5,1},h.r.s.hand.sequence,h.r.s.hand.deadlineNs,true},++h.r.intent,0});CHECK(acquired.claim);
        const auto empty=*h.out.freeRight;auto post=h.last;
        if(fault==1)++post.nativeTick;if(fault==2)h.r.f.denyMap=true;
        if(fault==3)h.r.f.Word(data+0x64,0x15500); // changed carried identity after TickHolster
        if(fault==4)h.r.s.interaction=BodyInteractionState::Suspended;
        if(fault==5)++h.r.s.input.generation;
        if(fault==6)h.r.s.input.hands[1].trigger=1;
        const bool began=h.r.adapter.BeginTemporaryTool(h.r.f.Memory(),h.r.s,post,h.holster,*key,17,empty,acquired.claim->token,h.r.hands);
        CHECK(began==(fault==0));CHECK(h.holster.TemporaryToolActive()==began);
        if(began){
            const auto before=h.holster.RequestId();h.Step(1);
            CHECK(h.out.visibility.enabled&&h.out.visibility.hide&&h.out.visibility.nativeOwner==h.r.s.owner);
            CHECK(!h.out.freeRight&&h.holster.LastTemporaryToolReturn()==0); // still actual original selection
            CHECK(h.holster.RequestTemporaryToolRehide(17));
            const auto held=h.r.hands.Current(InteractionHand::Right);CHECK(held);CHECK(h.r.hands.Release(h.r.s.hand,held->token).accepted);
            h.Step(0);CHECK(h.holster.RequestId()>before&&!h.out.freeRight);
            h.Step(0);h.Step(0);CHECK(h.out.freeRight&&h.holster.LastTemporaryToolReturn()==17&&!h.holster.TemporaryToolActive());
        }
    }
    std::puts("Connected knife inventory entry PASS plus six rejected stale/native-change/suspension/trigger cases.");
}
