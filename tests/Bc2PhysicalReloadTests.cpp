#include "Bc2PhysicalReload.h"
#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadConfigDescriptor.h"
#include "NativeProbeConfig.h"
#include "Test.h"
#include <cstring>
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
struct Fixture {
    std::int64_t now=1000000000;unsigned reads=0,starts=0,keeps=0,submits=0,cancels=0;
    std::int64_t keepDelay=0;
    bool keepDeferred=false,reserveDeferred=false,reserveRejected=false,reserveCohortGap=false;
    bool held=false,allowRetire=false,advanceAck=false,reserveAvailable=true,keepOkay=true;std::uint64_t sequence=100,cycle=0,intent=0,retirementEvent=0;
    std::uint64_t submittedInput=0;unsigned leaseChange=0,retirementVariant=0;std::int64_t retirementDelay=0;
    Bc2ReloadNativePolicy* selectedPolicy=nullptr;ReloadObservedConfig sourceConfig{}; // Test-only current-source dispatch gate.
    Bc2AmmoReserveLease reserve{};std::optional<Bc2ReloadNativeRequest> submitted;
    std::optional<Bc2ReloadAckEvidence> ack;
    PhysicalReloadApi api{};PhysicalReloadSample s{};HandInteraction hands;
    std::optional<HandClaim> gun;std::shared_ptr<SelectedMeshesSnapshot> mesh=std::make_shared<SelectedMeshesSnapshot>();
    std::optional<Bc2PhysicalReload> policy;PhysicalReloadResult result{};
    Fixture(bool enabled=true){
        reserve.identity.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};
        reserve.identity.firing={0x50000,0x60000,0x70000};reserve.identity.serverPlayer=0x80000;
        reserve.identity.serverSoldier=0x90000;reserve.identity.serverItem=0xa0000;
        reserve.loaded=2;reserve.reserve=8;reserve.capacity=8;reserve.verified=true;reserve.reloadInputReady=true;
        s.nativeOwner=reserve.identity.owner;s.weapon={0x40000,17};s.trackingEpoch=7;
        s.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},1,now,now+100000000,now,true,{true,true},{true,false}};
        s.bodyFromHand=Pose();s.geometrySequence=1;s.asset=SpasReloadAsset;s.meshes=mesh;
        mesh->owner=s.nativeOwner;mesh->stateCount=1;mesh->soleConfiguredArray=0x110000;mesh->states[0].count=1;
        auto& m=mesh->states[0].meshes[0];m.kind=SelectedMeshKind::Spas12;m.address=0x120000;
        std::memcpy(m.assetPath.data(),SpasReloadMesh.data(),SpasReloadMesh.size());
        s.raw.valid=true;s.raw.owner=s.nativeOwner;s.raw.rigFingerprint=SpasReloadRig;
        s.raw.weaponWorldMeters=Pose();Geometry(-.07f);
        api.context=this;
        api.clock=[](void* p)noexcept{return static_cast<Fixture*>(p)->now;};
        api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Fixture*>(p);++f.reads;return f.reserveAvailable&&(!f.selectedPolicy||f.selectedPolicy->MatchesSelectedConfig(f.sourceConfig))?std::optional(f.reserve):std::nullopt;};
        api.reserveObserved=[](void* p)noexcept{auto& f=*static_cast<Fixture*>(p);
   if(f.reserveDeferred)return ReloadReserveObservation{ReloadObservationResult::Deferred,{}};
   if(f.reserveRejected)return ReloadReserveObservation{ReloadObservationResult::Rejected,{}};
   if(f.reserveCohortGap)return ReloadReserveObservation{ReloadObservationResult::CohortGap,{}};
   return ReloadReserveObservation{ReloadObservationResult::Available,f.api.reserve(p)};};
  api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Fixture*>(p)->reserve.identity;};
        api.start=[](void* p,const ReloadCycleControl& c)noexcept{auto& f=*static_cast<Fixture*>(p);++f.starts;f.cycle=c.cycle;
            return c.observedNs==f.s.input.observedNs&&c.deadlineNs==f.s.input.deadlineNs;};
        api.keep=[](void* p,const ReloadCycleControl&)noexcept{auto& f=*static_cast<Fixture*>(p);++f.keeps;return f.keepOkay;};
        api.keepObserved=[](void* p,const ReloadCycleControl&)noexcept{auto& f=*static_cast<Fixture*>(p);++f.keeps;f.now+=f.keepDelay;return f.keepDeferred?ReloadKeepAliveResult::Deferred:f.keepOkay?ReloadKeepAliveResult::Accepted:ReloadKeepAliveResult::Rejected;};
        api.lease=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadRoundLease>{
            auto& f=*static_cast<Fixture*>(p);if(!f.held&&!f.submitted)return {};
            ReloadRoundLease out{id,cycle,f.sequence,f.now,f.now+100000000,f.reserve.loaded,f.reserve.reserve,f.reserve.capacity,true,f.held};
            if(f.leaseChange==1)++out.cycle;if(f.leaseChange==2)++out.identity.firing[2];if(f.leaseChange==3)out.allThreeHeld=false;return out;};
        api.submit=[](void* p,const Bc2ReloadNativeRequest& r)noexcept{auto& f=*static_cast<Fixture*>(p);++f.submits;f.submitted=r;f.submittedInput=f.s.input.sequence;f.held=false;return true;};
        api.ack=[](void* p,const ReloadHoldIdentity&,std::uint64_t)noexcept->std::optional<Bc2ReloadAckEvidence>{auto& f=*static_cast<Fixture*>(p);
            if(f.ack&&f.advanceAck){f.advanceAck=false;f.now+=1000000;f.ack->observedNs=f.now;f.ack->deadlineNs=f.now+100000000;}return f.ack;};
        api.cancel=[](void* p)noexcept{++static_cast<Fixture*>(p)->cancels;};
        api.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t c)noexcept->std::optional<ReloadCycleRetirement>{
            auto& f=*static_cast<Fixture*>(p);if(!f.allowRetire)return {};
            f.now+=f.retirementDelay;ReloadCycleRetirement receipt{id,c,++f.retirementEvent,f.now,f.now+200000000,true};
            if(f.retirementVariant==1)++receipt.identity.firing[0];if(f.retirementVariant==2)++receipt.cycle;
            if(f.retirementVariant==3){receipt.observedNs=f.now-200000000;receipt.deadlineNs=f.now;}
            if(f.retirementVariant==4)receipt.observedNs=f.now+1;if(f.retirementVariant==5)receipt.verified=false;
            if(f.retirementVariant==6)receipt.event=0;return receipt;};
        policy.emplace(enabled,api,AmmoSupplyConfig{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200000000});
    }
    void Geometry(float z){const auto p=SpasReloadInsertionProfile();
        s.raw.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(reload_insertion_detail::TravelPose(p,z),Multiply(p.weaponFromEntry,s.raw.weaponWorldMeters))));}
    void SendRail(float x,float y,float z){
        const auto old=s.input;now+=20000000;++s.input.sequence;s.input.observedNs=s.input.nowNs=now;s.input.deadlineNs=now+100000000;
        s.input.released[0]=false;s.gripPressed=true;s.geometrySequence=s.input.sequence;s.raw.inputEvidence=old;s.originalHandEvidence=old;
        const auto p=SpasReloadInsertionProfile();
        auto rail=reload_insertion_detail::TravelPose(p,z);const auto& d=p.travelDirection;
        const auto norm=std::hypot(d[0],d[1]);const std::array<float,3> side{d[1]/norm,-d[0]/norm,0};
        const std::array<float,3> other{-d[2]*side[1],d[2]*side[0],d[0]*side[1]-d[1]*side[0]};
        for(unsigned n=0;n<3;++n)rail.values[3][n]+=x*side[n]+y*other[n];
        s.raw.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(rail,Multiply(p.weaponFromEntry,s.raw.weaponWorldMeters))));Sync();
    }
    void Sync(){
        s.input.nowNs=now; // Processing time advances; original input observation/deadline do not.
        reserve.sequence=100000+(++sequence);reserve.observedNs=now;reserve.deadlineNs=now+100000000;
        mesh->sequence=sequence;mesh->observedNs=now;mesh->deadlineNs=now+200000000;
        hands.Update(s.input);
        const HandContactProof proof{{1002,1},s.input.sequence,s.input.deadlineNs,true};
        if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,proof,++intent,0}).claim;
        else gun=hands.Renew(s.input,gun->token,proof).claim;
        result=policy->Tick(s,hands,intent);
    }
    void Send(bool press,float z=-.07f,bool previous=true,bool restamp=false){
        const auto old=s.input;now+=20000000;++s.input.sequence;s.input.observedNs=s.input.nowNs=now;s.input.deadlineNs=now+100000000;
        s.input.released[0]=!press;s.gripPressed=press;s.geometrySequence=s.input.sequence;
        s.raw.inputEvidence=previous?old:s.input;s.originalHandEvidence=s.raw.inputEvidence;if(restamp)++s.raw.inputEvidence.observedNs;Geometry(z);Sync();
    }
    void SendAsync(float z){
        now+=20000000;++s.input.sequence;s.input.observedNs=s.input.nowNs=now;s.input.deadlineNs=now+100000000;
        s.input.released[0]=false;s.gripPressed=true;s.geometrySequence=s.input.sequence;
        Sync(); // Gameplay consumed this new input, but rendering still exposes the previous contact.
        now+=1000000;s.raw.inputEvidence=s.input;s.originalHandEvidence=s.input;Geometry(z);
        Sync(); // Real renderer contact arrives later within the SAME input generation.
    }
    bool Begin(){s.raw.inputEvidence=s.input;s.originalHandEvidence=s.input;Sync();Send(true);return starts==1&&result.ammoOwnsHand;}
    bool Insert(){if(!Begin())return false;held=true;Send(true,-.05f);Send(true,-.05f);Send(true,-.02f);Send(true,0);Send(true,.025f);
        for(unsigned n=0;n<9;++n)Send(true,.05f);if(submits!=1)std::cerr<<Report()<<'\n';return submits==1;}
    bool SeatAsync(){if(!Begin())return false;held=true;
        for(float z:{-.05f,-.05f,-.02f,0.f,.025f,.05f,.05f,.05f,.05f,.05f,.05f,.05f}){
            SendAsync(z);if(Report().find("\"event\":10,\"reason\":2")!=std::string::npos)return submits==0;}
        return false;}
    void Ack(){const auto& r=*submitted;const auto& o=s.nativeOwner;
        ++reserve.loaded;--reserve.reserve;ack=Bc2ReloadAckEvidence{{{r.request.id,{o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space},ReloadOperation::InsertRound,ReloadAcknowledgement::Applied},reserve.identity,cycle,sequence+1,200},now,now+100000000,true};}
    std::string Report(){std::ostringstream o;policy->Report(o);return o.str();}
};
int BeltReturnsBetweenActualAcknowledgedShells(){
    Fixture f;f.policy->EnableBeltAmmo();CHECK(f.Insert());CHECK(!f.result.tracking.belt);
    f.Ack();f.Send(true,.05f);CHECK(f.policy->ProbeState(f.now).completed==1);
    f.held=true;f.submitted.reset();f.ack.reset();
    for(unsigned n=0;n<350;++n){f.Send(false);const auto state=f.policy->ProbeState(f.now);
        CHECK(state.active&&state.nativeHolding&&!state.held&&!state.pending);
        CHECK(f.result.tracking.belt&&f.result.tracking.belt->heldCycle&&f.result.tracking.shellControl);
        CHECK(ReloadBeltCompatible(f.result.tracking,f.now)&&f.result.tracking.belt->heldCycle->cycle==f.cycle);
        CHECK(f.starts==1&&f.submits==1&&f.cancels==0&&f.reserve.loaded==3&&f.reserve.reserve==7);}
    f.Send(true);CHECK(f.result.ammoOwnsHand&&f.result.tracking.preview&&!f.result.tracking.belt);
    CHECK(f.starts==1&&f.submits==1);return 0;
}
int BetweenShellBeltRejectsLostHoldAndCancellation(){
    for(unsigned k=0;k<5;++k){Fixture f;f.policy->EnableBeltAmmo();CHECK(f.Insert());f.Ack();f.Send(true,.05f);
        f.held=true;f.submitted.reset();f.ack.reset();f.Send(false);CHECK(f.result.tracking.belt);
        if(k==0)f.leaseChange=1;if(k==1)f.leaseChange=2;if(k==2)f.leaseChange=3;
        if(k==3)f.s.cancel=true;if(k==4)f.held=false;
        for(unsigned n=0;n<6;++n)f.Send(false);
        CHECK(!f.result.tracking.belt&&f.submits==1&&f.reserve.loaded==3&&f.reserve.reserve==7);
    }return 0;
}

