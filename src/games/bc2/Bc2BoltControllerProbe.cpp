#include "Bc2BoltControllerProbe.h"
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
std::optional<std::array<math::Matrix4,2>> BoltCurrentWrists(const Bc2BoltControllerContact& raw,
    const interaction::InputFrame& input,const math::Matrix4& body)noexcept {
    if(!raw.raw.valid||!raw.mappingValid||!ValidInput(input)||!StaticReference(input)||!Rigid(body)||
       input.spaceGeneration!=raw.raw.nativeOwner.space||input.generation<raw.raw.input.sequence)return {};
    std::array<math::Matrix4,2> result;
    for(unsigned n=0;n<2;++n){
        const auto grip=Controller(input.hands[n].grip);
        if(!grip||!Rigid(raw.wristToGrip[n])||Distance(raw.wristToGrip[n],Identity())>.0001f)return {};
        result[n]=Multiply(Multiply(raw.wristToGrip[n],*grip),body);
    }return result;
}
std::optional<math::Pose> BoltProbeController(const Bc2BoltControllerContact& raw,const interaction::InputFrame& source,
    InteractionHand hand,const math::Matrix4& desired)noexcept {
    if((hand!=InteractionHand::Left&&hand!=InteractionHand::Right)||!raw.mappingValid||!raw.raw.valid||
       source.generation!=raw.raw.input.sequence||!Rigid(desired)||!Rigid(raw.weaponWorldMeters))return {};
    const auto original=BoltCurrentWrists(raw,source,raw.bodyWorldMeters);const auto index=weapon_cycle_detail::HandIndex(hand);
    if(!original||Distance((*original)[index],raw.rawWristWorldMeters[index])>.002f||Angle((*original)[index],raw.rawWristWorldMeters[index])>.005f)return {};
    const auto inverseBody=InverseRigid(raw.bodyWorldMeters),inverseAttachment=InverseRigid(raw.wristToGrip[index]);
    if(!inverseBody||!inverseAttachment)return {};
    const auto wanted=Multiply(Multiply(*inverseAttachment,desired),*inverseBody);const auto pose=Pose(wanted);
    if(!pose||std::hypot(pose->position.x,pose->position.y,pose->position.z)>1.25f)return {};return pose;
}
void Bc2BoltControllerProbe::Move(Phase next,std::int64_t now)noexcept {
    phase_=next;phaseAt_=now;alignedAt_=0;
    if(rowCount_<rows_.size())rows_[rowCount_++]={next,failure_,now};else ++eventDrops_;
}
void Bc2BoltControllerProbe::Fail(unsigned failure,std::int64_t now)noexcept {
    if(Failed()||Completed())return;failure_=failure;Move(Phase::Failed,now);
}
void Bc2BoltControllerProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,
    const Bc2BoltControllerContact& raw,const std::optional<Bc2NativeCycleView>& native,const std::optional<Bc2AmmoReserveLease>& reserve,
    std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    ++inputs_;if(!first_){first_=phaseAt_=now;for(unsigned n=0;n<2;++n)command_[n]=in.hands[n].grip;}
    if(now<=0||now<lastNow_||now-first_>=25000000000ll)Fail(1,now);lastNow_=now;
    if(requested_<1||requested_>2||!BoltCalibrationValid(calibration_)||!ValidInput(in)||!StaticReference(in)||
       !in.focused||!in.headValid||!in.hands[0].gripTracked||!in.hands[1].gripTracked||!in.hands[1].aimTracked||
       observed<=0||observed>now||deadline<=now||deadline-observed>100000000||
       (armed_&&(owner!=owner_||asset!="M95_sp")))Fail(2,now);
    if(last_&&in.generation<=last_->generation){
        if(in.generation<last_->generation)Fail(3,now);
        for(unsigned n=0;n<2;++n){in.hands[n].grip=last_->hands[n].grip;in.hands[n].aim=last_->hands[n].aim;
            in.hands[n].squeeze=Failed()||Completed()?0:last_->hands[n].squeeze;}
        in.hands[1].trigger=!Failed()&&phase_==Phase::Fire&&now-phaseAt_<100000000&&(!InputOnly()||last_->hands[1].trigger>=.75f)?1.f:0.f;return;
    }
    for(const auto& hand:in.hands)if(hand.held||hand.stickX||hand.stickY)Fail(4,now);
    if(armed_&&native&&native->phase==Bc2NativeCyclePhase::Cancelled)Fail(5,now);
    if(InputOnly()&&!startup_.Ready()&&!Failed()){
        const bool drawn=startup_.Prepare(in,owner,body_,now);
        if(startup_.Failed())Fail(12,now);
        for(unsigned n=0;n<2;++n)command_[n]=in.hands[n].grip;
        if(!drawn){last_=in;return;}
    }
    const Source* original=nullptr;
    if(raw.mappingValid&&raw.raw.valid&&raw.raw.nativeOwner==owner&&raw.raw.rigFingerprint==calibration_.rigFingerprint&&
       weapon_cycle_detail::Window(raw.raw.input.observedNs,raw.raw.input.deadlineNs,now))
        for(const auto& row:history_)if(row&&row->owner==owner&&row->input.generation==raw.raw.input.sequence&&
           row->observed==raw.raw.input.observedNs&&row->deadline==raw.raw.input.deadlineNs){original=&*row;break;}
    const bool freshRaw=original&&raw.raw.input.sequence>lastRaw_;if(original)++rawMatches_;
    const bool ammo=reserve&&reserve->verified&&reserve->identity.owner==owner&&reserve->sequence&&reserve->observedNs>0&&
        reserve->observedNs<=now&&reserve->deadlineNs>now&&reserve->deadlineNs-reserve->observedNs<=250000000&&
        reserve->allThreeIdle&&reserve->capacity==5&&reserve->loaded>=int(requested_)+1&&reserve->loaded<=5&&reserve->reserve>=0;
    if(!armed_&&!Failed()&&phase_==Phase::Warmup&&asset=="M95_sp"&&freshRaw&&ammo&&native&&
       !native->blocksFire&&native->phase==Bc2NativeCyclePhase::Watching&&now-first_>=2000000000ll&&
       (!InputOnly()||(referencesReady_&&selected_&&native->selectedMode==Bc2NativeCycleMode::M95&&native->selectedOwner==owner))){
        armed_=true;owner_=owner;loaded_=reserve->loaded;reserve_=reserve->reserve;originalGunWristInWeapon_=raw.nativeWristInWeapon[1];originalGripReferences_=raw.nativeWristInWeapon;
        Move(Phase::SupportApproach,now);
    }
    if(armed_&&!Failed()&&!Completed()){
        // A gap truncates an already-issued trigger. It cannot restart it.
        if(phase_==Phase::Fire&&(now-phaseAt_>=100000000||!original||!native))Move(Phase::WaitHeld,now);
        if(phase_==Phase::WaitHeld&&native&&native->held&&freshRaw){
            if(native->native.owner!=owner||native->phase!=Bc2NativeCyclePhase::Held||native->loaded!=loaded_-1||
               native->reserve!=reserve_||native->capacity!=5||!weapon_cycle_detail::Lease(*native->held,now))Fail(6,now);
            else{held_=native->held;loaded_=native->loaded;baselinePairs_=packs_.pairs;Move(Phase::EnterCustody,now);}
        }
        if(phase_==Phase::ReturnNeutral&&now-phaseAt_>=150000000)Move(Phase::ApproachGun,now);
        if(phase_==Phase::Settle&&now-phaseAt_>=300000000&&native&&!native->blocksFire&&support_){
            if(completed_==requested_)Move(Phase::Done,now);else Move(Phase::Fire,now);
        }
        float travel=0,rotation=0;const auto& p=calibration_.profile;
        if(phase_==Phase::Unlock)rotation=p.unlockRadians*std::clamp(float(now-phaseAt_)/700000000.f,0.f,1.f);
        if(phase_==Phase::Rear||phase_==Phase::Forward)rotation=p.unlockRadians;
        if(phase_==Phase::Rear)travel=p.stroke*std::clamp(float(now-phaseAt_)/900000000.f,0.f,1.f);
        if(phase_==Phase::Forward)travel=p.stroke*(1-std::clamp(float(now-phaseAt_)/900000000.f,0.f,1.f));
        if(phase_==Phase::Lock)rotation=p.unlockRadians*(1-std::clamp(float(now-phaseAt_)/700000000.f,0.f,1.f));
        if(freshRaw){
            if(phase_==Phase::SupportApproach||phase_==Phase::SupportGrip){
                const auto desired=Multiply(raw.nativeWristInWeapon[0],raw.weaponWorldMeters);
                const auto wanted=BoltProbeController(raw,original->input,InteractionHand::Left,desired);
                if(!wanted)Fail(7,now);else{command_[0]=Step(command_[0],*wanted);++poses_;
                    if(Distance(raw.rawWristWorldMeters[0],desired)<.004f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
                    if(phase_==Phase::SupportApproach&&alignedAt_&&now-alignedAt_>=100000000)Move(Phase::SupportGrip,now);}
            }
            if(phase_>=Phase::ApproachBolt&&phase_<=Phase::WaitReady){
                const bool returning=phase_==Phase::ApproachGun||phase_==Phase::ReturnGrip||phase_==Phase::WaitReady;
                const auto local=returning?originalGunWristInWeapon_:Multiply(calibration_.wristFromPart,weapon_cycle_detail::Target(p,travel,rotation));
                const auto desired=Multiply(local,raw.weaponWorldMeters);
                const auto wanted=BoltProbeController(raw,original->input,InteractionHand::Right,desired);
                if(!wanted)Fail(8,now);else{command_[1]=Step(command_[1],*wanted);++poses_;
                    if(Distance(raw.rawWristWorldMeters[1],desired)<.003f&&Angle(raw.rawWristWorldMeters[1],desired)<.025f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
                    if(alignedAt_&&now-alignedAt_>=100000000){
                        if(phase_==Phase::ApproachBolt)Move(Phase::Grip,now);
                        if(phase_==Phase::ApproachGun)Move(Phase::ReturnGrip,now);
                    }
                }
            }
            lastRaw_=raw.raw.input.sequence;
        }
        if(now-phaseAt_>=5000000000ll)Fail(9,now);
    }
    const bool active=!Failed()&&!Completed();
    const bool rightGrip=phase_<(InputOnly()?Phase::EnterCustody:Phase::ApproachBolt)||(phase_>=Phase::Grip&&phase_<=Phase::Lock)||phase_>=Phase::ReturnGrip;
    for(unsigned n=0;n<2;++n){in.hands[n].grip=command_[n];in.hands[n].aim=command_[n];in.hands[n].trigger=0;
        in.hands[n].squeeze=active&&(n==0?phase_>=Phase::SupportGrip:rightGrip)?1.f:0.f;}
    in.hands[1].trigger=active&&phase_==Phase::Fire?1.f:0.f;
    if(!last_||last_->hands[0].squeeze!=in.hands[0].squeeze||last_->hands[1].squeeze!=in.hands[1].squeeze||last_->hands[1].trigger!=in.hands[1].trigger){
        if(edgeCount_<edges_.size())edges_[edgeCount_++]={in.generation,observed,deadline,phase_,in.hands[0].squeeze>=.7f,in.hands[1].squeeze>=.7f,in.hands[1].trigger>=.7f};
        else ++edgeDrops_;
    }
    last_=in;history_[next_]=Source{in,owner,observed,deadline};next_=(next_+1)%history_.size();
}
void Bc2BoltControllerProbe::ObserveSupport(bool holding,std::int64_t now)noexcept {
    support_=holding;if(phase_==Phase::SupportGrip&&holding)Move(Phase::Fire,now);
}
void Bc2BoltControllerProbe::Observe(const Bc2PhysicalBoltResult& physical,const std::optional<Bc2NativeCycleView>& native,
    Bc2BoltPackCounters packs,std::int64_t now)noexcept {
    physical_=physical;packs_=packs;if(Failed()||Completed())return;
    const auto& t=physical.tracking;
    if(physical.custodyChange&&physical.custodyChange->transaction.accepted){
        if(custodyReceiptCount_<custodyReceipts_.size())custodyReceipts_[custodyReceiptCount_++]={t.input,t.custody,*physical.custodyChange};else ++custodyDrops_;}
    if(phase_==Phase::EnterCustody&&physical.custodyChange&&physical.custodyChange->transaction.accepted&&t.custody==BoltCustodyPhase::Manipulating)Move(Phase::ApproachBolt,now);
    if(phase_==Phase::Grip&&physical.ownsMechanism&&t.target)Move(Phase::Unlock,now);
    if(phase_==Phase::Unlock&&t.mechanismPhase==WeaponCyclePhase::Rear)Move(Phase::Rear,now);
    if(phase_==Phase::Rear&&t.mechanismPhase==WeaponCyclePhase::Forward)Move(Phase::Forward,now);
    if(phase_==Phase::Forward&&t.mechanismPhase==WeaponCyclePhase::Lock)Move(Phase::Lock,now);
    if(phase_==Phase::Lock&&t.mechanismPhase==WeaponCyclePhase::AwaitingNative)Move(Phase::ReturnNeutral,now);
    if(phase_==Phase::ReturnGrip&&physical.custodyChange&&physical.custodyChange->transaction.accepted&&t.custody==BoltCustodyPhase::Returned)Move(Phase::WaitReady,now);
    if(physical.settled){
        if(!held_||!weapon_cycle_detail::Same(physical.settled->release.cycle,*held_)||!native||
           native->native.owner!=owner_||native->loaded!=loaded_||native->reserve!=reserve_){Fail(10,now);return;}
        ready_=physical.settled;
    }
    if(phase_==Phase::WaitReady&&ready_&&t.custody==BoltCustodyPhase::Returned&&!physical.blocksFire&&native&&
       native->phase==Bc2NativeCyclePhase::Complete&&!native->blocksFire){
        if(completed_>=receipts_.size()||packs.pairs<=baselinePairs_){Fail(11,now);return;}
        receipts_[completed_++]={*ready_,native->native,loaded_,reserve_,packs.pairs-baselinePairs_,true};ready_.reset();held_.reset();Move(Phase::Settle,now);
    }
}
void Bc2BoltControllerProbe::Report(std::ostream& out)const {
    out<<"{\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"requested\":"<<requested_<<",\"completed\":"<<completed_
       <<",\"input_only\":"<<(InputOnly()?"true":"false")<<",\"references_ready\":"<<(referencesReady_?"true":"false")
       <<",\"selected_mode_ready\":"<<(selected_?"true":"false")<<",\"input_edge_drops\":"<<edgeDrops_
       <<",\"inputs\":"<<inputs_<<",\"raw_matches\":"<<rawMatches_<<",\"controller_poses\":"<<poses_
       <<",\"profile\":"<<calibration_.profile.id<<",\"revision\":"<<calibration_.profile.revision<<",\"rig_fingerprint\":"<<calibration_.rigFingerprint
       <<",\"custody_drops\":"<<custodyDrops_<<",\"event_drops\":"<<eventDrops_
       <<",\"native_animation_handle_written\":false,\"native_ammo_written\":false,\"cycles\":[";
    for(unsigned n=0;n<completed_;++n){if(n)out<<',';const auto& r=receipts_[n];out<<"{\"cycle\":"<<r.ready.release.cycle.cycle
        <<",\"shot\":"<<r.ready.release.cycle.shot<<",\"request\":"<<r.ready.release.request<<",\"loaded\":"<<r.loaded<<",\"reserve\":"<<r.reserve
        <<",\"release_ns\":"<<r.ready.release.observedNs<<",\"ready_ns\":"<<r.ready.observedNs<<",\"pairs\":"<<r.pairs<<",\"custody_returned\":"<<(r.custodyReturned?"true":"false");
        const auto& l=r.ready.release.cycle;const auto& o=r.native.owner;
        out<<",\"release_input_sequence\":"<<r.ready.release.inputSequence<<",\"release_deadline_ns\":"<<r.ready.release.deadlineNs
           <<",\"ready_sequence\":"<<r.ready.sequence<<",\"ready_deadline_ns\":"<<r.ready.deadlineNs
           <<",\"native_ready\":"<<(r.ready.nativeReady?"true":"false")<<",\"unchanged_ammunition\":"<<(r.ready.unchangedAmmunition?"true":"false")
           <<",\"held\":{\"sequence\":"<<l.sequence<<",\"observed_ns\":"<<l.observedNs<<",\"deadline_ns\":"<<l.deadlineNs
           <<",\"actor\":"<<l.owner.actor<<",\"actor_generation\":"<<l.owner.actorGeneration<<",\"equipment_generation\":"<<l.owner.equipGeneration
           <<",\"space\":"<<l.owner.space<<",\"item\":"<<l.item.id<<",\"item_generation\":"<<l.item.generation
           <<",\"mechanism\":"<<l.mechanism.id<<",\"mechanism_generation\":"<<l.mechanism.generation<<'}'
           <<",\"native\":{\"player\":"<<o.player<<",\"soldier\":"<<o.soldier<<",\"weak\":"<<o.weak<<",\"weapon\":"<<o.weapon
           <<",\"actor_generation\":"<<o.actorGeneration<<",\"equipment_generation\":"<<o.equipGeneration<<",\"space\":"<<o.space
           <<",\"firing\":["<<r.native.firing[0]<<','<<r.native.firing[1]<<','<<r.native.firing[2]<<']'
           <<",\"server_player\":"<<r.native.serverPlayer<<",\"server_soldier\":"<<r.native.serverSoldier<<",\"server_item\":"<<r.native.serverItem<<"}}";}
    out<<"],\"custody_events\":[";
    const auto claim=[&](const std::optional<HandClaim>& c){if(!c){out<<"null";return;}const auto& t=c->token;
        out<<"{\"id\":"<<t.id<<",\"hand\":"<<unsigned(t.hand)<<",\"kind\":"<<unsigned(t.kind)<<",\"parent\":"<<t.prerequisiteClaim
           <<",\"item\":"<<t.item.id<<",\"item_generation\":"<<t.item.generation<<",\"contact\":"<<t.contact.id
           <<",\"contact_generation\":"<<t.contact.generation<<",\"source_sequence\":"<<c->inputSequence<<",\"deadline_ns\":"<<c->deadlineNs<<'}';};
    for(unsigned n=0;n<custodyReceiptCount_;++n){if(n)out<<',';const auto& c=custodyReceipts_[n];
        out<<"{\"phase\":"<<unsigned(c.phase)<<",\"input_sequence\":"<<c.input.sequence<<",\"observed_ns\":"<<c.input.observedNs
           <<",\"deadline_ns\":"<<c.input.deadlineNs<<",\"now_ns\":"<<c.input.nowNs<<",\"gun\":";claim(c.result.transaction.claim);
        out<<",\"companion\":";claim(c.result.companion);out<<",\"released_ids\":[";
        for(unsigned h=0;h<2;++h){if(h)out<<',';out<<(c.result.transaction.released[h]?c.result.transaction.released[h]->token.id:0);}out<<"]}";}
    out<<"],\"startup_draw\":";startup_.Report(out);
    out<<",\"input_edges\":[";for(unsigned n=0;n<edgeCount_;++n){if(n)out<<',';const auto& e=edges_[n];
        out<<"{\"sequence\":"<<e.sequence<<",\"observed_ns\":"<<e.observed<<",\"deadline_ns\":"<<e.deadline
           <<",\"phase\":"<<unsigned(e.phase)<<",\"left_grip\":"<<(e.left?"true":"false")
           <<",\"right_grip\":"<<(e.right?"true":"false")<<",\"trigger\":"<<(e.fire?"true":"false")<<'}';}
    out<<"],\"events\":[";for(unsigned n=0;n<rowCount_;++n){if(n)out<<',';out<<"{\"phase\":"<<unsigned(rows_[n].phase)<<",\"failure\":"<<rows_[n].failure<<",\"now_ns\":"<<rows_[n].now<<'}';}out<<"]}";
}
}
