#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
namespace fvr::bc2 {
struct ReserveReadEvidence {
 unsigned stage=0,result=2,check=0,active=0;std::uint64_t sampledRevision=0,currentRevision=0,cycle=0;
 std::int64_t observedNs=0,validationNs=0,deadlineNs=0;
 bool targetKnown=false,targetMatched=false;
 bool ownerKnown=false,ownerCurrent=false,configKnown=false,configCurrent=false,cycleKnown=false;
 std::array<std::uint64_t,4> owner{};
};
// Unique writer slots; never overwrite. Report only after callback/reader drain.
// Reserve 32 slots for target-matched final checks; startup/foreign failures cannot evict them.
class ReserveReadEvidenceJournal {
 std::array<ReserveReadEvidence,64> rows_{};std::array<std::atomic<unsigned>,2> totals_{};
public:
 void Observe(const ReserveReadEvidence& e)noexcept {const unsigned bank=e.stage==7&&e.targetKnown&&e.targetMatched?1:0;const auto n=totals_[bank].fetch_add(1);if(n<32)rows_[bank*32+n]=e;}
 void Report(std::ostream& o,bool drained)const {
 o<<"{\"capacity\":64,\"drained\":"<<(drained?"true":"false")<<",\"totals\":["<<totals_[0].load()<<','<<totals_[1].load()<<"],\"dropped\":["<<(totals_[0].load()>32?totals_[0].load()-32:0)<<','<<(totals_[1].load()>32?totals_[1].load()-32:0)<<"],\"rows\":[";
 bool first=true;if(drained)for(unsigned b=0;b<2;++b)for(unsigned n=0;n<totals_[b].load()&&n<32;++n){const auto& e=rows_[b*32+n];if(!first)o<<',';first=false;
 o<<"{\"stage\":"<<e.stage<<",\"result\":"<<e.result<<",\"check\":"<<e.check<<",\"observed_ns\":"<<e.observedNs<<",\"validation_ns\":"<<e.validationNs<<",\"deadline_ns\":"<<e.deadlineNs<<",\"sampled_revision\":"<<e.sampledRevision<<",\"current_revision\":"<<e.currentRevision<<",\"active\":"<<e.active<<",\"owner_known\":"<<e.ownerKnown<<",\"owner_current\":"<<e.ownerCurrent<<",\"config_known\":"<<e.configKnown<<",\"config_current\":"<<e.configCurrent<<",\"target_known\":"<<e.targetKnown<<",\"target_matched\":"<<e.targetMatched<<",\"cycle_known\":"<<e.cycleKnown<<",\"cycle\":"<<e.cycle<<",\"owner\":["<<e.owner[0]<<','<<e.owner[1]<<','<<e.owner[2]<<','<<e.owner[3]<<"]}";}
 o<<"]}";
 }
};
}