int BeltAvailabilityComesFromActualConsumer(){
    Fixture f;f.reserve.allThreeIdle=true;f.s.raw.inputEvidence=f.s.input;f.Sync();CHECK(!f.result.tracking.belt);
    f.policy->EnableBeltAmmo();f.Sync();CHECK(f.result.tracking.belt&&SpasBeltAmmoFresh(*f.result.tracking.belt,f.now));
    CHECK(f.starts==0&&f.submits==0&&f.reserve.loaded==2&&f.reserve.reserve==8);
    const auto contact=f.result.tracking.belt->visual.contact;CHECK(contact.centerMeters==(std::array<float,3>{0,0,0})&&contact.radiusMeters==.15f);
    f.Send(true);CHECK(f.result.ammoOwnsHand&&!f.result.tracking.belt&&f.starts==1);
    f.held=true;f.Send(true,-.02f);CHECK(!f.result.tracking.belt);return 0;
}
int FullGunCanShowReserveButCannotMintShell(){
    Fixture f;f.reserve.allThreeIdle=true;f.reserve.loaded=8;f.policy->EnableBeltAmmo();f.s.raw.inputEvidence=f.s.input;
    f.Sync();CHECK(f.result.tracking.belt);f.Send(true);CHECK(f.result.tracking.belt&&!f.result.ammoOwnsHand);
    CHECK(f.starts==0&&f.submits==0&&f.reserve.loaded==8&&f.reserve.reserve==8);
    f.reserve.reserve=0;f.Send(false);CHECK(!f.result.tracking.belt);return 0;
}
int BeltRespectsNativeBusyAndSafetyWithoutNewAuthority(){
    for(unsigned k=0;k<7;++k){Fixture f;f.reserve.allThreeIdle=true;f.policy->EnableBeltAmmo();f.s.raw.inputEvidence=f.s.input;f.Sync();CHECK(f.result.tracking.belt);
        if(k==0)f.reserve.allThreeIdle=false;if(k==1)f.s.cancel=true;if(k==2)f.s.input.focused=false;
        if(k==3)f.s.asset="XM8_sp_s";if(k==4)f.s.meshes.reset();if(k==5)f.reserve.verified=false;if(k==6)f.s.input.tracked[0]=false;
        f.Sync();CHECK(!f.result.tracking.belt&&f.starts==0&&f.submits==0);
    }return 0;
}
int BodyPouchBeltSharesTheExistingAlternateContact(){
    Fixture f;f.reserve.allThreeIdle=true;const auto config=ChestAmmoSupply();f.policy.emplace(true,f.api,config);f.policy->EnableBeltAmmo(true,SupplyAnchorFrame::RecenteredBody);
    f.s.raw.inputEvidence=f.s.input;f.Sync();CHECK(f.result.tracking.belt);
    const auto&v=f.result.tracking.belt->visual;CHECK(v.frame==SupplyAnchorFrame::RecenteredBody);
    CHECK(v.contact.centerMeters==config.alternateContact->centerMeters&&v.contact.radiusMeters==config.alternateContact->radiusMeters);
    CHECK(v.source.identity.pool.id==f.reserve.identity.serverItem&&f.starts==0&&f.submits==0);return 0;
}
int FeedbackDistinguishesLatchFromVerifiedReload(){
    Fixture f;CHECK(f.Begin());CHECK(f.result.feedbackCount==0);f.held=true;f.Send(true,-.05f);
    CHECK(f.result.feedbackCount==1&&f.result.feedback[0].kind==FeedbackKind::ReloadCapture);
    const auto capture=f.result.feedback[0];CHECK(capture.inputSequence==f.s.raw.inputEvidence.sequence);
    CHECK(capture.observedNs==f.s.raw.inputEvidence.observedNs&&capture.deadlineNs<=f.s.raw.inputEvidence.deadlineNs);
    CHECK(ValidFeedback(capture,f.now)&&f.submits==0);f.Sync();CHECK(f.result.feedbackCount==0);
    for(float z:{-.02f,0.f,.025f,.05f,.05f,.05f,.05f,.05f,.05f,.05f,.05f}){
        f.Send(true,z);for(unsigned n=0;n<f.result.feedbackCount;++n)CHECK(f.result.feedback[n].kind!=FeedbackKind::ReloadApplied);}
    CHECK(f.submits==1&&f.reserve.loaded==2); // Seating/submission is not success feedback.
    f.Ack();f.Send(true,.05f);
    CHECK(f.result.feedbackCount==1&&f.result.feedback[0].kind==FeedbackKind::ReloadApplied&&f.result.feedback[0].id>capture.id);
    CHECK(f.result.feedback[0].inputSequence==f.s.input.sequence&&ValidFeedback(f.result.feedback[0],f.now));
    CHECK(f.policy->ProbeState(f.now).completed==1&&f.reserve.loaded==3&&f.reserve.reserve==7);
    f.Send(true,.05f);CHECK(f.result.feedbackCount==0);
    const auto report=f.Report();CHECK(report.find("\"kind\":\"capture\"")!=std::string::npos&&report.find("\"kind\":\"native_receipt\"")!=std::string::npos);
    return 0;
}
int FeedbackKeepsFinalReceiptButNotCancellation(){
    {Fixture f;f.reserve.loaded=7;CHECK(f.Insert());f.Ack();f.Send(true,.05f);
        CHECK(f.result.feedbackCount==1&&f.result.feedback[0].kind==FeedbackKind::ReloadApplied);
        CHECK(!f.policy->ProbeState(f.now).active&&f.reserve.loaded==8);}
    {Fixture f;CHECK(f.Insert());f.s.cancel=true;f.Send(true,.05f);CHECK(f.result.feedbackCount==0);
        CHECK(f.Report().find("\"kind\":\"native_receipt\"")==std::string::npos);}
    return 0;
}
int GuidedPresentationSurvivesOneMissingGeometryTick(){
    Fixture f;CHECK(f.Begin());f.held=true;f.Send(true,-.05f);f.Send(true,-.02f);
    CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Guided);
    const auto original=*f.result.tracking.preview;
    // Genuine current input/claims, but this tick lacks a paired renderer
    // contact. The captured rail retains its ORIGINAL evidence/deadlines.
    f.now+=1000000;f.s.originalHandEvidence.reset();f.Sync();
    CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Guided);
    CHECK(f.result.tracking.preview->targets.inputSequence==original.targets.inputSequence);
    CHECK(f.result.tracking.preview->targets.observedNs==original.targets.observedNs);
    CHECK(f.result.tracking.preview->targets.deadlineNs==original.targets.deadlineNs);
    CHECK(f.submits==0&&f.reserve.loaded==2&&f.reserve.reserve==8);
    return 0;
}
int GuidedContinuityCannotBorrowNewDeadlines(){
    Fixture f;CHECK(f.Begin());f.held=true;f.Send(true,-.05f);f.Send(true,-.02f);
    CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Guided);
    const auto original=*f.result.tracking.preview;
    f.s.originalHandEvidence.reset();f.now=original.targets.deadlineNs-1;f.Sync();
    CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Guided);
    CHECK(f.result.tracking.preview->targets.deadlineNs==original.targets.deadlineNs);
    CHECK(f.result.tracking.preview->native.deadlineNs==original.native.deadlineNs);
    ++f.now;f.Sync();
    CHECK(!f.result.tracking.preview||f.result.tracking.preview->phase==ReloadPreviewPhase::Carried);
    CHECK(f.submits==0&&f.reserve.loaded==2&&f.reserve.reserve==8);return 0;
}
int GuidedContinuityEndsOnFreshWithdrawalOrRejectedGeometry(){
    for(unsigned bad=0;bad<2;++bad){Fixture f;CHECK(f.Begin());f.held=true;f.Send(true,-.05f);f.Send(true,-.02f);
        CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Guided);
        if(!bad){for(float z:{-.045f,-.075f,-.105f,-.135f})f.Send(true,z);} // Beyond120mm release using bounded steps.
        else f.Send(true,-.02f,true,true); // Source pairing rejects this tick before recognizer sampling.
        if(!bad)CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Carried);
        else { // Missing evidence alone can retain, but a fresh invalid transform must revoke immediately.
            f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.s.raw.rawLeftWristWorldMeters={};f.Sync();
            CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Carried);
        }
        f.now+=1000000;f.s.originalHandEvidence.reset();f.Sync();
        CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Carried);
        CHECK(f.submits==0);
    }return 0;
}
int GuidedContinuityRequiresCurrentCycleClaimsAndHold(){
    for(unsigned bad=0;bad<7;++bad){Fixture f;CHECK(f.Begin());f.held=true;f.Send(true,-.05f);f.Send(true,-.02f);
        CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Guided);
        f.now+=1000000;f.s.originalHandEvidence.reset();
        if(bad<3)f.leaseChange=bad+1;
        if(bad==3)f.s.cancel=true;
        if(bad==4){f.hands.Reset();f.gun.reset();}
        if(bad==5)f.s.input.focused=false;
        if(bad==6){f.mesh=std::make_shared<SelectedMeshesSnapshot>(*f.mesh);++f.mesh->weaponData;f.s.meshes=f.mesh;}
        f.Sync();
        CHECK(!f.result.tracking.preview||f.result.tracking.preview->phase==ReloadPreviewPhase::Carried);
        CHECK(f.submits==0&&f.reserve.loaded==2&&f.reserve.reserve==8);
    }return 0;
}
int CycleVisibilitySurvivesPendingAndReceiptOnlyWhileActive(){
    Fixture f;CHECK(f.Insert());CHECK(f.submits==1&&!f.held&&f.result.tracking.shellControl);
    CHECK(!f.result.tracking.preview);CHECK(ReloadShellControlFresh(f.result.tracking,f.now));
    const auto cycle=f.result.tracking.shellControl->cycle;const auto identity=f.result.tracking.shellControl->reserve.identity;
    f.Ack();f.Send(true,.05f);CHECK(f.policy->ProbeState(f.now).completed==1&&!f.result.ammoOwnsHand&&!f.result.tracking.preview);
    CHECK(f.result.tracking.shellControl&&f.result.tracking.shellControl->cycle==cycle&&f.result.tracking.shellControl->reserve.identity==identity);
    CHECK(ReloadShellControlFresh(f.result.tracking,f.now));
    f.s.cancel=true;f.Send(true);CHECK(!f.result.tracking.shellControl&&!f.result.tracking.preview);
    Fixture waiting;waiting.reserve.reloadInputReady=false;waiting.s.raw.inputEvidence=waiting.s.input;waiting.Sync();waiting.Send(true);
    CHECK(waiting.result.ammoOwnsHand&&waiting.starts==0&&!waiting.result.tracking.shellControl);
    Fixture off(false);off.Send(true);CHECK(!off.result.tracking.shellControl);return 0;
}
int FreeCarryUsesCurrentRendererWrist(){
    Fixture f;CHECK(f.Begin());f.held=true;f.Send(true,-.085f);
    CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Carried);
    CHECK(!f.result.tracking.preview->targets.nativeCycle&&!f.result.tracking.preview->native.cycle);
    // Contact recognition consumed the old packet. The carried renderer must
    // instead derive the actual shell from this latest coherent wrist, not old
    // weapon-local geometric.targets returned while insertion is still Free.
    const auto oldWrist=f.s.raw.rawLeftWristWorldMeters;
    auto raw=f.s.raw;raw.inputEvidence=f.result.tracking.inputEvidence;raw.rawLeftWristWorldMeters.values[3][0]+=.03f;
    const auto target=ResolveReloadPreviewTargets(*f.result.tracking.preview,f.result.tracking,raw,f.now);CHECK(target);
    const auto expected=Multiply(raw.rawLeftWristWorldMeters,*InverseRigid(raw.weaponWorldMeters));
    CHECK(reload_insertion_detail::Distance(target->weaponFromLeftWristMeters,expected)<.00001f);
    CHECK(reload_insertion_detail::Distance(target->weaponFromLeftWristMeters,oldWrist)>.029f);
    CHECK(target->inputSequence==f.s.input.sequence&&target->observedNs==f.s.input.observedNs&&target->deadlineNs<=f.s.input.deadlineNs);
    f.Send(true,-.05f);CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Guided);
    CHECK(f.result.tracking.preview->targets.inputSequence<f.s.input.sequence);
    CHECK(f.result.tracking.preview->targets.observedNs<f.s.input.observedNs);
    return 0;
}
int UnderPortConsumerToAcknowledgement(){
    Fixture f;CHECK(f.Begin());f.held=true;
    f.SendRail(0,-.06f,-.01f);f.SendRail(0,-.06f,-.01f); // A far reposition cannot itself capture.
    CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Carried&&f.submits==0);
    f.SendRail(0,-.065f,-.01f);CHECK(f.result.tracking.preview->phase==ReloadPreviewPhase::Carried);
    f.SendRail(0,-.045f,-.01f);CHECK(f.result.tracking.preview->phase==ReloadPreviewPhase::Guided&&f.submits==0);
    f.SendRail(0,-.02f,0);f.SendRail(0,-.02f,.025f);
    for(unsigned n=0;n<10;++n)f.SendRail(0,-.02f,.05f);
    CHECK(f.submits==1&&f.reserve.loaded==2&&f.reserve.reserve==8);f.Ack();f.SendRail(0,-.02f,.05f);
    CHECK(f.policy->ProbeState(f.now).completed==1&&f.reserve.loaded==3&&f.reserve.reserve==7);
    const auto report=f.Report();CHECK(report.find("\"captures\":1")!=std::string::npos&&report.find("\"seats\":1")!=std::string::npos);
    CHECK(report.find("\"phase_is_actual_insertion\":true")!=std::string::npos);
    return 0;
}
int GeometryRingIsBoundedAndKeepsOriginalSource(){
    Fixture f;CHECK(f.Begin());f.held=true;
    f.Send(true,-.02f);const auto nearSequence=f.s.raw.inputEvidence.sequence;
    CHECK(f.submits==0&&f.result.tracking.preview->phase==ReloadPreviewPhase::Guided); // Near-mouth assistance is not a physical seat.
    for(unsigned n=0;n<220;++n)f.Send(true,-.05f);
    const auto report=f.Report();std::size_t count=0,at=0;
    while((at=report.find("\"rail_tip_m\"",at))!=std::string::npos){++count;++at;}
    CHECK(count==192&&f.submits==0);
    CHECK(report.find("\"source_sequence\":"+std::to_string(f.s.raw.inputEvidence.sequence)+",\"current_sequence\":"+std::to_string(f.s.input.sequence))!=std::string::npos);
    CHECK(report.find("\"observed_ns\":"+std::to_string(f.s.raw.inputEvidence.observedNs)+",\"deadline_ns\":"+std::to_string(f.s.raw.inputEvidence.deadlineNs))!=std::string::npos);
    CHECK(report.find("\"captures\":1,\"seats\":0")!=std::string::npos);
    CHECK(report.find("\"nearest\":{\"source_sequence\":"+std::to_string(nearSequence))!=std::string::npos);
    CHECK(report.find("\"nearest_front\":{\"source_sequence\":"+std::to_string(nearSequence))!=std::string::npos);
    CHECK(report.find("\"closest_unretained_contact_samples\":0")!=std::string::npos);return 0;
}
int CarryDuringPumpWaitsForFreshNativeReadiness(){
    Fixture f;f.reserve.reloadInputReady=false;f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.Sync();f.Send(true);
    CHECK(f.starts==0&&f.result.ammoOwnsHand&&!f.result.reloadHeld);
    const auto original=f.hands.Current(InteractionHand::Left)->token;
    for(unsigned n=0;n<170;++n){f.Send(true);CHECK(f.starts==0&&!f.result.reloadHeld&&f.result.ammoOwnsHand);
        CHECK(f.hands.Current(InteractionHand::Left)->token==original);
        CHECK(f.result.tracking.preview&&f.result.tracking.preview->phase==ReloadPreviewPhase::Carried);}
    // More than the old3s post-start timeout has elapsed, but no cycle/pulse was
    // sent while native state7 was busy. A current ready observation starts once.
    f.reserve.reloadInputReady=true;f.Send(true);
    CHECK(f.starts==1&&f.result.reloadHeld&&f.hands.Current(InteractionHand::Left)->token==original);
    for(unsigned n=0;n<6;++n)f.Send(true);CHECK(f.starts==1&&!f.result.reloadHeld&&f.submits==0);
    return 0;
}
int PendingReadyRejectsReleasedStaleOrChangedEvidence(){
    {Fixture f;f.reserve.reloadInputReady=false;f.s.raw.inputEvidence=f.s.input;f.Sync();f.Send(true);f.Send(false);
        f.reserve.reloadInputReady=true;f.Send(false);CHECK(f.starts==0&&!f.result.ammoOwnsHand);
        f.Send(true);CHECK(f.starts==1&&f.result.ammoOwnsHand);}
    {Fixture f;f.s.raw.inputEvidence=f.s.input;f.Sync(); // Cache ready reserve while no shell is held.
        f.reserveAvailable=false;f.Send(true);CHECK(f.starts==0&&f.result.ammoOwnsHand&&!f.result.reloadHeld);
        f.reserveAvailable=true;f.reserve.reloadInputReady=false;f.Send(true);CHECK(f.starts==0);
        f.reserve.reloadInputReady=true;f.Send(true);CHECK(f.starts==1);}
    for(unsigned bad=0;bad<4;++bad){Fixture f;f.reserve.reloadInputReady=false;f.s.raw.inputEvidence=f.s.input;f.Sync();f.Send(true);
        CHECK(f.starts==0&&f.result.ammoOwnsHand);f.reserve.reloadInputReady=true;
        if(bad==0){++f.s.nativeOwner.equipGeneration;f.Send(true);}
        if(bad==1){f.s.input.focused=false;f.Send(true);}
        if(bad==2){f.s.cancel=true;f.Send(true);}
        if(bad==3){f.now=f.s.input.deadlineNs;f.Sync();}
        CHECK(f.starts==0&&f.submits==0&&!f.result.reloadHeld&&!f.result.ammoOwnsHand);
    }
    return 0;
}
int StartRealCycleAndOriginalTime(){Fixture f;CHECK(f.Begin());CHECK(f.result.reloadHeld);CHECK(f.result.tracking.preview);
    CHECK(f.result.tracking.preview->phase==ReloadPreviewPhase::Carried&&f.result.tracking.preview->native.cycle==0);
    for(unsigned n=0;n<6;++n)f.Send(true);CHECK(!f.result.reloadHeld&&f.starts==1&&f.submits==0);return 0;}
