#include "Bc2BoltPlayerCustody.h"
#include "Bc2M95BoltCalibration.h"
#include "Bc2TrackedBodyBase.h"
#include "Test.h"
#include <cstring>
#define main PhysicalBoltRegressionMain
#include "Bc2PhysicalBoltTests.cpp"
#undef main
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
using namespace reload_insertion_detail;
math::Matrix4 Pose(){return reload_insertion_detail::Identity();}
struct Setup {
    InputFrame input{};HandInteractionSample safety{{(std::uint64_t(0x30000)<<32)|0x20000,2,3,4},1,1000000000,1100000000,1000000000,true,{true,true},{false,false}};
    ReloadStateOwner owner{0x10000,0x20000,0x30000,0x40000,2,7,4};Bc2BoltControllerContact raw;Bc2BoltBodyFrame frame;
    Setup(){
        input.generation=safety.sequence;input.spaceGeneration=4;input.predictedNs=safety.observedNs;
        input.focused=input.headValid=true;
        for(unsigned n=0;n<2;++n){auto& hand=input.hands[n];hand.active=Components;hand.gripTracked=hand.aimTracked=true;hand.squeeze=1;
            hand.grip.position={n?.1f:-.2f,-.2f,-.5f};raw.wristToGrip[n]=Pose();}
        raw.raw={owner,{},17,safety,Pose(),true};raw.mappingValid=true;raw.weaponWorldMeters=Pose();raw.bodyWorldMeters=Pose();
        frame={owner,safety,input.referenceHead,Pose(),1};
        const auto wrists=MapBoltTrackedWrists(raw,input,safety,frame);if(wrists)raw.rawWristWorldMeters=*wrists;
        raw.nativeWristInWeapon=raw.rawWristWorldMeters;raw.nativePartValid=true;raw.nativePartInWeapon=Pose();
    }
    void Next(){++safety.sequence;safety.observedNs+=10000000;safety.nowNs=safety.observedNs;safety.deadlineNs=safety.observedNs+100000000;
        input.generation=safety.sequence;input.predictedNs=safety.observedNs;frame.input=safety;}
};
int RebasedHeadAndCurrentBodyAnchor(){
    Setup f;auto expected=MapBoltTrackedWrists(f.raw,f.input,f.safety,f.frame);CHECK(expected);
    // Moving the complete reference-space origin leaves relative wrists fixed.
    f.input.referenceHead.position={2,1,3};f.input.head.position={2,1,3};
    for(auto& hand:f.input.hands){hand.grip.position.x+=2;hand.grip.position.y+=1;hand.grip.position.z+=3;}
    f.frame.referenceHead=f.input.referenceHead;
    const auto rebased=MapBoltTrackedWrists(f.raw,f.input,f.safety,f.frame);CHECK(rebased);
    for(unsigned n=0;n<2;++n)CHECK(Distance((*rebased)[n],(*expected)[n])<.000001f);
    Setup rotated;rotated.input.referenceHead.position={2,1,3};
    rotated.input.referenceHead.orientation={0,.707106781f,0,.707106781f};
    rotated.input.head=rotated.input.referenceHead;rotated.frame.referenceHead=rotated.input.referenceHead;
    for(auto& hand:rotated.input.hands){const auto old=hand.grip.position;
        hand.grip.position={2+old.z,1+old.y,3-old.x};hand.grip.orientation=rotated.input.referenceHead.orientation;}
    const auto same=MapBoltTrackedWrists(rotated.raw,rotated.input,rotated.safety,rotated.frame);CHECK(same);
    for(unsigned n=0;n<2;++n)CHECK(Distance((*same)[n],(*expected)[n])<.000001f&&Angle((*same)[n],(*expected)[n])<.001f);
    // Real roomscale hand movement uses the CURRENT compensated world anchor.
    f.input.hands[0].grip.position.x+=.2f;f.frame.bodyWorldMeters.values[3][0]=-.2f;
    const auto room=MapBoltTrackedWrists(f.raw,f.input,f.safety,f.frame);CHECK(room&&Distance((*room)[0],(*expected)[0])<.000001f);
    // Snap yaw rotates the reference-local basis, without changing raw evidence.
    f.frame.bodyWorldMeters=Pose();f.frame.bodyWorldMeters.values[0]={0,0,-1,0};f.frame.bodyWorldMeters.values[2]={1,0,0,0};
    const auto turned=MapBoltTrackedWrists(f.raw,f.input,f.safety,f.frame);CHECK(turned);
    CHECK(Near((*turned)[1].values[3][0],.5f)&&Near((*turned)[1].values[3][2],-.1f));
    const auto weapon=ReprojectBoltCustodyWeapon(f.raw,*turned,InteractionHand::Left);CHECK(weapon);
    const auto oldAttachment=Multiply(f.raw.weaponWorldMeters,*InverseRigid(f.raw.rawWristWorldMeters[0]));
    const auto newAttachment=Multiply(*weapon,*InverseRigid((*turned)[0]));
    CHECK(Distance(oldAttachment,newAttachment)<.000001f&&Angle(oldAttachment,newAttachment)<.001f);
    CHECK(f.raw.raw.input.sequence==1&&f.raw.raw.input.deadlineNs==1100000000);return 0;
}
int MappingRejectsStaleOrMismatchedFrame(){
    for(unsigned n=0;n<12;++n){Setup f;
        switch(n){case 0:++f.input.spaceGeneration;break;case 1:++f.frame.owner.weapon;break;
        case 2:++f.frame.input.sequence;break;case 3:f.frame.referenceHead.position.x=.1f;break;
        case 4:f.safety.nowNs=f.raw.raw.input.deadlineNs;break;case 5:f.input.focused=false;break;
        case 6:f.input.hands[1].gripTracked=false;break;case 7:f.raw.wristToGrip[1].values[3][0]=.01f;break;
        case 8:f.frame.units=2;break;case 9:f.raw.units=2;break;
        case 10:++f.raw.raw.input.owner.equipGeneration;break;case 11:f.frame.bodyWorldMeters.values[0][0]=2;break;}
        CHECK(!MapBoltTrackedWrists(f.raw,f.input,f.safety,f.frame));
    }return 0;
}
int StableReferencesFreezeAndRecenterInvalidates(){
    Setup f;auto calibration=std::make_shared<Bc2BoltCalibration>(M95AuthoredBoltCalibration(1));calibration->nativeJoined=true;calibration->rigFingerprint=17;
    f.raw.nativePartInWeapon=calibration->profile.closedContact;
    Bc2BoltPlayerCustody player(calibration);HandInteraction hands;std::uint64_t intent=0;
    Bc2BoltPlayerInput in;in.owner=f.owner;in.item={f.owner.weapon,3};in.asset=calibration->asset;in.mesh=calibration->mesh;
    in.native=Bc2NativeCycleView{};in.native->native.owner=f.owner;
    Bc2AmmoReserveLease ammo;ammo.identity.owner=f.owner;ammo.verified=ammo.allThreeIdle=true;ammo.capacity=5;ammo.loaded=5;ammo.reserve=45;
    for(unsigned n=0;n<70;++n){
        f.Next();f.raw.raw.input=f.safety;in.controls=&f.input;in.safety=f.safety;in.body=f.frame;in.contact=f.raw;
        ammo.sequence=f.safety.sequence;ammo.observedNs=f.safety.observedNs;ammo.deadlineNs=f.safety.deadlineNs;in.ammunition=ammo;
        CHECK(player.Prepare(in,hands,intent));
        if(n<59)CHECK(!player.ReferencesReady());
    }
    CHECK(player.ReferencesReady());const auto refs=*player.References();
    f.Next();f.raw.raw.input=f.safety;f.raw.nativeWristInWeapon[1].values[3][0]+=1;
    in.safety=f.safety;in.body=f.frame;in.contact=f.raw;CHECK(player.Prepare(in,hands,intent));
    CHECK(Distance((*player.References())[1],refs[1])<.000001f);
    // A tracked-space change while debt exists cannot silently retire it or
    // attach stale original references to a newly stamped native owner.
    Bc2PhysicalBoltResult owed;owed.blocksFire=true;owed.tracking.custody=BoltCustodyPhase::Manipulating;player.Observe(owed);
    ++f.input.spaceGeneration;++f.owner.space;++f.safety.owner.space;f.Next();f.frame.owner=f.owner;f.frame.input=f.safety;
    in.owner=f.owner;in.safety=f.safety;in.body=f.frame;
    CHECK(!player.Prepare(in,hands,intent));CHECK(!player.ReferencesReady()&&player.BlocksFire()&&player.NeedsOwnerRetirement());
    return 0;
}
int PlayerReleaseUsesActualAdapterAndImmutableControls(){
    Fixture f;Bc2BoltPlayerCustody player(f.calibration);InputFrame controls;
    CHECK(f.hands.Release(f.sample.input,f.originalSupport).accepted);
    auto supportRequest=f.Request(left,HandClaimKind::WeaponSupport,2,f.originalGun.id);
    supportRequest.contact.key={2,f.sample.item.id};
    const auto supportClaim=f.hands.Acquire(f.sample.input,supportRequest);CHECK(supportClaim.claim);f.originalSupport=supportClaim.claim->token;
    controls.focused=controls.headValid=true;controls.spaceGeneration=f.sample.input.owner.space;
    for(auto& hand:controls.hands){hand.active=Components;hand.gripTracked=hand.aimTracked=true;hand.squeeze=1;}
    const auto originalWeapon=f.sample.weaponWorld;
    const auto inverse=InverseRigid(originalWeapon);CHECK(inverse);
    const std::array<math::Matrix4,2> refs={Multiply(f.sample.wristWorld[0],*inverse),Multiply(f.sample.wristWorld[1],*inverse)};
    f.native.view.phase=Bc2NativeCyclePhase::Watching;f.native.view.held.reset();f.native.view.blocksFire=false;
    const auto prepare=[&](bool release)->std::optional<Bc2PhysicalBoltSample>{
        Bc2BoltControllerContact raw;raw.raw={f.sample.nativeOwner,f.rig.identity,f.calibration->rigFingerprint,f.sample.input,Pose(),true};
        raw.mappingValid=true;raw.bodyWorldMeters=Pose();raw.weaponWorldMeters=originalWeapon;
        raw.rawWristWorldMeters=f.sample.wristWorld;raw.wristToGrip={Pose(),Pose()};raw.nativeWristInWeapon=refs;
        raw.nativePartValid=true;raw.nativePartInWeapon=f.calibration->profile.closedContact;
        f.Next();f.sample.input.released[1]=release;controls.generation=f.sample.input.sequence;controls.predictedNs=f.sample.input.observedNs;
        controls.hands[1].squeeze=release?0.f:1.f;
        for(unsigned n=0;n<2;++n)controls.hands[n].grip.position={f.sample.wristWorld[n].values[3][0],f.sample.wristWorld[n].values[3][1],-f.sample.wristWorld[n].values[3][2]};
        if(!release){
            f.hands.Renew(f.sample.input,f.originalGun,{f.originalGun.contact,f.sample.input.sequence,f.sample.input.deadlineNs,true});
            f.hands.Renew(f.sample.input,f.originalSupport,{f.originalSupport.contact,f.sample.input.sequence,f.sample.input.deadlineNs,true});
        }
        Bc2AmmoReserveLease ammo;ammo.identity.owner=f.sample.nativeOwner;ammo.sequence=f.sample.input.sequence;
        ammo.observedNs=f.sample.input.observedNs;ammo.deadlineNs=f.sample.input.deadlineNs;ammo.verified=ammo.allThreeIdle=true;ammo.loaded=ammo.capacity=5;
        Bc2BoltPlayerInput input{f.sample.nativeOwner,f.sample.item,f.sample.asset,f.sample.mesh,&controls,f.sample.input,
            {f.sample.nativeOwner,f.sample.input,controls.referenceHead,Pose(),1},raw,f.native.view,ammo};
        const auto before=controls;auto out=player.Prepare(input,f.hands,f.intent);
        if(std::memcmp(&before,&controls,sizeof(controls)))return {};
        return out;
    };
    for(unsigned n=0;n<70;++n){const auto s=prepare(false);CHECK(s);player.Observe(f.bolt->Tick(*s,f.hands,f.intent));}
    CHECK(player.ReferencesReady());
    f.native.view.phase=Bc2NativeCyclePhase::Held;f.native.view.blocksFire=true;
    f.native.view.held=WeaponCycleLease{f.sample.input.owner,f.sample.item,{f.calibration->profile.id,f.sample.item.generation},1,1,1,
        f.sample.input.observedNs,f.sample.input.deadlineNs,true};
    const auto released=prepare(true);CHECK(released&&released->custodyTransfer&&released->custodyTransfer->releaseDepartingGun);
    const auto result=f.bolt->Tick(*released,f.hands,f.intent);player.Observe(result);
    CHECK(result.custodyChange&&result.custodyChange->transaction.accepted&&result.tracking.weapon&&result.blocksFire);
    CHECK(!f.hands.Current(right)&&f.hands.Current(left)->token.kind==HandClaimKind::GunHold&&player.OwnsGunCustody());
    CHECK(controls.hands[1].squeeze==0&&released->input.released[1]&&!released->custodyTransfer->nextGun.evidence.released[1]);
    f.entered=true;f.last=result;CHECK(f.Stroke()==0);player.Observe(f.last);
    CHECK(f.native.releases==1&&!f.native.acks&&player.BlocksFire());
    const auto neutral=prepare(true);CHECK(neutral&&!neutral->custodyTransfer);
    player.Observe(f.bolt->Tick(*neutral,f.hands,f.intent));
    const auto pressed=prepare(false);CHECK(pressed&&!pressed->custodyTransfer); // N-1 is still neutral.
    player.Observe(f.bolt->Tick(*pressed,f.hands,f.intent));
    const auto delayed=prepare(false);CHECK(delayed&&delayed->custodyTransfer&&!delayed->custodyTransfer->releaseDepartingGun);
    const auto returned=f.bolt->Tick(*delayed,f.hands,f.intent);player.Observe(returned);
    CHECK(returned.custodyChange&&returned.custodyChange->transaction.accepted&&returned.custodyChange->companion);
    CHECK(f.hands.Current(right)&&f.hands.Current(left)->token.prerequisiteClaim==f.hands.Current(right)->token.id&&player.BlocksFire());
    f.Ready();const auto final=prepare(false);CHECK(final);player.Observe(f.bolt->Tick(*final,f.hands,f.intent));
    CHECK(f.native.acks==1&&!player.BlocksFire());
    // A later real shot uses the retained returned gun claim; the second
    // release must not be mistaken for releasing an unsupported stale cycle.
    f.native.view.phase=Bc2NativeCyclePhase::Held;f.native.view.blocksFire=true;
    f.native.view.held=WeaponCycleLease{f.sample.input.owner,f.sample.item,{f.calibration->profile.id,f.sample.item.generation},2,2,1,
        f.sample.input.observedNs,f.sample.input.deadlineNs,true};
    const auto second=prepare(true);CHECK(second&&second->custodyTransfer&&second->custodyTransfer->releaseDepartingGun);
    const auto enteredAgain=f.bolt->Tick(*second,f.hands,f.intent);player.Observe(enteredAgain);
    CHECK(enteredAgain.custodyChange&&enteredAgain.custodyChange->transaction.accepted&&enteredAgain.tracking.weapon);
    CHECK(!f.hands.Current(right)&&f.hands.Current(left)&&player.BlocksFire());return 0;
}

