#include "Bc2PhysicalPump.h"
#include "Bc2SightContact.h"
#include <ostream>
namespace fvr::bc2 {
using namespace interaction;
namespace {
bool Point(math::Vec3 p)noexcept{return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::hypot(p.x,p.y,p.z)<.3f;}
bool SamePoint(math::Vec3 a,math::Vec3 b)noexcept{return a.x==b.x&&a.y==b.y&&a.z==b.z;}
math::Matrix4 Contact(math::Matrix4 wrist,math::Vec3 point,float sign=1)noexcept {
    for(unsigned n=0;n<3;++n)wrist.values[3][n]+=sign*(point.x*wrist.values[0][n]+point.y*wrist.values[1][n]+point.z*wrist.values[2][n]);return wrist;
}
bool Mapped(const ReloadStateOwner& n,const HandInteractionSample& s)noexcept {
    return n.weapon>=0x10000&&n.equipGeneration&&n.actorGeneration==s.owner.actorGeneration&&n.space==s.owner.space&&
        s.owner.actor==((std::uint64_t(n.weak)<<32)|n.soldier);
}
WeaponCycleProfile Profile(const Bc2PumpCalibration& c,math::Vec3 point)noexcept {
    WeaponCycleProfile p;p.id=0x5350415350554d50ull;p.revision=c.revision;p.closedContact=Contact(c.closedWrist,point);
    p.axis={0,0,c.rearDirection};p.stroke=SpasObservedForeEndStroke;
    // Gesture tolerances are explicit controller policy, not native calibration.
    p.rearTolerance=p.frontTolerance=.006f;p.contactRadius=.10f;p.lateralTolerance=.06f;p.maxStepMeters=.04f;
    p.rotationTolerance=.35f;p.endpointDwellNs=30000000;p.maximumCycleNs=30000000000ll;return p;
}
}
bool PumpCalibrationValid(const Bc2PumpCalibration& c)noexcept {
    return c.measured&&c.revision&&c.rigFingerprint&&(c.rearDirection==1||c.rearDirection==-1)&&
        weapon_cycle_detail::Profile(Profile(c,{}))&&reload_insertion_detail::Rigid(c.closedPart)&&reload_insertion_detail::Rigid(c.closedWrist);
}
bool PumpTrackingFresh(const Bc2PumpTracking& t,std::int64_t now)noexcept {
    return t.enabled&&t.calibration&&PumpCalibrationValid(*t.calibration)&&Mapped(t.nativeOwner,t.input)&&t.input.sequence&&
        t.input.focused&&t.input.tracked[0]&&t.input.tracked[1]&&!t.input.released[1]&&
        weapon_cycle_detail::Window(t.input.observedNs,t.input.deadlineNs,now);
}
bool PumpTargetFresh(const Bc2PumpTracking& t,std::int64_t now)noexcept {
    if(!PumpTrackingFresh(t,now)||!t.mechanism||!t.gun||!Point(t.pointWrist)||t.input.released[0])return false;
    auto input=t.input;input.nowNs=now;const auto expected=Profile(*t.calibration,t.pointWrist);
    const auto valid=[&](const auto& target,const auto& authority){
        return CurrentPhysicalWeaponCycleTarget(target,authority,input,*t.mechanism,*t.gun)&&
            t.profile.id==expected.id&&t.profile.revision==expected.revision&&target.profile==expected.id&&
            target.revision==expected.revision&&t.profile.axis==expected.axis&&t.profile.stroke==expected.stroke&&
            t.profile.family==expected.family&&t.profile.closedContact.values==expected.closedContact.values&&target.travel<=expected.stroke;};
    if(t.target&&t.held&&!t.recoveryTarget&&!t.recovery)return valid(*t.target,*t.held);
    return !t.target&&!t.held&&t.recoveryTarget&&t.recovery&&valid(*t.recoveryTarget,*t.recovery);
}
bool PumpTargetRetained(const Bc2PumpTracking& old,const Bc2PumpTracking& current,std::int64_t now)noexcept {
    if(!PumpTargetFresh(old,now)||!PumpTargetFresh(current,now)||old.nativeOwner!=current.nativeOwner||
       old.calibration!=current.calibration||!SamePoint(old.pointWrist,current.pointWrist)||old.input.sequence>current.input.sequence||
       old.mechanism->token!=current.mechanism->token||old.gun->token!=current.gun->token)return false;
    auto input=current.input;input.nowNs=now;
    if(old.target&&current.held)return CurrentPhysicalWeaponCycleTarget(*old.target,*current.held,input,*current.mechanism,*current.gun);
    return old.recoveryTarget&&current.recovery&&CurrentPhysicalWeaponCycleTarget(*old.recoveryTarget,*current.recovery,input,*current.mechanism,*current.gun);
}
Bc2PumpRawContact BuildPumpRawContact(const Bc2PumpTracking& t,const RigSnapshot& rig,const math::Matrix4& rawWrist,
    const math::Matrix4& placed,math::Vec3 point,float units,std::int64_t now,const math::Matrix4* body)noexcept {
    if(!PumpTrackingFresh(t,now)||rig.identity.soldier!=t.nativeOwner.soldier||rig.identity.weak!=t.nativeOwner.weak||
       !Point(point)||!std::isfinite(units)||units<=0||units>1000||!reload_insertion_detail::Rigid(rawWrist))return {};
    const auto binding=bc2_pump_detail::DerivePart(rig);const auto inverse=InverseRigid(placed);
    if(!binding||binding->fingerprint!=t.calibration->rigFingerprint||!inverse)return {};
    auto wrist=Multiply(rawWrist,*inverse);for(unsigned n=0;n<3;++n)wrist.values[3][n]/=units;
    const auto contact=Contact(wrist,point);if(!reload_insertion_detail::Rigid(contact))return {};
    Bc2PumpRawContact out{t.nativeOwner,rig.identity,binding->fingerprint,t.input,contact,point,true};
    if(body&&reload_insertion_detail::Rigid(*body)){
        out.trackingBodyWorldMeters=*body;out.weaponWorldMeters=placed;out.rawWristWorldMeters=rawWrist;
        for(auto matrix:{&out.trackingBodyWorldMeters,&out.weaponWorldMeters,&out.rawWristWorldMeters})
            for(unsigned n=0;n<3;++n)matrix->values[3][n]/=units;
        out.mappingValid=true;
    }
    return out;
}
Bc2PumpPresentation BuildPumpPresentation(const Bc2PumpTracking& t,const RigSnapshot& rig,const math::Matrix4& placed,
    float units,std::int64_t now)noexcept {
    if(!PumpTargetFresh(t,now)||!std::isfinite(units)||units<=0||units>1000)return {};
    const auto binding=bc2_pump_detail::DerivePart(rig);
    if(!binding||binding->fingerprint!=t.calibration->rigFingerprint)return {};
    const auto build=[&]<class Authority>(const PhysicalWeaponCycleTargetT<Authority>& target,const Authority& authority)->Bc2PumpPresentation {
        Bc2PumpPartSourceT<Authority> source{target.lease,rig.identity,t.calibration->closedPart,t.calibration->rearDirection,
            target.mechanism,target.gun,target.inputSequence,target.observedNs,target.deadlineNs,true};
        auto input=t.input;input.nowNs=now;
        std::optional<RigPosePlan> proof;
        if constexpr(std::is_same_v<Authority,WeaponCycleLease>)
            proof=BuildSpasPumpCyclePart(rig,*binding,source,t.profile,target,authority,input,*t.mechanism,*t.gun,placed,units);
        else proof=BuildSpasPumpRecoveryPart(rig,*binding,source,t.profile,target,authority,input,*t.mechanism,*t.gun,placed,units);
        if(!proof||proof->edits.size()!=1)return {};
        auto wrist=Contact(target.contact,t.pointWrist,-1);for(unsigned n=0;n<3;++n)wrist.values[3][n]*=units;
        auto part=t.calibration->closedPart;part.values[3][2]+=t.calibration->rearDirection*target.travel;
        for(unsigned n=0;n<3;++n)part.values[3][n]*=units;
        return {Multiply(wrist,placed),BoneWrite{binding->part,Multiply(part,placed)}};
    };
    return t.target?build(*t.target,*t.held):build(*t.recoveryTarget,*t.recovery);
}
Bc2PhysicalPump::Bc2PhysicalPump(std::shared_ptr<const Bc2PumpCalibration> calibration,Bc2PhysicalPumpApi api)noexcept:
    calibration_(std::move(calibration)),api_(api){}
void Bc2PhysicalPump::Cancel(const HandInteractionSample& source,HandInteraction& hands)noexcept {
    ++cancels_;supportReturn_.reset();supportCapture_.reset();
    if(api_.cancel)api_.cancel(api_.context);
    // Physical loss cannot settle an outstanding native transaction.
    blocks_=blocks_||bool(cycle_)||bool(pending_)||bool(acknowledged_)||bool(recoveryCycle_)||bool(recoveryPending_);
    DropPhysical(source,hands);
}
void Bc2PhysicalPump::DropPhysical(const HandInteractionSample& source,HandInteraction& hands)noexcept {
    PhysicalWeaponCycleSample sample;sample.source=source;
    if(cycle_)sample.lease=*cycle_;
    physical_.Update(sample,hands);
    PhysicalWeaponCycleIdleDebtSample recovery;recovery.source=source;
    if(recoveryCycle_)recovery.lease=*recoveryCycle_;
    recoveryPhysical_.Update(recovery,hands);
}
Bc2PhysicalPumpResult Bc2PhysicalPump::Tick(const Bc2PhysicalPumpSample& s,HandInteraction& hands,std::uint64_t& intent)noexcept {
    ++ticks_;
    Bc2PhysicalPumpResult out;
    if(!calibration_||!PumpCalibrationValid(*calibration_)||!api_.control||!api_.view||!api_.ack||!api_.cancel)return out;
    // A native outcome survives interrupted presentation, but permission to
    // continue a held hand does not. Observe actual input throughout the tail.
    supportRaw_=s.raw;
    if(supportReturn_){
        const auto& old=*supportReturn_;const auto gun=hands.Current(InteractionHand::Right);
        if(s.cancel||s.asset!=SpasReloadAsset||!Mapped(s.nativeOwner,s.input)||s.input.owner!=std::visit([](const auto& r){return r.cycle.owner;},old.release)||
           s.item!=std::visit([](const auto& r){return r.cycle.item;},old.release)||s.nativeOwner!=old.nativeOwner||!s.input.focused||!s.input.tracked[0]||!s.input.tracked[1]||
           s.input.released[0]||s.input.released[1]||s.input.sequence<old.last.sequence||
           s.input.observedNs<old.last.observedNs||s.input.nowNs>=old.last.deadlineNs||
           !weapon_cycle_detail::Window(s.input.observedNs,s.input.deadlineNs,s.input.nowNs)||
           !gun||gun->token!=old.gun||gun->inputSequence!=s.input.sequence||gun->deadlineNs<=s.input.nowNs||
           hands.Current(InteractionHand::Left)||(old.readyNs&&s.input.nowNs-old.readyNs>200000000)){
            supportReturn_.reset();++supportReturnLost_;
        }else supportReturn_->last=s.input;
    }
    // Resolve the exact old outcome before considering a changed selected item.
    auto view=api_.view(api_.context,s.input.nowNs);
    if(view&&view->ready){
        if(!acknowledged_&&physical_.ReconcileReady(*view->ready,s.input.nowNs))acknowledged_=view->ready;
        if(acknowledged_&&api_.ack(api_.context,*acknowledged_)){++acks_;out.settled=acknowledged_;
            if(supportReturn_&&supportReturn_->release==decltype(supportReturn_->release){acknowledged_->release})supportReturn_->readyNs=acknowledged_->observedNs;
            acknowledged_.reset();pending_.reset();cycle_.reset();}
        view=api_.view(api_.context,s.input.nowNs);
    }
    if(api_.recovery.Complete()){
        const auto recovered=api_.recovery.view(api_.context,s.input.nowNs);
        if(recovered&&recovered->ready){
            if(!recoveryAcknowledged_&&recoveryPhysical_.ReconcileReady(*recovered->ready,s.input.nowNs))recoveryAcknowledged_=recovered->ready;
            if(recoveryAcknowledged_&&api_.recovery.ack(api_.context,*recoveryAcknowledged_)){
                ++acks_;out.recovered=recoveryAcknowledged_;
                if(supportReturn_&&supportReturn_->release==decltype(supportReturn_->release){recoveryAcknowledged_->release})
                    supportReturn_->readyNs=recoveryAcknowledged_->observedNs;
                recoveryAcknowledged_.reset();recoveryPending_.reset();recoveryCycle_.reset();cycle_.reset();
            }
        }
    }
    const auto gun=hands.Current(InteractionHand::Right);
    const bool safe=!s.cancel&&s.asset==SpasReloadAsset&&Mapped(s.nativeOwner,s.input)&&gun&&
        gun->token.owner==s.input.owner&&gun->token.item==s.item&&gun->token.kind==HandClaimKind::GunHold&&
        gun->inputSequence==s.input.sequence&&gun->deadlineNs>s.input.nowNs&&
        s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&!s.input.released[1]&&
        weapon_cycle_detail::Window(s.input.observedNs,s.input.deadlineNs,s.input.nowNs);
    if(!safe){blocks_=view?view->blocksFire:(blocks_||bool(cycle_)||bool(pending_));Cancel(s.input,hands);out.blocksFire=blocks_;return out;}
    if(pending_&&view&&view->phase==Bc2NativeCyclePhase::Releasing){
        // Submission ended gesture authority. Native completion is independent
        // of later hand generations or tracking loss; poll its exact outcome.
        blocks_=true;out.blocksFire=true;DropPhysical(s.input,hands);
        out.tracking={true,s.nativeOwner,s.input,calibration_};return out;
    }
    Bc2NativeCycleControl control{s.nativeOwner,s.input,s.item,{0x5350415350554d50ull,s.item.generation},{},true};
    if(!api_.control(api_.context,control)){
        ++controlRejected_;DropPhysical(s.input,hands);blocks_=true;out.blocksFire=true;return out;
    }
    out.tracking={true,s.nativeOwner,s.input,calibration_};
    view=api_.view(api_.context,s.input.nowNs);blocks_=!view||view->blocksFire||bool(cycle_)||bool(pending_);
    out.blocksFire=blocks_;
    if(api_.recovery.Complete()&&!pending_&&!acknowledged_&&(!view||!view->held)){
        const auto recovery=api_.recovery.view(api_.context,s.input.nowNs);
        if(recovery&&(recovery->authority||recoveryCycle_||recoveryPending_)){
            auto next=TickRecovery(s,*recovery,hands,intent);next.recovered=out.recovered;return next;
        }
    }
    if(!view||!view->held){DropPhysical(s.input,hands);return out;}
    const auto& raw=s.raw;
    const bool rawOkay=raw.valid&&raw.nativeOwner==s.nativeOwner&&raw.rigFingerprint==calibration_->rigFingerprint&&
        raw.input.owner==s.input.owner&&raw.input.sequence<=s.input.sequence&&raw.input.observedNs<=s.input.observedNs&&
        weapon_cycle_detail::Window(raw.input.observedNs,raw.input.deadlineNs,s.input.nowNs)&&Point(raw.pointWrist)&&
        (!cycle_||SamePoint(pointWrist_,raw.pointWrist));
    if(!rawOkay){++rawRejected_;DropPhysical(s.input,hands);return out;}
    if(!cycle_){profile_=Profile(*calibration_,raw.pointWrist);
        if(!physical_.Begin(profile_,*view->held,s.input.nowNs))return out;cycle_=view->held;pointWrist_=raw.pointWrist;++begins_;}
    if(!weapon_cycle_detail::Same(*cycle_,*view->held))return out;
    PhysicalWeaponCycleSample sample;sample.source=s.input;sample.contactSource=raw.input;sample.lease=*view->held;
    sample.rawContact=raw.contact;sample.grip=s.grip;sample.gun=gun->token;
    sample.contact={sample.lease.mechanism,raw.input.sequence,raw.input.deadlineNs,true};
    if(intent==UINT64_MAX)return out;sample.acquireIntent=++intent;
    const auto left=hands.Current(InteractionHand::Left);
    if(left&&left->token.kind==HandClaimKind::WeaponSupport)sample.support=left->token;
    const auto result=physical_.Update(sample,hands);out.ownsHand=result.ownsMechanism;
    out.tracking.profile=profile_;out.tracking.held=view->held;out.tracking.pointWrist=pointWrist_;out.tracking.target=result.target;
    if(result.target)++targets_;
    out.tracking.mechanism=hands.Current(InteractionHand::Left);out.tracking.gun=hands.Current(InteractionHand::Right);
    if(result.release){++releases_;pending_=result.release;control.release=result.release;
        supportCapture_.reset();
        if(!s.input.released[0])supportReturn_=SupportReturn{*result.release,gun->token,s.nativeOwner,s.input,0,s.input.sequence};
        // An unavailable runtime gate is ambiguous; retain the exact submitted
        // transaction. Never synthesize RejectNative from a boolean failure.
        if(!api_.control(api_.context,control))++controlRejected_;out.tracking.target.reset();out.ownsHand=false;
    }
    return out;
}
Bc2PhysicalPumpResult Bc2PhysicalPump::TickRecovery(const Bc2PhysicalPumpSample& s,const Bc2NativeCycleRecoveryView& view,
    HandInteraction& hands,std::uint64_t& intent)noexcept {
    Bc2PhysicalPumpResult out;out.blocksFire=blocks_=true;out.tracking={true,s.nativeOwner,s.input,calibration_};
    if(recoveryPending_){PhysicalWeaponCycleIdleDebtSample pending;pending.source=s.input;
        if(recoveryCycle_)pending.lease=*recoveryCycle_;recoveryPhysical_.Update(pending,hands);return out;}
    const auto gun=hands.Current(InteractionHand::Right);
    const auto& raw=s.raw;
    if(!view.authority||view.native.owner!=s.nativeOwner||!gun||
       view.authority->owner!=s.input.owner||view.authority->item!=s.item||
       view.authority->mechanism!=HandInteractionKey{0x5350415350554d50ull,s.item.generation}||
       !weapon_cycle_detail::Lease(*view.authority,s.input.nowNs)||
       (cycle_&&(cycle_->owner!=view.authority->owner||cycle_->item!=view.authority->item||
        cycle_->mechanism!=view.authority->mechanism||cycle_->cycle!=view.authority->cycle||cycle_->shot!=view.authority->shot))||
       !raw.valid||raw.nativeOwner!=s.nativeOwner||raw.rigFingerprint!=calibration_->rigFingerprint||
       raw.input.owner!=s.input.owner||raw.input.sequence>s.input.sequence||raw.input.observedNs>s.input.observedNs||
       !weapon_cycle_detail::Window(raw.input.observedNs,raw.input.deadlineNs,s.input.nowNs)||!Point(raw.pointWrist)||
       ((cycle_||recoveryCycle_)&&!SamePoint(pointWrist_,raw.pointWrist))){DropPhysical(s.input,hands);return out;}
    if(recoveryCycle_&&!weapon_cycle_detail::Same(*recoveryCycle_,*view.authority)){
        if(recoveryPhysical_.Phase()!=WeaponCyclePhase::Cancelled||recoveryCycle_->cycle!=view.authority->cycle||
           recoveryCycle_->shot!=view.authority->shot||recoveryCycle_->grant.debt!=view.authority->grant.debt){DropPhysical(s.input,hands);return out;}
        recoveryCycle_.reset();
    }
    if(!recoveryCycle_){profile_=Profile(*calibration_,raw.pointWrist);
        if(!recoveryPhysical_.Begin(profile_,*view.authority,s.input.nowNs))return out;
        recoveryCycle_=view.authority;pointWrist_=raw.pointWrist;++begins_;}
    if(!weapon_cycle_detail::Same(*recoveryCycle_,*view.authority)){DropPhysical(s.input,hands);return out;}
    PhysicalWeaponCycleIdleDebtSample sample;sample.source=s.input;sample.contactSource=raw.input;sample.lease=*view.authority;
    sample.rawContact=raw.contact;sample.grip=s.grip;sample.gun=gun->token;
    sample.contact={sample.lease.mechanism,raw.input.sequence,raw.input.deadlineNs,true};
    if(intent==UINT64_MAX)return out;sample.acquireIntent=++intent;
    if(const auto left=hands.Current(InteractionHand::Left);left&&left->token.kind==HandClaimKind::WeaponSupport)sample.support=left->token;
    const auto previous=recoveryPhysical_.Phase();
    const auto result=recoveryPhysical_.Update(sample,hands);out.ownsHand=result.ownsMechanism;
    if(result.cycle.phase==WeaponCyclePhase::Cancelled&&previous!=WeaponCyclePhase::Cancelled)api_.cancel(api_.context);
    out.tracking.profile=profile_;out.tracking.recovery=view.authority;out.tracking.pointWrist=pointWrist_;out.tracking.recoveryTarget=result.target;
    out.tracking.mechanism=hands.Current(InteractionHand::Left);out.tracking.gun=gun;
    if(result.target)++targets_;
    if(result.release){++releases_;recoveryPending_=result.release;supportCapture_.reset();
        if(!s.input.released[0])supportReturn_=SupportReturn{*result.release,gun->token,s.nativeOwner,s.input,0,s.input.sequence};
        if(!api_.recovery.submit(api_.context,*result.release,s.input))++controlRejected_;
        out.tracking.recoveryTarget.reset();out.ownsHand=false;
    }
    return out;
}
std::optional<SupportGripResult> Bc2PhysicalPump::ContinueSupport(const SupportGripOwner& owner,const InputFrame& input,
    const SupportGripContact& contact,const HandInteractionSample& original,HandInteractionKey contactKey,
    HandInteraction& hands,SupportGrip& support,std::uint64_t& intent,bool cancel)noexcept {
    if(!supportReturn_)return {};
    if(cancel){supportReturn_.reset();++supportReturnLost_;return {};}
    const auto grant=*supportReturn_;const auto& current=grant.last;
    if(!grant.readyNs||blocks_||current.nowNs<grant.readyNs||current.nowNs-grant.readyNs>200000000)return {};
    const auto& raw=supportRaw_;
    if(!raw.valid||raw.nativeOwner!=grant.nativeOwner||raw.rigFingerprint!=calibration_->rigFingerprint||
       !reload_insertion_detail::Rigid(raw.contact)||!SamePoint(raw.pointWrist,pointWrist_)||
       contactKey!=HandInteractionKey{2,grant.nativeOwner.weapon}||owner.equipped!=grant.nativeOwner.weapon||
       raw.input.owner!=current.owner||raw.input.sequence!=original.sequence||raw.input.observedNs!=original.observedNs||
       raw.input.deadlineNs!=original.deadlineNs||raw.input.focused!=original.focused||raw.input.tracked!=original.tracked||
       raw.input.released!=original.released||original.observedNs<grant.readyNs||original.sequence<=grant.releasedAtSequence||
       !weapon_cycle_detail::Window(original.observedNs,original.deadlineNs,current.nowNs))return {};
    const auto gun=hands.Current(InteractionHand::Right);
    if(!gun||gun->token!=grant.gun||hands.Current(InteractionHand::Left)||input.generation!=current.sequence||
       !contact.valid||!std::isfinite(contact.distanceMeters)||contact.distanceMeters<0||contact.distanceMeters>.16f||
       weapon_cycle_detail::Distance(raw.contact,profile_.closedContact)>profile_.contactRadius)return {};
    // An eligible attempt consumes the continuation even if exact arbiter
    // history rejects it. New geometry cannot retry a rejected held gesture.
    supportReturn_.reset();
    if(intent==UINT64_MAX)return {};
    const HandClaimRequest request{current.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,std::visit([](const auto& r){return r.cycle.item;},grant.release),
        {contactKey,original.sequence,original.deadlineNs,true},++intent,gun->token.id};
    const auto acquired=hands.AcquireFrom(current,original,request);
    if(!acquired.claim){++supportReturnLost_;return {};}
    auto trial=support;const auto result=trial.AdoptHeld(owner,input,contact,current,original,*acquired.claim,*gun);
    if(!result.holding||!result.engaged){hands.Release(current,acquired.claim->token);++supportReturnLost_;return {};}
    support=trial;supportCapture_=Bc2PumpSupportCapture{result.token,acquired.claim->token,gun->token};++supportReturns_;return result;
}
void Bc2PhysicalPump::BindSupport(Bc2PumpTracking& tracking,const SupportGripResult& result,const HandInteraction& hands)noexcept {
    const auto left=hands.Current(InteractionHand::Left),gun=hands.Current(InteractionHand::Right);
    if(!supportCapture_)return;
    if(!result.holding||result.token!=supportCapture_->token||!left||!gun||left->token!=supportCapture_->support||gun->token!=supportCapture_->gun){supportCapture_.reset();return;}
    tracking.supportCapture=supportCapture_;tracking.mechanism=left;tracking.gun=gun;
}
std::optional<math::Matrix4> ResolvePumpSupportWrist(const Bc2PumpTracking& t,const RigSnapshot& rig,std::uint64_t token,std::int64_t now)noexcept {
    if(!PumpTrackingFresh(t,now)||!t.supportCapture||!token||t.supportCapture->token!=token||t.input.released[0]||
       !t.mechanism||!t.gun||t.mechanism->token!=t.supportCapture->support||t.gun->token!=t.supportCapture->gun||
       t.mechanism->token.kind!=HandClaimKind::WeaponSupport||t.mechanism->token.hand!=InteractionHand::Left||
       t.gun->token.kind!=HandClaimKind::GunHold||t.gun->token.hand!=InteractionHand::Right||
       t.mechanism->token.owner!=t.input.owner||t.gun->token.owner!=t.input.owner||
       t.mechanism->token.item!=t.gun->token.item||t.mechanism->token.prerequisiteClaim!=t.gun->token.id||
       t.mechanism->inputSequence>t.input.sequence||t.mechanism->deadlineNs<=now||t.mechanism->deadlineNs>t.input.deadlineNs||
       t.gun->inputSequence!=t.input.sequence||t.gun->deadlineNs<=now)return {};
    const auto binding=bc2_pump_detail::DerivePart(rig);
    if(!binding||binding->fingerprint!=t.calibration->rigFingerprint||rig.identity.soldier!=t.nativeOwner.soldier||rig.identity.weak!=t.nativeOwner.weak)return {};
    return t.calibration->closedWrist;
}
void Bc2PhysicalPump::Report(std::ostream& out)const {
    out<<"{\"candidate\":true,\"ticks\":"<<ticks_.load()<<",\"cancels\":"<<cancels_.load()
       <<",\"control_rejected\":"<<controlRejected_.load()<<",\"raw_rejected\":"<<rawRejected_.load()
       <<",\"cycles_begun\":"<<begins_.load()<<",\"targets\":"<<targets_.load()
       <<",\"releases\":"<<releases_.load()<<",\"acknowledgements\":"<<acks_.load()
       <<",\"support_returns\":"<<supportReturns_.load()<<",\"support_return_lost\":"<<supportReturnLost_.load()<<'}';
}
}

