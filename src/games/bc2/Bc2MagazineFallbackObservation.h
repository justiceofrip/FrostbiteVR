#pragma once
#include "Bc2MagazinePresentation.h"
#include <atomic>
namespace fvr::bc2 {
struct MagazineFallbackSnapshot {
 bool present=false,targetPresent=false;
 unsigned role=0;ReloadStateOwner owner{};interaction::HandInteractionOwner targetOwner{};
 std::uint64_t cycle=0,input=0,reserveSequence=0,targetInput=0,item=0,itemGeneration=0;
 interaction::HandClaimToken hand{},gun{};
 std::int64_t inputObservedNs=0,targetObservedNs=0;
 // input, pooled native reserve, selected meshes, family, target, carry frame
 std::array<std::int64_t,6> deadlines{};
 bool removalFrame=false,replacementFrame=false;
};
inline MagazineFallbackSnapshot MagazineFallbackSnapshotOf(const MagazineTracking* t)noexcept {
 MagazineFallbackSnapshot s;if(!t)return s;s.present=true;s.owner=t->owner;s.cycle=t->cycle;
 s.input=t->inputEvidence.sequence;s.reserveSequence=t->reserve.sequence;s.inputObservedNs=t->inputEvidence.observedNs;
 s.deadlines={t->inputEvidence.deadlineNs,t->reserve.deadlineNs,t->selected?t->selected->deadlineNs:0,t->family.deadlineNs,0,0};
 s.removalFrame=bool(t->removalFrame);s.replacementFrame=bool(t->replacementFrame);
 if(t->removalFrame)s.deadlines[5]=t->removalFrame->deadlineNs;else if(t->replacementFrame)s.deadlines[5]=t->replacementFrame->deadlineNs;
 if(t->target){const auto& v=*t->target;s.targetPresent=true;s.role=unsigned(v.role);s.targetOwner=v.owner;
  s.targetInput=v.inputSequence;s.targetObservedNs=v.observedNs;s.deadlines[4]=v.deadlineNs;
  s.hand=v.handClaim;s.gun=v.gunClaim;s.item=v.item.id;s.itemGeneration=v.item.generation;}
 return s;
}
struct MagazineFallbackEvent {
 std::uint64_t flags=0,drawSerial=0,source=0,shotSequence=0;
 std::int64_t nowNs=0,shotDeadlineQpcTicks=0;
 unsigned count=0,pack=0;bool shotPresent=false,shotValid=false,leftTracked=false,coherent=false;
 MagazineFallbackSnapshot original{},current{};
};
// Cold-path diagnostics only. Each unique slot is published once and is never
// overwritten. A reader sees only release/acquire committed immutable rows.
template<std::size_t Capacity=64>class MagazineFallbackJournal {
 struct Slot {MagazineFallbackEvent event{};std::atomic<bool> committed=false;};
 std::array<Slot,Capacity> rows_{};
 std::array<std::atomic<unsigned>,unsigned(MagazineFallbackReason::Count)> reasons_{};
 std::atomic<unsigned> total_=0,dropped_=0;
public:
 bool Record(const MagazineFallbackEvent& e)noexcept {
  for(unsigned n=0;n<reasons_.size();++n)if(e.flags&(1ull<<n))reasons_[n].fetch_add(1,std::memory_order_relaxed);
  const auto index=total_.fetch_add(1,std::memory_order_relaxed);
  if(index>=Capacity){dropped_.fetch_add(1,std::memory_order_relaxed);return false;}
  rows_[index].event=e;rows_[index].committed.store(true,std::memory_order_release);return true;
 }
 unsigned Total()const noexcept{return total_.load();}
 unsigned Dropped()const noexcept{return dropped_.load();}
 unsigned ReasonCount(MagazineFallbackReason r)const noexcept{return reasons_[unsigned(r)].load();}
 template<class F>void ForEach(F&& f)const {
  for(const auto& row:rows_)if(row.committed.load(std::memory_order_acquire))f(row.event);
 }
};
} // namespace fvr::bc2
