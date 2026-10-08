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
void Bc2PumpControllerProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,
    const Bc2PumpRawContact& raw,const std::optional<Bc2NativeCycleView>& native,
    std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    ++inputs_;if(raw.valid)++rawPackets_;if(!native)++viewMisses_;
    lastRawValid_=raw.valid;lastMapping_=raw.mappingValid;lastOwnerMatch_=raw.nativeOwner==owner;
    lastAssetMatch_=asset==SpasReloadAsset;lastRig_=raw.rigFingerprint;lastRawSeen_=raw.input.sequence;
    lastNativePhase_=native?unsigned(native->phase):UINT32_MAX;
    if(!first_){first_=phaseAt_=now;command_=in.hands[0].grip;}
    if((requested_!=1&&requested_!=2&&requested_!=8))Fail(10,now);
    if(now<lastNow_||now-first_>=(requested_==8?58000000000ll:28000000000ll))Fail(1,now);lastNow_=now;
    if(!PumpCalibrationValid(calibration_)||!ValidInput(in)||!StaticReference(in)||!in.focused||!in.headValid||
       !in.hands[0].gripTracked||!in.hands[1].gripTracked||!in.hands[1].aimTracked||
       observed<=0||observed>now||deadline<=now||deadline-observed>100000000||
       (armed_&&(owner!=owner_||asset!=SpasReloadAsset)))Fail(2,now);
    if(last_&&in.generation<=last_->generation){
        if(in.generation<last_->generation)Fail(3,now);
        in.hands[0].grip=last_->hands[0].grip;in.hands[0].aim=last_->hands[0].aim;
        in.hands[0].squeeze=Failed()||Completed()?0:last_->hands[0].squeeze;
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
    if(!armed_&&!Failed()&&asset==SpasReloadAsset&&newRaw&&native&&!native->blocksFire&&
       native->phase==Bc2NativeCyclePhase::Watching&&now-first_>=3000000000&&
       !in.hands[0].held&&!in.hands[1].held&&!in.hands[0].stickX&&!in.hands[0].stickY&&!in.hands[1].stickX&&!in.hands[1].stickY){
        armed_=true;owner_=owner;Move(Phase::Approach,now);
    }
    if(armed_&&(in.hands[0].held||in.hands[1].held||in.hands[0].stickX||in.hands[0].stickY||in.hands[1].stickX||in.hands[1].stickY))Fail(5,now);
    if(!Failed()&&!Completed()&&armed_){
        if(phase_==Phase::Fire&&now-phaseAt_>=100000000)Move(Phase::WaitHeld,now);
        if(phase_==Phase::WaitHeld&&newRaw&&native&&native->held){
            const auto& h=*native->held;
            if(native->phase!=Bc2NativeCyclePhase::Held||native->native.owner!=owner||h.owner!=raw.input.owner||
               !weapon_cycle_detail::Lease(h,now)||native->loaded<0||native->capacity!=8||native->reserve<0||
               (!completed_&&requested_==8&&native->loaded!=7)||
               (completed_&&(native->cycle!=cycles_[completed_-1].ready.release.cycle.cycle+1||
                 native->shot!=cycles_[completed_-1].ready.release.cycle.shot+1||native->loaded!=loaded_-1||native->reserve!=reserve_)))Fail(6,now);
            else {held_=h;loaded_=native->loaded;reserve_=native->reserve;capacity_=native->capacity;
                cycles_[completed_].native=native->native;baselinePairs_=packs_.pairs;rearPair_=false;Move(Phase::Grip,now);}
        }
        if(phase_==Phase::Grip&&physical_.ownsHand&&physical_.tracking.target&&held_&&
           weapon_cycle_detail::Same(physical_.tracking.target->lease,*held_))Move(Phase::Rear,now);
        if(phase_==Phase::RearDwell&&now-phaseAt_>=180000000&&rearPair_)Move(Phase::Forward,now);
        if(phase_==Phase::Forward&&native&&native->phase==Bc2NativeCyclePhase::Releasing)Move(Phase::WaitReady,now);
        if(phase_==Phase::Settle&&completed_&&cycles_[completed_-1].supportReturned&&now-phaseAt_>=400000000&&native&&!native->blocksFire&&native->phase==Bc2NativeCyclePhase::Complete){
            if(completed_==requested_)Move(Phase::Done,now);else Move(Phase::Fire,now);
        }
        float travel=0;
        if(phase_==Phase::Rear)travel=SpasObservedForeEndStroke*std::clamp(float(now-phaseAt_)/900000000.f,0.f,1.f);
        if(phase_==Phase::RearDwell)travel=SpasObservedForeEndStroke;
        if(phase_==Phase::Forward)travel=SpasObservedForeEndStroke*(1-std::clamp(float(now-phaseAt_)/900000000.f,0.f,1.f));
        const bool solve=phase_==Phase::Approach||phase_==Phase::Fire||phase_==Phase::WaitHeld||phase_==Phase::Grip||
            phase_==Phase::Rear||phase_==Phase::RearDwell||phase_==Phase::Forward||phase_==Phase::WaitReady||phase_==Phase::Settle;
        if(solve&&newRaw){
            auto local=calibration_.closedWrist;local.values[3][2]+=calibration_.rearDirection*travel;
            const auto desired=Multiply(local,raw.weaponWorldMeters);const auto wanted=PumpProbeController(raw,source->frame,desired);
            if(!wanted)Fail(7,now);
            else {
                command_=Step(command_,*wanted);lastRaw_=raw.input.sequence;++poses_;
                if(Distance(raw.rawWristWorldMeters,desired)<.002f&&Angle(raw.rawWristWorldMeters,desired)<.04f){if(!alignedAt_)alignedAt_=now;}
                else alignedAt_=0;
                const bool settled=alignedAt_&&now-alignedAt_>=100000000;
                if(phase_==Phase::Approach&&settled&&source->frame.hands[0].squeeze<=.35f&&native&&!native->blocksFire)Move(Phase::Fire,now);
                if(phase_==Phase::Rear&&travel==SpasObservedForeEndStroke&&settled){rearPairs_=packs_.pairs;Move(Phase::RearDwell,now);}
            }
        }
        if(now-phaseAt_>5000000000ll)Fail(8,now);
    }
    const bool grip=!Failed()&&!Completed()&&(phase_==Phase::Grip||phase_==Phase::Rear||phase_==Phase::RearDwell||phase_==Phase::Forward||phase_==Phase::WaitReady||phase_==Phase::Settle);
    in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=grip?1.f:0.f;
    in.hands[1].trigger=!Failed()&&phase_==Phase::Fire?1.f:0.f;
    last_=in;history_[next_]=Source{in,owner,observed,deadline};next_=(next_+1)%history_.size();
}
void Bc2PumpControllerProbe::Observe(const Bc2PhysicalPumpResult& physical,const std::optional<Bc2NativeCycleView>& native,
    Bc2PumpPackCounters packs,std::int64_t now)noexcept {
    physical_=physical;packs_=packs;
    if(Failed()||Completed())return;
    if(phase_==Phase::RearDwell&&physical.tracking.target&&
       physical.tracking.target->travel>=SpasObservedForeEndStroke-.006f&&packs.pairs>rearPairs_)rearPair_=true;
    if(physical.settled){
        const auto& ready=*physical.settled;
        if(!held_||completed_>=cycles_.size()||!weapon_cycle_detail::Same(ready.release.cycle,*held_)||
           !ready.nativeReady||!ready.unchangedAmmunition||ready.sequence<=ready.release.cycle.sequence||
           ready.observedNs<ready.release.observedNs||ready.observedNs>now||
           !native||native->phase!=Bc2NativeCyclePhase::Complete||native->blocksFire||physical.blocksFire||
           native->loaded!=loaded_||native->reserve!=reserve_||!rearPair_||packs.pairs<=baselinePairs_){Fail(9,now);return;}
        auto& c=cycles_[completed_++];c.ready=ready;c.loaded=loaded_;c.reserve=reserve_;c.capacity=capacity_;
        c.pairs=packs.pairs-baselinePairs_;c.rearPair=rearPair_;held_.reset();Move(Phase::Settle,now);
    }
}
void Bc2PumpControllerProbe::ObserveSupport(const Bc2PumpTracking& tracking,const SupportGripResult& supported,std::int64_t now)noexcept {
    if(Failed()||Completed()||phase_!=Phase::Settle||!completed_||!tracking.supportCapture)return;
    auto& cycle=cycles_[completed_-1];if(cycle.supportReturned)return;
    const auto& capture=*tracking.supportCapture;
    // Observe the actual completed-policy claim from Gameplay after its exact
    // AcquireFrom and ordinary support ownership renewal. This method cannot
    // create a claim, change squeeze, acknowledge Ready, or publish a palette.
    if(!supported.holding||!supported.token||supported.token!=capture.token||!PumpTrackingFresh(tracking,now)||
       !tracking.mechanism||!tracking.gun||tracking.mechanism->token!=capture.support||tracking.gun->token!=capture.gun||
       capture.support.kind!=HandClaimKind::WeaponSupport||capture.support.hand!=InteractionHand::Left||
       capture.gun.kind!=HandClaimKind::GunHold||capture.gun.hand!=InteractionHand::Right||
       capture.support.owner!=cycle.ready.release.cycle.owner||capture.support.item!=cycle.ready.release.cycle.item||
       capture.support.prerequisiteClaim!=capture.gun.id||tracking.input.released[0]||tracking.input.released[1]||
       supported.input.hands[0].squeeze<=.35f||supported.input.generation!=tracking.input.sequence||
       tracking.mechanism->inputSequence<=cycle.ready.release.inputSequence||tracking.mechanism->inputSequence>tracking.input.sequence||
       tracking.mechanism->deadlineNs<=now||tracking.mechanism->deadlineNs>tracking.input.deadlineNs||
       now<cycle.ready.observedNs){Fail(11,now);return;}
    cycle.supportReturned=true;cycle.supportToken=supported.token;cycle.supportClaim=capture.support.id;
    cycle.supportSource=tracking.mechanism->inputSequence;cycle.supportObserved=now;cycle.supportDeadline=tracking.mechanism->deadlineNs;
}
void Bc2PumpControllerProbe::Report(std::ostream& out)const {
    constexpr std::array names{"warmup","approach","fire","wait_held","grip","rear","rear_dwell","forward","wait_ready","settle","done","failed"};
    constexpr std::array failures{"none","clock_or_budget","input_or_owner","sequence","native_cancelled","unexpected_action",
        "held_identity_or_counts","controller_mapping","phase_timeout","completion_or_pairs","requested_count","support_return_evidence"};
    out<<"{\"enabled\":true,\"requires_held_support_return\":true,\"requested_cycles\":"<<requested_<<",\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"completed_cycles\":"<<completed_
       <<",\"phase_name\":\""<<names[unsigned(phase_)]<<"\",\"failure_name\":\""<<failures[failure_]<<'"'
       <<",\"input_calls\":"<<inputs_<<",\"raw_present\":"<<rawPackets_<<",\"original_raw_matches\":"<<rawMatches_
       <<",\"generated_controller_poses\":"<<poses_<<",\"native_view_misses\":"<<viewMisses_
       <<",\"last_raw_valid\":"<<(lastRawValid_?"true":"false")<<",\"last_mapping_valid\":"<<(lastMapping_?"true":"false")
       <<",\"last_owner_match\":"<<(lastOwnerMatch_?"true":"false")<<",\"last_asset_match\":"<<(lastAssetMatch_?"true":"false")
       <<",\"last_rig_fingerprint\":"<<lastRig_<<",\"last_raw_sequence\":"<<lastRawSeen_<<",\"last_native_phase\":"<<lastNativePhase_
       <<",\"completed\":"<<(Completed()?"true":"false")<<",\"headset_verified\":false,\"cycles\":[";
    for(unsigned n=0;n<completed_;++n){if(n)out<<',';const auto& c=cycles_[n];const auto& r=c.ready;
        out<<"{\"cycle\":"<<r.release.cycle.cycle<<",\"shot\":"<<r.release.cycle.shot<<",\"request\":"<<r.release.request
           <<",\"source_sequence\":"<<r.release.inputSequence<<",\"requested_ns\":"<<r.release.observedNs
           <<",\"ready_sequence\":"<<r.sequence<<",\"ready_ns\":"<<r.observedNs<<",\"ready_deadline_ns\":"<<r.deadlineNs
           <<",\"loaded_after_shot\":"<<c.loaded<<",\"reserve\":"<<c.reserve<<",\"capacity\":"<<c.capacity
           <<",\"support_returned\":"<<(c.supportReturned?"true":"false")<<",\"support_token\":"<<c.supportToken
           <<",\"support_claim\":"<<c.supportClaim<<",\"support_source_sequence\":"<<c.supportSource
           <<",\"support_observed_ns\":"<<c.supportObserved<<",\"support_deadline_ns\":"<<c.supportDeadline
           <<",\"verified_pairs\":"<<c.pairs<<",\"rear_pair\":"<<(c.rearPair?"true":"false")<<",\"firing\":["
           <<c.native.firing[0]<<','<<c.native.firing[1]<<','<<c.native.firing[2]<<"]}";
    }
    out<<"],\"transitions\":[";for(unsigned n=0;n<rowCount_;++n){if(n)out<<',';const auto& r=rows_[n];
        out<<"{\"phase\":"<<unsigned(r.phase)<<",\"failure\":"<<r.failure<<",\"now_ns\":"<<r.now<<'}';}out<<"]}";
}
}
