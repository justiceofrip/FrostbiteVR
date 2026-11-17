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
VehicleCameraPulseInput PulseInput(unsigned frame,std::uint64_t ms,float throttle=0){
    const auto row=Sample(frame,ms);VehicleCameraPulseInput input;
    input.owner=row.key.owner;input.space=row.key.space;input.seatGeneration=7;
    input.gather=frame+200;input.input=row.key.tracking;input.ms=ms;
    input.observedNs=std::int64_t(ms)*1000000;input.deadlineNs=input.observedNs+100000000;
    input.mask=3;input.axes[0]=throttle;return input;
}
std::int64_t CameraNow(const Row& row){return std::int64_t(row.ms)*1000000+1;}
int ShortPulseSurvivesLongNeutralPeriod(){
    VehicleCameraPulseEvidence<Row> pulses;auto pre=Sample(1,1000);
    CHECK(pulses.Input(PulseInput(1,1000))&&pulses.Observe(pre,CameraNow(pre),true));
    for(unsigned eye=0;eye<2;++eye)CHECK(pulses.Enrich(pre.key,Current(pre.key),eye,[=](Row& r){r.applied[eye]=10+eye;}));
    CHECK(pulses.Input(PulseInput(2,1020,.5f)));
    auto active=Sample(2,1030);CHECK(pulses.Due(active.key,active.ms,CameraNow(active)));
    CHECK(pulses.Observe(active,CameraNow(active),false));
    CHECK(pulses.Input(PulseInput(3,1100,.5f)));
    auto extra=Sample(3,1110);CHECK(!pulses.Due(extra.key,extra.ms,CameraNow(extra)));
    CHECK(!pulses.Observe(extra,CameraNow(extra),false));
    CHECK(pulses.Input(PulseInput(4,1220)));
    auto post=Sample(4,1230);CHECK(pulses.Observe(post,CameraNow(post),false));
    CHECK(pulses.Size()==1&&pulses.At(0).sampleMask==7&&pulses.At(0).end==VehicleCameraPulseEnd::Neutral);
    CHECK(pulses.At(0).began.input==102&&pulses.At(0).lastActive.input==103&&pulses.At(0).neutral.input==104);
    CHECK(pulses.At(0).samples[0].appliedMask==3&&pulses.At(0).samples[0].applied[1]==11);
    for(unsigned frame=5;frame<100;++frame){const auto ms=1500+500*(frame-5);auto row=Sample(frame,ms);
        CHECK(pulses.Input(PulseInput(frame,ms))&&pulses.Observe(row,CameraNow(row),true));}
    CHECK(pulses.Size()==1&&pulses.Samples()==1&&pulses.Omitted()==0);
    CHECK(pulses.At(0).samples[0].ms==1000&&pulses.At(0).samples[1].ms==1030&&pulses.At(0).samples[2].ms==1230);
    return 0;
}
int PulseMissingPhasesAndBoundedRollover(){
    VehicleCameraPulseEvidence<Row,2> pulses;
    CHECK(pulses.Input(PulseInput(1,1000,.5f))&&pulses.Input(PulseInput(2,1100)));
    auto post=Sample(2,1110);CHECK(pulses.Observe(post,CameraNow(post),false));
    CHECK(pulses.At(0).sampleMask==4); // No baseline or rendered active frame was observed.
    CHECK(pulses.Input(PulseInput(3,1200,.5f))&&pulses.Input(PulseInput(4,1300)));
    CHECK(pulses.Input(PulseInput(5,1400,.5f))); // A new pulse cannot manufacture the missing post-frame.
    CHECK(pulses.Size()==2&&pulses.Samples()==3&&pulses.Omitted()==1);
    CHECK(pulses.At(0).sequence==2&&pulses.At(0).end==VehicleCameraPulseEnd::Replaced&&pulses.At(0).sampleMask==0);
    CHECK(pulses.At(1).sequence==3&&pulses.At(1).end==VehicleCameraPulseEnd::Open);
    CHECK(pulses.Input(PulseInput(6,3501,.5f)));
    CHECK(pulses.TimedOut()==1&&pulses.At(1).end==VehicleCameraPulseEnd::TimedOut);
    auto late=Sample(6,3510);CHECK(!pulses.Due(late.key,late.ms,CameraNow(late)));
    return 0;
}
int PulseSourceBoundariesAndExactEyes(){
    for(unsigned change=0;change<4;++change){
        VehicleCameraPulseEvidence<Row> pulses;auto pre=Sample(1,1000),active=Sample(2,1030);
        CHECK(pulses.Input(PulseInput(1,1000))&&pulses.Observe(pre,CameraNow(pre),true));
        CHECK(pulses.Input(PulseInput(2,1020,.5f))&&pulses.Observe(active,CameraNow(active),false));
        CHECK(pulses.Enrich(active.key,Current(active.key),0,[](Row& r){r.applied[0]=21;}));
        CHECK(pulses.Enrich(active.key,Current(active.key),0,[](Row& r){r.applied[0]=99;}));
        CHECK(pulses.DuplicateEyes()==1&&pulses.At(0).samples[1].applied[0]==21);
        auto changed=PulseInput(3,1040,.5f);
        switch(change){case 0:++changed.owner.generation;break;case 1:++changed.space;break;
            case 2:++changed.seatGeneration;break;default:changed.mask=1;break;}
        CHECK(pulses.Input(changed));
        CHECK(pulses.Size()==2&&pulses.At(0).end==VehicleCameraPulseEnd::Interrupted&&pulses.At(1).sampleMask==0);
        auto current=Current(active.key);current.owner=changed.owner;current.space=changed.space;
        CHECK(!pulses.Enrich(active.key,current,1,[](Row& r){r.applied[1]=99;}));
        CHECK(pulses.At(0).samples[1].appliedMask==1);
    }
    VehicleCameraPulseEvidence<Row> pulses;CHECK(pulses.Input(PulseInput(1,1000,.5f)));
    pulses.Invalidate();CHECK(pulses.At(0).end==VehicleCameraPulseEnd::Interrupted);
    auto row=Sample(1,1010);CHECK(!pulses.Due(row.key,row.ms,CameraNow(row)));
    return 0;
}
int PulseClocksAndOldBaseline(){
    VehicleCameraPulseEvidence<Row> pulses;auto pre=Sample(1,1000);
    CHECK(pulses.Input(PulseInput(1,1000))&&pulses.Observe(pre,CameraNow(pre),true));
    CHECK(pulses.Input(PulseInput(2,1501,.5f))&&pulses.At(0).sampleMask==0);
    auto active=Sample(2,1510);const auto now=CameraNow(active);
    CHECK(!pulses.Due(active.key,active.ms,1500999999)&&!pulses.Due(active.key,active.ms,1601000000));
    auto old=active;old.key.tracking=101;CHECK(!pulses.Observe(old,now,false));
    CHECK(pulses.Observe(active,now,false));
    auto altered=PulseInput(2,1510,.5f); // Same transport generation cannot renew its source clock.
    CHECK(!pulses.Input(altered)&&pulses.Size()==1&&pulses.At(0).end==VehicleCameraPulseEnd::Interrupted);
    auto invalid=PulseInput(3,1520,.5f);invalid.deadlineNs=invalid.observedNs+100000001;
    CHECK(!pulses.Input(invalid)&&pulses.InvalidInputs()==2);
    return 0;
}
int PreOnsetCategoriesAndFrozenCandidate(){
    VehicleCameraPulseEvidence<Row> empty;
    CHECK(empty.Input(PulseInput(1,1000,.5f)));
    CHECK(empty.At(0).pre.reason==VehicleCameraPreReason::NoCandidate&&!empty.At(0).pre.candidateSeen);
    for(unsigned kind=0;kind<3;++kind){
        VehicleCameraPulseEvidence<Row> pulses;auto pre=Sample(1,kind==0?1050:1000);
        CHECK(pulses.Input(PulseInput(1,1000))&&pulses.Observe(pre,CameraNow(pre),true));
        CHECK(pulses.Enrich(pre.key,Current(pre.key),0,[](Row&){}));
        const auto onset=kind==0?1020:kind==1?1500:1501;
        CHECK(pulses.Input(PulseInput(2,onset,.5f)));
        const auto frozen=pulses.At(0).pre;
        CHECK(frozen.reason==(kind==0?VehicleCameraPreReason::Future:kind==1?VehicleCameraPreReason::Accepted:VehicleCameraPreReason::TooOld));
        CHECK(frozen.candidateSeen&&frozen.candidateAvailable&&frozen.key==pre.key&&frozen.ms==pre.ms&&frozen.appliedMask==1);
        CHECK(frozen.ageMs==(kind==0?0:kind==1?500:501));
        CHECK(frozen.latestBaseline.observed&&frozen.latestBaseline.failures==0);
        CHECK((pulses.At(0).sampleMask&1)==(kind==1?1:0));
        auto next=Sample(2,onset+1);pulses.Observe(next,CameraNow(next),true);
        CHECK(pulses.At(0).pre.latestBaseline.key==frozen.latestBaseline.key&&pulses.At(0).pre.appliedMask==1);
    }
    return 0;
}
int PreSourceAndInvalidationReasons(){
    for(auto reason:{VehicleCameraInvalidationReason::ViewChanged,VehicleCameraInvalidationReason::ViewUnavailable,
        VehicleCameraInvalidationReason::InputLockDrop,VehicleCameraInvalidationReason::ReadbackUnavailable}){
        VehicleCameraPulseEvidence<Row> pulses;auto pre=Sample(1,1000);const auto source=PulseInput(1,1000);
        CHECK(pulses.Input(source)&&pulses.Observe(pre,CameraNow(pre),true));pulses.Invalidate(reason);
        CHECK(pulses.Input(PulseInput(2,1020,.5f)));
        const auto& onset=pulses.At(0).pre;
        CHECK(onset.reason==VehicleCameraPreReason::Source&&onset.candidateSeen&&!onset.candidateAvailable);
        CHECK(onset.key==pre.key&&onset.invalidation.reason==reason&&onset.invalidation.sequence==1&&onset.invalidation.source.input==source.input);
    }
    VehicleCameraPulseEvidence<Row> changed;auto pre=Sample(1,1000);
    CHECK(changed.Input(PulseInput(1,1000))&&changed.Observe(pre,CameraNow(pre),true));
    auto other=PulseInput(2,1020,.5f);++other.owner.generation;CHECK(changed.Input(other));
    CHECK(changed.At(0).pre.reason==VehicleCameraPreReason::Source&&changed.At(0).pre.invalidation.reason==VehicleCameraInvalidationReason::SourceChanged);
    CHECK(changed.At(0).pre.invalidation.source.owner==other.owner);
    return 0;
}
int BaselineReadyFailureBits(){
    const unsigned bits[]={CameraNoFrame,CameraOwner,CameraSpace,CameraTracking,CameraTime,CameraBeforeSource,CameraExpiredSource};
    for(unsigned kind=0;kind<7;++kind){
        VehicleCameraPulseEvidence<Row> pulses;CHECK(pulses.Input(PulseInput(1,1000)));
        auto row=Sample(1,1000);auto now=CameraNow(row);
        switch(kind){case 0:row.key.frame=0;break;case 1:++row.key.owner.actor;break;case 2:++row.key.space;break;
        case 3:--row.key.tracking;break;case 4:--row.ms;break;case 5:now=999999999;break;default:now=1100000000;break;}
        CHECK(!pulses.Observe(row,now,true));CHECK(pulses.Input(PulseInput(2,1020,.5f)));
        const auto& ready=pulses.At(0).pre.latestBaseline;
        CHECK(ready.observed&&ready.failures==bits[kind]&&ready.key==row.key&&ready.ms==row.ms&&ready.nowNs==now);
        CHECK(ready.input.input==101&&ready.input.observedNs==1000000000&&ready.input.deadlineNs==1100000000);
        CHECK(pulses.At(0).pre.reason==VehicleCameraPreReason::NoCandidate);
    }
    VehicleCameraPulseEvidence<Row> missing;auto row=Sample(1,1000);
    CHECK(!missing.Observe(row,CameraNow(row),true)&&missing.Input(PulseInput(2,1020,.5f)));
    CHECK(missing.At(0).pre.latestBaseline.failures&CameraNoInput);
    VehicleCameraPulseEvidence<Row> overdue;CHECK(overdue.Input(PulseInput(1,1000,.5f)));
    row=Sample(1,3001);CHECK(!overdue.Observe(row,1000000001,true));
    CHECK(overdue.Input(PulseInput(2,1001))&&overdue.Input(PulseInput(3,1002,.5f)));
    CHECK(overdue.At(1).pre.latestBaseline.failures==CameraPulseTime);
    return 0;
}
}
int main(){
    if(HeadMotionAndLongRide()||OwnerAndSpaceBoundaries()||ExactEyesAndIncompleteEviction()||CompletedEvictionAndClockEdges()||ChangedCurrentOwnerCannotCompleteOldPlan())return 1;
    if(ShortPulseSurvivesLongNeutralPeriod()||PulseMissingPhasesAndBoundedRollover()||PulseSourceBoundariesAndExactEyes()||PulseClocksAndOldBaseline())return 1;
    if(PreOnsetCategoriesAndFrozenCandidate()||PreSourceAndInvalidationReasons()||BaselineReadyFailureBits())return 1;
    std::puts("Vehicle camera evidence: bounded ride/pulse retention, source boundaries and exact eye joins passed");
}
