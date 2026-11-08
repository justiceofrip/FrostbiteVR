#include "Test.h"
#include "Bc2AmmoPublisherObservation.h"
#include <cstring>
#include <iostream>
using namespace fvr::bc2;
namespace {
using Row=AmmoPublisherObservation;
using Window=AmmoPublisherObservationWindow;
constexpr std::int64_t Now=1000000000;
Row Sample(std::int64_t at,std::uint64_t sequence=1){
    Row r;r.beginNs=at;r.endNs=at+1;r.selectBeginNs=at;r.selectEndNs=at+1;
    r.leaseIdentity={{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    r.input.identity=r.leaseIdentity;
    const auto& i=r.leaseIdentity;
    fvr::interaction::AmmoResourceContext c{{i.owner.soldier,i.owner.actorGeneration,i.owner.weapon,1},i.owner.equipGeneration,i.owner.space};
    r.selectedSnapshot=fvr::interaction::AmmunitionSnapshot{c,sequence,at,at+100000000,{0,7,1},true};
    r.selected=true;r.selectPublication={true,true,true,at,at+1,
        AmmoResourceBinding{i.owner,c,0x80000,0x90000,0xa0000,0xb0000,at,at+100000000},r.selectedSnapshot};
    r.freshPublication=AmmoPublisherFreshPublication(r,r.selectPublication);return r;
}
int ShortCircuitParity(){
    // Each downstream predicate stands in for an observable read. Forced early
    // failures must leave exactly the same read trace, including null capture.
    constexpr const char* names[]{"prepare.boundary","prepare.capacity","publisher.weapon_data","publisher.persistence"};
    for(unsigned first=0;first<=4;++first)for(bool enabled:{false,true}){
        unsigned originalReads=0,instrumentedReads=0;Row row;
        const auto readA=[&](unsigned index){++originalReads;return index==first;};
        const auto readB=[&](unsigned index){++instrumentedReads;return index==first;};
        const bool original=readA(0)||readA(1)||readA(2)||readA(3);
        const bool observed=AmmoPublisherFails(enabled?&row:nullptr,names[0],readB(0))||
            AmmoPublisherFails(enabled?&row:nullptr,names[1],readB(1))||
            AmmoPublisherFails(enabled?&row:nullptr,names[2],readB(2))||
            AmmoPublisherFails(enabled?&row:nullptr,names[3],readB(3));
        CHECK(original==observed&&originalReads==instrumentedReads);
        CHECK(originalReads==(first<4?first+1:4));
        CHECK((row.failure!=nullptr)==(enabled&&first<4));
        if(row.failure){CHECK(std::strcmp(row.failure,names[first])==0);
            CHECK(AmmoPublisherFails(&row,"later",true));CHECK(std::strcmp(row.failure,names[first])==0);}
    }
    return 0;
}
int StalePublicationAndFirstRecovery(){
    std::atomic_flag flag=ATOMIC_FLAG_INIT;Window journal(flag);Window::Snapshot copy;
    CHECK(journal.Request(Now,Now+600000000));const auto ticket=journal.Begin(Now+1);
    auto fresh=Sample(Now+1);CHECK(fresh.freshPublication&&journal.Publish(ticket,fresh,false));
    auto failure=fresh;failure.selected=false;failure.freshPublication=false;failure.selectBeginNs=0;
    failure.failure="publisher.fire";failure.failureNs=Now+2;failure.endNs=Now+3;
    failure.updatePublication=fresh.selectPublication;failure.updatePublication.endNs=Now+100000001;
    CHECK(!AmmoPublisherFreshPublication(failure,failure.updatePublication));
    CHECK(journal.Publish(ticket,failure,true));
    for(unsigned n=0;n<3;++n){auto stale=failure;stale.failure="publisher.current_state";stale.failureNs=Now+315000000+n*100000000;
        stale.endNs=stale.failureNs+1;CHECK(journal.Publish(ticket,stale,true));}
    CHECK(journal.Read(copy));auto& gap=copy.retention.gaps[0];
    CHECK(copy.retention.open&&copy.retention.accepted==1&&copy.retention.republished==4);
    CHECK(gap.precedingAccepted&&gap.firstFailure&&gap.latestFailure&&!gap.firstSelect&&!gap.firstPublication);
    CHECK(std::strcmp(gap.firstFailure->failure,"publisher.fire")==0);
    CHECK(gap.latestFailure->failureNs==Now+515000000);
    auto select=Sample(Now+550000000,2);select.selectPublication.passed=false;select.freshPublication=false;
    select.failure="publisher.publish";select.failureNs=select.selectEndNs;
    CHECK(journal.Publish(ticket,select,true));
    CHECK(journal.Read(copy)&&copy.retention.open&&copy.retention.gaps[0].firstSelect&&!copy.retention.gaps[0].firstPublication);
    auto recovery=Sample(Now+560000000,3);CHECK(journal.Publish(ticket,recovery,false));
    auto later=Sample(Now+570000000,4);CHECK(journal.Publish(ticket,later,false));
    CHECK(journal.Read(copy)&&!copy.retention.open&&copy.retention.accepted==3);
    CHECK(copy.retention.gaps[0].firstSelect->selectedSnapshot->sequence==2);
    CHECK(copy.retention.gaps[0].firstPublication->selectedSnapshot->sequence==3);
    CHECK(copy.retention.gaps[0].precedingAccepted->selectedSnapshot->sequence==1);
    CHECK(copy.retention.lastFresh->selectedSnapshot->sequence==4);
    CHECK(!journal.Begin(Now+600000000).epoch); // Original window never gains a delay.
    CHECK(journal.Read(copy)&&copy.retention.gaps[0].firstPublication); // Drained evidence survives expiry.
    return 0;
}
int PublicationIdentityAndDeadline(){
    const auto fresh=Sample(Now);CHECK(fresh.freshPublication);
    auto r=fresh;r.selected=false;CHECK(!AmmoPublisherFreshPublication(r,r.selectPublication));
    r=fresh;r.selectPublication.passed=false;CHECK(!AmmoPublisherFreshPublication(r,r.selectPublication));
    r=fresh;++r.selectPublication.snapshot->sequence;CHECK(!AmmoPublisherFreshPublication(r,r.selectPublication));
    r=fresh;++r.selectPublication.binding->owner.equipGeneration;CHECK(!AmmoPublisherFreshPublication(r,r.selectPublication));
    r=fresh;++r.selectPublication.snapshot->context.resource.weaponGeneration;CHECK(!AmmoPublisherFreshPublication(r,r.selectPublication));
    r=fresh;r.selectPublication.endNs=Now+100000000;CHECK(!AmmoPublisherFreshPublication(r,r.selectPublication));
    r=fresh;r.selectPublication.endNs=Now-1;CHECK(!AmmoPublisherFreshPublication(r,r.selectPublication));
    r=fresh;r.selectPublication.binding.reset();CHECK(!AmmoPublisherFreshPublication(r,r.selectPublication));
    return 0;
}
int BoundedLossAndScope(){
    std::atomic_flag flag=ATOMIC_FLAG_INIT;Window journal(flag);Window::Snapshot copy;
    CHECK(!journal.Begin(Now).epoch&&!journal.Publish({},Sample(Now),false));
    CHECK(journal.Request(Now,Now+1000));const auto ticket=journal.Begin(Now+1);
    auto failure=Sample(Now+1);failure.selected=false;failure.freshPublication=false;failure.failure="prepare.context_read";
    failure.failureNs=Now+1;failure.preparation.stage=4;failure.preparation.readBranch=2;
    CHECK(journal.Publish(ticket,failure,true));
    auto other=Sample(Now+2);++other.leaseIdentity.owner.equipGeneration;
    CHECK(journal.Publish(ticket,other,false));CHECK(journal.Read(copy)&&copy.retention.open&&!copy.retention.gaps[0].firstPublication);
    CHECK(!flag.test_and_set());CHECK(!journal.Publish(ticket,failure,true)&&!journal.Read(copy)&&!journal.Request(Now+2,Now+2000));flag.clear();
    CHECK(journal.Drops().publication==1&&journal.Drops().snapshot==1&&journal.Drops().request==1);
    CHECK(journal.Publish(ticket,Sample(Now+3),false));
    for(unsigned n=0;n<10;++n){auto f=failure;f.failureNs=Now+10+n*2;f.endNs=f.failureNs;
        CHECK(journal.Publish(ticket,f,true));CHECK(journal.Publish(ticket,Sample(f.failureNs+1,n+2),false));}
    CHECK(journal.Read(copy)&&copy.retention.count==8&&copy.retention.episodes==11&&copy.retention.overwritten==3);
    CHECK(journal.Publish(ticket,failure,true));CHECK(journal.Read(copy)&&copy.retention.outOfOrder==1);
    CHECK(journal.Request(Now+1000,Now+2000));CHECK(!journal.Publish(ticket,failure,true));
    CHECK(journal.Read(copy)&&copy.lateEpochDropped==1&&copy.retention.episodes==0&&!copy.retention.lastFresh);
    return 0;
}
}
int main(){if(ShortCircuitParity()||StalePublicationAndFirstRecovery()||PublicationIdentityAndDeadline()||BoundedLossAndScope())return 1;
    std::cout<<"ammo publisher observation regression passed; row="<<sizeof(Row)
        <<" snapshot="<<sizeof(Window::Snapshot)<<" bytes\n";
}