int RailToNativeAck(){Fixture f;CHECK(f.Insert());CHECK(f.submitted->reservation.item.generation!=0);
    CHECK(f.submitted->heldLease.identity.owner.equipGeneration==3&&f.submitted->reservation.claim.owner.equipGeneration==17);
    CHECK(f.submitted->heldLease.sequence<100000&&f.reserve.sequence>100000);
    CHECK(f.Report().find("\"completed\":0")!=std::string::npos);
    for(unsigned n=0;n<4;++n)f.Send(true,.05f);CHECK(f.submits==1&&f.reserve.loaded==2);
    f.Ack();f.Send(true,.05f);CHECK(f.Report().find("\"completed\":1")!=std::string::npos);CHECK(!f.result.ammoOwnsHand);
    f.held=true;f.submitted.reset();f.ack.reset();f.Send(false);f.Send(true);CHECK(f.starts==1&&f.result.ammoOwnsHand);return 0;}
int DefaultAndEvidenceGates(){Fixture off(false);off.Sync();CHECK(off.reads==0&&!off.result.tracking.enabled);
    for(unsigned k=0;k<4;++k){Fixture f;f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;
        if(k==0)f.s.raw.valid=false;if(k==1)f.s.asset="XM8_sp";if(k==2)f.s.meshes.reset();if(k==3)f.reserve.reserve=0;
        f.Sync();f.Send(true);CHECK(f.starts==0&&f.submits==0&&!f.result.ammoOwnsHand);}return 0;}
