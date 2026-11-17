#include "Test.h"
#include "Bc2VehicleFinalInputEvidence.h"
using namespace fvr::bc2;
namespace {
VehicleFinalInputCommit Commit(unsigned throttle=0x3ec7ce0c){
    VehicleFinalInputCommit c;c.actor=1;c.actorGeneration=2;c.vehicle=3;c.entry=4;c.cache=5;c.driver=6;
    c.update=7;c.gather=8;c.input=9;c.throttle=throttle;c.permissions=1909;c.stamp={123,456};return c;
}
int ValuesAndBookends(){
    for(unsigned change=0;change<4;++change){
        const auto c=Commit();unsigned validations=0,reads=0;
        const auto row=ObserveVehicleFinalInput(c,false,false,[&](){++validations;return VehicleFinalInputCheck::Okay;},
            [&](unsigned& throttle,unsigned& permissions){++reads;throttle=change&1?0:c.throttle;permissions=c.permissions^(change&2?1:0);return true;});
        CHECK(row.complete&&row.readable&&validations==2&&reads==1);
        CHECK(row.throttleChanged==bool(change&1)&&row.permissionsChanged==bool(change&2));
        CHECK(row.commit.update==7&&row.commit.gather==8&&row.commit.input==9&&row.commit.stamp.qpc==123);
    }
    auto c=Commit(0);const auto zero=ObserveVehicleFinalInput(c,false,false,[](){return VehicleFinalInputCheck::Okay;},
        [&](unsigned& t,unsigned& p){t=0;p=c.permissions;return true;});
    CHECK(zero.complete&&!zero.throttleChanged&&!zero.permissionsChanged);
    return 0;
}
int RefusalsAndUnwind(){
    for(auto failure:{VehicleFinalInputCheck::Owner,VehicleFinalInputCheck::Route,VehicleFinalInputCheck::Driver}){
        for(unsigned bookend=0;bookend<2;++bookend){
            unsigned calls=0,reads=0;
            const auto row=ObserveVehicleFinalInput(Commit(),false,false,[&](){return calls++==bookend?failure:VehicleFinalInputCheck::Okay;},
                [&](unsigned& t,unsigned& p){++reads;t=0;p=0;return true;});
            CHECK(!row.complete&&!row.throttleChanged&&!row.permissionsChanged);
            CHECK(calls==bookend+1&&reads==bookend);
            CHECK((bookend?row.after:row.before)==failure&&row.readable==bool(bookend));
        }
    }
    for(unsigned omitted=0;omitted<3;++omitted){
        auto c=Commit();if(omitted==2)c.check=VehicleFinalInputCheck::Read;
        unsigned calls=0;
        const auto row=ObserveVehicleFinalInput(c,omitted==0,omitted==1,[&](){++calls;return VehicleFinalInputCheck::Okay;},
            [&](unsigned&,unsigned&){++calls;return true;});
        CHECK(!row.complete&&!row.readable&&calls==0);
        CHECK(row.before==(omitted==0?VehicleFinalInputCheck::Nested:omitted==1?VehicleFinalInputCheck::Unwind:VehicleFinalInputCheck::Read));
    }
    unsigned calls=0;
    const auto unreadable=ObserveVehicleFinalInput(Commit(),false,false,[&](){++calls;return VehicleFinalInputCheck::Okay;},
        [](unsigned& t,unsigned& p){t=0;p=1909;return false;});
    CHECK(calls==2&&!unreadable.complete&&!unreadable.readable&&unreadable.after==VehicleFinalInputCheck::Read);
    return 0;
}
int RetentionAndBounds(){
    VehicleFinalInputEvidence<4> journal;
    const auto add=[&](unsigned committed,unsigned final,bool failed=false){
        auto row=ObserveVehicleFinalInput(Commit(committed),false,failed,[](){return VehicleFinalInputCheck::Okay;},
            [&](unsigned& t,unsigned& p){t=final;p=1909;return true;});journal.Observe(row);
    };
    add(0,0);add(1,1);add(1,0);add(0,0);
    for(unsigned i=0;i<100;++i)add(0,0,i%2==0);
    CHECK(journal.Size()==4&&journal.Samples()==104&&journal.Omitted()==100);
    unsigned anchors=0;bool latest=false;
    for(std::size_t i=0;i<journal.Size();++i){const auto& row=journal.At(i);anchors|=row.firstMask;
        if(row.firstMask&1)CHECK(row.sequence==2);
        if(row.firstMask&2)CHECK(row.sequence==3&&row.throttleChanged);
        if(row.firstMask&4)CHECK(row.sequence==4&&row.commit.throttle==0);
        latest|=row.sequence==104;
    }
    CHECK(anchors==7&&latest&&journal.Failures()==50&&journal.OmittedFailures()==50);
    return 0;
}
}
int main(){
    if(ValuesAndBookends()||RefusalsAndUnwind()||RetentionAndBounds())return 1;
    std::puts("Vehicle final input evidence: exact bookends, net word changes, omissions and bounded retention passed");
}
