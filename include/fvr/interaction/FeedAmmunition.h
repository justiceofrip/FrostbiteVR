#pragma once
#include "fvr/interaction/FeedReload.h"

namespace fvr::interaction {
// Native loaded/reserve counts and mod-owned belt rounds are disjoint stores.
// No chamber count or transfer stage is inferred from a physical mechanism.
struct FeedAmmunitionCounts {
    int loaded=0,reserve=0,capacity=0;
    bool operator==(const FeedAmmunitionCounts&)const=default;
};
struct FeedBeltResource {
    FeedOwner owner{};
    std::uint64_t lifetime=0;
    // resource and container remain independently generated identities. A
    // container is never a second ammunition grant or a magazine token.
    FeedSupplyKey supply{};
    int rounds=0;
    bool operator==(const FeedBeltResource&)const=default;
};
struct FeedAmmunitionSnapshot {
    FeedOwner owner{};FeedKey mechanism{};
    std::uint64_t lifetime=0,sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    FeedAmmunitionCounts counts{};
    FeedSupplyKey loadedSupply{};
    bool coherentIdleVerified=false;
    bool operator==(const FeedAmmunitionSnapshot&)const=default;
};
enum class FeedAmmunitionEffect:std::uint8_t {
    Unavailable,PhysicalOnly,ExtractLoaded,AcquireReserve,LoadSupply,ReturnSupply,RefillLoaded
};
struct FeedAmmunitionAuthority {
    FeedKey key{},binding{};
    FeedOwner owner{};FeedKey mechanism{};
    std::uint64_t lifetime=0,profileId=0,profileRevision=0;
    FeedPhysicalOperation operation=FeedPhysicalOperation::None;
    FeedAmmunitionEffect effect=FeedAmmunitionEffect::Unavailable;
    FeedSupplyKey supply{};
    int rounds=0;
    // Adapter proof of this operation's actual side-effect strategy. In
    // particular, SeatFeed or CloseCover does not imply a native transfer.
    bool verified=false;
    bool operator==(const FeedAmmunitionAuthority&)const=default;
};
inline constexpr std::size_t FeedBeltResourceCapacity=8;
struct FeedAmmunitionCommand {
    FeedReloadRequest request{};
    FeedAmmunitionAuthority authority{};
    FeedAmmunitionSnapshot before{};
    FeedAmmunitionCounts after{};
    FeedSupplyKey loadedSupplyAfter{};
    std::array<std::optional<FeedBeltResource>,FeedBeltResourceCapacity> resourcesAfter{};
    bool mutationRequired=false;
    bool operator==(const FeedAmmunitionCommand&)const=default;
};
struct FeedAmmunitionReceipt {
    FeedAmmunitionCommand command{};
    FeedReloadAck acknowledgement{};
    FeedAmmunitionSnapshot post{};
    // Nonzero, increasing adapter invocation identity within this lifetime.
    std::uint64_t authorityInvocation=0;
    std::int64_t beganNs=0,completedNs=0;
    // Proves native execution (or explicit rejection without effects), the
    // authoritative callback and all required native copies. A physical-only
    // step still needs observed unchanged counts; a function return is not it.
    bool authorityVerified=false,copiesVerified=false;
    // Rejected proves a later coherent native observation with no transfer
    // entry. Submission refusal or elapsed time alone cannot set this evidence.
    bool noTransferEntry=false;
    bool operator==(const FeedAmmunitionReceipt&)const=default;
};
struct FeedAmmunitionState {
    bool bound=false,suspended=true,retired=false,needsReconciliation=false;
    FeedAmmunitionSnapshot snapshot{};
    std::array<std::optional<FeedBeltResource>,FeedBeltResourceCapacity> resources{};
    std::optional<FeedAmmunitionCommand> pending{};
    std::optional<FeedAmmunitionReceipt> lastReceipt{};
    std::uint64_t acceptedOperations=0,lastRequestId=0;
};

// One immutable FeedReload lifetime. This ledger owns retained quantities, not
// hand poses, native calls or firing permission. Only Dispatch's return may be
// sent to an adapter, once; State().pending is reconciliation evidence only.
// The owner must retain this object across tracking/stow until explicit retirement.
class FeedAmmunition {
public:
    FeedAmmunition(FeedMechanismProfile profile,FeedOwner owner,FeedKey mechanism,
        FeedSupplyKey goal,std::uint64_t lifetime,AmmoResourceIdentity resource={})noexcept:
        profile_(profile),owner_(owner),mechanism_(mechanism),goal_(goal),lifetime_(lifetime),resource_(resource){}
    FeedAmmunition(const FeedAmmunition&)=delete;
    FeedAmmunition& operator=(const FeedAmmunition&)=delete;
    [[nodiscard]] bool Bind(const FeedAmmunitionSnapshot& snapshot,std::int64_t now)noexcept;
    // Candidate and count snapshot must share a single coherent observation
    // sequence. Independently sampled counts cannot admit a mechanism request.
    [[nodiscard]] bool CanDispatch(const FeedPhysicalCandidate& candidate,const FeedAmmunitionAuthority& authority,
        const FeedAmmunitionSnapshot& snapshot,std::int64_t now)const noexcept;
    [[nodiscard]] std::optional<FeedAmmunitionCommand> Dispatch(const FeedReloadRequest& request,
        const FeedAmmunitionAuthority& authority,const FeedAmmunitionSnapshot& snapshot,std::int64_t now)noexcept;
    [[nodiscard]] bool Complete(const FeedAmmunitionReceipt& receipt,std::int64_t now)noexcept;
    void Suspend()noexcept {state_.suspended=true;}
    [[nodiscard]] bool Resume(const FeedAmmunitionSnapshot& snapshot,std::int64_t now)noexcept;
    [[nodiscard]] bool CanMigrateContext(const FeedContextMigration&,const FeedAmmunitionSnapshot& before,
        const FeedAmmunitionSnapshot& after,std::int64_t now)const noexcept;
    [[nodiscard]] bool MigrateContext(const FeedContextMigration&,const FeedAmmunitionSnapshot& before,
        const FeedAmmunitionSnapshot& after,std::int64_t now)noexcept;
    [[nodiscard]] bool Retire(const FeedAmmunitionSnapshot& snapshot,std::int64_t now)noexcept;
    [[nodiscard]] FeedAmmunitionState State()const noexcept {return state_;}
private:
    static constexpr int MaxRounds=1000000;
    bool Valid()const noexcept;
    bool Snapshot(const FeedAmmunitionSnapshot& s)const noexcept;
    bool Fresh(const FeedAmmunitionSnapshot& s,std::int64_t now)const noexcept;
    bool Unchanged(const FeedAmmunitionSnapshot& s)const noexcept;
    bool Plan(const FeedAmmunitionAuthority& a,const FeedAmmunitionSnapshot& s,FeedAmmunitionCommand& out)const noexcept;
    const FeedMechanismProfile profile_;
    FeedOwner owner_;
    const FeedKey mechanism_;
    const FeedSupplyKey goal_;
    const std::uint64_t lifetime_;
    const AmmoResourceIdentity resource_;
    FeedKey binding_{};
    FeedAmmunitionState state_{};
};
inline bool FeedAmmunition::Valid()const noexcept {
    return ValidateFeedMechanismProfile(profile_)&&feed_mechanism_detail::Owner(owner_)&&
        feed_mechanism_detail::Key(mechanism_)&&feed_mechanism_detail::Supply(goal_,profile_.container)&&lifetime_;
}
inline bool FeedAmmunition::Snapshot(const FeedAmmunitionSnapshot& s)const noexcept {
    const auto& c=s.counts;
    return s.coherentIdleVerified&&s.owner==owner_&&s.mechanism==mechanism_&&s.lifetime==lifetime_&&
        s.sequence&&s.observedNs>0&&s.deadlineNs>s.observedNs&&s.deadlineNs-s.observedNs<=profile_.maxAgeNs&&
        c.capacity>0&&c.capacity<=MaxRounds&&c.loaded>=0&&c.loaded<=c.capacity&&c.reserve>=0&&c.reserve<=MaxRounds&&
        (feed_mechanism_detail::Supply(s.loadedSupply,profile_.container)||
         (c.loaded==0&&feed_mechanism_detail::Empty(s.loadedSupply)));
}
inline bool FeedAmmunition::Fresh(const FeedAmmunitionSnapshot& s,std::int64_t now)const noexcept {
    return Snapshot(s)&&now>=s.observedNs&&now<s.deadlineNs;
}
inline bool FeedAmmunition::Unchanged(const FeedAmmunitionSnapshot& s)const noexcept {
    const auto& prior=state_.snapshot;
    return s.sequence>=prior.sequence&&s.observedNs>=prior.observedNs&&s.counts==prior.counts&&
        s.loadedSupply==prior.loadedSupply&&(s.sequence!=prior.sequence||s==prior);
}
inline bool FeedAmmunition::Bind(const FeedAmmunitionSnapshot& s,std::int64_t now)noexcept {
    if(!Valid()||state_.bound||state_.retired||!Fresh(s,now))return false;
    state_.snapshot=s;state_.bound=true;state_.suspended=false;return true;
}
inline bool FeedAmmunition::Plan(const FeedAmmunitionAuthority& a,const FeedAmmunitionSnapshot& s,
    FeedAmmunitionCommand& out)const noexcept {
    using namespace feed_mechanism_detail;
    out.before=s;out.authority=a;out.after=s.counts;out.loadedSupplyAfter=s.loadedSupply;
    out.resourcesAfter=state_.resources;
    if(a.rounds<0||a.rounds>MaxRounds)return false;
    if(a.effect==FeedAmmunitionEffect::PhysicalOnly)return a.rounds==0&&Empty(a.supply);
    if(!Supply(a.supply,profile_.container))return false;
    std::optional<FeedBeltResource>* slot=nullptr;
    std::optional<FeedBeltResource>* empty=nullptr;
    for(auto& r:out.resourcesAfter){
        if(!r){if(!empty)empty=&r;continue;}
        if(r->supply==a.supply)slot=&r;
        // Either identity cannot alias a different resource/container pairing.
        else if(r->supply.resource==a.supply.resource||
            (profile_.container&&r->supply.container==a.supply.container))return false;
    }
    if(!Empty(s.loadedSupply)&&s.loadedSupply!=a.supply&&
        (s.loadedSupply.resource==a.supply.resource||
         (profile_.container&&s.loadedSupply.container==a.supply.container)))return false;
    if(!slot){
        if(!empty||(a.effect!=FeedAmmunitionEffect::ExtractLoaded&&a.effect!=FeedAmmunitionEffect::AcquireReserve&&
                    a.effect!=FeedAmmunitionEffect::RefillLoaded))return false;
        *empty=FeedBeltResource{owner_,lifetime_,a.supply,0};slot=empty;
    }
    auto& belt=**slot;
    switch(a.effect){
    case FeedAmmunitionEffect::ExtractLoaded:
        if(s.loadedSupply!=a.supply||a.rounds!=s.counts.loaded||belt.rounds>MaxRounds-a.rounds)return false;
        belt.rounds+=a.rounds;out.after.loaded=0;out.loadedSupplyAfter={};break;
    case FeedAmmunitionEffect::AcquireReserve:
        if(a.rounds==0||s.counts.reserve<a.rounds||belt.rounds>MaxRounds-a.rounds)return false;
        belt.rounds+=a.rounds;out.after.reserve-=a.rounds;break;
    case FeedAmmunitionEffect::LoadSupply:
        if((!Empty(s.loadedSupply)&&s.loadedSupply!=a.supply)||a.rounds>belt.rounds||
           a.rounds>s.counts.capacity-s.counts.loaded)return false;
        belt.rounds-=a.rounds;out.after.loaded+=a.rounds;out.loadedSupplyAfter=a.supply;break;
    case FeedAmmunitionEffect::ReturnSupply:
        if(a.rounds==0||a.rounds>belt.rounds||s.counts.reserve>MaxRounds-a.rounds)return false;
        belt.rounds-=a.rounds;out.after.reserve+=a.rounds;break;
    case FeedAmmunitionEffect::RefillLoaded:
        // An explicitly proved atomic native transfer need not pass through an
        // invented intermediate held belt or require a second native write.
        if((!Empty(s.loadedSupply)&&s.loadedSupply!=a.supply)||a.rounds==0||
           a.rounds>s.counts.reserve||a.rounds>s.counts.capacity-s.counts.loaded)return false;
        out.after.reserve-=a.rounds;out.after.loaded+=a.rounds;out.loadedSupplyAfter=a.supply;break;
    default:return false;
    }
    out.mutationRequired=out.after!=s.counts;
    return true;
}
inline bool FeedAmmunition::CanDispatch(const FeedPhysicalCandidate& c,const FeedAmmunitionAuthority& a,
    const FeedAmmunitionSnapshot& s,std::int64_t now)const noexcept {
    using namespace feed_mechanism_detail;
    const auto op=static_cast<unsigned>(c.operation);
    if(!Valid()||!state_.bound||state_.suspended||state_.retired||state_.pending||!Fresh(s,now)||!Unchanged(s)||
       c.owner!=owner_||c.mechanism!=mechanism_||c.goal!=goal_||c.profileId!=profile_.id||
       c.profileRevision!=profile_.revision||c.snapshotSequence!=s.sequence||op==0||op>9||
       c.part!=OperationPart(c.operation)||!Part(profile_,static_cast<unsigned>(c.part))||
       c.role!=profile_.contacts[static_cast<unsigned>(c.part)].role||
       !a.verified||!Key(a.key)||!Key(a.binding)||a.owner!=owner_||a.mechanism!=mechanism_||
       a.lifetime!=lifetime_||a.profileId!=profile_.id||a.profileRevision!=profile_.revision||
       a.operation!=c.operation||(!Empty(binding_)&&binding_!=a.binding))return false;
    const bool resourceOperation=c.operation==FeedPhysicalOperation::WithdrawFeed||c.operation==FeedPhysicalOperation::DetachContainer||
        c.operation==FeedPhysicalOperation::AttachContainer||c.operation==FeedPhysicalOperation::SeatFeed;
    if(resourceOperation?!Supply(c.supply,profile_.container):!Empty(c.supply))return false;
    if(a.effect!=FeedAmmunitionEffect::PhysicalOnly){
        if(!Empty(c.supply)&&a.supply!=c.supply)return false;
        bool known=a.supply==goal_||a.supply==s.loadedSupply;
        for(const auto& r:state_.resources)known=known||(r&&r->supply==a.supply);
        if(!known)return false;
    }
    FeedAmmunitionCommand plan;return Plan(a,s,plan);
}
inline std::optional<FeedAmmunitionCommand> FeedAmmunition::Dispatch(const FeedReloadRequest& r,
    const FeedAmmunitionAuthority& a,const FeedAmmunitionSnapshot& s,std::int64_t now)noexcept {
    if(!r.id||r.id<=state_.lastRequestId||r.lifetime!=lifetime_||r.binding!=a.binding||
       !r.completion.id||!r.completion.inputSequence||r.completion.completedNs<s.observedNs||
       r.requestedNs<r.completion.completedNs||r.requestedNs>now||r.requestedNs>=s.deadlineNs||
       !CanDispatch(r.completion.candidate,a,s,now))return {};
    FeedAmmunitionCommand command;
    if(!Plan(a,s,command))return {};
    command.request=r;state_.snapshot=s;state_.pending=command;
    state_.lastRequestId=r.id;binding_=a.binding;return command;
}
inline bool FeedAmmunition::Complete(const FeedAmmunitionReceipt& r,std::int64_t now)noexcept {
    if(state_.lastReceipt&&r==*state_.lastReceipt)return !state_.pending;
    if(!state_.pending||r.command!=*state_.pending)return false;
    state_.needsReconciliation=true;
    const auto& command=*state_.pending;const auto& ack=r.acknowledgement;const auto& post=r.post;
    const bool applied=ack.status==FeedReloadAcknowledgement::Applied;
    const bool rejected=ack.status==FeedReloadAcknowledgement::Rejected;
    if((!applied&&!rejected)||r.noTransferEntry!=rejected||ack.request!=command.request||!r.authorityVerified||!r.copiesVerified||
       !r.authorityInvocation||(state_.lastReceipt&&r.authorityInvocation<=state_.lastReceipt->authorityInvocation)||
       r.beganNs<command.request.requestedNs||r.completedNs<r.beganNs||
       now<r.completedNs||!Snapshot(post)||post.sequence<=command.before.sequence||
       post.observedNs<=command.request.requestedNs||post.observedNs<r.completedNs||post.observedNs>now||
       ack.snapshotSequence!=post.sequence||ack.observedNs!=post.observedNs||
       post.counts!=(applied?command.after:command.before.counts)||
       post.loadedSupply!=(applied?command.loadedSupplyAfter:command.before.loadedSupply))return false;
    if(applied){state_.resources=command.resourcesAfter;++state_.acceptedOperations;}
    // Late exact evidence is admissible after tracking loss/stow or any amount
    // of elapsed time. There is no timeout rollback of a dispatched effect.
    state_.snapshot=post;state_.lastReceipt=r;state_.pending.reset();state_.needsReconciliation=false;return true;
}
inline bool FeedAmmunition::Resume(const FeedAmmunitionSnapshot& s,std::int64_t now)noexcept {
    if(!state_.bound||state_.retired||state_.pending||!Fresh(s,now)||!Unchanged(s)||
       s.sequence<=state_.snapshot.sequence||s.observedNs<=state_.snapshot.observedNs)return false;
    state_.snapshot=s;state_.suspended=false;return true;
}
inline bool FeedAmmunition::CanMigrateContext(const FeedContextMigration& m,const FeedAmmunitionSnapshot& before,
    const FeedAmmunitionSnapshot& after,std::int64_t now)const noexcept {
    if(!state_.bound||!state_.suspended||state_.retired||state_.pending||state_.needsReconciliation||
       !Snapshot(before)||now<before.observedNs||!Unchanged(before)||
       !feed_mechanism_detail::Migration(m,resource_,owner_,after.owner,now,profile_.maxAgeNs))return false;
    auto normalized=after;normalized.owner=owner_;
    if(!Fresh(normalized,now)||normalized.mechanism!=before.mechanism||normalized.lifetime!=before.lifetime||
       normalized.counts!=before.counts||normalized.loadedSupply!=before.loadedSupply)return false;
    const auto joined=[](const FeedAmmunitionSnapshot& a,const AmmunitionSnapshot& n){
        return a.sequence==n.sequence&&a.observedNs==n.observedNs&&a.deadlineNs==n.deadlineNs&&
            a.counts.loaded==n.counts.loaded&&a.counts.reserve==n.counts.reserve&&a.counts.capacity==n.counts.capacity;
    };
    return joined(before,m.before)&&joined(after,m.after);
}
inline bool FeedAmmunition::MigrateContext(const FeedContextMigration& m,const FeedAmmunitionSnapshot& before,
    const FeedAmmunitionSnapshot& after,std::int64_t now)noexcept {
    if(!CanMigrateContext(m,before,after,now))return false;
    owner_=after.owner;state_.snapshot=after;
    // Retained resources keep their custody provenance. Historical commands,
    // receipts, invocation/request IDs and round quantities are never rewritten.
    return true;
}
inline bool FeedAmmunition::Retire(const FeedAmmunitionSnapshot& s,std::int64_t now)noexcept {
    Suspend();
    if(!state_.bound||state_.retired||state_.pending||!Fresh(s,now)||!Unchanged(s)||
       s.sequence<=state_.snapshot.sequence||s.observedNs<=state_.snapshot.observedNs)return false;
    state_.snapshot=s;state_.retired=true;return true;
}
}
