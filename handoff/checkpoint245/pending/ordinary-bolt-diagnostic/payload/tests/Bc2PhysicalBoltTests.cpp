#include "Bc2BoltPart.h"
#include "Bc2M95BoltCalibration.h"
#include "Bc2BoltControllerProbe.h"
#include <sstream>
#include <fstream>
#include "Test.h"
#include <cstring>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;constexpr auto left=InteractionHand::Left,right=InteractionHand::Right;
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
bool Equal(const math::Matrix4& a,const math::Matrix4& b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(std::abs(a.values[r][c]-b.values[r][c])>1e-5f)return false;return true;}
struct Native {
    Bc2NativeCycleView view{};unsigned controls=0,releases=0,acks=0,cancels=0;bool rejectRelease=false;
    std::optional<WeaponCycleRelease> release;
    Bc2PhysicalBoltApi Api(){return {this,
        [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<Native*>(p);++n.controls;
            if(c.release){++n.releases;n.release=c.release;n.view.phase=Bc2NativeCyclePhase::Releasing;n.view.held.reset();return !n.rejectRelease;}return true;},
        [](void* p,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<Native*>(p)->view;},
        [](void* p,const WeaponCycleReady& r)noexcept{auto& n=*static_cast<Native*>(p);if(!n.view.ready||n.view.ready->release!=r.release||n.view.ready->sequence!=r.sequence)return false;
            ++n.acks;n.view.ready.reset();n.view.phase=Bc2NativeCyclePhase::Complete;n.view.blocksFire=false;return true;},
        [](void* p)noexcept{++static_cast<Native*>(p)->cancels;}};}
};
struct Fixture {
    Native native;HandInteraction hands;RigSnapshot rig;std::shared_ptr<Bc2BoltCalibration> calibration;
    std::unique_ptr<Bc2PhysicalBolt> bolt;Bc2PhysicalBoltSample sample;Bc2PhysicalBoltResult last;
    HandClaimToken originalGun{},originalSupport{};std::uint64_t intent=10;bool entered=false;
    Fixture(){
        rig.names={"root","jntWpn_1","jntWpn_3","other"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        rig.identity={0x20000,0x30000,0x50000,0x60000,0x70000,0x80000,0x90000,0xa0000,0xb0000,4,false};
        rig.world.assign(4,Identity());rig.inverseBind.assign(4,Identity());rig.evaluatedWorld.assign(4,Identity());rig.nativeEvaluated.resize(4);
        for(auto& bytes:rig.nativeEvaluated)bytes.fill(std::byte{0x5a});
        calibration=std::make_shared<Bc2BoltCalibration>(M95AuthoredBoltCalibration(1));
        // Synthetic callback/rig fixture only: this does not promote the actual
        // M95 authored reference to a runtime calibration or native admission.
        calibration->nativeJoined=true;calibration->rigFingerprint=DeriveBoltPart(rig,"jntWpn_3")->fingerprint;
        sample.nativeOwner={0x10000,0x20000,0x30000,0x40000,2,7,4};
        sample.input={{(std::uint64_t(0x30000)<<32)|0x20000,2,3,4},1,1000*ms,1150*ms,1000*ms,true,{true,true},{false,false}};
        sample.item={0x40000,3};sample.asset=calibration->asset;sample.mesh=calibration->mesh;
        sample.weaponWorld=Identity();sample.weaponWorld.values[3][1]=1.2f;sample.weaponWorld.values[3][2]=-.3f;
        sample.wristWorld={Identity(),Identity()};sample.wristWorld[0].values[3][0]=-.2f;sample.wristWorld[1].values[3][0]=.1f;
        auto gun=hands.Acquire(sample.input,Request(right,HandClaimKind::GunHold,1));originalGun=gun.claim->token;
        auto support=hands.Acquire(sample.input,Request(left,HandClaimKind::WeaponSupport,2,originalGun.id));originalSupport=support.claim->token;
        native.view.phase=Bc2NativeCyclePhase::Held;native.view.blocksFire=true;
        native.view.native.owner=sample.nativeOwner;
        native.view.held=WeaponCycleLease{sample.input.owner,sample.item,{calibration->profile.id,3},1,1,1,sample.input.observedNs,sample.input.deadlineNs,true};
        bolt=std::make_unique<Bc2PhysicalBolt>(calibration,native.Api());
    }
    HandClaimRequest Request(InteractionHand hand,HandClaimKind kind,std::uint64_t contact,std::uint64_t parent=0){return
        {sample.input.owner,hand,kind,sample.item,{{contact,3},sample.input.sequence,sample.input.deadlineNs,true},++intent,parent};}
    void Next(){auto& s=sample.input;++s.sequence;s.nowNs+=10*ms;s.observedNs=s.nowNs;s.deadlineNs=s.nowNs+150*ms;
        if(native.view.held){++native.view.held->sequence;native.view.held->observedNs=s.observedNs;native.view.held->deadlineNs=s.deadlineNs;}}
    Bc2PhysicalBoltResult Tick(const math::Matrix4& part,bool grip=true,bool advance=true){
        if(advance)Next();sample.grip=grip;sample.input.released[1]=!grip;
        sample.raw={sample.nativeOwner,rig.identity,calibration->rigFingerprint,sample.input,M95BoltWristFromPart(part),true};
        const auto hand=last.tracking.custody==BoltCustodyPhase::Returned?right:left;
        const auto gun=hands.Current(entered?hand:right);
        const auto contact=sample.custodyTransfer?sample.custodyTransfer->nextGun.request.contact.key:gun?gun->token.contact:HandInteractionKey{};
        sample.gunContact={contact,sample.input.sequence,sample.input.deadlineNs,true};
        if(!entered&&gun)hands.Renew(sample.input,gun->token,sample.gunContact);
        last=bolt->Tick(sample,hands,intent);sample.custodyTransfer.reset();return last;
    }
    int Enter(){
        sample.custodyTransfer=HandGunCustodyTransfer{originalGun,originalSupport,{Request(left,HandClaimKind::GunHold,2),sample.input},{}};
        const auto r=Tick(calibration->profile.closedContact,true,false);
        CHECK(r.custodyChange&&r.custodyChange->transaction.accepted&&r.ownsGunCustody&&!hands.Current(right)&&hands.Current(left));entered=true;return 0;
    }
    int Stroke(){
        auto r=Tick(calibration->profile.closedContact,false);CHECK(r.blocksFire&&r.tracking.weapon&&!r.ownsMechanism);
        r=Tick(calibration->profile.closedContact);CHECK(r.ownsMechanism&&r.tracking.target);
        const auto& p=calibration->profile;
        auto step=[&](float travel,float angle){return Tick(weapon_cycle_detail::Target(p,travel,angle));};
        for(unsigned n=1;n<=4;++n)step(0,p.unlockRadians*float(n)/4);
        for(unsigned n=0;n<5;++n)step(0,p.unlockRadians);
        for(unsigned n=1;n<=8;++n)step(p.stroke*float(n)/8,p.unlockRadians);
        for(unsigned n=0;n<5;++n)step(p.stroke,p.unlockRadians);
        for(unsigned n=1;n<=8;++n)step(p.stroke*(1.f-float(n)/8),p.unlockRadians);
        for(unsigned n=0;n<5;++n)step(0,p.unlockRadians);
        for(unsigned n=1;n<=4;++n)step(0,p.unlockRadians*(1.f-float(n)/4));
        for(unsigned n=0;n<5;++n)step(0,0);
        CHECK(native.releases==1&&native.release&&!hands.Current(right)&&hands.Current(left)&&last.blocksFire);return 0;
    }
    int Return(){
        Tick(calibration->profile.closedContact,false);Next();sample.input.released[1]=false;
        sample.custodyTransfer=HandGunCustodyTransfer{hands.Current(left)->token,{},
            {Request(right,HandClaimKind::GunHold,1),sample.input},HandCustodyTarget{Request(left,HandClaimKind::WeaponSupport,2),sample.input}};
        auto r=Tick(calibration->profile.closedContact,true,false);
        CHECK(r.custodyChange&&r.custodyChange->transaction.accepted&&r.custodyChange->companion&&r.tracking.weapon);
        CHECK(hands.Current(right)->token.kind==HandClaimKind::GunHold&&hands.Current(left)->token.prerequisiteClaim==hands.Current(right)->token.id);return 0;
    }
    void Ready(){native.view.ready=WeaponCycleReady{*native.release,sample.input.sequence+1,sample.input.nowNs,sample.input.nowNs+50*ms,true,true};}
};
int AdmissionRemainsDisabled(){
    auto c=std::make_shared<Bc2BoltCalibration>(M95AuthoredBoltCalibration(1));CHECK(!c->nativeJoined&&!BoltCalibrationValid(*c)&&!M95NativeBoltHandleVerified);
    Native native;Bc2PhysicalBolt adapter(c,native.Api());HandInteraction hands;std::uint64_t intent=0;const auto r=adapter.Tick({},hands,intent);
    CHECK(!r.tracking.enabled&&!native.controls&&!native.cancels&&!native.releases);return 0;
}
int OrderedPhysicalNativeHandshake(){
    Fixture f;CHECK(f.Enter()==0&&f.Stroke()==0);const auto release=*f.native.release;
    for(unsigned n=0;n<20;++n){auto r=f.Tick(f.calibration->profile.closedContact);CHECK(r.blocksFire&&!r.ownsMechanism&&f.native.releases==1);}
    CHECK(f.Return()==0);f.Ready();for(unsigned n=0;n<8;++n)f.Tick(f.calibration->profile.closedContact);
    CHECK(f.native.acks==1&&f.native.releases==1&&!f.last.blocksFire&&f.native.release==release);return 0;
}
int NativeReadyDoesNotReturnCustody(){
    Fixture f;CHECK(f.Enter()==0&&f.Stroke()==0);f.Ready();auto r=f.Tick(f.calibration->profile.closedContact);
    CHECK(r.settled&&f.native.acks==1&&r.blocksFire&&f.hands.Current(left)&&!f.hands.Current(right));
    CHECK(f.Return()==0);r=f.Tick(f.calibration->profile.closedContact);CHECK(!r.blocksFire);return 0;
}
int CompletedCycleRetainsOnlyCurrentGunPlacement(){
    Fixture f;CHECK(f.Enter()==0&&f.Stroke()==0&&f.Return()==0);f.Ready();
    auto r=f.Tick(f.calibration->profile.closedContact);CHECK(r.settled&&r.tracking.weapon);
    const auto returned=r.tracking.weapon->weaponInWorld;const auto gun=f.hands.Current(right)->token;
    // Pass many times the original held lease lifetime. Only current right-hand
    // proof sustains placement; no held lease, part pose or native request renews.
    for(unsigned n=0;n<40;++n){
        f.sample.wristWorld[1].values[3][0]+=.001f;r=f.Tick(f.calibration->profile.closedContact);
        CHECK(r.tracking.weapon&&r.tracking.gun&&r.tracking.gun->token==gun&&!r.blocksFire);
        CHECK(r.tracking.custody==BoltCustodyPhase::Returned&&!r.tracking.held&&!r.tracking.target&&!r.ownsMechanism);
        CHECK(Near(r.tracking.weapon->weaponInWorld.values[3][0],returned.values[3][0]+.001f*float(n+1)));
        CHECK(r.tracking.weapon->inputSequence==f.sample.input.sequence);
    }
    CHECK(f.native.releases==1&&f.native.acks==1);
    f.sample.input.tracked[1]=false;r=f.Tick(f.calibration->profile.closedContact);
    CHECK(!r.tracking.weapon&&!f.hands.Current(right)&&!f.hands.Current(left));return 0;
}
int AmbiguousSubmissionNeverRetries(){
    Fixture f;f.native.rejectRelease=true;CHECK(f.Enter()==0&&f.Stroke()==0);
    for(unsigned n=0;n<30;++n)f.Tick(f.calibration->profile.closedContact);
    CHECK(f.native.releases==1&&f.last.blocksFire&&!f.native.acks);return 0;
}
int PrivatePartAndRawMapping(){
    Fixture f;CHECK(f.Enter()==0);f.Tick(f.calibration->profile.closedContact,false);auto r=f.Tick(f.calibration->profile.closedContact);
    CHECK(r.tracking.target&&r.tracking.weapon);const auto before=f.rig.nativeEvaluated;
    const auto plan=BuildBoltPartPlan(r.tracking,f.rig,r.tracking.weapon->weaponInWorld,1,f.sample.input.nowNs);
    CHECK(plan&&plan->edits.size()==1&&plan->edits[0].index==2&&f.rig.nativeEvaluated==before);
    for(unsigned row=0;row<4;++row)for(unsigned col=12;col<16;++col)CHECK(plan->edits[0].after[row*16+col]==std::byte{0x5a});
    auto wrist=Multiply(M95BoltWristFromPart(f.calibration->profile.closedContact),r.tracking.weapon->weaponInWorld);
    const auto raw=BuildBoltRawContact(r.tracking,f.rig,wrist,r.tracking.weapon->weaponInWorld,1,f.sample.input.nowNs);
    CHECK(raw.valid&&raw.input.sequence==f.sample.input.sequence&&Equal(raw.mechanismWristInWeapon,f.sample.raw.mechanismWristInWeapon));
    f.rig.evaluatedWorld[2].values[3][2]=-.7f;
    const auto repeated=BuildBoltPartPlan(r.tracking,f.rig,r.tracking.weapon->weaponInWorld,1,f.sample.input.nowNs);
    CHECK(repeated&&repeated->edits[0].after==plan->edits[0].after);
    for(unsigned mutation=0;mutation<12;++mutation){auto bad=r.tracking;auto rig=f.rig;auto placed=r.tracking.weapon->weaponInWorld;
        switch(mutation){case 0:++bad.target->revision;break;case 1:bad.target->contact.values[3][0]+=.01f;break;
        case 2:bad.target->rotation=1;break;case 3:bad.target->travel=f.calibration->profile.stroke+.01f;break;
        case 4:bad.input.focused=false;break;case 5:bad.gun->token.hand=right;break;case 6:bad.mechanism->token.prerequisiteClaim=0;break;
        case 7:rig.names[3]="jntWpn_3";break;case 8:rig.parents[3]=2;break;case 9:rig.nativeHiddenLeaves={2};break;
        case 10:placed.values[3][0]+=.01f;break;case 11:++bad.weapon->inputSequence;break;}
        CHECK(!BuildBoltPartPlan(bad,rig,placed,1,f.sample.input.nowNs));}
    return 0;
}
int PhysicalLossNeverSettlesDebt(){
    Fixture f;CHECK(f.Enter()==0);f.Tick(f.calibration->profile.closedContact,false);f.Tick(f.calibration->profile.closedContact);
    f.sample.input.tracked[0]=false;auto r=f.Tick(f.calibration->profile.closedContact);
    CHECK(r.blocksFire&&!r.tracking.target&&!r.tracking.weapon&&!f.hands.Current(left)&&!f.hands.Current(right)&&!f.native.releases&&!f.native.acks);return 0;
}
int NativeOwnerAndHandOwnerStaySeparate(){
    for(unsigned n=0;n<4;++n){Fixture f;
        if(n==0)++f.native.view.native.owner.weapon;
        if(n==1)++f.native.view.held->owner.equipGeneration;
        if(n==2)++f.native.view.held->item.generation;
        if(n==3)++f.native.view.held->mechanism.id;
        const auto r=f.Tick(f.calibration->profile.closedContact,true,false);
        CHECK(r.blocksFire&&!r.ownsGunCustody&&!r.tracking.enabled&&f.native.cancels==1&&!f.native.releases);
        CHECK(f.hands.Current(right)->token==f.originalGun&&f.hands.Current(left)->token==f.originalSupport);
    }return 0;
}
int CancellationRetainsOriginalEvidence(){
    Fixture f;CHECK(f.Enter()==0);f.Tick(f.calibration->profile.closedContact,false);
    const auto held=f.Tick(f.calibration->profile.closedContact);CHECK(held.ownsMechanism&&!held.firstCancellation);
    auto bad=f.calibration->profile.closedContact;bad.values[3][0]+=.02f;
    const auto failed=f.Tick(bad);CHECK(failed.firstCancellation);
    const auto& evidence=*failed.firstCancellation;
    CHECK(evidence.failure==WeaponCycleFailure::Geometry&&evidence.rawAdmitted&&evidence.grip);
    CHECK(evidence.input.sequence==f.sample.input.sequence&&evidence.raw.input.sequence==f.sample.raw.input.sequence);
    CHECK(evidence.before[0]&&evidence.before[1]&&evidence.after[0]&&!evidence.after[1]);
    CHECK(evidence.after[0]->token==evidence.before[0]->token&&failed.blocksFire&&!f.native.releases&&!f.native.acks);
    const auto original=evidence.input;
    Bc2BoltControllerProbe probe(*f.calibration);probe.Observe(failed,f.native.view,{},f.sample.input.nowNs);
    for(unsigned n=0;n<10;++n){const auto later=f.Tick(f.calibration->profile.closedContact);
        CHECK(later.firstCancellation&&later.firstCancellation->input.sequence==original.sequence&&later.firstCancellation->input.deadlineNs==original.deadlineNs);
        probe.Observe(later,f.native.view,{},f.sample.input.nowNs);}
    std::ostringstream report;probe.Report(report);const auto text=report.str();
    CHECK(text.find("\"first_physical_cancellation\":{\"failure\":7")!=std::string::npos);
    CHECK(text.find("\"claims_after\":[{")!=std::string::npos&&text.find("\"raw_admitted\":true")!=std::string::npos);
    std::ofstream("bolt-cancellation-synthetic.json")<<text;
    CHECK(!f.native.releases&&!f.native.acks);return 0;
}
int MissingRawCancellationIsDiagnosticOnly(){
    Fixture f;CHECK(f.Enter()==0);f.Tick(f.calibration->profile.closedContact,false);CHECK(f.Tick(f.calibration->profile.closedContact).ownsMechanism);
    f.Next();f.sample.raw.valid=false;f.sample.raw.mechanismWristInWeapon.values[0][0]=std::numeric_limits<float>::quiet_NaN();
    f.sample.gunContact.inputSequence=f.sample.input.sequence;f.sample.gunContact.deadlineNs=f.sample.input.deadlineNs;
    const auto failed=f.bolt->Tick(f.sample,f.hands,f.intent);
    CHECK(failed.firstCancellation&&!failed.firstCancellation->rawAdmitted&&!failed.firstCancellation->raw.valid);
    CHECK(failed.blocksFire&&!failed.ownsMechanism&&!f.native.releases&&!f.native.acks);
    Bc2BoltControllerProbe probe(*f.calibration);probe.Observe(failed,f.native.view,{},f.sample.input.nowNs);
    std::ofstream report("bolt-cancellation-raw-invalid.json");probe.Report(report);
    return 0;
}
}
int main(){
    if(CancellationRetainsOriginalEvidence()||MissingRawCancellationIsDiagnosticOnly()||CompletedCycleRetainsOnlyCurrentGunPlacement()||AdmissionRemainsDisabled()||OrderedPhysicalNativeHandshake()||NativeReadyDoesNotReturnCustody()||AmbiguousSubmissionNeverRetries()||
       PrivatePartAndRawMapping()||PhysicalLossNeverSettlesDebt()||NativeOwnerAndHandOwnerStaySeparate())return 1;
    std::puts("Physical bolt adapter: 10 groups passed; real custody/cycle, synthetic native receipts, private one-leaf palette, admission OFF.");return 0;
}
