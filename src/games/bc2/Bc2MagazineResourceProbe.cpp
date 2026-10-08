#include "Bc2MagazineResourceProbe.h"
#include "Bc2PhysicalReloadProbe.h"
#include "fvr/interaction/BodyAnchors.h"
#include <algorithm>
#include <cmath>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
std::optional<math::Matrix4> Controller(const math::Pose& p)noexcept {
    const auto v=math::MakeLhViewFromOpenXRPose(p);return v?InverseRigid(*v):std::nullopt;
}
bool Static(const InputFrame& in)noexcept {
    const auto a=Controller(in.referenceHead),b=Controller(in.head);
    return a&&b&&Distance(*a,Identity())<.0001f&&Angle(*a,Identity())<.001f&&
        Distance(*b,Identity())<.0001f&&Angle(*b,Identity())<.001f;
}
math::Pose Step(const math::Pose& from,const math::Pose& target)noexcept {
    auto out=target;const float d=std::hypot(target.position.x-from.position.x,target.position.y-from.position.y,target.position.z-from.position.z);
    const float t=d>.01f?.01f/d:1;
    out.position={from.position.x+(target.position.x-from.position.x)*t,from.position.y+(target.position.y-from.position.y)*t,
        from.position.z+(target.position.z-from.position.z)*t};
    auto q=target.orientation;const auto a=from.orientation;float dot=a.x*q.x+a.y*q.y+a.z*q.z+a.w*q.w;
    if(dot<0){q={-q.x,-q.y,-q.z,-q.w};dot=-dot;}
    const float angle=2*std::acos(std::clamp(dot,0.f,1.f)),u=angle>.05f?.05f/angle:1;
    q={a.x+(q.x-a.x)*u,a.y+(q.y-a.y)*u,a.z+(q.z-a.z)*u,a.w+(q.w-a.w)*u};
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);out.orientation={q.x/n,q.y/n,q.z/n,q.w/n};return out;
}
}
void Bc2MagazineResourceProbe::Move(Phase p,std::int64_t now)noexcept {
    phase_=p;phaseAt_=now;alignedAt_=0;if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(p),failure_,now};
}
void Bc2MagazineResourceProbe::Fail(unsigned reason,std::int64_t now)noexcept {
    if(CancelConsumer())return;failure_=reason;Move(Phase::Failed,now);
}
void Bc2MagazineResourceProbe::Begin(const AmmoResourceView& v,std::int64_t now)noexcept {
    context_=v.binding.context;before_=v.snapshot.counts;requestBaseline_=v.request;nativeIdentity_=v.identity;
    original_.reset();removal_.reset();seat_.reset();seatRequest_=0;
    removedPair_=hiddenPair_=replacementPair_=attachedPair_=false;
    seatHandReleased_=supportReleased_=discarded_=false;roleStarts_={};baseline_=packs_;
    Move(Phase::ApproachMagazine,now);
}
bool Bc2MagazineResourceProbe::Receipt(const AmmoResourceView& v,const AmmunitionReceipt& r,std::int64_t now)const noexcept {
    const auto& c=r.command;
    return v.request>requestBaseline_&&v.requestState==AmmoResourceRequestState::Completed&&
        v.phase==AmmunitionLedgerPhase::Ready&&v.snapshot.context==context_&&c.context==context_&&c.id&&
        r.authorityVerified&&r.copiesVerified&&r.authorityInvocation&&c.sourceSequence&&c.requestedNs>0&&
        r.beganNs>=c.requestedNs&&r.completedNs>=r.beganNs&&r.completedNs<c.deadlineNs&&
        r.completedNs<=now&&v.snapshot.observedNs>=r.completedNs&&r.before==c.before&&r.after==c.after&&r.after==v.snapshot.counts;
}
void Bc2MagazineResourceProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,
    const MagazineRawContact& raw,const MagazineResourceProbeState& state,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
    if(!enabled_)return;
    if(!first_){first_=phaseAt_=now;command_=in.hands[0].grip;}
    if(now<lastNow_||now-first_>=30000000000ll)Fail(1,now);lastNow_=now;
    // The ordinary consumer already resolved the exact native configuration.
    // Asset-name lookup intentionally rejects same-name configuration variants.
    const auto published=state.result.tracking.family.binding.profile;
    const auto selected=published&&published==FindMagazineEquipment(published->nativeId)&&
        published->Ready()&&published->geometry->asset==asset?published:nullptr;
    if((armed_&&(owner!=owner_||!selected||!selected->Ready()||profile_!=selected))||in.spaceGeneration!=owner.space||!ValidInput(in)||
        !in.focused||!in.headValid||!Static(in)||!in.hands[0].gripTracked||!in.hands[1].gripTracked||!in.hands[1].aimTracked||
        observed<=0||observed>now||deadline<=now||deadline-observed>100000000)Fail(2,now);
    if(lastInput_&&in.generation<=lastInput_->generation){
        if(in.generation<lastInput_->generation)Fail(3,now);
        in.hands[0].grip=lastInput_->hands[0].grip;in.hands[0].aim=lastInput_->hands[0].aim;
        in.hands[0].squeeze=CancelConsumer()?0:lastInput_->hands[0].squeeze;return;
    }
    const bool fresh=state.resource&&state.resource->binding.owner==owner&&AmmoResourceViewFresh(*state.resource,now);
    // Startup can still be in a vehicle or an unsupported slot. Lock no owner
    // until ordinary consumers publish a real enrolled, attached resource.
    if(!armed_&&!CancelConsumer()&&selected&&selected->Ready()&&fresh&&raw.valid&&raw.owner==owner&&
        raw.rigFingerprint==selected->geometry->rigFingerprint&&state.result.tracking.owner==owner&&
        state.result.tracking.family.binding.profile==selected&&state.resource->phase==AmmunitionLedgerPhase::Ready&&
        !state.resource->wellEmpty&&state.result.interaction.phase==DetachableMagazinePhase::Attached&&
        !state.result.ownsLeftHand&&!state.result.blocksWeaponActions){
        armed_=true;armedAt_=now;owner_=owner;profile_=selected;
    }
    if(phase_!=Phase::Warmup&&!CancelConsumer()){
        if(state.result.interaction.phase==DetachableMagazinePhase::Cancelled)Fail(4,now);
        if(!fresh){if(!gapAt_)gapAt_=now;if(now-gapAt_>=100000000)Fail(5,now);}else gapAt_=0;
        if(fresh&&state.resource->binding.context!=context_)Fail(6,now);
    }
    const Input* source=nullptr;
    if(profile_&&raw.valid&&raw.owner==owner&&raw.rigFingerprint==profile_->geometry->rigFingerprint)
        for(const auto& h:history_)if(h&&h->owner==owner&&h->frame.generation==raw.inputEvidence.sequence&&h->observed==raw.inputEvidence.observedNs&&
            h->deadline==raw.inputEvidence.deadlineNs&&h->deadline>now){source=&*h;break;}
    const bool newRaw=source&&raw.inputEvidence.sequence>lastRaw_;
    if(armed_&&phase_==Phase::Warmup&&now-armedAt_>=6000000000ll){
        if(fresh&&newRaw&&state.resource->phase==AmmunitionLedgerPhase::Ready&&!state.resource->wellEmpty&&
            state.result.interaction.phase==DetachableMagazinePhase::Attached&&!state.result.ownsLeftHand&&!state.result.blocksWeaponActions&&
            (originalReturn_||state.resource->snapshot.counts.reserve>0)){
            acquiredBaseline_=state.result.acquired;Begin(*state.resource,now);
        }else if(now-armedAt_>=10000000000ll)Fail(7,now);
    }
    if(profile_&&!CancelConsumer()&&fresh){
        const auto& cfg=profile_->geometry->interaction;const auto& p=cfg.insertion;
        const float outside=-std::max(.02f,p.captureDistanceMeters*.4f);
        float along=p.travelMeters;bool solve=false;
        if(phase_==Phase::ApproachMagazine||phase_==Phase::Grip){solve=true;}
        if(phase_==Phase::Pull){along=p.travelMeters+(outside-p.travelMeters)*std::clamp(float(now-phaseAt_)/900000000.f,0.f,1.f);solve=true;}
        if(phase_==Phase::Release){along=outside;solve=true;}
        if(phase_==Phase::ApproachRail){along=-std::max(.11f,p.captureDistanceMeters+cfg.pullMeters*.4f);solve=true;}
        if(phase_==Phase::Enter){along=outside;solve=true;}
        if(phase_==Phase::Stroke){along=outside+(p.travelMeters-outside)*std::clamp(float(now-phaseAt_)/1200000000.f,0.f,1.f);solve=true;}
        if(solve&&newRaw){
            const auto desired=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,along),Multiply(p.weaponFromEntry,raw.weaponWorldMeters))));
            const auto control=PhysicalReloadProbeController(raw,source->frame,desired);
            if(!control)Fail(8,now);
            else {
                command_=Step(command_,*control);lastRaw_=raw.inputEvidence.sequence;
                if(Distance(raw.rawLeftWristWorldMeters,desired)<.003f&&Angle(raw.rawLeftWristWorldMeters,desired)<.05f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
                const bool settled=alignedAt_&&now-alignedAt_>=100000000;
                const auto phase=state.result.interaction.phase;
                if(phase_==Phase::ApproachMagazine&&settled&&source->frame.hands[0].squeeze<=.35f)Move(Phase::Grip,now);
                else if(phase_==Phase::Grip&&removal_&&phase==DetachableMagazinePhase::Pulling)Move(Phase::Pull,now);
                else if(phase_==Phase::Pull&&phase==DetachableMagazinePhase::RemovedHeld&&settled&&removedPair_)Move(originalReturn_?Phase::ApproachRail:Phase::Release,now);
                else if(phase_==Phase::Release&&phase==DetachableMagazinePhase::WellEmpty&&!state.result.ownsLeftHand&&hiddenPair_&&discarded_&&
                    source->frame.hands[0].squeeze<=.35f&&now-phaseAt_>=400000000)Move(Phase::Pouch,now);
                else if(phase_==Phase::ApproachRail&&settled)Move(Phase::Enter,now);
                else if(phase_==Phase::Enter&&settled&&(originalReturn_?phase==DetachableMagazinePhase::RemovedHeld:
                    phase==DetachableMagazinePhase::Guided&&replacementPair_))Move(Phase::Stroke,now);
            }
        }
        if(phase_==Phase::Pouch||phase_==Phase::GrabReplacement){
            math::Pose pouch;const auto c=chest_?ChestAmmoSupply():Bc2MagazinePhysicalReload::DefaultPouch();
            pouch.position={c.pouchCenterMeters[0],c.pouchCenterMeters[1],-c.pouchCenterMeters[2]};command_=Step(command_,pouch);
            if(phase_==Phase::Pouch&&newRaw&&source->frame.hands[0].squeeze<=.35f){
                const auto actual=chest_?BodyAnchorHandPose(source->frame,InteractionHand::Left):PhysicalReloadPouchPose(source->frame),target=Controller(pouch);
                if(actual&&target&&Distance(*actual,*target)<.01f&&Angle(*actual,*target)<.05f){if(!alignedAt_)alignedAt_=now;
                    if(now-alignedAt_>=100000000)Move(Phase::GrabReplacement,now);}else alignedAt_=0;
            }
            if(phase_==Phase::GrabReplacement&&state.result.acquired==acquiredBaseline_+1&&state.result.ownsLeftHand&&
                state.result.interaction.phase==DetachableMagazinePhase::ReplacementHeld)Move(Phase::ApproachRail,now);
        }
    }
    if(!CancelConsumer()&&phase_!=Phase::Warmup&&now-phaseAt_>4500000000ll)Fail(9,now);
    const bool squeeze=!CancelConsumer()&&(phase_==Phase::Grip||phase_==Phase::Pull||phase_==Phase::GrabReplacement||
        phase_==Phase::ApproachRail||phase_==Phase::Enter||phase_==Phase::Stroke);
    in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=squeeze?1.f:0.f;
    lastInput_=in;history_[next_]=Input{in,owner,observed,deadline};next_=(next_+1)%history_.size();
}
void Bc2MagazineResourceProbe::Observe(const MagazineResourceProbeState& state,const MagazinePackCounters& packs,std::int64_t now)noexcept {
    if(!enabled_||CancelConsumer())return;
    if(packs.copies<packs_.copies||packs.pairs<packs_.pairs||packs.fallbacks<packs_.fallbacks){Fail(10,now);return;}
    for(unsigned n=0;n<4;++n)if(packs.roleCopies[n]<packs_.roleCopies[n]||packs.rolePairs[n]<packs_.rolePairs[n]){Fail(10,now);return;}
    packs_=packs;if(phase_==Phase::Warmup)return;
    if(packs.fallbacks>baseline_.fallbacks){Fail(11,now);return;}
    const auto& out=state.result;
    if(out.interaction.phase==DetachableMagazinePhase::Cancelled){Fail(4,now);return;}
    if(!state.resource||!AmmoResourceViewFresh(*state.resource,now))return;
    const auto& v=*state.resource;
    if(v.binding.owner!=owner_||v.binding.context!=context_){Fail(6,now);return;}
    if(v.request>requestBaseline_&&(v.requestState==AmmoResourceRequestState::Rejected||v.requestState==AmmoResourceRequestState::Uncertain||v.phase==AmmunitionLedgerPhase::NeedsReconciliation)){Fail(12,now);return;}
    if(out.acquired<acquiredBaseline_||out.acquired>acquiredBaseline_+(originalReturn_?0:1)){Fail(13,now);return;}
    const auto pair=[&](unsigned role){if(!roleStarts_[role])roleStarts_[role]=packs.rolePairs[role];
        return packs.rolePairs[role]>*roleStarts_[role]&&packs.rolePairs[role]>baseline_.rolePairs[role];};
    const auto phase=out.interaction.phase;
    if(phase==DetachableMagazinePhase::RemovedHeld&&pair(1))removedPair_=true;
    if(phase==DetachableMagazinePhase::WellEmpty&&pair(3))hiddenPair_=true;
    if((phase==DetachableMagazinePhase::ReplacementHeld||phase==DetachableMagazinePhase::Guided)&&pair(2))replacementPair_=true;
    if(seatRequest_&&(phase==DetachableMagazinePhase::Attached||phase==DetachableMagazinePhase::Complete)&&pair(0))attachedPair_=true;
    if(!removal_&&v.receipt&&v.receipt->command.operation==AmmunitionOperation::RemoveMagazine&&v.request>requestBaseline_){
        const auto& r=*v.receipt;
        if(!Receipt(v,r,now)||!v.original||v.original->id!=r.command.id||v.original->owner!=context_.resource||
            v.original->rounds!=before_.loaded||v.original->capacity!=before_.capacity||r.before!=before_||
            r.after!=AmmunitionCounts{0,before_.reserve,before_.capacity}||!v.wellEmpty||v.originalState!=MagazineResourceState::Held){Fail(14,now);return;}
        original_=v.original;removal_=r;
    }
    if(removal_&&v.wellEmpty&&v.phase==AmmunitionLedgerPhase::Ready){
        if(v.original!=original_||v.snapshot.counts!=AmmunitionCounts{0,before_.reserve,before_.capacity}){Fail(15,now);return;}
        if(v.originalState==MagazineResourceState::Discarded){if(originalReturn_){Fail(15,now);return;}discarded_=true;}
    }
    if(out.tracking.resource&&out.tracking.resource->seatedRequest){
        const auto request=out.tracking.resource->seatedRequest;
        if(seatRequest_&&seatRequest_!=request){Fail(16,now);return;}
        if(!seatRequest_){
            if(!removal_||!removedPair_||(!originalReturn_&&(!discarded_||!hiddenPair_||!replacementPair_))){Fail(16,now);return;}
            seatRequest_=request;seatHandReleased_=!out.ownsLeftHand;supportReleased_=!MagazineBlocksSupport(out,now);
            if(!seatHandReleased_||!supportReleased_){Fail(17,now);return;}
            Move(Phase::WaitReceipt,now);
        }
    }
    if(seatRequest_&&!seat_&&v.request==seatRequest_&&v.requestState==AmmoResourceRequestState::Completed&&v.receipt){
        const auto& r=*v.receipt;const auto operation=originalReturn_?AmmunitionOperation::ReturnMagazine:AmmunitionOperation::RefillMagazine;
        const int refill=std::min(before_.capacity,before_.reserve);
        const AmmunitionCounts after=originalReturn_?before_:AmmunitionCounts{refill,before_.reserve-refill,before_.capacity};
        if(!Receipt(v,r,now)||r.command.id<=removal_->command.id||r.command.requestedNs<removal_->completedNs||
            r.command.operation!=operation||r.before!=AmmunitionCounts{0,before_.reserve,before_.capacity}||r.after!=after||v.wellEmpty||
            (originalReturn_?(r.command.original!=original_||v.originalState!=MagazineResourceState::Returned):(!discarded_||r.command.original))) {Fail(18,now);return;}
        seat_=r;Move(Phase::Settle,now);
    }
    if(phase_==Phase::Settle&&seat_&&phase==DetachableMagazinePhase::Attached&&!out.ownsLeftHand&&!out.blocksWeaponActions&&attachedPair_){
        nativeCycles_[completedCycles_]={nativeIdentity_,*removal_,*seat_};
        cycles_[completedCycles_++]={before_,seat_->after,removal_->command.id,seat_->command.id,originalReturn_,discarded_,seatHandReleased_,supportReleased_};
        if(combined_&&!second_){second_=true;originalReturn_=false;acquiredBaseline_=out.acquired;Begin(v,now);}
        else Move(Phase::Done,now);
    }
}
void Bc2MagazineResourceProbe::Report(std::ostream& out)const {
    out<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"synthetic_input\":true,\"resource_backend\":true,\"headset_verified\":false,\"eye_textures_verified\":false"
       <<",\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"actual_consumer_completed\":"<<(Completed()?"true":"false")
       <<",\"armed\":"<<armed_<<",\"armed_ns\":"<<armedAt_
       <<",\"chest_supply\":"<<(chest_?"true":"false")<<",\"profile\":"<<(profile_?profile_->geometry->interaction.insertion.id:0)
       <<",\"native_remove_receipt\":"<<bool(removal_)<<",\"native_seat_receipt\":"<<bool(seat_)<<",\"discarded\":"<<discarded_
       <<",\"seat_hand_released\":"<<seatHandReleased_<<",\"support_released\":"<<supportReleased_
       <<",\"removed_pair\":"<<removedPair_<<",\"hidden_pair\":"<<hiddenPair_<<",\"replacement_pair\":"<<replacementPair_<<",\"attached_pair\":"<<attachedPair_
       <<",\"copies\":"<<packs_.copies<<",\"pairs\":"<<packs_.pairs<<",\"fallbacks\":"<<packs_.fallbacks<<",\"completed_cycles\":"<<completedCycles_<<",\"cycles\":[";
    for(unsigned n=0;n<completedCycles_;++n){const auto& c=cycles_[n];if(n)out<<',';
        out<<"{\"original_return\":"<<c.originalReturn<<",\"loaded_before\":"<<c.before.loaded<<",\"reserve_before\":"<<c.before.reserve
           <<",\"loaded_after\":"<<c.after.loaded<<",\"reserve_after\":"<<c.after.reserve<<",\"remove_command\":"<<c.remove<<",\"seat_command\":"<<c.seat
           <<",\"discarded\":"<<c.discarded<<",\"hand_released\":"<<c.handReleased<<",\"support_released\":"<<c.supportReleased;
        const auto& native=nativeCycles_[n];const auto& identity=native.identity;
        out<<",\"firing\":["<<identity.firing[0]<<','<<identity.firing[1]<<','<<identity.firing[2]<<']'
           <<",\"server_player\":"<<identity.serverPlayer<<",\"server_soldier\":"<<identity.serverSoldier<<",\"server_item\":"<<identity.serverItem;
        const auto counts=[&](const AmmunitionCounts& a){out<<"{\"loaded\":"<<a.loaded<<",\"reserve\":"<<a.reserve<<",\"capacity\":"<<a.capacity<<'}';};
        const auto receipt=[&](const AmmunitionReceipt& r){const auto& cmd=r.command;const auto& ctx=cmd.context;
            out<<"{\"command\":"<<cmd.id<<",\"operation\":"<<unsigned(cmd.operation)<<",\"authority_invocation\":"<<r.authorityInvocation
               <<",\"authority_verified\":"<<r.authorityVerified<<",\"copies_verified\":"<<r.copiesVerified
               <<",\"source_sequence\":"<<cmd.sourceSequence<<",\"requested_ns\":"<<cmd.requestedNs
               <<",\"admission_deadline_ns\":"<<cmd.admissionDeadlineNs<<",\"deadline_ns\":"<<cmd.deadlineNs
               <<",\"began_ns\":"<<r.beganNs<<",\"completed_ns\":"<<r.completedNs
               <<",\"context\":{\"actor\":"<<ctx.resource.actor<<",\"actor_generation\":"<<ctx.resource.actorGeneration
               <<",\"weapon\":"<<ctx.resource.weapon<<",\"weapon_generation\":"<<ctx.resource.weaponGeneration
               <<",\"equip_generation\":"<<ctx.equipGeneration<<",\"space\":"<<ctx.space<<"},\"before\":";counts(r.before);
            out<<",\"after\":";counts(r.after);out<<'}';};
        out<<",\"remove_native\":";receipt(native.remove);out<<",\"seat_native\":";receipt(native.seat);out<<'}';}
    out<<"],\"rows\":[";for(unsigned n=0;n<rowCount_;++n){const auto& r=rows_[n];if(n)out<<',';
        out<<"{\"phase\":"<<r.phase<<",\"failure\":"<<r.failure<<",\"now_ns\":"<<r.now<<'}';}out<<"]}";
}
}
