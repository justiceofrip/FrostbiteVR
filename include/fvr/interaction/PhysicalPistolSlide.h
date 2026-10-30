#pragma once
#include "fvr/interaction/WeaponMechanism.h"
#include <limits>

namespace fvr::interaction {
// These receipts are adapter inputs, not a native capability. In particular,
// WeaponCycleLease/NativeAfterShot, ammunition counts, animation and elapsed
// stock time cannot be converted into after-feed or chamber authority here.
struct PistolSlideIdentity {
    HandInteractionOwner owner{};
    HandInteractionKey item{},mechanism{},magazine{},ammunition{};
    std::uint64_t descriptor=0,revision=0,feedTransaction=0,cycle=0,grant=0;
    bool operator==(const PistolSlideIdentity&)const=default;
};
struct PistolSlideAuthority {
    PistolSlideIdentity identity{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    ChamberKnowledge chamber=ChamberKnowledge::Unknown;
    bool verifiedAfterFeed=false,feedSeated=false,cycleOwed=false;
    bool operator==(const PistolSlideAuthority&)const=default;
};
struct PistolSlideProfile {
    std::uint64_t id=0,revision=0;
    // Proper rigid row-vector contact and unit rearward axis, canonical
    // weapon-local METRES. Adapter geometry must belong to the raw packet.
    math::Matrix4 closedContact{};
    std::array<float,3> axis{0,0,-1};
    float stroke=0,frontTolerance=0,rearTolerance=0,hysteresis=0;
    float contactRadius=0,lateralTolerance=0,rotationTolerance=0,maxStepMeters=0;
    std::int64_t endpointDwellNs=0,maximumStrokeNs=0;
    WeaponCycleHandAssignment hands{};
};
enum class PistolSlidePhase:std::uint8_t {Idle,AwaitingGrip,Pull,Return,AwaitingRelease,AwaitingNative,Complete,Retired};
enum class PistolSlideFailure:std::uint8_t {None,Input,Authority,Owner,Tracking,Custody,Geometry,Interrupted,Expired};
struct PistolSlideRequest {
    PistolSlideAuthority authority{};
    HandClaimToken mechanism{},gun{};
    std::uint64_t request=0,profile=0,revision=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const PistolSlideRequest&)const=default;
};
struct PistolSlidePoststate {
    PistolSlideIdentity identity{};
    ChamberKnowledge chamber=ChamberKnowledge::Unknown;
    bool feedSeated=false,nativeReady=false;
};
struct PistolSlideReady {
    PistolSlideRequest request{};
    PistolSlidePoststate poststate{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
};
struct PistolSlideNonDispatch {
    PistolSlideRequest request{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // Only proof that the exact request NEVER entered native dispatch.
    bool neverDispatched=false;
};
struct PistolSlideRetirement {
    PistolSlideIdentity identity{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool ownerLifetimeEnded=false;
};
struct PhysicalPistolSlideSample {
    HandInteractionSample source{};
    PistolSlideAuthority authority{};
    math::Matrix4 rawContact{};
    bool grip=false;
    HandClaimToken gun{};
    HandContactProof gunContact{},contact{};
    std::uint64_t acquireIntent=0;
};
struct PistolSlideTarget {
    PistolSlideIdentity identity{};
    HandClaimToken mechanism{},gun{};
    std::uint64_t profile=0,revision=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    math::Matrix4 contact{};
    float travel=0;
};
struct PhysicalPistolSlideResult {
    PistolSlidePhase phase=PistolSlidePhase::Idle;
    PistolSlideFailure failure=PistolSlideFailure::None;
    // False does not grant firing; the adapter still owns chamber/fire policy.
    bool blocksFire=false,ownsMechanism=false,requestExpired=false;
    std::optional<PistolSlideTarget> target;
    // One emission per full measured stroke, not an acknowledgement.
    std::optional<PistolSlideRequest> request;
};
namespace pistol_slide_detail {
inline bool Identity(const PistolSlideIdentity& i)noexcept {
    using namespace weapon_cycle_detail;
    return Owner(i.owner)&&Key(i.item)&&Key(i.mechanism)&&Key(i.magazine)&&Key(i.ammunition)&&
        i.descriptor&&i.revision&&i.feedTransaction&&i.cycle&&i.grant;
}
inline bool Authority(const PistolSlideAuthority& a,std::int64_t now)noexcept {
    return Identity(a.identity)&&a.sequence&&weapon_cycle_detail::Window(a.observedNs,a.deadlineNs,now)&&
        a.verifiedAfterFeed&&a.feedSeated&&a.cycleOwed&&a.chamber==ChamberKnowledge::Empty;
}
inline bool Profile(const PistolSlideProfile& p)noexcept {
    using namespace weapon_cycle_detail;
    const auto positive=[](float v,float max){return std::isfinite(v)&&v>0&&v<=max;};
    return p.id&&p.revision&&Hands(p.hands)&&feed_mechanism_detail::Pose(p.closedContact)&&
        std::isfinite(Dot(p.axis,p.axis))&&std::abs(Dot(p.axis,p.axis)-1)<1e-5&&
        positive(p.stroke,.15f)&&p.stroke>=.01f&&positive(p.frontTolerance,p.stroke*.15f)&&
        positive(p.rearTolerance,p.stroke*.15f)&&positive(p.hysteresis,p.stroke*.15f)&&
        p.frontTolerance+p.hysteresis<p.stroke*.3f&&p.rearTolerance+p.hysteresis<p.stroke*.3f&&
        positive(p.contactRadius,.15f)&&positive(p.lateralTolerance,.1f)&&
        positive(p.rotationTolerance,.5f)&&positive(p.maxStepMeters,p.stroke*.5f)&&
        p.endpointDwellNs>=5000000&&p.endpointDwellNs<=250000000&&
        p.maximumStrokeNs>=p.endpointDwellNs*4&&p.maximumStrokeNs<=30000000000ll;
}
inline float Angle(const math::Matrix4& a,const math::Matrix4& b)noexcept {
    double trace=0;for(unsigned row=0;row<3;++row)for(unsigned k=0;k<3;++k)trace+=double(a.values[row][k])*b.values[row][k];
    return float(std::acos(std::clamp((trace-1)*.5,-1.0,1.0)));
}
// Shared measured-motion kernel. It grants neither native chamber knowledge nor
// policy readiness; each caller supplies its own typed authority and custody.
inline std::optional<float> Travel(const PistolSlideProfile& p,const math::Matrix4& raw)noexcept {
    using namespace weapon_cycle_detail;
    if(!feed_mechanism_detail::Pose(raw)||Angle(raw,p.closedContact)>p.rotationTolerance)return {};
    const auto delta=Sub(Position(raw),Position(p.closedContact));
    const float travel=float(Dot(delta,p.axis));
    const double lateral=std::max(0.0,Dot(delta,delta)-double(travel)*travel);
    if(!std::isfinite(travel)||lateral>double(p.lateralTolerance)*p.lateralTolerance||
       travel<-(p.frontTolerance+p.hysteresis)||travel>p.stroke+p.rearTolerance+p.hysteresis)return {};
    return travel;
}
class MeasuredStroke {
public:
    void Start(float travel,std::int64_t observed)noexcept {started_=observed;endpoint_=0;last_=travel;}
    bool Expired(const PistolSlideProfile& p,std::int64_t observed)const noexcept {
        return observed-started_>p.maximumStrokeNs;
    }
    bool Step(const PistolSlideProfile& p,float travel)noexcept {
        if(std::abs(travel-last_)>p.maxStepMeters)return false;last_=travel;return true;
    }
    bool Release(const PistolSlideProfile& p,PistolSlidePhase phase,float travel,bool realRelease)const noexcept {
        return phase==PistolSlidePhase::AwaitingRelease&&std::abs(travel)<=p.frontTolerance&&realRelease;
    }
    void Advance(const PistolSlideProfile& p,PistolSlidePhase& phase,float travel,std::int64_t observed)noexcept {
        if(phase==PistolSlidePhase::Pull){
            if(travel>=p.stroke-p.rearTolerance){
                if(!endpoint_)endpoint_=observed;
                else if(observed-endpoint_>=p.endpointDwellNs){phase=PistolSlidePhase::Return;endpoint_=0;}
            }else if(travel<p.stroke-p.rearTolerance-p.hysteresis)endpoint_=0;
        }else if(phase==PistolSlidePhase::Return){
            if(std::abs(travel)<=p.frontTolerance){
                if(!endpoint_)endpoint_=observed;
                else if(observed-endpoint_>=p.endpointDwellNs){phase=PistolSlidePhase::AwaitingRelease;endpoint_=0;}
            }else if(std::abs(travel)>p.frontTolerance+p.hysteresis)endpoint_=0;
        }else if(phase==PistolSlidePhase::AwaitingRelease&&std::abs(travel)>p.frontTolerance+p.hysteresis){
            phase=PistolSlidePhase::Return;endpoint_=0;
        }
    }
private:
    std::int64_t started_=0,endpoint_=0;float last_=0;
};
}

// One serialized instance per interaction domain. It must see a real release
// before another consumer consumes that packet's arbiter release; an already
// lost claim cannot prove release completion. GunHold is renewed from explicit
// current adapter contact, never acquired here. Other hand roles are not stolen.
// Tracking/stow cleanup discards physical progress only. Adapter-issued debt and
// an ambiguous submitted request survive until exact completion or retirement.
class PhysicalPistolSlide {
public:
    PhysicalPistolSlide()=default;
    PhysicalPistolSlide(const PhysicalPistolSlide&)=delete;
    PhysicalPistolSlide& operator=(const PhysicalPistolSlide&)=delete;
    bool Begin(const PistolSlideProfile& profile,const WeaponMechanismDescriptor& descriptor,
        WeaponMechanismIntent intent,const PistolSlideAuthority& authority,std::int64_t now)noexcept {
        const auto selection=SelectWeaponMechanismPlan(descriptor,intent,authority.chamber);
        if(Owed()||pending_||mechanism_||now<lastNow_||!pistol_slide_detail::Profile(profile)||
           !pistol_slide_detail::Authority(authority,now)||authority.identity.grant<=lastGrant_||
           descriptor.id!=authority.identity.descriptor||descriptor.revision!=authority.identity.revision||
           descriptor.feed!=WeaponFeed::DetachableMagazine||intent!=WeaponMechanismIntent::EmptyFeed||
           selection.status!=WeaponMechanismStatus::Plan||!selection.plan||
           selection.plan->action!=WeaponAction::Slide||selection.plan->purpose!=WeaponCyclePurpose::AfterFeed||
           selection.plan->gestureFamily||now>INT64_MAX-profile.maximumStrokeNs)return false;
        // A fresh lease/nonce cannot replay an already completed same-owner feed.
        if(identity_.owner==authority.identity.owner&&identity_.item==authority.identity.item&&
           authority.identity.feedTransaction<=identity_.feedTransaction)return false;
        profile_=profile;identity_=authority.identity;latest_=authority;lastGrant_=identity_.grant;
        phase_=PistolSlidePhase::AwaitingGrip;failure_=PistolSlideFailure::None;
        neutral_=false;lastInput_=0;lastObserved_=0;lastNow_=now;motion_={};
        return true;
    }
    bool ObserveReady(const PistolSlideReady& ready,std::int64_t now)noexcept {
        if(!pending_||ready.request!=*pending_||ready.poststate.identity!=identity_||
           ready.poststate.chamber!=ChamberKnowledge::Occupied||!ready.poststate.feedSeated||!ready.poststate.nativeReady||
           !Receipt(ready.sequence,ready.observedNs,ready.deadlineNs,now))return false;
        pending_.reset();lastNow_=now;phase_=PistolSlidePhase::Complete;failure_=PistolSlideFailure::None;return true;
    }
    bool RejectUndispatched(const PistolSlideNonDispatch& rejected,std::int64_t now)noexcept {
        if(!pending_||rejected.request!=*pending_||!rejected.neverDispatched||
           !Receipt(rejected.sequence,rejected.observedNs,rejected.deadlineNs,now))return false;
        pending_.reset();lastNow_=now;Restart(PistolSlideFailure::Interrupted);return true;
    }
    bool Retire(const PistolSlideRetirement& retired,std::int64_t now)noexcept {
        if(!Owed()||pending_||mechanism_||retired.identity!=identity_||!retired.ownerLifetimeEnded||
           retired.sequence<=latest_.sequence||retired.observedNs<latest_.observedNs||
           !weapon_cycle_detail::Window(retired.observedNs,retired.deadlineNs,now))return false;
        lastNow_=now;phase_=PistolSlidePhase::Retired;neutral_=false;return true;
    }
    void CancelPhysical(const HandInteractionSample& source,HandInteraction& hands)noexcept {
        Drop(source,hands);if(Owed()&&!pending_)Restart(PistolSlideFailure::Interrupted);
    }
    PhysicalPistolSlideResult Update(const PhysicalPistolSlideSample& sample,HandInteraction& hands)noexcept {
        using namespace weapon_cycle_detail;
        const auto& s=sample.source;
        // Inactive/pending consumers do not rewind or otherwise affect another
        // consumer's current arbiter input. Pending state has no physical claim.
        if(!Owed()||pending_)return Output(s.nowNs,hands);
        if(s.owner!=identity_.owner){CancelPhysical(s,hands);failure_=PistolSlideFailure::Owner;return Output(s.nowNs,hands);}
        const auto fail=[&](PistolSlideFailure reason){Drop(s,hands);Restart(reason);return Output(s.nowNs,hands);};
        if(s.nowNs<lastNow_)return fail(PistolSlideFailure::Input);
        lastNow_=s.nowNs;
        if(!s.sequence||!Window(s.observedNs,s.deadlineNs,s.nowNs)||!s.focused||!s.tracked[0]||!s.tracked[1]||
           s.released[HandIndex(profile_.hands.gun)]||s.released[HandIndex(profile_.hands.mechanism)]==sample.grip)
            return fail(PistolSlideFailure::Tracking);
        if(s.sequence<lastInput_||s.observedNs<lastObserved_)return fail(PistolSlideFailure::Input);
        if(s.sequence==lastInput_)return Output(s.nowNs,hands); // No replay progress or dwell.
        const bool interrupted=lastObserved_&&s.observedNs-lastObserved_>150000000;
        if(s.observedNs<=lastObserved_)return fail(PistolSlideFailure::Input);
        lastInput_=s.sequence;lastObserved_=s.observedNs;
        if(interrupted)return fail(PistolSlideFailure::Expired);
        const auto& a=sample.authority;
        if(a.identity!=identity_||!pistol_slide_detail::Authority(a,s.nowNs)||a.sequence<latest_.sequence||
           a.observedNs<latest_.observedNs||(a.sequence==latest_.sequence&&a!=latest_))return fail(PistolSlideFailure::Authority);
        latest_=a;
        const auto before=hands.Current(profile_.hands.mechanism);
        const auto safety=hands.UpdateAndRenewGunHold(s,sample.gun,sample.gunContact);
        const auto gun=hands.Current(profile_.hands.gun);
        const bool gunOkay=safety.lifecycle.inputValid&&gun&&gun->token==sample.gun&&sample.gun.owner==identity_.owner&&
            sample.gun.kind==HandClaimKind::GunHold&&sample.gun.hand==profile_.hands.gun&&sample.gun.item==identity_.item&&
            gun->inputSequence==s.sequence&&gun->deadlineNs>s.nowNs&&gun->deadlineNs<=s.deadlineNs;
        if(!gunOkay)return fail(PistolSlideFailure::Custody);
        const auto measured=pistol_slide_detail::Travel(profile_,sample.rawContact);
        if(!measured)return fail(PistolSlideFailure::Geometry);
        const float travel=*measured;
        if(phase_==PistolSlidePhase::AwaitingGrip){
            if(!sample.grip){neutral_=true;return Output(s.nowNs,hands);}
            const bool press=neutral_;neutral_=false;
            if(!press)return Output(s.nowNs,hands);
            if(std::abs(travel)>profile_.frontTolerance||Distance(sample.rawContact,profile_.closedContact)>profile_.contactRadius||
               sample.contact.key!=identity_.mechanism||sample.contact.inputSequence!=s.sequence||!sample.contact.eligible||
               sample.contact.deadlineNs>s.deadlineNs||sample.contact.deadlineNs<=s.nowNs)return fail(PistolSlideFailure::Geometry);
            const auto acquired=hands.Acquire(s,{identity_.owner,profile_.hands.mechanism,HandClaimKind::Mechanism,identity_.item,
                sample.contact,sample.acquireIntent,sample.gun.id});
            if(!acquired.accepted||!acquired.claim)return fail(PistolSlideFailure::Custody);
            mechanism_=acquired.claim->token;gun_=sample.gun;phase_=PistolSlidePhase::Pull;
            motion_.Start(travel,s.observedNs);failure_=PistolSlideFailure::None;
            return WithTarget(sample,hands,travel);
        }
        if(motion_.Expired(profile_,s.observedNs))return fail(PistolSlideFailure::Expired);
        if(!mechanism_||gun_!=sample.gun||!before||before->token!=*mechanism_||before->deadlineNs<=s.nowNs||
           before->token.prerequisiteClaim!=sample.gun.id)return fail(PistolSlideFailure::Custody);
        if(!motion_.Step(profile_,travel))return fail(PistolSlideFailure::Geometry);
        if(!sample.grip){
            bool released=false;for(const auto& event:safety.lifecycle.released)
                released|=event&&event->token==*mechanism_&&event->reason==HandInteractionReason::Released;
            if(!motion_.Release(profile_,phase_,travel,released)||
               nextRequest_==std::numeric_limits<std::uint64_t>::max())return fail(PistolSlideFailure::Interrupted);
            const auto token=*mechanism_;
            const auto deadline=std::min({s.deadlineNs,a.deadlineNs,gun->deadlineNs});
            pending_=PistolSlideRequest{a,token,sample.gun,++nextRequest_,profile_.id,profile_.revision,s.sequence,s.observedNs,deadline};
            mechanism_.reset();phase_=PistolSlidePhase::AwaitingNative;failure_=PistolSlideFailure::None;
            auto out=Output(s.nowNs,hands);out.request=pending_;return out;
        }
        const auto current=hands.Current(profile_.hands.mechanism);
        if(!current||current->token!=*mechanism_||!hands.Renew(s,*mechanism_,
            {identity_.mechanism,s.sequence,std::min(s.deadlineNs,a.deadlineNs),true}).accepted)return fail(PistolSlideFailure::Custody);
        motion_.Advance(profile_,phase_,travel,s.observedNs);
        return WithTarget(sample,hands,travel);
    }
    PistolSlidePhase Phase()const noexcept{return phase_;}
    bool Owed()const noexcept{return phase_!=PistolSlidePhase::Idle&&phase_!=PistolSlidePhase::Complete&&phase_!=PistolSlidePhase::Retired;}
private:
    bool Receipt(std::uint64_t sequence,std::int64_t observed,std::int64_t deadline,std::int64_t now)const noexcept {
        return pending_&&sequence>pending_->authority.sequence&&observed>=pending_->observedNs&&
            now>=lastNow_&&weapon_cycle_detail::Window(observed,deadline,now);
    }
    void Restart(PistolSlideFailure reason)noexcept {
        phase_=PistolSlidePhase::AwaitingGrip;failure_=reason;neutral_=false;motion_={};
    }
    void Drop(const HandInteractionSample& s,HandInteraction& hands)noexcept {
        if(mechanism_){const auto held=hands.Current(mechanism_->hand);if(held&&held->token==*mechanism_)hands.Release(s,*mechanism_);}
        mechanism_.reset();
    }
    PhysicalPistolSlideResult Output(std::int64_t now,const HandInteraction& hands)const noexcept {
        const auto held=hands.Current(profile_.hands.mechanism);
        return {phase_,failure_,Owed(),mechanism_&&held&&held->token==*mechanism_&&held->deadlineNs>now,pending_&&now>=pending_->deadlineNs,{},{}};
    }
    PhysicalPistolSlideResult WithTarget(const PhysicalPistolSlideSample& sample,const HandInteraction& hands,float travel)const noexcept {
        auto out=Output(sample.source.nowNs,hands);const auto held=hands.Current(profile_.hands.mechanism),gun=hands.Current(profile_.hands.gun);
        if(!mechanism_||!held||!gun)return out;
        auto contact=profile_.closedContact;const float clamped=std::clamp(travel,0.f,profile_.stroke);
        for(unsigned k=0;k<3;++k)contact.values[3][k]+=profile_.axis[k]*clamped;
        out.target=PistolSlideTarget{identity_,*mechanism_,sample.gun,profile_.id,profile_.revision,sample.source.sequence,
            sample.source.observedNs,std::min({sample.source.deadlineNs,sample.authority.deadlineNs,held->deadlineNs,gun->deadlineNs}),contact,clamped};
        return out;
    }
    PistolSlideProfile profile_{};PistolSlideIdentity identity_{};PistolSlideAuthority latest_{};
    PistolSlidePhase phase_=PistolSlidePhase::Idle;PistolSlideFailure failure_=PistolSlideFailure::None;
    std::optional<HandClaimToken> mechanism_;HandClaimToken gun_{};
    std::optional<PistolSlideRequest> pending_;
    std::uint64_t nextRequest_=0,lastGrant_=0,lastInput_=0;
    std::int64_t lastObserved_=0,lastNow_=0;
    pistol_slide_detail::MeasuredStroke motion_{};bool neutral_=false;
};
}
