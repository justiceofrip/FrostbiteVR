#pragma once
#include "SightGrasp.h"
#include "HandInteraction.h"

namespace fvr::interaction {
struct SightVisualProgress {
    float appliedRadians=0,nativeProgressRadians=0;
    bool nativeObserved=false,rawObserved=false;
    float rawProgressRadians=0;std::uint64_t rawGeneration=0;
};
// This is the independent pre-IK target, never the displayed/snapped wrist.
// The adapter keeps identity/claim checks; this boundary rejects stale or future
// source generations and expired geometry using one caller clock.
struct SightVisualRawHand {
    math::Matrix4 wrist{};std::uint64_t generation=0;
    static std::optional<SightVisualRawHand> Fresh(const math::Matrix4& wrist,
        std::uint64_t sourceGeneration,std::uint64_t currentGeneration,
        std::int64_t now,std::int64_t deadline)noexcept {
        if(!sourceGeneration||sourceGeneration>currentGeneration||now<=0||deadline<=now||
           !sight_grasp_detail::Proper(wrist))return {};
        return SightVisualRawHand{wrist,sourceGeneration};
    }
};
struct SightVisualHandCapture {
    math::Matrix4 rawWrist{};math::Vec3 rawPalmWrist{};std::uint64_t generation=0;
};
// Captured visual geometry is not current contact proof. The caller must keep
// an exact, live semantic claim and validate the native snapshot's raw item,
// owner, space and deadline. Elapsed time only bounds motion toward observed
// native geometry; it never manufactures a mode acknowledgement or target.
// Once acknowledged, current raw hand motion drives the held visual; native
// animation supplies bounded fallback. Neither rewinds the committed grasp.
class SightVisualHandoff {
public:
    static constexpr float MaxNativeRadiansPerSecond=3.141592653589793f;
    static constexpr float MaxNativeStepRadians=.08726646259971647f; // Five degrees, including after a long gap.
    static std::optional<SightVisualHandoff> Begin(const math::Matrix4& rearAtGrab,SightMode mode,
        const std::optional<SightVisualHandCapture>& rawCapture={})noexcept {
        if(!sight_grasp_detail::Proper(rearAtGrab)||(mode!=SightMode::Primary&&mode!=SightMode::Secondary))return {};
        const math::Vec3 pivot{rearAtGrab.values[3][0],rearAtGrab.values[3][1],rearAtGrab.values[3][2]};
        math::Vec3 axis{rearAtGrab.values[0][0],rearAtGrab.values[0][1],rearAtGrab.values[0][2]};
        const float length=std::sqrt(sight_grasp_detail::Dot(axis,axis));axis={axis.x/length,axis.y/length,axis.z/length};
        return BeginWithGeometry(rearAtGrab,mode,pivot,axis,1.5707963267948966f,rawCapture);
    }
    // Profile-driven hinge, including descendant sideways sights. This is
    // visual policy only; no config match can acknowledge native selection.
    static std::optional<SightVisualHandoff> BeginWithGeometry(const math::Matrix4& rearAtGrab,SightMode mode,
        math::Vec3 pivot,math::Vec3 axis,float maxTravelRadians,
        const std::optional<SightVisualHandCapture>& rawCapture={})noexcept {
        using namespace sight_grasp_detail;
        if(!Proper(rearAtGrab)||!Finite(pivot)||!Unit(axis)||!std::isfinite(maxTravelRadians)||
           maxTravelRadians<=0||maxTravelRadians>1.5707963267948966f||
           (mode!=SightMode::Primary&&mode!=SightMode::Secondary))return {};
        const auto inverse=InverseAnimatedTransform(rearAtGrab);if(!inverse)return {};
        SightVisualHandoff out;out.rear_=rearAtGrab;out.direction_=mode==SightMode::Primary?1.f:-1.f;
        out.axis_=axis;out.pivot_=pivot;out.maxTravel_=maxTravelRadians;
        out.localPivot_=Point(pivot,*inverse);
        const auto localTip=Point({pivot.x+axis.x,pivot.y+axis.y,pivot.z+axis.z},*inverse);
        out.localAxis_={localTip.x-out.localPivot_.x,localTip.y-out.localPivot_.y,localTip.z-out.localPivot_.z};
        // Pick the most perpendicular source basis row, preserving the old
        // row2 radial for the old row0 hinge whenever equally perpendicular.
        unsigned best=2;float smallest=2;
        for(unsigned n:{2u,1u,0u}){
            const math::Vec3 row{rearAtGrab.values[n][0],rearAtGrab.values[n][1],rearAtGrab.values[n][2]};
            const float parallel=std::abs(Dot(row,axis));if(parallel<smallest){smallest=parallel;best=n;}
        }
        out.referenceRow_=best;
        if(rawCapture){
            if(!rawCapture->generation||!sight_grasp_detail::Proper(rawCapture->rawWrist)||
               !sight_grasp_detail::Finite(rawCapture->rawPalmWrist))return {};
            out.palm_=rawCapture->rawPalmWrist;const auto radial=out.RawRadial(rawCapture->rawWrist);if(!radial)return {};
            out.startRadial_=*radial;out.grabGeneration_=out.rawGeneration_=rawCapture->generation;
        }
        return out;
    }
    std::optional<SightVisualProgress> Update(SightFlipPhase phase,float gestureRadians,
        std::int64_t observedNs,const std::optional<math::Matrix4>& nativeRear={},
        const std::optional<SightVisualRawHand>& rawHand={})noexcept {
        if(observedNs<=0||(lastNs_&&observedNs<lastNs_)||!std::isfinite(gestureRadians)||phase==SightFlipPhase::Idle||
           phase>SightFlipPhase::Latched||(requested_&&phase==SightFlipPhase::Manipulating))return {};
        const float gesture=std::clamp(direction_*gestureRadians,0.f,maxTravel_);
        const auto elapsed=lastNs_?observedNs-lastNs_:0;
        lastNs_=observedNs;
        SightVisualProgress out;
        if(phase==SightFlipPhase::Manipulating)progress_=gesture;
        else{
            requested_=true;progress_=std::max(progress_,gesture);
            // Native mode authority has committed. Continue the already-held
            // visual from fresh raw motion instead of freezing at its request
            // angle until the authored equip animation finally catches up.
            // A committed mode cannot be reversed by dragging this same grasp.
            if(phase==SightFlipPhase::Latched&&rawHand&&grabGeneration_&&
               rawHand->generation>=grabGeneration_&&rawHand->generation>=rawGeneration_){
                if(const auto radial=RawRadial(rawHand->wrist)){
                    const auto axis=Axis();const auto a=startRadial_,b=*radial;
                    const math::Vec3 cross{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
                    const float angle=direction_*std::atan2(sight_grasp_detail::Dot(axis,cross),
                        std::clamp(sight_grasp_detail::Dot(a,b),-1.f,1.f));
                    if(std::isfinite(angle)){
                        out.rawObserved=true;
                        // A render may republish the same XR generation with
                        // different animation geometry. It cannot buy motion.
                        if(rawHand->generation>rawGeneration_)rawProgress_=std::clamp(angle,0.f,maxTravel_);
                        out.rawProgressRadians=rawProgress_;out.rawGeneration=rawGeneration_=rawHand->generation;
                        progress_=std::max(progress_,rawProgress_);
                    }
                }
            }
            if(nativeRear){
                const auto angle=NativeProgress(*nativeRear);
                if(angle){
                    out.nativeObserved=true;out.nativeProgressRadians=*angle;
                    nativeTarget_=std::max(nativeTarget_,*angle);
                    // BC2 can animate most of the remaining hinge travel in a
                    // few frames. Keep the held wrist and sight continuous in
                    // weapon space instead of snapping both to that new angle.
                    const float step=std::min(MaxNativeStepRadians,
                        MaxNativeRadiansPerSecond*float(double(elapsed)*1e-9));
                    if(!out.rawObserved)progress_=std::max(progress_,std::min(nativeTarget_,progress_+step));
                }
            }
        }
        out.appliedRadians=direction_*progress_;return out;
    }
private:
    math::Vec3 Axis()const noexcept {return axis_;}
    std::optional<math::Vec3> RawRadial(const math::Matrix4& wrist)const noexcept {
        using namespace sight_grasp_detail;if(!Proper(wrist))return {};
        const auto p=Point(palm_,wrist),axis=Axis();
        math::Vec3 delta{p.x-pivot_.x,p.y-pivot_.y,p.z-pivot_.z};
        const float along=Dot(delta,axis);delta={delta.x-along*axis.x,delta.y-along*axis.y,delta.z-along*axis.z};
        const float n=std::sqrt(Dot(delta,delta));if(!std::isfinite(n)||n<.015f)return {};
        return math::Vec3{delta.x/n,delta.y/n,delta.z/n};
    }
    std::optional<float> NativeProgress(const math::Matrix4& current)const noexcept {
        using namespace sight_grasp_detail;
        if(!Proper(current))return {};
        const auto row=[](const math::Matrix4& m,unsigned n){return math::Vec3{m.values[n][0],m.values[n][1],m.values[n][2]};};
        const auto normalize=[](math::Vec3 p){const float n=std::sqrt(Dot(p,p));return math::Vec3{p.x/n,p.y/n,p.z/n};};
        const auto axis=Axis();
        const auto pivot=Point(localPivot_,current),tip=Point({localPivot_.x+localAxis_.x,localPivot_.y+localAxis_.y,localPivot_.z+localAxis_.z},current);
        const auto otherAxis=normalize({tip.x-pivot.x,tip.y-pivot.y,tip.z-pivot.z});
        // Reject another mechanism/axis or a displaced hinge, rather than
        // interpreting an equip-transition collapse as native progress.
        if(Dot(axis,otherAxis)<.999f)return {};
        const auto a=pivot_,b=pivot;
        if(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)>.025f)return {};
        const auto radial=[&](math::Vec3 p){const float along=Dot(p,axis);return normalize({p.x-along*axis.x,p.y-along*axis.y,p.z-along*axis.z});};
        const auto start=radial(row(rear_,referenceRow_)),now=radial(row(current,referenceRow_));
        const math::Vec3 cross{start.y*now.z-start.z*now.y,start.z*now.x-start.x*now.z,start.x*now.y-start.y*now.x};
        const float angle=direction_*std::atan2(Dot(axis,cross),std::clamp(Dot(start,now),-1.f,1.f));
        if(!std::isfinite(angle)||angle<-.035f||angle>maxTravel_+.035f)return {};
        return std::clamp(angle,0.f,maxTravel_);
    }
    math::Matrix4 rear_{};float direction_=1,progress_=0,nativeTarget_=0,maxTravel_=1.5707963267948966f;bool requested_=false;
    math::Vec3 axis_{},pivot_{},localAxis_{},localPivot_{};unsigned referenceRow_=2;
    std::int64_t lastNs_=0;math::Vec3 palm_{},startRadial_{};
    std::uint64_t grabGeneration_=0,rawGeneration_=0;float rawProgress_=0;
};
// Only call after a policy trial has validated the already committed Latched
// state. This renews the SAME existing reservation using current raw safety;
// it is deliberately unable to acquire or resurrect an expired claim.
inline bool RenewLatchedSightReservation(HandInteraction& ownership,const HandInteractionSample& current,
    HandInteractionKey item,SightFlipPhase previousPhase,const SightFlipResult& validated)noexcept {
    if(previousPhase!=SightFlipPhase::Latched||validated.phase!=SightFlipPhase::Latched||
       validated.cancelled||validated.released||validated.grabbed||validated.request||validated.committedMode||validated.reason!=SightFlipCancel::None)return false;
    const auto left=ownership.Current(InteractionHand::Left),right=ownership.Current(InteractionHand::Right);
    if(!left||!right||left->token.kind!=HandClaimKind::Sight||right->token.kind!=HandClaimKind::GunHold||
       left->token.item!=item||right->token.item!=item||left->token.owner!=current.owner||right->token.owner!=current.owner||
       left->token.prerequisiteClaim!=right->token.id||left->deadlineNs<=current.nowNs||right->deadlineNs<=current.nowNs)return false;
    return ownership.Renew(current,left->token,
        {left->token.contact,current.sequence,current.deadlineNs,true}).accepted;
}
}
