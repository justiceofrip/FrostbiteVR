#include "Bc2FireRouteTelemetry.h"
#include "Test.h"
#include <algorithm>
using namespace fvr::bc2;
namespace {
FireRouteRecord Sample() {
    FireRouteRecord r;
    r.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};
    r.equipment.weapon=r.owner.weapon;r.equipment.data=0x50000;r.equipment.persistence=7;
    constexpr char asset[]="XM8";std::copy(std::begin(asset),std::end(asset),r.equipment.asset.begin());
    r.input=1;r.inputEpoch=2;r.inputObservedNs=1000000000;r.inputDeadlineNs=1100000000;
    r.sampledNs=1010000000;r.onFoot=r.inputPresent=r.inputFresh=true;
    r.reasons=0;r.write=FireRouteWrite::Applied;r.writeNs=r.sampledNs;
    return r;
}
Bc2AmmoReserveLease Ammo(const FireRouteRecord& r) {
    Bc2AmmoReserveLease a;a.identity.owner=r.owner;a.identity.firing={0x60000,0x70000,0x80000};
    a.identity.serverPlayer=0x90000;a.identity.serverSoldier=0xa0000;a.identity.serverItem=0xb0000;
    a.sequence=1;a.observedNs=r.sampledNs-1000000;a.deadlineNs=r.sampledNs+100000000;
    a.verified=true;a.loaded=a.capacity=30;a.reserve=180;return a;
}
}
int main() {
    // Time/sequence churn and duplicate controller packets do not fill the ring.
    FireRouteTelemetry journal;auto r=Sample();CHECK(journal.Observe(r));
    for(unsigned i=0;i<1000;++i){++r.input;++r.sampledNs;CHECK(journal.Observe(r));}
    FireRouteTelemetryBatch batch;CHECK(journal.TryDrain(batch));
    CHECK(batch.count==1&&batch.samples==1001&&batch.events==1&&batch.overwritten==0);
    CHECK(batch.records[0].changes==FireRouteFirst&&batch.latest.sampledNs==r.sampledNs);
    CHECK(batch.latest.inputAgeNs==r.sampledNs-r.inputObservedNs);
    CHECK(journal.TryDrain(batch)&&batch.count==0&&batch.latest.sampledNs==r.sampledNs);

    // Requested -> semantic rejection -> mapped -> filter -> native refusal ->
    // successful cache commit remain distinct, with the same owner and input.
    r.requested=true;CHECK(journal.Observe(r));
    r.reasons=FireRouteSemanticGate;CHECK(journal.Observe(r));
    r.mapped=r.effective=true;r.reasons=0;CHECK(journal.Observe(r));
    r.effective=false;r.reasons=FireRouteFeed;CHECK(journal.Observe(r));
    r.effective=true;r.write=FireRouteWrite::Refused;r.reasons=FireRouteApplyRefused;CHECK(journal.Observe(r));
    r.write=FireRouteWrite::Applied;r.reasons=0;CHECK(journal.Observe(r));
    CHECK(journal.TryDrain(batch)&&batch.count==6);
    CHECK(batch.records[0].changes&FireRouteInputChanged);
    CHECK(batch.records[1].requested&&!batch.records[1].mapped&&batch.records[1].reasons==FireRouteSemanticGate);
    CHECK(batch.records[2].mapped&&batch.records[2].effective);
    CHECK(batch.records[3].mapped&&!batch.records[3].effective&&batch.records[3].reasons==FireRouteFeed);
    CHECK(batch.records[4].write==FireRouteWrite::Refused&&(batch.records[4].changes&FireRouteWriteChanged));
    CHECK(batch.records[5].write==FireRouteWrite::Applied);

    // Coherent stock count changes are events; new leases with identical
    // counts update latest provenance without manufacturing additional shots.
    auto a=Ammo(r);ObserveFireRouteAmmo(r,a,r.sampledNs);CHECK(r.ammo==FireRouteAmmo::Valid);
    CHECK(journal.Observe(r));
    ++a.sequence;++a.observedNs;++r.sampledNs;ObserveFireRouteAmmo(r,a,r.sampledNs);CHECK(journal.Observe(r));
    --a.loaded;ObserveFireRouteAmmo(r,a,r.sampledNs);CHECK(journal.Observe(r));
    CHECK(journal.TryDrain(batch)&&batch.count==2);
    CHECK(batch.records[0].loaded==30&&batch.records[1].loaded==29);
    CHECK(batch.records[1].changes&FireRouteAmmoChanged);
    CHECK(batch.latest.ammoObservedNs==a.observedNs&&batch.latest.ammoDeadlineNs==a.deadlineNs);

    // Configuration is carried only across the exact owner/equipment tuple;
    // equipment replacement and input epoch changes remain independent.
    ReloadStateSnapshot config;config.owner=r.owner;config.config.weaponData=r.equipment.data;
    constexpr char path[]="Weapons/XM8/XM8";std::copy(std::begin(path),std::end(path),config.config.assetPath.begin());
    config.config.firingData=0xc0000;config.config.primaryFire=0xd0000;config.config.fireInputAction=8;
    ObserveFireRouteConfiguration(r,config);CHECK(journal.Observe(r));
    auto next=r;next.configuration={};CHECK(journal.Observe(next));
    CHECK(journal.TryDrain(batch)&&batch.count==1&&batch.latest.configuration.observed);
    CHECK(batch.latest.configuration.path==config.config.assetPath);
    ++next.inputEpoch;CHECK(journal.Observe(next));
    CHECK(journal.TryDrain(batch)&&batch.count==1);
    CHECK((batch.records[0].changes&FireRouteInputChanged)&&!(batch.records[0].changes&FireRouteOwnerChanged));
    CHECK(batch.latest.configuration.observed);
    ++next.owner.equipGeneration;++next.equipment.data;ObserveFireRouteAmmo(next,a,next.sampledNs);
    CHECK(next.ammo==FireRouteAmmo::WrongOwner);CHECK(journal.Observe(next));
    CHECK(journal.TryDrain(batch)&&batch.count==1);
    CHECK((batch.records[0].changes&FireRouteOwnerChanged)&&!batch.latest.configuration.observed);
    CHECK(batch.latest.loaded==-1&&batch.latest.ammoSequence==0);
    auto wrong=config;wrong.owner=next.owner;ObserveFireRouteConfiguration(next,wrong);
    CHECK(!next.configuration.observed); // Same asset name cannot grant a new data object's config.

    // Invalid native values and expired/future/unverified/wrong-owner evidence
    // never enter the journal as available numeric ammunition.
    const auto validRecord=Sample();const auto validAmmo=Ammo(validRecord);
    for(unsigned defect=0;defect<11;++defect){auto bad=validAmmo;auto sample=validRecord;
        switch(defect){
        case 0:bad.loaded=-1;break;case 1:bad.loaded=31;break;case 2:bad.capacity=0;break;
        case 3:bad.capacity=1000001;break;case 4:bad.reserve=-1;break;case 5:bad.reserve=1000001;break;
        case 6:bad.deadlineNs=sample.sampledNs;break;case 7:bad.observedNs=sample.sampledNs+1;break;
        case 8:bad.verified=false;break;case 9:++bad.identity.owner.actorGeneration;break;
        case 10:bad.identity.firing[2]=0;break;
        }
        ObserveFireRouteAmmo(sample,bad,sample.sampledNs);
        CHECK(sample.ammo!=FireRouteAmmo::Valid&&sample.loaded==-1&&sample.reserve==-1&&sample.capacity==-1);
        CHECK(sample.ammoSequence==0&&sample.ammoObservedNs==0&&sample.ammoDeadlineNs==0);
    }
    auto empty=validAmmo;empty.loaded=0;auto zero=validRecord;ObserveFireRouteAmmo(zero,empty,zero.sampledNs);
    CHECK(zero.ammo==FireRouteAmmo::Valid&&zero.loaded==0);
    ObserveFireRouteAmmo(zero,std::nullopt,zero.sampledNs);CHECK(zero.ammo==FireRouteAmmo::Unavailable&&zero.loaded==-1);

    // Overflow discards exactly the oldest transitions, then draining permits
    // continued capture without silently resetting cumulative drop evidence.
    FireRouteTelemetry bounded;auto toggle=Sample();
    constexpr auto total=FireRouteTelemetryCapacity+19;
    for(std::size_t i=0;i<total;++i){toggle.requested=(i%2)!=0;toggle.sampledNs+=100;CHECK(bounded.Observe(toggle));}
    CHECK(bounded.TryDrain(batch)&&batch.count==FireRouteTelemetryCapacity);
    CHECK(batch.events==total&&batch.overwritten==19&&batch.records[0].serial==20);
    for(std::size_t i=1;i<batch.count;++i)CHECK(batch.records[i].serial==batch.records[i-1].serial+1);
    CHECK(batch.records.back().sampledNs==toggle.sampledNs);
    toggle.requested=!toggle.requested;CHECK(bounded.Observe(toggle));
    CHECK(bounded.TryDrain(batch)&&batch.count==1&&batch.overwritten==19&&batch.events==total+1);
    return 0;
}
