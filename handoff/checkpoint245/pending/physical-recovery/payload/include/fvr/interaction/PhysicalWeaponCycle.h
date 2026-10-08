#pragma once
#include "fvr/interaction/WeaponCycle.h"

namespace fvr::interaction {
template<class Authority> struct PhysicalWeaponCycleSampleT {
    HandInteractionSample source{};
    Authority lease{};
    math::Matrix4 rawContact{};
    bool grip=false;
    HandClaimToken gun{};
    // Contact is original renderer/controller evidence, never a guided pose.
    HandContactProof contact{};
    std::uint64_t acquireIntent=0;
    // Explicit permission to transfer this exact current support claim when
    // the new native held cycle begins. Other hand roles are never stolen.
    std::optional<HandClaimToken> support;
    // Renderer N-1 contact retains its actual recorded controller packet.
    // The arbiter validates this packet from its history via *From APIs.
    std::optional<HandInteractionSample> contactSource;
};
template<class Authority> struct PhysicalWeaponCycleTargetT {
    Authority lease{};
    HandClaimToken mechanism{},gun{};
    std::uint64_t profile=0,revision=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    math::Matrix4 contact{};
    float travel=0,rotation=0;
    WeaponCycleHandAssignment hands{};
};
template<class Authority> struct PhysicalWeaponCycleResultT {
    WeaponCycleResultT<Authority> cycle{};
    std::optional<PhysicalWeaponCycleTargetT<Authority>> target;
    // Emitted once. A failed adapter submission must explicitly reject the
    // transaction; repeated controller packets never reissue a native call.
    std::optional<WeaponCycleReleaseT<Authority>> release;
    bool ownsMechanism=false;
};
template<class Authority> inline bool CurrentPhysicalWeaponCycleTarget(const PhysicalWeaponCycleTargetT<Authority>& target,
    const Authority& lease,const HandInteractionSample& input,const HandClaim& mechanism,const HandClaim& gun,
    WeaponCycleHandAssignment expectedHands={})noexcept {
    using namespace weapon_cycle_detail;
    return Hands(expectedHands)&&AuthorityForHands(lease,expectedHands)&&AuthorityForHands(target.lease,expectedHands)&&target.hands==expectedHands&&target.profile&&target.revision&&Same(target.lease,lease)&&Lease(target.lease,input.nowNs)&&Lease(lease,input.nowNs)&&
        lease.sequence>=target.lease.sequence&&lease.observedNs>=target.lease.observedNs&&
        (lease.sequence!=target.lease.sequence||(lease.observedNs==target.lease.observedNs&&lease.deadlineNs==target.lease.deadlineNs))&&
        input.owner==lease.owner&&input.focused&&input.tracked[0]&&input.tracked[1]&&!input.released[0]&&!input.released[1]&&
        Window(input.observedNs,input.deadlineNs,input.nowNs)&&target.inputSequence&&target.inputSequence<=input.sequence&&
        target.observedNs<=input.observedNs&&Window(target.observedNs,target.deadlineNs,input.nowNs)&&
        target.deadlineNs<=target.lease.deadlineNs&&target.mechanism==mechanism.token&&target.gun==gun.token&&
        mechanism.inputSequence>=target.inputSequence&&mechanism.inputSequence<=input.sequence&&gun.inputSequence==input.sequence&&
        mechanism.deadlineNs>input.nowNs&&gun.deadlineNs>input.nowNs&&mechanism.deadlineNs<=input.deadlineNs&&gun.deadlineNs<=input.deadlineNs&&
        mechanism.token.owner==input.owner&&gun.token.owner==input.owner&&mechanism.token.id&&gun.token.id&&
        mechanism.token.kind==HandClaimKind::Mechanism&&mechanism.token.hand==expectedHands.mechanism&&
        gun.token.kind==HandClaimKind::GunHold&&gun.token.hand==expectedHands.gun&&
        mechanism.token.item==lease.item&&gun.token.item==lease.item&&mechanism.token.contact==lease.mechanism&&
        mechanism.token.prerequisiteClaim==gun.token.id&&feed_mechanism_detail::Pose(target.contact)&&
        std::isfinite(target.travel)&&target.travel>=0&&std::isfinite(target.rotation);
}

// Serialized portable consumer. Native held/ready receipts, exact profile
// geometry and current GunHold are adapter inputs. This owns only its assigned
// Mechanism claim. It never writes ammunition, invokes native code or decides
// that an unsupported weapon is admitted.
template<class Authority> class PhysicalWeaponCycleT {
public:
    using WeaponCycleLease=Authority;
    using WeaponCycleRelease=WeaponCycleReleaseT<Authority>;
    using WeaponCycleReady=WeaponCycleReadyT<Authority>;
    using WeaponCycleResult=WeaponCycleResultT<Authority>;
    using WeaponCycle=WeaponCycleT<Authority>;
    using PhysicalWeaponCycleSample=PhysicalWeaponCycleSampleT<Authority>;
    using PhysicalWeaponCycleTarget=PhysicalWeaponCycleTargetT<Authority>;
    using PhysicalWeaponCycleResult=PhysicalWeaponCycleResultT<Authority>;
    PhysicalWeaponCycleT()=default;
    PhysicalWeaponCycleT(const PhysicalWeaponCycleT&)=delete;
    PhysicalWeaponCycleT& operator=(const PhysicalWeaponCycleT&)=delete;
    bool Begin(const WeaponCycleProfile& profile,const WeaponCycleLease& lease,std::int64_t now)noexcept {
        if(mechanism_||awaitingOutcome_||!policy_.Begin(profile,lease,now))return false;
        profile_=profile;identity_=lease;target_.reset();submitted_=false;adoptSupport_=weapon_cycle_detail::InitialHeldAdoption(lease);
        previousGrip_=true;lastInput_=0;return true;
    }
    bool ObserveReady(const WeaponCycleReady& ready,std::int64_t now)noexcept {
        if(!submitted_||!policy_.Complete(ready,now))return false;
        target_.reset();awaitingOutcome_=false;return true;
    }
    bool ReconcileReady(const WeaponCycleReady& ready,std::int64_t now)noexcept {
        if(!awaitingOutcome_||!policy_.ReconcileReady(ready,submittedRelease_,now))return false;
        target_.reset();awaitingOutcome_=false;return true;
    }
    void RejectNative(const WeaponCycleRelease& release)noexcept {
        // Adapter proof of rejection/non-dispatch is required. A timeout or
        // missing callback is not permission to discard an ambiguous request.
        if(awaitingOutcome_&&submittedRelease_==release){policy_.Cancel(WeaponCycleFailure::NativeRejected);target_.reset();awaitingOutcome_=false;}
    }
    PhysicalWeaponCycleResult Update(const PhysicalWeaponCycleSample& sample,HandInteraction& hands)noexcept {
        using namespace weapon_cycle_detail;const auto& s=sample.source;
        auto phase=policy_.Phase();
        // An inactive mechanism owns no custody. Other consumers can already
        // have advanced the arbiter clock with this same controller packet.
        // Do not replay this caller's earlier processing time over their shell,
        // magazine, support or gun claims. Global input safety still expires
        // those claims; this path neither renews nor releases them.
        if((phase==WeaponCyclePhase::Idle||phase==WeaponCyclePhase::Complete)&&!mechanism_){
            target_.reset();return Output(policy_.PollNative(s.nowNs),sample,hands);
        }
        auto original=sample.contactSource.value_or(s);original.nowNs=s.nowNs; // Processing time only; source bounds are immutable.
        const bool originalOkay=original.owner==s.owner&&original.sequence<=s.sequence&&original.observedNs<=s.observedNs&&
            original.sequence&&original.focused&&original.tracked[0]&&original.tracked[1]&&!original.released[HandIndex(profile_.hands.gun)]&&
            Window(original.observedNs,original.deadlineNs,s.nowNs);
        const bool grip=sample.contactSource?!original.released[HandIndex(profile_.hands.mechanism)]:sample.grip;
        const auto safety=hands.Update(s);
        const bool freshPacket=original.sequence>lastInput_;
        const bool press=freshPacket&&grip&&!previousGrip_;
        if(freshPacket){lastInput_=original.sequence;previousGrip_=grip;}
        const auto currentGun=hands.Current(profile_.hands.gun);
        const bool gunOkay=currentGun&&currentGun->token==sample.gun&&sample.gun.owner==identity_.owner&&
            sample.gun.kind==HandClaimKind::GunHold&&sample.gun.item==identity_.item&&
            currentGun->inputSequence==s.sequence&&currentGun->deadlineNs>s.nowNs&&currentGun->deadlineNs<=s.deadlineNs;
        const bool inputOkay=safety.inputValid&&s.owner==identity_.owner&&s.focused&&s.tracked[0]&&s.tracked[1]&&
            !s.released[HandIndex(profile_.hands.gun)]&&Window(s.observedNs,s.deadlineNs,s.nowNs)&&gunOkay;
        WeaponCycleInput input{original,sample.rawContact,grip,false};
        if(phase==WeaponCyclePhase::AwaitingNative){
            // Native completion may arrive after focus/tracking/equip changed;
            // preserve its exact transaction while yielding physical custody.
            Drop(s,hands);return Output(policy_.PollNative(s.nowNs),sample,hands);
        }
        if(phase==WeaponCyclePhase::Idle||phase==WeaponCyclePhase::Complete){
            Drop(s,hands);return Output(policy_.PollNative(s.nowNs),sample,hands);
        }
        if(!inputOkay||!originalOkay){
            Drop(s,hands);adoptSupport_=false;
            if(phase!=WeaponCyclePhase::Cancelled)policy_.Cancel(s.owner==identity_.owner?WeaponCycleFailure::Tracking:WeaponCycleFailure::Owner);
            return Output(policy_.PollNative(s.nowNs),sample,hands);
        }
        if(phase==WeaponCyclePhase::Cancelled){
            Drop(s,hands);adoptSupport_=false;
            // Must remain the original held native cycle and timeout. Native
            // evidence loss/owner retirement/rejection cannot be rearmed here.
            if(!freshPacket||!policy_.Regrip(input,sample.lease))return Output(policy_.PollNative(s.nowNs),sample,hands);
            phase=policy_.Phase();
        }
        if(mechanism_){
            const auto held=hands.Current(profile_.hands.mechanism);
            if(!held||held->token!=*mechanism_||!sample.grip||s.released[HandIndex(profile_.hands.mechanism)]||
               !(sample.contactSource?hands.RenewFrom(s,original,*mechanism_,{identity_.mechanism,original.sequence,original.deadlineNs,true}):
                   hands.Renew(s,*mechanism_,{identity_.mechanism,s.sequence,
                       held->inputSequence==s.sequence?held->deadlineNs:s.deadlineNs,true})).accepted){
                Drop(s,hands);adoptSupport_=false;
                return Output(policy_.Cancel(WeaponCycleFailure::Released),sample,hands);
            }
            input.mechanismClaim=true;
        }
        if(phase==WeaponCyclePhase::AwaitingGrip&&!mechanism_){
            const bool contactOkay=sample.contact.key==identity_.mechanism&&sample.contact.inputSequence==original.sequence&&
                sample.contact.deadlineNs>s.nowNs&&sample.contact.deadlineNs<=original.deadlineNs&&sample.contact.eligible&&
                feed_mechanism_detail::Pose(sample.rawContact)&&Distance(sample.rawContact,profile_.closedContact)<=profile_.contactRadius;
            const auto old=hands.Current(profile_.hands.mechanism);
            // Support may retain an earlier still-live render packet while
            // Gather has advanced. TransferFrom checks new contact evidence
            // against history; the old claim's original bounds remain intact.
            const bool supportEvidence=old&&old->inputSequence&&old->inputSequence<=s.sequence&&
                old->deadlineNs>s.nowNs&&old->deadlineNs<=s.deadlineNs;
            // A fresh original grip edge can already have acquired ordinary
            // support in the previous Gather pass. Transfer that exact claim
            // when its delayed renderer contact arrives; a held bit alone
            // still cannot reclaim support after neutral adoption was spent.
            const bool transfer=(adoptSupport_||press)&&sample.support&&old&&old->token==*sample.support&&
                old->token.kind==HandClaimKind::WeaponSupport&&old->token.item==identity_.item&&
                old->token.owner==s.owner&&old->token.prerequisiteClaim==sample.gun.id&&supportEvidence;
            if(grip&&(press||transfer)&&contactOkay&&sample.acquireIntent&&Lease(sample.lease,s.nowNs)&&Same(identity_,sample.lease)){
                const HandClaimRequest request{s.owner,profile_.hands.mechanism,HandClaimKind::Mechanism,identity_.item,
                    sample.contact,sample.acquireIntent,sample.gun.id};
                const auto acquired=sample.contactSource?
                    (transfer?hands.TransferFrom(s,original,*sample.support,request):hands.AcquireFrom(s,original,request)):
                    (transfer?hands.Transfer(s,*sample.support,request):hands.Acquire(s,request));
                adoptSupport_=false;
                if(acquired.accepted&&acquired.claim){
                    mechanism_=acquired.claim->token;input.mechanismClaim=true;
                    if(transfer&&!policy_.AdoptHeld(sample.lease,input,*acquired.claim,*currentGun,&s)){
                        Drop(s,hands);return Output(policy_.Cancel(WeaponCycleFailure::Evidence),sample,hands);
                    }
                }
            }
            if(!grip)adoptSupport_=false;
        }
        auto result=policy_.Update(input,sample.lease);
        if(result.phase==WeaponCyclePhase::Cancelled){Drop(s,hands);adoptSupport_=false;}
        if(result.release&&!submitted_){
            submitted_=awaitingOutcome_=true;submittedRelease_=*result.release;Drop(s,hands);
            auto out=Output(result,sample,hands);out.release=result.release;return out;
        }
        if(result.contact&&mechanism_){
            const auto held=hands.Current(profile_.hands.mechanism);
            if(held&&currentGun)target_=PhysicalWeaponCycleTarget{sample.lease,*mechanism_,sample.gun,profile_.id,profile_.revision,
                original.sequence,original.observedNs,std::min({original.deadlineNs,s.deadlineNs,sample.lease.deadlineNs,held->deadlineNs,currentGun->deadlineNs}),
                *result.contact,result.travel,result.rotation,profile_.hands};
        }
        return Output(result,sample,hands);
    }
    WeaponCyclePhase Phase()const noexcept{return policy_.Phase();}
private:
    void Drop(const HandInteractionSample& sample,HandInteraction& hands)noexcept {
        if(mechanism_)hands.Release(sample,*mechanism_);
        mechanism_.reset();target_.reset();
    }
    PhysicalWeaponCycleResult Output(WeaponCycleResult cycle,const PhysicalWeaponCycleSample& sample,const HandInteraction& hands)noexcept {
        // The recognizer retains its request for exact acknowledgement; the
        // public consumer emits that request only through its one-shot field.
        cycle.release.reset();cycle.contact.reset();
        PhysicalWeaponCycleResult out{cycle};
        const auto mechanism=hands.Current(profile_.hands.mechanism),gun=hands.Current(profile_.hands.gun);
        out.ownsMechanism=mechanism_&&mechanism&&mechanism->token==*mechanism_;
        if(target_&&mechanism&&gun&&CurrentPhysicalWeaponCycleTarget(*target_,sample.lease,sample.source,*mechanism,*gun,profile_.hands))out.target=target_;
        else target_.reset();
        return out;
    }
    WeaponCycle policy_;
    WeaponCycleProfile profile_{};WeaponCycleLease identity_{};WeaponCycleRelease submittedRelease_{};
    std::optional<HandClaimToken> mechanism_;
    std::optional<PhysicalWeaponCycleTarget> target_;
    std::uint64_t lastInput_=0;
    bool previousGrip_=true,submitted_=false,adoptSupport_=false,awaitingOutcome_=false;
};
using PhysicalWeaponCycleSample=PhysicalWeaponCycleSampleT<WeaponCycleLease>;
using PhysicalWeaponCycleTarget=PhysicalWeaponCycleTargetT<WeaponCycleLease>;
using PhysicalWeaponCycleResult=PhysicalWeaponCycleResultT<WeaponCycleLease>;
using PhysicalWeaponCycle=PhysicalWeaponCycleT<WeaponCycleLease>;
using PhysicalWeaponCycleIdleDebtSample=PhysicalWeaponCycleSampleT<WeaponCycleIdleDebtLease>;
using PhysicalWeaponCycleIdleDebtTarget=PhysicalWeaponCycleTargetT<WeaponCycleIdleDebtLease>;
using PhysicalWeaponCycleIdleDebtResult=PhysicalWeaponCycleResultT<WeaponCycleIdleDebtLease>;
using PhysicalWeaponCycleIdleDebt=PhysicalWeaponCycleT<WeaponCycleIdleDebtLease>;
}
