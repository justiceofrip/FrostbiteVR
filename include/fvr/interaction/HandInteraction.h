#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace fvr::interaction {
// Opaque semantic identities, never native pointers or array slots.
struct HandInteractionOwner {
    std::uint64_t actor=0,actorGeneration=0,equipGeneration=0,space=0;
    bool operator==(const HandInteractionOwner&)const=default;
};
struct HandInteractionKey {
    std::uint64_t id=0,generation=0;
    bool operator==(const HandInteractionKey&)const=default;
};
enum class InteractionHand:std::uint8_t {Left,Right};
enum class HandClaimKind:std::uint8_t {
    None,GunHold,WeaponSupport,Sight,Mechanism,AmmoObject,BodyInventory
};
struct HandInteractionSample {
    HandInteractionOwner owner{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0,nowNs=0;
    bool focused=false;
    std::array<bool,2> tracked{};
    // Current real release evidence. Native always-held gun ownership may keep
    // its hand false; this field never invents a neutral controller sample.
    std::array<bool,2> released{};
};
struct HandContactProof {
    HandInteractionKey key{};
    // Geometry/eligibility must belong to this exact input packet. Do not stamp
    // old geometry with the current input sequence or extend its deadline.
    std::uint64_t inputSequence=0;
    std::int64_t deadlineNs=0;
    bool eligible=false;
};
struct HandClaimRequest {
    HandInteractionOwner owner{};
    InteractionHand hand=InteractionHand::Left;
    HandClaimKind kind=HandClaimKind::None;
    HandInteractionKey item{};
    HandContactProof contact{};
    // Strictly increasing per destination hand for this arbiter's lifetime.
    // An attempted acquisition consumes the intent even when contention fails.
    std::uint64_t intent=0;
    // Explicit other-hand GunHold dependency; mandatory for WeaponSupport.
    // Sight/Mechanism can be exclusive standalone claims or share such a hold.
    std::uint64_t prerequisiteClaim=0;
};
struct HandClaimToken {
    std::uint64_t id=0;
    HandInteractionOwner owner{};
    InteractionHand hand=InteractionHand::Left;
    HandClaimKind kind=HandClaimKind::None;
    HandInteractionKey item{},contact{};
    std::uint64_t prerequisiteClaim=0;
    bool operator==(const HandClaimToken&)const=default;
};
struct HandClaim {
    HandClaimToken token{};
    std::int64_t deadlineNs=0;
    std::uint64_t inputSequence=0;
};
enum class HandInteractionReason:std::uint8_t {
    None,InvalidConfig,InvalidSample,ClockReversed,SequenceRollback,
    StaleInput,IdentityChanged,FocusLost,TrackingLost,LeaseExpired,
    InvalidRequest,StaleIntent,WrongOwner,ContactUnavailable,ContactMismatch,
    MissingEvidence,HandOccupied,ItemOccupied,MissingPrerequisite,StaleToken,
    TokenExhausted,Released,Transferred,DependencyLost,Reset
};
struct HandClaimRelease {HandClaimToken token{};HandInteractionReason reason=HandInteractionReason::None;};
struct HandInteractionResult {
    bool inputValid=false,accepted=false;
    HandInteractionReason reason=HandInteractionReason::None;
    std::optional<HandClaim> claim;
    std::array<std::optional<HandClaimRelease>,2> released{};
};
struct HandInteractionConfig {
    std::int64_t maxInputLifetimeNs=150000000;
};

// One serialized arbiter per actor interaction domain; no allocation, priorities,
// implicit steals, gesture detection, native action or rendering. Tokens are
// unique within this instance across Reset. Do not copy/recreate it to reset.
class HandInteraction {
public:
    explicit HandInteraction(HandInteractionConfig config={})noexcept:config_(config){}
    HandInteraction(const HandInteraction&)=delete;
    HandInteraction& operator=(const HandInteraction&)=delete;
    HandInteractionResult Update(const HandInteractionSample&)noexcept;
    HandInteractionResult Acquire(const HandInteractionSample&,const HandClaimRequest&)noexcept;
    HandInteractionResult Transfer(const HandInteractionSample&,const HandClaimToken&,const HandClaimRequest&)noexcept;
    HandInteractionResult Renew(const HandInteractionSample&,const HandClaimToken&,const HandContactProof&)noexcept;
    // Historical evidence must exactly match a fresh packet previously observed
    // by Update/Prepare (32-entry bounded history). Current safety is authoritative.
    // Evidence nowNs is not identity: it is the caller's processing-time field.
    // Neutral-boundary geometry is allowed for a new current-authorized intent;
    // gesture policies still own press/release pairing. Pre-release evidence rejects.
    HandInteractionResult AcquireFrom(const HandInteractionSample& currentSafety,const HandInteractionSample& originalEvidence,const HandClaimRequest&)noexcept;
    HandInteractionResult TransferFrom(const HandInteractionSample& currentSafety,const HandInteractionSample& originalEvidence,const HandClaimToken&,const HandClaimRequest&)noexcept;
    HandInteractionResult RenewFrom(const HandInteractionSample& currentSafety,const HandInteractionSample& originalEvidence,const HandClaimToken&,const HandContactProof&)noexcept;
    HandInteractionResult Release(const HandInteractionSample&,const HandClaimToken&)noexcept;
    HandInteractionResult Reset()noexcept;
    // Inspect only after Update with the current clock/input. This is not a
    // clock source and cannot expire a lease while the adapter is not ticking.
    std::optional<HandClaim> Current(InteractionHand hand)const noexcept {
        return ValidHand(hand)?claims_[Index(hand)]:std::nullopt;
    }
private:
    static bool ValidHand(InteractionHand h)noexcept {return h==InteractionHand::Left||h==InteractionHand::Right;}
    static std::size_t Index(InteractionHand h)noexcept {return static_cast<std::size_t>(h);}
    static bool ValidKey(HandInteractionKey k)noexcept {return k.id&&k.generation;}
    static bool ValidOwner(HandInteractionOwner o)noexcept {return o.actor&&o.actorGeneration&&o.equipGeneration&&o.space;}
    static bool ValidKind(HandClaimKind k)noexcept {return k>=HandClaimKind::GunHold&&k<=HandClaimKind::BodyInventory;}
    static bool DependentKind(HandClaimKind k)noexcept {
        return k==HandClaimKind::WeaponSupport||k==HandClaimKind::Sight||k==HandClaimKind::Mechanism;
    }
    void Drop(std::size_t,HandInteractionReason,HandInteractionResult&)noexcept;
    void DropAll(HandInteractionReason,HandInteractionResult&)noexcept;
    void DropDependents(HandInteractionResult&)noexcept;
    bool Available(InteractionHand,HandInteractionResult&)const noexcept;
    bool Contact(const HandContactProof&,HandInteractionResult&,const HandInteractionSample* evidence=nullptr)const noexcept;
    bool Evidence(const HandInteractionSample&,InteractionHand,HandInteractionResult&)const noexcept;
    void Remember(const HandInteractionSample& s)noexcept {history_[historyNext_]=s;historyNext_=(historyNext_+1)%history_.size();}
    bool Exact(const HandClaimToken&)const noexcept;
    HandInteractionResult Prepare(const HandInteractionSample&)noexcept;
    HandInteractionResult Change(const HandInteractionSample&,const HandClaimToken*,const HandClaimRequest&,const HandInteractionSample* evidence=nullptr)noexcept;
    HandInteractionResult Refresh(const HandInteractionSample&,const HandClaimToken&,const HandContactProof&,const HandInteractionSample* evidence)noexcept;
    HandInteractionConfig config_{};
    std::array<std::optional<HandClaim>,2> claims_{};
    HandInteractionSample sample_{};
    std::array<std::uint64_t,2> intents_{},releaseBarrier_{},invalidationBarrier_{};
    std::array<HandInteractionSample,32> history_{};
    std::size_t historyNext_=0;
    std::uint64_t nextClaim_=0;
    std::int64_t lastNow_=0;
    bool initialized_=false,blocked_=false;
};
inline void HandInteraction::Drop(std::size_t hand,HandInteractionReason reason,HandInteractionResult& out)noexcept {
    if(!claims_[hand])return;
    for(auto& event:out.released)if(!event){event=HandClaimRelease{claims_[hand]->token,reason};break;}
    claims_[hand].reset();
}
inline void HandInteraction::DropAll(HandInteractionReason reason,HandInteractionResult& out)noexcept {
    for(std::size_t h=0;h<2;++h)Drop(h,reason,out);
}
inline void HandInteraction::DropDependents(HandInteractionResult& out)noexcept {
    for(std::size_t h=0;h<2;++h)if(claims_[h]&&claims_[h]->token.prerequisiteClaim){
        const auto& other=claims_[1-h];
        if(!other||other->token.id!=claims_[h]->token.prerequisiteClaim){
            invalidationBarrier_[h]=sample_.sequence;
            Drop(h,HandInteractionReason::DependencyLost,out);
        }
    }
}
inline HandInteractionResult HandInteraction::Reset()noexcept {
    HandInteractionResult out;out.reason=HandInteractionReason::Reset;DropAll(out.reason,out);
    // Preserve token/intent high-water marks and the last input sequence:
    // queued work cannot reuse the packet that preceded an explicit reset.
    blocked_=true;sample_.focused=false;sample_.tracked={};
    invalidationBarrier_.fill(sample_.sequence);history_={};historyNext_=0;return out;
}
inline HandInteractionResult HandInteraction::Update(const HandInteractionSample& s)noexcept {
    HandInteractionResult out;
    const auto reject=[&](HandInteractionReason reason){
        out.reason=reason;DropAll(reason,out);blocked_=true;
        invalidationBarrier_.fill(sample_.sequence);return out;
    };
    if(config_.maxInputLifetimeNs<=0)return reject(HandInteractionReason::InvalidConfig);
    if(!ValidOwner(s.owner)||!s.sequence||s.observedNs<=0||s.nowNs<s.observedNs||
       s.deadlineNs<=s.observedNs||s.deadlineNs-s.observedNs>config_.maxInputLifetimeNs)
        return reject(HandInteractionReason::InvalidSample);
    if(s.nowNs<lastNow_)return reject(HandInteractionReason::ClockReversed);
    lastNow_=s.nowNs;
    if(s.nowNs>=s.deadlineNs)return reject(HandInteractionReason::StaleInput);
    const bool newOwner=!initialized_||s.owner!=sample_.owner;
    if(initialized_&&(s.observedNs<sample_.observedNs||(newOwner&&s.observedNs==sample_.observedNs)))
        return reject(HandInteractionReason::StaleInput);
    if(newOwner){
        if(initialized_){out.reason=HandInteractionReason::IdentityChanged;DropAll(out.reason,out);}
        sample_=s;initialized_=true;blocked_=false;history_={};historyNext_=0;releaseBarrier_={};invalidationBarrier_={};Remember(s);
    }else if(s.sequence<sample_.sequence)return reject(HandInteractionReason::SequenceRollback);
    else if(s.sequence==sample_.sequence){
        if(s.observedNs!=sample_.observedNs||s.deadlineNs!=sample_.deadlineNs)
            return reject(HandInteractionReason::InvalidSample);
        if(blocked_)return reject(HandInteractionReason::StaleInput);
        // Duplicate polls may cancel, but never restore focus/tracking or
        // refresh timestamps. Recovery needs a genuinely new input packet.
        sample_.focused=sample_.focused&&s.focused;
        for(std::size_t h=0;h<2;++h){sample_.tracked[h]=sample_.tracked[h]&&s.tracked[h];sample_.released[h]=sample_.released[h]||s.released[h];}
        sample_.nowNs=s.nowNs;
    }else {sample_=s;blocked_=false;Remember(s);}
    out.inputValid=true;
    if(!sample_.focused){out.reason=HandInteractionReason::FocusLost;invalidationBarrier_.fill(sample_.sequence);DropAll(out.reason,out);return out;}
    for(std::size_t h=0;h<2;++h){
        if(!sample_.tracked[h]){invalidationBarrier_[h]=sample_.sequence;Drop(h,HandInteractionReason::TrackingLost,out);}
        else if(sample_.released[h]){releaseBarrier_[h]=sample_.sequence;Drop(h,HandInteractionReason::Released,out);}
        else if(claims_[h]&&s.nowNs>=claims_[h]->deadlineNs)Drop(h,HandInteractionReason::LeaseExpired,out);
    }
    DropDependents(out);return out;
}
inline bool HandInteraction::Available(InteractionHand hand,HandInteractionResult& out)const noexcept {
    if(!out.inputValid)return false;
    if(!ValidHand(hand)){out.reason=HandInteractionReason::InvalidRequest;return false;}
    if(!sample_.focused){out.reason=HandInteractionReason::FocusLost;return false;}
    if(!sample_.tracked[Index(hand)]){out.reason=HandInteractionReason::TrackingLost;return false;}
    if(sample_.released[Index(hand)]){out.reason=HandInteractionReason::Released;return false;}
    return true;
}
inline bool HandInteraction::Evidence(const HandInteractionSample& source,InteractionHand hand,HandInteractionResult& out)const noexcept {
    if(source.owner!=sample_.owner){out.reason=HandInteractionReason::WrongOwner;return false;}
    if(!source.sequence||source.sequence>sample_.sequence||source.observedNs<=0||source.observedNs>sample_.observedNs||
       source.deadlineNs<=source.observedNs||source.deadlineNs-source.observedNs>config_.maxInputLifetimeNs||
       source.deadlineNs<=sample_.nowNs||!source.focused||!source.tracked[Index(hand)]||
       source.sequence<=invalidationBarrier_[Index(hand)]||source.sequence<releaseBarrier_[Index(hand)]||
       (source.sequence==releaseBarrier_[Index(hand)]&&!source.released[Index(hand)])){
        out.reason=HandInteractionReason::ContactMismatch;return false;
    }
    for(const auto& recorded:history_)if(recorded.sequence==source.sequence&&recorded.owner==source.owner){
        if(recorded.observedNs==source.observedNs&&recorded.deadlineNs==source.deadlineNs&&
           recorded.focused==source.focused&&recorded.tracked==source.tracked&&recorded.released==source.released)return true;
        out.reason=HandInteractionReason::ContactMismatch;return false;
    }
    out.reason=HandInteractionReason::MissingEvidence;return false;
}
inline bool HandInteraction::Contact(const HandContactProof& proof,HandInteractionResult& out,const HandInteractionSample* evidence)const noexcept {
    if(!ValidKey(proof.key)||!proof.eligible){out.reason=HandInteractionReason::ContactUnavailable;return false;}
    const auto& source=evidence?*evidence:sample_;
    if(proof.inputSequence!=source.sequence||proof.deadlineNs<=sample_.nowNs||proof.deadlineNs>source.deadlineNs||
       (evidence&&proof.deadlineNs!=source.deadlineNs)){
        out.reason=HandInteractionReason::ContactMismatch;return false;
    }
    return true;
}
inline bool HandInteraction::Exact(const HandClaimToken& token)const noexcept {
    return token.id&&ValidHand(token.hand)&&claims_[Index(token.hand)]&&claims_[Index(token.hand)]->token==token;
}
inline HandInteractionResult HandInteraction::Prepare(const HandInteractionSample& s)noexcept {
    // A queued operation cannot roll back the authoritative input or invalidate
    // a newer claim. Update remains the explicit lifecycle/clock-loss entrypoint.
    if(initialized_&&(s.nowNs<lastNow_||s.observedNs<sample_.observedNs||
       (s.owner==sample_.owner&&s.sequence<sample_.sequence)||
       (s.owner!=sample_.owner&&s.observedNs<=sample_.observedNs))){
        HandInteractionResult out;out.reason=HandInteractionReason::StaleInput;return out;
    }
    return Update(s);
}
inline HandInteractionResult HandInteraction::Change(const HandInteractionSample& s,const HandClaimToken* old,const HandClaimRequest& r,const HandInteractionSample* evidence)noexcept {
    if(old&&!Exact(*old)){HandInteractionResult out;out.reason=HandInteractionReason::StaleToken;return out;}
    auto out=Prepare(s);if(!out.inputValid)return out;
    if(!ValidHand(r.hand)){out.reason=HandInteractionReason::InvalidRequest;return out;}
    const auto h=Index(r.hand);
    if(!r.intent||r.intent<=intents_[h]){out.reason=HandInteractionReason::StaleIntent;return out;}
    intents_[h]=r.intent;
    // A new intent attempted during unavailable tracking/focus is consumed.
    // Recovery must not retry a held gesture with freshly stamped geometry.
    if(!Available(r.hand,out))return out;
    if(r.owner!=sample_.owner){out.reason=HandInteractionReason::WrongOwner;return out;}
    if(!ValidKind(r.kind)||!ValidKey(r.item)){out.reason=HandInteractionReason::InvalidRequest;return out;}
    if(old&&!Exact(*old)){out.reason=HandInteractionReason::StaleToken;return out;}
    if(evidence&&!Evidence(*evidence,r.hand,out))return out;
    if(!Contact(r.contact,out,evidence))return out;
    const auto replaced=old?old->id:0;
    if(claims_[h]&&claims_[h]->token.id!=replaced){out.reason=HandInteractionReason::HandOccupied;return out;}
    if(r.prerequisiteClaim){
        const auto& parent=claims_[1-h];
        if(!DependentKind(r.kind)||!parent||parent->token.id==replaced||
           parent->token.id!=r.prerequisiteClaim||parent->token.kind!=HandClaimKind::GunHold||
           parent->token.owner!=r.owner||parent->token.item!=r.item||
           parent->token.contact==r.contact.key){
            out.reason=HandInteractionReason::MissingPrerequisite;return out;
        }
    }else if(r.kind==HandClaimKind::WeaponSupport){out.reason=HandInteractionReason::MissingPrerequisite;return out;}
    const auto& other=claims_[1-h];
    if(other&&other->token.id!=replaced&&other->token.item==r.item&&r.prerequisiteClaim!=other->token.id){
        out.reason=HandInteractionReason::ItemOccupied;return out;
    }
    if(nextClaim_==std::numeric_limits<std::uint64_t>::max()){out.reason=HandInteractionReason::TokenExhausted;return out;}
    // Validation is complete. No old claim is removed on any failed acquisition.
    const HandClaim next{{++nextClaim_,r.owner,r.hand,r.kind,r.item,r.contact.key,r.prerequisiteClaim},
                         r.contact.deadlineNs,r.contact.inputSequence};
    if(old)Drop(Index(old->hand),HandInteractionReason::Transferred,out);
    claims_[h]=next;DropDependents(out);
    out.accepted=true;out.reason=HandInteractionReason::None;out.claim=next;return out;
}
inline HandInteractionResult HandInteraction::Acquire(const HandInteractionSample& s,const HandClaimRequest& r)noexcept {
    return Change(s,nullptr,r);
}
inline HandInteractionResult HandInteraction::Transfer(const HandInteractionSample& s,const HandClaimToken& old,const HandClaimRequest& r)noexcept {
    return Change(s,&old,r);
}
inline HandInteractionResult HandInteraction::Refresh(const HandInteractionSample& s,const HandClaimToken& token,const HandContactProof& proof,const HandInteractionSample* evidence)noexcept {
    if(!Exact(token)){HandInteractionResult out;out.reason=HandInteractionReason::StaleToken;return out;}
    auto out=Prepare(s);if(!Available(token.hand,out))return out;
    if(!Exact(token)){out.reason=HandInteractionReason::StaleToken;return out;}
    if(proof.key!=token.contact){out.reason=HandInteractionReason::ContactMismatch;return out;}
    if(evidence&&!Evidence(*evidence,token.hand,out))return out;
    if(!Contact(proof,out,evidence))return out;
    auto& claim=*claims_[Index(token.hand)];
    if(proof.inputSequence<claim.inputSequence){out.reason=HandInteractionReason::ContactMismatch;return out;}
    if(proof.inputSequence==claim.inputSequence&&proof.deadlineNs>claim.deadlineNs){
        out.reason=HandInteractionReason::ContactMismatch;return out;
    }
    claim.deadlineNs=proof.deadlineNs;claim.inputSequence=proof.inputSequence;
    out.accepted=true;out.reason=HandInteractionReason::None;out.claim=claim;return out;
}
inline HandInteractionResult HandInteraction::Renew(const HandInteractionSample& s,const HandClaimToken& token,const HandContactProof& proof)noexcept {
    return Refresh(s,token,proof,nullptr);
}
inline HandInteractionResult HandInteraction::AcquireFrom(const HandInteractionSample& current,const HandInteractionSample& source,const HandClaimRequest& request)noexcept {
    return Change(current,nullptr,request,&source);
}
inline HandInteractionResult HandInteraction::TransferFrom(const HandInteractionSample& current,const HandInteractionSample& source,const HandClaimToken& token,const HandClaimRequest& request)noexcept {
    return Change(current,&token,request,&source);
}
inline HandInteractionResult HandInteraction::RenewFrom(const HandInteractionSample& current,const HandInteractionSample& source,const HandClaimToken& token,const HandContactProof& proof)noexcept {
    return Refresh(current,token,proof,&source);
}
inline HandInteractionResult HandInteraction::Release(const HandInteractionSample& s,const HandClaimToken& token)noexcept {
    if(!Exact(token)){HandInteractionResult out;out.reason=HandInteractionReason::StaleToken;return out;}
    auto out=Prepare(s);if(!out.inputValid)return out;
    if(!Exact(token)){out.reason=HandInteractionReason::StaleToken;return out;}
    invalidationBarrier_[Index(token.hand)]=sample_.sequence;
    Drop(Index(token.hand),HandInteractionReason::Released,out);DropDependents(out);
    out.accepted=true;out.reason=HandInteractionReason::None;return out;
}
}
