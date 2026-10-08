#pragma once
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/FeedMechanism.h"
#include <numbers>

namespace fvr::interaction {
// A physical gesture and a native cycle are separate authorities. This policy
// never grants ammunition, advances a native timer, or makes a weapon ready.
enum class WeaponCycleFamily : unsigned { Pump, Bolt };
enum class WeaponCyclePhase : unsigned { Idle, AwaitingGrip, Unlock, Rear, Forward, Lock, AwaitingNative, Complete, Cancelled };
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
};
struct WeaponCycleLease {
    HandInteractionOwner owner{};HandInteractionKey item{},mechanism{};
    std::uint64_t cycle=0,shot=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // Adapter-issued receipt for the exact native cycle being held. These
    // fields alone never constitute a native capability in a game adapter.
    bool held=false;
    bool operator==(const WeaponCycleLease&)const=default;
};
struct WeaponCycleInput {
    HandInteractionSample source{};
    math::Matrix4 rawContact{};
    bool grip=false,mechanismClaim=false;
};
struct WeaponCycleRelease {
    WeaponCycleLease cycle{};
    std::uint64_t request=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const WeaponCycleRelease&)const=default;
};
struct WeaponCycleReady {
    WeaponCycleRelease release{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool nativeReady=false,unchangedAmmunition=false;
};
struct WeaponCycleResult {
    WeaponCyclePhase phase=WeaponCyclePhase::Idle;
    WeaponCycleFailure failure=WeaponCycleFailure::None;
    bool blocksFire=false;
    float travel=0,rotation=0;
    // Presentation only; adapters must validate current native part identity
    // and their renderer receipts independently before using this target.
    std::optional<math::Matrix4> contact;
    std::optional<WeaponCycleRelease> release;
};
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
    return a.owner==b.owner&&a.item==b.item&&a.mechanism==b.mechanism&&a.cycle==b.cycle&&a.shot==b.shot;
}
inline bool Lease(const WeaponCycleLease& p,std::int64_t now){
    return Owner(p.owner)&&Key(p.item)&&Key(p.mechanism)&&p.cycle&&p.sequence&&p.held&&Window(p.observedNs,p.deadlineNs,now);
}
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
class WeaponCycle {
public:
    static constexpr std::int64_t MaximumInputGapNs=150000000;
    bool Begin(const WeaponCycleProfile& profile,const WeaponCycleLease& lease,std::int64_t now)noexcept {
        using namespace weapon_cycle_detail;
        if((phase_!=WeaponCyclePhase::Idle&&phase_!=WeaponCyclePhase::Complete&&phase_!=WeaponCyclePhase::Cancelled)||
           !Profile(profile)||!Lease(lease,now)||now>INT64_MAX-profile.maximumCycleNs||
           (identity_.cycle&&Same(identity_,lease)))return false;
        profile_=profile;identity_=lease;latest_=lease;deadline_=now+profile.maximumCycleNs;
        phase_=WeaponCyclePhase::AwaitingGrip;failure_=WeaponCycleFailure::None;neutral_=false;
        lastSequence_=0;lastObserved_=0;endpoint_=0;release_.reset();travel_=rotation_=0;return true;
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
        if(phase_==WeaponCyclePhase::Idle||phase_==WeaponCyclePhase::Complete||phase_==WeaponCyclePhase::Cancelled)return Result();
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
        lastSequence_=s.sequence;lastObserved_=s.observedNs;
        if(phase_==WeaponCyclePhase::AwaitingGrip){
            if(!in.grip){neutral_=true;return Result();}
            if(!neutral_||!in.mechanismClaim||Distance(in.rawContact,profile_.closedContact)>profile_.contactRadius)return Result();
            grabbed_=previous_=in.rawContact;phase_=profile_.family==WeaponCycleFamily::Pump?WeaponCyclePhase::Rear:WeaponCyclePhase::Unlock;
            return Result(true);
        }
        if(!in.grip||!in.mechanismClaim)return Cancel(WeaponCycleFailure::Released);
        if(Distance(in.rawContact,previous_)>profile_.maxStepMeters)return Cancel(WeaponCycleFailure::Geometry);
        const auto delta=Sub(Position(in.rawContact),Position(grabbed_));const auto axial=Dot(delta,profile_.axis);
        const double lateral2=std::max(0.,Dot(delta,delta)-axial*axial);
        if(lateral2>double(profile_.lateralTolerance)*profile_.lateralTolerance||axial<-.03||axial>profile_.stroke+.03)return Cancel(WeaponCycleFailure::Geometry);
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
                if(nextRequest_==UINT64_MAX)return Cancel(WeaponCycleFailure::Invalid);
                phase_=WeaponCyclePhase::AwaitingNative;
                release_=WeaponCycleRelease{latest_,++nextRequest_,s.sequence,s.observedNs,std::min(s.deadlineNs,latest_.deadlineNs)};
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
    WeaponCycleResult Cancel(WeaponCycleFailure why)noexcept {phase_=WeaponCyclePhase::Cancelled;failure_=why;release_.reset();return Result();}
    WeaponCyclePhase Phase()const noexcept{return phase_;}
private:
    WeaponCycleResult Result(bool target=false)const noexcept {
        WeaponCycleResult r{phase_,failure_,phase_!=WeaponCyclePhase::Idle&&phase_!=WeaponCyclePhase::Complete,travel_,rotation_};
        if(target)r.contact=weapon_cycle_detail::Target(profile_,travel_,rotation_);
        if(phase_==WeaponCyclePhase::AwaitingNative)r.release=release_;
        return r;
    }
    WeaponCycleProfile profile_{};WeaponCycleLease identity_{},latest_{};
    WeaponCyclePhase phase_=WeaponCyclePhase::Idle;WeaponCycleFailure failure_=WeaponCycleFailure::None;
    std::optional<WeaponCycleRelease> release_;
    math::Matrix4 grabbed_{},previous_{};
    std::uint64_t lastSequence_=0,nextRequest_=0;
    std::int64_t deadline_=0,lastObserved_=0,endpoint_=0;
    bool neutral_=false;float travel_=0,rotation_=0;
};
}
