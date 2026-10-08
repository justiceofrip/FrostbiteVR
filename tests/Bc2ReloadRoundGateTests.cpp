#include "Test.h"
#include "Bc2ReloadRoundGate.h"
#include <cstring>
#include <limits>
using namespace fvr::bc2;
namespace {
ReloadHoldInput Input(){
    ReloadHoldInput i;i.verified=true;i.branch=0;i.nowNs=1000000000;i.leaseDeadlineNs=i.nowNs+100000000;
    i.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};i.identity.firing={0x50000,0x60000,0x70000};
    i.identity.serverPlayer=0x80000;i.identity.serverSoldier=0x90000;i.identity.serverItem=0xa0000;
    auto& c=i.config;std::memcpy(c.assetName.data(),"SPAS12_sp",sizeof("SPAS12_sp"));std::memcpy(c.assetPath.data(),"Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12",sizeof("Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12"));
    c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
    c.fireLogicType=1;c.reloadType=0;c.fireInputAction=8;c.reloadInputAction=29;c.baseCapacity=c.numberOfMagazines=4;
    c.reloadDelay=.06f;c.reloadTime=.72f;c.reloadThreshold=c.postReloadTime=1;c.boltDelay=.5f;
    i.context.deltaSeconds=.005f;i.context.reloadTimeMultiplier=1;i.context.flags24Through28[0]=true;
    for(unsigned n=0;n<3;++n){auto& b=i.branches[n];b.address=i.identity.firing[n];b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;
        b.currentState=11;b.nextState=12;b.phaseTimer=.2f;b.loaded=2;b.reserve=8;i.capacities[n]=8;}
    return i;
}

struct Simulation {
    ReloadRoundGate gate;ReloadHoldInput input=Input();std::uint64_t id=0;unsigned nativeCalls=0;
    void Time(std::int64_t t){input.nowNs=t;input.leaseDeadlineNs=t+100000000;}
    ReloadRoundGateDecision Begin(unsigned branch,bool stable=true,bool timing=true){input.branch=branch;return gate.Evaluate(input,timing,stable,++id);}
    bool End(const ReloadRoundGateDecision& d,bool heldOkay=true){++nativeCalls;ReloadDeltaOverride delta;
        if(d.hold){delta.applied=delta.restored=heldOkay;delta.original=1;}
        return gate.Finish(d,input.branches[d.branch],input.nowNs+100,true,delta);}
    bool Arm(){if(!gate.Enable())return false;for(unsigned n=0;n<3;++n){Time(input.nowNs+1000);auto d=Begin(n);if(n<2){if(d.tracked)return false;}else if(!d.hold||!End(d))return false;}
        for(unsigned n=0;n<3;++n){Time(input.nowNs+1000);auto d=Begin(n);if(!d.hold||!End(d))return false;}return true;}
    bool ExpireFirst(){
        while(input.nowNs+5000000<gate.FirstDeadline()){
            Time(input.nowNs+4000000);for(unsigned n=0;n<3;++n){auto d=Begin(n);if(!d.hold||!End(d))return false;Time(input.nowNs+1000);}
        }
        Time(gate.FirstDeadline()-1000000);for(unsigned n=0;n<3;++n){auto d=Begin(n);if(!d.hold||!End(d))return false;Time(input.nowNs+1000);}
        Time(gate.FirstDeadline());return true;}
    bool Advance(unsigned branch,bool stable=true){auto d=Begin(branch,stable);if(!d.tracked||d.hold)return false;
        auto& b=input.branches[branch];ReloadRoundTransfer e{input.identity,1,1000+branch,input.nowNs+1,input.nowNs+2,branch,b.loaded,b.reserve,b.loaded+1,b.reserve-1,true,true};
        if(!gate.Transfer(e,d.update))return false;b.loaded++;b.reserve--;b.phaseTimer=.719995f;
        if(b.loaded==input.capacities[branch]||!b.reserve){b.currentState=b.nextState=1;b.phaseTimer=.9999f;}
        return End(d);}
    bool Rehold(unsigned branch){Time(input.nowNs+1000);auto d=Begin(branch);return d.hold&&End(d);}
};
int ExactlyOneThenThreeReholds(){for(const auto order:{std::array<unsigned,3>{0,1,2},std::array<unsigned,3>{2,0,1},std::array<unsigned,3>{1,2,0}}){
    Simulation s;CHECK(s.Arm()&&s.ExpireFirst());
    for(auto branch:order){CHECK(s.Advance(branch));CHECK(!s.gate.Acknowledgement());CHECK(s.Rehold(branch));}
    s.Time(s.input.nowNs+1000);auto d=s.Begin(0);CHECK(d.hold&&s.gate.Phase()==ReloadRoundGatePhase::SecondHold&&s.End(d));
    const auto ack=s.gate.Acknowledgement();CHECK(ack&&ack->semantic.request==1&&ack->cycle==1&&ack->serverInvocation==1002&&ack->identity==s.input.identity);
    CHECK(s.gate.SecondDeadline()-s.gate.SecondBegin()==350000000);for(auto id:s.gate.FirstReholds())CHECK(id);
    s.Time(s.gate.SecondDeadline());CHECK(!s.Begin(0).tracked&&s.gate.Phase()==ReloadRoundGatePhase::Released);CHECK(!s.gate.Enable());
    for(const auto& b:s.input.branches)CHECK(b.loaded==3&&b.reserve==7);
}return 0;}
int LaggingCopiesAndBusyCohort(){Simulation s;CHECK(s.Arm()&&s.ExpireFirst());CHECK(s.Advance(0));CHECK(s.Rehold(0));
    auto lag=s.Begin(1,false);CHECK(lag.tracked&&!lag.hold&&s.End(lag));CHECK(!s.gate.Acknowledgement());
    CHECK(s.Advance(1,false)&&s.Rehold(1));CHECK(s.Advance(2,false)&&s.Rehold(2));s.Time(s.input.nowNs+1000);
    auto busy=s.Begin(0,false);CHECK(busy.hold&&s.End(busy)&&!s.gate.Acknowledgement());
    s.Time(s.input.nowNs+1000);auto quiet=s.Begin(0,true);CHECK(quiet.hold&&s.gate.Acknowledgement()&&s.End(quiet));return 0;}
