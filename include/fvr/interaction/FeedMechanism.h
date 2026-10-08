#pragma once
#include "fvr/math/StereoMath.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace fvr::interaction {
struct FeedKey {
    std::uint64_t id=0,generation=0;
    bool operator==(const FeedKey&)const=default;
};
struct FeedOwner {
    std::uint64_t actor=0,actorGeneration=0,weapon=0,equipGeneration=0,space=0;
    bool operator==(const FeedOwner&)const=default;
};
// The container and its ammunition are distinct identities, not two ammo grants.
struct FeedSupplyKey {
    FeedKey resource{},container{};
    bool operator==(const FeedSupplyKey&)const=default;
};
enum class FeedFamily:std::uint8_t {Unknown,Belt,Magazine};
enum class FeedCoverState:std::uint8_t {Unknown,Absent,Closed,Open};
enum class FeedLatchState:std::uint8_t {Unknown,Absent,Engaged,Released};
enum class FeedContainerState:std::uint8_t {Unknown,Absent,Detached,Attached};
enum class FeedSeatState:std::uint8_t {Unknown,Unseated,Seated};
enum class FeedCycleState:std::uint8_t {Unknown,Absent,Required,Pending,NativeReady};
enum class FeedSupplyState:std::uint8_t {Unknown,Usable,Depleted,Unavailable};
enum class FeedPart:std::uint8_t {Cover,Latch,Container,Feed,Charge,Count};
enum class FeedPhysicalOperation:std::uint8_t {
    None,ReleaseLatch,OpenCover,WithdrawFeed,DetachContainer,AttachContainer,
    SeatFeed,CloseCover,EngageLatch,CycleAction
};
struct FeedContactBinding {
    std::uint32_t role=0;
    float radiusMeters=0,maxAngleRadians=0;
};
// This first family uses feed replacement followed by closure and optional
// charging. Other orderings need another validated policy; no gun-name lookup.
struct FeedMechanismProfile {
    FeedFamily family=FeedFamily::Unknown;
    std::uint64_t id=0,revision=0;
    std::uint32_t ammunitionKind=0;
    bool cover=false,latch=false,container=false,charge=false;
    bool containerNeedsOpen=false,feedNeedsOpen=false,detachNeedsUnseated=false;
    std::int64_t maxAgeNs=0;
    std::array<FeedContactBinding,static_cast<unsigned>(FeedPart::Count)> contacts{};
};
struct FeedObservedContact {
    std::uint32_t role=0;
    // Proper rigid canonical row-vector frame in weapon-local METRES. The
    // adapter supplies a current anatomical contact, not a snapped wrist.
    math::Matrix4 weaponFromContact{};
};
struct FeedSupplyObservation {
    FeedSupplyKey key{};std::uint32_t ammunitionKind=0;
    FeedSupplyState state=FeedSupplyState::Unknown;
};
struct FeedMechanismSnapshot {
    FeedOwner owner{};FeedKey mechanism{};
    std::uint64_t profileId=0,profileRevision=0,sequence=0,inputSequence=0;
    std::int64_t observedNs=0;
    // Caller has reconciled native/resource effects of prior interruptions.
    // This is NOT a native-binding capability or permission to dispatch.
    bool reconciled=false;
    FeedCoverState cover=FeedCoverState::Unknown;
    FeedLatchState latch=FeedLatchState::Unknown;
    FeedContainerState container=FeedContainerState::Unknown;
    FeedSeatState feed=FeedSeatState::Unknown;
    FeedCycleState cycle=FeedCycleState::Unknown;
    FeedSupplyKey attached{},seated{};
    FeedSupplyObservation supply{};
    std::array<FeedObservedContact,static_cast<unsigned>(FeedPart::Count)> contacts{};
};
struct FeedPhysicalCandidate {
    FeedOwner owner{};FeedKey mechanism{};FeedSupplyKey supply{},goal{};
    std::uint64_t profileId=0,profileRevision=0,snapshotSequence=0;
    FeedPhysicalOperation operation=FeedPhysicalOperation::None;
    FeedPart part=FeedPart::Feed;std::uint32_t role=0;
    bool operator==(const FeedPhysicalCandidate&)const=default;
};
enum class FeedMechanismStatus:std::uint8_t {
    Candidate,NoPhysicalStep,UnsupportedFamily,InvalidProfile,InvalidSnapshot,
    UnknownState,Unreconciled,Stale,WrongResource,ResourceUnavailable,CyclePending,
    CandidateChanged,TrackingUnavailable,HandUnavailable,IncoherentContact,InvalidGeometry,
    ContactMiss,ContactMatch
};
struct FeedMechanismResult {
    FeedMechanismStatus status=FeedMechanismStatus::InvalidSnapshot;
    std::optional<FeedPhysicalCandidate> candidate{};
};
struct FeedContactSample {
    FeedOwner owner{};FeedKey mechanism{};
    std::uint64_t snapshotSequence=0,inputSequence=0;
    std::int64_t observedNs=0;
    bool tracked=false,handAvailable=false;
    math::Matrix4 weaponFromHandContact{};
};
struct FeedContactResult {
    FeedMechanismStatus status=FeedMechanismStatus::InvalidSnapshot;
    double distanceMeters=0;float angleRadians=0;
    // Presentation/contact target only. No gesture edge, state advancement,
    // native acknowledgement, ammunition count or fire-ready output exists.
    std::optional<math::Matrix4> target{};
};
namespace feed_mechanism_detail {
inline bool Key(FeedKey k)noexcept{return k.id&&k.generation;}
inline bool Empty(FeedKey k)noexcept{return !k.id&&!k.generation;}
inline bool Owner(const FeedOwner& o)noexcept {
    return o.actor&&o.actorGeneration&&o.weapon&&o.equipGeneration&&o.space;
}
inline bool Supply(FeedSupplyKey k,bool container)noexcept {
    return Key(k.resource)&&(container?Key(k.container):Empty(k.container));
}
inline bool Empty(FeedSupplyKey k)noexcept{return Empty(k.resource)&&Empty(k.container);}
inline bool Part(const FeedMechanismProfile& p,unsigned n)noexcept {
    switch(static_cast<FeedPart>(n)){
    case FeedPart::Cover:return p.cover;case FeedPart::Latch:return p.latch;
    case FeedPart::Container:return p.container;case FeedPart::Feed:return true;
    case FeedPart::Charge:return p.charge;default:return false;
    }
}
inline bool Fresh(std::int64_t observed,std::int64_t now,std::int64_t age)noexcept {
    return observed>0&&now>=observed&&now-observed<=age;
}
inline bool Pose(const math::Matrix4& m)noexcept {
    const auto& a=m.values;
    for(const auto& row:a)for(float v:row)if(!std::isfinite(v))return false;
    for(unsigned i=0;i<4;++i)if(std::abs(a[i][3]-(i==3?1.f:0.f))>1e-5f)return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=i;j<3;++j){
        double dot=0;for(unsigned k=0;k<3;++k)dot+=double(a[i][k])*a[j][k];
        if(std::abs(dot-(i==j?1.0:0.0))>1e-4)return false;
    }
    const double det=double(a[0][0])*(double(a[1][1])*a[2][2]-double(a[1][2])*a[2][1])-
        double(a[0][1])*(double(a[1][0])*a[2][2]-double(a[1][2])*a[2][0])+
        double(a[0][2])*(double(a[1][0])*a[2][1]-double(a[1][1])*a[2][0]);
    return det>0;
}
inline FeedPart OperationPart(FeedPhysicalOperation op)noexcept {
    switch(op){
    case FeedPhysicalOperation::ReleaseLatch:case FeedPhysicalOperation::EngageLatch:return FeedPart::Latch;
    case FeedPhysicalOperation::OpenCover:case FeedPhysicalOperation::CloseCover:return FeedPart::Cover;
    case FeedPhysicalOperation::DetachContainer:case FeedPhysicalOperation::AttachContainer:return FeedPart::Container;
    case FeedPhysicalOperation::CycleAction:return FeedPart::Charge;
    default:return FeedPart::Feed;
    }
}
}
[[nodiscard]] inline bool ValidateFeedMechanismProfile(const FeedMechanismProfile& p)noexcept {
    using namespace feed_mechanism_detail;
    if(p.family!=FeedFamily::Belt||!p.id||!p.revision||!p.ammunitionKind||p.maxAgeNs<=0||
       (p.latch&&!p.cover)||(p.containerNeedsOpen&&(!p.container||!p.cover))||
       (p.feedNeedsOpen&&!p.cover)||(p.detachNeedsUnseated&&!p.container))return false;
    for(unsigned n=0;n<p.contacts.size();++n){
        const auto& c=p.contacts[n];
        if(!Part(p,n)){if(c.role||c.radiusMeters!=0||c.maxAngleRadians!=0)return false;continue;}
        if(!c.role||!std::isfinite(c.radiusMeters)||c.radiusMeters<=0||
           !std::isfinite(c.maxAngleRadians)||c.maxAngleRadians<0||c.maxAngleRadians>3.141592654f)return false;
        for(unsigned j=0;j<n;++j)if(Part(p,j)&&p.contacts[j].role==c.role)return false;
    }
    return true;
}
// Pure desired-state query. Every call starts from observed state; candidates
// never modify it or mean that a previous candidate succeeded. The caller owns
// immutable snapshot sequences, gesture rearming and native/resource authority.
[[nodiscard]] inline FeedMechanismResult EvaluateFeedMechanism(const FeedMechanismProfile& p,
    const FeedMechanismSnapshot& s,FeedSupplyKey desired,std::int64_t nowNs)noexcept {
    using namespace feed_mechanism_detail;
    const auto fail=[](FeedMechanismStatus why){return FeedMechanismResult{why,{}};};
    if(p.family!=FeedFamily::Belt)return fail(FeedMechanismStatus::UnsupportedFamily);
    if(!ValidateFeedMechanismProfile(p))return fail(FeedMechanismStatus::InvalidProfile);
    if(!Owner(s.owner)||!Key(s.mechanism)||!s.sequence||!s.inputSequence||s.profileId!=p.id||s.profileRevision!=p.revision)
        return fail(FeedMechanismStatus::InvalidSnapshot);
    if(!Fresh(s.observedNs,nowNs,p.maxAgeNs))return fail(FeedMechanismStatus::Stale);
    if(!s.reconciled)return fail(FeedMechanismStatus::Unreconciled);
    if(s.cover==FeedCoverState::Unknown||s.latch==FeedLatchState::Unknown||
       s.container==FeedContainerState::Unknown||s.feed==FeedSeatState::Unknown||s.cycle==FeedCycleState::Unknown)
        return fail(FeedMechanismStatus::UnknownState);
    if((p.cover?(s.cover!=FeedCoverState::Closed&&s.cover!=FeedCoverState::Open):s.cover!=FeedCoverState::Absent)||
       (p.latch?(s.latch!=FeedLatchState::Engaged&&s.latch!=FeedLatchState::Released):s.latch!=FeedLatchState::Absent)||
       (p.container?(s.container!=FeedContainerState::Detached&&s.container!=FeedContainerState::Attached):s.container!=FeedContainerState::Absent)||
       (s.feed!=FeedSeatState::Unseated&&s.feed!=FeedSeatState::Seated)||
       (p.charge?(s.cycle!=FeedCycleState::Required&&s.cycle!=FeedCycleState::Pending&&s.cycle!=FeedCycleState::NativeReady):s.cycle!=FeedCycleState::Absent)||
       (s.cover==FeedCoverState::Open&&s.latch==FeedLatchState::Engaged))return fail(FeedMechanismStatus::InvalidSnapshot);
    if((s.container==FeedContainerState::Attached?!Supply(s.attached,true):!Empty(s.attached))||
       (s.feed==FeedSeatState::Seated?!Supply(s.seated,p.container):!Empty(s.seated))||
       (s.container==FeedContainerState::Attached&&s.feed==FeedSeatState::Seated&&s.attached!=s.seated))
        return fail(FeedMechanismStatus::InvalidSnapshot);
    if(!Supply(desired,p.container)||desired!=s.supply.key||s.supply.ammunitionKind!=p.ammunitionKind)
        return fail(FeedMechanismStatus::WrongResource);
    if(s.supply.state!=FeedSupplyState::Usable)return fail(FeedMechanismStatus::ResourceUnavailable);
    for(unsigned n=0;n<p.contacts.size();++n)if(Part(p,n)&&
        (s.contacts[n].role!=p.contacts[n].role||!Pose(s.contacts[n].weaponFromContact)))return fail(FeedMechanismStatus::InvalidGeometry);
    if(s.cycle==FeedCycleState::Pending)return fail(FeedMechanismStatus::CyclePending);
    const auto candidate=[&](FeedPhysicalOperation op,FeedSupplyKey resource){
        const auto part=OperationPart(op);
        return FeedMechanismResult{FeedMechanismStatus::Candidate,FeedPhysicalCandidate{
            s.owner,s.mechanism,resource,desired,p.id,p.revision,s.sequence,op,part,p.contacts[static_cast<unsigned>(part)].role}};
    };
    const auto access=[&](FeedPhysicalOperation op,FeedSupplyKey resource,bool needsOpen){
        if(needsOpen&&s.cover==FeedCoverState::Closed){
            if(s.latch==FeedLatchState::Engaged)return candidate(FeedPhysicalOperation::ReleaseLatch,{});
            return candidate(FeedPhysicalOperation::OpenCover,{});
        }
        return candidate(op,resource);
    };
    const bool replacingContainer=p.container&&(s.container!=FeedContainerState::Attached||s.attached!=desired);
    if(s.feed==FeedSeatState::Seated&&(s.seated!=desired||(replacingContainer&&s.container==FeedContainerState::Attached&&p.detachNeedsUnseated)))
        return access(FeedPhysicalOperation::WithdrawFeed,s.seated,p.feedNeedsOpen);
    if(replacingContainer){
        if(s.container==FeedContainerState::Attached)return access(FeedPhysicalOperation::DetachContainer,s.attached,p.containerNeedsOpen);
        return access(FeedPhysicalOperation::AttachContainer,desired,p.containerNeedsOpen);
    }
    if(s.feed==FeedSeatState::Unseated)return access(FeedPhysicalOperation::SeatFeed,desired,p.feedNeedsOpen);
    if(s.cover==FeedCoverState::Open)return candidate(FeedPhysicalOperation::CloseCover,{});
    if(s.latch==FeedLatchState::Released)return candidate(FeedPhysicalOperation::EngageLatch,{});
    if(s.cycle==FeedCycleState::Required)return candidate(FeedPhysicalOperation::CycleAction,{});
    return fail(FeedMechanismStatus::NoPhysicalStep); // NOT native readiness/acknowledgement.
}
// Contact is evaluated against a freshly revalidated candidate and geometry
// tagged to the SAME original raw input packet. Never pairs current buttons with
// prior rendered/snap-to-contact geometry. The caller must arbitrate hand use.
[[nodiscard]] inline FeedContactResult EvaluateFeedContact(const FeedMechanismProfile& p,
    const FeedMechanismSnapshot& current,FeedSupplyKey desired,const FeedPhysicalCandidate& expected,
    const FeedContactSample& hand,std::int64_t nowNs)noexcept {
    FeedContactResult out;
    const auto next=EvaluateFeedMechanism(p,current,desired,nowNs);
    if(!next.candidate){out.status=next.status;return out;}
    if(*next.candidate!=expected){out.status=FeedMechanismStatus::CandidateChanged;return out;}
    if(!hand.tracked){out.status=FeedMechanismStatus::TrackingUnavailable;return out;}
    if(!hand.handAvailable){out.status=FeedMechanismStatus::HandUnavailable;return out;}
    if(hand.owner!=current.owner||hand.mechanism!=current.mechanism||hand.snapshotSequence!=current.sequence||
       hand.inputSequence!=current.inputSequence||!feed_mechanism_detail::Fresh(hand.observedNs,nowNs,p.maxAgeNs)){
        out.status=FeedMechanismStatus::IncoherentContact;return out;
    }
    if(!feed_mechanism_detail::Pose(hand.weaponFromHandContact)){out.status=FeedMechanismStatus::InvalidGeometry;return out;}
    const auto index=static_cast<unsigned>(expected.part);
    const auto& target=current.contacts[index].weaponFromContact;const auto& pose=hand.weaponFromHandContact;
    double squared=0,trace=0;
    for(unsigned i=0;i<3;++i){
        const double d=double(target.values[3][i])-pose.values[3][i];squared+=d*d;
        for(unsigned j=0;j<3;++j)trace+=double(target.values[i][j])*pose.values[i][j];
    }
    out.distanceMeters=std::sqrt(squared);
    out.angleRadians=static_cast<float>(std::acos(std::clamp((trace-1.0)*.5,-1.0,1.0)));
    const auto& binding=p.contacts[index];
    out.status=out.distanceMeters<=binding.radiusMeters&&out.angleRadians<=binding.maxAngleRadians?
        FeedMechanismStatus::ContactMatch:FeedMechanismStatus::ContactMiss;
    out.target=target;return out;
}
}