int SupportCannotBeStolen(){Fixture f;f.s.raw.inputEvidence=f.s.input;f.Sync();
    const auto next=f.s.input.sequence+1;f.now+=20000000;f.s.input.sequence=next;f.s.input.observedNs=f.s.input.nowNs=f.now;f.s.input.deadlineNs=f.now+100000000;
    f.s.input.released[0]=false;f.hands.Update(f.s.input);f.gun=f.hands.Renew(f.s.input,f.gun->token,{{1002,1},next,f.s.input.deadlineNs,true}).claim;
    auto support=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,f.s.weapon,{{1003,1},next,f.s.input.deadlineNs,true},++f.intent,f.gun->token.id});CHECK(support.claim);
    f.s.gripPressed=true;f.s.geometrySequence=next;f.Sync();CHECK(f.starts==0&&f.hands.Current(InteractionHand::Left)->token==support.claim->token);return 0;}
int CancelPendingAndRebaseline(){Fixture f;CHECK(f.Insert());f.s.cancel=true;f.Send(true);CHECK(f.cancels==1&&f.Report().find("\"retiring\":true")!=std::string::npos);
    f.s.cancel=false;f.Send(false);CHECK(f.starts==1);f.allowRetire=true;f.Send(false);f.Send(false);f.Send(true);
    CHECK(f.Report().find("\"reconciled\":1")!=std::string::npos&&f.starts==2);CHECK(f.Report().find("\"completed\":0")!=std::string::npos);return 0;}
int ReplacementSurvivesOriginalAck(){Fixture f;CHECK(f.Insert());const auto original=f.submitted->reservation.item;
    f.Send(false);f.Send(true);CHECK(f.result.ammoOwnsHand);const auto replacement=f.hands.Current(InteractionHand::Left)->token;
    CHECK(replacement.item!=original);f.Ack();f.advanceAck=true;f.Send(true);
    CHECK(f.Report().find("\"completed\":0")!=std::string::npos); // reserve read predates in-call ack
    f.Send(true);CHECK(f.Report().find("\"completed\":1")!=std::string::npos);
    CHECK(f.hands.Current(InteractionHand::Left)->token==replacement&&f.result.ammoOwnsHand&&f.starts==1);
    const auto report=f.Report();const auto at=report.find("\"transactions\":[");CHECK(at!=std::string::npos);
    const auto receipt=report.substr(at);
    CHECK(receipt.find("\"item_generation\":"+std::to_string(original.generation))!=std::string::npos);
    CHECK(receipt.find("\"resolved\":true")!=std::string::npos&&receipt.find("\"server_invocation\":200")!=std::string::npos);
    CHECK(receipt.find("\"physical_equip_generation\":17")!=std::string::npos&&receipt.find("\"equip_generation\":3")!=std::string::npos);
    return 0;}
int DeathNewOwnerAndLateOldAck(){Fixture f;CHECK(f.Insert());const auto oldNative=f.reserve.identity;f.s.input.focused=false;f.Send(false);CHECK(f.cancels==1);
    f.Ack(); // late old acknowledgement is deliberately retained by the test runtime
    f.s.input.focused=true;++f.s.nativeOwner.actorGeneration;++f.s.nativeOwner.equipGeneration;++f.s.nativeOwner.space;
    f.reserve.identity.owner=f.s.nativeOwner;f.reserve.identity.serverItem+=0x1000;
    ++f.s.input.owner.actorGeneration;++f.s.input.owner.equipGeneration;++f.s.input.owner.space;++f.s.weapon.generation;
    f.s.trackingEpoch=f.s.nativeOwner.space;f.mesh->owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;
    f.gun.reset();f.hands.Reset();f.allowRetire=true;f.Send(false);f.Send(false);f.Send(false);f.Send(true);
    CHECK(f.starts==2&&f.result.ammoOwnsHand&&f.Report().find("\"reconciled\":1")!=std::string::npos);
    CHECK(f.Report().find("\"completed\":0")!=std::string::npos&&f.ack->acknowledgement.identity==oldNative);return 0;}
int ExpiredSourceAndStaleGeometry(){Fixture f;CHECK(f.Insert());f.reserveAvailable=false;
    for(unsigned n=0;n<6;++n)f.Send(true);CHECK(f.cancels==1&&f.Report().find("\"completed\":0")!=std::string::npos);
    f.allowRetire=true;f.reserveAvailable=true;f.Send(false);f.Send(false);f.Send(false);f.Send(true);CHECK(f.starts==2);
    Fixture stale;CHECK(stale.Begin());stale.held=true;
    for(float z:{-.05f,-.05f,-.02f,0.f,.025f,.05f,.05f,.05f,.05f,.05f,.05f,.05f,.05f,.05f})stale.Send(true,z,true,true);
    CHECK(stale.submits==0);return 0;}
int FullFinalRoundAndFlagBoundary(){Fixture f;f.reserve.loaded=7;CHECK(f.Insert());f.Ack();f.Send(true);
    CHECK(f.Report().find("\"completed\":1")!=std::string::npos&&f.cancels==1&&!f.result.ammoOwnsHand);
    Fixture last;last.reserve.reserve=1;CHECK(last.Insert());last.Ack();last.Send(true);
    CHECK(last.Report().find("\"completed\":1")!=std::string::npos&&last.cancels==1&&!last.result.ammoOwnsHand);
    constexpr auto flags=0x2000000u|0x197800u|9;CHECK(ValidPhysicalReloadConfig(flags));CHECK(ValidPhysicalReloadConfig(flags|0x400u));
    CHECK(ValidPhysicalReloadConfig(0));for(auto bad:{0x8000u,0x20000u,0x40000u,0x400000u,0x800000u,0x1000000u})CHECK(!ValidPhysicalReloadConfig(flags|bad));
    for(auto missing:{0x800u,0x1000u,0x2000u,0x4000u,0x10000u,0x80000u,0x100000u})CHECK(!ValidPhysicalReloadConfig(flags&~missing));return 0;}
int AsyncContactSeatIsNotLost(){Fixture f;CHECK(f.SeatAsync());const auto original=f.s.input;
    for(unsigned n=0;n<3;++n){f.now+=1000000;f.Sync();CHECK(f.submits==0);} // Duplicate input cannot dispatch.
    f.SendAsync(.05f); // The next genuine input may consume the retained one-shot seat.
    if(f.submits!=1)std::cerr<<f.Report()<<'\n';CHECK(f.submits==1&&f.submitted&&f.submittedInput==original.sequence+1);
    const auto report=f.Report();
    CHECK(report.find("\"original_input\":{\"sequence\":"+std::to_string(original.sequence)+",\"observed_ns\":"+std::to_string(original.observedNs)+",\"deadline_ns\":"+std::to_string(original.deadlineNs))!=std::string::npos);
    for(unsigned n=0;n<3;++n)f.SendAsync(.05f);CHECK(f.submits==1);f.Ack();f.SendAsync(.05f);
    CHECK(f.Report().find("\"completed\":1")!=std::string::npos);return 0;}
