#pragma once
#include "Bc2AmmoResourceService.h"
#include "Bc2PistolAfterFeedInput.h"
#include "Bc2ReloadConfigDescriptor.h"
#include "fvr/interaction/PistolAfterFeedPolicy.h"
#include <array>

namespace fvr::bc2 {
// Native20 proof01 SHA7877e01f24a8e9d374f0109119bdd210083acecb7eeded1bf519eef4e9b67561.
// This descriptor only selects the sidecar. Existing native admission must
// still verify code, live timing/configuration, finite ammo and ownership.
inline constexpr ReloadConfigDescriptor PistolAfterFeedDescriptor{
    "MP443_sp","Objects/Weapons/Handheld/RU_hg_MP443Grach/SP_hg_MP443Grach",
    {0,1,8,29,17,3,0,3.75f,.5f,0,0,0,false,false},{},ReloadDescriptorAdmission::ReviewedNative};
inline bool PistolAfterFeedConfigMatches(const ReloadObservedConfig& c)noexcept {
    return MatchesReloadDescriptor(c,PistolAfterFeedDescriptor);
}
struct PistolAfterFeedNativeBits {
    std::uint8_t inhibit8a=0,releaseA4=0,releaseA5=0;
    bool verified=false;
    bool operator==(const PistolAfterFeedNativeBits&)const=default;
    bool Ready()const noexcept{return verified&&!inhibit8a&&!releaseA4&&!releaseA5;}
};
// Initial mod-owned slide status for a newly enrolled, already loaded weapon.
// Trigger/zoom release latches require a later neutral Update, not a new slide
// stroke. They remain mandatory in the separate post-feed firing witness.
inline bool PistolAfterFeedInitialPolicyReady(const PistolAfterFeedNativeBits& bits,bool finite,
    unsigned current,unsigned next,float timer,int loaded,int reserve,const interaction::AmmunitionCounts& expected)noexcept {
    return bits.verified&&finite&&current==2&&next==2&&timer==0&&expected.capacity==17&&
        expected.loaded>0&&expected.loaded<=expected.capacity&&expected.reserve>=0&&loaded==expected.loaded&&reserve==expected.reserve;
}
struct PistolAfterFeedOwnUpdate {
    AmmoResourceOwnUpdate own{};
    ReloadHoldIdentity identity{};
    interaction::AmmoResourceContext context{};
    PistolAfterFeedNativeBits before{},after{};
    std::uint32_t beforeFiring=0,beforeServerPlayer=0,beforeServerSoldier=0,beforeServerItem=0;
    std::uint64_t nativeInvocation=0,nativeParent=0,nativeUpdate=0;
    PistolAfterFeedInputReceipt input{};
    bool effectiveContextVerified=false,finiteAmmoVerified=false;
    bool operator==(const PistolAfterFeedOwnUpdate&)const=default;
};
struct PistolAfterFeedReadiness {
    interaction::PistolAfterFeedNativeReady semantic{};
    ReloadHoldIdentity feedIdentity{},currentIdentity{};
    std::array<PistolAfterFeedOwnUpdate,3> branches{};
};
struct PistolAfterFeedRelease {
    interaction::PistolAfterFeedClearance clearance{};
};
enum class PistolAfterFeedEventKind : std::uint8_t {Admitted,Completed,RejectedBeforeNative,ActorRetired};
struct PistolAfterFeedEvent {
    std::uint64_t event=0;
    PistolAfterFeedEventKind kind=PistolAfterFeedEventKind::Admitted;
    AmmoResourceRequest request{};
    interaction::AmmunitionCommand command{};
    ReloadHoldIdentity identity{};
    interaction::AmmunitionSnapshot admission{};
    bool initialPolicyReadyVerified=false;
    std::optional<AmmoResourceOutcome> outcome;
};
struct PistolAfterFeedActorRetirement {
    std::uint64_t event=0,callbackRevision=0,lifetimeEndEpoch=0;
    std::int64_t observedNs=0;
    ReloadHoldIdentity previous{},replacement{};
    std::array<interaction::AmmoResourceIdentity,32> resources{};
    std::size_t retiredItems=0;
};
struct PistolAfterFeedResourceState {
    interaction::AmmoResourceIdentity resource{};
    AmmoResourceRequest request{};
    interaction::AmmunitionCommand command{};
    ReloadHoldIdentity admittedIdentity{};
    interaction::AmmunitionSnapshot admission{};
    bool initialPolicyReadyVerified=false;
    std::optional<AmmoResourceOutcome> removal,feed;
    std::optional<PistolAfterFeedReadiness> ready;
    bool blocksFire=true,emptyDebt=false,pending=false;
};
inline bool PistolAfterFeedIdentityValid(const ReloadHoldIdentity& i)noexcept {
    const auto& o=i.owner;
    if(o.player<0x10000||o.soldier<0x10000||o.weak<0x10000||o.weapon<0x10000||
       !o.actorGeneration||!o.equipGeneration||!o.space||i.serverPlayer<0x10000||i.serverSoldier<0x10000||i.serverItem<0x10000)return false;
    for(unsigned n=0;n<3;++n){if(i.firing[n]<0x10000)return false;
        for(unsigned p=0;p<n;++p)if(i.firing[n]==i.firing[p])return false;}
    return true;
}
inline bool PistolAfterFeedContextMatches(const interaction::AmmoResourceContext& c,const ReloadHoldIdentity& i)noexcept {
    return PistolAfterFeedIdentityValid(i)&&c.resource.actor==i.owner.soldier&&c.resource.actorGeneration==i.owner.actorGeneration&&
        c.resource.weapon==i.owner.weapon&&c.resource.weaponGeneration&&c.equipGeneration==i.owner.equipGeneration&&c.space==i.owner.space;
}
inline bool PistolAfterFeedOutcomeExact(const AmmoResourceOutcome& o,const AmmoResourceRequest& request,
    const interaction::AmmunitionCommand& command,const ReloadHoldIdentity& identity)noexcept {
    if(o.state!=AmmoResourceRequestState::Completed||!o.receipt||o.request!=request||request.discard||
       !request.intent.id||request.intent.context!=command.context||request.intent.operation!=command.operation||
       request.binding.context!=command.context||request.binding.owner!=identity.owner||
       !PistolAfterFeedContextMatches(command.context,identity))return false;
    const auto& r=*o.receipt;
    return r.command==command&&command.id&&command.sourceSequence&&r.authorityInvocation&&
        r.authorityVerified&&r.copiesVerified&&r.before==command.before&&r.after==command.after&&
        r.beganNs>=command.requestedNs&&r.beganNs<command.admissionDeadlineNs&&
        r.completedNs>=r.beganNs&&r.completedNs<command.deadlineNs&&o.resolvedNs>=r.completedNs;
}
// Bounded persistent sidecar. All methods run under the adapter's existing
// ammunition gate; no method calls native code or consumes controller input.
class PistolAfterFeedEvidence {
    struct Slot {
        PistolAfterFeedResourceState state{};
        ReloadHoldIdentity feedIdentity{},cohortIdentity{};
        std::array<std::optional<PistolAfterFeedOwnUpdate>,3> rows{};
        std::array<std::optional<interaction::PistolAfterFeedNativeReady>,4> issued{};
        unsigned nextIssued=0;
        bool occupied=false,previousBlocked=false;
        std::uint64_t lastOutcome=0,sequence=0;
    };
public:
    bool Admit(const AmmoResourceRequest& r,const interaction::AmmunitionCommand& c,const ReloadHoldIdentity& identity,
        const interaction::AmmunitionSnapshot& admission,bool initialPolicyReadyVerified)noexcept {
        using interaction::AmmunitionOperation;
        AmmoResourceCompletion commandValidator;
        if(r.discard||!r.intent.id||!c.id||!c.sourceSequence||r.intent.context!=c.context||
           r.intent.operation!=c.operation||r.intent.original!=c.original||r.binding.context!=c.context||r.binding.owner!=identity.owner||
           !commandValidator.Begin(c,identity)||r.intent.observedNs<=0||r.intent.observedNs>c.requestedNs||
           c.requestedNs>=r.intent.deadlineNs||c.admissionDeadlineNs>r.intent.deadlineNs||
           !PistolAfterFeedContextMatches(c.context,identity)||admission.context!=c.context||admission.sequence!=c.sourceSequence||
           admission.counts!=c.before||!admission.coherentIdleVerified||admission.observedNs<=0||
           admission.observedNs>c.requestedNs||admission.deadlineNs<=c.requestedNs||
           c.before.capacity!=17||c.after.capacity!=17||
           c.before.loaded<0||c.before.reserve<0||c.after.loaded<0||c.after.reserve<0||
           (c.operation!=AmmunitionOperation::RemoveMagazine&&c.operation!=AmmunitionOperation::ReturnMagazine&&
            c.operation!=AmmunitionOperation::RefillMagazine))return false;
        auto* slot=Find(c.context.resource);
        if(!slot){for(auto& candidate:slots_)if(!candidate.occupied){slot=&candidate;slot->occupied=true;
            slot->state.resource=c.context.resource;slot->state.blocksFire=false;break;}}
        if(!slot||slot->state.pending||r.intent.id<=slot->lastOutcome)return false;
        slot->previousBlocked=slot->state.blocksFire;slot->state.request=r;slot->state.command=c;
        slot->state.admittedIdentity=identity;slot->state.pending=true;slot->state.blocksFire=true;
        slot->state.admission=admission;slot->state.initialPolicyReadyVerified=initialPolicyReadyVerified;
        slot->rows={};slot->issued={};slot->state.ready.reset();
        Append(PistolAfterFeedEventKind::Admitted,*slot,{});return true;
    }
    bool Outcome(const AmmoResourceOutcome& o)noexcept {
        using interaction::AmmunitionOperation;
        auto* slot=Find(o.request.intent.context.resource);
        if(!slot||!slot->state.pending||o.request!=slot->state.request)return false;
        auto& s=slot->state;
        if(o.state==AmmoResourceRequestState::Rejected&&!o.nativeDispatched&&!o.receipt){
            s.pending=false;s.blocksFire=slot->previousBlocked;slot->lastOutcome=o.request.intent.id;
            Append(PistolAfterFeedEventKind::RejectedBeforeNative,*slot,o);return true;
        }
        if(!PistolAfterFeedOutcomeExact(o,s.request,s.command,s.admittedIdentity))return false;
        s.pending=false;slot->lastOutcome=o.request.intent.id;slot->rows={};s.ready.reset();
        if(s.command.operation==AmmunitionOperation::RemoveMagazine){
            s.removal=o;s.feed.reset();s.emptyDebt=s.emptyDebt||o.receipt->before.loaded==0;
        }else {
            // Both return and refill begin at loaded0. Only the actual retained
            // removal can establish a tactical lineage; a missing one blocks.
            const bool lineage=s.removal&&s.removal->receipt&&
                s.removal->request.intent.context.resource==s.resource&&
                s.removal->receipt->completedNs<=o.receipt->beganNs&&
                (s.command.operation!=AmmunitionOperation::ReturnMagazine||
                 (s.command.original&&s.command.original->id==s.removal->receipt->command.id&&
                  s.command.original->rounds==s.removal->receipt->before.loaded));
            s.emptyDebt=s.emptyDebt||!lineage;s.feed=o;slot->feedIdentity=s.admittedIdentity;
        }
        Append(PistolAfterFeedEventKind::Completed,*slot,o);return true;
    }
    bool Observe(const PistolAfterFeedOwnUpdate& r)noexcept {
        auto* slot=Find(r.context.resource);if(!slot||!slot->state.feed||slot->state.pending)return false;
        auto& s=slot->state;const auto& feed=*s.feed->receipt;const auto& u=r.own;
        if(!PistolAfterFeedContextMatches(r.context,r.identity)||!AmmoResourceSameNativeActor(r.identity,slot->feedIdentity)||
           r.identity.owner.actorGeneration!=slot->feedIdentity.owner.actorGeneration||
           r.identity.owner.weapon!=slot->feedIdentity.owner.weapon||u.branch>=3)return false;
        if(slot->cohortIdentity!=r.identity){slot->cohortIdentity=r.identity;slot->rows={};slot->issued={};s.ready.reset();}
        // Every rejected newer own event invalidates that branch's old witness.
        if(slot->rows[u.branch]&&u.invocation<=slot->rows[u.branch]->own.invocation){
            if(u.invocation==slot->rows[u.branch]->own.invocation&&r!=*slot->rows[u.branch]){slot->rows[u.branch].reset();s.ready.reset();}
            return false;
        }
        if(slot->rows[u.branch]&&u.endNs<slot->rows[u.branch]->own.endNs)return false;
        slot->rows[u.branch].reset();s.ready.reset();
        if(!u.invocation||u.invocation==feed.authorityInvocation||r.nativeInvocation!=u.invocation||r.nativeParent||r.nativeUpdate!=u.invocation||
           u.beginNs<feed.completedNs||u.endNs<u.beginNs||
           u.depth!=1||!u.finished||!u.identityRetained||!u.neutralContext||u.animationHold||
           !r.effectiveContextVerified||!r.finiteAmmoVerified||!r.before.verified||!r.after.Ready()||
           u.beforeOwner!=r.identity.owner||u.afterOwner!=r.identity.owner||
           u.firing!=r.identity.firing[u.branch]||r.beforeFiring!=u.firing||
           u.current!=2||u.next!=2||u.timer!=0||u.loadedBefore!=feed.after.loaded||u.loadedAfter!=feed.after.loaded||
           u.reserveBefore!=feed.after.reserve||u.reserveAfter!=feed.after.reserve||u.loadedAfter<=0)return false;
        if(u.branch==2&&(u.serverPlayer!=r.identity.serverPlayer||u.serverSoldier!=r.identity.serverSoldier||
           u.serverItem!=r.identity.serverItem||r.beforeServerPlayer!=u.serverPlayer||
           r.beforeServerSoldier!=u.serverSoldier||r.beforeServerItem!=u.serverItem))return false;
        for(unsigned n=0;n<3;++n)if(n!=u.branch&&slot->rows[n]&&slot->rows[n]->own.invocation==u.invocation)return false;
        slot->rows[u.branch]=r;if(slot->sequence!=UINT64_MAX)++slot->sequence;return true;
    }
    std::optional<PistolAfterFeedResourceState> Read(const interaction::AmmoResourceIdentity& resource,std::int64_t now)noexcept {
        auto* slot=Find(resource);if(!slot)return {};auto state=slot->state;
        state.ready=Ready(*slot,now);
        if(state.ready){bool known=false;for(const auto& issued:slot->issued)known=known||(issued&&*issued==state.ready->semantic);
            if(!known)slot->issued[slot->nextIssued++%slot->issued.size()]=state.ready->semantic;}
        return state;
    }
    bool Release(const PistolAfterFeedRelease& release,std::int64_t now)noexcept {
        const auto& clearance=release.clearance;const auto& feed=clearance.feed;
        auto* slot=Find(feed.receipt.command.context.resource);
        if(!slot||slot->state.pending||!slot->state.feed||!slot->state.feed->receipt||
           feed.intent!=slot->state.feed->request.intent||feed.receipt!=*slot->state.feed->receipt)return false;
        const auto current=Ready(*slot,now);
        bool issued=false;for(const auto& witness:slot->issued)issued=issued||(witness&&*witness==clearance.nativeReady);
        if(!current||!issued||clearance.nativeReady.context!=current->semantic.context||
           now<clearance.nativeReady.observedNs||now>=clearance.nativeReady.deadlineNs)return false;
        if(slot->state.emptyDebt){
            if(!clearance.stroke)return false;const auto& stroke=*clearance.stroke;
            if(stroke.feed!=feed||stroke.context.resource!=slot->state.resource||
               !stroke.request||!stroke.profile||!stroke.revision||!stroke.inputSequence||
               stroke.observedNs<feed.receipt.completedNs||stroke.deadlineNs<=stroke.observedNs||
               !stroke.mechanism.id||!stroke.gun.id||stroke.mechanism.kind!=interaction::HandClaimKind::Mechanism||
               stroke.gun.kind!=interaction::HandClaimKind::GunHold||stroke.mechanism.prerequisiteClaim!=stroke.gun.id||
               stroke.mechanism.owner!=stroke.gun.owner||stroke.mechanism.item!=stroke.gun.item||
               stroke.mechanism.hand==stroke.gun.hand||stroke.gun.owner.actorGeneration!=stroke.context.resource.actorGeneration||
               stroke.gun.owner.actor!=((std::uint64_t(slot->feedIdentity.owner.weak)<<32)|slot->feedIdentity.owner.soldier)||
               !stroke.gun.owner.equipGeneration||stroke.gun.item.id!=stroke.context.resource.weapon||
               stroke.gun.item.generation!=stroke.gun.owner.equipGeneration||stroke.gun.owner.space!=stroke.context.space)return false;
            for(const auto& row:clearance.nativeReady.updates)if(row.beganNs<stroke.observedNs)return false;
        }else if(!clearance.retainedPolicyReady&&!clearance.stroke)return false;
        slot->state.blocksFire=false;slot->state.emptyDebt=false;return true;
    }
    bool Blocks(const interaction::AmmoResourceIdentity& resource)const noexcept {
        const auto* s=Find(resource);return s&&s->state.blocksFire;
    }
    std::optional<ReloadHoldIdentity> ActorForReplacement(const ReloadStateOwner& owner)const noexcept {
        for(const auto& s:slots_)if(s.occupied&&(s.state.admittedIdentity.owner.soldier!=owner.soldier||
            s.state.admittedIdentity.owner.weak!=owner.weak))return s.state.admittedIdentity;return {};
    }
    bool Retire(const Bc2AmmoReserveLease& replacement,const AmmoResourceActorDrain& drain,std::int64_t now)noexcept {
        if(!drain.callbacksDrained||!drain.callbackRevision||!drain.lifetimeEndEpoch||drain.observedNs<=0||
           now<drain.observedNs||now>=drain.deadlineNs||drain.deadlineNs-drain.observedNs>100000000||
           !AmmoResourceReplacementFresh(replacement,now)||!AmmoResourceActorReplacement(drain.expected,replacement.identity)||
           retirementSequence_==UINT64_MAX)return false;
        PistolAfterFeedActorRetirement retired;retired.event=++retirementSequence_;retired.callbackRevision=drain.callbackRevision;
        retired.lifetimeEndEpoch=drain.lifetimeEndEpoch;retired.observedNs=now;retired.previous=drain.expected;retired.replacement=replacement.identity;
        for(auto& slot:slots_)if(slot.occupied&&AmmoResourceSameNativeActor(slot.state.admittedIdentity,drain.expected)&&
            slot.state.resource.actorGeneration==drain.expected.owner.actorGeneration){
            retired.resources[retired.retiredItems++]=slot.state.resource;
            Append(PistolAfterFeedEventKind::ActorRetired,slot,{});slot={};
        }
        if(!retired.retiredItems)return false;retirements_[(retired.event-1)%retirements_.size()]=retired;return true;
    }
    std::optional<PistolAfterFeedEvent> Event(std::uint64_t after)const noexcept {
        const PistolAfterFeedEvent* found=nullptr;for(const auto& e:events_)if(e.event>after&&(!found||e.event<found->event))found=&e;
        return found?std::optional{*found}:std::nullopt;
    }
    std::optional<PistolAfterFeedActorRetirement> Retirement(std::uint64_t after)const noexcept {
        const PistolAfterFeedActorRetirement* found=nullptr;for(const auto& e:retirements_)if(e.event>after&&(!found||e.event<found->event))found=&e;
        return found?std::optional{*found}:std::nullopt;
    }
private:
    static std::optional<PistolAfterFeedReadiness> Ready(const Slot& slot,std::int64_t now)noexcept {
        if(!slot.state.feed||!slot.state.feed->receipt||!slot.sequence||slot.state.pending)return {};
        PistolAfterFeedReadiness out;out.feedIdentity=slot.feedIdentity;out.currentIdentity=slot.cohortIdentity;
        out.semantic.feed={slot.state.feed->request.intent,*slot.state.feed->receipt};out.semantic.sequence=slot.sequence;
        std::int64_t oldest=INT64_MAX,latest=0;
        for(unsigned n=0;n<3;++n){if(!slot.rows[n])return {};const auto& r=*slot.rows[n];const auto& u=r.own;
            if(now<u.endNs||now-u.endNs>=100000000)return {};
            out.branches[n]=r;out.semantic.context=r.context;
            out.semantic.updates[n]={u.invocation,u.beginNs,u.endNs,true};oldest=std::min(oldest,u.endNs);latest=std::max(latest,u.endNs);
        }
        if(oldest>INT64_MAX-100000000)return {};out.semantic.observedNs=latest;out.semantic.deadlineNs=oldest+100000000;return out;
    }
    Slot* Find(const interaction::AmmoResourceIdentity& r)noexcept {for(auto& s:slots_)if(s.occupied&&s.state.resource==r)return &s;return nullptr;}
    const Slot* Find(const interaction::AmmoResourceIdentity& r)const noexcept {for(const auto& s:slots_)if(s.occupied&&s.state.resource==r)return &s;return nullptr;}
    void Append(PistolAfterFeedEventKind kind,const Slot& slot,std::optional<AmmoResourceOutcome> outcome)noexcept {
        if(eventSequence_==UINT64_MAX)return;const auto event=++eventSequence_;
        events_[(event-1)%events_.size()]={event,kind,slot.state.request,slot.state.command,slot.state.admittedIdentity,
            slot.state.admission,slot.state.initialPolicyReadyVerified,outcome};
    }
    std::array<Slot,32> slots_{};
    std::array<PistolAfterFeedEvent,128> events_{};
    std::array<PistolAfterFeedActorRetirement,32> retirements_{};
    std::uint64_t eventSequence_=0,retirementSequence_=0;
};
}
