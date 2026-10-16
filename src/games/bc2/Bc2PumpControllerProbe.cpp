#include "Bc2PumpControllerProbe.h"
#include <ostream>
#include <algorithm>
#include <cmath>
#include <limits>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
std::optional<math::Matrix4> Controller(const math::Pose& pose)noexcept {
    const auto view=math::MakeLhViewFromOpenXRPose(pose);return view?InverseRigid(*view):std::nullopt;
}
std::optional<math::Pose> Pose(const math::Matrix4& row)noexcept {
    if(!Rigid(row))return {};
    // Quaternion from column rotation; row-vector LH basis is its transpose.
    float m[3][3];for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m[r][c]=row.values[c][r];
    math::Quaternion q;const float tr=m[0][0]+m[1][1]+m[2][2];
    if(tr>0){const float s=std::sqrt(tr+1)*2;q.w=.25f*s;q.x=(m[2][1]-m[1][2])/s;q.y=(m[0][2]-m[2][0])/s;q.z=(m[1][0]-m[0][1])/s;}
    else if(m[0][0]>m[1][1]&&m[0][0]>m[2][2]){const float s=std::sqrt(1+m[0][0]-m[1][1]-m[2][2])*2;q.w=(m[2][1]-m[1][2])/s;q.x=.25f*s;q.y=(m[0][1]+m[1][0])/s;q.z=(m[0][2]+m[2][0])/s;}
    else if(m[1][1]>m[2][2]){const float s=std::sqrt(1+m[1][1]-m[0][0]-m[2][2])*2;q.w=(m[0][2]-m[2][0])/s;q.x=(m[0][1]+m[1][0])/s;q.y=.25f*s;q.z=(m[1][2]+m[2][1])/s;}
    else{const float s=std::sqrt(1+m[2][2]-m[0][0]-m[1][1])*2;q.w=(m[1][0]-m[0][1])/s;q.x=(m[0][2]+m[2][0])/s;q.y=(m[1][2]+m[2][1])/s;q.z=.25f*s;}
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);if(!std::isfinite(n)||n<.5f)return {};
    math::Pose out;out.orientation={-q.x/n,-q.y/n,q.z/n,q.w/n};out.position={row.values[3][0],row.values[3][1],-row.values[3][2]};
    const auto round=Controller(out);if(!round||Distance(*round,row)>.0005f||Angle(*round,row)>.005f)return {};return out;
}
bool StaticReference(const InputFrame& s)noexcept {
    const auto ref=Controller(s.referenceHead),head=Controller(s.head);
    return ref&&head&&Distance(*ref,Identity())<.0001f&&Angle(*ref,Identity())<.001f&&
        Distance(*head,Identity())<.0001f&&Angle(*head,Identity())<.001f;
}
math::Pose Step(const math::Pose& from,const math::Pose& target)noexcept {
    auto out=target;const float d=std::hypot(target.position.x-from.position.x,target.position.y-from.position.y,target.position.z-from.position.z);
    const float t=d>.015f?.015f/d:1;
    out.position={from.position.x+(target.position.x-from.position.x)*t,from.position.y+(target.position.y-from.position.y)*t,from.position.z+(target.position.z-from.position.z)*t};
    auto q=target.orientation;const auto a=from.orientation;float dot=a.x*q.x+a.y*q.y+a.z*q.z+a.w*q.w;
    if(dot<0){q={-q.x,-q.y,-q.z,-q.w};dot=-dot;}
    const float angle=2*std::acos(std::clamp(dot,0.f,1.f));const float u=angle>.06f?.06f/angle:1;
    q={a.x+(q.x-a.x)*u,a.y+(q.y-a.y)*u,a.z+(q.z-a.z)*u,a.w+(q.w-a.w)*u};
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);out.orientation={q.x/n,q.y/n,q.z/n,q.w/n};return out;
}
}
std::optional<math::Pose> PumpProbeController(const Bc2PumpRawContact& raw,const interaction::InputFrame& input,
    const math::Matrix4& desired)noexcept {
    if(!raw.valid||!raw.mappingValid||raw.input.sequence!=input.generation||raw.nativeOwner.space!=input.spaceGeneration||
       !ValidInput(input)||!StaticReference(input)||!Rigid(raw.trackingBodyWorldMeters)||!Rigid(raw.rawWristWorldMeters)||!Rigid(desired))return {};
    const auto grip=Controller(input.hands[0].grip),inverseBody=InverseRigid(raw.trackingBodyWorldMeters);
    if(!grip||!inverseBody)return {};
    const auto nativeLocal=Multiply(raw.rawWristWorldMeters,*inverseBody);
    // Verify the same zero-translation wrist attachment used by TrackedRig.
    // The rotation is derived from this original renderer packet only.
    if(Distance(nativeLocal,*grip)>.002f)return {};
    auto attachment=Multiply(nativeLocal,*InverseRigid(*grip));attachment.values[3]={0,0,0,1};
    if(!Rigid(attachment))return {};
    const auto wanted=Multiply(Multiply(*InverseRigid(attachment),desired),*inverseBody);
    const auto result=Pose(wanted);if(!result||std::hypot(result->position.x,result->position.y,result->position.z)>1.25f)return {};
    return result;
}
void Bc2PumpControllerProbe::Move(Phase p,std::int64_t now)noexcept {
    phase_=p;phaseAt_=now;alignedAt_=0;
    if(rowCount_<rows_.size())rows_[rowCount_++]={p,failure_,now};
}
void Bc2PumpControllerProbe::Fail(unsigned why,std::int64_t now)noexcept {
    if(Failed()||Completed())return;failure_=why;Move(Phase::Failed,now);
}
bool Bc2PumpControllerProbe::FollowTrackingRebase(const InputFrame& input,const ReloadStateOwner& owner,
    const Bc2NativeCycleView& native,std::int64_t now)noexcept {
    if(followedRebase_||phase_!=Phase::InterruptLeft||!Interrupts()||!armed_||!held_||!last_||
       !interruptionObserved_||!lostStarted_||!native.trackingRebase||completed_>=cycles_.size()||
       !ValidInput(input)||!input.focused||!input.headValid||!input.hands[0].gripTracked||!input.hands[1].gripTracked||
       input.spaceGeneration!=owner.space||input.generation<=last_->generation||now<lastNow_||
       native.phase!=Bc2NativeCyclePhase::Converging||!native.blocksFire||native.loaded!=loaded_||native.reserve!=reserve_||native.capacity!=capacity_)return false;
    const auto& r=*native.trackingRebase;auto expected=cycles_[completed_].native;expected.owner=owner;
    if(r.previous!=owner_||r.current!=owner||r.previousHand!=held_->owner||r.item!=held_->item||r.mechanism!=held_->mechanism||
       r.cycle!=held_->cycle||r.shot!=held_->shot||r.observedNs>now||native.native!=expected||
       native.cycle!=held_->cycle||native.shot!=held_->shot)return false;
    if(ReceiverGap()&&(!receiverReturnNeutral_||receiverReturnNeutral_->space!=owner.space||
       receiverReturnNeutral_->sequence>input.generation||receiverReturnNeutral_->accepted>now))return false;
    followedRebase_=r;owner_=owner;cycles_[completed_].native=native.native;
    // Keep the original held lease as historical evidence only. Fresh authority
    // and the new gun claim must be observed from the actual physical consumer.
    physical_={};recovery_.reset();history_={};last_.reset();lastRaw_=0;
    command_=input.hands[0].grip;lostEnded_=now;
    neutralSequence_=ReceiverGap()?receiverReturnNeutral_->sequence:input.generation;
    neutralAt_=ReceiverGap()?receiverReturnNeutral_->accepted:now;
    Move(Phase::RecoveryNeutral,now);return true;
}
bool Bc2PumpControllerProbe::ObserveReceiverReturnNeutral(const InputFrame& input,const ReloadStateOwner& owner,
    std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    if(receiverReturnNeutral_)return true; // Keep the first original source facts.
    auto expected=owner_;if(expected.space==UINT64_MAX)return false;++expected.space;
    if(!AwaitingReceiver()||!interruptionObserved_||!lostStarted_||!last_||!held_||owner!=expected||
       input.spaceGeneration!=owner.space||input.generation<=last_->generation||
       !ValidInput(input)||!input.focused||!input.headValid||now<lastNow_||observed<=lostStarted_||
       !weapon_cycle_detail::Window(observed,deadline,now)||deadline-observed>100000000)return false;
    for(const auto& hand:input.hands)if(!hand.gripTracked||!hand.aimTracked||hand.squeeze>.35f||
        hand.trigger||hand.held||hand.stickX||hand.stickY)return false;
    receiverReturnNeutral_=ReturnNeutral{input.generation,input.spaceGeneration,observed,deadline,now};return true;
}
bool Bc2PumpControllerProbe::ObserveMissingReceiver(std::int64_t now)noexcept {
    if(!AwaitingReceiver()||!held_||!receiverGapWitness_||now<lastNow_)return false;
    if(!lostStarted_){lostStarted_=now;lostFirst_=last_?last_->generation:0;}
    ++receiverMissingSamples_;return true; // No input, clock or lease is synthesized.
}
std::optional<PumpReceiverGapWitness> Bc2PumpControllerProbe::ReceiverGapReady(std::int64_t now)const noexcept {
    if(!AwaitingReceiver()||lostStarted_||!receiverGapWitness_||!ValidPumpReceiverGapWitness(*receiverGapWitness_,now))return {};
    return receiverGapWitness_;
}
void Bc2PumpControllerProbe::ObserveMissingReceiverNative(const std::optional<Bc2NativeCycleView>& native,bool handsEmpty,std::int64_t now)noexcept {
    if(!AwaitingReceiver()||!lostStarted_||!receiverMissingSamples_||!held_||!native)return;
    if(native->failure!=Bc2NativeCycleFailure::None||native->phase==Bc2NativeCyclePhase::Cancelled||
       native->native!=cycles_[completed_].native||native->cycle!=held_->cycle||native->shot!=held_->shot||
       native->loaded!=loaded_||native->reserve!=reserve_||native->capacity!=capacity_||!native->blocksFire){Fail(12,now);return;}
    if(native->phase==Bc2NativeCyclePhase::Converging&&handsEmpty){
        interruptionObserved_=true;++receiverConvergingSamples_;
        if(!receiverConvergingFirst_)receiverConvergingFirst_=now;receiverConvergingLast_=now;
    }
}
void Bc2PumpControllerProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,
    const Bc2PumpRawContact& raw,const std::optional<Bc2NativeCycleView>& native,
    std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    ++inputs_;if(raw.valid)++rawPackets_;if(!native)++viewMisses_;
    lastRawValid_=raw.valid;lastMapping_=raw.mappingValid;lastOwnerMatch_=raw.nativeOwner==owner;
    lastAssetMatch_=asset==calibration_.asset;lastRig_=raw.rigFingerprint;lastRawSeen_=raw.input.sequence;
    lastNativePhase_=native?unsigned(native->phase):UINT32_MAX;
    if(!first_){first_=phaseAt_=now;command_=in.hands[0].grip;}
    if((requested_!=1&&requested_!=2&&requested_!=3&&requested_!=8)||
       (unsigned(scenario_)>unsigned(Scenario::HeldSupportNaturalReceiverGap))||
       (scenario_!=Scenario::None&&scenario_!=Scenario::HeldSupportNatural&&!ReceiverGap()&&requested_!=1))Fail(10,now);
    if(now<lastNow_||now-first_>=(requested_==8?58000000000ll:28000000000ll))Fail(1,now);lastNow_=now;
    if(!PumpCalibrationValid(calibration_)||!ValidInput(in)||!StaticReference(in)||!in.focused||!in.headValid||
       !in.hands[0].gripTracked||!in.hands[1].gripTracked||!in.hands[1].aimTracked||
       observed<=0||observed>now||deadline<=now||deadline-observed>100000000||
       (armed_&&(owner!=owner_||asset!=calibration_.asset)))Fail(2,now);
    if(last_&&in.generation<=last_->generation){
        if(in.generation<last_->generation)Fail(3,now);
        in.hands[0].grip=last_->hands[0].grip;in.hands[0].aim=last_->hands[0].aim;
        in.hands[0].squeeze=Failed()||Completed()?0:last_->hands[0].squeeze;
        if(Interrupts()){in.hands[0].gripTracked=last_->hands[0].gripTracked;
            in.hands[0].aimTracked=last_->hands[0].aimTracked;}
        in.hands[1].trigger=Failed()||Completed()?0:last_->hands[1].trigger;return;
    }
    if(armed_&&native&&native->phase==Bc2NativeCyclePhase::Cancelled)Fail(4,now);
    const Source* source=nullptr;
    if(raw.valid&&raw.mappingValid&&raw.nativeOwner==owner&&raw.rigFingerprint==calibration_.rigFingerprint&&
       raw.input.owner.space==owner.space&&weapon_cycle_detail::Window(raw.input.observedNs,raw.input.deadlineNs,now))
        for(const auto& h:history_)if(h&&h->owner==owner&&h->frame.generation==raw.input.sequence&&
            h->observed==raw.input.observedNs&&h->deadline==raw.input.deadlineNs){source=&*h;break;}
    const bool newRaw=source&&raw.input.sequence>lastRaw_;
    if(source)++rawMatches_;
    if(!armed_&&!Failed()&&asset==calibration_.asset&&newRaw&&native&&!native->blocksFire&&
       native->phase==Bc2NativeCyclePhase::Watching&&now-first_>=3000000000&&
       !in.hands[0].held&&!in.hands[1].held&&!in.hands[0].stickX&&!in.hands[0].stickY&&!in.hands[1].stickX&&!in.hands[1].stickY){
        armed_=true;owner_=owner;preShotCycle_=native->cycle;preShotShot_=native->shot;Move(Phase::Approach,now);
    }
    if(armed_&&(in.hands[0].held||in.hands[1].held||in.hands[0].stickX||in.hands[0].stickY||in.hands[1].stickX||in.hands[1].stickY))Fail(5,now);
    if(!Failed()&&!Completed()&&armed_){
        if(phase_==Phase::SupportBeforeFire&&preShotLossPending_&&newRaw&&native){
            if(preShotRegrips_||completed_||held_||native->blocksFire||
               native->phase!=Bc2NativeCyclePhase::Watching||native->native.owner!=owner_||
               native->cycle!=preShotCycle_||native->shot!=preShotShot_){Fail(17,now);}
            else {
                ++preShotRegrips_;preShotLossPending_=false;naturalSupportAt_=0;supportCorrection_=0;
                // Approach publishes neutral squeeze and solves against fresh
                // geometry. Only a new real support claim can permit Fire.
                Move(Phase::Approach,now);
            }
        }
        if(phase_==Phase::Fire&&now-phaseAt_>=100000000)Move(Phase::WaitHeld,now);
        if(phase_==Phase::WaitHeld&&newRaw&&native&&native->held){
            const auto& h=*native->held;
            if(native->phase!=Bc2NativeCyclePhase::Held||native->native.owner!=owner||h.owner!=raw.input.owner||
               !weapon_cycle_detail::Lease(h,now)||native->loaded<0||native->capacity!=8||native->reserve<0||
               (!completed_&&requested_==8&&native->loaded!=7)||
               (completed_&&(native->cycle!=(cycles_[completed_-1].recovered?cycles_[completed_-1].recovered->release.cycle.cycle:cycles_[completed_-1].ready.release.cycle.cycle)+1||
                 native->shot!=(cycles_[completed_-1].recovered?cycles_[completed_-1].recovered->release.cycle.shot:cycles_[completed_-1].ready.release.cycle.shot)+1||native->loaded!=loaded_-1||native->reserve!=reserve_)))Fail(6,now);
            else {held_=h;loaded_=native->loaded;reserve_=native->reserve;capacity_=native->capacity;
                cycles_[completed_].native=native->native;baselinePairs_=packs_.pairs;rearPair_=false;Move(Phase::Grip,now);}
        }
        if(phase_==Phase::Grip&&physical_.ownsHand&&physical_.tracking.target&&held_&&
           weapon_cycle_detail::Same(physical_.tracking.target->lease,*held_)){
            if(HeldSupportStart()){
                const auto& expectedGun=ReceiverGap()&&receiverReturnedGun_.id?receiverReturnedGun_:initialGun_;
                if(!initialSupport_.id||!physical_.tracking.mechanism||!physical_.tracking.gun||
                   physical_.tracking.mechanism->token.kind!=HandClaimKind::Mechanism||
                   physical_.tracking.mechanism->token.id==initialSupport_.id||physical_.tracking.gun->token!=expectedGun||
                   physical_.tracking.mechanism->token.prerequisiteClaim!=expectedGun.id){Fail(15,now);}
                else supportAdopted_=true;
            }
            if(!Failed())Move(Phase::Rear,now);
        }
        if(phase_==Phase::RearDwell&&(NaturalStroke()||now-phaseAt_>=180000000)&&rearPair_)Move(Phase::Forward,now);
        if(phase_==Phase::InterruptLeft&&!ReceiverGap()){
            if(!lostStarted_){lostStarted_=now;lostFirst_=in.generation;}
            if(now-lostStarted_>=500000000){
                if(!interruptionObserved_){Fail(12,now);}
                else {lostEnded_=now;neutralSequence_=in.generation;neutralAt_=now;Move(Phase::RecoveryNeutral,now);}
            }else lostLast_=in.generation;
        }
        if(phase_==Phase::RecoveryGrip&&physical_.ownsHand&&physical_.tracking.recoveryTarget&&recovery_&&
           weapon_cycle_detail::Same(physical_.tracking.recoveryTarget->lease,*recovery_))Move(Phase::RecoveryRear,now);
        if(phase_==Phase::RecoveryRearDwell&&(NaturalStroke()||now-phaseAt_>=180000000)&&rearPair_)Move(Phase::RecoveryForward,now);
        if(phase_==Phase::Forward&&native&&native->phase==Bc2NativeCyclePhase::Releasing)Move(Phase::WaitReady,now);
        if(phase_==Phase::Settle&&completed_&&cycles_[completed_-1].supportReturned&&now-phaseAt_>=400000000&&native&&!native->blocksFire&&native->phase==Bc2NativeCyclePhase::Complete){
            if(completed_==requested_)Move(Phase::Done,now);else {resumedPause_=false;recovery_.reset();Move(Phase::Fire,now);}
        }
        float travel=0;
        // Headset274 repeatedly pulled about20cm for the9.5cm virtual stroke.
        // Exercise that overshoot plus off-axis motion, instead of an ideal
        // straight11cm path. This changes only the synthetic controller driver.
        const float back=NaturalStroke()?.22f:calibration_.geometry.stroke,duration=NaturalStroke()?280000000.f:900000000.f;
        if(phase_==Phase::Rear||phase_==Phase::RecoveryRear)travel=back*std::clamp(float(now-phaseAt_)/duration,0.f,1.f);
        if(phase_==Phase::RearDwell||phase_==Phase::RecoveryRearDwell)travel=back;
        if(phase_==Phase::Forward||phase_==Phase::RecoveryForward)travel=back*(1-std::clamp(float(now-phaseAt_)/duration,0.f,1.f));
        const bool solve=phase_==Phase::Approach||phase_==Phase::Fire||phase_==Phase::WaitHeld||phase_==Phase::Grip||
            phase_==Phase::Rear||phase_==Phase::RearDwell||phase_==Phase::Forward||phase_==Phase::WaitReady||phase_==Phase::Settle||
            phase_==Phase::SupportBeforeFire||phase_==Phase::RecoveryNeutral||phase_==Phase::RecoveryGrip||phase_==Phase::RecoveryRear||
            phase_==Phase::RecoveryRearDwell||phase_==Phase::RecoveryForward||phase_==Phase::RecoveryWait;
        // Waiting for native completion is not controller movement. Following
        // the rendered gun here creates positive feedback through two-hand aim:
        // the simulated hand follows the gun which is itself following the hand.
        // Keep the player's last commanded pose; completion still needs the
        // ordinary physical/native/support evidence below.
        const bool stationary=phase_==Phase::WaitReady||phase_==Phase::Settle||phase_==Phase::RecoveryWait||
            (HeldSupportStart()&&(phase_==Phase::Fire||phase_==Phase::WaitHeld))||
            (NaturalStroke()&&phase_==Phase::SupportBeforeFire&&naturalSupportAt_);
        if(solve&&newRaw&&!stationary){
            auto local=calibration_.closedWrist;for(unsigned n=0;n<3;++n)local.values[3][n]+=calibration_.geometry.axis[n]*calibration_.rearDirection*travel;
            if(NaturalStroke())local.values[3][0]+=.08f*std::sin(3.141592653589793f*travel/back);
            const auto desired=Multiply(local,raw.weaponWorldMeters);const auto wanted=PumpProbeController(raw,source->frame,desired);
            if(!wanted)Fail(7,now);
            else {
                command_=Step(command_,*wanted);lastRaw_=raw.input.sequence;++poses_;
                if(Distance(raw.rawWristWorldMeters,desired)<.002f&&Angle(raw.rawWristWorldMeters,desired)<.04f){if(!alignedAt_)alignedAt_=now;}
                else alignedAt_=0;
                const bool settled=alignedAt_&&now-alignedAt_>=100000000;
                if(phase_==Phase::Approach&&settled&&source->frame.hands[0].squeeze<=.35f&&native&&!native->blocksFire)Move(HeldSupportStart()?Phase::SupportBeforeFire:Phase::Fire,now);
                const bool rearReached=NaturalStroke()?Distance(raw.rawWristWorldMeters,desired)<.004f:settled;
                if((phase_==Phase::Rear||phase_==Phase::RecoveryRear)&&travel==back&&rearReached){
                    rearPairs_=packs_.pairs;Move(phase_==Phase::Rear?Phase::RearDwell:Phase::RecoveryRearDwell,now);}
                if(phase_==Phase::RecoveryNeutral&&settled&&source->frame.hands[0].gripTracked&&
                   source->frame.hands[0].squeeze<=.35f&&source->frame.generation>=neutralSequence_&&
                   physical_.tracking.recovery&&native&&held_){
                    const auto& authority=*physical_.tracking.recovery;const auto& debt=authority.grant.debt;
                    unsigned failed=0;
                    if(!weapon_cycle_detail::Lease(authority,now))failed|=1;
                    if(native->phase!=Bc2NativeCyclePhase::Converging||!native->blocksFire||
                       native->native!=cycles_[completed_].native||native->loaded!=loaded_||native->reserve!=reserve_)failed|=2;
                    if(authority.owner!=(followedRebase_?followedRebase_->currentHand:held_->owner)||
                       authority.item!=held_->item||authority.mechanism!=held_->mechanism)failed|=4;
                    if(authority.cycle!=held_->cycle||authority.shot!=held_->shot||
                       debt.loaded!=loaded_||debt.reserve!=reserve_||debt.capacity!=capacity_)failed|=8;
                    if(authority.grant.neutral.sequence<neutralSequence_||
                       (ReceiverGap()&&(!receiverReturnNeutral_||authority.grant.neutral.owner.space!=receiverReturnNeutral_->space||
                        authority.grant.neutral.observedNs<receiverReturnNeutral_->observed)))failed|=16;
                    if(!physical_.tracking.gun)failed|=32;
                    else {
                        if(physical_.tracking.gun->token.owner!=authority.owner||physical_.tracking.gun->token.item!=authority.item)failed|=64;
                        if(!resumedPause_&&(followedRebase_?physical_.tracking.gun->token.id==partialGun_:
                           physical_.tracking.gun->token.id!=partialGun_))failed|=128;
                    }
                    if(failed){
                        recoveryAuthorityFailure_={failed,authority.grant.nonce,authority.grant.neutral.sequence,
                            physical_.tracking.gun?physical_.tracking.gun->token.id:0,authority.grant.neutral.observedNs,
                            authority.grant.neutral.deadlineNs,authority.observedNs,authority.deadlineNs,now};Fail(13,now);
                    }
                    else {recovery_=authority;recoveryGun_=physical_.tracking.gun->token.id;rearPair_=false;baselinePairs_=packs_.pairs;Move(Phase::RecoveryGrip,now);}
                }
            }
        }
        if(now-phaseAt_>(AwaitingReceiver()?10000000000ll:5000000000ll))Fail(8,now);
    }
    const bool grip=!Failed()&&!Completed()&&(AwaitingReceiver()||(HeldSupportStart()&&(phase_==Phase::SupportBeforeFire||phase_==Phase::Fire||phase_==Phase::WaitHeld))||phase_==Phase::Grip||phase_==Phase::Rear||phase_==Phase::RearDwell||phase_==Phase::Forward||phase_==Phase::WaitReady||phase_==Phase::Settle||phase_==Phase::RecoveryGrip||
        phase_==Phase::RecoveryRear||phase_==Phase::RecoveryRearDwell||phase_==Phase::RecoveryForward||phase_==Phase::RecoveryWait);
    in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=grip?1.f:0.f;
    if(NaturalStroke()&&naturalSupportAt_&&!Failed()&&!Completed()){
        // Keep the support controller still while turning the raw gun hand.
        // This creates a real nonzero support correction before the shot,
        // exposing a lost aiming transform at support -> pump handoff.
        const float angle=.35f*std::clamp(float(now-naturalSupportAt_)/200000000.f,0.f,1.f);
        const float sn=std::sin(angle*.5f),cs=std::cos(angle*.5f);
        for(auto pose:{&in.hands[1].grip,&in.hands[1].aim}){const auto q=pose->orientation;
            pose->orientation={cs*q.x+sn*q.z,cs*q.y+sn*q.w,cs*q.z-sn*q.x,cs*q.w-sn*q.y};}
    }
    in.hands[1].trigger=!Failed()&&phase_==Phase::Fire?1.f:0.f;
    if(!Failed()&&phase_==Phase::InterruptLeft&&!ReceiverGap())in.hands[0].gripTracked=in.hands[0].aimTracked=false;
    last_=in;history_[next_]=Source{in,owner,observed,deadline};next_=(next_+1)%history_.size();
}
void Bc2PumpControllerProbe::Observe(const Bc2PhysicalPumpResult& physical,const std::optional<Bc2NativeCycleView>& native,
    Bc2PumpPackCounters packs,std::int64_t now)noexcept {
    physical_=physical;packs_=packs;
    if(Failed()||Completed())return;
    // Respond like a player to an observed native input-pause recovery: let
    // go, approach the actual closed fore-end, then perform a NEW full stroke.
    // This never resets a consumer or creates authority, and is reported
    // separately from an uninterrupted pass. Bound this fallback to one.
    if(!Interrupts()&&!resumedPause_&&held_&&last_&&native&&
       (phase_==Phase::Grip||phase_==Phase::Rear||phase_==Phase::RearDwell||phase_==Phase::Forward)&&
       native->phase==Bc2NativeCyclePhase::Converging&&native->heldSuspensions&&
       native->failure==Bc2NativeCycleFailure::None&&native->blocksFire&&!native->ready&&
       native->native==cycles_[completed_].native&&native->cycle==held_->cycle&&native->shot==held_->shot&&
       native->loaded==loaded_&&native->reserve==reserve_&&native->capacity==capacity_){
        if(pauseRegrips_||last_->generation==UINT64_MAX){Fail(18,now);return;}
        resumedPause_=true;++pauseRegrips_;partialGun_=initialGun_.id;
        neutralSequence_=last_->generation+1;neutralAt_=now;rearPair_=false;
        Move(Phase::RecoveryNeutral,now);
    }
    if(Interrupts()&&phase_==Phase::Rear&&physical.tracking.target&&held_&&native&&
       physical.ownsHand&&native->phase==Bc2NativeCyclePhase::Held&&native->held&&
       weapon_cycle_detail::Lease(*native->held,now)&&weapon_cycle_detail::Same(*native->held,*held_)&&
       native->native==cycles_[completed_].native&&native->loaded==loaded_&&native->reserve==reserve_&&
       weapon_cycle_detail::Same(physical.tracking.target->lease,*held_)&&
       physical.tracking.target->travel>=.03f&&physical.tracking.target->travel<=.055f&&
       PumpTargetFresh(physical.tracking,now)&&physical.tracking.gun&&packs.pairs>baselinePairs_){
        partialTravel_=physical.tracking.target->travel;partialSource_=physical.tracking.target->inputSequence;
        partialAt_=now;partialGun_=physical.tracking.gun->token.id;interruptedPairs_=packs.pairs-baselinePairs_;
        Move(Phase::InterruptLeft,now);
    }
    if(AwaitingReceiver()&&!lostStarted_&&held_&&native&&native->phase==Bc2NativeCyclePhase::Held&&
       native->held&&physical.tracking.target&&physical.ownsHand&&PumpTargetFresh(physical.tracking,now)&&
       weapon_cycle_detail::Same(physical.tracking.target->lease,*held_)&&native->cycle==held_->cycle&&native->shot==held_->shot&&
       native->loaded==loaded_&&native->reserve==reserve_){
        const auto& source=physical.tracking.input;
        PumpReceiverGapWitness witness{owner_.space,held_->cycle,held_->shot,source.sequence,source.observedNs,source.deadlineNs,
            loaded_,reserve_,capacity_,physical.tracking.target->travel};
        if(ValidPumpReceiverGapWitness(witness,now))receiverGapWitness_=witness;
    }
    if(AwaitingReceiver()&&lostStarted_&&receiverMissingSamples_&&native&&held_&&
       native->phase==Bc2NativeCyclePhase::Converging&&native->failure==Bc2NativeCycleFailure::None&&native->blocksFire&&
       native->native==cycles_[completed_].native&&native->cycle==held_->cycle&&native->shot==held_->shot&&
       native->loaded==loaded_&&native->reserve==reserve_&&!physical.ownsHand&&physical.blocksFire)interruptionObserved_=true;
    if(phase_==Phase::InterruptLeft&&lostStarted_&&last_&&!last_->hands[0].gripTracked&&!last_->hands[0].aimTracked&&
       last_->hands[0].squeeze==0&&native&&native->phase==Bc2NativeCyclePhase::Converging&&native->blocksFire&&
       native->native==cycles_[completed_].native&&native->loaded==loaded_&&native->reserve==reserve_&&!physical.ownsHand&&physical.blocksFire)
        interruptionObserved_=true;
    if(phase_==Phase::RecoveryRearDwell&&physical.tracking.recoveryTarget&&recovery_&&
       weapon_cycle_detail::Same(physical.tracking.recoveryTarget->lease,*recovery_)&&
       physical.tracking.recoveryTarget->travel>=calibration_.geometry.stroke-.006f&&packs.pairs>rearPairs_)rearPair_=true;
    if(phase_==Phase::RecoveryForward&&!physical.ownsHand&&!physical.tracking.recoveryTarget&&physical.blocksFire&&rearPair_)
        Move(Phase::RecoveryWait,now);
    if(physical.recovered){
        const auto& ready=*physical.recovered;
        if((!Interrupts()&&!resumedPause_)||!held_||!recovery_||completed_>=cycles_.size()||
           !weapon_cycle_detail::Same(ready.release.cycle,*recovery_)||!ready.nativeReady||!ready.unchangedAmmunition||
           ready.sequence<=ready.release.cycle.sequence||ready.observedNs<ready.release.observedNs||ready.observedNs>now||
           !native||native->phase!=Bc2NativeCyclePhase::Complete||native->blocksFire||physical.blocksFire||
           native->native!=cycles_[completed_].native||native->loaded!=loaded_||native->reserve!=reserve_||
           !rearPair_||packs.pairs<=baselinePairs_||(!resumedPause_&&(!interruptionObserved_||(!followedRebase_&&lostEnded_-lostStarted_<500000000)))){Fail(14,now);return;}
        auto& c=cycles_[completed_++];c.recovered=ready;c.loaded=loaded_;c.reserve=reserve_;c.capacity=capacity_;
        c.pairs=packs.pairs-baselinePairs_;c.rearPair=rearPair_;held_.reset();Move(Phase::Settle,now);
    }
    if(phase_==Phase::RearDwell&&physical.tracking.target&&
       physical.tracking.target->travel>=calibration_.geometry.stroke-.006f&&packs.pairs>rearPairs_)rearPair_=true;
    if(physical.settled){
        const auto& ready=*physical.settled;
        if(Interrupts()||!held_||completed_>=cycles_.size()||!weapon_cycle_detail::Same(ready.release.cycle,*held_)||
           !ready.nativeReady||!ready.unchangedAmmunition||ready.sequence<=ready.release.cycle.sequence||
           ready.observedNs<ready.release.observedNs||ready.observedNs>now||
           !native||native->phase!=Bc2NativeCyclePhase::Complete||native->blocksFire||physical.blocksFire||
           native->loaded!=loaded_||native->reserve!=reserve_||!rearPair_||packs.pairs<=baselinePairs_){Fail(9,now);return;}
        auto& c=cycles_[completed_++];c.ready=ready;c.loaded=loaded_;c.reserve=reserve_;c.capacity=capacity_;
        c.pairs=packs.pairs-baselinePairs_;c.rearPair=rearPair_;held_.reset();Move(Phase::Settle,now);
    }
}
void Bc2PumpControllerProbe::ObserveHeldSupport(const SupportGripResult& supported,const HandInteractionSample& input,
    const std::optional<HandClaim>& left,const std::optional<HandClaim>& gun,std::int64_t now)noexcept {
    if(phase_!=Phase::SupportBeforeFire||!HeldSupportStart())return;
    if(!supported.holding){
        if(naturalSupportAt_&&!preShotLossPending_){
            preShotLossPending_=true;preShotLossSequence_=input.sequence;preShotLossNs_=now;
        }
        return;
    }
    if(preShotLossPending_)return;
    if(!left||!gun){Fail(15,now);return;}
    if(!supported.token||supported.input.generation!=input.sequence||supported.input.hands[0].squeeze<=.35f||
       !input.focused||!input.tracked[0]||!input.tracked[1]||input.released[0]||input.released[1]||
       !weapon_cycle_detail::Window(input.observedNs,input.deadlineNs,now)||
       left->token.kind!=HandClaimKind::WeaponSupport||left->token.hand!=InteractionHand::Left||
       gun->token.kind!=HandClaimKind::GunHold||gun->token.hand!=InteractionHand::Right||
       left->token.owner!=input.owner||gun->token.owner!=input.owner||left->token.item!=gun->token.item||
       left->token.item.id!=owner_.weapon||left->token.prerequisiteClaim!=gun->token.id||
       !left->inputSequence||left->inputSequence>input.sequence||left->deadlineNs<=now||
       gun->inputSequence!=input.sequence||gun->deadlineNs<=now){Fail(15,now);return;}
    if(NaturalStroke()){
        if(!naturalSupportAt_)naturalSupportAt_=now;
        if(now-naturalSupportAt_<350000000)return;
        supportCorrection_=supported.correctionRadians;
        if(supportCorrection_<.15f){Fail(16,now);return;}
    }
    initialSupport_=left->token;initialGun_=gun->token;supportFireSource_=input.sequence;supportFireAt_=now;Move(Phase::Fire,now);
}
void Bc2PumpControllerProbe::ObserveSupport(const Bc2PumpTracking& tracking,const SupportGripResult& supported,std::int64_t now)noexcept {
    if(Failed()||Completed()||phase_!=Phase::Settle||!completed_||!tracking.supportCapture)return;
    auto& cycle=cycles_[completed_-1];if(cycle.supportReturned)return;
    const auto& capture=*tracking.supportCapture;
    // Observe the actual completed-policy claim from Gameplay after its exact
    // AcquireFrom and ordinary support ownership renewal. This method cannot
    // create a claim, change squeeze, acknowledge Ready, or publish a palette.
    const auto valid=[&](const auto& ready){return supported.holding&&supported.token&&supported.token==capture.token&&PumpTrackingFresh(tracking,now)&&
       tracking.mechanism&&tracking.gun&&tracking.mechanism->token==capture.support&&tracking.gun->token==capture.gun&&
       capture.support.kind==HandClaimKind::WeaponSupport&&capture.support.hand==InteractionHand::Left&&
       capture.gun.kind==HandClaimKind::GunHold&&capture.gun.hand==InteractionHand::Right&&
       capture.support.owner==ready.release.cycle.owner&&capture.support.item==ready.release.cycle.item&&
       capture.support.prerequisiteClaim==capture.gun.id&&!tracking.input.released[0]&&!tracking.input.released[1]&&
       supported.input.hands[0].squeeze>.35f&&supported.input.generation==tracking.input.sequence&&
       tracking.mechanism->inputSequence>ready.release.inputSequence&&tracking.mechanism->inputSequence<=tracking.input.sequence&&
       tracking.mechanism->deadlineNs>now&&tracking.mechanism->deadlineNs<=tracking.input.deadlineNs&&now>=ready.observedNs&&
       (!cycle.recovered||capture.gun.id==recoveryGun_);};
    if(!(cycle.recovered?valid(*cycle.recovered):valid(cycle.ready))){Fail(11,now);return;}

    // A subsequent shot uses the genuinely returned support/gun claims.
    // Preserve the original support/fire evidence for the report.
    if(ReceiverGap()&&cycle.recovered)receiverReturnedGun_=capture.gun;
    cycle.supportReturned=true;cycle.supportToken=supported.token;cycle.supportClaim=capture.support.id;
    cycle.supportSource=tracking.mechanism->inputSequence;cycle.supportObserved=now;cycle.supportDeadline=tracking.mechanism->deadlineNs;
}
void Bc2PumpControllerProbe::Report(std::ostream& out)const {
    constexpr std::array names{"warmup","approach","fire","wait_held","grip","rear","rear_dwell","forward","wait_ready","settle","done","failed","interrupt_left","recovery_neutral","recovery_grip","recovery_rear","recovery_rear_dwell","recovery_forward","recovery_wait","support_before_fire"};
    constexpr std::array failures{"none","clock_or_budget","input_or_owner","sequence","native_cancelled","unexpected_action",
        "held_identity_or_counts","controller_mapping","phase_timeout","completion_or_pairs","requested_count","support_return_evidence","interruption_not_observed","recovery_authority","recovery_completion","initial_support_evidence","initial_support_correction","pre_shot_regrip_rejected","repeated_native_pause"};
    out<<"{\"enabled\":true,\"requires_held_support_return\":true,\"requested_cycles\":"<<requested_<<",\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"completed_cycles\":"<<completed_
       <<",\"phase_name\":\""<<names[unsigned(phase_)]<<"\",\"failure_name\":\""<<failures[failure_]<<'"'
       <<",\"input_calls\":"<<inputs_<<",\"raw_present\":"<<rawPackets_<<",\"original_raw_matches\":"<<rawMatches_
       <<",\"generated_controller_poses\":"<<poses_<<",\"native_view_misses\":"<<viewMisses_
       <<",\"last_raw_valid\":"<<(lastRawValid_?"true":"false")<<",\"last_mapping_valid\":"<<(lastMapping_?"true":"false")
       <<",\"last_owner_match\":"<<(lastOwnerMatch_?"true":"false")<<",\"last_asset_match\":"<<(lastAssetMatch_?"true":"false")
       <<",\"last_rig_fingerprint\":"<<lastRig_<<",\"last_raw_sequence\":"<<lastRawSeen_<<",\"last_native_phase\":"<<lastNativePhase_
       <<",\"initial_support_claim\":"<<initialSupport_.id<<",\"initial_gun_claim\":"<<initialGun_.id
       <<",\"initial_support_source\":"<<supportFireSource_<<",\"initial_support_ns\":"<<supportFireAt_
       <<",\"support_adopted_without_release\":"<<(supportAdopted_?"true":"false")
       <<",\"scenario\":"<<unsigned(scenario_)<<",\"support_correction_rad\":"<<supportCorrection_
       <<",\"stroke_schedule_revision\":3,\"stationary_native_waits\":true,\"commanded_rear_m\":"<<(NaturalStroke()?.22f:calibration_.geometry.stroke)
       <<",\"commanded_lateral_m\":"<<(NaturalStroke()?.08f:0.f)
       <<",\"pre_shot_regrips\":"<<preShotRegrips_<<",\"pre_shot_loss_sequence\":"<<preShotLossSequence_<<",\"pre_shot_loss_ns\":"<<preShotLossNs_
       <<",\"receiver_gap_scenario\":"<<(ReceiverGap()?"true":"false")<<",\"receiver_missing_samples\":"<<receiverMissingSamples_
       <<",\"receiver_converging_samples\":"<<receiverConvergingSamples_<<",\"receiver_converging_first_ns\":"<<receiverConvergingFirst_
       <<",\"receiver_converging_last_ns\":"<<receiverConvergingLast_
       <<",\"receiver_return_neutral\":{\"sequence\":"<<(receiverReturnNeutral_?receiverReturnNeutral_->sequence:0)
       <<",\"space\":"<<(receiverReturnNeutral_?receiverReturnNeutral_->space:0)
       <<",\"observed_ns\":"<<(receiverReturnNeutral_?receiverReturnNeutral_->observed:0)
       <<",\"deadline_ns\":"<<(receiverReturnNeutral_?receiverReturnNeutral_->deadline:0)
       <<",\"accepted_ns\":"<<(receiverReturnNeutral_?receiverReturnNeutral_->accepted:0)<<'}'
       <<",\"recovery_authority_failure\":{\"predicates\":"<<recoveryAuthorityFailure_.predicates
       <<",\"grant\":"<<recoveryAuthorityFailure_.grant<<",\"neutral_sequence\":"<<recoveryAuthorityFailure_.neutral
       <<",\"neutral_observed_ns\":"<<recoveryAuthorityFailure_.neutralObserved
       <<",\"neutral_deadline_ns\":"<<recoveryAuthorityFailure_.neutralDeadline
       <<",\"lease_observed_ns\":"<<recoveryAuthorityFailure_.leaseObserved
       <<",\"lease_deadline_ns\":"<<recoveryAuthorityFailure_.leaseDeadline
       <<",\"gun_claim\":"<<recoveryAuthorityFailure_.gun<<",\"now_ns\":"<<recoveryAuthorityFailure_.now<<'}'
       <<",\"native_pause_regrips\":"<<pauseRegrips_
       <<",\"partial_travel_m\":"<<partialTravel_<<",\"partial_source_sequence\":"<<partialSource_
       <<",\"partial_observed_ns\":"<<partialAt_<<",\"partial_gun_claim\":"<<partialGun_<<",\"partial_pairs\":"<<interruptedPairs_
       <<",\"lost_started_ns\":"<<lostStarted_<<",\"lost_ended_ns\":"<<lostEnded_<<",\"lost_first_sequence\":"<<lostFirst_
       <<",\"lost_last_sequence\":"<<lostLast_<<",\"neutral_sequence\":"<<neutralSequence_<<",\"neutral_ns\":"<<neutralAt_
       <<",\"interruption_observed\":"<<(interruptionObserved_?"true":"false")
       <<",\"followed_tracking_rebase\":"<<(followedRebase_?"true":"false")<<",\"recovery_gun_claim\":"<<recoveryGun_
       <<",\"completed\":"<<(Completed()?"true":"false")<<",\"headset_verified\":false,\"cycles\":[";
    for(unsigned n=0;n<completed_;++n){if(n)out<<',';const auto& c=cycles_[n];const auto emit=[&](const auto& r){
        out<<"{\"cycle\":"<<r.release.cycle.cycle<<",\"shot\":"<<r.release.cycle.shot<<",\"request\":"<<r.release.request
           <<",\"source_sequence\":"<<r.release.inputSequence<<",\"requested_ns\":"<<r.release.observedNs
           <<",\"ready_sequence\":"<<r.sequence<<",\"ready_ns\":"<<r.observedNs<<",\"ready_deadline_ns\":"<<r.deadlineNs
           <<",\"loaded_after_shot\":"<<c.loaded<<",\"reserve\":"<<c.reserve<<",\"capacity\":"<<c.capacity
           <<",\"support_returned\":"<<(c.supportReturned?"true":"false")<<",\"support_token\":"<<c.supportToken
           <<",\"support_claim\":"<<c.supportClaim<<",\"support_source_sequence\":"<<c.supportSource
           <<",\"support_observed_ns\":"<<c.supportObserved<<",\"support_deadline_ns\":"<<c.supportDeadline
           <<",\"verified_pairs\":"<<c.pairs<<",\"rear_pair\":"<<(c.rearPair?"true":"false")<<",\"firing\":["
           <<c.native.firing[0]<<','<<c.native.firing[1]<<','<<c.native.firing[2]<<"]";};
        if(c.recovered){emit(*c.recovered);const auto& a=c.recovered->release.cycle;
            out<<",\"recovery\":true,\"recovery_grant\":"<<a.grant.nonce<<",\"guard_epoch\":"<<a.grant.debt.guardEpoch
               <<",\"grant_neutral_sequence\":"<<a.grant.neutral.sequence
               <<",\"grant_neutral_observed_ns\":"<<a.grant.neutral.observedNs
               <<",\"grant_neutral_deadline_ns\":"<<a.grant.neutral.deadlineNs;}
        else {emit(c.ready);out<<",\"recovery\":false";}out<<'}';
    }
    out<<"],\"transitions\":[";for(unsigned n=0;n<rowCount_;++n){if(n)out<<',';const auto& r=rows_[n];
        out<<"{\"phase\":"<<unsigned(r.phase)<<",\"failure\":"<<r.failure<<",\"now_ns\":"<<r.now<<'}';}out<<"]}";
}
}