int DeferredSeatCannotSpendExpiredOrReplacementEvidence(){
    {Fixture f;CHECK(f.SeatAsync());f.now+=100000000;f.SendAsync(.05f);CHECK(f.submits==0);}
    {Fixture f;CHECK(f.SeatAsync());const auto old=f.hands.Current(InteractionHand::Left)->token;
        f.Send(false);f.Send(true);CHECK(f.submits==0&&f.result.ammoOwnsHand&&f.hands.Current(InteractionHand::Left)->token!=old);
        f.SendAsync(.05f);CHECK(f.submits==0);}
    {Fixture f;CHECK(f.SeatAsync());f.s.cancel=true;f.allowRetire=true;f.Send(true);
        CHECK(f.submits==0&&f.Report().find("\"retiring\":false")!=std::string::npos);}
    return 0;
}
int CancellationStillDrainsAndReconciles(){
    Fixture f;CHECK(f.Insert());f.allowRetire=true;f.s.cancel=true;
    f.Send(true);f.Send(true);f.Send(true);
    CHECK(f.cancels==1&&f.starts==1&&f.submits==1&&f.Report().find("\"retiring\":false")!=std::string::npos);
    CHECK(f.Report().find("\"reconciled\":1")!=std::string::npos&&f.Report().find("\"completed\":0")!=std::string::npos);
    f.s.cancel=false;f.Send(false);f.Send(true);CHECK(f.starts==2);
    Fixture expired;CHECK(expired.Begin());expired.keepOkay=false;expired.Send(true);CHECK(expired.cancels==1);
    expired.keepOkay=true;expired.allowRetire=true;expired.Send(false);expired.Send(true);
    CHECK(expired.starts==2&&expired.result.ammoOwnsHand&&expired.submits==0);return 0;
}
int SecondShellCannotUseFirstReceipt(){
    Fixture f;CHECK(f.Insert());const auto first=*f.submitted;f.Ack();f.Send(true,.05f);
    CHECK(f.policy->ProbeState(f.now).completed==1);const auto oldAck=f.ack;
    f.held=true;f.submitted.reset();f.ack.reset();
    for(unsigned n=0;n<4;++n)f.Send(true,.05f);
    CHECK(f.policy->ProbeState(f.now).acquired==1&&f.submits==1); // Held trigger is no new pouch edge.
    f.Send(false);f.Send(true);CHECK(f.starts==1&&f.result.ammoOwnsHand);
    for(float z:{-.05f,-.05f,-.02f,0.f,.025f,.05f,.05f,.05f,.05f,.05f,.05f,.05f})f.SendAsync(z);
    f.SendAsync(.05f); // Fresh input consumes the retained renderer seat.
    if(f.submits!=2)std::cerr<<f.Report()<<'\n';CHECK(f.submits==2&&f.submitted);
    const auto second=*f.submitted;
    CHECK(first.reservation.item!=second.reservation.item&&first.reservation.claim!=second.reservation.claim&&first.request.id!=second.request.id);
    CHECK(first.heldLease.cycle==second.heldLease.cycle);
    f.ack=oldAck;f.Send(true,.05f);
    CHECK(f.policy->ProbeState(f.now).completed==1&&f.reserve.loaded==3&&f.reserve.reserve==7);
    f.ack.reset();f.Ack();f.Send(true,.05f);
    CHECK(f.policy->ProbeState(f.now).completed==2&&f.reserve.loaded==4&&f.reserve.reserve==6&&f.starts==1);
    return 0;
}
int PartialReceiptRetainsHoldThroughSevenSecondsNeutral(){
    Fixture f;CHECK(f.Insert());f.Ack();f.Send(true,.05f);
    CHECK(f.policy->ProbeState(f.now).completed==1&&f.reserve.loaded==3&&f.reserve.reserve==7);
    f.held=true;f.submitted.reset();f.ack.reset();const auto keeps=f.keeps;
    for(unsigned n=0;n<350;++n){f.Send(false);const auto state=f.policy->ProbeState(f.now);
        CHECK(state.active&&state.nativeHolding&&!state.held&&!state.pending&&f.cancels==0&&f.starts==1&&f.submits==1);
        CHECK(f.reserve.loaded==3&&f.reserve.reserve==7&&!f.result.reloadHeld&&!f.result.tracking.preview);}
    CHECK(f.keeps>=keeps+350);
    f.s.cancel=true;f.s.cancelFlags=ReloadCancelFire;f.Send(false);
    const auto report=f.Report();CHECK(f.cancels==1&&!f.policy->ProbeState(f.now).active);
    CHECK(report.find("\"reason_name\":\"requested_input\",\"source_flags\":16")!=std::string::npos);
    CHECK(report.find("\"held\":false,\"item_generation\":0,\"pending\":false")!=std::string::npos);
    CHECK(report.find("\"loaded\":3,\"reserve\":7,\"capacity\":8")!=std::string::npos);
    CHECK(report.find("\"lease_present\":true,\"all_three_held\":true")!=std::string::npos);
    return 0;
}
int CancellationEvidenceDistinguishesSafetyCapacityAndPending(){
    {Fixture f;CHECK(f.Insert());f.s.cancel=true;f.s.cancelFlags=ReloadCancelUse|ReloadCancelNextWeapon;f.Send(true);
        const auto report=f.Report();CHECK(f.cancels==1&&f.submits==1&&f.reserve.loaded==2&&f.reserve.reserve==8);
        CHECK(report.find("\"reason_name\":\"requested_input\",\"source_flags\":96")!=std::string::npos);
        CHECK(report.find("\"held\":false,\"item_generation\":0,\"pending\":true,\"pending_request\":1")!=std::string::npos);}
    {Fixture f;f.reserve.loaded=7;CHECK(f.Insert());f.Ack();f.Send(true);
        CHECK(f.Report().find("\"reason_name\":\"full_magazine\",\"source_flags\":0")!=std::string::npos);}
    {Fixture f;f.reserve.reserve=1;CHECK(f.Insert());f.Ack();f.Send(true);
        CHECK(f.Report().find("\"reason_name\":\"empty_reserve\",\"source_flags\":0")!=std::string::npos);}
    {Fixture f;CHECK(f.Begin());f.held=true;f.Send(true);f.keepOkay=false;f.Send(true);
        CHECK(f.Report().find("\"reason_name\":\"keepalive_rejected\",\"source_flags\":0")!=std::string::npos);}
    {Fixture f;CHECK(f.Begin());f.s.input.focused=false;f.Send(false);
        CHECK(f.Report().find("\"reason_name\":\"unsafe_input\",\"source_flags\":0")!=std::string::npos);}
    {Fixture f;CHECK(f.Begin());f.s.asset="XM8_sp";f.Send(true);
        CHECK(f.Report().find("\"reason_name\":\"wrong_asset\",\"source_flags\":0")!=std::string::npos);}
    {Fixture f;CHECK(f.Begin());f.s.cancelFlags=ReloadCancelFire;
        f.policy->Cancel(f.s.input,f.hands); // External cleanup must not inherit last input's labels.
        CHECK(f.Report().find("\"reason_name\":\"external\",\"source_flags\":0")!=std::string::npos);}
    return 0;
}
int CancellationEvidenceIsBoundedWithoutAffectingRetirement(){
    Fixture f;CHECK(f.Begin());f.allowRetire=true;
    for(unsigned n=0;n<36;++n){f.s.cancel=true;f.s.cancelFlags=ReloadCancelUse;f.Send(true);
        f.s.cancel=false;f.s.cancelFlags=0;f.Send(false);f.Send(true);}
    CHECK(f.cancels==36&&f.starts==37&&f.submits==0&&f.reserve.loaded==2&&f.reserve.reserve==8);
    const auto report=f.Report();CHECK(report.find("\"capacity\":32,\"total\":36,\"dropped\":4")!=std::string::npos);
    std::size_t count=0,at=0;while((at=report.find("\"reason_name\":",at))!=std::string::npos){++count;++at;}
    CHECK(count==32);return 0;
}
}
namespace {
void SupplyReplaceSelected(Fixture& f,std::string_view asset,bool reserveAvailable){
 f.s.asset=asset;f.reserveAvailable=reserveAvailable;
 ++f.s.nativeOwner.equipGeneration;++f.s.input.owner.equipGeneration;
 f.s.weapon.generation=f.s.input.owner.equipGeneration;f.reserve.identity.owner=f.s.nativeOwner;
 f.mesh->owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;f.gun.reset();f.hands.Reset();
}
void SupplyCurrentMesh(Fixture& f){
 // This safety-only path does not compare independently supplied counters.
 // Preserve the fixture's deliberately larger configured-mesh sequence.
 f.result=f.policy->Tick(f.s,f.hands,f.intent);
}
int AvailabilityDiagnosesBeforeAnyCycle(){
 {Fixture f;f.reserveAvailable=false;f.Send(false);const auto r=f.Report();
  CHECK(f.starts==0&&r.find("\"supply_availability\"")!=std::string::npos&&r.find("\"reserve_current\":false")!=std::string::npos);}
 {Fixture f;f.s.raw.valid=false;f.Send(false);CHECK(f.starts==0&&f.Report().find("\"raw_valid\":false")!=std::string::npos);}
 {Fixture f;f.s.input.tracked[1]=false;f.Send(false);CHECK(f.starts==0&&f.Report().find("\"gun_claim_current\":false")!=std::string::npos);}
 {Fixture f;f.s.cancel=true;f.s.cancelFlags=ReloadCancelBodyDraw;f.Send(false);CHECK(f.starts==0&&f.Report().find("\"source_flags\":1")!=std::string::npos);}
 {Fixture f;f.s.cancel=true;f.Send(false);CHECK(f.starts==0&&f.Report().find("\"unattributed_cancel\":true")!=std::string::npos);}
 {Fixture f;f.reserve.loaded=f.reserve.capacity;f.reserve.allThreeIdle=true;f.policy->EnableBeltAmmo();f.Send(false);f.Send(true);
  CHECK(f.starts==0&&!f.result.ammoOwnsHand&&f.result.tracking.belt);CHECK(f.Report().find("\"loaded\":8,\"reserve\":8,\"capacity\":8")!=std::string::npos);}
 {Fixture f;f.reserve.reserve=0;f.Send(false);f.Send(true);CHECK(f.starts==0&&!f.result.ammoOwnsHand&&!f.result.tracking.belt);
  CHECK(f.Report().find("\"loaded\":2,\"reserve\":0,\"capacity\":8")!=std::string::npos);}
 {Fixture f;f.reserve.loaded=0;CHECK(f.Begin());CHECK(f.result.ammoOwnsHand&&f.reserve.loaded==0&&f.reserve.reserve==8);}
 {Fixture f;f.s.bodyFromHand=Pose(2,0,0);f.Send(false);f.Send(true);CHECK(f.starts==0);
  CHECK(f.Report().find("\"supply_reason\":"+std::to_string(unsigned(AmmoSupplyReason::OutsidePouch)))!=std::string::npos);}
 {Fixture f;f.Send(true);CHECK(f.starts==0);CHECK(f.Report().find("\"supply_reason\":"+std::to_string(unsigned(AmmoSupplyReason::NeedNeutral)))!=std::string::npos);}
 return 0;
}
int AvailabilityRingIsBoundedAndTransitionOnly(){
 Fixture f;f.Send(false);for(unsigned n=0;n<10;++n)f.Send(false);
 // The first source observation changes owner; the next neutral sample is
 // ready. Subsequent identical eligibility does not log sequence/time churn.
 CHECK(f.Report().find("\"samples\":11,\"transitions\":2")!=std::string::npos);
 for(unsigned n=0;n<110;++n){f.s.bodyFromHand=Pose(n%2?2.f:0.f);f.Send(false);}
 const auto report=f.Report();CHECK(report.find("\"capacity\":96")!=std::string::npos);
 CHECK(report.find("\"overwritten\":15")!=std::string::npos&&report.size()<180000);
 CHECK(f.starts==0&&f.submits==0&&f.cancels==0);return 0;
}
int PendingRetirementUnblocksOnlyFreshDifferentEquipment(){
 Fixture f;CHECK(f.Insert());const int loaded=f.reserve.loaded,reserve=f.reserve.reserve;
 f.s.cancel=true;f.allowRetire=true;f.Send(false);SupplyCurrentMesh(f);
 CHECK(f.policy->BlocksEquipment()&&f.policy->ProbeState(f.now).pending); // same item still quarantined
 f.s.cancel=false;f.reserveAvailable=false;++f.s.nativeOwner.space;++f.s.input.owner.space;f.s.trackingEpoch=f.s.nativeOwner.space;
 f.reserve.identity.owner=f.s.nativeOwner;f.mesh->owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;
 f.gun.reset();f.hands.Reset();f.Send(false);SupplyCurrentMesh(f);CHECK(f.policy->BlocksEquipment()); // recenter alone
 SupplyReplaceSelected(f,"AEK971_sp",false);f.Send(false);f.mesh->owner.weapon++;
 SupplyCurrentMesh(f);CHECK(f.policy->BlocksEquipment()); // wrong current configured owner
 f.mesh->owner=f.s.nativeOwner;SupplyCurrentMesh(f);CHECK(!f.policy->BlocksEquipment());
 CHECK(f.policy->ProbeState(f.now).pending&&!f.result.ammoOwnsHand&&!f.result.tracking.preview&&!f.result.reloadHeld);
 for(unsigned n=0;n<20;++n){f.Send(false);SupplyCurrentMesh(f);CHECK(!f.policy->BlocksEquipment());}
 CHECK(f.reserve.loaded==loaded&&f.reserve.reserve==reserve&&f.policy->ProbeState(f.now).pending);
 // Losing current safety must not allow the stale replacement packet to keep
 // the block exemption. The durable drain fact never renews input/mesh proof.
 f.s.input.focused=false;f.Send(false);SupplyCurrentMesh(f);CHECK(f.policy->BlocksEquipment());
 f.s.input.focused=true;f.Send(false);SupplyCurrentMesh(f);CHECK(!f.policy->BlocksEquipment());
 const auto meshDeadline=f.mesh->deadlineNs;f.mesh->deadlineNs=f.now;SupplyCurrentMesh(f);CHECK(f.policy->BlocksEquipment());
 f.mesh->deadlineNs=meshDeadline;SupplyCurrentMesh(f);CHECK(!f.policy->BlocksEquipment());
 const auto mesh=f.s.meshes;f.s.meshes.reset();SupplyCurrentMesh(f);CHECK(f.policy->BlocksEquipment());
 f.s.meshes=mesh;SupplyCurrentMesh(f);CHECK(!f.policy->BlocksEquipment());
 --f.now;SupplyCurrentMesh(f);CHECK(f.policy->BlocksEquipment()); // clock rollback cannot retain the exemption
 ++f.now;SupplyCurrentMesh(f);CHECK(!f.policy->BlocksEquipment());
 SupplyReplaceSelected(f,SpasReloadAsset,true);f.Send(false);SupplyCurrentMesh(f);
 for(unsigned n=0;n<4;++n)f.Send(false);
 CHECK(!f.policy->BlocksEquipment()&&!f.policy->ProbeState(f.now).pending);
 f.Send(true);CHECK(f.starts==2&&f.result.ammoOwnsHand&&f.reserve.loaded==loaded&&f.reserve.reserve==reserve);return 0;
}
int RetirementReceiptMustBeExactFreshAndReal(){
 for(unsigned variant=1;variant<=6;++variant){Fixture f;CHECK(f.Insert());
  SupplyReplaceSelected(f,"AEK971_sp",false);f.allowRetire=true;f.retirementVariant=variant;
  f.Send(false);SupplyCurrentMesh(f);CHECK(f.policy->BlocksEquipment()&&f.policy->ProbeState(f.now).pending);
  f.retirementVariant=0;f.Send(false);SupplyCurrentMesh(f);CHECK(!f.policy->BlocksEquipment());
 }
 // Real retirement receipts are stamped inside the native API, after Tick's
 // initial clock read. Processing time may accept that receipt but may never
 // renew the current input/mesh/hand leases across a slow call.
 for(auto delay:{1000000ll,101000000ll}){Fixture f;CHECK(f.Insert());
  SupplyReplaceSelected(f,"AEK971_sp",false);f.allowRetire=true;f.retirementDelay=delay;f.Send(false);
  CHECK(f.policy->ProbeState(f.now).pending&&f.policy->BlocksEquipment()==(delay>100000000));
 }
 return 0;
}
int SupplyReturnsAfterOrdinaryWeaponTransitions(){
 Fixture f;f.reserve.allThreeIdle=true;f.policy->EnableBeltAmmo();f.Send(false);CHECK(f.result.tracking.belt);
 for(auto asset:{"AEK971_sp","XM8_sp_s"}){SupplyReplaceSelected(f,asset,false);f.Send(false);f.Send(true);
  CHECK(!f.policy->BlocksEquipment()&&!f.result.ammoOwnsHand&&!f.result.tracking.belt);}
 SupplyReplaceSelected(f,SpasReloadAsset,true);f.Send(false);CHECK(f.result.tracking.belt);
 f.Send(true);CHECK(f.result.ammoOwnsHand&&f.starts==1);return 0;
}
}

