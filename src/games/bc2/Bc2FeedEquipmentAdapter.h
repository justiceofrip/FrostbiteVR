#pragma once
#include "Bc2AmmoResourceBinding.h"
#include "Bc2AmmoResourceEvidence.h"
#include "Bc2AmmoActorRetirement.h"
#include "Bc2ReloadConfigDescriptor.h"
#include "fvr/interaction/FeedEquipmentSession.h"
#include <memory>

namespace fvr::bc2 {
// This is the exact work12 M60 configuration, not a magazine-profile grant.
// The shared Transfer implementation is independently proved; this predicate
// grants neither a native reload cycle nor chamber/fire readiness.
inline bool FeedEquipmentConfigMatches(const ReloadObservedConfig& c)noexcept {
    return ReloadDescriptorText(c.assetName,"M60_sp")&&
        ReloadDescriptorText(c.assetPath,"Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60")&&
        c.weaponData>=0x10000&&c.firingData>=0x10000&&c.primaryFire>=0x10000&&
        std::uint64_t(c.primaryFire)+0x170==c.ammoAddress&&
        c.fireLogicType==2&&c.reloadType==1&&c.fireInputAction==8&&c.reloadInputAction==29&&
        c.baseCapacity==100&&c.numberOfMagazines==2&&c.reloadDelay==0&&c.reloadTime==7.8f&&
        c.reloadThreshold==.7f&&c.postReloadTime==0&&c.boltDelay==0&&c.boltTime==0&&
        !c.holdBoltUntilFireRelease&&!c.holdBoltUntilZoomRelease;
}
inline constexpr std::uint64_t FeedEquipmentM60RigFingerprint=0xa7f219a1426216abull;
class FeedEquipmentIssuedSource;
struct FeedEquipmentNativeView {
    AmmoResourceBinding binding{};
    ReloadHoldIdentity identity{};
    ReloadObservedConfig config{};
    interaction::FeedAmmunitionCounts counts{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // Immutable provenance survives observation-ring rollover only while an
    // owner retains this exact issued view. It never renews native freshness.
    std::shared_ptr<const FeedEquipmentIssuedSource> issuedSource;
    bool operator==(const FeedEquipmentNativeView&)const=default;
};
class FeedEquipmentIssuedSource {
    friend class FeedEquipmentAdapter;
    struct Issuer {};
    std::shared_ptr<const Issuer> issuer_;
    FeedEquipmentNativeView source_;
    FeedEquipmentIssuedSource(std::shared_ptr<const Issuer> issuer,FeedEquipmentNativeView source)
        :issuer_(std::move(issuer)),source_(std::move(source)){source_.issuedSource.reset();}
public:
    FeedEquipmentIssuedSource(const FeedEquipmentIssuedSource&)=delete;
    FeedEquipmentIssuedSource& operator=(const FeedEquipmentIssuedSource&)=delete;
};
// Produced by the physical owner after exact current selected-rig/contact
// admission. These are mod-owned mechanism frames, never stock cover fields.
// A mesh/name match alone cannot supply selectedRigVerified or measured motion.
struct FeedEquipmentGeometryProof {
    interaction::FeedMechanismProfile profile{};
    interaction::FeedMotionProfile motion{};
    std::uint64_t rigFingerprint=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool selectedRigVerified=false;
};
struct FeedEquipmentSubmission {
    interaction::FeedAmmunitionCommand command{};
    FeedEquipmentNativeView source{};
    FeedEquipmentGeometryProof geometry{};
};
enum class FeedEquipmentSubmitResult:std::uint8_t {Submitted,NoEntryRetained,RetentionBusy,Invalid};
// Inventory lifetime, deliberately independent of selection/tracking epochs.
// Enrollment always blocks stock Reload; blocksFire is retained physical debt.
struct FeedEquipmentGate {
    interaction::AmmoResourceIdentity resource{};
    std::uint64_t lifetime=0,revision=0;
    bool blocksFire=true,closedVerified=false;
    bool operator==(const FeedEquipmentGate&)const=default;
};
struct FeedEquipmentNativeCall {
    interaction::FeedAmmunitionCommand command{};
    ReloadHoldIdentity identity{};
    std::uint64_t invocation=0;
    std::int64_t beganNs=0,completedNs=0;
    // Applied physical-only is an owned no-op with actual post-count checks.
    // Rejected is allowed only for an invocation that did not enter Transfer.
    bool applied=false,nativeEntered=false,exact=false,contextUnchanged=false;
};
// Supplied only inside the existing native invocation exclusion. Feed work is
// never represented by an ordinary AmmunitionCommand or ammunition success.
struct FeedEquipmentActorDrain {
    ReloadHoldIdentity expected{};
    std::uint64_t callbackRevision=0,lifetimeEndEpoch=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool callbacksDrained=false;
};
struct FeedEquipmentActorRetirement {
    std::uint64_t event=0,callbackRevision=0,lifetimeEndEpoch=0;
    std::int64_t observedNs=0;
    ReloadHoldIdentity previous{},replacement{};
    std::optional<interaction::FeedAmmunitionCommand> pending;
    std::size_t retiredItems=0,refusedCommands=0;
    bool nativeDispatched=false,ambiguous=false;
};
inline bool FeedEquipmentActorRetirementValid(const FeedEquipmentActorRetirement& r)noexcept {
    return r.event&&r.callbackRevision&&r.lifetimeEndEpoch&&r.observedNs>0&&r.retiredItems&&
        AmmoResourceActorReplacement(r.previous,r.replacement);
}
inline interaction::FeedOwner FeedEquipmentOwner(const ReloadStateOwner& o)noexcept {
    return {o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space};
}
inline bool FeedEquipmentViewFresh(const FeedEquipmentNativeView& v,std::int64_t now)noexcept {
    const auto& c=v.counts;
    if(!FeedEquipmentConfigMatches(v.config)||!AmmoResourceBindingFresh(v.binding,v.identity.owner,now)||
       v.binding.data!=v.config.weaponData||!v.sequence||v.observedNs<=0||now<v.observedNs||now>=v.deadlineNs||
       v.deadlineNs-v.observedNs>100000000||v.deadlineNs<=v.observedNs||
       v.observedNs<v.binding.observedNs||v.deadlineNs>v.binding.deadlineNs||
       c.capacity<=0||c.capacity>1000000||c.loaded<0||c.loaded>c.capacity||c.reserve<0||c.reserve>1000000||
       v.identity.serverPlayer<0x10000||v.identity.serverSoldier<0x10000||v.identity.serverItem<0x10000)return false;
    for(unsigned n=0;n<3;++n){if(v.identity.firing[n]<0x10000)return false;
        for(unsigned j=0;j<n;++j)if(v.identity.firing[n]==v.identity.firing[j])return false;}
    return true;
}
inline bool FeedEquipmentSameItem(const FeedEquipmentNativeView& a,const FeedEquipmentNativeView& b)noexcept {
    return a.identity==b.identity&&a.config==b.config&&a.binding.context==b.binding.context&&
        a.binding.inventory==b.binding.inventory&&a.binding.switching==b.binding.switching&&
        a.binding.data==b.binding.data&&a.binding.persistence==b.binding.persistence;
}
inline bool FeedEquipmentSubmissionValid(const FeedEquipmentSubmission& s,std::int64_t now)noexcept {
    using namespace interaction;using namespace feed_mechanism_detail;
    const auto& c=s.command;const auto& r=c.request;const auto& a=c.authority;const auto& b=c.before;
    const auto& candidate=r.completion.candidate;const auto& g=s.geometry;const auto& p=g.profile;
    if(!FeedEquipmentViewFresh(s.source,r.requestedNs)||!ValidateFeedMechanismProfile(p)||p.maxAgeNs>100000000||
       !g.selectedRigVerified||g.rigFingerprint!=FeedEquipmentM60RigFingerprint||
       !g.inputSequence||g.inputSequence!=r.completion.inputSequence||g.observedNs<=0||
       g.observedNs>r.completion.completedNs||g.deadlineNs<=now||g.deadlineNs-g.observedNs>p.maxAgeNs||
       !feed_interaction_detail::Motion(g.motion)||g.motion.operation!=candidate.operation||
       !r.id||!r.lifetime||!r.completion.id||!Key(r.binding)||r.binding!=a.binding||
       r.requestedNs<r.completion.completedNs||r.completion.completedNs<b.observedNs||now<r.requestedNs||
       now>=b.deadlineNs||!a.verified||!Key(a.key)||a.owner!=FeedEquipmentOwner(s.source.identity.owner)||
       !Key(a.mechanism)||a.lifetime!=r.lifetime||a.profileId!=p.id||a.profileRevision!=p.revision||
       a.operation!=candidate.operation||candidate.owner!=a.owner||candidate.mechanism!=a.mechanism||
       candidate.profileId!=p.id||candidate.profileRevision!=p.revision||candidate.snapshotSequence!=b.sequence||
       candidate.part!=OperationPart(candidate.operation)||!Part(p,unsigned(candidate.part))||
       candidate.role!=p.contacts[unsigned(candidate.part)].role||!Supply(candidate.goal,p.container)||
       b.owner!=a.owner||b.mechanism!=a.mechanism||b.lifetime!=r.lifetime||!b.coherentIdleVerified||
       b.sequence!=s.source.sequence||b.observedNs!=s.source.observedNs||b.deadlineNs!=s.source.deadlineNs||
       b.deadlineNs-b.observedNs>p.maxAgeNs||b.counts!=s.source.counts||
       !(Supply(b.loadedSupply,p.container)||(b.counts.loaded==0&&Empty(b.loadedSupply))))return false;
    switch(candidate.operation){
    case FeedPhysicalOperation::OpenCover:case FeedPhysicalOperation::CloseCover:
        if(g.motion.kind!=FeedMotionKind::Hinge||!Empty(candidate.supply))return false;break;
    case FeedPhysicalOperation::WithdrawFeed:case FeedPhysicalOperation::DetachContainer:
    case FeedPhysicalOperation::AttachContainer:case FeedPhysicalOperation::SeatFeed:
        if(g.motion.kind!=FeedMotionKind::FreePlacement||!Supply(candidate.supply,p.container))return false;break;
    default:return false; // No latch/charge semantics have been proved for M60.
    }
    if(a.effect==FeedAmmunitionEffect::PhysicalOnly)
        return a.rounds==0&&Empty(a.supply)&&!c.mutationRequired&&c.after==b.counts&&c.loadedSupplyAfter==b.loadedSupply;
    if(a.effect!=FeedAmmunitionEffect::RefillLoaded||
       (candidate.operation!=FeedPhysicalOperation::AttachContainer&&candidate.operation!=FeedPhysicalOperation::SeatFeed&&
        candidate.operation!=FeedPhysicalOperation::CloseCover)||
       !Supply(a.supply,p.container)||a.supply!=c.loadedSupplyAfter||
       (!Empty(candidate.supply)&&a.supply!=candidate.supply)||
       (!Empty(b.loadedSupply)&&b.loadedSupply!=a.supply)||a.supply!=candidate.goal)return false;
    const auto refill=PlanNativeAmmoRefill(b.counts.loaded,b.counts.reserve,b.counts.capacity,1);
    return refill&&c.mutationRequired&&a.rounds==refill->units&&
        c.after==FeedAmmunitionCounts{refill->expectedLoaded,refill->expectedReserve,refill->capacity};
}

// Serialized by the runtime's existing native-ammunition gate. This owns a
// full feed command, never a translated AmmunitionCommand/magazine token.
// Pending/uncertain effects survive selection loss and have no timer rollback.
class FeedEquipmentAdapter {
public:
    FeedEquipmentAdapter()=default;
    FeedEquipmentAdapter(const FeedEquipmentAdapter&)=delete;
    FeedEquipmentAdapter& operator=(const FeedEquipmentAdapter&)=delete;
    bool Publish(FeedEquipmentNativeView view,std::int64_t now)noexcept {
        if(sequence_==UINT64_MAX){view_.reset();return false;}
        view.sequence=++sequence_;
        if(!FeedEquipmentViewFresh(view,now)){view_.reset();return false;}
        try{
            if(!issuer_)issuer_=std::make_shared<const FeedEquipmentIssuedSource::Issuer>();
            view.issuedSource.reset();
            view.issuedSource=std::shared_ptr<const FeedEquipmentIssuedSource>(new FeedEquipmentIssuedSource(issuer_,view));
        }catch(...){view_.reset();return false;}
        view_=view;views_[viewCursor_++%views_.size()]=view;return true;
    }
    std::optional<FeedEquipmentNativeView> Read(const ReloadStateOwner& owner,std::int64_t now)const noexcept {
        return view_&&view_->identity.owner==owner&&FeedEquipmentViewFresh(*view_,now)?view_:std::nullopt;
    }
    std::optional<FeedEquipmentNativeView> ReadAt(const ReloadStateOwner& owner,std::int64_t inputObservedNs,
                                                std::int64_t processingNow)const noexcept {
        if(inputObservedNs<=0||processingNow<inputObservedNs||!view_||view_->identity.owner!=owner||
           !FeedEquipmentViewFresh(*view_,processingNow))return {};
        std::optional<FeedEquipmentNativeView> result;
        for(const auto& v:views_)if(v&&v->identity.owner==owner&&v->observedNs<=inputObservedNs&&
            FeedEquipmentSameItem(*v,*view_)&&FeedEquipmentViewFresh(*v,processingNow)&&
            (!result||v->observedNs>result->observedNs||(v->observedNs==result->observedNs&&v->sequence>result->sequence)))result=v;
        return result;
    }
    bool PublishGate(const FeedEquipmentGate& gate,std::int64_t now)noexcept {
        if(!gate.lifetime||!gate.revision||(!gate.blocksFire&&!gate.closedVerified))return false;
        for(auto& slot:gates_)if(slot&&slot->gate.resource==gate.resource){
            if(gate.revision<=slot->gate.revision)return false;
            if(slot->gate.lifetime!=gate.lifetime){
                if(gate.lifetime<=lastEnrollment_||slot->gate.blocksFire||!slot->gate.closedVerified||
                   !view_||!FeedEquipmentViewFresh(*view_,now)||view_->binding.context.resource!=gate.resource)return false;
                if(pending_&&pending_->command.request.lifetime==slot->gate.lifetime)return false;
                for(const auto& debt:refused_)if(debt&&debt->submission.command.request.lifetime==slot->gate.lifetime&&!debt->completed)return false;
                slot=Enrollment{gate,*view_};lastEnrollment_=gate.lifetime;return true;
            }
            if(!gate.blocksFire){
                if(pending_&&pending_->command.request.lifetime==gate.lifetime)return false;
                for(const auto& debt:refused_)if(debt&&debt->submission.command.request.lifetime==gate.lifetime&&!debt->completed)return false;
                if(slot->gate.blocksFire){
                    bool closed=false;
                    for(const auto& r:receipts_)if(r&&r->command.request.lifetime==gate.lifetime&&
                       r->command.request.id>slot->closedRequest&&r->acknowledgement.status==interaction::FeedReloadAcknowledgement::Applied&&
                       r->command.authority.operation==interaction::FeedPhysicalOperation::CloseCover){
                        slot->closedRequest=r->command.request.id;closed=true;
                    }
                    if(!closed)return false;
                }
            }
            slot->gate=gate;return true;
        }
        if(gate.lifetime<=lastEnrollment_||!view_||!FeedEquipmentViewFresh(*view_,now)||
           view_->binding.context.resource!=gate.resource)return false;
        for(auto& slot:gates_)if(!slot){slot=Enrollment{gate,*view_};lastEnrollment_=gate.lifetime;return true;}
        return false; // Never evict live equipment because a bounded table is full.
    }
    std::optional<FeedEquipmentGate> ReadGate(const interaction::AmmoResourceIdentity& resource)const noexcept {
        for(const auto& slot:gates_)if(slot&&slot->gate.resource==resource)return slot->gate;
        return {};
    }
    std::optional<ReloadHoldIdentity> ActorIdentityForReplacement(const ReloadStateOwner& owner)const noexcept {
        std::optional<ReloadHoldIdentity> result;
        for(const auto& slot:gates_)if(slot){const auto& previous=slot->source.identity;
            if((previous.owner.soldier!=owner.soldier||previous.owner.weak!=owner.weak)&&
               owner.actorGeneration>previous.owner.actorGeneration&&
               (!result||previous.owner.actorGeneration<result->owner.actorGeneration))result=previous;
        }
        return result;
    }
    // Selection, space and input changes cannot retire debt. The native caller
    // provides a fresh replacement read and the actual drained callback proof.
    bool RetireForReplacementActor(const Bc2AmmoReserveLease& replacement,std::int64_t now,
                                  const FeedEquipmentActorDrain& drain)noexcept {
        if(!drain.callbacksDrained||!drain.callbackRevision||!drain.lifetimeEndEpoch||lastRetirement_==UINT64_MAX||
           !AmmoResourceReplacementFresh(replacement,now)||!AmmoResourceActorReplacement(drain.expected,replacement.identity)||
           drain.observedNs<replacement.observedNs||drain.observedNs>now||drain.deadlineNs<=now||
           drain.deadlineNs>replacement.deadlineNs||drain.deadlineNs-drain.observedNs>100000000)return false;
        if(const auto previous=Retirement(lastRetirement_?lastRetirement_-1:0);previous&&
           (drain.callbackRevision<previous->callbackRevision||drain.lifetimeEndEpoch<previous->lifetimeEndEpoch||
            drain.observedNs<previous->observedNs))return false;
        bool expected=false;
        for(const auto& slot:gates_)if(slot&&slot->source.identity==drain.expected)expected=true;
        if(!expected)return false;
        const auto matches=[&](const ReloadHoldIdentity& identity){return SameActorEpoch(identity,drain.expected);};
        if(pending_&&matches(pending_->source.identity)&&
           (pending_->command.request.requestedNs>drain.observedNs||
            (call_&&call_->completedNs>drain.observedNs)))return false;
        for(const auto& debt:refused_)if(debt&&matches(debt->submission.source.identity)&&debt->retainedNs>drain.observedNs)return false;
        FeedEquipmentActorRetirement proof;
        proof.event=lastRetirement_+1;proof.callbackRevision=drain.callbackRevision;proof.lifetimeEndEpoch=drain.lifetimeEndEpoch;
        proof.observedNs=drain.observedNs;proof.previous=drain.expected;proof.replacement=replacement.identity;
        if(pending_&&matches(pending_->source.identity)){
            proof.pending=pending_->command;proof.nativeDispatched=call_&&call_->nativeEntered;
            proof.ambiguous=call_.has_value()||uncertain_;pending_.reset();
            seen_={};invocations_={};mask_=0;server_=false;uncertain_=false;
        }
        if(call_&&matches(call_->identity))call_.reset();
        for(auto& debt:refused_)if(debt&&matches(debt->submission.source.identity)){
            if(!debt->completed){++proof.refusedCommands;proof.ambiguous=proof.ambiguous||debt->uncertain;}
            debt.reset();
        }
        for(auto& slot:gates_)if(slot&&matches(slot->source.identity)){slot.reset();++proof.retiredItems;}
        if(view_&&matches(view_->identity))view_.reset();
        for(auto& view:views_)if(view&&matches(view->identity))view.reset();
        retirements_[retirementCursor_++%retirements_.size()]=proof;lastRetirement_=proof.event;return true;
    }
    std::optional<FeedEquipmentActorRetirement> Retirement(std::uint64_t afterEvent)const noexcept {
        std::optional<FeedEquipmentActorRetirement> result;
        for(const auto& row:retirements_)if(row&&row->event>afterEvent&&(!result||row->event<result->event))result=row;
        return result;
    }
    bool Submit(const FeedEquipmentSubmission& s,std::int64_t now)noexcept {
        return SubmitResult(s,now)==FeedEquipmentSubmitResult::Submitted;
    }
    FeedEquipmentSubmitResult SubmitResult(const FeedEquipmentSubmission& s,std::int64_t now,bool noEntryOnly=false)noexcept {
        using Result=FeedEquipmentSubmitResult;
        if(now<s.command.request.requestedNs||!Issued(s.source)||
           !FeedEquipmentSubmissionValid(s,s.command.request.requestedNs))return Result::Invalid;
        if(pending_&&pending_->command.request.lifetime==s.command.request.lifetime&&pending_->command.request.id==s.command.request.id)
            return Result::Invalid; // An admitted command must never enter the no-entry path.
        for(const auto& debt:refused_)if(debt&&debt->submission.command.request==s.command.request)
            return debt->submission.command==s.command&&debt->submission.source==s.source?Result::NoEntryRetained:Result::Invalid;
        Enrollment* enrollment=nullptr;
        for(auto& slot:gates_)if(slot&&slot->gate.lifetime==s.command.request.lifetime&&
           slot->gate.resource==s.source.binding.context.resource)enrollment=&*slot;
        if(!enrollment||s.command.request.id<=enrollment->lastRequest)return Result::Invalid;
        bool known=false;
        for(const auto& source:views_)if(source&&*source==s.source)known=true;
        if(!known&&!noEntryOnly)return Result::Invalid;
        enrollment->gate.blocksFire=true;
        const bool admissible=!noEntryOnly&&!pending_&&view_&&FeedEquipmentSubmissionValid(s,now)&&
            FeedEquipmentViewFresh(*view_,now)&&FeedEquipmentSameItem(s.source,*view_)&&
            view_->sequence>=s.source.sequence&&view_->counts==s.source.counts;
        if(admissible){
            enrollment->lastRequest=s.command.request.id;enrollment->gate.blocksFire=true;
            pending_=s;call_.reset();seen_={};invocations_={};mask_=0;server_=false;uncertain_=false;return Result::Submitted;
        }
        for(auto& debt:refused_)if(!debt||debt->completed){
            debt=NoEntry{s,now};enrollment->lastRequest=s.command.request.id;enrollment->gate.blocksFire=true;
            return Result::NoEntryRetained;
        }
        return Result::RetentionBusy; // Caller retains exact proof and retries noEntryOnly.
    }
    const auto& Pending()const noexcept{return pending_;}
    bool Invoked()const noexcept{return call_.has_value();}
    bool Uncertain()const noexcept{return uncertain_;}
    bool Current(const FeedEquipmentNativeView& adjacent,std::int64_t now)const noexcept {
        return pending_&&FeedEquipmentViewFresh(adjacent,now)&&FeedEquipmentSameItem(pending_->source,adjacent)&&
            adjacent.sequence>=pending_->source.sequence&&adjacent.counts==pending_->command.before.counts;
    }
    // Call is recorded at the real server Update, after exact native/context
    // postchecks (or an explicit no-entry rejection). Even a failed call is
    // retained: it can never be retried or erased on a deadline.
    bool Call(const FeedEquipmentNativeCall& call)noexcept {
        if(!pending_||call_||call.command!=pending_->command||call.identity!=pending_->source.identity||
           !call.invocation||call.beganNs<pending_->command.request.requestedNs||call.completedNs<call.beganNs)return false;
        call_=call;
        const bool refill=call.command.authority.effect==interaction::FeedAmmunitionEffect::RefillLoaded;
        uncertain_=!call.exact||!call.contextUnchanged||(!call.applied&&call.nativeEntered)||
            (call.applied&&(refill!=call.nativeEntered))||
            (call.applied&&(call.beganNs>=call.command.before.deadlineNs||call.beganNs>=pending_->geometry.deadlineNs));
        return !uncertain_;
    }
    bool Observe(const AmmoResourceOwnUpdate& r,std::int64_t now)noexcept {
        const bool reconciled=ObserveNoEntry(r,now);
        if(!pending_||!call_||uncertain_||r.branch>=3||r.firing!=call_->identity.firing[r.branch]||r.endNs<call_->completedNs)return reconciled;
        const auto& c=call_->command;const auto& identity=call_->identity;
        const auto counts=call_->applied?c.after:c.before.counts;
        const auto fail=[&]{uncertain_=true;return false;};
        if(r.beforeOwner!=identity.owner||r.afterOwner!=identity.owner||!r.finished||!r.identityRetained||
           !r.invocation||r.depth!=1||r.beginNs<=0||r.endNs<r.beginNs||r.endNs>now||
           !r.neutralContext||r.animationHold||r.current!=2||r.next!=2||r.timer!=0)return fail();
        if(r.branch==2&&(r.serverPlayer!=identity.serverPlayer||r.serverSoldier!=identity.serverSoldier||r.serverItem!=identity.serverItem))return fail();
        if(r.branch==2&&r.invocation==call_->invocation){
            if(r.beginNs>call_->beganNs||r.endNs<call_->completedNs||r.loadedBefore!=c.before.counts.loaded||
               r.reserveBefore!=c.before.counts.reserve||r.loadedAfter!=counts.loaded||r.reserveAfter!=counts.reserve)return fail();
            server_=true;
        }
        if(r.endNs<seen_[r.branch])return false;
        const bool expected=r.loadedAfter==counts.loaded&&r.reserveAfter==counts.reserve;
        const bool baseline=r.loadedAfter==c.before.counts.loaded&&r.reserveAfter==c.before.counts.reserve;
        if(!expected){if(!baseline||(mask_&(1u<<r.branch)))return fail();return false;}
        for(unsigned n=0;n<3;++n)if(n!=r.branch&&invocations_[n]==r.invocation)return fail();
        seen_[r.branch]=r.endNs;invocations_[r.branch]=r.invocation;mask_|=1u<<r.branch;
        if(!server_||mask_!=7||sequence_==UINT64_MAX)return false;
        const auto age=std::min<std::int64_t>(100000000,pending_->geometry.profile.maxAgeNs);
        for(auto t:seen_)if(now<t||now-t>=age)return false;
        const auto completed=*std::max_element(seen_.begin(),seen_.end());
        if(completed<=c.request.requestedNs)return false;
        auto post=c.before;post.sequence=++sequence_;post.observedNs=completed;post.deadlineNs=completed+age;
        post.counts=counts;post.loadedSupply=call_->applied?c.loadedSupplyAfter:c.before.loadedSupply;
        const interaction::FeedReloadAck ack{c.request,call_->applied?interaction::FeedReloadAcknowledgement::Applied:
            interaction::FeedReloadAcknowledgement::Rejected,post.sequence,post.observedNs};
        receipts_[receiptCursor_++%receipts_.size()]=interaction::FeedAmmunitionReceipt{
            c,ack,post,call_->invocation,call_->beganNs,completed,true,true,!call_->applied&&!call_->nativeEntered};
        pending_.reset();return true;
    }
    std::optional<interaction::FeedAmmunitionReceipt> Receipt(std::uint64_t lifetime,std::uint64_t request)const noexcept {
        for(const auto& r:receipts_)if(r&&r->command.request.lifetime==lifetime&&r->command.request.id==request)return r;
        return {};
    }
private:
    static bool SameActorEpoch(const ReloadHoldIdentity& a,const ReloadHoldIdentity& b)noexcept {
        return a.owner.actorGeneration==b.owner.actorGeneration&&AmmoResourceSameNativeActor(a,b);
    }
    bool Issued(const FeedEquipmentNativeView& source)const noexcept {
        if(!issuer_||!source.issuedSource||source.issuedSource->issuer_!=issuer_)return false;
        auto exact=source;exact.issuedSource.reset();return exact==source.issuedSource->source_;
    }
    bool ObserveNoEntry(const AmmoResourceOwnUpdate& row,std::int64_t now)noexcept {
        bool completedAny=false;
        for(auto& slot:refused_){
            if(!slot||slot->completed||slot->uncertain||row.branch>=3)continue;
            auto& debt=*slot;const auto& s=debt.submission;const auto& c=s.command;const auto& identity=s.source.identity;
            if(row.firing!=identity.firing[row.branch]||row.beginNs<debt.retainedNs)continue;
            const auto counts=c.before.counts;
            if(!row.finished||!row.invocation||row.depth!=1||row.endNs<row.beginNs||row.endNs>now)continue;
            if(row.beforeOwner!=identity.owner||row.afterOwner!=identity.owner||!row.identityRetained||
               row.loadedBefore!=counts.loaded||row.reserveBefore!=counts.reserve||
               row.loadedAfter!=counts.loaded||row.reserveAfter!=counts.reserve||
               (row.branch==2&&(row.serverPlayer!=identity.serverPlayer||row.serverSoldier!=identity.serverSoldier||row.serverItem!=identity.serverItem))){
                debt.uncertain=true;continue;
            }
            // Busy input is not a no-entry receipt and not lost ownership.
            // Invalidate its old vote; a genuinely later neutral Update may
            // establish unchanged observation without repeating the command.
            if(!row.neutralContext||row.animationHold||row.current!=2||row.next!=2||row.timer!=0){
                debt.seen[row.branch]=0;debt.invocations[row.branch]=0;continue;
            }
            if(row.endNs<=debt.seen[row.branch])continue;
            bool duplicate=false;
            for(unsigned n=0;n<3;++n)if(n!=row.branch&&debt.invocations[n]==row.invocation)duplicate=true;
            if(duplicate){debt.uncertain=true;continue;}
            debt.seen[row.branch]=row.endNs;debt.invocations[row.branch]=row.invocation;
            if(row.branch==2)debt.serverBeganNs=row.beginNs;
            const auto age=std::min<std::int64_t>(100000000,s.geometry.profile.maxAgeNs);
            bool fresh=true;for(auto observed:debt.seen)if(observed<=0||now<observed||now-observed>=age)fresh=false;
            if(!fresh||sequence_==UINT64_MAX||!view_||!FeedEquipmentViewFresh(*view_,now)||
               !FeedEquipmentSameItem(s.source,*view_)||view_->counts!=counts)continue;
            const auto completed=*std::max_element(debt.seen.begin(),debt.seen.end());
            if(completed<=c.request.requestedNs)continue;
            auto post=c.before;post.sequence=++sequence_;post.observedNs=completed;post.deadlineNs=completed+age;
            const interaction::FeedReloadAck ack{c.request,interaction::FeedReloadAcknowledgement::Rejected,post.sequence,completed};
            receipts_[receiptCursor_++%receipts_.size()]=interaction::FeedAmmunitionReceipt{
                c,ack,post,debt.invocations[2],debt.serverBeganNs,completed,true,true,true};
            debt.completed=true;completedAny=true;
        }
        return completedAny;
    }
    struct Enrollment {FeedEquipmentGate gate{};FeedEquipmentNativeView source{};std::uint64_t lastRequest=0,closedRequest=0;};
    struct NoEntry {
        FeedEquipmentSubmission submission{};std::int64_t retainedNs=0,serverBeganNs=0;
        std::array<std::int64_t,3> seen{};std::array<std::uint64_t,3> invocations{};
        bool uncertain=false,completed=false;
    };
    std::optional<FeedEquipmentNativeView> view_;
    std::array<std::optional<FeedEquipmentNativeView>,64> views_{};
    std::array<std::optional<Enrollment>,16> gates_{};
    std::array<std::optional<NoEntry>,16> refused_{};
    std::optional<FeedEquipmentSubmission> pending_;
    std::optional<FeedEquipmentNativeCall> call_;
    std::array<std::optional<interaction::FeedAmmunitionReceipt>,64> receipts_{};
    std::array<std::optional<FeedEquipmentActorRetirement>,64> retirements_{};
    std::shared_ptr<const FeedEquipmentIssuedSource::Issuer> issuer_;
    std::size_t viewCursor_=0,receiptCursor_=0,retirementCursor_=0;
    std::array<std::int64_t,3> seen_{};std::array<std::uint64_t,3> invocations_{};
    std::uint64_t sequence_=0,lastEnrollment_=0,lastRetirement_=0;
    unsigned mask_=0;bool server_=false,uncertain_=false;
};
}
