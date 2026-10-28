#include "Test.h"
#include "Bc2Rig.h"
#include "Bc2RigPaletteCopy.h"
#include "Bc2RigPublicationCommit.h"
#include "Bc2RigPoseHandoff.h"
#include "Bc2RigPublication.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <functional>
#include <iostream>

#include <cstring>
#include <limits>
using namespace fvr;
namespace rig_input_handoff_regression {
using namespace fvr::bc2;
using Tracking=fvr::bc2::rigPublication::Tracking;
void Check(bool good){if(!good)throw std::runtime_error("immutable complete rig input regression");}
RigPaletteCopyState State(const Tracking& value,bool valid=true){
    return {{value.soldier,value.weak,value.weapon,value.ownerGeneration,value.equipmentGeneration,value.input.spaceGeneration},
        value.input.generation,value.deadline,valid};
}
void LatestInputSurvivesScratchContention(){
    Tracking first;first.soldier=0x10000;first.weak=0x11000;first.weapon=0x12000;
    first.ownerGeneration=4;first.equipmentGeneration=7;first.input.spaceGeneration=2;
    first.input.generation=19;first.deadline=1000;first.nativeEquipment.data=0x14000;
    first.supportHolding=true;first.supportToken=10;first.magazine.inputEvidence.sequence=19;
    std::atomic<std::shared_ptr<const Tracking>> publication;
    std::atomic<std::shared_ptr<const RigPaletteCopyState>> guard;
    PublishRigInputAfterGuard(publication,std::make_shared<const Tracking>(first),[&]{guard.store(std::make_shared<const RigPaletteCopyState>(State(first)));});
    const auto held=ReadRigInputForPose(publication,[](std::int64_t deadline){return 100<deadline;});
    Check(held&&held->input.generation==19);
    std::mutex poseMutex;std::unique_lock scratch(poseMutex);
    bool contended=false,complete=false;
    std::thread producer([&]{
        // The old producer's try-lock path would have dropped this packet.
        contended=!poseMutex.try_lock();if(!contended)poseMutex.unlock();
        Tracking next=first;next.input.generation=20;next.deadline=1100;
        next.weapon=0x22000;next.equipmentGeneration=8;next.nativeEquipment.data=0x24000;
        next.supportToken=11;next.magazine.inputEvidence.sequence=20;
        // Exercise the exact production publication operation. Its guard must
        // advance before render input, even while unrelated scratch is owned.
        PublishRigInputAfterGuard(publication,std::make_shared<const Tracking>(next),[&]{
            Check(publication.load()==held);
            guard.store(std::make_shared<const RigPaletteCopyState>(State(next)),std::memory_order_release);
        });
        const auto now=ReadRigInputForPose(publication,[](std::int64_t deadline){return 101<deadline;});
        complete=now&&now->weapon==0x22000&&now->equipmentGeneration==8&&now->nativeEquipment.data==0x24000&&
            now->input.generation==20&&now->supportHolding&&now->supportToken==11&&now->magazine.inputEvidence.sequence==20;
        next.supportToken=99; // Mutable producer scratch cannot rewrite the publication.
        complete=complete&&now->supportToken==11;
    });producer.join();
    Check(contended&&complete&&held->weapon==0x12000&&held->deadline==1000&&held->supportToken==10);
    Check(!RigPaletteCopyFresh(State(*held),{*guard.load(),102}));
    // Invalid latest data remains visible during scratch ownership; it must not
    // silently fall back to an earlier valid owner's packet or renew its lease.
    Tracking invalid=*publication.load();invalid.deadline=0;invalid.input.focused=false;
    PublishRigInputAfterGuard(publication,std::make_shared<const Tracking>(invalid),[&]{guard.store(std::make_shared<const RigPaletteCopyState>(State(invalid,false)));});
    Check(!ReadRigInputForPose(publication,[](std::int64_t deadline){return 102<deadline;}));
    Check(!RigPaletteCopyFresh(State(*held),{*guard.load(),102}));
    PublishRigInputAfterGuard(publication,held,[&]{guard.store(std::make_shared<const RigPaletteCopyState>(State(*held)));});
    Check(!ReadRigInputForPose(publication,[](std::int64_t deadline){return 1000<deadline;}));
}
void ProducerContentionCounterexampleAndCommit(){
    // Only the retired producer/shared-lock admission is modeled here. Both
    // paths use the actual production root-commit and hand-handoff helpers.
    // This does not claim native root-to-render-job ordering was established.
    struct Pose{RigPaletteCopyState state;unsigned root;};
    Tracking old;old.soldier=0x10000;old.weak=0x11000;old.weapon=0x12000;old.ownerGeneration=4;
    old.equipmentGeneration=7;old.input.spaceGeneration=2;old.input.generation=19;old.deadline=1000;
    Tracking next=old;next.input.generation=20;next.deadline=1100;
    std::atomic<std::shared_ptr<const Tracking>> input;
    std::atomic<std::shared_ptr<const RigPaletteCopyState>> guard;
    PublishRigInputAfterGuard(input,std::make_shared<const Tracking>(next),[&]{guard.store(std::make_shared<const RigPaletteCopyState>(State(next)));});
    const auto previous=std::make_shared<const Pose>(Pose{State(old),50});
    std::atomic<std::shared_ptr<const Pose>> poses(previous);
    const auto observe=[&]{return RigPaletteCopyObservation{*guard.load(),100};};
    const auto describe=[](const Pose& value){return value.state;};
    unsigned root=10; // Current native getter produced stock output first.
    std::mutex historicalFrameMutex;std::unique_lock producer(historicalFrameMutex);
    bool skipped=false,oldSplit=false,newMatched=false;
    std::thread render([&]{
        std::unique_lock retiredAdmission(historicalFrameMutex,std::try_to_lock);
        skipped=!retiredAdmission.owns_lock();
        const auto acceptedOld=ReadRigPoseForCopy(poses,describe,observe);
        oldSplit=skipped&&acceptedOld==previous&&root!=acceptedOld->root;
        const auto current=ReadRigInputForPose(input,[](std::int64_t deadline){return 100<deadline;});
        std::mutex poseOnlyScratch;std::unique_lock scratch(poseOnlyScratch,std::try_to_lock);
        if(current&&scratch.owns_lock()){
            const auto prepared=std::make_shared<const Pose>(Pose{State(*current),60});
            const bool committed=TryCommitRigPublication(prepared->state,observe,[]{return true;},[&]()noexcept{
                root=prepared->root;poses.store(prepared,std::memory_order_release);
            });
            const auto accepted=ReadRigPoseForCopy(poses,describe,observe);
            newMatched=committed&&accepted==prepared&&root==accepted->root&&accepted->state.input==20;
        }
    });render.join();
    Check(skipped&&oldSplit&&newMatched);
    std::cout<<"Rig input producer contention: legacy native-root/private-hand split reproduced; immutable input + real commit/handoff matched\n";
}
void Run(){LatestInputSurvivesScratchContention();ProducerContentionCounterexampleAndCommit();}
}
namespace publication_commit_regression {
using namespace fvr::bc2;
void Check(bool good){if(!good)throw std::runtime_error("rig publication commit regression");}
struct Transaction {
    RigPaletteCopyState captured{{0x10000,0x11000,0x12000,4,7,2},19,1000,true};
    std::atomic<std::shared_ptr<const RigPaletteCopyState>> guard;
    std::array<unsigned,4> outputs{11,12,13,14}; // getter, palette, shot, attachment
    unsigned validations=0,commits=0;std::int64_t now=100;
    Transaction():guard(std::make_shared<const RigPaletteCopyState>(captured)){}
    void Publish(const RigPaletteCopyState& value){guard.store(std::make_shared<const RigPaletteCopyState>(value));}
    template<class Validate> bool Run(Validate&& validate){
        return TryCommitRigPublication(captured,[&]{return RigPaletteCopyObservation{*guard.load(),now};},
            [&]{++validations;return validate();},[&]()noexcept {outputs={21,22,23,24};++commits;});
    }
    void Unchanged(){Check((outputs==std::array<unsigned,4>{11,12,13,14})&&commits==0);}
};
void PreparedFailureLeavesEveryOutputNative(){
    // The old adapter wrote getter/shot before discovering invalid visibility.
    // A complete solved pose is only preparation; final rejection writes none.
    Transaction p;Check(!p.Run([]{return false;}));p.Unchanged();Check(p.validations==1);
    Transaction rejected;auto invalid=rejected.captured;invalid.valid=false;rejected.Publish(invalid);
    Check(!rejected.Run([]{return true;}));rejected.Unchanged();Check(!rejected.validations);
}
void AsyncInvalidationDoesNotWaitForPoseMutex(){
    for(unsigned transition=0;transition<8;++transition){
        Transaction p;std::mutex poseMutex;std::unique_lock poseLock(poseMutex);bool mutexWasHeld=false;
        Check(!p.Run([&]{
            std::thread producer([&]{
                mutexWasHeld=!poseMutex.try_lock();if(!mutexWasHeld)poseMutex.unlock();
                auto next=p.captured;
                if(transition==0)next.valid=false; // tracking/focus loss, even without authored bindings
                else if(transition==1)++next.owner.space; // recenter
                else if(transition==2)++next.owner.actorGeneration; // checkpoint/respawn
                else if(transition==3)++next.owner.equipmentGeneration; // same pointer, new item
                else if(transition==4)++next.owner.weapon;
                else if(transition==5)++next.owner.soldier;
                else if(transition==6)++next.owner.weak;
                else --next.input;
                p.Publish(next); // Mirrors the producer's independent atomic guard.
            });producer.join();return true;
        }));p.Unchanged();Check(mutexWasHeld);
    }
}
void LaterSameOwnerPacketDoesNotRenewOriginalLease(){
    Transaction good;
    Check(good.Run([&]{auto next=good.captured;++next.input;next.deadline=2000;good.Publish(next);good.now=110;return true;}));
    Check(good.commits==1&&(good.outputs==std::array<unsigned,4>{21,22,23,24}));
    Transaction expired;
    Check(!expired.Run([&]{auto next=expired.captured;++next.input;next.deadline=2000;expired.Publish(next);
        expired.now=expired.captured.deadline;return true;}));expired.Unchanged();
    Transaction clock;
    Check(!clock.Run([&]{clock.now=99;return true;}));clock.Unchanged();
}
void NativeConflictAndOptionalBindingFailureCannotPartiallyCommit(){
    for(bool hasAuthoredBinding:{false,true}){
        Transaction outputConflict;auto changedNative=outputConflict.outputs;changedNative[0]=99;
        outputConflict.outputs=changedNative;
        Check(!outputConflict.Run([&]{return outputConflict.outputs[0]==11;}));
        Check(outputConflict.outputs==changedNative&&!outputConflict.commits);
        Transaction sourceConflict;Check(!sourceConflict.Run([]{return false;}));sourceConflict.Unchanged();
        // Every class takes this gate; an absent optional binding never bypasses it.
        Transaction lost;auto next=lost.captured;next.valid=false;lost.Publish(next);unsigned optionalChecks=0;
        Check(!lost.Run([&]{if(hasAuthoredBinding)++optionalChecks;return true;}));
        lost.Unchanged();Check(optionalChecks==0);
    }
}
void Run(){PreparedFailureLeavesEveryOutputNative();AsyncInvalidationDoesNotWaitForPoseMutex();
    LaterSameOwnerPacketDoesNotRenewOriginalLease();NativeConflictAndOptionalBindingFailureCannotPartiallyCommit();}
}
namespace palette_copy_regression {
using namespace fvr::bc2;
void Check(bool good){if(!good)throw std::runtime_error("palette copy lifecycle regression");}
struct Pair {
    std::vector<RigPaletteBone> native{8},before{8},posed{8},scratch;
    RigPaletteCopyState captured{{0x10000,0x11000,0x12000,4,7,2},19,1000,true};
    RigPaletteCopyState current=captured;
    std::array<std::vector<std::byte>,2> output;
    unsigned originals=0,privateMask=0,reads=0;bool readable=true;
    std::int64_t observedNow=100;std::function<void()> duringRead;
    Pair(){
        for(unsigned b=0;b<native.size();++b)for(unsigned p=0;p<64;++p)
            native[b][p]=std::byte((b*17+p)&255);
        before=native;posed=before;posed[3][48]=std::byte{0x7f};
    }
    std::vector<std::byte> Pack(const std::vector<RigPaletteBone>& src){
        // Verified packer layout: twelve source scalars per bone, transposed.
        std::vector<std::byte> result(src.size()*48);
        for(unsigned b=0;b<src.size();++b)for(unsigned col=0;col<3;++col)
            for(unsigned row=0;row<4;++row)
                std::memcpy(result.data()+b*48+col*16+row*4,src[b].data()+row*16+col*4,4);
        return result;
    }
    void Copy(unsigned copyIndex,std::int64_t now=100){
        const auto originalNative=native;observedNow=now;
        const bool use=RigPaletteCurrentAtCopy(captured,0x20000,0x20000,
            unsigned(native.size()),before,posed,scratch,[&](std::uintptr_t at,void* out,std::size_t n){
                ++reads;Check(at==0x20000&&n==native.size()*64);
                if(!readable)return false;std::memcpy(out,native.data(),n);
                if(duringRead)duringRead();return true;},
            [&]{return RigPaletteCopyObservation{current,observedNow};});
        ++originals;output[copyIndex]=Pack(use?posed:native);
        if(use)privateMask|=1u<<copyIndex;
        Check(native==originalNative); // No restoration/writeback to native animation.
    }
};
void SuccessfulPair(){Pair p;p.Copy(0);++p.current.input;p.Copy(1,110);
    Check(p.originals==2&&p.privateMask==3&&p.output[0]==p.Pack(p.posed)&&p.output[1]==p.output[0]);}
void ProducerMutationBetweenCopies(){Pair p;p.Copy(0);p.native[3][48]=std::byte{0x55};p.Copy(1);
    Check(p.originals==2&&p.privateMask==1&&p.output[1]==p.Pack(p.native)&&p.output[1]!=p.output[0]);}
void LifetimeTransitionBetweenCopies(){
    for(unsigned n=0;n<10;++n){Pair p;p.Copy(0);
        if(n==0)++p.current.owner.actorGeneration;else if(n==1)++p.current.owner.space;
        else if(n==2)++p.current.owner.equipmentGeneration;else if(n==3)++p.current.owner.soldier;
        else if(n==4)++p.current.owner.weak;else if(n==5)++p.current.owner.weapon;
        else if(n==6)p.current.valid=false;else if(n==7)--p.current.input;
        else if(n==8)p.current.deadline=100;else p.captured.deadline=100;
        p.Copy(1);Check(p.originals==2&&p.privateMask==1&&p.output[1]==p.Pack(p.native));
    }
}
void AdmissionChangesBeforeFirstCopy(){
    Pair p;p.native[3][48]=std::byte{0x55};p.Copy(0);p.Copy(1);
    Check(p.originals==2&&p.privateMask==0&&p.output[0]==p.Pack(p.native)&&p.output[1]==p.output[0]);
    Pair failed;failed.readable=false;failed.Copy(0);failed.Copy(1);
    Check(failed.originals==2&&failed.privateMask==0&&failed.reads==2);
}
void GuardChangesDuringRead(){
    for(unsigned fault=0;fault<5;++fault){Pair p;p.Copy(0);
        p.duringRead=[&]{if(fault==0)++p.current.owner.actorGeneration;
            else if(fault==1)++p.current.owner.space;
            else if(fault==2)p.current.valid=false;
            else if(fault==3)p.observedNow=p.captured.deadline;
            else --p.current.input;};
        p.Copy(1);Check(p.originals==2&&p.privateMask==1&&p.output[1]==p.Pack(p.native));
    }
    Pair newer;newer.Copy(0);newer.duringRead=[&]{++newer.current.input;++newer.observedNow;};
    newer.Copy(1);Check(newer.privateMask==3&&newer.originals==2);
}
void SourceIdentityAndBounds(){Pair p;unsigned reads=0;
    const auto read=[&](std::uintptr_t,void*,std::size_t){++reads;return true;};
    auto observe=[&]{return RigPaletteCopyObservation{p.current,p.observedNow};};
    Check(!RigPaletteCurrentAtCopy(p.captured,0x20000,0x30000,8,p.before,p.posed,p.scratch,read,observe));
    Check(!RigPaletteCurrentAtCopy(p.captured,0x20000,0x20000,9,p.before,p.posed,p.scratch,read,observe));
    p.observedNow=0;
    Check(!RigPaletteCurrentAtCopy(p.captured,0x20000,0x20000,8,p.before,p.posed,p.scratch,read,observe));
    Check(reads==0);
}
void Run(){SuccessfulPair();ProducerMutationBetweenCopies();LifetimeTransitionBetweenCopies();
    AdmissionChangesBeforeFirstCopy();GuardChangesDuringRead();SourceIdentityAndBounds();}

}
namespace rig_handoff_regression {
using namespace fvr::bc2;
void Check(bool good){if(!good)throw std::runtime_error("immutable rig handoff regression");}
struct Pose {RigPaletteCopyState state;std::vector<RigPaletteBone> before,posed;};
void ReadWhileProducerOwnsMutex(){
    palette_copy_regression::Pair p;
    auto prepared=std::make_shared<const Pose>(Pose{p.captured,p.before,p.posed});
    std::atomic<std::shared_ptr<const Pose>> publication(prepared);
    std::mutex frameMutex;std::unique_lock producer(frameMutex);
    bool contended=false,admitted=false,copied=false;
    std::thread consumer([&]{
        contended=!frameMutex.try_lock();if(!contended)frameMutex.unlock();
        const auto pose=ReadRigPoseForCopy(publication,[](const Pose& x){return x.state;},
            [&]{return RigPaletteCopyObservation{p.current,100};});
        admitted=pose==prepared;
        if(pose)copied=RigPaletteCurrentAtCopy(pose->state,0x20000,0x20000,8,
            pose->before,pose->posed,p.scratch,[&](std::uintptr_t,void* out,std::size_t n){
                std::memcpy(out,p.native.data(),n);return true;},
            [&]{return RigPaletteCopyObservation{p.current,100};});
    });consumer.join();
    Check(contended&&admitted&&copied&&p.native==p.before);
}
void OwnershipAndCopyInvalidation(){
    for(unsigned n=0;n<9;++n){
        palette_copy_regression::Pair p;
        const auto prepared=std::make_shared<const Pose>(Pose{p.captured,p.before,p.posed});
        std::atomic<std::shared_ptr<const Pose>> publication(prepared);
        const auto describe=[](const Pose& x){return x.state;};
        const auto observe=[&]{return RigPaletteCopyObservation{p.current,p.observedNow};};
        const auto held=ReadRigPoseForCopy(publication,describe,observe);Check(held==prepared);
        if(n==0)++p.current.owner.space;
        else if(n==1)++p.current.owner.actorGeneration;
        else if(n==2)++p.current.owner.equipmentGeneration;
        else if(n==3)++p.current.owner.weapon;
        else if(n==4)++p.current.owner.soldier;
        else if(n==5)++p.current.owner.weak;
        else if(n==6)p.current.valid=false;
        else if(n==7)--p.current.input;
        else {++p.current.input;p.current.deadline=2000;p.observedNow=held->state.deadline;}
        Check(!ReadRigPoseForCopy(publication,describe,observe));
        // An already acquired shared_ptr cannot bypass the existing native-copy
        // guard, even when a new input has a longer deadline.
        unsigned reads=0;
        Check(!RigPaletteCurrentAtCopy(held->state,0x20000,0x20000,8,
            held->before,held->posed,p.scratch,[&](std::uintptr_t,void*,std::size_t){++reads;return true;},observe));
        Check(reads==0);
    }
}
void ClearAndProducerMutationRemainRejected(){
    palette_copy_regression::Pair p;
    const auto prepared=std::make_shared<const Pose>(Pose{p.captured,p.before,p.posed});
    std::atomic<std::shared_ptr<const Pose>> publication(prepared);
    const auto describe=[](const Pose& x){return x.state;};
    const auto observe=[&]{return RigPaletteCopyObservation{p.current,100};};
    const auto held=ReadRigPoseForCopy(publication,describe,observe);Check(held==prepared);
    p.native[3][48]=std::byte{0x55};
    Check(!RigPaletteCurrentAtCopy(held->state,0x20000,0x20000,8,held->before,held->posed,p.scratch,
        [&](std::uintptr_t,void* out,std::size_t n){std::memcpy(out,p.native.data(),n);return true;},observe));
    publication.store({},std::memory_order_release);
    Check(!ReadRigPoseForCopy(publication,describe,observe));
    Check(held->posed==p.posed); // Safe storage retention grants no native write.
}
void PreviewDoesNotBorrowLongerPacketDeadline(){
    Check(RigSightPreviewForCopy(9,9,true,150,149));
    Check(!RigSightPreviewForCopy(9,9,true,150,150));
    Check(!RigSightPreviewForCopy(9,9,true,150,151));
    Check(!RigSightPreviewForCopy(9,10,true,150,100));
    Check(!RigSightPreviewForCopy(9,0,true,150,100));
    Check(!RigSightPreviewForCopy(9,9,false,150,100));
    Check(!RigSightPreviewForCopy(0,0,true,150,100));
    Check(!RigSightPreviewForCopy(9,9,true,150,0));
}
void Run(){ReadWhileProducerOwnsMutex();OwnershipAndCopyInvalidation();
    ClearAndProducerMutationRemainRejected();PreviewDoesNotBorrowLongerPacketDeadline();}
}

