#include "fvr/runtime/SceneMilestones.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
using namespace fvr::runtime;
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}} while(false)
namespace {
constexpr auto any=[](const auto&){return true;};
constexpr auto stamp=[](){return SceneMilestoneStamp{100,200,3};};
void RebuildMustReturnAndValidate(){
    SceneMilestones<32,3> j;
    const auto rebuild=j.BeginRebuild({1,2,3,4,5},stamp);CHECK(rebuild);
    CHECK(!j.Begin(SceneMilestoneKind::WorldUpdate,{},any,stamp));
    j.Mark(rebuild,SceneMilestoneStage::OriginalReturned,{},stamp);
    CHECK(!j.Begin(SceneMilestoneKind::WorldUpdate,{},any,stamp));
    j.CompleteRebuild(rebuild,false,stamp);
    CHECK(!j.Begin(SceneMilestoneKind::WorldUpdate,{},any,stamp));
    const auto good=j.BeginRebuild({6,7,3,8,9},stamp);j.CompleteRebuild(good,true,stamp);
    CHECK(j.Begin(SceneMilestoneKind::WorldUpdate,{},any,stamp));
}
void ExactFirstInvocationAndSameAddressReuse(){
    SceneMilestones<64,3> j;
    const auto rebuild=j.BeginRebuild({1,2,3,4,5},stamp);j.CompleteRebuild(rebuild,true,stamp);
    const auto old=j.Begin(SceneMilestoneKind::GameplayUpdate,{123},any,stamp);CHECK(old);
    const auto nested=j.Begin(SceneMilestoneKind::GameplayUpdate,{123},any,stamp);CHECK(!nested);
    const auto count=j.Reserved();j.Mark(nested,SceneMilestoneStage::Exit,{},stamp);CHECK(j.Reserved()==count);
    // No Gather or old callback exit occurs before the same addresses rebuild.
    const auto next=j.BeginRebuild({1,2,3,4,6},stamp);j.CompleteRebuild(next,true,stamp);
    const auto fresh=j.Begin(SceneMilestoneKind::GameplayUpdate,{123},any,stamp);CHECK(fresh&&fresh.epoch!=old.epoch);
    j.Mark(fresh,SceneMilestoneStage::Exit,{},stamp);
    j.Mark(old,SceneMilestoneStage::Exit,{},stamp);
    SceneMilestoneRow row{};CHECK(j.Read(j.Reserved()-1,row));
    CHECK(row.epoch==old.epoch&&row.invocation==old.invocation&&row.scene.frame==5);
    CHECK(j.Read(j.Reserved()-2,row));CHECK(row.epoch==fresh.epoch&&row.invocation==fresh.invocation&&row.scene.frame==6);
}
void NegativeMatchingHasNoStampOrAdmission(){
    SceneMilestones<32,2> j;const auto r=j.BeginRebuild({1,2,3,4,5},stamp);j.CompleteRebuild(r,true,stamp);
    unsigned stamps=0;const auto counted=[&](){++stamps;return stamp();};
    CHECK(!j.Begin(SceneMilestoneKind::MainDraw,{},[](const auto& s){return s.request==999;},counted));CHECK(stamps==0);
    CHECK(j.Begin(SceneMilestoneKind::MainDraw,{},any,counted));CHECK(stamps==1);
    for(unsigned i=0;i<10000;++i)CHECK(!j.Begin(SceneMilestoneKind::MainDraw,{},any,counted));
    CHECK(stamps==1);
}
void InvalidOwnerTransitionsAreImmediateAndBounded(){
    SceneMilestones<64,2> j;SceneMilestoneContext c{};c.self=1;c.player=1;c.soldier=5;
    CHECK(!j.Owner(1,SceneOwnerOutcome::Unresolved,{},stamp));
    const auto valid=j.Owner(1,SceneOwnerOutcome::Valid,c,stamp);CHECK(valid);
    CHECK(!j.Owner(2,SceneOwnerOutcome::Unresolved,{},stamp));
    const auto invalid=j.Owner(1,SceneOwnerOutcome::Unresolved,c,stamp);CHECK(invalid);
    // Observe failure before calling the original; no new input/Gather required.
    SceneMilestoneRow row{};CHECK(j.Read(invalid.invocation-1,row));
    CHECK(row.context.soldier==5&&row.context.outcome==static_cast<unsigned>(SceneOwnerOutcome::Unresolved));
    CHECK(!j.Owner(1,SceneOwnerOutcome::Unresolved,c,stamp));
    CHECK(j.Owner(1,SceneOwnerOutcome::WrongThread,c,stamp));
    CHECK(j.Owner(1,SceneOwnerOutcome::Valid,c,stamp));
    c.self=2;c.player=2;CHECK(j.Owner(2,SceneOwnerOutcome::Valid,c,stamp));
    CHECK(!j.Owner(1,SceneOwnerOutcome::Unresolved,{},stamp));
}
void FullJournalRetainsEarlierEvidence(){
    SceneMilestones<4,2> j;
    const auto r=j.BeginRebuild({1,2,3,4,5},stamp);j.CompleteRebuild(r,true,stamp);
    const auto callback=j.Begin(SceneMilestoneKind::MainDraw,{},any,stamp);CHECK(callback);
    j.Mark(callback,SceneMilestoneStage::OriginalReturned,{},stamp);
    for(unsigned i=0;i<100;++i)j.Mark(callback,SceneMilestoneStage::Exit,{},stamp);
    CHECK(j.Reserved()==4&&j.Overflow()==100);
    SceneMilestoneRow row{};CHECK(j.Read(0,row)&&row.id==1&&row.kind==SceneMilestoneKind::Rebuild);
    CHECK(j.Read(3,row)&&row.stage==SceneMilestoneStage::OriginalReturned);
    CHECK(!j.Read(4,row));
}
void EpochOverflowNeverReusesFirstFlags(){
    SceneMilestones<32,1> j;const auto r=j.BeginRebuild({1,2,3,4,5},stamp);j.CompleteRebuild(r,true,stamp);
    CHECK(j.Begin(SceneMilestoneKind::MainDraw,{},any,stamp));
    CHECK(!j.BeginRebuild({1,2,3,4,6},stamp));CHECK(j.EpochOverflow()==1);
    CHECK(!j.Begin(SceneMilestoneKind::EyeDraw,{},any,stamp));
}
void ConcurrentFirstCallbackHasOneMatchedCompletion(){
    auto j=std::make_unique<SceneMilestones<>>();const auto r=j->BeginRebuild({1,2,3,4,5},stamp);j->CompleteRebuild(r,true,stamp);
    std::atomic<bool> start=false;std::atomic<unsigned> winners=0;std::vector<std::thread> threads;
    for(unsigned i=0;i<24;++i)threads.emplace_back([&,i]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        const auto t=j->Begin(SceneMilestoneKind::MainDraw,{i+10},any,stamp);
        if(t){++winners;j->Mark(t,SceneMilestoneStage::OriginalReturned,{i+10},stamp);j->Mark(t,SceneMilestoneStage::Exit,{i+10},stamp);}});
    start.store(true,std::memory_order_release);for(auto& t:threads)t.join();CHECK(winners==1&&j->Reserved()==5);
    SceneMilestoneRow begin{},returned{},end{};CHECK(j->Read(2,begin)&&j->Read(3,returned)&&j->Read(4,end));
    CHECK(begin.invocation==end.invocation&&returned.invocation==end.invocation&&begin.context.self==end.context.self);
}
void UnpublishedEarlierSlotDoesNotHideLaterEvidence(){
    SceneMilestones<32,2> j;const auto r=j.BeginRebuild({1,2,3,4,5},stamp);j.CompleteRebuild(r,true,stamp);
    std::atomic<bool> inside=false,release=false;
    std::thread paused([&]{j.Begin(SceneMilestoneKind::WorldUpdate,{99},any,[&]{inside.store(true,std::memory_order_release);
        while(!release.load(std::memory_order_acquire))std::this_thread::yield();return stamp();});});
    while(!inside.load(std::memory_order_acquire))std::this_thread::yield();
    CHECK(j.Begin(SceneMilestoneKind::GameplayUpdate,{100},any,stamp));SceneMilestoneRow row{};
    CHECK(!j.Read(2,row));CHECK(j.Read(3,row)&&row.context.self==100);
    release.store(true,std::memory_order_release);paused.join();CHECK(j.Read(2,row)&&row.context.self==99);
    CHECK(j.Read(3,row)&&row.context.self==100);
}
}
int main(){RebuildMustReturnAndValidate();ExactFirstInvocationAndSameAddressReuse();NegativeMatchingHasNoStampOrAdmission();
    InvalidOwnerTransitionsAreImmediateAndBounded();FullJournalRetainsEarlierEvidence();EpochOverflowNeverReusesFirstFlags();
    ConcurrentFirstCallbackHasOneMatchedCompletion();UnpublishedEarlierSlotDoesNotHideLaterEvidence();
    std::cout<<"SceneMilestones: 8 groups passed\n";}
