#include "Bc2BodyAmmo.h"
#include "Bc2MagazineConsumerFixture.h"
using namespace magazine_consumer_fixture;
namespace {
BodyAmmoTracking Source(Fixture& f){
    f.Send();BodyAmmoTracking t;t.magazine=f.result.tracking;
    const auto owners=BindMagazineOwners(f.s.input.owner,f.s.weapon,f.reserve,0,f.now,f.s.family);
    t.visual.enabled=true;t.visual.source=*MagazineSupply(*owners,f.reserve,f.s.trackingEpoch,f.now);
    t.visual.input=f.s.input;t.visual.gun=*f.gun;
    const auto supply=ChestAmmoSupply();
    t.visual.contact={supply.pouchCenterMeters,supply.pouchRadiusMeters};t.visual.frame=SupplyAnchorFrame::RecenteredBody;
    return t;
}
InputFrame Input(const Fixture& f){InputFrame i;i.generation=f.s.input.sequence;i.spaceGeneration=f.s.input.owner.space;
    i.predictedNs=f.now;i.focused=i.headValid=true;i.referenceHead.orientation.w=i.head.orientation.w=1;
    i.referenceHead.position=i.head.position={0,1.7f,0};
    for(auto& hand:i.hands){hand.gripTracked=hand.aimTracked=true;hand.grip.orientation.w=hand.aim.orientation.w=1;}
    return i;
}
int SharedProfileBodyPose(){
    unsigned profiles=0;
    for(const auto& registered:RegisteredMagazineNativeProfiles()){
        const auto* p=FindMagazineEquipment(registered.id);if(!p||!p->Ready())continue;++profiles;
        const auto asset=p->geometry->asset;
        for(float units:{1.f,100.f}){
            Fixture f(true,*p);auto source=Source(f);auto input=Input(f);input.worldUnitsPerMeter=units;
            input.referenceHead.orientation={0,std::sin(.3f),0,std::cos(.3f)};
            input.head.orientation={0,std::sin(.9f),0,std::cos(.9f)};input.head.position={.15f,1.3f,-.18f};
            const auto body=Pose(20,-10,30);const auto pose=BuildBodyAmmoPose(source,input,body,f.now);CHECK(pose);
            const auto contact=AmmoSupplyAnchorWorld(input,body,source.visual);CHECK(contact);
            // Extracted magazine is in part-local metres. Its actual grasp
            // lands on the body's configured pickup point at either scale.
            const auto grasp=Multiply(p->geometry->interaction.insertion.itemFromHand,pose->partWorld);
            for(unsigned n=0;n<3;++n)CHECK(Near(grasp.values[3][n],contact->values[3][n],.0002f));
            CHECK(pose->asset==asset&&pose->mesh==p->geometry->mesh&&pose->part==p->geometry->bones.magazine);
            // Looking sideways changes no recentered-body anchor; moving the
            // weapon has no input to this independent body prop at all.
            input.head.orientation={0,std::sin(-.4f),0,std::cos(-.4f)};
            const auto turned=BuildBodyAmmoPose(source,input,body,f.now);CHECK(turned&&Same(pose->partWorld,turned->partWorld));
        }
    }
    CHECK(profiles>=1);
#ifdef FVR_EXPECT_GENERATED_MAGAZINE
    CHECK(profiles>=2);
#endif
    std::cout<<"body supply profiles checked: "<<profiles<<" (native calls mocked)\n";return 0;
}
int LowerChestRemainsThePickupContact(){
    Fixture f;const auto source=Source(f);auto input=Input(f);
    const auto contact=source.visual.contact;CHECK(contact.centerMeters[1]<=-.40f);
    // Human-sized direct target: hand at lower chest, then a look down and a
    // seated/standing translation. Display and acquisition use the same point.
    for(float headHeight:{1.7f,1.2f})for(float pitch:{0.f,.9f}){
        input.head.position.y=headHeight;input.head.orientation={std::sin(pitch/2),0,0,std::cos(pitch/2)};
        input.hands[0].grip.position={contact.centerMeters[0],headHeight+contact.centerMeters[1],-contact.centerMeters[2]};
        const auto hand=BodyAnchorHandPose(input,InteractionHand::Left);CHECK(hand);
        CHECK(BodyAnchorContains(BodyAnchorConfig{}.chest,*hand));
        const auto anchor=AmmoSupplyAnchorWorld(input,Pose(),source.visual);CHECK(anchor);
        CHECK(Near(anchor->values[3][1],headHeight-input.referenceHead.position.y-.45f));
        const auto body=BuildBodyAmmoPose(source,input,Pose(),f.now);CHECK(body);
        const auto grasp=Multiply(f.profile->geometry->interaction.insertion.itemFromHand,body->partWorld);
        for(unsigned n=0;n<3;++n)CHECK(Near(grasp.values[3][n],anchor->values[3][n]));
    }
    // A hand beside the visor no longer counts as reaching the ammunition.
    auto face=Pose(contact.centerMeters[0],-.10f,contact.centerMeters[2]);
    CHECK(!BodyAnchorContains(BodyAnchorConfig{}.chest,face));return 0;
}
int ExactSourceAndOriginalExpiry(){
    Fixture f;const auto source=Source(f);CHECK(BodyAmmoFresh(source,f.now));CHECK(BodyAmmoRetained(source,source,f.now));
    for(unsigned fault=0;fault<12;++fault){auto t=source;
        if(fault==0)++t.visual.source.objectUnits;
        if(fault==1)++t.visual.source.reserveUnits;
        if(fault==2)++t.visual.source.sequence;
        if(fault==3)++t.visual.source.identity.pool.id;
        if(fault==4)t.visual.occupied=true;
        if(fault==5)++t.visual.input.sequence;
        if(fault==6)++t.visual.input.deadlineNs;
        if(fault==7)++t.magazine->owner.equipGeneration;
        if(fault==8)t.magazine->family.verified=false;
        if(fault==9)t.shell=BeltAmmoTracking{};
        if(fault==10)t.magazine.reset();
        if(fault==11)t.visual.input.released[1]=true;
        CHECK(!BodyAmmoFresh(t,f.now));CHECK(!BodyAmmoRetained(source,t,f.now));
    }
    CHECK(!BodyAmmoFresh(source,source.visual.input.deadlineNs));
    auto next=source;next.visual.contact.centerMeters[0]+=.1f;CHECK(!BodyAmmoRetained(source,next,f.now));
    next=source;++next.visual.gun.token.id;CHECK(!BodyAmmoRetained(source,next,f.now));
    auto meshes=std::make_shared<SelectedMeshesSnapshot>(*source.magazine->selected);
    ++meshes->deadlineNs;next=source;next.magazine->selected=meshes;CHECK(!BodyAmmoRetained(source,next,f.now));
    return 0;
}
int NoWrongSpaceOrHeldReplacement(){
    Fixture f;auto source=Source(f);auto input=Input(f);CHECK(BuildBodyAmmoPose(source,input,Pose(),f.now));
    ++input.spaceGeneration;CHECK(!BuildBodyAmmoPose(source,input,Pose(),f.now));input=Input(f);
    ++input.generation;CHECK(!BuildBodyAmmoPose(source,input,Pose(),f.now));input=Input(f);
    input.headValid=false;CHECK(!BuildBodyAmmoPose(source,input,Pose(),f.now));input=Input(f);
    source.magazine->target=MagazinePropTarget{};source.magazine->target->role=MagazinePropRole::Replacement;
    CHECK(!BodyAmmoFresh(source,f.now));source.magazine->target->role=MagazinePropRole::Removed;
    CHECK(!BodyAmmoFresh(source,f.now));return 0;
}
}
int main(){if(SharedProfileBodyPose()||LowerChestRemainsThePickupContact()||ExactSourceAndOriginalExpiry()||NoWrongSpaceOrHeldReplacement())return 1;
    std::cout<<"BodyAmmo: 4 CPU groups passed; no independent native draw or headset acceptance\n";}
