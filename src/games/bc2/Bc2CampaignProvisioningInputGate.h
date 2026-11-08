#pragma once
#include "Bc2HolsterInput.h"
#include <mutex>
#include <optional>

namespace fvr::bc2::campaignProvisioning {
// Private opt-in callback boundary. Input callbacks retain this lock through
// their original invocation. Seat control is nonblocking and retains the same
// lock through Service::SeatEntry, including native assignment and retirement.
// Recursive native entry may observe/suppress, but cannot admit seat control.
class InputGate {
public:
    class Update {
    public:
        explicit Update(InputGate& gate,bool enabled=true):gate_(gate),lock_(gate.mutex_,std::defer_lock){
            if(enabled){lock_.lock();++gate_.callbacks_;excluded_=gate_.excluded_;}
        }
        ~Update(){if(lock_.owns_lock())--gate_.callbacks_;}
        Update(const Update&)=delete;Update& operator=(const Update&)=delete;
        bool Excluded()const noexcept{return excluded_;}
    private:
        InputGate& gate_;std::unique_lock<std::recursive_mutex> lock_;bool excluded_=false;
    };
    class Gather {
    public:
        explicit Gather(InputGate& gate,bool enabled=true):gate_(gate),lock_(gate.mutex_,std::defer_lock){
            if(enabled){
                lock_.lock();++gate_.callbacks_;sequence_=++gate_.gatherSequence_;
                // Must happen BEFORE every original Gather, including failed,
                // mismatched, repeated, nested and out-of-Update callbacks.
                gate_.neutral_.reset();
            }
        }
        ~Gather(){if(lock_.owns_lock()){
            // An outer Gather may write again after a nested one returned.
            if(!committed_||sequence_!=gate_.gatherSequence_)gate_.neutral_.reset();
            --gate_.callbacks_;
        }}
        Gather(const Gather&)=delete;Gather& operator=(const Gather&)=delete;
        bool Excluded()const noexcept{return lock_.owns_lock()&&gate_.excluded_;}
        std::uint64_t Nonce()const noexcept{return lock_.owns_lock()?gate_.nonce_:0;}
        std::uint64_t NativeTick()const noexcept{return sequence_;}
        bool Commit(const HolsterSuppressionReceipt& receipt)noexcept{
            if(!Excluded()||sequence_!=gate_.gatherSequence_||receipt.equipmentDispatched||
               receipt.request!=gate_.nonce_||receipt.nativeTick!=sequence_||receipt.owner!=gate_.owner_||receipt.cache!=gate_.cache_||
               !HolsterSuppressionCurrent(receipt,receipt))return false;
            gate_.neutral_=receipt;committed_=true;return true;
        }
    private:
        InputGate& gate_;std::unique_lock<std::recursive_mutex> lock_;std::uint64_t sequence_=0;bool committed_=false;
    };
    class Control {
    public:
        explicit Control(InputGate& gate):gate_(gate),lock_(gate.mutex_,std::try_to_lock){}
        Control(const Control&)=delete;Control& operator=(const Control&)=delete;
        // The count also rejects same-thread reentry through the recursive lock.
        explicit operator bool()const noexcept{return lock_.owns_lock()&&!gate_.callbacks_;}
        bool Excluded()const noexcept{return bool(*this)&&gate_.excluded_;}
        bool Begin(std::uint64_t nonce,const ReloadStateOwner& owner,std::uint32_t cache)noexcept{
            if(!*this||gate_.excluded_||!nonce||cache<0x10000)return false;
            gate_.neutral_.reset();gate_.nonce_=nonce;gate_.owner_=owner;gate_.cache_=cache;
            gate_.excluded_=true;return true;
        }
        bool Neutral(const ReloadStateOwner& owner,std::uint64_t nonce,std::int64_t now)const noexcept{
            if(!Excluded()||nonce!=gate_.nonce_||owner!=gate_.owner_||!gate_.neutral_)return false;
            const auto& receipt=*gate_.neutral_;
            auto current=static_cast<const HolsterSuppressionRequest&>(receipt);
            // Preserve the original observedNs/deadlineNs; only evaluation time
            // changes. A repeated or expired XR packet cannot renew authority.
            current.input.nowNs=now;
            return now>=receipt.input.nowNs&&receipt.owner==gate_.owner_&&receipt.cache==gate_.cache_&&
                receipt.request==gate_.nonce_&&HolsterSuppressionCurrent(receipt,current);
        }
        bool Resume(std::uint64_t nonce)noexcept{
            if(!Excluded()||nonce!=gate_.nonce_)return false;
            gate_.neutral_.reset();gate_.excluded_=false;gate_.nonce_=0;return true;
        }
    private:
        InputGate& gate_;std::unique_lock<std::recursive_mutex> lock_;
    };
private:
    std::recursive_mutex mutex_;unsigned callbacks_=0;bool excluded_=false;
    std::uint64_t nonce_=0,gatherSequence_=0;ReloadStateOwner owner_{};std::uint32_t cache_=0;
    std::optional<HolsterSuppressionReceipt> neutral_;
};
}
