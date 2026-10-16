#pragma once
#include "Bc2KnifeDrawReturnGate.h"
#include "Bc2ReloadFlow.h"
#include <cstring>
namespace fvr::bc2::private_knife {
inline std::optional<std::array<std::byte,0x30>> CopyKnifeAttackInput(std::span<const std::byte> original)noexcept {
    const auto decoded=DecodeReloadUpdateContext(original);
    if(!decoded||decoded->inputFlags||decoded->deltaSeconds<=0)return {};
    std::array<std::byte,0x30> copy{};std::memcpy(copy.data(),original.data(),copy.size());
    const unsigned fire=1;std::memcpy(copy.data()+0x2c,&fire,sizeof(fire));return copy;
}
// The +40 client and server firing objects consume input (bits1/2). The
// +3c client object receives the +40 snapshot through the verified native
// export/restore path; it must never receive a second Fire for that action.
// All three objects still require independent state observations (bits0/1/2).
// Accepted native siblings never renew source input or authorize a new stroke.
class KnifeAttackGate {
public:
    using Command=interaction::KnifeCommand;
    static constexpr unsigned InputMask=6,ObservationMask=7;
    static constexpr bool InputBranch(unsigned branch)noexcept{return branch==1||branch==2;}
    bool ExpectedObservation(unsigned branch,std::uint64_t serial)const noexcept {
        if(!pending_||pending_->serial!=serial||branch>=3)return false;
        // Prediction activity is correlated only with this command's actual
        // +40 input consumer, never with a server observation or a timer.
        return (consumed_&(1u<<(branch==0?1:branch)))!=0;
    }
    bool Stage(const Command& c,std::int64_t now)noexcept {
        if(pending_||c.kind!=interaction::KnifeCommandKind::Attack||!c.serial||c.serial<=lastSerial_||
           !DrawReturnGate::Fresh(c,now)||!c.stroke||c.stroke->hold.kind!=interaction::HandClaimKind::GunHold||
           c.stroke->hold.owner!=c.source.owner||c.stroke->hold.hand!=c.hand||
           c.stroke->sourceSequence!=c.source.sequence||c.stroke->observedNs!=c.source.observedNs||
           c.stroke->deadlineNs!=c.source.deadlineNs)return false;
        pending_=c;lastSerial_=c.serial;consumed_=attack_=0;delivered_=false;return true;
    }
    bool Consume(unsigned branch,std::int64_t now,bool originalSourceCurrent)noexcept {
        if(!pending_||!InputBranch(branch)||(consumed_&(1u<<branch))||now<=0||
           (!consumed_&&(!originalSourceCurrent||!DrawReturnGate::Fresh(*pending_,now))))return false;
        consumed_|=1u<<branch;return true;
    }
    std::optional<interaction::KnifeCommit> ResolveBeforeSource(std::uint64_t sequence,std::int64_t observed,std::int64_t now)noexcept {
        if(!pending_||delivered_)return {};
        if(consumed_){delivered_=true;return interaction::KnifeCommit::Accepted;}
        if(sequence!=pending_->source.sequence||observed!=pending_->source.observedNs||!DrawReturnGate::Fresh(*pending_,now)){
            pending_.reset();return interaction::KnifeCommit::RejectedNoEffect;
        }return {};
    }
    void Observe(unsigned branch,unsigned before,unsigned current,unsigned next)noexcept {
        if(pending_&&ExpectedObservation(branch,pending_->serial)&&
           (before==5||before==6||current==5||current==6||next==5||next==6))attack_|=1u<<branch;
    }
    bool Complete(std::uint64_t serial,bool allCopiesReady,bool allCopiesReturned,bool quiescentUnknown=false)noexcept {
        if(!pending_||pending_->serial!=serial||!delivered_||consumed_!=InputMask||(!quiescentUnknown&&attack_!=ObservationMask)||
           !allCopiesReady||!allCopiesReturned)return false;
        pending_.reset();return true;
    }
    void LifetimeEnded()noexcept {pending_.reset();consumed_=attack_=0;delivered_=false;}
    const std::optional<Command>& Pending()const noexcept{return pending_;}
    bool Accepted()const noexcept{return consumed_!=0;}
    unsigned ConsumedMask()const noexcept{return consumed_;}
    unsigned AttackMask()const noexcept{return attack_;}
private:
    std::optional<Command> pending_;std::uint64_t lastSerial_=0;
    unsigned consumed_=0,attack_=0;bool delivered_=false;
};
}