int ActualHoldsRequired(){Simulation s;CHECK(s.Arm()&&s.ExpireFirst());for(unsigned n=0;n<3;++n)CHECK(s.Advance(n));
    s.Time(s.input.nowNs+1000);auto d=s.Begin(0);CHECK(d.hold&&!s.gate.Acknowledgement());CHECK(!s.End(d,false));CHECK(s.gate.Phase()==ReloadRoundGatePhase::Aborted);return 0;}
int DuplicateRoundAndWrongParent(){for(unsigned test=0;test<5;++test){Simulation s;CHECK(s.Arm()&&s.ExpireFirst());auto d=s.Begin(0);CHECK(d.tracked&&!d.hold);
    ReloadRoundTransfer e{s.input.identity,1,1000,s.input.nowNs+1,s.input.nowNs+2,0,2,8,3,7,true,true};
    if(test==0)e.cycle++;if(test==1)e.identity.owner.space++;if(test==2)e.loadedAfter++;if(test==3)e.ordinaryState12Verified=false;
    CHECK(!s.gate.Transfer(e,test==4?d.update+1:d.update)&&s.gate.Phase()==ReloadRoundGatePhase::Aborted);}
    Simulation s;CHECK(s.Arm()&&s.ExpireFirst());auto d=s.Begin(0);ReloadRoundTransfer e{s.input.identity,1,1000,s.input.nowNs+1,s.input.nowNs+2,0,2,8,3,7,true,true};
    CHECK(s.gate.Transfer(e,d.update)&&s.gate.Transfer(e,d.update));e.invocation++;CHECK(!s.gate.Transfer(e,d.update));return 0;}
int UnsafeTimingAndCancellation(){for(unsigned test=0;test<8;++test){Simulation s;CHECK(s.Arm()&&s.ExpireFirst());
    switch(test){case 0:s.input.context.reloadTimeMultiplier=.0001f;break;case 1:s.input.context.deltaSeconds=.051f;break;case 2:s.input.context.inputFlags=1;break;
        case 3:s.input.identity.owner.equipGeneration++;break;case 4:s.input.leaseDeadlineNs=s.input.nowNs;break;case 5:s.input.branches[0].phaseTimer=std::numeric_limits<float>::quiet_NaN();break;
        case 6:for(auto& c:s.input.capacities)c=16;break;case 7:s.input.verified=false;break;}
    CHECK(!s.Begin(0).tracked&&s.gate.Phase()==ReloadRoundGatePhase::Aborted&&!s.gate.Acknowledgement());}
    Simulation s;CHECK(s.Arm()&&s.ExpireFirst());CHECK(!s.Begin(0,true,false).tracked&&s.gate.Failure()==ReloadRoundGateFailure::Timing);
    Simulation t;CHECK(t.Arm());t.gate.Cancel(ReloadRoundGateFailure::Stopped);CHECK(!t.Begin(0).tracked&&!t.gate.Enable());return 0;}
