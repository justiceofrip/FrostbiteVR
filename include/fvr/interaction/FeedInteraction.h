#pragma once
#include "fvr/interaction/FeedReload.h"
#include <limits>

namespace fvr::interaction {
enum class FeedMotionKind:std::uint8_t {Unknown,Linear,Hinge,FreePlacement};
struct FeedPlacementGeometry {
    // Explicit measured object poses and grip frame, proper rigid row-vector
    // canonical METRES. No default source, destination, or grip is usable.
    // sourceFromGrip * weaponFromSource is the source anatomical contact.
    math::Matrix4 weaponFromSource{},weaponFromDestination{},sourceFromGrip{};
};
// An adapter must explicitly supply measured geometry for each operation. No
// weapon name, default rail, or default hinge establishes physical eligibility.
struct FeedMotionProfile {
    FeedPhysicalOperation operation=FeedPhysicalOperation::None;
    FeedMotionKind kind=FeedMotionKind::Unknown;
    bool measured=false,returnToStart=false;
    // Proper rigid row-vector frames, in METRES. Motion-local +X is a linear
    // rail; motion-local +Z is the hinge axis through the frame origin.
    math::Matrix4 weaponFromMotion{},motionFromContact{};
    float travel=0; // Signed metres (linear) or signed radians (hinge).
    float completeFraction=0,releaseFraction=0;
    float pathToleranceMeters=0,orientationToleranceRadians=0;
    float maxStepMeters=0,maxStepRadians=0;
    std::int64_t endpointDwellNs=0,maximumGestureNs=0,maxInputGapNs=0;
    // FreePlacement uses these frames instead of rail/hinge geometry, travel,
    // fractions or returnToStart. Path/orientation tolerances bound source
    // contact agreement and destination alignment, never the free carry path.
    FeedPlacementGeometry placement{};
};
struct FeedInteractionProfile {
    std::uint64_t profileId=0,profileRevision=0;
    std::array<FeedMotionProfile,10> operations{};
};
struct FeedInteractionSample {
    FeedContactSample contact{};
    // The adapter identifies the actual hand and tracking lifetime and has
    // already arbitrated handAvailable. A changed hand cannot inherit a grasp.
    FeedKey hand{};std::uint64_t trackingGeneration=0;
    bool focused=false,grip=false,cancel=false;
    // Caller excludes a pending request/applied-wait from prior intent. False
    // interrupts ONLY this transient gesture; it cannot cancel accepted work.
    bool operationPermitted=false;
};
enum class FeedInteractionStatus:std::uint8_t {
    NeedNeutral,Armed,Acquired,Moving,Endpoint,Returning,Completed,Duplicate,
    InvalidProfile,InvalidInput,CandidateChanged,ContactMiss,Interrupted,
    Expired,OffPath,MotionJump,CounterExhausted
};
enum class FeedInteractionTargetKind:std::uint8_t {None,Contact,Object};
struct FeedInteractionResult {
    FeedInteractionStatus status=FeedInteractionStatus::NeedNeutral;
    FeedMechanismStatus evidence=FeedMechanismStatus::InvalidSnapshot;
    double progress=0;
    std::optional<math::Matrix4> target{}; // Presentation only, never native state.
    std::optional<FeedPhysicalCompletion> completion{};
    // Linear/hinge target is an anatomical contact; free placement target is
    // the carried object. Adapters must not apply an object pose to the hand.
    FeedInteractionTargetKind targetKind=FeedInteractionTargetKind::None;
};
namespace feed_interaction_detail {
inline math::Matrix4 Multiply(const math::Matrix4& a,const math::Matrix4& b)noexcept {
    math::Matrix4 out{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){
        double sum=0;for(unsigned k=0;k<4;++k)sum+=double(a.values[i][k])*b.values[k][j];
        out.values[i][j]=static_cast<float>(sum);
    }return out;
}
inline math::Matrix4 Inverse(const math::Matrix4& m)noexcept {
    math::Matrix4 out{};out.values[3][3]=1;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out.values[i][j]=m.values[j][i];
    for(unsigned j=0;j<3;++j){double sum=0;for(unsigned k=0;k<3;++k)sum-=double(m.values[3][k])*out.values[k][j];
        out.values[3][j]=static_cast<float>(sum);}
    return out;
}
inline double Distance(const math::Matrix4& a,const math::Matrix4& b)noexcept {
    double sum=0;for(unsigned k=0;k<3;++k){const double d=double(a.values[3][k])-b.values[3][k];sum+=d*d;}
    return std::sqrt(sum);
}
inline double Angle(const math::Matrix4& a,const math::Matrix4& b)noexcept {
    double trace=0;for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)trace+=double(a.values[i][j])*b.values[i][j];
    return std::acos(std::clamp((trace-1)*.5,-1.,1.));
}
inline math::Matrix4 Along(const math::Matrix4& start,FeedMotionKind kind,double travel)noexcept {
    if(kind==FeedMotionKind::Linear){auto out=start;out.values[3][0]+=static_cast<float>(travel);return out;}
    math::Matrix4 turn{};const auto c=static_cast<float>(std::cos(travel)),s=static_cast<float>(std::sin(travel));
    turn.values[0]={c,s,0,0};turn.values[1]={-s,c,0,0};turn.values[2][2]=turn.values[3][3]=1;
    return Multiply(start,turn);
}
inline bool Same(const FeedPhysicalCandidate& a,FeedPhysicalCandidate b)noexcept {
    b.snapshotSequence=a.snapshotSequence;return a==b;
}
inline bool Same(const FeedMotionProfile& a,const FeedMotionProfile& b)noexcept {
    return a.operation==b.operation&&a.kind==b.kind&&a.measured==b.measured&&a.returnToStart==b.returnToStart&&
        a.weaponFromMotion.values==b.weaponFromMotion.values&&a.motionFromContact.values==b.motionFromContact.values&&
        a.travel==b.travel&&a.completeFraction==b.completeFraction&&a.releaseFraction==b.releaseFraction&&
        a.pathToleranceMeters==b.pathToleranceMeters&&a.orientationToleranceRadians==b.orientationToleranceRadians&&
        a.maxStepMeters==b.maxStepMeters&&a.maxStepRadians==b.maxStepRadians&&a.endpointDwellNs==b.endpointDwellNs&&
        a.maximumGestureNs==b.maximumGestureNs&&a.maxInputGapNs==b.maxInputGapNs&&
        a.placement.weaponFromSource.values==b.placement.weaponFromSource.values&&
        a.placement.weaponFromDestination.values==b.placement.weaponFromDestination.values&&
        a.placement.sourceFromGrip.values==b.placement.sourceFromGrip.values;
}
inline bool Same(const FeedMechanismProfile& a,const FeedMechanismProfile& b)noexcept {
    if(a.family!=b.family||a.id!=b.id||a.revision!=b.revision||a.ammunitionKind!=b.ammunitionKind||
       a.cover!=b.cover||a.latch!=b.latch||a.container!=b.container||a.charge!=b.charge||
       a.containerNeedsOpen!=b.containerNeedsOpen||a.feedNeedsOpen!=b.feedNeedsOpen||
       a.detachNeedsUnseated!=b.detachNeedsUnseated||a.maxAgeNs!=b.maxAgeNs)return false;
    for(unsigned i=0;i<a.contacts.size();++i)if(a.contacts[i].role!=b.contacts[i].role||
       a.contacts[i].radiusMeters!=b.contacts[i].radiusMeters||a.contacts[i].maxAngleRadians!=b.contacts[i].maxAngleRadians)return false;
    return true;
}
inline bool Motion(const FeedMotionProfile& m)noexcept {
    const auto op=static_cast<unsigned>(m.operation);
    if(m.kind==FeedMotionKind::FreePlacement){
        if(!m.measured||m.returnToStart||
           (m.operation!=FeedPhysicalOperation::AttachContainer&&m.operation!=FeedPhysicalOperation::DetachContainer&&
            m.operation!=FeedPhysicalOperation::SeatFeed&&m.operation!=FeedPhysicalOperation::WithdrawFeed)||
           !feed_mechanism_detail::Pose(m.placement.weaponFromSource)||
           !feed_mechanism_detail::Pose(m.placement.weaponFromDestination)||
           !feed_mechanism_detail::Pose(m.placement.sourceFromGrip))return false;
        for(float v:{m.pathToleranceMeters,m.orientationToleranceRadians,m.maxStepMeters,m.maxStepRadians})
            if(!std::isfinite(v))return false;
        if(m.pathToleranceMeters<=0||m.orientationToleranceRadians<0||m.orientationToleranceRadians>=1.570796327f||
           m.maxStepMeters<=0||m.maxStepRadians<=0||m.maxStepRadians>=3.141592654f||m.endpointDwellNs<=0||
           m.maximumGestureNs<=m.endpointDwellNs||m.maxInputGapNs<=0||m.maxInputGapNs>m.maximumGestureNs)return false;
        // Source and destination acceptance regions must be distinct, and a
        // single source-to-destination teleport must exceed a step bound.
        const auto distance=Distance(m.placement.weaponFromSource,m.placement.weaponFromDestination);
        const auto angle=Angle(m.placement.weaponFromSource,m.placement.weaponFromDestination);
        return (distance>2*m.pathToleranceMeters||angle>2*m.orientationToleranceRadians)&&
            (distance>m.maxStepMeters||angle>m.maxStepRadians)&&
            feed_mechanism_detail::Pose(Multiply(m.placement.sourceFromGrip,m.placement.weaponFromSource));
    }
    if(op==0||op>9||!m.measured||(m.kind!=FeedMotionKind::Linear&&m.kind!=FeedMotionKind::Hinge)||
       !feed_mechanism_detail::Pose(m.weaponFromMotion)||!feed_mechanism_detail::Pose(m.motionFromContact))return false;
    for(float v:{m.travel,m.completeFraction,m.releaseFraction,m.pathToleranceMeters,m.orientationToleranceRadians,
                 m.maxStepMeters,m.maxStepRadians})if(!std::isfinite(v))return false;
    if(m.travel==0||m.completeFraction<=.5f||m.completeFraction>1||m.releaseFraction<=0||
       m.releaseFraction>=m.completeFraction||m.pathToleranceMeters<=0||m.orientationToleranceRadians<0||
       m.orientationToleranceRadians>=1.570796327f||m.maxStepMeters<=0||m.maxStepRadians<=0||
       m.maxStepRadians>=3.141592654f||m.endpointDwellNs<=0||m.maximumGestureNs<=m.endpointDwellNs||
       m.maxInputGapNs<=0||m.maxInputGapNs>m.maximumGestureNs)return false;
    if(m.kind==FeedMotionKind::Linear){
        if(m.pathToleranceMeters>=std::abs(m.travel)*m.releaseFraction||m.maxStepMeters>=std::abs(m.travel))return false;
    }else {
        const double x=m.motionFromContact.values[3][0],y=m.motionFromContact.values[3][1];
        if(std::abs(m.travel)>=3.141592654f||m.maxStepRadians>=std::abs(m.travel)||
           std::hypot(x,y)<=m.pathToleranceMeters*2)return false;
    }
    return feed_mechanism_detail::Pose(Multiply(m.motionFromContact,m.weaponFromMotion))&&
        feed_mechanism_detail::Pose(Multiply(Along(m.motionFromContact,m.kind,m.travel),m.weaponFromMotion));
}
}
// Pure gesture policy. It neither changes a FeedMechanismSnapshot nor dispatches
// native work. Keep this instance alive for the associated FeedReload lifetime:
// receipt IDs never reset on interruption, rearming, or ownership changes.
class FeedInteraction {
public:
    void Interrupt()noexcept {Drop();}
    [[nodiscard]] FeedInteractionResult Update(const FeedMechanismProfile& mechanismProfile,
        const FeedInteractionProfile& profile,const FeedMechanismSnapshot& snapshot,FeedSupplyKey desired,
        const FeedInteractionSample& sample,std::int64_t nowNs)noexcept {
        using namespace feed_interaction_detail;
        FeedInteractionResult out;
        const auto fail=[&](FeedInteractionStatus why){
            Drop();out.status=why;out.target.reset();out.completion.reset();out.targetKind=FeedInteractionTargetKind::None;
            const bool sameRejected=hasRejected_&&sample.contact.owner==rejected_.contact.owner&&
                sample.contact.mechanism==rejected_.contact.mechanism&&sample.hand==rejected_.hand&&
                sample.trackingGeneration==rejected_.trackingGeneration;
            if(!sameRejected||sample.contact.inputSequence>=rejected_.contact.inputSequence)rejected_=sample;
            hasRejected_=true;return out;
        };
        if(hasRejected_&&sample.contact.owner==rejected_.contact.owner&&sample.contact.mechanism==rejected_.contact.mechanism&&
           sample.hand==rejected_.hand&&sample.trackingGeneration==rejected_.trackingGeneration&&
           sample.contact.inputSequence<=rejected_.contact.inputSequence)return fail(FeedInteractionStatus::InvalidInput);
        const auto query=EvaluateFeedMechanism(mechanismProfile,snapshot,desired,nowNs);out.evidence=query.status;
        if(!query.candidate)return fail(FeedInteractionStatus::InvalidInput);
        const auto& candidate=*query.candidate;const auto index=static_cast<unsigned>(candidate.operation);
        if(profile.profileId!=candidate.profileId||profile.profileRevision!=candidate.profileRevision||index>=profile.operations.size()||
           profile.operations[index].operation!=candidate.operation||!Motion(profile.operations[index]))return fail(FeedInteractionStatus::InvalidProfile);
        const auto& motion=profile.operations[index];const auto& contact=sample.contact;
        const auto checked=EvaluateFeedContact(mechanismProfile,snapshot,desired,candidate,contact,nowNs);out.evidence=checked.status;
        if(checked.status!=FeedMechanismStatus::ContactMatch&&checked.status!=FeedMechanismStatus::ContactMiss)
            return fail(FeedInteractionStatus::InvalidInput);
        if(contact.observedNs<snapshot.observedNs)return fail(FeedInteractionStatus::InvalidInput);
        if(!feed_mechanism_detail::Key(sample.hand)||!sample.trackingGeneration||!sample.focused||sample.cancel||!sample.operationPermitted)
            return fail(FeedInteractionStatus::Interrupted);
        if(nowNs<lastNow_)return fail(FeedInteractionStatus::InvalidInput);
        lastNow_=nowNs;
        if(moving_&&nowNs-started_>motion.maximumGestureNs)return fail(FeedInteractionStatus::Expired);
        const bool sameStream=stream_&&contact.owner==lastOwner_&&contact.mechanism==lastMechanism_&&
            sample.hand==lastHand_&&sample.trackingGeneration==lastTracking_;
        if(sameStream&&(contact.inputSequence<lastInput_||contact.observedNs<lastObserved_||snapshot.sequence<lastSnapshot_))
            return fail(FeedInteractionStatus::InvalidInput);
        if(sameStream&&contact.inputSequence==lastInput_){
            if(snapshot.sequence!=lastSnapshot_||contact.observedNs!=lastObserved_||
               contact.weaponFromHandContact.values!=lastRaw_.values||sample.grip!=lastGrip_)
                return fail(FeedInteractionStatus::InvalidInput);
            if(bound_&&(!Same(candidate_,candidate)||!Same(motion_,motion)||!Same(mechanism_,mechanismProfile)))
                return fail(FeedInteractionStatus::CandidateChanged);
            out.status=FeedInteractionStatus::Duplicate;return out;
        }
        if(sameStream&&snapshot.sequence==lastSnapshot_)return fail(FeedInteractionStatus::InvalidInput);
        const auto previousObserved=lastObserved_;
        stream_=true;lastOwner_=contact.owner;lastMechanism_=contact.mechanism;lastHand_=sample.hand;
        lastTracking_=sample.trackingGeneration;lastInput_=contact.inputSequence;lastObserved_=contact.observedNs;
        lastSnapshot_=snapshot.sequence;lastRaw_=contact.weaponFromHandContact;lastGrip_=sample.grip;
        if(bound_&&(!sameStream||!Same(candidate_,candidate)||!Same(motion_,motion)||!Same(mechanism_,mechanismProfile)))
            return fail(FeedInteractionStatus::CandidateChanged);
        if(bound_&&contact.observedNs-previousObserved>motion.maxInputGapNs)return fail(FeedInteractionStatus::Interrupted);
        if(motion.kind==FeedMotionKind::FreePlacement&&sameStream&&contact.observedNs<=previousObserved)
            return fail(FeedInteractionStatus::InvalidInput);
        if(!moving_){
            if(!sample.grip){
                bound_=true;candidate_=candidate;motion_=motion;mechanism_=mechanismProfile;
                out.status=FeedInteractionStatus::Armed;return out;
            }
            if(!bound_){out.status=FeedInteractionStatus::NeedNeutral;return out;}
            // A missed press consumes neutral; sliding into contact while held
            // cannot become an acquisition edge.
            if(checked.status!=FeedMechanismStatus::ContactMatch)return fail(FeedInteractionStatus::ContactMiss);
            const bool placement=motion.kind==FeedMotionKind::FreePlacement;
            const auto start=placement?Multiply(motion.placement.sourceFromGrip,motion.placement.weaponFromSource):
                Multiply(motion.motionFromContact,motion.weaponFromMotion);
            const auto& observed=snapshot.contacts[static_cast<unsigned>(candidate.part)].weaponFromContact;
            const auto& binding=mechanismProfile.contacts[static_cast<unsigned>(candidate.part)];
            if(Distance(start,observed)>motion.pathToleranceMeters||Angle(start,observed)>motion.orientationToleranceRadians||
               Distance(start,contact.weaponFromHandContact)>binding.radiusMeters||Angle(start,contact.weaponFromHandContact)>binding.maxAngleRadians)
                return fail(FeedInteractionStatus::OffPath);
            // Preserve the actual pickup offset: neither hand nor carried
            // object jumps to the measured grip or destination socket.
            grabbed_=placement?Multiply(motion.placement.weaponFromSource,Inverse(contact.weaponFromHandContact)):
                Multiply(contact.weaponFromHandContact,Inverse(motion.weaponFromMotion));
            if(!feed_mechanism_detail::Pose(grabbed_)||(motion.kind==FeedMotionKind::Hinge&&
               std::hypot(double(grabbed_.values[3][0]),double(grabbed_.values[3][1]))<=motion.pathToleranceMeters*2))
                return fail(FeedInteractionStatus::OffPath);
            previous_=contact.weaponFromHandContact;moving_=true;started_=contact.observedNs;
            out.status=FeedInteractionStatus::Acquired;
            out.target=placement?motion.placement.weaponFromSource:start;
            out.targetKind=placement?FeedInteractionTargetKind::Object:FeedInteractionTargetKind::Contact;
            if(placement)carried_=*out.target;
            return out;
        }
        const bool placement=motion.kind==FeedMotionKind::FreePlacement;
        if(!sample.grip&&!placement)return fail(FeedInteractionStatus::Interrupted);
        if(contact.observedNs<=previousObserved)return fail(FeedInteractionStatus::InvalidInput);
        if(Distance(previous_,contact.weaponFromHandContact)>motion.maxStepMeters||
           Angle(previous_,contact.weaponFromHandContact)>motion.maxStepRadians)return fail(FeedInteractionStatus::MotionJump);
        if(placement){
            out.target=Multiply(grabbed_,contact.weaponFromHandContact);
            out.targetKind=FeedInteractionTargetKind::Object;
            if(!feed_mechanism_detail::Pose(*out.target))return fail(FeedInteractionStatus::OffPath);
            if(Distance(carried_,*out.target)>motion.maxStepMeters||Angle(carried_,*out.target)>motion.maxStepRadians)
                return fail(FeedInteractionStatus::MotionJump);
            previous_=contact.weaponFromHandContact;carried_=*out.target;
            const bool aligned=Distance(carried_,motion.placement.weaponFromDestination)<=motion.pathToleranceMeters&&
                Angle(carried_,motion.placement.weaponFromDestination)<=motion.orientationToleranceRadians;
            // No hysteresis outside the measured destination. A fresh held
            // observation must establish dwell before a fresh release edge.
            if(!aligned){endpoint_=0;placementDwelled_=false;}
            if(sample.grip){
                if(aligned&&!endpoint_)endpoint_=contact.observedNs;
                if(endpoint_&&contact.observedNs-endpoint_>=motion.endpointDwellNs)placementDwelled_=true;
                out.status=aligned?FeedInteractionStatus::Endpoint:FeedInteractionStatus::Moving;
                return out;
            }
            if(!aligned||!placementDwelled_)return fail(FeedInteractionStatus::Interrupted);
            if(receiptId_==std::numeric_limits<std::uint64_t>::max())return fail(FeedInteractionStatus::CounterExhausted);
            out.completion=FeedPhysicalCompletion{++receiptId_,contact.inputSequence,contact.observedNs,candidate};
            out.status=FeedInteractionStatus::Completed;Drop();return out;
        }
        const auto local=Multiply(contact.weaponFromHandContact,Inverse(motion.weaponFromMotion));
        if(!feed_mechanism_detail::Pose(local))return fail(FeedInteractionStatus::OffPath);
        double travel=double(local.values[3][0])-grabbed_.values[3][0];
        if(motion.kind==FeedMotionKind::Hinge){
            const double x=grabbed_.values[3][0],y=grabbed_.values[3][1];
            travel=std::atan2(x*local.values[3][1]-y*local.values[3][0],x*local.values[3][0]+y*local.values[3][1]);
        }
        out.progress=travel/motion.travel;
        const auto expected=Along(grabbed_,motion.kind,travel);
        const double overtravel=1-motion.releaseFraction;
        if(out.progress < -overtravel||out.progress>1+overtravel||Distance(local,expected)>motion.pathToleranceMeters||
           Angle(local,expected)>motion.orientationToleranceRadians)return fail(FeedInteractionStatus::OffPath);
        previous_=contact.weaponFromHandContact;
        out.target=Multiply(Along(motion.motionFromContact,motion.kind,std::clamp(out.progress,0.,1.)*motion.travel),motion.weaponFromMotion);
        out.targetKind=FeedInteractionTargetKind::Contact;
        if(!feed_mechanism_detail::Pose(*out.target))return fail(FeedInteractionStatus::OffPath);
        const bool enter=returning_?out.progress<=1-motion.completeFraction:out.progress>=motion.completeFraction;
        const bool leave=returning_?out.progress>1-motion.releaseFraction:out.progress<motion.releaseFraction;
        if(leave)endpoint_=0;
        if(!endpoint_&&enter)endpoint_=contact.observedNs;
        out.status=endpoint_?FeedInteractionStatus::Endpoint:(returning_?FeedInteractionStatus::Returning:FeedInteractionStatus::Moving);
        if(!endpoint_||contact.observedNs-endpoint_<motion.endpointDwellNs)return out;
        if(motion.returnToStart&&!returning_){returning_=true;endpoint_=0;out.status=FeedInteractionStatus::Returning;return out;}
        if(receiptId_==std::numeric_limits<std::uint64_t>::max())return fail(FeedInteractionStatus::CounterExhausted);
        out.completion=FeedPhysicalCompletion{++receiptId_,contact.inputSequence,contact.observedNs,candidate};
        out.status=FeedInteractionStatus::Completed;Drop();return out;
    }
private:
    void Drop()noexcept {bound_=moving_=returning_=placementDwelled_=false;endpoint_=started_=0;}
    bool stream_=false,bound_=false,moving_=false,returning_=false,lastGrip_=false;
    bool placementDwelled_=false;
    bool hasRejected_=false;FeedInteractionSample rejected_{};
    FeedOwner lastOwner_{};FeedKey lastMechanism_{},lastHand_{};
    std::uint64_t lastTracking_=0,lastInput_=0,lastSnapshot_=0,receiptId_=0;
    std::int64_t lastNow_=0,lastObserved_=0,started_=0,endpoint_=0;
    FeedPhysicalCandidate candidate_{};FeedMotionProfile motion_{};FeedMechanismProfile mechanism_{};
    math::Matrix4 grabbed_{},previous_{},lastRaw_{},carried_{};
};
}
