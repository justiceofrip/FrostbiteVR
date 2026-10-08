#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace fvr::bc2 {
struct ReloadLockBodyRow {unsigned site=0,thread=0,firing=0;std::int64_t beginNs=0,endNs=0;};
// Diagnostic only. Rows are read only AFTER hooks/workers drain. No authority.
class ReloadLockBodyTelemetry {
 std::array<ReloadLockBodyRow,32> rows_{};std::atomic<unsigned> total_=0,slow_=0,invalid_=0;
 std::atomic<std::int64_t> maximum_=0;
public:
 void Observe(unsigned site,unsigned thread,unsigned firing,std::int64_t begin,std::int64_t end) noexcept {
  ++total_;if(begin<=0||end<begin){++invalid_;return;}const auto elapsed=end-begin;
  auto prior=maximum_.load();while(elapsed>prior&&!maximum_.compare_exchange_weak(prior,elapsed)){}
  if(elapsed>=100000){const auto slot=slow_.fetch_add(1);if(slot<rows_.size())rows_[slot]={site,thread,firing,begin,end};}
 }
 unsigned Total()const noexcept{return total_.load();}unsigned Slow()const noexcept{return slow_.load();}
 unsigned Invalid()const noexcept{return invalid_.load();}std::int64_t Maximum()const noexcept{return maximum_.load();}
 unsigned CountAfterDrain()const noexcept{return Slow()<rows_.size()?Slow():unsigned(rows_.size());}
 const auto& RowsAfterDrain()const noexcept{return rows_;}
};
}