namespace {
void SupplyReplaceWithScopedAlias(Fixture& f){
 SupplyReplaceSelected(f,Xm8MagazineAsset,false);
 f.s.weapon={0xc0000,f.s.input.owner.equipGeneration}; // PersistentWeapon is NOT native rifle0x40000.
 const auto& geometry=Xm8MagazineGeometry();auto& mesh=f.mesh->states[0].meshes[0];
 mesh.kind=geometry.meshKind;mesh.assetPath={};std::copy(geometry.mesh.begin(),geometry.mesh.end(),mesh.assetPath.begin());
 MagazineFamilyEvidence e;e.binding.owner=f.s.nativeOwner;e.binding.weapon=f.s.weapon;
 e.binding.inventory=0xd0000;e.binding.launcher=0xe0000;e.binding.launcherSlot=4;
 e.observedNs=f.now;e.deadlineNs=f.now+100000000;e.verified=true;f.s.replacementFamily=e;
 f.gun.reset();f.hands.Reset();
}
int ScopedAliasRetirementUsesActualMapping(){
 for(unsigned k=0;k<11;++k){Fixture f;CHECK(f.Insert());const auto loaded=f.reserve.loaded,reserve=f.reserve.reserve;
  SupplyReplaceWithScopedAlias(f);CHECK(f.s.weapon.id!=f.s.nativeOwner.weapon);
  f.allowRetire=k!=10;MagazineEquipmentProfile wrongProfile=Xm8MagazineEquipment();
  if(k==1)f.s.replacementFamily.reset();
  if(k==2)f.s.replacementFamily->deadlineNs=f.now;
  if(k==3)++f.s.replacementFamily->binding.owner.equipGeneration;
  if(k==4)++f.s.replacementFamily->binding.weapon.id;
  if(k==5)f.s.replacementFamily->verified=false;
  if(k==6){wrongProfile.geometryVerified=false;f.s.replacementFamily->binding.profile=&wrongProfile;}
  if(k==7)f.mesh->states[0].meshes[0].assetPath[0]='!';
  if(k==8)f.s.replacementFamily->binding.launcher=f.s.nativeOwner.weapon;
  if(k==9)f.s.replacementFamily->observedNs=f.now+40000000;
  f.Send(false);
  CHECK(f.policy->BlocksEquipment()==(k!=0)&&f.policy->ProbeState(f.now).pending);
  CHECK(!f.result.ammoOwnsHand&&!f.result.tracking.preview&&!f.result.tracking.belt&&!f.result.reloadHeld);
  CHECK(f.starts==1&&f.submits==1&&f.reserve.loaded==loaded&&f.reserve.reserve==reserve);
  if(k==0){
   // Every subsequent exemption still needs the original mapping's deadline;
   // fresh controller/mesh packets do not refresh the family receipt.
   for(unsigned n=0;n<5;++n)f.Send(false);
   CHECK(f.policy->BlocksEquipment()&&f.policy->ProbeState(f.now).pending);
  }
 }
 return 0;
}
int ScopedAliasCannotGrantSpasSupply(){
 Fixture f;SupplyReplaceWithScopedAlias(f);f.reserveAvailable=true;
 f.s.asset=SpasReloadAsset;const auto& geometry=SpasReloadMesh;auto& mesh=f.mesh->states[0].meshes[0];
 mesh.kind=SelectedMeshKind::Spas12;mesh.assetPath={};std::copy(geometry.begin(),geometry.end(),mesh.assetPath.begin());
 f.Send(false);f.Send(true);CHECK(f.starts==0&&!f.result.ammoOwnsHand&&!f.result.tracking.preview&&!f.result.tracking.belt);
 return 0;
}
}