struct Memory {
 std::vector<std::byte> data=std::vector<std::byte>(0x10000);
 std::vector<std::pair<std::uint32_t,std::size_t>> reads;
 std::vector<std::pair<std::uint32_t,std::uint32_t>> unreadable;
 std::function<void(Memory&,std::uint32_t,std::size_t)> afterRead;
 static bool Read(void* p,std::uint32_t at,void* out,std::size_t n){auto& m=*static_cast<Memory*>(p);m.reads.emplace_back(at,n);
  if(n>65536||at<0x10000||at-0x10000>m.data.size()||n>m.data.size()-(at-0x10000))return false;
  for(const auto& [begin,end]:m.unreadable)if(at<end&&std::uint64_t(at)+n>begin)return false;
  std::memcpy(out,m.data.data()+at-0x10000,n);if(m.afterRead)m.afterRead(m,at,n);return true;}
 void Put(unsigned at,unsigned value){std::memcpy(data.data()+at-0x10000,&value,4);}
 void Init(){
  Put(0x11000,0x10004);data[0x475]=std::byte{1};Put(0x103b4,0x12000);Put(0x103b8,0x13000);Put(0x13004,0x10000);
  Put(0x12050,0x14000);Put(0x12054,0x15000);Put(0x12938,7);Put(0x14010,8);Put(0x15028,0x14000);Put(0x1501c,8);
  Put(0x15018,0x16000);Put(0x16000,8);Put(0x16004,0x18000);Put(0x14018,0x17000);Put(0x1401c,0x17020);
  Put(0x1400c,0x17100);Put(0x15010,0x17100);Put(0x16008,0x1a000);Put(0x1231c,0x1b000);Put(0x17104,8);Put(0x17108,0x17200);
  const char* names[]={"root","LeftArm","LeftForeArm","LeftHand","RightArm","RightForeArm","RightHand","jntWpn_1"};
  const int parents[]={-1,0,1,2,0,4,5,0};
  for(unsigned n=0;n<8;++n){Put(0x17000+n*4,0x19000+n*64);Put(0x17200+n*0x98,0x19000+n*64);Put(0x17204+n*0x98,unsigned(parents[n]));
   std::memcpy(data.data()+0x9000+n*64,names[n],std::strlen(names[n])+1);
   math::Matrix4 m{};for(unsigned a=0;a<4;++a)m.values[a][a]=1;std::memcpy(data.data()+0x7200+n*0x98+0x4c,&m,64);m.values[3][0]=float(n);m.values[3][2]=2;m.values[3][3]=0;std::memcpy(data.data()+0x8000+n*64,&m,64);std::memcpy(data.data()+0xa000+n*64,&m,64);m.values[3][1]=.125f;std::memcpy(data.data()+0xb000+n*64,&m,64);
  }
 }
};
namespace rig_read_batch_regression {
bool Same(const bc2::RigSnapshot& a,const bc2::RigSnapshot& b){
 const auto matrices=[](const auto& x,const auto& y){if(x.size()!=y.size())return false;for(unsigned n=0;n<x.size();++n)if(x[n].values!=y[n].values)return false;return true;};
 return a.identity==b.identity&&a.names==b.names&&a.parents==b.parents&&a.nativeWorld==b.nativeWorld&&a.nativeEvaluated==b.nativeEvaluated&&
  matrices(a.world,b.world)&&matrices(a.inverseBind,b.inverseBind)&&matrices(a.evaluatedWorld,b.evaluatedWorld)&&a.skinConsistencyError==b.skinConsistencyError&&
  a.left.shoulder==b.left.shoulder&&a.left.elbow==b.left.elbow&&a.left.wrist==b.left.wrist&&
  a.right.shoulder==b.right.shoulder&&a.right.elbow==b.right.elbow&&a.right.wrist==b.right.wrist&&a.weaponBone==b.weaponBone&&a.nativeHiddenLeaves==b.nativeHiddenLeaves;
}
void RootName(Memory& m,unsigned at,const std::string& name){
 m.Put(0x17000,at);m.Put(0x17200,at);std::memcpy(m.data.data()+at-0x10000,name.c_str(),name.size()+1);
}
// Keep the real reader's complete relationship graph and the original eight
// role bones, adding unrelated bones to exercise the native 1024-bone bound.
void Expand(Memory& m,unsigned count){
 const char* roles[]={"root","LeftArm","LeftForeArm","LeftHand","RightArm","RightForeArm","RightHand","jntWpn_1"};
 const int parents[]={-1,0,1,2,0,4,5,0};m.data.resize(0x90000);
 for(auto at:{0x14010u,0x1501cu,0x16000u,0x17104u})m.Put(at,count);
 m.Put(0x14018,0x18000);m.Put(0x1401c,0x18000+count*4);m.Put(0x17108,0x20000);
 m.Put(0x16004,0x50000);m.Put(0x16008,0x60000);m.Put(0x1231c,0x70000);
 for(unsigned n=0;n<count;++n){const auto name=n<8?std::string(roles[n]):"aux"+std::to_string(n);
  m.Put(0x18000+n*4,0x80000+n*64);m.Put(0x20000+n*0x98,0x80000+n*64);m.Put(0x20004+n*0x98,unsigned(n<8?parents[n]:0));
  std::memcpy(m.data.data()+0x70000+n*64,name.c_str(),name.size()+1);
  math::Matrix4 matrix{};for(unsigned a=0;a<4;++a)matrix.values[a][a]=1;
  std::memcpy(m.data.data()+0x10000+n*0x98+0x4c,&matrix,64);matrix.values[3][0]=float(n);matrix.values[3][2]=2;matrix.values[3][3]=0;
  for(unsigned offset:{0x40000u,0x50000u,0x60000u})std::memcpy(m.data.data()+offset+n*64,&matrix,64);
 }
}
int Run(){
 Memory m;m.Init();bc2::RigMemory reader{&m,Memory::Read};const auto bytes=m.data;
 const auto baseline=bc2::ReadFirstPersonRig(reader,0x10000,0x11000);CHECK(baseline);
 // This fixture used 147 callbacks with the bytewise reader. The fresh block
 // reader needs 50; keep headroom for added validation, not per-character I/O.
 CHECK(m.reads.size()<=60);CHECK(m.data==bytes);
 CHECK((baseline->names==std::vector<std::string>{"root","LeftArm","LeftForeArm","LeftHand","RightArm","RightForeArm","RightHand","jntWpn_1"}));
 // Unused record bytes were never required by the old reader. A bulk-read
 // failure there must fall back without changing any snapshot/palette byte.
 m.unreadable={{0x17208,0x1720c}};m.reads.clear();
 const auto fallback=bc2::ReadFirstPersonRig(reader,0x10000,0x11000);CHECK(fallback&&Same(*baseline,*fallback));CHECK(m.data==bytes);
 m.unreadable={{0x17200+0x4c,0x17200+0x50}};CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 // Page split, inaccessible bytes after the NUL within a page, and a string
 // ending at the final readable byte all remain valid. No name read crosses
 // a 4 KiB page, even when the name itself crosses one.
 for(unsigned scenario=0;scenario<3;++scenario){Memory edge;edge.Init();
  const auto at=scenario==0?0x1cffcu:scenario==1?0x1f004u:0x1fffbu;RootName(edge,at,"root");
  if(scenario==0)edge.unreadable={{0x1d001,0x1e000}};
  if(scenario==1)edge.unreadable={{0x1f009,0x1f044}};
  bc2::RigMemory edgeReader{&edge,Memory::Read};const auto rig=bc2::ReadFirstPersonRig(edgeReader,0x10000,0x11000);
  CHECK(rig&&Same(*baseline,*rig));
  for(const auto& [address,size]:edge.reads)if(address>=0x1c000)CHECK((address&4095u)+size<=4096);
 }
 for(unsigned fault=0;fault<4;++fault){Memory bad;bad.Init();RootName(bad,0x1f000,std::string(63,'a'));
  if(fault==0)bad.data[0xf000]=std::byte{0}; // Empty.
  if(fault==1)bad.data[0xf000+63]=std::byte{'a'}; // No NUL in 64 bytes.
  if(fault==2)bad.data[0xf000+2]=std::byte{31}; // Non-printable.
  if(fault==3)bad.unreadable={{0x1f002,0x1f003}}; // Unreadable before the NUL.
  bc2::RigMemory badReader{&bad,Memory::Read};CHECK(!bc2::ReadFirstPersonRig(badReader,0x10000,0x11000));
 }
 Memory longest;longest.Init();RootName(longest,0x1f000,std::string(63,'a'));
 bc2::RigMemory longestReader{&longest,Memory::Read};const auto longRig=bc2::ReadFirstPersonRig(longestReader,0x10000,0x11000);
 CHECK(longRig&&longRig->names[0]==std::string(63,'a'));
 // No metadata or palette survives between calls: mutate names, inverse bind,
 // and all live palettes at identical addresses, keeping skin = bind * world.
 Memory fresh;fresh.Init();fresh.data[0x299c]=std::byte{1};bc2::RigMemory freshReader{&fresh,Memory::Read};
 const auto before=bc2::ReadFirstPersonRig(freshReader,0x10000,0x11000);CHECK(before);
 RootName(fresh,0x19000,"ROOT");
 const auto putFloat=[&](unsigned at,float value){std::memcpy(fresh.data.data()+at-0x10000,&value,4);};
 putFloat(0x17200+0x4c+48,.5f);putFloat(0x1a000+48,.5f);putFloat(0x1b000+48,.5f);
 for(auto at:{0x18000u,0x1a000u,0x1b000u})putFloat(at+3*64+48,33.f);
 const auto changed=bc2::ReadFirstPersonRig(freshReader,0x10000,0x11000);CHECK(changed);
 CHECK(changed->names[0]=="ROOT"&&Near(changed->inverseBind[0].values[3][0],.5f));
 CHECK(Near(changed->world[3].values[3][0],33.f)&&Near(changed->evaluatedWorld[3].values[3][0],33.f));
 CHECK(changed->nativeWorld!=before->nativeWorld&&changed->nativeEvaluated!=before->nativeEvaluated);
 // Owner/palette replacement after the first dynamic read must still fail the
 // final identity bracket, even though all metadata was already copied.
 for(unsigned fault=0;fault<5;++fault){Memory moving;moving.Init();bool mutated=false;
  moving.afterRead=[&](Memory& target,unsigned at,std::size_t){if(at!=0x18000||mutated)return;mutated=true;
   if(fault==0)target.Put(0x11000,0x10008);else if(fault==1)target.Put(0x12050,0x14004);
   else if(fault==2)target.Put(0x16004,0x18040);else if(fault==3)target.data[0x299c]=std::byte{1};
   else target.data[0x475]=std::byte{0};};
  bc2::RigMemory movingReader{&moving,Memory::Read};CHECK(!bc2::ReadFirstPersonRig(movingReader,0x10000,0x11000));CHECK(mutated);
 }
 // 147 is the observed checkpoint434 skeleton size. 1024 exercises multiple
 // bone-table blocks and the exact 64 KiB native palette callback limit.
 for(unsigned count:{147u,1024u}){Memory large;large.Init();Expand(large,count);large.data[0x299c]=std::byte{1};
  const auto original=large.data;bc2::RigMemory largeReader{&large,Memory::Read};
  const auto rig=bc2::ReadFirstPersonRig(largeReader,0x10000,0x11000);CHECK(rig&&rig->identity.count==count&&rig->names.size()==count);
  CHECK(Near(rig->evaluatedWorld.back().values[3][0],float(count-1))&&large.data==original);
  CHECK(large.reads.size()<=count+50);bool fullBlock=false;
  for(const auto& [address,size]:large.reads){CHECK(size<=65536);fullBlock=fullBlock||size==65536;}
  CHECK(count!=1024||fullBlock);
 }
 return 0;
}
}
namespace hidden_weapon_leaf_regression {
math::Matrix4 Identity(){math::Matrix4 value{};for(unsigned i=0;i<4;++i)value.values[i][i]=1;return value;}
void Matrix(Memory& m,unsigned address,const math::Matrix4& value){std::memcpy(m.data.data()+address-0x10000,&value,64);}
void Name(Memory& m,unsigned n,const char* name){
 const auto address=0x19000+n*64;m.Put(0x17000+n*4,address);m.Put(0x17200+n*0x98,address);
 std::memset(m.data.data()+address-0x10000,0,64);std::memcpy(m.data.data()+address-0x10000,name,std::strlen(name)+1);
}
void Hidden(Memory& m,unsigned n){
 auto bind=Identity();bind.values[3][1]=-1.1f;Matrix(m,0x17200+n*0x98+0x4c,bind);
 auto hidden=Identity();hidden.values[0][0]=hidden.values[1][1]=hidden.values[2][2]=.0001f;
 unsigned palette=0;
 for(unsigned base:{0x18000u,0x1a000u,0x1b000u}){
  // Native hidden palettes have different finite translations. Padding is
  // opaque SIMD metadata, including non-float bits, and must survive exactly.
  hidden.values[3][0]=100.f+float(n+palette);hidden.values[3][1]=200.f+float(palette);hidden.values[3][2]=300.f;
  Matrix(m,base+n*64,hidden);
  for(unsigned row=0;row<4;++row)m.Put(base+n*64+row*16+12,0x7fc00042u+row+palette*4);
  ++palette;
 }
}
Memory Fixture(){
 Memory m;m.Init();m.data[0x299c]=std::byte{1};
 for(auto at:{0x14010u,0x1501cu,0x16000u,0x17104u})m.Put(at,10);
 m.Put(0x1401c,0x17028);
 for(unsigned n:{8u,9u}){Name(m,n,n==8?"jntWpn_7":"jntWpn_6");m.Put(0x17204+n*0x98,7);Hidden(m,n);}
 return m;
}
std::optional<bc2::RigSnapshot> Read(Memory& m){return bc2::ReadFirstPersonRig({&m,Memory::Read},0x10000,0x11000);}
int Run(){
 auto memory=Fixture();const auto original=memory.data;const auto rig=Read(memory);
 CHECK(rig&&(rig->nativeHiddenLeaves==std::vector<std::uint32_t>{8,9})&&memory.data==original);
 CHECK(rig->skinConsistencyError<.002f);
 for(unsigned n:{8u,9u}){
  CHECK(!std::memcmp(rig->nativeWorld[n].data(),original.data()+0x8000+n*64,64));
  CHECK(!std::memcmp(rig->nativeEvaluated[n].data(),original.data()+0xb000+n*64,64));
 }
 // Exercise the actual downstream weapon retarget and palette planner. Hidden
 // leaves stay byte-identical while the weapon root and ordinary hand move.
 auto target=rig->evaluatedWorld[7];target.values[3][0]+=.25f;
 CHECK(!interaction::RetargetRigSubtree(rig->parents,rig->evaluatedWorld,7,target));
 auto writes=interaction::RetargetRigSubtree(rig->parents,rig->evaluatedWorld,7,target,rig->nativeHiddenLeaves);
 CHECK(writes&&writes->size()==1&&writes->front().index==7);
 auto wrist=rig->evaluatedWorld[3];wrist.values[3][1]+=.5f;writes->push_back({3,wrist});
 const auto plan=bc2::BuildRigPosePlan(*rig,*writes);CHECK(plan&&plan->edits.size()==2&&memory.data==original);
 auto privatePalette=rig->nativeEvaluated;
 for(const auto& edit:plan->edits){CHECK(edit.index!=8&&edit.index!=9);privatePalette[edit.index]=edit.after;}
 CHECK(privatePalette[8]==rig->nativeEvaluated[8]&&privatePalette[9]==rig->nativeEvaluated[9]);
 for(unsigned n:{8u,9u}){
  const std::array<interaction::BoneWrite,1> attachment{{{n,Identity()}}};
  CHECK(!bc2::BuildRigPosePlan(*rig,attachment));
  auto mixed=*writes;mixed.push_back(attachment[0]);CHECK(!bc2::BuildRigPosePlan(*rig,mixed));
 }
 // Weapon names and indices are not feature authority. Differently numbered
 // hidden leaves may coexist; ordinary native (non-custom-IK) palettes work too.
 auto renamed=Fixture();Name(renamed,8,"jntWpn_31");Name(renamed,9,"jntWpn_27");renamed.data[0x299c]=std::byte{0};
 const auto ordinary=Read(renamed);CHECK(ordinary&&(ordinary->nativeHiddenLeaves==std::vector<std::uint32_t>{8,9}));
 auto unrelatedName=Fixture();Name(unrelatedName,9,"inactive_geometry");CHECK(!Read(unrelatedName));
 // Preserve strict topology: unrelated/arm leaves, a hidden ancestor, and a
 // nested weapon descendant do not acquire this exception.
 for(unsigned parent:{0u,3u,6u,8u}){auto bad=Fixture();bad.Put(0x17204+9*0x98,parent);CHECK(!Read(bad));}
 auto underArm=Fixture();underArm.Put(0x17204+7*0x98,6);CHECK(!Read(underArm));
 // A role can be a direct weapon child in an otherwise valid ancestry graph.
 // The validated role/arm branch must still reject collapse, not become hidden.
 auto role=Fixture();role.Put(0x17204+7*0x98,2);role.Put(0x17204+3*0x98,7);Hidden(role,3);CHECK(!Read(role));
 for(unsigned n=0;n<8;++n){auto bad=Fixture();Hidden(bad,n);CHECK(!Read(bad));}
 // Each of the three native palettes independently needs the exact observed
 // collapse basis and finite consumed values. Arbitrary singular poses fail.
 for(unsigned base:{0x18000u,0x1a000u,0x1b000u}){
  for(unsigned fault=0;fault<4;++fault){auto bad=Fixture();auto matrix=Identity();
   if(fault==0)Matrix(bad,base+9*64,matrix); // Only this palette becomes visible.
   if(fault==1)bad.Put(base+9*64,0); // Zero scale is not the native hide signature.
   if(fault==2){const float wrong=.001f;std::memcpy(bad.data.data()+base-0x10000+9*64+4,&wrong,4);}
   if(fault==3)bad.Put(base+9*64+48,0x7fc00000); // Consumed translation, unlike padding.
   CHECK(!Read(bad));
  }
 }
 auto badBind=Fixture();badBind.Put(0x17200+9*0x98+0x4c,0);CHECK(!Read(badBind));
 // A real visible sibling still obeys bind*world; hidden exceptions must not
 // hide a separate stale or mixed native palette elsewhere in the rig.
 auto mixedPalette=Fixture();mixedPalette.Put(0x1a000+7*64+48,0);CHECK(!Read(mixedPalette));
 // Visibility is derived fresh each read, never sticky state or unhide authority.
 auto visible=Fixture();const auto identity=Identity();Matrix(visible,0x17200+9*0x98+0x4c,identity);
 for(unsigned base:{0x18000u,0x1a000u,0x1b000u})Matrix(visible,base+9*64,identity);
 const auto next=Read(visible);CHECK(next&&(next->nativeHiddenLeaves==std::vector<std::uint32_t>{8}));
 const std::array<interaction::BoneWrite,1> visibleWrite{{{9,identity}}};CHECK(bc2::BuildRigPosePlan(*next,visibleWrite));
 Hidden(visible,9);const auto hiddenAgain=Read(visible);CHECK(hiddenAgain&&(hiddenAgain->nativeHiddenLeaves==std::vector<std::uint32_t>{8,9}));
 CHECK(!bc2::BuildRigPosePlan(*hiddenAgain,visibleWrite));
 return 0;
}
}
int main(){rig_input_handoff_regression::Run();rig_handoff_regression::Run();publication_commit_regression::Run();palette_copy_regression::Run();CHECK(rig_read_batch_regression::Run()==0);CHECK(hidden_weapon_leaf_regression::Run()==0);Memory memory;memory.Init();bc2::RigMemory reader{&memory,Memory::Read};const auto original=memory.data;
 auto rig=bc2::ReadFirstPersonRig(reader,0x10000,0x11000);CHECK(rig&&rig->left.wrist==3&&rig->right.wrist==6&&rig->weaponBone==7);CHECK(rig->world[3].values[3][2]==-2);CHECK(memory.data==original);
 // Owner, both count sources, backlink, bone-name correspondence, parent cycles,
 // duplicate names, missing role and malformed affine poses must be rejected.
 for(auto edit:{std::pair{0x11000u,0x10008u},std::pair{0x15028u,0x14004u},std::pair{0x16000u,9u},std::pair{0x14010u,1025u},std::pair{0x17104u,9u},std::pair{0x17200u,0x19040u},std::pair{0x17204u,0u},std::pair{0x12938u,20u}}){memory.data=original;memory.Put(edit.first,edit.second);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));}
 memory.data=original;memory.data[0x9040]=std::byte{'X'};CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=original;memory.Put(0x18000,0x7fc00000);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=original;memory.Put(0x17200+2*0x98,0x19040);memory.Put(0x17008,0x19040);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=original;memory.data[0x299c]=std::byte{1};rig=bc2::ReadFirstPersonRig(reader,0x10000,0x11000);CHECK(rig&&rig->identity.nativeIk&&rig->identity.evaluatedMatrices==0x1b000&&Near(rig->evaluatedWorld[3].values[3][1],.125f));
 // Skin palette edits preserve padding and native animation; malformed or
 // duplicate writes never produce a partial usable plan.
 auto moved=rig->evaluatedWorld[3];moved.values[3][0]+=.25f;
 std::array<interaction::BoneWrite,1> writes{interaction::BoneWrite{3,moved}};
 // Non-float padding is legal and must be copied byte-for-byte.
 for(unsigned row=0;row<4;++row){const unsigned tag=0x7fc00042u+row;std::memcpy(rig->nativeEvaluated[3].data()+row*16+12,&tag,4);}
 const auto before=memory.data;auto plan=bc2::BuildRigPosePlan(*rig,writes);CHECK(plan&&plan->identity==rig->identity&&plan->edits.size()==1&&plan->edits[0].address==0x1b0c0&&memory.data==before);
 for(unsigned row=0;row<4;++row)CHECK(!std::memcmp(plan->edits[0].before.data()+row*16+12,plan->edits[0].after.data()+row*16+12,4));
 float x=0;std::memcpy(&x,plan->edits[0].after.data()+48,4);CHECK(Near(x,3.25f));
 const std::array<interaction::BoneWrite,2> duplicates{writes[0],writes[0]};CHECK(!bc2::BuildRigPosePlan(*rig,duplicates));writes[0].index=8;CHECK(!bc2::BuildRigPosePlan(*rig,writes));
 memory.data=original;memory.Put(0x1a038,0);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 // Observed hidden SPAS leaf is preserved without weakening arm/root checks.
 memory.data=original;
 for(auto at:{0x14010u,0x1501cu,0x16000u,0x17104u})memory.Put(at,9);
 memory.Put(0x1401c,0x17024);memory.Put(0x17020,0x19200);
 memory.Put(0x17200+8*0x98,0x19200);memory.Put(0x17204+8*0x98,7);
 std::memcpy(memory.data.data()+0x9200,"jntWpn_7",9);
 math::Matrix4 bind{};for(unsigned a=0;a<4;++a)bind.values[a][a]=1;
 std::memcpy(memory.data.data()+0x7200+8*0x98+0x4c,&bind,64);
 auto hidden=bind;hidden.values[0][0]=hidden.values[1][1]=hidden.values[2][2]=.0001f;
 for(unsigned offset:{0x8000u,0xa000u,0xb000u})std::memcpy(memory.data.data()+offset+8*64,&hidden,64);
 auto hiddenRig=bc2::ReadFirstPersonRig(reader,0x10000,0x11000);
 CHECK(hiddenRig&&hiddenRig->nativeHiddenLeaves==std::vector<std::uint32_t>{8});
 std::array<interaction::BoneWrite,1> hiddenWrite{interaction::BoneWrite{8,bind}};
 CHECK(!bc2::BuildRigPosePlan(*hiddenRig,hiddenWrite));
 const auto hiddenMemory=memory.data;
 memory.Put(0x17204+8*0x98,6);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=hiddenMemory;memory.data[0x9200]=std::byte{'X'};CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=hiddenMemory;memory.Put(0x18000+8*64,0x7fc00000);CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));
 memory.data=original;memory.data[0x475]=std::byte{0};CHECK(!bc2::ReadFirstPersonRig(reader,0x10000,0x11000));return 0;
}