int TimeoutsAndReverts(){Simulation s;CHECK(s.Arm()&&s.ExpireFirst());auto d=s.Begin(0);CHECK(d.tracked&&s.End(d));
    s.Time(s.gate.AdvanceDeadline());CHECK(!s.Begin(0).tracked&&s.gate.Failure()==ReloadRoundGateFailure::Expired);
    Simulation r;CHECK(r.Arm()&&r.ExpireFirst()&&r.Advance(0));r.input.branches[0].loaded=2;r.input.branches[0].reserve=8;
    CHECK(!r.Begin(0).tracked&&r.gate.Phase()==ReloadRoundGatePhase::Aborted);return 0;}
int FullOrEmptyReserveFinish(){for(bool full:{false,true}){Simulation s;
    for(auto& b:s.input.branches){b.loaded=full?7:2;b.reserve=1;}CHECK(s.Arm()&&s.ExpireFirst());for(unsigned n=0;n<3;++n)CHECK(s.Advance(n));
    s.Time(s.input.nowNs+1000);CHECK(!s.Begin(0).tracked&&s.gate.Phase()==ReloadRoundGatePhase::Released&&s.gate.Acknowledgement());}return 0;}
int TimingReaderRejectsChanges(){struct Memory {float time=.72f,threshold=1;unsigned reads=0;bool race=false,fail=false;
    static bool Read(void* v,unsigned at,void* out,std::size_t size){auto& m=*static_cast<Memory*>(v);++m.reads;if(m.fail||size!=4)return false;
        const float value=at==0xd0010?m.threshold:at==0xd0018?(m.race&&m.reads>2?.73f:m.time):0;std::memcpy(out,&value,4);return true;}};
    auto in=Input();Memory m;ReloadStateMemory reader{&m,Memory::Read,nullptr};CHECK(ReadReloadRoundTiming(reader,in.config)&&m.reads==4);
    m.reads=0;m.race=true;CHECK(!ReadReloadRoundTiming(reader,in.config));m.race=false;m.threshold=.5f;CHECK(!ReadReloadRoundTiming(reader,in.config));
    m.threshold=1;m.fail=true;CHECK(!ReadReloadRoundTiming(reader,in.config));return 0;}
int InitialInflightHoldDoesNotCountAsRehold(){Simulation s;CHECK(s.Arm());
    while(s.input.nowNs+5000000<s.gate.FirstDeadline()){s.Time(s.input.nowNs+4000000);for(unsigned n=0;n<3;++n){auto d=s.Begin(n);CHECK(d.hold&&s.End(d));s.Time(s.input.nowNs+1000);}}
    s.Time(s.gate.FirstDeadline()-500);auto old=s.Begin(2);CHECK(old.hold);
    s.Time(s.gate.FirstDeadline());auto first=s.Begin(0);CHECK(first.tracked&&!first.hold&&s.gate.Phase()==ReloadRoundGatePhase::Advancing);
    CHECK(s.End(old)&&s.End(first));CHECK(!s.gate.FirstReholds()[2]&&!s.gate.Acknowledgement());return 0;}
int DefaultOffAndNativeOverlap(){Simulation s;CHECK(!s.Begin(0).tracked&&s.gate.Phase()==ReloadRoundGatePhase::Disabled);
    CHECK(s.Arm()&&s.ExpireFirst());auto d=s.Begin(0);CHECK(d.tracked);CHECK(!s.Begin(0).tracked&&s.gate.Failure()==ReloadRoundGateFailure::Overlap);return 0;}
}
int main(){if(ExactlyOneThenThreeReholds()||LaggingCopiesAndBusyCohort()||ActualHoldsRequired()||DuplicateRoundAndWrongParent()||UnsafeTimingAndCancellation()||TimeoutsAndReverts()||FullOrEmptyReserveFinish()||TimingReaderRejectsChanges()||InitialInflightHoldDoesNotCountAsRehold()||DefaultOffAndNativeOverlap())return 1;
    std::printf("Ten diagnostic single-round release/rehold groups passed; no live integration enabled.\n");return 0;}
