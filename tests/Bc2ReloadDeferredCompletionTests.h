#pragma once
#include "Bc2ReloadFlowRuntime.h"
#ifndef LEGACY_END_DROP
#include "Bc2ReloadDeferredCompletion.h"
#else
namespace fvr::bc2 {struct ReloadDeferredCompletions {
 static constexpr unsigned Capacity=64;
 bool Publish(std::uint64_t,const ReloadFlowEventEnd&){return false;}
 void Drain(ReloadFlowRecords&){}
 unsigned Pending()const{return 0;} unsigned Recovered()const{return 0;}
 unsigned Overflow()const{return 0;} unsigned Rejected()const{return 0;}
};}
#endif
#include <memory>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
using namespace fvr::bc2;
namespace completion_journal_tests {
ReloadFlowEventInput Input(){ReloadFlowEventInput in;in.thread=7;in.depth=1;in.nowNs=100;
 in.boundary.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};in.boundary.firing=0x50000;return in;}
ReloadFlowEventEnd End(){ReloadFlowEventEnd out;out.thread=7;out.nowNs=200;out.tickMs=2;
 out.boundary=Input().boundary;out.contextCopied=true;out.copiedContext[0]=std::byte{23};return out;}
int ContendedEnd(){auto records=std::make_unique<ReloadFlowRecords>();ReloadDeferredCompletions journal;
 const auto id=records->Begin(Input());CHECK(id);auto captured=End();
 // The native End already returned. A busy record gate cannot modify Records.
 std::atomic_flag gate=ATOMIC_FLAG_INIT;CHECK(!gate.test_and_set(std::memory_order_acquire));
 CHECK(gate.test_and_set(std::memory_order_acquire)); // Deterministic busy End gate.
 journal.Publish(id,captured);CHECK(!records->Records()[0].finished);gate.clear(std::memory_order_release);
 captured.nowNs=999;captured.copiedContext[0]=std::byte{99};
 journal.Drain(*records);CHECK(records->Records()[0].finished);
 CHECK(records->Records()[0].exit.nowNs==200&&records->Records()[0].exit.copiedContext[0]==std::byte{23});
 CHECK(journal.Pending()==0&&journal.Recovered()==1&&journal.Overflow()==0&&journal.Rejected()==0);
 journal.Drain(*records);CHECK(journal.Recovered()==1&&records->Rejected()==0);return 0;}
int Rejection(){auto records=std::make_unique<ReloadFlowRecords>();ReloadDeferredCompletions journal;
 const auto id=records->Begin(Input());CHECK(journal.Publish(999,End()));
 auto bad=End();bad.thread=8;CHECK(journal.Publish(id,bad));bad=End();bad.nowNs=99;CHECK(journal.Publish(id,bad));
 journal.Drain(*records);CHECK(journal.Rejected()==3&&journal.Recovered()==0&&!records->Records()[0].finished);
 CHECK(journal.Publish(id,End())&&journal.Publish(id,End()));journal.Drain(*records);
 CHECK(journal.Recovered()==1&&journal.Rejected()==4&&journal.Pending()==0);return 0;}
int Overflow(){auto records=std::make_unique<ReloadFlowRecords>();ReloadDeferredCompletions journal;
 for(unsigned n=0;n<65;++n){const auto id=records->Begin(Input());CHECK(id==n+1);
  CHECK(journal.Publish(id,End())==(n<64));}
 CHECK(journal.Pending()==64&&journal.Overflow()==1);journal.Drain(*records);
 CHECK(journal.Recovered()==64&&journal.Pending()==0&&!records->Records()[64].finished);
 journal.Drain(*records);CHECK(journal.Overflow()==1&&journal.Recovered()==64);return 0;}
int Parent(){auto records=std::make_unique<ReloadFlowRecords>();ReloadDeferredCompletions journal;
 const auto id=records->Begin(Input());CHECK(journal.Publish(id,End()));journal.Drain(*records);
 auto child=Input();child.depth=2;child.parent=id;child.update=id;child.kind=ReloadFlowEvent::Commit;
 CHECK(!records->Begin(child)); // Successful gate flush precedes parent validation.
 auto parent=Input();const auto open=records->Begin(parent);child.parent=child.update=open;
 const auto nested=records->Begin(child);CHECK(nested);
 CHECK(journal.Publish(nested,End()));journal.Drain(*records);CHECK(records->Records()[nested-1].finished);
 CHECK(!records->Records()[open-1].finished);CHECK(records->End(open,End()));return 0;}
int Concurrent(){auto records=std::make_unique<ReloadFlowRecords>();ReloadDeferredCompletions journal;
 for(unsigned n=0;n<512;++n)CHECK(records->Begin(Input())==n+1);
 std::atomic<unsigned> done=0,failed=0;std::atomic<bool> begin=false;
 std::vector<std::thread> producers;
 for(unsigned t=0;t<4;++t)producers.emplace_back([&,t]{while(!begin.load(std::memory_order_acquire))std::this_thread::yield();
  for(unsigned n=t;n<512;n+=4){if(!journal.Publish(n+1,End()))++failed;}++done;});
 begin.store(true,std::memory_order_release);
 // One consumer; no concurrent Records writer or reader during drain.
 while(done.load()!=4){journal.Drain(*records);std::this_thread::yield();}
 for(auto& t:producers)t.join();journal.Drain(*records);
 CHECK(journal.Pending()==0&&journal.Recovered()+journal.Overflow()==512);
 CHECK(journal.Overflow()==failed.load()&&journal.Rejected()==0);
 unsigned finished=0;for(const auto& r:records->Records())finished+=r.finished;
 CHECK(finished==journal.Recovered());return 0;}
int MissingAuthority(){auto records=std::make_unique<ReloadFlowRecords>();ReloadDeferredCompletions journal;
 const auto id=records->Begin(Input());auto end=End();end.boundary.reset();
 CHECK(journal.Publish(id,end));journal.Drain(*records);
 CHECK(records->Records()[0].finished&&!records->Records()[0].identityRetained);
 CHECK(!ReloadRestoreMatched(records->Records()[0]));return 0;}
int Run(){if(ContendedEnd()||Rejection()||Overflow()||Parent()||Concurrent()||MissingAuthority())return 1;
 std::cout<<"PASS completion journal 6 groups\n";return 0;}

}
