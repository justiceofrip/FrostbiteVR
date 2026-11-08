#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

namespace fvr::bc2 {
// Bounded diagnostic value copies only. The supplied flag is dedicated to this
// journal; updates/copies try it once and hold it for no native work.
struct DiagnosticObservationNoRetention {
    template<class Entry> void Observe(const Entry&,bool)noexcept {}
};
template<class Row,class Retention=DiagnosticObservationNoRetention> class DiagnosticObservationWindow {
public:
    struct Ticket {
        std::uint64_t epoch=0;
        std::int64_t beginNs=0,endNs=0;
    };
    struct Entry {Ticket window{};Row value{};};
    struct Snapshot {
        Ticket window{};
        std::uint64_t calls=0,rejected=0,lateEpochDropped=0;
        std::optional<Entry> firstFailure,precedingAccepted,lastAccepted,latest;
        Retention retention{};
    };
    struct DropCounts {std::uint64_t publication=0,request=0,snapshot=0;};
    explicit DiagnosticObservationWindow(std::atomic_flag& flag)noexcept:flag_(flag){}
    bool Request(std::int64_t now,std::int64_t existingUntilNs)noexcept {
        if(now<=0||existingUntilNs<=now)return false;
        Gate gate(flag_);
        if(!gate.held){requestDropped_.fetch_add(1,std::memory_order_relaxed);return false;}
        if(state_.window.epoch&&now<state_.window.beginNs)return false;
        if(!state_.window.epoch||now>=state_.window.endNs){
            if(state_.window.epoch==std::numeric_limits<std::uint64_t>::max())return false;
            const auto epoch=state_.window.epoch+1;
            // Disable entry before resetting. Never publish a fresh epoch whose
            // reset failed, or carry a preceding acceptance across windows.
            epoch_.store(0,std::memory_order_release);
            state_={};state_.window={epoch,now,existingUntilNs};
            beginNs_.store(now,std::memory_order_relaxed);
        }else if(existingUntilNs>state_.window.endNs)state_.window.endNs=existingUntilNs;
        endNs_.store(state_.window.endNs,std::memory_order_relaxed);
        epoch_.store(state_.window.epoch,std::memory_order_release);
        return true;
    }
    Ticket Begin(std::int64_t now)const noexcept {
        const auto epoch=epoch_.load(std::memory_order_acquire);
        if(!epoch||now<=0)return {};
        const auto end=endNs_.load(std::memory_order_relaxed);if(now>=end)return {};
        const auto begin=beginNs_.load(std::memory_order_relaxed);
        // A racing reset may drop this observation, but cannot admit it into a
        // different epoch. No retry or gameplay authority follows from a ticket.
        if(now<begin||epoch!=epoch_.load(std::memory_order_acquire))return {};
        return {epoch,begin,end};
    }
    bool Publish(const Ticket& ticket,const Row& row,bool rejected)noexcept {
        if(!ticket.epoch)return false;
        Gate gate(flag_);
        if(!gate.held){publicationDropped_.fetch_add(1,std::memory_order_relaxed);return false;}
        if(ticket.epoch!=state_.window.epoch){++state_.lateEpochDropped;return false;}
        const Entry entry{ticket,row};++state_.calls;
        if(rejected){
            ++state_.rejected;
            if(!state_.firstFailure){state_.firstFailure=entry;state_.precedingAccepted=state_.lastAccepted;}
        }else state_.lastAccepted=entry;
        state_.latest=entry;state_.retention.Observe(entry,rejected);return true;
    }
    bool Read(Snapshot& copy)noexcept {
        Gate gate(flag_);
        if(!gate.held){snapshotDropped_.fetch_add(1,std::memory_order_relaxed);return false;}
        copy=state_;return true;
    }
    // Busy drops cannot safely acquire the flag. These counters remain process
    // totals; retained calls/rejections and late-epoch drops reset per window.
    DropCounts Drops()const noexcept {
        return {publicationDropped_.load(std::memory_order_relaxed),requestDropped_.load(std::memory_order_relaxed),
            snapshotDropped_.load(std::memory_order_relaxed)};
    }
private:
    struct Gate {
        std::atomic_flag& flag;bool held;
        explicit Gate(std::atomic_flag& value)noexcept:flag(value),held(!flag.test_and_set(std::memory_order_acquire)){}
        ~Gate(){if(held)flag.clear(std::memory_order_release);}
    };
    std::atomic_flag& flag_;
    std::atomic<std::uint64_t> epoch_=0,publicationDropped_=0,requestDropped_=0,snapshotDropped_=0;
    std::atomic<std::int64_t> beginNs_=0,endNs_=0;
    Snapshot state_{};
};
}