namespace {
int VehicleAndEquipmentSourceGapRequiresFreshNativeRecovery(){
 for(unsigned transition=0;transition<3;++transition)for(int loaded:{0,2,8}){
  Fixture f;CHECK(f.Begin());CHECK(f.starts==1&&f.result.ammoOwnsHand);
  const auto original=f.reserve.identity;
  // The real non-infantry path cancels physical hand ownership. No pending
  // native insertion exists here; the exact callback drain still must occur.
  f.policy->Cancel(f.s.input,f.hands);f.hands.Reset();f.gun.reset();f.reserveAvailable=false;f.allowRetire=true;
  f.s.input.owner.equipGeneration+=2;f.s.weapon.generation=f.s.input.owner.equipGeneration;f.s.nativeOwner.equipGeneration+=2;
  if(transition==0){++f.s.input.owner.space;++f.s.nativeOwner.space;f.s.trackingEpoch=f.s.nativeOwner.space;}
  if(transition==1){f.s.nativeOwner.weapon+=0x100;f.s.weapon.id=f.s.nativeOwner.weapon;}
  if(transition==2){f.s.nativeOwner.soldier+=0x100;f.s.nativeOwner.weak+=0x100;++f.s.nativeOwner.actorGeneration;
   f.s.input.owner.actor=(std::uint64_t(f.s.nativeOwner.weak)<<32)|f.s.nativeOwner.soldier;
   f.s.input.owner.actorGeneration=f.s.nativeOwner.actorGeneration;}
  f.mesh->owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;f.reserve.loaded=loaded;
  // Reproduce the retained failure: valid current gun/mesh/raw geometry and
  // fresh squeeze at the pouch cannot substitute for missing native counts.
  for(unsigned n=0;n<15;++n){f.Send(n%2==1);CHECK(!f.result.ammoOwnsHand&&!f.result.tracking.preview&&!f.result.tracking.belt);
   CHECK(!f.policy->BlocksEquipment()&&f.starts==1&&f.submits==0&&!f.policy->ProbeState(f.now).reserve);}
  CHECK(f.Report().find("\"cancel_reason\":6")!=std::string::npos);
  f.reserveAvailable=true; // A fresh timestamp with the PREVIOUS native owner remains invalid.
  f.Send(false);f.Send(true);CHECK(f.reserve.identity==original);
  CHECK(f.starts==1&&!f.result.ammoOwnsHand&&!f.policy->ProbeState(f.now).reserve);
  // Only the actual new native identity/count source restores availability.
  f.reserve.identity.owner=f.s.nativeOwner;f.Send(false);f.Send(true);
  CHECK(f.policy->ProbeState(f.now).reserve&&f.reserve.loaded==loaded&&f.reserve.reserve==8);
  CHECK(f.starts==(loaded==8?1u:2u)&&f.result.ammoOwnsHand==(loaded!=8));
  CHECK(f.submits==0&&f.policy->ProbeState(f.now).completed==0);
 }
 std::cout<<"Physical supply recovery: 9 actual-consumer vehicle/equipment/actor and empty/partial/full variants passed\n";
 return 0;
}
}

