#define main ExistingBodyInventoryMain
#include "Bc2BodyInventoryTests.cpp"
#undef main
#include "Bc2PersistentBodyEquipment.h"
#include <iostream>
using namespace fvr::bc2;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
std::shared_ptr<const CarriedMeshesSnapshot> Mesh(const BodyInventoryDisplay& d,unsigned i){
    auto c=std::make_shared<CarriedMeshesSnapshot>();const auto& slot=d.slots[i];
    c->weapon=slot.native.weapon;c->nativeSlot=slot.native.slot;c->persistence=slot.native.persistence;
    auto& s=c->configured;s.owner=d.selectedOwner;s.sequence=d.sequence;s.observedNs=d.observedNs;s.deadlineNs=d.deadlineNs;
    s.weaponData=slot.native.data;s.inventory=d.carried.inventory;s.selectedSlot=0;
    std::memcpy(s.weaponName.data(),"SPAS12_sp",sizeof("SPAS12_sp"));s.stateCount=1;s.soleConfiguredArray=0x40000;
    s.states[0].array=s.soleConfiguredArray;s.states[0].count=1;
    strcpy_s(s.states[0].meshes[0].assetPath.data(),512,"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh");return c;
}
int ActualSlotsAndLease(){Run r;r.Tick();auto a=r.adapter.Display(r.s.hand.nowNs);CHECK(a&&a->count==2);
    CHECK(a->selectedOwner==r.s.owner&&a->physicalSelected.id==Fixture::a);
    const auto deadline=a->deadlineNs,observed=a->observedNs;r.Tick();a=r.adapter.Display(r.s.hand.nowNs+1);
    CHECK(a&&a->observedNs==observed&&a->deadlineNs==deadline);CHECK(!r.adapter.Display(deadline));
    auto cohort=a->cohort;r.Advance();r.Tick();a=r.adapter.Display(r.s.hand.nowNs);CHECK(a&&a->cohort==cohort);
    r.f.Word(Fixture::bd+0x64,71);r.Advance();r.Tick();a=r.adapter.Display(r.s.hand.nowNs);CHECK(a&&a->cohort!=cohort);
    CHECK(a->slots[1].native.persistence==71||a->slots[0].native.persistence==71);return 0;}
int DropPickupAndSelection(){Run r;r.Tick();auto a=r.adapter.Display(r.s.hand.nowNs);CHECK(a);const auto old=*a;
    r.f.Word(Fixture::array+4,0);r.Advance();r.Tick();a=r.adapter.Display(r.s.hand.nowNs);CHECK(a&&a->count==1&&!SameBodyInventoryDisplayCohort(old,*a));
    r.f.Word(Fixture::array+4,Fixture::b);r.Advance();r.Tick();a=r.adapter.Display(r.s.hand.nowNs);CHECK(a&&a->count==2&&a->cohort>old.cohort);
    r.f.Select(1);r.s.owner=r.f.owner;r.s.gun={Fixture::b,100};++r.s.hand.owner.equipGeneration;r.Advance();r.Gun();r.Tick();
    a=r.adapter.Display(r.s.hand.nowNs);CHECK(a&&a->physicalSelected.id==Fixture::b&&a->cohort>old.cohort);return 0;}
int LifecycleRetires(){for(unsigned variant=0;variant<4;++variant){Run r;r.Tick();auto a=r.adapter.Display(r.s.hand.nowNs);CHECK(a);const auto cohort=a->cohort;
    if(variant==0)r.f.Word(Fixture::player+0xc68,0); // vehicle/controlled actor
    if(variant==1)r.s.input.focused=false;
    if(variant==2)r.f.denyMap=true;
    if(variant==3)r.s.cancel=true;
    r.Advance();r.Tick();CHECK(!r.adapter.Display(r.s.hand.nowNs));
    r.f.Word(Fixture::player+0xc68,Fixture::soldier);r.f.denyMap=false;r.s.cancel=false;r.s.input.focused=true;
    ++r.s.owner.space;++r.s.hand.owner.space;++r.s.input.spaceGeneration;r.Advance();r.Gun();r.Tick();
    a=r.adapter.Display(r.s.hand.nowNs);CHECK(a&&a->cohort>cohort&&a->physicalOwner.space==r.s.input.spaceGeneration);
    CHECK(!SameBodyInventoryDisplayCohort(*a,BodyInventoryDisplay{}));}return 0;}
int ActualSlotsToHost(){Run r;r.Tick();auto d=r.adapter.Display(r.s.hand.nowNs);CHECK(d);
    auto source=std::make_shared<const BodyInventoryDisplay>(*d);std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> configs{};
    unsigned index=0;while(index<d->count&&d->slots[index].native.weapon!=Fixture::b)++index;CHECK(index<d->count);configs[index]=Mesh(*d,index);
    const auto identity=Identity();const interaction::BodyAnchorConfig anchors{};
    auto batch=BuildBodyCarriedBatch(source,configs,r.s.input,identity,anchors,r.s.hand.nowNs);CHECK(batch&&batch->count==1);
    CHECK(batch->instances[0].equipmentGeneration==graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Carried,d->cohort,index));graphics::BodyPropEye eye{};
    // The exact host comparator rejects a shifted prefix even when geometry,
    // actor and the original numeric generation are identical.
    auto selected=batch->instances[0];selected.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::SelectedHolster,d->cohort);
    CHECK(!graphics::SameBodyPropSource(selected,batch->instances[0]));
    auto otherSlot=batch->instances[0];otherSlot.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Carried,d->cohort,(index+1)%8);
    CHECK(!graphics::SameBodyPropSource(otherSlot,batch->instances[0]));
    CHECK(AppendBodyCarriedHostInstances(*batch,*batch,r.s.hand.nowNs,d->physicalOwner.space,eye)&&eye.count==1);
    auto changed=*batch;auto newd=std::make_shared<BodyInventoryDisplay>(*d);++newd->cohort;changed.inventory=newd;changed.instances[0].equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Carried,newd->cohort,index);
    const auto oldCount=eye.count;CHECK(!AppendBodyCarriedHostInstances(*batch,changed,r.s.hand.nowNs,d->physicalOwner.space,eye)&&eye.count==oldCount);
    eye.count=graphics::MaxBodyProps;CHECK(!AppendBodyCarriedHostInstances(*batch,*batch,r.s.hand.nowNs,d->physicalOwner.space,eye)&&eye.count==graphics::MaxBodyProps);
    CHECK(!AppendBodyCarriedHostInstances(*batch,*batch,d->deadlineNs,d->physicalOwner.space,eye));
    auto renewed=*batch;auto c=std::make_shared<CarriedMeshesSnapshot>(*configs[index]);c->configured.deadlineNs+=100000000;renewed.configured[0]=c;
    CHECK(!AppendBodyCarriedHostInstances(*batch,renewed,d->deadlineNs,d->physicalOwner.space,eye));return 0;}
