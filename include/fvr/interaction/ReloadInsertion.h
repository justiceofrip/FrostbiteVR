#pragma once
#include "HandInteraction.h"
#include "ManualReload.h"
#include "TrackingMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace fvr::interaction {
enum class ReloadInsertionFamily : std::uint8_t {Unknown,Magazine,SingleShell};
enum class ReloadInsertionOrientation : std::uint8_t {Keyed,AxialSymmetry};
enum class ReloadInsertionApproach : std::uint8_t {AxialFront,FrontHemisphere,EntrySphere,RailContact};
struct ReloadInsertionProfile {
    std::uint64_t id=0,revision=0;
    ReloadInsertionFamily family=ReloadInsertionFamily::Unknown;
    // AxialSymmetry requires authored evidence for this single-shell item.
    ReloadInsertionOrientation orientation=ReloadInsertionOrientation::Keyed;
    // Explicit authored interaction choice. FrontHemisphere accepts a radial
    // approach into the SAME front-side entry sphere; it does not enlarge it or
    // permit initial in-socket/back-side poses. EntrySphere is a single-shell
    // opt-in: any hemisphere may enter after an observed outside-volume pose,
    // with the entire capture volume strictly short of the seating depth.
    // RailContact is explicit magnetic assistance along the whole
    // authored path. It permits initial proximity contact from an owned shell,
    // aligns orientation after capture, and requires new post-capture travel.
    // Keyed magazines retain their capture/release orientation cone; only
    // axially symmetric single shells waive pre-capture orientation.
    // Existing profiles stay axial.
    ReloadInsertionApproach approach=ReloadInsertionApproach::AxialFront;
    // Proper canonical row-vector transforms, all translations in METRES.
    // itemFromHand maps hand-local into item-local at the authored grasp.
    // itemFromInsertion maps the item's insertion landmark into item-local.
    // weaponFromEntry maps the matching entry landmark into weapon-local;
    // its +Z is the insertion orientation reference; travelDirection supplies
    // the independent inward displacement axis. No world/native offsets here.
    math::Matrix4 itemFromHand{},itemFromInsertion{},weaponFromEntry{};
    // Unit displacement axis in entry coordinates, independent of the item's
    // insertion orientation. Existing profiles travel along entry +Z.
    std::array<float,3> travelDirection{0,0,1};
    float travelMeters=0,captureDistanceMeters=0,releaseDistanceMeters=0;
    // RailContact is an authored assist: capture near the authored path,
    // then require this much real inward motion before a physical seat event.
    // Existing profile modes leave this zero and retain their original policy.
    float postCaptureTravelMeters=0;
    float captureAngleRadians=0,releaseAngleRadians=0,seatToleranceMeters=0;
    float maxStepMeters=0,maxStepRadians=0;
    std::int64_t alignmentNs=0,seatDwellNs=0,maxSampleGapNs=0,maxGuidedNs=0;
};
struct ReloadInsertionIdentity {
    HandInteractionOwner owner{};
    HandInteractionKey weapon{},item{};
    std::uint64_t trackingEpoch=0;
    bool operator==(const ReloadInsertionIdentity&)const=default;
};
enum class ReloadInsertionItemSource:std::uint8_t {Ammunition,RetainedMagazine};
struct ReloadInsertionSample {
    ReloadInsertionIdentity identity{};
    HandInteractionKey profile{}; // id and revision, not an asset guess
    HandClaim itemClaim{},weaponClaim{}; // Current results from HandInteraction
    std::uint64_t sequence=0,geometrySequence=0;
    std::int64_t observedNs=0,deadlineNs=0,nowNs=0;
    bool focused=false,itemTracked=false,weaponTracked=false,held=false;
    // Adapter confirms exact resource/socket compatibility and access state.
    // This is not permission to dispatch a native operation or grant ammo.
    bool eligible=false,cancel=false;
    math::Matrix4 weaponFromHand{}; // Raw coherent hand pose, never guided output
    // A retained magazine is a Mechanism owned through this exact GunHold. It
    // never becomes AmmoSupply or credits reserve; the adapter proves return.
    ReloadInsertionItemSource itemSource=ReloadInsertionItemSource::Ammunition;
};
enum class ReloadInsertionPhase : std::uint8_t {Free,Guided,Seated};
enum class ReloadInsertionReason : std::uint8_t {
    None,InvalidProfile,InvalidSample,IdentityChanged,OwnershipLost,LeaseExpired,
    TrackingLost,Released,Ineligible,Explicit,ClockReversed,SequenceRollback,
    StaleInput,InvalidGeometry,PoseJump,Withdrawn,ContactLost,Timeout,TokenExhausted
};
enum class ReloadInsertionHaptic : std::uint8_t {None,Seated};
struct ReloadInsertionSeat {
    std::uint64_t id=0,inputSequence=0;
    ReloadInsertionIdentity identity{};
    HandInteractionKey profile{};
    HandClaimToken itemClaim{},weaponClaim{};
    ReloadOperation operation=ReloadOperation::None;
};
struct ReloadInsertionResult {
    ReloadInsertionPhase phase=ReloadInsertionPhase::Free;
    ReloadInsertionReason reason=ReloadInsertionReason::None;
    bool captured=false,cancelled=false;
    float progress=0,alignment=0;
    std::optional<math::Matrix4> rawItem{},guidedItem{},guidedHand{};
    std::optional<ReloadInsertionSeat> seat{}; // Physical candidate, NEVER native ack
    ReloadInsertionHaptic haptic=ReloadInsertionHaptic::None;
};
namespace reload_insertion_detail {
inline bool Key(HandInteractionKey k)noexcept{return k.id&&k.generation;}
inline bool Owner(HandInteractionOwner o)noexcept{return o.actor&&o.actorGeneration&&o.equipGeneration&&o.space;}
inline bool Rigid(const math::Matrix4& m)noexcept {
    const auto& a=m.values;
    for(const auto& row:a)for(float v:row)if(!std::isfinite(v))return false;
    for(unsigned i=0;i<4;++i)if(std::abs(a[i][3]-(i==3?1.f:0.f))>1e-5f)return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=i;j<3;++j){double d=0;for(unsigned k=0;k<3;++k)d+=double(a[i][k])*a[j][k];if(std::abs(d-(i==j?1.:0.))>1e-4)return false;}
    const double det=double(a[0][0])*(double(a[1][1])*a[2][2]-double(a[1][2])*a[2][1])-
        double(a[0][1])*(double(a[1][0])*a[2][2]-double(a[1][2])*a[2][0])+
        double(a[0][2])*(double(a[1][0])*a[2][1]-double(a[1][1])*a[2][0]);
    return det>0;
}
inline float Angle(const math::Matrix4& a,const math::Matrix4& b)noexcept {
    double trace=0;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)trace+=double(a.values[i][j])*b.values[i][j];
    return float(std::acos(std::clamp((trace-1)*.5,-1.,1.)));
}
inline math::Matrix4 Identity()noexcept {math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
inline float AlignmentAngle(const math::Matrix4& raw,ReloadInsertionOrientation mode)noexcept {
    // Authored transform round trips can perturb axis length slightly. atan2
    // measures its direction without turning a tiny length error into a swing.
    return mode==ReloadInsertionOrientation::AxialSymmetry?float(std::atan2(std::hypot(double(raw.values[2][0]),double(raw.values[2][1])),double(raw.values[2][2]))):Angle(raw,Identity());
}
// Minimal swing carries the item's +Z to rail +Z while retaining its axial
// roll. This is permitted only for an explicitly rotationally symmetric item.
inline math::Matrix4 AlignSymmetric(const math::Matrix4& raw,float blend)noexcept {
    if(blend<=0)return raw;
    const double angle=AlignmentAngle(raw,ReloadInsertionOrientation::AxialSymmetry);
    double x=raw.values[2][1],y=-raw.values[2][0],norm=std::hypot(x,y);
    if(angle<1e-6)return raw;
    // At the antiparallel singularity, use the item's own X axis for a stable
    // half-turn. Ordinary small-angle axial alignment keeps the same math.
    if(norm<1e-8){x=raw.values[0][0];y=raw.values[0][1];norm=std::hypot(x,y);}
    const double a=x/norm,b=y/norm,t=angle*blend,c=std::cos(t),s=std::sin(t),v=1-c;
    auto correction=Identity();
    correction.values[0][0]=float(c+a*a*v);correction.values[0][1]=float(a*b*v);correction.values[0][2]=float(-b*s);
    correction.values[1][0]=float(a*b*v);correction.values[1][1]=float(c+b*b*v);correction.values[1][2]=float(a*s);
    correction.values[2][0]=float(b*s);correction.values[2][1]=float(-a*s);correction.values[2][2]=float(c);
    auto out=Multiply(raw,correction);out.values[3]=raw.values[3];return out;
}
inline float Distance(const math::Matrix4& a,const math::Matrix4& b)noexcept {
    double d=0;for(unsigned i=0;i<3;++i){const double v=double(a.values[3][i])-b.values[3][i];d+=v*v;}return float(std::sqrt(d));
}
inline double TravelCoordinate(const math::Matrix4& raw,const ReloadInsertionProfile& p)noexcept {
    double along=0;for(unsigned n=0;n<3;++n)along+=double(raw.values[3][n])*p.travelDirection[n];return along;
}
inline double TravelRadial(const math::Matrix4& raw,const ReloadInsertionProfile& p)noexcept {
    const auto along=TravelCoordinate(raw,p);double squared=0;
    for(unsigned n=0;n<3;++n){const double lateral=raw.values[3][n]-along*p.travelDirection[n];squared+=lateral*lateral;}
    return std::sqrt(squared);
}
inline math::Matrix4 TravelPose(const ReloadInsertionProfile& p,float distance)noexcept {
    auto pose=Identity();for(unsigned n=0;n<3;++n)pose.values[3][n]=p.travelDirection[n]*distance;return pose;
}
inline bool EntrySideAllowed(const math::Matrix4& raw,const ReloadInsertionProfile& p)noexcept {
    return p.approach==ReloadInsertionApproach::EntrySphere||p.approach==ReloadInsertionApproach::RailContact||TravelCoordinate(raw,p)<=0;
}
inline double CaptureDistance(const math::Matrix4& raw,const ReloadInsertionProfile& p)noexcept {
    const double along=p.approach==ReloadInsertionApproach::RailContact?std::clamp(TravelCoordinate(raw,p),0.,double(p.travelMeters)):0;
    double square=0;for(unsigned n=0;n<3;++n){const double d=raw.values[3][n]-along*p.travelDirection[n];square+=d*d;}return std::sqrt(square);
}
inline bool CaptureGeometry(const math::Matrix4& raw,const ReloadInsertionProfile& p)noexcept {
    return EntrySideAllowed(raw,p)&&CaptureDistance(raw,p)<=p.captureDistanceMeters&&
        ((p.approach==ReloadInsertionApproach::RailContact&&p.orientation==ReloadInsertionOrientation::AxialSymmetry)||
         AlignmentAngle(raw,p.orientation)<=p.captureAngleRadians);
}
inline bool OutsideEntryApproach(const math::Matrix4& raw,const ReloadInsertionProfile& p)noexcept {
    const auto& v=raw.values[3];
    if(p.approach==ReloadInsertionApproach::RailContact)return CaptureDistance(raw,p)>p.captureDistanceMeters;
    if(p.approach==ReloadInsertionApproach::EntrySphere)return std::hypot(double(v[0]),double(v[1]),double(v[2]))>p.captureDistanceMeters;
    return TravelCoordinate(raw,p)<-p.captureDistanceMeters||
        (p.approach==ReloadInsertionApproach::FrontHemisphere&&TravelCoordinate(raw,p)<=0&&
         std::hypot(double(v[0]),double(v[1]),double(v[2]))>p.captureDistanceMeters);
}
// Shortest rotation to rail identity. The admitted cone is strictly <90deg,
// avoiding the ambiguous 180deg axis. Translation is handled independently.
inline math::Matrix4 Align(const math::Matrix4& raw,float blend)noexcept {
    if(blend<=0)return raw;
    auto out=Identity();const auto& r=raw.values;const float angle=Angle(raw,out);
    if(blend<1&&angle>1e-5f){
        const double d=2*std::sin(double(angle));
        const double x=(r[1][2]-r[2][1])/d,y=(r[2][0]-r[0][2])/d,z=(r[0][1]-r[1][0])/d;
        const double norm=std::sqrt(x*x+y*y+z*z);
        if(norm>1e-8){const double a=x/norm,b=y/norm,c=z/norm,t=angle*(1-blend),co=std::cos(t),si=std::sin(t),v=1-co;
            out.values[0][0]=float(co+a*a*v);out.values[0][1]=float(a*b*v+c*si);out.values[0][2]=float(a*c*v-b*si);
            out.values[1][0]=float(a*b*v-c*si);out.values[1][1]=float(co+b*b*v);out.values[1][2]=float(b*c*v+a*si);
            out.values[2][0]=float(a*c*v+b*si);out.values[2][1]=float(b*c*v-a*si);out.values[2][2]=float(co+c*c*v);
        }
    }
    for(unsigned i=0;i<3;++i)out.values[3][i]=r[3][i];return out;
}
}
[[nodiscard]] inline bool ValidateReloadInsertionProfile(const ReloadInsertionProfile& p)noexcept {
    using namespace reload_insertion_detail;
    if(!p.id||!p.revision||(p.family!=ReloadInsertionFamily::Magazine&&p.family!=ReloadInsertionFamily::SingleShell)||
       !Rigid(p.itemFromHand)||!Rigid(p.itemFromInsertion)||!Rigid(p.weaponFromEntry)||
       (p.orientation!=ReloadInsertionOrientation::Keyed&&p.orientation!=ReloadInsertionOrientation::AxialSymmetry)||
       (p.approach!=ReloadInsertionApproach::AxialFront&&p.approach!=ReloadInsertionApproach::FrontHemisphere&&p.approach!=ReloadInsertionApproach::EntrySphere&&p.approach!=ReloadInsertionApproach::RailContact)||
       (p.approach!=ReloadInsertionApproach::AxialFront&&p.approach!=ReloadInsertionApproach::RailContact&&p.family!=ReloadInsertionFamily::SingleShell)||
       (p.approach==ReloadInsertionApproach::EntrySphere&&p.captureDistanceMeters>=p.travelMeters-2*p.seatToleranceMeters)||
       (p.orientation==ReloadInsertionOrientation::AxialSymmetry&&p.family!=ReloadInsertionFamily::SingleShell))return false;
    if(!std::isfinite(p.postCaptureTravelMeters)||p.postCaptureTravelMeters<0)return false;
    if(p.approach==ReloadInsertionApproach::RailContact){
        if((p.family==ReloadInsertionFamily::SingleShell?p.orientation!=ReloadInsertionOrientation::AxialSymmetry:p.orientation!=ReloadInsertionOrientation::Keyed)||
           p.postCaptureTravelMeters<=2*p.seatToleranceMeters||p.postCaptureTravelMeters>=p.travelMeters)return false;
    }else if(p.postCaptureTravelMeters!=0)return false;
    for(float v:{p.travelMeters,p.captureDistanceMeters,p.releaseDistanceMeters,p.captureAngleRadians,p.releaseAngleRadians,p.seatToleranceMeters,p.maxStepMeters,p.maxStepRadians})
        if(!std::isfinite(v)||v<=0)return false;
    double travelNorm=0;for(float v:p.travelDirection){if(!std::isfinite(v))return false;travelNorm+=double(v)*v;}
    if(std::abs(travelNorm-1)>1e-5)return false;
    return p.releaseDistanceMeters>p.captureDistanceMeters&&p.releaseAngleRadians>p.captureAngleRadians&&
        p.releaseAngleRadians<1.570796327f&&p.seatToleranceMeters<p.travelMeters*.25f&&
        p.maxStepMeters<p.travelMeters&&p.maxStepRadians<1.570796327f&&p.alignmentNs>0&&
        p.seatDwellNs>=0&&p.maxSampleGapNs>0&&p.maxGuidedNs>p.alignmentNs&&p.seatDwellNs<p.maxGuidedNs-p.alignmentNs;
}
// Geometry/presentation recognizer only. One serialized instance per insertion
// domain; immutable profile; do not copy/recreate it to reset event IDs. The
// adapter retains resource authority and exact ManualReload command/ack gating.
class ReloadInsertion {
public:
    explicit ReloadInsertion(ReloadInsertionProfile profile)noexcept:profile_(profile){}
    ReloadInsertion(const ReloadInsertion&)=delete;
    ReloadInsertion& operator=(const ReloadInsertion&)=delete;
    ReloadInsertionResult Update(const ReloadInsertionSample&)noexcept;
    ReloadInsertionResult Reset()noexcept {auto r=Cancel(ReloadInsertionReason::Explicit);initialized_=false;return r;}
    // Only after explicit local reset/retirement. Seat IDs remain monotonic.
    bool Reconfigure(ReloadInsertionProfile profile)noexcept {
        if(phase_!=ReloadInsertionPhase::Free||!ValidateReloadInsertionProfile(profile))return false;
        Reset();profile_=profile;return true;
    }
private:
    ReloadInsertionResult Cancel(ReloadInsertionReason reason)noexcept {
        const bool active=phase_!=ReloadInsertionPhase::Free;phase_=ReloadInsertionPhase::Free;
        armed_=rawHistory_=false;originalMinimumAlong_.reset();captureNs_=seatNs_=0;captureAlong_=captureOffset_=0;cached_={};cached_.reason=reason;cached_.cancelled=active;return cached_;
    }
    ReloadInsertionResult Snapshot()const noexcept {
        auto r=cached_;r.captured=r.cancelled=false;r.reason=ReloadInsertionReason::None;r.seat.reset();r.haptic=ReloadInsertionHaptic::None;return r;
    }
    void Adopt(const ReloadInsertionSample& s)noexcept {
        identity_=s.identity;itemToken_=s.itemClaim.token;weaponToken_=s.weaponClaim.token;
        sequence_=s.sequence;lastNow_=s.nowNs;lastObserved_=s.observedNs;initialized_=true;
    }
    bool Claims(const ReloadInsertionSample& s)const noexcept;
    ReloadInsertionProfile profile_{};ReloadInsertionIdentity identity_{};
    HandClaimToken itemToken_{},weaponToken_{};
    ReloadInsertionPhase phase_=ReloadInsertionPhase::Free;ReloadInsertionResult cached_{};
    math::Matrix4 previousRaw_{};
    std::uint64_t sequence_=0,nextSeat_=0;
    std::int64_t lastNow_=0,lastObserved_=0,captureNs_=0,seatNs_=0;
    double captureAlong_=0,captureOffset_=0;
    std::optional<double> originalMinimumAlong_;
    bool initialized_=false,armed_=false,rawHistory_=false;
};
inline bool ReloadInsertion::Claims(const ReloadInsertionSample& s)const noexcept {
    using namespace reload_insertion_detail;
    const auto& item=s.itemClaim.token;const auto& weapon=s.weaponClaim.token;
    const auto hand=[](InteractionHand h){return h==InteractionHand::Left||h==InteractionHand::Right;};
    const bool source=s.itemSource==ReloadInsertionItemSource::Ammunition?
        item.kind==HandClaimKind::AmmoObject&&!item.prerequisiteClaim&&item.item==s.identity.item:
        s.itemSource==ReloadInsertionItemSource::RetainedMagazine&&profile_.family==ReloadInsertionFamily::Magazine&&
        item.kind==HandClaimKind::Mechanism&&item.prerequisiteClaim==weapon.id&&item.item==s.identity.weapon;
    return source&&item.id&&weapon.id&&item.id!=weapon.id&&item.owner==s.identity.owner&&weapon.owner==s.identity.owner&&
        weapon.kind==HandClaimKind::GunHold&&
        weapon.item==s.identity.weapon&&Key(item.contact)&&Key(weapon.contact)&&
        hand(item.hand)&&hand(weapon.hand)&&item.hand!=weapon.hand&&!weapon.prerequisiteClaim&&
        s.itemClaim.inputSequence&&s.itemClaim.inputSequence<=s.sequence&&s.weaponClaim.inputSequence&&s.weaponClaim.inputSequence<=s.sequence;
}
inline ReloadInsertionResult ReloadInsertion::Update(const ReloadInsertionSample& s)noexcept {
    using namespace reload_insertion_detail;
    if(!ValidateReloadInsertionProfile(profile_))return Cancel(ReloadInsertionReason::InvalidProfile);
    if(!Owner(s.identity.owner)||!Key(s.identity.weapon)||!Key(s.identity.item)||s.identity.weapon==s.identity.item||
       !s.identity.trackingEpoch||s.profile!=HandInteractionKey{profile_.id,profile_.revision}||!s.sequence||
       s.geometrySequence!=s.sequence||s.observedNs<=0||s.nowNs<s.observedNs||s.deadlineNs<s.observedNs)
        return Cancel(ReloadInsertionReason::InvalidSample);
    if(s.deadlineNs<=s.nowNs||s.deadlineNs-s.observedNs>profile_.maxSampleGapNs)return Cancel(ReloadInsertionReason::StaleInput);
    if(initialized_&&(s.identity!=identity_||s.itemClaim.token!=itemToken_||s.weaponClaim.token!=weaponToken_)){
        auto r=Cancel(ReloadInsertionReason::IdentityChanged);Adopt(s);return r;
    }
    if(!Claims(s))return Cancel(ReloadInsertionReason::OwnershipLost);
    if(s.itemClaim.deadlineNs<=s.nowNs||s.weaponClaim.deadlineNs<=s.nowNs)return Cancel(ReloadInsertionReason::LeaseExpired);
    if(!s.focused||!s.itemTracked||!s.weaponTracked)return Cancel(ReloadInsertionReason::TrackingLost);
    if(!s.held)return Cancel(ReloadInsertionReason::Released);
    if(!s.eligible)return Cancel(ReloadInsertionReason::Ineligible);
    if(s.cancel)return Cancel(ReloadInsertionReason::Explicit);
    bool fresh=true;
    if(initialized_){
        if(s.nowNs<lastNow_||s.observedNs<lastObserved_)return Cancel(ReloadInsertionReason::ClockReversed);
        lastNow_=s.nowNs;
        if(s.sequence<sequence_)return Cancel(ReloadInsertionReason::SequenceRollback);
        fresh=s.sequence>sequence_;
        if(!fresh&&s.observedNs!=lastObserved_)return Cancel(ReloadInsertionReason::InvalidSample);
        if(s.nowNs-lastObserved_>profile_.maxSampleGapNs){auto r=Cancel(ReloadInsertionReason::StaleInput);Adopt(s);return r;}
        if(fresh){sequence_=s.sequence;lastObserved_=s.observedNs;}
    }else Adopt(s);
    if(phase_!=ReloadInsertionPhase::Free&&s.nowNs-captureNs_>profile_.maxGuidedNs)return Cancel(ReloadInsertionReason::Timeout);
    if(!fresh)return Snapshot(); // Safety can cancel duplicates; geometry/seat cannot advance.
    if(!Rigid(s.weaponFromHand))return Cancel(ReloadInsertionReason::InvalidGeometry);
    const auto rawItem=Multiply(*InverseRigid(profile_.itemFromHand),s.weaponFromHand);
    const auto raw=Multiply(Multiply(profile_.itemFromInsertion,rawItem),*InverseRigid(profile_.weaponFromEntry));
    if(!Rigid(rawItem)||!Rigid(raw))return Cancel(ReloadInsertionReason::InvalidGeometry);
    if(rawHistory_&&(Distance(raw,previousRaw_)>profile_.maxStepMeters||Angle(raw,previousRaw_)>profile_.maxStepRadians))
        return Cancel(ReloadInsertionReason::PoseJump);
    previousRaw_=raw;rawHistory_=true;
    const auto& p=raw.values[3];const float angle=AlignmentAngle(raw,profile_.orientation);
    const double along=TravelCoordinate(raw,profile_),radial=TravelRadial(raw,profile_);
    const bool assisted=profile_.approach==ReloadInsertionApproach::RailContact;
    auto r=Snapshot();r.rawItem=rawItem;r.guidedItem.reset();r.guidedHand.reset();
    if(phase_==ReloadInsertionPhase::Free){
        r.progress=r.alignment=0;
        // Legacy modes still need observed outside motion. Front-only modes
        // additionally require their entry hemisphere; RailContact instead uses
        // explicit item ownership plus new post-capture motion to prevent seat.
        if(OutsideEntryApproach(raw,profile_))armed_=true;
        if(!EntrySideAllowed(raw,profile_))armed_=false;
        // A claimed RailContact item may begin inside the assist volume:
        // native arming can finish after the player has reached the mouth.
        // Its post-capture displacement requirement still forbids automatic seat.
        // An original starts inside the well. Outward removal is not insertion.
        // Allow return after leaving the volume OR a measured inward reversal
        // by the profile's existing post-capture stroke; no mandatory far pull.
        // Replacement ammunition retains the intentional inside-volume catch.
        const bool needsWithdrawal=s.itemSource==ReloadInsertionItemSource::RetainedMagazine;
        if(needsWithdrawal&&assisted){
            originalMinimumAlong_=originalMinimumAlong_?std::min(*originalMinimumAlong_,along):along;
            if(along-*originalMinimumAlong_>=profile_.postCaptureTravelMeters-1e-6)armed_=true;
        }
        if(((!assisted||needsWithdrawal)&&!armed_)||!CaptureGeometry(raw,profile_)){cached_=r;return r;}
        phase_=ReloadInsertionPhase::Guided;captureNs_=s.observedNs;seatNs_=0;armed_=false;r.captured=true;
        captureAlong_=along;
        // A catch near the terminal mouth is staged short of the seat, smoothly
        // over alignment. Actual inward motion advances it; time alone cannot.
        captureOffset_=assisted?std::max(0.,along-(profile_.travelMeters-profile_.postCaptureTravelMeters)):0;
    }
    const double insertionAlong=along-captureOffset_;
    if(along<-profile_.releaseDistanceMeters)return Cancel(ReloadInsertionReason::Withdrawn);
    if(radial>profile_.releaseDistanceMeters||((!assisted||profile_.orientation==ReloadInsertionOrientation::Keyed)&&angle>profile_.releaseAngleRadians)||along>profile_.travelMeters+profile_.releaseDistanceMeters)
        return Cancel(ReloadInsertionReason::ContactLost);
    if(phase_==ReloadInsertionPhase::Seated&&insertionAlong<profile_.travelMeters-2*profile_.seatToleranceMeters)return Cancel(ReloadInsertionReason::Withdrawn);
    const auto elapsed=s.observedNs-captureNs_;
    const float t=float(std::clamp(double(elapsed)/double(profile_.alignmentNs),0.,1.));
    r.alignment=t*t*(3-2*t);r.progress=float(std::clamp(insertionAlong/profile_.travelMeters,0.,1.));
    const bool travelled=!assisted||along-captureAlong_>=profile_.postCaptureTravelMeters-1e-6;
    if(phase_==ReloadInsertionPhase::Guided){
        if(r.alignment==1&&travelled&&insertionAlong>=profile_.travelMeters-profile_.seatToleranceMeters){if(!seatNs_)seatNs_=s.observedNs;}
        else if(r.alignment<1||!travelled||insertionAlong<profile_.travelMeters-2*profile_.seatToleranceMeters)seatNs_=0;
        if(seatNs_&&s.observedNs-seatNs_>=profile_.seatDwellNs){
            if(nextSeat_==std::numeric_limits<std::uint64_t>::max())return Cancel(ReloadInsertionReason::TokenExhausted);
            phase_=ReloadInsertionPhase::Seated;
            r.seat=ReloadInsertionSeat{++nextSeat_,s.sequence,s.identity,s.profile,s.itemClaim.token,s.weaponClaim.token,
                profile_.family==ReloadInsertionFamily::Magazine?ReloadOperation::SeatMagazine:ReloadOperation::InsertRound};
            r.haptic=ReloadInsertionHaptic::Seated;
        }
    }
    auto guided=profile_.orientation==ReloadInsertionOrientation::AxialSymmetry?AlignSymmetric(raw,r.alignment):Align(raw,r.alignment);
    const double targetAlong=phase_==ReloadInsertionPhase::Seated?profile_.travelMeters:std::clamp(insertionAlong,-double(profile_.captureDistanceMeters),double(profile_.travelMeters));
    const double guidedAlong=assisted?along+(targetAlong-along)*r.alignment:targetAlong;
    for(unsigned n=0;n<3;++n)guided.values[3][n]=float(profile_.travelDirection[n]*guidedAlong+(p[n]-profile_.travelDirection[n]*along)*(1-r.alignment));
    if(phase_==ReloadInsertionPhase::Seated)r.progress=1;
    r.guidedItem=Multiply(*InverseRigid(profile_.itemFromInsertion),Multiply(guided,profile_.weaponFromEntry));
    r.guidedHand=Multiply(profile_.itemFromHand,*r.guidedItem);r.phase=phase_;
    cached_=r;return r;
}
}
