#include "Bc2BodyAmmo.h"
#include "Bc2BodyAmmoHost.h"
#include "Bc2BodyEquipment.h"
#include "Bc2MagazineConsumerFixture.h"
#include "Bc2TrackedBodyBase.h"
#include "fvr/engine/FrostbiteCamera.h"
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
        CHECK(Near(anchor->values[3][1],headHeight-input.referenceHead.position.y+contact.centerMeters[1]));
        const auto body=BuildBodyAmmoPose(source,input,Pose(),f.now);CHECK(body);
        const auto grasp=Multiply(f.profile->geometry->interaction.insertion.itemFromHand,body->partWorld);
        for(unsigned n=0;n<3;++n)CHECK(Near(grasp.values[3][n],anchor->values[3][n]));
    }
    // A hand beside the visor no longer counts as reaching the ammunition.
    auto face=Pose(contact.centerMeters[0],-.10f,contact.centerMeters[2]);
    CHECK(!BodyAnchorContains(BodyAnchorConfig{}.chest,face));return 0;
}
math::Quaternion Product(math::Quaternion a,math::Quaternion b){return {
    a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
    a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
math::Quaternion HeadAngles(float yaw,float pitch,float roll){
    return Product(Product({0,std::sin(yaw/2),0,std::cos(yaw/2)},
        {std::sin(pitch/2),0,0,std::cos(pitch/2)}),{0,0,std::sin(roll/2),std::cos(roll/2)});
}
// A physical controller placed relative to the upright reference heading. This
// deliberately does not call the render anchor or invert its computed matrix.
math::Pose PhysicalChestHand(const InputFrame& input,math::Vec3 bodyPoint){
    const auto& q=input.referenceHead.orientation;
    const float yaw=2*std::atan2(q.y,q.w),c=std::cos(yaw),s=std::sin(yaw);
    math::Pose hand;hand.orientation=q;
    hand.position={input.head.position.x+c*bodyPoint.x-s*bodyPoint.z,
        input.head.position.y+bodyPoint.y,input.head.position.z-s*bodyPoint.x-c*bodyPoint.z};
    return hand;
}
bool ClosePoint(math::Vec4 a,math::Vec4 b,float units){
    constexpr float metres=.0015f;
    return Near(a.x/units,b.x/units,metres)&&Near(a.y/units,b.y/units,metres)&&
        Near(a.z/units,b.z/units,metres)&&Near(a.w,b.w,metres);
}
int PhysicalPickupThroughNativeStereo(){
    const BodyAnchorConfig config{};
    CHECK(config.chest.center.y+config.chest.radius<-.10f);
    // Zero/look-down/look-up, yaw, roll and combined orientations. Heights are
    // actual XR heights, while native stance/world and consumed roomscale vary
    // independently. This catches cancellations hidden by an identity eye base.
    const std::array<std::array<float,3>,7> angles{{{0,0,0},{0,-1.05f,0},{0,.65f,0},
        {1.75f,0,0},{-1.75f,0,0},{0,0,.55f},{.8f,-.85f,-.40f}}};
    unsigned profiles=0,cases=0;
    std::optional<BodyAmmoTracking> previousWeapon;
    for(const auto& registered:RegisteredMagazineNativeProfiles()){
        const auto* profile=FindMagazineEquipment(registered.id);if(!profile||!profile->Ready())continue;
        Fixture f(true,*profile);++profiles;
        // Fresh equip identity models selecting another registered family.
        f.s.input.owner.equipGeneration+=profiles;f.s.weapon.generation=f.s.input.owner.equipGeneration;
        f.s.nativeOwner.equipGeneration+=profiles;f.reserve.identity.owner=f.s.nativeOwner;f.meshes->owner=f.s.nativeOwner;
        auto source=Source(f);if(previousWeapon)CHECK(!BodyAmmoRetained(*previousWeapon,source,f.now));
        previousWeapon=source;
        const auto& geometry=*profile->geometry;
        const auto partFromContact=InverseRigid(geometry.interaction.insertion.itemFromHand);CHECK(partFromContact);
        const auto& grasp=geometry.interaction.insertion.itemFromHand.values[3];
        for(float units:{1.f,100.f})for(float height:{1.2f,1.75f})
        for(float bodyYaw:{-.9f,3.14159265f,4.7f})for(unsigned room=0;room<2;++room)for(const auto& angle:angles){
            auto input=Input(f);input.worldUnitsPerMeter=units;
            input.referenceHead.position={.4f,height,-.6f};input.referenceHead.orientation=HeadAngles(.45f,0,0);
            input.head.position={.4f+(room?.33f:0),height-(room?.12f:0),-.6f-(room?.17f:0)};
            input.head.orientation=HeadAngles(angle[0],angle[1],angle[2]);
            const math::Vec3 consumed=room?math::Vec3{.17f,0,-.08f}:math::Vec3{};
            // Native camera translation is intentionally unlike the soldier's
            // current collision origin. The real bridge selects the latter.
            const auto nativeEye=Pose(40*units,(height+.35f)*units,-70*units);
            const math::Vec3 actor{(room?13.f:-7.f)*units,.35f*units,(room?-9.f:21.f)*units};
            const auto base=BuildTrackedBodyBase(nativeEye,bodyYaw,units,actor,consumed);CHECK(base);
            for(unsigned recentered=0;recentered<2;++recentered){
                if(recentered){
                    const auto old=source;const auto reference=ExplicitRecenterReference(input.head,input.referenceHead);CHECK(reference);
                    input.referenceHead=*reference;++input.spaceGeneration;
                    CHECK(!BuildBodyAmmoPose(old,input,*base,f.now));
                    f.s.nativeOwner.space=input.spaceGeneration;f.s.input.owner.space=input.spaceGeneration;
                    f.s.trackingEpoch=input.spaceGeneration;f.reserve.identity.owner=f.s.nativeOwner;f.meshes->owner=f.s.nativeOwner;
                    source=Source(f);input.generation=source.visual.input.sequence;input.predictedNs=f.now;
                }
                input.hands[0].grip=PhysicalChestHand(input,config.chest.center);input.hands[0].aim=input.hands[0].grip;
                const auto contactHand=BodyAnchorHandPose(input,InteractionHand::Left);CHECK(contactHand);
                CHECK(BodyAnchorContains(config.chest,*contactHand));
                for(unsigned axis=0;axis<3;++axis)CHECK(Near(contactHand->values[3][axis],source.visual.contact.centerMeters[axis]));
                const auto anchor=AmmoSupplyAnchorWorld(input,*base,source.visual);CHECK(anchor);
                const auto body=BuildBodyAmmoPose(source,input,*base,f.now);CHECK(body);
                CHECK(body->asset==geometry.asset&&body->mesh==geometry.mesh&&body->part==geometry.bones.magazine);
                const auto placed=math::TransformRowVector({grasp[0],grasp[1],grasp[2],1},body->partWorld);
                CHECK(ClosePoint(placed,{anchor->values[3][0],anchor->values[3][1],anchor->values[3][2],1},units));
                const auto headWorld=math::ComposeRuntimeHeadWithLhCamera(*base,input.referenceHead,input.head,units);CHECK(headWorld);
                CHECK(Near((placed.y-headWorld->values[3][1])/units,config.chest.center.y,.0015f));
                // Empty-hand availability uses this distinct body-equipment
                // placement function. Compare its geometry only, not fabricated
                // holster/source authority or a claimed native stow transition.
                const auto stowed=BodyEquipmentPose(input,*base,config.chest,*partFromContact);CHECK(stowed);
                CHECK(Same(body->partWorld,*stowed,.002f*units));
                const auto eyes=math::ComputeEyePoses(input.head,.064f);CHECK(eyes);
                unsigned side=0;
                for(const auto& eye:{eyes->left,eyes->right}){
                    const math::FovTangents fov=side++?math::FovTangents{-.8f,1.1f,.9f,-.75f}:math::FovTangents{-1.1f,.8f,.85f,-.9f};
                    const auto projection=math::MakeLhProjectionFromFovTangents(fov,.05f*units,1000*units);CHECK(projection);
                    const auto eyeWorld=math::ComposeRuntimeHeadWithLhCamera(*base,input.referenceHead,eye,units);CHECK(eyeWorld);
                    const auto native=engine::NativeEye({*eyeWorld,*projection});CHECK(native);
                    const auto delivered=CanonicalBodyPropEye(native->view,native->projection);CHECK(delivered);
                    const auto observed=math::TransformRowVector(placed,delivered->view);
                    // Independent expected eye coordinates come directly from
                    // the XR eye and physical controller, without any body or
                    // render anchor matrix. Compare clip coordinates as well.
                    const auto relative=math::MakeRelativePose(eye,input.hands[0].grip,units);CHECK(relative);
                    const math::Vec4 expected{relative->position.x,relative->position.y,-relative->position.z,1};
                    CHECK(ClosePoint(observed,expected,units));
                    const auto clip=graphics::RigidPropClipTransform(body->partWorld,delivered->view,delivered->projection);CHECK(clip);
                    const auto actualClip=math::TransformRowVector({grasp[0],grasp[1],grasp[2],1},*clip);
                    const auto expectedClip=math::TransformRowVector(expected,*projection);
                    // Homogeneous W is also distance-scaled after projection.
                    CHECK(Near(actualClip.x/units,expectedClip.x/units,.0015f));
                    CHECK(Near(actualClip.y/units,expectedClip.y/units,.0015f));
                    CHECK(Near(actualClip.z/units,expectedClip.z/units,.0015f));
                    CHECK(Near(actualClip.w/units,expectedClip.w/units,.0015f));
                }
                auto face=input;face.hands[0].grip=PhysicalChestHand(input,{config.chest.center.x,-.03f,.06f});
                const auto faceHand=BodyAnchorHandPose(face,InteractionHand::Left);CHECK(faceHand&&!BodyAnchorContains(config.chest,*faceHand));
                ++cases;
            }
        }
    }
    CHECK(profiles&&cases==profiles*336);
    std::cout<<"Chest pickup/native stereo: "<<profiles<<" registered profiles, "<<cases<<" physical poses; no GPU/headset claim\n";
    return 0;
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
int main(){if(SharedProfileBodyPose()||LowerChestRemainsThePickupContact()||PhysicalPickupThroughNativeStereo()||ExactSourceAndOriginalExpiry()||NoWrongSpaceOrHeldReplacement())return 1;
    std::cout<<"BodyAmmo: 5 CPU groups passed; no independent native draw or headset acceptance\n";}
