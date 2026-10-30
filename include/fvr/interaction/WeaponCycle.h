#pragma once
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/FeedMechanism.h"
#include <numbers>
#include <type_traits>
#include "fvr/interaction/WeaponCycleRecoveryAuthority.h"

namespace fvr::interaction {
// A physical gesture and a native cycle are separate authorities. This policy
// never grants ammunition, advances a native timer, or makes a weapon ready.
enum class WeaponCycleFamily : unsigned { Pump, Bolt };
enum class WeaponCyclePhase : unsigned { Idle, AwaitingGrip, Unlock, Rear, Forward, Lock, AwaitingNative, Complete, Cancelled, ReloadPaused };
enum class WeaponCycleFailure : unsigned { None, Invalid, Expired, Owner, Evidence, Tracking, Released, Geometry, NativeRejected };
struct WeaponCycleHandAssignment {
    InteractionHand mechanism=InteractionHand::Left,gun=InteractionHand::Right;
    bool operator==(const WeaponCycleHandAssignment&)const=default;
};
struct WeaponCycleProfile {
    std::uint64_t id=0,revision=0;
    WeaponCycleFamily family=WeaponCycleFamily::Pump;
    // Axis points from the closed part toward its rear stop. Distances/metres,
    // angles/radians, and input matrices are canonical weapon-local values.
    std::array<float,3> axis{0,0,-1};
    math::Matrix4 closedContact{};
    float stroke=0,rearTolerance=0,frontTolerance=0,contactRadius=0,lateralTolerance=0;
    float unlockRadians=0,rotationTolerance=0,maxStepMeters=0;
    std::int64_t endpointDwellNs=0,maximumCycleNs=0;
    // Fixed for the whole cycle. Adapters must transfer custody explicitly;
    // selecting right-hand manipulation never creates a left GunHold.
    WeaponCycleHandAssignment hands{};
    // A non-rotating sliding rail has no physical controller stop. Crossing
    // each endpoint in order counts; do not require the player to dwell there.
    // Defaults preserve the rotational bolt/detent contract.
    bool slidingRail=false;
    float railOvertravelMeters=0,maximumSpeedMetersPerSecond=0;
    // Optional held-grasp envelope around the rail segment. Controller travel
    // beyond a virtual stop clamps the displayed part; it is not a release.
    // Acquisition still uses contactRadius. Zero retains the older limits.
    float railRetentionRadius=0;
};
struct WeaponCycleLease {
    HandInteractionOwner owner{};HandInteractionKey item{},mechanism{};
    std::uint64_t cycle=0,shot=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // Adapter-issued receipt for the exact native cycle being held. These
    // fields alone never constitute a native capability in a game adapter.
    bool held=false;
    // Mutually exclusive with a frozen native boundary. A continuously guarded
    // native clock may advance while the same physical stroke remains owed.
    std::optional<WeaponCycleGuardedAdvance> advancing;
    bool operator==(const WeaponCycleLease&)const=default;
};
struct WeaponCycleInput {
    HandInteractionSample source{};
    math::Matrix4 rawContact{};
    bool grip=false,mechanismClaim=false;
};
// A retained physical milestone, never a release or a native capability. The
// original active-stroke deadline and source packet are immutable. Reload may
// suspend the active budget for at most thirty seconds total for this cycle.
struct WeaponCycleReloadPause {
    HandInteractionOwner owner{};HandInteractionKey item{},mechanism{};
    std::uint64_t cycle=0,shot=0,pause=0,profile=0,revision=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0,cycleDeadlineNs=0,pausedNs=0,pauseDeadlineNs=0;
    float travel=0,rotation=0;
    bool operator==(const WeaponCycleReloadPause&)const=default;
};
template<class Authority> struct WeaponCycleReleaseT {
    Authority cycle{};
    std::uint64_t request=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const WeaponCycleReleaseT&)const=default;
};
template<class Authority> struct WeaponCycleReadyT {
    WeaponCycleReleaseT<Authority> release{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool nativeReady=false,unchangedAmmunition=false;
};
template<class Authority> struct WeaponCycleResultT {
    WeaponCyclePhase phase=WeaponCyclePhase::Idle;
    WeaponCycleFailure failure=WeaponCycleFailure::None;
    bool blocksFire=false;
    float travel=0,rotation=0;
    // Presentation only; adapters must validate current native part identity
    // and their renderer receipts independently before using this target.
    std::optional<math::Matrix4> contact;
    std::optional<WeaponCycleReleaseT<Authority>> release;
};
using WeaponCycleRelease=WeaponCycleReleaseT<WeaponCycleLease>;
using WeaponCycleReady=WeaponCycleReadyT<WeaponCycleLease>;
using WeaponCycleResult=WeaponCycleResultT<WeaponCycleLease>;
using WeaponCycleIdleDebtRelease=WeaponCycleReleaseT<WeaponCycleIdleDebtLease>;
using WeaponCycleIdleDebtReady=WeaponCycleReadyT<WeaponCycleIdleDebtLease>;
using WeaponCycleIdleDebtResult=WeaponCycleResultT<WeaponCycleIdleDebtLease>;
namespace weapon_cycle_detail {
inline bool Hands(WeaponCycleHandAssignment h){
    return (h.mechanism==InteractionHand::Left||h.mechanism==InteractionHand::Right)&&
        (h.gun==InteractionHand::Left||h.gun==InteractionHand::Right)&&h.mechanism!=h.gun;
}
inline std::size_t HandIndex(InteractionHand h){return static_cast<std::size_t>(h);}
inline bool Key(HandInteractionKey k){return k.id&&k.generation;}
inline bool Owner(HandInteractionOwner o){return o.actor&&o.actorGeneration&&o.equipGeneration&&o.space;}
inline bool Window(std::int64_t observed,std::int64_t deadline,std::int64_t now){
    return observed>0&&observed<=now&&now<deadline&&deadline-observed<=200000000;
}
inline double Dot(const std::array<float,3>& a,const std::array<float,3>& b){return double(a[0])*b[0]+double(a[1])*b[1]+double(a[2])*b[2];}
inline std::array<float,3> Cross(const std::array<float,3>& a,const std::array<float,3>& b){
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
inline std::array<float,3> Position(const math::Matrix4& a){return {a.values[3][0],a.values[3][1],a.values[3][2]};}
inline std::array<float,3> Sub(std::array<float,3> a,const std::array<float,3>& b){for(unsigned n=0;n<3;++n)a[n]-=b[n];return a;}
inline float Distance(const math::Matrix4& a,const math::Matrix4& b){const auto d=Sub(Position(a),Position(b));return float(std::sqrt(Dot(d,d)));}
inline bool Same(const WeaponCycleLease& a,const WeaponCycleLease& b){
    return a.owner==b.owner&&a.item==b.item&&a.mechanism==b.mechanism&&a.cycle==b.cycle&&a.shot==b.shot&&
        a.advancing.has_value()==b.advancing.has_value()&&(!a.advancing||
        (a.advancing->guardEpoch==b.advancing->guardEpoch&&a.advancing->loaded==b.advancing->loaded&&
         a.advancing->reserve==b.advancing->reserve&&a.advancing->capacity==b.advancing->capacity));
}
inline bool Lease(const WeaponCycleLease& p,std::int64_t now){
    if(!Owner(p.owner)||!Key(p.item)||!Key(p.mechanism)||!p.cycle||!p.sequence||
       !Window(p.observedNs,p.deadlineNs,now)||p.held==p.advancing.has_value())return false;
    if(!p.advancing)return true;
    const auto& g=*p.advancing;
    if(!p.shot||!g.guardEpoch||g.loaded<0||g.reserve<0||g.capacity<=0||g.loaded>=g.capacity)return false;
    for(unsigned b=0;b<3;++b){
        if(!g.updates[b])return false;
        for(unsigned a=0;a<b;++a)if(g.updates[a]==g.updates[b])return false;
    }
    return true;
}
inline bool Same(const WeaponCycleIdleDebtLease& a,const WeaponCycleIdleDebtLease& b){
    return a.owner==b.owner&&a.item==b.item&&a.mechanism==b.mechanism&&a.cycle==b.cycle&&a.shot==b.shot&&a.grant==b.grant;
}
inline bool MagazineCommand(const AmmunitionCommand& c){
    const auto& b=c.before;const auto& a=c.after;const auto& r=c.context.resource;
    if(!r.actor||!r.actorGeneration||!r.weapon||!r.weaponGeneration||!c.context.equipGeneration||!c.context.space||
       !c.id||!c.sourceSequence||c.requestedNs<=0||c.admissionDeadlineNs<=c.requestedNs||
       c.deadlineNs<c.admissionDeadlineNs||c.deadlineNs-c.requestedNs>2000000000ll||
       b.capacity<=0||b.loaded<0||b.loaded>b.capacity||b.reserve<0||a.capacity!=b.capacity||
       a.loaded<0||a.loaded>a.capacity||a.reserve<0||c.mutationRequired!=(a!=b))return false;
    switch(c.operation){
    case AmmunitionOperation::RemoveMagazine:return !c.original&&a.loaded==0&&a.reserve==b.reserve;
    case AmmunitionOperation::ReturnMagazine:return c.original&&c.original->id&&c.original->owner==r&&
        c.original->capacity==b.capacity&&c.original->rounds>=0&&c.original->rounds<=b.capacity&&
        b.loaded==0&&a.loaded==c.original->rounds&&a.reserve==b.reserve;
    case AmmunitionOperation::RefillMagazine:return !c.original&&b.loaded==0&&b.reserve>0&&
        a.loaded==std::min(b.capacity,b.reserve)&&a.reserve==b.reserve-a.loaded;
    default:return false;
    }
}
inline bool AmmoRebase(const WeaponCycleDebt& d){
    if(!d.ammunitionRebase)return false;
    const auto& p=*d.ammunitionRebase;const auto& r=p.receipt;const auto& c=r.command;const auto& o=c.context;
    // The adapter proved inventory actor/equip -> hand actor/equip mapping at
    // PrepareReload. The receipt retains the original inventory context verbatim.
    if(p.owner!=d.owner||p.item!=d.item||p.mechanism!=d.mechanism||p.cycle!=d.cycle||p.shot!=d.shot||
       !p.pause||!p.previousGuardEpoch||p.guardEpoch<=p.previousGuardEpoch||p.guardEpoch!=d.guardEpoch||!p.drainRevision||
       !MagazineCommand(c)||o.resource.actorGeneration!=d.owner.actorGeneration||
       o.resource.weapon!=d.item.id||!o.equipGeneration||o.space!=d.owner.space||
       !r.authorityVerified||!r.copiesVerified||!r.authorityInvocation||r.beganNs<c.requestedNs||
       r.beganNs>=c.admissionDeadlineNs||r.completedNs<r.beganNs||r.completedNs>=c.deadlineNs||
       r.before!=c.before||r.after!=c.after||r.after!=AmmunitionCounts{d.loaded,d.reserve,d.capacity})return false;
    for(unsigned b=0;b<3;++b){if(!p.ownUpdates[b])return false;
        for(unsigned a=0;a<b;++a)if(p.ownUpdates[a]==p.ownUpdates[b])return false;}
    return true;
}
inline bool Lease(const WeaponCycleIdleDebtLease& p,std::int64_t now){
    const auto& g=p.grant;const auto& d=g.debt;const auto& n=g.neutral;
    if(!Owner(p.owner)||!Key(p.item)||!Key(p.mechanism)||!p.cycle||!p.shot||!p.sequence||
       !Window(p.observedNs,p.deadlineNs,now)||!g.nonce||!d.guardEpoch||d.loaded<0||d.reserve<0||d.capacity<=0||d.loaded>d.capacity||
       (d.ammunitionRebase?!AmmoRebase(d):d.loaded>=d.capacity)||
       d.owner!=p.owner||d.item!=p.item||d.mechanism!=p.mechanism||d.cycle!=p.cycle||d.shot!=p.shot||
       n.owner!=p.owner||!n.sequence||!n.focused||!n.tracked[0]||!n.tracked[1]||
       !Window(n.observedNs,n.deadlineNs,n.nowNs)||n.observedNs>p.observedNs)return false;
    for(unsigned b=0;b<3;++b)if(!p.first[b]||p.second[b]<=p.first[b]||p.second[b]>p.sequence)return false;
    return true;
}
inline bool AuthorityForHands(const WeaponCycleLease&,WeaponCycleHandAssignment){return true;}
inline bool AuthorityForHands(const WeaponCycleIdleDebtLease& a,WeaponCycleHandAssignment h){
    return Hands(h)&&a.grant.neutral.released[HandIndex(h.mechanism)]&&!a.grant.neutral.released[HandIndex(h.gun)];
}
inline bool InitialHeldAdoption(const WeaponCycleLease&){return true;}
inline bool InitialHeldAdoption(const WeaponCycleIdleDebtLease&){return false;}
inline bool Profile(const WeaponCycleProfile& p){
    if(!p.id||!p.revision||!Hands(p.hands)||(p.family!=WeaponCycleFamily::Pump&&p.family!=WeaponCycleFamily::Bolt)||
        !feed_mechanism_detail::Pose(p.closedContact)||!std::isfinite(Dot(p.axis,p.axis))||std::abs(Dot(p.axis,p.axis)-1)>1e-5||
        !std::isfinite(p.stroke)||p.stroke<=0||p.stroke>.5f||!std::isfinite(p.rearTolerance)||p.rearTolerance<0||p.rearTolerance>=p.stroke*.25f||
        !std::isfinite(p.frontTolerance)||p.frontTolerance<0||p.frontTolerance>=p.stroke*.25f||
        !std::isfinite(p.contactRadius)||p.contactRadius<=0||p.contactRadius>.2f||
        !std::isfinite(p.lateralTolerance)||p.lateralTolerance<=0||p.lateralTolerance>.2f||
        !std::isfinite(p.maxStepMeters)||p.maxStepMeters<=0||p.maxStepMeters>=p.stroke||
        !std::isfinite(p.rotationTolerance)||p.rotationTolerance<0||p.rotationTolerance>.5f||
        p.endpointDwellNs<=0||p.endpointDwellNs>250000000||p.maximumCycleNs<=0||p.maximumCycleNs>30000000000ll)return false;
    if(p.slidingRail&&(p.family!=WeaponCycleFamily::Pump||!std::isfinite(p.railOvertravelMeters)||
       p.railOvertravelMeters<.03f||p.railOvertravelMeters>.15f||!std::isfinite(p.maximumSpeedMetersPerSecond)||
       p.maximumSpeedMetersPerSecond<=0||p.maximumSpeedMetersPerSecond>10))return false;
    if(!std::isfinite(p.railRetentionRadius)||p.railRetentionRadius<0||p.railRetentionRadius>.5f||
       (p.railRetentionRadius>0&&(!p.slidingRail||p.railRetentionRadius<p.contactRadius)))return false;
    return p.family==WeaponCycleFamily::Pump?p.unlockRadians==0:
        std::isfinite(p.unlockRadians)&&std::abs(p.unlockRadians)>.25f&&std::abs(p.unlockRadians)<std::numbers::pi_v<float>-.1f;
}
inline math::Matrix4 Target(const WeaponCycleProfile& p,float travel,float angle){
    auto result=p.closedContact;const double c=std::cos(angle),s=std::sin(angle);
    // Row-vector Rodrigues rotation; translate separately along the rail.
    for(unsigned row=0;row<3;++row){std::array<float,3> v{result.values[row][0],result.values[row][1],result.values[row][2]};
        const auto cross=Cross(p.axis,v);const auto dot=Dot(p.axis,v);
        for(unsigned k=0;k<3;++k)result.values[row][k]=float(v[k]*c+cross[k]*s+p.axis[k]*dot*(1-c));}
    for(unsigned k=0;k<3;++k)result.values[3][k]+=p.axis[k]*travel;
    return result;
}
}
template<class Authority> class WeaponCycleT {
    template<class> friend class WeaponCycleT;
public:
    using WeaponCycleLease=Authority;
    using WeaponCycleRelease=WeaponCycleReleaseT<Authority>;
    using WeaponCycleReady=WeaponCycleReadyT<Authority>;
    using WeaponCycleResult=WeaponCycleResultT<Authority>;
    static constexpr std::int64_t MaximumInputGapNs=150000000;
    static constexpr std::int64_t MaximumReloadPauseNs=30000000000ll;
    bool Begin(const WeaponCycleProfile& profile,const WeaponCycleLease& lease,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        if((phase_!=WeaponCyclePhase::Idle&&phase_!=WeaponCyclePhase::Complete&&phase_!=WeaponCyclePhase::Cancelled)||
           !Profile(profile)||!Lease(lease,now)||!AuthorityForHands(lease,profile.hands)||now>INT64_MAX-profile.maximumCycleNs||
           (identity_.cycle&&Same(identity_,lease)))return false;
        profile_=profile;identity_=lease;latest_=lease;deadline_=now+profile.maximumCycleNs;
        phase_=WeaponCyclePhase::AwaitingGrip;failure_=WeaponCycleFailure::None;neutral_=false;
        lastSequence_=0;lastObserved_=0;endpoint_=0;pausedBudgetNs_=0;release_.reset();pause_.reset();resumeOpen_=false;travel_=rotation_=0;return true;
    }
    std::optional<WeaponCycleReloadPause> PauseForReload(const WeaponCycleInput& input)noexcept {
        using namespace weapon_cycle_detail;const auto& s=input.source;
        if(phase_!=WeaponCyclePhase::Forward||profile_.family!=WeaponCycleFamily::Bolt||release_||
           travel_<profile_.stroke-profile_.rearTolerance||std::abs(rotation_-profile_.unlockRadians)>profile_.rotationTolerance||
           s.owner!=identity_.owner||s.nowNs>=deadline_||!s.focused||!s.tracked[0]||!s.tracked[1]||input.grip||
           !s.released[HandIndex(profile_.hands.mechanism)]||s.released[HandIndex(profile_.hands.gun)]||
           s.sequence<=lastSequence_||s.observedNs<=lastObserved_||s.observedNs-lastObserved_>MaximumInputGapNs||
           !Window(s.observedNs,s.deadlineNs,s.nowNs)||pauseNonce_==UINT64_MAX||pausedBudgetNs_>=MaximumReloadPauseNs||
           s.nowNs>INT64_MAX-(MaximumReloadPauseNs-pausedBudgetNs_))return {};
        pause_=WeaponCycleReloadPause{identity_.owner,identity_.item,identity_.mechanism,identity_.cycle,identity_.shot,
            ++pauseNonce_,profile_.id,profile_.revision,s.sequence,s.observedNs,s.deadlineNs,deadline_,
            s.nowNs,s.nowNs+MaximumReloadPauseNs-pausedBudgetNs_,travel_,rotation_};
        lastSequence_=s.sequence;lastObserved_=s.observedNs;phase_=WeaponCyclePhase::ReloadPaused;return pause_;
    }
    template<class Previous> bool CanResumeAfterReload(const WeaponCycleT<Previous>& previous,
        const WeaponCycleLease& lease,const HandInteractionSample& neutral)const noexcept {
        using namespace weapon_cycle_detail;
        if constexpr(!std::is_same_v<Authority,WeaponCycleIdleDebtLease>)return false;
        else {
            const auto& p=previous.pause_;const auto& d=lease.grant.debt;
            return (phase_==WeaponCyclePhase::Idle||phase_==WeaponCyclePhase::AwaitingGrip)&&
                previous.phase_==WeaponCyclePhase::ReloadPaused&&p&&AmmoRebase(d)&&d.ammunitionRebase->pause==p->pause&&
                lease.owner==p->owner&&lease.item==p->item&&lease.mechanism==p->mechanism&&lease.cycle==p->cycle&&lease.shot==p->shot&&
                Lease(lease,neutral.nowNs)&&AuthorityForHands(lease,previous.profile_.hands)&&neutral.nowNs>=p->pausedNs&&
                neutral.nowNs<p->pauseDeadlineNs&&neutral.nowNs<=INT64_MAX-(p->cycleDeadlineNs-p->pausedNs)&&
                neutral.owner==p->owner&&neutral.sequence>p->inputSequence&&neutral.observedNs>p->observedNs&&
                neutral.sequence>=lease.grant.neutral.sequence&&neutral.observedNs>=lease.grant.neutral.observedNs&&
                neutral.focused&&neutral.tracked[0]&&neutral.tracked[1]&&
                neutral.released[HandIndex(previous.profile_.hands.mechanism)]&&!neutral.released[HandIndex(previous.profile_.hands.gun)]&&
                Window(neutral.observedNs,neutral.deadlineNs,neutral.nowNs);
        }
    }
    template<class Previous> bool ResumeAfterReload(WeaponCycleT<Previous>& previous,
        const WeaponCycleLease& lease,const HandInteractionSample& neutral)noexcept {
        if(!CanResumeAfterReload(previous,lease,neutral))return false;
        const auto elapsed=neutral.nowNs-previous.pause_->pausedNs;
        profile_=previous.profile_;identity_=latest_=lease;deadline_=previous.deadline_+elapsed;
        pausedBudgetNs_=previous.pausedBudgetNs_+elapsed;
        grabbed_=previous.grabbed_;previous_=previous.previous_;travel_=previous.travel_;rotation_=previous.rotation_;
        nextRequest_=std::max(nextRequest_,previous.nextRequest_);pauseNonce_=std::max(pauseNonce_,previous.pauseNonce_);
        lastSequence_=neutral.sequence;lastObserved_=neutral.observedNs;endpoint_=0;neutral_=true;resumeOpen_=true;
        release_.reset();pause_=previous.pause_;phase_=WeaponCyclePhase::AwaitingGrip;failure_=WeaponCycleFailure::None;
        previous.pause_.reset();previous.phase_=WeaponCyclePhase::Cancelled;previous.failure_=WeaponCycleFailure::Released;return true;
    }
    const std::optional<WeaponCycleReloadPause>& ReloadPause()const noexcept{return pause_;}
    math::Matrix4 AcquisitionContact()const noexcept {
        return resumeOpen_?weapon_cycle_detail::Target(profile_,travel_,rotation_):profile_.closedContact;
    }
    // A current arbiter-issued Mechanism claim can enter without a regrip.
    // The adapter may transfer existing WeaponSupport through the arbiter;
    // a held controller bit or an old support token alone is not admission.
    bool BeginHeld(const WeaponCycleProfile& profile,const WeaponCycleLease& lease,
        const WeaponCycleInput& input,const HandClaim& mechanism,const HandClaim& gun)noexcept {
        if(!HeldInput(profile,lease,input,mechanism,gun)||!Begin(profile,lease,input.source.nowNs))return false;
        return AdoptHeld(lease,input,mechanism,gun);
    }
    // Only an exact current arbiter transfer can bypass neutral acquisition.
    bool AdoptHeld(const WeaponCycleLease& lease,const WeaponCycleInput& input,
        const HandClaim& mechanism,const HandClaim& gun,const HandInteractionSample* currentSafety=nullptr)noexcept {
        using namespace weapon_cycle_detail;const auto& s=input.source;
        if(phase_!=WeaponCyclePhase::AwaitingGrip||s.nowNs>=deadline_||!Same(identity_,lease)||
           !CurrentLease(lease,s.nowNs)||!HeldInput(profile_,lease,input,mechanism,gun,currentSafety))return false;
        latest_=lease;grabbed_=previous_=input.rawContact;lastSequence_=s.sequence;lastObserved_=s.observedNs;neutral_=true;
        phase_=profile_.family==WeaponCycleFamily::Pump?WeaponCyclePhase::Rear:WeaponCyclePhase::Unlock;return true;
    }
    // Physical interruption does not retire an adapter's still-held native
    // cycle. Restart the entire gesture only after fresh neutral evidence,
    // preserving the original cycle deadline and request high-water mark.
    bool Regrip(const WeaponCycleInput& input,const WeaponCycleLease& lease)noexcept {
        using namespace weapon_cycle_detail;const auto& s=input.source;
        if(phase_!=WeaponCyclePhase::Cancelled||release_||
           (failure_!=WeaponCycleFailure::Tracking&&failure_!=WeaponCycleFailure::Released&&failure_!=WeaponCycleFailure::Geometry)||
           s.nowNs>=deadline_||!Same(identity_,lease)||!CurrentLease(lease,s.nowNs)||s.owner!=identity_.owner||
           !s.focused||!s.tracked[0]||!s.tracked[1]||input.grip||!s.released[HandIndex(profile_.hands.mechanism)]||
           s.sequence<=lastSequence_||s.observedNs<=lastObserved_||!Window(s.observedNs,s.deadlineNs,s.nowNs)||
           !feed_mechanism_detail::Pose(input.rawContact)||Distance(input.rawContact,profile_.closedContact)>profile_.contactRadius)return false;
        latest_=lease;lastSequence_=s.sequence;lastObserved_=s.observedNs;endpoint_=0;travel_=rotation_=0;neutral_=true;
        phase_=WeaponCyclePhase::AwaitingGrip;failure_=WeaponCycleFailure::None;return true;
    }
    // Once submitted, release/ready is a native transaction, independent of
    // whether a hand remains tracked or a native held lease is still published.
    // This cannot acknowledge completion or extend its original timeout.
    WeaponCycleResult PollNative(std::int64_t now)noexcept {
        if(phase_==WeaponCyclePhase::AwaitingNative&&now>=deadline_)return Cancel(WeaponCycleFailure::Expired);
        return Result();
    }
private:
    static bool HeldInput(const WeaponCycleProfile& profile,const WeaponCycleLease& lease,
        const WeaponCycleInput& input,const HandClaim& mechanism,const HandClaim& gun,const HandInteractionSample* currentSafety=nullptr)noexcept {
        using namespace weapon_cycle_detail;const auto& s=input.source;const auto& current=currentSafety?*currentSafety:s;
        if(current.owner!=s.owner||current.sequence<s.sequence||current.observedNs<s.observedNs||current.nowNs!=s.nowNs||
           !current.focused||!current.tracked[0]||!current.tracked[1]||current.released[0]||current.released[1]||
           !Window(current.observedNs,current.deadlineNs,current.nowNs))return false;
        if(!Profile(profile)||!Lease(lease,s.nowNs)||s.owner!=lease.owner||!s.sequence||!s.focused||!s.tracked[0]||!s.tracked[1]||
           !Window(s.observedNs,s.deadlineNs,s.nowNs)||!input.grip||!input.mechanismClaim||!feed_mechanism_detail::Pose(input.rawContact)||
           Distance(input.rawContact,profile.closedContact)>profile.contactRadius||
           !mechanism.token.id||!gun.token.id||mechanism.token.owner!=s.owner||gun.token.owner!=s.owner||
           mechanism.token.item!=lease.item||gun.token.item!=lease.item||mechanism.token.contact!=lease.mechanism||
           mechanism.token.hand!=profile.hands.mechanism||mechanism.token.kind!=HandClaimKind::Mechanism||
           gun.token.hand!=profile.hands.gun||gun.token.kind!=HandClaimKind::GunHold||
           mechanism.token.prerequisiteClaim!=gun.token.id||mechanism.inputSequence!=s.sequence||gun.inputSequence!=current.sequence||
           mechanism.deadlineNs<=s.nowNs||gun.deadlineNs<=s.nowNs||mechanism.deadlineNs>s.deadlineNs||gun.deadlineNs>current.deadlineNs)return false;
        return true;
    }
    bool CurrentLease(const WeaponCycleLease& lease,std::int64_t now)const noexcept {
        return weapon_cycle_detail::Lease(lease,now)&&lease.sequence>=latest_.sequence&&lease.observedNs>=latest_.observedNs&&
            (lease.sequence!=latest_.sequence||(lease.observedNs==latest_.observedNs&&lease.deadlineNs==latest_.deadlineNs));
    }
public:
    WeaponCycleResult Update(const WeaponCycleInput& in,const WeaponCycleLease& lease)noexcept {
        using namespace weapon_cycle_detail;const auto& s=in.source;
        if(phase_==WeaponCyclePhase::Idle||phase_==WeaponCyclePhase::Complete||phase_==WeaponCyclePhase::Cancelled||phase_==WeaponCyclePhase::ReloadPaused)return Result();
        if(s.nowNs>=deadline_)return Cancel(WeaponCycleFailure::Expired);
        if(!Same(identity_,lease)||s.owner!=identity_.owner)return Cancel(WeaponCycleFailure::Owner);
        if(!CurrentLease(lease,s.nowNs))return Cancel(WeaponCycleFailure::Evidence);
        if(!Window(s.observedNs,s.deadlineNs,s.nowNs)||!s.sequence||!s.focused||!s.tracked[0]||!s.tracked[1])return Cancel(WeaponCycleFailure::Tracking);
        latest_=lease;
        if(phase_==WeaponCyclePhase::AwaitingNative)return Result();
        if(!feed_mechanism_detail::Pose(in.rawContact))return Cancel(WeaponCycleFailure::Geometry);
        if(s.sequence<lastSequence_||s.observedNs<lastObserved_||
           (lastObserved_&&s.observedNs-lastObserved_>MaximumInputGapNs))return Cancel(WeaponCycleFailure::Tracking);
        // A duplicate cannot accrue endpoint dwell or mutate a gesture.
        if(s.sequence==lastSequence_)return Result(phase_!=WeaponCyclePhase::AwaitingGrip);
        const auto elapsed=s.observedNs-lastObserved_;
        lastSequence_=s.sequence;lastObserved_=s.observedNs;
        if(phase_==WeaponCyclePhase::AwaitingGrip){
            if(!in.grip){neutral_=true;return Result();}
            if(!neutral_||!in.mechanismClaim||Distance(in.rawContact,AcquisitionContact())>profile_.contactRadius)return Result();
            previous_=in.rawContact;
            if(resumeOpen_){resumeOpen_=false;phase_=WeaponCyclePhase::Forward;}
            else {grabbed_=in.rawContact;phase_=profile_.family==WeaponCycleFamily::Pump?WeaponCyclePhase::Rear:WeaponCyclePhase::Unlock;}
            return Result(true);
        }
        if(!in.grip||!in.mechanismClaim)return Cancel(WeaponCycleFailure::Released);
        const auto stepLimit=profile_.slidingRail?
            std::max(profile_.maxStepMeters,float(elapsed)*1e-9f*profile_.maximumSpeedMetersPerSecond):profile_.maxStepMeters;
        if(Distance(in.rawContact,previous_)>stepLimit)return Cancel(WeaponCycleFailure::Geometry);
        const auto delta=Sub(Position(in.rawContact),Position(grabbed_));const auto axial=Dot(delta,profile_.axis);
        const double lateral2=std::max(0.,Dot(delta,delta)-axial*axial);
        const double overtravel=profile_.slidingRail?profile_.railOvertravelMeters:.03;
        if(profile_.slidingRail&&profile_.railRetentionRadius>0){
            const auto beyond=std::max({0.,-axial,axial-profile_.stroke});
            if(lateral2+beyond*beyond>double(profile_.railRetentionRadius)*profile_.railRetentionRadius)
                return Cancel(WeaponCycleFailure::Geometry);
        }else if(lateral2>double(profile_.lateralTolerance)*profile_.lateralTolerance||axial<-overtravel||axial>profile_.stroke+overtravel)
            return Cancel(WeaponCycleFailure::Geometry);
        if(profile_.slidingRail){
            // Wrist roll does not rotate a sliding fore-end. The displayed
            // wrist/part remain on the measured rail, independently of raw input.
            rotation_=0;travel_=std::clamp(float(axial),0.f,profile_.stroke);previous_=in.rawContact;
            if(phase_==WeaponCyclePhase::Rear&&travel_>=profile_.stroke-profile_.rearTolerance)
                phase_=WeaponCyclePhase::Forward;
            else if(phase_==WeaponCyclePhase::Forward&&travel_<=profile_.frontTolerance)Release(s);
            return Result(true);
        }
        // Choose the captured orientation row most perpendicular to the rail.
        unsigned best=0;double minimum=2;
        for(unsigned n=0;n<3;++n){std::array<float,3> v{grabbed_.values[n][0],grabbed_.values[n][1],grabbed_.values[n][2]};
            const double d=std::abs(Dot(v,profile_.axis));if(d<minimum){minimum=d;best=n;}}
        std::array<float,3> from{},to{};
        for(unsigned k=0;k<3;++k){from[k]=grabbed_.values[best][k];to[k]=in.rawContact.values[best][k];}
        const double fd=Dot(from,profile_.axis),td=Dot(to,profile_.axis);
        for(unsigned k=0;k<3;++k){from[k]-=float(fd)*profile_.axis[k];to[k]-=float(td)*profile_.axis[k];}
        if(Dot(to,to)<.5)return Cancel(WeaponCycleFailure::Geometry);
        rotation_=float(std::atan2(Dot(profile_.axis,Cross(from,to)),Dot(from,to)));
        travel_=std::clamp(float(axial),0.f,profile_.stroke);previous_=in.rawContact;
        bool endpoint=false;
        if(phase_==WeaponCyclePhase::Unlock){
            if(travel_>profile_.frontTolerance)return Cancel(WeaponCycleFailure::Geometry);
            endpoint=std::abs(rotation_-profile_.unlockRadians)<=profile_.rotationTolerance;
        }else if(phase_==WeaponCyclePhase::Rear||phase_==WeaponCyclePhase::Forward){
            const float expected=profile_.family==WeaponCycleFamily::Pump?0:profile_.unlockRadians;
            if(std::abs(rotation_-expected)>profile_.rotationTolerance)return Cancel(WeaponCycleFailure::Geometry);
            endpoint=phase_==WeaponCyclePhase::Rear?travel_>=profile_.stroke-profile_.rearTolerance:travel_<=profile_.frontTolerance;
        }else if(phase_==WeaponCyclePhase::Lock){
            if(travel_>profile_.frontTolerance)return Cancel(WeaponCycleFailure::Geometry);
            endpoint=std::abs(rotation_)<=profile_.rotationTolerance;
        }
        if(!endpoint)endpoint_=0;
        else if(!endpoint_)endpoint_=s.observedNs;
        else if(s.observedNs-endpoint_>=profile_.endpointDwellNs){
            endpoint_=0;
            if(phase_==WeaponCyclePhase::Unlock)phase_=WeaponCyclePhase::Rear;
            else if(phase_==WeaponCyclePhase::Rear)phase_=WeaponCyclePhase::Forward;
            else if(phase_==WeaponCyclePhase::Forward&&profile_.family==WeaponCycleFamily::Bolt)phase_=WeaponCyclePhase::Lock;
            else {
                Release(s);
            }
        }
        return Result(true);
    }
    bool Complete(const WeaponCycleReady& ready,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        if(phase_!=WeaponCyclePhase::AwaitingNative||!release_||now>=deadline_||!ready.nativeReady||!ready.unchangedAmmunition||
           ready.release!=*release_||
           ready.sequence<=release_->cycle.sequence||ready.observedNs<release_->observedNs||!Window(ready.observedNs,ready.deadlineNs,now))return false;
        phase_=WeaponCyclePhase::Complete;return true;
    }
    // A retained immutable completion is an outcome, not a current native
    // lease. The adapter must retain the exact submitted request and original
    // completion timestamps. This resolves even a local polling timeout; it
    // cannot create/renew a target or accept a completion after that deadline.
    bool ReconcileReady(const WeaponCycleReady& ready,const WeaponCycleRelease& submitted,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        if((phase_!=WeaponCyclePhase::AwaitingNative&&!(phase_==WeaponCyclePhase::Cancelled&&failure_==WeaponCycleFailure::Expired))||
           ready.release!=submitted||!submitted.request||submitted.request!=nextRequest_||!Same(submitted.cycle,identity_)||
           !ready.nativeReady||!ready.unchangedAmmunition||ready.sequence<=submitted.cycle.sequence||
           ready.observedNs<submitted.observedNs||ready.observedNs>=deadline_||now<ready.observedNs||
           !Window(ready.observedNs,ready.deadlineNs,ready.observedNs))return false;
        phase_=WeaponCyclePhase::Complete;failure_=WeaponCycleFailure::None;return true;
    }
    WeaponCycleResult Cancel(WeaponCycleFailure why)noexcept {
        if(phase_==WeaponCyclePhase::ReloadPaused)return Result();
        phase_=WeaponCyclePhase::Cancelled;failure_=why;release_.reset();return Result();}
    WeaponCyclePhase Phase()const noexcept{return phase_;}
private:
    void Release(const HandInteractionSample& source)noexcept {
        if(nextRequest_==UINT64_MAX){Cancel(WeaponCycleFailure::Invalid);return;}
        phase_=WeaponCyclePhase::AwaitingNative;
        release_=WeaponCycleRelease{latest_,++nextRequest_,source.sequence,source.observedNs,std::min(source.deadlineNs,latest_.deadlineNs)};
    }
    WeaponCycleResult Result(bool target=false)const noexcept {
        WeaponCycleResult r{phase_,failure_,phase_!=WeaponCyclePhase::Idle&&phase_!=WeaponCyclePhase::Complete,travel_,rotation_};
        if(target)r.contact=weapon_cycle_detail::Target(profile_,travel_,rotation_);
        if(phase_==WeaponCyclePhase::AwaitingNative)r.release=release_;
        return r;
    }
    WeaponCycleProfile profile_{};WeaponCycleLease identity_{},latest_{};
    WeaponCyclePhase phase_=WeaponCyclePhase::Idle;WeaponCycleFailure failure_=WeaponCycleFailure::None;
    std::optional<WeaponCycleRelease> release_;
    std::optional<WeaponCycleReloadPause> pause_;
    math::Matrix4 grabbed_{},previous_{};
    std::uint64_t lastSequence_=0,nextRequest_=0,pauseNonce_=0;
    std::int64_t deadline_=0,lastObserved_=0,endpoint_=0,pausedBudgetNs_=0;
    bool neutral_=false,resumeOpen_=false;float travel_=0,rotation_=0;
};
using WeaponCycle=WeaponCycleT<WeaponCycleLease>;
using WeaponCycleIdleDebt=WeaponCycleT<WeaponCycleIdleDebtLease>;
}
