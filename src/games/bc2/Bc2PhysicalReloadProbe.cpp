#include "Bc2PhysicalReloadProbe.h"
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
std::optional<math::Pose> PhysicalReloadProbeController(const ReloadRawContact& raw,const interaction::InputFrame& input,
    const math::Matrix4& desired)noexcept {
    if(!raw.valid||raw.rigFingerprint!=SpasReloadRig||raw.inputEvidence.sequence!=input.generation||raw.owner.space!=input.spaceGeneration||
        !ValidInput(input)||!StaticReference(input)||!Rigid(raw.trackingBodyWorldMeters)||!Rigid(raw.rawLeftWristWorldMeters)||!Rigid(desired))return {};
    const auto grip=Controller(input.hands[0].grip);if(!grip)return {};
    const auto inverseBody=InverseRigid(raw.trackingBodyWorldMeters);
    const auto nativeLocal=Multiply(raw.rawLeftWristWorldMeters,*inverseBody);
    // Existing TrackedRig has zero translation in wrist attachment. Prove that
    // against the actual publication instead of inferring a calibration offset.
    if(Distance(nativeLocal,*grip)>.002f)return {};
    auto attachment=Multiply(nativeLocal,*InverseRigid(*grip));attachment.values[3]={0,0,0,1};
    if(!Rigid(attachment))return {};
    const auto wanted=Multiply(Multiply(*InverseRigid(attachment),desired),*inverseBody);
    const auto result=Pose(wanted);if(!result||std::hypot(result->position.x,result->position.y,result->position.z)>1.25f)return {};
    return result;
}
void Bc2PhysicalReloadProbe::PhaseTo(Phase phase,std::int64_t now)noexcept {
    phase_=phase;phaseAt_=now;alignedAt_=0;
    if(rowsCount_<rows_.size())rows_[rowsCount_++]={unsigned(phase),failure_,now};
}
void Bc2PhysicalReloadProbe::Fail(unsigned reason,std::int64_t now)noexcept {failure_=reason;PhaseTo(Phase::Failed,now);}
bool Bc2PhysicalReloadProbe::Episode(const BodyAnchorConfig& anchors,std::int64_t warmup)noexcept {
    if(first_||episode_||!ValidBodyAnchors(anchors)||warmup<0||warmup>6000000000ll)return false;
    episode_=true;warmupNs_=warmup;pouchPosition_={anchors.chest.center.x,anchors.chest.center.y,-anchors.chest.center.z};return true;
}
std::optional<PhysicalReloadProbeState> Bc2PhysicalReloadProbe::EpisodeState(const PhysicalReloadProbeState& source)const noexcept {
    auto out=source;if(!episode_)return out;
    if(!baseline_||source.acquired<acquiredBase_||source.submitted<submittedBase_||source.completed<completedBase_)return {};
    out.acquired-=acquiredBase_;out.submitted-=submittedBase_;out.completed-=completedBase_;return out;
}
void Bc2PhysicalReloadProbe::Prepare(interaction::InputFrame& input,const ReloadStateOwner& owner,std::string_view asset,const ReloadRawContact& raw,
    const PhysicalReloadProbeState& source,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    if(!enabled_)return;
    if(CancelConsumer()){input.hands[0].grip=command_;input.hands[0].aim=command_;input.hands[0].squeeze=0;return;}
    if(!first_){first_=phaseAt_=now;owner_=owner;command_.position=pouchPosition_;
        if(episode_){if(source.active||source.held||source.pending||source.submitted!=source.completed){Fail(17,now);return;}
            acquiredBase_=source.acquired;submittedBase_=source.submitted;completedBase_=source.completed;baseline_=true;}}
    const auto episode=EpisodeState(source);if(!episode){Fail(17,now);return;}const auto& state=*episode;
    if(rounds_<1||rounds_>2)Fail(13,now);
    if(now<lastNow_||now-first_>=30000000000ll){Fail(1,now);}lastNow_=now;
    if(owner_!=owner||asset!=SpasReloadAsset||!ValidInput(input)||!input.focused||!input.headValid||!StaticReference(input)||
        !input.hands[0].gripTracked||!input.hands[1].gripTracked||!input.hands[1].aimTracked||observed<=0||observed>now||deadline<=now||deadline-observed>100000000)Fail(2,now);
    if(last_&&input.generation<=last_->generation){
        if(input.generation<last_->generation)Fail(3,now);
        input.hands[0].grip=last_->hands[0].grip;input.hands[0].aim=last_->hands[0].aim;
        input.hands[0].squeeze=CancelConsumer()?0:last_->hands[0].squeeze;return;
    }
    if(state.submitted>rounds_||state.completed>rounds_||state.completed>state.submitted)Fail(4,now);
    if(phase_==Phase::Warmup&&now-first_>=warmupNs_){
        // A trigger command is not proof that native firing consumed a round.
        // Start only from fresh actual counts, independently of prep scheduling.
        preparationReserveFresh_=state.reserve&&state.reserve->verified&&state.reserve->identity.owner==owner&&
            state.reserve->observedNs>0&&state.reserve->observedNs<=now&&state.reserve->deadlineNs>now&&
            state.reserve->deadlineNs-state.reserve->observedNs<=200000000;
        preparationRaw_=raw.valid;
        preparationLoaded_=preparationReserveFresh_?state.reserve->loaded:-1;
        preparationReserve_=preparationReserveFresh_?state.reserve->reserve:-1;
        preparationCapacity_=preparationReserveFresh_?state.reserve->capacity:-1;
        const bool emptySlots=preparationLoaded_>=0&&preparationCapacity_>=preparationLoaded_&&
            preparationCapacity_-preparationLoaded_>=int(rounds_);
        const bool reserveRounds=preparationReserve_>=int(rounds_);
        if(preparationReserveFresh_&&emptySlots&&reserveRounds&&raw.valid){
            loadedBefore_=state.reserve->loaded;reserveBefore_=state.reserve->reserve;PhaseTo(Phase::Grab,now);
            roundsEvidence_[0].grabInput=input.generation;
        }else if(now-first_>=10000000000ll)Fail(!preparationReserveFresh_?5:!emptySlots?15:!reserveRounds?16:5,now);
    }
    if(phase_==Phase::Rearm){
        // A successful receipt leaves the actual native cycle held. Release and
        // return using NEW controller packets; never reuse the first shell's
        // press, claim, contact or receipt to manufacture the next insertion.
        math::Pose pouch;pouch.position=pouchPosition_;command_=Step(command_,pouch);
        const Input* neutral=nullptr;
        for(const auto& h:history_)if(h&&h->owner==owner&&h->frame.generation==raw.inputEvidence.sequence&&
            h->observed==raw.inputEvidence.observedNs&&h->deadline==raw.inputEvidence.deadlineNs&&h->deadline>now&&
            h->frame.hands[0].squeeze<=.35f&&h->observed>=phaseAt_){neutral=&*h;break;}
        const auto actual=neutral&&raw.valid&&raw.owner==owner?(episode_?BodyAnchorHandPose(neutral->frame,InteractionHand::Left):PhysicalReloadPouchPose(neutral->frame)):std::nullopt;
        const auto desired=Controller(pouch);
        const bool atPouch=actual&&desired&&Distance(*actual,*desired)<.02f&&Angle(*actual,*desired)<.05f;
        const bool fresh=state.reserve&&state.reserve->verified&&state.reserve->identity.owner==owner&&
            state.reserve->observedNs>0&&state.reserve->observedNs<=now&&state.reserve->deadlineNs>now&&
            state.reserve->deadlineNs-state.reserve->observedNs<=200000000&&
            state.reserve->loaded==loadedBefore_+int(completed_)&&state.reserve->reserve==reserveBefore_-int(completed_);
        if(atPouch&&!state.held&&!state.pending&&state.active&&state.nativeHolding&&fresh&&
            state.acquired==completed_&&state.submitted==completed_&&state.completed==completed_){
            if(!neutralFirst_){neutralFirst_=neutral->frame.generation;alignedAt_=now;}
            neutralLast_=neutral->frame.generation;
            if(neutralLast_>neutralFirst_&&input.generation>neutralLast_&&now-alignedAt_>=80000000ll){
                auto& row=roundsEvidence_[completed_];row.firstNeutral=neutralFirst_;row.lastNeutral=neutralLast_;row.grabInput=input.generation;
                PhaseTo(Phase::Grab,now);
            }
        }else {neutralFirst_=neutralLast_=0;alignedAt_=0;}
        if(phase_==Phase::Rearm&&now-phaseAt_>4000000000ll)Fail(14,now);
    }
    if(phase_==Phase::Grab&&state.held)PhaseTo(Phase::WaitHold,now);
    if(phase_==Phase::Grab&&now-phaseAt_>1000000000ll)Fail(6,now);
    if(phase_==Phase::WaitHold&&state.nativeHolding)PhaseTo(Phase::Approach,now);
    if(phase_==Phase::WaitHold&&now-phaseAt_>3500000000ll)Fail(7,now);
    if(phase_>=Phase::Approach&&phase_<=Phase::WaitAck){
        const Input* original=nullptr;
        for(const auto& h:history_)if(h&&h->owner==owner&&h->frame.generation==raw.inputEvidence.sequence&&
            h->observed==raw.inputEvidence.observedNs&&h->deadline==raw.inputEvidence.deadlineNs&&h->deadline>now){original=&*h;break;}
        if(original&&raw.valid&&raw.owner==owner&&raw.inputEvidence.sequence>lastRaw_){
            // Begin outside the60mm magnetic capsule, then provide genuine
            // inward raw movement through capture and the entire seated stroke.
            float along=-.09f;if(phase_==Phase::Enter)along=-.02f;
            if(phase_==Phase::Stroke)along=-.02f+.07f*std::clamp(float(now-phaseAt_)/800000000.f,0.f,1.f);
            if(phase_==Phase::WaitAck)along=.05f;
            const auto p=SpasReloadInsertionProfile();const auto rail=TravelPose(p,along);
            const auto desired=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(rail,Multiply(p.weaponFromEntry,raw.weaponWorldMeters))));
            if(const auto controller=PhysicalReloadProbeController(raw,original->frame,desired)){
                command_=Step(command_,*controller);lastRaw_=raw.inputEvidence.sequence;
                const float position=Distance(raw.rawLeftWristWorldMeters,desired),angle=Angle(raw.rawLeftWristWorldMeters,desired);
                if(position<.003f&&angle<.05f){if(!alignedAt_)alignedAt_=now;
                    if(now-alignedAt_>=80000000ll){if(phase_==Phase::Approach)PhaseTo(Phase::Enter,now);else if(phase_==Phase::Enter)PhaseTo(Phase::Stroke,now);}}
                else alignedAt_=0;
                if(rowsCount_<rows_.size()&&now-rowAt_>=100000000){rowAt_=now;rows_[rowsCount_++]={unsigned(phase_),0,now,input.generation,raw.inputEvidence.sequence,position,angle,along,
                    state.reserve?state.reserve->loaded:-1,state.reserve?state.reserve->reserve:-1};}
            }else Fail(8,now);
        }
        if(state.submitted==completed_+1&&phase_!=Phase::WaitAck)PhaseTo(Phase::WaitAck,now);
        if((phase_==Phase::Approach||phase_==Phase::Enter)&&now-phaseAt_>4000000000ll)Fail(9,now);
        if(phase_==Phase::Stroke&&now-phaseAt_>2500000000ll)Fail(10,now);
        if(phase_==Phase::WaitAck&&now-phaseAt_>1800000000ll)Fail(11,now);
    }
    input.hands[0].grip=command_;input.hands[0].aim=command_;
    input.hands[0].squeeze=phase_>=Phase::Grab&&phase_<=Phase::WaitAck?1.f:0.f;
    last_=input;history_[next_]=Input{input,observed,deadline,owner};next_=(next_+1)%history_.size();
}
void Bc2PhysicalReloadProbe::Observe(const PhysicalReloadProbeState& source,std::int64_t now)noexcept {
    if(!enabled_||CancelConsumer()||(episode_&&!baseline_))return;
    const auto episode=EpisodeState(source);if(!episode){Fail(17,now);return;}const auto& s=*episode;submitted_=s.submitted;
    if(s.completed>completed_){
        if(s.completed!=completed_+1||s.completed>rounds_||s.submitted!=s.completed||!s.reserve||!s.reserve->verified||
            s.reserve->identity.owner!=owner_||s.reserve->observedNs>now||s.reserve->deadlineNs<=now||
            s.reserve->loaded!=loadedBefore_+int(s.completed)||s.reserve->reserve!=reserveBefore_-int(s.completed))Fail(12,now);
        else {
            completed_=s.completed;auto& r=roundsEvidence_[completed_-1];r.number=completed_;r.acquired=s.acquired;r.completedNs=now;
            r.loaded=s.reserve->loaded;r.reserve=s.reserve->reserve;
            if(completed_==rounds_)PhaseTo(Phase::Done,now);
            else {neutralFirst_=neutralLast_=0;PhaseTo(Phase::Rearm,now);}
        }
    }else if(s.submitted>rounds_||s.completed<completed_||s.completed>s.submitted)Fail(4,now);
}
void Bc2PhysicalReloadProbe::Report(std::ostream& o)const {
    const auto precision=o.precision();o.precision(std::numeric_limits<float>::max_digits10);
    o<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"synthetic_input\":true,\"headset_verified\":false,\"phase\":"<<unsigned(phase_)
        <<",\"failure\":"<<failure_<<",\"actual_consumer_completed\":"<<(phase_==Phase::Done?"true":"false")
        <<",\"submitted\":"<<submitted_<<",\"completed\":"<<completed_<<",\"requested_rounds\":"<<rounds_
        <<",\"episode\":"<<(episode_?"true":"false")<<",\"counter_baseline\":{\"acquired\":"<<acquiredBase_<<",\"submitted\":"<<submittedBase_<<",\"completed\":"<<completedBase_<<'}'
        <<",\"loaded_before\":"<<loadedBefore_<<",\"reserve_before\":"<<reserveBefore_
        <<",\"preparation\":{\"reserve_fresh\":"<<(preparationReserveFresh_?"true":"false")
        <<",\"raw_contact_valid\":"<<(preparationRaw_?"true":"false")<<",\"loaded\":"<<preparationLoaded_
        <<",\"reserve\":"<<preparationReserve_<<",\"capacity\":"<<preparationCapacity_<<"},\"rounds\":[";
    for(unsigned n=0;n<std::min(rounds_,2u);++n){if(n)o<<',';const auto& r=roundsEvidence_[n];
        o<<"{\"number\":"<<r.number<<",\"acquired\":"<<r.acquired<<",\"completed_ns\":"<<r.completedNs
            <<",\"first_neutral_input\":"<<r.firstNeutral<<",\"last_neutral_input\":"<<r.lastNeutral<<",\"grab_input\":"<<r.grabInput
            <<",\"loaded\":"<<r.loaded<<",\"reserve\":"<<r.reserve<<'}';}o<<"],\"rows\":[";
    for(unsigned n=0;n<rowsCount_;++n){if(n)o<<',';const auto& r=rows_[n];o<<"{\"phase\":"<<r.phase<<",\"reason\":"<<r.reason<<",\"now_ns\":"<<r.now
        <<",\"input\":"<<r.input<<",\"raw\":"<<r.raw<<",\"position_error_m\":"<<r.positionError<<",\"angle_error_rad\":"<<r.angleError
        <<",\"rail_m\":"<<r.rail<<",\"loaded\":"<<r.loaded<<",\"reserve\":"<<r.reserve<<'}';}o<<"]}";o.precision(precision);
}
}
