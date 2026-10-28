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
        raw.raw={owner,{},17,safety,Pose(),true};raw.raw.rig.soldier=owner.soldier;raw.raw.rig.weak=owner.weak;raw.mappingValid=true;raw.weaponWorldMeters=Pose();raw.bodyWorldMeters=Pose();
        frame={owner,safety,input.referenceHead,Pose(),1};
        const auto wrists=MapBoltTrackedWrists(raw,input,safety,frame);if(wrists)raw.rawWristWorldMeters=*wrists;
        raw.nativeWristInWeapon=raw.rawWristWorldMeters;raw.nativePartValid=true;raw.nativePartInWeapon=Pose();
    }
    void Next(){++safety.sequence;safety.observedNs+=10000000;safety.nowNs=safety.observedNs;safety.deadlineNs=safety.observedNs+100000000;
        input.generation=safety.sequence;input.predictedNs=safety.observedNs;frame.input=safety;}
};
// Same row-vector LH -> OpenXR conversion used by the existing controller
// fixture. The round trip below proves actual mapper input, not a raw bypass.
std::optional<math::Pose> GapControllerPose(const math::Matrix4& row){
    if(!Rigid(row))return {};
    float m[3][3];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m[r][c]=row.values[c][r];
    math::Quaternion q;const float tr=m[0][0]+m[1][1]+m[2][2];
    if(tr>0){const float s=std::sqrt(tr+1)*2;q.w=.25f*s;q.x=(m[2][1]-m[1][2])/s;q.y=(m[0][2]-m[2][0])/s;q.z=(m[1][0]-m[0][1])/s;}
    else if(m[0][0]>m[1][1]&&m[0][0]>m[2][2]){const float s=std::sqrt(1+m[0][0]-m[1][1]-m[2][2])*2;q.w=(m[2][1]-m[1][2])/s;q.x=.25f*s;q.y=(m[0][1]+m[1][0])/s;q.z=(m[0][2]+m[2][0])/s;}
    else if(m[1][1]>m[2][2]){const float s=std::sqrt(1+m[1][1]-m[0][0]-m[2][2])*2;q.w=(m[0][2]-m[2][0])/s;q.x=(m[0][1]+m[1][0])/s;q.y=.25f*s;q.z=(m[1][2]+m[2][1])/s;}
    else{const float s=std::sqrt(1+m[2][2]-m[0][0]-m[1][1])*2;q.w=(m[1][0]-m[0][1])/s;q.x=(m[0][2]+m[2][0])/s;q.y=(m[1][2]+m[2][1])/s;q.z=.25f*s;}
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);if(!std::isfinite(n)||n<.5f)return {};
    math::Pose pose;pose.orientation={-q.x/n,-q.y/n,q.z/n,q.w/n};pose.position={row.values[3][0],row.values[3][1],-row.values[3][2]};
    const auto view=math::MakeLhViewFromOpenXRPose(pose);const auto round=view?InverseRigid(*view):std::nullopt;
    if(!round||Distance(*round,row)>.0005f||Angle(*round,row)>.005f)return {};return pose;
}
int PlayerMappedCustodyRenderGap(){
    // Same real player mapper + physical adapter as Gameplay, with a render
    // gap much longer than the last raw deadline and consecutive fresh input.
    for(unsigned mutation=0;mutation<16;++mutation){
        Fixture f;Bc2BoltPlayerCustody player(f.calibration);InputFrame controls;
        controls.focused=controls.headValid=true;controls.spaceGeneration=f.sample.input.owner.space;
        for(auto& h:controls.hands){h.active=Components;h.gripTracked=h.aimTracked=true;h.squeeze=1;}
        const auto originalWeapon=f.sample.weaponWorld;
        const auto originalWrist=f.sample.wristWorld[0];
        const auto attachment=Multiply(originalWeapon,*InverseRigid(originalWrist));
        Bc2BoltControllerContact cached;
        const auto prepare=[&](const math::Matrix4& part,bool held,bool missing,unsigned fault=0)->std::optional<Bc2PhysicalBoltSample>{
            auto s=f.sample.input;s.released[1]=!held;
            auto wrists=f.sample.wristWorld;
            const auto weapon=Multiply(attachment,wrists[0]);
            wrists[1]=Multiply(M95BoltWristFromPart(part),weapon);
            for(unsigned h=0;h<2;++h){const auto pose=GapControllerPose(wrists[h]);if(!pose)return {};controls.hands[h].grip=*pose;}
            controls.generation=s.sequence;controls.predictedNs=s.observedNs;controls.hands[1].squeeze=held?1.f:0.f;
            Bc2BoltControllerContact raw;
            raw.raw={f.sample.nativeOwner,f.rig.identity,f.calibration->rigFingerprint,s,M95BoltWristFromPart(part),true};
            raw.mappingValid=true;raw.units=1;raw.weaponWorldMeters=weapon;raw.bodyWorldMeters=Pose();
            raw.rawWristWorldMeters=wrists;raw.wristToGrip={Pose(),Pose()};
            raw.nativeWristInWeapon={Multiply(wrists[0],*InverseRigid(weapon)),Multiply(wrists[1],*InverseRigid(weapon))};
            raw.nativePartValid=true;raw.nativePartInWeapon=f.calibration->profile.closedContact;
            if(!missing)cached=raw;
            Bc2BoltPlayerInput in{f.sample.nativeOwner,f.sample.item,f.sample.asset,f.sample.mesh,&controls,s,
                {f.sample.nativeOwner,s,controls.referenceHead,Pose(),1},missing?Bc2BoltControllerContact{}:raw,f.native.view,{}};
            switch(fault){
            case 1:++in.owner.weapon;break;
            case 2:++in.safety.owner.space;++controls.spaceGeneration;in.body.input=in.safety;break;
            case 3:controls.hands[0].gripTracked=false;in.safety.tracked[0]=false;in.body.input=in.safety;break;
            case 4:controls.hands[1].gripTracked=false;in.safety.tracked[1]=false;in.body.input=in.safety;break;
            case 5:controls.focused=false;in.safety.focused=false;in.body.input=in.safety;break;
            case 6:in.safety.nowNs=in.safety.deadlineNs;in.body.input=in.safety;break;
            case 7:in.contact=raw;++in.contact.raw.nativeOwner.weapon;break;
            case 8:in.body.bodyWorldMeters.values[0][0]=2;break;
            case 9:++in.body.input.sequence;break;
            case 10:controls.worldUnitsPerMeter=2;break;
            case 11:controls.hands[0].squeeze=0;in.safety.released[0]=true;in.body.input=in.safety;break;
            case 14:in.contact=raw;++in.contact.raw.rig.soldier;break;
            case 15:in.contact=raw;++in.contact.raw.rig.weak;break;
            }
            return player.Prepare(in,f.hands,f.intent);
        };
        // Establish the ordinary idle identity before the first native shot,
        // as production does. Starting with Held would correctly mark unknown
        // prior-actor debt for retirement instead of inventing initial custody.
        const auto heldView=f.native.view;
        f.native.view.phase=Bc2NativeCyclePhase::Watching;f.native.view.blocksFire=false;f.native.view.held.reset();
        const auto initial=prepare(f.calibration->profile.closedContact,true,false);
        CHECK(initial&&!initial->cancel&&!player.NeedsOwnerRetirement());
        f.native.view=heldView;
        CHECK(f.Enter()==0);player.Observe(f.last);
        const auto root=f.hands.Current(left)->token;
        const auto tick=[&](const math::Matrix4& part,bool held,bool missing)->bool{
            f.Next();auto s=prepare(part,held,missing);if(!s)return false;
            const auto before=f.native.controls;const auto life=f.bolt->UpdateGunHold(*s,f.hands);
            if(!life||!life->inputValid||f.native.controls!=before)return false;
            f.hands.Update(s->input);f.last=f.bolt->Tick(*s,f.hands,f.intent);player.Observe(f.last);
            return bool(f.last.tracking.weapon)&&f.last.blocksFire;
        };
        CHECK(tick(f.calibration->profile.closedContact,false,false));
        CHECK(tick(f.calibration->profile.closedContact,true,false));
        CHECK(f.last.ownsMechanism&&f.last.tracking.target);
        const auto mechanism=f.hands.Current(right)->token;
        const auto originalDeadline=cached.raw.input.deadlineNs;
        // Missing raw never becomes a new stamped raw. Stationary fresh hands
        // retain the same claims without advancing a bolt stroke or calling it.
        for(unsigned n=0;n<24;++n){
            CHECK(tick(f.calibration->profile.closedContact,true,true));
            CHECK(f.hands.Current(left)->token==root&&f.hands.Current(right)->token==mechanism);
            CHECK(!f.last.tracking.rawContactValid&&f.last.tracking.rawInputSequence==0);
            CHECK(f.last.tracking.currentContactGun&&*f.last.tracking.currentContactGun==root&&
                  f.last.tracking.currentContactInput==f.sample.input.sequence&&
                  f.last.tracking.currentContactObserved==f.sample.input.observedNs&&
                  f.last.tracking.currentContactDeadline==f.sample.input.deadlineNs);
            CHECK(f.last.tracking.mechanismPhase==WeaponCyclePhase::Unlock);
            CHECK(!f.native.releases&&!f.native.acks&&!f.native.cancels);
        }
        CHECK(f.sample.input.nowNs>originalDeadline&&cached.raw.input.deadlineNs==originalDeadline);
        const auto heldBefore=f.native.view.held;
        const auto duplicate=prepare(f.calibration->profile.closedContact,true,true);CHECK(duplicate);
        const auto deadlineBefore=f.hands.Current(left)->deadlineNs;
        f.last=f.bolt->Tick(*duplicate,f.hands,f.intent);player.Observe(f.last);
        CHECK(f.last.ownsMechanism&&f.last.tracking.mechanismPhase==WeaponCyclePhase::Unlock);
        CHECK(f.hands.Current(left)->deadlineNs==deadlineBefore&&f.native.view.held==heldBefore);
        auto strict=cached;CHECK(!MapBoltTrackedWrists(strict,controls,f.sample.input,
            {f.sample.nativeOwner,f.sample.input,controls.referenceHead,Pose(),1}));
        if(mutation){
            f.Next();
            if(mutation==12){auto stale=f.sample.input;stale.nowNs=f.hands.Current(left)->deadlineNs;f.hands.Update(stale);}
            auto s=prepare(f.calibration->profile.closedContact,true,true,mutation);
            if(mutation==11||mutation==13){CHECK(s);
                if(mutation==13){CHECK(s->controllerCustody);++s->controllerCustody->id;}
                f.bolt->UpdateGunHold(*s,f.hands);f.last=f.bolt->Tick(*s,f.hands,f.intent);
                CHECK(!f.last.ownsMechanism&&!f.hands.Current(right));}
            else CHECK(!s);
            CHECK(!f.native.releases&&!f.native.acks&&player.BlocksFire());continue;
        }
        // A resumed publication does not rewind the current contact source.
        CHECK(tick(f.calibration->profile.closedContact,true,false));
        CHECK(f.hands.Current(right)->token==mechanism);
        const auto& p=f.calibration->profile;
        const auto step=[&](float travel,float angle){return tick(weapon_cycle_detail::Target(p,travel,angle),true,true);};
        for(unsigned n=1;n<=4;++n)CHECK(step(0,p.unlockRadians*float(n)/4));
        for(unsigned n=0;n<5;++n)CHECK(step(0,p.unlockRadians));
        for(unsigned n=1;n<=8;++n)CHECK(step(p.stroke*float(n)/8,p.unlockRadians));
        for(unsigned n=0;n<5;++n)CHECK(step(p.stroke,p.unlockRadians));
        for(unsigned n=1;n<=8;++n)CHECK(step(p.stroke*(1.f-float(n)/8),p.unlockRadians));
        for(unsigned n=0;n<5;++n)CHECK(step(0,p.unlockRadians));
        for(unsigned n=1;n<=4;++n)CHECK(step(0,p.unlockRadians*(1.f-float(n)/4)));
        for(unsigned n=0;n<5;++n)CHECK(step(0,0));
        CHECK(f.native.releases==1&&!f.native.acks&&f.last.blocksFire&&f.hands.Current(left)->token==root);
        CHECK(f.last.tracking.mechanismPhase==WeaponCyclePhase::AwaitingNative);
        // No actual ready receipt means no invented completion or new Fire.
        CHECK(tick(p.closedContact,false,true));CHECK(!f.native.acks&&player.BlocksFire());
        CHECK(tick(p.closedContact,true,true));CHECK(!f.last.custodyChange&&!f.native.acks&&player.BlocksFire());
    }
    return 0;
}

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
int PlayerReleaseUsesActualAdapterAndImmutableControls(bool expireGap=false){
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
    const auto prepare=[&](bool release,unsigned contactMutation=0)->std::optional<Bc2PhysicalBoltSample>{
        Bc2BoltControllerContact raw;raw.raw={f.sample.nativeOwner,f.rig.identity,f.calibration->rigFingerprint,f.sample.input,Pose(),true};
        raw.raw.mechanismWristInWeapon=M95BoltWristFromPart(f.calibration->profile.closedContact);
        raw.mappingValid=true;raw.bodyWorldMeters=Pose();raw.weaponWorldMeters=originalWeapon;
        raw.rawWristWorldMeters=f.sample.wristWorld;raw.wristToGrip={Pose(),Pose()};raw.nativeWristInWeapon=refs;
        raw.nativePartValid=true;raw.nativePartInWeapon=f.calibration->profile.closedContact;
        f.Next();f.sample.input.released[1]=release;controls.generation=f.sample.input.sequence;controls.predictedNs=f.sample.input.observedNs;
        controls.hands[1].squeeze=release?0.f:1.f;
        auto currentWrists=f.sample.wristWorld;
        if(player.OwnsGunCustody())currentWrists[1]=Multiply(M95BoltWristFromPart(f.calibration->profile.closedContact),originalWeapon);
        for(unsigned n=0;n<2;++n){const auto pose=GapControllerPose(currentWrists[n]);if(!pose)return {};controls.hands[n].grip=*pose;}
        if(!release){
            f.hands.Renew(f.sample.input,f.originalGun,{f.originalGun.contact,f.sample.input.sequence,f.sample.input.deadlineNs,true});
            f.hands.Renew(f.sample.input,f.originalSupport,{f.originalSupport.contact,f.sample.input.sequence,f.sample.input.deadlineNs,true});
        }
        Bc2AmmoReserveLease ammo;ammo.identity.owner=f.sample.nativeOwner;ammo.sequence=f.sample.input.sequence;
        ammo.observedNs=f.sample.input.observedNs;ammo.deadlineNs=f.sample.input.deadlineNs;ammo.verified=ammo.allThreeIdle=true;ammo.loaded=ammo.capacity=5;
        Bc2BoltPlayerInput input{f.sample.nativeOwner,f.sample.item,f.sample.asset,f.sample.mesh,&controls,f.sample.input,
            {f.sample.nativeOwner,f.sample.input,controls.referenceHead,Pose(),1},raw,f.native.view,ammo};
        if(contactMutation==1)input.contact={};
        if(contactMutation==2)++input.contact.raw.nativeOwner.weapon;
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
    CHECK(!f.hands.Current(right)&&f.hands.Current(left)&&player.BlocksFire());
    // Actual252 entered the second cycle, then stalled at the new grip edge.
    // Keep the real player coordinator and delayed renderer packet in the loop
    // beyond the atomic custody exchange, rather than stopping at EnterCustody.
    for(unsigned n=0;n<8;++n){const auto s=prepare(true);CHECK(s);
        const auto r=f.bolt->Tick(*s,f.hands,f.intent);player.Observe(r);
        CHECK(r.tracking.weapon&&!r.ownsMechanism);}
    for(unsigned n=0;n<2;++n){const auto s=prepare(false);CHECK(s);
        f.last=f.bolt->Tick(*s,f.hands,f.intent);player.Observe(f.last);}
    CHECK(f.last.ownsMechanism&&f.last.tracking.target);
    CHECK(f.last.tracking.target->lease.cycle==2&&f.native.releases==1);
    // Actual254 lost the contact publication for one update while the gun
    // and mechanism claims stayed live. Empty publication is not a new owner.
    // Continue only using the previous original contact's remaining lifetime.
    const auto originalRaw=f.last.tracking.rawInputSequence;
    const auto gap=prepare(false,1);CHECK(gap&&!gap->raw.valid&&gap->controllerCustody);
    const auto originalDeadline=gap->raw.input.deadlineNs;
    f.last=f.bolt->Tick(*gap,f.hands,f.intent);player.Observe(f.last);
    CHECK(f.last.ownsMechanism&&f.last.tracking.weapon&&player.ReferencesReady());
    CHECK(!f.last.tracking.rawContactValid&&f.last.tracking.rawInputSequence==0&&f.native.releases==1&&f.native.acks==1);
    const auto repeatedGap=prepare(false,1);CHECK(repeatedGap);
    CHECK(!repeatedGap->raw.valid&&repeatedGap->raw.input.deadlineNs==originalDeadline&&repeatedGap->controllerCustody);
    f.last=f.bolt->Tick(*repeatedGap,f.hands,f.intent);player.Observe(f.last);
    CHECK(f.last.ownsMechanism&&f.last.tracking.weapon);
    if(expireGap){
        for(unsigned n=0;n<20;++n)f.Next();
        f.hands.Update(f.sample.input);
        CHECK(!prepare(false,1)); // Repeated reads never renew cached evidence.
    }else{
        const auto resumed=prepare(false);CHECK(resumed&&resumed->raw.input.sequence>originalRaw);
        f.last=f.bolt->Tick(*resumed,f.hands,f.intent);player.Observe(f.last);
        CHECK(f.last.ownsMechanism&&f.last.tracking.weapon);
        CHECK(!prepare(false,2)); // A foreign publication is never hidden by cache.
    }
    CHECK(player.NeedsOwnerRetirement()&&player.BlocksFire()&&!player.ReferencesReady());
    return 0;
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

int SameLeftCustodyRecoveryRequiresTypedAuthority(){
    for(unsigned mutation=0;mutation<6;++mutation){Setup f;
        auto calibration=std::make_shared<Bc2BoltCalibration>(M95AuthoredBoltCalibration(1));calibration->nativeJoined=true;calibration->rigFingerprint=17;
        f.raw.nativePartInWeapon=calibration->profile.closedContact;
        Bc2BoltPlayerCustody player(calibration);HandInteraction hands;std::uint64_t intent=0;
        Bc2BoltPlayerInput in;in.owner=f.owner;in.item={f.owner.weapon,3};in.asset=calibration->asset;in.mesh=calibration->mesh;
        in.native=Bc2NativeCycleView{};in.native->native.owner=f.owner;
        Bc2AmmoReserveLease ammo;ammo.identity.owner=f.owner;ammo.verified=ammo.allThreeIdle=true;ammo.capacity=5;ammo.loaded=5;ammo.reserve=45;
        const auto sync=[&]{f.raw.raw.input=f.safety;in.controls=&f.input;in.safety=f.safety;in.body=f.frame;in.contact=f.raw;
            ammo.sequence=f.safety.sequence;ammo.observedNs=f.safety.observedNs;ammo.deadlineNs=f.safety.deadlineNs;in.ammunition=ammo;};
        for(unsigned n=0;n<70;++n){f.Next();sync();CHECK(player.Prepare(in,hands,intent));}
        CHECK(player.ReferencesReady());const auto references=*player.References();
        hands.Update(f.safety);const auto acquired=hands.Acquire(f.safety,{f.safety.owner,InteractionHand::Left,HandClaimKind::GunHold,in.item,
            {{1,3},f.safety.sequence,f.safety.deadlineNs,true},++intent,0});CHECK(acquired.claim);
        WeaponCycleLease held{f.safety.owner,in.item,{calibration->profile.id,3},1,1,50,f.safety.observedNs,f.safety.deadlineNs,true};
        Bc2PhysicalBoltResult owned;owned.tracking.enabled=true;owned.tracking.held=held;owned.tracking.gun=acquired.claim;
        owned.tracking.custody=BoltCustodyPhase::Manipulating;player.Observe(owned);
        f.Next();f.safety.tracked[1]=f.input.hands[1].gripTracked=false;sync();CHECK(!player.Prepare(in,hands,intent));
        CHECK(player.NeedsOwnerRetirement()&&!player.ReferencesReady());
        f.Next();f.safety.tracked[1]=f.input.hands[1].gripTracked=true;f.safety.released[1]=true;f.input.hands[1].squeeze=0;f.frame.input=f.safety;sync();
        CHECK(hands.Renew(f.safety,acquired.claim->token,{{1,3},f.safety.sequence,f.safety.deadlineNs,true}).accepted);
        WeaponCycleDebt debt{held.owner,held.item,held.mechanism,held.cycle,held.shot,77,4,45,5};
        WeaponCycleRecoveryGrant grant{debt,f.safety,1};
        WeaponCycleIdleDebtLease authority{held.owner,held.item,held.mechanism,1,1,106,
            f.safety.observedNs,f.safety.deadlineNs,grant,{101,102,103},{104,105,106}};
        CHECK(weapon_cycle_detail::Lease(authority,f.safety.nowNs));
        in.recovery=Bc2NativeCycleRecoveryView{};in.recovery->native.owner=f.owner;in.recovery->authority=authority;
        if(mutation==1)in.recovery.reset();if(mutation==2)++in.recovery->authority->shot;
        if(mutation==3)++in.recovery->native.owner.weapon;
        if(mutation==4){CHECK(hands.Release(f.safety,acquired.claim->token).accepted);}
        if(mutation==5)in.recovery->authority->deadlineNs=f.safety.nowNs;
        const auto prepared=player.Prepare(in,hands,intent);CHECK(prepared);
        if(!mutation){CHECK(!prepared->cancel&&!player.NeedsOwnerRetirement()&&player.ReferencesReady()&&player.BlocksFire());
            CHECK(Distance((*player.References())[0],references[0])<.000001f);}
        else CHECK(prepared->cancel&&player.NeedsOwnerRetirement()&&player.BlocksFire());
    }return 0;
}
int PlayerCurrentCustodyOverlap(){
    // Exercise the real player mapper's gunContacts, then the adapter's exact
    // held root before the same generic Update used by Gameplay.
    for(unsigned mutation=0;mutation<9;++mutation){
        Fixture f;Bc2BoltPlayerCustody player(f.calibration);InputFrame controls;
        controls.focused=controls.headValid=true;controls.spaceGeneration=f.sample.input.owner.space;
        for(auto& hand:controls.hands){hand.active=Components;hand.gripTracked=hand.aimTracked=true;hand.squeeze=1;}
        const auto inverse=InverseRigid(f.sample.weaponWorld);CHECK(inverse);
        const std::array<math::Matrix4,2> refs={Multiply(f.sample.wristWorld[0],*inverse),Multiply(f.sample.wristWorld[1],*inverse)};
        const auto prepare=[&](const HandInteractionSample& s,bool idle)->std::optional<Bc2PhysicalBoltSample>{
            controls.generation=s.sequence;controls.predictedNs=s.observedNs;
            controls.hands[1].squeeze=s.released[1]?0.f:1.f;
            for(unsigned n=0;n<2;++n)controls.hands[n].grip.position={f.sample.wristWorld[n].values[3][0],f.sample.wristWorld[n].values[3][1],-f.sample.wristWorld[n].values[3][2]};
            Bc2BoltControllerContact raw;raw.raw={f.sample.nativeOwner,f.rig.identity,f.calibration->rigFingerprint,s,Pose(),true};
            raw.raw.mechanismWristInWeapon=M95BoltWristFromPart(f.calibration->profile.closedContact);
            raw.mappingValid=true;raw.bodyWorldMeters=Pose();raw.weaponWorldMeters=f.sample.weaponWorld;
            raw.rawWristWorldMeters=f.sample.wristWorld;raw.wristToGrip={Pose(),Pose()};raw.nativeWristInWeapon=refs;
            raw.nativePartValid=true;raw.nativePartInWeapon=f.calibration->profile.closedContact;
            auto native=f.native.view;if(idle){native.phase=Bc2NativeCyclePhase::Watching;native.blocksFire=false;native.held.reset();}
            Bc2BoltPlayerInput input{f.sample.nativeOwner,f.sample.item,f.sample.asset,f.sample.mesh,&controls,s,
                {f.sample.nativeOwner,s,controls.referenceHead,Pose(),1},raw,native,{}};
            return player.Prepare(input,f.hands,f.intent);
        };
        CHECK(prepare(f.sample.input,true));CHECK(f.Enter()==0);player.Observe(f.last);
        const auto held=f.hands.Current(left);CHECK(held);const auto token=held->token;
        auto current=f.sample.input;++current.sequence;
        current.observedNs=held->deadlineNs-19889800;current.nowNs=held->deadlineNs+1889900;current.deadlineNs=current.observedNs+100*ms;
        auto prepared=prepare(current,false);CHECK(prepared&&!prepared->cancel&&!prepared->custodyTransfer);
        CHECK(prepared->gunContacts[0].key==token.contact&&prepared->gunContacts[0].inputSequence==current.sequence&&
              prepared->gunContacts[0].deadlineNs==current.deadlineNs&&prepared->gunContacts[0].eligible);
        const auto controlsBefore=f.native.controls,releases=f.native.releases,acks=f.native.acks,cancels=f.native.cancels;
        switch(mutation){
        case 1:f.hands.Update(current);break; // Historical caller order cannot be repaired afterward.
        case 2:prepared->cancel=true;break;
        case 3:prepared->custodyTransfer=HandGunCustodyTransfer{};break;
        case 4:++prepared->gunContacts[0].key.id;break;
        case 5:--prepared->gunContacts[0].inputSequence;break;
        case 6:++prepared->gunContacts[0].deadlineNs;break;
        case 7:++prepared->nativeOwner.weapon;break;
        case 8:prepared->wristWorld[0].values[0][0]=2;break;
        }
        const auto lifecycle=f.bolt->UpdateGunHold(*prepared,f.hands);CHECK(lifecycle);
        CHECK(f.native.controls==controlsBefore&&f.native.releases==releases&&f.native.acks==acks&&f.native.cancels==cancels);
        if(!mutation){CHECK(lifecycle->inputValid&&f.hands.Current(left)&&f.hands.Current(left)->token==token);
            CHECK(f.hands.Current(left)->inputSequence==current.sequence&&f.hands.Current(left)->deadlineNs==current.deadlineNs);
            CHECK(f.hands.Update(current).inputValid&&f.hands.Current(left)->token==token&&!f.hands.Current(right));}
        else CHECK(!f.hands.Current(left)&&!f.hands.Current(right));
    }
    return 0;
}

}
int main(){if(PlayerMappedCustodyRenderGap())return 1;if(PlayerCurrentCustodyOverlap())return 1;if(SameLeftCustodyRecoveryRequiresTypedAuthority())return 1;CHECK(!OrdinaryBoltCompiled);if(RendererAndPlayerShareCurrentBodyBase()||StartupBlockDoesNotInventOwedAction()||RebasedHeadAndCurrentBodyAnchor()||MappingRejectsStaleOrMismatchedFrame()||StableReferencesFreezeAndRecenterInvalidates()||PlayerReleaseUsesActualAdapterAndImmutableControls()||PlayerReleaseUsesActualAdapterAndImmutableControls(true))return 1;
    std::puts("7 ordinary bolt mapping/reference/recovery/real-adapter groups passed; default OFF, original packets immutable.");return 0;}
