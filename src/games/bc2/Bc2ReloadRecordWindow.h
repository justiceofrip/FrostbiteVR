#pragma once
#include <atomic>
#include <cstdint>
namespace fvr::bc2 {
// Diagnostic recording only. Native invocation/hold/transfer authority never
// reads this clock. A deferred window opens once; later calls cannot renew it.
class ReloadRecordWindow {
public:
 static constexpr std::int64_t Duration=20000000000ll;
 bool Pump(unsigned cycles)noexcept {
  if(start_.load()||begun_||deferred_||pump_||(cycles!=1&&cycles!=2&&cycles!=8))return false;
  pump_=true;duration_=cycles==8?3*Duration:Duration;return true;
 }
 bool Bolt()noexcept {
  if(start_.load()||begun_||deferred_||pump_)return false;
  pump_=true;duration_=30000000000ll;return true;
 }
 bool Defer(bool recovery=false)noexcept {if(start_.load()||begun_||deferred_||pump_)return false;deferred_=true;duration_=recovery?2*Duration:Duration;return true;}
 bool CombinedPump()noexcept {
  if(start_.load()||begun_||!deferred_||pump_||duration_!=2*Duration)return false;
  pump_=true;duration_=3*Duration;return true;
 }
 void Start(std::int64_t now)noexcept {if(begun_||now<=0)return;begun_=true;if(!deferred_)start_.store(now,std::memory_order_release);}
 bool OpenDeferred(std::int64_t now)noexcept {
  if(!begun_||!deferred_||now<=0)return false;std::int64_t zero=0;
  start_.compare_exchange_strong(zero,now,std::memory_order_acq_rel);return true;
 }
 std::int64_t StartNs()const noexcept{return start_.load(std::memory_order_acquire);}
 std::int64_t DurationNs()const noexcept{return duration_;}
 bool Contains(std::int64_t now)const noexcept {const auto start=StartNs();return start>0&&now>=start&&now-start<duration_;}
private:
 std::atomic<std::int64_t> start_{0};bool deferred_=false,begun_=false,pump_=false;
 std::int64_t duration_=Duration;
};
}