int CurrentSelectedAndUnknownNeverDuplicate(){Run r;r.Tick();auto d=r.adapter.Display(r.s.hand.nowNs);CHECK(d);
    std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> configs{};for(unsigned i=0;i<d->count;++i)configs[i]=Mesh(*d,i);
    const auto source=std::make_shared<const BodyInventoryDisplay>(*d);
    const auto b=BuildBodyCarriedBatch(source,configs,r.s.input,Identity(),{},r.s.hand.nowNs);CHECK(b&&b->count==1);
    for(unsigned i=0;i<d->count;++i)if(d->slots[i].native.weapon==Fixture::b){auto bad=std::make_shared<CarriedMeshesSnapshot>(*configs[i]);
        strcpy_s(bad->configured.weaponName.data(),128,"UNKNOWN");configs[i]=bad;}
    CHECK(!BuildBodyCarriedBatch(source,configs,r.s.input,Identity(),{},r.s.hand.nowNs));return 0;}
int LinkedSelectedIsNotSpare(){Run r;r.f.Word(Fixture::ad+0x84,5);r.f.Word(Fixture::ad+0x64,55);r.f.Word(Fixture::bd+0x64,55);
    r.s.verifiedFamily=bc2::WeaponModeCommand{7,Fixture::b,1,Fixture::inventory,55};r.Tick();
    const auto d=r.adapter.Display(r.s.hand.nowNs);CHECK(d&&d->count==1&&d->physicalSelected.id==Fixture::b);
    std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> configs{};configs[0]=Mesh(*d,0);
    const auto b=BuildBodyCarriedBatch(std::make_shared<const BodyInventoryDisplay>(*d),configs,r.s.input,Identity(),{},r.s.hand.nowNs);
    CHECK(b&&b->count==0);return 0;}
int RenderEpochNamespaces(){using graphics::BodyPropSourceKind;using graphics::BodyPropWireEpoch;
    const auto ammo=BodyPropWireEpoch(BodyPropSourceKind::Ammo,1),held=BodyPropWireEpoch(BodyPropSourceKind::SelectedHolster,1);
    const auto carried=BodyPropWireEpoch(BodyPropSourceKind::Carried,1,0),other=BodyPropWireEpoch(BodyPropSourceKind::Carried,1,1);
    CHECK(ammo&&held&&carried&&other&&ammo!=held&&held!=carried&&carried!=other);
    CHECK(!BodyPropWireEpoch(BodyPropSourceKind::Carried,0));CHECK(!BodyPropWireEpoch(BodyPropSourceKind::Carried,UINT64_MAX));
    CHECK(!BodyPropWireEpoch(BodyPropSourceKind::Carried,1,8));
    CHECK(!BodyPropWireEpoch(BodyPropSourceKind::Carried,(UINT64_MAX>>7)+1));
    CHECK(BodyPropWireEpoch(BodyPropSourceKind::VehicleReticle,UINT64_MAX>>7,7));
    Run r;r.Tick();auto d=r.adapter.Display(r.s.hand.nowNs);CHECK(d&&d->count==2);d->slots[1]=d->slots[0];CHECK(!BodyInventoryDisplayFresh(*d,r.s.hand.nowNs));
    graphics::BodyPropInstance empty{};CHECK(!graphics::SameBodyPropSource(empty,empty));
    CHECK(sizeof(graphics::BodyPropFrame)==2480&&graphics::MaxBodyProps==8);return 0;}
int InPlaceAssetMutationIsRejected(){Run r;r.f.Word(Fixture::bd+12,0x39000);r.f.Text(0x39000,"SPAS12_sp");r.Tick();
    const auto d=r.adapter.Display(r.s.hand.nowNs);CHECK(d);unsigned index=0;while(index<d->count&&d->slots[index].native.weapon!=Fixture::b)++index;CHECK(index<d->count);
    const auto c=Mesh(*d,index);const auto e=body_carried_detail::Equipment(*c);CHECK(e&&WeaponEquipmentStillCurrent(r.f.Memory(),*e));
    r.f.Text(0x39000,"XM8_sp_s");CHECK(!WeaponEquipmentStillCurrent(r.f.Memory(),*e));return 0;}
}
int main(){for(auto fn:{ActualSlotsAndLease,DropPickupAndSelection,LifecycleRetires,ActualSlotsToHost,CurrentSelectedAndUnknownNeverDuplicate,LinkedSelectedIsNotSpare,RenderEpochNamespaces,InPlaceAssetMutationIsRejected})if(fn())return 1;
    std::cout<<"8 persistent body inventory/host groups passed\n";return 0;}
