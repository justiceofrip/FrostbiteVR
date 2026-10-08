#include "Test.h"
#include "Bc2NativeCycleEvidence.h"
#include "Bc2ReloadFlowRuntime.h"
#include <bit>
#include <cstring>
#include <limits>
using namespace fvr::bc2;
namespace {
template<std::size_t N,class T>void Put(std::array<std::byte,N>& bytes,std::size_t at,T value){std::memcpy(bytes.data()+at,&value,sizeof(value));}
ReloadHoldIdentity Identity(){return {{0x10000,0x11000,0x12000,0x13000,11,12,13},{0x14000,0x15000,0x16000},0x17000,0x18000,0x19000};}
ReloadFlowRecord Update(unsigned branch=0){
    const auto i=Identity();ReloadFlowRecord r;r.id=100+branch;
    auto& a=r.entry;a.kind=ReloadFlowEvent::Update;a.update=r.id;a.nativeUpdate=r.id;a.nativeInvocation=r.id;
    a.thread=9;a.depth=1;a.caller=0x6e90b0;a.context=0x21000;a.nowNs=1000000;a.contextCopied=true;
    Put(a.copiedContext,0x18,.016f);Put(a.copiedContext,0x20,1.f);a.copiedContext[0x24]=std::byte{1};
    auto& b=a.boundary;b.owner=i.owner;b.snapshotSequence=20;b.firing=i.firing[branch];b.branch=std::uint8_t(branch);
    b.wrapperOffset=branch==0?0x3c:branch==1?0x40:0x10;b.current=7;b.previous=6;b.next=8;b.timer=.15f;b.loaded=4;b.reserve=20;
    if(branch==2){b.serverPlayer=i.serverPlayer;b.serverSoldier=i.serverSoldier;b.serverItem=i.serverItem;}
    r.exit.thread=a.thread;r.exit.nowNs=a.nowNs+100;r.exit.boundary=b;r.exit.copiedContext=a.copiedContext;r.exit.contextCopied=true;
    r.finished=true;r.identityRetained=true;return r;
}
ReloadFlowRecord Held(unsigned branch=0){auto r=Update(branch);r.exit.holdRequested=true;r.exit.hold={std::bit_cast<std::uint32_t>(.016f),0,true,true,false};return r;}
ReloadFlowRecord Shot(unsigned branch=0){auto r=Update(branch);r.entry.boundary.current=2;r.entry.boundary.previous=1;r.entry.boundary.next=2;r.entry.boundary.timer=0;r.entry.boundary.loaded=5;return r;}
ReloadFlowRecord Commit(unsigned branch=0,unsigned from=8,unsigned to=1){
    auto r=Update(branch);r.id=201+branch;r.entry.kind=ReloadFlowEvent::Commit;r.entry.depth=2;
    r.entry.parent=100;r.entry.nativeParent=100;r.entry.update=100;r.entry.nativeUpdate=100;r.entry.nativeInvocation=r.id;
    auto& before=r.entry.boundary;before.current=from;before.previous=from==8?7:8;before.next=to;before.timer=0;
    r.exit.boundary=before;r.exit.boundary->previous=from;r.exit.boundary->current=to;return r;
}
int ExactUpdateAndIdentity(){
    for(unsigned branch=0;branch<3;++branch){auto r=Update(branch);CHECK(ValidateCycleUpdate(r,Identity(),branch));
        CHECK(!ValidateCycleUpdate(r,Identity(),(branch+1)%3));
        Put(r.exit.copiedContext,0x14,0xabcdef01u);CHECK(ValidateCycleUpdate(r,Identity(),branch));}
    for(unsigned n=0;n<31;++n){auto r=Update();switch(n){
        case 0:r.id=0;break;case 1:r.entry.nativeInvocation++;break;case 2:r.finished=false;break;case 3:r.identityRetained=false;break;
        case 4:r.exit.boundary.reset();break;case 5:r.exit.thread++;break;case 6:r.entry.thread=r.exit.thread=0;break;
        case 7:r.entry.nowNs=0;break;case 8:r.exit.nowNs=r.entry.nowNs-1;break;case 9:r.entry.caller=0;break;
        case 10:r.entry.depth=2;break;case 11:r.entry.parent=1;break;case 12:r.entry.nativeParent=1;break;
        case 13:r.entry.update++;break;case 14:r.entry.nativeUpdate++;break;case 15:r.entry.kind=ReloadFlowEvent::Restore;break;
        case 16:r.entry.boundary.firing++;break;case 17:r.exit.boundary->owner.equipGeneration++;break;
        case 18:r.exit.boundary->snapshotSequence++;break;case 19:r.entry.boundary.snapshotSequence=0;break;
        case 20:r.entry.boundary.wrapperOffset=0x40;break;case 21:r.exit.boundary->serverItem=0x19000;break;
        case 22:r.exit.boundary->soldierFlags=1;break;case 23:r.entry.context=0;break;case 24:r.entry.contextCopied=false;break;
        case 25:r.exit.contextCopied=false;break;case 26:r.exit.boundary->timer=std::numeric_limits<float>::quiet_NaN();break;
        case 27:r.exit.boundary->timer=-.1f;break;case 28:r.exit.boundary->current=16;break;
        case 29:r.entry.boundary.loaded=-1;break;case 30:r.exit.boundary->flagsA8=8;break;}
        CHECK(!ValidateCycleUpdate(r,Identity(),0));}
    for(unsigned n=0;n<12;++n){auto i=Identity();switch(n){
        case 0:i.owner.player=0;break;case 1:i.owner.soldier=UINT32_MAX;break;case 2:i.owner.weak=0;break;
        case 3:i.owner.weapon=0;break;case 4:i.owner.actorGeneration=0;break;case 5:i.owner.equipGeneration=0;break;
        case 6:i.owner.space=0;break;case 7:i.serverPlayer=0;break;case 8:i.serverSoldier=0;break;case 9:i.serverItem=0;break;
        case 10:i.firing[2]=i.firing[0];break;case 11:i.firing[2]=0;break;}
        CHECK(!ValidateCycleUpdate(Update(),i,0));}
    CHECK(!ValidateCycleUpdate(Update(),Identity(),3));return 0;
}
int ContextBytesAndRanges(){
    for(unsigned offset=0;offset<0x30;++offset){if(offset>=0x14&&offset<0x18)continue;
        auto r=Update();r.exit.copiedContext[offset]^=std::byte{1};CHECK(!ValidateCycleUpdate(r,Identity(),0));}
    for(float bad:{0.f,-.01f,.1001f,std::numeric_limits<float>::quiet_NaN()}){auto r=Update();Put(r.entry.copiedContext,0x18,bad);r.exit.copiedContext=r.entry.copiedContext;CHECK(!ValidateCycleUpdate(r,Identity(),0));}
    for(float bad:{0.f,-1.f,4.01f,std::numeric_limits<float>::infinity()}){auto r=Update();Put(r.entry.copiedContext,0x20,bad);r.exit.copiedContext=r.entry.copiedContext;CHECK(!ValidateCycleUpdate(r,Identity(),0));}
    for(unsigned offset=0x24;offset<=0x28;++offset){auto r=Update();r.entry.copiedContext[offset]=std::byte{2};r.exit.copiedContext=r.entry.copiedContext;CHECK(!ValidateCycleUpdate(r,Identity(),0));}
    auto r=Update();Put(r.entry.copiedContext,0x2c,8u);r.exit.copiedContext=r.entry.copiedContext;CHECK(!ValidateCycleUpdate(r,Identity(),0));return 0;
}
int ShotMustBeOriginalPositiveManualCycle(){
    for(unsigned branch=0;branch<3;++branch){auto r=Shot(branch);CHECK(CycleShot(r,Identity(),branch));
        Put(r.entry.copiedContext,0x2c,1u);r.exit.copiedContext=r.entry.copiedContext;CHECK(CycleShot(r,Identity(),branch));
        for(auto path:{std::array<unsigned,3>{6,5,7},{7,6,7},{7,6,8},{8,7,1}}){r.exit.boundary->current=path[0];r.exit.boundary->previous=path[1];r.exit.boundary->next=path[2];CHECK(CycleShot(r,Identity(),branch));}}
    for(unsigned n=0;n<13;++n){auto r=Shot();switch(n){
        case 0:r.exit.boundary->loaded=5;break;case 1:r.exit.boundary->loaded=3;break;case 2:r.exit.boundary->reserve++;break;
        case 3:r.entry.boundary.loaded=1;r.exit.boundary->loaded=0;break;case 4:r.exit.boundary->current=2;r.exit.boundary->previous=1;r.exit.boundary->next=2;break;
        case 5:r.exit.holdRequested=true;break;case 6:r.exit.hold.applied=true;break;case 7:r.exit.hold.original=1;break;
        case 8:Put(r.entry.copiedContext,0x2c,2u);break;case 9:Put(r.entry.copiedContext,0x2c,4u);break;
        case 10:r.entry.copiedContext[0x26]=std::byte{1};break;case 11:r.entry.copiedContext[0x28]=std::byte{1};break;
        case 12:r.exit.boundary->previous=2;break;}
        r.exit.copiedContext=r.entry.copiedContext;CHECK(!CycleShot(r,Identity(),0));}
    return 0;
}
int EmptyShotIsSeparateExactPath(){
    for(unsigned branch=0;branch<3;++branch){auto r=Shot(branch);r.entry.boundary.loaded=1;
        r.exit.boundary->loaded=0;r.exit.boundary->current=6;r.exit.boundary->previous=5;r.exit.boundary->next=1;
        r.exit.boundary->timer=.7005f;r.exit.boundary->flagsA8=2;
        CHECK(CycleEmptyShot(r,Identity(),branch)&&!CycleShot(r,Identity(),branch));
        for(unsigned n=0;n<11;++n){auto bad=r;switch(n){
            case 0:bad.entry.boundary.loaded=2;break;case 1:bad.exit.boundary->loaded=1;break;
            case 2:bad.exit.boundary->reserve--;break;case 3:bad.exit.boundary->current=7;break;
            case 4:bad.exit.boundary->previous=6;break;case 5:bad.exit.boundary->next=7;break;
            case 6:bad.exit.boundary->timer=0;break;case 7:bad.exit.boundary->timer=1.001f;break;
            case 8:bad.exit.holdRequested=true;break;case 9:bad.entry.copiedContext[0x28]=std::byte{1};bad.exit.copiedContext=bad.entry.copiedContext;break;
            case 10:bad.entry.nativeUpdate++;break;}
            CHECK(!CycleEmptyShot(bad,Identity(),branch));}
    }return 0;
}
int EmptyHoldAndRestoreRequireTypedBoundary(){
    const NativeCycleHeldBoundary empty{6,5,1,.1f,1.f,true},positive{6,5,1,.1f,1.f};
    for(unsigned branch=0;branch<3;++branch){auto r=Held(branch);r.entry.boundary.current=6;
        r.entry.boundary.previous=5;r.entry.boundary.next=1;r.entry.boundary.loaded=0;
        r.entry.boundary.timer=.7005f;r.entry.boundary.flagsA8=2;r.exit.boundary=r.entry.boundary;
        CHECK(CycleHold(r,Identity(),branch,empty,true)&&!CycleHold(r,Identity(),branch,positive,true));
        r.entry.boundary.loaded=r.exit.boundary->loaded=1;CHECK(!CycleHold(r,Identity(),branch,empty));
    }return 0;
}
int HoldReceiptConservesExactState(){
    const NativeCycleHeldBoundary pump{7,6,8,.1f,.5f},boltCandidate{8,7,1,.1f,2.3f};
    for(unsigned branch=0;branch<3;++branch){auto r=Held(branch);CHECK(CycleHold(r,Identity(),branch,pump,true));
        r.entry.boundary.timer=.01f;r.exit.boundary=r.entry.boundary;CHECK(CycleHold(r,Identity(),branch,pump));CHECK(!CycleHold(r,Identity(),branch,pump,true));
        r.entry.boundary.current=8;r.entry.boundary.previous=7;r.entry.boundary.next=1;r.entry.boundary.timer=1.6f;r.exit.boundary=r.entry.boundary;
        CHECK(CycleHold(r,Identity(),branch,boltCandidate,true));CHECK(!CycleHold(r,Identity(),branch,pump));}
    for(unsigned n=0;n<22;++n){auto r=Held();switch(n){
        case 0:r.exit.holdRequested=false;break;case 1:r.exit.hold.applied=false;break;case 2:r.exit.hold.restored=false;break;
        case 3:r.exit.hold.unexpectedNativeWrite=true;break;case 4:r.exit.hold.original++;break;case 5:r.exit.hold.beforeRestore=1;break;
        case 6:r.entry.boundary.timer=r.exit.boundary->timer=0;break;case 7:r.entry.boundary.timer=r.exit.boundary->timer=.51f;break;
        case 8:r.exit.boundary->timer=.149f;break;case 9:r.exit.boundary->loaded--;break;case 10:r.exit.boundary->reserve++;break;
        case 11:r.exit.boundary->current=8;break;case 12:r.exit.boundary->previous=5;break;case 13:r.exit.boundary->next=1;break;
        case 14:r.exit.boundary->flagsA8=1;break;case 15:r.entry.boundary.loaded=r.exit.boundary->loaded=0;break;
        case 16:Put(r.entry.copiedContext,0x2c,1u);r.exit.copiedContext=r.entry.copiedContext;break;
        case 17:r.entry.copiedContext[0x27]=std::byte{1};r.exit.copiedContext=r.entry.copiedContext;break;
        case 18:Put(r.exit.copiedContext,0x18,0.f);break;case 19:r.entry.boundary.next=r.exit.boundary->next=7;break;
        case 20:r.exit.hold.restored=true;r.exit.hold.applied=false;break;
        case 21:Put(r.entry.copiedContext,0x2c,4u);r.exit.copiedContext=r.entry.copiedContext;break;}
        CHECK(!CycleHold(r,Identity(),0,pump));}
    for(auto bad:{NativeCycleHeldBoundary{7,6,7,.1f,.5f},{7,6,8,0,.5f},{7,6,8,.6f,.5f},{7,6,8,.1f,31.f},{16,6,8,.1f,.5f}})CHECK(!CycleHold(Held(),Identity(),0,bad));
    return 0;
}
int DirectCommitOnly(){
    for(unsigned branch=0;branch<3;++branch){for(auto pair:{std::array<unsigned,2>{7,8},{8,1},{1,2}}){const auto r=Commit(branch,pair[0],pair[1]);const auto e=CycleCommit(r,Identity(),branch,100);
        CHECK(e&&e->invocation==r.id&&e->update==100&&e->branch==branch&&e->beforeCurrent==pair[0]&&e->current==pair[1]&&e->previous==pair[0]&&e->next==pair[1]&&e->loaded==4&&e->reserve==20&&e->timer==0&&e->beginNs==r.entry.nowNs&&e->endNs==r.exit.nowNs);
        CHECK(!ValidateCycleUpdate(r,Identity(),branch));}}
    for(unsigned n=0;n<19;++n){auto r=Commit();switch(n){
        case 0:r.id=99;r.entry.nativeInvocation=99;break;case 1:r.entry.parent=101;break;case 2:r.entry.nativeParent=101;break;
        case 3:r.entry.update=101;break;case 4:r.entry.nativeUpdate=101;break;case 5:r.entry.depth=3;break;
        case 6:r.entry.nativeInvocation++;break;case 7:r.entry.kind=ReloadFlowEvent::Update;break;
        case 8:r.entry.boundary.next=r.entry.boundary.current;break;case 9:r.exit.boundary->previous=7;break;
        case 10:r.exit.boundary->current=2;break;case 11:r.exit.boundary->next=2;break;case 12:r.exit.boundary->timer=.01f;break;
        case 13:r.exit.boundary->loaded--;break;case 14:r.exit.boundary->reserve--;break;case 15:r.exit.boundary->flagsA8=1;break;
        case 16:Put(r.exit.copiedContext,0x14,123u);break;case 17:r.exit.holdRequested=true;break;
        case 18:Put(r.entry.copiedContext,0x2c,1u);r.exit.copiedContext=r.entry.copiedContext;break;}
        CHECK(!CycleCommit(r,Identity(),0,100));}
    CHECK(!CycleCommit(Commit(),Identity(),0,0));CHECK(!CycleCommit(Commit(),Identity(),0,99));return 0;
}
ReloadFlowRecord Restore(){
    auto r=Update();r.entry.kind=ReloadFlowEvent::Restore;r.entry.update=r.entry.nativeUpdate=0;
    r.entry.contextCopied=r.exit.contextCopied=false;r.entry.snapshotCopied=r.exit.snapshotCopied=true;
    r.exit.boundary->previous=7;r.exit.boundary->timer=.149f;
    Put(r.entry.copiedSnapshot,0,7u);Put(r.entry.copiedSnapshot,4,8u);Put(r.entry.copiedSnapshot,8,.149f);
    Put(r.entry.copiedSnapshot,0x18,4);Put(r.entry.copiedSnapshot,0x1c,20);r.exit.copiedSnapshot=r.entry.copiedSnapshot;return r;
}
int PredictionRestoreIsIndependentEvidence(){
    const NativeCycleHeldBoundary p{7,6,8,.1f,.5f};auto good=Restore();
    CHECK(CycleRestore(good,Identity(),0,p));good.entry.boundary.previous=7;CHECK(CycleRestore(good,Identity(),0,p));
    CHECK(!CycleHold(good,Identity(),0,p));CHECK(!CycleShot(good,Identity(),0));
    for(unsigned n=0;n<21;++n){auto r=Restore();switch(n){
        case 0:r.entry.nativeInvocation=0;break;case 1:r.entry.nativeInvocation++;break;
        case 2:r.entry.parent=r.entry.nativeParent=1;break;case 3:r.entry.nativeUpdate=1;break;
        case 4:r.entry.depth=2;break;case 5:r.entry.contextCopied=true;break;case 6:r.exit.contextCopied=true;break;
        case 7:r.exit.copiedSnapshot[0x20]=std::byte{1};break;case 8:r.entry.snapshotCopied=false;break;
        case 9:r.exit.boundary->previous=6;break;case 10:r.exit.boundary->timer=.148f;break;
        case 11:r.exit.boundary->reserve++;break;case 12:r.entry.boundary.previous=5;break;
        case 13:r.entry.boundary.current=6;break;case 14:r.exit.boundary->soldierFlags++;break;
        case 15:r.exit.boundary->snapshotSequence++;break;case 16:r.exit.boundary->flagsA8=1;break;
        case 17:r.exit.holdRequested=true;break;case 18:r.exit.boundary->owner.space++;break;
        case 19:r.entry.context=0;break;case 20:r.identityRetained=false;break;}
        CHECK(!CycleRestore(r,Identity(),0,p));}
    CHECK(!CycleRestore(Restore(),Identity(),2,p));return 0;
}
}
int main(){
    if(ExactUpdateAndIdentity()||ContextBytesAndRanges()||ShotMustBeOriginalPositiveManualCycle()||EmptyShotIsSeparateExactPath()||EmptyHoldAndRestoreRequireTypedBoundary()||HoldReceiptConservesExactState()||DirectCommitOnly()||PredictionRestoreIsIndependentEvidence())return 1;
    std::printf("Eight native cycle evidence groups passed: own Update, positive/empty shot, typed hold, direct Commit and client Restore provenance. Synthetic records; no native admission.\n");return 0;
}
