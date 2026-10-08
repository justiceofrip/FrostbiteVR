#pragma once
#include "fvr/math/StereoMath.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <initializer_list>
#include <optional>

namespace fvr::interaction {
enum class SightMode : std::uint8_t {Primary,Secondary};
// Equipped is the semantic physical item, not its internal native mode/slot.
// The adapter preserves it across an expected rifle/launcher mode transition.
struct SightFlipOwner {
    std::uint64_t actor=0,generation=0,equipped=0,space=0;
    bool operator==(const SightFlipOwner&)const=default;
};
struct SightFlipConfig {
    // All geometry is supplied by a verified adapter, in weapon-local metres.
    // Positive signed rotation around axis opens Primary -> Secondary. Negate
    // the supplied axis if the authored mechanism opens in the other direction.
    math::Vec3 pivotMeters{},axis{};
    float grabRadiusMeters=0,holdRadiusMeters=0,minLeverMeters=0;
    float thresholdRadians=0,hysteresisRadians=0,maxStepRadians=0;
    std::int64_t detentHoldNs=0,gestureTimeoutNs=0,ackTimeoutNs=0,maxSampleGapNs=0;
    float pressSqueeze=.7f,releaseSqueeze=.35f;
    // A committed native mode can temporarily have no settled contact geometry.
    // This one-shot bridge never refreshes; normal contact is required again
    // after its first fresh recovery. The adapter supplies current tracking.
    std::int64_t latchedContactGraceNs=1000000000;
};
struct SightFlipSample {
    SightFlipOwner owner{};
    std::uint64_t sequence=0;
    std::int64_t nowNs=0; // Monotonic caller time, including repeated pose packets.
    bool focused=false,tracked=false,contactValid=false,nativeModeValid=false;
    math::Vec3 handLocalMeters{};
    float squeeze=0,contactDistanceMeters=0; // Distance to the actual sight contact.
    SightMode nativeMode=SightMode::Primary;
    // Echo only after this exact request has reached the authoritative native
    // mode; matching target mode alone is not acknowledgement. Zero means none.
    std::uint64_t acknowledgedRequest=0;
};
struct SightFlipRequest {std::uint64_t id=0;SightFlipOwner owner{};SightMode target=SightMode::Primary;};
enum class SightFlipPhase : std::uint8_t {Idle,Manipulating,AwaitingAcknowledgement,Latched};
enum class SightFlipCancel : std::uint8_t {
    None,InvalidSample,TrackingLost,IdentityChanged,ClockDiscontinuity,StaleTracking,
    ContactLost,Released,NativeModeChanged,GestureTimeout,AcknowledgementTimeout,InvalidGeometry
};
struct SightFlipResult {
    SightFlipPhase phase=SightFlipPhase::Idle;
    std::optional<SightFlipRequest> request{}; // One edge; never an automatic retry.
    std::optional<SightMode> committedMode{}; // Only after matching native ack.
    std::uint64_t cancelledRequest=0;
    bool grabbed=false,released=false,cancelled=false;
    float signedRadians=0;
    bool detent=false;
    std::int64_t detentDwellNs=0;
    SightFlipCancel reason=SightFlipCancel::None;
};
// Pure interaction policy. Does not move sight geometry, select native slots,
// write gameplay state or predict native success. Geometry is fixed throughout
// an active gesture; an adapter may refresh verified geometry while idle.
class SightFlip {
public:
    explicit SightFlip(SightFlipConfig config)noexcept:config_(config){}
    SightFlipResult Update(const SightFlipSample& sample)noexcept;
    const SightFlipConfig& Config()const noexcept{return config_;}
    // Refresh before an idle grab without changing neutral arming or request IDs.
    // Invalid or active replacements leave the entire existing binding intact.
    bool SetIdleGeometry(math::Vec3 pivotMeters,math::Vec3 axis)noexcept;
private:
    static float Dot(math::Vec3 a,math::Vec3 b)noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
    static math::Vec3 Cross(math::Vec3 a,math::Vec3 b)noexcept{return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    static bool Finite(math::Vec3 p)noexcept{return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
    static bool ValidMode(SightMode mode)noexcept{return mode==SightMode::Primary||mode==SightMode::Secondary;}
    bool ValidConfig()const noexcept;
    std::optional<math::Vec3> Radial(math::Vec3 point)const noexcept;
    SightFlipResult Snapshot()const noexcept;
    SightFlipResult Cancel(SightFlipCancel reason)noexcept;
    SightFlipConfig config_{};SightFlipOwner owner_{};
    SightFlipPhase phase_=SightFlipPhase::Idle;SightMode startMode_=SightMode::Primary;
    std::uint64_t sequence_=0,nextRequest_=0,pendingRequest_=0;
    std::int64_t lastCall_=0,lastFresh_=0,grabbedAt_=0,detentAt_=0,requestedAt_=0,latchedAt_=0;
    bool initialized_=false,armed_=false,detent_=false,latchedContactReady_=false;
    float angle_=0;math::Vec3 lastRadial_{};
};
inline bool SightFlip::SetIdleGeometry(math::Vec3 pivotMeters,math::Vec3 axis)noexcept {
    if(phase_!=SightFlipPhase::Idle||!Finite(pivotMeters)||!Finite(axis)||
       std::abs(Dot(axis,axis)-1.f)>=.0001f)return false;
    config_.pivotMeters=pivotMeters;config_.axis=axis;return true;
}
inline bool SightFlip::ValidConfig()const noexcept {
    const auto& c=config_;
    for(float x:{c.grabRadiusMeters,c.holdRadiusMeters,c.minLeverMeters,c.thresholdRadians,
        c.hysteresisRadians,c.maxStepRadians,c.pressSqueeze,c.releaseSqueeze})if(!std::isfinite(x))return false;
    return Finite(c.pivotMeters)&&Finite(c.axis)&&std::abs(Dot(c.axis,c.axis)-1.f)<.0001f&&
        c.grabRadiusMeters>0&&c.holdRadiusMeters>c.grabRadiusMeters&&c.minLeverMeters>0&&
        c.thresholdRadians>0&&c.thresholdRadians<3.14159265f&&c.hysteresisRadians>0&&
        c.hysteresisRadians<c.thresholdRadians&&c.maxStepRadians>0&&c.maxStepRadians<3.14159265f&&
        c.detentHoldNs>0&&c.gestureTimeoutNs>c.detentHoldNs&&c.ackTimeoutNs>0&&c.maxSampleGapNs>0&&
        c.latchedContactGraceNs>0&&c.releaseSqueeze>=0&&c.releaseSqueeze<c.pressSqueeze&&c.pressSqueeze<=1;
}
inline std::optional<math::Vec3> SightFlip::Radial(math::Vec3 point)const noexcept {
    if(!Finite(point))return {};
    math::Vec3 r{point.x-config_.pivotMeters.x,point.y-config_.pivotMeters.y,point.z-config_.pivotMeters.z};
    const float axial=Dot(r,config_.axis);
    r={r.x-axial*config_.axis.x,r.y-axial*config_.axis.y,r.z-axial*config_.axis.z};
    const float length=std::hypot(r.x,r.y,r.z);
    if(!std::isfinite(length)||length<config_.minLeverMeters)return {};
    return math::Vec3{r.x/length,r.y/length,r.z/length};
}
inline SightFlipResult SightFlip::Snapshot()const noexcept {
    SightFlipResult out;out.phase=phase_;out.signedRadians=angle_;out.detent=detent_;
    out.detentDwellNs=detent_&&lastCall_>detentAt_?lastCall_-detentAt_:0;return out;
}
inline SightFlipResult SightFlip::Cancel(SightFlipCancel reason)noexcept {
    auto out=Snapshot();out.cancelled=phase_==SightFlipPhase::Manipulating||phase_==SightFlipPhase::AwaitingAcknowledgement;
    out.released=phase_!=SightFlipPhase::Idle;out.cancelledRequest=pendingRequest_;out.reason=reason;
    phase_=SightFlipPhase::Idle;armed_=detent_=latchedContactReady_=false;angle_=0;pendingRequest_=0;latchedAt_=0;
    out.phase=phase_;return out;
}
inline SightFlipResult SightFlip::Update(const SightFlipSample& s)noexcept {
    if(!ValidConfig()||!s.owner.actor||!s.owner.generation||!s.owner.equipped||!s.owner.space||
       !s.sequence||s.nowNs<=0||!std::isfinite(s.squeeze)||s.squeeze<0||s.squeeze>1||
       !s.nativeModeValid||!ValidMode(s.nativeMode))return Cancel(SightFlipCancel::InvalidSample);
    if(!s.focused||!s.tracked)return Cancel(SightFlipCancel::TrackingLost);
    SightFlipResult transition;
    if(initialized_){
        if(owner_!=s.owner)transition=Cancel(SightFlipCancel::IdentityChanged);
        else if(s.sequence<sequence_||s.nowNs<lastCall_)transition=Cancel(SightFlipCancel::ClockDiscontinuity);
        else if(s.nowNs-lastFresh_>config_.maxSampleGapNs)transition=Cancel(SightFlipCancel::StaleTracking);
        if(transition.reason!=SightFlipCancel::None){
            owner_=s.owner;sequence_=s.sequence;lastCall_=lastFresh_=s.nowNs;
            armed_=s.squeeze<=config_.releaseSqueeze;return transition;
        }
    }
    owner_=s.owner;lastCall_=s.nowNs;
    if(phase_==SightFlipPhase::AwaitingAcknowledgement&&s.nowNs-requestedAt_>=config_.ackTimeoutNs)
        return Cancel(SightFlipCancel::AcknowledgementTimeout);
    if(phase_==SightFlipPhase::Manipulating&&s.nowNs-grabbedAt_>=config_.gestureTimeoutNs)
        return Cancel(SightFlipCancel::GestureTimeout);
    if(phase_==SightFlipPhase::Latched){
        const auto target=startMode_==SightMode::Primary?SightMode::Secondary:SightMode::Primary;
        if(s.nativeMode!=target)return Cancel(SightFlipCancel::NativeModeChanged);
        const bool duplicate=initialized_&&s.sequence==sequence_;
        // Current safety release wins even on a repeated tracking packet, but
        // only a fresh neutral packet can rearm the next intentional grasp.
        if(s.squeeze<=config_.releaseSqueeze){
            auto out=Cancel(SightFlipCancel::Released);
            if(!duplicate){sequence_=s.sequence;lastFresh_=s.nowNs;armed_=true;}
            return out;
        }
        if(!s.contactValid){
            if(latchedContactReady_||s.nowNs-latchedAt_>=config_.latchedContactGraceNs)
                return Cancel(SightFlipCancel::ContactLost);
        }else{
            if(!std::isfinite(s.contactDistanceMeters)||s.contactDistanceMeters<0||
               s.contactDistanceMeters>config_.holdRadiusMeters)return Cancel(SightFlipCancel::ContactLost);
            if(!Finite(s.handLocalMeters))return Cancel(SightFlipCancel::InvalidGeometry);
            if(!duplicate)latchedContactReady_=true;
        }
        if(!duplicate){sequence_=s.sequence;lastFresh_=s.nowNs;}
        return Snapshot(); // No second request, commit, angle or detent update.
    }
    // Timeouts still run on duplicate packets, but duplicates cannot change a
    // gesture, arm a button, or acknowledge a request with stale pose data.
    if(initialized_&&s.sequence==sequence_){
        // An idle duplicate has no gesture geometry to invalidate. Preserve
        // arming from a fresh neutral packet even while the hand is away from
        // the sight; the next fresh squeeze still needs valid nearby contact.
        // Identity, focus, tracking and time guards above remain authoritative.
        if(phase_==SightFlipPhase::Idle)return Snapshot();
        // Native selection can finish before the next XR packet. Keep its
        // acknowledged request pending through the equipment contact gap, but
        // never commit on duplicate tracking. Identity/tracking/time guards
        // already ran, and releasing still invalidates this exception.
        const auto target=startMode_==SightMode::Primary?SightMode::Secondary:SightMode::Primary;
        if(phase_==SightFlipPhase::AwaitingAcknowledgement&&s.squeeze>config_.releaseSqueeze&&
           s.acknowledgedRequest==pendingRequest_&&s.nativeMode==target)return Snapshot();
        if(!s.contactValid||!std::isfinite(s.contactDistanceMeters)||s.contactDistanceMeters<0)
            return Cancel(SightFlipCancel::ContactLost);
        if(!Finite(s.handLocalMeters))return Cancel(SightFlipCancel::InvalidGeometry);
        return Snapshot();
    }
    initialized_=true;sequence_=s.sequence;lastFresh_=s.nowNs;
    if(s.squeeze<=config_.releaseSqueeze){
        auto out=phase_==SightFlipPhase::Idle?Snapshot():Cancel(SightFlipCancel::Released);
        armed_=true;return out;
    }
    // An acknowledged native transition can invalidate the old mode's contact
    // for one frame before its replacement pose is published. Accept that
    // completed transition without inventing geometry, but only after fresh
    // identity/tracking/time/release checks above. Unacknowledged loss cancels.
    if(phase_==SightFlipPhase::AwaitingAcknowledgement){
        const auto target=startMode_==SightMode::Primary?SightMode::Secondary:SightMode::Primary;
        if(s.acknowledgedRequest==pendingRequest_&&s.nativeMode==target){
            phase_=SightFlipPhase::Latched;pendingRequest_=0;latchedAt_=s.nowNs;latchedContactReady_=false;auto out=Snapshot();out.committedMode=target;return out;
        }
    }
    if(!s.contactValid||!std::isfinite(s.contactDistanceMeters)||s.contactDistanceMeters<0)
        return Cancel(SightFlipCancel::ContactLost);
    if(!Finite(s.handLocalMeters))return Cancel(SightFlipCancel::InvalidGeometry);
    if(phase_==SightFlipPhase::Idle){
        if(s.squeeze<config_.pressSqueeze)return Snapshot();
        if(!armed_)return Snapshot();
        armed_=false;
        if(s.contactDistanceMeters>config_.grabRadiusMeters)return Cancel(SightFlipCancel::ContactLost);
        const auto radial=Radial(s.handLocalMeters);if(!radial)return Cancel(SightFlipCancel::InvalidGeometry);
        lastRadial_=*radial;angle_=0;startMode_=s.nativeMode;grabbedAt_=s.nowNs;detent_=false;
        phase_=SightFlipPhase::Manipulating;auto out=Snapshot();out.grabbed=true;return out;
    }
    if(s.contactDistanceMeters>config_.holdRadiusMeters)return Cancel(SightFlipCancel::ContactLost);
    if(phase_==SightFlipPhase::AwaitingAcknowledgement)return Snapshot();
    if(phase_==SightFlipPhase::Latched)return Snapshot();
    if(s.nativeMode!=startMode_)return Cancel(SightFlipCancel::NativeModeChanged);
    const auto radial=Radial(s.handLocalMeters);if(!radial)return Cancel(SightFlipCancel::InvalidGeometry);
    const float step=std::atan2(Dot(config_.axis,Cross(lastRadial_,*radial)),std::clamp(Dot(lastRadial_,*radial),-1.f,1.f));
    if(!std::isfinite(step)||std::abs(step)>config_.maxStepRadians||std::abs(angle_+step)>3.14159265f)
        return Cancel(SightFlipCancel::InvalidGeometry);
    angle_+=step;lastRadial_=*radial;
    const float progress=startMode_==SightMode::Primary?angle_:-angle_;
    if(progress>=config_.thresholdRadians&&!detent_){detent_=true;detentAt_=s.nowNs;}
    else if(progress<config_.thresholdRadians-config_.hysteresisRadians)detent_=false;
    if(detent_&&s.nowNs-detentAt_>=config_.detentHoldNs){
        if(nextRequest_==std::numeric_limits<std::uint64_t>::max())return Cancel(SightFlipCancel::InvalidSample);
        pendingRequest_=++nextRequest_;requestedAt_=s.nowNs;phase_=SightFlipPhase::AwaitingAcknowledgement;
        auto out=Snapshot();out.request=SightFlipRequest{pendingRequest_,owner_,startMode_==SightMode::Primary?SightMode::Secondary:SightMode::Primary};return out;
    }
    return Snapshot();
}
}