namespace {
ReloadObservedConfig CurrentSpasConfig(){
 const auto& d=SpasReloadDescriptor;ReloadObservedConfig c;
 c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
 const auto&v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
int ActualSelectorControlsFreshShellSourceAfterRevisits(){
 Fixture f;Bc2ReloadNativePolicy selected;f.selectedPolicy=&selected;f.sourceConfig=CurrentSpasConfig();
 CHECK(selected.MatchesSelectedConfig(f.sourceConfig));f.s.raw.inputEvidence=f.s.input;f.Sync();CHECK(f.policy->ProbeState(f.now).reserve);
 ReloadCycleControl c{f.reserve.identity,1,1,f.now,f.now+100000000,true};
 ReloadRetirementReceipts receipts;auto receiptTime=f.now;
 const auto retire=[&]{return CancelAndDrainReloadCycle(selected,selected.Identity(),selected.Cycle(),true)&&
   bool(receipts.Observe(selected,selected.Identity(),selected.Cycle(),++receiptTime,true));};
 CHECK(selected.Start(c,f.now));CHECK(retire());CHECK(selected.SelectMagazineProfile(NativeMagazineProfileId::ScopedXm8,true,true));
 const auto&o=c.identity.owner;ManualReloadRequest unseat{1,{o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space},ReloadOperation::UnseatMagazine,0,0};
 CHECK(selected.StartMagazine(c,unseat,f.now));CHECK(retire());CHECK(selected.SelectFamily(ReloadNativeFamily::SpasTube,true,true));
 // The native dispatcher has consumed its single receipt at each successful
 // switch. The policy must remember only the exact stopped cycle, not counts.
 CHECK(selected.SelectMagazineProfile(NativeMagazineProfileId::AuthoredAek,true,false));
 CHECK(!selected.MatchesSelectedConfig(f.sourceConfig));
 for(unsigned n=0;n<12;++n)f.Send(false);f.Send(true);
 CHECK(!f.policy->ProbeState(f.now).reserve&&!f.result.ammoOwnsHand&&f.starts==0&&f.submits==0);
 CHECK(selected.SelectFamily(ReloadNativeFamily::SpasTube,true,false));CHECK(selected.MatchesSelectedConfig(f.sourceConfig));
 CHECK(!selected.Lease(c.identity,c.cycle,f.now)); // Remembered stop is never a live hold or ammo receipt.
 f.Send(false);f.Send(true);
 CHECK(f.policy->ProbeState(f.now).reserve&&f.result.ammoOwnsHand&&f.starts==1&&f.submits==0);
 CHECK(f.reserve.loaded==2&&f.reserve.reserve==8&&f.policy->ProbeState(f.now).completed==0);
 return 0;
}
}

int DeferredKeepAliveNeverRenews(){
 auto fp=std::make_unique<Fixture>();auto& f=*fp;CHECK(f.Begin());const auto deadline=f.s.input.deadlineNs;f.keepDeferred=true;
 for(unsigned i=0;i<4;++i){f.Send(true);CHECK(f.now<deadline&&f.cancels==0&&f.submits==0&&f.policy->BlocksEquipment());CHECK(!f.result.reloadHeld);}
 f.Send(true);CHECK(f.now==deadline&&f.cancels==1&&f.submits==0);
 auto gp=std::make_unique<Fixture>();auto& g=*gp;CHECK(g.Begin());g.keepDeferred=true;g.Send(true);CHECK(g.cancels==0);g.keepDeferred=false;g.Send(true);CHECK(g.cancels==0);g.keepOkay=false;g.Send(true);CHECK(g.cancels==1);return 0;
}
int ObserverDeferralAndStartOrigin(){
 auto ptr=std::make_unique<Fixture>();auto& f=*ptr;f.s.actionFlagsKnown=true;f.s.actionHeld=123;f.s.actionPressed=456;CHECK(f.Begin());
 const auto journal=f.Report();CHECK(journal.find("\"start_flags_known\":true")!=std::string::npos);
 CHECK(journal.find("\"start_grip_pressed\":true")!=std::string::npos&&journal.find("\"start_held\":123")!=std::string::npos);
 const auto deadline=f.s.input.deadlineNs;f.reserveDeferred=true;
 for(unsigned i=0;i<4;++i){f.Send(true);CHECK(f.now<deadline&&f.cancels==0&&f.submits==0&&f.policy->BlocksEquipment());}
 f.Send(true);CHECK(f.now==deadline&&f.cancels==1&&f.submits==0);
 auto gp=std::make_unique<Fixture>();auto& g=*gp;CHECK(g.Begin());g.reserveRejected=true;g.Send(true);CHECK(g.cancels==1);return 0;
}
int SubmittedShellCohortGapRetainsOnlyOriginalLease(){
 auto ptr=std::make_unique<Fixture>();auto& f=*ptr;CHECK(f.Insert());
 const auto before=f.policy->ProbeState(f.now);CHECK(before.pending&&before.reserve);
 const auto original=*before.reserve;const auto deadline=f.s.input.deadlineNs;
 f.reserveCohortGap=true;++f.reserve.loaded; // One native copy transferred; API publishes no coherent count.
 f.Send(true,.05f);CHECK(f.cancels==0&&f.submits==1&&f.policy->ProbeState(f.now).completed==0);
 const auto gap=f.policy->ProbeState(f.now);CHECK(gap.reserve&&gap.reserve->sequence==original.sequence&&gap.reserve->observedNs==original.observedNs&&gap.reserve->deadlineNs==original.deadlineNs);
 CHECK(gap.reserve->loaded==original.loaded&&gap.reserve->reserve==original.reserve);
 --f.reserve.loaded;f.reserveCohortGap=false;f.Ack();f.Send(true,.05f);
 CHECK(f.now<deadline&&f.cancels==0&&f.submits==1&&f.policy->ProbeState(f.now).completed==1);
 for(unsigned fault=0;fault<4;++fault){auto qp=std::make_unique<Fixture>();auto& q=*qp;CHECK(q.Insert());q.reserveCohortGap=true;
  if(fault==0)q.s.input.focused=false;if(fault==1)++q.s.nativeOwner.space;
  if(fault==2){q.now+=100000000;}if(fault==3)q.reserveRejected=true;
  q.Send(true,.05f);CHECK(q.cancels==1&&q.submits==1&&q.policy->ProbeState(q.now).completed==0);
 }
 auto ap=std::make_unique<Fixture>();auto& a=*ap;CHECK(a.Begin());a.reserveCohortGap=true;a.Send(true);CHECK(a.cancels==0);
 return 0;
}

int StartupCohortGapRetainsOriginalShell(){
 auto fp=std::make_unique<Fixture>();auto& f=*fp;CHECK(f.Begin());const auto initial=f.policy->ProbeState(f.now);
 CHECK(initial.active&&initial.held&&!initial.nativeHolding);const auto token=f.hands.Current(InteractionHand::Left)->token;
 f.reserveCohortGap=true;f.Send(true);CHECK(f.cancels==0&&f.starts==1&&f.submits==0&&f.result.reloadHeld);
 CHECK(f.hands.Current(InteractionHand::Left)->token==token&&f.policy->ProbeState(f.now).held);
 CHECK(!f.result.tracking.preview&&!f.result.tracking.belt);
 f.reserveCohortGap=false;f.Send(true);
 CHECK(!f.policy->ProbeState(f.now).nativeHolding&&f.submits==0&&f.reserve.loaded==2&&f.reserve.reserve==8);
 f.held=true;f.Send(true);CHECK(f.cancels==0&&f.policy->ProbeState(f.now).nativeHolding);
 for(float z:{-.05f,-.05f,-.02f,0.f,.025f,.05f,.05f,.05f,.05f,.05f,.05f,.05f})f.Send(true,z);
 CHECK(f.submits==1);return 0;
}
int StartupCohortGapIsBounded(){
 auto fp=std::make_unique<Fixture>();auto& f=*fp;CHECK(f.Begin());f.reserveCohortGap=true;
 for(unsigned n=0;n<3;++n){f.Send(true);CHECK(f.cancels==0&&f.submits==0);}
 f.Send(true);CHECK(f.cancels==1&&f.submits==0);return 0;
}
int StartupCohortGapSafetyWins(){
 for(unsigned fault=0;fault<5;++fault){auto fp=std::make_unique<Fixture>();auto& f=*fp;CHECK(f.Begin());f.reserveCohortGap=true;
  if(fault==0)f.s.input.focused=false;if(fault==1)f.s.input.tracked[0]=false;
  if(fault==2)++f.s.nativeOwner.space;if(fault==3)f.reserveRejected=true;if(fault==4)f.keepOkay=false;
  f.Send(true);CHECK(f.cancels==1&&f.submits==0);}
 auto fp=std::make_unique<Fixture>();auto& f=*fp;CHECK(f.Begin());f.reserveCohortGap=true;f.Send(false);
 CHECK(f.cancels==1&&!f.hands.Current(InteractionHand::Left)&&f.submits==0);return 0;
}
int StartupGapKeepsOriginalClocks(){
 auto fp=std::make_unique<Fixture>();auto& f=*fp;CHECK(f.Begin());
 const auto started=f.now;for(unsigned n=0;n<6;++n)f.Send(true);CHECK(f.cancels==0&&!f.result.reloadHeld);
 f.reserveCohortGap=true;f.Send(true);CHECK(f.cancels==0&&!f.result.reloadHeld);
 // Duplicate input is allowed to cancel but cannot reset the first gap.
 f.now+=50*1000000;f.Sync();CHECK(f.cancels==1&&f.submits==0);
 auto gp=std::make_unique<Fixture>();auto& g=*gp;CHECK(g.Begin());
 for(unsigned n=0;n<150;++n)g.Send(true);CHECK(g.cancels==0);
 g.reserveCohortGap=true;g.Send(true);CHECK(g.cancels==1&&g.submits==0);
 CHECK(g.Report().find("hold_not_established")!=std::string::npos);
 auto qp=std::make_unique<Fixture>();auto& q=*qp;CHECK(q.Begin());q.reserveCohortGap=true;q.keepDelay=60*1000000;
 q.Send(true);CHECK(q.cancels==1&&q.submits==0);return 0;
}
int HeldShellTypedGapCannotGrantOrRenew(){
 auto fp=std::make_unique<Fixture>();auto& f=*fp;CHECK(f.Begin());f.held=true;f.Send(true,-.05f);
 const auto initial=f.policy->ProbeState(f.now);CHECK(initial.nativeHolding&&initial.reserve&&initial.held);
 const auto original=*initial.reserve;const auto deadline=f.s.input.deadlineNs;const auto keeps=f.keeps;
 f.reserveCohortGap=true;for(unsigned n=0;n<3;++n){f.Send(true,-.02f);
  const auto gap=f.policy->ProbeState(f.now);CHECK(f.cancels==0&&f.starts==1&&f.submits==0&&f.keeps==keeps);
  CHECK(gap.reserve&&gap.reserve->sequence==original.sequence&&gap.reserve->deadlineNs==original.deadlineNs&&gap.reserve->loaded==original.loaded);
  CHECK(!f.result.tracking.preview&&!f.result.tracking.belt&&!f.result.reloadHeld);}
 f.now=deadline;f.Send(true);CHECK(f.cancels==1&&f.submits==0);
 for(unsigned fault=0;fault<3;++fault){auto gp=std::make_unique<Fixture>();auto& g=*gp;CHECK(g.Begin());g.held=true;g.Send(true,-.05f);g.reserveCohortGap=true;
  if(fault==0)++g.s.nativeOwner.equipGeneration;if(fault==1)g.s.input.focused=false;if(fault==2)g.reserveRejected=true;
  g.Send(true);CHECK(g.cancels==1&&g.starts==1&&g.submits==0);}
 auto ap=std::make_unique<Fixture>();auto& a=*ap;a.reserveCohortGap=true;a.s.raw.inputEvidence=a.s.input;a.s.originalHandEvidence=a.s.input;a.Sync();a.Send(true);
 CHECK(a.starts==0&&a.submits==0&&!a.hands.Current(InteractionHand::Left));return 0;
}

int SubmittedShellReleasesHandBeforeNativeAmmoChanges(){
 auto fp=std::make_unique<Fixture>();auto& f=*fp;CHECK(f.Insert());
 CHECK(!f.result.ammoOwnsHand&&!f.hands.Current(InteractionHand::Left));
 CHECK(f.policy->ProbeState(f.now).pending&&f.submits==1&&f.reserve.loaded==2&&f.reserve.reserve==8);
 // Keep squeezing while moving from insertion to fore-end: the spent shell
 // cannot reacquire the hand or manufacture another native request.
 for(unsigned n=0;n<5;++n){f.Send(true,.7f);CHECK(!f.result.ammoOwnsHand&&f.submits==1);}
 f.Send(false,.7f);f.s.bodyFromHand=Pose(0,0,.7f);f.Send(true,.7f);
 const HandContactProof proof{{1234,1},f.s.input.sequence,f.s.input.deadlineNs,true};
 auto support=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,
     f.s.weapon,proof,++f.intent,f.gun->token.id}).claim;CHECK(support);
 for(unsigned n=0;n<25;++n){f.Send(true,.7f);
   support=f.hands.Renew(f.s.input,support->token,{{1234,1},f.s.input.sequence,f.s.input.deadlineNs,true}).claim;
   CHECK(support&&!f.result.ammoOwnsHand&&f.policy->ProbeState(f.now).pending);
   CHECK(f.submits==1&&f.reserve.loaded==2&&f.reserve.reserve==8);
 }
 // Exact native acknowledgement must not release the replacement support claim.
 f.Ack();f.Send(true,.7f);CHECK(f.policy->ProbeState(f.now).completed==1);
 CHECK(f.hands.Current(InteractionHand::Left)->token==support->token&&f.reserve.loaded==3&&f.reserve.reserve==7);
 return 0;
}
int main(){CHECK(SubmittedShellReleasesHandBeforeNativeAmmoChanges()==0);CHECK(StartupGapKeepsOriginalClocks()==0);CHECK(StartupCohortGapRetainsOriginalShell()==0);CHECK(StartupCohortGapIsBounded()==0);CHECK(StartupCohortGapSafetyWins()==0);CHECK(HeldShellTypedGapCannotGrantOrRenew()==0);CHECK(SubmittedShellCohortGapRetainsOnlyOriginalLease()==0);CHECK(ObserverDeferralAndStartOrigin()==0);CHECK(DeferredKeepAliveNeverRenews()==0);if(ActualSelectorControlsFreshShellSourceAfterRevisits()||VehicleAndEquipmentSourceGapRequiresFreshNativeRecovery()||ScopedAliasRetirementUsesActualMapping()||ScopedAliasCannotGrantSpasSupply()||PendingRetirementUnblocksOnlyFreshDifferentEquipment()||AvailabilityDiagnosesBeforeAnyCycle()||AvailabilityRingIsBoundedAndTransitionOnly()||RetirementReceiptMustBeExactFreshAndReal()||SupplyReturnsAfterOrdinaryWeaponTransitions()||BeltReturnsBetweenActualAcknowledgedShells()||BetweenShellBeltRejectsLostHoldAndCancellation()||BeltAvailabilityComesFromActualConsumer()||FullGunCanShowReserveButCannotMintShell()||BeltRespectsNativeBusyAndSafetyWithoutNewAuthority()||BodyPouchBeltSharesTheExistingAlternateContact()||FeedbackDistinguishesLatchFromVerifiedReload()||FeedbackKeepsFinalReceiptButNotCancellation()||GuidedContinuityCannotBorrowNewDeadlines()||GuidedContinuityEndsOnFreshWithdrawalOrRejectedGeometry()||GuidedContinuityRequiresCurrentCycleClaimsAndHold()||GuidedPresentationSurvivesOneMissingGeometryTick()||CycleVisibilitySurvivesPendingAndReceiptOnlyWhileActive()||PartialReceiptRetainsHoldThroughSevenSecondsNeutral()||CancellationEvidenceDistinguishesSafetyCapacityAndPending()||CancellationEvidenceIsBoundedWithoutAffectingRetirement()||FreeCarryUsesCurrentRendererWrist()||UnderPortConsumerToAcknowledgement()||GeometryRingIsBoundedAndKeepsOriginalSource()||CarryDuringPumpWaitsForFreshNativeReadiness()||PendingReadyRejectsReleasedStaleOrChangedEvidence()||SecondShellCannotUseFirstReceipt()||AsyncContactSeatIsNotLost()||DeferredSeatCannotSpendExpiredOrReplacementEvidence()||CancellationStillDrainsAndReconciles()||StartRealCycleAndOriginalTime()||RailToNativeAck()||DefaultAndEvidenceGates()||SupportCannotBeStolen()||CancelPendingAndRebaseline()||
    ReplacementSurvivesOriginalAck()||DeathNewOwnerAndLateOldAck()||ExpiredSourceAndStaleGeometry()||FullFinalRoundAndFlagBoundary())return 1;
    std::cout<<"Bc2PhysicalReload: 47 actual-composition cases passed (test runtime only; no headset claim)\n";}
