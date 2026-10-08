#pragma once
#include <atomic>
#include <cstdint>
namespace fvr::bc2 {
// Diagnostic recording only. Native invocation/hold/transfer authority never
// reads this clock. A deferred window opens once; later calls cannot renew it.
class ReloadRecordWindow {
public:
 static constexpr std::int64_t Duration=20000000000ll;
 bool Defer()noexcept {if(start_.load()||begun_)return false;deferred_=true;return true;}
 void Start(std::int64_t now)noexcept {if(begun_||now<=0)return;begun_=true;if(!deferred_)start_.store(now,std::memory_order_release);}
 bool OpenDeferred(std::int64_t now)noexcept {
  if(!begun_||!deferred_||now<=0)return false;std::int64_t zero=0;
  start_.compare_exchange_strong(zero,now,std::memory_order_acq_rel);return true;
 }
 std::int64_t StartNs()const noexcept{return start_.load(std::memory_order_acquire);}
 bool Contains(std::int64_t now)const noexcept {const auto start=StartNs();return start>0&&now>=start&&now-start<Duration;}
private:
 std::atomic<std::int64_t> start_{0};bool deferred_=false,begun_=false;
};
}
