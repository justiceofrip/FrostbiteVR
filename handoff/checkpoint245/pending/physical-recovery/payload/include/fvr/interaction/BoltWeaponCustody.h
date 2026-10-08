#pragma once
#include "fvr/interaction/PhysicalWeaponCycle.h"
#include "fvr/interaction/TrackingMath.h"

namespace fvr::interaction {
enum class BoltCustodyPhase:unsigned {Idle,AwaitingCustody,Manipulating,Returned};
template<class Authority> struct BoltCustodySampleT {
    HandInteractionSample source{};
    Authority lease{};
    // Current custody-hand wrist in world; mechanism wrist in canonical weapon
    // space from original raw controller evidence, never a guided hand target.
    math::Matrix4 gunWristInWorld{},mechanismWristInWeapon{};
    HandContactProof gunContact{},mechanismContact{};
    bool mechanismGrip=false;
    std::uint64_t acquireIntent=0;
    std::optional<HandInteractionSample> contactSource;
};
struct BoltCustodyWeaponTarget {
    HandClaimToken gun{};
    std::uint64_t inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    math::Matrix4 weaponInWorld{};
};
inline bool CurrentBoltCustodyWeaponTarget(const BoltCustodyWeaponTarget& target,
    const HandInteractionSample& input,const HandClaim& gun)noexcept {
    using namespace weapon_cycle_detail;
    const auto hand=target.gun.hand;
    return (hand==InteractionHand::Left||hand==InteractionHand::Right)&&target.gun.id&&target.gun==gun.token&&
        gun.token.kind==HandClaimKind::GunHold&&!gun.token.prerequisiteClaim&&gun.token.owner==input.owner&&
        input.focused&&input.tracked[HandIndex(hand)]&&!input.released[HandIndex(hand)]&&
        Window(input.observedNs,input.deadlineNs,input.nowNs)&&target.inputSequence==input.sequence&&gun.inputSequence==input.sequence&&
        target.observedNs==input.observedNs&&Window(target.observedNs,target.deadlineNs,input.nowNs)&&
        target.deadlineNs<=gun.deadlineNs&&gun.deadlineNs<=input.deadlineNs&&feed_mechanism_detail::Pose(target.weaponInWorld);
}
template<class Authority> struct BoltCustodyResultT {
    BoltCustodyPhase custody=BoltCustodyPhase::Idle;
    PhysicalWeaponCycleResultT<Authority> physical{};
    std::optional<BoltCustodyWeaponTarget> weapon;
    bool blocksFire=false;
};

// Portable custody/gesture coordinator, not native admission or a rig writer.
// The adapter supplies a held native cycle and verified calibration. Offline
// authored fixtures may exercise this API without claiming live calibration.
template<class Authority> class BoltWeaponCustodyT {
    template<class> friend class BoltWeaponCustodyT;
public:
    using WeaponCycleLease=Authority;
    using WeaponCycleRelease=WeaponCycleReleaseT<Authority>;
    using WeaponCycleReady=WeaponCycleReadyT<Authority>;
    using PhysicalWeaponCycle=PhysicalWeaponCycleT<Authority>;
    using PhysicalWeaponCycleSample=PhysicalWeaponCycleSampleT<Authority>;
    using BoltCustodySample=BoltCustodySampleT<Authority>;
    using BoltCustodyResult=BoltCustodyResultT<Authority>;
    bool Arm(const WeaponCycleProfile& profile,const WeaponCycleLease& lease,
        const math::Matrix4& contactFromMechanismWrist,std::int64_t now)noexcept {
        if((phase_!=BoltCustodyPhase::Idle&&!(phase_==BoltCustodyPhase::Returned&&cycle_.Phase()==WeaponCyclePhase::Complete))||
           profile.family!=WeaponCycleFamily::Bolt||!feed_mechanism_detail::Pose(contactFromMechanismWrist)||
           !cycle_.Begin(profile,lease,now))return false;
        profile_=profile;identity_=lease;contactFromWrist_=contactFromMechanismWrist;
        phase_=BoltCustodyPhase::AwaitingCustody;gun_.reset();submitted_.reset();returnNeutral_=0;
        deadline_=now+profile.maximumCycleNs;return true;
    }
    // Transfer only still-live physical custody when a retained debt changes
    // its authority kind. This neither mints a hand claim nor accepts a native
    // completion. Expired/lost custody must return through ordinary gun/support
    // acquisition and the normal atomic exchange instead.
    template<class Previous> bool AdoptRetainedCustody(BoltWeaponCustodyT<Previous>& previous,
        const HandInteractionSample& source,const math::Matrix4& wrist,
        const HandContactProof& contact,HandInteraction& hands)noexcept {
        using namespace weapon_cycle_detail;
        if(phase_!=BoltCustodyPhase::AwaitingCustody||source.nowNs>=deadline_||previous.submitted_||
           previous.phase_!=BoltCustodyPhase::Manipulating||!previous.gun_||
           profile_.hands!=previous.profile_.hands||source.owner!=identity_.owner||
           identity_.owner!=previous.identity_.owner||identity_.item!=previous.identity_.item||
           identity_.mechanism!=previous.identity_.mechanism||identity_.cycle!=previous.identity_.cycle||
           identity_.shot!=previous.identity_.shot||!source.released[HandIndex(profile_.hands.mechanism)]||
           source.released[HandIndex(profile_.hands.gun)]||!feed_mechanism_detail::Pose(wrist)||
           hands.Current(profile_.hands.mechanism)||!source.focused||!source.tracked[0]||!source.tracked[1]||
           !Window(source.observedNs,source.deadlineNs,source.nowNs))return false;
        const auto gun=hands.Current(profile_.hands.gun);
        if(!gun||gun->token!=*previous.gun_||gun->token.item!=identity_.item||
           contact.key!=gun->token.contact||!contact.eligible||contact.inputSequence!=source.sequence||
           !hands.Renew(source,gun->token,contact).accepted)return false;
        gun_=previous.gun_;attachment_=previous.attachment_;
        originalGunContact_=previous.originalGunContact_;originalSupportContact_=previous.originalSupportContact_;
        phase_=BoltCustodyPhase::Manipulating;previous.gun_.reset();previous.phase_=BoltCustodyPhase::Idle;
        return true;
    }
    HandGunCustodyResult EnterCustody(const HandInteractionSample& current,const HandGunCustodyTransfer& transfer,
        const math::Matrix4& weaponInWorld,const math::Matrix4& receivingWristInWorld,HandInteraction& hands)noexcept {
        HandGunCustodyResult out;out.transaction.reason=HandInteractionReason::InvalidRequest;
        if(phase_!=BoltCustodyPhase::AwaitingCustody||current.nowNs>=deadline_||current.owner!=identity_.owner||
           !transfer.companion||transfer.gun.hand!=profile_.hands.mechanism||transfer.gun.item!=identity_.item||
           transfer.companion->hand!=profile_.hands.gun||transfer.companion->kind!=HandClaimKind::WeaponSupport||
           transfer.nextGun.request.hand!=profile_.hands.gun||transfer.nextCompanion||
           !feed_mechanism_detail::Pose(weaponInWorld)||!feed_mechanism_detail::Pose(receivingWristInWorld))return out;
        const auto inverse=InverseRigid(receivingWristInWorld);if(!inverse)return out;
        const auto attachment=Multiply(weaponInWorld,*inverse);if(!feed_mechanism_detail::Pose(attachment))return out;
        out=hands.TransferGunCustody(current,transfer);
        if(out.transaction.accepted&&out.transaction.claim){
            originalGunContact_=transfer.gun.contact;originalSupportContact_=transfer.companion->contact;
            gun_=out.transaction.claim->token;attachment_=attachment;phase_=BoltCustodyPhase::Manipulating;
        }return out;
    }
    BoltCustodyResult Update(const BoltCustodySample& sample,HandInteraction& hands)noexcept {
        BoltCustodyResult out;out.custody=phase_;
        if(phase_==BoltCustodyPhase::Idle)return out;
        const auto& source=sample.source;
        if(phase_==BoltCustodyPhase::AwaitingCustody){
            hands.Update(source);out.physical.cycle={cycle_.Phase(),WeaponCycleFailure::None,true};out.blocksFire=true;return out;
        }
        const bool poseOkay=feed_mechanism_detail::Pose(sample.gunWristInWorld);
        const auto renewed=gun_&&poseOkay?hands.Renew(source,*gun_,sample.gunContact):hands.Update(source);
        const auto currentGun=gun_?hands.Current(gun_->hand):std::nullopt;
        const bool gunOkay=renewed.accepted&&gun_&&currentGun&&currentGun->token==*gun_&&
            currentGun->inputSequence==source.sequence&&currentGun->deadlineNs>source.nowNs&&
            sample.gunContact.key==gun_->contact&&sample.gunContact.eligible&&poseOkay;
        PhysicalWeaponCycleSample physical;
        physical.source=source;physical.lease=sample.lease;physical.grip=sample.mechanismGrip;
        physical.contact=sample.mechanismContact;physical.acquireIntent=sample.acquireIntent;physical.contactSource=sample.contactSource;
        const auto inverse=InverseRigid(contactFromWrist_);
        if(inverse&&feed_mechanism_detail::Pose(sample.mechanismWristInWeapon))physical.rawContact=Multiply(*inverse,sample.mechanismWristInWeapon);
        if(gunOkay)physical.gun=*gun_;
        out.physical=cycle_.Update(physical,hands);
        out.blocksFire=phase_!=BoltCustodyPhase::Returned||out.physical.cycle.blocksFire||!gunOkay;
        if(out.physical.release)submitted_=out.physical.release;
        // Right/left return admission requires a real neutral packet after
        // submission. The release that emitted the request is not a grip edge.
        const auto mechanismIndex=weapon_cycle_detail::HandIndex(profile_.hands.mechanism);
        if(submitted_&&source.owner==identity_.owner&&source.focused&&source.tracked[0]&&source.tracked[1]&&
           source.released[mechanismIndex]&&source.sequence>submitted_->inputSequence&&
           weapon_cycle_detail::Window(source.observedNs,source.deadlineNs,source.nowNs))returnNeutral_=source.sequence;
        if(gunOkay){
            const auto weapon=Multiply(attachment_,sample.gunWristInWorld);
            const BoltCustodyWeaponTarget target{*gun_,source.sequence,source.observedNs,std::min(source.deadlineNs,currentGun->deadlineNs),weapon};
            if(CurrentBoltCustodyWeaponTarget(target,source,*currentGun))out.weapon=target;
        }
        return out;
    }
    // Completed native debt and current gun placement have independent lives.
    // Renew only the exact returned gun claim using CURRENT controller proof;
    // this neither refreshes the old held lease nor emits a mechanism target.
    BoltCustodyResult MaintainReturned(const HandInteractionSample& source,
        const math::Matrix4& wrist,const HandContactProof& contact,HandInteraction& hands)noexcept {
        if(phase_!=BoltCustodyPhase::Returned||cycle_.Phase()!=WeaponCyclePhase::Complete)return {};
        BoltCustodySample sample;sample.source=source;sample.gunWristInWorld=wrist;
        sample.gunContact=contact;sample.lease=identity_;
        return Update(sample,hands);
    }
    // Return is physical custody only. It cannot mark the cycle ready or clear
    // an owed action; the exact native outcome remains independently pending.
    HandGunCustodyResult ReturnCustody(const HandInteractionSample& current,const HandGunCustodyTransfer& transfer,
        const math::Matrix4& currentCustodyWristInWorld,const math::Matrix4& receivingWristInWorld,HandInteraction& hands)noexcept {
        HandGunCustodyResult out;out.transaction.reason=HandInteractionReason::InvalidRequest;
        const auto mechanismIndex=weapon_cycle_detail::HandIndex(profile_.hands.mechanism);
        if(phase_!=BoltCustodyPhase::Manipulating||!submitted_||!gun_||!returnNeutral_||current.sequence<=returnNeutral_||
           current.released[mechanismIndex]||transfer.gun!=*gun_||transfer.companion||!transfer.nextCompanion||
           transfer.nextGun.request.hand!=profile_.hands.mechanism||transfer.nextGun.request.contact.key!=originalGunContact_||
           transfer.nextGun.evidence.sequence<returnNeutral_||transfer.nextCompanion->request.hand!=profile_.hands.gun||
           transfer.nextCompanion->request.kind!=HandClaimKind::WeaponSupport||transfer.nextCompanion->request.contact.key!=originalSupportContact_||
           !feed_mechanism_detail::Pose(currentCustodyWristInWorld)||!feed_mechanism_detail::Pose(receivingWristInWorld))return out;
        const auto inverse=InverseRigid(receivingWristInWorld);if(!inverse)return out;
        const auto nextAttachment=Multiply(Multiply(attachment_,currentCustodyWristInWorld),*inverse);
        if(!feed_mechanism_detail::Pose(nextAttachment))return out;
        out=hands.TransferGunCustody(current,transfer);
        if(out.transaction.accepted&&out.transaction.claim){gun_=out.transaction.claim->token;attachment_=nextAttachment;phase_=BoltCustodyPhase::Returned;}
        return out;
    }
    bool ObserveReady(const WeaponCycleReady& ready,std::int64_t now)noexcept {return cycle_.ObserveReady(ready,now);}
    bool ReconcileReady(const WeaponCycleReady& ready,std::int64_t now)noexcept {return cycle_.ReconcileReady(ready,now);}
    void RejectNative(const WeaponCycleRelease& release)noexcept {cycle_.RejectNative(release);}
    BoltCustodyPhase Custody()const noexcept{return phase_;}
    WeaponCyclePhase CyclePhase()const noexcept{return cycle_.Phase();}
    std::optional<HandClaimToken> Gun()const noexcept{return gun_;}
private:
    PhysicalWeaponCycle cycle_;
    WeaponCycleProfile profile_{};WeaponCycleLease identity_{};
    math::Matrix4 contactFromWrist_{},attachment_{};
    HandInteractionKey originalGunContact_{},originalSupportContact_{};
    std::optional<HandClaimToken> gun_;
    std::optional<WeaponCycleRelease> submitted_;
    std::uint64_t returnNeutral_=0;
    std::int64_t deadline_=0;
    BoltCustodyPhase phase_=BoltCustodyPhase::Idle;
};
using BoltCustodySample=BoltCustodySampleT<WeaponCycleLease>;
using BoltCustodyResult=BoltCustodyResultT<WeaponCycleLease>;
using BoltWeaponCustody=BoltWeaponCustodyT<WeaponCycleLease>;
using BoltCustodyIdleDebtSample=BoltCustodySampleT<WeaponCycleIdleDebtLease>;
using BoltCustodyIdleDebtResult=BoltCustodyResultT<WeaponCycleIdleDebtLease>;
using BoltWeaponIdleDebtCustody=BoltWeaponCustodyT<WeaponCycleIdleDebtLease>;
}