int RendererAndPlayerShareCurrentBodyBase(){
    Setup f;auto eye=Pose();eye.values[3][0]=100;eye.values[3][1]=1.8f;eye.values[3][2]=200;
    const auto base=BuildTrackedBodyBase(eye,3.141592653589793f,1,{10,0,20},{.2f,0,.3f});CHECK(base);
    CHECK(Near(base->values[3][0],9.8f)&&Near(base->values[3][1],1.8f)&&Near(base->values[3][2],20.3f));
    const auto turned=BuildTrackedBodyBase(eye,4.71238898038469f,1,{10,0,20},{.2f,0,.3f});CHECK(turned);
    f.frame.bodyWorldMeters=*turned;const auto wrists=MapBoltTrackedWrists(f.raw,f.input,f.safety,f.frame);CHECK(wrists);
    CHECK(Distance((*wrists)[0],f.raw.rawWristWorldMeters[0])>10);
    CHECK(f.raw.raw.input.sequence==1&&f.raw.raw.input.deadlineNs==1100000000);
    auto bad=eye;bad.values[0][0]=0;CHECK(!BuildTrackedBodyBase(bad,0,1,{},{}));
    CHECK(!BuildTrackedBodyBase(eye,0,0,{},{}));
    CHECK(!BuildTrackedBodyBase(eye,0,1,{NAN,0,0},{}));return 0;
}
int StartupBlockDoesNotInventOwedAction(){
    auto calibration=std::make_shared<Bc2BoltCalibration>(M95AuthoredBoltCalibration(1));calibration->nativeJoined=true;
    Bc2BoltPlayerCustody player(calibration);Bc2PhysicalBoltResult blocked;blocked.blocksFire=true;
    player.Observe(blocked);CHECK(!player.BlocksFire()&&!player.NeedsOwnerRetirement());
    blocked.tracking.custody=BoltCustodyPhase::Manipulating;player.Observe(blocked);CHECK(player.BlocksFire());return 0;
}

}
int main(){CHECK(!OrdinaryBoltCompiled);if(RendererAndPlayerShareCurrentBodyBase()||StartupBlockDoesNotInventOwedAction()||RebasedHeadAndCurrentBodyAnchor()||MappingRejectsStaleOrMismatchedFrame()||StableReferencesFreezeAndRecenterInvalidates()||PlayerReleaseUsesActualAdapterAndImmutableControls())return 1;
    std::puts("6 ordinary bolt mapping/reference/real-adapter groups passed; default OFF, original packets immutable.");return 0;}
