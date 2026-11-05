#pragma once
#include "ChestKnifeCustody.h"
#include <atomic>
namespace fvr::bc2::private_knife {
// Gather is the sole writer, including calls that cannot acquire the native
// gate. Native callbacks read only the monotonic stamp. This record grants no
// input authority and never changes the draw/return source or accepted work.
class StockKnifeSourceRevocation {
public:
    bool Advance(const interaction::KnifeSample& s,bool returnWanted=false)noexcept {
        const auto& f=s.source;
        bool revoke=returnWanted||!CurrentEvidence(s);
        if(last_){const auto& old=*last_;const auto& p=old.source;
            revoke|=f.owner!=p.owner||f.sequence!=p.sequence||f.observedNs!=p.observedNs||
                f.deadlineNs!=p.deadlineNs||f.focused!=p.focused||f.tracked!=p.tracked||
                f.released!=p.released||f.nowNs<p.nowNs||s.knife!=old.knife||
                s.menu.epoch!=old.menu.epoch||s.stockTrigger!=old.stockTrigger||
                s.heldContact.key!=old.heldContact.key||s.heldContact.inputSequence!=old.heldContact.inputSequence||
                s.heldContact.deadlineNs!=old.heldContact.deadlineNs;
        }
        last_=s;
        if(revoke)stamp_.fetch_add(1,std::memory_order_acq_rel);
        return revoke;
    }
    std::uint64_t Stamp()const noexcept{return stamp_.load(std::memory_order_acquire);}
    bool Current(std::uint64_t stamp)const noexcept{return stamp&&stamp==Stamp();}
private:
    static bool CurrentEvidence(const interaction::KnifeSample& s)noexcept {
        const auto& f=s.source;const auto& p=s.heldContact;
        return f.owner.actor&&f.owner.actorGeneration&&f.owner.equipGeneration&&f.owner.space&&f.sequence&&
            f.focused&&f.tracked[0]&&f.tracked[1]&&f.observedNs>0&&f.nowNs>=f.observedNs&&f.deadlineNs>f.nowNs&&
            f.deadlineNs-f.observedNs<=100000000&&interaction::GameplayOwnsInput(s.menu,f.nowNs)&&
            s.stockTriggerReady&&s.stockTriggerInputCurrent&&(s.controlsNeutral||s.holdInputCurrent)&&
            s.onFoot&&s.fireSuppressed&&s.mechanicsSettled&&s.knifeSelected&&s.trackedKnifeOwned&&
            s.knife.id&&s.knife.generation&&s.gripHeld[1]&&!f.released[1]&&
            p.eligible&&p.key.id&&p.key.generation&&p.inputSequence==f.sequence&&p.deadlineNs>f.nowNs&&p.deadlineNs<=f.deadlineNs&&
            std::isfinite(s.stockTrigger)&&s.stockTrigger>=0&&s.stockTrigger<=1;
    }
    std::optional<interaction::KnifeSample> last_;
    std::atomic<std::uint64_t> stamp_=1;
};
}
