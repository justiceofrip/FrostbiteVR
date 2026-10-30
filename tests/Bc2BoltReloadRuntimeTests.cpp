#include "Bc2BoltPlayerCustody.h"
#include "Bc2BoltRuntimeGeometry.h"
#include "fvr/interaction/WeaponCycleConvergence.h"
#define main ExistingPhysicalBoltRegressionMain
#include "Bc2PhysicalBoltTests.cpp"
#undef main

namespace {
// Deterministic callbacks only. Receipts below pass the real ammunition ledger
// and convergence policy; none of this fixture is native/game evidence.
struct ReloadCallbacks {
    Native* base=nullptr;WeaponCycleConvergence<ReloadHoldIdentity> convergence;AmmunitionLedger ledger;
    std::optional<WeaponCycleRecoveryGrant> grant;std::optional<WeaponCycleReloadPause> pause;
    AmmoResourceContext resource{};std::uint64_t invocation=100,revision=1,sequence=100;
    unsigned pauses=0,resumes=0,submits=0;bool prepared=false;
    bool Round(std::int64_t begin,std::int64_t now){const auto d=convergence.Debt();
        for(unsigned b=0;b<3;++b){const auto at=begin+b*100000;
            if(!convergence.ObserveIdle(base->view.native,{b,++invocation,d.guardEpoch,at,at+50000,at+80000000,
                d.loaded,d.reserve,d.capacity},now))return false;}return true;}
    void Control(const Bc2NativeCycleControl& c){
        if(!prepared)return;
        Round(c.input.nowNs-4000000,c.input.nowNs);Round(c.input.nowNs-2000000,c.input.nowNs);
        if(!grant)grant=convergence.ArmRecovery(base->view.native,c.input,{right,left});
    }
    bool Ammo(AmmunitionOperation operation,std::int64_t now){
        const auto permit=convergence.ReloadPermit(base->view.native,now);if(!permit)return false;
        const auto d=convergence.Debt();AmmunitionSnapshot snapshot{resource,++sequence,now,now+80*ms,{d.loaded,d.reserve,d.capacity},true};
        if(!ledger.Bind(snapshot,now))return false;
        const auto command=ledger.Queue(operation,snapshot,now);
        if(!command||!ledger.Dispatch(snapshot,now)||
           !convergence.BeginReloadCommand(base->view.native,*permit,*command,{++revision,now},now))return false;
        WeaponCycleReloadAmmoRebase receipt;receipt.dispatched=*convergence.ReloadCommand();
        receipt.receipt={*command,900+command->id,now+1,now+100,command->before,command->after,true,true};
        for(unsigned b=0;b<3;++b)receipt.ownUpdates[b]={b,++invocation,d.guardEpoch,now+10+b*10,now+11+b*10,
            now+80*ms,command->after.loaded,command->after.reserve,command->after.capacity};
        receipt.drain={++revision,now+100};receipt.guardEpoch=d.guardEpoch+1;
        if(!ledger.Complete(receipt.receipt,now+100)||!convergence.ApplyReloadReceipt(base->view.native,receipt,now+100))return false;
        if(operation==AmmunitionOperation::RemoveMagazine)return ledger.Original()&&ledger.Discard(*ledger.Original());
        return true;
    }
    Bc2PhysicalBoltApi Api(){auto api=Bc2PhysicalBoltApi{};api.context=this;
        api.controlResult=[](void* context,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<ReloadCallbacks*>(context);
            n.Control(c);return n.base->Api().controlResult(n.base,c);};
        api.view=[](void* context,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<ReloadCallbacks*>(context)->base->view;};
        api.ack=[](void* context,const WeaponCycleReady& r)noexcept{auto& n=*static_cast<ReloadCallbacks*>(context);return n.base->Api().ack(n.base,r);};
        api.cancel=[](void* context)noexcept{++static_cast<ReloadCallbacks*>(context)->base->cancels;};
        api.pauseReload=[](void* context,const Bc2NativeCycleControl& c,const WeaponCycleReloadPause& p)noexcept{
            auto& n=*static_cast<ReloadCallbacks*>(context);++n.pauses;
            if(n.prepared)return Bc2NativeCycleSubmitResult::Rejected;
            const WeaponCycleDebt debt{p.owner,p.item,p.mechanism,p.cycle,p.shot,60,4,10,5};
            n.resource={{p.owner.actor,p.owner.actorGeneration,p.item.id,6},p.owner.equipGeneration,p.owner.space};
            if(!n.convergence.Begin(n.base->view.native,debt,c.input.nowNs-1)||
               !n.convergence.PrepareReload(n.base->view.native,p,n.resource,{n.revision,c.input.nowNs},c.input.nowNs))return Bc2NativeCycleSubmitResult::Rejected;
            n.pause=p;n.prepared=true;n.base->view.held.reset();n.base->view.phase=Bc2NativeCyclePhase::Cancelled;
            return Bc2NativeCycleSubmitResult::Accepted;};
        api.resumeReload=[](void* context,const Bc2NativeCycleControl& c,const WeaponCycleIdleDebtLease& a)noexcept{
            auto& n=*static_cast<ReloadCallbacks*>(context);
            if(!n.convergence.ResumeReload(n.base->view.native,a,c.input.nowNs))return Bc2NativeCycleSubmitResult::Rejected;
            ++n.resumes;return Bc2NativeCycleSubmitResult::Accepted;};
        api.recovery.view=[](void* context,std::int64_t now)noexcept->std::optional<Bc2NativeCycleRecoveryView>{
            auto& n=*static_cast<ReloadCallbacks*>(context);if(!n.prepared)return {};
            Bc2NativeCycleRecoveryView view;view.native=n.base->view.native;
            if(n.grant)view.authority=n.convergence.RecoveryAuthority(view.native,*n.grant,now);return view;};
        api.recovery.submit=[](void* context,const WeaponCycleIdleDebtRelease& r,const HandInteractionSample& s)noexcept{
            auto& n=*static_cast<ReloadCallbacks*>(context);if(!n.convergence.SubmitRecovered(n.base->view.native,r,s.nowNs))return false;
            ++n.submits;return true;};
        api.recovery.ack=[](void*,const WeaponCycleIdleDebtReady&)noexcept{return false;};return api;
    }
};
struct ReloadFixture:Fixture {
    ReloadCallbacks callbacks;Bc2BoltPlayerCustody player{calibration};InputFrame controls;
    Bc2BoltControllerContact raw{};std::array<math::Matrix4,2> references{};math::Matrix4 renderedWeapon=sample.weaponWorld;
    ReloadFixture(){callbacks.base=&native;bolt=std::make_unique<Bc2PhysicalBolt>(calibration,callbacks.Api());
        controls.focused=controls.headValid=true;controls.spaceGeneration=sample.input.owner.space;
        for(auto& hand:controls.hands){hand.active=Components;hand.gripTracked=hand.aimTracked=true;hand.squeeze=1;}
        for(unsigned h=0;h<2;++h)references[h]=Multiply(sample.wristWorld[h],*InverseRigid(sample.weaponWorld));
    }
    std::optional<Bc2PhysicalBoltSample> Prepare(bool held,bool idle=false,const Bc2BoltControllerContact* rendered=nullptr){
        sample.input.released[1]=!held;controls.generation=sample.input.sequence;controls.predictedNs=sample.input.observedNs;
        controls.hands[1].squeeze=held?1.f:0.f;
        for(unsigned h=0;h<2;++h)controls.hands[h].grip.position={sample.wristWorld[h].values[3][0],
            sample.wristWorld[h].values[3][1],-sample.wristWorld[h].values[3][2]};
        raw.raw={sample.nativeOwner,rig.identity,calibration->rigFingerprint,sample.input,calibration->wristFromPart,true};
        raw.mappingValid=true;raw.units=1;raw.weaponWorldMeters=renderedWeapon;raw.bodyWorldMeters=Identity();
        raw.rawWristWorldMeters=sample.wristWorld;raw.wristToGrip={Identity(),Identity()};raw.nativeWristInWeapon=references;
        raw.nativePartValid=true;raw.nativePartInWeapon=calibration->profile.closedContact;
        auto view=native.view;if(idle){view.held.reset();view.blocksFire=false;view.phase=Bc2NativeCyclePhase::Watching;}
        Bc2BoltPlayerInput input{sample.nativeOwner,sample.item,sample.asset,sample.mesh,&controls,sample.input,
            {sample.nativeOwner,sample.input,controls.referenceHead,Identity(),1},rendered?*rendered:raw,view};
        if(idle){Bc2AmmoReserveLease ammo;ammo.verified=ammo.allThreeIdle=true;ammo.identity.owner=sample.nativeOwner;
            ammo.sequence=sample.input.sequence;ammo.observedNs=sample.input.observedNs;ammo.deadlineNs=sample.input.deadlineNs;
            ammo.loaded=4;ammo.capacity=5;ammo.reserve=10;input.ammunition=ammo;}
        return player.Prepare(input,hands,intent);
    }
    bool Run(bool held,const Bc2BoltControllerContact* rendered=nullptr){
        const auto prepared=Prepare(held,false,rendered);if(!prepared)return false;
        sample=*prepared;last=bolt->Tick(sample,hands,intent);player.Observe(last);return true;
    }
    int Open(){
        for(unsigned n=0;n<65;++n){Next();CHECK(Prepare(true,true));
            for(unsigned h=0;h<2;++h){const auto c=hands.Current(static_cast<InteractionHand>(h));CHECK(c);
                CHECK(hands.Renew(sample.input,c->token,{c->token.contact,sample.input.sequence,sample.input.deadlineNs,true}).accepted);}}
        CHECK(player.ReferencesReady());CHECK(Enter()==0);player.Observe(last);
        Tick(calibration->profile.closedContact,false);Tick(calibration->profile.closedContact,true);
        const auto& p=calibration->profile;
        for(unsigned n=1;n<=4;++n)Tick(weapon_cycle_detail::Target(p,0,p.unlockRadians*n/4.f));
        for(unsigned n=0;n<5;++n)Tick(weapon_cycle_detail::Target(p,0,p.unlockRadians));
        for(unsigned n=1;n<=8;++n)Tick(weapon_cycle_detail::Target(p,p.stroke*n/8.f,p.unlockRadians));
        for(unsigned n=0;n<5;++n)Tick(weapon_cycle_detail::Target(p,p.stroke,p.unlockRadians));
        CHECK(last.tracking.mechanismPhase==WeaponCyclePhase::Forward);player.Observe(last);
        Next();CHECK(Run(false));CHECK(last.tracking.custody==BoltCustodyPhase::ReloadPaused&&last.tracking.reloadPause);
        CHECK(BuildPausedBoltPart(last.tracking,rig,last.tracking.weapon->weaponInWorld,1,sample.input.nowNs));return 0;
    }
    int ReturnWithNMinusOne(){
        const auto neutral=raw;Next();CHECK(Run(true,&neutral));
        CHECK(last.tracking.custody==BoltCustodyPhase::ReloadPaused&&!last.custodyChange);
        const auto press=raw;Next();CHECK(Run(true,&press));
        CHECK(sample.raw.input.sequence==press.raw.input.sequence&&sample.raw.input.deadlineNs==press.raw.input.deadlineNs);
        CHECK(last.custodyChange&&last.custodyChange->transaction.accepted&&last.tracking.custody==BoltCustodyPhase::Reloading);
        CHECK(hands.Current(right)&&!hands.Current(left)&&!last.ownsMechanism);return 0;
    }
};
int NMinusOnePressCannotOutliveReleaseOrItsOriginalDeadline(){
    for(unsigned variant=0;variant<3;++variant){ReloadFixture f;CHECK(f.Open()==0);
        const auto neutral=f.raw;f.Next();CHECK(f.Run(true,&neutral));const auto press=f.raw;
        if(variant==0){f.Next();CHECK(f.Run(false,&press));}
        if(variant==1){for(unsigned n=0;n<16;++n){f.Next();CHECK(f.Run(true,&neutral));}}
        if(variant==2){auto foreign=press;++foreign.raw.input.owner.space;f.Next();CHECK(!f.Prepare(true,false,&foreign));continue;}
        f.Next();const auto prepared=f.Prepare(variant!=0,false,&press);CHECK(prepared&&!prepared->custodyTransfer);
        CHECK(f.callbacks.pauses==1&&!f.callbacks.resumes&&!f.native.releases);
    }return 0;
}
int ReloadResumeKeepsOpenPartUntilRealRegrip(){
    ReloadFixture f;CHECK(f.Open()==0&&f.ReturnWithNMinusOne()==0);
    CHECK(f.callbacks.Ammo(AmmunitionOperation::RemoveMagazine,f.sample.input.nowNs));
    f.Next();CHECK(f.Run(true));CHECK(f.callbacks.Ammo(AmmunitionOperation::RefillMagazine,f.sample.input.nowNs));
    f.Next();CHECK(f.Run(true));
    const auto support=f.hands.Acquire(f.sample.input,f.Request(left,HandClaimKind::WeaponSupport,2,f.hands.Current(right)->token.id));CHECK(support.claim);
    // The renderer is absent on the real release. Established controller/root
    // mapping supplies current geometry before the atomic release transaction.
    const Bc2BoltControllerContact missing{};f.Next();CHECK(f.Run(false,&missing));
    CHECK(f.last.custodyChange&&f.last.custodyChange->transaction.accepted);
    CHECK(f.last.tracking.custody==BoltCustodyPhase::ReloadResumePending&&!f.hands.Current(right));
    f.Next();CHECK(f.Run(false,&missing));
    CHECK(f.callbacks.resumes==1&&f.last.tracking.mechanismPhase==WeaponCyclePhase::AwaitingGrip);
    CHECK(f.last.tracking.reloadMechanicalPose&&!f.last.tracking.reloadPause&&!f.last.tracking.target&&!f.last.tracking.recoveryTarget);
    CHECK(f.last.tracking.recovery&&f.last.tracking.recovery->grant.debt.loaded==5&&f.last.blocksFire&&!f.last.ownsMechanism);
    const auto pose=*f.last.tracking.reloadMechanicalPose;
    for(unsigned n=0;n<5;++n){f.Next();CHECK(f.Run(false,&missing));
        const auto& t=f.last.tracking;CHECK(t.reloadMechanicalPose==pose&&t.mechanismPhase==WeaponCyclePhase::AwaitingGrip);
        const auto part=BuildPausedBoltPart(t,f.rig,t.weapon->weaponInWorld,1,f.sample.input.nowNs);CHECK(part);
        CHECK(Equal(part->transform,Multiply(weapon_cycle_detail::Target(f.calibration->profile,pose.pause.travel,pose.pause.rotation),t.weapon->weaponInWorld)));
        CHECK(!BuildBoltPresentation(t,f.rig,t.weapon->weaponInWorld,1,f.sample.input.nowNs));
    }
    for(unsigned variant=0;variant<6;++variant){auto bad=f.last.tracking;
        switch(variant){case 0:++bad.reloadMechanicalPose->authority.grant.debt.ammunitionRebase->pause;break;
        case 1:++bad.reloadMechanicalPose->gun.id;break;case 2:++bad.reloadMechanicalPose->cycleDeadlineNs;break;
        case 3:bad.mechanismPhase=WeaponCyclePhase::Forward;break;case 4:bad.recovery->deadlineNs=f.sample.input.nowNs;break;
        case 5:bad.reloadMechanicalPose->pause.rotation=std::numeric_limits<float>::quiet_NaN();break;}
        CHECK(!BuildPausedBoltPart(bad,f.rig,bad.weapon->weaponInWorld,1,f.sample.input.nowNs));}
    // Use the real physical adapter's fresh raw contact at the retained open
    // location. No guided hand target exists until it acquires Mechanism.
    f.Next();f.sample.input.released[1]=false;f.sample.grip=true;f.sample.reloadIntent.reset();f.sample.custodyTransfer.reset();f.sample.controllerCustody.reset();
    f.sample.raw={f.sample.nativeOwner,f.rig.identity,f.calibration->rigFingerprint,f.sample.input,
        M95BoltWristFromPart(weapon_cycle_detail::Target(f.calibration->profile,pose.pause.travel,pose.pause.rotation)),true};
    const auto gun=f.hands.Current(left);CHECK(gun);f.sample.gunContacts[0]={gun->token.contact,f.sample.input.sequence,f.sample.input.deadlineNs,true};
    f.last=f.bolt->Tick(f.sample,f.hands,f.intent);
    CHECK(f.last.ownsMechanism&&f.last.tracking.recoveryTarget&&f.last.tracking.mechanismPhase==WeaponCyclePhase::Forward);
    CHECK(!f.last.tracking.reloadMechanicalPose&&f.callbacks.resumes==1&&!f.callbacks.submits&&!f.native.releases);return 0;
}
int MappedReleaseCannotReviveExpiredGun(){
    ReloadFixture f;CHECK(f.Open()==0&&f.ReturnWithNMinusOne()==0);
    f.Next();CHECK(f.Run(true));const auto gun=f.hands.Current(right);CHECK(gun);
    const auto support=f.hands.Acquire(f.sample.input,f.Request(left,HandClaimKind::WeaponSupport,2,gun->token.id));CHECK(support.claim);
    f.Next();f.sample.input.nowNs=gun->deadlineNs;f.sample.input.observedNs=f.sample.input.nowNs;
    f.sample.input.deadlineNs=f.sample.input.nowNs+150*ms;
    const Bc2BoltControllerContact missing{};const auto sample=f.Prepare(false,false,&missing);CHECK(sample&&!sample->custodyTransfer);
    f.last=f.bolt->Tick(*sample,f.hands,f.intent);CHECK(!f.hands.Current(left)&&!f.hands.Current(right)&&!f.callbacks.resumes);return 0;
}
}
int main(){if(NMinusOnePressCannotOutliveReleaseOrItsOriginalDeadline()||ReloadResumeKeepsOpenPartUntilRealRegrip()||MappedReleaseCannotReviveExpiredGun())return 1;
    std::puts("3 bolt reload runtime groups passed: original press/release, exact custody, rebased open-part retention.");return 0;}
