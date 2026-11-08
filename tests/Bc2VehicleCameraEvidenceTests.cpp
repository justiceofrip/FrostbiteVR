#include "Test.h"
#include "Bc2VehicleCameraEvidence.h"
using namespace fvr::bc2;
namespace {
struct Row {
    VehicleCameraEvidenceKey key{};
    std::uint64_t ms=0;
    unsigned appliedMask=0;
    float headMotion=0;
    std::array<int,2> applied{};
};
Row Sample(unsigned frame,std::uint64_t ms){
    Row row;row.key={{1,2,3,4},frame,5,frame+100,1000000+frame};row.ms=ms;return row;
}
fvr::interaction::VehicleCameraSample Current(const VehicleCameraEvidenceKey& key){
    fvr::interaction::VehicleCameraSample current;current.live=true;current.owner=key.owner;current.space=key.space;return current;
}
int HeadMotionAndLongRide(){
    VehicleCameraEvidence<Row> journal;
    // Ten minutes of continuously changing head poses at 100Hz. Retained
    // samples still reach the late scene; head jitter cannot exhaust a prefix.
    for(unsigned n=0;n<=60000;++n){
        auto row=Sample(n+1,n*10);row.headMotion=float(n)*.01f;
        CHECK(journal.Observe(row)==(n%50==0));
        CHECK(journal.Size()<=16&&journal.At(0).key.frame==1);
    }
    CHECK(journal.Samples()==1201&&journal.Size()==16&&journal.Omitted()==1185);
    CHECK(journal.At(15).ms==600000&&journal.At(1).ms==593000);
    for(unsigned i=2;i<journal.Size();++i)CHECK(journal.At(i).ms-journal.At(i-1).ms==500);
    CHECK(journal.Incomplete()==1201&&journal.OmittedIncomplete()==1185&&journal.RetainedIncomplete()==16);
    return 0;
}
int OwnerAndSpaceBoundaries(){
    VehicleCameraEvidence<Row,4> journal;
    auto first=Sample(1,1000);CHECK(journal.Observe(first));
    auto head=Sample(2,1001);head.headMotion=10;CHECK(!journal.Observe(head));
    auto owner=Sample(3,1001);owner.key.owner.vehicle=9;CHECK(journal.Observe(owner));
    auto space=Sample(4,1001);space.key.owner=owner.key.owner;space.key.space=6;CHECK(journal.Observe(space));
    CHECK(journal.Size()==3&&journal.At(1).key==owner.key&&journal.At(2).key==space.key);
    auto next=space;next.key.frame=5;next.ms=1500;CHECK(!journal.Observe(next));
    next.ms=1501;CHECK(journal.Observe(next));
    next.ms=2001;next.key.frame=6;CHECK(journal.Observe(next));
    // The bounded window retains the first and latest rows; an expired
    // boundary is explicitly omitted rather than silently freezing sampling.
    CHECK(journal.At(0).key==first.key&&journal.At(1).key==space.key&&journal.At(3).key==next.key);
    CHECK(journal.Omitted()==1&&journal.OmittedIncomplete()==1);
    return 0;
}
int ExactEyesAndIncompleteEviction(){
    VehicleCameraEvidence<Row,4> journal;
    auto first=Sample(1,1000),second=Sample(2,1500);
    CHECK(journal.Observe(first)&&journal.Observe(second));
    const auto eye=[](int value){return [=](Row& row){row.applied[0]=value;};};
    // A delayed exact first-frame callback enriches its retained row, never
    // the latest plan. A duplicate cannot replace the original measurement.
    CHECK(journal.Enrich(first.key,Current(first.key),0,eye(11)));
    CHECK(!journal.Enrich(first.key,Current(first.key),0,eye(99))&&journal.At(0).applied[0]==11);
    CHECK(journal.At(1).appliedMask==0&&journal.DuplicateEyes()==1);
    for(unsigned field=0;field<9;++field){
        auto wrong=second.key;
        switch(field){case 0:++wrong.frame;break;case 1:++wrong.owner.actor;break;
        case 2:++wrong.owner.generation;break;case 3:++wrong.owner.vehicle;break;
        case 4:++wrong.owner.seat;break;case 5:++wrong.space;break;
        case 6:++wrong.tracking;break;case 7:++wrong.predictedNs;break;default:wrong={};break;}
        CHECK(!journal.Enrich(wrong,Current(second.key),0,eye(99)));
    }
    CHECK(journal.UnmatchedEyes()==9&&journal.At(1).appliedMask==0);
    CHECK(!journal.Enrich(second.key,Current(second.key),2,eye(99)));
    CHECK(journal.Enrich(first.key,Current(first.key),1,[](Row& row){row.applied[1]=12;}));
    CHECK(journal.Incomplete()==1&&journal.At(0).appliedMask==3);
    CHECK(journal.Enrich(second.key,Current(second.key),0,eye(21)));
    CHECK(journal.Observe(Sample(3,2000))&&journal.Observe(Sample(4,2500))&&journal.Observe(Sample(5,3000)));
    CHECK(journal.Samples()==5&&journal.Omitted()==1&&journal.Incomplete()==4);
    CHECK(journal.OmittedIncomplete()==1&&journal.RetainedIncomplete()==3);
    CHECK(!journal.Enrich(second.key,Current(second.key),1,[](Row& row){row.applied[1]=22;}));
    CHECK(journal.At(0).applied[0]==11&&journal.At(0).applied[1]==12);
    auto latest=Sample(5,3500);CHECK(!journal.Observe(latest));
    CHECK(journal.Incomplete()==4&&journal.OmittedIncomplete()==1);
    auto repeatedFirst=first;repeatedFirst.ms=4000;CHECK(!journal.Observe(repeatedFirst));
    return 0;
}
int CompletedEvictionAndClockEdges(){
    VehicleCameraEvidence<Row,2> journal;
    auto first=Sample(1,1000),second=Sample(2,1500);
    CHECK(!journal.Observe(Sample(0,1000))&&journal.Size()==0);
    CHECK(journal.Observe(first)&&journal.Observe(second));
    for(const auto& key:{first.key,second.key})for(unsigned eye=0;eye<2;++eye)
        CHECK(journal.Enrich(key,Current(key),eye,[](Row&){}));
    CHECK(journal.Incomplete()==0);
    CHECK(!journal.Observe(Sample(3,1499))&&!journal.Observe(Sample(3,1500))&&!journal.Observe(Sample(3,1999)));
    CHECK(journal.Observe(Sample(3,2000)));
    CHECK(journal.Omitted()==1&&journal.OmittedIncomplete()==0&&journal.Incomplete()==1&&journal.RetainedIncomplete()==1);
    CHECK(journal.At(0).key==first.key&&journal.At(1).key.frame==3);
    return 0;
}
int ChangedCurrentOwnerCannotCompleteOldPlan(){
    VehicleCameraEvidence<Row> journal;auto row=Sample(1,1000);CHECK(journal.Observe(row));
    CHECK(journal.Enrich(row.key,Current(row.key),0,[](Row& target){target.applied[0]=11;}));
    for(unsigned field=0;field<6;++field){
        auto changed=Current(row.key);
        switch(field){case 0:changed.live=false;break;case 1:++changed.owner.actor;break;
        case 2:++changed.owner.generation;break;case 3:++changed.owner.vehicle;break;
        case 4:++changed.owner.seat;break;default:++changed.space;break;}
        CHECK(!journal.Enrich(row.key,changed,1,[](Row& target){target.applied[1]=99;}));
        CHECK(journal.At(0).appliedMask==1&&journal.At(0).applied[1]==0&&journal.Incomplete()==1);
    }
    auto next=Sample(2,1001);++next.key.owner.generation;CHECK(journal.Observe(next));
    CHECK(!journal.Enrich(row.key,Current(next.key),1,[](Row& target){target.applied[1]=99;}));
    CHECK(journal.Enrich(next.key,Current(next.key),0,[](Row& target){target.applied[0]=21;}));
    CHECK(journal.Enrich(next.key,Current(next.key),1,[](Row& target){target.applied[1]=22;}));
    CHECK(journal.Incomplete()==1&&journal.At(0).appliedMask==1&&journal.At(1).appliedMask==3);
    return 0;
}
}
int main(){
    if(HeadMotionAndLongRide()||OwnerAndSpaceBoundaries()||ExactEyesAndIncompleteEviction()||CompletedEvictionAndClockEdges()||ChangedCurrentOwnerCannotCompleteOldPlan())return 1;
    std::puts("Vehicle camera evidence: bounded long-session retention, owner/space boundaries and exact eye joins passed");
}
