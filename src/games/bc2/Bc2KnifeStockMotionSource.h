#pragma once
#include "ChestKnifeCustody.h"
#include <atomic>
namespace fvr::bc2::private_knife {
// Independent of trigger arming: a deliberate tracked stroke can request the
// original stock attack with a neutral physical trigger. Gather revokes this
// source before trying the native mutex, including immutable-packet mutations.
// Native work already consumed keeps its existing once-only sibling drain.
class StockKnifeMotionSourceRevocation {
public:
    bool Advance(const interaction::KnifeSample& s,bool returnWanted=false)noexcept {
        const auto& f=s.source;bool revoke=returnWanted||!CurrentEvidence(s);
        if(last_){const auto& old=*last_;const auto& p=old.source;
            revoke|=f.owner!=p.owner||f.sequence!=p.sequence||f.observedNs!=p.observedNs||
                f.deadlineNs!=p.deadlineNs||f.focused!=p.focused||f.tracked!=p.tracked||
                f.released!=p.released||f.nowNs<p.nowNs||s.knife!=old.knife||s.menu.epoch!=old.menu.epoch||
                s.tip!=old.tip||s.direction!=old.direction||s.stockTrigger!=old.stockTrigger||
                s.heldContact.key!=old.heldContact.key||s.heldContact.inputSequence!=old.heldContact.inputSequence||
                s.heldContact.deadlineNs!=old.heldContact.deadlineNs;
        }
        last_=s;if(revoke)stamp_.fetch_add(1,std::memory_order_acq_rel);return revoke;
    }
    std::uint64_t Stamp()const noexcept{return stamp_.load(std::memory_order_acquire);}
    bool Current(std::uint64_t stamp)const noexcept{return stamp&&stamp==Stamp();}
    // Gather only. Caller separately proves an unchanged raw controller packet,
    // live claim and successful pending Stage. Missing native observation fields
    // are never copied back into a sample or used to admit another command.
    bool PreservePending(const interaction::KnifeSample& s,std::uint64_t stamp)const noexcept {
        if(!Current(stamp)||!last_||!CurrentEvidence(*last_))return false;
        const auto& old=*last_;const auto& f=s.source;const auto& p=old.source;
        return f.owner==p.owner&&f.sequence==p.sequence&&f.observedNs==p.observedNs&&f.deadlineNs==p.deadlineNs&&
            f.focused==p.focused&&f.tracked==p.tracked&&f.released==p.released&&f.nowNs>=p.nowNs&&
            f.nowNs<f.deadlineNs&&f.nowNs<old.heldContact.deadlineNs&&s.knife==old.knife&&s.menu==old.menu&&
            interaction::GameplayOwnsInput(s.menu,f.nowNs)&&s.stockTrigger==old.stockTrigger&&
            s.fireSuppressed&&s.holdInputCurrent&&interaction::KnifeActionInputCurrent(s)&&s.gripHeld[1]&&!f.released[1];
    }
private:
    static bool CurrentEvidence(const interaction::KnifeSample& s)noexcept {
        const auto& f=s.source;const auto& p=s.heldContact;
        return f.owner.actor&&f.owner.actorGeneration&&f.owner.equipGeneration&&f.owner.space&&f.sequence&&
            f.focused&&f.tracked[0]&&f.tracked[1]&&f.observedNs>0&&f.nowNs>=f.observedNs&&f.deadlineNs>f.nowNs&&
            f.deadlineNs-f.observedNs<=100000000&&interaction::GameplayOwnsInput(s.menu,f.nowNs)&&
            s.nativeAttackReady&&interaction::KnifeActionInputCurrent(s)&&(s.controlsNeutral||s.holdInputCurrent)&&
            std::isfinite(s.stockTrigger)&&s.stockTrigger>=0&&s.stockTrigger<=.1f&&
            s.onFoot&&s.fireSuppressed&&s.mechanicsSettled&&s.knifeSelected&&s.trackedKnifeOwned&&
            s.knife.id&&s.knife.generation&&s.gripHeld[1]&&!f.released[1]&&
            p.eligible&&p.key.id&&p.key.generation&&p.inputSequence==f.sequence&&p.deadlineNs>f.nowNs&&p.deadlineNs<=f.deadlineNs&&
            std::isfinite(s.tip.x)&&std::isfinite(s.tip.y)&&std::isfinite(s.tip.z)&&
            std::isfinite(s.direction.x)&&std::isfinite(s.direction.y)&&std::isfinite(s.direction.z)&&
            std::abs(std::hypot(s.direction.x,s.direction.y,s.direction.z)-1.f)<=.001f;
    }
    std::optional<interaction::KnifeSample> last_;
    std::atomic<std::uint64_t> stamp_=1;
};
}
