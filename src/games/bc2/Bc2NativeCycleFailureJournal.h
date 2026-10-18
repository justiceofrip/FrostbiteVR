#pragma once
#include "Bc2NativeCycleService.h"
#include <atomic>
#include <bit>
#include <cmath>
#include <ostream>
#include <type_traits>

namespace fvr::bc2 {
// One immutable diagnostic publication per module lifetime. The native callback
// copies values only; file IO and formatting belong to the probe worker. Readers
// do not acquire the gameplay policy gate or renew any native/input authority.
struct NativeCycleFirstFailure {
    Bc2NativeCycleView view{};
    Bc2NativeCyclePhase previousPhase=Bc2NativeCyclePhase::Watching;
    Bc2NativeCycleMode mode=Bc2NativeCycleMode::Spas;
    interaction::HandInteractionSample control{};
    std::int64_t observedNs=0;
    std::uint64_t cancelEpoch=0,contention=0;
    unsigned thread=0;
    unsigned policyAccess=UINT32_MAX;
    // Captured stack values only. No pointers into the event or game survive.
    std::optional<ReloadFlowRecord> triggeringEvent;
    std::optional<Bc2NativeCycleDiagnostic> policyBefore,policyAfter;
};
class NativeCycleFailureJournal {
public:
    bool Capture(const NativeCycleFirstFailure& value,const ReloadFlowRecord* triggeringEvent=nullptr,
        const Bc2NativeCycleDiagnostic* policyBefore=nullptr,const Bc2NativeCycleDiagnostic* policyAfter=nullptr)noexcept {
        if(value.view.phase!=Bc2NativeCyclePhase::Cancelled||
           value.view.failure==Bc2NativeCycleFailure::None||value.observedNs<=0)return false;
        unsigned empty=0;
        if(!state_.compare_exchange_strong(empty,1,std::memory_order_relaxed))return false;
        value_=value;
        if(triggeringEvent)value_.triggeringEvent=*triggeringEvent;
        if(policyBefore)value_.policyBefore=*policyBefore;
        if(policyAfter)value_.policyAfter=*policyAfter;
        state_.store(2,std::memory_order_release);return true;
    }
    std::optional<NativeCycleFirstFailure> Read()const noexcept {
        if(state_.load(std::memory_order_acquire)!=2)return {};
        return value_;
    }
    bool Published()const noexcept{return state_.load(std::memory_order_acquire)==2;}
    static void WriteJson(std::ostream& out,const NativeCycleFirstFailure& f) {
        const auto& v=f.view;const auto& n=v.native;const auto& c=f.control;
        out<<"{\"schema\":\"fvr.bc2.native-cycle-first-failure.v1\",\"observed_ns\":"<<f.observedNs
           <<",\"thread\":"<<f.thread<<",\"mode\":"<<unsigned(f.mode)<<",\"phase_before\":"<<unsigned(f.previousPhase)
           <<",\"phase\":"<<unsigned(v.phase)<<",\"failure\":"<<unsigned(v.failure)
           <<",\"invocation\":"<<v.failureInvocation<<",\"cycle\":"<<v.cycle<<",\"shot\":"<<v.shot
           <<",\"blocks_fire\":"<<(v.blocksFire?"true":"false")<<",\"loaded\":"<<v.loaded<<",\"reserve\":"<<v.reserve
           <<",\"capacity\":"<<v.capacity<<",\"cancel_epoch\":"<<f.cancelEpoch<<",\"contention\":"<<f.contention
           <<",\"owner\":{\"player\":"<<n.owner.player<<",\"soldier\":"<<n.owner.soldier<<",\"weapon\":"<<n.owner.weapon
           <<",\"weak\":"<<n.owner.weak<<",\"actor_generation\":"<<n.owner.actorGeneration
           <<",\"equip_generation\":"<<n.owner.equipGeneration<<",\"space\":"<<n.owner.space<<'}'
           <<",\"firing\":["<<n.firing[0]<<','<<n.firing[1]<<','<<n.firing[2]<<']'
           <<",\"controller\":{\"sequence\":"<<c.sequence<<",\"observed_ns\":"<<c.observedNs
           <<",\"deadline_ns\":"<<c.deadlineNs<<",\"processing_ns\":"<<c.nowNs
           <<",\"focused\":"<<(c.focused?"true":"false")<<",\"tracked\":["<<(c.tracked[0]?"true":"false")
           <<','<<(c.tracked[1]?"true":"false")<<"],\"released\":["<<(c.released[0]?"true":"false")
           <<','<<(c.released[1]?"true":"false")<<"]}"
           <<",\"policy_access\":"<<f.policyAccess<<",\"triggering_event\":";
        if(f.triggeringEvent)WriteEvent(out,*f.triggeringEvent,v.failureInvocation);else out<<"null";
        out<<",\"policy_before\":";if(f.policyBefore)WritePolicy(out,*f.policyBefore);else out<<"null";
        out<<",\"policy_after\":";if(f.policyAfter)WritePolicy(out,*f.policyAfter);else out<<"null";
        out<<"}\n";
    }
private:
    static void WritePolicy(std::ostream& out,const Bc2NativeCycleDiagnostic& p) {
        out<<"{\"recovery_active\":"<<(p.recoveryActive?"true":"false")
           <<",\"concurrent_cycle\":"<<(p.concurrentCycle?"true":"false")
           <<",\"release_present\":"<<(p.releasePresent?"true":"false")
           <<",\"ready_present\":"<<(p.readyPresent?"true":"false")
           <<",\"fired_mask\":"<<p.firedMask<<",\"held_mask\":"<<p.heldMask<<",\"ready_mask\":"<<p.readyMask;
        const auto array=[&](const char* name,const auto& values){out<<",\""<<name<<"\":[";
            for(unsigned n=0;n<values.size();++n){if(n)out<<',';out<<values[n];}out<<']';};
        array("tail",p.tail);array("cycle_state",p.cycleState);array("last_invocation",p.lastInvocation);
        array("open_update",p.openUpdate);array("finished_at_ns",p.finishedAt);
        out<<",\"began_ns\":"<<p.beganNs<<",\"deadline_ns\":"<<p.deadlineNs
           <<",\"release_submitted_ns\":"<<p.releaseSubmittedNs<<",\"release_observed_ns\":"<<p.releaseObservedNs
           <<",\"acknowledged_at_ns\":"<<p.acknowledgedAtNs<<'}';
    }
    static void WriteOwner(std::ostream& out,const ReloadStateOwner& owner) {
        out<<"{\"player\":"<<owner.player<<",\"soldier\":"<<owner.soldier<<",\"weak\":"<<owner.weak
           <<",\"weapon\":"<<owner.weapon<<",\"actor_generation\":"<<owner.actorGeneration
           <<",\"equip_generation\":"<<owner.equipGeneration<<",\"space\":"<<owner.space<<'}';
    }
    static void WriteBoundary(std::ostream& out,const ReloadFlowBoundary& b) {
        out<<"{\"owner\":";WriteOwner(out,b.owner);
        out<<",\"snapshot_sequence\":"<<b.snapshotSequence<<",\"firing\":"<<b.firing
           <<",\"wrapper_offset\":"<<b.wrapperOffset<<",\"server_player\":"<<b.serverPlayer
           <<",\"server_soldier\":"<<b.serverSoldier<<",\"server_item\":"<<b.serverItem
           <<",\"branch\":"<<unsigned(b.branch)<<",\"soldier_flags\":"<<unsigned(b.soldierFlags)
           <<",\"current\":"<<b.current<<",\"previous\":"<<b.previous<<",\"next\":"<<b.next<<",\"timer\":";
        if(std::isfinite(b.timer))out<<b.timer;else out<<"null";
        out<<",\"timer_bits\":"<<std::bit_cast<std::uint32_t>(b.timer)<<",\"loaded\":"<<b.loaded
           <<",\"reserve\":"<<b.reserve<<",\"flags_a8\":"<<unsigned(b.flagsA8)<<'}';
    }
    template<std::size_t N>static void WriteBytes(std::ostream& out,const std::array<std::byte,N>& bytes,bool copied) {
        // Worker-only serialization. Hex preserves every operand; the small
        // FNV-1a digest is diagnostic comparison, never a source/admission pin.
        constexpr char hex[]="0123456789abcdef";
        out<<"{\"copied\":"<<(copied?"true":"false")<<",\"hex\":";
        if(!copied){out<<"null,\"fnv1a64\":null}";return;}
        std::uint64_t hash=14695981039346656037ull;out.put('"');
        for(const auto byte:bytes){const auto v=std::to_integer<unsigned>(byte);
            out.put(hex[v>>4]);out.put(hex[v&15]);hash=(hash^v)*1099511628211ull;}
        out<<"\",\"fnv1a64\":\"";
        for(unsigned nibble=16;nibble>0;--nibble)out.put(hex[(hash>>((nibble-1)*4))&15]);
        out<<"\"}";
    }
    static void WriteEvent(std::ostream& out,const ReloadFlowRecord& r,std::uint64_t failureInvocation) {
        const auto& a=r.entry;const auto& b=r.exit;
        out<<"{\"id\":"<<r.id<<",\"matches_failure_invocation\":"<<(r.id&&r.id==failureInvocation?"true":"false")
           <<",\"kind\":"<<unsigned(a.kind)<<",\"finished\":"<<(r.finished?"true":"false")
           <<",\"identity_retained\":"<<(r.identityRetained?"true":"false")
           <<",\"snapshot_unchanged\":"<<(a.snapshotCopied&&b.snapshotCopied&&a.copiedSnapshot==b.copiedSnapshot?"true":"false")
           <<",\"restore_matched\":"<<(ReloadRestoreMatched(r)?"true":"false")
           <<",\"entry\":{\"parent\":"<<a.parent<<",\"update\":"<<a.update
           <<",\"native_invocation\":"<<a.nativeInvocation<<",\"native_parent\":"<<a.nativeParent
           <<",\"native_update\":"<<a.nativeUpdate<<",\"thread\":"<<a.thread<<",\"depth\":"<<a.depth
           <<",\"caller\":"<<a.caller<<",\"context\":"<<a.context<<",\"argument\":"<<a.argument
           <<",\"now_ns\":"<<a.nowNs<<",\"tick_ms\":"<<a.tickMs<<",\"boundary\":";
        WriteBoundary(out,a.boundary);out<<",\"snapshot\":";WriteBytes(out,a.copiedSnapshot,a.snapshotCopied);
        out<<",\"context_bytes\":";WriteBytes(out,a.copiedContext,a.contextCopied);
        out<<"},\"exit\":{\"thread\":"<<b.thread<<",\"now_ns\":"<<b.nowNs<<",\"tick_ms\":"<<b.tickMs<<",\"boundary\":";
        if(b.boundary)WriteBoundary(out,*b.boundary);else out<<"null";
        out<<",\"snapshot\":";WriteBytes(out,b.copiedSnapshot,b.snapshotCopied);
        out<<",\"context_bytes\":";WriteBytes(out,b.copiedContext,b.contextCopied);
        const auto& h=b.hold;
        out<<",\"hold\":{\"requested\":"<<(b.holdRequested?"true":"false")
           <<",\"applied\":"<<(h.applied?"true":"false")<<",\"restored\":"<<(h.restored?"true":"false")
           <<",\"unexpected_native_write\":"<<(h.unexpectedNativeWrite?"true":"false")
           <<",\"original\":"<<h.original<<",\"before_restore\":"<<h.beforeRestore<<'}';
        const auto& g=b.cycleGuard;
        out<<",\"cycle_guard\":{\"epoch\":"<<b.cycleGuardEpoch<<",\"steps\":"<<b.cycleGuardSteps
           <<",\"rejected\":"<<b.cycleGuardRejected<<",\"requested\":"<<(g.requested?"true":"false")
           <<",\"applied\":"<<(g.applied?"true":"false")<<",\"rollback\":"<<(g.rollback?"true":"false")
           <<",\"restored\":"<<(g.restored?"true":"false")<<",\"unexpected_native_write\":"<<(g.unexpectedWrite?"true":"false")
           <<",\"original\":["<<g.original[0]<<','<<g.original[1]<<"],\"effective\":["<<g.effective[0]<<','<<g.effective[1]
           <<"],\"before_restore\":["<<g.beforeRestore[0]<<','<<g.beforeRestore[1]<<"]}}}";
    }
    std::atomic<unsigned> state_{0};
    NativeCycleFirstFailure value_{};
};
static_assert(std::is_trivially_copyable_v<ReloadFlowRecord>);
static_assert(sizeof(NativeCycleFirstFailure)<=4096); // One fixed bounded publication.
}
