#include "Bc2FireRouteTelemetry.h"
#include "Test.h"
#include <algorithm>
#include <limits>
#include <sstream>
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

    // Short interaction failures survive a later recovery within the same
    // writer interval. No final Gameplay::Report or live consumer read is used.
    FireRouteTelemetry interactions;auto episode=Sample();
    episode.requested=episode.mapped=episode.effective=true;
    auto& support=episode.interactions.support;support.observed=support.evaluated=support.holding=true;
    support.ownershipAttempted=support.ownershipAccepted=true;support.token=41;
    CHECK(interactions.Observe(episode));
    support.previousToken=41;support.token=0;support.holding=false;support.released=true;
    support.reason=6;support.distanceBand=3;episode.supportDistance=.31f;
    CHECK(interactions.Observe(episode));
    auto& shell=episode.interactions.shell;shell.observed=true;shell.cancelReason=6;shell.supplyReason=9;
    shell.sourceFlags=256;shell.bridgePhase=4;CHECK(interactions.Observe(episode));
    auto& feed=episode.interactions.feed;feed.observed=true;feed.exit=4;feed.admissionChecked=15;feed.admissionPassed=7;
    CHECK(interactions.Observe(episode));
    feed.exit=7;feed.admissionChecked=feed.admissionPassed=63;feed.rawChecked=15;feed.rawPassed=7;
    CHECK(interactions.Observe(episode));
    auto& sight=episode.interactions.sight;sight.observed=sight.evaluated=true;sight.phase=0;sight.policyPhase=1;
    sight.reason=1;sight.ownershipAttempted=true;sight.ownershipAccepted=false;
    CHECK(interactions.Observe(episode));
    support.holding=true;support.released=false;support.token=42;support.reason=0;support.distanceBand=1;
    shell.cancelReason=shell.supplyReason=~0u;feed.exit=1;feed.rawChecked=feed.rawPassed=511;
    CHECK(interactions.Observe(episode));
    CHECK(interactions.TryDrain(batch)&&batch.count==7);
    CHECK(batch.records[1].interactions.support.reason==6&&batch.records[1].interactions.support.previousToken==41);
    CHECK(batch.records[2].interactions.shell.cancelReason==6&&batch.records[2].interactions.shell.sourceFlags==256);
    CHECK(batch.records[3].interactions.feed.admissionChecked==15&&batch.records[3].interactions.feed.admissionPassed==7);
    CHECK(batch.records[4].interactions.feed.rawChecked==15&&batch.records[4].interactions.feed.rawPassed==7);
    CHECK(batch.records[5].interactions.sight.ownershipAttempted&&!batch.records[5].interactions.sight.ownershipAccepted);
    for(std::size_t i=1;i<batch.count;++i){
        CHECK(batch.records[i].changes==FireRouteInteractionChanged);
        CHECK(batch.records[i].owner==episode.owner&&batch.records[i].equipment==episode.equipment);
        CHECK(batch.records[i].requested&&batch.records[i].mapped&&batch.records[i].effective);
        CHECK(batch.records[i].write==FireRouteWrite::Applied&&batch.records[i].reasons==0);
    }

    // Duplicate source packets, numeric pose jitter and clocks only refresh
    // latest provenance. They cannot crowd actual failures out of the ring.
    sight.paired=sight.packetObserved=true;sight.packetReason=0;sight.advanced=true;feed.status=3u<<8;
    CHECK(interactions.Observe(episode));CHECK(interactions.TryDrain(batch)&&batch.count==1);
    for(unsigned i=0;i<1000;++i){sight.packetReason=i%2;sight.advanced=(i%2)==0;
        feed.status=(i%2?7u:3u)<<8;episode.supportDistance=.1f+float(i)*.00001f;
        ++episode.supportSource;++episode.supportSourceDeadlineNs;++episode.sampledNs;
        CHECK(interactions.Observe(episode));}
    CHECK(interactions.TryDrain(batch)&&batch.count==0&&batch.latest.supportSource==episode.supportSource);
    CHECK(batch.latest.sampledNs==episode.sampledNs&&batch.latest.inputDeadlineNs==episode.inputDeadlineNs);
    // Moving -> Duplicate -> Returning must retain the real new result, even
    // when the duplicate was the immediately preceding callback.
    feed.status=4u<<8;CHECK(interactions.Observe(episode));
    CHECK(interactions.TryDrain(batch)&&batch.count==1&&batch.records[0].changes==FireRouteInteractionChanged);
    feed.status=7u<<8;CHECK(interactions.Observe(episode));
    CHECK(interactions.TryDrain(batch)&&batch.count==0&&batch.latest.interactions.feed.status==(7u<<8));

    // Idle feed gates are transitions, while contact measurements, neutral age
    // and duplicate/new-source provenance cannot fill the bounded event ring.
    FireRouteTelemetry startup;auto idle=Sample();auto& idleFeed=idle.interactions.feed;
    idleFeed.observed=true;idleFeed.exit=1;idleFeed.idleChecked=idleFeed.idlePassed=2047;
    idle.feedCoverDistance=.03f;idle.feedCoverAngle=.1f;idle.feedNeutralAgeNs=100;
    CHECK(startup.Observe(idle));
    for(unsigned i=0;i<1000;++i){
        idleFeed.idlePassed=(2047u&~(1u<<2))|((i%2)<<2);
        idle.feedCoverDistance=.03f+float(i)*.00001f;idle.feedCoverAngle=.1f+float(i)*.00001f;
        ++idle.feedNeutralAgeNs;++idle.feedSource;CHECK(startup.Observe(idle));
    }
    CHECK(startup.TryDrain(batch)&&batch.count==1&&batch.overwritten==0);
    CHECK(batch.latest.feedCoverDistance==idle.feedCoverDistance&&batch.latest.feedNeutralAgeNs==1100);
    idleFeed.idlePassed&=~(1u<<4);CHECK(startup.Observe(idle));
    CHECK(startup.TryDrain(batch)&&batch.count==1&&batch.records[0].changes==FireRouteInteractionChanged);
    std::ostringstream startupJson;WriteFireRouteTelemetry(startupJson,batch);
    CHECK(startupJson.str().find("\"idle_checked\":2047,\"idle_passed\":2031")!=std::string::npos);
    CHECK(startupJson.str().find("\"neutral_age_ns\":1100")!=std::string::npos);
    CHECK(startupJson.str().find("\"feed_idle_bits\":[\"hand_available\",\"released_neutral\",\"new_source_packet\"")!=std::string::npos);
    idleFeed.idleChecked=idleFeed.idlePassed=0;idle.feedCoverDistance.reset();idle.feedCoverAngle.reset();idle.feedNeutralAgeNs=-1;
    CHECK(startup.Observe(idle)&&startup.TryDrain(batch)&&batch.count==1);
    std::ostringstream idleUnknown;WriteFireRouteTelemetry(idleUnknown,batch);
    CHECK(idleUnknown.str().find("\"idle_checked\":0,\"idle_passed\":0,\"cover_distance_m\":null,\"cover_angle_radians\":null,\"neutral_age_ns\":null")!=std::string::npos);

    // The caller's routing/input tuple is untouched by observer bookkeeping;
    // unavailable evaluations are not carried over to a new owner/gather.
    const auto observed=episode;CHECK(interactions.Observe(episode));
    CHECK(episode.serial==observed.serial&&episode.changes==observed.changes&&episode.inputAgeNs==observed.inputAgeNs);
    CHECK(episode.interactions==observed.interactions&&episode.input==observed.input&&episode.inputDeadlineNs==observed.inputDeadlineNs);
    episode.interactions={};++episode.owner.equipGeneration;CHECK(interactions.Observe(episode));
    CHECK(interactions.TryDrain(batch)&&batch.count==1&&!batch.latest.interactions.shell.observed);
    CHECK((batch.records[0].changes&FireRouteOwnerChanged)&&(batch.records[0].changes&FireRouteInteractionChanged));

    // Serialization retains the old fire schema, explicit reason dictionaries,
    // unknown values, source deadlines and cumulative loss evidence.
    std::ostringstream absent;WriteFireRouteTelemetry(absent,batch);
    CHECK(absent.str().find("\"interactions\":{\"support\":null,\"sight\":null,\"shell\":null,\"feed\":null}")!=std::string::npos);
    episode=observed;episode.supportDistance=std::numeric_limits<float>::infinity();
    episode.interactions.shell.cancelReason=~0u;episode.interactions.shell.supplyReason=~0u;
    episode.interactions.feed.reloadPhase=~0u;episode.interactions.feed.operation=~0u;
    episode.interactions.support.reset[0]='"';episode.interactions.support.reset[1]='\\';
    CHECK(interactions.Observe(episode));CHECK(interactions.TryDrain(batch));
    batch.overwritten=19;batch.sampleLockDrops=3;batch.drainLockMisses=2;
    std::ostringstream serialized;WriteFireRouteTelemetry(serialized,batch);const auto json=serialized.str();
    CHECK(json.find("\"schema\":1")!=std::string::npos&&json.find("\"interaction_schema\":1")!=std::string::npos);
    CHECK(json.find("\"cancel_reason\":null,\"supply_reason\":null")!=std::string::npos);
    CHECK(json.find("\"reload_phase\":null,\"operation\":null")!=std::string::npos);
    CHECK(json.find("\"distance_m\":null")!=std::string::npos&&json.find("\"source_deadline_ns\":1000")!=std::string::npos);
    CHECK(json.find("\"reset\":\"\\\"\\\\\"")!=std::string::npos);
    CHECK(json.find("\"overwritten\":19,\"sample_lock_drops\":3,\"drain_lock_misses\":2")!=std::string::npos);
    CHECK(json.find("selection_or_input_unverified")!=std::string::npos&&json.find("recorded_left_evidence")!=std::string::npos);
    episode.interactions.sight.evaluated=false;episode.interactions.sight.reset=false;
    CHECK(interactions.Observe(episode)&&interactions.TryDrain(batch));
    std::ostringstream unevaluated;WriteFireRouteTelemetry(unevaluated,batch);
    CHECK(unevaluated.str().find("\"policy_phase\":null,\"reason\":null,\"packet_observed\":1,\"packet_reason\":1")!=std::string::npos);

    // An interaction-only overflow retains the newest transitions and reports
    // every overwritten event even though native Fire never changed.
    FireRouteTelemetry interactionOverflow;episode=Sample();episode.interactions.shell.observed=true;
    for(std::size_t i=0;i<total;++i){episode.interactions.shell.cancelReason=unsigned(i%2);CHECK(interactionOverflow.Observe(episode));}
    CHECK(interactionOverflow.TryDrain(batch)&&batch.count==FireRouteTelemetryCapacity&&batch.overwritten==19);
    CHECK(batch.records[0].serial==20&&batch.records.back().serial==total);
    return 0;
}
