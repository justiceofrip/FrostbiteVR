// CPU composition through the production coherent reader, body policy and
// native selection resolver. These are not renderer/native game receipts.
int HipCarryChecks(){
    using namespace interaction;
    const BodyAnchorConfig anchors{};
    CHECK(ValidBodyAnchors(anchors)&&!FindBodyWeaponAnchor(anchors,anchors.chest.slot));
    for(unsigned slot:{anchors.chest.slot,anchors.shoulders[0].slot,anchors.shoulders[1].slot}){
        auto bad=anchors;bad.hip.slot=slot;CHECK(!ValidBodyAnchors(bad));
    }
    for(const auto asset:{"M1911","M9","M9-3","MP412","MP443","MP443_sp"})
        CHECK(bc2::ResolveBodyCarry(2,asset)==bc2::BodyCarryClass::Sidearm);
    for(const auto asset:{"SCAR_sp","XM8C","MP443_sp_suffix","UNKNOWN"})
        CHECK(bc2::ResolveBodyCarry(2,asset)==bc2::BodyCarryClass::LongGun);
    CHECK(bc2::ResolveBodyCarry(5,"MP443_sp")==bc2::BodyCarryClass::Unsupported);
    const auto pistol=[](Run& r){r.f.Word(Fixture::bd+0x84,2);r.f.Text(0x2b100,"MP443_sp");};
    {Run r;pistol(r);r.Tick();const auto rifle=r.adapter.AssignedSlot(Fixture::a),sidearm=r.adapter.AssignedSlot(Fixture::b);
        CHECK(rifle&&sidearm&&rifle->slot==anchors.shoulders[0].slot&&sidearm->slot==anchors.hip.slot);
        const auto raw=r.f.bytes;auto read=bc2::ReadBodyInventory(r.f.Memory(),r.s.owner,true);
        CHECK(read.snapshot&&read.snapshot->items[1].Carry()==bc2::BodyCarryClass::Sidearm&&r.f.bytes==raw);
        // Empty-air grip and the old shoulder cannot draw the pistol.
        for(const auto point:{math::Vec3{0,1.3f,-.4f},math::Vec3{.20f,1.58f,.18f}}){
            r.s.input.hands[1].grip.position=point;r.Advance();r.Tick();r.Advance(1);CHECK(!r.Tick().command);
        }
        r.s.input.hands[1].grip.position={anchors.hip.center.x,1.7f+anchors.hip.center.y,-anchors.hip.center.z};
        r.Advance();r.Tick();r.Advance(1);const auto draw=r.Tick();
        CHECK(draw.command&&draw.command->targetWeapon==Fixture::b&&draw.command->action==7);
    }
    {Run r;pistol(r);r.Tick();const auto old=*r.adapter.AssignedSlot(Fixture::b),rifle=*r.adapter.AssignedSlot(Fixture::a);
        // Same object/data/category can change on pickup. Do not retain its hip.
        r.f.Text(0x2b100,"SCAR_sp");r.Advance();r.Tick();const auto smg=r.adapter.AssignedSlot(Fixture::b);
        CHECK(smg&&smg->slot==anchors.shoulders[1].slot&&smg->item!=old.item&&r.adapter.AssignedSlot(Fixture::a)==rifle);
        r.f.Text(0x2b100,"MP443_sp");r.Advance();r.Tick();const auto again=r.adapter.AssignedSlot(Fixture::b);
        CHECK(again&&again->slot==anchors.hip.slot&&again->item!=old.item&&again->item!=smg->item);
        const auto saved=*again;++r.s.owner.space;++r.s.hand.owner.space;++r.s.input.spaceGeneration;r.Advance();r.Gun();r.Tick();
        CHECK(r.adapter.AssignedSlot(Fixture::b)==saved);
    }
    {Run r;r.Tick();const auto first=r.adapter.AssignedSlot(Fixture::a),second=r.adapter.AssignedSlot(Fixture::b);
        constexpr unsigned c=0x13400,cd=0x14800;
        r.f.Word(Fixture::array+8,c);r.f.Word(c+4,cd);r.f.Word(cd,Fixture::table);r.f.types[cd]="SoldierWeaponData";
        r.f.Word(cd+0x84,2);r.f.Word(cd+0xc,0x2b200);r.f.Text(0x2b200,"MP443_sp");r.Advance();r.Tick();
        CHECK(r.adapter.AssignedSlot(Fixture::a)==first&&r.adapter.AssignedSlot(Fixture::b)==second);
        CHECK(r.adapter.AssignedSlot(c)&&r.adapter.AssignedSlot(c)->slot==anchors.hip.slot);
        // Removal followed by native pointer reuse must create a fresh item.
        const auto prior=*r.adapter.AssignedSlot(c);r.f.Word(Fixture::array+8,0);r.Advance();r.Tick();
        CHECK(!r.adapter.AssignedSlot(c));r.f.Word(Fixture::array+8,c);r.Advance();r.Tick();
        CHECK(r.adapter.AssignedSlot(c)&&r.adapter.AssignedSlot(c)->item!=prior.item);
    }
    for(unsigned fault=0;fault<4;++fault){Run r;pistol(r);
        if(fault==0)r.f.Word(Fixture::bd+0xc,0);
        if(fault==1)for(unsigned n=0;n<64;++n)r.f.Byte(0x2b100+n,'a');
        if(fault==2)r.f.Byte(0x2b100,1);
        unsigned reads=0;auto memory=r.f.Memory();
        struct Change {Fixture* f;bc2::WeaponModeMemory source;unsigned* reads;bool mutate;};
        Change change{&r.f,memory,&reads,fault==3};
        memory.context=&change;memory.read=[](void* p,unsigned at,void* out,std::size_t n){auto& c=*static_cast<Change*>(p);
            if(at==0x2b100&&++*c.reads==2&&c.mutate)c.f->Text(0x2b100,"SCAR_sp");
            return c.source.read(c.source.context,at,out,n);};
        memory.type=[](void* p,unsigned at,const char* name){auto& c=*static_cast<Change*>(p);return c.source.type(c.source.context,at,name);};
        CHECK(!bc2::ReadBodyInventory(memory,r.s.owner,true).snapshot);
    }
    std::puts("Hip carry: classification, native draw, pickup lifetime, three slots, recenter and malformed/racing identity passed");
    return 0;
}
